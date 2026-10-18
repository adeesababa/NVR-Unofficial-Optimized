#include "BloodLens.h"

void BloodLensEffect::UpdateConstants() {
	// UNOFFICIAL fixes-b #45 (LOOK CHANGE, BloodLens is off by default): UpdateConstants runs twice a rendered frame, and the fade
	// counter used to step on both calls (the fade took half the frames the setting says). It steps once per rendered frame
	// (FrameNumber) now. Params.w (the shader's only strength input) was only set in UpdateSettings, so the fade never reached
	// the shader: it is set here, every call.
	static unsigned lastFrame = ~0u;
	const bool newFrame = TheFrameRateManager->FrameNumber != lastFrame;
	lastFrame = TheFrameRateManager->FrameNumber;

	if (TheShaderManager->GameState.isUnderwater) {
		Constants.Percent = 0.0f;
	}

	if (Constants.Percent > 0.0f) {
		Constants.Time.z = effectTime;
		bool started = false;
		if (Constants.Percent == 1.0f) {
			started = true;
			Constants.Time.w = 0.0f;
			srand(time(NULL));
			Constants.Params.x = (double)rand() / (RAND_MAX + 1) * (0.75f - 0.25f) + 0.25f; //from 0.25 to 0.75
			Constants.Params.y = (double)rand() / (RAND_MAX + 1) * (0.5f + 0.1f) - 0.1f; //from -0.1 to 0.5
			Constants.Params.z = (double)rand() / (RAND_MAX + 1) * (2.0f + 2.0f) - 2.0f; //from -2 to 2
		}
		if (newFrame || started) Constants.Time.w += 1.0f;
		Constants.Percent = 1.0f - Constants.Time.w / Constants.Time.z;
		if (Constants.Percent < 0.0f)
			Constants.Percent = 0.0f;
	}
	Constants.Params.w = intensity * Constants.Percent;
}

void BloodLensEffect::UpdateSettings() {
	effectTime = TheSettingManager->GetSettingF("Shaders.BloodLens.Main", "Time");
	intensity = TheSettingManager->GetSettingF("Shaders.BloodLens.Main", "Intensity");
	Constants.Params.w = intensity * Constants.Percent;
	Constants.BloodColor.x = TheSettingManager->GetSettingF("Shaders.BloodLens.Main", "ColorR");
	Constants.BloodColor.y = TheSettingManager->GetSettingF("Shaders.BloodLens.Main", "ColorG");
	Constants.BloodColor.z = TheSettingManager->GetSettingF("Shaders.BloodLens.Main", "ColorB");
}

void BloodLensEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_BloodLensParams", &Constants.Params);
	TheShaderManager->RegisterConstant("TESR_BloodLensColor", &Constants.BloodColor);
}

bool BloodLensEffect::ShouldRender() {
	return Constants.Percent > 0.0f; 
};