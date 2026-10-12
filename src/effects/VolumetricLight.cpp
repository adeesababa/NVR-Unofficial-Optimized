#include "VolumetricLight.h"

void VolumetricLightEffect::UpdateSettings() {
	const char* section = "Shaders.VolumetricLight.Main";
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF(section, "Strength"), 0.0f, 10.0f);
	Settings.Darken = std::clamp(TheSettingManager->GetSettingF(section, "Darken"), 0.0f, 1.0f);
	Settings.Density = std::clamp(TheSettingManager->GetSettingF(section, "Density"), 0.0f, 10.0f);
	Settings.MaxDistance = std::clamp(TheSettingManager->GetSettingF(section, "MaxDistance"), 100.0f, 30000.0f);
	Settings.Steps = std::clamp(TheSettingManager->GetSettingI(section, "Steps"), 4, 48);
	Settings.Anisotropy = std::clamp(TheSettingManager->GetSettingF(section, "Anisotropy"), 0.0f, 0.9f);
	Settings.NightStrength = std::clamp(TheSettingManager->GetSettingF(section, "NightStrength"), 0.0f, 1.0f);
	Settings.RainStrength = std::clamp(TheSettingManager->GetSettingF(section, "RainStrength"), 0.0f, 1.0f);
}

void VolumetricLightEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_VolumetricLightData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_VolumetricLightData2", &Constants.Data2);
	TheShaderManager->RegisterConstant("TESR_VolumetricLightLayout", &Constants.Layout);
}

void VolumetricLightEffect::RegisterTextures() {
	const UINT w = (TheRenderManager->width + 1) / 2, h = (TheRenderManager->height + 1) / 2;
	TheTextureManager->InitTexture("TESR_VolumetricLightMarch", &Textures.MarchTexture, &Textures.MarchSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_VolumetricLightBuffer", &Textures.BlurTexture, &Textures.BlurSurface, w, h, D3DFMT_A16B16G16R16F);
	Constants.Layout = D3DXVECTOR4(1.0f / w, 1.0f / h, 0.0f, 0.0f);
}

void VolumetricLightEffect::UpdateConstants() {
	const ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	float fade = 1.0f - std::clamp(shadows->Constants.ShadowFade.x, 0.0f, 1.0f);
	fade *= std::lerp(Settings.NightStrength, 1.0f, std::clamp(TheShaderManager->GameState.isDayTime, 0.0f, 1.0f));
	fade *= std::lerp(1.0f, Settings.RainStrength, std::clamp(TheShaderManager->Effects.Rain->Constants.RainAnimator.GetValue(), 0.0f, 1.0f));

	Constants.Data.x = Settings.Strength;
	Constants.Data.y = Settings.Darken;
	Constants.Data.z = Settings.Density * 0.001f;
	Constants.Data.w = Settings.MaxDistance;
	Constants.Data2.x = (float)Settings.Steps;
	Constants.Data2.y = Settings.Anisotropy;
	Constants.Data2.z = fade;
	Constants.Data2.w = 0.0f;
}

bool VolumetricLightEffect::ShouldRender() {
	const ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	return TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater &&
		shadows && shadows->Constants.ShadowFade.y > 0.0f &&
		Constants.Data2.z > 0.001f && (Settings.Strength > 0.0f || Settings.Darken > 0.0f) &&
		Textures.MarchSurface && Textures.BlurSurface;
}
