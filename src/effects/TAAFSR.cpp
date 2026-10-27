// UNOFFICIAL 2026-10-26 (NVR DXVK): AMD FidelityFX Super Resolution 2 (FSR 2.2.1) as TAA's resolve, at native resolution.
//
// CREDIT: FSR 2 is AMD's (FidelityFX Super Resolution 2.2.1, GPUOpen, https://github.com/GPUOpen-Effects/FidelityFX-FSR2),
// MIT licence: src\hlsl\NewVegas\Compute\FSR2\LICENSE-FidelityFX-FSR2.txt. The shaders are AMD's GLSL passes as released, built
// to SPIR-V with NVR's callbacks header (FSR2\ffx_fsr2_callbacks_nvr.h says what it changes and why); this file does what AMD's
// ffx_fsr2.cpp does on the CPU side (constants, ping-pong resources, clears, dispatch order), written for NVR's DXVK extension.
//
// Needs NVR DXVK (compute through the extension). Without it, or after any failure (logged once), TAA's own resolve runs.
// Settings: [Shaders.TAA.FSR] Enabled / Sharpness / WeaponReactive (TAA must be on; TAA's Jitter and MipBias are used).
//
// Per frame, all at the render (= display) resolution, after TAA's usual inputs are ready (post-tonemap frame in
// TESR_SourceBuffer, the world jittered by BeginJitter):
//   FSRPrep        NVR: camera motion vectors from depth (TAA's reprojection) + FSR-style device depth + reactive (weapon)
//   FSRLumaMip     NVR: average log luma per 32x32 block (what AMD's luminance pyramid stores in the mip FSR reads)
//   FSR2Reconstruct, FSR2DepthClip, FSR2Lock, FSR2Accumulate[Sharpen], FSR2RCAS   AMD's passes
//   FSRComposite   NVR: FSR's output into the game target, the first-person weapon put back as rendered
// What FSR does not get (yet): motion of skinned characters and other moving objects (camera motion only; FSR's depth clip and
// luma locks handle what they can), a reactive mask for particles/transparencies, and real upscaling (render scale 1 only).

#include <algorithm>

#include "TAA.h"
#include "../core/DxvkCompute.h"
#include "../core/GpuProfiler.h"

namespace {
	// cbFSR2 in ffx_fsr2_callbacks_nvr.h (std140; the same layout as AMD's FfxFsr2Constants).
	struct FSRConstants {
		INT32	renderSize[2];
		INT32	maxRenderSize[2];
		INT32	displaySize[2];
		INT32	inputColorResourceDimensions[2];
		INT32	lumaMipDimensions[2];
		INT32	lumaMipLevelToUse;
		INT32	frameIndex;
		float	deviceToViewDepth[4];
		float	jitterOffset[2];
		float	motionVectorScale[2];
		float	downscaleFactor[2];
		float	motionVectorJitterCancellation[2];
		float	preExposure;
		float	previousFramePreExposure;
		float	tanHalfFOV;
		float	jitterPhaseCount;
		float	deltaTime;
		float	dynamicResChangeFactor;
		float	viewSpaceToMetersFactor;
		float	pad;
	};
	static_assert(sizeof(FSRConstants) == 128, "cbFSR2 layout");

	// Constants of FSRPrep.comp (std140).
	struct FSRPrepConstants {
		D3DXMATRIX	projection, view, prevView;
		D3DXVECTOR4	cameraData, prevProjection, cameraDelta, size;
	};

	// The push block of AMD's passes in NVR's build (ffx_fsr2_callbacks_nvr.h NvrFsrPush_t).
	struct FSRPush { UInt32 pointIndex, linearIndex, pad0, pad1; UInt32 rcas[4]; };

	const NVR_EXT_BINDING_DESC* const NoBindings = nullptr;

	// Binding tables: (set, binding) as the SPIR-V declares them (checked with spirv-dis), slot = binding for images,
	// buffer slot 0 = the constants, buffer slot 1 = the reconstructed depth buffer; samplers POINT (slot 0, push 0), LINEAR (1, 4).
#define IMG(b)      { 1, b, NVR_EXT_BIND_SAMPLED_IMAGE, b, NVR_EXT_VIEW_2D, FALSE, 0 }
#define UAV(b)      { 1, b, NVR_EXT_BIND_STORAGE_IMAGE, b, NVR_EXT_VIEW_2D, TRUE, 0 }
#define CB(s, b)    { s, b, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 }
#define DEPTHBUF(b, w) { 1, b, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, w, 0 }
#define POINTS      { 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 0 }
#define LINEARS     { 0, 0, NVR_EXT_BIND_SAMPLER, 1, NVR_EXT_VIEW_2D, FALSE, 4 }
	const NVR_EXT_BINDING_DESC PrepBindings[] = { IMG(0), IMG(1), UAV(2), UAV(3), CB(2, 0), POINTS };
	const NVR_EXT_BINDING_DESC LumaBindings[] = { IMG(0), UAV(1), POINTS };
	const NVR_EXT_BINDING_DESC ReconstructBindings[] = { IMG(0), IMG(1), IMG(2), DEPTHBUF(5, TRUE), UAV(6), UAV(7), UAV(11), CB(1, 12) };
	const NVR_EXT_BINDING_DESC DepthClipBindings[] = { DEPTHBUF(0, FALSE), IMG(1), IMG(2), IMG(6), IMG(7), IMG(8), IMG(9), UAV(12), UAV(13), CB(1, 14), POINTS, LINEARS };
	const NVR_EXT_BINDING_DESC LockBindings[] = { IMG(0), UAV(1), DEPTHBUF(2, TRUE), CB(1, 3) };
	const NVR_EXT_BINDING_DESC AccumulateBindings[] = { IMG(1), IMG(2), IMG(3), IMG(4), IMG(6), IMG(10), IMG(12), UAV(13), UAV(14), UAV(15), UAV(16), UAV(17), CB(1, 18), POINTS, LINEARS };
	const NVR_EXT_BINDING_DESC AccumulateSharpenBindings[] = { IMG(1), IMG(2), IMG(3), IMG(4), IMG(6), IMG(10), IMG(12), UAV(13), UAV(14), UAV(16), UAV(17), CB(1, 18), POINTS, LINEARS };
	const NVR_EXT_BINDING_DESC RCASBindings[] = { IMG(1), UAV(2), CB(1, 3) };
	const NVR_EXT_BINDING_DESC CompositeBindings[] = { IMG(0), IMG(1), IMG(2), IMG(3), IMG(4), POINTS };
#undef IMG
#undef UAV
#undef CB
#undef DEPTHBUF
#undef POINTS
#undef LINEARS

	struct PipelineInfo { const char* name; const NVR_EXT_BINDING_DESC* bindings; UINT count; UINT push; bool samplers; };
	const PipelineInfo Pipelines[] = {
		{ "FSRPrep",               PrepBindings,              _countof(PrepBindings),              4,  true  },
		{ "FSRLumaMip",            LumaBindings,              _countof(LumaBindings),              16, true  },
		{ "FSR2Reconstruct",       ReconstructBindings,       _countof(ReconstructBindings),       0,  false },
		{ "FSR2DepthClip",         DepthClipBindings,         _countof(DepthClipBindings),         32, true  },
		{ "FSR2Lock",              LockBindings,              _countof(LockBindings),              0,  false },
		{ "FSR2Accumulate",        AccumulateBindings,        _countof(AccumulateBindings),        32, true  },
		{ "FSR2AccumulateSharpen", AccumulateSharpenBindings, _countof(AccumulateSharpenBindings), 32, true  },
		{ "FSR2RCAS",              RCASBindings,              _countof(RCASBindings),              32, false },
	};

	const D3DXVECTOR4* Registered(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
	IDirect3DBaseTexture9* SlotTexture(const char* name) {
		std::string slotName = name;
		IDirect3DBaseTexture9** slot = TheTextureManager->GetTextureSlotByName(slotName);
		return slot ? *slot : nullptr;
	}

	NVR_EXT_RESOURCE_BINDING Image(UINT slot, IUnknown* resource, bool storage = false) {
		NVR_EXT_RESOURCE_BINDING b = {};
		b.Slot = slot; b.Type = storage ? NVR_EXT_BIND_STORAGE_IMAGE : NVR_EXT_BIND_SAMPLED_IMAGE; b.pResource = resource;
		return b;
	}
	NVR_EXT_RESOURCE_BINDING Buffer(UINT slot, NVR_EXT_BIND_TYPE type, ID3D9NvrExtBuffer* buffer) {
		NVR_EXT_RESOURCE_BINDING b = {};
		b.Slot = slot; b.Type = type; b.pResource = buffer;
		return b;
	}
	NVR_EXT_RESOURCE_BINDING Sampler(UINT slot, ID3D9NvrExtSampler* sampler) {
		NVR_EXT_RESOURCE_BINDING b = {};
		b.Slot = slot; b.Type = NVR_EXT_BIND_SAMPLER; b.pResource = sampler;
		return b;
	}

	// AMD's ffx_fsr1.h FsrRcasCon: the sharpness in stops (0 = maximum), as a float and as two packed halves.
	UInt32 HalfBits(float value) {
		UInt32 f = *(UInt32*)&value;
		UInt32 sign = (f >> 16) & 0x8000u;
		int exponent = (int)((f >> 23) & 0xFF) - 127 + 15;
		UInt32 mantissa = f & 0x7FFFFFu;
		if (exponent <= 0) return sign;
		if (exponent >= 31) return sign | 0x7C00u;
		return sign | ((UInt32)exponent << 10) | (mantissa >> 13);
	}
}

void TAAEffect::FSRReleaseResources() {
	FSRTexture* textures[] = { &fsrMotion, &fsrDepth, &fsrLumaMip, &fsrDilatedMotion[0], &fsrDilatedMotion[1], &fsrDilatedDepth, &fsrLockLuma,
		&fsrNewLocks, &fsrPrepared, &fsrLumaHistory[0], &fsrLumaHistory[1], &fsrReactiveMasks, &fsrUpscaled[0], &fsrUpscaled[1],
		&fsrLockStatus[0], &fsrLockStatus[1], &fsrOutput };
	for (FSRTexture* t : textures) {
		if (t->Surface) { t->Surface->Release(); t->Surface = nullptr; }
		if (t->Texture) { t->Texture->Release(); t->Texture = nullptr; }
	}
	DxvkCompute::Drop(fsrReconstructedDepth);
	fsrWidth = fsrHeight = 0;
}

// The textures FSR keeps between frames (AMD's internal resources) and the per-frame inputs NVR builds, made on first use and
// again when the resolution changes. Formats: AMD's, or a float format of at least the same precision where D3D9 has no render
// target of AMD's (see ffx_fsr2_callbacks_nvr.h). All are render-target textures so DXVK can add storage use to them.
bool TAAEffect::FSREnsureResources() {
	const int width = TheRenderManager->width, height = TheRenderManager->height;
	if (fsrWidth == width && fsrHeight == height && fsrOutput.Surface) return true;
	FSRReleaseResources();

	struct Make { FSRTexture* texture; const char* name; int w, h; D3DFORMAT format; };
	const int lumaW = (std::max)(1, width / 32), lumaH = (std::max)(1, height / 32);
	const Make list[] = {
		{ &fsrMotion,           "TESR_FSRMotion",          width, height, D3DFMT_G16R16F },
		{ &fsrDepth,            "TESR_FSRDepth",           width, height, D3DFMT_G32R32F },
		{ &fsrLumaMip,          "TESR_FSRLumaMip",         lumaW, lumaH,  D3DFMT_R16F },
		{ &fsrDilatedMotion[0], "TESR_FSRDilatedMotion0",  width, height, D3DFMT_G16R16F },
		{ &fsrDilatedMotion[1], "TESR_FSRDilatedMotion1",  width, height, D3DFMT_G16R16F },
		{ &fsrDilatedDepth,     "TESR_FSRDilatedDepth",    width, height, D3DFMT_R32F },
		{ &fsrLockLuma,         "TESR_FSRLockLuma",        width, height, D3DFMT_R16F },
		{ &fsrNewLocks,         "TESR_FSRNewLocks",        width, height, D3DFMT_R16F },
		{ &fsrPrepared,         "TESR_FSRPrepared",        width, height, D3DFMT_A16B16G16R16F },
		{ &fsrLumaHistory[0],   "TESR_FSRLumaHistory0",    width, height, D3DFMT_A16B16G16R16F },
		{ &fsrLumaHistory[1],   "TESR_FSRLumaHistory1",    width, height, D3DFMT_A16B16G16R16F },
		{ &fsrReactiveMasks,    "TESR_FSRReactiveMasks",   width, height, D3DFMT_G16R16F },
		{ &fsrUpscaled[0],      "TESR_FSRUpscaled0",       width, height, D3DFMT_A16B16G16R16F },
		{ &fsrUpscaled[1],      "TESR_FSRUpscaled1",       width, height, D3DFMT_A16B16G16R16F },
		{ &fsrLockStatus[0],    "TESR_FSRLockStatus0",     width, height, D3DFMT_G16R16F },
		{ &fsrLockStatus[1],    "TESR_FSRLockStatus1",     width, height, D3DFMT_G16R16F },
		{ &fsrOutput,           "TESR_FSROutput",          width, height, D3DFMT_A16B16G16R16F },
	};
	for (const Make& m : list) {
		TheTextureManager->InitTexture(m.name, &m.texture->Texture, &m.texture->Surface, m.w, m.h, m.format);
		if (!m.texture->Surface) {
			Logger::Log("[ERROR] TAA FSR: could not create %s (%ix%i, format %u)", m.name, m.w, m.h, (UInt32)m.format);
			FSRReleaseResources();
			return false;
		}
	}
	fsrReconstructedDepth = DxvkCompute::CreateBuffer(width * height * 4, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "FSR reconstructed depth");
	if (!fsrReconstructedDepth) { FSRReleaseResources(); return false; }

	// AMD's first-execution clears (lock status, prepared colour) plus the new locks and the history, and the reconstructed depth
	// buffer at the far plane (AMD's lock pass resets it to that after each frame).
	IDirect3DDevice9* device = TheRenderManager->device;
	FSRTexture* cleared[] = { &fsrLockStatus[0], &fsrLockStatus[1], &fsrPrepared, &fsrNewLocks, &fsrUpscaled[0], &fsrUpscaled[1],
		&fsrLumaHistory[0], &fsrLumaHistory[1], &fsrDilatedMotion[0], &fsrDilatedMotion[1] };
	for (FSRTexture* t : cleared) device->ColorFill(t->Surface, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
	DxvkCompute::Extension()->ClearBuffer(fsrReconstructedDepth, 0, 0, 0x3f800000u);

	fsrWidth = width;
	fsrHeight = height;
	fsrParity = 0;
	fsrFrameIndex = 0;
	Logger::Log("UNOFFICIAL NVR DXVK: TAA FSR 2 resources made (%ix%i, about %u MB)", width, height,
		(UInt32)((UInt64)width * height * (4 * 4 + 8 + 4 + 2 * 2 + 8 * 6 + 4 * 3 + 4) / (1024 * 1024)));
	return true;
}

bool TAAEffect::FSRWanted() {
	if (!FSRSettings.Enabled || fsrFailed) return false;
	if (!DxvkCompute::Available()) {
		if (!fsrMissingLogged) Logger::Log("UNOFFICIAL: TAA FSR is on but needs NVR DXVK (%s); TAA's own resolve runs.", DxvkCompute::Status());
		fsrMissingLogged = true;
		return false;
	}
	return true;
}

bool TAAEffect::FSRDispatch(int index, const NVR_EXT_RESOURCE_BINDING* bindings, UINT count, const void* push, UINT pushSize, UINT groupsX, UINT groupsY) {
	NVR_EXT_DISPATCH_DESC dispatch = {};
	dispatch.pPipeline = fsrPipelines[index];
	dispatch.BindingCount = count;
	dispatch.pBindings = bindings;
	dispatch.pPushData = push;
	dispatch.PushDataSize = pushSize;
	dispatch.GroupsX = groupsX;
	dispatch.GroupsY = groupsY;
	dispatch.GroupsZ = 1;
	// F10: one GPU timer per pass, inside TAA's ("TAA" covers the whole FSR path).
	static GpuTimer timers[FSRPipelineCount] = { GpuTimer("  FSR prep (NVR)"), GpuTimer("  FSR luma mip (NVR)"), GpuTimer("  FSR reconstruct"),
		GpuTimer("  FSR depth clip"), GpuTimer("  FSR lock"), GpuTimer("  FSR accumulate"), GpuTimer("  FSR accumulate (sharpen)"), GpuTimer("  FSR RCAS") };
	GpuProfileScope gpu(timers[index], TheRenderManager->device);
	DxvkCompute::PreLoad(bindings, count);	// UNOFFICIAL 2026-10-27 (merge with the alflicker fix): managed textures on the GPU
	if (FAILED(DxvkCompute::Extension()->Dispatch(&dispatch))) {
		Logger::Log("[ERROR] UNOFFICIAL NVR DXVK: TAA FSR pass %s failed to dispatch; FSR is off for this session (TAA's own resolve runs)", Pipelines[index].name);
		fsrFailed = true;
		return false;
	}
	return true;
}

bool TAAEffect::FSRDraw(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, bool reset) {
	auto fail = [this](const char* why) {
		Logger::Log("[ERROR] UNOFFICIAL NVR DXVK: TAA FSR: %s; FSR is off for this session (TAA's own resolve runs)", why);
		fsrFailed = true;
		return false;
	};

	if (!fsrTried) {
		fsrTried = true;
		for (int i = 0; i < FSRPipelineCount; i++) {
			const PipelineInfo& p = Pipelines[i];
			fsrPipelines[i] = DxvkCompute::CreatePipeline(p.name, p.bindings, p.count, p.push, p.samplers);
			if (!fsrPipelines[i]) return fail("a compute pipeline could not be made (Compute\\FSR*.spv missing or refused)");
		}
		fsrComposite.Tried = true;
		fsrComposite.Pipeline = DxvkCompute::CreateGraphicsPipeline("FSRComposite", CompositeBindings, _countof(CompositeBindings), 16, true);
		if (!fsrComposite.Pipeline) return fail("the composite pass could not be made (Compute\\FSRComposite.spv)");
		QueryPerformanceCounter(&fsrLastTime);
	}
	for (int i = 0; i < FSRPipelineCount; i++) if (!fsrPipelines[i]) return false;
	if (!FSREnsureResources()) return fail("its textures could not be made");

	const D3DXVECTOR4* projection = Registered("TESR_ProjectionTransform");
	const D3DXVECTOR4* view = Registered("TESR_ViewTransform");
	const D3DXVECTOR4* cameraData = Registered("TESR_CameraData");
	IDirect3DBaseTexture9* source = TheTextureManager->SourceTexture;
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* viewModel = SlotTexture("TESR_DepthBufferViewModel");
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	if (!projection || !view || !cameraData || !source || !depth || !viewModel || !point || !linear) return fail("an input is missing");

	const int width = fsrWidth, height = fsrHeight;
	const float nearZ = cameraData->x, farZ = cameraData->y;
	const D3DXMATRIX& proj = *(const D3DXMATRIX*)projection;

	if (reset) {
		// AMD's reset: clear the lock status and history it would read, start the frame count again.
		Device->ColorFill(fsrLockStatus[fsrParity].Surface, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
		Device->ColorFill(fsrUpscaled[fsrParity].Surface, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
		DxvkCompute::Extension()->ClearBuffer(fsrReconstructedDepth, 0, 0, 0x3f800000u);
		fsrFrameIndex = 0;
	}
	else fsrFrameIndex++;
	const int srv = fsrParity, uav = fsrParity ^ 1;

	LARGE_INTEGER now, frequency;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&frequency);
	float deltaTime = (float)(now.QuadPart - fsrLastTime.QuadPart) / (float)frequency.QuadPart;
	fsrLastTime = now;

	// 1. NVR's inputs: motion vectors, FSR depth + reactive.
	FSRPrepConstants prep = {};
	memcpy(&prep.projection, projection, sizeof(D3DXMATRIX));
	memcpy(&prep.view, view, sizeof(D3DXMATRIX));
	prep.prevView = Constants.PrevViewTransform;
	prep.cameraData = *cameraData;
	prep.prevProjection = Constants.PrevProjection;
	prep.cameraDelta = Constants.CameraDelta;
	prep.size = D3DXVECTOR4((float)width, (float)height, reset ? 1.0f : 0.0f, FSRSettings.WeaponReactive);
	ID3D9NvrExtBuffer* prepBuffer = ExtUniforms(fsrPrepUniforms, &prep, sizeof(prep));
	if (!prepBuffer) return fail("its constant buffer could not be written");

	FSRConstants c = {};
	c.renderSize[0] = c.maxRenderSize[0] = c.displaySize[0] = c.inputColorResourceDimensions[0] = width;
	c.renderSize[1] = c.maxRenderSize[1] = c.displaySize[1] = c.inputColorResourceDimensions[1] = height;
	c.lumaMipLevelToUse = 4;					// FFX_FSR2_SHADING_CHANGE_MIP_LEVEL
	c.lumaMipDimensions[0] = (std::max)(1, width / 32);	// maxRenderSize / (2 << 4), as AMD's
	c.lumaMipDimensions[1] = (std::max)(1, height / 32);
	c.frameIndex = fsrFrameIndex;
	// AMD's setupDeviceDepthToViewSpaceDepthParams for a standard (not inverted, finite) projection with NVR's near and far:
	// FSRPrep writes a depth that formula turns back into NVR's linear view depth.
	const float q = farZ / (nearZ - farZ);
	c.deviceToViewDepth[0] = -q;
	c.deviceToViewDepth[1] = q * nearZ;
	c.deviceToViewDepth[2] = 1.0f / proj._11;	// tan(horizontal fov / 2)
	c.deviceToViewDepth[3] = 1.0f / proj._22;	// tan(vertical fov / 2)
	// The jitter as FSR defines it (the sample at pixel p shows the unjittered point p - jitter, y down): BeginJitter moves the
	// frustum right/up by (x, y) pixels, i.e. the picture left/down, so FSR's offset is (-x, +y).
	c.jitterOffset[0] = frameJittered ? -frameJitterX : 0.0f;
	c.jitterOffset[1] = frameJittered ? frameJitterY : 0.0f;
	c.motionVectorScale[0] = c.motionVectorScale[1] = 1.0f;	// FSRPrep writes uv units
	c.downscaleFactor[0] = c.downscaleFactor[1] = 1.0f;
	c.preExposure = c.previousFramePreExposure = 1.0f;
	c.tanHalfFOV = 1.0f / proj._11;
	c.jitterPhaseCount = 8.0f;					// ffxFsr2GetJitterPhaseCount at scale 1; TAA's Halton sequence is 8 long
	c.deltaTime = deltaTime < 0.0f ? 0.0f : (deltaTime > 1.0f ? 1.0f : deltaTime);
	c.dynamicResChangeFactor = 0.0f;
	c.viewSpaceToMetersFactor = 1.0f / 70.0f;	// Gamebryo units: about 70 per metre
	ID3D9NvrExtBuffer* constants = ExtUniforms(fsrUniforms, &c, sizeof(c));
	if (!constants) return fail("its constant buffer could not be written");

	FSRPush push = {};
	const float sharpness = FSRSettings.Sharpness;
	const bool sharpen = sharpness > 0.0f;
	if (sharpen) {
		// ffx_fsr2.cpp: sharpenessRemapped = -2 * sharpness + 2, then FsrRcasCon: exp2(-stops) as float and packed halves.
		float stops = -2.0f * sharpness + 2.0f;
		float s = exp2f(-stops);
		push.rcas[0] = *(UInt32*)&s;
		push.rcas[1] = HalfBits(s) | (HalfBits(s) << 16);
	}

	const UINT gx8 = (width + 7) / 8, gy8 = (height + 7) / 8;
	ID3D9NvrExtBuffer* depthBuffer = fsrReconstructedDepth;

	{
		NVR_EXT_RESOURCE_BINDING b[] = { Image(0, depth), Image(1, viewModel), Image(2, fsrMotion.Texture, true), Image(3, fsrDepth.Texture, true),
			Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, prepBuffer), Sampler(0, point) };
		UInt32 prepPush = 0;
		if (!FSRDispatch(FSRPrep, b, _countof(b), &prepPush, 4, gx8, gy8)) return false;
	}
	{
		NVR_EXT_RESOURCE_BINDING b[] = { Image(0, source), Image(1, fsrLumaMip.Texture, true), Sampler(0, point) };
		struct { UInt32 pointIndex, pad; INT32 w, h; } lumaPush = { 0, 0, width, height };
		if (!FSRDispatch(FSRLumaMip, b, _countof(b), &lumaPush, sizeof(lumaPush), (std::max)(1, width / 32), (std::max)(1, height / 32))) return false;
	}
	// 2. AMD's passes, in ffx_fsr2.cpp's order and with its ping-pong choice (srv = last frame's, uav = this frame's).
	{
		NVR_EXT_RESOURCE_BINDING b[] = { Image(0, fsrMotion.Texture), Image(1, fsrDepth.Texture), Image(2, source),
			Buffer(1, NVR_EXT_BIND_STORAGE_BUFFER, depthBuffer), Image(6, fsrDilatedMotion[uav].Texture, true), Image(7, fsrDilatedDepth.Texture, true),
			Image(11, fsrLockLuma.Texture, true), Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants) };
		if (!FSRDispatch(FSRReconstruct, b, _countof(b), nullptr, 0, gx8, gy8)) return false;
	}
	{
		NVR_EXT_RESOURCE_BINDING b[] = { Buffer(1, NVR_EXT_BIND_STORAGE_BUFFER, depthBuffer), Image(1, fsrDilatedMotion[uav].Texture),
			Image(2, fsrDilatedDepth.Texture), Image(6, fsrDilatedMotion[srv].Texture), Image(7, fsrMotion.Texture), Image(8, source),
			Image(9, fsrDepth.Texture), Image(12, fsrReactiveMasks.Texture, true), Image(13, fsrPrepared.Texture, true),
			Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants), Sampler(0, point), Sampler(1, linear) };
		if (!FSRDispatch(FSRDepthClip, b, _countof(b), &push, sizeof(push), gx8, gy8)) return false;
	}
	{
		NVR_EXT_RESOURCE_BINDING b[] = { Image(0, fsrLockLuma.Texture), Image(1, fsrNewLocks.Texture, true),
			Buffer(1, NVR_EXT_BIND_STORAGE_BUFFER, depthBuffer), Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants) };
		if (!FSRDispatch(FSRLock, b, _countof(b), nullptr, 0, gx8, gy8)) return false;
	}
	{
		NVR_EXT_RESOURCE_BINDING b[] = { Image(1, fsrReactiveMasks.Texture), Image(2, fsrDilatedMotion[uav].Texture), Image(3, fsrUpscaled[srv].Texture),
			Image(4, fsrLockStatus[srv].Texture), Image(6, fsrPrepared.Texture), Image(10, fsrLumaMip.Texture), Image(12, fsrLumaHistory[srv].Texture),
			Image(13, fsrUpscaled[uav].Texture, true), Image(14, fsrLockStatus[uav].Texture, true), Image(15, fsrOutput.Texture, true),
			Image(16, fsrNewLocks.Texture, true), Image(17, fsrLumaHistory[uav].Texture, true),
			Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants), Sampler(0, point), Sampler(1, linear) };
		if (sharpen) {
			// The sharpening variant has no output binding (RCAS writes it).
			NVR_EXT_RESOURCE_BINDING s[_countof(b) - 1];
			UINT n = 0;
			for (const NVR_EXT_RESOURCE_BINDING& r : b) if (!(r.Type == NVR_EXT_BIND_STORAGE_IMAGE && r.Slot == 15)) s[n++] = r;
			if (!FSRDispatch(FSRAccumulateSharpen, s, n, &push, sizeof(push), gx8, gy8)) return false;
		}
		else if (!FSRDispatch(FSRAccumulate, b, _countof(b), &push, sizeof(push), gx8, gy8)) return false;
	}
	if (sharpen) {
		NVR_EXT_RESOURCE_BINDING b[] = { Image(1, fsrUpscaled[uav].Texture), Image(2, fsrOutput.Texture, true), Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, constants) };
		if (!FSRDispatch(FSRRCAS, b, _countof(b), &push, sizeof(push), (width + 15) / 16, (height + 15) / 16)) return false;
	}

	// 3. Into the game target, the weapon as rendered (DebugView 1 / 5: NVR's motion / depth inputs instead).
	{
		NVR_EXT_RESOURCE_BINDING b[] = { Image(0, fsrOutput.Texture), Image(1, source), Image(2, viewModel), Image(3, fsrMotion.Texture),
			Image(4, fsrDepth.Texture), Sampler(0, point) };
		struct { UInt32 pointIndex; float debugView; float w, h; } compositePush = { 0, (float)Settings.DebugView, (float)width, (float)height };
		static GpuTimer compositeTimer("  FSR composite (NVR)");
		GpuProfileScope gpu(compositeTimer, Device);
		if (!ExtDrawTo(fsrComposite.Pipeline, &RenderTarget, 1, b, _countof(b), &compositePush, sizeof(compositePush))) return fail("the composite pass failed");
	}

	fsrParity ^= 1;
	if (++fsrFrames == 1) Logger::Log("UNOFFICIAL NVR DXVK: TAA runs FSR 2 (AMD FidelityFX Super Resolution 2.2.1) as its resolve, %ix%i native, sharpness %.2f.", width, height, sharpness);
	return true;
}
