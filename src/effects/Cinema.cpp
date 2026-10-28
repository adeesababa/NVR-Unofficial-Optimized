#include "Cinema.h"
#include "../core/DxvkCompute.h"

void CinemaEffect::UpdateConstants() {

	Constants.Data.x = 1.0f; // set cinema aspect ratio to native ar
	if (Enabled) {
		// disable based on settings/context
		if ((Mode == 1 && (TheShaderManager->GameState.isDialog || TheShaderManager->GameState.isPersuasion)) ||
			(Mode == 2 && (!TheShaderManager->GameState.isDialog)) ||
			(Mode == 3 && (!TheShaderManager->GameState.isPersuasion)) ||
			(Mode == 4 && (!TheShaderManager->GameState.isDialog && !TheShaderManager->GameState.isPersuasion))) Constants.Data.x = 1.0f;
		else
			Constants.Data.x = aspectRatio;
	}

}

void CinemaEffect::UpdateSettings() {
	Mode = TheSettingManager->GetSettingI("Shaders.Cinema.Main", "Mode");
	aspectRatio = TheSettingManager->GetSettingF("Shaders.Cinema.Main", "AspectRatio");
	Constants.Data.y = TheSettingManager->GetSettingF("Shaders.Cinema.Main", "VignetteRadius");
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.Cinema.Main", "VignetteDarkness");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.Cinema.Main", "OverlayStrength");
	Constants.Settings.y = TheSettingManager->GetSettingF("Shaders.Cinema.Main", "FilmGrainAmount");
	Constants.Settings.z = TheSettingManager->GetSettingF("Shaders.Cinema.Main", "ChromaticAberration");
	Constants.Settings.w = TheSettingManager->GetSettingF("Shaders.Cinema.Main", "LetterBoxDepth");
}

void CinemaEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_CinemaData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_CinemaSettings", &Constants.Settings);
}

bool CinemaEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex > 1) return false;
	struct Push {
		D3DXVECTOR4 cinemaData, cinemaSettings, gameTime, reciprocalResolution, cameraData, skyDitherData;
		UInt32 samplerIndex, noiseSamplerIndex, dither, pad;
	} push = {};
	push.cinemaData = Constants.Data;
	push.cinemaSettings = Constants.Settings;
	push.gameTime = TheShaderManager->ShaderConst.GameTime;
	push.reciprocalResolution = TheShaderManager->ShaderConst.ReciprocalResolution;
	push.cameraData = TheRenderManager->CameraData;
	push.skyDitherData = TheShaderManager->Effects.SkyDither ? TheShaderManager->Effects.SkyDither->Constants.Data : D3DXVECTOR4(0, 0, 0, 0);
	push.dither = techniqueIndex;
	IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture,
		TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr,
		ExtFileTexture("TESR_BlueNoiseSampler") };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "Cinema", textures, _countof(textures), &push, sizeof(push), 96, false, 100, true);
}