#include "Terrain.h"

void TerrainShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_TerrainData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainExtraData", &Constants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_TerrainSkyData", &Constants.SkyData);
	TheShaderManager->RegisterConstant("TESR_TerrainParallaxData", &ParallaxConstants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainParallaxExtraData", &ParallaxConstants.ExtraData);
}


void TerrainShaders::UpdateSettings() {
	usePBR = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "UsePBR");

	Settings.Default.Saturation = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "TerrainSaturation");
	Settings.Default.Metallicness = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "Metallicness");
	Settings.Default.Roughness = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "Roughness");
	Settings.Default.LightScale = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "LightingScale");
	Settings.Default.AmbientScale = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "AmbientScale");
	Settings.Default.SkylightingScale = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "SkylightingScale");
	Settings.Default.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.Terrain.Main", "SkylightingDirectionality");

	Settings.Rain.Saturation = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "TerrainSaturation");
	Settings.Rain.Metallicness = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "Metallicness");
	Settings.Rain.Roughness = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "Roughness");
	Settings.Rain.LightScale = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "LightingScale");
	Settings.Rain.AmbientScale = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "AmbientScale");
	Settings.Rain.SkylightingScale = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "SkylightingScale");
	Settings.Rain.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.Terrain.Rain", "SkylightingDirectionality");

	Settings.Night.Saturation = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "TerrainSaturation");
	Settings.Night.Metallicness = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "Metallicness");
	Settings.Night.Roughness = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "Roughness");
	Settings.Night.LightScale = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "LightingScale");
	Settings.Night.AmbientScale = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "AmbientScale");
	Settings.Night.SkylightingScale = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "SkylightingScale");
	Settings.Night.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.Terrain.Night", "SkylightingDirectionality");

	Settings.NightRain.Saturation = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "TerrainSaturation");
	Settings.NightRain.Metallicness = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "Metallicness");
	Settings.NightRain.Roughness = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "Roughness");
	Settings.NightRain.LightScale = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "LightingScale");
	Settings.NightRain.AmbientScale = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "AmbientScale");
	Settings.NightRain.SkylightingScale = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "SkylightingScale");
	Settings.NightRain.SkylightingDirectionality = TheSettingManager->GetSettingF("Shaders.Terrain.NightRain", "SkylightingDirectionality");

	ParallaxSettings.Enabled = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "Enabled");
	ParallaxSettings.HighQuality = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "HighQuality");
	ParallaxSettings.Shadows = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "Shadows");
	ParallaxSettings.HeightBlend = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "HeightBlend");
	ParallaxSettings.MaxDistance = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "MaxDistance");
	ParallaxSettings.Height = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "Height");
	ParallaxSettings.ShadowsIntensity = TheSettingManager->GetSettingF("Shaders.Terrain.Parallax", "ShadowsIntensity");

	LODSettings.NoiseScale = TheSettingManager->GetSettingF("Shaders.Terrain.LOD", "LODNoiseScale");
	LODSettings.NoiseTile = TheSettingManager->GetSettingF("Shaders.Terrain.LOD", "LODNoiseTile");
}

void TerrainShaders::UpdateConstants() {
	// get max value between rain animator and puddle animator
	float rainFactor = max(TheShaderManager->Effects.WetWorld->Constants.Data.x, TheShaderManager->Effects.WetWorld->Constants.Data.z);

	if (!TheShaderManager->GameState.isExterior) return;

	Constants.ExtraData.x = usePBR;
	Constants.ExtraData.y = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.Saturation, Settings.Night.Saturation, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.Saturation, Settings.NightRain.Saturation, 0.0), rainFactor);
	// Hemisphere skylight strength. No separate toggle: 0 disables it. Terrain has no
	// Interiors section -- UpdateConstants returns early indoors -- so the interior operand
	// of GetTransitionValue is 0, matching how the other terrain settings blend.
	Constants.SkyData.x = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.SkylightingScale, Settings.Night.SkylightingScale, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.SkylightingScale, Settings.NightRain.SkylightingScale, 0.0), rainFactor);


	// Used only when SKYLIGHTING_MODE is 1; the SH path has no direction to lean.
	Constants.SkyData.y = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.SkylightingDirectionality, Settings.Night.SkylightingDirectionality, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.SkylightingDirectionality, Settings.NightRain.SkylightingDirectionality, 0.0), rainFactor);

	Constants.ExtraData.z = LODSettings.NoiseScale;
	Constants.ExtraData.w = LODSettings.NoiseTile;

	if (usePBR) {
		Constants.Data.x = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.Metallicness, Settings.Night.Metallicness, 0.0),
			TheShaderManager->GetTransitionValue(Settings.Rain.Metallicness, Settings.NightRain.Metallicness, 0.0), rainFactor);
		Constants.Data.y = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.Roughness, Settings.Night.Roughness, 0.0),
			TheShaderManager->GetTransitionValue(Settings.Rain.Roughness, Settings.NightRain.Roughness, 0.0), rainFactor);
	}

	Constants.Data.z = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.LightScale, Settings.Night.LightScale, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.LightScale, Settings.NightRain.LightScale, 0.0), rainFactor);
	Constants.Data.w = std::lerp(TheShaderManager->GetTransitionValue(Settings.Default.AmbientScale, Settings.Night.AmbientScale, 0.0),
		TheShaderManager->GetTransitionValue(Settings.Rain.AmbientScale, Settings.NightRain.AmbientScale, 0.0), rainFactor);

	ParallaxConstants.Data.x = ParallaxSettings.Enabled;
	ParallaxConstants.Data.y = ParallaxSettings.Shadows;
	ParallaxConstants.Data.z = ParallaxSettings.HeightBlend;
	// .w: 0 = 8 steps, 1 = 16 (HighQuality), 2 = [Main.Main.ReducedQuality] ParallaxLite (Parallax.hlsl), which
	// replaces both.
	ParallaxConstants.Data.w = TheSettingManager->SettingsMain.Main.ParallaxLite ? 2.0f : ParallaxSettings.HighQuality;

	// ParallaxLite also caps how far the terrain parallax (and its shadows) reaches: 1024 units at 1440p, where the user
	// saw no difference from 2048 in a rocky desert view and it saved 0.8 ms on top of the lite search. Scaled with the
	// screen height, since a bump's size on screen is (screen height / distance): 768 at 1080p, 1536 at 4K. A lower
	// MaxDistance the player chose still applies; everyone without ParallaxLite keeps MaxDistance as it is.
	float maxDistance = ParallaxSettings.MaxDistance;
	if (TheSettingManager->SettingsMain.Main.ParallaxLite)
		maxDistance = min(maxDistance, 1024.0f * TheRenderManager->height / 1440.0f);
	ParallaxConstants.ExtraData.x = maxDistance;
	ParallaxConstants.ExtraData.y = ParallaxSettings.Height;
	ParallaxConstants.ExtraData.z = ParallaxSettings.ShadowsIntensity;

	// [Main.Main.ReducedQuality] CheapUnderwaterTerrain: ground below the water surface skips parallax and its shadows
	// (TerrainTemplate.hlsl). .w is the camera-relative height below which terrain counts as under water: the level of
	// the water the player is in or looking at, a little lower so the shoreline itself keeps its parallax. Only while
	// a water plane is loaded nearby (otherwise the cell's default water level could lie above dry ground). -FLT_MAX = off.
	static const float WaterlineMargin = 10.0f;  // game units, about 14 cm
	ParallaxConstants.ExtraData.w = -FLT_MAX;
	if (TheSettingManager->SettingsMain.Main.CheapUnderwaterTerrain && Tes && Tes->waterManager && Tes->waterManager->waterGroups.count) {
		TESWaterForm* water = nullptr;
		const float height = Tes->GetWaterHeight(Player, WorldSceneGraph, &water);
		if (water) ParallaxConstants.ExtraData.w = height - WaterlineMargin - TheRenderManager->CameraPosition.z;
	}
};

