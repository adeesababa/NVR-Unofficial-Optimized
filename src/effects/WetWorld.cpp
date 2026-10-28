#include "WetWorld.h"
#include "../core/DxvkCompute.h"
#include "CoverMap.h"

void WetWorldEffect::UpdateConstants() {
	// Rain fall & puddles
	if (TheShaderManager->GameState.isRainy && Constants.PuddlesAnimator.switched == false) {
		// it just started raining
		Constants.PuddlesAnimator.switched = true;
		Constants.PuddlesAnimator.Start(increaseRate, 1);
	}
	else if (!TheShaderManager->GameState.isRainy && Constants.PuddlesAnimator.switched) {
		// it just stopped raining
		Constants.PuddlesAnimator.switched = false;
		Constants.PuddlesAnimator.Start(decreaseRate, 0);
	}
	Constants.Data.x = TheShaderManager->Effects.Rain->Constants.RainAnimator.GetValue();
	Constants.Data.y = TheShaderManager->GameState.isRainy;
	Constants.Data.z = Constants.PuddlesAnimator.GetValue();

	if (Constants.Data.x || Constants.Data.z) TheShaderManager->orthoRequired = true; // mark ortho map calculation as necessary
	else if (TheCoverMap && TheCoverMap->WetActive()) TheShaderManager->orthoRequired = true;
}

void WetWorldEffect::UpdateSettings() {
	increaseRate = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Increase");
	decreaseRate = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Decrease");

	Constants.Coeffs.x = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_R");
	Constants.Coeffs.y = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_G");
	Constants.Coeffs.z = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_B");
	Constants.Coeffs.w = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleSpecularMultiplier");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Amount");
	Constants.Look.x = TheSettingManager->GetSettingI("Shaders.WetWorld.Main", "KeepHighlights") ? 1.0f : 0.0f;
	fastPaths = TheSettingManager->GetSettingI("Shaders.WetWorld.Main", "FastPaths") != 0;
	fixedTrace = TheSettingManager->GetSettingI("Shaders.WetWorld.Main", "FixedReflectionTrace") != 0;
}

void WetWorldEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_WetWorldCoeffs", &Constants.Coeffs);
	TheShaderManager->RegisterConstant("TESR_WetWorldData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_WetWorldLook", &Constants.Look);
}

void WetWorldEffect::RegisterTextures() {
	TheTextureManager->InitTexture("NVR_WetWorldCover", &coverTexture, &coverSurface, 1, 1, D3DFMT_A8R8G8B8);
}

void WetWorldEffect::RenderCoverTest(IDirect3DDevice9* Device) {
	if (!Enabled || !Effect || !coverSurface) return;
	if (!coverQuery && FAILED(Device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &coverQuery))) { coverQuery = nullptr; return; }
	if (coverPending) {
		DWORD pixels = 0;
		if (coverQuery->GetData(&pixels, sizeof(pixels), 0) != S_OK) return;
		covered = pixels > 0;
		coverPending = false;
	}
	DWORD zEnable = 0, zWrite = 0;
	Device->GetRenderState(D3DRS_ZENABLE, &zEnable);
	Device->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
	Device->SetRenderState(D3DRS_ZENABLE, FALSE);
	Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	coverQuery->Issue(D3DISSUE_BEGIN);
	Device->SetRenderTarget(0, coverSurface);
	Render(Device, coverSurface, coverSurface, 1, false, nullptr);
	coverQuery->Issue(D3DISSUE_END);
	coverPending = true;
	Device->SetRenderState(D3DRS_ZENABLE, zEnable);
	Device->SetRenderState(D3DRS_ZWRITEENABLE, zWrite);
}

bool WetWorldEffect::ShouldRender() {
	return (Constants.Data.x || Constants.Data.z || (TheCoverMap && TheCoverMap->WetActive())) && TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater;
};

struct WetWorldUniforms {
	D3DXVECTOR4 shadowToLight[4];
	D3DXVECTOR4 projection[4];
	D3DXVECTOR4 invProjection[4];
	D3DXVECTOR4 view[4];
	D3DXVECTOR4 invView[4];
	D3DXVECTOR4 cameraData;
	D3DXVECTOR4 depthConstants;
	D3DXVECTOR4 cameraPosition;
	D3DXVECTOR4 reciprocalResolution;
	D3DXVECTOR4 gameTime;
	D3DXVECTOR4 sunDirection;
	D3DXVECTOR4 sunPosition;
	D3DXVECTOR4 sunAmount;
	D3DXVECTOR4 sunColor;
	D3DXVECTOR4 sunAmbient;
	D3DXVECTOR4 skyData;
	D3DXVECTOR4 skyColor;
	D3DXVECTOR4 skyLowColor;
	D3DXVECTOR4 horizonColor;
	D3DXVECTOR4 orthoData;
	D3DXVECTOR4 waterSettings;
	D3DXVECTOR4 wetWorldData;
	D3DXVECTOR4 wetWorldLook;
	D3DXVECTOR4 lightPosition[12];
	D3DXVECTOR4 shadowLightPosition[12];
	D3DXVECTOR4 lightColor[24];
};
static_assert(sizeof(WetWorldUniforms) == 1376, "WetWorldUniforms must match the std140 block");

struct WetWorldPush { UInt32 samplers[4]; UInt32 mode; UInt32 pad[3]; };

static const D3DXVECTOR4* WetRegisteredConstant(const char* name) {
	auto found = TheShaderManager->ConstantsTable.find(name);
	return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
}

static bool WetCopyMatrix(D3DXVECTOR4* dst, const char* name) {
	const D3DXVECTOR4* src = WetRegisteredConstant(name);
	if (!src) return false;
	D3DXMATRIX m;
	D3DXMatrixTranspose(&m, (const D3DXMATRIX*)src);
	memcpy(dst, &m, sizeof(m));
	return true;
}

static bool WetCopyConstant(D3DXVECTOR4* dst, const char* name, UINT count) {
	const D3DXVECTOR4* src = WetRegisteredConstant(name);
	if (!src) return false;
	memcpy(dst, src, count * sizeof(D3DXVECTOR4));
	return true;
}

static UINT WetBindings(NVR_EXT_BINDING_DESC* out, UINT textures, UINT samplers) {
	UINT n = 0;
	for (UINT i = 0; i < textures; i++) {
		out[n].Set = 1; out[n].Binding = i; out[n].Type = NVR_EXT_BIND_SAMPLED_IMAGE; out[n].Slot = i; out[n].ViewType = NVR_EXT_VIEW_2D; n++;
	}
	out[n].Set = 2; out[n].Binding = 0; out[n].Type = NVR_EXT_BIND_UNIFORM_BUFFER; out[n].Slot = 0; n++;
	for (UINT j = 0; j < samplers; j++) {
		out[n].Type = NVR_EXT_BIND_SAMPLER; out[n].Slot = j; out[n].PushOffset = 4 * j; n++;
	}
	return n;
}

bool WetWorldEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (ClearRenderTarget || (techniqueIndex != 2 && techniqueIndex != 3 && techniqueIndex != 4)) return false;
	if (!SourceBuffer || SourceBuffer == RenderTarget || SourceBuffer != TheTextureManager->SourceSurface || !TheTextureManager->SourceTexture) return false;
	const bool fast = techniqueIndex != 3;
	const bool fixTrace = techniqueIndex >= 3;

	auto engineTexture = [](const char* name) -> IDirect3DBaseTexture9* {
		std::string n = name;
		return TheTextureManager->GetTextureByName(n);
	};
	IDirect3DBaseTexture9* depth = engineTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* depthViewModel = engineTexture("TESR_DepthBufferViewModel");
	IDirect3DBaseTexture9* ortho = engineTexture("TESR_OrthoMapBuffer");
	IDirect3DBaseTexture9* normals = engineTexture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* pointShadow = engineTexture("TESR_PointShadowBuffer");
	IDirect3DBaseTexture9* ripple = ExtFileTexture("TESR_RippleSampler");
	IDirect3DBaseTexture9* splash = ExtFileTexture("TESR_SplashSampler");
	IDirect3DBaseTexture9* splashNormal = ExtFileTexture("TESR_SplashNormalSampler");
	IDirect3DBaseTexture9* cloud = ExtFileTexture("TESR_PuddleCloudSampler");
	IDirect3DBaseTexture9* puddleNormal = ExtFileTexture("TESR_PuddleNormalSampler");
	IDirect3DBaseTexture9* streak = ExtFileTexture("TESR_WaterStreakSampler");
	if (!depth || !depthViewModel || !ortho || !normals || !pointShadow || !ripple || !splash || !splashNormal || !cloud || !puddleNormal || !streak) return false;

	ID3D9NvrExtSampler* linearClamp = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* pointClamp = DxvkCompute::PointClamp();
	ID3D9NvrExtSampler* mipWrap = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_WRAP);
	ID3D9NvrExtSampler* mipClamp = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_CLAMP);
	if (!linearClamp || !pointClamp || !mipWrap || !mipClamp) return false;

	if (!extMap.Tried) {
		extMap.Tried = true;
		NVR_EXT_BINDING_DESC bindings[8] = {};
		const UINT count = WetBindings(bindings, 5, 2);
		extMap.Pipeline = DxvkCompute::CreateGraphicsPipeline("WetWorldMap", bindings, count, sizeof(WetWorldPush), true);
	}
	if (!extWet.Tried) {
		extWet.Tried = true;
		NVR_EXT_BINDING_DESC bindings[20] = {};
		const UINT count = WetBindings(bindings, 13, 4);
		extWet.Pipeline = DxvkCompute::CreateGraphicsPipeline("WetWorld", bindings, count, sizeof(WetWorldPush), true);
	}
	if (!extMap.Pipeline || !extWet.Pipeline) return false;

	WetWorldUniforms uniforms = {};
	bool copied = WetCopyMatrix(uniforms.shadowToLight, "TESR_ShadowCameraToLightTransformOrtho")
		&& WetCopyMatrix(uniforms.projection, "TESR_ProjectionTransform")
		&& WetCopyMatrix(uniforms.invProjection, "TESR_InvProjectionTransform")
		&& WetCopyMatrix(uniforms.view, "TESR_ViewTransform")
		&& WetCopyMatrix(uniforms.invView, "TESR_InvViewTransform")
		&& WetCopyConstant(&uniforms.cameraData, "TESR_CameraData", 1)
		&& WetCopyConstant(&uniforms.depthConstants, "TESR_DepthConstants", 1)
		&& WetCopyConstant(&uniforms.cameraPosition, "TESR_CameraPosition", 1)
		&& WetCopyConstant(&uniforms.reciprocalResolution, "TESR_ReciprocalResolution", 1)
		&& WetCopyConstant(&uniforms.gameTime, "TESR_GameTime", 1)
		&& WetCopyConstant(&uniforms.sunDirection, "TESR_SunDirection", 1)
		&& WetCopyConstant(&uniforms.sunPosition, "TESR_SunPosition", 1)
		&& WetCopyConstant(&uniforms.sunAmount, "TESR_SunAmount", 1)
		&& WetCopyConstant(&uniforms.sunColor, "TESR_SunColor", 1)
		&& WetCopyConstant(&uniforms.sunAmbient, "TESR_SunAmbient", 1)
		&& WetCopyConstant(&uniforms.skyData, "TESR_SkyData", 1)
		&& WetCopyConstant(&uniforms.skyColor, "TESR_SkyColor", 1)
		&& WetCopyConstant(&uniforms.skyLowColor, "TESR_SkyLowColor", 1)
		&& WetCopyConstant(&uniforms.horizonColor, "TESR_HorizonColor", 1)
		&& WetCopyConstant(&uniforms.orthoData, "TESR_OrthoData", 1)
		&& WetCopyConstant(&uniforms.waterSettings, "TESR_WaterSettings", 1)
		&& WetCopyConstant(&uniforms.wetWorldData, "TESR_WetWorldData", 1)
		&& WetCopyConstant(&uniforms.wetWorldLook, "TESR_WetWorldLook", 1)
		&& WetCopyConstant(uniforms.lightPosition, "TESR_LightPosition", 12)
		&& WetCopyConstant(uniforms.shadowLightPosition, "TESR_ShadowLightPosition", 12)
		&& WetCopyConstant(uniforms.lightColor, "TESR_LightColor", 24);
	if (!copied) return false;
	ID3D9NvrExtBuffer* constants = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	if (!constants) return false;

	auto drawPass = [&](ExtPass& pass, IDirect3DBaseTexture9* const* textures, UINT textureCount, ID3D9NvrExtSampler* const* samplers,
		UINT samplerCount, UINT mode, IDirect3DSurface9* surface, IDirect3DSurface9* source, bool last) {
		NVR_EXT_RESOURCE_BINDING resources[20] = {};
		UINT n = 0;
		for (UINT i = 0; i < textureCount; i++) {
			resources[n].Slot = i; resources[n].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[n].pResource = textures[i]; n++;
		}
		resources[n].Slot = 0; resources[n].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[n].pResource = constants; n++;
		for (UINT j = 0; j < samplerCount; j++) {
			resources[n].Slot = j; resources[n].Type = NVR_EXT_BIND_SAMPLER; resources[n].pResource = samplers[j]; n++;
		}
		WetWorldPush push = {};
		push.mode = mode;
		return ExtFullScreen(Device, RenderTarget, surface, source, pass.Pipeline, resources, n, &push, sizeof(push), last);
	};

	IDirect3DBaseTexture9* mapTextures[5] = { TheTextureManager->RenderedTexture, depth, depthViewModel, ortho, normals };
	ID3D9NvrExtSampler* mapSamplers[2] = { linearClamp, pointClamp };
	if (!drawPass(extMap, mapTextures, 5, mapSamplers, 2, 0, RenderedSurface, SourceBuffer, false)) return false;

	for (UINT blur = 1; blur <= 2; blur++) {
		mapTextures[0] = TheTextureManager->RenderedTexture;
		if (!drawPass(extMap, mapTextures, 5, mapSamplers, 2, blur, TheTextureManager->RenderedSurface, nullptr, false)) return true;
	}

	IDirect3DBaseTexture9* wetTextures[13] = { TheTextureManager->RenderedTexture, TheTextureManager->SourceTexture, depth, depthViewModel,
		ortho, normals, pointShadow, ripple, splash, splashNormal, cloud, puddleNormal, streak };
	ID3D9NvrExtSampler* wetSamplers[4] = { linearClamp, pointClamp, mipWrap, mipClamp };
	const UINT wetMode = (fast ? 1u : 0u) | (fixTrace ? 2u : 0u);
	drawPass(extWet, wetTextures, 13, wetSamplers, 4, wetMode, TheTextureManager->RenderedSurface, nullptr, true);
	return true;
}