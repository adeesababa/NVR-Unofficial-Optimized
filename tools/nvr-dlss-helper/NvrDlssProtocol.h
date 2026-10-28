
#pragma once

#include <stdint.h>

#define NVR_DLSS_MAGIC            0x53534C44u
#define NVR_DLSS_PROTOCOL_VERSION 1u
#define NVR_DLSS_FRAME_RING       16u

#define NVR_DLSS_EVENT_FRAME_SUFFIX  L"-frame"
#define NVR_DLSS_EVENT_STATUS_SUFFIX L"-status"

enum NvrDlssMode : uint32_t {
	NVR_DLSS_MODE_DLSS        = 0,
	NVR_DLSS_MODE_PASSTHROUGH = 1,
};

enum NvrDlssState : uint32_t {
	NVR_DLSS_STATE_STARTING = 0,
	NVR_DLSS_STATE_READY    = 1,
	NVR_DLSS_STATE_FAILED   = 2,
	NVR_DLSS_STATE_EXITED   = 3,
};

enum NvrDlssImage : uint32_t {
	NVR_DLSS_IMAGE_COLOR  = 0,
	NVR_DLSS_IMAGE_DEPTH  = 1,
	NVR_DLSS_IMAGE_MOTION = 2,
	NVR_DLSS_IMAGE_OUTPUT = 3,
	NVR_DLSS_IMAGE_COUNT  = 4,
};

struct NvrDlssImageDesc {
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

struct NvrDlssFrame {
	uint64_t	frame;
	int64_t		writtenQpc;
	uint32_t	renderWidth, renderHeight;
	uint32_t	reset;
	uint32_t	hdr;
	float		jitterX, jitterY;
	float		mvScaleX, mvScaleY;
	float		preExposure;
	float		frameTimeMs;
	uint32_t	quality;
	uint32_t	flags;
};

struct NvrDlssShared {
	uint32_t	magic;
	uint32_t	version;
	uint32_t	size;
	uint32_t	mode;
	uint32_t	parentPid;
	uint32_t	vendorId;
	uint8_t		deviceLuid[8];
	uint32_t	deviceLuidValid;
	uint32_t	pad0;
	uint8_t		deviceUuid[16];
	uint8_t		driverUuid[16];
	NvrDlssImageDesc images[NVR_DLSS_IMAGE_COUNT];
	uint64_t	semInputsReady;
	uint64_t	semOutputReady;
	uint32_t	displayWidth, displayHeight;
	uint32_t	inputWidth, inputHeight;
	uint32_t	setupDone;
	uint32_t	pad1;

	volatile int64_t	latestFrame;
	NvrDlssFrame		frames[NVR_DLSS_FRAME_RING];

	volatile uint32_t	state;
	volatile uint32_t	ngxAvailable;
	volatile int32_t	ngxResult;
	volatile uint32_t	messageSerial;
	volatile int64_t	submittedFrame;
	volatile uint64_t	framesSubmitted;
	volatile uint32_t	gpuTimeUsLast;
	volatile uint32_t	gpuTimeUsAvg;
	volatile uint32_t	responseUsAvg;
	volatile uint32_t	responseUsMax;
	volatile uint32_t	paramMismatches;
	volatile uint32_t	dlssEvaluated;
	char				message[512];

	volatile uint32_t	quit;
	uint32_t			pad2;
};

#ifdef __cplusplus
static_assert(sizeof(NvrDlssImageDesc) == 56, "NvrDlssImageDesc layout");
static_assert(sizeof(NvrDlssFrame) == 64, "NvrDlssFrame layout");
static_assert(sizeof(NvrDlssShared) == 1944, "NvrDlssShared layout (the same in the 32-bit and the 64-bit build)");
#endif
