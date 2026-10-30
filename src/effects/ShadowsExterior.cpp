#include "ShadowsExterior.h"
#include "../core/TestTools.h"	// UNOFFICIAL: NVR_TEST_TOOLS (SkipWhenFadedCheck)
#include "../core/LampFixtures.h"	// UNOFFICIAL 2026-10-20: LampModelOwnDistance
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-25 (NVR DXVK)
#include "../core/SunDepthFit.h"	// UNOFFICIAL 2026-10-27 FitToDepth
#include "../core/RtSunShadows.h"	// UNOFFICIAL 2026-10-28 RT sun shadows

// UNOFFICIAL 2026-10-16 lamp contact shadows (PointShadows.fx LampContactPS, technique 2; drawn by ShaderManager after the lamp
// pass): every number the shader needs (see the constants' comments there). Kept out of ShadowsExterior.h (widely included).
static D3DXVECTOR4 LampContact(0.0f, 40.0f, 12.0f, 0.5f), LampContactFade(25.0f, 0.05f, 3000.0f, 1.0f / 900.0f),
	LampContactBias(0.3f, 0.002f, 0.001f, 0.2f), LampContactNoise(0.0f, 0.0f, 52.9829189f, 0.0f);
float LampContactStrength() { return LampContact.x; }
// UNOFFICIAL 2026-10-16 MaskHalfRes ([Shaders.ShadowsInteriors.Forward]): the lamp mask at half resolution (PointShadows.fx
// LampMaskHalf + LampMaskUpsample; ShaderManager draws them). R = mask, G = depth.
static bool MaskHalfResOn = false;
static IDirect3DTexture9* MaskHalfTexture = nullptr;
static IDirect3DSurface9* MaskHalfSurface = nullptr;
bool LampMaskHalfRes() { return MaskHalfResOn && MaskHalfTexture && MaskHalfSurface; }
IDirect3DTexture9* LampMaskHalfTexture() { return MaskHalfTexture; }
IDirect3DSurface9* LampMaskHalfSurface() { return MaskHalfSurface; }

// UNOFFICIAL 2026-10-27 [Shaders.ShadowsExteriors.ScreenSpace] NearFade: TESR_ShadowScreenSpaceFade (SunShadows.fx ContactNearFade; the
// Vulkan passes take x, y in their flags.y, z): contact shadows none nearer than x, full from y (view depth). Here, not in Constants, so
// the header (and everything that includes it) stays as it was.
static D3DXVECTOR4 ScreenSpaceFade(0.0f, 0.0f, 0.0f, 0.0f);
static float ScreenSpaceNearFade = 0.0f;	// the setting (UpdateSettings); ScreenSpaceFade is made from it each frame (UpdateConstants)

void ShadowsExteriorEffect::UpdateConstants() {

	// UNOFFICIAL 2026-10-28 [Shaders.Terrain.Displacement] ContactShadowsOutside (tester: a crusty, darker ring round the player on the
	// tessellated land): while the land's bumps are on, the contact shadows start where the bumps end (DxvkCompute::DisplaceContactFade),
	// or at NearFade if that is farther. Off, or no bumps: NearFade alone, as before.
	ScreenSpaceFade = D3DXVECTOR4(ScreenSpaceNearFade * 0.5f, ScreenSpaceNearFade, 0.0f, 0.0f);
	float contactStart = 0.0f, contactEnd = 0.0f;
	if (DxvkCompute::DisplaceContactFade(contactStart, contactEnd)) {
		ScreenSpaceFade.x = (std::max)(ScreenSpaceFade.x, contactStart);
		ScreenSpaceFade.y = (std::max)(ScreenSpaceFade.y, contactEnd);
	}

	Constants.ShadowFade.x = 0; // Fade 1.0 == no shadows
	if (TheShaderManager->GameState.isExterior) {
		Constants.ShadowFade.x = smoothStep(0.5f, 0.1f, abs(TheShaderManager->GameState.dayLight - 0.5f)); // fade shadows to 0 at sunrise/sunset.  

		TimeGlobals* GameTimeGlobals = TimeGlobals::Get();
		float DaysPassed = GameTimeGlobals->GameDaysPassed ? GameTimeGlobals->GameDaysPassed->data : 1.0f;

		if(TheShaderManager->GameState.isDayTime < 0.5f) {
			// at night time, fade based on moonphase
			// moonphase goes from 0 to 8
			float MoonPhase = (fmod(DaysPassed, 8 * Tes->sky->firstClimate->phaseLength & 0x3F)) / (Tes->sky->firstClimate->phaseLength & 0x3F);
			const float moonPhaseDays = MoonPhase;	// UNOFFICIAL 2026-10-16: for the NIGHT STATE line below

			float PI = 3.1416f; // use cos curve to fade moon light shadows strength
			MoonPhase = std::lerp(-PI, PI, MoonPhase / 8) - PI / 4; // map moonphase to 1/2PI/2PI + 1/2

			// map MoonVisibility to MinNightDarkness/1 range
			float nightMinDarkness = 1 - Settings.Exteriors.NightMinDarkness;
			float MoonVisibility = std::lerp((float)0.0, nightMinDarkness, (float)(cos(MoonPhase) * 0.5 + 0.5));
			Constants.ShadowFade.x = std::lerp(MoonVisibility, (float)1.0, Constants.ShadowFade.x);

			// UNOFFICIAL 2026-10-16 (night cost study): what the night skips (ShadowsAtNight, ContactHardening AtNight) run
			// under, logged when it changes: the moon phase sets how strong the moon's shadows are (ShadowFade.x, 1 = none).
			static float loggedFade = -1.0f, loggedDayLight = -1.0f;
			if (fabsf(Constants.ShadowFade.x - loggedFade) > 0.01f || (TheShaderManager->GameState.dayLight <= 0.0f) != (loggedDayLight <= 0.0f)) {
				Logger::Log("NIGHT STATE: days passed %.2f, moon phase %.2f of 8 (phase length %d days), sun shadow fade %.3f (1 = none), dayLight %.2f, sun colour %.3f %.3f %.3f",
					DaysPassed, moonPhaseDays, (int)(Tes->sky->firstClimate->phaseLength & 0x3F), Constants.ShadowFade.x,
					TheShaderManager->GameState.dayLight, TheShaderManager->ShaderConst.sunColor.x, TheShaderManager->ShaderConst.sunColor.y,
					TheShaderManager->ShaderConst.sunColor.z);
				loggedFade = Constants.ShadowFade.x;
				loggedDayLight = TheShaderManager->GameState.dayLight;
			}
		}

		// pass the enabled/disabled property of the pointlight shadows to the shadowfade constant
		// (UNOFFICIAL 2026-10-13: every frame; it was only on isDayTimeChanged, so switching UsePointShadowsDay/Night in the
		// menu did nothing until the next dusk or dawn)
		bool usePointLights = (TheShaderManager->GameState.isDayTime > 0.5) ? Settings.Exteriors.UsePointShadowsDay: Settings.Exteriors.UsePointShadowsNight;
		Constants.ShadowFade.z = usePointLights;
		Constants.ShadowFade.y = Settings.Exteriors.Enabled && Enabled;
		Constants.ShadowFade.w = Constants.ShadowMapRadius.w; //furthest distance for point lights shadows
		// UNOFFICIAL 2026-10-27 FitToDepth: the lamp shadows' reach stays the unfitted one (the fit may end the cascades sooner).
		if (SunDepthFit::Enabled && SunDepthFit::UnfittedRadiusW > 0.0f) Constants.ShadowFade.w = SunDepthFit::UnfittedRadiusW;

		// Update constants used by shadow shaders: x=quality, y=darkness
		Constants.Data.x = Settings.Exteriors.Quality;
		//if (Enabled) Constants.ShadowData->x = -1; // Disable the forward shadowing
		Constants.Data.y = Settings.Exteriors.Darkness;

		// Mode and format data. x=mode, y=bits per pixel
		Constants.FormatData.x = Settings.ShadowMaps.Mode;
		Constants.FormatData.y = Settings.ShadowMaps.FormatBits;

		// z: are we in an exterior cell right now. GetSunShadow reads this on its own --
		// ShadowFade.y means something different indoors (interior point-shadows enabled,
		// not sun shadows enabled), so the sun-shadow path can't share it. See the interior
		// branch below.
		Constants.FormatData.z = 1.0f;

		// UNOFFICIAL 2026-10-16 SkipWhenFaded (dawn cost study, work/bench/foundations/dawncost): around sunrise and sunset
		// (dayLight 0.4-0.6) the fade above is exactly 1, and every lit shader's sun shadow comes out as exactly 1 whatever
		// the cascades hold (Shadow.hlsl GetSunShadow ends with lerp(shadow, 1, fade)). The forward path's own off switch,
		// read first there, returns that 1 without the cascade lookups. No shader changes.
		SunFaded = Settings.Exteriors.SkipWhenFaded && Constants.ShadowFade.x >= 1.0f;
		Constants.ForwardData.x = (!Settings.Exteriors.ForwardShadows || SunFaded) ? 1.0f : 0.0f;
		// UNOFFICIAL 2026-10-28 RT sun shadows Mode 1 (replace): the object shaders' cascade lookups off; the deferred apply does the
		// cascades (faded out inside the RT range) and the traced visibility is multiplied in after it (core/RtSunShadows.cpp).
		if (RtSunShadows::Replacing()) Constants.ForwardData.x = 1.0f;
		RtSunShadows::ReplaceRange();
#if NVR_TEST_TOOLS
		// DIAGNOSTIC SkipWhenFadedCheck 3: the lookups off at any hour (what they cost; by day the sun shadows leave the lit
		// shaders, so the picture changes).
		if (Settings.Exteriors.SkipWhenFadedCheck == 3) Constants.ForwardData.x = 1.0f;
#endif

		// UNOFFICIAL 2026-10-16: logged when it changes, with the game hours the fade is 1 in (ShaderManager's dayLight ramps:
		// sunrise from an hour before the climate's sunrise to an hour before its end, sunset an hour after; fade 1 = dayLight
		// 0.4-0.6 of either ramp).
		const int fadeState = (Constants.ShadowFade.x >= 1.0f ? 1 : 0) | (SunFaded ? 2 : 0);
		static int loggedFadeState = -1;
		if (fadeState != loggedFadeState && Tes && Tes->sky) {
			Sky* sky = Tes->sky;
			const float riseBegin = sky->GetSunriseBegin(), riseEnd = sky->GetSunriseEnd(), setBegin = sky->GetSunsetBegin(), setEnd = sky->GetSunsetEnd();
			TimeGlobals* time = TimeGlobals::Get();
			Logger::Log("SUN FADE STATE: game hour %.3f, dayLight %.3f, sun shadow fade %.4f (1 = none), SkipWhenFaded %s: sun shadow lookups %s. "
				"The fade is 1 from %.2f to %.2f (dawn) and %.2f to %.2f (dusk), game hours.",
				time && time->GameHour ? fmod(time->GameHour->data, 24.0f) : -1.0f, TheShaderManager->GameState.dayLight, Constants.ShadowFade.x,
				Settings.Exteriors.SkipWhenFaded ? "on" : "off", SunFaded ? "SKIPPED" : "run",
				riseBegin - 1.0f + 0.4f * (riseEnd - riseBegin), riseBegin - 1.0f + 0.6f * (riseEnd - riseBegin),
				setEnd + 1.0f - 0.6f * (setEnd - setBegin), setEnd + 1.0f - 0.4f * (setEnd - setBegin));
			loggedFadeState = fadeState;
		}
	}
	else {
		SunFaded = false;	// UNOFFICIAL 2026-10-16 SkipWhenFaded: nothing to skip indoors
		Constants.ForwardData.x = Settings.Exteriors.ForwardShadows ? 0.0f : 1.0f;
		// pass the enabled/disabled property of the shadow maps to the shadowfade constant
		Constants.ShadowFade.y = TheShaderManager->Effects.ShadowsInteriors->Enabled;
		Constants.ShadowFade.z = 1; // z enables point lights
		Constants.ShadowFade.w = Settings.Interiors.DrawDistance; //furthest distance for point lights shadows

		// Sun shadows never apply indoors -- unlike ShadowFade.y, this one exists solely for
		// GetSunShadow, so it's safe to just say "no" here rather than borrow another flag.
		Constants.FormatData.z = 0.0f;

		// Update constants used by shadow shaders: x=quality, y=darkness
		Constants.Data.x = Settings.Interiors.Quality;
		//if (TheShaderManager->Effects.ShadowsInteriors->Enabled) Constants.Data.x = -1; // Disable the forward shadowing
		Constants.Data.y = Settings.Interiors.Darkness;
		Constants.Data.z = 1.0f / (float)Settings.Interiors.ShadowCubeMapSize;
	}

	// Force-rebind FormatData/ForwardData directly, once per frame, bypassing the
	// per-shader "bound by name" constant table.
	//
	// GetSunShadow's gates on TESR_ShadowFormatData.z (interior/exterior) and
	// TESR_ShadowForwardData.x (forward suppressed) live inside the
	// !DIFFUSE && !POINT block in ObjectTemplate.hlsl. A DIFFUSE- or POINT-lit
	// permutation never reaches that code, so its own compiled constant table
	// never lists these names -- ShaderRecord::CreateCT has nothing to bind, and
	// SetCT() never issues the SetPixelShaderConstantF that would refresh c129/
	// c133 for that draw call. D3D9 pixel shader constant registers persist raw
	// values across draw calls (they are not per-shader, per-draw state), so
	// that object silently inherits whatever an earlier, unrelated draw left in
	// those registers -- which is how exterior shadow state was observed to
	// stick after walking into an interior lit mostly by DIFFUSE/POINT objects.
	//
	// Both values are frame-invariant (same for every object drawn this frame),
	// so one unconditional bind here, ahead of the frame's world geometry, is
	// sufficient: any shader that DOES list these names will simply rewrite them
	// with the identical value when it draws.
	TheRenderManager->device->SetPixelShaderConstantF(129, (const float*)&Constants.FormatData, 1);
	TheRenderManager->device->SetPixelShaderConstantF(133, (const float*)&Constants.ForwardData, 1);
	UpdateLightColors();	// UNOFFICIAL 2026-10-19 (SunFar)
}

// UNOFFICIAL 2026-10-19, after Conor's (mrfury97) ShadowsExteriorEffect::UpdateLightColors (6043062a, GPL-3.0-or-later): the sun and
// ambient colours exactly as BSShaderLightingProperty::SetLight1x2x (0xB70820) hands them to the object shaders as PSLightColor[0]
// and AmbientColor, for the SunFar pass to split a pixel's light the way the forward path does. They are not the weather's raw
// colours (TESR_SunColor, TESR_SunAmbient): both are scaled by the light's dimmer, the sun by the HDR sunlight dimmer outdoors, and
// the ambient floored at fMinAmbient. Left out are the per-object factors SetLight also applies -- forced darkness and the LOD
// dimmers -- which a screen-space pass cannot know and which are 1 on almost everything.
void ShadowsExteriorEffect::UpdateLightColors() {
	static const bool* bHDR = (const bool*)0x11F941E;				// BSShaderManager::bHDR
	static const bool* bInterior = (const bool*)0x11F9427;			// BSShaderManager::bInterior
	static const float* fSunlightDimmer = (const float*)0x11F9190;	// BSShaderManager::fSunlightDimmer
	static const float* fMinAmbient = (const float*)0x11F947C;		// BSShaderManager::fMinAmbient

	NiDirectionalLight* sun = Tes ? Tes->directionalLight : nullptr;
	if (!sun) {
		SunLight = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
		AmbientLight = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}
	const float dimmer = *bHDR ? sun->Dimmer : (std::min)(sun->Dimmer, 1.0f);
	D3DXVECTOR3 ambient(sun->Amb.r * dimmer, sun->Amb.g * dimmer, sun->Amb.b * dimmer);
	if (*fMinAmbient > 0.0f) {
		const float luminance = ambient.x * 0.33f + ambient.y * 0.34f + ambient.z * 0.33f;
		if (luminance > 0.0f) {
			const float boost = luminance <= *fMinAmbient ? *fMinAmbient / luminance : 1.0f;
			ambient = (ambient + D3DXVECTOR3(0.1f, 0.1f, 0.1f)) * boost;
		}
		else ambient = D3DXVECTOR3(*fMinAmbient, *fMinAmbient, *fMinAmbient);
	}
	D3DXVECTOR3 sunColor(sun->Diff.r * dimmer, sun->Diff.g * dimmer, sun->Diff.b * dimmer);
	if (*bHDR && !*bInterior) sunColor *= *fSunlightDimmer;
	SunLight = D3DXVECTOR4(sunColor.x, sunColor.y, sunColor.z, 1.0f);
	AmbientLight = D3DXVECTOR4(ambient.x, ambient.y, ambient.z, 1.0f);
}

bool ShadowsExteriorEffect::UpdateSettingsFromQuality(int quality) {
	bool cascadeSettingsChanged = false;
	
	D3DFORMAT oldFormat = Settings.ShadowMaps.Format;
	int oldCascadeResolution = Settings.ShadowMaps.CascadeResolution;
	bool oldMSAA = Settings.ShadowMaps.MSAA;
	bool oldPrefilter = Settings.ShadowMaps.Prefilter;

	// Read for every quality level: it removes doubled and ghost shadows, it does not trade quality.
	Settings.ShadowMaps.LodSkipLoaded = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "LodSkipLoaded");
	// UNOFFICIAL 2026-10-28 CasterFadeMin (CasterView: distant trees' leaves under a fade node at 0.00, drawn on screen yet never casting;
	// their shadow snapped on at 0.75 as they came nearer). Every quality.
	Settings.ShadowMaps.CasterFadeMin = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ShadowMaps", "CasterFadeMin"), 0.0f, 1.0f);
	
	// Custom settings.
	if (quality < 0 || quality > 3) {
		// (UNOFFICIAL 2026-10-27: Mode and Format are no longer read: PCF always, below.)
		Settings.ShadowMaps.FormatBits = 0;

		Settings.ShadowMaps.Distance = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ShadowMaps", "Distance"), 100.0f);
		Settings.ShadowMaps.CascadeLambda = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ShadowMaps", "CascadeLambda"), 0.0f, 1.0f);
		// UNOFFICIAL 2026-10-15 SmoothFilter (TESR_ShadowBlur.z, read by Includes/Shadow.hlsl ShadowPCF3x3), live
		Settings.ShadowMaps.SmoothFilter = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "SmoothFilter") != 0;
		Constants.ShadowBlur.z = Settings.ShadowMaps.SmoothFilter ? 1.0f : 0.0f;
		Settings.ShadowMaps.LimitFrequency = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "LimitFrequency");

		// 0..5 -> 1024, 1536, 2048, 2560, 3072, 3584 per cascade. The atlas packs 2x2 cascades,
		// so the texture is twice this in each dimension: index 5 is a 7168x7168 atlas.
		// UNOFFICIAL 2026-10-15: 6 -> 4096 (an 8192 x 8192 atlas, 256 MB as PCF's R32F), Max Quality's.
		int cascadeStep = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "CascadeResolution"), 0, 6);
		ULONG cascadeRes = (ULONG)(cascadeStep + 2) * 512;

		// Cap against what the device can actually create. InitTexture does not report failure,
		// so an atlas past the hardware limit would come back null and read as frozen or missing
		// shadows rather than as an error.
		if (TheRenderManager && TheRenderManager->device) {
			D3DCAPS9 caps;
			if (SUCCEEDED(TheRenderManager->device->GetDeviceCaps(&caps))) {
				ULONG maxCascade = (ULONG)min(caps.MaxTextureWidth, caps.MaxTextureHeight) / 2;
				while (cascadeRes > maxCascade && cascadeRes > 1024) cascadeRes -= 512;
			}
		}

		Settings.ShadowMaps.CascadeResolution = cascadeRes;

		Settings.ShadowMaps.MSAA = 0;	// (PCF: below)

		// Mipmaps and anisotropy are disabled due to deferred shadows - derivatives are messed up and causing artifacts.
		// https://aras-p.info/blog/2010/01/07/screenspace-vs-mip-mapping/
		/*bool oldMips = Settings.ShadowMaps.Mipmaps;
		Settings.ShadowMaps.Mipmaps = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "Mipmaps");

		if (oldMips != Settings.ShadowMaps.Mipmaps)
			cascadeSettingsChanged = true;

		Settings.ShadowMaps.Anisotropy = (std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "Anisotropy"), 0, 2)) * 8;*/

		Settings.ShadowMaps.Prefilter = 0;	// (PCF: below)

		for (int shadowType = 0; shadowType <= MapLod; shadowType++) {
			char sectionName[256] = "Shaders.ShadowsExteriors.Forms";
			switch (shadowType) {
			case MapNear:
				strcat(sectionName, "Near");
				break;
			case MapMiddle:
				strcat(sectionName, "Middle");
				break;
			case MapFar:
				strcat(sectionName, "Far");
				break;
			case MapLod:
				strcat(sectionName, "Lod");
				break;
			case MapOrtho:
				strcat(sectionName, "Ortho");
				break;
			}
			ShadowMapSettings* ShadowMap = &ShadowMaps[shadowType];

			ShadowMap->Forms.AlphaEnabled = TheSettingManager->GetSettingI(sectionName, "AlphaEnabled");
			ShadowMap->Forms.Activators = TheSettingManager->GetSettingI(sectionName, "Activators");
			ShadowMap->Forms.Actors = TheSettingManager->GetSettingI(sectionName, "Actors");
			ShadowMap->Forms.Apparatus = TheSettingManager->GetSettingI(sectionName, "Apparatus");
			ShadowMap->Forms.Books = TheSettingManager->GetSettingI(sectionName, "Books");
			ShadowMap->Forms.Containers = TheSettingManager->GetSettingI(sectionName, "Containers");
			ShadowMap->Forms.Doors = TheSettingManager->GetSettingI(sectionName, "Doors");
			ShadowMap->Forms.Furniture = TheSettingManager->GetSettingI(sectionName, "Furniture");
			ShadowMap->Forms.Misc = TheSettingManager->GetSettingI(sectionName, "Misc");
			ShadowMap->Forms.Statics = TheSettingManager->GetSettingI(sectionName, "Statics");
			ShadowMap->Forms.Terrain = TheSettingManager->GetSettingI(sectionName, "Terrain");
			ShadowMap->Forms.Trees = TheSettingManager->GetSettingI(sectionName, "Trees");
			ShadowMap->Forms.Lod = TheSettingManager->GetSettingI(sectionName, "Lod");
			ShadowMap->Forms.MinRadius = TheSettingManager->GetSettingF(sectionName, "MinRadius");
			ShadowMap->Forms.OrigMinRadius = TheSettingManager->GetSettingF(sectionName, "MinRadius");
		};
	}
	else {
		for (int shadowType = 0; shadowType <= MapLod; shadowType++) {
			char sectionName[256] = "Shaders.ShadowsExteriors.Forms";
			switch (shadowType) {
			case MapNear:
				strcat(sectionName, "Near");
				break;
			case MapMiddle:
				strcat(sectionName, "Middle");
				break;
			case MapFar:
				strcat(sectionName, "Far");
				break;
			case MapLod:
				strcat(sectionName, "Lod");
				break;
			case MapOrtho:
				strcat(sectionName, "Ortho");
				break;
			}
			ShadowMapSettings* ShadowMap = &ShadowMaps[shadowType];

			ShadowMap->Forms.AlphaEnabled = (shadowType == MapOrtho) ? 0 : 1;
			ShadowMap->Forms.Activators = (shadowType < MapLod) ? 1 : 0;
			ShadowMap->Forms.Actors = (shadowType < MapLod) ? 1 : 0;
			ShadowMap->Forms.Apparatus = 0;
			ShadowMap->Forms.Books = (shadowType < MapFar) ? 1 : 0;
			ShadowMap->Forms.Containers = (shadowType < MapLod) ? 1 : 0;
			ShadowMap->Forms.Doors = (shadowType == MapOrtho) ? 0 : 1;
			ShadowMap->Forms.Furniture = (shadowType < MapLod) ? 1 : 0;
			ShadowMap->Forms.Misc = 1;
			ShadowMap->Forms.Statics = 1;
			ShadowMap->Forms.Terrain = 1;
			ShadowMap->Forms.Trees = 1;
			ShadowMap->Forms.Lod = quality < 2 ? 0 : 1;
			ShadowMap->Forms.MinRadius = (MapFar <= shadowType && shadowType <= MapLod) ? 10.0f : 1.0f;
			ShadowMap->Forms.OrigMinRadius = (MapFar <= shadowType && shadowType <= MapLod) ? 10.0f : 1.0f;
		};

		Settings.ShadowMaps.CascadeLambda = 0.9f;
		Settings.ShadowMaps.LimitFrequency = 1;
		Settings.ShadowMaps.MSAA = 1;
		Settings.ShadowMaps.Prefilter = 1;

		switch (quality) {
		case 0:
			Settings.ShadowMaps.Distance = 3000.0f;
			Settings.ShadowMaps.CascadeResolution = 1024;
			Settings.ShadowMaps.MSAA = 0;
			break;
		case 1:
			Settings.ShadowMaps.Distance = 4000.0f;
			Settings.ShadowMaps.CascadeResolution = 1024;
			break;
		case 2:
			Settings.ShadowMaps.Distance = 4500.0f;
			Settings.ShadowMaps.CascadeResolution = 2048;
			break;
		case 3:
			Settings.ShadowMaps.Distance = 6000.0f;
			Settings.ShadowMaps.CascadeResolution = 2048;
			break;
		}
	}
	
	// UNOFFICIAL 2026-10-27 (user: "force pcf from now on no other options"): the sun shadow maps are always Mode 3 (PCF), at every
	// Quality and whatever a settings file says. A tester's file saved by an earlier build still held Mode 2 (EVSM4, the default
	// before 2026-10-14), and Quality 0-3 picked VSM / EVSM themselves: every character-shadow fix since is PCF-only (and EVSM has
	// no normal offset), so faces and clothing shaded their own triangles there. Quality 0-3 keep their distance and resolution.
	// UNOFFICIAL 2026-10-14 Mode 3 (PCF): the atlas holds the depth itself, compared at lookup (Shadow.hlsl ShadowPCF3x3), so
	// there is nothing to prefilter (the blur is what made VSM/EVSM soft, and its cost), and MSAA's resolve would average depths
	// across edges (shadow acne and gaps), so both are off.
	Settings.ShadowMaps.Mode = 3;
	Settings.ShadowMaps.FormatBits = 0;
	Settings.ShadowMaps.Prefilter = 0;
	Settings.ShadowMaps.MSAA = 0;
	Settings.ShadowMaps.Format = Formats[Settings.ShadowMaps.Mode][Settings.ShadowMaps.FormatBits];

	// Set clear color for clearing the cascades.
	float pos = exp(Settings.ShadowMaps.FormatBits ? 40.0f : 5.54f);
	float neg = -exp(-5.0f);

	for (int shadowType = 0; shadowType <= MapLod; shadowType++) {
		ShadowMapSettings* ShadowMap = &ShadowMaps[shadowType];
		ShadowMap->CustomClearRequired = false;

		switch (Settings.ShadowMaps.Mode) {
		case 0:
			ShadowMap->ClearColor = D3DXVECTOR4(1.0f, 1.0f, 0.0f, 1.0f);
			break;
		case 1:
			ShadowMap->ClearColor = D3DXVECTOR4(pos, neg, 0.0f, 1.0f);
			ShadowMap->CustomClearRequired = true;
			break;
		case 2:
			ShadowMap->ClearColor = D3DXVECTOR4(pos, neg, pos * pos, neg * neg);
			ShadowMap->CustomClearRequired = true;
			break;
		default:
			ShadowMap->ClearColor = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 1.0f);
		}
	}
	ShadowMaps[MapOrtho].CustomClearRequired = false;

	if (oldFormat != Settings.ShadowMaps.Format)
		cascadeSettingsChanged = true;

	if (oldCascadeResolution != 0 && oldCascadeResolution != Settings.ShadowMaps.CascadeResolution)
		cascadeSettingsChanged = true;

	if (oldMSAA != Settings.ShadowMaps.MSAA)
		cascadeSettingsChanged = true;

	// The prefilter's ping-pong scratch target is only allocated when the prefilter is on,
	// so toggling it has to go through RecreateTextures.
	if (oldPrefilter != Settings.ShadowMaps.Prefilter)
		cascadeSettingsChanged = true;

	return cascadeSettingsChanged;
}

void ShadowsExteriorEffect::UpdateSettings() {

	Constants.ScreenSpaceData.x = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ScreenSpace", "Enabled") && Enabled;
	Constants.ScreenSpaceData.y = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "BlurRadius");
	Constants.ScreenSpaceData.z = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "RenderDistance");
	Constants.ScreenSpaceData.w = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "Intensity"), 0.0f);
	const float nearFade = (std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ScreenSpace", "NearFade"), 0.0f);
	ScreenSpaceNearFade = nearFade;
	ScreenSpaceFade = D3DXVECTOR4(nearFade * 0.5f, nearFade, 0.0f, 0.0f);
	Settings.ScreenSpace.Sharp = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ScreenSpace", "Sharp") != 0;	// UNOFFICIAL

	// Sun smoothing settings.
	Settings.SunSmoothing.SmoothSun = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.SunSmoothing", "SmoothSun");
	Settings.SunSmoothing.QuantizeSun = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.SunSmoothing", "QuantizeSun");
	Settings.SunSmoothing.SmoothingFactor = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "SmoothingFactor"), 0.0f, 1.0f);
	Settings.SunSmoothing.YawStepSize = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "YawStepSize"), 0.0f, 15.0f);
	Settings.SunSmoothing.PitchStepSize = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "PitchStepSize"), 0.0f, 15.0f);
	Settings.SunSmoothing.MaxJumpAngle = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "MaxJumpAngle"), 5.0f, 30.0f);
	Settings.SunSmoothing.GlideSeconds = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.SunSmoothing", "GlideSeconds"), 0.0f, 10.0f);
	Settings.SunSmoothing.CrossFade = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.SunSmoothing", "CrossFade");

	// Generic exterior shadows settings
	Settings.Exteriors.Enabled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "Enabled");
	Settings.Exteriors.ForwardShadows = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "ForwardShadows");

	// Runtime half of the forward/deferred switch.
	//
	// The FORWARD_SHADOWS macro decides whether the forward code is COMPILED IN; this decides
	// whether it RUNS. Both the game shaders and SunShadows.fx read it, so the two halves hand
	// over in the same frame -- which matters, because the alternative is both paths applying
	// shadows at once. A macro alone cannot do this: game shaders have no runtime reload path
	// (ShaderCollection::SwitchShader is a stub), and the effect reload path is never triggered
	// (nothing ever sets ShaderManager::EffectReloadQueued), so the macro is frozen at whatever
	// it was when the shader was first compiled.
	Constants.ForwardData.x = Settings.Exteriors.ForwardShadows ? 0.0f : 1.0f;
	// UNOFFICIAL 2026-10-20 SmoothCharacterShadows (P82 testers: sun shadows on faces blocky and popping as the head turns):
	// 1 = the skin shaders' sun and lamp shadow lookups (and the old skin's sky light) take the smooth vertex normal their vertex
	// shader sends instead of the flat triangle normal (Includes/Shadow.hlsl ShadowLookupNormal). Live: a constant, bound with .x.
	Settings.Exteriors.SmoothCharacterShadows = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "SmoothCharacterShadows") != 0;
	Constants.ForwardData.y = Settings.Exteriors.SmoothCharacterShadows ? 1.0f : 0.0f;
	// CharacterShadowSoftness: with SmoothCharacterShadows, the least half-width a sun shadow edge on skin fades over, in world units
	// (Shadow.hlsl ShadowMinPenumbra; contact hardening's disc lookup, so it needs that compiled in, as it is by default).
	Settings.Exteriors.CharacterShadowSoftness = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Main", "CharacterShadowSoftness"), 0.0f, 4.0f);
	Constants.ForwardData.z = Settings.Exteriors.SmoothCharacterShadows ? Settings.Exteriors.CharacterShadowSoftness : 0.0f;
	// UNOFFICIAL 2026-10-22 CharacterShadowReach (P82 tester, after the cascade handover: hair and nose shadows faint or missing on faces
	// close up in forward mode): a cap, in world units, on the slack the skin shaders' PCF sun lookups give the receiver's own surface
	// (Shadow.hlsl ShadowSlackCap: the normal offset and every depth slack, on the Near cascade; growing with the map's texel on the
	// coarser ones), so casters closer than that to the skin still cast. 0 = off, the texel slack alone. Live: TESR_ShadowBlur.w.
	Settings.Exteriors.CharacterShadowReach = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Main", "CharacterShadowReach"), 0.0f, 8.0f);
	Constants.ShadowBlur.w = Settings.Exteriors.CharacterShadowReach;
	// UNOFFICIAL 2026-10-21 HandoverBlend (P82 tester: the hair's shadow on the brow popping as the third-person camera zooms): how
	// far inside each sun cascade's edge the cross-fade into the next starts, in world units (Shadow.hlsl ShadowCascadeBands, read
	// by both the forward and the deferred lookups). 0 = the old rule, a tenth of the radius. Every Quality level; live.
	Settings.ShadowMaps.HandoverBlend = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ShadowMaps", "HandoverBlend"), 0.0f, 2000.0f);
	Constants.ForwardData.w = Settings.ShadowMaps.HandoverBlend;
	// UNOFFICIAL 2026-10-23 PlayerInNear (P82 tester, after HandoverBlend and CharacterShadowReach: the hair's, nose's and brow's sun
	// shadows on the face still fading in only as the third-person camera zooms in to about 1.5-2 m): in third person the Near cascade
	// grows (GetCascadeViewProj) to hold the player's head while the camera is within this many world units of it (420 = 6 m). 0 = off.
	Settings.ShadowMaps.PlayerInNear = std::clamp(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.ShadowMaps", "PlayerInNear"), 0.0f, 1400.0f);
	Settings.ShadowMaps.PlayerInNearLog = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "PlayerInNearLog") != 0;	// diagnostic: Near's radius once a second
	// UNOFFICIAL 2026-10-27 FitToDepth (core\SunDepthFit.h, docs/next/FORK2-SDSM.md): the sun cascades fitted to the depth on screen,
	// its debug view and log. Live; every Quality level. Off (the default): nothing is measured or changed.
	SunDepthFit::Enabled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "FitToDepth") != 0;
	SunDepthFit::DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "FitToDepthDebug"), 0, 2);
	SunDepthFit::Log = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "FitToDepthLog") != 0;
	// UNOFFICIAL 2026-10-23 CharacterShadowMap (docs/next/CHARACTER-SHADOW-MAP-DESIGN.md): the player's own small sun shadow map, read by the
	// skin and object shaders and the deferred sun pass (ShadowManager::RenderCharacterShadowMap). The switch is compiled into the shaders at
	// startup (CHARACTER_SHADOW_MAP, ShaderRecord::BuildDefines); the rest is live. Every Quality level.
	{
		const char* section = "Shaders.ShadowsExteriors.ShadowMaps";
		Settings.ShadowMaps.CharacterMap = TheSettingManager->GetSettingI(section, "CharacterShadowMap") != 0;
		const int resolution = std::clamp(TheSettingManager->GetSettingI(section, "CharacterShadowMapResolution"), 256, 4096);
		Settings.ShadowMaps.CharacterMapResolution = (resolution + 128) / 256 * 256;	// a multiple of 256
		Settings.ShadowMaps.CharacterMapFit = std::clamp(TheSettingManager->GetSettingI(section, "CharacterShadowMapFit"), 0, 1);
		Settings.ShadowMaps.CharacterMapMargin = std::clamp(TheSettingManager->GetSettingF(section, "CharacterShadowMapMargin"), 4.0f, 200.0f);
		Settings.ShadowMaps.CharacterMapBlend = std::clamp(TheSettingManager->GetSettingF(section, "CharacterShadowMapBlend"), 1.0f, 100.0f);
		Settings.ShadowMaps.CharacterMapBias = std::clamp(TheSettingManager->GetSettingF(section, "CharacterShadowMapBias"), 0.0f, 4.0f);
		Settings.ShadowMaps.CharacterMapOthers = TheSettingManager->GetSettingI(section, "CharacterShadowMapOthers") != 0;
		Settings.ShadowMaps.CharacterMapLog = TheSettingManager->GetSettingI(section, "CharacterShadowMapLog") != 0;
		Settings.ShadowMaps.CharacterMapDebug = std::clamp(TheSettingManager->GetSettingI(section, "CharacterShadowMapDebug"), 0, 3);
		if (Settings.ShadowMaps.CharacterMap && !CharacterMapCompiled) {
			static bool hinted = false;
			if (!hinted) { hinted = true; Logger::Log("UNOFFICIAL CharacterShadowMap was turned on after startup: restart the game once to apply it."); }
		}
	}
	Settings.Exteriors.Quality = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "Quality"), 0, 4);
	Settings.Exteriors.Darkness = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Main", "Darkness");
	Settings.Exteriors.NightMinDarkness = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Main", "NightMinDarkness");
	Settings.Exteriors.SkipWhenFaded = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "SkipWhenFaded") != 0;	// UNOFFICIAL 2026-10-16
	// UNOFFICIAL 2026-10-21 (sunshadow): the same picture for less CPU time (ShadowManager::RenderShadowMaps)
	Settings.Exteriors.SunSkipFlat = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "SunCascadeSkipFlat") != 0;
	Settings.Exteriors.SunOneWalk = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "SunCascadeOneWalk") != 0;
	Settings.Exteriors.SunLeanDraws = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "SunCascadeLeanDraws") != 0;
	Settings.Exteriors.OverheadSunFix = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "OverheadSunFix") != 0;	// UNOFFICIAL 2026-10-21 (sunfix)
#if NVR_TEST_TOOLS
	Settings.Exteriors.SkipWhenFadedCheck = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "SkipWhenFadedCheck"), 0, 4);
	Settings.Exteriors.CascadeStats = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "SunCascadeStats");	// UNOFFICIAL 2026-10-21
	Settings.Exteriors.SunVerify = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "SunCascadeVerify"), 0, 2);	// UNOFFICIAL 2026-10-21
#else
	Settings.Exteriors.SkipWhenFadedCheck = 0;
	Settings.Exteriors.CascadeStats = 0;
	Settings.Exteriors.SunVerify = 0;
#endif
	Settings.Exteriors.UsePointShadowsDay = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "UsePointShadowsDay");
	Settings.Exteriors.UsePointShadowsNight = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "UsePointShadowsNight");

	// Shadow maps specific configuration.
	bool cascadeSettingsChanged = UpdateSettingsFromQuality(Settings.Exteriors.Quality);

	// Ortho map.
	bool orthoSettingsChanged = false;

	int oldOrthoResolution = Settings.OrthoMap.Resolution;

	Settings.OrthoMap.Resolution = 128 * pow(2, (std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Ortho", "Resolution"), 0, 4)));
	Settings.OrthoMap.Distance = max(TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.Ortho", "Distance"), 100.0f);
	Settings.OrthoMap.LimitFrequency = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Ortho", "LimitFrequency");
	Settings.OrthoMap.ProbeDump = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Ortho", "ProbeDump");	// UNOFFICIAL test tools (OrthoProbe.h)
	// UNOFFICIAL 2026-10-17 Skylighting v2: how its ortho map is drawn (only while v2 is on; ShadowManager::RenderShadowMaps).
	Settings.OrthoMap.SkyCleanMap = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "CleanMap") != 0;
	Settings.OrthoMap.SkyMapRefresh = std::clamp(TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "MapRefresh"), 1, 100000);
	Settings.OrthoMap.SkyMoveRefresh = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "MoveRefresh"), 1.0f, 100000.0f);
	Settings.OrthoMap.SkyOwnProjection = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "OwnProjection") != 0;
	++Settings.OrthoMap.SkySettingsGeneration;

	if (oldOrthoResolution != 0 && oldOrthoResolution != Settings.OrthoMap.Resolution)
		orthoSettingsChanged = true;

	ShadowMaps[MapOrtho].Forms.AlphaEnabled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "AlphaEnabled");
	ShadowMaps[MapOrtho].Forms.Activators = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Activators");
	ShadowMaps[MapOrtho].Forms.Actors = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Actors");
	ShadowMaps[MapOrtho].Forms.Apparatus = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Apparatus");
	ShadowMaps[MapOrtho].Forms.Books = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Books");
	ShadowMaps[MapOrtho].Forms.Containers = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Containers");
	ShadowMaps[MapOrtho].Forms.Doors = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Doors");
	ShadowMaps[MapOrtho].Forms.Furniture = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Furniture");
	ShadowMaps[MapOrtho].Forms.Misc = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Misc");
	ShadowMaps[MapOrtho].Forms.Statics = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Statics");
	ShadowMaps[MapOrtho].Forms.Terrain = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Terrain");
	ShadowMaps[MapOrtho].Forms.Trees = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Trees");
	ShadowMaps[MapOrtho].Forms.Lod = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.FormsOrtho", "Lod");
	ShadowMaps[MapOrtho].Forms.MinRadius = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.FormsOrtho", "MinRadius");
	ShadowMaps[MapOrtho].Forms.OrigMinRadius = TheSettingManager->GetSettingF("Shaders.ShadowsExteriors.FormsOrtho", "MinRadius");

	// Interiors.
	Settings.Interiors.Enabled = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Enabled");
	// UNOFFICIAL: blockers near a lamp fade out of its shadow (PointShadows.fx; the forward shadows read it themselves).
	Constants.PointShadowNear.x = (std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "NearFade"), 0.0f);
	// UNOFFICIAL EdgeFade: each lamp's lit disc (and the shadows in it) fades out over this fraction of its radius instead of
	// ending on a hard circle where the clamped light amount drops (PointShadows.fx EdgeWindow).
	Constants.PointShadowNear.y = (std::min)((std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "EdgeFade"), 0.0f), 0.9f);
	// UNOFFICIAL 2026-10-16 MaskSkipFar (PointShadows.fx Shadow()): lamps skip the pixels beyond their radius (exact: they give 0 there).
	Constants.PointShadowNear.w = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "MaskSkipFar") ? 1.0f : 0.0f;
	MaskHalfResOn = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "MaskHalfRes") != 0;	// UNOFFICIAL 2026-10-16
	// UNOFFICIAL 2026-10-16 lamp contact shadows (PointShadows.fx LampContactPS).
	const char* interiorMain = "Shaders.ShadowsInteriors.Main";
	const float contactDistance = std::clamp(TheSettingManager->GetSettingF(interiorMain, "ContactDistance"), 200.0f, 20000.0f);
	LampContact = D3DXVECTOR4(std::clamp(TheSettingManager->GetSettingF(interiorMain, "ContactShadows"), 0.0f, 1.0f),
		std::clamp(TheSettingManager->GetSettingF(interiorMain, "ContactLength"), 4.0f, 200.0f),
		std::clamp(TheSettingManager->GetSettingF(interiorMain, "ContactThickness"), 1.0f, 100.0f), 0.5f);
	LampContactFade = D3DXVECTOR4(25.0f, 1.0f / 20.0f, contactDistance, 1.0f / (0.3f * contactDistance));	// in past the weapon over 25-45 units; out over the last 30%
	// Interleaved gradient noise (Jimenez 2014): frac(52.98 x frac(dot(pixel, (0.06711056, 0.00583715)))), pixel = uv x screen size.
	LampContactNoise = D3DXVECTOR4(0.06711056f * (float)(std::max)(TheRenderManager->width, (UInt32)1), 0.00583715f * (float)(std::max)(TheRenderManager->height, (UInt32)1), 52.9829189f, TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "DarkeningBlur") ? 1.0f : 0.0f);
	Settings.Interiors.Forms.AlphaEnabled = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "AlphaEnabled");
	Settings.Interiors.Forms.Activators = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Activators");
	Settings.Interiors.Forms.Actors = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Actors");
	Settings.Interiors.Forms.Apparatus = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Apparatus");
	Settings.Interiors.Forms.Books = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Books");
	Settings.Interiors.Forms.Containers = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Containers");
	Settings.Interiors.Forms.Doors = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Doors");
	Settings.Interiors.Forms.Furniture = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Furniture");
	Settings.Interiors.Forms.Misc = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Misc");
	Settings.Interiors.Forms.Statics = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Statics");
	Settings.Interiors.Forms.MinRadius = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "MinRadius");
	Settings.Interiors.Quality = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "Quality");
	Settings.Interiors.LightPoints = max(0, min(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "LightPoints"), ShadowCubeMapsMax));
	Settings.Interiors.ExteriorLightPoints = max(0, min(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "ExteriorLightPoints"), ExteriorBatchedMax));
	Settings.Interiors.TorchesCastShadows = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "TorchesCastShadows");
	// UNOFFICIAL 2026-10-16 (bug hunt): clamped -- 12 R32F cubes at 4096 are about 4.8 GB, and the size sets every cube's memory.
	Settings.Interiors.ShadowCubeMapSize = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "ShadowCubeMapSize"), 64, 2048);
	Settings.Interiors.Darkness = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "Darkness");
	Settings.Interiors.LightRadiusMult = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "LightRadiusMult");
	Settings.Interiors.DrawDistance = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "DrawDistance");
	Settings.Interiors.UseCastShadowFlag = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "UseCastShadowFlag");
	Settings.Interiors.PlayerShadowFirstPerson = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "PlayerShadowFirstPerson");
	Settings.Interiors.PlayerShadowThirdPerson = TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "PlayerShadowThirdPerson");
	// UNOFFICIAL (2026-10-07 review): the per-frame code (GetNearbyLights, PointShadowForward, PlayerLampOffset, the
	// contact hardening) used to read these by name every frame; they are read here, when settings change.
	Settings.Interiors.PlayerInsideLamp = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "PlayerInsideLamp") != 0;
	Settings.Interiors.RedrawActorsOnly = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "RedrawActorsOnly") != 0;
	Settings.Interiors.PlayerLampMargin = (std::max)(TheSettingManager->GetSettingF("Shaders.ShadowsInteriors.Main", "PlayerLampMargin"), 1.0f);
	{
		InteriorsStruct::ForwardStruct& forward = Settings.Interiors.Forward;
		const char* section = "Shaders.ShadowsInteriors.Forward";
		forward.Enabled = TheSettingManager->GetSettingI(section, "Enabled") != 0;
		forward.KeepDarkening = TheSettingManager->GetSettingI(section, "KeepDarkening") != 0;
		forward.DarkeningBlur = TheSettingManager->GetSettingI(section, "DarkeningBlur") != 0;
		forward.FirstPerson = TheSettingManager->GetSettingI(section, "FirstPerson") != 0;
		forward.ScopeFix = TheSettingManager->GetSettingI(section, "ScopeFix") != 0;
		forward.LogLamps = TheSettingManager->GetSettingI(section, "LogLamps") != 0;
		forward.LampRanking = TheSettingManager->GetSettingI(section, "LampRanking");
		forward.DebugView = TheSettingManager->GetSettingI(section, "DebugView");
		forward.LampSwitchMargin = (std::max)(0.0f, (std::min)(0.9f, TheSettingManager->GetSettingF(section, "LampSwitchMargin")));
		forward.Strength = TheSettingManager->GetSettingF(section, "Strength");
		forward.Bias = TheSettingManager->GetSettingF(section, "Bias");
		forward.NormalOffset = TheSettingManager->GetSettingF(section, "NormalOffset");
		forward.Softness = TheSettingManager->GetSettingF(section, "Softness");
		forward.FadeIn = TheSettingManager->GetSettingF(section, "FadeIn");
		forward.ShadowClusterEnabled = TheSettingManager->GetSettingI(section, "ShadowClusterEnabled") != 0;
		forward.ShadowClusterDistance = (std::max)(0.0f, (std::min)(512.0f, TheSettingManager->GetSettingF(section, "ShadowClusterDistance")));
		forward.ShadowClusterInvisibleOnly = TheSettingManager->GetSettingI(section, "ShadowClusterInvisibleOnly") != 0;	// UNOFFICIAL 2026-10-29
		forward.ShadowClusterMinLights = (std::max)(0, (std::min)(64, TheSettingManager->GetSettingI(section, "ShadowClusterMinLights")));
		forward.CameraIndependent = TheSettingManager->GetSettingI(section, "CameraIndependent") != 0;
		forward.KeepLampsInView = TheSettingManager->GetSettingI(section, "KeepLampsInView") != 0;
		forward.FillLightRadius = (std::max)(0.0f, TheSettingManager->GetSettingF(section, "FillLightRadius"));
		forward.FillLightShadowStrength = std::clamp(TheSettingManager->GetSettingF(section, "FillLightShadowStrength"), 0.0f, 1.0f);
		forward.Quality = std::clamp(TheSettingManager->GetSettingI(section, "Quality"), 0, 3);
		forward.ContactHardening = TheSettingManager->GetSettingI(section, "ContactHardening") != 0;
		forward.ContactHardeningStrength = (std::max)(0.0f, TheSettingManager->GetSettingF(section, "ContactHardeningStrength"));
		forward.ContactHardeningMinSoftness = std::clamp(TheSettingManager->GetSettingF(section, "ContactHardeningMinSoftness"), 0.0f, 1.0f);
		forward.InvisibleLightShadowStrength = std::clamp(TheSettingManager->GetSettingF(section, "InvisibleLightShadowStrength"), 0.0f, 1.0f);
		forward.FixtureDistance = std::clamp(TheSettingManager->GetSettingF(section, "FixtureDistance"), 0.0f, 512.0f);
		forward.LampSize = std::clamp(TheSettingManager->GetSettingF(section, "LampSize"), 0.0f, 64.0f);
		forward.MaxSoftness = std::clamp(TheSettingManager->GetSettingF(section, "MaxSoftness"), 1.0f, 32.0f);
		forward.LampGlassShadows = TheSettingManager->GetSettingI(section, "LampGlassShadows") != 0;
		forward.OwnGlassDistance = std::clamp(TheSettingManager->GetSettingF(section, "OwnGlassDistance"), -1.0f, 128.0f);
		forward.LampModelShadows = TheSettingManager->GetSettingI(section, "LampModelShadows") != 0;
		forward.LampModelDistance = std::clamp(TheSettingManager->GetSettingF(section, "LampModelDistance"), 0.0f, 512.0f);
		LampFixtures::OwnLightDistance = std::clamp(TheSettingManager->GetSettingF(section, "LampModelOwnDistance"), -1.0f, 128.0f);
		{	// UNOFFICIAL NoShadowLampModels (2026-10-27): the whole text (GetSettingS cuts a string at 79 characters, as ClothWind reads its lists)
			SettingManager::Configuration::ConfigNode node;
			LampFixtures::SetNoShadowWords(TheSettingManager->Config.FillNode(&node, section, "NoShadowLampModels") ?
				std::string(node.LongValue.empty() ? node.Value : node.LongValue.c_str()) : std::string());
		}
		forward.InvisibleShadowFromLamp = std::clamp(TheSettingManager->GetSettingF(section, "InvisibleShadowFromLamp"), 0.0f, 1024.0f);
		forward.InvisibleLightSize = std::clamp(TheSettingManager->GetSettingF(section, "InvisibleLightSize"), 0.0f, 128.0f);
		forward.EvenTaps = TheSettingManager->GetSettingI(section, "EvenTaps") != 0;	// compiled in at startup (ShaderRecord::BuildDefines)
		forward.MagnifiedSoftness = std::clamp(TheSettingManager->GetSettingF(section, "MagnifiedSoftness"), 0.0f, 8.0f);	// POINT_SHADOW_MAG if above 0 at startup
		forward.TexelFilter = TheSettingManager->GetSettingI(section, "TexelFilter") != 0;	// POINT_SHADOW_TEXEL, compiled in at startup
		forward.AutoNormalOffset = TheSettingManager->GetSettingI(section, "AutoNormalOffset") != 0;	// UNOFFICIAL 2026-10-24 (charfacets)
	}
	Settings.ContactHardening.Enabled = TheSettingManager->GetSettingI("Shaders.ContactHardening.Status", "Enabled") != 0;
	Settings.ContactHardening.SunSize = max(TheSettingManager->GetSettingF("Shaders.ContactHardening.Main", "SunSize"), 0.0f);
	Settings.ContactHardening.MaxSoftness = max(min(TheSettingManager->GetSettingF("Shaders.ContactHardening.Main", "MaxSoftness"), 32.0f), 1.0f);
	Settings.ContactHardening.Smooth = TheSettingManager->GetSettingI("Shaders.ContactHardening.Main", "Smooth") != 0;
	Settings.ContactHardening.AtNight = TheSettingManager->GetSettingI("Shaders.ContactHardening.Main", "AtNight") != 0;
	// UNOFFICIAL LampReachMask (PointShadows.fx LampReach): z = 1 / LightRadiusMult, the game radius as a share of the cube
	// map's radius, so a lamp counts in full out to its game radius and fades over the rest; 0 = off (the old falloff).
	// UNOFFICIAL 2026-10-24 (charfacets; owner: indoors near lamps every triangle of a character showed, hard-edged; LampReachMask cured it):
	// with the per-object lamp shadows on, this pass is only the room darkening, and its old per-lamp amount took the angle to the lamp
	// from the depth-rebuilt normals buffer (one flat normal per triangle), so near a lamp every triangle got its own darkening. The object
	// shaders already give each lamp its falloff and angle (smooth normals), so in that mode the reach mask is used.
	// UNOFFICIAL 2026-10-25 ForwardReachMask (live): 0 = only LampReachMask decides, as before; 1 = the reach mask (it also drops the
	// darkening's distance falloff: rooms lighter, interior look 2026-10-20); 2 (default) = the old falloff without the angle term (z < 0,
	// PointShadows.fx LampAmount): no facets, the old brightness. LampReachMask wins. Later the same day (owner): with the per-object lamp
	// shadows OFF too, where this pass is the lamp shadow itself: its angle term came from the same flat per-triangle normal and doubled the
	// object shaders' own angle, so that no setting combination brings the facets back.
	const int forwardMode = std::clamp(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "ForwardReachMask"), 0, 2);
	if (TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "LampReachMask") || forwardMode == 1)
		Constants.PointShadowNear.z = 1.0f / (std::max)(Settings.Interiors.LightRadiusMult, 1.0f);
	else
		Constants.PointShadowNear.z = forwardMode == 2 ? -1.0f : 0.0f;

	TerrainShadows.UpdateSettings();	// UNOFFICIAL 2026-10-19 [Shaders.ShadowsExteriors.TerrainShadows]

	bool isExterior = TheShaderManager->GameState.isExterior;

	// if the effect was turned off the buffer must be cleared
	if (!Enabled || (isExterior && !Settings.Exteriors.Enabled) || (!isExterior && !Settings.Interiors.Enabled)) clearShadowsBuffer();

	// If certain shadow map settings were changed, recreate the textures and surfaces.
	if (texturesInitialized)
		RecreateTextures(cascadeSettingsChanged, orthoSettingsChanged, false);
}


/*
* The exterior apply multiplies the scene by the sun-shadow term that SunShadows writes into the red
* channel of TESR_PointShadowBuffer. When SunShadows is not running (its effect file failed to compile --
* seen on a tester's install with an outdated Effects/Includes/Normals.hlsl -- or it was switched off),
* that channel holds only the point-light term, about 0 outdoors, and the apply darkened the WHOLE image
* by the Darkness setting. Skip the apply instead (this also skips the composite apply in the fog pass).
*/
bool ShadowsExteriorEffect::ShouldRender() {
	SunShadowsEffect* sun = TheShaderManager->Effects.SunShadows;
	if (!TheShaderManager->GameState.isExterior || (sun && sun->Enabled && sun->Effect)) return true;
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL exterior shadow apply skipped: the SunShadows effect is not running (look for a SunShadows.fx.hlsl "
			"error above, usually a missing or outdated file in Shaders\\NewVegasReloaded\\Effects\\Includes). Without it the "
			"shadow pass would darken the whole image by the Darkness setting.");
		reported = true;
	}
	return false;
}

// UNOFFICIAL 2026-10-25 (NVR DXVK): the push block of ShadowsExteriors.frag (208 bytes; the shader reads the first 200).
struct ShadowsExteriorPush {
	D3DXVECTOR4 viewColumn[4];		// 0: TESR_ViewTransform's HLSL columns 0-3 (see ExtRender)
	D3DXVECTOR4 cameraPosition;		// 64: TESR_CameraPosition
	D3DXVECTOR4 projectionFar;		// 80: x TESR_ProjectionTransform[0][0], y [1][1], z TESR_CameraData.y (farZ)
	D3DXVECTOR4 shadowFade;			// 96: TESR_ShadowFade
	D3DXVECTOR4 shadowData;			// 112: TESR_ShadowData
	D3DXVECTOR4 waterSettings;		// 128: TESR_WaterSettings
	D3DXVECTOR4 sunFarApply;		// 144: TESR_SunFarApply
	D3DXVECTOR4 sunAmbient;			// 160: TESR_SunAmbient
	D3DXVECTOR4 skyColor;			// 176: TESR_SkyColor
	UInt32		samplerIndex;		// 192: LINEAR, CLAMP (filled in by the binding)
	UInt32		pointSamplerIndex;	// 196: POINT, CLAMP (filled in by the binding)
	UInt32		pad[2];
};

// The constant registered under a name: the memory the D3D9 path uploads (SetCT reads it at each draw). Null if absent.
static const D3DXVECTOR4* ShadowConstant(const char* name) {
	auto found = TheShaderManager->ConstantsTable.find(name);
	return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
}

// The texture registered under a sampler name (InitTexture / RegisterTexture). Null if absent.
static IDirect3DBaseTexture9* ShadowTexture(const char* name) {
	auto found = TheTextureManager->TextureNames.find(name);
	return found == TheTextureManager->TextureNames.end() || !found->second ? nullptr : *found->second;
}

// UNOFFICIAL 2026-10-25 (NVR DXVK): the shadow apply's one pass (technique Shadow), as ShadowsExteriors.frag. The matrix: the D3D9
// path uploads TESR_ViewTransform with ID3DXEffect::SetMatrix, so the shader's M[i][j] is the matrix's logical element (row i,
// column j). The matrix's memory is row-major, so the push block takes its columns: viewColumn[j] = (M(0,j), M(1,j), M(2,j), M(3,j)).
// TESR_ProjectionTransform's [0][0] and [1][1] are on the diagonal, the same either way. Samplers: LINEAR for the rendered buffer,
// the point shadows, the depth and the normals; POINT for the sun factor (its tex2Dlod sampler in the .fx). Every texture is at one
// mip (InitTexture without mipmaps), so the implicit LOD of tex2D is level 0, as the explicit ones are.
bool ShadowsExteriorEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	const D3DXMATRIX* view = (const D3DXMATRIX*)ShadowConstant("TESR_ViewTransform");
	const D3DXMATRIX* projection = (const D3DXMATRIX*)ShadowConstant("TESR_ProjectionTransform");
	const D3DXVECTOR4* cameraPosition = ShadowConstant("TESR_CameraPosition");
	const D3DXVECTOR4* cameraData = ShadowConstant("TESR_CameraData");
	const D3DXVECTOR4* shadowFade = ShadowConstant("TESR_ShadowFade");
	const D3DXVECTOR4* shadowData = ShadowConstant("TESR_ShadowData");
	const D3DXVECTOR4* waterSettings = ShadowConstant("TESR_WaterSettings");
	const D3DXVECTOR4* sunFarApply = ShadowConstant("TESR_SunFarApply");
	const D3DXVECTOR4* sunAmbient = ShadowConstant("TESR_SunAmbient");
	const D3DXVECTOR4* skyColor = ShadowConstant("TESR_SkyColor");
	if (!view || !projection || !cameraPosition || !cameraData || !shadowFade || !shadowData || !waterSettings || !sunFarApply || !sunAmbient || !skyColor) return false;

	ShadowsExteriorPush push = {};
	for (int j = 0; j < 4; j++) push.viewColumn[j] = D3DXVECTOR4((*view)(0, j), (*view)(1, j), (*view)(2, j), (*view)(3, j));
	push.cameraPosition = *cameraPosition;
	push.projectionFar = D3DXVECTOR4((*projection)(0, 0), (*projection)(1, 1), cameraData->y, 0.0f);
	push.shadowFade = *shadowFade;
	push.shadowData = *shadowData;
	push.waterSettings = *waterSettings;
	push.sunFarApply = *sunFarApply;
	push.sunAmbient = *sunAmbient;
	push.skyColor = *skyColor;

	// The rendered buffer as the chain has it now; the others by their sampler names (one missing: the D3D9 pass, as before).
	IDirect3DBaseTexture9* textures[5] = {
		TheTextureManager->RenderedTexture,
		ShadowTexture("TESR_SunFarBuffer"),
		ShadowTexture("TESR_PointShadowBuffer"),
		ShadowTexture("TESR_DepthBuffer"),
		ShadowTexture("TESR_NormalsBuffer") };
	for (IDirect3DBaseTexture9* texture : textures) if (!texture) return false;

	const UINT TextureCount = 5, SamplerCount = 2;	// samplers: slot 0 LINEAR, slot 1 POINT (their heap indices at 192 and 196)
	if (!extPass.Tried) {
		extPass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[TextureCount + SamplerCount] = {};
		for (UINT i = 0; i < TextureCount; i++) {
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i; bindings[i].ViewType = NVR_EXT_VIEW_2D;
		}
		bindings[TextureCount].Type = NVR_EXT_BIND_SAMPLER; bindings[TextureCount].Slot = 0; bindings[TextureCount].PushOffset = 192;
		bindings[TextureCount + 1].Type = NVR_EXT_BIND_SAMPLER; bindings[TextureCount + 1].Slot = 1; bindings[TextureCount + 1].PushOffset = 196;
		extPass.Pipeline = DxvkCompute::CreateGraphicsPipeline("ShadowsExteriors", bindings, TextureCount + SamplerCount, sizeof(push), true);
	}
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	if (!extPass.Pipeline || !linear || !point) return false;
	NVR_EXT_RESOURCE_BINDING resources[TextureCount + SamplerCount] = {};
	for (UINT i = 0; i < TextureCount; i++) {
		resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
	}
	resources[TextureCount].Slot = 0; resources[TextureCount].Type = NVR_EXT_BIND_SAMPLER; resources[TextureCount].pResource = linear;
	resources[TextureCount + 1].Slot = 1; resources[TextureCount + 1].Type = NVR_EXT_BIND_SAMPLER; resources[TextureCount + 1].pResource = point;
	return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass.Pipeline, resources, TextureCount + SamplerCount, &push, sizeof(push), true);
}

/*
* UNOFFICIAL 2026-10-16 SkipWhenFaded (dawn cost study, work/bench/foundations/dawncost): while the sun's shadows are fully
* faded (SunFaded), whether anything reads the contact shadows' passes. Their red channel is read only through the fade,
* which makes it exactly 1 whatever it holds (the shadow apply and its copies in the fog's composite and AmbientLight, snow),
* or times 1 - fade = 0 (Specular). Their green channel, the lamps' light (PointShadows.fx), which the passes also blur and
* raise to the Intensity power, counts only with lamp shadows on outdoors (ShadowFade.z): without them the apply's
* 1 + green comes out at 1 whatever green is (Darkness 0 or more). Effects that read the buffer without the fade: blood
* flow, wet world, underwater, the debug view.
*/
bool ShadowsExteriorEffect::FadedContactUnread() {
#if NVR_TEST_TOOLS
	if (SunFaded && Settings.Exteriors.SkipWhenFadedCheck == 2) return true;	// DIAGNOSTIC: the readers ignored (positive control)
#endif
	auto draws = [](EffectRecord* effect) { return effect && effect->Enabled && effect->Effect && effect->ShouldRender(); };
	return SunFaded && Constants.ShadowFade.z == 0.0f && Settings.Exteriors.Darkness >= 0.0f &&
		!draws(TheShaderManager->Effects.BloodFlow) && !draws(TheShaderManager->Effects.WetWorld) &&
		!draws(TheShaderManager->Effects.Underwater) && !draws(TheShaderManager->Effects.Debug);
}

/*
* UNOFFICIAL 2026-10-16 SkipWhenFaded: while SunFaded, whether anything reads the sun's cascade maps this frame. The forward
* lookups are off (ForwardData.x; the skin's second lookup runs only where the first found shadow), and Specular multiplies
* its lookup by 1 - fade = 0. Still reading them: the volumetric fog's sun glow (GetFogShadowVisibility, NOT faded, with its
* ShadowStrength above 0, the default: so by default the maps stay), the volumetric light (it stops itself at fade 0.999),
* and with ForwardShadows off the deferred lookup in SunShadows.fx whenever the contact passes run.
*/
bool ShadowsExteriorEffect::FadedCascadesUnread() {
#if NVR_TEST_TOOLS
	if (SunFaded && Settings.Exteriors.SkipWhenFadedCheck == 2) return true;	// DIAGNOSTIC: the readers ignored (positive control)
#endif
	auto draws = [](EffectRecord* effect) { return effect && effect->Enabled && effect->Effect && effect->ShouldRender(); };
	VolumetricFogEffect* fog = TheShaderManager->Effects.VolumetricFog;
	const bool fogReads = draws(fog) && fog->Constants.Scatter.y > 0.0f;
	const bool deferredReads = !Settings.Exteriors.ForwardShadows && !FadedContactUnread();
	return SunFaded && !fogReads && !deferredReads && !draws(TheShaderManager->Effects.VolumetricLight);
}

void ShadowsExteriorEffect::clearShadowsBuffer() {
	// clear shadows buffer
	IDirect3DSurface9* currentRT;
	TheRenderManager->device->GetRenderTarget(0, &currentRT);
	TheRenderManager->device->SetRenderTarget(0, Textures.ShadowPassSurface);
	TheRenderManager->device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 255, 0, 0), 1.0f, 0L);
	TheRenderManager->device->SetRenderTarget(0, currentRT);
	if (currentRT) currentRT->Release();
}


void ShadowsExteriorEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SmoothedSunDir", &Constants.SmoothedSunDir);
	TheShaderManager->RegisterConstant("TESR_RtSunReplace", RtSunShadows::ReplaceConstant());	// UNOFFICIAL 2026-10-28 RT sun shadows Mode 1
	TheShaderManager->RegisterConstant("TESR_ShadowData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_ShadowFormatData", &Constants.FormatData);
	TheShaderManager->RegisterConstant("TESR_ShadowForwardData", &Constants.ForwardData);
	TheShaderManager->RegisterConstant("TESR_ShadowBlur", &Constants.ShadowBlur);
	TheShaderManager->RegisterConstant("TESR_ShadowScreenSpaceData", &Constants.ScreenSpaceData);
	TheShaderManager->RegisterConstant("TESR_ShadowScreenSpaceFade", &ScreenSpaceFade);	// UNOFFICIAL 2026-10-27 NearFade
	TheShaderManager->RegisterConstant("TESR_OrthoData", &Constants.OrthoData);
	TheShaderManager->RegisterConstant("TESR_ShadowFade", &Constants.ShadowFade);
	TheShaderManager->RegisterConstant("TESR_PointShadowNear", &Constants.PointShadowNear);
	TheShaderManager->RegisterConstant("TESR_LampContact", &LampContact);
	TheShaderManager->RegisterConstant("TESR_LampContactFade", &LampContactFade);
	TheShaderManager->RegisterConstant("TESR_LampContactBias", &LampContactBias);
	TheShaderManager->RegisterConstant("TESR_LampContactNoise", &LampContactNoise);
	TheShaderManager->RegisterConstant("TESR_ShadowRadius", &Constants.ShadowMapRadius);
	// UNOFFICIAL 2026-10-27 FitToDepth: Effects\SunDepthFit.fx's (core\SunDepthFit.h)
	TheShaderManager->RegisterConstant("TESR_SunDepthFitData", &SunDepthFit::Data);
	TheShaderManager->RegisterConstant("TESR_SunDepthFitInfo", &SunDepthFit::Info);
	TheShaderManager->RegisterConstant("TESR_SunDepthFitSplits", &SunDepthFit::Splits);
	TheShaderManager->RegisterConstant("TESR_SunDepthFitOriginal", &SunDepthFit::Original);
	TheShaderManager->RegisterConstant("TESR_ShadowViewProjTransform", (D3DXVECTOR4*)&Constants.ShadowViewProj);
	TheShaderManager->RegisterConstant("TESR_ShadowNearCenter", &ShadowMaps[MapNear].ShadowMapCascadeCenterRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformNear", (D3DXVECTOR4*)&ShadowMaps[MapNear].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowMiddleCenter", &ShadowMaps[MapMiddle].ShadowMapCascadeCenterRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformMiddle", (D3DXVECTOR4*)&ShadowMaps[MapMiddle].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowFarCenter", &ShadowMaps[MapFar].ShadowMapCascadeCenterRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformFar", (D3DXVECTOR4*)&ShadowMaps[MapFar].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowLodCenter", &ShadowMaps[MapLod].ShadowMapCascadeCenterRadius);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformLod", (D3DXVECTOR4*)&ShadowMaps[MapLod].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowCameraToLightTransformOrtho", (D3DXVECTOR4*)&ShadowMaps[MapOrtho].ShadowCameraToLight);
	TheShaderManager->RegisterConstant("TESR_ShadowOldCameraToLightTransformNear", (D3DXVECTOR4*)&Constants.OldCameraToLight[MapNear]);
	TheShaderManager->RegisterConstant("TESR_ShadowOldCameraToLightTransformMiddle", (D3DXVECTOR4*)&Constants.OldCameraToLight[MapMiddle]);
	TheShaderManager->RegisterConstant("TESR_ShadowOldCameraToLightTransformFar", (D3DXVECTOR4*)&Constants.OldCameraToLight[MapFar]);
	TheShaderManager->RegisterConstant("TESR_ShadowOldCameraToLightTransformLod", (D3DXVECTOR4*)&Constants.OldCameraToLight[MapLod]);
	TheShaderManager->RegisterConstant("TESR_ShadowOldNearCenter", &Constants.OldCenter[MapNear]);
	TheShaderManager->RegisterConstant("TESR_ShadowOldMiddleCenter", &Constants.OldCenter[MapMiddle]);
	TheShaderManager->RegisterConstant("TESR_ShadowOldFarCenter", &Constants.OldCenter[MapFar]);
	TheShaderManager->RegisterConstant("TESR_ShadowOldLodCenter", &Constants.OldCenter[MapLod]);
	TheShaderManager->RegisterConstant("TESR_ShadowCrossFade", &Constants.CrossFade);
	Constants.CrossFade = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	TheShaderManager->RegisterConstant("TESR_ShadowCubeMapLightPosition", &Constants.ShadowCubeMapLightPosition);
	TheShaderManager->RegisterConstant("TESR_ShadowLightPosition", (D3DXVECTOR4*)&Constants.ShadowLightPosition);
	TheShaderManager->RegisterConstant("TESR_ShadowSunLight", &SunLight);	// UNOFFICIAL 2026-10-19 (SunFar)
	TheShaderManager->RegisterConstant("TESR_ShadowAmbientLight", &AmbientLight);
	// UNOFFICIAL 2026-10-23 CharacterShadowMap (Includes/Shadow.hlsl, SunShadows.fx.hlsl)
	TheShaderManager->RegisterConstant("TESR_CharShadowCameraToLight", (D3DXVECTOR4*)&Constants.CharacterCameraToLight);
	TheShaderManager->RegisterConstant("TESR_CharShadowCenter", &Constants.CharacterCenter);
	TheShaderManager->RegisterConstant("TESR_CharShadowData", &Constants.CharacterData);
	D3DXMatrixIdentity(&Constants.CharacterCameraToLight);
	Constants.CharacterCenter = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
	Constants.CharacterData = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	TerrainShadows.RegisterConstants();
}

void ShadowsExteriorEffect::RegisterTextures() {
	TerrainShadows.RegisterTextures();	// UNOFFICIAL 2026-10-19
	ULONG ShadowMapSize = Settings.ShadowMaps.CascadeResolution;
	ULONG ShadowCubeMapSize = Settings.Interiors.ShadowCubeMapSize;
	ULONG ShadowAtlasSize = ShadowMapSize * 2;

	TheTextureManager->InitTexture("TESR_ShadowAtlas", &ShadowAtlasTexture, &ShadowAtlasSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, Settings.ShadowMaps.Mipmaps);

	// Intermediate for the separable prefilter. Only allocated when the prefilter is on.
	if (Settings.ShadowMaps.Prefilter)
		TheTextureManager->InitTexture("TESR_ShadowAtlasBlur", &ShadowAtlasBlurTexture, &ShadowAtlasBlurSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, false);

	// SunSmoothing CrossFade: the copy of the atlas from before a sun step. Only when the setting is on at startup,
	// which is also when the shaders get the fade compiled in (SUN_CROSSFADE in ShaderRecord/EffectRecord).
	// Forward shadows only (the default): the deferred lookup in SunShadows.fx has no fade, so without forward
	// shadows the steps keep gliding.
	if (Settings.SunSmoothing.CrossFade && Settings.Exteriors.ForwardShadows) {
		TheTextureManager->InitTexture("TESR_ShadowAtlasOld", &ShadowAtlasOldTexture, &ShadowAtlasOldSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, false);
		SunCrossFadeReady = ShadowAtlasOldTexture && ShadowAtlasOldSurface;
		Logger::Log("UNOFFICIAL sun shadow cross-fade %s (%lux%lu copy of the shadow atlas).", SunCrossFadeReady ? "on" : "FAILED, off",
			ShadowAtlasSize, ShadowAtlasSize);
	}

	if (!Settings.ShadowMaps.MSAA)
		TheRenderManager->device->CreateDepthStencilSurface(ShadowAtlasSize, ShadowAtlasSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &ShadowAtlasDepthSurface, NULL);
	else {
		TheRenderManager->device->CreateRenderTarget(ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, D3DMULTISAMPLE_4_SAMPLES, 0, 0, &ShadowAtlasSurfaceMSAA, NULL);
		TheRenderManager->device->CreateDepthStencilSurface(ShadowAtlasSize, ShadowAtlasSize, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, true, &ShadowAtlasDepthSurface, NULL);
	}

	for (int i = 0; i <= MapLod; i++) {
		ShadowMaps[i].ShadowMapViewPort = { i % 2 == 0 ? 0 : ShadowMapSize, i < 2 ? 0 : ShadowMapSize, ShadowMapSize, ShadowMapSize, 0.0f, 1.0f };
		ShadowMaps[i].ShadowMapResolution = (float)ShadowMapSize;
		ShadowMaps[i].ShadowMapInverseResolution = 1.0f / (float)ShadowMapSize;
	}

	TheShaderManager->CreateFrameVertex(ShadowAtlasSize, ShadowAtlasSize, &ShadowAtlasVertexBuffer);
	Constants.ShadowBlur.x = 1.0f / (float)ShadowAtlasSize;

	// ortho texture
	ULONG orthoMapRes = Settings.OrthoMap.Resolution;
	TheTextureManager->InitTexture("TESR_OrthoMapBuffer", &ShadowMapOrthoTexture, &ShadowMapOrthoSurface, orthoMapRes, orthoMapRes, D3DFMT_R32F);
	TheRenderManager->device->CreateDepthStencilSurface(orthoMapRes, orthoMapRes, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &ShadowMapOrthoDepthSurface, NULL);
	ShadowMaps[MapOrtho].ShadowMapViewPort = { 0, 0, orthoMapRes, orthoMapRes, 0.0f, 1.0f };
	ShadowMaps[MapOrtho].ShadowMapResolution = (float)orthoMapRes;
	ShadowMaps[MapOrtho].ShadowMapInverseResolution = 1.0f / (float)orthoMapRes;


	// initialize spot lights maps
	for (int i = 0; i < SpotLightsMax; i++) {
		std::string textureName = "TESR_ShadowSpotlightBuffer" + std::to_string(i);
		TheTextureManager->InitTexture(textureName.c_str(), &Textures.ShadowSpotlightTexture[i], &Textures.ShadowSpotlightSurface[i], ShadowCubeMapSize, ShadowCubeMapSize, D3DFMT_R32F);
	}


	// initialize point lights cubemaps (UNOFFICIAL: the screen-space passes' 12 here; the rest on first use, EnsureCubeMap)
	CubeMapSizeCreated = ShadowCubeMapSize;
	CubeGeneration++;
	for (int i = 0; i < ShadowCubeMapsMax; i++) {
		Textures.ShadowCubeMapTexture[i] = nullptr;
		Textures.ShadowCubeMapStaticTexture[i] = nullptr;
		for (int j = 0; j < 6; j++) Textures.ShadowCubeMapSurface[i][j] = Textures.ShadowCubeMapStaticSurface[i][j] = nullptr;
		if (i >= ShadowCubeMapsScreen) continue;
		// UNOFFICIAL 2026-10-16 (bug hunt): a failed create (video memory) left a null texture that was used at once; now the slot
		// stays empty, as EnsureCubeMap does (the shadow passes skip it).
		if (FAILED(TheRenderManager->device->CreateCubeTexture(ShadowCubeMapSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
			&Textures.ShadowCubeMapTexture[i], NULL)) || !Textures.ShadowCubeMapTexture[i]) {
			Textures.ShadowCubeMapTexture[i] = nullptr;
			Logger::Log("UNOFFICIAL point shadows: could not create the cube map for slot %d (%d x %d; video memory?)", i, (int)ShadowCubeMapSize, (int)ShadowCubeMapSize);
		}
		else for (int j = 0; j < 6; j++) {
			Textures.ShadowCubeMapTexture[i]->GetCubeMapSurface((D3DCUBEMAP_FACES)j, 0, &Textures.ShadowCubeMapSurface[i][j]);
		}
		std::string textureName = "TESR_ShadowCubeMapBuffer" + std::to_string(i);
		TheTextureManager->RegisterTexture(textureName.c_str(), (IDirect3DBaseTexture9**)&Textures.ShadowCubeMapTexture[i]);
	}
	// Create the stencil surface used for rendering cubemaps
	TheRenderManager->device->CreateDepthStencilSurface(ShadowCubeMapSize, ShadowCubeMapSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &Textures.ShadowCubeMapDepthSurface, NULL);

	//TheShadowManager->ShadowCubeMapViewPort = { 0, 0, ShadowCubeMapSize, ShadowCubeMapSize, 0.0f, 1.0f };
	//memset(TheShadowManager->ShadowCubeMapLights, NULL, sizeof(ShadowCubeMapLights));

	// UNOFFICIAL 2026-10-23 CharacterShadowMap: the shaders get the lookup only when the setting is on at startup (CHARACTER_SHADOW_MAP in
	// ShaderRecord::BuildDefines, read from the same setting). The texture is made now so every shader binds TESR_CharShadowMap by name
	// from its first draw (a name registered later is looked up on every draw until found).
	CharacterMapCompiled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "CharacterShadowMap") != 0;
	if (CharacterMapCompiled) {
		EnsureCharacterMap();
		Logger::Log("UNOFFICIAL CharacterShadowMap: compiled into the skin and object shaders and the sun pass (%s, %u x %u)",
			CharacterMapTexture ? "map made" : "the map could NOT be made", CharacterMapSizeCreated, CharacterMapSizeCreated);
	}

	// Initialize shadow buffer
	TheTextureManager->InitTexture("TESR_PointShadowBuffer", &Textures.ShadowPassTexture, &Textures.ShadowPassSurface, TheRenderManager->width, TheRenderManager->height, D3DFMT_G16R16);
	TheTextureManager->InitTexture("NVR_LampMaskHalf", &MaskHalfTexture, &MaskHalfSurface, (TheRenderManager->width + 1) / 2, (TheRenderManager->height + 1) / 2, D3DFMT_G16R16F);	// UNOFFICIAL MaskHalfRes

	texturesInitialized = true;
}


/*
 * Recreate specific shadow maps, to be used after specific settings change.
 */
bool ShadowsExteriorEffect::EnsureCubeMap(int slot) {
	if (slot < 0 || slot >= ShadowCubeMapsMax || !CubeMapSizeCreated) return false;
	if (Textures.ShadowCubeMapTexture[slot]) return true;
	if (FAILED(TheRenderManager->device->CreateCubeTexture(CubeMapSizeCreated, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
		&Textures.ShadowCubeMapTexture[slot], NULL)) || !Textures.ShadowCubeMapTexture[slot]) {
		Textures.ShadowCubeMapTexture[slot] = nullptr;
		static bool warned = false;
		if (!warned) { warned = true; Logger::Log("UNOFFICIAL point shadows: could not create the cube map for slot %d (video memory?); lamps past it get no shadow.", slot); }
		return false;
	}
	for (int j = 0; j < 6; j++) Textures.ShadowCubeMapTexture[slot]->GetCubeMapSurface((D3DCUBEMAP_FACES)j, 0, &Textures.ShadowCubeMapSurface[slot][j]);
	CubeGeneration++;
	Logger::Log("UNOFFICIAL point shadows: cube map for slot %d created (%u x %u)", slot, CubeMapSizeCreated, CubeMapSizeCreated);
	return true;
}

// UNOFFICIAL RedrawActorsOnly: a slot's static cube map (only what never moves), made the first time a lamp in that slot
// has people in reach. Same size and format as the slot's cube map, so a face copies over with StretchRect.
bool ShadowsExteriorEffect::EnsureStaticCubeMap(int slot) {
	if (slot < 0 || slot >= ShadowCubeMapsMax || !CubeMapSizeCreated) return false;
	if (Textures.ShadowCubeMapStaticTexture[slot]) return true;
	if (FAILED(TheRenderManager->device->CreateCubeTexture(CubeMapSizeCreated, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
		&Textures.ShadowCubeMapStaticTexture[slot], NULL)) || !Textures.ShadowCubeMapStaticTexture[slot]) {
		Textures.ShadowCubeMapStaticTexture[slot] = nullptr;
		static bool warned = false;
		if (!warned) { warned = true; Logger::Log("UNOFFICIAL RedrawActorsOnly: could not create a static cube map for slot %d (video memory?); that lamp is redrawn whole.", slot); }
		return false;
	}
	for (int j = 0; j < 6; j++) Textures.ShadowCubeMapStaticTexture[slot]->GetCubeMapSurface((D3DCUBEMAP_FACES)j, 0, &Textures.ShadowCubeMapStaticSurface[slot][j]);
	CubeGeneration++;
	Logger::Log("UNOFFICIAL RedrawActorsOnly: static cube map for slot %d created (%u x %u)", slot, CubeMapSizeCreated, CubeMapSizeCreated);
	return true;
}

void ShadowsExteriorEffect::RecreateTextures(bool cascades, bool ortho, bool cubemaps) {
	if (cascades) {
		// UNOFFICIAL 2026-10-16 SkipWhenFaded: no skip until the new atlas holds a full draw. (Only here: this function also
		// runs, without remaking anything, on every settings change.)
		TheShadowManager->SunAtlasDrawn = false;
		if (ShadowAtlasSurface) {
			ShadowAtlasSurface->Release();
			ShadowAtlasSurface = nullptr;
		}
		if (ShadowAtlasSurfaceMSAA) {
			ShadowAtlasSurfaceMSAA->Release();
			ShadowAtlasSurfaceMSAA = nullptr;
		}
		if (ShadowAtlasTexture) {
			ShadowAtlasTexture->Release();
			ShadowAtlasTexture = nullptr;
		};
		if (ShadowAtlasBlurSurface) {
			ShadowAtlasBlurSurface->Release();
			ShadowAtlasBlurSurface = nullptr;
		}
		if (ShadowAtlasBlurTexture) {
			ShadowAtlasBlurTexture->Release();
			ShadowAtlasBlurTexture = nullptr;
		}
		if (ShadowAtlasDepthSurface) {
			ShadowAtlasDepthSurface->Release();
			ShadowAtlasDepthSurface = nullptr;
		};
		if (ShadowAtlasVertexBuffer) {
			ShadowAtlasVertexBuffer->Release();
			ShadowAtlasVertexBuffer = nullptr;
		}

		ULONG ShadowMapSize = Settings.ShadowMaps.CascadeResolution;
		ULONG ShadowAtlasSize = ShadowMapSize * 2;

		TheTextureManager->InitTexture("TESR_ShadowAtlas", &ShadowAtlasTexture, &ShadowAtlasSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, Settings.ShadowMaps.Mipmaps);

		if (Settings.ShadowMaps.Prefilter)
			TheTextureManager->InitTexture("TESR_ShadowAtlasBlur", &ShadowAtlasBlurTexture, &ShadowAtlasBlurSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, false);

		// The cross-fade copy follows the atlas' size and format; a fade in progress ends (its copy is gone).
		if (SunCrossFadeReady) {
			if (ShadowAtlasOldSurface) ShadowAtlasOldSurface->Release();
			if (ShadowAtlasOldTexture) ShadowAtlasOldTexture->Release();
			ShadowAtlasOldSurface = nullptr;
			ShadowAtlasOldTexture = nullptr;
			TheTextureManager->InitTexture("TESR_ShadowAtlasOld", &ShadowAtlasOldTexture, &ShadowAtlasOldSurface, ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, false);
			SunCrossFadeReady = ShadowAtlasOldTexture && ShadowAtlasOldSurface;
			Constants.CrossFade.x = 0.0f;
			TheShaderManager->Effects.SunShadows->ClearSampler("TESR_ShadowAtlasOld", 19);
			TheShaderManager->ClearShaderSamplers("TESR_ShadowAtlasOld", 19);
		}

		if (!Settings.ShadowMaps.MSAA)
			TheRenderManager->device->CreateDepthStencilSurface(ShadowAtlasSize, ShadowAtlasSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &ShadowAtlasDepthSurface, NULL);
		else {
			TheRenderManager->device->CreateRenderTarget(ShadowAtlasSize, ShadowAtlasSize, Settings.ShadowMaps.Format, D3DMULTISAMPLE_4_SAMPLES, 0, 0, &ShadowAtlasSurfaceMSAA, NULL);
			TheRenderManager->device->CreateDepthStencilSurface(ShadowAtlasSize, ShadowAtlasSize, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, true, &ShadowAtlasDepthSurface, NULL);
		}

		for (int i = 0; i <= MapLod; i++) {
			ShadowMaps[i].ShadowMapViewPort = { i % 2 == 0 ? 0 : ShadowMapSize, i < 2 ? 0 : ShadowMapSize, ShadowMapSize, ShadowMapSize, 0.0f, 1.0f };
			ShadowMaps[i].ShadowMapResolution = (float)ShadowMapSize;
			ShadowMaps[i].ShadowMapInverseResolution = 1.0f / (float)ShadowMapSize;
		}

		TheShaderManager->CreateFrameVertex(ShadowAtlasSize, ShadowAtlasSize, &ShadowAtlasVertexBuffer);
		Constants.ShadowBlur.x = 1.0f / (float)ShadowAtlasSize;

		TheShaderManager->Effects.SunShadows->ClearSampler("TESR_ShadowAtlas", 16);

		// Game shaders sample the atlas too when forward sun shadows are on, and each holds
		// its own cached pointer to the texture just released above. Clearing only the
		// deferred effect leaves every object/terrain/parallax shader reading the dead
		// texture, which the device still references -- so the shadows freeze at whatever
		// was last rendered into it instead of failing outright.
		TheShaderManager->ClearShaderSamplers("TESR_ShadowAtlas", 16);
	}

	if (ortho) {
		if (ShadowMapOrthoSurface) {
			ShadowMapOrthoSurface->Release();
			ShadowMapOrthoSurface = nullptr;
		}
		if (ShadowMapOrthoTexture) {
			ShadowMapOrthoTexture->Release();
			ShadowMapOrthoTexture = nullptr;
		}
		// Released here because it is recreated below.
		if (ShadowMapOrthoDepthSurface) {
			ShadowMapOrthoDepthSurface->Release();
			ShadowMapOrthoDepthSurface = nullptr;
		}

		ULONG orthoMapRes = Settings.OrthoMap.Resolution;
		TheTextureManager->InitTexture("TESR_OrthoMapBuffer", &ShadowMapOrthoTexture, &ShadowMapOrthoSurface, orthoMapRes, orthoMapRes, D3DFMT_R32F);
		TheRenderManager->device->CreateDepthStencilSurface(orthoMapRes, orthoMapRes, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &ShadowMapOrthoDepthSurface, NULL);
		ShadowMaps[MapOrtho].ShadowMapViewPort = { 0, 0, orthoMapRes, orthoMapRes, 0.0f, 1.0f };
		ShadowMaps[MapOrtho].ShadowMapResolution = (float)orthoMapRes;
		ShadowMaps[MapOrtho].ShadowMapInverseResolution = 1.0f / (float)orthoMapRes;

		TheShaderManager->Effects.Rain->ClearSampler("TESR_OrthoMapBuffer", 19);
		TheShaderManager->Effects.Snow->ClearSampler("TESR_OrthoMapBuffer", 19);
		TheShaderManager->Effects.SnowAccumulation->ClearSampler("TESR_OrthoMapBuffer", 19);
		TheShaderManager->Effects.WetWorld->ClearSampler("TESR_OrthoMapBuffer", 19);
	}

	// Reset shadow manager frame counter, and refresh every cascade next frame: the staggered
	// schedule would otherwise leave freshly created cascades uninitialised for several frames.
	TheShadowManager->FrameCounter = 0;
	TheShadowManager->ForceAllCascades = true;
}


// UNOFFICIAL 2026-10-23 CharacterShadowMap: the map at the size the setting asks for; remade when that changes (the shaders' cached
// pointers to the old texture cleared, as RecreateTextures does for the atlas). false: no map (video memory), nothing is drawn or read.
bool ShadowsExteriorEffect::EnsureCharacterMap() {
	const UINT size = (UINT)Settings.ShadowMaps.CharacterMapResolution;
	if (CharacterMapTexture && CharacterMapDepthSurface && CharacterMapSizeCreated == size) return true;
	if (CharacterMapSurface) { CharacterMapSurface->Release(); CharacterMapSurface = nullptr; }
	if (CharacterMapTexture) { CharacterMapTexture->Release(); CharacterMapTexture = nullptr; }
	if (CharacterMapDepthSurface) { CharacterMapDepthSurface->Release(); CharacterMapDepthSurface = nullptr; }
	TheTextureManager->InitTexture("TESR_CharShadowMap", &CharacterMapTexture, &CharacterMapSurface, size, size, D3DFMT_R32F);
	if (!CharacterMapTexture || !CharacterMapSurface) {
		CharacterMapTexture = nullptr; CharacterMapSurface = nullptr;
		Logger::Log("UNOFFICIAL CharacterShadowMap: could not make the %u x %u map (video memory?); off until it can be.", size, size);
		CharacterMapSizeCreated = 0;
		return false;
	}
	if (FAILED(TheRenderManager->device->CreateDepthStencilSurface(size, size, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, true, &CharacterMapDepthSurface, NULL)) || !CharacterMapDepthSurface) {
		CharacterMapDepthSurface = nullptr;
		Logger::Log("UNOFFICIAL CharacterShadowMap: could not make the %u x %u depth surface; off until it can be.", size, size);
		CharacterMapSizeCreated = 0;
		return false;
	}
	if (CharacterMapSizeCreated) {	// a remake: every shader holds a pointer to the texture just released
		if (TheShaderManager->Effects.SunShadows) TheShaderManager->Effects.SunShadows->ClearSampler("TESR_CharShadowMap", 18);
		TheShaderManager->ClearShaderSamplers("TESR_CharShadowMap", 18);
		Logger::Log("UNOFFICIAL CharacterShadowMap: map remade at %u x %u", size, size);
	}
	CharacterMapSizeCreated = size;
	CharacterMap.ShadowMapViewPort = { 0, 0, size, size, 0.0f, 1.0f };
	CharacterMap.ShadowMapResolution = (float)size;
	CharacterMap.ShadowMapInverseResolution = 1.0f / (float)size;
	return true;
}

// UNOFFICIAL 2026-10-23 CharacterShadowMap: the fit (docs/next/CHARACTER-SHADOW-MAP-DESIGN.md, "Map fit"). A sphere round the player --
// Fit 0: the head and upper body, the segment from the Spine2 bone to the head bone plus the margin (hat brims, hair, shoulders);
// Fit 1: the actor node's world bound plus the margin -- seen from the sun as an ortho box 2R across and 2R deep (eye R toward the
// sun from the centre, as GetCascadeViewProj). Built in camera-relative world space like the cascades (the space the casters' world
// matrices and the shaders' GetShadowWorldPos use). Stable: R is rounded UP to 8 world units, so a breathing bound does not change the
// texel size every frame, and the projection is shifted so the world origin lands on a texel corner (GetCascadeViewProj's rounding),
// so a step of the character moves the picture by whole texels and the shadows do not swim. The sun direction is the cascades' quantised
// one, so the map turns only when they do. The camera's zoom (FOV) plays no part: the same texels at any third-person distance.
bool ShadowsExteriorEffect::GetCharacterViewProj(const D3DXVECTOR3& sunDir, D3DXMATRIX& viewProj) {
	if (!Player || !Player->isThirdPerson) return false;
	NiNode* node = Player->GetNode();
	if (!node || (node->m_flags & NiAVObject::APP_CULLED)) return false;
	const NiPoint3 cameraPosition = WorldSceneGraph->camera->m_worldTransform.pos;
	D3DXVECTOR3 centre;
	float radius;
	if (Settings.ShadowMaps.CharacterMapFit == 0 && Player->ActorSkinInfo && Player->ActorSkinInfo->HeadNode && Player->ActorSkinInfo->Spine2Node) {
		const NiPoint3& h = Player->ActorSkinInfo->HeadNode->m_worldTransform.pos;
		const NiPoint3& t = Player->ActorSkinInfo->Spine2Node->m_worldTransform.pos;
		const D3DXVECTOR3 head(h.x, h.y, h.z), torso(t.x, t.y, t.z);
		const D3DXVECTOR3 axis = head - torso;
		centre = torso + axis * 0.5f;
		radius = 0.5f * D3DXVec3Length(&axis) + Settings.ShadowMaps.CharacterMapMargin;
	}
	else {
		NiBound* bound = node->GetWorldBound();
		if (!bound) return false;
		centre = D3DXVECTOR3(bound->Center.x, bound->Center.y, bound->Center.z);
		radius = bound->Radius + Settings.ShadowMaps.CharacterMapMargin;
	}
	if (!std::isfinite(radius) || !std::isfinite(centre.x) || !std::isfinite(centre.y) || !std::isfinite(centre.z) || radius <= 0.0f) return false;
	radius = std::ceil(radius / 8.0f) * 8.0f;
	radius = (std::min)(radius, 2048.0f);

	// camera-relative, like the cascades' frustum corners
	centre -= D3DXVECTOR3(cameraPosition.x, cameraPosition.y, cameraPosition.z);
	D3DXVECTOR3 dir = sunDir;
	D3DXVec3Normalize(&dir, &dir);
	D3DXVECTOR3 up(0.0f, 0.0f, 1.0f);
	if (SunOverhead(dir)) up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);	// as GetCascadeViewProj's OverheadSunFix
	const D3DXVECTOR3 eye = centre + dir * radius;
	D3DXMATRIX view, proj;
	D3DXMatrixLookAtRH(&view, &eye, &centre, &up);
	D3DXMatrixOrthoOffCenterRH(&proj, -radius, radius, -radius, radius, 0.0f, 2.0f * radius);
	viewProj = view * proj;

	// the world origin on a texel corner (GetCascadeViewProj's rounding): the picture moves by whole texels as the character moves
	const float mapSize = CharacterMap.ShadowMapResolution > 0.0f ? CharacterMap.ShadowMapResolution : (float)Settings.ShadowMaps.CharacterMapResolution;
	D3DXVECTOR4 origin(-cameraPosition.x, -cameraPosition.y, -cameraPosition.z, 1.0f);
	D3DXVec4Transform(&origin, &origin, &viewProj);
	const float ox = origin.x * mapSize * 0.5f, oy = origin.y * mapSize * 0.5f;
	proj._41 += (floorf(ox + 0.5f) - ox) * 2.0f / mapSize;
	proj._42 += (floorf(oy + 0.5f) - oy) * 2.0f / mapSize;
	viewProj = view * proj;

	// the map's settings for the draw (as AmbientLight::GridSunViewProj): actors only, alpha test on (hair cards, hat meshes), nothing too small
	ShadowMapSettings& map = CharacterMap;
	map.Forms = {};
	map.Forms.Actors = true;
	map.Forms.AlphaEnabled = true;
	map.Forms.MinRadius = 0.0f;
	map.Forms.OrigMinRadius = 0.0f;
	map.ShadowMapRadius = radius;
	map.ShadowMapNear = 0.0f;
	map.CustomClearRequired = false;
	map.ClearColor = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 1.0f);
	map.ShadowMapCascadeCenterRadius = D3DXVECTOR4(centre.x, centre.y, centre.z, radius);
	map.CameraTranslation = D3DXVECTOR3(cameraPosition.x, cameraPosition.y, cameraPosition.z);
	NiFrustum frustum(-radius, radius, radius, -radius, 0.0f, 2.0f * radius, true);
	TheCameraManager->SetFrustumPlanes(&map.ShadowMapFrustumPlanes, &viewProj, eye, frustum);
	map.ShadowMapFrustumPlanes.SetActivePlaneState(62);

	// the lookups' constants (w: set by the caller once the map holds the draw)
	Constants.CharacterCameraToLight = viewProj;
	Constants.CharacterCenter = D3DXVECTOR4(centre.x, centre.y, centre.z, radius);
	Constants.CharacterData.x = 1.0f / mapSize;
	Constants.CharacterData.y = (std::min)(Settings.ShadowMaps.CharacterMapBlend / radius, 0.9f);
	Constants.CharacterData.z = Settings.ShadowMaps.CharacterMapBias / (2.0f * radius);	// world units -> the map's depth (0..1 over 2R)
	return true;
}

/*
* Produce a smooth sun direction.
*/
D3DXVECTOR3 ShadowsExteriorEffect::CalculateSmoothedSunDir() {
	const float yawStepSize = D3DXToRadian(Settings.SunSmoothing.YawStepSize);		// horizontal rotation
	const float pitchStepSize = D3DXToRadian(Settings.SunSmoothing.PitchStepSize);	// vertical rotation
	const float smoothingFactor = Settings.SunSmoothing.SmoothingFactor;			// smoothing strength (0 = no smoothing, 1 = instant)
	const float maxJumpAngle = D3DXToRadian(Settings.SunSmoothing.MaxJumpAngle);	// if sun moves more than setting, apply instantly

	const bool quantizeSun = Settings.SunSmoothing.QuantizeSun && yawStepSize > 0.0f && pitchStepSize > 0.0f;
	const bool smoothSun = Settings.SunSmoothing.SmoothSun && smoothingFactor > 0.0f;

	D3DXVECTOR3 SunDir(TheShaderManager->ShaderConst.SunDir);

	if (quantizeSun) {
		float theta = atan2f(SunDir.y, SunDir.x);   // Yaw
		float phi = acosf(std::clamp(SunDir.z, -1.0f, 1.0f));	// Pitch (UNOFFICIAL 2026-10-22: clamped, a rounding step past 1 gave NaN)

		theta = roundf(theta / yawStepSize) * yawStepSize;
		phi = roundf(phi / pitchStepSize) * pitchStepSize;

		SunDir.x = sinf(phi) * cosf(theta);
		SunDir.y = sinf(phi) * sinf(theta);
		SunDir.z = cosf(phi);
		D3DXVec3Normalize(&SunDir, &SunDir);
	}

	D3DXVECTOR3 SmoothedSunDir(Constants.SmoothedSunDir);

	// UNOFFICIAL: with quantizing on, the sun only moves in steps (15 degrees, about one in-game hour, by default),
	// and a step is always bigger than MaxJumpAngle, so the smoothing below never ran and every step was an instant
	// jump of all sun shadows. Instead slide each step over GlideSeconds of real time, eased at both ends. Anything
	// bigger than one step (waiting, sleeping, fast travel, loading) still jumps instantly.
	// CrossFade (opt-in, needs a restart): the sun jumps to the new step at once and the shaders fade from the shadows
	// drawn before the step to the new ones, so no shadow map turns while it is on screen (a turning one makes
	// shadow edges crawl). See StartSunCrossFade.
	if (quantizeSun && smoothSun && Settings.SunSmoothing.GlideSeconds > 0.0f) {
		const ULONGLONG now = GetTickCount64();
		const bool crossFade = SunCrossFadeReady && Settings.SunSmoothing.CrossFade && Settings.Exteriors.ForwardShadows;
		if (!SunGlideValid) {
			SunGlideFrom = SunGlideTo = SunDir;
			SunGlideValid = true;
			SunCrossFading = false;
		}
		else if (D3DXVec3Dot(&SunDir, &SunGlideTo) < 0.99999f) {
			// A new step. Measured from the previous step's target, so a step that arrives mid-glide is still one step.
			const float stepAngle = acosf(std::clamp(D3DXVec3Dot(&SunDir, &SunGlideTo), -1.0f, 1.0f));
			const bool oneStep = stepAngle < 1.5f * max(yawStepSize, pitchStepSize);
			SunCrossFading = oneStep && crossFade && StartSunCrossFade();
			static int logged = 0;
			if (logged < 20 || (TheSettingManager->GetSettingI("Shaders.CloudShadows.Main", "FlickerWatch") && logged < 2000)) {	// (2026-10-25: more while FlickerWatch tests run)
				logged++;
				Logger::Log("UNOFFICIAL sun step %.1f degrees at hour %.2f (to %.3f %.3f %.3f): %s", D3DXToDegree(stepAngle), TheShaderManager->ShaderConst.GameTime.y, SunDir.x, SunDir.y, SunDir.z,
					!oneStep ? "jumps (bigger than one step: waiting, sleeping or loading)" : SunCrossFading ? "cross-fades" : "glides");
			}
			SunGlideFrom = oneStep && !SunCrossFading ? SmoothedSunDir : SunDir;
			SunGlideTo = SunDir;
			SunGlideStart = now;
		}
		const float t = std::clamp((now - SunGlideStart) / (1000.0f * Settings.SunSmoothing.GlideSeconds), 0.0f, 1.0f);
		const float eased = t * t * (3.0f - 2.0f * t);
		if (SunCrossFading && crossFade && t < 1.0f) UpdateSunCrossFade(1.0f - eased);
		else {
			SunCrossFading = false;
			Constants.CrossFade.x = 0.0f;
		}
		D3DXVec3Lerp(&SmoothedSunDir, &SunGlideFrom, &SunGlideTo, eased);
		D3DXVec3Normalize(&SmoothedSunDir, &SmoothedSunDir);
		Constants.SmoothedSunDir = D3DXVECTOR4(SmoothedSunDir, 0.0f);
		return SmoothedSunDir;
	}
	SunGlideValid = false;
	SunCrossFading = false;
	Constants.CrossFade.x = 0.0f;

	if (smoothSun) {
		// Compute angle difference between smoothed and new direction
		float dotProduct = D3DXVec3Dot(&SunDir, &SmoothedSunDir);
		dotProduct = max(-1.0f, min(1.0f, dotProduct)); // Clamp to avoid NaN
		float angleDifference = acosf(dotProduct); // Angle between old and new direction

		// Apply smoothing only if the change is small
		if (angleDifference < maxJumpAngle) {
			D3DXVec3Lerp(&SmoothedSunDir, &SmoothedSunDir, &SunDir, smoothingFactor);
		}
		else {
			SmoothedSunDir = SunDir;
		}
	}
	else {
		SmoothedSunDir = SunDir;
	}
	
	Constants.SmoothedSunDir = D3DXVECTOR4(SmoothedSunDir, 0.0f);
	return SmoothedSunDir;
}

/*
* UNOFFICIAL SunSmoothing CrossFade, at a sun step (called before this frame's shadow maps are drawn): keep the atlas
* as the last frame left it -- the shadows of the previous sun direction -- with the matrices and centres its
* cascades were drawn with, and have every cascade redrawn this frame for the new direction.
*/
bool ShadowsExteriorEffect::StartSunCrossFade() {
	HRESULT copied = TheRenderManager->device->StretchRect(ShadowAtlasSurface, NULL, ShadowAtlasOldSurface, NULL, D3DTEXF_NONE);
	if (FAILED(copied)) {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL sun shadow cross-fade: copying the shadow atlas failed (%08lx), steps glide instead.", copied);
		reported = true;
		return false;
	}
	for (int i = MapNear; i <= MapLod; i++) {
		SunFadeMatrix[i] = ShadowMaps[i].ShadowCameraToLight;
		SunFadeCenter[i] = ShadowMaps[i].ShadowMapCascadeCenterRadius;
		SunFadeCamera[i] = ShadowMaps[i].CameraTranslation;
	}
	TheShadowManager->ForceAllCascades = true;
	return true;
}

/*
* The kept cascades for this frame: their matrices and centres are relative to the camera position they were drawn
* from, so move them by how far the camera has moved since (as ShadowManager does for cascades it does not redraw).
*/
void ShadowsExteriorEffect::UpdateSunCrossFade(float oldWeight) {
	const NiPoint3& position = WorldSceneGraph->camera->m_worldTransform.pos;
	const D3DXVECTOR3 camera(position.x, position.y, position.z);
	for (int i = MapNear; i <= MapLod; i++) {
		const D3DXVECTOR3 moved = camera - SunFadeCamera[i];
		D3DXMATRIX translation;
		D3DXMatrixTranslation(&translation, moved.x, moved.y, moved.z);
		Constants.OldCameraToLight[i] = translation * SunFadeMatrix[i];
		Constants.OldCenter[i] = D3DXVECTOR4(SunFadeCenter[i].x - moved.x, SunFadeCenter[i].y - moved.y,
			SunFadeCenter[i].z - moved.z, SunFadeCenter[i].w);
	}
	Constants.CrossFade.x = oldWeight;
}


void ShadowsExteriorEffect::GetCascadeDepths() {
	NiCamera* sceneCamera = WorldSceneGraph->camera;

	float nearClip = sceneCamera->Frustum.Near;
	float farClip = sceneCamera->Frustum.Far;
	float clipRange = farClip - nearClip;

	float minZ = nearClip + 10.0f;
	float maxZ = min(nearClip + Settings.ShadowMaps.Distance, farClip);

	float range = maxZ - minZ;
	float ratio = maxZ / minZ;

	int cascadeCount = 4;
	float splitDepth[4];	// UNOFFICIAL 2026-10-27 FitToDepth: the same splits as view depths

	for (int i = 0; i < cascadeCount; ++i) {
		float p = (i + 1) / static_cast<float>(cascadeCount);
		float log = minZ * std::pow(ratio, p);
		float uniform = minZ + range * p;
		float d = Settings.ShadowMaps.CascadeLambda * (log - uniform) + uniform;
		ShadowMaps[i].ShadowMapRadius = (d - nearClip) / clipRange;
		splitDepth[i] = d;
	}

	// UNOFFICIAL 2026-10-27 FitToDepth ([Shaders.ShadowsExteriors.Main], core\SunDepthFit.h): the splits refitted to the depth on
	// screen (measured a few frames ago), only where no visible depth gets a coarser cascade; the normal ones otherwise. Off: unchanged.
	SunDepthFit::UnfittedRadiusW = ShadowMaps[MapLod].ShadowMapRadius * clipRange;
	float nearStart = minZ;
	{
		bool airReaders = false;	// something reads the cascades through the air along the view (sky pixels included)
		if (SunDepthFit::Enabled) {
			auto draws = [](EffectRecord* effect) { return effect && effect->Enabled && effect->Effect && effect->ShouldRender(); };
			airReaders = draws(TheShaderManager->Effects.VolumetricLight) || draws(TheShaderManager->Effects.Sandstorm);
		}
		float fitted[4];
		if (SunDepthFit::Choose(minZ, maxZ, Settings.ShadowMaps.CascadeLambda, splitDepth, fitted, nearStart, airReaders))
			for (int i = 0; i < cascadeCount; ++i) ShadowMaps[i].ShadowMapRadius = (fitted[i] - nearClip) / clipRange;
		else nearStart = minZ;
	}

	// Get Near distance for each cascade
	ShadowMaps[MapNear].ShadowMapNear = nearStart > minZ ? (nearStart - nearClip) / clipRange : 10.0f / clipRange;
	ShadowMaps[MapMiddle].ShadowMapNear = ShadowMaps[MapNear].ShadowMapRadius;
	ShadowMaps[MapFar].ShadowMapNear = ShadowMaps[MapMiddle].ShadowMapRadius;
	ShadowMaps[MapLod].ShadowMapNear = ShadowMaps[MapFar].ShadowMapRadius;

	// Ortho.
	ShadowMaps[MapOrtho].ShadowMapNear = 10.0f / clipRange;
	ShadowMaps[MapOrtho].ShadowMapRadius = Settings.OrthoMap.Distance / clipRange;

	// Store absolute Shadow map splits in Constants to pass to the Shaders
	Constants.ShadowMapRadius.x = ShadowMaps[MapNear].ShadowMapRadius * clipRange;
	Constants.ShadowMapRadius.y = ShadowMaps[MapMiddle].ShadowMapRadius * clipRange;
	Constants.ShadowMapRadius.z = ShadowMaps[MapFar].ShadowMapRadius * clipRange;
	Constants.ShadowMapRadius.w = ShadowMaps[MapLod].ShadowMapRadius * clipRange;

	// Reset blur constant for handling limited refresh rate.
	Constants.ShadowBlur.y = 1.0f;
}

// Banker round helper.
float BankerRound(float num) {
	float rounded = round(num);
	if (fabs(rounded - num) == 0.5f) {
		return 2.0 * round(0.5 * num);
	}
	return rounded;
}


// Round a vector using the banker round method.
void Vector3Round(D3DXVECTOR3* out, D3DXVECTOR3* in) {
	out->x = BankerRound(in->x);
	out->y = BankerRound(in->y);
	out->z = BankerRound(in->z);
}

void Vector4Round(D3DXVECTOR4* out, D3DXVECTOR4* in) {
	out->x = BankerRound(in->x);
	out->y = BankerRound(in->y);
	out->z = BankerRound(in->z);
	out->w = BankerRound(in->w);
}


// Generate the ViewProj matrix for a particular shadow cascade.
// Inspired by MJP's https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/ article and code example.
D3DXMATRIX ShadowsExteriorEffect::GetCascadeViewProj(ShadowMapSettings* ShadowMap, D3DXVECTOR3* SunDir) {
	// Get z-range for this cascade.
	NiCamera* sceneCamera = WorldSceneGraph->camera;
	NiPoint3 cameraPosition = sceneCamera->m_worldTransform.pos;
	float zNear = ShadowMap->ShadowMapNear;
	float zFar = ShadowMap->ShadowMapRadius;

	// Calculate the frustum corners in world space (from a unit cube in projective space).
	D3DXMATRIX* invViewProj = &TheRenderManager->InvViewProjMatrix;

	float ndcNear = TheRenderManager->IsReversedDepth() ? 1.0f : 0.0f;	// (was written '1.0f ? IsReversedDepth() : 0.0f', same value)
	float ndcFar = 1.0f - ndcNear;
	D3DXVECTOR3 frustumCorners[8] = {
		D3DXVECTOR3(-1.0f,  1.0f, ndcNear), // Near plane.
		D3DXVECTOR3( 1.0f,  1.0f, ndcNear),
		D3DXVECTOR3( 1.0f, -1.0f, ndcNear),
		D3DXVECTOR3(-1.0f, -1.0f, ndcNear),
		D3DXVECTOR3(-1.0f,  1.0f, ndcFar),  // Far plane.
		D3DXVECTOR3( 1.0f,  1.0f, ndcFar),
		D3DXVECTOR3( 1.0f, -1.0f, ndcFar),
		D3DXVECTOR3(-1.0f, -1.0f, ndcFar),
	};
	for (auto i = 0; i < 8; ++i) {
		D3DXVec3TransformCoord(&frustumCorners[i], &frustumCorners[i], invViewProj);
	}

	// Get the corners of the current cascade slice of the view frustum.
	for (auto i = 0; i < 4; ++i)
	{
		D3DXVECTOR3 cornerRay = frustumCorners[i + 4] - frustumCorners[i];
		D3DXVECTOR3 nearCornerRay = cornerRay * zNear;
		D3DXVECTOR3 farCornerRay = cornerRay * zFar;
		frustumCorners[i + 4] = frustumCorners[i] + farCornerRay;
		frustumCorners[i] = frustumCorners[i] + nearCornerRay;
	}

	// Calculate the centroid of the view frustum slice.
	D3DXVECTOR3 frustumCenter(0.0f, 0.0f, 0.0f);
	for (auto i = 0; i < 8; ++i)
		frustumCenter = frustumCenter + frustumCorners[i];
	frustumCenter *= 1.0f / 8.0f;
	
	// Must be kept stable.
	D3DXVECTOR3 upDir(0.0f, 0.0f, 1.0f);
	// UNOFFICIAL 2026-10-21 (sunfix) OverheadSunFix: a sun within ~2.6 degrees of straight overhead (QuantizeSun rounds any sun within
	// half a PitchStepSize of the zenith to exactly (0, 0, 1)) is parallel to that up direction: the view's side axes come out as zero
	// vectors, every caster is drawn as zero-area triangles and the cascade stays empty (no sun shadows at all; SunCascadeSkipFlat then
	// skips it). World +Y is taken as up instead (map x = world +X, y = world +Y, as AmbientLight's grid sun map and the billboard
	// vectors do for that case). It depends on the sun direction alone, so the texel snapping below holds still from frame to frame.
	// The tilted ortho direction (0.05, 0.05, 1) is ~4 degrees off the zenith: not affected.
	if (Settings.Exteriors.OverheadSunFix && SunOverhead(*SunDir))
		upDir = D3DXVECTOR3(0.0f, 1.0f, 0.0f);

	D3DXVECTOR3 minExtents, maxExtents;

	// Calculate the radius of a bounding sphere surrounding the frustum corners
	float sphereRadius = 0.0f;
	for (auto i = 0; i < 8; ++i)
	{
		D3DXVECTOR3 centerToCorner = frustumCorners[i] - frustumCenter;
		float dist = D3DXVec3Length(&centerToCorner);
		sphereRadius = max(sphereRadius, dist);
	}
	sphereRadius = std::ceil(sphereRadius * 16.0f) / 16.0f;

	// Modify sphere radius to compensate for lower than default FOV (aiming, zooming, ...).
	float defaultWorldFOV = *(float*)(0x120315C + 4);
	float currentWorldFOV = WorldSceneGraph->cameraFOV;
	float radiusFOVCompensation = tan(defaultWorldFOV * 0.5f * (3.1416f / 180.0f)) / tan(currentWorldFOV * 0.5f * (3.1416f / 180.0f));
	sphereRadius *= radiusFOVCompensation;

	// UNOFFICIAL 2026-10-23 PlayerInNear ([Shaders.ShadowsExteriors.ShadowMaps], world units, live, 0 = off). P82 tester: in third person
	// the face's own sun shadows (hair strands on the brow, the nose's on the cheek, 1-2 cm wide) showed only once the camera zoomed in to
	// about 1.5-2 m: further out the head is in the Middle cascade, whose texels (about 1 cm at CascadeResolution 1536, CascadeLambda 0.9)
	// and 3 x 3 filter blur a strand away, and the zoom also narrows the FOV, which scales every radius above, so the border moves with it.
	// With this, while the camera is within PlayerInNear of the player's head, the Near sphere is grown to the smallest one holding both
	// its own slice sphere and the head (24 units round the head bone, plus the hand-over band, so the face reads Near alone): the face
	// keeps Near's texels at every zoom up to that distance, and Near's texels grow with the sphere (at 6 m about twice today's Near
	// texel, still about half Middle's). The radius is rounded up to 16 units so walking does not change the texel size every frame.
	if (Settings.ShadowMaps.PlayerInNear > 0.0f && ShadowMap == &ShadowMaps[MapNear] && Player && Player->isThirdPerson &&
		Player->ActorSkinInfo && Player->ActorSkinInfo->HeadNode) {
		const NiPoint3& h = Player->ActorSkinInfo->HeadNode->m_worldTransform.pos;
		const D3DXVECTOR3 head(h.x - cameraPosition.x, h.y - cameraPosition.y, h.z - cameraPosition.z);	// camera-relative, as the corners
		const D3DXVECTOR3 toHead = head - frustumCenter;
		const float headDistance = D3DXVec3Length(&head);
		const float centerToHead = D3DXVec3Length(&toHead);
		float margin = 24.0f + Settings.ShadowMaps.HandoverBlend;
		if (headDistance <= Settings.ShadowMaps.PlayerInNear && std::isfinite(centerToHead) && centerToHead + margin > sphereRadius) {
			float grown = 0.5f * (sphereRadius + centerToHead + margin);
			margin += (std::max)(0.0f, 0.1f * grown - Settings.ShadowMaps.HandoverBlend);	// the band is never under a tenth of the radius
			grown = std::ceil(0.5f * (sphereRadius + centerToHead + margin) / 16.0f) * 16.0f;
			// the new centre: on the line to the head, so the old sphere's far side stays inside
			frustumCenter += toHead * ((grown - sphereRadius) / (std::max)(centerToHead, 0.001f));
			sphereRadius = grown;
		}
		static DWORD lastLog = 0;
		if (Settings.ShadowMaps.PlayerInNearLog && GetTickCount() - lastLog > 1000) {
			lastLog = GetTickCount();
			const D3DXVECTOR3 fromCenter = head - frustumCenter;
			Logger::Log("PlayerInNear: head %.0f units from the camera, Near radius %.0f (FOV %.1f, default %.1f), head %.0f from its centre",
				headDistance, sphereRadius, currentWorldFOV, defaultWorldFOV, D3DXVec3Length(&fromCenter));
		}
	}

	maxExtents = D3DXVECTOR3(sphereRadius, sphereRadius, sphereRadius);
	minExtents = -maxExtents;
	
	D3DXVECTOR3 cascadeExtents = maxExtents - minExtents;

	// Create a shadow frustum center by moving the view frustum slice center away from the camera.
	// Should make it so we can more easily use the full resolution, which is mostly wasted due to
	// stabilization.
	D3DXVECTOR3 shadowFrustumCenter = frustumCenter;
	//D3DXVec3Normalize(&shadowFrustumCenter, &frustumCenter);  // Get the direction from camera to the frustum center.
	//shadowFrustumCenter *= sphereRadius;  // Move the center so that the length is equal to the sphere radius.
	
	ShadowMap->ShadowMapCascadeCenterRadius.x = shadowFrustumCenter.x;
	ShadowMap->ShadowMapCascadeCenterRadius.y = shadowFrustumCenter.y;
	ShadowMap->ShadowMapCascadeCenterRadius.z = shadowFrustumCenter.z;
	ShadowMap->ShadowMapCascadeCenterRadius.w = sphereRadius;

	// Calculate correct bound size limit for current cascade.
	ShadowMap->Forms.MinRadius = ShadowMap->Forms.OrigMinRadius * sphereRadius * ShadowMap->ShadowMapInverseResolution;

	float nearPlane = 0.0f;  // Shadow casters are pancaked to near plane in the vertex shader.
	float farPlane = cascadeExtents.z;
	D3DXVECTOR3 shadowCameraPos = shadowFrustumCenter + D3DXVECTOR3(*SunDir) * -minExtents.z;
	
	D3DXMATRIX shadowView, shadowProj, shadowViewProj;

	D3DXMatrixLookAtRH(&shadowView, &shadowCameraPos, &shadowFrustumCenter, &upDir);
	D3DXMatrixOrthoOffCenterRH(&shadowProj, minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, nearPlane, farPlane);
	shadowViewProj = shadowView * shadowProj;

	// Create the rounding matrix, by projecting the world-space origin and determining
	// the fractional offset in texel space.
	float sMapSize = ShadowMap->ShadowMapResolution;
	// We are working in camera relative world space - camera position is our fixed point for stabilization.
	D3DXVECTOR4 shadowOrigin(-cameraPosition.x, -cameraPosition.y, -cameraPosition.z, 1.0f);
	D3DXVec4Transform(&shadowOrigin, &shadowOrigin, &shadowViewProj);
	D3DXVec4Scale(&shadowOrigin, &shadowOrigin, sMapSize / 2.0f);
	D3DXVECTOR4 roundedOrigin, roundOffset;
	Vector4Round(&roundedOrigin, &shadowOrigin);
	D3DXVec4Subtract(&roundOffset, &roundedOrigin, &shadowOrigin);
	D3DXVec4Scale(&roundOffset, &roundOffset, 2.0f / sMapSize);

	shadowProj._41 = shadowProj._41 + roundOffset.x;
	shadowProj._42 = shadowProj._42 + roundOffset.y;

	shadowViewProj = shadowView * shadowProj;

	NiFrustum frustum(minExtents.x, maxExtents.x, maxExtents.y, minExtents.y, nearPlane, farPlane, true);
	TheCameraManager->SetFrustumPlanes(&ShadowMap->ShadowMapFrustumPlanes, &shadowViewProj, shadowCameraPos, frustum);
	ShadowMap->ShadowMapFrustumPlanes.SetActivePlaneState(62);

	// Cache the current camera translation. Used to offset against camera movement when using a cached map.
	ShadowMap->CameraTranslation = D3DXVECTOR3(cameraPosition.x, cameraPosition.y, cameraPosition.z);

	return shadowViewProj;
}

