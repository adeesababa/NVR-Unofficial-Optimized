#include "SkyDither.h"

void SkyDitherEffect::UpdateSettings() {
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF("Shaders.SkyDither.Main", "Strength"), 0.0f, 4.0f);
	Settings.SkyOnly = TheSettingManager->GetSettingI("Shaders.SkyDither.Main", "SkyOnly") != 0;
}

void SkyDitherEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkyDitherData", &Constants.Data);
}

void SkyDitherEffect::UpdateConstants() {
	frame = (frame + 1) % 64;
	Constants.Data = D3DXVECTOR4(Settings.Strength / 255.0f, Settings.SkyOnly ? 1.0f : 0.0f, (float)frame, 0.0f);
}
