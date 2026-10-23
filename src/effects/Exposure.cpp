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
}

void ExposureEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_ExposureData", &Constants.Data);
}

// UNOFFICIAL 2026-10-23 (NVR DXVK): Exposure.frag; the same two textures and constant the technique reads.
bool ExposureEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	struct Push { D3DXVECTOR4 exposureData; UInt32 samplerIndex; UInt32 pad[3]; };
	if (!extTried) {
		extTried = true;
		static const NVR_EXT_BINDING_DESC bindings[] = {
			{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D, FALSE, 0 },		// t_rendered
			{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE, 1, NVR_EXT_VIEW_2D, FALSE, 0 },		// t_avgLuma
			{ 0, 0, NVR_EXT_BIND_SAMPLER,       0, NVR_EXT_VIEW_2D, FALSE, 16 },		// samplerIndex
		};
		extPipeline = DxvkCompute::CreateGraphicsPipeline("Exposure", bindings, _countof(bindings), sizeof(Push), true);
	}
	IDirect3DTexture9* avgLuma = TheShaderManager->Effects.AvgLuma ? TheShaderManager->Effects.AvgLuma->Textures.AvgLumaTexture : nullptr;
	ID3D9NvrExtSampler* sampler = DxvkCompute::LinearClamp();
	if (!extPipeline || !avgLuma || !sampler || !TheTextureManager->RenderedTexture) return false;
	Push push = {};
	push.exposureData = Constants.Data;
	NVR_EXT_RESOURCE_BINDING resources[] = {
		{ 0, NVR_EXT_BIND_SAMPLED_IMAGE, TheTextureManager->RenderedTexture, 0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_SAMPLED_IMAGE, avgLuma,                           0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_SAMPLER,       sampler,                           0, 0, 0, FALSE },
	};
	return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, extPipeline, resources, _countof(resources), &push, sizeof(push));
}

