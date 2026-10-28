// NvrRtProtocol.h -- the shared-memory block between New Vegas Reloaded (32-bit, inside the game) and NvrRtHelper.exe
// (64-bit), the process that owns NVR's ray tracing scene. MIT licence (tools/nvr-rt-helper/LICENSE), so both sides may
// include it. docs/next/FORK-RT-CORE.md has the whole design and the rules for writing a trace pass.
//
// Why a helper: Fallout New Vegas, NVR and the NVR DXVK d3d9.dll are 32-bit, and Windows' 32-bit Vulkan drivers offer no
// ray tracing (measured 2026-10-28: AMD 25.10 and NVIDIA 582 list no VK_KHR_acceleration_structure / ray_query to a 32-bit
// process; their 64-bit drivers do). The helper makes its own Vulkan device on the same GPU, imports images and two
// timeline semaphores the fork exports (patch 0026, ID3D9NvrExtShare), and per frame:
//
//   NVR (render thread)                                      helper (its own process, same GPU)
//   StageGeometry ... FlushStagedGeometry (fork, GPU)
//   CopyToShared the passes' inputs (fork, GPU)
//   frames[N % RING] = ops, instances, passes; latestFrame = N, SetEvent  ----->  wakes up, copies frames[N % RING] out,
//   SignalShared(inputsReady, N)  (fork: flush)                              sets consumedFrame = N, records and submits:
//   WaitShared(outputReady, N)    (GPU wait only)                            GPU waits inputsReady >= N; arena -> its own
//   CopyFromShared the passes' outputs (fork, GPU)                           buffer; new geometry built; the scene (TLAS)
//                                                                            rebuilt from the instances; the passes run;
//                                                                            signals outputReady = N
// Nobody waits on the CPU per frame. NVR only waits (briefly, with a timeout) before it would overwrite a ring slot the
// helper has not read yet (consumedFrame), since geometry operations must never be lost. Fixed-size fields only (the two
// sides differ in pointer size); 8-byte alignment kept by hand (static_asserts below).

#pragma once

#include <stdint.h>

#define NVR_RT_MAGIC            0x54525256u   // 'VRRT'
#define NVR_RT_PROTOCOL_VERSION 1u
#define NVR_RT_FRAME_RING       4u            // frame records; NVR waits for consumedFrame before reusing one
#define NVR_RT_MAX_IMAGES       16u           // shared images, registered at setup (index 0 is the geometry arena)
#define NVR_RT_MAX_SHADERS      16u           // trace shaders (.spv paths), registered any time
#define NVR_RT_MAX_OPS          2048u         // geometry operations per frame
#define NVR_RT_MAX_INSTANCES    32768u        // scene instances per frame
#define NVR_RT_MAX_PASSES       8u            // trace passes per frame
#define NVR_RT_PASS_IMAGES      8u            // images bound to one pass (bindings 2..9)
#define NVR_RT_PASS_CONSTANTS   512u          // bytes of a pass's uniform block (binding 1)
#define NVR_RT_ARENA_IMAGE      0u

// <base> is passed as "--shm <base>"; events <base>-frame (auto-reset, NVR -> helper) and <base>-status (helper -> NVR).
#define NVR_RT_EVENT_FRAME_SUFFIX  L"-frame"
#define NVR_RT_EVENT_STATUS_SUFFIX L"-status"

enum NvrRtState : uint32_t {
	NVR_RT_STATE_STARTING = 0,
	NVR_RT_STATE_READY    = 1,       // images and semaphores imported, ray tracing available: frames are taken
	NVR_RT_STATE_FAILED   = 2,       // see message (no RT on this GPU, import failed, ...); the helper exits
	NVR_RT_STATE_EXITED   = 3,
};

enum NvrRtOp : uint32_t {
	NVR_RT_OP_BUILD   = 1,           // geometry `id` from the arena: 3 x vertexCount floats at arenaTexel, then 3 x triangleCount uints
	NVR_RT_OP_RELEASE = 2,           // geometry `id` is gone (the helper frees it once no frame in flight uses it)
};

// What the fork's ID3D9NvrExtSharedImage::GetInfo reported, plus the NT handle duplicated into the helper's process.
// (The same layout as NvrDlssImageDesc.)
struct NvrRtImageDesc {
	uint64_t	handle;              // HANDLE value valid in the helper's process (the helper closes it after importing)
	uint64_t	allocationSize;
	uint32_t	memoryTypeIndex;
	uint32_t	vkFormat;
	uint32_t	width, height;
	uint32_t	usage;
	uint32_t	flags;
	uint32_t	tiling;
	uint32_t	layout;              // GENERAL between uses
	uint32_t	handleType;
	uint32_t	dedicated;
};

struct NvrRtGeometryOp {             // 32 bytes
	uint32_t	op;                  // NvrRtOp
	uint32_t	id;                  // NVR's geometry id, 1 .. 2^24 - 1 (0 = none)
	uint32_t	vertexCount;
	uint32_t	triangleCount;
	uint32_t	arenaTexel;          // first 32-bit texel in the arena (row-major)
	uint32_t	flags;               // reserved
	uint32_t	pad[2];
};

struct NvrRtInstance {               // 64 bytes
	float		transform[3][4];     // object -> scene (camera-relative world space): row-major 3x4, as VkTransformMatrixKHR
	uint32_t	geometryId;          // a built geometry (0 or unknown: skipped)
	uint32_t	customIndex;         // low 24 bits: rayQueryGetIntersectionInstanceCustomIndexEXT
	uint32_t	mask;                // low 8 bits: ANDed with the ray's cull mask
	uint32_t	flags;               // VkGeometryInstanceFlagBitsKHR (1 = cull disable, 2 = flip facing, 4 = force opaque)
};

struct NvrRtPass {                   // 576 bytes
	uint32_t	shader;              // index into shaderPaths
	uint32_t	groupsX, groupsY, groupsZ;
	uint32_t	imageCount;          // images[0 .. imageCount-1] bound at bindings 2 .. (storage images, GENERAL)
	uint32_t	images[NVR_RT_PASS_IMAGES];   // indices into images[] of the shared block
	uint32_t	writeMask;           // bit i: the pass writes images[i] (a barrier after the pass for those)
	uint32_t	constantsSize;
	uint32_t	pad;
	uint8_t		constants[NVR_RT_PASS_CONSTANTS];
};

struct NvrRtFrame {
	uint64_t	frame;               // N
	int64_t		writtenQpc;          // QueryPerformanceCounter when NVR wrote this (the same clock in both processes)
	uint32_t	opCount;
	uint32_t	instanceCount;
	uint32_t	passCount;
	uint32_t	arenaTexels;         // texels of the arena this frame's builds read (0 = none)
	NvrRtGeometryOp	ops[NVR_RT_MAX_OPS];
	NvrRtInstance	instances[NVR_RT_MAX_INSTANCES];
	NvrRtPass		passes[NVR_RT_MAX_PASSES];
};

struct NvrRtShared {
	// ---- written by NVR before setupDone, read-only afterwards
	uint32_t	magic;
	uint32_t	version;
	uint32_t	size;                // sizeof(NvrRtShared)
	uint32_t	parentPid;
	uint32_t	vendorId;            // of DXVK's Vulkan device
	uint32_t	deviceLuidValid;
	uint8_t		deviceLuid[8];
	uint8_t		deviceUuid[16];
	uint8_t		driverUuid[16];
	uint32_t	imageCount;
	uint32_t	pad0;
	NvrRtImageDesc images[NVR_RT_MAX_IMAGES];
	uint64_t	semInputsReady;      // HANDLE values valid in the helper's process (timeline semaphores, OPAQUE_WIN32)
	uint64_t	semOutputReady;
	uint32_t	setupDone;           // 1 once everything above is filled in
	uint32_t	pad1;

	// ---- written by NVR any time (shaderCount last, with a release barrier); paths are full, ANSI
	volatile uint32_t	shaderCount;
	uint32_t			pad2;
	char				shaderPaths[NVR_RT_MAX_SHADERS][260];

	// ---- per frame, written by NVR (latestFrame last, with a release barrier)
	volatile int64_t	latestFrame;
	NvrRtFrame			frames[NVR_RT_FRAME_RING];

	// ---- written by the helper
	volatile uint32_t	state;               // NvrRtState
	volatile uint32_t	messageSerial;       // incremented with every new message
	volatile int64_t	consumedFrame;       // the last frame record the helper has copied out (NVR may reuse slots up to it)
	volatile int64_t	submittedFrame;      // the last frame the helper submitted
	volatile uint64_t	framesSubmitted;
	volatile uint32_t	gpuTimeUsLast;       // the helper's GPU work of the last measured frame, microseconds
	volatile uint32_t	gpuTimeUsAvg;        // average of the last 64
	volatile uint32_t	buildUsAvg;          // of which: copying the arena + new geometry builds (frames that had any)
	volatile uint32_t	sceneUsAvg;          // of which: the scene (TLAS) rebuild
	volatile uint32_t	traceUsAvg;          // of which: the passes
	volatile uint32_t	responseUsAvg;       // NVR's write -> the helper's submit, CPU, average of the last 64
	volatile uint32_t	responseUsMax;
	volatile uint32_t	geometries;          // alive in the helper
	volatile uint64_t	triangles;           // in them
	volatile uint64_t	geometryBytes;       // their structures
	volatile uint32_t	lastInstances;       // in the last scene build
	volatile uint32_t	failedOps;           // build ops refused (bad sizes, no memory) since start
	volatile uint32_t	shaderFailures;      // bitmask: shaders that did not load / compile
	volatile uint32_t	pad3;
	char				message[512];        // the last status line, NUL-terminated (the helper's log has them all)

	// ---- written by NVR
	volatile uint32_t	quit;                // 1: exit
	uint32_t			pad4;
};

#ifdef __cplusplus
static_assert(sizeof(NvrRtImageDesc) == 56, "NvrRtImageDesc layout");
static_assert(sizeof(NvrRtGeometryOp) == 32, "NvrRtGeometryOp layout");
static_assert(sizeof(NvrRtInstance) == 64, "NvrRtInstance layout");
static_assert(sizeof(NvrRtPass) == 576, "NvrRtPass layout");
static_assert(sizeof(NvrRtFrame) % 8 == 0, "NvrRtFrame layout");
static_assert(sizeof(NvrRtShared) % 8 == 0, "NvrRtShared layout");
#endif
