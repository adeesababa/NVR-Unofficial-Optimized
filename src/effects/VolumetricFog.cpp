#include "VolumetricFog.h"

void VolumetricFogEffect::UpdateConstants() {}

void VolumetricFogEffect::UpdateSettings(){

	char SettingCategory[50] = "Shaders.VolumetricFog.";
	
	if (TheShaderManager->GameState.isExterior) 
		strcat(SettingCategory, "Main");
	else
		strcat(SettingCategory, "Interiors");

	Constants.Data.x = TheSettingManager->GetSettingF(SettingCategory, "MinimumBaseFog");
	//Constants.Data.y = TheSettingManager->GetSettingF(SettingCategory, "ColorCoeff");
	//Constants.Data.w = TheSettingManager->GetSettingF(SettingCategory, "MaxDistance");
	Constants.Data.z = TheSettingManager->GetSettingF(SettingCategory, "Amount");

	Constants.LowFog.x = TheSettingManager->GetSettingF(SettingCategory, "FogSaturation");
	Constants.LowFog.y = TheSettingManager->GetSettingF(SettingCategory, "WeatherImpact");

	Constants.HighFog.x = TheSettingManager->GetSettingF(SettingCategory, "HeightFogDensity");
	Constants.HighFog.y = TheSettingManager->GetSettingF(SettingCategory, "HeightFogFalloff");
	Constants.HighFog.z = TheSettingManager->GetSettingF(SettingCategory, "HeightFogDist");

	Constants.Height.y = TheSettingManager->GetSettingF(SettingCategory, "HeightFogHeight");
	Constants.Height.z = TheSettingManager->GetSettingF(SettingCategory, "SimpleFogHeight");
	Constants.Height.w = TheShaderManager->GameState.isExterior ? 1.0f : 0.0f;

	Constants.SimpleFog.x = TheSettingManager->GetSettingF(SettingCategory, "Extinction");
	Constants.SimpleFog.y = TheSettingManager->GetSettingF(SettingCategory, "Inscattering");

	Constants.Blend.y = TheSettingManager->GetSettingF(SettingCategory, "HeightFogBlend");
	Constants.Blend.z = TheSettingManager->GetSettingF(SettingCategory, "HeightFogRolloff");
	Constants.Blend.w = TheSettingManager->GetSettingF(SettingCategory, "SimpleFogBlend");

	if (TheShaderManager->GameState.isExterior) {
		Constants.Height.x = TheSettingManager->GetSettingF(SettingCategory, "DistantFogHeight");
		Constants.LowFog.z = TheSettingManager->GetSettingF(SettingCategory, "DistantFogRange");
		Constants.Blend.x = TheSettingManager->GetSettingF(SettingCategory, "DistantFogBlend");

		Constants.LowFog.w = TheSettingManager->GetSettingF(SettingCategory, "SunPower");
		Constants.SimpleFog.z = TheSettingManager->GetSettingF(SettingCategory, "FogNight");
		Constants.SimpleFog.w = TheSettingManager->GetSettingF(SettingCategory, "SimpleFogSkyColor");
		Constants.HighFog.w = TheSettingManager->GetSettingF(SettingCategory, "HeightFogSkyColor");
	}
	else {
		// these settings don't do anything in interiors
		Constants.Height.x = 0.0;
		Constants.LowFog.z = 0.0;
		Constants.Blend.x = 0.0;

		Constants.LowFog.w = 0.0;
		Constants.SimpleFog.z = 1.0;
		Constants.SimpleFog.w = 0.0;
		Constants.HighFog.w = 0.0;
	}


}

void VolumetricFogEffect::RegisterConstants(){
	TheShaderManager->RegisterConstant("TESR_VolumetricFogLow", &Constants.LowFog);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogHigh", &Constants.HighFog);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogSimple", &Constants.SimpleFog);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogBlend", &Constants.Blend);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogHeight", &Constants.Height);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogData", &Constants.Data);
}


bool VolumetricFogEffect::ShouldRender()
{
	return !TheShaderManager->GameState.isUnderwater;
};


bool VolumetricFogEffect::CanComposite() {
	return !compositeFailed && TheSettingManager->SettingsMain.Main.CompositeFog && Enabled && Effect && ShouldRender() &&
		Effect->GetTechniqueByName("CompositeFog") && Effect->GetParameterByName(NULL, "NVR_CompositeFlags") &&
		TheShaderManager->Chain.IsActive();
}

void VolumetricFogEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (!compositeShadow && !compositeAO) {
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }

	FrameChain& chain = TheShaderManager->Chain;
	D3DXHANDLE technique = Effect->GetTechniqueByName("CompositeFog");
	D3DXHANDLE flagsHandle = Effect->GetParameterByName(NULL, "NVR_CompositeFlags");
	if (compositeFailed || !technique || !flagsHandle || (compositeAO && !compositeAOTexture) || !chain.Owns(RenderTarget, RenderedSurface))
		return;

	auto timer = TimeLogger();
	D3DXVECTOR4 flags(compositeShadow ? 1.0f : 0.0f, compositeAO ? 1.0f : 0.0f, 0.0f, 0.0f);
	HRESULT result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) {
		SetCT();
		result = Effect->SetVector(flagsHandle, &flags);
	}
	UINT passes = 0;
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		Device->SetTexture(5, nullptr);
		result = Device->SetRenderTarget(0, chain.Output());
		if (SUCCEEDED(result)) result = Effect->BeginPass(0);
		if (SUCCEEDED(result)) {
			RebindSlotTextures();
			Device->SetTexture(5, compositeAO ? compositeAOTexture : nullptr);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	Device->SetTexture(5, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	if (FAILED(result)) {
		compositeFailed = true;
		Logger::Log("Composite fog failed (%08lx); shadows, AO and fog use their own passes until restart.", result);
		return;
	}
	chain.Commit();
	compositeApplied = true;

	static bool reported = false;
	if (!reported) {
		Logger::Log("Composite fog active: the sun-shadow composite and the AO combine are applied in the fog pass.");
		reported = true;
	}
	renderTime = timer.LogTime("VolumetricFog::Composite");
}