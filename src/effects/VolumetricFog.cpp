#include "VolumetricFog.h"
#include "../core/GpuProfiler.h"
#include "../core/TestTools.h"
#include "../core/DxvkCompute.h"
#include "../core/RenderRect.h"

static void BindSky(IDirect3DDevice9* Device, IDirect3DTexture9* sky) {
	Device->SetTexture(12, sky);
	TheRenderManager->renderState->NoteDeviceTexture(12, sky);
	if (!sky) return;
	const DWORD states[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
		{D3DSAMP_MAGFILTER, D3DTEXF_POINT}, {D3DSAMP_MINFILTER, D3DTEXF_POINT}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
	for (const auto& s : states) TheRenderManager->SetSamplerState(12, (D3DSAMPLERSTATETYPE)s[0], s[1]);
}

static void BindSunFar(IDirect3DDevice9* Device, IDirect3DTexture9* factor) {
	Device->SetTexture(13, factor);
	TheRenderManager->renderState->NoteDeviceTexture(13, factor);
	if (!factor) return;
	const DWORD states[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
		{D3DSAMP_MAGFILTER, D3DTEXF_POINT}, {D3DSAMP_MINFILTER, D3DTEXF_POINT}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
	for (const auto& s : states) TheRenderManager->SetSamplerState(13, (D3DSAMPLERSTATETYPE)s[0], s[1]);
}

static GpuTimer* FogPassTimers() {
	static GpuTimer timers[2] = { GpuTimer("  Fog estimate (half)"), GpuTimer("  Fog composite (full)") };
	return timers;
}

IDirect3DTexture9* VolumetricFogEffect::SunFarFor(bool shadow) {
	IDirect3DTexture9* factor = shadow && TheShaderManager->Effects.SunFar ? TheShaderManager->Effects.SunFar->FactorTexture() : nullptr;
	D3DXHANDLE handle = ParameterHandle(paramSunFarFlags, "NVR_SunFarFlags");
	if (!handle) return nullptr;
	D3DXVECTOR4 flags(factor ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
	Effect->SetVector(handle, &flags);
	return factor;
}

void VolumetricFogEffect::UpdateConstants() {
	// live weather-driven sky-filter disable, smoothly animated the same way RainEffect
	// animates rain onset/offset (Animator started once on the true/false edge, sampled every frame).
	// Animator's clock runs in GAME HOURS, not real seconds (Animator.cpp derives currenttime from
	// GameDaysPassed) -- 0.1f here is a few game-minutes, a few real seconds at typical time scale,
	// matching the same order of magnitude RainEffect uses (0.05f/0.07f) rather than the ~6-real-minute
	// transition an unadjusted "3.0f meaning 3 seconds" mistake would have produced.
	bool shouldDisableFilter = (TheShaderManager->GameState.isRainy && rainyDisablesSkyFilter) ||
	                           (TheShaderManager->GameState.isCloudy && cloudyDisablesSkyFilter);

	if (shouldDisableFilter && weatherFilterWasActive) {
		// weather just started hiding the sky filter / disabling NVR-driven density
		weatherFilterWasActive = false;
		Constants.WeatherFilterAnimator.Start(0.1f, 0.0f);
	}
	else if (!shouldDisableFilter && !weatherFilterWasActive) {
		// weather cleared, sky filter/density fade back in
		weatherFilterWasActive = true;
		Constants.WeatherFilterAnimator.Start(0.1f, 1.0f);
	}

	Constants.Weather.x = Constants.WeatherFilterAnimator.GetValue();
	Constants.Weather.y = TheShaderManager->GameState.isExterior ? 1.0f : 0.0f;
	// SkyAmbientRadiance's TESR_SkyIrradiance is only refreshed while Shaders.Sky is enabled;
	// fall back to flat sky color in the shader when it's off rather than reading stale/zero data.
	Constants.Weather.z = TheShaderManager->Shaders.Sky->Enabled ? 1.0f : 0.0f;

	// Moon-phase-driven night ambient ceiling, mirroring ShadowsExterior.cpp's own moon-phase
	// shadow-fade formula (same day-count/phaseLength inputs, same cosine curve) so fog's night
	// floor tracks the same lunar cycle the shadow system already uses. The day/night fade itself
	// is left to the shader's own continuous isDayTime curve rather than duplicated here, so the
	// transition stays smooth instead of snapping at a hard cutoff. Harmless to compute for
	// interiors too -- NightAmbientStrength (read in UpdateSettings) is gated to zero there by the
	// shader's own nightFactor (isExterior term), so this has no effect regardless.
	TimeGlobals* GameTimeGlobals = TimeGlobals::Get();
	float DaysPassed = GameTimeGlobals->GameDaysPassed ? GameTimeGlobals->GameDaysPassed->data : 1.0f;
	float MoonPhase = (fmod(DaysPassed, 8 * Tes->sky->firstClimate->phaseLength & 0x3F)) / (Tes->sky->firstClimate->phaseLength & 0x3F);
	MoonPhase = std::lerp(-D3DX_PI, D3DX_PI, MoonPhase / 8) - D3DX_PI / 4;
	Constants.Global.z = std::lerp(0.0f, nightMinDarkness, cosf(MoonPhase) * 0.5f + 0.5f);
}

void VolumetricFogEffect::UpdateSettings(){

	char SettingCategory[50] = "Shaders.VolumetricFog.";

	if (TheShaderManager->GameState.isExterior)
		strcat(SettingCategory, "Main");
	else
		strcat(SettingCategory, "Interiors");

	Constants.Legacy.x = TheSettingManager->GetSettingI("Shaders.VolumetricFog.Main", "LegacyFog") ? 1.0f : 0.0f;
	if (Constants.Legacy.x > 0.5f) UpdateLegacySettings(SettingCategory);
	const int classicWas = classicLook;
	const bool referenceWas = referenceTest;
	classicLook = std::clamp(TheSettingManager->GetSettingI("Shaders.VolumetricFog.Main", "ClassicLook"), 0, 2);
#if NVR_TEST_TOOLS
	referenceTest = TheSettingManager->GetSettingI("Main.Main.Performance", "FogReferenceTest") != 0;
#endif
	if (classicLook != classicWas || referenceTest != referenceWas)
		Logger::Log("VOLUMETRIC FOG: %s", referenceTest ? "REFERENCE TEST, the original NVR fog's own shader (FogReference), full resolution, shadow and AO applies as their own passes" :
			IsLegacy() ? "legacy fog (LegacyFog on; ClassicLook does not apply)" : classicLook == 2 ? "ClassicLook 2, the original look, full resolution in one pass" :
			classicLook == 1 ? "ClassicLook 1, the original look with the half-resolution coefficients" : "ClassicLook off (today's fog)");

	Constants.Global.x = TheSettingManager->GetSettingF(SettingCategory, "Amount");
	Constants.Weather.w = TheSettingManager->GetSettingF(SettingCategory, "FogSaturation");

	Constants.Density.x = TheSettingManager->GetSettingF(SettingCategory, "BaseDensity");
	Constants.Density.y = TheSettingManager->GetSettingF(SettingCategory, "WeatherImpact");

	Constants.Shape.x = TheSettingManager->GetSettingF(SettingCategory, "HeightFalloff");
	Constants.Shape.y = TheSettingManager->GetSettingF(SettingCategory, "MaxHeight");
	Constants.Shape.z = TheSettingManager->GetSettingF(SettingCategory, "Extinction");
	Constants.Shape.w = TheSettingManager->GetSettingF(SettingCategory, "Inscattering");

	float windAngleRad = TheSettingManager->GetSettingF(SettingCategory, "WindAngle") * 0.0174532925f; // degrees to radians
	Constants.Wind.x = cosf(windAngleRad);
	Constants.Wind.y = sinf(windAngleRad);
	Constants.Wind.z = TheSettingManager->GetSettingF(SettingCategory, "WindSpeed");
	Constants.Wind.w = TheSettingManager->GetSettingF(SettingCategory, "NoiseScale");

	Constants.Scatter.z = TheSettingManager->GetSettingF(SettingCategory, "NoiseStrength");
	Constants.Scatter.w = TheSettingManager->GetSettingF(SettingCategory, "HeightInfluence");

	rainyDisablesSkyFilter = TheSettingManager->GetSettingI(SettingCategory, "RainyDisablesSkyFilter") != 0;
	cloudyDisablesSkyFilter = TheSettingManager->GetSettingI(SettingCategory, "CloudyDisablesSkyFilter") != 0;

	if (TheShaderManager->GameState.isExterior) {
		Constants.Density.z = TheSettingManager->GetSettingF(SettingCategory, "MorningFogDip");
		Constants.Density.w = TheSettingManager->GetSettingF(SettingCategory, "SunriseSunsetBoost");

		Constants.Scatter.x = TheSettingManager->GetSettingF(SettingCategory, "PhaseAsymmetry");
		Constants.Scatter.y = TheSettingManager->GetSettingF(SettingCategory, "ShadowStrength");
		Constants.Sun.x = std::clamp(TheSettingManager->GetSettingF(SettingCategory, "SunGlow"), 0.0f, 2.0f);

		Constants.Aerial.x = TheSettingManager->GetSettingF(SettingCategory, "AerialStrength");
		Constants.Aerial.y = TheSettingManager->GetSettingF(SettingCategory, "AerialRangeStart");
		Constants.Aerial.z = TheSettingManager->GetSettingF(SettingCategory, "AerialTintBlend");
		Constants.Aerial.w = TheSettingManager->GetSettingF(SettingCategory, "AerialDayFadeStart");

		Constants.AerialTintColor.x = TheSettingManager->GetSettingF(SettingCategory, "AerialTintR");
		Constants.AerialTintColor.y = TheSettingManager->GetSettingF(SettingCategory, "AerialTintG");
		Constants.AerialTintColor.z = TheSettingManager->GetSettingF(SettingCategory, "AerialTintB");

		Constants.Distant.x = TheSettingManager->GetSettingF(SettingCategory, "DistantFogRange");
		Constants.Distant.y = TheSettingManager->GetSettingF(SettingCategory, "DistantFogBlend");
		Constants.Distant.z = TheSettingManager->GetSettingF(SettingCategory, "DistantFogHeight");
		Constants.Distant.w = TheSettingManager->GetSettingF(SettingCategory, "EdgeAA");

		// shared with ShadowsExterior's own moon-phase shadow fade -- keeps fog's night-ambient
		// ceiling consistent with the shadow system's, rather than a second, disconnected knob.
		nightMinDarkness = 1.0f - TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Main", "NightMinDarkness");

		Constants.Global.w = TheSettingManager->GetSettingF(SettingCategory, "MinDensityFloor");
	}
	else {
		// these settings don't do anything in interiors: no sun, no horizon, no distant view
		Constants.Density.z = 0.0f;
		Constants.Density.w = 0.0f;

		Constants.Scatter.x = 0.0f;
		Constants.Scatter.y = 0.0f;
		Constants.Sun.x = 0.0f;

		Constants.Aerial = D3DXVECTOR4(0, 0, 0, 0);
		Constants.AerialTintColor = D3DXVECTOR4(0, 0, 0, 0);
		Constants.Distant = D3DXVECTOR4(0, 0, 0, 0);

		Constants.Global.w = 0.0f;
	}

	// Own section (not Main/Interiors-switched via SettingCategory), so it gets its own settings-UI
	// tab instead of crowding Main. Harmless to read unconditionally for interiors too -- the shader
	// gates its actual effect on isExterior the same way it already does for MorningFogDip's
	// timeOfDayScale, so it has no effect there regardless of what's read here.
	Constants.Night.x = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "DensityScale");
	Constants.Night.y = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "AmountScale");
	Constants.Night.z = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "HeightFalloffScale");
	Constants.Night.w = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "MaxHeightOffset");

	Constants.NightScatter.x = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "NoiseStrengthScale");
	Constants.NightScatter.y = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "WindSpeedScale");
	Constants.NightScatter.z = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "ExtinctionScale");
	Constants.NightScatter.w = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "InscatteringScale");

	Constants.NightTint.x = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "TintR");
	Constants.NightTint.y = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "TintG");
	Constants.NightTint.z = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "TintB");
	// Moved here from Global.y (Main-scoped) so it fades via the shared shader-side nightFactor like
	// the rest of this section, instead of needing its own interior-zeroing branch above.
	Constants.NightTint.w = TheSettingManager->GetSettingF("Shaders.VolumetricFog.Night", "NightAmbientStrength");
	// Packed into Global.y (its spare slot) rather than a new vector for one bool -- see the shader's
	// skyMaskFactor for how it's used.
	Constants.Global.y = TheSettingManager->GetSettingI("Shaders.VolumetricFog.Night", "DisableSkyMask") ? 1.0f : 0.0f;
}

void VolumetricFogEffect::UpdateLegacySettings(const char* SettingCategory) {
	Constants.LegacyData.x = TheSettingManager->GetSettingF(SettingCategory, "MinimumBaseFog");
	Constants.LegacyData.y = TheSettingManager->GetSettingF(SettingCategory, "HeightFogSaturation");
	Constants.LegacyData.z = TheSettingManager->GetSettingF(SettingCategory, "Amount");
	Constants.LegacyData.w = TheSettingManager->GetSettingF(SettingCategory, "HeightFogInscattering");

	Constants.LegacyLow.x = TheSettingManager->GetSettingF(SettingCategory, "FogSaturation");
	Constants.LegacyLow.y = TheSettingManager->GetSettingF(SettingCategory, "WeatherImpact");

	Constants.LegacyHigh.x = TheSettingManager->GetSettingF(SettingCategory, "HeightFogDensity");
	Constants.LegacyHigh.y = TheSettingManager->GetSettingF(SettingCategory, "HeightFogFalloff");
	Constants.LegacyHigh.z = TheSettingManager->GetSettingF(SettingCategory, "HeightFogDist");

	Constants.LegacyHeight.y = TheSettingManager->GetSettingF(SettingCategory, "HeightFogHeight");
	Constants.LegacyHeight.z = TheSettingManager->GetSettingF(SettingCategory, "SimpleFogHeight");
	Constants.LegacyHeight.w = TheShaderManager->GameState.isExterior ? 1.0f : 0.0f;

	Constants.LegacySimple.x = TheSettingManager->GetSettingF(SettingCategory, "Extinction");
	Constants.LegacySimple.y = TheSettingManager->GetSettingF(SettingCategory, "Inscattering");

	Constants.LegacyBlend.y = TheSettingManager->GetSettingF(SettingCategory, "HeightFogBlend");
	Constants.LegacyBlend.z = TheSettingManager->GetSettingF(SettingCategory, "HeightFogRolloff");
	Constants.LegacyBlend.w = TheSettingManager->GetSettingF(SettingCategory, "SimpleFogBlend");

	if (TheShaderManager->GameState.isExterior) {
		Constants.LegacyHeight.x = TheSettingManager->GetSettingF(SettingCategory, "DistantFogHeight");
		Constants.LegacyLow.z = TheSettingManager->GetSettingF(SettingCategory, "DistantFogRange");
		Constants.LegacyBlend.x = TheSettingManager->GetSettingF(SettingCategory, "DistantFogBlend");

		Constants.LegacyLow.w = TheSettingManager->GetSettingF(SettingCategory, "SunPower");
		Constants.LegacySimple.z = TheSettingManager->GetSettingF(SettingCategory, "FogNight");
		Constants.LegacySimple.w = TheSettingManager->GetSettingF(SettingCategory, "SimpleFogSkyColor");
		Constants.LegacyHigh.w = TheSettingManager->GetSettingF(SettingCategory, "HeightFogSkyColor");
	}
	else {
		Constants.LegacyHeight.x = 0.0f;
		Constants.LegacyLow.z = 0.0f;
		Constants.LegacyBlend.x = 0.0f;

		Constants.LegacyLow.w = 0.0f;
		Constants.LegacySimple.z = 1.0f;
		Constants.LegacySimple.w = 0.0f;
		Constants.LegacyHigh.w = 0.0f;
	}
}

void VolumetricFogEffect::RegisterConstants(){
	TheShaderManager->RegisterConstant("TESR_VolumetricFogDensity", &Constants.Density);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogShape", &Constants.Shape);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogWind", &Constants.Wind);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogScatter", &Constants.Scatter);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogWeather", &Constants.Weather);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogAerial", &Constants.Aerial);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogAerialTint", &Constants.AerialTintColor);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogDistant", &Constants.Distant);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogGlobal", &Constants.Global);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogNight", &Constants.Night);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogNightScatter", &Constants.NightScatter);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogNightTint", &Constants.NightTint);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogSun", &Constants.Sun);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogLow", &Constants.LegacyLow);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogHigh", &Constants.LegacyHigh);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogSimple", &Constants.LegacySimple);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogBlend", &Constants.LegacyBlend);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogHeight", &Constants.LegacyHeight);
	TheShaderManager->RegisterConstant("TESR_VolumetricFogData", &Constants.LegacyData);
}


bool VolumetricFogEffect::ShouldRender()
{
	return !TheShaderManager->GameState.isUnderwater;
};

void VolumetricFogEffect::RegisterTextures() {
	const int width = MaxQuality::Reduced(TheRenderManager->width, 2);
	const int height = MaxQuality::Reduced(TheRenderManager->height, 2);
	TheTextureManager->InitTexture("NVR_FogBuffer0", &fogTexture[0], &fogSurface[0], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_FogBuffer1", &fogTexture[1], &fogSurface[1], width, height, D3DFMT_A16B16G16R16F);
}

bool VolumetricFogEffect::CanComposite(IDirect3DSurface9* aoSurface) {
	if (!Enabled || !Effect || !ShouldRender() || dedicatedFogFailed || !fogSurface[0] || PlainOnly() ||
		TheSettingManager->SettingsMain.Main.DisableCompositeApply ||
		!CompositeTechnique() || !ParameterHandle(paramCompositeFlags, "NVR_CompositeFlags"))
		return false;
	if (aoSurface && !ParameterHandle(paramCompositeAOTexel, "NVR_CompositeAOTexel")) {
		D3DSURFACE_DESC fog = {}, ao = {};
		if (FAILED(fogSurface[0]->GetDesc(&fog)) || FAILED(aoSurface->GetDesc(&ao)) ||
			fog.Width != ao.Width || fog.Height != ao.Height)
			return false;
	}
	return true;
}

int VolumetricFogEffect::HalfStoreMode(IDirect3DDevice9* Device) {
	if (halfStoreMode != -2) return halfStoreMode;
	halfStoreMode = -1;
	D3DXHANDLE technique = Effect ? Effect->GetTechniqueByName("HalfStoreProbe") : NULL;
	if (!technique || !Device) { Logger::Log("INTERIOR SHADOW FOLD: no HalfStoreProbe technique in VolumetricFog.fx: the fold stays off"); return halfStoreMode; }
	const UINT W = 64, H = 16;
	IDirect3DTexture9* texture = nullptr;
	IDirect3DSurface9* surface = nullptr;
	IDirect3DSurface9* readback = nullptr;
	IDirect3DSurface9* oldTarget0 = nullptr;
	IDirect3DSurface9* oldTarget1 = nullptr;
	IDirect3DVertexDeclaration9* oldDeclaration = nullptr;
	DWORD oldFVF = 0;
	D3DVIEWPORT9 oldViewport = {};
	unsigned nearest = 0, toward = 0, total = 0;
	HRESULT hr = Device->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &texture, NULL);
	if (SUCCEEDED(hr)) hr = texture->GetSurfaceLevel(0, &surface);
	if (SUCCEEDED(hr)) hr = Device->CreateOffscreenPlainSurface(W, H, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &readback, NULL);
	if (SUCCEEDED(hr)) {
		Device->GetRenderTarget(0, &oldTarget0);
		Device->GetRenderTarget(1, &oldTarget1);
		Device->GetVertexDeclaration(&oldDeclaration);
		Device->GetFVF(&oldFVF);
		Device->GetViewport(&oldViewport);
		if (oldTarget1) Device->SetRenderTarget(1, nullptr);
		hr = Device->SetRenderTarget(0, surface);
		UINT passes = 0;
		if (SUCCEEDED(hr)) hr = Effect->SetTechnique(technique);
		if (SUCCEEDED(hr)) hr = Effect->Begin(&passes, 0);
		if (SUCCEEDED(hr)) {
			hr = Effect->BeginPass(0);
			if (SUCCEEDED(hr)) {
				struct Vertex { float x, y, z, w, u, v; };
				const Vertex quad[4] = { { -1, 1, 0, 1, 0, 0 }, { 1, 1, 0, 1, 1, 0 }, { -1, -1, 0, 1, 0, 1 }, { 1, -1, 0, 1, 1, 1 } };
				Device->SetFVF(D3DFVF_XYZW | D3DFVF_TEX1);
				hr = Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
				Effect->EndPass();
			}
			Effect->End();
		}
		if (SUCCEEDED(hr)) hr = Device->GetRenderTargetData(surface, readback);
		D3DLOCKED_RECT locked = {};
		if (SUCCEEDED(hr) && SUCCEEDED(hr = readback->LockRect(&locked, NULL, D3DLOCK_READONLY))) {
			for (UINT y = 0; y < H; y++) {
				const UInt16* row = (const UInt16*)((const UInt8*)locked.pBits + (size_t)y * locked.Pitch);
				for (UINT x = 0; x < W; x++, total++) {
					if (row[x * 4] == row[x * 4 + 1]) nearest++;
					if (row[x * 4] == row[x * 4 + 2]) toward++;
				}
			}
			readback->UnlockRect();
		}
		Device->SetRenderTarget(0, oldTarget0);
		if (oldTarget1) Device->SetRenderTarget(1, oldTarget1);
		if (oldDeclaration) Device->SetVertexDeclaration(oldDeclaration);
		else if (oldFVF) Device->SetFVF(oldFVF);
		Device->SetViewport(&oldViewport);
	}
	if (oldTarget0) oldTarget0->Release();
	if (oldTarget1) oldTarget1->Release();
	if (oldDeclaration) oldDeclaration->Release();
	if (readback) readback->Release();
	if (surface) surface->Release();
	if (texture) texture->Release();
	if (total == W * H && nearest == total && toward != total) halfStoreMode = 0;
	else if (total == W * H && toward == total && nearest != total) halfStoreMode = 2;
	Logger::Log("INTERIOR SHADOW FOLD: this GPU stores 16-bit floats %s (probe %08lx: %u of %u values as round to nearest even, %u as round toward zero)",
		halfStoreMode == 0 ? "rounding to nearest even" : halfStoreMode == 2 ? "rounding toward zero" : "in neither way: the fold stays off",
		(unsigned long)hr, nearest, total, toward);
	return halfStoreMode;
}

bool VolumetricFogEffect::CanApplyShadowAO() {
	return Enabled && Effect && !TheSettingManager->SettingsMain.Main.DisableCompositeApply && !referenceTest &&
		TechniqueHandle(techApplyShadowAO, "ApplyShadowAO") && ParameterHandle(paramCompositeFlags, "NVR_CompositeFlags") &&
		ParameterHandle(paramCompositeAOTexel, "NVR_CompositeAOTexel");
}

bool VolumetricFogEffect::RenderShadowAO(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, bool shadow, IDirect3DTexture9* aoTexture) {
	D3DXHANDLE technique = TechniqueHandle(techApplyShadowAO, "ApplyShadowAO");
	D3DXHANDLE flagsHandle = ParameterHandle(paramCompositeFlags, "NVR_CompositeFlags");
	D3DXHANDLE aoTexelHandle = ParameterHandle(paramCompositeAOTexel, "NVR_CompositeAOTexel");
	IDirect3DTexture9* scene = TheTextureManager->RenderedTexture;
	D3DSURFACE_DESC ao = {};
	if (!technique || !flagsHandle || !aoTexelHandle || !scene || !RenderedSurface || (!shadow && !aoTexture) ||
		(aoTexture && FAILED(aoTexture->GetLevelDesc(0, &ao))))
		return false;
	if (!ExtFailed && DxvkCompute::PassesOn() && ExtShadowAO(Device, RenderTarget, RenderedSurface, shadow, aoTexture)) return true;

	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);
	Effect->SetTechnique(technique);
	SetCT();
	IDirect3DTexture9* sky = aoTexture ? compositeSkyTexture : nullptr;
	D3DXVECTOR4 flags(shadow ? 1.0f : 0.0f, aoTexture ? 1.0f : 0.0f, sky ? 1.0f : 0.0f, 0.0f);
	Effect->SetVector(flagsHandle, &flags);
	IDirect3DTexture9* sunFar = SunFarFor(shadow);
	if (aoTexture) {
		D3DXVECTOR4 aoTexel(1.0f / ao.Width, 1.0f / ao.Height, AmbientOcclusionEffect::SkyTakeComposite().x, AmbientOcclusionEffect::SkyTakeComposite().y);
		Effect->SetVector(aoTexelHandle, &aoTexel);
	}
	UINT passes = 0;
	HRESULT result = Device->SetRenderTarget(0, chained ? chain.Output() : RenderTarget);
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		result = Effect->BeginPass(0);
		if (SUCCEEDED(result)) {
			Device->SetTexture(0, scene);
			Device->SetTexture(6, nullptr);
			Device->SetTexture(7, nullptr);
			Device->SetTexture(8, aoTexture);
			BindSky(Device, sky);
			if (sunFar) BindSunFar(Device, sunFar);
			RenderRect::ApplyViewport(Device);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Effect->EndPass();
		}
		Effect->End();
	}
	Device->SetTexture(8, nullptr);
	if (sky) BindSky(Device, nullptr);
	if (sunFar) BindSunFar(Device, nullptr);
	Device->SetTexture(0, TheTextureManager->SourceTexture);
	Device->SetRenderTarget(0, RenderTarget);
	if (FAILED(result)) {
		if (!chained) Device->StretchRect(RenderedSurface, NULL, RenderTarget, NULL, D3DTEXF_NONE);
		return false;
	}
	if (chained) chain.Commit();
	else Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL merged apply active: sun shadows %s and AO %s in one pass (effects between them and the fog).",
			shadow ? "yes" : "no", aoTexture ? "yes" : "no");
		reported = true;
	}
	return true;
}

bool VolumetricFogEffect::RenderDedicated(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface) {
	if (dedicatedFogFailed) return false;
	if (ReShadeIntegration::KeepDepthBound()) return false;
	const bool composite = compositeShadow || compositeInteriorShadow || (compositeAO && compositeAOTexture);
	D3DXHANDLE technique = composite ? (compositeInteriorShadow ? CompositeInteriorTechnique() : CompositeTechnique()) : DedicatedTechnique();
	D3DXHANDLE flagsHandle = composite ? ParameterHandle(paramCompositeFlags, "NVR_CompositeFlags") : NULL;
	if (composite && !flagsHandle) return false;
	D3DXHANDLE layoutHandle = ParameterHandle(paramFogLayout, "NVR_FogLayout");
	D3DXTECHNIQUE_DESC description = {};
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC target = {}, scratch = {}, fog = {}, fogAdd = {};
	const D3DCAPS9* caps = TheRenderManager->DeviceCaps();
	IDirect3DTexture9* scene = sceneOverride ? sceneOverride : TheTextureManager->RenderedTexture;
	if (!technique || !layoutHandle || FAILED(Effect->GetTechniqueDesc(technique, &description)) ||
		description.Passes != 2 || !RenderedSurface || !scene ||
		!fogTexture[0] || !fogTexture[1] || !fogSurface[0] || !fogSurface[1] ||
		!caps || caps->NumSimultaneousRTs < 2 ||
		FAILED(Device->GetViewport(&original)) || FAILED(RenderTarget->GetDesc(&target)) ||
		FAILED(RenderedSurface->GetDesc(&scratch)) || FAILED(fogSurface[0]->GetDesc(&fog)) ||
		FAILED(fogSurface[1]->GetDesc(&fogAdd)) ||
		original.X || original.Y ||
		((original.Width != target.Width || original.Height != target.Height) && !RenderRect::Active()) ||
		scratch.Width != target.Width || scratch.Height != target.Height ||
		fog.Width != MaxQuality::Reduced(target.Width, 2) || fog.Height != MaxQuality::Reduced(target.Height, 2) ||
		fogAdd.Width != fog.Width || fogAdd.Height != fog.Height || fogAdd.Format != fog.Format ||
		target.MultiSampleType != D3DMULTISAMPLE_NONE)
		return false;
	if (RenderRect::Active()) { original.Width = target.Width; original.Height = target.Height; }

	Sm6Pass* sm6[2] = { nullptr, nullptr };
	if (!targetOverride && !sceneOverride && Sm6Pass::On()) {
		sm6[0] = Sm6For(technique, 0);
		sm6[1] = Sm6For(technique, 1);
		if (!sm6[0] || !sm6[1]) sm6[0] = sm6[1] = nullptr;
	}

	if (!sm6[0] && !targetOverride && !sceneOverride && !ExtFailed && DxvkCompute::PassesOn()) {
		const D3DXVECTOR4 extLayout((float)fog.Width / original.Width, (float)fog.Height / original.Height, 1.0f / fog.Width, 1.0f / fog.Height);
		bool extGrid = false;
		if (ExtDedicated(Device, RenderTarget, RenderedSurface, scene, composite, extLayout, extGrid)) {
			compositeApplied = composite;
			gridApplied = extGrid;
			return true;
		}
	}

	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = !targetOverride && chain.Owns(RenderTarget, RenderedSurface);
	IDirect3DTexture9* sunFar = nullptr;
	IDirect3DSurface9* finalTarget = targetOverride ? targetOverride : chained ? chain.Output() : RenderTarget;
	D3DVIEWPORT9 reduced = original;
	reduced.Width = fog.Width;
	reduced.Height = fog.Height;
	D3DXVECTOR4 layout((float)reduced.Width / original.Width,
		(float)reduced.Height / original.Height, 1.0f / reduced.Width, 1.0f / reduced.Height);
	IDirect3DSurface9* depthSurface = nullptr;
	DWORD oldWriteMask1 = 0xF;
	HRESULT result = Device->GetRenderState(D3DRS_COLORWRITEENABLE1, &oldWriteMask1);
	if (SUCCEEDED(result)) result = Device->GetDepthStencilSurface(&depthSurface);
	if (SUCCEEDED(result)) result = Device->SetDepthStencilSurface(nullptr);
	if (FAILED(result)) {
		if (depthSurface) depthSurface->Release();
		dedicatedFogFailed = true;
		return false;
	}

	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	if (composite) {
		D3DXVECTOR4 flags(compositeShadow ? 1.0f : 0.0f, (compositeAO && compositeAOTexture) ? 1.0f : 0.0f,
			(compositeAO && compositeAOTexture && compositeSkyTexture) ? 1.0f : 0.0f,
			compositeInteriorShadow ? compositeRound : 0.0f);
		Effect->SetVector(flagsHandle, &flags);
		sunFar = compositeShadow && !compositeInteriorShadow ? SunFarFor(true) : SunFarFor(false);
		D3DXHANDLE aoTexelHandle = ParameterHandle(paramCompositeAOTexel, "NVR_CompositeAOTexel");
		D3DSURFACE_DESC ao = {};
		if (aoTexelHandle && compositeAO && compositeAOTexture && SUCCEEDED(compositeAOTexture->GetLevelDesc(0, &ao))) {
			D3DXVECTOR4 aoTexel(1.0f / ao.Width, 1.0f / ao.Height, AmbientOcclusionEffect::SkyTakeComposite().x, AmbientOcclusionEffect::SkyTakeComposite().y);
			Effect->SetVector(aoTexelHandle, &aoTexel);
		}
	}
	D3DXHANDLE gridHandle = ParameterHandle(paramGridFlags, "NVR_FogGridFlags");
	const bool grid = gridTexture && gridHandle;
	if (gridHandle) {
		D3DXVECTOR4 gridFlags(grid ? 1.0f : 0.0f, grid ? 1.0f : 0.0f, 0.0f, 0.0f);
		Effect->SetVector(gridHandle, &gridFlags);
	}
	if (grid) {
		Device->SetTexture(11, gridTexture);
		TheRenderManager->renderState->NoteDeviceTexture(11, gridTexture);
		const DWORD states[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
			{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
		for (const auto& s : states) TheRenderManager->SetSamplerState(11, (D3DSAMPLERSTATETYPE)s[0], s[1]);
	}
	UINT passes = 0;
	result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		GpuTimer* passTimers = FogPassTimers();
		for (UINT p = 0; p < passes && SUCCEEDED(result); ++p) {
			const bool combine = p == passes - 1;
			GpuProfileScope gpuPass(passTimers[combine ? 1 : 0], Device);
			Device->SetTexture(6, nullptr);
			Device->SetTexture(7, nullptr);
			Device->SetTexture(8, nullptr);
			if (combine) result = Device->SetRenderTarget(1, nullptr);
			if (SUCCEEDED(result)) result = Device->SetRenderTarget(0, combine ? finalTarget : fogSurface[0]);
			if (SUCCEEDED(result) && !combine) result = Device->SetRenderTarget(1, fogSurface[1]);
			if (SUCCEEDED(result) && !combine) result = Device->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
			if (SUCCEEDED(result)) result = Device->SetViewport(combine ? &original : &reduced);
			if (FAILED(result)) break;
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			Device->SetTexture(0, scene);
			if (combine) {
				Device->SetTexture(6, fogTexture[0]);
				Device->SetTexture(7, fogTexture[1]);
				if (composite && compositeAO && compositeAOTexture) Device->SetTexture(8, compositeAOTexture);
				if (composite && compositeAO && compositeAOTexture && compositeSkyTexture) BindSky(Device, compositeSkyTexture);
				if (sunFar) BindSunFar(Device, sunFar);
			}
			Sm6Pass* sm6Pass = sm6[0] ? sm6[p] : nullptr;
			if (sm6Pass && sm6Pass->Draw(Device, combine ? &finalTarget : fogSurface, combine ? 1 : 2, combine ? ExtUv::ScreenQuad : ExtUv::TexelCentre)) result = D3D_OK;
			else {
				RenderRect::ApplyViewport(Device);
				result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			}
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	Device->SetTexture(6, nullptr);
	Device->SetTexture(7, nullptr);
	Device->SetTexture(8, nullptr);
	if (compositeSkyTexture) BindSky(Device, nullptr);
	if (sunFar) BindSunFar(Device, nullptr);
	if (grid) {
		Device->SetTexture(11, nullptr);
		TheRenderManager->renderState->NoteDeviceTexture(11, nullptr);
	}
	if (gridHandle) {
		D3DXVECTOR4 off(0.0f, 0.0f, 0.0f, 0.0f);
		Effect->SetVector(gridHandle, &off);
	}
	Device->SetTexture(0, TheTextureManager->SourceTexture);
	Device->SetRenderTarget(1, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, oldWriteMask1);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	if (targetOverride) {
		compositeApplied = SUCCEEDED(result) && composite;
		return SUCCEEDED(result);
	}
	if (SUCCEEDED(result)) {
		if (chained) chain.Commit();
		else result = Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	}
	if (FAILED(result)) {
		Device->StretchRect(RenderedSurface, NULL, RenderTarget, NULL, D3DTEXF_NONE);
		dedicatedFogFailed = true;
		Logger::Log("Dedicated volumetric fog failed (%08lx); using packed path until restart.", result);
		return false;
	}
	compositeApplied = composite;
	gridApplied = grid;
	static bool reportedComposite = false;
	if (composite && !reportedComposite) {
		Logger::Log("UNOFFICIAL composite apply active: sun shadows %s, AO %s folded into the fog pass.",
			compositeShadow ? "yes" : compositeInteriorShadow ? "no (the interior darkening instead)" : "no", (compositeAO && compositeAOTexture) ? "yes" : "no");
		reportedComposite = true;
	}
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL dedicated volumetric fog active: %ux%u -> %ux%u, full-resolution apply (MRT).",
			original.Width, original.Height, reduced.Width, reduced.Height);
		reported = true;
	}
	return true;
}

Sm6Pass* VolumetricFogEffect::Sm6For(D3DXHANDLE technique, UINT pass) {
	D3DXTECHNIQUE_DESC description = {};
	if (!technique || !Effect || FAILED(Effect->GetTechniqueDesc(technique, &description)) || !description.Name) return nullptr;
	Sm6Pass& sm6 = sm6Passes[std::string(description.Name) + "." + std::to_string(pass)];
	return sm6.Prepare(this, description.Name, pass) ? &sm6 : nullptr;
}

UINT VolumetricFogEffect::PlainTechniqueIndex(UINT techniqueIndex) {
	const char* name = referenceTest ? "FogReference" : IsLegacy() ? "VolumetricFogLegacy" : IsClassic() ? "VolumetricFogClassic" : nullptr;
	if (!name || !Effect) return techniqueIndex;
	D3DXEFFECT_DESC effect = {};
	if (FAILED(Effect->GetDesc(&effect))) return techniqueIndex;
	for (UINT t = 0; t < effect.Techniques; t++) {
		D3DXTECHNIQUE_DESC description = {};
		D3DXHANDLE technique = Effect->GetTechnique(t);
		if (technique && SUCCEEDED(Effect->GetTechniqueDesc(technique, &description)) && description.Name &&
			!strcmp(description.Name, name))
			return t;
	}
	return techniqueIndex;
}

void VolumetricFogEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
	IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }

	static IDirect3DBaseTexture9* noise = nullptr;
	static bool noiseLoaded = false;
	if (!noiseLoaded) {
		noiseLoaded = true;
		noise = TheTextureManager->GetFileTexture("Data\\Textures\\Effects\\NoiseVolume.dds", TextureRecord::TextureRecordType::VolumeBuffer);
		if (!noise) Logger::Log("[ERROR] VolumetricFog: Textures\\Effects\\NoiseVolume.dds is missing: the fog's patches are off.");
	}
	noiseTexture = noise;
	struct NoiseBinding {
		IDirect3DDevice9* device;
		NoiseBinding(IDirect3DDevice9* d, IDirect3DBaseTexture9* t) : device(d) {
			device->SetTexture(9, t);
			TheRenderManager->renderState->NoteDeviceTexture(9, t);
			const DWORD states[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP}, {D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP},
				{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_LINEAR} };
			for (const auto& s : states) TheRenderManager->SetSamplerState(9, (D3DSAMPLERSTATETYPE)s[0], s[1]);
		}
		~NoiseBinding() { device->SetTexture(9, nullptr); TheRenderManager->renderState->NoteDeviceTexture(9, nullptr); }
	} noiseBinding(Device, noise);

	struct SkyViewBinding {
		IDirect3DDevice9* device; bool bound;
		SkyViewBinding(IDirect3DDevice9* d) : device(d), bound(false) {
			SkyViewEffect* table = TheShaderManager->Effects.SkyView;
			if (!table || !table->Textures.TableTexture || !table->ShouldRender()) return;
			bound = true;
			device->SetTexture(10, table->Textures.TableTexture);
			TheRenderManager->renderState->NoteDeviceTexture(10, table->Textures.TableTexture);
			const DWORD states[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
				{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
			for (const auto& s : states) TheRenderManager->SetSamplerState(10, (D3DSAMPLERSTATETYPE)s[0], s[1]);
		}
		~SkyViewBinding() { if (bound) { device->SetTexture(10, nullptr); TheRenderManager->renderState->NoteDeviceTexture(10, nullptr); } }
	} skyViewBinding(Device);

	if (PlainOnly() && !(compositeShadow || compositeAO || compositeInteriorShadow)) {
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, PlainTechniqueIndex(techniqueIndex), ClearRenderTarget, SourceBuffer);
		return;
	}

	{
		auto timer = TimeLogger();
		if (RenderDedicated(Device, RenderTarget, RenderedSurface)) {
			renderTime = timer.LogTime("VolumetricFog::DedicatedRender");
			return;
		}
	}

	if (compositeShadow || compositeAO || compositeInteriorShadow) return;

	D3DXHANDLE technique = PackedTechnique();
	D3DXTECHNIQUE_DESC description = {};
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC target = {}, scratch = {};
	if (packedFogFailed || !technique || !RenderedSurface || !SourceBuffer ||
		FAILED(Effect->GetTechniqueDesc(technique, &description)) || description.Passes != 2 ||
		FAILED(Device->GetViewport(&original)) || FAILED(RenderTarget->GetDesc(&target)) ||
		FAILED(RenderedSurface->GetDesc(&scratch)) || original.X || original.Y ||
		original.Width != target.Width || original.Height != target.Height ||
		target.Width != scratch.Width || target.Height != scratch.Height ||
		(target.Width & 1) || (target.Height & 1) ||
		RenderRect::Active()) {
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, PlainTechniqueIndex(techniqueIndex), ClearRenderTarget, SourceBuffer);
		return;
	}

	auto timer = TimeLogger();
	TheShaderManager->Chain.Sync();
	D3DVIEWPORT9 reduced = original;
	reduced.Width /= 2;
	reduced.Height /= 2;
	RECT packed = {0, 0, (LONG)reduced.Width, (LONG)reduced.Height};
	HRESULT result = Device->StretchRect(RenderTarget, NULL, SourceBuffer, NULL, D3DTEXF_NONE);
	if (SUCCEEDED(result)) result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) SetCT();
	UINT passes = 0;
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		for (UINT p = 0; p < passes && SUCCEEDED(result); ++p) {
			const bool combine = p == passes - 1;
			result = Device->SetViewport(combine ? &original : &reduced);
			if (FAILED(result)) break;
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
			if (SUCCEEDED(result)) result = Device->StretchRect(RenderTarget, combine ? NULL : &packed,
				RenderedSurface, combine ? NULL : &packed, D3DTEXF_NONE);
		}
		Effect->End();
	}
	Device->SetViewport(&original);

	if (FAILED(result)) {
		Device->StretchRect(SourceBuffer, NULL, RenderTarget, NULL, D3DTEXF_NONE);
		Device->StretchRect(SourceBuffer, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
		packedFogFailed = true;
		Logger::Log("Packed volumetric fog failed (%08lx); using legacy path until restart.", result);
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, PlainTechniqueIndex(techniqueIndex), ClearRenderTarget, SourceBuffer);
		return;
	}
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL packed volumetric fog active: %ux%u -> %ux%u.",
			original.Width, original.Height, reduced.Width, reduced.Height);
		reported = true;
	}
	renderTime = timer.LogTime("VolumetricFog::PackedRender");
}

namespace {
	const char* const FogExtMatrixNames[] = { "TESR_ShadowCameraToLightTransformNear", "TESR_ShadowCameraToLightTransformMiddle",
		"TESR_ShadowCameraToLightTransformFar", "TESR_ShadowCameraToLightTransformLod", "TESR_ViewTransform", "TESR_ProjectionTransform" };
	const char* const FogExtVectorNames[] = { "TESR_FogColor", "TESR_FogData", "TESR_ReciprocalResolution", "TESR_GameTime", "TESR_SmoothedSunDir",
		"TESR_SunPosition", "TESR_SunColor", "TESR_SunDiskColor", "TESR_SunAmbient", "TESR_FogHorizonColor", "TESR_FogSkyLowColor", "TESR_FogSkyColor",
		"TESR_SkyData", "TESR_SunsetColor", "TESR_SunAmount", "TESR_PBRData",
		"TESR_ShadowNearCenter", "TESR_ShadowMiddleCenter", "TESR_ShadowFarCenter", "TESR_ShadowLodCenter", "TESR_ShadowFormatData", "TESR_ShadowBlur",
		"TESR_ShadowFade",
		"TESR_VolumetricFogDensity", "TESR_VolumetricFogShape", "TESR_VolumetricFogWind", "TESR_VolumetricFogScatter", "TESR_VolumetricFogWeather",
		"TESR_VolumetricFogAerial", "TESR_VolumetricFogAerialTint", "TESR_VolumetricFogDistant", "TESR_VolumetricFogGlobal", "TESR_VolumetricFogNight",
		"TESR_VolumetricFogNightScatter", "TESR_VolumetricFogSun", "TESR_VolumetricFogNightTint",
		"TESR_PhysSky", "TESR_PhysSkySun",
		"TESR_VolumetricLightData", "TESR_VolumetricLightData2", "TESR_VolumetricLightGridLayout", "TESR_VolumetricLightGridData",
		"TESR_VolumetricFogLow", "TESR_VolumetricFogHigh", "TESR_VolumetricFogSimple", "TESR_VolumetricFogBlend", "TESR_VolumetricFogHeight",
		"TESR_VolumetricFogData",
		"TESR_ShadowData", "TESR_WaterSettings", "TESR_AmbientOcclusionAOData", "TESR_AmbientOcclusionData",
		"TESR_CameraData", "TESR_CameraPosition" };
	struct FogExtConstants {
		D3DXMATRIX	Matrices[_countof(FogExtMatrixNames)];
		D3DXVECTOR4	Vectors[_countof(FogExtVectorNames)];
		D3DXVECTOR4	SkyIrradiance[9];
		D3DXVECTOR4	SkyIrradianceGame[9];
		D3DXVECTOR4	Linear[9];
	};
	const char* const FogExtLinearNames[] = { "TESR_FogColor", "TESR_FogSkyColor", "TESR_FogSkyLowColor", "TESR_FogHorizonColor", "TESR_SunDiskColor",
		"TESR_SunsetColor", "TESR_SunAmbient", "TESR_VolumetricFogAerialTint", "TESR_SunColor" };

	float FogLinear(float c) { return c <= 0.04045f ? c / 12.92f : powf(fabsf((c + 0.055f) / 1.055f), 2.4f); }

	bool FogExtCopy(void* to, size_t bytes, const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		if (found == TheShaderManager->ConstantsTable.end() || !found->second) {
			static bool logged = false;
			if (!logged) {
				logged = true;
				Logger::Log("UNOFFICIAL NVR DXVK: VolumetricFog: %s is not registered; the D3D9 techniques are used", name);
			}
			return false;
		}
		memcpy(to, found->second, bytes);
		return true;
	}

	UINT FogExtBindings(NVR_EXT_BINDING_DESC* bindings, UINT images) {
		for (UINT i = 0; i < images; i++) {
			bindings[i] = {};
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i;
			bindings[i].ViewType = i == 5 ? NVR_EXT_VIEW_3D : NVR_EXT_VIEW_2D;
		}
		bindings[images] = { 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 };
		for (UINT s = 0; s < 3; s++) {
			bindings[images + 1 + s] = {};
			bindings[images + 1 + s].Type = NVR_EXT_BIND_SAMPLER; bindings[images + 1 + s].Slot = s; bindings[images + 1 + s].PushOffset = s * 4;
		}
		return images + 4;
	}

	UINT FogExtResources(NVR_EXT_RESOURCE_BINDING* resources, IDirect3DBaseTexture9* const* textures, UINT images, ID3D9NvrExtBuffer* uniforms) {
		ID3D9NvrExtSampler* samplers[3] = { DxvkCompute::PointClamp(), DxvkCompute::LinearClamp(), DxvkCompute::LinearWrap() };
		for (UINT i = 0; i < images; i++) {
			resources[i] = {};
			resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
		}
		resources[images] = {};
		resources[images].Slot = 0; resources[images].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[images].pResource = uniforms;
		for (UINT s = 0; s < 3; s++) {
			if (!samplers[s]) return 0;
			resources[images + 1 + s] = {};
			resources[images + 1 + s].Slot = s; resources[images + 1 + s].Type = NVR_EXT_BIND_SAMPLER; resources[images + 1 + s].pResource = samplers[s];
		}
		return images + 4;
	}
}

IDirect3DBaseTexture9* VolumetricFogEffect::ExtTextureOf(const char* name) {
	for (UInt32 c = 0; c < TextureShaderValuesCount; c++) {
		ShaderTextureValue* sampler = &TextureShaderValues[c];
		if (!sampler->Name || strcmp(sampler->Name, name) || !sampler->Texture) continue;
		if (sampler->Texture->TextureRef) sampler->Texture->Texture = *sampler->Texture->TextureRef;
		if (!sampler->Texture->Texture) sampler->Texture->BindTexture(sampler->Name);
		return sampler->Texture->Texture;
	}
	return nullptr;
}

bool VolumetricFogEffect::ExtSetup(IDirect3DBaseTexture9* (&inputs)[ExtInputCount], ExtFogPush& push) {
	IDirect3DBaseTexture9* depth = ExtTextureOf("TESR_DepthBuffer");
	IDirect3DBaseTexture9* atlas = ExtTextureOf("TESR_ShadowAtlas");
	IDirect3DBaseTexture9* normals = ExtTextureOf("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* pointShadow = ExtTextureOf("TESR_PointShadowBuffer");
	if (!depth || !atlas || !normals || !pointShadow || !noiseTexture) return false;
	const bool physical = ParameterHandle(paramPhysSky, "TESR_PhysSky") != NULL;
	SkyViewEffect* table = TheShaderManager->Effects.SkyView;
	IDirect3DBaseTexture9* skyView = table && table->Textures.TableTexture && table->ShouldRender() ? table->Textures.TableTexture : nullptr;

	FogExtConstants constants;
	bool registered = true;
	for (UINT i = 0; i < _countof(FogExtMatrixNames); i++) registered = FogExtCopy(&constants.Matrices[i], sizeof(D3DXMATRIX), FogExtMatrixNames[i]) && registered;
	for (UINT i = 0; i < _countof(FogExtVectorNames); i++) registered = FogExtCopy(&constants.Vectors[i], sizeof(D3DXVECTOR4), FogExtVectorNames[i]) && registered;
	registered = FogExtCopy(constants.SkyIrradiance, sizeof(constants.SkyIrradiance), "TESR_SkyIrradiance") && registered;
	registered = FogExtCopy(constants.SkyIrradianceGame, sizeof(constants.SkyIrradianceGame), "TESR_SkyIrradianceGame") && registered;
	for (UINT i = 0; i < _countof(FogExtLinearNames); i++) {
		D3DXVECTOR4& c = constants.Linear[i];
		registered = FogExtCopy(&c, sizeof(D3DXVECTOR4), FogExtLinearNames[i]) && registered;
		c = D3DXVECTOR4(FogLinear(c.x), FogLinear(c.y), FogLinear(c.z), c.w);
	}
	if (!registered) return false;
	const D3DXVECTOR4& physSky = constants.Vectors[36];
	if (physical && !skyView && physSky.x > 0.0f && TheShaderManager->GameState.isExterior) return false;
	ID3D9NvrExtBuffer* uniforms = ExtUniforms(extUniforms, &constants, sizeof(constants));
	if (!uniforms) return false;

	inputs[0] = nullptr;
	inputs[1] = depth; inputs[2] = atlas; inputs[3] = normals; inputs[4] = pointShadow; inputs[5] = noiseTexture;
	inputs[6] = skyView ? skyView : depth;
	for (UINT i = 7; i < ExtInputCount; i++) inputs[i] = depth;
	push = {};
	push.physicalSky = physical ? 1 : 0;
	return true;
}

bool VolumetricFogEffect::ExtApplyPass(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, IDirect3DSurface9* SourceBuffer,
	IDirect3DBaseTexture9* const (&inputs)[ExtInputCount], const ExtFogPush& push, bool last) {
	char suffix[32];
	_snprintf_s(suffix, sizeof(suffix), _TRUNCATE, "m%ul%uc%up%u", push.mode, push.legacy, push.classic, push.physicalSky);
	ID3D9NvrExtPipeline* pipeline = ExtVariant(extVariants, extApply, "VolumetricFogApply", suffix, [](const char* name) {
		NVR_EXT_BINDING_DESC bindings[ExtInputCount + 4];
		const UINT count = FogExtBindings(bindings, ExtInputCount);
		return DxvkCompute::CreateGraphicsPipeline(name, bindings, count, sizeof(ExtFogPush), true);
	});
	if (!pipeline || !extUniforms.Buffer) return false;
	for (UINT i = 0; i < ExtInputCount; i++) if (!inputs[i]) return false;
	NVR_EXT_RESOURCE_BINDING resources[ExtInputCount + 4];
	const UINT count = FogExtResources(resources, inputs, ExtInputCount, extUniforms.Buffer);
	if (!count) return false;
	return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, pipeline, resources, count, &push, sizeof(push), last);
}

bool VolumetricFogEffect::ExtDedicated(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, IDirect3DTexture9* scene,
	bool composite, const D3DXVECTOR4& layout, bool& grid) {
	IDirect3DBaseTexture9* inputs[ExtInputCount];
	ExtFogPush push;
	if (!ExtSetup(inputs, push)) return false;
	inputs[0] = scene;

	push.legacy = IsLegacy() ? 1 : 0;
	push.classic = IsClassic() ? 1 : 0;
	push.interior = composite && compositeInteriorShadow ? 1 : 0;
	push.mode = !composite ? 0 : compositeInteriorShadow ? 2 : 1;
	push.fogLayout = layout;
	const bool ao = composite && compositeAO && compositeAOTexture;
	if (composite) {
		push.compositeFlags = D3DXVECTOR4(compositeShadow ? 1.0f : 0.0f, ao ? 1.0f : 0.0f, (ao && compositeSkyTexture) ? 1.0f : 0.0f,
			compositeInteriorShadow ? compositeRound : 0.0f);
		IDirect3DTexture9* sunFar = compositeShadow && !compositeInteriorShadow ? SunFarFor(true) : SunFarFor(false);
		push.sunFarFlags = D3DXVECTOR4(sunFar ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		if (sunFar) inputs[12] = sunFar;
		D3DSURFACE_DESC aoDesc = {};
		if (ao && SUCCEEDED(compositeAOTexture->GetLevelDesc(0, &aoDesc)))
			push.compositeAOTexel = D3DXVECTOR4(1.0f / aoDesc.Width, 1.0f / aoDesc.Height, AmbientOcclusionEffect::SkyTakeComposite().x, AmbientOcclusionEffect::SkyTakeComposite().y);
		if (ao) inputs[10] = compositeAOTexture;
		if (ao && compositeSkyTexture) inputs[11] = compositeSkyTexture;
	}
	grid = gridTexture && ParameterHandle(paramGridFlags, "NVR_FogGridFlags");
	push.gridFlags = grid ? D3DXVECTOR4(1.0f, 1.0f, 0.0f, 0.0f) : D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	if (grid) inputs[7] = gridTexture;

	char suffix[32];
	_snprintf_s(suffix, sizeof(suffix), _TRUNCATE, "l%uc%ui%up%u", push.legacy, push.classic, push.interior, push.physicalSky);
	ID3D9NvrExtPipeline* estimate = ExtVariant(extVariants, extEstimate, "VolumetricFogEstimate", suffix, [](const char* name) {
		NVR_EXT_BINDING_DESC bindings[8 + 4];
		const UINT count = FogExtBindings(bindings, 8);
		return DxvkCompute::CreateGraphicsPipeline(name, bindings, count, sizeof(ExtFogPush), true, 2);
	});
	if (!estimate) return false;
	NVR_EXT_RESOURCE_BINDING resources[8 + 4];
	const UINT count = FogExtResources(resources, inputs, 8, extUniforms.Buffer);
	if (!count) return false;
	GpuTimer* passTimers = FogPassTimers();
	{
		GpuProfileScope gpuPass(passTimers[0], Device);
		if (!ExtDrawTo(estimate, fogSurface, 2, resources, count, &push, sizeof(push))) return false;
	}
	inputs[8] = fogTexture[0];
	inputs[9] = fogTexture[1];
	GpuProfileScope gpuPass(passTimers[1], Device);
	return ExtApplyPass(Device, RenderTarget, RenderedSurface, nullptr, inputs, push, false);
}

bool VolumetricFogEffect::ExtShadowAO(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, bool shadow, IDirect3DTexture9* aoTexture) {
	IDirect3DBaseTexture9* inputs[ExtInputCount];
	ExtFogPush push;
	if (!ExtSetup(inputs, push)) return false;
	inputs[0] = TheTextureManager->RenderedTexture;
	IDirect3DTexture9* sky = aoTexture ? compositeSkyTexture : nullptr;
	push.mode = 3;
	push.compositeFlags = D3DXVECTOR4(shadow ? 1.0f : 0.0f, aoTexture ? 1.0f : 0.0f, sky ? 1.0f : 0.0f, 0.0f);
	IDirect3DTexture9* sunFar = SunFarFor(shadow);
	push.sunFarFlags = D3DXVECTOR4(sunFar ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
	if (sunFar) inputs[12] = sunFar;
	D3DSURFACE_DESC aoDesc = {};
	if (aoTexture && SUCCEEDED(aoTexture->GetLevelDesc(0, &aoDesc)))
		push.compositeAOTexel = D3DXVECTOR4(1.0f / aoDesc.Width, 1.0f / aoDesc.Height, AmbientOcclusionEffect::SkyTakeComposite().x, AmbientOcclusionEffect::SkyTakeComposite().y);
	if (aoTexture) inputs[10] = aoTexture;
	if (sky) inputs[11] = sky;
	return ExtApplyPass(Device, RenderTarget, RenderedSurface, nullptr, inputs, push, false);
}

bool VolumetricFogEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (ClearRenderTarget || (SourceBuffer && SourceBuffer == RenderTarget)) return false;
	D3DXTECHNIQUE_DESC description = {};
	D3DXHANDLE technique = Effect->GetTechnique(techniqueIndex);
	if (!technique || FAILED(Effect->GetTechniqueDesc(technique, &description)) || description.Passes != 1) return false;
	const char* name = description.Name ? description.Name : "";
	const bool legacy = !strcmp(name, "VolumetricFogLegacy"), classic = !strcmp(name, "VolumetricFogClassic");
	if (*name && !legacy && !classic) return false;
	IDirect3DBaseTexture9* inputs[ExtInputCount];
	ExtFogPush push;
	if (!ExtSetup(inputs, push)) return false;
	inputs[0] = TheTextureManager->SourceTexture;
	push.mode = 4;
	push.legacy = legacy ? 1 : 0;
	push.classic = classic ? 1 : 0;
	return ExtApplyPass(Device, RenderTarget, RenderedSurface, SourceBuffer, inputs, push, true);
}
