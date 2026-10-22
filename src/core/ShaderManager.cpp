#include "OrthoProbe.h"
#include "GpuProfiler.h"
#include "PointShadowSlots.h"
#include "PointShadowForward.h"
#include "LampFixtures.h"
#include "EffectSelfTest.h"

#define RESZ_CODE 0x7FA05000

/**
* Initializes the Shader Manager Singleton.
* The Shader Manager creates and holds onto the Effects activated in the Settings Manager, and sets the constants.
*/
void ShaderManager::Initialize() {

	auto timer = TimeLogger();

	Logger::Log("Starting the shaders manager...");
	TheShaderManager = new ShaderManager();
	TheShaderManager->FrameVertex = NULL;

	TheShaderManager->EffectReloadQueued = false;

	memset(TheShaderManager->WaterVertexShaders, NULL, sizeof(WaterVertexShaders));
	memset(TheShaderManager->WaterPixelShaders, NULL, sizeof(WaterPixelShaders));

	TheShaderManager->CreateFrameVertex(TheRenderManager->width, TheRenderManager->height, &TheShaderManager->FrameVertex);

	TheShaderManager->PreviousCell = nullptr;
	TheShaderManager->IsMenuSwitch = false;

	// Make sure the shader/effect cache directories exist.
	std::error_code ec;

	std::filesystem::create_directories("data/Shaders/NewVegasReloaded/Shaders/Cache/Bink", ec);
	if (ec) {
		Logger::Log("Failed to create shader cache directory: %s", ec.message());
	}
	std::filesystem::create_directories("data/Shaders/NewVegasReloaded/Shaders/Cache/Shadows", ec);
	if (ec) {
		Logger::Log("Failed to create shader cache directory: %s", ec.message());
	}
	std::filesystem::create_directories("data/Shaders/NewVegasReloaded/Effects/Cache", ec);
	if (ec) {
		Logger::Log("Failed to create effect cache directory: %s", ec.message());
	}
	std::filesystem::create_directories("Data/Textures/NewVegasReloaded/LUTs", ec);
	if (ec) {
		Logger::Log("Failed to create LUT directory: %s", ec.message());
	}

	// initializing the list of effect names
	TheShaderManager->RegisterEffect<AvgLumaEffect>(&TheShaderManager->Effects.AvgLuma);
	TheShaderManager->RegisterEffect<SkyViewEffect>(&TheShaderManager->Effects.SkyView);
	TheShaderManager->RegisterEffect<AmbientLightEffect>(&TheShaderManager->Effects.AmbientLight);
	TheShaderManager->RegisterEffect<AmbientOcclusionEffect>(&TheShaderManager->Effects.AmbientOcclusion);
	TheShaderManager->RegisterEffect<SkinScatteringEffect>(&TheShaderManager->Effects.SkinScattering);
	TheShaderManager->RegisterEffect<BloodLensEffect>(&TheShaderManager->Effects.BloodLens);
	TheShaderManager->RegisterEffect<BloomEffect>(&TheShaderManager->Effects.Bloom);
	TheShaderManager->RegisterEffect<BloomLegacyEffect>(&TheShaderManager->Effects.BloomLegacy);
	TheShaderManager->RegisterEffect<ColoringEffect>(&TheShaderManager->Effects.Coloring);
	TheShaderManager->RegisterEffect<LUTEffect>(&TheShaderManager->Effects.LUT);
	TheShaderManager->RegisterEffect<CinemaEffect>(&TheShaderManager->Effects.Cinema);
	TheShaderManager->RegisterEffect<CombineDepthEffect>(&TheShaderManager->Effects.CombineDepth);
	TheShaderManager->RegisterEffect<DepthOfFieldEffect>(&TheShaderManager->Effects.DepthOfField);
	TheShaderManager->RegisterEffect<DebugEffect>(&TheShaderManager->Effects.Debug);
	TheShaderManager->RegisterEffect<ExposureEffect>(&TheShaderManager->Effects.Exposure);
	TheShaderManager->RegisterEffect<FlashlightEffect>(&TheShaderManager->Effects.Flashlight);
	TheShaderManager->RegisterEffect<FlashlightBeamEffect>(&TheShaderManager->Effects.FlashlightBeam);
	TheShaderManager->RegisterEffect<GodRaysEffect>(&TheShaderManager->Effects.GodRays);
	TheShaderManager->RegisterEffect<ImageAdjustEffect>(&TheShaderManager->Effects.ImageAdjust);
	TheShaderManager->RegisterEffect<LensEffect>(&TheShaderManager->Effects.Lens);
	TheShaderManager->RegisterEffect<LowHFEffect>(&TheShaderManager->Effects.LowHF);
	TheShaderManager->RegisterEffect<MotionBlurEffect>(&TheShaderManager->Effects.MotionBlur);
	TheShaderManager->RegisterEffect<NormalsEffect>(&TheShaderManager->Effects.Normals);
	TheShaderManager->RegisterEffect<RainEffect>(&TheShaderManager->Effects.Rain);
	TheShaderManager->RegisterEffect<SharpeningEffect>(&TheShaderManager->Effects.Sharpening);
	TheShaderManager->RegisterEffect<BounceLightEffect>(&TheShaderManager->Effects.BounceLight);
	TheShaderManager->RegisterEffect<ShadowsExteriorEffect>(&TheShaderManager->Effects.ShadowsExteriors);
	TheShaderManager->RegisterEffect<ShadowsInteriorsEffect>(&TheShaderManager->Effects.ShadowsInteriors);
	TheShaderManager->RegisterEffect<PointShadowsEffect>(&TheShaderManager->Effects.PointShadows);
	TheShaderManager->RegisterEffect<PointShadows2Effect>(&TheShaderManager->Effects.PointShadows2);
	TheShaderManager->RegisterEffect<SunShadowsEffect>(&TheShaderManager->Effects.SunShadows);
	TheShaderManager->RegisterEffect<SunFarEffect>(&TheShaderManager->Effects.SunFar);
	TheShaderManager->RegisterEffect<SpecularEffect>(&TheShaderManager->Effects.Specular);
	TheShaderManager->RegisterEffect<SnowEffect>(&TheShaderManager->Effects.Snow);
	TheShaderManager->RegisterEffect<SnowAccumulationEffect>(&TheShaderManager->Effects.SnowAccumulation);
	TheShaderManager->RegisterEffect<UnderwaterEffect>(&TheShaderManager->Effects.Underwater);
	TheShaderManager->RegisterEffect<VolumetricFogEffect>(&TheShaderManager->Effects.VolumetricFog);
	TheShaderManager->RegisterEffect<WaterLensEffect>(&TheShaderManager->Effects.WaterLens);
	TheShaderManager->RegisterEffect<WetWorldEffect>(&TheShaderManager->Effects.WetWorld);
	TheShaderManager->RegisterEffect<BarrelHazeEffect>(&TheShaderManager->Effects.BarrelHaze);
	TheShaderManager->RegisterEffect<HeatHazeEffect>(&TheShaderManager->Effects.HeatHaze);
	TheShaderManager->RegisterEffect<SandstormEffect>(&TheShaderManager->Effects.Sandstorm);
	TheShaderManager->RegisterEffect<VolumetricLightEffect>(&TheShaderManager->Effects.VolumetricLight);
	TheShaderManager->RegisterEffect<SkyDitherEffect>(&TheShaderManager->Effects.SkyDither);
	TheShaderManager->RegisterEffect<BodycamEffect>(&TheShaderManager->Effects.Bodycam);
	TheShaderManager->RegisterEffect<ReflectionsEffect>(&TheShaderManager->Effects.Reflections);
	TheShaderManager->RegisterEffect<VolumetricCloudsEffect>(&TheShaderManager->Effects.VolumetricClouds);
	TheShaderManager->RegisterEffect<BloodFlowEffect>(&TheShaderManager->Effects.BloodFlow);
	TheShaderManager->Effects.BarrelHaze->Enabled = TheShaderManager->Effects.BarrelHaze->Effect != nullptr;
	TheShaderManager->RegisterEffect<VolumetricSmokeEffect>(&TheShaderManager->Effects.VolumetricSmoke);
	TheShaderManager->Effects.VolumetricSmoke->Enabled = TheShaderManager->Effects.VolumetricSmoke->Effect != nullptr;
	TheShaderManager->RegisterEffect<DitherBusterEffect>(&TheShaderManager->Effects.DitherBuster);
	TheShaderManager->RegisterEffect<SMAAEffect>(&TheShaderManager->Effects.SMAA);
	TheShaderManager->RegisterEffect<FXAAEffect>(&TheShaderManager->Effects.FXAA);
	TheShaderManager->RegisterEffect<TAAEffect>(&TheShaderManager->Effects.TAA);

	TheShaderManager->RegisterShaderCollection<TonemappingShaders>(&TheShaderManager->Shaders.Tonemapping);
	TheShaderManager->RegisterShaderCollection<POMShaders>(&TheShaderManager->Shaders.POM);
	TheShaderManager->RegisterShaderCollection<PBRShaders>(&TheShaderManager->Shaders.PBR);
	TheShaderManager->RegisterShaderCollection<WaterShaders>(&TheShaderManager->Shaders.Water);
	TheShaderManager->RegisterShaderCollection<SkyShaders>(&TheShaderManager->Shaders.Sky);
	TheShaderManager->RegisterShaderCollection<SkinShaders>(&TheShaderManager->Shaders.Skin);
	TheShaderManager->RegisterShaderCollection<GrassShaders>(&TheShaderManager->Shaders.Grass);
	TheShaderManager->RegisterShaderCollection<ParticleShaders>(&TheShaderManager->Shaders.Particles);
	TheShaderManager->RegisterShaderCollection<TerrainShaders>(&TheShaderManager->Shaders.Terrain);
	
	//setup map of constant names
	TheShaderManager->RegisterConstant("TESR_WorldTransform", (D3DXVECTOR4*)&TheRenderManager->worldMatrix);
	TheShaderManager->RegisterConstant("TESR_ViewTransform", (D3DXVECTOR4*)&TheRenderManager->viewMatrix);
	TheShaderManager->RegisterConstant("TESR_InvViewTransform", (D3DXVECTOR4*)&TheRenderManager->InvViewMatrix);
	TheShaderManager->RegisterConstant("TESR_ProjectionTransform", (D3DXVECTOR4*)&TheRenderManager->projMatrix);
	TheShaderManager->RegisterConstant("TESR_InvProjectionTransform",  (D3DXVECTOR4*)&TheRenderManager->InvProjMatrix);
	TheShaderManager->RegisterConstant("TESR_WorldViewProjectionTransform",  (D3DXVECTOR4*)&TheRenderManager->WorldViewProjMatrix);
	TheShaderManager->RegisterConstant("TESR_InvViewProjectionTransform", (D3DXVECTOR4*)&TheRenderManager->InvViewProjMatrix);
	TheShaderManager->RegisterConstant("TESR_ViewProjectionTransform", (D3DXVECTOR4*)&TheRenderManager->ViewProjMatrix);
	TheShaderManager->RegisterConstant("TESR_OcclusionWorldViewProjTransform", (D3DXVECTOR4*)&TheShaderManager->ShaderConst.OcclusionMap.OcclusionWorldViewProj);
	TheShaderManager->RegisterConstant("TESR_LightPosition", (D3DXVECTOR4*) &TheShaderManager->LightPosition);
	TheShaderManager->RegisterConstant("TESR_LightColor", (D3DXVECTOR4*) &TheShaderManager->LightColor);
	TheShaderManager->RegisterConstant("TESR_ExtraLights", (D3DXVECTOR4*) &TheShaderManager->ExtraLights);
	TheShaderManager->RegisterConstant("TESR_SpotLightPosition", (D3DXVECTOR4*) &TheShaderManager->SpotLightPosition);
	TheShaderManager->RegisterConstant("TESR_SpotLightColor", (D3DXVECTOR4*) &TheShaderManager->SpotLightColor);
	TheShaderManager->RegisterConstant("TESR_SpotLightDirection", (D3DXVECTOR4*) &TheShaderManager->SpotLightDirection);
	TheShaderManager->RegisterConstant("TESR_SpotLightToWorldTransform", (D3DXVECTOR4*) &TheShaderManager->SpotLightWorldToLightMatrix[0]);
	TheShaderManager->RegisterConstant("TESR_VolumetricData", &TheShaderManager->VolumetricData);
	TheShaderManager->RegisterConstant("TESR_ViewSpaceLightDir", &TheShaderManager->ShaderConst.ViewSpaceLightDir);
	TheShaderManager->RegisterConstant("TESR_ScreenSpaceLightDir", &TheShaderManager->ShaderConst.ScreenSpaceLightDir);
	TheShaderManager->RegisterConstant("TESR_ReciprocalResolution", &TheShaderManager->ShaderConst.ReciprocalResolution);
	TheShaderManager->RegisterConstant("TESR_CameraForward", &TheRenderManager->CameraForward);
	TheShaderManager->RegisterConstant("TESR_DepthConstants", &TheRenderManager->DepthConstants);
	TheShaderManager->RegisterConstant("TESR_CameraData", &TheRenderManager->CameraData);
	TheShaderManager->RegisterConstant("TESR_CameraPosition", &TheRenderManager->CameraPosition);
	TheShaderManager->RegisterConstant("TESR_SunDirection", &TheShaderManager->ShaderConst.SunDir);
	TheShaderManager->RegisterConstant("TESR_SunPosition", &TheShaderManager->ShaderConst.SunPosition);
	TheShaderManager->RegisterConstant("TESR_SunTiming", &TheShaderManager->ShaderConst.SunTiming);
	TheShaderManager->RegisterConstant("TESR_SunAmount", &TheShaderManager->ShaderConst.SunAmount);
	TheShaderManager->RegisterConstant("TESR_GameTime", &TheShaderManager->ShaderConst.GameTime);
	TheShaderManager->RegisterConstant("TESR_FogData", &TheShaderManager->ShaderConst.fogData);
	TheShaderManager->RegisterConstant("TESR_FogDistance", &TheShaderManager->ShaderConst.fogDistance);
	TheShaderManager->RegisterConstant("TESR_FogColor", &TheShaderManager->ShaderConst.fogColor);
	TheShaderManager->RegisterConstant("TESR_SunColor", &TheShaderManager->ShaderConst.sunColor);
	TheShaderManager->RegisterConstant("TESR_SunDiskColor", &TheShaderManager->ShaderConst.sunDiskColor);
	TheShaderManager->RegisterConstant("TESR_SunAmbient", &TheShaderManager->ShaderConst.sunAmbient);
	TheShaderManager->RegisterConstant("TESR_SkyColor", &TheShaderManager->ShaderConst.skyColor);
	TheShaderManager->RegisterConstant("TESR_SkyLowColor", &TheShaderManager->ShaderConst.skyLowColor);
	TheShaderManager->RegisterConstant("TESR_HorizonColor", &TheShaderManager->ShaderConst.horizonColor);

	TheShaderManager->InitializeConstants();

	timer.LogTime("ShaderManager::Initialize");
}

void ShaderManager::CreateFrameVertex(UInt32 Width, UInt32 Height, IDirect3DVertexBuffer9** FrameVertex) {
	
	void* VertexData = NULL;
	float OffsetX = (1.0f / (float)Width) * 0.5f;
	float OffsetY = (1.0f / (float)Height) * 0.5f;
	
	FrameVS FrameVertices[] = {
		{ -1.0f,  1.0f, 1.0f, 0.0f + OffsetX, 0.0f + OffsetY },
		{ -1.0f, -1.0f, 1.0f, 0.0f + OffsetX, 1.0f + OffsetY },
		{  1.0f,  1.0f, 1.0f, 1.0f + OffsetX, 0.0f + OffsetY },
		{  1.0f, -1.0f, 1.0f, 1.0f + OffsetX, 1.0f + OffsetY }
	};
	TheRenderManager->device->CreateVertexBuffer(4 * sizeof(FrameVS), D3DUSAGE_WRITEONLY, FrameFVF, D3DPOOL_DEFAULT, FrameVertex, NULL);
	(*FrameVertex)->Lock(0, 0, &VertexData, NULL);
	memcpy(VertexData, FrameVertices, sizeof(FrameVertices));
	(*FrameVertex)->Unlock();

}


/*
* Initializes and register an effect and its constants
*/
template <typename T> void ShaderManager::RegisterEffect(T** Pointer)
{
	T* effect = new T();
	*Pointer = effect;

	EffectsNames[effect->Name] = (EffectRecord**)Pointer;
	effect->UpdateSettings();
	effect->RegisterConstants();
	effect->RegisterTextures();
	effect->LoadEffect();
}


template <typename T> void ShaderManager::RegisterShaderCollection(T** Pointer)
{
	T* collection = new T();
	*Pointer = collection;
	
	ShaderNames[collection->Name] = (ShaderCollection**)Pointer;
	collection->RegisterConstants();
}


/*
 * Drops the cached texture pointer for a named sampler across EVERY loaded game shader.
 *
 * Counterpart to EffectRecord's per-effect ClearSampler. Call both whenever a TESR_ texture
 * is released and recreated (see ShadowsExteriorEffect::RecreateTextures): each shader owns
 * a private TextureRecord holding a raw IDirect3DTexture9*, which SetCT only re-resolves
 * when it is null. Miss one and it keeps sampling the released texture -- which the D3D9
 * device is usually still holding a reference to, so instead of failing it quietly returns
 * the last contents that were rendered into it.
 */
void ShaderManager::ClearShaderSamplers(const char* TextureName, size_t Length)
{
	for (const auto& Entry : ShaderNames) {
		ShaderCollection* Collection = Entry.second ? *Entry.second : nullptr;
		if (!Collection) continue;

		for (auto& VertexShader : Collection->VertexShaderList) {
			for (int i = 0; i < 3; i++)  // Default / Exterior / Interior
				if (VertexShader->ShaderProg[i]) VertexShader->ShaderProg[i]->ClearSampler(TextureName, Length);
		}
		for (auto& PixelShader : Collection->PixelShaderList) {
			for (int i = 0; i < 3; i++)
				if (PixelShader->ShaderProg[i]) PixelShader->ShaderProg[i]->ClearSampler(TextureName, Length);
		}
	}
}


void ShaderManager::RegisterConstant(const char* Name, D3DXVECTOR4* FloatValue)
{
	ConstantsTable[Name] = FloatValue;
}


void ShaderManager::InitializeConstants() {

	ShaderConst.pWeather = NULL;

	ShaderConst.ReciprocalResolution.x = 1.0f / (float)TheRenderManager->width;
	ShaderConst.ReciprocalResolution.y = 1.0f / (float)TheRenderManager->height;
	ShaderConst.ReciprocalResolution.z = (float)TheRenderManager->width / (float)TheRenderManager->height;
	ShaderConst.ReciprocalResolution.w = 0.0f; // Reserved to store the FoV
}


/*
Updates the values of the constants that can be accessed from shader code, with values representing the state of the game's elements.
*/
void ShaderManager::UpdateConstants() {

	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // Main toggle

	auto timer = TimeLogger();

	bool IsThirdPersonView = !TheCameraManager->IsFirstPerson();
	Sky* WorldSky = Tes->sky;
	NiNode* SunRoot = WorldSky->sun->RootNode;
	TESClimate* currentClimate = WorldSky->firstClimate;
	TESWeather* currentWeather = WorldSky->firstWeather;
	TESWeather* previousWeather = WorldSky->secondWeather;
	TESObjectCELL* currentCell = Player->parentCell;
	TESWorldSpace* currentWorldSpace = Player->GetWorldSpace();
	TESRegion* currentRegion = Player->GetRegion();
	float weatherPercent = WorldSky->weatherPercent;
	float lastGameTime = ShaderConst.GameTime.y;
	const char* sectionName = NULL;
	avglumaRequired = false; // toggle for rendering AvgLuma
	orthoRequired = false; // toggle for rendering Ortho map
	orthoSkyRequired = false;

	if (Effects.Debug->Enabled) avglumaRequired = true;

	// context variables
	GameState.PipBoyIsOn = InterfaceManager->IsPipBoyOpen();
	GameState.VATSIsOn = InterfaceManager->IsActive(Menu::kMenuType_VATS);

	const bool bHasRTM = TheGameMenuManager->IsLiveMenu != nullptr;
	bool bComputersMenu = InterfaceManager->IsActive(Menu::kMenuType_Computers) && (bHasRTM ? (TheGameMenuManager->IsLiveMenu(Menu::kMenuType_Computers, false, false) == GameMenuManager::MENU_PAUSED) : true);
	if (!bComputersMenu)
		bComputersMenu = InterfaceManager->IsActive(Menu::kMenuType_Hacking) && (bHasRTM ? (TheGameMenuManager->IsLiveMenu(Menu::kMenuType_Hacking, false, false) == GameMenuManager::MENU_PAUSED) : true);
	bool bLockpickMenu = LockPickMenu::GetSingleton() && (bHasRTM ? (TheGameMenuManager->IsLiveMenu(Menu::kMenuType_LockPick, false, false) == GameMenuManager::MENU_PAUSED) : true);

	GameState.OverlayIsOn = bComputersMenu || bLockpickMenu ||
		InterfaceManager->IsActive(Menu::kMenuType_Surgery) ||
		InterfaceManager->IsActive(Menu::kMenuType_SlotMachine) ||
		InterfaceManager->IsActive(Menu::kMenuType_Blackjack) ||
		InterfaceManager->IsActive(Menu::kMenuType_Roulette) ||
		InterfaceManager->IsActive(Menu::kMenuType_Caravan);
	GameState.isDialog = InterfaceManager->IsActive(Menu::MenuType::kMenuType_Dialog);
	GameState.isPersuasion = InterfaceManager->IsActive(Menu::MenuType::kMenuType_Persuasion);
	
	if (!currentCell) return; // if no cell is present, we skip update to avoid trying to access values that don't exist
	
	GameState.isExterior = !currentCell->IsInterior();// || Player->parentCell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior; // < use exterior flag, broken for now
	static unsigned int cellFrame = ~0u;
	static bool cellChangedThisFrame = false;
	const bool firstCallOfFrame = TheFrameRateManager->FrameNumber != cellFrame;
	if (firstCallOfFrame) {
		cellFrame = TheFrameRateManager->FrameNumber;
		cellChangedThisFrame = currentCell != PreviousCell;
		PreviousCell = currentCell;
	}
	GameState.isCellChanged = cellChangedThisFrame;
	if (firstCallOfFrame && GameState.isCellChanged) {
		TheSettingManager->SettingsChanged = true; // force update constants during cell transition
		// Preset resolve+apply as early in this function as possible -- before
		// anything below reads SettingsMain, so nothing this frame observes a
		// stale value (docs/preset-manager-design.md § "Application mechanism").
		//
		// Gated on the master toggle and on ShouldReResolve, not on isCellChanged
		// alone: Override presets are assigned per-worldspace outdoors (no
		// per-cell keyword tier exists for exteriors), so re-resolving on every
		// exterior cell border within the same worldspace was a no-op at best
		// and, at worst, silently clobbered any live tweak that hadn't been
		// saved yet the moment the player happened to cross one. ShouldReResolve
		// keeps interiors at today's per-cell granularity and always re-resolves
		// across an interior<->exterior boundary. The short-circuit order
		// matters: ShouldReResolve has side effects (updates its cached "last
		// resolved identity"), so it must not run while the master toggle is
		// off, or re-enabling it later would see a stale-but-matching cache and
		// skip the immediate resolve it needs to do.
		if (TheSettingManager->SettingsMain.Main.PresetManagerEnabled && PresetManager::ShouldReResolve(currentCell))
			PresetManager::ResolveAndApply(currentCell, true);
	}

	GameState.isUnderwater = Tes->sky->GetIsUnderWater();
	GameState.isRainy = currentWeather?currentWeather->GetWeatherType() == TESWeather::WeatherType::kType_Rainy : false;
	GameState.isSnow = currentWeather?currentWeather->GetWeatherType() == TESWeather::WeatherType::kType_Snow : false;
	if (!GameState.isSnow && TheSettingManager->GetSettingI("Shaders.Snow.Main", "ForceSnow")) { GameState.isSnow = true; GameState.isRainy = false; }
	GameState.isCloudy = currentWeather?currentWeather->GetWeatherType() == TESWeather::WeatherType::kType_Cloudy : false;

	TimeGlobals* GameTimeGlobals = TimeGlobals::Get();
	float GameHour = fmod(GameTimeGlobals->GameHour->data, 24); // make sure the hours values are less than 24

	float SunriseStart = WorldSky->GetSunriseBegin();
	float SunriseEnd = WorldSky->GetSunriseEnd();
	float SunsetStart = WorldSky->GetSunsetBegin();
	float SunsetEnd = WorldSky->GetSunsetEnd();

	// calculating sun amount for shaders (currently not used by any shaders)
	float sunRise = step(SunriseStart, SunriseEnd, GameHour); // 0 at night to 1 after sunrise
	float sunSet = step(SunsetEnd, SunsetStart, GameHour);  // 1 before sunset to 0 at night
	GameState.isDayTime = sunRise * sunSet;

	ShaderConst.SunTiming.x = WorldSky->GetSunriseColorBegin();
	ShaderConst.SunTiming.y = SunriseEnd;
	ShaderConst.SunTiming.z = SunsetStart;
	ShaderConst.SunTiming.w = WorldSky->GetSunsetColorEnd();

	// fake sunset time tracking with more time given before sunrise/sunset
	float sunRiseLight = step(SunriseStart - 1.0f, SunriseEnd - 1.0f, GameHour); // 0 at night to 1 after sunrise
	float sunSetLight = step(SunsetEnd + 1.0f, SunsetStart + 1.0f, GameHour);  // 1 before sunset to 0 at night
	float newDayLight = sunRiseLight * sunSetLight;
	float transitionPower = max(0.01f, TheSettingManager->SettingsMain.Transitions.TransitionCurvePower);
	GameState.transitionCurve = pow(smoothStep(0.0f, 1.0f, newDayLight), transitionPower); // a curve for day/night transitions that occurs mostly during second half of sunset

	GameState.isDayTimeChanged = (newDayLight != GameState.dayLight);  // allow effects to fire settings update during sunset/sunrise transitions
	GameState.dayLight = newDayLight;

	ShaderConst.GameTime.x = TimeGlobals::GetGameTime(); //time in milliseconds
	ShaderConst.GameTime.y = GameHour; //time in hours
	ShaderConst.GameTime.z = (float)TheFrameRateManager->Time;
	ShaderConst.GameTime.w = TheFrameRateManager->ElapsedTime; // frameTime in seconds

	ShaderConst.SunPosition = SunRoot->m_localTransform.pos.toD3DXVEC4();
	ShaderConst.SunPosition.w = 0.0f;
	// SunRoot (the sun disc's NiNode) is only positioned by the game's own sky-dome rendering,
	// which only runs while an exterior sky is actually drawn. On a fresh process load straight
	// into an interior save, before the player has ever seen an exterior sky this session, it
	// sits at its post-load default -- a zero vector -- and D3DXVec4Normalize of a zero-length
	// vector divides by zero, producing NaN. That NaN then poisons every downstream consumer of
	// SunPosition, notably EvalSky's per-sample radiance in SkyShaders::UpdateConstants (Sky.cpp)
	// -- every one of its 512 integration samples comes out NaN, so all 9 SH sky-irradiance
	// coefficients do too, and PBR Skylighting goes dark (indoors AND out) until the player
	// visits an exterior cell once and the sun disc gets a real position, matching the "only
	// works after having been outdoors" symptom exactly -- and matches SkyDebug's logged
	// Irradiance[0]=(nan,nan,nan) on a cold interior load.
	if (D3DXVec4LengthSq(&ShaderConst.SunPosition) > 0.0001f)
		D3DXVec4Normalize(&ShaderConst.SunPosition, &ShaderConst.SunPosition);
	else
		ShaderConst.SunPosition = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f); // straight up: a safe, neutral default
	ShaderConst.SunPosition.w = 1.0f;

	ShaderConst.SunDir = Tes->directionalLight->direction.toD3DXVEC4() * -1.0f;
	ShaderConst.SunDir.w = 0.0f;
	D3DXVec4Normalize(&ShaderConst.SunDir, &ShaderConst.SunDir);
	ShaderConst.SunDir.w = 1.0f;

	// during the day, track the sun mesh position instead of the lighting direction in exteriors
	if (GameState.isExterior && GameState.dayLight > 0.5)
		ShaderConst.SunDir = ShaderConst.SunPosition;
	else
		ShaderConst.SunPosition.z = -ShaderConst.SunPosition.z;


	// expose the light vector in view space for screen space lighting
	D3DXVec4Transform(&ShaderConst.ScreenSpaceLightDir, &ShaderConst.SunDir, &TheRenderManager->ViewProjMatrix);
	D3DXVec4Normalize(&ShaderConst.ScreenSpaceLightDir, &ShaderConst.ScreenSpaceLightDir);

	D3DXVec4Transform(&ShaderConst.ViewSpaceLightDir, &ShaderConst.SunDir, &TheRenderManager->ViewMatrix);
	D3DXVec4Normalize(&ShaderConst.ViewSpaceLightDir, &ShaderConst.ViewSpaceLightDir);

	ShaderConst.sunGlare = currentWeather ? (currentWeather->GetSunGlare() / 255.0f) : 0.5f;

	ShaderConst.SunAmount.y = GameState.isDayTime; // accurate 0 - 1 value based on weather transition times
	GameState.isDayTime = smoothStep(0, 1, GameState.dayLight); // smooth daytime progression -- more accurate to light changes
	ShaderConst.SunAmount.x = GameState.isDayTime;

	ShaderConst.sunColor.x = WorldSky->sunDirectional.r;
	ShaderConst.sunColor.y = WorldSky->sunDirectional.g;
	ShaderConst.sunColor.z = WorldSky->sunDirectional.b;
	ShaderConst.sunColor.w = ShaderConst.sunGlare;

	if (Shaders.Sky->useSunDiskColor) {
		// experimental color used for more sky tinting capabilities
		ShaderConst.sunDiskColor.x = WorldSky->SunColor.r;
		ShaderConst.sunDiskColor.y = WorldSky->SunColor.g;
		ShaderConst.sunDiskColor.z = WorldSky->SunColor.b;
		ShaderConst.sunDiskColor.w = 1.0;
	}
	else {
		ShaderConst.sunDiskColor = ShaderConst.sunColor; // override with the color of the lighting
	}

	ShaderConst.windSpeed = WorldSky->windSpeed;

	ShaderConst.fogColor.x = WorldSky->fogColor.r;
	ShaderConst.fogColor.y = WorldSky->fogColor.g;
	ShaderConst.fogColor.z = WorldSky->fogColor.b;
	ShaderConst.fogColor.w = 1.0f;

	ShaderConst.horizonColor.x = WorldSky->Horizon.r;
	ShaderConst.horizonColor.y = WorldSky->Horizon.g;
	ShaderConst.horizonColor.z = WorldSky->Horizon.b;
	ShaderConst.horizonColor.w = 1.0f;

	ShaderConst.sunAmbient.x = WorldSky->sunAmbient.r;
	ShaderConst.sunAmbient.y = WorldSky->sunAmbient.g;
	ShaderConst.sunAmbient.z = WorldSky->sunAmbient.b;
	ShaderConst.sunAmbient.w = 1.0f;

	ShaderConst.skyLowColor.x = WorldSky->SkyLower.r;
	ShaderConst.skyLowColor.y = WorldSky->SkyLower.g;
	ShaderConst.skyLowColor.z = WorldSky->SkyLower.b;
	ShaderConst.skyLowColor.w = 1.0f;

	ShaderConst.skyColor.x = WorldSky->skyUpper.r;
	ShaderConst.skyColor.y = WorldSky->skyUpper.g;
	ShaderConst.skyColor.z = WorldSky->skyUpper.b;
	ShaderConst.skyColor.w = 1.0f;

	// replicate vanilla behavior of enforcing max fog distance in interiors
	ShaderConst.fogData.y = WorldSky->fogFarPlane;
	if (!GameState.isExterior && (WorldSky->fogFarPlane < 0 || WorldSky->fogFarPlane > 163840)) {
		ShaderConst.fogData.y = 163840;
	}

	// for near plane, ensure that far > near
	ShaderConst.fogData.x = WorldSky->fogNearPlane;
	if (WorldSky->fogNearPlane < 0 || ShaderConst.fogData.y < WorldSky->fogNearPlane)
		ShaderConst.fogData.x = ShaderConst.fogData.y * 0.17;

	ShaderConst.fogData.z = ShaderConst.sunGlare;
	ShaderConst.fogData.w = WorldSky->fogPower;

	ShaderConst.fogDistance.x = ShaderConst.fogData.x;
	ShaderConst.fogDistance.y = ShaderConst.fogData.y;
	ShaderConst.fogDistance.z = 1.0f;
	ShaderConst.fogDistance.w = ShaderConst.sunGlare;

	timer.LogTime("ShaderManager::UpdateConstants for generic constants");

	if (TheSettingManager->SettingsChanged) {
		// TheGameMenuManager->UpdateSettings(); — replaced by ImGui overlay

		// update settings
		for (const auto [Name, effect] : EffectsNames) {
			(*effect)->UpdateSettings();
		}
		for (const auto [Name, shader] : ShaderNames) {
			(*shader)->UpdateSettings();
		}

		// sky settings are used in several shaders whether the shader is active or not
		ShaderConst.SunAmount.w = TheSettingManager->GetSettingF("Shaders.Sky.Main", "GlareStrength");
		timer.LogTime("ShaderManager::UpdateSettings for shaders & effects");
	}

	// update Constants
	for (const auto [Name, shader] : ShaderNames) {
		if ((*shader)->Enabled) (*shader)->UpdateConstants();
	}
	timer.LogTime("ShaderManager::UpdateConstants for shaders");

	for (const auto [Name, effect] : EffectsNames) {
		if ((*effect)->Enabled) {
			(*effect)->UpdateConstants();
			(*effect)->constantUpdateTime = timer.LogTime((*effect)->Name);
		}
		else {
			(*effect)->constantUpdateTime = 0;
		}
	}
	if (!Effects.AmbientOcclusion->Enabled) Effects.AmbientOcclusion->UpdateSkyConstants();

	// The skin, hair and grass shaders route their light and ambient terms through
	// TESR_PBRData (see Shaders/Includes/PBRScale.hlsl) and render black at a zero scale, so
	// these constants stay current whether or not the PBR collection is enabled.
	if (!Shaders.PBR->Enabled) Shaders.PBR->UpdateConstants();

	if (!Effects.Sandstorm->Enabled || !Effects.Sandstorm->Effect) Effects.Sandstorm->ClearWind();

	// Underwater effect uses constants from the water shader
	if (Effects.Underwater->Enabled && !Shaders.Water->Enabled) Shaders.Water->UpdateConstants();
	if (!Effects.ShadowsExteriors->Enabled && Effects.ShadowsInteriors->Enabled) Effects.ShadowsExteriors->UpdateConstants(); // Interior and exterior shadows share settings

	TheSettingManager->SettingsChanged = false;
	timer.LogTime("ShaderManager::UpdateConstants");
}


float ShaderManager::GetTransitionValue(float Day, float Night, float Interior) {
	if (GameState.isExterior) {
		return std::lerp(Night, Day, GameState.transitionCurve);
	}
	else {
		return Interior;
	}
}


ShaderCollection* ShaderManager::GetShaderCollection(const char* Name) {

	if (!memcmp(Name, "WATER", 5)) return Shaders.Water;
	if (!memcmp(Name, "GRASS", 5)) return Shaders.Grass;
	if (!memcmp(Name, "ISHDR", 5) || !memcmp(Name, "HDR", 3)) return Shaders.Tonemapping; // tonemapping shaders have different names between New vegas and Oblivion
	if (!memcmp(Name, "PAR", 3)) return Shaders.POM;
	if (!memcmp(Name, "SKIN", 4)) return Shaders.Skin;
	// Hair (BSSM_3XLIGHTING_*) lives in the SM3 family, not HAIR*. Only SM3003 has a
	// replacement on disk; the rest resolve to no file and fall through to vanilla.
	if (!memcmp(Name, "SM3", 3)) return Shaders.PBR;
	// SpeedTree leaves. STLEAF001/003.vso are vs_3_0 replacements, so every leaf PS must have
	// a ps_3_0 replacement too: D3D9 rejects a 2.x VS paired with a 3.0 PS.
	if (!memcmp(Name, "STLEAF", 6)) return Shaders.PBR;
	if (!memcmp(Name, "SKY", 3)) return Shaders.Sky;
	if (!memcmp(Name, "NOLIGHT", 7) || !memcmp(Name, "GDECAL", 6)) return Shaders.Particles;
	if (strstr(BloodShaders, Name)) return Shaders.Blood;

	if (Shaders.PBR->GetTemplate(Name).Name != NULL) return Shaders.PBR;
	if (Shaders.Terrain->GetTemplate(Name).Name != NULL) return Shaders.Terrain;

	return NULL;
}

/*
* Reload all effects.
*/
void ShaderManager::ReloadEffects() {
	for (const auto [Name, effect] : EffectsNames) {
		(*effect)->DisposeEffect();
		(*effect)->LoadEffect();
	}
}

/*
* Load generic Vertex Shaders as well as the ones for interiors and exteriors if the exist. 
* Returns false if generic one isn't found (as other ones are optional)
*/
bool ShaderManager::LoadShader(NiD3DVertexShader* Shader) {
	
	NiD3DVertexShaderEx* VertexShader = (NiD3DVertexShaderEx*)Shader;
	ShaderCollection* Collection = GetShaderCollection(VertexShader->Name);

	if (!Collection) {
		VertexShader->ShaderProg[ShaderRecordType::Default] = NULL;
		VertexShader->ShaderProg[ShaderRecordType::Exterior] = NULL;
		VertexShader->ShaderProg[ShaderRecordType::Interior] = NULL;
		VertexShader->Enabled = false;
		return false;
	}
	
	bool enabled = Collection->Enabled;

	ShaderTemplate Template = Collection->GetTemplate(VertexShader->Name);

	// Load generic, interior and exterior shaders
	VertexShader->ShaderProg[ShaderRecordType::Default]  = (ShaderRecordVertex*)ShaderRecord::LoadShader(VertexShader->Name, NULL, Template);
	VertexShader->ShaderProg[ShaderRecordType::Exterior] = (ShaderRecordVertex*)ShaderRecord::LoadShader(VertexShader->Name, "Exteriors\\", Template);
	VertexShader->ShaderProg[ShaderRecordType::Interior] = (ShaderRecordVertex*)ShaderRecord::LoadShader(VertexShader->Name, "Interiors\\", Template);
	VertexShader->Enabled = enabled;

	if (VertexShader->ShaderProg[ShaderRecordType::Default] != nullptr || VertexShader->ShaderProg[ShaderRecordType::Exterior] != nullptr || VertexShader->ShaderProg[ShaderRecordType::Interior] != nullptr) {
		Collection->VertexShaderList.push_back(VertexShader);
		Logger::Log("Loaded %s Vertex Shader %s", Collection->Name, VertexShader->Name);
	}

	return enabled;
}


/*
* Load generic Pixel Shaders as well as the ones for interiors and exteriors if the exist. 
* Returns false if generic one isn't found (as other ones are optional)
*/
bool ShaderManager::LoadShader(NiD3DPixelShader* Shader) {

	NiD3DPixelShaderEx* PixelShader = (NiD3DPixelShaderEx*)Shader;
	ShaderCollection* Collection = GetShaderCollection(PixelShader->Name);

	if (!Collection) {
		PixelShader->ShaderProg[ShaderRecordType::Default] = NULL;
		PixelShader->ShaderProg[ShaderRecordType::Exterior] = NULL;
		PixelShader->ShaderProg[ShaderRecordType::Interior] = NULL;
		PixelShader->Enabled = false;
		return false;
	}

	bool enabled = Collection->Enabled;

	ShaderTemplate Template = Collection->GetTemplate(PixelShader->Name);

	PixelShader->ShaderProg[ShaderRecordType::Default]  = (ShaderRecordPixel*)ShaderRecord::LoadShader(PixelShader->Name, NULL, Template);
	PixelShader->ShaderProg[ShaderRecordType::Exterior] = (ShaderRecordPixel*)ShaderRecord::LoadShader(PixelShader->Name, "Exteriors\\", Template);
	PixelShader->ShaderProg[ShaderRecordType::Interior] = (ShaderRecordPixel*)ShaderRecord::LoadShader(PixelShader->Name, "Interiors\\", Template);
	if (!PixelShader->ShaderProg[ShaderRecordType::Interior] && PixelShader->ShaderProg[ShaderRecordType::Default] &&
		PointShadowForward::Eligible(PixelShader->Name, Template) &&
		TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "Enabled")) {
		static D3DXMACRO overrides[4][7] = {
			{ { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" }, { "CONTACT_HARDENING", "0" }, { NULL, NULL } },
			{ { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" }, { "CONTACT_HARDENING", "0" }, { "POINT_SHADOW_FIT", "1" }, { NULL, NULL } },
			{ { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" }, { "CONTACT_HARDENING", "0" }, { "POINT_SHADOW_FIT", "2" }, { NULL, NULL } },
			{ { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" }, { "CONTACT_HARDENING", "0" }, { "POINT_SHADOW_FIT", "3" }, { NULL, NULL } } };
		static const bool lampFalloffOverride = [] {
			const char* value = PBRShaders::LampFalloffInteriorDefine();
			if (value) for (auto& level : overrides) for (auto& d : level) if (!d.Name) { d = { "LAMP_FALLOFF_ON", value }; break; }
			return value != nullptr;
		}();
		(void)lampFalloffOverride;
		static const char* const cache[4] = { "Interiors\\", "Interiors\\Fit1\\", "Interiors\\Fit2\\", "Interiors\\Fit3\\" };
		static const char* const what[4] = { "", "the High/Ultra taps in a loop (the same shadows)",
			"no High/Ultra: lamp shadow Quality is held at Medium for every object", "also no blocker search: contact hardening and LampSize are off for every object's lamp shadows" };
		ShaderRecordPixel* record = nullptr;
		int level = 0;
		for (; level < 4 && !record; level++) {
			if (level) Logger::Log("UNOFFICIAL lamp shadows: %s with lamp shadows did not load; trying a shorter version (%d of 3: %s).", PixelShader->Name, level, what[level]);
			record = (ShaderRecordPixel*)ShaderRecord::LoadShader(PixelShader->Name, NULL, Template, cache[level], overrides[level]);
		}
		if (record) {
			if (--level) {
				Logger::Log("UNOFFICIAL lamp shadows: %s with lamp shadows loaded at the shorter version %d (%s).", PixelShader->Name, level, what[level]);
				if (level > PointShadowForward::FitLevel) PointShadowForward::FitLevel = level;
			}
			record->PointShadowForward = true;
			PixelShader->ShaderProg[ShaderRecordType::Interior] = record;
			PointShadowForward::CompiledIn = true;
			PointShadowForward::CompiledCount++;
		}
		else {
			Logger::Log("[ERROR] UNOFFICIAL lamp shadows: no version of %s with lamp shadows loads on this GPU. Forward lamp shadows are off: every object "
				"uses the screen-space lamp shadows, as with [Shaders.ShadowsInteriors.Forward] Enabled off (the objects drawn with it would otherwise lose "
				"their lamp shadows whenever enough lamps reach them).", PixelShader->Name);
			PointShadowForward::Refused = true;
		}
	}
	PixelShader->Enabled = enabled;

	if (PixelShader->ShaderProg[ShaderRecordType::Default] != nullptr || PixelShader->ShaderProg[ShaderRecordType::Exterior] != nullptr || PixelShader->ShaderProg[ShaderRecordType::Interior] != nullptr) {
		Collection->PixelShaderList.push_back(PixelShader);
		Logger::Log("Loaded %s Pixel Shader %s", Collection->Name, PixelShader->Name);
	}

	return enabled;
}


void ShaderManager::GetNearbyLights(ShadowSceneLight* ShadowLightsList[], NiPointLight* LightsList[], NiSpotLight* SpotLightList[]) {
	D3DXVECTOR4 PlayerPosition = Player->pos.toD3DXVEC4();
	//Logger::Log(" ==== Getting lights ====");
	auto timer = TimeLogger();

	// create a map of all nearby valid lights and sort them per distance to player
	std::multimap<float, ShadowSceneLight*> SceneLights;
	NiTList<ShadowSceneLight>::Entry* Entry = SceneNode->lights.start;

	ShadowsExteriorEffect::InteriorsStruct* Settings = &Effects.ShadowsExteriors->Settings.Interiors;
	ShadowsExteriorEffect::ShadowStruct* ShadowsConstants = &Effects.ShadowsExteriors->Constants;

	static const void* previousSlots[ShadowCubeMapsMax] = {};
	const float switchMargin = Settings->Forward.Enabled ? Settings->Forward.LampSwitchMargin : 0.0f;
	auto wasCaster = [&](const ShadowSceneLight* light) {
		if (!(switchMargin > 0.0f)) return false;
		for (int s = 0; s < ShadowCubeMapsMax; s++) if (previousSlots[s] == light) return true;
		return false;
	};

	const bool viewTest = Settings->Forward.Enabled;
	const bool cameraIndependent = viewTest && Settings->Forward.CameraIndependent;
	const float fillLightRadius = viewTest ? Settings->Forward.FillLightRadius : 0.0f;
	const D3DXMATRIX& camera = TheRenderManager->InvViewMatrix;
	const D3DXVECTOR3 camRight(camera._11, camera._12, camera._13), camUp(camera._21, camera._22, camera._23), camForward(camera._31, camera._32, camera._33);
	const float tanX = fabsf(TheRenderManager->InvProjMatrix._11), tanY = fabsf(TheRenderManager->InvProjMatrix._22);
	const float normX = 1.0f / sqrtf(1.0f + tanX * tanX), normY = 1.0f / sqrtf(1.0f + tanY * tanY);
	const int lampRanking = viewTest ? Settings->Forward.LampRanking : 0;
	const bool logLamps = Settings->Forward.LogLamps;
	struct LampNote { const ShadowSceneLight* light; int reason; float distance, key, radius; D3DXVECTOR4 position, colour; int carry; };
	static LampNote notes[128];
	int noteCount = 0;
	auto note = [&](const ShadowSceneLight* light, int reason, float distance, float key, NiPointLight* source) {
		if (!logLamps || noteCount >= 128 || !source) return;
		notes[noteCount++] = { light, reason, distance, key, source->Spec.r, source->m_worldTransform.pos.toD3DXVEC4(),
			D3DXVECTOR4(source->Diff.r, source->Diff.g, source->Diff.b, source->Dimmer), (int)source->CanCarry };
	};
	const bool indoorsForward = viewTest && !GameState.isExterior;
	const float invisibleStrength = indoorsForward ? Settings->Forward.InvisibleLightShadowStrength : 1.0f;
	const bool classifyLamps = indoorsForward && (invisibleStrength < 1.0f || Settings->Forward.InvisibleLightSize > 0.0f ||
		!Settings->Forward.LampModelShadows || Settings->Forward.InvisibleShadowFromLamp > 0.0f || logLamps);
	if (classifyLamps) LampFixtures::Update(Player->parentCell, logLamps);
	LampFixtures::LogModels = logLamps;

	auto reachesView = [&](const D3DXVECTOR4& lightPosition, float reach) {
		const D3DXVECTOR3 v(lightPosition.x - TheRenderManager->CameraPosition.x, lightPosition.y - TheRenderManager->CameraPosition.y,
			lightPosition.z - TheRenderManager->CameraPosition.z);
		const float x = D3DXVec3Dot(&v, &camRight), y = D3DXVec3Dot(&v, &camUp), z = D3DXVec3Dot(&v, &camForward);
		if (z < -reach) return false;
		if ((tanX * z - x) * normX < -reach || (tanX * z + x) * normX < -reach) return false;
		if ((tanY * z - y) * normY < -reach || (tanY * z + y) * normY < -reach) return false;
		return true;
	};

	// Creating list of lights in order of distance to the player
	while (Entry) {
		if (!Entry->data || !Entry->data->sourceLight) { Entry = Entry->next; continue; }
		NiPointLight* Light = Entry->data->sourceLight;
		D3DXVECTOR4 LightPosition = Light->m_worldTransform.pos.toD3DXVEC4();

		bool lightCulled = Light->m_flags & NiAVObject::NiFlags::APP_CULLED;
		bool lightOn = (Light->Diff.r + Light->Diff.g + Light->Diff.b) * Light->Dimmer > 5.0 / 255.0; // Check for low values in case of human error
		if (lightCulled || !lightOn) {
			note(Entry->data, lightCulled ? 1 : 2, Light->GetDistance(&Player->pos), 0.0f, Light);
			Entry = Entry->next;
			continue;
		}

		D3DXVECTOR4 LightVector = LightPosition - PlayerPosition;
		D3DXVec4Normalize(&LightVector, &LightVector);
		const bool incumbent = wasCaster(Entry->data);
		bool inFront = cameraIndependent ||
			(viewTest ? reachesView(LightPosition, Light->Spec.r * Settings->LightRadiusMult * (incumbent ? 1.0f + switchMargin : 1.0f))
			: D3DXVec4Dot(&LightVector, &TheRenderManager->CameraForward) > (incumbent ? -0.5f : 0.0f));
		float Distance = Light->GetDistance(&Player->pos);
		float radius = Light->Spec.r * Settings->LightRadiusMult;

		// select lights that will be tracked by removing culled lights and lights behind the player further away than their radius
		// TODO: handle using frustum check
		float drawDistance = 8000;//TheShaderManager->GameState.isExterior ? TheSettingManager->SettingsShadows.Exteriors.ShadowMapRadius[TheShadowManager->ShadowMapTypeEnum::MapLod] : TheSettingManager->SettingsShadows.Interiors.DrawDistance;
		if ((inFront || Distance < radius) && (Distance + radius) < drawDistance) {
			float key = Distance * (incumbent ? 1.0f - switchMargin : 1.0f);
			if (lampRanking) {
				const float brightness = (std::max)(0.0f, (Light->Diff.r + Light->Diff.g + Light->Diff.b) / 3.0f * Light->Dimmer);
				key = -brightness * Light->Spec.r / (std::max)(Distance, 0.25f * Light->Spec.r + 1.0f) * (incumbent ? 1.0f + switchMargin : 1.0f);
			}
			SceneLights.insert({ key, Entry->data });
			note(Entry->data, 0, Distance, key, Light);
		}
		else note(Entry->data, inFront ? 4 : 3, Distance, 0.0f, Light);

		Entry = Entry->next;
	}

	// save only the n first lights (based on #define TrackedLightsMax)
	memset(&TheShaderManager->LightPosition, 0, TrackedLightsMax * sizeof(D3DXVECTOR4)); // clear previous lights from array
	// Must be cleared. The fill loop below only zeroes trailing slots once it runs out of scene
	// lights; with more lights than slots it never reaches that branch, and a slot left holding
	// last frame's position keeps GetPointLightAmount sampling a cubemap nobody redraws.
	memset(&ShadowsConstants->ShadowLightPosition, 0, ShadowCubeMapsScreen * sizeof(D3DXVECTOR4));
	memset(&TheShaderManager->LightColor, 0, (TrackedLightsMax + ShadowCubeMapsScreen) * sizeof(D3DXVECTOR4)); // clear previous lights from array
	memset(ExtraLights, 0, sizeof(ExtraLights));

	// ShadowManager::RenderShadowMaps only renders cubemaps for the first LightPoints slots.
	// Filling past that gives the shader a live position and colour for a face that is never
	// redrawn, so it samples whatever that cubemap last held -- a shadow frozen from an earlier
	// frame or cell. Lights beyond the cap fall through to the non-shadowing tracked list.
	const bool allSlots = PointShadowForward::UsesAllSlots(TheShaderManager->GameState.isExterior);
	const bool batched = PointShadowForward::ExteriorBatched(TheShaderManager->GameState.isExterior);
	const int ShadowLightsMax = batched ? PointShadowForward::ShadowedSlots(true) : min(Settings->LightPoints, allSlots ? (int)ShadowCubeMapsMax : (int)ShadowCubeMapsScreen);
	memset(BatchLightPosition, 0, sizeof(BatchLightPosition));
	memset(BatchLightColor, 0, sizeof(BatchLightColor));

	// get the data for all tracked lights
	int ShadowIndex = 0;
	int LightIndex = 0;
	TheShadowManager->PointLightsNum = 0;
	ShadowSceneLight* ShadowCasters[ShadowCubeMapsMax] = { NULL };

#if defined(OBLIVION)
	bool TorchOnBeltEnabled = TheSettingManager->SettingsMain.EquipmentMode.Enabled && TheSettingManager->SettingsMain.EquipmentMode.TorchKey != 255;
#endif

	D3DXVECTOR4 Empty = D3DXVECTOR4(0, 0, 0, 0);

	// TEMP : get data for spotlights. Right now, is done manually since spotlights aren't implemented in the engine
	// FlashlightEffect::UpdateConstants publishes SpotLightPosition/Direction/Color itself,
	// so the cone constants and the cookie matrix always come from one read of the bone.
	TheShaderManager->Effects.Flashlight->UpdateConstants();
	if (TheShaderManager->Effects.Flashlight->Enabled && TheShaderManager->Effects.Flashlight->spotLightActive) {
		SpotLightList[0] = TheShaderManager->Effects.Flashlight->SpotLight;
	}
	else {
		SpotLightList[0] = nullptr;
	}

	auto v = SceneLights.begin();
	for (int i = 0; i < TrackedLightsMax + ShadowCubeMapsMax; i++) {
		// set null values if we reached the end of lights in the scene and current index is lower than max amount
		if (v == SceneLights.end()) {
			if (LightIndex < TrackedLightsMax) {
				//Logger::Log("clearing light at index %i", LightIndex);
				LightsList[LightIndex] = NULL;
				LightPosition[LightIndex] = Empty;
				LightColor[ShadowCubeMapsScreen + LightIndex] = Empty;
				LightIndex++;
			}

			continue;
		}

		NiPointLight* Light = v->second->sourceLight;
		if (!Light) {
			v++;
			continue;
		}

		if (Light->EffectType == NiDynamicEffect::EffectTypes::POINT_LIGHT) {
			// determin if light is a shadow caster
			//bool CastShadow = Settings->UseCastShadowFlag ? Light->CastShadows : true; // Flag is broken by JIP
			bool CastShadow = true;
			if (fillLightRadius > 0.0f && Light->Spec.r > fillLightRadius) {
				CastShadow = Settings->Forward.FillLightShadowStrength > 0.0f;
				static const void* loggedFill[64] = {};
				static int loggedFillCount = 0;
				if (logLamps && loggedFillCount < 64) {
					bool seen = false;
					for (int f = 0; f < loggedFillCount; f++) if (loggedFill[f] == v->second) seen = true;
					if (!seen) {
						loggedFill[loggedFillCount++] = v->second;
						const D3DXVECTOR4 at = Light->m_worldTransform.pos.toD3DXVEC4();
						Logger::Log("LAMP %p is a fill light (reach %.0f > FillLightRadius %.0f): it lights as before, its shadow at strength %.2f (0: none) | at %.0f %.0f %.0f, colour %.2f %.2f %.2f x %.2f",
							v->second, Light->Spec.r, fillLightRadius, Settings->Forward.FillLightShadowStrength, at.x, at.y, at.z, Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer);
					}
				}
			}

			if (classifyLamps && CastShadow) {
				const bool invisible = LampFixtures::Invisible(Light, Settings->Forward.FixtureDistance);
				if (invisible) CastShadow = invisibleStrength > 0.0f;
				static std::unordered_map<const void*, unsigned> loggedKind;
				if (logLamps && loggedKind.size() < 512) {
					auto seen = loggedKind.find(v->second);
					if (seen == loggedKind.end() || seen->second != LampFixtures::Generation) {
						loggedKind[v->second] = LampFixtures::Generation;
						const D3DXVECTOR4 at = Light->m_worldTransform.pos.toD3DXVEC4();
						Logger::Log("LAMP %p is %s: nearest glowing mesh %.0f units (GLOW %d; FixtureDistance %.0f; %u glowing meshes in the cell)%s | at %.0f %.0f %.0f, reach %.0f, colour %.2f %.2f %.2f x %.2f",
							v->second, invisible ? "an INVISIBLE light" : "at a lamp", LampFixtures::NearestGlow(Light), LampFixtures::NearestGlowIndex(Light),
							Settings->Forward.FixtureDistance, (unsigned)LampFixtures::Glows.size(),
							invisible ? (invisibleStrength > 0.0f ? ", its shadow at InvisibleLightShadowStrength" : ", no shadow") : "",
							at.x, at.y, at.z, Light->Spec.r, Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer);
					}
				}
			}

#if defined(OBLIVION)
			// Oblivion exception for carried torch lights
			if (TorchOnBeltEnabled && Light->CanCarry == 2) {
				HighProcessEx* Process = (HighProcessEx*)Player->process;
				if (Process->OnBeltState == HighProcessEx::State::In) CastShadow = false;
		}
#endif
			float radius = Light->Spec.r * Settings->LightRadiusMult;
			D3DXVECTOR4 LightPos = Light->m_worldTransform.pos.toD3DXVEC4();
			LightPos.w = radius;

			if (CastShadow && ShadowIndex < ShadowLightsMax && radius > 10) {
				ShadowCasters[ShadowIndex] = v->second;

				ShadowIndex++;
				TheShadowManager->PointLightsNum++; // Constant to track number of shadow casting lights are present
			}
			else if (LightIndex < TrackedLightsMax) {
				LightsList[LightIndex] = Light;
				LightPosition[LightIndex] = LightPos;
				LightColor[ShadowCubeMapsScreen + LightIndex] = D3DXVECTOR4(Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer);
				LightIndex++;
			};
		}
		else if (Light->EffectType == NiDynamicEffect::EffectTypes::SPOT_LIGHT) {
			// Here will go the collecting of the spotlights and setting of constants
		}
		v++;
	}

	{
		const int casters = TheShadowManager->PointLightsNum;
		const int sampledSlots = allSlots || batched ? ShadowLightsMax : min(ShadowLightsMax, (int)ShadowCubeMapsSampled);
		const int stableCasters = min(casters, sampledSlots);
		const void* ranked[ShadowCubeMapsMax] = {};
		const void* assigned[ShadowCubeMapsMax] = {};
		for (int r = 0; r < stableCasters; r++) ranked[r] = ShadowCasters[r];
		AssignStablePointShadowSlots(previousSlots, ranked, stableCasters, sampledSlots, assigned);
		static unsigned lampLogLines = 0;
		if (logLamps && lampLogLines < 600) {
			auto inSet = [](const void* const* set, int count, const void* light) { for (int s = 0; s < count; s++) if (set[s] == light) return true; return false; };
			auto find = [&](const void* light) -> const LampNote* { for (int n = 0; n < noteCount; n++) if (notes[n].light == light) return &notes[n]; return nullptr; };
			auto rankOf = [&](const void* light) { int r = 1; for (const auto& entry : SceneLights) { if (entry.second == light) return r; r++; } return 0; };
			static const char* const why[] = { "outranked", "culled by the game", "off or too dim", "its light does not reach the view", "too far (8000)" };
			for (int s = 0; s < sampledSlots && lampLogLines < 600; s++) {
				const void* lost = previousSlots[s];
				if (lost && !inSet(assigned, sampledSlots, lost)) {
					lampLogLines++;
					const LampNote* n = find(lost);
					if (!n) Logger::Log("LAMP %p lost its shadow: no longer in the game's light list.", lost);
					else Logger::Log("LAMP %p lost its shadow: %s (rank %d of %d, %d get shadows) | at %.0f %.0f %.0f radius %.0f, %.0f away, colour %.2f %.2f %.2f x %.2f, key %.3f",
						lost, why[n->reason], n->reason ? 0 : rankOf(lost), (int)SceneLights.size(), sampledSlots, n->position.x, n->position.y, n->position.z,
						n->radius, n->distance, n->colour.x, n->colour.y, n->colour.z, n->colour.w, n->key);
				}
				const void* gained = assigned[s];
				if (gained && !inSet(previousSlots, sampledSlots, gained) && lampLogLines < 600) {
					lampLogLines++;
					const LampNote* n = find(gained);
					if (n) Logger::Log("LAMP %p got a shadow (slot %d, rank %d of %d) | at %.0f %.0f %.0f radius %.0f, %.0f away, colour %.2f %.2f %.2f x %.2f, key %.3f, carry flag %d, cube radius %.0f",
						gained, s, rankOf(gained), (int)SceneLights.size(), n->position.x, n->position.y, n->position.z, n->radius, n->distance,
						n->colour.x, n->colour.y, n->colour.z, n->colour.w, n->key, n->carry, n->radius * Settings->LightRadiusMult);
				}
			}
			if (lampLogLines >= 600) Logger::Log("LAMP log: 600 lines, stopping until restart.");
		}
		if (!allSlots && !batched && casters > sampledSlots) assigned[ShadowCubeMapsScreen - 1] = ShadowCasters[sampledSlots];
		for (int s = 0; s < ShadowCubeMapsMax; s++) previousSlots[s] = s < sampledSlots ? assigned[s] : nullptr;

		for (int s = 0; s < ShadowCubeMapsMax; s++) {
			ShadowSceneLight* shadowLight = (ShadowSceneLight*)assigned[s];
			ShadowLightsList[s] = shadowLight;
			if (!shadowLight) continue;
			NiPointLight* Light = shadowLight->sourceLight;
			D3DXVECTOR4 LightPos = Light->m_worldTransform.pos.toD3DXVEC4();
			LightPos.w = Light->Spec.r * Settings->LightRadiusMult;
			BatchLightPosition[s] = LightPos;
			BatchLightColor[s] = D3DXVECTOR4(Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer);
			if (s >= ShadowCubeMapsScreen) continue;
			if (batched && s == ShadowCubeMapsScreen - 1) continue;
			ShadowsConstants->ShadowLightPosition[s] = LightPos;
			LightColor[s] = D3DXVECTOR4(Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer);
		}
		if (allSlots && sampledSlots > ShadowCubeMapsScreen) {
			NiPointLight* keepLight[TrackedLightsMax] = {};
			D3DXVECTOR4 keepPos[TrackedLightsMax], keepColour[TrackedLightsMax];
			int kept = 0, extra = 0;
			auto take = [&](NiPointLight* light, const D3DXVECTOR4& pos, const D3DXVECTOR4& colour) {
				if (kept < TrackedLightsMax) { keepLight[kept] = light; keepPos[kept] = pos; keepColour[kept] = colour; kept++; }
				else if (extra < ExtraLightsMax && pos.w >= 1.0f) {
					const float weight = (0.2126f * colour.x + 0.7152f * colour.y + 0.0722f * colour.z) * colour.w;
					ExtraLights[extra] = pos;
					ExtraLights[extra].w = floorf(pos.w) * 8.0f + std::clamp(weight, 0.0f, 7.99f);
					extra++;
				}
			};
			for (int s = ShadowCubeMapsScreen; s < sampledSlots; s++) {
				ShadowSceneLight* shadowLight = (ShadowSceneLight*)assigned[s];
				if (!shadowLight || !shadowLight->sourceLight) continue;
				NiPointLight* Light = shadowLight->sourceLight;
				D3DXVECTOR4 pos = Light->m_worldTransform.pos.toD3DXVEC4();
				pos.w = Light->Spec.r * Settings->LightRadiusMult;
				take(Light, pos, D3DXVECTOR4(Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer));
			}
			for (int t = 0; t < LightIndex; t++)
				if (LightsList[t]) take(LightsList[t], LightPosition[t], LightColor[ShadowCubeMapsScreen + t]);
			for (int t = 0; t < TrackedLightsMax; t++) {
				LightsList[t] = t < kept ? keepLight[t] : nullptr;
				LightPosition[t] = t < kept ? keepPos[t] : Empty;
				LightColor[ShadowCubeMapsScreen + t] = t < kept ? keepColour[t] : Empty;
			}
		}
	}

	timer.LogTime("ShaderManager::GetNearbyLights");
}


void ShaderManager::RenderPointShadowBatches() {
	ShadowsExteriorEffect* shadows = Effects.ShadowsExteriors;
	const int slots = PointShadowForward::ShadowedSlots(true);
	const int batchSize = ShadowCubeMapsSampled;
	if (slots <= batchSize || !Effects.PointShadows->Effect || Effects.PointShadows->Effect->GetTechnique(1) == NULL) return;
	IDirect3DCubeTexture9* savedCube[ShadowCubeMapsScreen];
	D3DXVECTOR4 savedPosition[ShadowCubeMapsScreen], savedColour[ShadowCubeMapsScreen];
	for (int k = 0; k < ShadowCubeMapsScreen; k++) {
		savedCube[k] = shadows->Textures.ShadowCubeMapTexture[k];
		savedPosition[k] = shadows->Constants.ShadowLightPosition[k];
		savedColour[k] = LightColor[k];
	}
	int drawn = 0;
	for (int first = batchSize; first < slots; first += batchSize) {
		bool any = false;
		for (int k = 0; k < ShadowCubeMapsScreen; k++) {
			const int s = first + k;
			IDirect3DCubeTexture9* cube = k < batchSize && s < slots ? shadows->Textures.ShadowCubeMapTexture[s] : nullptr;
			const bool live = cube && BatchLightPosition[s].w > 0.0f;
			shadows->Textures.ShadowCubeMapTexture[k] = live ? cube : savedCube[k];
			shadows->Constants.ShadowLightPosition[k] = live ? BatchLightPosition[s] : D3DXVECTOR4(0, 0, 0, 0);
			LightColor[k] = live ? BatchLightColor[s] : D3DXVECTOR4(0, 0, 0, 0);
			any |= live;
		}
		if (any) { RenderEffectToRT(shadows->Textures.ShadowPassSurface, Effects.PointShadows, false, 1); drawn++; }
	}
	int live = 0;
	for (int s = 0; s < slots; s++) live += BatchLightPosition[s].w > 0.0f;
	static int loggedLive = -1, loggedDrawn = -1;
	if (live != loggedLive || drawn != loggedDrawn) {
		Logger::Log("UNOFFICIAL ExteriorLightPoints: %d lamps with shadows (of %d slots), %d further batch(es) drawn", live, slots, drawn);
		loggedLive = live; loggedDrawn = drawn;
	}
	for (int k = 0; k < ShadowCubeMapsScreen; k++) {
		shadows->Textures.ShadowCubeMapTexture[k] = savedCube[k];
		shadows->Constants.ShadowLightPosition[k] = savedPosition[k];
		LightColor[k] = savedColour[k];
	}
}

bool ShaderManager::ShouldRenderShadowMaps() {
	if (GameState.isExterior)
		return orthoRequired || orthoSkyRequired || (
			Effects.ShadowsExteriors->Settings.Exteriors.Enabled &&
			Effects.ShadowsExteriors->Enabled);
	else
		return Effects.ShadowsInteriors->Enabled;
}

/*
* Renders a given effect to an arbitrary render target
*/
float LampContactStrength();
bool LampMaskHalfRes();
IDirect3DTexture9* LampMaskHalfTexture();
IDirect3DSurface9* LampMaskHalfSurface();

static int LampMaskRect(RECT& rect, int& lampCount) {
	lampCount = 0;
	if (TheShaderManager->ExtraLights[0].w != 0.0f) return 0;
	D3DXVECTOR4 lamps[ShadowCubeMapsScreen + TrackedLightsMax + 1];
	for (int i = 0; i < ShadowCubeMapsScreen; i++)
		if (TheShaderManager->Effects.ShadowsExteriors->Constants.ShadowLightPosition[i].w != 0.0f) lamps[lampCount++] = TheShaderManager->Effects.ShadowsExteriors->Constants.ShadowLightPosition[i];
	for (int i = 0; i < TrackedLightsMax; i++)
		if (TheShaderManager->LightPosition[i].w != 0.0f) lamps[lampCount++] = TheShaderManager->LightPosition[i];
	if (TheShaderManager->SpotLightPosition[0].w != 0.0f) lamps[lampCount++] = TheShaderManager->SpotLightPosition[0];
	if (lampCount == 0) return 1;
	const D3DXMATRIX& V = TheRenderManager->viewMatrix;
	const D3DXMATRIX& P = TheRenderManager->projMatrix;
	const D3DXVECTOR3 R(V._11, V._21, V._31), U(V._12, V._22, V._32), F(V._13, V._23, V._33);
	if (fabsf(D3DXVec3Length(&F) - 1.0f) > 1e-3f || fabsf(D3DXVec3Length(&R) - 1.0f) > 1e-3f || fabsf(D3DXVec3Length(&U) - 1.0f) > 1e-3f ||
		fabsf(D3DXVec3Dot(&F, &R)) > 1e-3f || fabsf(D3DXVec3Dot(&F, &U)) > 1e-3f || !(P._11 > 0.0f) || !(P._22 > 0.0f) || P._44 != 0.0f) {
		static bool reported = false;
		if (!reported) { reported = true; Logger::Log("LAMP MASK SCISSOR: unusual camera matrices (forward %.4f %.4f %.4f, P44 %.3f): the pass is drawn whole", F.x, F.y, F.z, P._44); }
		return 0;
	}
	{
		static bool logged = false;
		const D3DXMATRIX& I = TheRenderManager->InvViewMatrix;
		if (!logged) { logged = true; Logger::Log("LAMP MASK SCISSOR: view forward %.4f %.4f %.4f, world camera forward %.4f %.4f %.4f, P00 %.4f P11 %.4f", F.x, F.y, F.z, I._31, I._32, I._33, P._11, P._22); }
	}
	const float W = (float)TheRenderManager->width, H = (float)TheRenderManager->height;
	const D3DXVECTOR3 C(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
	float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
	bool any = false;
	for (int k = 0; k < lampCount; k++) {
		const float radius = lamps[k].w;
		if (!(radius > 0.0f) || radius > 1e6f) return 0;
		const float reach = radius * 1.02f + 2.0f;
		float lx0 = 1e30f, ly0 = 1e30f, lx1 = -1e30f, ly1 = -1e30f;
		const float kNearClip = 0.25f;
		D3DXVECTOR3 cam[8];
		bool front[8];
		int inFront = 0;
		for (int corner = 0; corner < 8; corner++) {
			const D3DXVECTOR3 p(lamps[k].x + ((corner & 1) ? reach : -reach) - C.x, lamps[k].y + ((corner & 2) ? reach : -reach) - C.y,
				lamps[k].z + ((corner & 4) ? reach : -reach) - C.z);
			cam[corner] = D3DXVECTOR3(D3DXVec3Dot(&p, &R), D3DXVec3Dot(&p, &U), D3DXVec3Dot(&p, &F));
			front[corner] = cam[corner].z >= kNearClip;
			if (front[corner]) inFront++;
		}
		if (inFront == 0) continue;
		auto grow = [&](float x, float y, float z) {
			const float u = (x / z * P._11 + 1.0f) * 0.5f * W;
			const float v = (1.0f - y / z * P._22) * 0.5f * H;
			lx0 = (std::min)(lx0, u); lx1 = (std::max)(lx1, u); ly0 = (std::min)(ly0, v); ly1 = (std::max)(ly1, v);
		};
		for (int corner = 0; corner < 8; corner++) {
			if (!front[corner]) continue;
			grow(cam[corner].x, cam[corner].y, cam[corner].z);
			if (inFront == 8) continue;
			for (int bit = 1; bit < 8; bit <<= 1) {
				const int other = corner ^ bit;
				if (front[other]) continue;
				const float t = (kNearClip - cam[corner].z) / (cam[other].z - cam[corner].z);
				grow(cam[corner].x + (cam[other].x - cam[corner].x) * t, cam[corner].y + (cam[other].y - cam[corner].y) * t, kNearClip);
			}
		}
		if (lx1 < -1.0f || ly1 < -1.0f || lx0 > W + 1.0f || ly0 > H + 1.0f) continue;
		any = true;
		minX = (std::min)(minX, lx0); maxX = (std::max)(maxX, lx1); minY = (std::min)(minY, ly0); maxY = (std::max)(maxY, ly1);
	}
	if (!any) return 1;
	rect.left = (LONG)(std::max)(0.0f, floorf(minX) - 1.0f);
	rect.top = (LONG)(std::max)(0.0f, floorf(minY) - 1.0f);
	rect.right = (LONG)(std::min)(W, ceilf(maxX) + 1.0f);
	rect.bottom = (LONG)(std::min)(H, ceilf(maxY) + 1.0f);
	if (rect.right <= rect.left || rect.bottom <= rect.top) return 1;
	if (rect.left == 0 && rect.top == 0 && rect.right == (LONG)W && rect.bottom == (LONG)H) return 0;
	return 2;
}

#if NVR_TEST_TOOLS
static void LampMaskSelfCheck(IDirect3DDevice9* Device, IDirect3DSurface9* surface, int maskMode, const RECT& rect) {
	static int check = 0;
	static unsigned frame = 0, checks = 0, checksCleared = 0, diffInside = 0, diffOutside = 0, maxDiff = 0, reported = 0;
	if ((frame % 60) == 0) check = TheSettingManager->GetSettingI("Main.Main.Performance", "LampMaskScissorCheck");
	frame++;
	if (check <= 0 || (frame % 8) != 0) return;
	static IDirect3DSurface9* readA = nullptr;
	static IDirect3DSurface9* readB = nullptr;
	static D3DSURFACE_DESC have = {};
	D3DSURFACE_DESC d = {};
	if (FAILED(surface->GetDesc(&d)) || d.Format != D3DFMT_G16R16) return;
	if (!readA || d.Width != have.Width || d.Height != have.Height) {
		if (readA) { readA->Release(); readA = nullptr; }
		if (readB) { readB->Release(); readB = nullptr; }
		if (FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &readA, NULL)) ||
			FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &readB, NULL))) {
			Logger::Log("LAMP MASK CHECK: could not create the read-back surfaces");
			if (readA) { readA->Release(); readA = nullptr; }
			return;
		}
		have = d;
	}
	if (FAILED(Device->GetRenderTargetData(surface, readA))) return;
	TheShaderManager->RenderEffectToRT(surface, TheShaderManager->Effects.PointShadows, true);
	if (FAILED(Device->GetRenderTargetData(surface, readB))) return;
	D3DLOCKED_RECT la = {}, lb = {};
	if (FAILED(readA->LockRect(&la, NULL, D3DLOCK_READONLY))) return;
	if (FAILED(readB->LockRect(&lb, NULL, D3DLOCK_READONLY))) { readA->UnlockRect(); return; }
	unsigned inside = 0, outside = 0, worst = 0, firstX = 0, firstY = 0;
	for (UINT y = 0; y < d.Height; y++) {
		const UInt16* pa = (const UInt16*)((const UInt8*)la.pBits + (size_t)y * la.Pitch);
		const UInt16* pb = (const UInt16*)((const UInt8*)lb.pBits + (size_t)y * lb.Pitch);
		for (UINT x = 0; x < d.Width * 2; x++) {
			if (pa[x] == pb[x]) continue;
			const unsigned diff = pa[x] > pb[x] ? pa[x] - pb[x] : pb[x] - pa[x];
			const bool in = maskMode == 2 && (LONG)(x / 2) >= rect.left && (LONG)(x / 2) < rect.right && (LONG)y >= rect.top && (LONG)y < rect.bottom;
			if (!inside && !outside) { firstX = x / 2; firstY = y; }
			if (in) inside++; else outside++;
			if (diff > worst) worst = diff;
		}
	}
	readB->UnlockRect();
	readA->UnlockRect();
	checks++;
	if (maskMode == 1) checksCleared++;
	diffInside += inside; diffOutside += outside; maxDiff = (std::max)(maxDiff, worst);
	if ((inside || outside) && reported < 10) {
		reported++;
		Logger::Log("LAMP MASK CHECK: DIFFERENCE, mode %d rect %d %d %d %d: %u values inside, %u outside, max %u (16-bit units), first at %u %u",
			maskMode, (int)rect.left, (int)rect.top, (int)rect.right, (int)rect.bottom, inside, outside, worst, firstX, firstY);
	}
	if (checks % 30 == 0)
		Logger::Log("LAMP MASK CHECK: %u checks (%u cleared only, %u scissored), differing values: %u inside the rectangles, %u outside, max %u",
			checks, checksCleared, checks - checksCleared, diffInside, diffOutside, maxDiff);
}
#endif

#if NVR_TEST_TOOLS
static IDirect3DTexture9* FoldCheckSceneTexture = nullptr;
static IDirect3DSurface9* FoldCheckSceneSurface = nullptr;
static IDirect3DTexture9* FoldCheckOutTexture = nullptr;
static IDirect3DSurface9* FoldCheckOutSurface = nullptr;
static IDirect3DSurface9* FoldCheckReadA = nullptr;
static IDirect3DSurface9* FoldCheckReadB = nullptr;
static bool FoldCheckSurfaces(IDirect3DDevice9* Device, IDirect3DSurface9* like) {
	D3DSURFACE_DESC d = {}, have = {};
	if (!like || FAILED(like->GetDesc(&d)) || d.Format != D3DFMT_A16B16G16R16F) return false;
	if (FoldCheckSceneSurface && SUCCEEDED(FoldCheckSceneSurface->GetDesc(&have)) && have.Width == d.Width && have.Height == d.Height) return true;
	IUnknown* old[] = { FoldCheckSceneSurface, FoldCheckSceneTexture, FoldCheckOutSurface, FoldCheckOutTexture, FoldCheckReadA, FoldCheckReadB };
	for (IUnknown* o : old) if (o) o->Release();
	FoldCheckSceneSurface = FoldCheckOutSurface = FoldCheckReadA = FoldCheckReadB = nullptr;
	FoldCheckSceneTexture = FoldCheckOutTexture = nullptr;
	if (FAILED(Device->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &FoldCheckSceneTexture, NULL)) ||
		FAILED(FoldCheckSceneTexture->GetSurfaceLevel(0, &FoldCheckSceneSurface)) ||
		FAILED(Device->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &FoldCheckOutTexture, NULL)) ||
		FAILED(FoldCheckOutTexture->GetSurfaceLevel(0, &FoldCheckOutSurface)) ||
		FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &FoldCheckReadA, NULL)) ||
		FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &FoldCheckReadB, NULL))) {
		Logger::Log("INTERIOR SHADOW FOLD CHECK: could not create its surfaces");
		FoldCheckSceneSurface = nullptr;
		return false;
	}
	return true;
}

static void InteriorFoldSelfCheck(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* SourceSurface,
	bool ao, IDirect3DTexture9* aoTexture, IDirect3DTexture9* skyTexture) {
	static unsigned checks = 0, differing = 0, failed = 0, reported = 0;
	static unsigned long long differingPixels = 0;
	static unsigned worst = 0;
	VolumetricFogEffect* Fog = TheShaderManager->Effects.VolumetricFog;
	IDirect3DSurface9* result = TheTextureManager->RenderedSurface;
	if (!FoldCheckSceneSurface || !result || FAILED(Device->GetRenderTargetData(result, FoldCheckReadA))) { failed++; return; }
	const int foldRound = TheSettingManager->SettingsMain.Main.InteriorShadowFoldRound;
	Fog->compositeShadow = false;
	Fog->compositeInteriorShadow = true;
	Fog->compositeRound = foldRound == 1 ? 1.0f : foldRound == 2 ? 2.0f : foldRound == 3 ? 0.0f : Fog->halfStoreMode == 2 ? 2.0f : 0.0f;
	Fog->compositeAO = ao;
	Fog->compositeAOTexture = aoTexture;
	Fog->compositeSkyTexture = ao ? skyTexture : nullptr;
	Fog->compositeApplied = false;
	Fog->gridTexture = nullptr;
	Fog->sceneOverride = FoldCheckSceneTexture;
	Fog->targetOverride = FoldCheckOutSurface;
	Fog->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	const bool applied = Fog->compositeApplied;
	Fog->sceneOverride = nullptr;
	Fog->targetOverride = nullptr;
	Fog->compositeShadow = Fog->compositeAO = Fog->compositeApplied = Fog->compositeInteriorShadow = false;
	Fog->compositeSkyTexture = nullptr;
	Device->SetRenderTarget(0, RenderTarget);
	if (!applied || FAILED(Device->GetRenderTargetData(FoldCheckOutSurface, FoldCheckReadB))) { failed++; return; }
	D3DSURFACE_DESC d = {};
	FoldCheckReadA->GetDesc(&d);
	D3DLOCKED_RECT la = {}, lb = {};
	if (FAILED(FoldCheckReadA->LockRect(&la, NULL, D3DLOCK_READONLY))) { failed++; return; }
	if (FAILED(FoldCheckReadB->LockRect(&lb, NULL, D3DLOCK_READONLY))) { FoldCheckReadA->UnlockRect(); failed++; return; }
	unsigned pixels = 0, frameWorst = 0, firstX = 0, firstY = 0;
	for (UINT y = 0; y < d.Height; y++) {
		const UInt16* pa = (const UInt16*)((const UInt8*)la.pBits + (size_t)y * la.Pitch);
		const UInt16* pb = (const UInt16*)((const UInt8*)lb.pBits + (size_t)y * lb.Pitch);
		for (UINT x = 0; x < d.Width; x++) {
			bool differs = false;
			for (int c = 0; c < 3; c++) {
				const UInt16 a = pa[x * 4 + c], b = pb[x * 4 + c];
				if (a == b) continue;
				differs = true;
				const unsigned diff = a > b ? a - b : b - a;
				if (diff > frameWorst) frameWorst = diff;
			}
			if (differs) { if (!pixels) { firstX = x; firstY = y; } pixels++; }
		}
	}
	FoldCheckReadB->UnlockRect();
	FoldCheckReadA->UnlockRect();
	checks++;
	if (pixels) { differing++; differingPixels += pixels; worst = (std::max)(worst, frameWorst); }
	if (pixels && reported < 12) {
		reported++;
		Logger::Log("INTERIOR SHADOW FOLD CHECK: DIFFERENCE, %u of %u pixels differ (max %u 16-bit float steps), first at %u %u",
			pixels, d.Width * d.Height, frameWorst, firstX, firstY);
	}
	if (checks % 10 == 0 || (checks < 10 && !pixels))
		Logger::Log("INTERIOR SHADOW FOLD CHECK: %u frames compared (old separate pass vs folded fog pass, same frame), %u differ (%llu pixels in all, max %u steps); %u checks could not run",
			checks, differing, (unsigned long long)differingPixels, worst, failed);
}
#endif

void ShaderManager::RenderEffectToRT(IDirect3DSurface9* RenderTarget, EffectRecord* Effect, bool clearRenderTarget, UINT techniqueIndex) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	Device->SetRenderTarget(0, RenderTarget);
	Effect->Render(Device, RenderTarget, RenderTarget, techniqueIndex, clearRenderTarget, RenderTarget);
};


void ShaderManager::RenderEffectsPreTonemapping(IDirect3DSurface9* RenderTarget) {
	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // Main toggle
	if (!Player->parentCell) return;
	if (GameState.OverlayIsOn && TESMain::IsMenuBackgroundReady()) return; // disable all effects during terminal/lockpicking sequences

	auto timer = TimeLogger();
	static CpuTimer preTonemapCpuTimer("Pre-tonemap chain (CPU)");
	CpuProfileScope preTonemapCpu(preTonemapCpuTimer);

	IDirect3DDevice9* Device = TheRenderManager->device;
	IDirect3DSurface9* SourceSurface = TheTextureManager->SourceSurface;
	IDirect3DSurface9* RenderedSurface = TheTextureManager->RenderedSurface;
	static GpuTimer depthTimer("Depth combine");
	static GpuTimer normalsTimer("Normal reconstruction");
	static GpuTimer pointShadowTimer("Point shadow lighting");
	static GpuTimer sunContactTimer("Sun contact shadows");
	static GpuTimer shadowApplyTimer("Shadow apply");
	static GpuTimer aoTimer("Ambient occlusion");
	static GpuTimer snowAccumulationTimer("Snow accumulation");
	static GpuTimer materialEffectsTimer("Wet + light materials");
	static GpuTimer fogTimer("Volumetric fog");
	static GpuTimer godRaysTimer("God rays");
	static GpuTimer sandstormTimer("Sandstorm");
	static GpuTimer volumetricLightTimer("Volumetric light");
	static GpuTimer cloudsTimer("Volumetric clouds");
	static GpuTimer hdrTimer("Luma + exposure + bloom");
	static GpuTimer avgLumaTimer("  Average luma");
	static GpuTimer exposureTimer("  Exposure");
	static GpuTimer bloomTimer("  Bloom buffers");
	static GpuTimer preColorTimer("Pre-tonemap lens + LUT");

	// prepare device for effects
	Device->SetStreamSource(0, FrameVertex, 0, sizeof(FrameVS));
	Device->SetFVF(FrameFVF);

	// render post process normals for use by shaders
	bool mergedNormals = false;
	{
		GpuProfileScope gpu(depthTimer, Device);
		NormalsEffect* Normals = Effects.Normals;
		if (Normals->Enabled && Normals->Effect && Normals->ShouldRender())
			mergedNormals = Effects.CombineDepth->RenderWithNormals(Device, Normals->Textures.NormalsSurface);
		if (!mergedNormals)
			RenderEffectToRT(Effects.CombineDepth->Textures.CombinedDepthSurface, Effects.CombineDepth, false);
	}
	{
		GpuProfileScope gpu(normalsTimer, Device);
		if (!mergedNormals)
			RenderEffectToRT(Effects.Normals->Textures.NormalsSurface, Effects.Normals, false);
	}
#if NVR_TEST_TOOLS
	OrthoProbe::AfterCombine(Device);
#endif

	const bool forwardInterior = !GameState.isExterior && Effects.ShadowsInteriors->Enabled && PointShadowForward::Active &&
		PointShadowForward::BoundThisFrame && Shaders.PBR && Shaders.PBR->Enabled;
	const bool keepDarkening = !forwardInterior || Effects.ShadowsExteriors->Settings.Interiors.Forward.KeepDarkening;

	static bool shadowBufferClear = false;
	// render a shadow pass for point lights
	if (forwardInterior && !keepDarkening) {
		if (!shadowBufferClear) { Effects.ShadowsExteriors->clearShadowsBuffer(); shadowBufferClear = true; }
	}
	else if ((GameState.isExterior && Effects.ShadowsExteriors->Enabled) || (!GameState.isExterior && Effects.ShadowsInteriors->Enabled)) {
		shadowBufferClear = false;
		{
			GpuProfileScope gpu(pointShadowTimer, Device);
			const float pointShadowsWere = Effects.ShadowsExteriors->Constants.ShadowFade.z;
			if (forwardInterior) Effects.ShadowsExteriors->Constants.ShadowFade.z = 0.0f;
			static EffectRecord::CachedHandle halfCache, upCache;
			const bool halfMask = forwardInterior && LampMaskHalfRes() &&
				Effects.PointShadows->TechniqueHandle(halfCache, "LampMaskHalf") == Effects.PointShadows->Effect->GetTechnique(3) &&
				Effects.PointShadows->TechniqueHandle(upCache, "LampMaskUpsample") == Effects.PointShadows->Effect->GetTechnique(4) &&
				Effects.PointShadows->Effect->GetTechnique(4) != NULL;
			static EffectRecord::CachedHandle mergedTechnique;
			const bool mergedPointShadows = Effects.PointShadows->TechniqueHandle(mergedTechnique, "MergedPointShadows") != NULL;
			RECT maskRect = {};
			int lampCount = 0, maskMode = 0;
			if (TheSettingManager->SettingsMain.Main.LampMaskScissor && !halfMask && mergedPointShadows &&
				!PointShadowForward::ExteriorBatched(GameState.isExterior) &&
				Effects.PointShadows->Enabled && Effects.PointShadows->Effect && Effects.PointShadows->ShouldRender())
				maskMode = LampMaskRect(maskRect, lampCount);
			static int loggedMaskMode = -1;
			if (maskMode != loggedMaskMode) {
				static int stateLines = 0;
				if (stateLines < 20 || GpuTimer::Enabled) {
					Logger::Log("LAMP MASK STATE: %s (%d lamps in its lists)", maskMode == 1 ? "SKIPPED, no lamp reaches the screen: the buffer cleared to 0, as the pass would write" :
						maskMode == 2 ? "SCISSORED to the lamps' screen rectangle" : "drawn whole", lampCount);
					if (++stateLines == 20 && !GpuTimer::Enabled) Logger::Log("LAMP MASK STATE: no more lines (20 logged; the F10 profile logs them all)");
				}
				loggedMaskMode = maskMode;
			}
			if (GpuTimer::Enabled) {
				static unsigned frames = 0, skipped = 0, scissored = 0, lampsSum = 0;
				static double area = 0.0;
				frames++; lampsSum += lampCount;
				if (maskMode == 1) skipped++;
				if (maskMode == 2) { scissored++; area += (double)(maskRect.right - maskRect.left) * (maskRect.bottom - maskRect.top) / ((double)TheRenderManager->width * TheRenderManager->height); }
				if (frames >= 240) {
					Logger::Log("LAMP MASK SCISSOR %s: %u frames, %u cleared only, %u scissored (average %.1f%% of the screen), %u whole; %.1f lamps per frame in its lists",
						TheSettingManager->SettingsMain.Main.LampMaskScissor ? "on" : "off", frames, skipped, scissored, scissored ? 100.0 * area / scissored : 0.0,
						frames - skipped - scissored, (double)lampsSum / frames);
					frames = skipped = scissored = lampsSum = 0; area = 0.0;
				}
			}
			if (maskMode != 0) {
				IDirect3DSurface9* maskSurface = Effects.ShadowsExteriors->Textures.ShadowPassSurface;
				DWORD scissorWas = FALSE;
				RECT scissorRectWas = {};
				Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissorWas);
				const bool haveRectWas = SUCCEEDED(Device->GetScissorRect(&scissorRectWas));
				Device->SetRenderTarget(0, maskSurface);
				Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
				Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0L);
				if (maskMode == 2) {
					Device->SetScissorRect(&maskRect);
					Device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
					Effects.PointShadows->Render(Device, maskSurface, maskSurface, 0, false, maskSurface);
					Device->SetRenderTarget(0, maskSurface);
				}
				Device->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorWas);
				if (haveRectWas) Device->SetScissorRect(&scissorRectWas);
#if NVR_TEST_TOOLS
				LampMaskSelfCheck(Device, maskSurface, maskMode, maskRect);
#endif
			}
			else if (halfMask) {
				RenderEffectToRT(LampMaskHalfSurface(), Effects.PointShadows, true, 3);
				Device->SetTexture(13, LampMaskHalfTexture());
				TheRenderManager->renderState->NoteDeviceTexture(13, LampMaskHalfTexture());
				RenderEffectToRT(Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.PointShadows, false, 4);
				Device->SetTexture(13, nullptr);
				TheRenderManager->renderState->NoteDeviceTexture(13, nullptr);
			}
			else RenderEffectToRT(Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.PointShadows, true);
			if (PointShadowForward::ExteriorBatched(GameState.isExterior)) RenderPointShadowBatches();
			if (!mergedPointShadows && Effects.ShadowsExteriors->Settings.Interiors.LightPoints > 6)
				RenderEffectToRT(Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.PointShadows2, false);
			Effects.ShadowsExteriors->Constants.ShadowFade.z = pointShadowsWere;
		}
		if (!GameState.isExterior && LampContactStrength() > 0.0f) {
			static EffectRecord::CachedHandle contactCache;
			D3DXHANDLE contactTechnique = Effects.PointShadows->TechniqueHandle(contactCache, "LampContact");
			if (contactTechnique && Effects.PointShadows->Effect->GetTechnique(2) == contactTechnique) {
				static GpuTimer lampContactTimer("Lamp contact shadows");
				GpuProfileScope gpu(lampContactTimer, Device);
				RenderEffectToRT(Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.PointShadows, false, 2);
			}
		}
		if (GameState.isExterior) {
			GpuProfileScope gpu(sunContactTimer, Device);
			static EffectRecord::CachedHandle fusedCache;
			D3DXHANDLE fusedTechnique = Effects.SunShadows->TechniqueHandle(fusedCache, "ForwardContactShadows");
			const bool fusedForwardContact = Effects.ShadowsExteriors->Settings.Exteriors.ForwardShadows &&
				fusedTechnique != NULL && Effects.SunShadows->Effect->GetTechnique(1) == fusedTechnique;
			static EffectRecord::CachedHandle sharpCache;
			D3DXHANDLE sharpTechnique = Effects.SunShadows->TechniqueHandle(sharpCache, "ForwardContactSharp");
			const bool sharpContact = fusedForwardContact && Effects.ShadowsExteriors->Settings.ScreenSpace.Sharp && sharpTechnique != NULL &&
				Effects.SunShadows->Effect->GetTechnique(2) == sharpTechnique;
			ShadowsExteriorEffect* sunShadows = Effects.ShadowsExteriors;
			const bool skipContact = sunShadows->FadedContactUnread();
			static int loggedSkip = -1;
			if ((int)skipContact != loggedSkip) {
				Logger::Log("SUN FADE STATE: contact shadows %s", skipContact ? "SKIPPED" : sunShadows->SunFaded ?
					"drawn (an effect reads them: lamp shadows outdoors, blood flow, wet world, underwater or the debug view)" : "drawn");
				loggedSkip = skipContact;
			}
			if (!skipContact) {
				const float forwardWas = sunShadows->Constants.ForwardData.x;
				if (sunShadows->SunFaded && sunShadows->Settings.Exteriors.ForwardShadows) sunShadows->Constants.ForwardData.x = 0.0f;
				RenderEffectToRT(sunShadows->Textures.ShadowPassSurface, Effects.SunShadows, false, sharpContact ? 2 : fusedForwardContact ? 1 : 0);
				sunShadows->Constants.ForwardData.x = forwardWas;
			}
#if NVR_TEST_TOOLS
			else if (sunShadows->Settings.Exteriors.SkipWhenFadedCheck) {
				Device->SetRenderTarget(0, sunShadows->Textures.ShadowPassSurface);
				Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
			}
#endif
		}
	}
	else {
		// Nothing above ran this frame, so ShadowPassSurface keeps whatever it last
		// held -- e.g. an exterior sun-shadow composite, walked in from outdoors,
		// frozen here for as long as this branch keeps being skipped (interior with
		// Interior point-shadows off is the common case). It's still sampled
		// unconditionally by other independently-enabled effects (Specular and
		// others), so reset it to the neutral "no shadow" value rather than leaving
		// stale exterior data for them to read.
		if (!shadowBufferClear) { Effects.ShadowsExteriors->clearShadowsBuffer(); shadowBufferClear = true; }
	}

	Effects.SunFar->RenderFactor(Device);

	Device->SetRenderTarget(0, RenderTarget);

	struct ChainGuard { ~ChainGuard() { TheShaderManager->Chain.End(); } } chainGuard;
	if (!Chain.Begin(RenderTarget))
		Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);

	Effects.Reflections->CaptureMaterial(Device);
	Device->SetRenderTarget(0, RenderTarget);

	if (Effects.SkinScattering && SkinShaders::NewSkinEnabled()) {
		static GpuTimer skinScatterTimer("Skin scattering");
		GpuProfileScope gpu(skinScatterTimer, Device);
		Effects.SkinScattering->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	auto wouldRender = [](EffectRecord* effect) { return effect && effect->Enabled && effect->Effect && effect->ShouldRender(); };
	if (Effects.SkyView && Effects.SkyView->ShouldRender()) {
		static GpuTimer skyViewTimer("Physical sky table");
		GpuProfileScope gpu(skyViewTimer, Device);
		Effects.SkyView->RenderTable(Device, RenderTarget);
	}
	if (wouldRender(Effects.VolumetricClouds)) {
		GpuProfileScope gpu(cloudsTimer, Device);
		Effects.VolumetricClouds->RenderClouds(Device, RenderTarget, TheTextureManager->RenderedSurface, SourceSurface);
	}
	const bool ambientLight = wouldRender(Effects.AmbientLight);
	struct EnabledGuard { EffectRecord* effect; bool was; ~EnabledGuard() { if (effect) effect->Enabled = was; } };
	EnabledGuard aoGuard{ ambientLight ? Effects.AmbientOcclusion : nullptr, Effects.AmbientOcclusion->Enabled };
	EnabledGuard bounceGuard{ ambientLight ? Effects.BounceLight : nullptr, Effects.BounceLight->Enabled };
	if (ambientLight) Effects.AmbientOcclusion->Enabled = Effects.BounceLight->Enabled = false;
	AmbientOcclusionEffect* AO = Effects.AmbientOcclusion;
	VolumetricFogEffect* Fog = Effects.VolumetricFog;
	const bool shadowApplies = GameState.isExterior && wouldRender(Effects.ShadowsExteriors);
	const bool aoApplies = wouldRender(AO);
	const bool effectsBetween = wouldRender(Effects.SnowAccumulation) || wouldRender(Effects.WetWorld) || wouldRender(Effects.BloodFlow) ||
		wouldRender(Effects.Flashlight) || wouldRender(Effects.Specular) || wouldRender(Effects.Underwater) ||
		wouldRender(Effects.BounceLight) || wouldRender(Effects.Reflections) || ambientLight;
	const bool interiorFoldable = !GameState.isExterior && keepDarkening && forwardInterior &&
		!Effects.ShadowsExteriors->Settings.Interiors.Forward.DarkeningBlur && TheSettingManager->SettingsMain.Main.InteriorShadowFold &&
		!(Effects.Reflections->Enabled && Effects.Reflections->Effect && Effects.Reflections->CubeWanted()) &&
		Effects.ShadowsInteriors->DedicatedUsable(RenderTarget, TheTextureManager->RenderedSurface) && Fog->CanCompositeInterior() &&
		(TheSettingManager->SettingsMain.Main.InteriorShadowFoldRound != 0 || Fog->HalfStoreMode(Device) >= 0);
	bool foldCheckFrame = false;
#if NVR_TEST_TOOLS
	{
		static unsigned frame = 0;
		static int every = 0;
		if ((frame % 60) == 0) every = TheSettingManager->GetSettingI("Main.Main.Performance", "InteriorShadowFoldCheck");
		frame++;
		foldCheckFrame = every > 0 && interiorFoldable && (frame % (unsigned)every) == 0 && !effectsBetween &&
			Fog->CanComposite(aoApplies ? AO->NextResultSurface() : nullptr) && FoldCheckSurfaces(Device, TheTextureManager->RenderedSurface);
	}
#endif
	const bool interiorApplies = interiorFoldable && !foldCheckFrame;
	bool composite = (shadowApplies || aoApplies || interiorApplies) && !effectsBetween &&
		Fog->CanComposite(aoApplies ? AO->NextResultSurface() : nullptr);
	auto interiorApply = [&]() {
		Effects.ShadowsInteriors->skipBlur = forwardInterior && !Effects.ShadowsExteriors->Settings.Interiors.Forward.DarkeningBlur;
		Effects.ShadowsInteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, true, SourceSurface);
		Effects.ShadowsInteriors->skipBlur = false;
	};
	if (!GameState.isExterior) {
		static int loggedFold = -1;
		const int fold = (composite && interiorApplies) || foldCheckFrame ? 1 : 0;
		if (fold != loggedFold) {
			loggedFold = fold;
			Logger::Log("INTERIOR SHADOW FOLD: %s", fold ? "on, the lamp darkening is applied inside the fog composite (no separate pass)" :
				!TheSettingManager->SettingsMain.Main.InteriorShadowFold ? "off (setting)" : !keepDarkening ? "off (no darkening drawn: KeepDarkening off)" :
				!forwardInterior ? "off (no forward lamp shadows this frame: the old blurred pass)" :
				Effects.ShadowsExteriors->Settings.Interiors.Forward.DarkeningBlur ? "off (DarkeningBlur)" :
				effectsBetween ? "off (an effect between it and the fog reads the picture)" :
				(Effects.Reflections->Enabled && Effects.Reflections->Effect && Effects.Reflections->CubeWanted()) ? "off (the Reflections surroundings cube reads the picture)" :
				"off (the dedicated pass or the fog composite is unavailable, or this GPU's 16-bit rounding is unknown)");
		}
	}
	const bool pairApply = !composite && shadowApplies && aoApplies && !wouldRender(Effects.SnowAccumulation) &&
		Fog->CanApplyShadowAO();
	AO->deferredReady = false;
	AO->skyReady = false;
	const bool shadowIntoAmbientLight = ambientLight && shadowApplies && !composite && !pairApply && !wouldRender(Effects.SnowAccumulation) &&
		!TheSettingManager->SettingsMain.Main.DisableCompositeApply;

	{
		GpuProfileScope gpu(shadowApplyTimer, Device);
		if (GameState.isExterior) {
			if (!(composite && shadowApplies) && !pairApply && !shadowIntoAmbientLight)
				Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		}
		else if (keepDarkening && !(composite && interiorApplies)) {
#if NVR_TEST_TOOLS
			if (foldCheckFrame) Device->StretchRect(TheTextureManager->RenderedSurface, NULL, FoldCheckSceneSurface, NULL, D3DTEXF_NONE);
#endif
			interiorApply();
		}
	}

	{
		GpuProfileScope gpu(snowAccumulationTimer, Device);
		Effects.SnowAccumulation->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(aoTimer, Device);
		AO->deferCombine = (composite || pairApply) && aoApplies;
		AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		AO->deferCombine = false;
		if (pairApply) {
			static GpuTimer mergedApplyTimer("  Shadow + AO apply (merged)");
			bool merged = false;
			if (AO->deferredReady) {
				GpuProfileScope gpuMerged(mergedApplyTimer, Device);
				Fog->compositeSkyTexture = AO->SkyTexture();
				merged = Fog->RenderShadowAO(Device, RenderTarget, TheTextureManager->RenderedSurface, true, AO->ResultTexture());
				Fog->compositeSkyTexture = nullptr;
			}
			if (!merged) {
				Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
				if (AO->deferredReady) {
					AO->combineOnly = true;
					AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
					AO->combineOnly = false;
				}
				else AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
			}
			AO->deferredReady = false;
		}
		if (composite && aoApplies && !AO->deferredReady) {
			composite = false;
			if (shadowApplies)
				Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
			if (interiorApplies) interiorApply();
			AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		}
	}
	const bool specularRenders = wouldRender(Effects.Specular) && Effects.Specular->Textures.LightSurface;
	const bool bounceIntoSpecular = specularRenders && wouldRender(Effects.BounceLight) && !wouldRender(Effects.WetWorld) &&
		!wouldRender(Effects.Flashlight) && Effects.Specular->CanTakeBounce() && !Effects.BounceLight->SpillOn() &&
		!Effects.BounceLight->HardEdgesOn();
	if (ambientLight) {
		static GpuTimer ambientLightTimer("Ambient light (AO + bounce)");
		GpuProfileScope gpu(ambientLightTimer, Device);
		Effects.AmbientLight->RenderPrepare(Device);
		if (AmbientOcclusionEffect::SkyV2Active() && AO->Effect) {
			AO->lastLowRes = false;
			AO->RenderSkyVisibility(Device, Effects.AmbientLight->SmoothedNormals());
			Device->SetRenderTarget(0, RenderTarget);
		}
		Effects.AmbientLight->foldShadow = shadowIntoAmbientLight;
		Effects.AmbientLight->shadowApplied = false;
		Effects.AmbientLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Effects.AmbientLight->foldShadow = false;
		if (shadowIntoAmbientLight && !Effects.AmbientLight->shadowApplied)
			Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
#if NVR_TEST_TOOLS
	SettingShots::CaptureStage(Device, TheTextureManager->RenderedSurface);
#endif
	Effects.BounceLight->deferredReady = false;
	if (Effects.BounceLight->Enabled) {
		static GpuTimer bounceLightTimer("Bounce light");
		GpuProfileScope gpu(bounceLightTimer, Device);
		Effects.BounceLight->deferCombine = bounceIntoSpecular;
		Effects.BounceLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Effects.BounceLight->deferCombine = false;
	}
	{
		static GpuTimer bloodFlowTimer("Blood flow");
		GpuProfileScope gpu(bloodFlowTimer, Device);
		Effects.BloodFlow->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(materialEffectsTimer, Device);
		if (Effects.WetWorld->Enabled && Effects.WetWorld->Constants.Data.x > 0.0f && GameState.isExterior && !GameState.isUnderwater &&
			TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "HudDrops") > 0.0f) {
			Effects.WetWorld->RenderCoverTest(Device);
			Device->SetRenderTarget(0, RenderTarget);
		}
		Effects.WetWorld->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, Effects.WetWorld->MainTechnique(), false, SourceSurface);
		// Beam march first, into its own half res buffer, so the Flashlight Combine pass can
		// read it. Control.x already folds the effect toggle, the per view toggle and the
		// strength together, so this one test gates the whole thing.
		if (Effects.FlashlightBeam->Constants.Control.x > 0.0f) {
			RenderEffectToRT(Effects.FlashlightBeam->Textures.VolumetricSurface, Effects.FlashlightBeam, true);
			Device->SetRenderTarget(0, RenderTarget);
		}
		Effects.Flashlight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, Effects.Flashlight->selectedPass, true, SourceSurface);
		if (specularRenders) {
			RenderEffectToRT(Effects.Specular->Textures.NormalsSurface, Effects.Specular, false, 1);
			Effects.Specular->BindExtras(Device, true);
			RenderEffectToRT(Effects.Specular->Textures.LightSurface, Effects.Specular, false, 2);
			Effects.Specular->BindExtras(Device, false);
			Device->SetRenderTarget(0, RenderTarget);
		}
		bool specularDone = false;
		if (Effects.BounceLight->deferredReady) {
			static GpuTimer mergedCombineTimer("  Bounce + specular combine (merged)");
			{
				GpuProfileScope gpuMerged(mergedCombineTimer, Device);
				specularDone = Effects.Specular->RenderWithBounce(Device, RenderTarget, TheTextureManager->RenderedSurface,
					Effects.BounceLight->deferredTexture, Effects.BounceLight->Constants.Data, Effects.BounceLight->deferredLayout);
			}
			if (!specularDone) {
				Effects.BounceLight->combineOnly = true;
				Effects.BounceLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
				Effects.BounceLight->combineOnly = false;
			}
			Effects.BounceLight->deferredReady = false;
		}
		if (!specularDone) Effects.Specular->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Effects.Underwater->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
#if NVR_TEST_TOOLS
	EffectSelfTest::Capture(EffectSelfTest::AfterSpill, TheTextureManager->RenderedSurface);
#endif
	{
		static GpuTimer reflectionsTimer("Reflections (SSR)");
		GpuProfileScope gpu(reflectionsTimer, Device);
		if (wouldRender(Effects.Reflections)) {
			Effects.Reflections->RenderBuffers(Device);
			Device->SetRenderTarget(0, RenderTarget);
			Effects.Reflections->BindMaterial();
			Effects.Reflections->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
			Effects.Reflections->ClearPassTextures();
		}
		else if (Effects.Reflections->Enabled && Effects.Reflections->Effect && Effects.Reflections->CubeWanted()) {
			Effects.Reflections->RenderCubeOnly(Device);
			Device->SetRenderTarget(0, RenderTarget);
		}
	}
	const bool lightGrid = Effects.VolumetricLight->CompositeTechnique() == 8 && wouldRender(Effects.VolumetricLight);
	if (lightGrid) {
		Effects.VolumetricLight->RenderBuffers(Device);
		Device->SetRenderTarget(0, RenderTarget);
	}
	Fog->gridTexture = lightGrid ? Effects.VolumetricLight->Textures.GridTexture : nullptr;
	Fog->gridApplied = false;
	{
		GpuProfileScope gpu(fogTimer, Device);
		Fog->compositeShadow = composite && shadowApplies;
		Fog->compositeInteriorShadow = composite && interiorApplies;
		const int foldRound = TheSettingManager->SettingsMain.Main.InteriorShadowFoldRound;
		Fog->compositeRound = foldRound == 1 ? 1.0f : foldRound == 2 ? 2.0f : foldRound == 3 ? 0.0f : Fog->halfStoreMode == 2 ? 2.0f : 0.0f;
		Fog->compositeAO = composite && AO->deferredReady;
		Fog->compositeAOTexture = AO->ResultTexture();
		Fog->compositeSkyTexture = Fog->compositeAO ? AO->SkyTexture() : nullptr;
		Fog->compositeApplied = false;
		Fog->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Fog->compositeSkyTexture = nullptr;
		const bool applied = Fog->compositeApplied;
#if NVR_TEST_TOOLS
		const bool checkAO = Fog->compositeAO && applied;
		IDirect3DTexture9* checkAOTexture = Fog->compositeAOTexture;
		IDirect3DTexture9* checkSky = checkAO ? AO->SkyTexture() : nullptr;
#endif
		Fog->compositeShadow = Fog->compositeAO = Fog->compositeApplied = Fog->compositeInteriorShadow = false;
		if (composite && !applied) {
			if (shadowApplies)
				Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
			if (interiorApplies) interiorApply();
			if (AO->deferredReady) {
				AO->combineOnly = true;
				AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
				AO->combineOnly = false;
			}
			Fog->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		}
#if NVR_TEST_TOOLS
		if (foldCheckFrame && (composite ? applied : !aoApplies)) InteriorFoldSelfCheck(Device, RenderTarget, SourceSurface, checkAO, checkAOTexture, checkSky);
#endif
		AO->deferredReady = false;
	}
	{
		GpuProfileScope gpu(volumetricLightTimer, Device);
		if (lightGrid) {
			if (!Fog->gridApplied)
				Effects.VolumetricLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 8, false, SourceSurface);
		}
		else if (wouldRender(Effects.VolumetricLight)) {
			Effects.VolumetricLight->RenderBuffers(Device);
			Device->SetRenderTarget(0, RenderTarget);
			Effects.VolumetricLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, Effects.VolumetricLight->CompositeTechnique(), false, SourceSurface);
		}
		Fog->gridTexture = nullptr;
	}
	{
		GpuProfileScope gpu(sandstormTimer, Device);
		if (wouldRender(Effects.Sandstorm) && Effects.Sandstorm->UseFastPath()) {
			Effects.Sandstorm->RenderBuffers(Device);
			Device->SetRenderTarget(0, RenderTarget);
			Effects.Sandstorm->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 5, false, SourceSurface);
		}
		else Effects.Sandstorm->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(godRaysTimer, Device);
		Effects.GodRays->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, true, SourceSurface);
	}

	{
		GpuProfileScope gpu(hdrTimer, Device);
		// calculate average luma for use by shaders
		if (avglumaRequired) {
			GpuProfileScope gpuLuma(avgLumaTimer, Device);
			RenderEffectToRT(Effects.AvgLuma->Textures.AvgLumaSurface, Effects.AvgLuma, NULL);
			Device->SetRenderTarget(0, RenderTarget); 	// restore device used for effects
		}
		{
			GpuProfileScope gpuExposure(exposureTimer, Device);
			if (!(TheSettingManager->SettingsMain.Main.SkipIdentityExposure && Effects.Exposure->Constants.Data.y == 0.0f))
				Effects.Exposure->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		}
		{
			GpuProfileScope gpuBloom(bloomTimer, Device);
			Effects.Bloom->RenderBloomBuffer(RenderTarget);
		}
	}

	{
		GpuProfileScope gpu(preColorTimer, Device);
		Effects.Lens->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		if (Effects.LUT->Settings.PreTonemapping)
			Effects.LUT->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}

	timer.LogTime("ShaderManager::RenderEffectsPreTonemapping");
}


static const char* ImageSpaceInstanceKind(UInt32 vtable) {
	switch (vtable) {
	case 0x102D12C: return "form";
	case 0x102D2A4: return "DOF";
	case 0x102D36C: return "radial blur";
	case 0x102D454: return "radial blur (DRB)";
	case 0x102D1CC: return "base";
	default: return "unknown";
	}
}

static void LogActiveImageSpaceEffects() {
	if (!Tes) return;
	int count = 0;
	for (TList<void>::Entry* entry = &Tes->activeIMODs.First; entry && count < 32; entry = entry->next) {
		UInt32* instance = (UInt32*)entry->item;
		if (!instance || IsBadReadPtr(instance, 12 * sizeof(UInt32))) continue;
		count++;
		UInt32 formID = 0, formOffset = 0;
		for (UInt32 k = 1; k < 12 && !formID; k++) {
			TESForm* form = (TESForm*)instance[k];
			if (form && !IsBadReadPtr(form, sizeof(TESForm)) && form->formType == TESForm::FormType::kFormType_ImageSpaceModifier) {
				formID = form->refID;
				formOffset = k * 4;
			}
		}
		char words[12 * 9 + 1] = {};
		for (int k = 0; k < 12; k++) sprintf_s(words + k * 9, 10, "%08X ", instance[k]);
		Logger::Log("IMAGESPACE active %d: %s instance %p, IMOD form %08X (at +%02X) | %s", count, ImageSpaceInstanceKind(instance[0]),
			(void*)instance, formID, formOffset, words);
	}
	Logger::Log("IMAGESPACE %d active image-space effect(s) (the game's fades, night eye, chem and weather tints)", count);
}

static void LogActiveSwitches(bool force) {
	static std::string lastLine;
	static const char* sections[][2] = { { "Main.Main.Performance", "performance" }, { "Main.Main.ReducedQuality", "reduced quality" },
		{ "Shaders.Terrain.Parallax", "terrain parallax" }, { "Shaders.ShadowsExteriors.SunSmoothing", "sun smoothing" },
		{ "Shaders.BounceLight.Status", "bounce light" }, { "Shaders.BounceLight.Main", "bounce light" },
		{ "Shaders.ContactHardening.Status", "contact hardening" }, { "Shaders.ContactHardening.Main", "contact hardening" },
		{ "Shaders.Particles.Status", "lit particles" }, { "Shaders.Particles.Main", "lit particles" } };
	typedef SettingManager::Configuration Config;
	std::string line;
	Config::SettingList nodes;
	for (auto& section : sections) {
		TheSettingManager->Config.FillSettings(&nodes, section[0]);
		std::sort(nodes.begin(), nodes.end());
		if (!line.empty()) line += " |";
		line += " ";
		line += section[1];
		line += ":";
		for (const Config::ConfigNode& node : nodes) {
			char value[96];
			switch (node.Type) {
			case Config::NodeType::Boolean: strcpy(value, node.BoolValue ? "on" : "off"); break;
			case Config::NodeType::Integer: sprintf(value, "%d", node.IntValue); break;
			case Config::NodeType::Float: sprintf(value, "%g", node.FloatValue); break;
			default: strcpy(value, node.Value); break;
			}
			line += " ";
			line += node.Key;
			line += "=";
			line += value;
		}
	}
	if (!force && line == lastLine) return;
	if (!force) TheFrameTimeMonitor().Flush();
	lastLine = line;
	Logger::Log("SWITCHES%s", line.c_str());
}

/*
* Renders the effect that have been set to enabled.
*/
void ShaderManager::RenderEffects(IDirect3DSurface9* RenderTarget) {
	VolumetricSmoke::FrameNumber++;
	static CpuTimer frameIntervalTimer("Frame interval (CPU)");
	if (Player->parentCell && !InterfaceManager->IsActive(Menu::kMenuType_Loading) && Global->OnKeyDown(0x44)) {
		GpuTimer::Enabled = !GpuTimer::Enabled;
		Logger::Log("GPU PROFILE P78 test (forward quality + culling + lamp trims) %s (F10), effects %s, D3D9 runtime: %s", GpuTimer::Enabled ? "enabled" : "paused",
			TheSettingManager->SettingsMain.Main.RenderEffects ? "on" : "OFF", TheRenderManager->D3D9RuntimeDescription());
		LogActiveImageSpaceEffects();
		if (!GpuTimer::Enabled) TheFrameTimeMonitor().Flush();
		else LogActiveSwitches(true);
	}
	if (GpuTimer::Enabled) {
		static unsigned switchCheckFrame = 0;
		if (++switchCheckFrame >= 30) { switchCheckFrame = 0; LogActiveSwitches(false); }
		static TESObjectCELL* lastCell = nullptr;
		static unsigned framesSinceTransition = 1000;
		TESObjectCELL* cell = Player ? Player->parentCell : nullptr;
		if (cell != lastCell || InterfaceManager->IsActive(Menu::kMenuType_Loading)) { lastCell = cell; framesSinceTransition = 0; }
		else framesSinceTransition++;
		const double interval = frameIntervalTimer.Tick();
		if (interval > 0.0) TheFrameTimeMonitor().Add(interval, framesSinceTransition < 180);
	}
	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // Main toggle
	if (!Player->parentCell) return;
	if (GameState.OverlayIsOn) return; // disable all effects during terminal/lockpicking sequences because they bleed through the overlay

	auto timer = TimeLogger();
	static CpuTimer postChainCpuTimer("Post chain (CPU)");
	CpuProfileScope postChainCpu(postChainCpuTimer);

	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* SourceSurface = TheTextureManager->SourceSurface;
	IDirect3DSurface9* RenderedSurface = TheTextureManager->RenderedSurface;
	static GpuTimer postCopyTimer("Post-tonemap copies");
	static GpuTimer weatherTimer("Weather + legacy bloom");
	static GpuTimer heatHazeTimer("Heat haze");
	static GpuTimer colorTimer("Coloring + LUT");
	static GpuTimer dofTimer("Depth of field");
	static GpuTimer motionBlurTimer("Motion blur");
	static GpuTimer lensTimer("Lens overlays");
	static GpuTimer ditherTimer("Dither buster");
	static GpuTimer smaaTimer("SMAA");
	static GpuTimer fxaaTimer("FXAA");
	static GpuTimer sharpenTimer("Sharpening");
	static GpuTimer cinemaTimer("Cinema");
	static GpuTimer imageAdjustTimer("Image adjust + debug");
	static GpuTimer skyDitherTimer("Sky dither");
	static GpuTimer bodycamTimer("Bodycam");
	static GpuTimer taaTimer("TAA");

	Device->SetStreamSource(0, FrameVertex, 0, sizeof(FrameVS));
	Device->SetFVF(FrameFVF);

	// prepare device for effects
	Device->SetRenderTarget(0, RenderTarget);

	// copy the source render target to both the rendered and source textures (rendered gets updated after every pass, source once per effect)
	{
		GpuProfileScope gpu(postCopyTimer, Device);
		if (!Chain.Begin(RenderTarget))
			Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	}
	struct ChainGuard { ~ChainGuard() { TheShaderManager->Chain.End(); } } chainGuard;
	Effects.Debug->CaptureClipStart(Device, RenderTarget);

	auto renders = [](EffectRecord* effect) { return effect && effect->Enabled && effect->Effect && effect->ShouldRender(); };
	const bool colorLUT = renders(Effects.Coloring) && !Effects.LUT->Settings.PreTonemapping && renders(Effects.LUT) &&
		Effects.Coloring->Effect->GetTechniqueByName("ColoringLUT");
	const bool cinemaDither = renders(Effects.Cinema) && renders(Effects.SkyDither) && !renders(Effects.ImageAdjust) &&
		!renders(Effects.Bodycam) && Effects.Cinema->Effect->GetTechniqueByName("CinemaSkyDither");

	{
		EffectRecord* const order[] = {
			Effects.Rain, Effects.Snow, Effects.BloomLegacy, Effects.HeatHaze, Effects.VolumetricSmoke, Effects.BarrelHaze, Effects.Coloring, Effects.LUT, Effects.DepthOfField,
			Effects.MotionBlur, Effects.BloodLens, Effects.WaterLens, Effects.LowHF, Effects.DitherBuster,
			Effects.SMAA, Effects.FXAA, Effects.Sharpening, Effects.Cinema, Effects.ImageAdjust, Effects.Bodycam, Effects.SkyDither, Effects.Debug };
		EffectRecord* last = nullptr;
		for (EffectRecord* effect : order)
			if (effect && effect->Enabled && effect->Effect && effect->ShouldRender() &&
				!(colorLUT && effect == Effects.LUT) && !(cinemaDither && effect == Effects.SkyDither)) last = effect;
		if (Effects.Debug->ClipViewActive()) last = Effects.Debug;
		Chain.SetFinalEffect(last);
	}

	{
		GpuProfileScope gpu(taaTimer, Device);
		Effects.TAA->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}

	{
		GpuProfileScope gpu(weatherTimer, Device);
		Effects.Rain->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Effects.Snow->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Effects.BloomLegacy->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(heatHazeTimer, Device);
		Effects.HeatHaze->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}

	Effects.VolumetricSmoke->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, nullptr);
	Effects.BarrelHaze->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, nullptr);

	// screenspace coloring/blurring effects get rendered last
	{
		GpuProfileScope gpu(colorTimer, Device);
		Effects.Coloring->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, colorLUT ? 1 : 0, false, SourceSurface);
		if (!Effects.LUT->Settings.PreTonemapping && !colorLUT)
			Effects.LUT->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(dofTimer, Device);
		const UINT technique = !Effects.DepthOfField->Constants.Enabled && Effects.DepthOfField->Constants.Blur.x ? 1 : 0;
		Effects.DepthOfField->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, technique, false, technique == 1 ? nullptr : SourceSurface);
	}
	{
		GpuProfileScope gpu(motionBlurTimer, Device);
		Effects.MotionBlur->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}

	// lens effects
	{
		GpuProfileScope gpu(lensTimer, Device);
		Effects.BloodLens->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Effects.WaterLens->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Effects.LowHF->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(ditherTimer, Device);
		Effects.DitherBuster->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, Effects.DitherBuster->TechniqueIndex(), false, SourceSurface);
	}
	{
		GpuProfileScope gpu(smaaTimer, Device);
		Effects.SMAA->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(fxaaTimer, Device);
		Effects.FXAA->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(sharpenTimer, Device);
		Effects.Sharpening->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(cinemaTimer, Device);
		Effects.Cinema->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, cinemaDither ? 1 : 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(imageAdjustTimer, Device);
		Effects.ImageAdjust->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(bodycamTimer, Device);
		Effects.Bodycam->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(skyDitherTimer, Device);
		if (!cinemaDither) Effects.SkyDither->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
	}
	{
		GpuProfileScope gpu(imageAdjustTimer, Device);
		Effects.Debug->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface);
		Effects.Debug->RenderClipView(Device, RenderTarget);
	}

	timer.LogTime("ShaderManager::RenderEffects");
}

EffectRecord* ShaderManager::GetEffectByName(const char* Name) {
	// effects
	EffectsList::iterator t = EffectsNames.find(Name);
	if (t == EffectsNames.end()) return nullptr;
	return *(t->second);
}


ShaderCollection* ShaderManager::GetShaderCollectionByName(const char* Name) {
	// shaders
	ShaderList::iterator t = ShaderNames.find(Name);
	if (t == ShaderNames.end()) return nullptr;
	return *(t->second);
}

/*
* Writes the settings corresponding to the shader/effect name, to switch it between enabled/disabled.*
* Also creates or deletes the corresponding Effect Record.
*/
void ShaderManager::SwitchShaderStatus(const char* Name) {
	IsMenuSwitch = true;

	// effects
	EffectRecord* effect = GetEffectByName(Name);
	if (effect) {
		bool setting = effect->SwitchEffect();
		TheSettingManager->SetMenuShaderEnabled(Name, setting);

		IsMenuSwitch = false;
		return;
	}

	// shaders
	ShaderCollection* shader = GetShaderCollectionByName(Name);
	if (shader) {
		bool setting = shader->SwitchShader();
		TheSettingManager->SetMenuShaderEnabled(Name, setting);

		IsMenuSwitch = false;
		return;
	}

	TheSettingManager->SetMenuShaderEnabled(Name, !TheSettingManager->GetMenuShaderEnabled(Name));
	IsMenuSwitch = false;
}

void ShaderManager::SetCustomConstant(const char* Name, D3DXVECTOR4 Value) {
	CustomConstants::iterator v = CustomConst.find(std::string(Name));
	if (v != CustomConst.end()) v->second = Value;
}


bool FrameChain::Owns(IDirect3DSurface9* renderTarget, IDirect3DSurface9* renderedSurface) {
	const bool owns = Active && renderTarget == GameTarget && renderedSurface == TheTextureManager->RenderedSurface;
	if (owns) ReclaimFinal();
	return owns;
}

void FrameChain::ReclaimFinal() {
	if (!FinalWritten) return;
	TheRenderManager->device->StretchRect(GameTarget, NULL, Surf[Current], NULL, D3DTEXF_NONE);
	FinalWritten = false;
	FinalEffect = nullptr;
}

void FrameChain::Publish() {
	TheTextureManager->RenderedTexture = Tex[Current];
	TheTextureManager->RenderedSurface = Surf[Current];
}

bool FrameChain::EnsureTexture(Pair& pair, int slot) {
	if (pair.Texture[slot]) return true;
	if (FAILED(TheRenderManager->device->CreateTexture(pair.Width, pair.Height, 1, D3DUSAGE_RENDERTARGET, pair.Format,
		D3DPOOL_DEFAULT, &pair.Texture[slot], NULL)) || FAILED(pair.Texture[slot]->GetSurfaceLevel(0, &pair.Surface[slot]))) {
		if (pair.Surface[slot]) { pair.Surface[slot]->Release(); pair.Surface[slot] = nullptr; }
		if (pair.Texture[slot]) { pair.Texture[slot]->Release(); pair.Texture[slot] = nullptr; }
		Logger::Log("[ERROR] Frame chain: could not create a %ux%u target (format %u); using copies.", pair.Width, pair.Height, pair.Format);
		return false;
	}
	return true;
}

bool FrameChain::Begin(IDirect3DSurface9* gameTarget) {
	if (Active) End();
	FinalEffect = nullptr;
	FinalWritten = false;
	if (!gameTarget || TheSettingManager->SettingsMain.Main.DisableFrameChain) return false;
	IDirect3DDevice9* Device = TheRenderManager->device;
	D3DSURFACE_DESC desc = {};
	if (FAILED(gameTarget->GetDesc(&desc)) || desc.MultiSampleType != D3DMULTISAMPLE_NONE) return false;

	int index = -1;
	for (int i = 0; i < 2 && index < 0; i++) {
		const Pair& pair = Pairs[i];
		if (pair.Format == desc.Format && pair.Width == desc.Width && pair.Height == desc.Height) index = i;
	}
	for (int i = 0; i < 2 && index < 0; i++) {
		if (Pairs[i].Format == D3DFMT_UNKNOWN) {
			Pairs[i].Format = desc.Format;
			Pairs[i].Width = desc.Width;
			Pairs[i].Height = desc.Height;
			index = i;
		}
	}
	if (index < 0) return false;
	Pair& pair = Pairs[index];

	IDirect3DTexture9* gameTexture = nullptr;
	const bool useGameTexture = !TheSettingManager->SettingsMain.Main.DisableChainGameTexture &&
		SUCCEEDED(gameTarget->GetContainer(IID_IDirect3DTexture9, (void**)&gameTexture)) && gameTexture &&
		gameTexture->GetLevelCount() == 1;
	if (useGameTexture) {
		if (!EnsureTexture(pair, 0)) { gameTexture->Release(); return false; }
		Tex[0] = gameTexture;	Surf[0] = gameTarget;
		Tex[1] = pair.Texture[0]; Surf[1] = pair.Surface[0];
		GameTexture = gameTexture;
	}
	else {
		if (gameTexture) { gameTexture->Release(); gameTexture = nullptr; }
		if (!EnsureTexture(pair, 0) || !EnsureTexture(pair, 1)) return false;
		if (FAILED(Device->StretchRect(gameTarget, NULL, pair.Surface[0], NULL, D3DTEXF_NONE))) return false;
		Tex[0] = pair.Texture[0]; Surf[0] = pair.Surface[0];
		Tex[1] = pair.Texture[1]; Surf[1] = pair.Surface[1];
	}

	static bool reported[2][2] = {};
	if (!reported[index][useGameTexture]) {
		Logger::Log("UNOFFICIAL frame chain: %ux%u format %u, %s.", desc.Width, desc.Height, desc.Format,
			useGameTexture ? "game target used as a chain buffer (no seed copy)" : "two NVR buffers (seed copy)");
		reported[index][useGameTexture] = true;
	}

	Current = 0;
	SavedTexture = TheTextureManager->RenderedTexture;
	SavedSurface = TheTextureManager->RenderedSurface;
	GameTarget = gameTarget;
	Active = true;
	Publish();
	return true;
}

void FrameChain::Commit() {
	if (!Active) return;
	Current ^= 1;
	Publish();
}

void FrameChain::Sync() {
	if (!Active) return;
	ReclaimFinal();
	IDirect3DDevice9* Device = TheRenderManager->device;
	if (Surf[Current] == GameTarget) {
		Device->StretchRect(GameTarget, NULL, Surf[Current ^ 1], NULL, D3DTEXF_NONE);
		Current ^= 1;
		Publish();
	}
	else
		Device->StretchRect(Surf[Current], NULL, GameTarget, NULL, D3DTEXF_NONE);
}

void FrameChain::End() {
	if (!Active) return;
	IDirect3DDevice9* Device = TheRenderManager->device;
	{
		static GpuTimer endCopyTimers[2] = { GpuTimer("  Chain end copy (HDR)"), GpuTimer("  Chain end copy (LDR)") };
		GpuProfileScope gpu(endCopyTimers[GameTexture ? 0 : 1], Device);
		if (Surf[Current] != GameTarget && !FinalWritten)
			Device->StretchRect(Surf[Current], NULL, GameTarget, NULL, D3DTEXF_NONE);
	}
	FinalEffect = nullptr;
	FinalWritten = false;
	Device->SetRenderTarget(0, GameTarget);
	TheTextureManager->RenderedTexture = SavedTexture;
	TheTextureManager->RenderedSurface = SavedSurface;
	if (GameTexture) { GameTexture->Release(); GameTexture = nullptr; }
	Tex[0] = Tex[1] = nullptr;
	Surf[0] = Surf[1] = nullptr;
	Active = false;
	GameTarget = nullptr;
}
