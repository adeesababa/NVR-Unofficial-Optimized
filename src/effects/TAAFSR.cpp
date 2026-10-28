
#include <algorithm>

#include "TAA.h"
#include "../core/DxvkCompute.h"
#include "../core/GpuProfiler.h"
#include "../core/Upscaler.h"

namespace {
	struct FSRPrepConstants {
		D3DXMATRIX	projection, view, prevView;
		D3DXVECTOR4	cameraData, prevProjection, cameraDelta, size;
	};

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
	static GpuTimer timer("  FSR prep (NVR)");
	GpuProfileScope gpu(timer, TheRenderManager->device);
	DxvkCompute::PreLoad(bindings, count);
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

	Upscaler::Inputs in;
	in.Color = source;
	in.Depth = fsrDepth.Texture;
	in.Motion = fsrMotion.Texture;
	in.RenderWidth = in.InputWidth = in.DisplayWidth = (UINT)width;
	in.RenderHeight = in.InputHeight = in.DisplayHeight = (UINT)height;
	in.JitterX = frameJittered ? -frameJitterX : 0.0f;
	in.JitterY = frameJittered ? frameJitterY : 0.0f;
	in.JitterPhases = 8.0f;
	in.NearZ = nearZ;
	in.FarZ = farZ;
	in.TanHalfFovX = 1.0f / proj._11;
	in.TanHalfFovY = 1.0f / proj._22;
	in.DeltaSeconds = deltaTime;
	in.Sharpness = FSRSettings.Sharpness;
	in.Hdr = false;
	in.Reset = reset || !lastPathFSR;
	Upscaler::IUpscaler* upscaler = Upscaler::Choose(FSRSettings.Upscaler);
	if (!upscaler->Evaluate(Device, in)) return fail(upscaler->Failed() ? "the upscaler failed" : "the upscaler could not run this frame");

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
