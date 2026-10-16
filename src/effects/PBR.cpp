#include "PBR.h"

void PBRShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_PBRData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_PBRExtraData", &Constants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_PBRMetal", &Constants.Metal);
	TheShaderManager->RegisterConstant("TESR_PBRMetalLook", &Constants.MetalLook);
	TheShaderManager->RegisterConstant("TESR_SharpSky", &Constants.SharpSky);
	TheShaderManager->RegisterConstant("TESR_EnvCube", &Constants.EnvCube);
	TheShaderManager->RegisterConstant("TESR_LinearLighting", &Constants.Linear);
	TheShaderManager->RegisterConstant("TESR_PBREnv", &Constants.Env);
	TheShaderManager->RegisterConstant("TESR_LampPassTrim", &Constants.LampPassTrim);
	TheShaderManager->RegisterConstant("TESR_PBRMath", &Constants.Math);
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
	Constants.MetalLook.z = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "Reflections"), 0.0f, 4.0f);
	Constants.MetalLook.w = TheSettingManager->GetSettingI("Shaders.PBR.Metal", "DebugView") ? 1.0f : 0.0f;
	// UNOFFICIAL SharpSky (SkyAmbient.hlsl SkyAmbientSharpened): metal and the env maps take the sky from the direction they
	// reflect, sharper, rather than its smooth average. 0 = the plain sky.
	Constants.SharpSky.x = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "SharpSky"), 0.0f, 1.0f);
	// UNOFFICIAL environment maps by the light (EnvTemplate.hlsl).
	Constants.Env.x = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "Lighting"), 0.0f, 1.0f);
	Constants.Env.y = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "Strength"), 0.0f, 8.0f);
	Constants.Env.z = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "WithMetal"), 0.0f, 1.0f);
	Constants.Env.w = 0.0f;
	// UNOFFICIAL CubeReflections (Includes/LiveCube.hlsl): metal and the env maps reflect the live surroundings cube that the
	// Reflections effect keeps (CubeMap); applied in UpdateConstants only while that cube is kept up.
	MetalSettings.CubeReflections = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Metal", "CubeReflections"), 0.0f, 1.0f);
	MetalSettings.EnvCubeReflections = std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.EnvMaps", "CubeReflections"), 0.0f, 1.0f);

	// UNOFFICIAL linear lighting (Includes/LinearLight.hlsl: ObjectTemplate, TerrainTemplate). Enabled is read at startup
	// (ShaderRecord::LoadShader compiles it in); Gamma is live, and 1 returns every colour unchanged.
	const float linearGamma = TheSettingManager->GetSettingI("Shaders.PBR.Linear", "Enabled") ?
		std::clamp(TheSettingManager->GetSettingF("Shaders.PBR.Linear", "Gamma"), 1.0f, 3.0f) : 1.0f;
	Constants.Linear.x = linearGamma;
	Constants.Linear.y = 1.0f / linearGamma;

	// UNOFFICIAL 2026-10-16 EnergySplit (Includes/PBR.hlsl DiffuseEnergyKeep): white surfaces no longer send back more light than
	// they receive in the full and sun passes. Live.
	Constants.Math.x = TheSettingManager->GetSettingI("Shaders.PBR.Main", "EnergySplit") ? 1.0f : 0.0f;

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

	Settings.Rain.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "Saturation");
	Settings.Rain.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "Metallicness");
	Settings.Rain.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "Roughness");
	Settings.Rain.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "LightingScale");
	Settings.Rain.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "AmbientScale");
	Settings.Rain.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "SkylightingScale");
	Settings.Rain.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "SkylightingDirectionality");
	Settings.Rain.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.Rain", "SkylightingSaturation");

	Settings.Night.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.Night", "Saturation");
	Settings.Night.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.Night", "Metallicness");
	Settings.Night.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.Night", "Roughness");
	Settings.Night.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.Night", "LightingScale");
	Settings.Night.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.Night", "AmbientScale");
	Settings.Night.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.Night", "SkylightingScale");
	Settings.Night.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.Night", "SkylightingDirectionality");
	Settings.Night.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.Night", "SkylightingSaturation");

	Settings.NightRain.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "Saturation");
	Settings.NightRain.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "Metallicness");
	Settings.NightRain.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "Roughness");
	Settings.NightRain.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "LightingScale");
	Settings.NightRain.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "AmbientScale");
	Settings.NightRain.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "SkylightingScale");
	Settings.NightRain.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "SkylightingDirectionality");
	Settings.NightRain.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.NightRain", "SkylightingSaturation");

	Settings.Interiors.Saturation = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "Saturation");
	Settings.Interiors.Metallicness = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "Metallicness");
	Settings.Interiors.Roughness = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "Roughness");
	Settings.Interiors.LightScale = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "LightingScale");
	Settings.Interiors.AmbientScale = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "AmbientScale");
	Settings.Interiors.SkylightingScale = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "SkylightingScale");
	Settings.Interiors.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "SkylightingDirectionality");
	Settings.Interiors.SkylightingSaturation = TheSettingManager->GetSettingF("Shaders.PBR.Interiors", "SkylightingSaturation");
}

void PBRShaders::UpdateConstants() {
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
	Constants.Translucency.w = 0.0f;

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
};
