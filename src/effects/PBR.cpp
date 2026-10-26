#include "PBR.h"
#include "../core/PassSums.h"
#include "../core/LinearPreDecode.h"
#include "GrassCollision.h"	// UNOFFICIAL 2026-10-19 (TESR_Translucency.w)

void PBRShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_PBRData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_PBRExtraData", &Constants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_PBRMetal", &Constants.Metal);
	TheShaderManager->RegisterConstant("TESR_PBRMetalLook", &Constants.MetalLook);
	TheShaderManager->RegisterConstant("TESR_SharpSky", &Constants.SharpSky);
	TheShaderManager->RegisterConstant("TESR_EnvCube", &Constants.EnvCube);
	TheShaderManager->RegisterConstant("TESR_LinearLighting", &Constants.Linear);
	TheShaderManager->RegisterConstant("TESR_LampFalloff", &Constants.LampFalloff);	// UNOFFICIAL 2026-10-21 (Includes/LampFalloff.hlsl)
	TheShaderManager->RegisterConstant("TESR_PBREnv", &Constants.Env);
	TheShaderManager->RegisterConstant("TESR_LampPassTrim", &Constants.LampPassTrim);
	TheShaderManager->RegisterConstant("TESR_PBRMath", &Constants.Math);
	TheShaderManager->RegisterConstant("TESR_LinearScales", &Constants.LinearScales);	// UNOFFICIAL LinearPreDecode (Object.hlsl)
	TheShaderManager->RegisterConstant("TESR_PassRedirect", &PassSums::Redirect);	// UNOFFICIAL ExactPassSums (core/PassSums.h)
	TheShaderManager->RegisterConstant("TESR_Translucency", &Constants.Translucency);
	TheShaderManager->RegisterConstant("TESR_MaterialRule", &MaterialRule);
	TheShaderManager->RegisterConstant("TESR_MaterialRough", &MaterialRough);
	TheShaderManager->RegisterConstant("TESR_MaterialTint", &MaterialTint);
}

// The first-person model's metal strength for its pass. Shaders upload their constants when bound, so the one still bound
// from the world pass gets it directly (c34: TESR_PBRMetal in Object.hlsl).
void PBRShaders::BeginFirstPerson(IDirect3DDevice9* Device) {
	Constants.Metal.x = MetalSettings.FirstPerson;
	if (Device) Device->SetPixelShaderConstantF(34, (const float*)&Constants.Metal, 1);
}

void PBRShaders::EndFirstPerson(IDirect3DDevice9* Device) {
	Constants.Metal.x = MetalSettings.World;
	if (Device) Device->SetPixelShaderConstantF(34, (const float*)&Constants.Metal, 1);
}


// fixes-a #23: MetalnessScale / AOScale read 0 when the key is missing (a DLL used with an older defaults toml), which switched every
// _rmaos metal and AO off. A missing key now means 1.0 (the shipped default), like boolSetting's fallback.
static float RmaosScaleOr1(const char* Section, const char* Key) {
	SettingManager::Configuration::ConfigNode node;
	return TheSettingManager->Config.FillNode(&node, Section, Key) ? node.FloatValue : 1.0f;
}

void PBRShaders::UpdateSettings() {
	// UNOFFICIAL metal (Object.hlsl): strengths per pass, the guess and the finish.
	// MetalOn: the metal's own switch (the panel's Enabled button switches all of the PBR shaders).
	const bool metalOn = TheSettingManager->GetSettingI("Shaders.PBR.Metal", "MetalOn") != 0;
	MetalSettings.FirstPerson = metalOn ? std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "FirstPerson"), 0.0f, 1.0f) : 0.0f;
	MetalSettings.World = metalOn ? std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "World"), 0.0f, 1.0f) : 0.0f;
	MetalSettings.FadeDistance = (std::max)(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "FadeDistance"), 0.0f);
	Constants.Metal.x = MetalSettings.World;
	Constants.Metal.y = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "GreyMetal"), 0.0f, 1.0f);
	Constants.Metal.z = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "ColourStart"), 0.0f, 1.0f);
	Constants.Metal.w = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "ColourEnd"), 0.0f, 1.0f);
	Constants.MetalLook.x = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "Roughness"), 0.05f, 1.0f);
	Constants.MetalLook.y = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "PolishedRoughness"), 0.05f, 1.0f);
	MetalSettings.Reflections = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "Reflections"), 0.0f, 4.0f);
	Constants.MetalLook.z = MetalSettings.Reflections;	// times Reflectivity in UpdateConstants
	Constants.MetalLook.w = TheSettingManager->GetSettingI("Shaders.PBR.Metal", "DebugView") ? 1.0f : 0.0f;
	// UNOFFICIAL SharpSky (SkyAmbient.hlsl SkyAmbientSharpened): metal and the env maps take the sky from the direction they
	// reflect, sharper, rather than its smooth average. 0 = the plain sky.
	Constants.SharpSky.x = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "SharpSky"), 0.0f, 1.0f);
	// UNOFFICIAL environment maps by the light (EnvTemplate.hlsl).
	Constants.Env.x = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "Lighting"), 0.0f, 1.0f);
	Constants.Env.y = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "Strength"), 0.0f, 8.0f);
	Constants.Env.z = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "WithMetal"), 0.0f, 1.0f);
	Constants.Env.w = 0.0f;	// Reflectivity - 1, UpdateConstants
	// UNOFFICIAL 2026-10-25 Reflectivity (live): applied in UpdateConstants by whether the player is outdoors or in.
	ReflectivitySettings.Exterior = std::clamp(RmaosScaleOr1("Shaders.PBR.Main", "ReflectivityExterior"), 0.0f, 3.0f);
	ReflectivitySettings.Interior = std::clamp(RmaosScaleOr1("Shaders.PBR.Main", "ReflectivityInterior"), 0.0f, 3.0f);
	// UNOFFICIAL CubeReflections (Includes/LiveCube.hlsl): metal and the env maps reflect the live surroundings cube that the
	// Reflections effect keeps (CubeMap); applied in UpdateConstants only while that cube is kept up.
	MetalSettings.CubeReflections = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "CubeReflections"), 0.0f, 1.0f);
	MetalSettings.EnvCubeReflections = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "CubeReflections"), 0.0f, 1.0f);

	// UNOFFICIAL linear lighting (Includes/LinearLight.hlsl: ObjectTemplate, TerrainTemplate). Enabled is read at startup
	// (ShaderRecord::LoadShader compiles it in); Gamma is live, and 1 returns every colour unchanged.
	LinearSettings.Enabled = TheSettingManager->GetSettingI("Shaders.PBR.Linear", "Enabled") != 0;
	LinearSettings.Gamma = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Linear", "Gamma"), 1.0f, 3.0f);
	// UNOFFICIAL 2026-10-20 NightGamma (user request): the gamma outside at night, live; 0 = the same as Gamma.
	const float nightGamma = TheSettingManager->GetSettingF("Shaders.PBR.Linear", "NightGamma");
	LinearSettings.NightGamma = nightGamma > 0.0f ? std::clamp(nightGamma, 1.0f, 3.0f) : 0.0f;
	LinearSettings.FalloffMatch = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Linear", "FalloffMatch"), 0.0f, 1.0f);
	ApplyLinearGamma();

	// UNOFFICIAL 2026-10-21 LampFalloff (Includes/LampFalloff.hlsl), live once compiled in (LampFalloffCompiledIn, at startup).
	{
		LampFalloffSettingsStruct& lamp = LampFalloffSettings;
		lamp.Exterior = TheSettingManager->GetSettingI("Shaders.PBR.LampFalloff", "Exterior") != 0;
		lamp.Interior = TheSettingManager->GetSettingI("Shaders.PBR.LampFalloff", "Interior") != 0;
		lamp.MatchEverywhere = TheSettingManager->GetSettingI("Shaders.PBR.LampFalloff", "MatchEverywhere") != 0;
		lamp.Falloff = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.LampFalloff", "Falloff"), 0.05f, 1.0f);
		lamp.Brightness = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.LampFalloff", "Brightness"), 0.1f, 4.0f);
		lamp.Norm = LampFalloffNorm(lamp.Falloff);
		// Said once, and again whenever one of them changes.
		static float last[5] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
		const float now[5] = { lamp.Exterior ? 1.0f : 0.0f, lamp.Interior ? 1.0f : 0.0f, lamp.MatchEverywhere ? 1.0f : 0.0f, lamp.Falloff, lamp.Brightness };
		if (memcmp(last, now, sizeof(now))) {
			memcpy(last, now, sizeof(now));
			if (!LampFalloffCompiledIn())
				Logger::Log("UNOFFICIAL LampFalloff: off (not compiled into the shaders)%s", lamp.Exterior || lamp.Interior || lamp.MatchEverywhere ?
					"; switched on now: it takes effect after a restart" : "");
			else {
				const bool later = (lamp.Exterior && !LampFalloffAtStartup(LampFalloffExterior) && !LampFalloffShapeIn()) ||
					(lamp.Interior && !LampFalloffAtStartup(LampFalloffInterior)) || (lamp.MatchEverywhere && !LampFalloffAtStartup(LampFalloffMatch));
				Logger::Log("UNOFFICIAL LampFalloff: compiled in; physical falloff outdoors %s, indoors %s (Falloff %.2f, Brightness %.2f, strength %.3f); "
					"MatchEverywhere %s%s", lamp.Exterior ? "on" : "off", lamp.Interior ? "on" : "off", lamp.Falloff, lamp.Brightness, lamp.Norm * lamp.Brightness,
					lamp.MatchEverywhere ? "on" : "off", later ? " (a switch turned on since the start needs a restart to apply everywhere)" : "");
			}
		}
	}

	// UNOFFICIAL 2026-10-16 EnergySplit (Includes/PBR.hlsl DiffuseEnergyKeep): white surfaces no longer send back more light than
	// they receive in the full and sun passes. Live.
	Constants.Math.x = TheSettingManager->GetSettingI("Shaders.PBR.Main", "EnergySplit") ? 1.0f : 0.0f;
	// UNOFFICIAL 2026-10-18 SmithCorrelated TEST mode (2, read at startup like the shaders): the lane carries SmithCorrelatedTest instead
	// (EnergySplit is not compiled into those shaders). Live.
	static const int smithMode = TheSettingManager->GetSettingI("Shaders.PBR.Main", "SmithCorrelated");	// as the shaders were compiled
	if (smithMode == 2)
		Constants.Math.x = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Main", "SmithCorrelatedTest"), 0.0f, 1.0f);

	// UNOFFICIAL material rules (Render.cpp MaterialRulesDraw), live; the table itself is read once at startup.
	MaterialRulesSettings.Enabled = TheSettingManager->GetSettingI("Shaders.PBR.MaterialRules", "Enabled") != 0;
	MaterialRulesSettings.DebugView = TheSettingManager->GetSettingI("Shaders.PBR.MaterialRules", "DebugView") != 0;
	MaterialRulesSettings.LogMatches = TheSettingManager->GetSettingI("Shaders.PBR.MaterialRules", "LogMatches") != 0;
	MaterialRulesSettings.MetalOn = metalOn;
	MaterialRough.z = MaterialRulesSettings.Enabled && MaterialRulesSettings.DebugView ? 1.0f : 0.0f;

	// UNOFFICIAL _rmaos maps (Render.cpp RmaosDraw), live; Enabled itself is read at startup (ShaderRecord compiles it in).
	RmaosSettings.On = TheSettingManager->GetSettingI("Shaders.PBR.Rmaos", "On") != 0;
	RmaosSettings.Occlusion = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Rmaos", "Occlusion"), 0.0f, 1.0f);
	RmaosSettings.DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.PBR.Rmaos", "DebugView"), 0, 4);
	RmaosSettings.LogMatches = TheSettingManager->GetSettingI("Shaders.PBR.Rmaos", "LogMatches") != 0;

	// UNOFFICIAL foliage translucency (grass and leaf vertex shaders, FoliageTranslucency in Grass23x000.vso.hlsl). Power stays
	// at 0.5 or more so pow() is well behaved near zero.
	FoliageSettings.Translucency = std::clamp(TheSettingManager->GetSettingF("Shaders.Foliage.Main", "Translucency"), 0.0f, 1.0f);
	FoliageSettings.Power = std::clamp(TheSettingManager->GetSettingF("Shaders.Foliage.Main", "Power"), 0.5f, 16.0f);
	FoliageSettings.SunGate = std::clamp(TheSettingManager->GetSettingF("Shaders.Foliage.Main", "SunGate"), 0.0f, 20.0f);

Settings.Default.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.Main", "Saturation");
	Settings.Default.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.Main", "Metallicness");
	Settings.Default.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.Main", "Roughness");
	Settings.Default.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.Main", "LightingScale");
	Settings.Default.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.Main", "AmbientScale");
	Settings.Default.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.Main", "SkylightingScale");
	Settings.Default.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.Main", "SkylightingDirectionality");
	Settings.Default.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.Main", "SkylightingSaturation");
	Settings.Default.RmaosMetalnessScale = std::clamp(RmaosScaleOr1("Shaders.PBR.Main", "MetalnessScale"), 0.0f, 1.0f);	// UNOFFICIAL 2026-10-18 (_rmaos maps)
	Settings.Default.RmaosAOScale = std::clamp(RmaosScaleOr1("Shaders.PBR.Main", "AOScale"), 0.0f, 1.0f);
	Settings.Default.RmaosRoughnessScale = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Main", "RoughnessScale"), 1.0f, 2.0f);

	Settings.Rain.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "Saturation");
	Settings.Rain.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "Metallicness");
	Settings.Rain.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "Roughness");
	Settings.Rain.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "LightingScale");
	Settings.Rain.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "AmbientScale");
	Settings.Rain.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "SkylightingScale");
	Settings.Rain.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "SkylightingDirectionality");
	Settings.Rain.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "SkylightingSaturation");
	Settings.Rain.RmaosMetalnessScale = std::clamp(RmaosScaleOr1("Shaders.PBR.Rain", "MetalnessScale"), 0.0f, 1.0f);	// UNOFFICIAL 2026-10-18 (_rmaos maps)
	Settings.Rain.RmaosAOScale = std::clamp(RmaosScaleOr1("Shaders.PBR.Rain", "AOScale"), 0.0f, 1.0f);
	Settings.Rain.RmaosRoughnessScale = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Rain", "RoughnessScale"), 1.0f, 2.0f);

	Settings.Night.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.Night", "Saturation");
	Settings.Night.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.Night", "Metallicness");
	Settings.Night.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.Night", "Roughness");
	Settings.Night.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.Night", "LightingScale");
	Settings.Night.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.Night", "AmbientScale");
	Settings.Night.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.Night", "SkylightingScale");
	Settings.Night.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.Night", "SkylightingDirectionality");
	Settings.Night.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.Night", "SkylightingSaturation");
	Settings.Night.RmaosMetalnessScale = std::clamp(RmaosScaleOr1("Shaders.PBR.Night", "MetalnessScale"), 0.0f, 1.0f);	// UNOFFICIAL 2026-10-18 (_rmaos maps)
	Settings.Night.RmaosAOScale = std::clamp(RmaosScaleOr1("Shaders.PBR.Night", "AOScale"), 0.0f, 1.0f);
	Settings.Night.RmaosRoughnessScale = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Night", "RoughnessScale"), 1.0f, 2.0f);

	Settings.NightRain.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "Saturation");
	Settings.NightRain.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "Metallicness");
	Settings.NightRain.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "Roughness");
	Settings.NightRain.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "LightingScale");
	Settings.NightRain.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "AmbientScale");
	Settings.NightRain.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "SkylightingScale");
	Settings.NightRain.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "SkylightingDirectionality");
	Settings.NightRain.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "SkylightingSaturation");
	Settings.NightRain.RmaosMetalnessScale = std::clamp(RmaosScaleOr1("Shaders.PBR.NightRain", "MetalnessScale"), 0.0f, 1.0f);	// UNOFFICIAL 2026-10-18 (_rmaos maps)
	Settings.NightRain.RmaosAOScale = std::clamp(RmaosScaleOr1("Shaders.PBR.NightRain", "AOScale"), 0.0f, 1.0f);
	Settings.NightRain.RmaosRoughnessScale = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "RoughnessScale"), 1.0f, 2.0f);

	Settings.Interiors.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "Saturation");
	Settings.Interiors.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "Metallicness");
	Settings.Interiors.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "Roughness");
	Settings.Interiors.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "LightingScale");
	Settings.Interiors.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "AmbientScale");
	Settings.Interiors.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "SkylightingScale");
	Settings.Interiors.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "SkylightingDirectionality");
	Settings.Interiors.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "SkylightingSaturation");
	Settings.Interiors.RmaosMetalnessScale = std::clamp(RmaosScaleOr1("Shaders.PBR.Interiors", "MetalnessScale"), 0.0f, 1.0f);	// UNOFFICIAL 2026-10-18 (_rmaos maps)
	Settings.Interiors.RmaosAOScale = std::clamp(RmaosScaleOr1("Shaders.PBR.Interiors", "AOScale"), 0.0f, 1.0f);
	Settings.Interiors.RmaosRoughnessScale = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "RoughnessScale"), 1.0f, 2.0f);
}

// UNOFFICIAL linear lighting's gamma for this frame (see UpdateSettings). With NightGamma set, exteriors blend from Gamma (day) to
// NightGamma (night) on the same transition curve as the other day / night settings (GetTransitionValue); interiors keep Gamma.
// Without it (0, the default) this is Gamma exactly as before 2026-10-20. LinearPreDecode remakes its copies only when the value
// changes (dusk and dawn): a dozen powf.
void PBRShaders::ApplyLinearGamma() {
	float gamma = 1.0f;
	if (LinearSettings.Enabled) {
		gamma = LinearSettings.NightGamma > 0.0f ?
			TheShaderManager->GetTransitionValue(LinearSettings.Gamma, LinearSettings.NightGamma, LinearSettings.Gamma) : LinearSettings.Gamma;
	}
	Constants.Linear.x = gamma;
	Constants.Linear.y = 1.0f / gamma;
	// UNOFFICIAL 2026-10-20 FalloffMatch (LinearLight.hlsl LinFalloff): the exponent a lamp's distance falloff is decoded with. 1 = the
	// lamp alone falls off as in the game (att^gamma); 0 = the falloff used as is in linear light (before 2026-10-20: a lamp alone came
	// out att^(1/gamma) bright -- 2-3.5x along its reach; Rivet City's hangar ~2x too bright). Live.
	Constants.Linear.z = 1.0f + (gamma - 1.0f) * LinearSettings.FalloffMatch;
	PassSums::Gamma = gamma;	// UNOFFICIAL ExactPassSums: the fold-in decodes with the same gamma
	LinearPreDecode::Gamma = gamma;	// UNOFFICIAL 2026-10-17 LinearPreDecode: the colours decoded on the CPU, the same gamma
}

// UNOFFICIAL 2026-10-21 LampFalloff (Includes/LampFalloff.hlsl LampShape): the shape's strength that makes a lamp send out as much light
// over its reach as the game's falloff does in linear light (display gamma 2.2): the integral of att^2.2 over the lamp's sphere
// (x^2 dx, x = d / r) over the shape's. Falloff 0.5 comes out close to the game's own curve (strength 1.15); 0.25 -> 2.73, 0.1 -> 12.2.
float PBRShaders::LampFalloffNorm(float Falloff) {
	const double k = 1.0 / ((double)Falloff * Falloff);
	double game = 0.0, shape = 0.0;
	const int steps = 2048;
	for (int i = 0; i < steps; i++) {
		const double x = (i + 0.5) / steps, q = x * x, window = (1.0 - q * q) * (1.0 - q * q);
		game += pow(1.0 - q, 2.2) * q;
		shape += window / (1.0 + q * k) * q;
	}
	return shape > 0.0 ? (float)(game / shape) : 1.0f;
}

void PBRShaders::UpdateConstants() {
	ApplyLinearGamma();	// first: the LinearScales below decode with it

	// UNOFFICIAL 2026-10-21 LampFalloff (Includes/LampFalloff.hlsl), live: the shape outdoors with Exterior, indoors with Interior.
	{
		const LampFalloffSettingsStruct& lamp = LampFalloffSettings;
		// Started with Exterior but not Interior, the indoor lamp-shadow records were built without the shape (and without MatchEverywhere
		// unless it was on too): switched on later, both wait indoors for a restart, so that every object in a room stays alike.
		const bool indoors = !TheShaderManager->GameState.isExterior;
		const bool interiorBuilt = LampFalloffInteriorDefine() == nullptr;
		const bool shaped = indoors ? lamp.Interior && interiorBuilt : lamp.Exterior;
		const bool match = lamp.MatchEverywhere && (!indoors || interiorBuilt || LampFalloffAtStartup(LampFalloffMatch));
		Constants.LampFalloff = D3DXVECTOR4(shaped ? 1.0f : 0.0f, 1.0f / (lamp.Falloff * lamp.Falloff), lamp.Norm * lamp.Brightness,
			match ? 1.0f : 0.0f);
	}

	// UNOFFICIAL TrimLampPasses (ObjectTemplate.hlsl DIFFUSE / POINT passes), live.
	Constants.LampPassTrim.x = TheSettingManager->SettingsMain.Main.TrimLampPasses ? 1.0f : 0.0f;

	// UNOFFICIAL CubeReflections, live: zero while the Reflections effect does not keep the cube up (off, CubeMap off, a frame
	// without it), so the shaders never read a stale or missing cube.
	const ReflectionsEffect* reflections = TheShaderManager->Effects.Reflections;
	const bool cube = reflections && reflections->CubeReady();
	Constants.EnvCube = D3DXVECTOR4(cube ? MetalSettings.CubeReflections : 0.0f, cube ? MetalSettings.EnvCubeReflections : 0.0f,
		(float)(ReflectionsEffect::CubeLevels - 1), 0.0f);

	// UNOFFICIAL foliage translucency (see UpdateSettings), live.
	Constants.Translucency.x = FoliageSettings.Translucency;
	Constants.Translucency.y = FoliageSettings.Power;
	Constants.Translucency.z = FoliageSettings.SunGate;
	Constants.Translucency.w = GrassCollision::FieldScale();	// UNOFFICIAL 2026-10-19 grass collision: 1 / its push texture's width (0 off)

	// get max value between rain animator and puddle animator
	float rainFactor = max(TheShaderManager->Effects.WetWorld->Constants.Data.x, TheShaderManager->Effects.WetWorld->Constants.Data.z);

	Constants.ExtraData.x = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.Saturation, Settings.Night.Saturation, Settings.Interiors.Saturation),
		TheShaderManager->GetTransitionValue(Settings.Rain.Saturation, Settings.NightRain.Saturation, Settings.Interiors.Saturation), rainFactor);

	// Hemisphere skylight strength. No separate toggle: 0 disables it.
	Constants.ExtraData.y = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.SkylightingScale, Settings.Night.SkylightingScale, Settings.Interiors.SkylightingScale),
		TheShaderManager->GetTransitionValue(Settings.Rain.SkylightingScale, Settings.NightRain.SkylightingScale, Settings.Interiors.SkylightingScale), rainFactor);

	// UNOFFICIAL: no shader constant; Sky.cpp folds it into the sky irradiance coefficients.
	SkylightSaturation = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.SkylightingSaturation, Settings.Night.SkylightingSaturation, Settings.Interiors.SkylightingSaturation),
		TheShaderManager->GetTransitionValue(Settings.Rain.SkylightingSaturation, Settings.NightRain.SkylightingSaturation, Settings.Interiors.SkylightingSaturation), rainFactor);


	// UNOFFICIAL: where the metal has faded out with distance (Object.hlsl getMetalFade). Never past the point where the
	// game starts fading objects' highlights out: beyond its end it draws them without, and metal still on vanished at
	// once. Read every frame, as the game may change it.
	float metalFade = MetalSettings.FadeDistance;
	const float highlightsFade = *BSShaderManager::fSpecularLODStartFade;
	if (*BSShaderManager::fSpecularLODEnd > 0.0f && highlightsFade > 0.0f && (metalFade <= 0.0f || metalFade > highlightsFade))
		metalFade = highlightsFade;
	Constants.ExtraData.w = metalFade;

	// Used only when SKYLIGHTING_MODE is 1; the SH path has no direction to lean.
	Constants.ExtraData.z = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.SkylightingDirectionality, Settings.Night.SkylightingDirectionality, Settings.Interiors.SkylightingDirectionality),
		TheShaderManager->GetTransitionValue(Settings.Rain.SkylightingDirectionality, Settings.NightRain.SkylightingDirectionality, Settings.Interiors.SkylightingDirectionality), rainFactor);

	Constants.Data.x = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.Metallicness, Settings.Night.Metallicness, Settings.Interiors.Metallicness),
		TheShaderManager->GetTransitionValue(Settings.Rain.Metallicness, Settings.NightRain.Metallicness, Settings.Interiors.Metallicness), rainFactor);
	Constants.Data.y = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.Roughness, Settings.Night.Roughness, Settings.Interiors.Roughness),
		TheShaderManager->GetTransitionValue(Settings.Rain.Roughness, Settings.NightRain.Roughness, Settings.Interiors.Roughness), rainFactor);

	Constants.Data.z = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.LightScale, Settings.Night.LightScale, Settings.Interiors.LightScale),
		TheShaderManager->GetTransitionValue(Settings.Rain.LightScale, Settings.NightRain.LightScale, Settings.Interiors.LightScale), rainFactor);
	Constants.Data.w = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.AmbientScale, Settings.Night.AmbientScale, Settings.Interiors.AmbientScale),
		TheShaderManager->GetTransitionValue(Settings.Rain.AmbientScale, Settings.NightRain.AmbientScale, Settings.Default.AmbientScale), rainFactor);

	// UNOFFICIAL 2026-10-17 LinearPreDecode (Object.hlsl TESR_LinearScales): the strengths LinScale decoded per pixel, decoded here once
	// (LinScale: pow(max(s, 0), gamma) when gamma > 1).
	const float gamma = Constants.Linear.x;
	auto linScale = [gamma](float s) { return gamma > 1.0f ? powf(s > 0.0f ? s : 0.0f, gamma) : s; };
	Constants.LinearScales = D3DXVECTOR4(linScale(Constants.Data.z), linScale(Constants.Data.w), linScale(Constants.ExtraData.y), 0.0f);

	// UNOFFICIAL 2026-10-18 per-weather scales for materials with an _rmaos map (idea from Physically-Based-Reloaded (Balebandro), GPL-3.0,
	// commit c1415cb8; Object.hlsl RmaosRead): blended over day / night / interiors and rain like the values above. TESR_PBRMath z and w
	// carry MetalnessScale - 1 and RoughnessScale - 1 (0 = the map as it is, also for a register never written); AOScale scales the
	// Occlusion that RmaosDraw sends per object (c220.y). All 1.0 by default: the picture exactly as without them.
	auto weather = [&](float PBRSettings::* member) {
		return std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.*member, Settings.Night.*member, Settings.Interiors.*member),
			TheShaderManager->GetTransitionValue(Settings.Rain.*member, Settings.NightRain.*member, Settings.Interiors.*member), rainFactor);
	};
	Constants.Math.z = std::clamp(weather(&PBRSettings::RmaosMetalnessScale), 0.0f, 1.0f) - 1.0f;
	Constants.Math.w = std::clamp(weather(&PBRSettings::RmaosRoughnessScale), 1.0f, 2.0f) - 1.0f;
	RmaosSettings.OcclusionNow = RmaosSettings.Occlusion * std::clamp(weather(&PBRSettings::RmaosAOScale), 0.0f, 1.0f);

	// UNOFFICIAL 2026-10-25 Reflectivity ([Shaders.PBR.Main] ReflectivityExterior / ReflectivityInterior): the highlights (TESR_PBRMath.y,
	// Includes/PBR.hlsl ReflectivityF0), metal's reflections of its surroundings and the env-map pass (TESR_PBREnv.w). 1 = as before.
	const float reflectivity = TheShaderManager->GameState.isExterior ? ReflectivitySettings.Exterior : ReflectivitySettings.Interior;
	Constants.Math.y = reflectivity - 1.0f;
	Constants.MetalLook.z = MetalSettings.Reflections * reflectivity;
	Constants.Env.w = reflectivity - 1.0f;
};
