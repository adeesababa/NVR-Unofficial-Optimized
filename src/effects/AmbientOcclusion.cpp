#include "AmbientOcclusion.h"

void AmbientOcclusionEffect::UpdateConstants() {
}

void AmbientOcclusionEffect::RegisterConstants() {
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionAOData"] = &Constants.AOData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionData"] = &Constants.Data;
}

void AmbientOcclusionEffect::RegisterTextures() {
	TheTextureManager->InitTexture("NVR_AOBuffer0", &aoTexture[0], &aoSurface[0], TheRenderManager->width, TheRenderManager->height, D3DFMT_G16R16F);
	TheTextureManager->InitTexture("NVR_AOBuffer1", &aoTexture[1], &aoSurface[1], TheRenderManager->width, TheRenderManager->height, D3DFMT_G16R16F);
}

void AmbientOcclusionEffect::UpdateSettings() {
	const char* sectionName = TheShaderManager->GameState.isExterior?"Shaders.AmbientOcclusion.Exteriors":"Shaders.AmbientOcclusion.Interiors";

	Constants.Enabled = TheSettingManager->GetSettingI(sectionName, "Enabled");
	Constants.AOData.x = TheSettingManager->GetSettingF(sectionName, "Samples");
	Constants.AOData.y = TheSettingManager->GetSettingF(sectionName, "StrengthMultiplier");
	Constants.AOData.z = TheSettingManager->GetSettingF(sectionName, "ClampStrength");
	Constants.AOData.w = TheSettingManager->GetSettingF(sectionName, "Range");
	Constants.Data.x = TheSettingManager->GetSettingF(sectionName, "AngleBias");
	Constants.Data.y = TheSettingManager->GetSettingF(sectionName, "LumThreshold");
	Constants.Data.z = TheSettingManager->GetSettingF(sectionName, "BlurDropThreshold");
	Constants.Data.w = TheSettingManager->GetSettingF(sectionName, "BlurRadiusMultiplier");
}

bool AmbientOcclusionEffect::ShouldRender() {
	return Constants.Enabled && !bNVAOLoaded;
}

bool AmbientOcclusionEffect::CanDedicate() {
	return !dedicatedFailed && TheSettingManager->SettingsMain.Main.DedicatedAO && Effect &&
		aoTexture[0] && aoTexture[1] && aoSurface[0] && aoSurface[1] &&
		Effect->GetTechniqueByName("DedicatedAO") && TheShaderManager->Chain.IsActive();
}

void AmbientOcclusionEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }

	FrameChain& chain = TheShaderManager->Chain;
	D3DXHANDLE technique = CanDedicate() ? Effect->GetTechniqueByName("DedicatedAO") : NULL;
	D3DXTECHNIQUE_DESC description = {};
	D3DSURFACE_DESC target = {}, ao = {};
	const bool usable = technique && SUCCEEDED(Effect->GetTechniqueDesc(technique, &description)) && description.Passes == 4 &&
		RenderedSurface && SUCCEEDED(RenderTarget->GetDesc(&target)) && SUCCEEDED(aoSurface[0]->GetDesc(&ao)) &&
		ao.Width == target.Width && ao.Height == target.Height && chain.Owns(RenderTarget, RenderedSurface);
	if (!usable) {
		if (deferCombine) return;
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}

	auto timer = TimeLogger();
	const UINT first = combineOnly ? 3 : 0;
	const UINT last = deferCombine ? 2 : 3;
	IDirect3DSurface9* destinations[3] = { aoSurface[0], aoSurface[1], aoSurface[0] };
	IDirect3DTexture9* inputs[4] = { nullptr, aoTexture[0], aoTexture[1], aoTexture[0] };
	HRESULT result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) SetCT();
	UINT passes = 0;
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		for (UINT p = first; p <= last && SUCCEEDED(result); ++p) {
			Device->SetTexture(5, nullptr);
			result = Device->SetRenderTarget(0, p == 3 ? chain.Output() : destinations[p]);
			if (FAILED(result)) break;
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			RebindSlotTextures();
			if (inputs[p]) Device->SetTexture(5, inputs[p]);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	Device->SetTexture(5, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	if (FAILED(result)) {
		dedicatedFailed = true;
		Logger::Log("Dedicated ambient occlusion failed (%08lx); using the original passes until restart.", result);
		if (!deferCombine) EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	if (last == 3) chain.Commit();
	else deferredReady = true;

	static bool reported = false;
	if (!reported) {
		Logger::Log("Dedicated ambient occlusion active: full resolution, two G16R16F targets, one HDR pass, no frame copy.");
		reported = true;
	}
	renderTime = timer.LogTime("AmbientOcclusion::Dedicated");
}
