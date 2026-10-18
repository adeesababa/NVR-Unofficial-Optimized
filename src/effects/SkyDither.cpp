#include "SkyDither.h"

void SkyDitherEffect::UpdateSettings() {
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF("Shaders.SkyDither.Main", "Strength"), 0.0f, 4.0f);
	Settings.SkyOnly = TheSettingManager->GetSettingI("Shaders.SkyDither.Main", "SkyOnly") != 0;
	Settings.Pattern = TheSettingManager->GetSettingI("Shaders.SkyDither.Main", "Pattern") != 0 ? 1 : 0;
}

void SkyDitherEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkyDitherData", &Constants.Data);
}

void SkyDitherEffect::UpdateConstants() {
	// A new noise pattern every frame: at the frame rate the eye averages it into a smooth gradient, where a still
	// pattern could itself be seen as a fine grain stuck to the screen.
	// UNOFFICIAL aastab: Pattern 1 keeps one pattern (frame 0): nothing for the anti-aliasing to twinkle on with a still camera.
	// fixes-a #35: the end dither takes the fixed frame 32, the tonemap dither (Tonemapping.cpp) frame 0: with both on frame 0 they were
	// the same noise field and added up in the sky to about 2 steps instead of 1.
	frame = Settings.Pattern == 1 ? 32 : (frame + 1) % 64;
	Constants.Data = D3DXVECTOR4(Settings.Strength / 255.0f, Settings.SkyOnly ? 1.0f : 0.0f, (float)frame, 0.0f);
}
