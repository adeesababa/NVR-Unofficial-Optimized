#include "Specular.h"

void SpecularEffect::UpdateConstants() {
	const float rain = TheShaderManager->Effects.Rain->Constants.RainAnimator.GetValue();
	const LookStruct& dry = Settings.Exterior;
	const LookStruct& wet = Settings.Rain;
	Constants.Data.x = std::lerp(dry.SunStrength, wet.SunStrength, rain);
	Constants.Data.y = std::lerp(dry.Roughness, wet.Roughness, rain);
	Constants.Data.z = std::lerp(dry.SkyReflections, wet.SkyReflections, rain);
	Constants.Data.w = std::lerp(dry.DistanceFade, wet.DistanceFade, rain);
	Constants.EffectStrength.x = std::lerp(dry.SpecularAA, wet.SpecularAA, rain);
	Constants.EffectStrength.y = std::lerp(dry.Reflectance, wet.Reflectance, rain);
	Constants.EffectStrength.z = 0.0f;
	Constants.EffectStrength.w = 0.0f;
}

void SpecularEffect::UpdateSettings() {
	auto read = [](const char* section, LookStruct& look) {
		look.SunStrength = std::clamp(TheSettingManager->GetSettingF(section, "SunStrength"), 0.0f, 4.0f);
		look.Roughness = std::clamp(TheSettingManager->GetSettingF(section, "Roughness"), 0.05f, 1.0f);
		look.SkyReflections = std::clamp(TheSettingManager->GetSettingF(section, "SkyReflections"), 0.0f, 4.0f);
		look.DistanceFade = std::clamp(TheSettingManager->GetSettingF(section, "DistanceFade"), 500.0f, 300000.0f);
		look.SpecularAA = std::clamp(TheSettingManager->GetSettingF(section, "SpecularAA"), 0.0f, 1.0f);
		look.Reflectance = std::clamp(TheSettingManager->GetSettingF(section, "Reflectance"), 0.0f, 0.2f);
	};
	read("Shaders.Specular.Exterior", Settings.Exterior);
	read("Shaders.Specular.Rain", Settings.Rain);
}

void SpecularEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SpecularData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SpecularEffects", &Constants.EffectStrength);
}

void SpecularEffect::RegisterTextures() {
	const UINT w = (TheRenderManager->width + 3) / 4, h = (TheRenderManager->height + 3) / 4;
	TheTextureManager->InitTexture("TESR_SpecularNormals", &Textures.NormalsTexture, &Textures.NormalsSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SpecularBuffer", &Textures.LightTexture, &Textures.LightSurface, w, h, D3DFMT_A16B16G16R16F);
}

bool SpecularEffect::ShouldRender() {
	return TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater;
};

bool SpecularEffect::CanTakeBounce() {
	return Enabled && Effect && Effect->GetTechniqueByName("SpecularBounce") && Effect->GetParameterByName(NULL, "NVR_BounceData") &&
		Effect->GetParameterByName(NULL, "NVR_BounceLayout");
}

bool SpecularEffect::RenderWithBounce(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	IDirect3DTexture9* bounce, const D3DXVECTOR4& data, const D3DXVECTOR4& layout) {
	if (!Enabled || !Effect || !ShouldRender() || !bounce) return false;
	D3DXHANDLE technique = Effect->GetTechniqueByName("SpecularBounce");
	D3DXHANDLE dataHandle = Effect->GetParameterByName(NULL, "NVR_BounceData");
	D3DXHANDLE layoutHandle = Effect->GetParameterByName(NULL, "NVR_BounceLayout");
	FrameChain& chain = TheShaderManager->Chain;
	if (!technique || !dataHandle || !layoutHandle || !chain.Owns(RenderTarget, RenderedSurface)) return false;
	auto timer = TimeLogger();
	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(dataHandle, &data);
	Effect->SetVector(layoutHandle, &layout);
	UINT passes = 0;
	HRESULT result = Device->SetRenderTarget(0, chain.Output());
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		result = Effect->BeginPass(0);
		if (SUCCEEDED(result)) {
			RebindSlotTextures();
			Device->SetTexture(8, bounce);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Effect->EndPass();
		}
		Effect->End();
	}
	Device->SetTexture(8, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	renderTime = timer.LogTime("EffectRecord::Render Specular (with bounce light)");
	if (FAILED(result)) return false;
	chain.Commit();
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL merged combine active: bounce light and specular in one pass.");
		reported = true;
	}
	return true;
}
