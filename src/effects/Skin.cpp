#include <algorithm>

#include "Skin.h"

void SkinShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkinData", &Constants.SkinData);
	TheShaderManager->RegisterConstant("TESR_SkinColor", &Constants.SkinColor);
	TheShaderManager->RegisterConstant("TESR_SkinDiffusion", &Constants.SkinDiffusion);
	TheShaderManager->RegisterConstant("TESR_SkinLightData", &NewConstants.LightData);
	TheShaderManager->RegisterConstant("TESR_SkinLightExtra", &NewConstants.LightExtra);
	TheShaderManager->RegisterConstant("TESR_SkinDebugData", &NewConstants.DebugData);
}

void SkinShaders::UpdateConstants() {
	if (!NewSkinEnabled()) return;
	const bool isExterior = TheShaderManager->GameState.isExterior;
	const MaterialStruct& Material = isExterior ? Exterior : Interior;
	NewConstants.LightData.x = Material.SpecularStrength;
	NewConstants.LightData.y = Material.Roughness;
	NewConstants.LightData.z = Material.PerPixelWidth;
	NewConstants.LightData.w = Material.Translucency;
	NewConstants.LightExtra.z = Material.ShadowScatter;
	{
		const float F0 = 0.028f, Lobe0 = 0.75f, Lobe1 = 1.3f, Mix = 0.15f;
		const float a0 = std::clamp(Material.Roughness * Lobe0, 0.04f, 1.0f), a1 = std::clamp(Material.Roughness * Lobe1, 0.04f, 1.0f);
		const float alpha0 = a0 * a0, alpha1 = a1 * a1;
		const float peak = F0 / (4.0f * alpha0 * alpha0) * (1.0f - Mix) + F0 / (4.0f * alpha1 * alpha1) * Mix;
		const float matched = std::clamp(1.0f / (2.0f * peak), 1.0f, 12.0f);
		const float weight = std::clamp(Material.VanillaMatchedHighlights, 0.0f, 1.0f);
		NewConstants.LightExtra.w = 1.0f + (matched - 1.0f) * weight;
	}
	NewConstants.LightExtra.x = isExterior ? SkyReflectionScale : 0.0f;
	const float rainFactor = (std::max)(TheShaderManager->Effects.WetWorld->Constants.Data.x, TheShaderManager->Effects.WetWorld->Constants.Data.z);
	NewConstants.LightExtra.y = isExterior ? std::clamp(rainFactor * RainWetness, 0.0f, 1.0f) : 0.0f;
	NewConstants.DebugData.x = DebugView < FirstScatteringDebugView ? (float)DebugView : 0.0f;
	NewConstants.DebugData.y = isExterior ? 0.0f : 1.0f;
	NewConstants.DebugData.z = Material.FillLight;
}

static void ReadSkinMaterial(SkinShaders::MaterialStruct* Material, const char* Section, const char* ScatteringSection) {
	Material->SpecularStrength = (std::max)(0.0f, TheSettingManager->GetSettingF(Section, "SpecularStrength"));
	Material->Roughness = std::clamp(TheSettingManager->GetSettingF(Section, "Roughness"), 0.1f, 1.0f);
	Material->VanillaMatchedHighlights = std::clamp(TheSettingManager->GetSettingF(Section, "VanillaMatchedHighlights"), 0.0f, 1.0f);
	Material->ShadowScatter = (std::max)(0.0f, TheSettingManager->GetSettingF(Section, "ShadowScatter"));
	Material->Translucency = (std::max)(0.0f, TheSettingManager->GetSettingF(Section, "Translucency"));
	Material->PerPixelWidth = (std::max)(0.0f, TheSettingManager->GetSettingF(ScatteringSection, "PerPixelWidth"));
	Material->FillLight = std::clamp(TheSettingManager->GetSettingF(Section, "FillLight"), 0.0f, 2.0f);
}

void SkinShaders::UpdateSettings() {
	Constants.SkinData.x = TheSettingManager->GetSettingF("Shaders.Skin.Main", "Attenuation");
	Constants.SkinData.y = TheSettingManager->GetSettingF("Shaders.Skin.Main", "SpecularPower");
	Constants.SkinData.z = TheSettingManager->GetSettingF("Shaders.Skin.Main", "MaterialThickness");
	Constants.SkinData.w = TheSettingManager->GetSettingF("Shaders.Skin.Main", "RimScalar");

	Constants.SkinColor.x = TheSettingManager->GetSettingF("Shaders.Skin.Main", "CoeffRed");
	Constants.SkinColor.y = TheSettingManager->GetSettingF("Shaders.Skin.Main", "CoeffGreen");
	Constants.SkinColor.z = TheSettingManager->GetSettingF("Shaders.Skin.Main", "CoeffBlue");

	Constants.SkinDiffusion.x = (std::max)(0.0f, (std::min)(1.0f, TheSettingManager->GetSettingF("Shaders.Skin.Main", "PerPixelDiffusion")));
	Constants.SkinDiffusion.y = (std::max)(0.0f, (std::min)(2.0f, TheSettingManager->GetSettingF("Shaders.Skin.Main", "Translucency")));

	ReadSkinMaterial(&Exterior, "Shaders.Skin.NewSkin", "Shaders.Skin.Scattering");
	ReadSkinMaterial(&Interior, "Shaders.Skin.Interiors", "Shaders.Skin.Interiors");
	SkyReflectionScale = (std::max)(0.0f, TheSettingManager->GetSettingF("Shaders.Skin.NewSkin", "SkyReflectionScale"));
	RainWetness = std::clamp(TheSettingManager->GetSettingF("Shaders.Skin.NewSkin", "RainWetness"), 0.0f, 1.0f);
	DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.Skin.Debug", "DebugView"), 0, DebugViewCount);
	BindFaceGenMaps = TheSettingManager->GetSettingI("Shaders.Skin.Debug", "BindFaceGenMaps") != 0;
	MuteGameHighlights = TheSettingManager->GetSettingI("Shaders.Skin.NewSkin", "MuteGameHighlights") != 0;
}