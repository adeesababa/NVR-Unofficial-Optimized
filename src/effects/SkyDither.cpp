#include "SkyDither.h"
#include "../core/DxvkCompute.h"

void SkyDitherEffect::UpdateSettings() {
	Settings.Strength = std::clamp(TheSettingManager->GetSettingF("Shaders.SkyDither.Main", "Strength"), 0.0f, 4.0f);
	Settings.SkyOnly = TheSettingManager->GetSettingI("Shaders.SkyDither.Main", "SkyOnly") != 0;
	Settings.Pattern = TheSettingManager->GetSettingI("Shaders.SkyDither.Main", "Pattern") != 0 ? 1 : 0;
}

void SkyDitherEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SkyDitherData", &Constants.Data);
}

void SkyDitherEffect::UpdateConstants() {
	frame = Settings.Pattern == 1 ? 32 : (frame + 1) % 64;
	Constants.Data = D3DXVECTOR4(Settings.Strength / 255.0f, Settings.SkyOnly ? 1.0f : 0.0f, (float)frame, 0.0f);
}

bool SkyDitherEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	struct Push { D3DXVECTOR4 skyDitherData; UInt32 samplerIndex; UInt32 pad[3]; } push = {};
	push.skyDitherData = Constants.Data;
	IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture, TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "SkyDither", textures, _countof(textures), &push, sizeof(push), 16, true);
}

