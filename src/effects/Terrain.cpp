#include "Terrain.h"
#include "../core/GpuProfiler.h"

bool ParallaxLiteDistanceMigrationPending = false;

void TerrainShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_TerrainData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainExtraData", &Constants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_TerrainSkyData", &Constants.SkyData);
	TheShaderManager->RegisterConstant("TESR_TerrainParallaxData", &ParallaxConstants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainParallaxExtraData", &ParallaxConstants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_TerrainBlendData", &BlendingConstants.Data);
	TheShaderManager->RegisterConstant("TESR_TerrainBlendExtra", &BlendingConstants.Extra);
	TheShaderManager->RegisterConstant("TESR_TerrainBlendShape", &BlendingConstants.Shape);
}

void TerrainShaders::BeginWorldScene() {
	BlendDepthState = 0;
	BlendWorldDepth = nullptr;
	BindsBeforeCopy = 0;
	TerrainShaders* terrain = TheShaderManager->Shaders.Terrain;
	if (!terrain || !terrain->BlendingSettings.Enabled || !TheTextureManager->TerrainBlendDepthTexture || !TheShaderManager->GameState.isExterior) return;
	IDirect3DSurface9* depth = nullptr;
	TheRenderManager->device->GetDepthStencilSurface(&depth);
	if (!depth) return;
	BlendWorldDepth = depth;
	depth->Release();
	BlendDepthState = 1;
}

void TerrainShaders::EndWorldScene() {
	BlendDepthState = 0;
	BlendWorldDepth = nullptr;
}

void TerrainShaders::PrepareBlending() {
	BlendingConstants.Data.x = 0.0f;
	if (BlendDepthState == 0 || !BlendWorldDepth) return;

	IDirect3DSurface9* current = nullptr;
	TheRenderManager->device->GetDepthStencilSurface(&current);
	const bool mainScene = current && current == BlendWorldDepth;
	if (current) current->Release();
	if (!mainScene) return;

	if (BlendDepthState == 1) {
		static GpuTimer copyTimer("Terrain blend depth copy");
		GpuProfileScope gpu(copyTimer, TheRenderManager->device);
		TheRenderManager->ResolveDepthBuffer(TheTextureManager->TerrainBlendDepthTexture);
		BlendDepthState = 2;
		static unsigned copies = 0;
		if (copies < 3 || copies % 3600 == 0)
			Logger::Log("Terrain blending: depth copied at the first land draw of the world scene, after %u other passes%s", BindsBeforeCopy,
				BindsBeforeCopy ? "" : " (nothing drawn before the land: nothing to blend with)");
		copies++;
	}
	BlendingConstants.Data.x = 1.0f;
	if (BlendingConstants.Extra.w > 0.5f) {
		NiDX9RenderState* state = TheRenderManager->renderState;
		if (!state->GetRenderState(D3DRS_ALPHABLENDENABLE)) {
			state->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE, RenderStateArgs);
			state->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA, RenderStateArgs);
			state->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA, RenderStateArgs);
			state->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD, RenderStateArgs);
		}
	}
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

	{
		static bool seen = false, wasLite = false;
		const bool lite = TheSettingManager->SettingsMain.Main.ParallaxLite;
		const bool switchedOn = lite && (seen ? !wasLite : ParallaxLiteDistanceMigrationPending);
		seen = true;
		wasLite = lite;
		ParallaxLiteDistanceMigrationPending = false;
		if (switchedOn && TheRenderManager && TheRenderManager->height) {
			const int suggested = (int)(1024.0f * TheRenderManager->height / 1440.0f + 0.5f);
			if (ParallaxSettings.MaxDistance > (float)suggested) {
				Logger::Log("UNOFFICIAL ParallaxLite switched on: Shaders.Terrain.Parallax MaxDistance %g -> %d (suggested for %u lines; "
					"change it in the menu if you want it longer, it is not changed again).", ParallaxSettings.MaxDistance, suggested,
					TheRenderManager->height);
				TheSettingManager->SetSetting("Shaders.Terrain.Parallax", "MaxDistance", suggested);
				ParallaxSettings.MaxDistance = (float)suggested;
			}
		}
	}

	LODSettings.NoiseScale = TheSettingManager->GetSettingF("Shaders.Terrain.LOD", "LODNoiseScale");
	LODSettings.NoiseTile = TheSettingManager->GetSettingF("Shaders.Terrain.LOD", "LODNoiseTile");

	BlendingSettings.Enabled = TheSettingManager->GetSettingI("Shaders.Terrain.Blending", "Enabled") != 0;
	BlendingSettings.Debug = TheSettingManager->GetSettingI("Shaders.Terrain.Blending", "Debug") != 0;
	BlendingSettings.BlendDistance = std::clamp(TheSettingManager->GetSettingF("Shaders.Terrain.Blending", "BlendDistance"), 0.5f, 20.0f);
	BlendingSettings.MaxDistance = std::clamp(TheSettingManager->GetSettingF("Shaders.Terrain.Blending", "MaxDistance"), 256.0f, 16384.0f);
	BlendingSettings.GroundWidth = std::clamp(TheSettingManager->GetSettingF("Shaders.Terrain.Blending", "GroundWidth"), 0.0f, 100.0f);
	BlendingSettings.HeightBlend = std::clamp(TheSettingManager->GetSettingF("Shaders.Terrain.Blending", "HeightBlend"), 0.0f, 2.0f);
	BlendingSettings.SlopeBlend = std::clamp(TheSettingManager->GetSettingF("Shaders.Terrain.Blending", "SlopeBlend"), 0.0f, 2.0f);
	BlendingSettings.MinPixels = std::clamp(TheSettingManager->GetSettingF("Shaders.Terrain.Blending", "MinPixels"), 0.0f, 8.0f);
	BlendingSettings.DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.Terrain.Blending", "DebugView"), 0, 4);
	if (BlendingSettings.Debug && BlendingSettings.DebugView == 0) BlendingSettings.DebugView = 1;
	BlendingConstants.Data.y = 1.0f / BlendingSettings.BlendDistance;
	BlendingConstants.Data.z = BlendingSettings.MaxDistance;
	BlendingConstants.Data.w = (float)BlendingSettings.DebugView;
	BlendingConstants.Shape = D3DXVECTOR4(BlendingSettings.GroundWidth, BlendingSettings.HeightBlend, BlendingSettings.SlopeBlend, BlendingSettings.MinPixels);
	BlendingConstants.Extra.w = TheSettingManager->GetSettingI("Shaders.Terrain.Blending", "Smooth") ? 1.0f : 0.0f;
}

void TerrainShaders::UpdateConstants() {
	// get max value between rain animator and puddle animator
	float rainFactor = max(TheShaderManager->Effects.WetWorld->Constants.Data.x, TheShaderManager->Effects.WetWorld->Constants.Data.z);

	if (!TheShaderManager->GameState.isExterior) return;

	BlendingConstants.Extra.x = 1.0f / (float)TheRenderManager->width;
	BlendingConstants.Extra.y = 1.0f / (float)TheRenderManager->height;
	TAAEffect* taa = TheShaderManager->Effects.TAA;
	BlendingConstants.Extra.z = taa && taa->Enabled && taa->Effect ? fmodf(BlendingConstants.Extra.z + 1.0f, 64.0f) : 0.0f;

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
	ParallaxConstants.Data.w = TheSettingManager->SettingsMain.Main.ParallaxLite ? 2.0f : ParallaxSettings.HighQuality;

	ParallaxConstants.ExtraData.x = ParallaxSettings.MaxDistance;
	ParallaxConstants.ExtraData.y = ParallaxSettings.Height;
	ParallaxConstants.ExtraData.z = ParallaxSettings.ShadowsIntensity;

	static const float WaterlineMargin = 10.0f;
	ParallaxConstants.ExtraData.w = -FLT_MAX;
	if (TheSettingManager->SettingsMain.Main.CheapUnderwaterTerrain && Tes && Tes->waterManager && Tes->waterManager->waterGroups.count) {
		TESWaterForm* water = nullptr;
		const float height = Tes->GetWaterHeight(Player, WorldSceneGraph, &water);
		if (water) ParallaxConstants.ExtraData.w = height - WaterlineMargin - TheRenderManager->CameraPosition.z;
	}
};

