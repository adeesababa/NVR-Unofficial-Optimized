// NvrDlssProtocol.h -- the shared-memory block between New Vegas Reloaded (32-bit, inside the game) and NvrDlssHelper.exe
// (64-bit). MIT licence (tools/nvr-dlss-helper/LICENSE), so both sides may include it. No NVIDIA code or types in here.
//
// How a frame goes (docs/next/FORK2-DLSS.md has the long version):
//   NVR (render thread)                                      helper (its own process, same GPU, own Vulkan device)
//   CopyToShared colour/depth/motion (fork, GPU)
//   frames[N % NVR_DLSS_FRAME_RING] = this frame's values
//   latestFrame = N, SetEvent(frame event)          ----->   wakes up, reads frames[N % ring]
//   SignalShared(inputsReady, N)  (fork: flush)               records DLSS (or the pass-through copy) and submits it:
//   WaitShared(outputReady, N)    (GPU wait only)              GPU waits inputsReady >= N, signals outputReady = N
//   CopyFromShared output -> NVR's texture (GPU)
// Nobody waits on the CPU per frame. Fixed-size fields only (the two sides differ in pointer size); 8-byte alignment kept
// by hand so both compilers lay the block out the same way (static_asserts below).

#pragma once

#include <stdint.h>

#define NVR_DLSS_MAGIC            0x53534C44u   // 'DLSS'
#define NVR_DLSS_PROTOCOL_VERSION 1u
#define NVR_DLSS_FRAME_RING       16u           // per-frame values kept; NVR never runs more than a few frames ahead

// File mapping and event names: <base> is passed to the helper as "--shm <base>"; the events are <base>-frame (auto-reset,
// NVR -> helper: a new frame) and <base>-status (auto-reset, helper -> NVR: state or message changed).
#define NVR_DLSS_EVENT_FRAME_SUFFIX  L"-frame"
#define NVR_DLSS_EVENT_STATUS_SUFFIX L"-status"

enum NvrDlssMode : uint32_t {
	NVR_DLSS_MODE_DLSS        = 0,   // NVIDIA DLSS (needs an RTX GPU and nvngx_dlss.dll next to the helper)
	NVR_DLSS_MODE_PASSTHROUGH = 1,   // test: the colour input stretched to the output plus a frame marker (any NVIDIA GPU)
};

enum NvrDlssState : uint32_t {
	NVR_DLSS_STATE_STARTING = 0,
	NVR_DLSS_STATE_READY    = 1,     // images and semaphores imported; frames are taken
	NVR_DLSS_STATE_FAILED   = 2,     // see message; the helper exits
	NVR_DLSS_STATE_EXITED   = 3,
};

enum NvrDlssImage : uint32_t {
	NVR_DLSS_IMAGE_COLOR  = 0,       // render size in the top-left of an input-size image
	NVR_DLSS_IMAGE_DEPTH  = 1,       // r: device depth 0 near .. 1 far (not inverted)
	NVR_DLSS_IMAGE_MOTION = 2,       // rg: previous uv - current uv, render size, unjittered
	NVR_DLSS_IMAGE_OUTPUT = 3,       // display size, written whole by the helper every frame
	NVR_DLSS_IMAGE_COUNT  = 4,
};

// What the fork's ID3D9NvrExtSharedImage::GetInfo reported, plus the NT handle duplicated into the helper's process.
struct NvrDlssImageDesc {
	uint64_t	handle;              // HANDLE value valid in the helper's process (the helper closes it after importing)
	uint64_t	allocationSize;
	uint32_t	memoryTypeIndex;
	uint32_t	vkFormat;
	uint32_t	width, height;
	uint32_t	usage;
	uint32_t	flags;
	uint32_t	tiling;
	uint32_t	layout;              // the layout the image is in between uses (GENERAL)
	uint32_t	handleType;
	uint32_t	dedicated;
};

struct NvrDlssFrame {
	uint64_t	frame;               // N: the helper waits for inputsReady >= N and signals outputReady = N
	int64_t		writtenQpc;          // QueryPerformanceCounter when NVR wrote this (the same clock in both processes)
	uint32_t	renderWidth, renderHeight;
	uint32_t	reset;               // no usable history
	uint32_t	hdr;                 // colour is linear HDR (before tonemapping)
	float		jitterX, jitterY;    // sub-pixel offset in render pixels, DLSS's convention (NVR converts)
	float		mvScaleX, mvScaleY;  // motion texel * scale = render pixels
	float		preExposure;         // 1
	float		frameTimeMs;
	uint32_t	quality;             // NVSDK_NGX_PerfQuality_Value number (NVR picks it from render / display size)
	uint32_t	flags;               // reserved
};

struct NvrDlssShared {
	// ---- written by NVR before it starts the helper, read-only afterwards
	uint32_t	magic;
	uint32_t	version;
	uint32_t	size;                // sizeof(NvrDlssShared)
	uint32_t	mode;                // NvrDlssMode
	uint32_t	parentPid;
	uint32_t	vendorId;            // of DXVK's Vulkan device
	uint8_t		deviceLuid[8];
	uint32_t	deviceLuidValid;
	uint32_t	pad0;
	uint8_t		deviceUuid[16];
	uint8_t		driverUuid[16];
	NvrDlssImageDesc images[NVR_DLSS_IMAGE_COUNT];
	uint64_t	semInputsReady;      // HANDLE values valid in the helper's process (timeline semaphores, OPAQUE_WIN32)
	uint64_t	semOutputReady;
	uint32_t	displayWidth, displayHeight;
	uint32_t	inputWidth, inputHeight;
	uint32_t	setupDone;           // 1 once everything above is filled in
	uint32_t	pad1;

	// ---- per frame, written by NVR (latestFrame last, with a release barrier)
	volatile int64_t	latestFrame;
	NvrDlssFrame		frames[NVR_DLSS_FRAME_RING];

	// ---- written by the helper
	volatile uint32_t	state;               // NvrDlssState
	volatile uint32_t	ngxAvailable;        // 1: DLSS can run on this GPU (0 also when built without NVIDIA's SDK)
	volatile int32_t	ngxResult;           // the last NGX result code (0 = none)
	volatile uint32_t	messageSerial;       // incremented with every new message
	volatile int64_t	submittedFrame;      // the last frame the helper submitted
	volatile uint64_t	framesSubmitted;
	volatile uint32_t	gpuTimeUsLast;       // the helper's own GPU work per frame (timestamps), microseconds
	volatile uint32_t	gpuTimeUsAvg;        // average of the last 64
	volatile uint32_t	responseUsAvg;       // NVR's write -> the helper's submit, CPU, average of the last 64
	volatile uint32_t	responseUsMax;       // worst since the last report
	volatile uint32_t	paramMismatches;     // frames whose values were overwritten before the helper read them
	volatile uint32_t	dlssEvaluated;       // frames DLSS itself ran (0 in pass-through)
	char				message[512];        // the last status line, NUL-terminated (the helper's log has them all)

	// ---- written by NVR
	volatile uint32_t	quit;                // 1: exit
	uint32_t			pad2;
};

#ifdef __cplusplus
static_assert(sizeof(NvrDlssImageDesc) == 56, "NvrDlssImageDesc layout");
static_assert(sizeof(NvrDlssFrame) == 64, "NvrDlssFrame layout");
static_assert(sizeof(NvrDlssShared) == 1944, "NvrDlssShared layout (the same in the 32-bit and the 64-bit build)");
#endif
