#include "SkyDither.h"

void SkyDitherEffect::UpdateSettings() {
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF("Shaders.SkyDither.Main", "Strength"), 0.0f, 4.0f);
	Settings.SkyOnly = TheSettingManager->GetSettingI("Shaders.SkyDither.Main", "SkyOnly") != 0;
}

void SkyDitherEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkyDitherData", &Constants.Data);
}

void SkyDitherEffect::UpdateConstants() {
	// A new noise pattern every frame: at the frame rate the eye averages it into a smooth gradient, where a still
	// pattern could itself be seen as a fine grain stuck to the screen.
	frame = (frame + 1) % 64;
	Constants.Data = D3DXVECTOR4(Settings.Strength / 255.0f, Settings.SkyOnly ? 1.0f : 0.0f, (float)frame, 0.0f);
}
