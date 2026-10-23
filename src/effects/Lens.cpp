#include "Lens.h"
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-23 (NVR DXVK)

void LensEffect::UpdateConstants() {
	Constants.Data.x = TheShaderManager->GetTransitionValue(Settings.Main.strength, Settings.Night.strength, Settings.Interiors.strength) * (!TheShaderManager->GameState.isUnderwater); // cancel out if underwater;
	Constants.Data.y = TheShaderManager->GetTransitionValue(Settings.Main.bloomExponent, Settings.Night.bloomExponent, Settings.Interiors.bloomExponent);
	Constants.Data.z = TheShaderManager->GetTransitionValue(Settings.Main.smudginess, Settings.Night.smudginess, Settings.Interiors.smudginess);
}

void LensEffect::UpdateSettings(){
	if (!Enabled) Constants.Data.x = 0; // cancel out if disabled

	Settings.Main.bloomExponent = TheSettingManager->GetSettingF("Shaders.Lens.Main", "BloomExponent");
	Settings.Main.strength = TheSettingManager->GetSettingF("Shaders.Lens.Main", "Strength");
	Settings.Main.smudginess = TheSettingManager->GetSettingF("Shaders.Lens.Main", "Smudginess");
	Settings.Night.bloomExponent = TheSettingManager->GetSettingF("Shaders.Lens.Night", "BloomExponent");
	Settings.Night.strength = TheSettingManager->GetSettingF("Shaders.Lens.Night", "Strength");
	Settings.Night.smudginess = TheSettingManager->GetSettingF("Shaders.Lens.Night", "Smudginess");
	Settings.Interiors.bloomExponent = TheSettingManager->GetSettingF("Shaders.Lens.Interiors", "BloomExponent");
	Settings.Interiors.strength = TheSettingManager->GetSettingF("Shaders.Lens.Interiors", "Strength");
	Settings.Interiors.smudginess = TheSettingManager->GetSettingF("Shaders.Lens.Interiors", "Smudginess");
}

void LensEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_LensData", &Constants.Data);
}

bool LensEffect::ShouldRender() { 
	return !TheShaderManager->GameState.isUnderwater && (Constants.Data.x > 0);
};

// UNOFFICIAL 2026-10-23 (NVR DXVK): Lens.frag; TESR_SourceBuffer (refreshed by ExtFullScreen), the first bloom buffer and the
// dirt file texture through the WRAP sampler.
bool LensEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	struct Push { D3DXVECTOR4 lensData; UInt32 samplerIndex; UInt32 wrapIndex; UInt32 pad[2]; } push = {};
	push.lensData = Constants.Data;
	IDirect3DBaseTexture9* bloom = TheShaderManager->Effects.Bloom ? TheShaderManager->Effects.Bloom->Textures.BloomTexture[0] : nullptr;
	IDirect3DBaseTexture9* textures[] = { TheTextureManager->SourceTexture, bloom, ExtFileTexture("TESR_LensSampler") };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "Lens", textures, _countof(textures), &push, sizeof(push), 16, false, 20);
}
