#include "Exposure.h"
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-23 (NVR DXVK)

void ExposureEffect::UpdateConstants() {
	TheShaderManager->avglumaRequired = true; // mark average luma calculation as necessary

	if (TheSettingManager->SettingsChanged || TheShaderManager->GameState.isDayTimeChanged) {
		Constants.Data.x = TheShaderManager->GetTransitionValue(Settings.Main.MinBrightness, Settings.Night.MinBrightness, Settings.Interiors.MinBrightness);
		Constants.Data.y = TheShaderManager->GetTransitionValue(Settings.Main.MaxBrightness, Settings.Night.MaxBrightness, Settings.Interiors.MaxBrightness);
		Constants.Data.z = TheShaderManager->GetTransitionValue(Settings.Main.DarkAdaptSpeed, Settings.Night.DarkAdaptSpeed, Settings.Interiors.DarkAdaptSpeed);
		Constants.Data.w = TheShaderManager->GetTransitionValue(Settings.Main.LightAdaptSpeed, Settings.Night.LightAdaptSpeed, Settings.Interiors.LightAdaptSpeed);
	}
}

void ExposureEffect::UpdateSettings(){
	Settings.Main.MinBrightness = TheSettingManager->GetSettingF("Shaders.Exposure.Main", "MinBrightness");
	Settings.Main.MaxBrightness = TheSettingManager->GetSettingF("Shaders.Exposure.Main", "MaxBrightness");
	Settings.Main.DarkAdaptSpeed = TheSettingManager->GetSettingF("Shaders.Exposure.Main", "DarkAdaptSpeed");
	Settings.Main.LightAdaptSpeed = TheSettingManager->GetSettingF("Shaders.Exposure.Main", "LightAdaptSpeed");
													
	Settings.Night.MinBrightness = TheSettingManager->GetSettingF("Shaders.Exposure.Night", "MinBrightness");
	Settings.Night.MaxBrightness = TheSettingManager->GetSettingF("Shaders.Exposure.Night", "MaxBrightness");
	Settings.Night.DarkAdaptSpeed = TheSettingManager->GetSettingF("Shaders.Exposure.Night", "DarkAdaptSpeed");
	Settings.Night.LightAdaptSpeed = TheSettingManager->GetSettingF("Shaders.Exposure.Night", "LightAdaptSpeed");
													
	Settings.Interiors.MinBrightness = TheSettingManager->GetSettingF("Shaders.Exposure.Interiors", "MinBrightness");
	Settings.Interiors.MaxBrightness = TheSettingManager->GetSettingF("Shaders.Exposure.Interiors", "MaxBrightness");
	Settings.Interiors.DarkAdaptSpeed = TheSettingManager->GetSettingF("Shaders.Exposure.Interiors", "DarkAdaptSpeed");
	Settings.Interiors.LightAdaptSpeed = TheSettingManager->GetSettingF("Shaders.Exposure.Interiors", "LightAdaptSpeed");

	// UNOFFICIAL 2026-10-26 (NVR DXVK, fork-only): see Exposure.h HistogramStruct.
	Histogram.Histogram = TheSettingManager->GetSettingI("Shaders.Exposure.Histogram", "Histogram") != 0;
	Histogram.LowPercent = max(0.0f, min(TheSettingManager->GetSettingF("Shaders.Exposure.Histogram", "LowPercent"), 49.0f));
	Histogram.HighPercent = max(0.0f, min(TheSettingManager->GetSettingF("Shaders.Exposure.Histogram", "HighPercent"), 49.0f));
}

void ExposureEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_ExposureData", &Constants.Data);
}

// UNOFFICIAL 2026-10-23 (NVR DXVK): Exposure.frag; the textures and constants the technique reads (EffectRecord::ExtTexturePass).
bool ExposureEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	struct Push { D3DXVECTOR4 exposureData; UInt32 samplerIndex; UInt32 pad[3]; } push = {};
	push.exposureData = Constants.Data;
	IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture, TheShaderManager->Effects.AvgLuma ? TheShaderManager->Effects.AvgLuma->Textures.AvgLumaTexture : nullptr };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "Exposure", textures, _countof(textures), &push, sizeof(push), 16);
}

