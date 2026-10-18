#include "AmbientOcclusion.h"
#include "../core/GpuProfiler.h"

static D3DXVECTOR4 GTAOData(0.0f, 0.0f, 0.0f, 64.0f);
static EffectRecord::CachedHandle techGTAO;
static D3DXVECTOR4 SkyData(0.0f, 1.0f, 600.0f, 0.0001f);
static bool SkyEnabled = false;
static D3DXVECTOR4 SkyData2(0.0f, 1.0f, 0.0f, 0.0f);
static D3DXVECTOR4 SkyData3(0.0f, 0.0f, 1.0f, 0.0f);
static D3DXVECTOR4 SkyData4(800.0f, 40.0f, 0.0f, 0.0f);
static float SkyAmbientScale = 1.0f;
static int SkyVersion = 2;

static float LinearLuma(const D3DXVECTOR4& c) {
	return 0.2126f * powf(max(c.x, 0.0f), 2.2f) + 0.7152f * powf(max(c.y, 0.0f), 2.2f) + 0.0722f * powf(max(c.z, 0.0f), 2.2f);
}

bool AmbientOcclusionEffect::SkyV2Active() { return SkyData2.x > 0.0f; }
static bool FastPaths = false;
static EffectRecord::CachedHandle techFast, techGTAOFast;

void AmbientOcclusionEffect::UpdateConstants() {
	const bool sky = SkyEnabled && TheShaderManager->GameState.isExterior && Constants.Enabled;
	SkyData.x = sky && SkyVersion == 1 ? 1.0f : 0.0f;
	SkyData2.x = sky && SkyVersion == 2 ? 1.0f : 0.0f;
	SkyData2.z = LinearLuma(TheShaderManager->ShaderConst.sunAmbient) * SkyAmbientScale;
	SkyData2.w = LinearLuma(TheShaderManager->ShaderConst.sunColor);
	if (SkyData.x > 0.0f) TheShaderManager->orthoRequired = true;
	if (SkyData2.x > 0.0f) TheShaderManager->orthoSkyRequired = true;
	static bool skyWasActive = false;
	TheShaderManager->orthoSkyFresh = SkyData2.x > 0.0f && !skyWasActive;
	skyWasActive = SkyData2.x > 0.0f;
}

void AmbientOcclusionEffect::RegisterConstants() {
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionAOData"] = &Constants.AOData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionData"] = &Constants.Data;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionGTAO"] = &GTAOData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky"] = &SkyData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky2"] = &SkyData2;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky3"] = &SkyData3;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky4"] = &SkyData4;
}

void AmbientOcclusionEffect::RegisterTextures() {
	const int width = MaxQuality::Reduced(TheRenderManager->width, 2);
	const int height = MaxQuality::Reduced(TheRenderManager->height, 2);
	TheTextureManager->InitTexture("NVR_AOBuffer0", &aoTexture[0], &aoSurface[0], width, height, D3DFMT_G16R16F);
	TheTextureManager->InitTexture("NVR_AOBuffer1", &aoTexture[1], &aoSurface[1], width, height, D3DFMT_G16R16F);
	const int lowWidth = MaxQuality::Reduced(TheRenderManager->width, 4);
	const int lowHeight = MaxQuality::Reduced(TheRenderManager->height, 4);
	TheTextureManager->InitTexture("NVR_AOBufferLow0", &aoTextureLow[0], &aoSurfaceLow[0], lowWidth, lowHeight, D3DFMT_G16R16F);
	TheTextureManager->InitTexture("NVR_AOBufferLow1", &aoTextureLow[1], &aoSurfaceLow[1], lowWidth, lowHeight, D3DFMT_G16R16F);
}

bool AmbientOcclusionEffect::UseLowRes() const {
	return TheSettingManager->SettingsMain.Main.AOLowRes &&
		aoTextureLow[0] && aoTextureLow[1] && aoSurfaceLow[0] && aoSurfaceLow[1];
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
	GTAOData.x = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.GTAO", "Enabled") ? 1.0f : 0.0f;
	GTAOData.w = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.GTAO", "MaxRadius"), 4.0f, 256.0f);
	SkyEnabled = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "Enabled");
	FastPaths = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.FastPaths", "Enabled") != 0;
	SkyData.y = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "Strength"), 0.0f, 1.0f);
	SkyData.z = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "Distance"), 64.0f, 4000.0f);
	SkyVersion = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "Version") == 1 ? 1 : 2;
	SkyData2.y = SkyData.y;
	SkyData3.w = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "DebugView"), 0, 4);
	SkyAmbientScale = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "AmbientScale"), 0.0f, 10.0f);
	SkyData3.z = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "ShareApply") ? 1.0f : 0.0f;
	SkyData3.x = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "DepthNormals") ? 1.0f : 0.0f;
	SkyData3.y = 1.0f / std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "OpenThreshold"), 0.5f, 1.0f);
	SkyData4.x = SkyData.z;
	SkyData4.y = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "SoftWidth"), 1.0f, 500.0f);
}

void AmbientOcclusionEffect::RenderSkyVisibility(IDirect3DDevice9* Device) {
	IDirect3DTexture9* orthoMap = TheShaderManager->Effects.ShadowsExteriors ? TheShaderManager->Effects.ShadowsExteriors->ShadowMapOrthoTexture : nullptr;
	IDirect3DTexture9* sunMask = TheShaderManager->Effects.ShadowsExteriors && TheShaderManager->Effects.ShadowsExteriors->Enabled ?
		TheShaderManager->Effects.ShadowsExteriors->Textures.ShadowPassTexture : nullptr;
	D3DXHANDLE technique = TechniqueHandle(techSky, "SkyVisibilityV2");
	static bool createTried[2] = { false, false };
	const int which = lastLowRes ? 1 : 0;
	if (technique && orthoMap && !createTried[which]) {
		createTried[which] = true;
		if (lastLowRes)
			TheTextureManager->InitTexture("NVR_SkyVisBufferLow", &skyTextureLow, &skySurfaceLow, MaxQuality::Reduced(TheRenderManager->width, 4), MaxQuality::Reduced(TheRenderManager->height, 4), D3DFMT_G16R16F);
		else
			TheTextureManager->InitTexture("NVR_SkyVisBuffer", &skyTexture, &skySurface, MaxQuality::Reduced(TheRenderManager->width, 2), MaxQuality::Reduced(TheRenderManager->height, 2), D3DFMT_G16R16F);
	}
	IDirect3DSurface9* target = lastLowRes ? skySurfaceLow : skySurface;
	D3DSURFACE_DESC desc = {};
	if (!technique || !orthoMap || !target || FAILED(target->GetDesc(&desc))) return;
	static GpuTimer skyTimer("  Sky visibility v2");
	GpuProfileScope gpu(skyTimer, Device);
	D3DVIEWPORT9 original = {};
	Device->GetViewport(&original);
	D3DVIEWPORT9 quarter = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
	SkyData4.z = sunMask ? 1.0f : 0.0f;
	Effect->SetTechnique(technique);
	SetCT();
	UINT passes = 0;
	IDirect3DSurface9* depthSurface = nullptr;
	Device->GetDepthStencilSurface(&depthSurface);
	Device->SetDepthStencilSurface(nullptr);
	HRESULT result = Device->SetRenderTarget(0, target);
	if (SUCCEEDED(result)) result = Device->SetViewport(&quarter);
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		result = Effect->BeginPass(0);
		if (SUCCEEDED(result)) {
			Device->SetTexture(6, orthoMap);
			Device->SetTexture(8, sunMask);
			TheRenderManager->renderState->NoteDeviceTexture(6, orthoMap);
			TheRenderManager->renderState->NoteDeviceTexture(8, sunMask);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Effect->EndPass();
		}
		Effect->End();
	}
	Device->SetTexture(6, nullptr);
	Device->SetTexture(8, nullptr);
	TheRenderManager->renderState->NoteDeviceTexture(6, nullptr);
	TheRenderManager->renderState->NoteDeviceTexture(8, nullptr);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	skyReady = SUCCEEDED(result);
	static bool reported = false;
	if (skyReady && !reported) {
		Logger::Log("UNOFFICIAL Skylighting v2 active: sky factor %ux%u G16R16F (sun mask %s), applied to the sky's share.", desc.Width, desc.Height, sunMask ? "yes" : "no");
		reported = true;
	}
}

bool AmbientOcclusionEffect::ShouldRender() {
	return Constants.Enabled && !bNVAOLoaded;
}

void AmbientOcclusionEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
	IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }
	if (!combineOnly) lastLowRes = UseLowRes();
	if (!combineOnly && SkyData2.x > 0.0f) {
		RenderSkyVisibility(Device);
		Device->SetRenderTarget(0, RenderTarget);
	}
	IDirect3DTexture9* const* textures = lastLowRes ? aoTextureLow : aoTexture;
	IDirect3DSurface9* const* surfaces = lastLowRes ? aoSurfaceLow : aoSurface;
	const UINT divisor = MaxQuality::Divisor(lastLowRes ? 4 : 2);
	D3DXHANDLE technique = TechniqueHandle(techDedicated, "DedicatedAO");
	if (FastPaths) {
		D3DXHANDLE fast = TechniqueHandle(techFast, "DedicatedAOFast");
		if (fast) technique = fast;
	}
	if (GTAOData.x > 0.5f) {
		D3DXHANDLE gtao = FastPaths ? TechniqueHandle(techGTAOFast, "DedicatedGTAOFast") : NULL;
		if (!gtao) gtao = TechniqueHandle(techGTAO, "DedicatedGTAO");
		if (gtao) technique = gtao;
	}
	D3DXHANDLE layoutHandle = ParameterHandle(paramLayout, "NVR_AOLayout");
	D3DXTECHNIQUE_DESC description = {};
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC target = {}, scratch = {}, aoTarget = {};
	IDirect3DSurface9* depthSurface = nullptr;
	if (packedAOFailed || !technique || !layoutHandle || FAILED(Effect->GetTechniqueDesc(technique, &description)) ||
		description.Passes != 4 || !RenderedSurface || !SourceBuffer || !textures[0] || !textures[1] ||
		!surfaces[0] || !surfaces[1] ||
		FAILED(Device->GetViewport(&original)) || FAILED(RenderTarget->GetDesc(&target)) ||
		FAILED(RenderedSurface->GetDesc(&scratch)) || FAILED(surfaces[0]->GetDesc(&aoTarget)) ||
		original.X || original.Y ||
		original.Width != target.Width || original.Height != target.Height ||
		scratch.Width != target.Width || scratch.Height != target.Height ||
		aoTarget.Width != (target.Width + divisor - 1) / divisor || aoTarget.Height != (target.Height + divisor - 1) / divisor ||
		aoTarget.Format != D3DFMT_G16R16F ||
		target.MultiSampleType != D3DMULTISAMPLE_NONE) {
		if (deferCombine) return;
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	auto timer = TimeLogger();
	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);
	IDirect3DSurface9* finalTarget = chained ? chain.Output() : RenderTarget;
	D3DVIEWPORT9 reduced = original;
	reduced.Width = aoTarget.Width;
	reduced.Height = aoTarget.Height;
	D3DXVECTOR4 layout((float)reduced.Width / original.Width,
		(float)reduced.Height / original.Height, 1.0f / reduced.Width, 1.0f / reduced.Height);
	IDirect3DTexture9* scene = TheTextureManager->RenderedTexture;
	HRESULT result = scene ? Device->GetDepthStencilSurface(&depthSurface) : E_FAIL;
	if (SUCCEEDED(result) && !ReShadeIntegration::KeepDepthBound()) result = Device->SetDepthStencilSurface(nullptr);
	if (FAILED(result)) {
		if (depthSurface) depthSurface->Release();
		return;
	}
	IDirect3DTexture9* skyVis = !deferCombine ? SkyTexture() : nullptr;
	SkyData4.w = skyVis ? 1.0f : 0.0f;
	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	IDirect3DTexture9* orthoMap = SkyData.x > 0.0f && TheShaderManager->Effects.ShadowsExteriors ? TheShaderManager->Effects.ShadowsExteriors->ShadowMapOrthoTexture : nullptr;
	Device->SetTexture(6, orthoMap);
	TheRenderManager->renderState->NoteDeviceTexture(6, orthoMap);
	UINT passes = 0;
	result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		const UINT firstPass = combineOnly && passes ? passes - 1 : 0;
		const UINT endPass = deferCombine && passes ? passes - 1 : passes;
		for (UINT p = firstPass; p < endPass && SUCCEEDED(result); ++p) {
			const bool combine = p == passes - 1;
			Device->SetTexture(5, nullptr);
			IDirect3DSurface9* destination = combine ? finalTarget : surfaces[p == 1 ? 1 : 0];
			result = Device->SetRenderTarget(0, destination);
			if (SUCCEEDED(result)) result = Device->SetViewport(combine ? &original : &reduced);
			if (FAILED(result)) break;
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			IDirect3DBaseTexture9* stage5 = p == 1 ? textures[0] : p == 2 ? textures[1] : combine ? textures[0] : nullptr;
			if (stage5) Device->SetTexture(5, stage5);
			if (combine && skyVis) {
				Device->SetTexture(7, skyVis);
				TheRenderManager->renderState->NoteDeviceTexture(7, skyVis);
			}
			Device->SetTexture(2, scene);
			TheRenderManager->renderState->NoteDeviceTexture(5, stage5);
			TheRenderManager->renderState->NoteDeviceTexture(2, scene);
			static GpuTimer passTimers[4] = { GpuTimer("  AO estimate (half)"), GpuTimer("  AO blur X (half)"),
				GpuTimer("  AO blur Y (half)"), GpuTimer("  AO combine (full)") };
			{
				GpuProfileScope gpu(passTimers[p < 4 ? p : 3], Device);
				result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			}
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	Device->SetTexture(5, nullptr);
	if (orthoMap) { Device->SetTexture(6, nullptr); TheRenderManager->renderState->NoteDeviceTexture(6, nullptr); }
	if (skyVis) { Device->SetTexture(7, nullptr); TheRenderManager->renderState->NoteDeviceTexture(7, nullptr); }
	Device->SetTexture(2, TheTextureManager->SourceTexture);
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	static bool reported[2] = {};
	auto reportActive = [&]() {
		if (reported[lastLowRes]) return;
		Logger::Log("UNOFFICIAL dedicated AO active: %ux%u G16R16F ping-pong%s.", reduced.Width, reduced.Height,
			lastLowRes ? " (AOLowRes)" : "");
		reported[lastLowRes] = true;
	};
	if (SUCCEEDED(result) && deferCombine) {
		deferredReady = true;
		reportActive();
		renderTime = timer.LogTime("AmbientOcclusion::Deferred");
		return;
	}
	if (SUCCEEDED(result)) {
		if (chained) chain.Commit();
		else result = Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	}
	if (FAILED(result)) {
		Device->StretchRect(RenderedSurface, NULL, RenderTarget, NULL, D3DTEXF_NONE);
		packedAOFailed = true;
		Logger::Log("Packed AO failed (%08lx); using legacy AO until restart.", result);
		if (deferCombine) return;
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	reportActive();
	renderTime = timer.LogTime("AmbientOcclusion::PackedAO");
}
