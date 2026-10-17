#include "WetWorld.h"

void WetWorldEffect::UpdateConstants() {
	// Rain fall & puddles
	if (TheShaderManager->GameState.isRainy && Constants.PuddlesAnimator.switched == false) {
		// it just started raining
		Constants.PuddlesAnimator.switched = true;
		Constants.PuddlesAnimator.Start(increaseRate, 1);
	}
	else if (!TheShaderManager->GameState.isRainy && Constants.PuddlesAnimator.switched) {
		// it just stopped raining
		Constants.PuddlesAnimator.switched = false;
		Constants.PuddlesAnimator.Start(decreaseRate, 0);
	}
	Constants.Data.x = TheShaderManager->Effects.Rain->Constants.RainAnimator.GetValue();
	Constants.Data.y = TheShaderManager->GameState.isRainy;
	Constants.Data.z = Constants.PuddlesAnimator.GetValue();

	if (Constants.Data.x || Constants.Data.z) TheShaderManager->orthoRequired = true; // mark ortho map calculation as necessary
}

void WetWorldEffect::UpdateSettings() {
	increaseRate = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Increase");
	decreaseRate = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Decrease");

	Constants.Coeffs.x = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_R");
	Constants.Coeffs.y = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_G");
	Constants.Coeffs.z = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleCoeff_B");
	Constants.Coeffs.w = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "PuddleSpecularMultiplier");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "Amount");
	// UNOFFICIAL 2026-10-16 KeepHighlights: the wet pass writes every pixel in rain, the sky included; it clamped them to 1.0 before
	// the tonemapper (sun, bright sky and lit wet highlights lost their roll-off). On: clamped only below 0.
	Constants.Look.x = TheSettingManager->GetSettingI("Shaders.WetWorld.Main", "KeepHighlights") ? 1.0f : 0.0f;
	fastPaths = TheSettingManager->GetSettingI("Shaders.WetWorld.Main", "FastPaths") != 0;
}

void WetWorldEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_WetWorldCoeffs", &Constants.Coeffs);
	TheShaderManager->RegisterConstant("TESR_WetWorldData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_WetWorldLook", &Constants.Look);
}

void WetWorldEffect::RegisterTextures() {
	TheTextureManager->InitTexture("NVR_WetWorldCover", &coverTexture, &coverSurface, 1, 1, D3DFMT_A8R8G8B8);
}

// UNOFFICIAL HudDrops: the cover test, once a frame while it rains outdoors (after the ortho map is drawn). The previous
// test's answer is read first if it is in; a new one is issued only once it is.
void WetWorldEffect::RenderCoverTest(IDirect3DDevice9* Device) {
	if (!Enabled || !Effect || !coverSurface) return;
	if (!coverQuery && FAILED(Device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &coverQuery))) { coverQuery = nullptr; return; }
	if (coverPending) {
		DWORD pixels = 0;
		if (coverQuery->GetData(&pixels, sizeof(pixels), 0) != S_OK) return;	// not in yet
		covered = pixels > 0;
		coverPending = false;
	}
	DWORD zEnable = 0, zWrite = 0;
	Device->GetRenderState(D3DRS_ZENABLE, &zEnable);
	Device->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
	Device->SetRenderState(D3DRS_ZENABLE, FALSE);
	Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	coverQuery->Issue(D3DISSUE_BEGIN);
	Device->SetRenderTarget(0, coverSurface);
	Render(Device, coverSurface, coverSurface, 1, false, nullptr);
	coverQuery->Issue(D3DISSUE_END);
	coverPending = true;
	Device->SetRenderState(D3DRS_ZENABLE, zEnable);
	Device->SetRenderState(D3DRS_ZWRITEENABLE, zWrite);
}

bool WetWorldEffect::ShouldRender() { 
	return (Constants.Data.x || Constants.Data.z) && TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater;
};