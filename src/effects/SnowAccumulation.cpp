#include "SnowAccumulation.h"
#include "../core/DxvkCompute.h"
#include "CoverMap.h"

void SnowAccumulationEffect::UpdateConstants() {

	// Snow Accumulation
	if (TheShaderManager->GameState.isSnow && !Constants.SnowAccumulationAnimator.switched) {
		// it just started snowing
		Constants.SnowAccumulationAnimator.switched = true;
		Constants.SnowAccumulationAnimator.Start(increaseRate, 1);
	}
	else if (!TheShaderManager->GameState.isSnow && Constants.SnowAccumulationAnimator.switched) {
		// it just stopped snowing
		Constants.SnowAccumulationAnimator.switched = false;
		Constants.SnowAccumulationAnimator.Start(decreaseRate, 0);
	}

	Constants.Data.w = Constants.SnowAccumulationAnimator.GetValue();
	if (Constants.Data.w) TheShaderManager->orthoRequired = true; // mark ortho map calculation as necessary
	else if (TheCoverMap && TheCoverMap->SnowActive()) TheShaderManager->orthoRequired = true;

}

void SnowAccumulationEffect::UpdateSettings(){
	increaseRate = TheSettingManager->GetSettingF("Shaders.SnowAccumulation.Main", "Increase");
	decreaseRate = TheSettingManager->GetSettingF("Shaders.SnowAccumulation.Main", "Decrease");

	Constants.Data.x = TheSettingManager->GetSettingF("Shaders.SnowAccumulation.Main", "BlurNormDropThreshhold");
	Constants.Data.y = TheSettingManager->GetSettingF("Shaders.SnowAccumulation.Main", "BlurRadiusMultiplier");
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.SnowAccumulation.Main", "SunPower");
	Constants.Color.x = TheSettingManager->GetSettingF("Shaders.SnowAccumulation.Main", "SnowColorR");
	Constants.Color.y = TheSettingManager->GetSettingF("Shaders.SnowAccumulation.Main", "SnowColorG");
	Constants.Color.z = TheSettingManager->GetSettingF("Shaders.SnowAccumulation.Main", "SnowColorB");
}

void SnowAccumulationEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SnowAccumulationParams", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SnowAccumulationColor", &Constants.Color);
}

bool SnowAccumulationEffect::ShouldRender() {
	return (Constants.Data.w > 0.0f || (TheCoverMap && TheCoverMap->SnowActive())) && TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater;
}

struct SnowAccumulationUniforms {
	D3DXMATRIX		shadowToLight;
	D3DXMATRIX		projection;
	D3DXMATRIX		invProjection;
	D3DXMATRIX		view;
	D3DXMATRIX		invView;
	D3DXVECTOR4		cameraData;
	D3DXVECTOR4		cameraPosition;
	D3DXVECTOR4		reciprocalResolution;
	D3DXVECTOR4		sunDirection;
	D3DXVECTOR4		sunColor;
	D3DXVECTOR4		sunAmbient;
	D3DXVECTOR4		waterSettings;
	D3DXVECTOR4		shadowFade;
	D3DXVECTOR4		shadowScreenSpaceData;
	D3DXVECTOR4		orthoData;
	D3DXVECTOR4		snowParams;
	D3DXVECTOR4		snowAccColor;
	D3DXVECTOR4		shadowLightPosition[12];
	D3DXVECTOR4		lightPosition[12];
	D3DXVECTOR4		lightColor[24];
};

struct SnowAccumulationPush {
	D3DXVECTOR2		offsetMask;
	UInt32			samplerClamp;
	UInt32			samplerWrap;
	UInt32			mode;
	UInt32			pad;
};

static const D3DXVECTOR4* SnowAccumulationRegistered(const char* name) {
	auto found = TheShaderManager->ConstantsTable.find(name);
	return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
}

static bool SnowAccumulationCopy(void* dst, const char* name, size_t bytes) {
	const D3DXVECTOR4* src = SnowAccumulationRegistered(name);
	if (!src) return false;
	memcpy(dst, src, bytes);
	return true;
}

bool SnowAccumulationEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0 || ClearRenderTarget) return false;
	if (!SourceBuffer || SourceBuffer == RenderTarget || SourceBuffer != TheTextureManager->SourceSurface || !TheTextureManager->SourceTexture || !TheTextureManager->RenderedTexture) return false;

	auto engineTexture = [](const char* name) -> IDirect3DBaseTexture9* {
		std::string n = name;
		return TheTextureManager->GetTextureByName(n);
	};
	IDirect3DBaseTexture9* depth = engineTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* ortho = engineTexture("TESR_OrthoMapBuffer");
	IDirect3DBaseTexture9* normals = engineTexture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* pointShadow = engineTexture("TESR_PointShadowBuffer");
	IDirect3DBaseTexture9* snowNormal = ExtFileTexture("TESR_SnowNormSampler");
	IDirect3DBaseTexture9* blueNoise = ExtFileTexture("TESR_BlueNoiseSampler");
	if (!depth || !ortho || !normals || !pointShadow || !snowNormal || !blueNoise) return false;

	ID3D9NvrExtSampler* clamp = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* wrap = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_WRAP);
	if (!clamp || !wrap) return false;

	if (!extPass.Tried) {
		extPass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[11] = {};
		UINT n = 0;
		for (UINT i = 0; i < 8; i++) { bindings[n].Set = 1; bindings[n].Binding = i; bindings[n].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[n].Slot = i; bindings[n].ViewType = NVR_EXT_VIEW_2D; n++; }
		bindings[n].Set = 2; bindings[n].Binding = 0; bindings[n].Type = NVR_EXT_BIND_UNIFORM_BUFFER; bindings[n].Slot = 0; n++;
		bindings[n].Type = NVR_EXT_BIND_SAMPLER; bindings[n].Slot = 0; bindings[n].PushOffset = 8; n++;
		bindings[n].Type = NVR_EXT_BIND_SAMPLER; bindings[n].Slot = 1; bindings[n].PushOffset = 12; n++;
		extPass.Pipeline = DxvkCompute::CreateGraphicsPipeline("SnowAccumulation", bindings, n, sizeof(SnowAccumulationPush), true);
	}
	if (!extPass.Pipeline) return false;

	SnowAccumulationUniforms uniforms = {};
	bool copied = SnowAccumulationCopy(&uniforms.shadowToLight, "TESR_ShadowCameraToLightTransformOrtho", sizeof(D3DXMATRIX))
		&& SnowAccumulationCopy(&uniforms.projection, "TESR_ProjectionTransform", sizeof(D3DXMATRIX))
		&& SnowAccumulationCopy(&uniforms.invProjection, "TESR_InvProjectionTransform", sizeof(D3DXMATRIX))
		&& SnowAccumulationCopy(&uniforms.view, "TESR_ViewTransform", sizeof(D3DXMATRIX))
		&& SnowAccumulationCopy(&uniforms.invView, "TESR_InvViewTransform", sizeof(D3DXMATRIX))
		&& SnowAccumulationCopy(&uniforms.cameraData, "TESR_CameraData", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.cameraPosition, "TESR_CameraPosition", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.reciprocalResolution, "TESR_ReciprocalResolution", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.sunDirection, "TESR_SunDirection", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.sunColor, "TESR_SunColor", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.sunAmbient, "TESR_SunAmbient", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.waterSettings, "TESR_WaterSettings", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.shadowFade, "TESR_ShadowFade", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.shadowScreenSpaceData, "TESR_ShadowScreenSpaceData", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(&uniforms.orthoData, "TESR_OrthoData", sizeof(D3DXVECTOR4))
		&& SnowAccumulationCopy(uniforms.shadowLightPosition, "TESR_ShadowLightPosition", sizeof(uniforms.shadowLightPosition))
		&& SnowAccumulationCopy(uniforms.lightPosition, "TESR_LightPosition", sizeof(uniforms.lightPosition))
		&& SnowAccumulationCopy(uniforms.lightColor, "TESR_LightColor", sizeof(uniforms.lightColor));
	if (!copied) return false;
	uniforms.snowParams = Constants.Data;
	uniforms.snowAccColor = Constants.Color;
	ID3D9NvrExtBuffer* constants = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	if (!constants) return false;

	IDirect3DBaseTexture9* textures[8] = { TheTextureManager->RenderedTexture, TheTextureManager->SourceTexture, depth, ortho, normals,
		pointShadow, snowNormal, blueNoise };
	auto drawPass = [&](UINT mode, D3DXVECTOR2 offsetMask, IDirect3DSurface9* source, bool last) {
		textures[0] = TheTextureManager->RenderedTexture;
		NVR_EXT_RESOURCE_BINDING resources[11] = {};
		UINT n = 0;
		for (UINT i = 0; i < 8; i++) { resources[n].Slot = i; resources[n].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[n].pResource = textures[i]; n++; }
		resources[n].Slot = 0; resources[n].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[n].pResource = constants; n++;
		resources[n].Slot = 0; resources[n].Type = NVR_EXT_BIND_SAMPLER; resources[n].pResource = clamp; n++;
		resources[n].Slot = 1; resources[n].Type = NVR_EXT_BIND_SAMPLER; resources[n].pResource = wrap; n++;
		SnowAccumulationPush push = {};
		push.offsetMask = offsetMask;
		push.mode = mode;
		return ExtFullScreen(Device, RenderTarget, TheTextureManager->RenderedSurface, source, extPass.Pipeline, resources, n, &push, sizeof(push), last);
	};

	const D3DXVECTOR2 noMask(0.0f, 0.0f);
	const D3DXVECTOR2 offsetMaskH(1.0f, 0.0f);
	const D3DXVECTOR2 offsetMaskV(0.0f, 1.0f);
	if (!drawPass(0, noMask, SourceBuffer, false)) return false;
	if (!drawPass(1, noMask, nullptr, false)) return true;
	if (!drawPass(2, offsetMaskH, nullptr, false)) return true;
	if (!drawPass(2, offsetMaskV, nullptr, false)) return true;
	drawPass(3, noMask, nullptr, true);
	return true;
}