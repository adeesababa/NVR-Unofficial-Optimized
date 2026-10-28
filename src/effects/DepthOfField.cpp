#include "DepthOfField.h"
#include "../core/DxvkCompute.h"

void DepthOfFieldEffect::UpdateConstants() {
	TheShaderManager->avglumaRequired = true;

	ValuesStruct* category = &Settings.FirstPerson;
	if (TheCameraManager->IsVanity())
		category = &Settings.VanityView;
	else if (!TheCameraManager->IsFirstPerson())
		category = &Settings.ThirdPerson;

	int Mode = category->Mode;
	int dofActive = category->Enabled;

	// disable based on settings/context
	if ((Mode == 1 && (TheShaderManager->GameState.isDialog || TheShaderManager->GameState.isPersuasion)) ||
		(Mode == 2 && (!TheShaderManager->GameState.isDialog)) ||
		(Mode == 3 && (!TheShaderManager->GameState.isPersuasion)) ||
		(Mode == 4 && (!TheShaderManager->GameState.isDialog || !TheShaderManager->GameState.isPersuasion))) dofActive = 0;

	Constants.Enabled = dofActive;

	Constants.Blur.x = category->DistantBlur;
	Constants.Blur.y = category->DistantBlurStartRange;
	Constants.Blur.z = category->DistantBlurEndRange;
	Constants.Blur.w = category->BaseBlurRadius;
	Constants.Data.x = category->focusDistance * dofActive;
	Constants.Data.y = category->Radius;
	Constants.Data.z = category->DiameterRange;
	Constants.Data.w = category->NearBlurCutOff;

}

void DepthOfFieldEffect::UpdateSettings() {
	Settings.FirstPerson.DistantBlur = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "DistantBlur");
	Settings.FirstPerson.DistantBlurStartRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "DistantBlurStartRange");
	Settings.FirstPerson.DistantBlurEndRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "DistantBlurEndRange");
	Settings.FirstPerson.BaseBlurRadius = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "BaseBlurRadius");
	Settings.FirstPerson.focusDistance = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "BlurFallOff");
	Settings.FirstPerson.Radius = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "Radius");
	Settings.FirstPerson.DiameterRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "DiameterRange");
	Settings.FirstPerson.NearBlurCutOff = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "NearBlurCutOff");
	Settings.FirstPerson.Mode = TheSettingManager->GetSettingI("Shaders.DepthOfField.FirstPersonView", "Mode");
	Settings.FirstPerson.Enabled = TheSettingManager->GetSettingI("Shaders.DepthOfField.FirstPersonView", "Enabled");

	Settings.ThirdPerson.DistantBlur = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "DistantBlur");
	Settings.ThirdPerson.DistantBlurStartRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "DistantBlurStartRange");
	Settings.ThirdPerson.DistantBlurEndRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "DistantBlurEndRange");
	Settings.ThirdPerson.BaseBlurRadius = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "BaseBlurRadius");
	Settings.ThirdPerson.focusDistance = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "BlurFallOff");
	Settings.ThirdPerson.Radius = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "Radius");
	Settings.ThirdPerson.DiameterRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "DiameterRange");
	Settings.ThirdPerson.NearBlurCutOff = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "NearBlurCutOff");
	Settings.ThirdPerson.Mode = TheSettingManager->GetSettingI("Shaders.DepthOfField.ThirdPersonView", "Mode");
	Settings.ThirdPerson.Enabled = TheSettingManager->GetSettingI("Shaders.DepthOfField.ThirdPersonView", "Enabled");

	Settings.VanityView.DistantBlur = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "DistantBlur");
	Settings.VanityView.DistantBlurStartRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "DistantBlurStartRange");
	Settings.VanityView.DistantBlurEndRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "DistantBlurEndRange");
	Settings.VanityView.BaseBlurRadius = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "BaseBlurRadius");
	Settings.VanityView.focusDistance = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "BlurFallOff");
	Settings.VanityView.Radius = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "Radius");
	Settings.VanityView.DiameterRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "DiameterRange");
	Settings.VanityView.NearBlurCutOff = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "NearBlurCutOff");
	Settings.VanityView.Mode = TheSettingManager->GetSettingI("Shaders.DepthOfField.VanityView", "Mode");
	Settings.VanityView.Enabled = TheSettingManager->GetSettingI("Shaders.DepthOfField.VanityView", "Enabled");

	Bokeh.Bokeh = TheSettingManager->GetSettingI("Shaders.DepthOfField.Bokeh", "Bokeh") != 0;
	Bokeh.BokehShape = TheSettingManager->GetSettingI("Shaders.DepthOfField.Bokeh", "BokehShape");
	if (Bokeh.BokehShape != 0) Bokeh.BokehShape = max(5, min(Bokeh.BokehShape, 8));
	Bokeh.BokehThreshold = max(0.0f, min(TheSettingManager->GetSettingF("Shaders.DepthOfField.Bokeh", "BokehThreshold"), 0.99f));
	Bokeh.BokehIntensity = max(0.0f, min(TheSettingManager->GetSettingF("Shaders.DepthOfField.Bokeh", "BokehIntensity"), 10.0f));
	Bokeh.BokehSize = max(1.0f, min(TheSettingManager->GetSettingF("Shaders.DepthOfField.Bokeh", "BokehSize"), 64.0f));
	Bokeh.BokehRotation = TheSettingManager->GetSettingF("Shaders.DepthOfField.Bokeh", "BokehRotation");
	Bokeh.BokehMaxSprites = max(256, min(TheSettingManager->GetSettingI("Shaders.DepthOfField.Bokeh", "BokehMaxSprites"), 16384));
}

void DepthOfFieldEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex == 0 && BokehWanted() && !ExtFailed && !DxvkCompute::PassesOn() && DxvkCompute::Available() &&
		Enabled && Effect && ShouldRender() && ExtRender(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer)) {
		Device->SetRenderTarget(0, RenderTarget);
		return;
	}
	EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
}

namespace {
	struct BokehPush {
		UInt32 halfW, halfH, fullW, fullH, phase, maxSprites, shape, samplerIndex;
		float threshold, intensity, maxRadius, rotation;
	};
	const UINT BokehSpriteHeader = 16;
}

void DepthOfFieldEffect::BokehRelease() {
	DxvkCompute::Drop(bokehScatter);
	DxvkCompute::Drop(bokehCombine);
	DxvkCompute::Drop(bokehResolve);
	DxvkCompute::Drop(bokehSprites);
	DxvkCompute::Drop(bokehAccum);
	DxvkCompute::Drop(bokehSurface);
	DxvkCompute::Drop(bokehTexture);
}

static bool MakeBokehTargets(UINT w, UINT h, UINT budget, ID3D9NvrExtBuffer*& sprites, ID3D9NvrExtBuffer*& accum, IDirect3DTexture9*& texture, IDirect3DSurface9*& surface) {
	sprites = DxvkCompute::CreateBuffer(16 + budget * 32, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "DoF bokeh sprites");
	accum = DxvkCompute::CreateBuffer(w * h * 12, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "DoF bokeh accumulation");
	if (FAILED(TheRenderManager->device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &texture, NULL))) texture = nullptr;
	if (texture && FAILED(texture->GetSurfaceLevel(0, &surface))) surface = nullptr;
	return sprites && accum && texture && surface;
}

bool DepthOfFieldEffect::BokehReady() {
	const UINT w = (TheRenderManager->width + 1) / 2, h = (TheRenderManager->height + 1) / 2, budget = (UINT)Bokeh.BokehMaxSprites;
	if (bokehScatter && bokehCombine && bokehResolve && bokehSprites && bokehAccum && bokehSurface) {
		if (w == bokehW && h == bokehH && budget == bokehMax) return true;
		DxvkCompute::Drop(bokehSprites);
		DxvkCompute::Drop(bokehAccum);
		DxvkCompute::Drop(bokehSurface);
		DxvkCompute::Drop(bokehTexture);
		bokehW = w; bokehH = h; bokehMax = budget;
		if (MakeBokehTargets(w, h, budget, bokehSprites, bokehAccum, bokehTexture, bokehSurface)) return true;
		BokehRelease();
		bokehFailed = true;
		Logger::Log("UNOFFICIAL DepthOfField: bokeh buffers could not be made (%ux%u); bokeh off for this session", w, h);
		return false;
	}
	if (bokehTried) return false;
	bokehTried = true;
	const NVR_EXT_BINDING_DESC scatterBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE,  2, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 3, NVR_EXT_BIND_STORAGE_BUFFER, 0, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 4, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 28 },
	};
	const NVR_EXT_BINDING_DESC combineBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE,  2, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 208 },
	};
	const NVR_EXT_BINDING_DESC resolveBindings[] = {
		{ 1, 0, NVR_EXT_BIND_STORAGE_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
	};
	bokehScatter = DxvkCompute::CreatePipeline("DepthOfFieldBokehScatter", scatterBindings, _countof(scatterBindings), sizeof(BokehPush), true);
	bokehCombine = DxvkCompute::CreateGraphicsPipeline("DepthOfFieldCombineBokeh", combineBindings, _countof(combineBindings), 224, true);
	bokehResolve = DxvkCompute::CreateGraphicsPipeline("DepthOfFieldBokehResolve", resolveBindings, _countof(resolveBindings), 16, false);
	bokehW = w; bokehH = h; bokehMax = budget;
	const bool targets = MakeBokehTargets(w, h, budget, bokehSprites, bokehAccum, bokehTexture, bokehSurface);
	if (!bokehScatter || !bokehCombine || !bokehResolve || !targets) {
		BokehRelease();
		bokehFailed = true;
		Logger::Log("UNOFFICIAL DepthOfField: bokeh could not be set up (needs NVR DXVK and Compute\\DepthOfFieldBokehScatter.spv, DepthOfFieldBokehResolve.spv, DepthOfFieldCombineBokeh.spv); bokeh off for this session");
		return false;
	}
	Logger::Log("UNOFFICIAL DepthOfField: bokeh ready (%ux%u half resolution, up to %u sprites)", w, h, budget);
	return true;
}

bool DepthOfFieldEffect::BokehScatter(IDirect3DDevice9* Device, IDirect3DBaseTexture9* depth) {
	static GpuTimer bokehTimer("  DoF bokeh scatter (compute)");
	GpuProfileScope gpu(bokehTimer, Device);
	ID3D9NvrExt* ext = DxvkCompute::Extension();
	ID3D9NvrExtSampler* sampler = DxvkCompute::LinearClamp();
	IDirect3DBaseTexture9* source = TheTextureManager->SourceTexture;
	IDirect3DBaseTexture9* blur = TheTextureManager->RenderedTexture;
	if (!ext || !sampler || !source || !blur || !depth) return false;

	BokehPush push = {};
	push.halfW = bokehW; push.halfH = bokehH;
	push.fullW = TheRenderManager->width; push.fullH = TheRenderManager->height;
	push.maxSprites = bokehMax;
	push.shape = (UInt32)Bokeh.BokehShape;
	push.threshold = Bokeh.BokehThreshold;
	push.intensity = Bokeh.BokehIntensity;
	push.maxRadius = Bokeh.BokehSize * (float)TheRenderManager->height / 1080.0f * 0.5f;
	push.rotation = Bokeh.BokehRotation * (float)D3DX_PI / 180.0f;

	NVR_EXT_RESOURCE_BINDING bindings[] = {
		{ 0, NVR_EXT_BIND_SAMPLED_IMAGE,  source,       0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_SAMPLED_IMAGE,  blur,         0, 0, 0, FALSE },
		{ 2, NVR_EXT_BIND_SAMPLED_IMAGE,  depth,        0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_STORAGE_BUFFER, bokehSprites, 0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_STORAGE_BUFFER, bokehAccum,   0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_SAMPLER,        sampler,      0, 0, 0, FALSE },
	};
	NVR_EXT_DISPATCH_DESC dispatch = {};
	dispatch.pPipeline = bokehScatter;
	dispatch.BindingCount = _countof(bindings);
	dispatch.pBindings = bindings;
	dispatch.pPushData = &push;
	dispatch.PushDataSize = sizeof(push);

	DxvkCompute::PreLoad(bindings, _countof(bindings));
	HRESULT result = ext->ClearBuffer(bokehSprites, 0, BokehSpriteHeader, 0);
	if (SUCCEEDED(result)) result = ext->ClearBuffer(bokehAccum, 0, 0, 0);
	push.phase = 0;
	dispatch.GroupsX = (bokehW + 15) / 16; dispatch.GroupsY = (bokehH + 15) / 16; dispatch.GroupsZ = 1;
	if (SUCCEEDED(result)) result = ext->Dispatch(&dispatch);
	push.phase = 1;
	dispatch.GroupsX = bokehMax; dispatch.GroupsY = 1;
	if (SUCCEEDED(result)) result = ext->Dispatch(&dispatch);
	if (SUCCEEDED(result)) {
		struct { UInt32 width, height, pad0, pad1; } resolvePush = { bokehW, bokehH, 0, 0 };
		NVR_EXT_RESOURCE_BINDING resolveResources[] = { { 0, NVR_EXT_BIND_STORAGE_BUFFER, bokehAccum, 0, 0, 0, FALSE } };
		NVR_EXT_DRAW_DESC draw = {};
		draw.pPipeline = bokehResolve;
		draw.TargetCount = 1;
		draw.Targets[0].pResource = bokehSurface;
		draw.BindingCount = _countof(resolveResources);
		draw.pBindings = resolveResources;
		draw.pPushData = &resolvePush;
		draw.PushDataSize = sizeof(resolvePush);
		result = ext->Draw(&draw);
	}
	if (FAILED(result)) {
		Logger::Log("UNOFFICIAL DepthOfField: bokeh dispatch failed (%08lX); bokeh off for this session", result);
		BokehRelease();
		bokehFailed = true;
		return false;
	}
	if (++bokehFrames == 1) Logger::Log("UNOFFICIAL DepthOfField: bokeh running (shape %d, threshold %.2f, size %.1f px at 1080p)", Bokeh.BokehShape, Bokeh.BokehThreshold, Bokeh.BokehSize);
	return true;
}

void DepthOfFieldEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_DepthOfFieldBlur", &Constants.Blur);
	TheShaderManager->RegisterConstant("TESR_DepthOfFieldData", &Constants.Data);
}

bool DepthOfFieldEffect::ShouldRender() {
	if (Constants.Enabled) return true;
	if (!Constants.Blur.x) return false;
	const float farZ = TheRenderManager->CameraData.y;
	const bool skip = farZ > 0.0f && farZ <= Constants.Blur.y;
	static int logged = -1;
	if (GpuTimer::Enabled && logged != (int)skip) {
		logged = skip;
		Logger::Log("UNOFFICIAL distant blur %s: camera far plane %.0f, blur starts at %.0f", skip ? "skipped (nothing that far)" : "runs", farZ, Constants.Blur.y);
	}
	return !skip;
};

struct DofPush {
	D3DXMATRIX viewTransform, projectionTransform;
	D3DXVECTOR4 reciprocalResolution, blur, data, cameraPosition, cameraData;
	UInt32 samplerIndex;
	float useBokeh;
	UInt32 pad[2];
};
static_assert(sizeof(DofPush) == 224, "DofPush: DepthOfField.glsl's push block (BokehReady makes the bokeh Combine with 224)");

bool DepthOfFieldEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (ClearRenderTarget) return false;
	DofPush push = {};
	push.viewTransform = TheRenderManager->viewMatrix;
	push.projectionTransform = TheRenderManager->projMatrix;
	push.reciprocalResolution = TheShaderManager->ShaderConst.ReciprocalResolution;
	push.blur = Constants.Blur;
	push.data = Constants.Data;
	push.cameraPosition = TheRenderManager->CameraPosition;
	push.cameraData = TheRenderManager->CameraData;

	IDirect3DBaseTexture9* depth = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
	if (techniqueIndex == 1) {
		IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture, depth };
		return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "DepthOfField", textures, _countof(textures), &push, sizeof(push), 208);
	}
	if (techniqueIndex != 0) return false;

	if (!SourceBuffer || SourceBuffer == RenderTarget || SourceBuffer != TheTextureManager->SourceSurface || !TheTextureManager->SourceTexture) return false;
	FrameChain& chain = TheShaderManager->Chain;
	if (!chain.Owns(RenderTarget, RenderedSurface)) return false;
	IDirect3DBaseTexture9* avgLuma = TheShaderManager->Effects.AvgLuma ? TheShaderManager->Effects.AvgLuma->Textures.AvgLumaTexture : nullptr;
	if (!depth || !avgLuma) return false;

	auto make = [](ExtPass& pass, const char* spvName, UINT textureCount) {
		if (pass.Tried) return;
		pass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[3] = {};
		for (UINT i = 0; i < textureCount; i++) {
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i; bindings[i].ViewType = NVR_EXT_VIEW_2D;
		}
		bindings[textureCount].Type = NVR_EXT_BIND_SAMPLER; bindings[textureCount].Slot = 0; bindings[textureCount].PushOffset = 208;
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvName, bindings, textureCount + 1, sizeof(DofPush), true);
	};
	make(extFocus, "DepthOfFieldFocus", 2);
	make(extMosaic, "DepthOfFieldMosaic", 2);
	make(extBokeh, "DepthOfFieldBokeh", 1);
	make(extCombine, "DepthOfFieldCombine", 2);
	ID3D9NvrExtSampler* sampler = DxvkCompute::LinearClamp();
	if (!extFocus.Pipeline || !extMosaic.Pipeline || !extBokeh.Pipeline || !extCombine.Pipeline || !sampler) return false;

	auto draw = [&](ExtPass& pass, IDirect3DBaseTexture9* const* textures, UINT textureCount, IDirect3DSurface9* source, bool lastPass, float useBokeh) {
		push.useBokeh = useBokeh;
		NVR_EXT_RESOURCE_BINDING resources[3] = {};
		for (UINT i = 0; i < textureCount; i++) { resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i]; }
		resources[textureCount].Slot = 0; resources[textureCount].Type = NVR_EXT_BIND_SAMPLER; resources[textureCount].pResource = sampler;
		return ExtFullScreen(Device, RenderTarget, TheTextureManager->RenderedSurface, source, pass.Pipeline, resources, textureCount + 1, &push, sizeof(push), lastPass);
	};

	IDirect3DBaseTexture9* focusTextures[] = { depth, avgLuma };
	if (!draw(extFocus, focusTextures, 2, SourceBuffer, false, 0.0f)) return false;

	const bool bokeh = BokehWanted() && BokehReady() && BokehScatter(Device, depth);

	IDirect3DBaseTexture9* mosaicTextures[] = { TheTextureManager->RenderedTexture, TheTextureManager->SourceTexture };
	if (!draw(extMosaic, mosaicTextures, 2, nullptr, false, 0.0f)) return true;
	const float bokehPasses[] = { 1.0f, 1.0f, 0.0f };
	for (float useBokeh : bokehPasses) {
		IDirect3DBaseTexture9* bokehTextures[] = { TheTextureManager->RenderedTexture };
		if (!draw(extBokeh, bokehTextures, 1, nullptr, false, useBokeh)) return true;
	}
	IDirect3DBaseTexture9* combineTextures[] = { TheTextureManager->RenderedTexture, TheTextureManager->SourceTexture };
	if (bokeh) {
		push.useBokeh = 0.0f;
		push.pad[0] = bokehW; push.pad[1] = bokehH;
		NVR_EXT_RESOURCE_BINDING resources[] = {
			{ 0, NVR_EXT_BIND_SAMPLED_IMAGE,  combineTextures[0], 0, 0, 0, FALSE },
			{ 1, NVR_EXT_BIND_SAMPLED_IMAGE,  combineTextures[1], 0, 0, 0, FALSE },
			{ 2, NVR_EXT_BIND_SAMPLED_IMAGE,  bokehTexture,       0, 0, 0, FALSE },
			{ 0, NVR_EXT_BIND_SAMPLER,        sampler,            0, 0, 0, FALSE },
		};
		ExtFullScreen(Device, RenderTarget, TheTextureManager->RenderedSurface, nullptr, bokehCombine, resources, _countof(resources), &push, sizeof(push), true);
		return true;
	}
	draw(extCombine, combineTextures, 2, nullptr, true, 0.0f);
	return true;
}
