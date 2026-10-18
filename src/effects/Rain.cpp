#include "Rain.h"

void RainEffect::UpdateConstants() {
	if (TheShaderManager->GameState.isRainy && Constants.RainAnimator.switched == false) {
		// it just started raining
		Constants.RainAnimator.switched = true;
		Constants.RainAnimator.Start(0.05f, 1);
	}
	else if (!TheShaderManager->GameState.isRainy && Constants.RainAnimator.switched) {
		// it just stopped raining
		Constants.RainAnimator.switched = false;
		Constants.RainAnimator.Start(0.07f, 0);
	}

	Constants.Data.x = Constants.RainAnimator.GetValue();
}

void RainEffect::UpdateSettings() {
	Constants.Data.y = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "VerticalScale");
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Speed");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Opacity");

	Constants.Aspect.x = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Refraction");
	Constants.Aspect.y = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Coloring");
	Constants.Aspect.z = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Bloom");
	// UNOFFICIAL 2026-10-17 (Precipitations.fx.hlsl): skip the rain layers behind each pixel (they add exactly 0). A key missing
	// from the user's settings file reads the shipped default (on); only a key missing from the DEFAULTS toml reads 0 = off.
	// Note (fixes-b #37): GetOrtho samples with tex2Dlod level 0 whether this option is on or off (identical unless a driver
	// anisotropic override acts in minification).
	Constants.Aspect.w = TheSettingManager->GetSettingI("Shaders.Precipitations.Main", "SkipHiddenLayers") != 0 ? 1.0f : 0.0f;
}

void RainEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_RainData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_RainAspect", &Constants.Aspect);
}

bool RainEffect::ShouldRender() {
	return Constants.Data.x > 0.0f &&
		TheShaderManager->GameState.isExterior &&
		!TheShaderManager->GameState.isUnderwater;
};