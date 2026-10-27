// UNOFFICIAL 2026-10-26 (NVR DXVK): AMD FidelityFX Super Resolution 2 (FSR 2.2.1) as TAA's resolve, at native resolution.
//
// CREDIT: FSR 2 is AMD's (FidelityFX Super Resolution 2.2.1, GPUOpen, https://github.com/GPUOpen-Effects/FidelityFX-FSR2),
// MIT licence: src\hlsl\NewVegas\Compute\FSR2\LICENSE-FidelityFX-FSR2.txt. AMD's passes and their CPU side live in
// core\Upscaler.cpp since 2026-10-27 (shared with RenderScale, effects\TAAUpscale.cpp); this file builds NVR's inputs for them
// at native resolution and puts the result into the frame.
//
// Needs NVR DXVK (compute through the extension). Without it, or after any failure (logged once), TAA's own resolve runs.
// Settings: [Shaders.TAA.FSR] Enabled / Sharpness / WeaponReactive (TAA must be on; TAA's Jitter and MipBias are used).
//
// Per frame, all at the render (= display) resolution, after TAA's usual inputs are ready (post-tonemap frame in
// TESR_SourceBuffer, the world jittered by BeginJitter):
//   FSRPrep        NVR: camera motion vectors from depth (TAA's reprojection) + FSR-style device depth + reactive (weapon)
//   Upscaler::Fsr2()->Evaluate: FSRLumaMip (NVR), then AMD's FSR2Reconstruct, FSR2DepthClip, FSR2Lock, FSR2Accumulate[Sharpen], FSR2RCAS
//   FSRComposite   NVR: FSR's output into the game target, the first-person weapon put back as rendered
// What FSR does not get (yet): motion of skinned characters and other moving objects (camera motion only; FSR's depth clip and
// luma locks handle what they can), a reactive mask for particles/transparencies. Real upscaling: RenderScale (TAAUpscale.cpp).

#include <algorithm>

#include "TAA.h"
#include "../core/DxvkCompute.h"
#include "../core/GpuProfiler.h"
#include "../core/Upscaler.h"

namespace {
	// Constants of FSRPrep.comp (std140).
	struct FSRPrepConstants {
		D3DXMATRIX	projection, view, prevView;
		D3DXVECTOR4	cameraData, prevProjection, cameraDelta, size;
	};

	// Binding tables: (set, binding) as the SPIR-V declares them (checked with spirv-dis), slot = binding for images,
	// buffer slot 0 = the constants; sampler POINT (slot 0, push 0).
#define IMG(b)      { 1, b, NVR_EXT_BIND_SAMPLED_IMAGE, b, NVR_EXT_VIEW_2D, FALSE, 0 }
#define UAV(b)      { 1, b, NVR_EXT_BIND_STORAGE_IMAGE, b, NVR_EXT_VIEW_2D, TRUE, 0 }
#define CB(s, b)    { s, b, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 }
#define POINTS      { 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 0 }
	const NVR_EXT_BINDING_DESC PrepBindings[] = { IMG(0), IMG(1), UAV(2), UAV(3), CB(2, 0), POINTS };
	const NVR_EXT_BINDING_DESC CompositeBindings[] = { IMG(0), IMG(1), IMG(2), IMG(3), IMG(4), POINTS };
#undef IMG
#undef UAV
#undef CB
#undef POINTS

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
}

void TAAEffect::FSRReleaseResources() {
	FSRTexture* textures[] = { &fsrMotion, &fsrDepth };
	for (FSRTexture* t : textures) {
		if (t->Surface) { t->Surface->Release(); t->Surface = nullptr; }
		if (t->Texture) { t->Texture->Release(); t->Texture = nullptr; }
	}
	fsrWidth = fsrHeight = 0;
}

// NVR's per-frame inputs for FSR (motion vectors, FSR-style depth + reactive), made on first use and again when the resolution
// changes; FSR's own resources are the upscaler's (core\Upscaler.cpp). Render-target textures so DXVK can add storage use.
bool TAAEffect::FSREnsureResources() {
	const int width = TheRenderManager->width, height = TheRenderManager->height;
	if (fsrWidth == width && fsrHeight == height && fsrMotion.Surface && fsrDepth.Surface) return true;
	FSRReleaseResources();
	TheTextureManager->InitTexture("TESR_FSRMotion", &fsrMotion.Texture, &fsrMotion.Surface, width, height, D3DFMT_G16R16F);
	TheTextureManager->InitTexture("TESR_FSRDepth", &fsrDepth.Texture, &fsrDepth.Surface, width, height, D3DFMT_G32R32F);
	if (!fsrMotion.Surface || !fsrDepth.Surface) {
		Logger::Log("[ERROR] TAA FSR: could not create its motion / depth inputs (%ix%i)", width, height);
		FSRReleaseResources();
		return false;
	}
	fsrWidth = width;
	fsrHeight = height;
	Logger::Log("UNOFFICIAL NVR DXVK: TAA FSR 2 inputs made (%ix%i, about %u MB)", width, height, (UInt32)((UInt64)width * height * 12 / (1024 * 1024)));
	return true;
}

bool TAAEffect::FSRWanted() {
	if (!FSRSettings.Enabled || fsrFailed) return false;
	if (!DxvkCompute::Available()) {
		if (!fsrMissingLogged) Logger::Log("UNOFFICIAL: TAA FSR is on but needs NVR DXVK (%s); TAA's own resolve runs.", DxvkCompute::Status());
		fsrMissingLogged = true;
		return false;
	}
	if (Upscaler::Fsr2()->Failed()) return false;
	return true;
}

bool TAAEffect::FSRDispatchPrep(const NVR_EXT_RESOURCE_BINDING* bindings, UINT count, const void* push, UINT pushSize, UINT groupsX, UINT groupsY) {
	NVR_EXT_DISPATCH_DESC dispatch = {};
	dispatch.pPipeline = fsrPrep;
	dispatch.BindingCount = count;
	dispatch.pBindings = bindings;
	dispatch.pPushData = push;
	dispatch.PushDataSize = pushSize;
	dispatch.GroupsX = groupsX;
	dispatch.GroupsY = groupsY;
	dispatch.GroupsZ = 1;
	// F10: inside TAA's timer ("TAA" covers the whole FSR path).
	static GpuTimer timer("  FSR prep (NVR)");
	GpuProfileScope gpu(timer, TheRenderManager->device);
	DxvkCompute::PreLoad(bindings, count);	// UNOFFICIAL 2026-10-27 (merge with the alflicker fix): managed textures on the GPU
	if (FAILED(DxvkCompute::Extension()->Dispatch(&dispatch))) {
		Logger::Log("[ERROR] UNOFFICIAL NVR DXVK: TAA FSR pass FSRPrep failed to dispatch; FSR is off for this session (TAA's own resolve runs)");
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
		fsrPrep = DxvkCompute::CreatePipeline("FSRPrep", PrepBindings, _countof(PrepBindings), 4, true);
		if (!fsrPrep) return fail("a compute pipeline could not be made (Compute\\FSR*.spv missing or refused)");
		fsrComposite.Tried = true;
		fsrComposite.Pipeline = DxvkCompute::CreateGraphicsPipeline("FSRComposite", CompositeBindings, _countof(CompositeBindings), 16, true);
		if (!fsrComposite.Pipeline) return fail("the composite pass could not be made (Compute\\FSRComposite.spv)");
		QueryPerformanceCounter(&fsrLastTime);
	}
	if (!fsrPrep || !fsrComposite.Pipeline) return false;
	if (!FSREnsureResources()) return fail("its textures could not be made");

	const D3DXVECTOR4* projection = Registered("TESR_ProjectionTransform");
	const D3DXVECTOR4* view = Registered("TESR_ViewTransform");
	const D3DXVECTOR4* cameraData = Registered("TESR_CameraData");
	IDirect3DBaseTexture9* source = TheTextureManager->SourceTexture;
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* viewModel = SlotTexture("TESR_DepthBufferViewModel");
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	if (!projection || !view || !cameraData || !source || !depth || !viewModel || !point) return fail("an input is missing");

	const int width = fsrWidth, height = fsrHeight;
	const float nearZ = cameraData->x, farZ = cameraData->y;
	const D3DXMATRIX& proj = *(const D3DXMATRIX*)projection;

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
	{
		NVR_EXT_RESOURCE_BINDING b[] = { Image(0, depth), Image(1, viewModel), Image(2, fsrMotion.Texture, true), Image(3, fsrDepth.Texture, true),
			Buffer(0, NVR_EXT_BIND_UNIFORM_BUFFER, prepBuffer), Sampler(0, point) };
		UInt32 prepPush = 0;
		if (!FSRDispatchPrep(b, _countof(b), &prepPush, 4, (width + 7) / 8, (height + 7) / 8)) return false;
	}

	// 2. The upscaler (FSR 2) at render size = display size, on TAA's 8-step jitter.
	Upscaler::Inputs in;
	in.Color = source;
	in.Depth = fsrDepth.Texture;
	in.Motion = fsrMotion.Texture;
	in.RenderWidth = in.InputWidth = in.DisplayWidth = (UINT)width;
	in.RenderHeight = in.InputHeight = in.DisplayHeight = (UINT)height;
	// The jitter as FSR defines it (the sample at pixel p shows the unjittered point p - jitter, y down): BeginJitter moves the
	// frustum right/up by (x, y) pixels, i.e. the picture left/down, so FSR's offset is (-x, +y).
	in.JitterX = frameJittered ? -frameJitterX : 0.0f;
	in.JitterY = frameJittered ? frameJitterY : 0.0f;
	in.JitterPhases = 8.0f;					// ffxFsr2GetJitterPhaseCount at scale 1; TAA's Halton sequence is 8 long
	in.NearZ = nearZ;
	in.FarZ = farZ;
	in.TanHalfFovX = 1.0f / proj._11;		// tan(horizontal fov / 2)
	in.TanHalfFovY = 1.0f / proj._22;		// tan(vertical fov / 2)
	in.DeltaSeconds = deltaTime;
	in.Sharpness = FSRSettings.Sharpness;
	in.Hdr = false;							// the tonemapped frame
	in.Reset = reset || !lastPathFSR;		// the upscaler's history is RenderScale's after an upscaled frame
	Upscaler::IUpscaler* upscaler = Upscaler::Choose(FSRSettings.Upscaler);	// FSR 2 (DLSS is not built: core/Upscaler.h)
	if (!upscaler->Evaluate(Device, in)) return fail(upscaler->Failed() ? "the upscaler failed" : "the upscaler could not run this frame");

	// 3. Into the game target, the weapon as rendered (DebugView 1 / 5: NVR's motion / depth inputs instead).
	{
		NVR_EXT_RESOURCE_BINDING b[] = { Image(0, upscaler->Output()), Image(1, source), Image(2, viewModel), Image(3, fsrMotion.Texture),
			Image(4, fsrDepth.Texture), Sampler(0, point) };
		struct { UInt32 pointIndex; float debugView; float w, h; } compositePush = { 0, (float)Settings.DebugView, (float)width, (float)height };
		static GpuTimer compositeTimer("  FSR composite (NVR)");
		GpuProfileScope gpu(compositeTimer, Device);
		if (!ExtDrawTo(fsrComposite.Pipeline, &RenderTarget, 1, b, _countof(b), &compositePush, sizeof(compositePush))) return fail("the composite pass failed");
	}

	if (++fsrFrames == 1) Logger::Log("UNOFFICIAL NVR DXVK: TAA runs FSR 2 (AMD FidelityFX Super Resolution 2.2.1) as its resolve, %ix%i native, sharpness %.2f.", width, height, FSRSettings.Sharpness);
	return true;
}
