
#pragma once

#include <stdint.h>

#define NVR_RT_MAGIC            0x54525256u
#define NVR_RT_PROTOCOL_VERSION 2u
#define NVR_RT_FRAME_RING       4u
#define NVR_RT_MAX_IMAGES       16u
#define NVR_RT_MAX_SHADERS      16u
#define NVR_RT_MAX_OPS          2048u
#define NVR_RT_MAX_INSTANCES    32768u
#define NVR_RT_MAX_PASSES       8u
#define NVR_RT_PASS_IMAGES      8u
#define NVR_RT_PASS_CONSTANTS   512u
#define NVR_RT_ARENA_IMAGE      0u
#define NVR_RT_BINDING_RECORDS  10u
#define NVR_RT_BINDING_GEOMETRY 11u
#define NVR_RT_NO_DATA          0xFFFFFFFFu

#define NVR_RT_RECORD_LAND          0x01u
#define NVR_RT_RECORD_WATER         0x02u
#define NVR_RT_RECORD_GLOW          0x04u
#define NVR_RT_RECORD_METAL         0x08u
#define NVR_RT_RECORD_ALBEDO_KNOWN  0x10u

#define NVR_RT_EVENT_FRAME_SUFFIX  L"-frame"
#define NVR_RT_EVENT_STATUS_SUFFIX L"-status"

enum NvrRtState : uint32_t {
	NVR_RT_STATE_STARTING = 0,
	NVR_RT_STATE_READY    = 1,
	NVR_RT_STATE_FAILED   = 2,
	NVR_RT_STATE_EXITED   = 3,
};

enum NvrRtOp : uint32_t {
	NVR_RT_OP_BUILD   = 1,
	NVR_RT_OP_RELEASE = 2,
};

struct NvrRtImageDesc {
	uint64_t	handle;
	uint64_t	allocationSize;
	uint32_t	memoryTypeIndex;
	uint32_t	vkFormat;
	uint32_t	width, height;
	uint32_t	usage;
	uint32_t	flags;
	uint32_t	tiling;
	uint32_t	layout;
	uint32_t	handleType;
	uint32_t	dedicated;
};

struct NvrRtGeometryOp {
	uint32_t	op;
	uint32_t	id;
	uint32_t	vertexCount;
	uint32_t	triangleCount;
	uint32_t	arenaTexel;
	uint32_t	flags;
	uint32_t	pad[2];
};

struct NvrRtInstance {
	float		transform[3][4];
	uint32_t	geometryId;
	uint32_t	customIndex;
	uint32_t	mask;
	uint32_t	flags;
};

struct NvrRtInstanceRecord {
	float		albedo[4];
	uint32_t	flags;
	uint32_t	geometryId;
	uint32_t	vertexOffset;
	uint32_t	indexOffset;
};

struct NvrRtPass {
	uint32_t	shader;
	uint32_t	groupsX, groupsY, groupsZ;
	uint32_t	imageCount;
	uint32_t	images[NVR_RT_PASS_IMAGES];
	uint32_t	writeMask;
	uint32_t	constantsSize;
	uint32_t	pad;
	uint8_t		constants[NVR_RT_PASS_CONSTANTS];
};

struct NvrRtFrame {
	uint64_t	frame;
	int64_t		writtenQpc;
	uint32_t	opCount;
	uint32_t	instanceCount;
	uint32_t	passCount;
	uint32_t	arenaTexels;
	NvrRtGeometryOp	ops[NVR_RT_MAX_OPS];
	NvrRtInstance	instances[NVR_RT_MAX_INSTANCES];
	NvrRtInstanceRecord records[NVR_RT_MAX_INSTANCES];
	NvrRtPass		passes[NVR_RT_MAX_PASSES];
};

struct NvrRtShared {
	uint32_t	magic;
	uint32_t	version;
	uint32_t	size;
	uint32_t	parentPid;
	uint32_t	vendorId;
	uint32_t	deviceLuidValid;
	uint8_t		deviceLuid[8];
	uint8_t		deviceUuid[16];
	uint8_t		driverUuid[16];
	uint32_t	imageCount;
	uint32_t	pad0;
	NvrRtImageDesc images[NVR_RT_MAX_IMAGES];
	uint64_t	semInputsReady;
	uint64_t	semOutputReady;
	uint32_t	setupDone;
	uint32_t	pad1;

	volatile uint32_t	shaderCount;
	uint32_t			pad2;
	char				shaderPaths[NVR_RT_MAX_SHADERS][260];

	volatile int64_t	latestFrame;
	NvrRtFrame			frames[NVR_RT_FRAME_RING];

	volatile uint32_t	state;
	volatile uint32_t	messageSerial;
	volatile int64_t	consumedFrame;
	volatile int64_t	submittedFrame;
	volatile uint64_t	framesSubmitted;
	volatile uint32_t	gpuTimeUsLast;
	volatile uint32_t	gpuTimeUsAvg;
	volatile uint32_t	buildUsAvg;
	volatile uint32_t	sceneUsAvg;
	volatile uint32_t	traceUsAvg;
	volatile uint32_t	responseUsAvg;
	volatile uint32_t	responseUsMax;
	volatile uint32_t	geometries;
	volatile uint64_t	triangles;
	volatile uint64_t	geometryBytes;
	volatile uint32_t	lastInstances;
	volatile uint32_t	failedOps;
	volatile uint32_t	shaderFailures;
	volatile uint32_t	pad3;
	char				message[512];

	volatile uint32_t	quit;
	uint32_t			pad4;
};

#ifdef __cplusplus
static_assert(sizeof(NvrRtImageDesc) == 56, "NvrRtImageDesc layout");
static_assert(sizeof(NvrRtGeometryOp) == 32, "NvrRtGeometryOp layout");
static_assert(sizeof(NvrRtInstance) == 64, "NvrRtInstance layout");
static_assert(sizeof(NvrRtPass) == 576, "NvrRtPass layout");
static_assert(sizeof(NvrRtInstanceRecord) == 32, "NvrRtInstanceRecord layout");
static_assert(sizeof(NvrRtFrame) % 8 == 0, "NvrRtFrame layout");
static_assert(sizeof(NvrRtShared) % 8 == 0, "NvrRtShared layout");
#endif
