#include "LowHF.h"
#include "../core/DxvkCompute.h"

void LowHFEffect::UpdateConstants() {
	float PlayerHealthPercent = (float)Player->GetActorValue(Actor::ActorVal::kActorVal_Health) / (float)Player->GetBaseActorValue(Actor::ActorVal::kActorVal_Health);
	float PlayerFatiguePercent = (float)Player->GetActorValue(Actor::ActorVal::kActorVal_Stamina) / (float)Player->GetBaseActorValue(Actor::ActorVal::kActorVal_Stamina);

	Constants.Data.x = 0.0f;
	Constants.Data.y = 0.0f;
	Constants.Data.z = 0.0f;
	Constants.Data.w = 0.0f;

	float healthLimit = TheSettingManager->GetSettingF("Shaders.LowHF.Main", "HealthLimit");
	if (Player->IsAlive()) {
		Constants.HealthCoeff = 1.0f - PlayerHealthPercent / healthLimit;
		Constants.FatigueCoeff = 1.0f - PlayerFatiguePercent / Settings.FatigueLimit;
		if (PlayerHealthPercent < healthLimit) {
			Constants.Data.x = Constants.HealthCoeff * Settings.LumaMultiplier;
			Constants.Data.y = Constants.HealthCoeff * 0.01f * Settings.BlurMultiplier;
			Constants.Data.z = Constants.HealthCoeff * 20.0f * Settings.VignetteMultiplier;
			Constants.Data.w = (1.0f - Constants.HealthCoeff) * Settings.DarknessMultiplier;
		}
		if (!Constants.Data.x && PlayerFatiguePercent < Settings.FatigueLimit)
			Constants.Data.x = Constants.FatigueCoeff * Settings.LumaMultiplier;
	}
}

void LowHFEffect::UpdateSettings() {
	Settings.healthLimit = TheSettingManager->GetSettingF("Shaders.LowHF.Main", "HealthLimit");
	Settings.FatigueLimit = TheSettingManager->GetSettingF("Shaders.LowHF.Main", "FatigueLimit");
	Settings.LumaMultiplier = TheSettingManager->GetSettingF("Shaders.LowHF.Main", "LumaMultiplier");
	Settings.BlurMultiplier = TheSettingManager->GetSettingF("Shaders.LowHF.Main", "BlurMultiplier");
	Settings.VignetteMultiplier = TheSettingManager->GetSettingF("Shaders.LowHF.Main", "VignetteMultiplier");
	Settings.DarknessMultiplier = TheSettingManager->GetSettingF("Shaders.LowHF.Main", "DarknessMultiplier");
}

void LowHFEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_LowHFData", &Constants.Data);
}

bool LowHFEffect::ShouldRender() {
	return Constants.Data.x; 
};

bool LowHFEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	struct Push { D3DXVECTOR4 lowHFData; UInt32 samplerIndex; UInt32 pad[3]; } push = {};
	push.lowHFData = Constants.Data;
	IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "LowHF", textures, _countof(textures), &push, sizeof(push), 16);
}

