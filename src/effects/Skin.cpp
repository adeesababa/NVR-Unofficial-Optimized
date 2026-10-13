#include "Skin.h"

void SkinShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkinData", &Constants.SkinData);
	TheShaderManager->RegisterConstant("TESR_SkinColor", &Constants.SkinColor);
	TheShaderManager->RegisterConstant("TESR_SkinDiffusion", &Constants.SkinDiffusion);
}

void SkinShaders::UpdateConstants() {}

void SkinShaders::UpdateSettings() {
	Constants.SkinData.x = TheSettingManager->GetSettingF("Shaders.Skin.Main", "Attenuation");
	Constants.SkinData.y = TheSettingManager->GetSettingF("Shaders.Skin.Main", "SpecularPower");
	Constants.SkinData.z = TheSettingManager->GetSettingF("Shaders.Skin.Main", "MaterialThickness");
	Constants.SkinData.w = TheSettingManager->GetSettingF("Shaders.Skin.Main", "RimScalar");

	Constants.SkinColor.x = TheSettingManager->GetSettingF("Shaders.Skin.Main", "CoeffRed");
	Constants.SkinColor.y = TheSettingManager->GetSettingF("Shaders.Skin.Main", "CoeffGreen");
	Constants.SkinColor.z = TheSettingManager->GetSettingF("Shaders.Skin.Main", "CoeffBlue");

	// UNOFFICIAL: per-pixel sun diffusion (Includes\Skin.hlsl GetSunDiffusion). x: PerPixelDiffusion 0-1, y: Translucency 0-2.
	Constants.SkinDiffusion.x = (std::max)(0.0f, (std::min)(1.0f, TheSettingManager->GetSettingF("Shaders.Skin.Main", "PerPixelDiffusion")));
	Constants.SkinDiffusion.y = (std::max)(0.0f, (std::min)(2.0f, TheSettingManager->GetSettingF("Shaders.Skin.Main", "Translucency")));
}