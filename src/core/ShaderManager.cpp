#include "OrthoProbe.h"	// UNOFFICIAL 2026-10-17 test tools (AfterCombine)
#include "CapturedNormals.h"	// UNOFFICIAL 2026-10-28 CapturedNormals (TESR_CapturedNormalsData)
#include "ObjectMotion.h"	// UNOFFICIAL 2026-10-29 ObjectMotion (TESR_ObjectMotionData)
#include "FlickerWatch.h"	// UNOFFICIAL 2026-10-23 test only
#include "GpuProfiler.h"
#include "HiZ.h"	// UNOFFICIAL 2026-10-26 (deep/fork-hiz)
#include "PixelInspector.h"	// UNOFFICIAL 2026-10-26 (debugging tool)
#include "PointShadowSlots.h"
#include "PointShadowForward.h"
#include "EffectPrecompile.h"	// UNOFFICIAL 2026-10-28 the effects compiled in parallel at startup
#include "LampFixtures.h"
#include "EffectSelfTest.h"
#include "../effects/GpuParticles.h"	// UNOFFICIAL 2026-10-26 (NVR DXVK 1.5-particles)
#include "SunDepthFit.h"	// UNOFFICIAL 2026-10-27 FitToDepth
#include "../effects/CoverMap.h"	// UNOFFICIAL 2026-10-27 Persistent wetness and snow
#include "RtCore.h"	// UNOFFICIAL 2026-10-28 (NVR DXVK) the ray tracing core
#include "RtSunShadows.h"	// UNOFFICIAL 2026-10-28 RT sun shadows
#include <unordered_map>	// UNOFFICIAL 2026-10-27 RenderScale v2 (RenderRect::RescaleRegistered)
#include <vector>

#define RESZ_CODE 0x7FA05000

/**
* Initializes the Shader Manager Singleton.
* The Shader Manager creates and holds onto the Effects activated in the Settings Manager, and sets the constants.
*/
static void LoadEarlyShaders(ShaderManager* Manager);	// UNOFFICIAL 2026-10-27 (below LoadShader)

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
	EffectPrecompile::Start();	// UNOFFICIAL 2026-10-28 ParallelShaderCompile: every effect prepared on worker threads; each LoadEffect waits for its own
	TheShaderManager->RegisterEffect<AvgLumaEffect>(&TheShaderManager->Effects.AvgLuma);
	TheShaderManager->RegisterEffect<SkyViewEffect>(&TheShaderManager->Effects.SkyView);
	TheShaderManager->RegisterEffect<AmbientLightEffect>(&TheShaderManager->Effects.AmbientLight);
	TheShaderManager->RegisterEffect<AmbientOcclusionEffect>(&TheShaderManager->Effects.AmbientOcclusion);
	TheShaderManager->RegisterEffect<SkinScatteringEffect>(&TheShaderManager->Effects.SkinScattering);	// UNOFFICIAL Conor's skin shader
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
	TheShaderManager->RegisterEffect<SunFarEffect>(&TheShaderManager->Effects.SunFar);	// UNOFFICIAL 2026-10-19
	TheShaderManager->RegisterEffect<SpecularEffect>(&TheShaderManager->Effects.Specular);
	TheShaderManager->RegisterEffect<SnowEffect>(&TheShaderManager->Effects.Snow);
	// UNOFFICIAL 2026-10-27 Persistent wetness and snow (CoverMap.h): the cover map WetWorld and SnowAccumulation read when their
	// Persistent setting is on. No settings section of its own: always on when its file loads, does nothing while both are off.
	TheShaderManager->RegisterEffect<CoverMapEffect>(&TheCoverMap);
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
	LoadEarlyShaders(TheShaderManager);	// UNOFFICIAL 2026-10-27: shaders the game made while the sets above did not exist yet
	
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
	TheShaderManager->RegisterConstant("TESR_ExtraLights", (D3DXVECTOR4*) &TheShaderManager->ExtraLights);	// UNOFFICIAL, GetNearbyLights
	TheShaderManager->RegisterConstant("TESR_SpotLightPosition", (D3DXVECTOR4*) &TheShaderManager->SpotLightPosition);
	TheShaderManager->RegisterConstant("TESR_SpotLightColor", (D3DXVECTOR4*) &TheShaderManager->SpotLightColor);
	TheShaderManager->RegisterConstant("TESR_SpotLightDirection", (D3DXVECTOR4*) &TheShaderManager->SpotLightDirection);
	TheShaderManager->RegisterConstant("TESR_SpotLightToWorldTransform", (D3DXVECTOR4*) &TheShaderManager->SpotLightWorldToLightMatrix[0]);
	TheShaderManager->RegisterConstant("TESR_VolumetricData", &TheShaderManager->VolumetricData);
	TheShaderManager->RegisterConstant("TESR_ViewSpaceLightDir", &TheShaderManager->ShaderConst.ViewSpaceLightDir);
	TheShaderManager->RegisterConstant("TESR_ScreenSpaceLightDir", &TheShaderManager->ShaderConst.ScreenSpaceLightDir);
	TheShaderManager->RegisterConstant("TESR_ReciprocalResolution", &TheShaderManager->ShaderConst.ReciprocalResolution);
	// UNOFFICIAL 2026-10-27 RenderScale (effects\TAAUpscale.cpp): xy = the internal resolution / the screen's while the world is drawn
	// smaller, else 1 (always registered: an unregistered TESR_ constant would read 0). The water shaders' refraction lookup uses it.
	TheShaderManager->RegisterConstant("TESR_RenderScale", &TAAEffect::RenderScaleConstant);
	TheShaderManager->RegisterConstant("TESR_CameraForward", &TheRenderManager->CameraForward);
	TheShaderManager->RegisterConstant("TESR_DepthConstants", &TheRenderManager->DepthConstants);
	TheShaderManager->RegisterConstant("TESR_CameraData", &TheRenderManager->CameraData);
	TheShaderManager->RegisterConstant("TESR_CameraPosition", &TheRenderManager->CameraPosition);
	TheShaderManager->RegisterConstant("TESR_SunDirection", &TheShaderManager->ShaderConst.SunDir);
	TheShaderManager->RegisterConstant("TESR_SunPosition", &TheShaderManager->ShaderConst.SunPosition);
	TheShaderManager->RegisterConstant("TESR_SunTiming", &TheShaderManager->ShaderConst.SunTiming);
	TheShaderManager->RegisterConstant("TESR_SunAmount", &TheShaderManager->ShaderConst.SunAmount);
	TheShaderManager->RegisterConstant("TESR_GameTime", &TheShaderManager->ShaderConst.GameTime);
	TheShaderManager->RegisterConstant("TESR_CapturedNormalsData", &CapturedNormals::Data);	// UNOFFICIAL 2026-10-28 CapturedNormals
	TheShaderManager->RegisterConstant("TESR_MaterialBufferData", &CapturedNormals::MaterialData);	// UNOFFICIAL 2026-10-29 MaterialBuffer
	TheShaderManager->RegisterConstant("TESR_ObjectMotionData", &ObjectMotion::Data);	// UNOFFICIAL 2026-10-29 ObjectMotion
	TheShaderManager->RegisterConstant("TESR_FogData", &TheShaderManager->ShaderConst.fogData);
	TheShaderManager->RegisterConstant("TESR_FogDistance", &TheShaderManager->ShaderConst.fogDistance);
	TheShaderManager->RegisterConstant("TESR_FogColor", &TheShaderManager->ShaderConst.fogColor);
	TerrainErosion::Initialize();   // [Shaders.Terrain.Erosion]: its constants
	TheShaderManager->RegisterConstant("TESR_SunColor", &TheShaderManager->ShaderConst.sunColor);
	TheShaderManager->RegisterConstant("TESR_SunDiskColor", &TheShaderManager->ShaderConst.sunDiskColor);
	TheShaderManager->RegisterConstant("TESR_SunAmbient", &TheShaderManager->ShaderConst.sunAmbient);
	TheShaderManager->RegisterConstant("TESR_SkyColor", &TheShaderManager->ShaderConst.skyColor);
	TheShaderManager->RegisterConstant("TESR_SkyLowColor", &TheShaderManager->ShaderConst.skyLowColor);
	TheShaderManager->RegisterConstant("TESR_HorizonColor", &TheShaderManager->ShaderConst.horizonColor);

	TheShaderManager->InitializeConstants();

	timer.LogTime("ShaderManager::Initialize");
}

void ShaderManager::CreateFrameVertex(UInt32 Width, UInt32 Height, IDirect3DVertexBuffer9** FrameVertex, float ScaleX, float ScaleY) {

	void* VertexData = NULL;
	float OffsetX = (1.0f / (float)Width) * 0.5f;
	float OffsetY = (1.0f / (float)Height) * 0.5f;

	// UNOFFICIAL 2026-10-27 RenderScale v2: ScaleX/Y below 1 = the rect quad (uv 0..Scale, same half-texel offset).
	FrameVS FrameVertices[] = {
		{ -1.0f,  1.0f, 1.0f, 0.0f + OffsetX, 0.0f + OffsetY },
		{ -1.0f, -1.0f, 1.0f, 0.0f + OffsetX, ScaleY + OffsetY },
		{  1.0f,  1.0f, 1.0f, ScaleX + OffsetX, 0.0f + OffsetY },
		{  1.0f, -1.0f, 1.0f, ScaleX + OffsetX, ScaleY + OffsetY }
	};
	TheRenderManager->device->CreateVertexBuffer(4 * sizeof(FrameVS), D3DUSAGE_WRITEONLY, FrameFVF, D3DPOOL_DEFAULT, FrameVertex, NULL);
	(*FrameVertex)->Lock(0, 0, &VertexData, NULL);
	memcpy(VertexData, FrameVertices, sizeof(FrameVertices));
	(*FrameVertex)->Unlock();

}

// UNOFFICIAL 2026-10-27 RenderScale v2 (core/RenderRect.h, docs/next/RENDERSCALE-V2.md).
bool RenderRect::ScreenShaped(unsigned w, unsigned h) {
	const unsigned sw = TheRenderManager->width, sh = TheRenderManager->height;
	if (!w || !h || !sw || !sh || w > sw || h > sh) return false;
	const float aspect = (float)w / (float)h, screen = (float)sw / (float)sh;
	return fabsf(aspect - screen) <= 0.01f * screen;
}

void RenderRect::ApplyViewport(IDirect3DDevice9* Device) {
	ShaderManager* sm = TheShaderManager;
	if (!On) return;	// off: nothing at all (the frame exactly as before)
	IDirect3DSurface9* target = nullptr;
	D3DSURFACE_DESC desc = {};
	const bool got = SUCCEEDED(Device->GetRenderTarget(0, &target)) && target && SUCCEEDED(target->GetDesc(&desc));
	if (target) target->Release();
	if (!got || !ScreenShaped(desc.Width, desc.Height)) {
		// a whole-target pass: only undo our own quad (a caller's own vertex buffer stays bound)
		IDirect3DVertexBuffer9* bound = nullptr;
		UINT offset = 0, stride = 0;
		if (SUCCEEDED(Device->GetStreamSource(0, &bound, &offset, &stride)) && bound) {
			if (bound == sm->FrameVertexRect) Device->SetStreamSource(0, sm->FrameVertex, 0, sizeof(FrameVS));
			bound->Release();
		}
		return;
	}
	if (!sm->FrameVertexRect || sm->FrameVertexRectScale[0] != ScaleX || sm->FrameVertexRectScale[1] != ScaleY) {
		if (sm->FrameVertexRect) { sm->FrameVertexRect->Release(); sm->FrameVertexRect = nullptr; }
		sm->CreateFrameVertex(TheRenderManager->width, TheRenderManager->height, &sm->FrameVertexRect, ScaleX, ScaleY);
		sm->FrameVertexRectScale[0] = ScaleX;
		sm->FrameVertexRectScale[1] = ScaleY;
	}
	const D3DVIEWPORT9 port = { 0, 0, Width(desc.Width), Height(desc.Height), 0.0f, 1.0f };
	Device->SetViewport(&port);
	Device->SetStreamSource(0, sm->FrameVertexRect ? sm->FrameVertexRect : sm->FrameVertex, 0, sizeof(FrameVS));
}

void RenderRect::WholeViewport(IDirect3DDevice9* Device, D3DVIEWPORT9& Viewport) {
	if (!On || Viewport.X || Viewport.Y) return;
	IDirect3DSurface9* target = nullptr;
	D3DSURFACE_DESC desc = {};
	const bool got = SUCCEEDED(Device->GetRenderTarget(0, &target)) && target && SUCCEEDED(target->GetDesc(&desc));
	if (target) target->Release();
	if (got && ScreenShaped(desc.Width, desc.Height) && Viewport.Width == Width(desc.Width) && Viewport.Height == Height(desc.Height)) {
		Viewport.Width = desc.Width;
		Viewport.Height = desc.Height;
	}
}

namespace {
	std::vector<IDirect3DSurface9*> RescaleList;	// this rect frame's buffers read after the upscale (RenderRect::RescaleAfterUpscale)
	struct RescaleTemp { IDirect3DTexture9* Texture = nullptr; IDirect3DSurface9* Surface = nullptr; D3DSURFACE_DESC Desc = {}; };
	std::unordered_map<IDirect3DSurface9*, RescaleTemp> RescaleTemps;	// one spare of the same size and format per buffer
}

void RenderRect::RescaleAfterUpscale(IDirect3DSurface9* Surface) {
	if (!On || !Surface || std::find(RescaleList.begin(), RescaleList.end(), Surface) != RescaleList.end()) return;
	RescaleList.push_back(Surface);
}

void RenderRect::RescaleRegistered(IDirect3DDevice9* Device) {
	for (IDirect3DSurface9* buffer : RescaleList) {
		D3DSURFACE_DESC desc = {};
		if (FAILED(buffer->GetDesc(&desc))) continue;
		RescaleTemp& spare = RescaleTemps[buffer];
		if (!spare.Surface || spare.Desc.Width != desc.Width || spare.Desc.Height != desc.Height || spare.Desc.Format != desc.Format) {
			if (spare.Surface) spare.Surface->Release();
			if (spare.Texture) spare.Texture->Release();
			spare = RescaleTemp();
			if (FAILED(Device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &spare.Texture, nullptr)) ||
				FAILED(spare.Texture->GetSurfaceLevel(0, &spare.Surface))) {
				if (spare.Texture) spare.Texture->Release();
				spare = RescaleTemp();
				static bool logged = false;
				if (!logged) Logger::Log("[ERROR] UNOFFICIAL RenderScale v2: no spare to rescale a %ux%u buffer after the upscale (read at the wrong size).", desc.Width, desc.Height);
				logged = true;
				continue;
			}
			spare.Desc = desc;
		}
		const RECT drawn = { 0, 0, (LONG)(std::max)(1.0f, floorf((float)desc.Width * LastScaleX + 0.5f)), (LONG)(std::max)(1.0f, floorf((float)desc.Height * LastScaleY + 0.5f)) };
		if (SUCCEEDED(Device->StretchRect(buffer, &drawn, spare.Surface, nullptr, D3DTEXF_LINEAR)))
			Device->StretchRect(spare.Surface, nullptr, buffer, nullptr, D3DTEXF_NONE);
	}
	RescaleList.clear();
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
void NvrMsaaWarningTick();	// Hooks/Settings.cpp (UNOFFICIAL 2026-10-27): the game's MSAA is on: said once on screen

void ShaderManager::UpdateConstants() {

	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // Main toggle

	auto timer = TimeLogger();
	MarkShadersFrom = TheSettingManager->GetSettingI("Shaders.PBR.Main", "MarkShadersFrom");	// UNOFFICIAL 2026-10-22 MarkShaders (test only)
	MarkShadersTo = TheSettingManager->GetSettingI("Shaders.PBR.Main", "MarkShadersTo");
	MarkShadersFamily = TheSettingManager->GetSettingI("Shaders.PBR.Main", "MarkShadersFamily");
	MarkShadersVanilla = TheSettingManager->GetSettingI("Shaders.PBR.Main", "MarkShadersVanilla");
	MarkShadersVsFrom = TheSettingManager->GetSettingI("Shaders.PBR.Main", "MarkShadersVsFrom");
	MarkShadersVsTo = TheSettingManager->GetSettingI("Shaders.PBR.Main", "MarkShadersVsTo");
	GameShaderEyes = TheSettingManager->GetSettingI("Shaders.PBR.Main", "GameShaderEyes") != 0;	// UNOFFICIAL 2026-10-27 (Render.cpp SetShadersHook)
	NvrMsaaWarningTick();
	// MarkShadersVanilla 2 (test only, once): the game's lamp falloff textures (BSShaderManager::spAttenuationXY 0x011F9534 / Z 0x011F9538)
	// saved next to the log as PNG, to compare their shape with the shaders' 1 - (d / r)^2.
	if (MarkShadersVanilla == 2) {
		static bool dumped = false;
		if (!dumped) {
			dumped = true;
			const UInt32 addrs[2] = { 0x011F9534, 0x011F9538 };
			for (int i = 0; i < 2; i++) {
				NiTexture* tex = *(NiTexture**)addrs[i];
				IDirect3DBaseTexture9* d3d = (tex && tex->rendererData) ? tex->rendererData->dTexture : nullptr;
				char path[MAX_PATH];
				sprintf_s(path, ".\\NVR-Unofficial-Optimized-Logs\\NVR-attenuation-%s.png", i ? "Z" : "XY");
				HRESULT hr = d3d ? D3DXSaveTextureToFileA(path, D3DXIFF_PNG, d3d, NULL) : E_FAIL;
				D3DSURFACE_DESC desc = {};
				if (d3d && d3d->GetType() == D3DRTYPE_TEXTURE) ((IDirect3DTexture9*)d3d)->GetLevelDesc(0, &desc);
				Logger::Log("MarkShaders: attenuation %s texture %p d3d %p type %d %ux%u format %d saved %s (hr %08X)", i ? "Z" : "XY", tex, d3d, d3d ? (int)d3d->GetType() : -1, desc.Width, desc.Height, (int)desc.Format, path, (unsigned)hr);
			}
		}
	}

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
	orthoSkyRequired = false; // UNOFFICIAL Skylighting v2 (AmbientOcclusionEffect::UpdateConstants)

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
	// fixes-a #43: UpdateConstants runs twice a rendered frame; the second call used to see the cell already stored and clear the
	// flag, so every reader after it (TAA's history reset, AmbientLight, BounceLight, clouds) never saw a change. The change is
	// worked out once per frame (FrameNumber) and held for the whole frame; the one-time work below still runs once.
	static unsigned int cellFrame = ~0u;
	static bool cellChangedThisFrame = false;
	// UNOFFICIAL 2026-10-27 (user, FSR's RenderScale: "the effects jitter a bit occasionally when walking around ... vol clouds drop to a
	// really low res pixelated version for a few frames"; their log: every jump at "cellchg"): walking over an exterior cell border
	// changes the cell too, and every frame-to-frame history (TAA, FSR, AmbientLight, BounceLight, the clouds...) took it for a
	// teleport and started again. isCameraCut is a cell change that really is a jump: into or out of an interior, between interiors,
	// or the camera moved more than 512 units in one frame (fast travel, coc). The histories reset on that; isCellChanged stays for
	// what is about the cell itself (presets, the interior balance).
	static bool cameraCutThisFrame = false;
	static D3DXVECTOR4 cutCamera(0.0f, 0.0f, 0.0f, 0.0f);
	static bool cutInterior = false, cutKnown = false;
	const bool firstCallOfFrame = TheFrameRateManager->FrameNumber != cellFrame;
	if (firstCallOfFrame) {
		cellFrame = TheFrameRateManager->FrameNumber;
		cellChangedThisFrame = currentCell != PreviousCell;
		PreviousCell = currentCell;
		const bool interior = currentCell->IsInterior();
		const D3DXVECTOR4 camera = TheRenderManager->CameraPosition;
		const float dx = camera.x - cutCamera.x, dy = camera.y - cutCamera.y, dz = camera.z - cutCamera.z;
		cameraCutThisFrame = cellChangedThisFrame && (!cutKnown || interior || cutInterior || dx * dx + dy * dy + dz * dz > 512.0f * 512.0f);
		cutCamera = camera;
		cutInterior = interior;
		cutKnown = true;
	}
	GameState.isCellChanged = cellChangedThisFrame;
	GameState.isCameraCut = cameraCutThisFrame;
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
			PresetManager::ResolveAndApply(currentCell, true);	// UNOFFICIAL 2026-10-19: automatic (keeps live edits, see PresetManager.h)
	}

	GameState.isUnderwater = Tes->sky->GetIsUnderWater();
	GameState.isRainy = currentWeather?currentWeather->GetWeatherType() == TESWeather::WeatherType::kType_Rainy : false;
	GameState.isSnow = currentWeather?currentWeather->GetWeatherType() == TESWeather::WeatherType::kType_Snow : false;
	// UNOFFICIAL 2026-10-19: [Shaders.Snow.Main] ForceSnow -- the snow effects act as if it snowed (no snow weathers in the game or
	// most load orders, so macintroll's snow shaders could not be seen otherwise)
	if (!GameState.isSnow && TheSettingManager->GetSettingI("Shaders.Snow.Main", "ForceSnow")) { GameState.isSnow = true; GameState.isRainy = false; }
	// UNOFFICIAL 2026-10-25: [Shaders.Precipitations.Main] ForceRain, the same for rain (FalloutNV.esm has no rainy weather at all)
	else if (!GameState.isRainy && TheSettingManager->GetSettingI("Shaders.Precipitations.Main", "ForceRain")) GameState.isRainy = true;
	// UNOFFICIAL 2026-10-26 GPU particles (NVR DXVK 1.5-particles): their drops land on the ortho map's roofs and ground
	if (GpuParticles::Wanted()) orthoRequired = true;
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
	Shaders.Sky->UpdateMoonlight();	// UNOFFICIAL 2026-10-25 MoonBrightness: the night light (and sunColor) scaled; the sky's own colours keep the game's

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
	Shaders.Sky->UpdateFogSky();	// UNOFFICIAL 2026-10-27 VolumetricFog's copies of the three sky colours with the sky look in them

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
	// UNOFFICIAL 2026-10-20 (skyamb): Skylighting's constants while AmbientOcclusion is off too: AmbientLight hosts the sky test then
	// (AmbientOcclusion.h UpdateSkyConstants; with AmbientLight off as well the sky stays off, as before).
	if (!Effects.AmbientOcclusion->Enabled) Effects.AmbientOcclusion->UpdateSkyConstants();

	// The skin, hair and grass shaders route their light and ambient terms through
	// TESR_PBRData (see Shaders/Includes/PBRScale.hlsl) and render black at a zero scale, so
	// these constants stay current whether or not the PBR collection is enabled.
	if (!Shaders.PBR->Enabled) Shaders.PBR->UpdateConstants();

	// UNOFFICIAL 2026-10-18 sandport: the grass and leaf shaders read the sandstorm's wind whether or not it is on: off (or not
	// loaded), there is no storm, and they sway exactly as the game's.
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
	// UNOFFICIAL lit particles and blood decals: only NOLIGHT016/017.vso, NOLIGHTTEXVC.pso and GDECAL(S) have
	// replacements on disk; the rest of the NOLIGHT family resolves to no file and stays the game's.
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

// UNOFFICIAL 2026-10-27 (user: still no grass shadows; from the 20:10 launch on, NVR's grass shaders were never loaded and the log said
// nothing): a shader the game creates while ShaderManager::Initialize is still registering the shader sets found its set missing and
// was silently left to the game. Those are kept here and loaded right after the sets exist (LoadEarlyShaders), before anything draws.
static bool ShaderSetsReady = false;
static std::vector<NiD3DVertexShader*> EarlyVertexShaders;
static std::vector<NiD3DPixelShader*> EarlyPixelShaders;

static void LoadEarlyShaders(ShaderManager* Manager) {
	ShaderSetsReady = true;
	if (EarlyVertexShaders.empty() && EarlyPixelShaders.empty()) return;
	Logger::Log("UNOFFICIAL shader sets ready: loading %u vertex and %u pixel shaders the game made before them",
		(unsigned)EarlyVertexShaders.size(), (unsigned)EarlyPixelShaders.size());
	for (NiD3DVertexShader* s : EarlyVertexShaders) Manager->LoadShader(s);
	for (NiD3DPixelShader* s : EarlyPixelShaders) Manager->LoadShader(s);
	EarlyVertexShaders.clear();
	EarlyPixelShaders.clear();
}

/*
* Load generic Vertex Shaders as well as the ones for interiors and exteriors if the exist. 
* Returns false if generic one isn't found (as other ones are optional)
*/
bool ShaderManager::LoadShader(NiD3DVertexShader* Shader) {
	
	NiD3DVertexShaderEx* VertexShader = (NiD3DVertexShaderEx*)Shader;
	if (!ShaderSetsReady) {	// (UNOFFICIAL 2026-10-27, see LoadEarlyShaders)
		VertexShader->ShaderProg[ShaderRecordType::Default] = NULL;
		VertexShader->ShaderProg[ShaderRecordType::Exterior] = NULL;
		VertexShader->ShaderProg[ShaderRecordType::Interior] = NULL;
		VertexShader->Enabled = false;
		EarlyVertexShaders.push_back(Shader);
		Logger::Log("UNOFFICIAL shader %s made before NVR's shader sets: loaded once they exist", VertexShader->Name ? VertexShader->Name : "?");
		return false;
	}
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
	if (!ShaderSetsReady) {	// (UNOFFICIAL 2026-10-27, see LoadEarlyShaders)
		PixelShader->ShaderProg[ShaderRecordType::Default] = NULL;
		PixelShader->ShaderProg[ShaderRecordType::Exterior] = NULL;
		PixelShader->ShaderProg[ShaderRecordType::Interior] = NULL;
		PixelShader->Enabled = false;
		EarlyPixelShaders.push_back(Shader);
		Logger::Log("UNOFFICIAL shader %s made before NVR's shader sets: loaded once they exist", PixelShader->Name ? PixelShader->Name : "?");
		return false;
	}
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
	// UNOFFICIAL interior forward point-light shadows (PointShadowForward.h): the lit pixel shaders get an Interior record
	// from their own source with INTERIOR_SHADOWS, the sun's code compiled out (it never runs indoors). Only with the
	// setting on at startup; exteriors keep their records.
	if (!PixelShader->ShaderProg[ShaderRecordType::Interior] && PixelShader->ShaderProg[ShaderRecordType::Default] &&
		PointShadowForward::Eligible(PixelShader->Name, Template) &&
		TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "Enabled")) {
		// UNOFFICIAL 2026-10-20 (P82 tester): when the GPU refuses a record (too long), shorter ones in turn (PointShadowForward.hlsl
		// POINT_SHADOW_FIT, each with its own cache folder; the first is the old one exactly, also what ShaderPrecompile makes), and
		// PointShadowForward::FitLevel / Refused keep every object to what the shortest one loaded can do.
		static D3DXMACRO overrides[4][7] = {
			{ { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" }, { "CONTACT_HARDENING", "0" }, { NULL, NULL } },
			{ { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" }, { "CONTACT_HARDENING", "0" }, { "POINT_SHADOW_FIT", "1" }, { NULL, NULL } },
			{ { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" }, { "CONTACT_HARDENING", "0" }, { "POINT_SHADOW_FIT", "2" }, { NULL, NULL } },
			{ { "INTERIOR_SHADOWS", "1" }, { "FORWARD_SHADOWS", "0" }, { "SUN_CROSSFADE", "0" }, { "CONTACT_HARDENING", "0" }, { "POINT_SHADOW_FIT", "3" }, { NULL, NULL } } };
		// UNOFFICIAL 2026-10-21 LampFalloff (Includes/LampFalloff.hlsl): these records draw indoors only, so without [Shaders.PBR.LampFalloff]
		// Interior they get no lamp shape (LAMP_FALLOFF_ON 1 = MatchEverywhere only, 0 = nothing): the six-lamp shaders stay as long as they
		// were (SLS2035: 3592 slots with the user's settings; the shape takes it to ~3700, past what the GPU takes, and the next level).
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
	std::multimap<float, ShadowSceneLight*> SceneLights;	// UNOFFICIAL: a multimap (two lamps with the same key used to drop one)
	NiTList<ShadowSceneLight>::Entry* Entry = SceneNode->lights.start;
	ShadowPool::BeginRanking();	// UNOFFICIAL 2026-10-29 ShadowPool: the shadow-casting lamps the live slots have no room for, in order

	ShadowsExteriorEffect::InteriorsStruct* Settings = &Effects.ShadowsExteriors->Settings.Interiors;
	ShadowsExteriorEffect::ShadowStruct* ShadowsConstants = &Effects.ShadowsExteriors->Constants;

	// The lights given cube-map slots last frame (filled at the end). UNOFFICIAL, with forward shadows on
	// ([Shaders.ShadowsInteriors.Forward] LampSwitchMargin): such a lamp keeps its shadow unless another is clearly nearer,
	// and stays while somewhat behind you. Lamps at the edge of the LightPoints cap or of the "in front" test otherwise
	// switch in and out as you move and turn, and their shadows pop on and off.
	static const void* previousSlots[ShadowCubeMapsMax] = {};
	const float switchMargin = Settings->Forward.Enabled ? Settings->Forward.LampSwitchMargin : 0.0f;	// (read on change, see ShadowsExteriorEffect::UpdateSettings)
	auto wasCaster = [&](const ShadowSceneLight* light) {
		if (!(switchMargin > 0.0f)) return false;
		for (int s = 0; s < ShadowCubeMapsMax; s++) if (previousSlots[s] == light) return true;
		return false;
	};

	// UNOFFICIAL, with forward shadows on: a lamp counts when its light reaches what the camera sees -- its sphere against
	// the view frustum's four sides and the plane through the eye (not the far plane), from the camera. The old test, "in
	// front of the player", changed as you turned and dropped lamps beside or behind you whose light (and shadows) fell
	// on what you look at: their shadows came and went with the view angle.
	const bool viewTest = Settings->Forward.Enabled;
	// UNOFFICIAL CameraIndependent (2026-10-08, with forward shadows on): no view test at all -- every lamp in range competes
	// by its rank round the player. A busy room has far more lamps than cube maps (the Tops: 50-70 for 11; LogLamps showed
	// most swaps were 'its light does not reach the view'), so with the view test turning the camera swapped lamps in and
	// out constantly and their shadows popped at the edges of the screen. Now only walking changes them (LampSwitchMargin).
	const bool cameraIndependent = viewTest && Settings->Forward.CameraIndependent;
	// UNOFFICIAL FillLightRadius (2026-10-08, with forward shadows on): a lamp reaching farther than this is a level's big fill
	// light (the Tops: reach 500-980 against 180-420 for its real lamps, often dimmer x 1.25-1.9), hidden up in the ceiling to
	// brighten a whole area. Its cube map saw the ceiling round it, so with a shadow all its light was taken off every floor
	// piece it lights (dark rectangles along the floor's mesh pieces); it also took cube maps from real lamps (its rank is
	// high). It still lights as before, without a shadow, and still counts as a tracked lamp for the darkening.
	const float fillLightRadius = viewTest ? Settings->Forward.FillLightRadius : 0.0f;
	const D3DXMATRIX& camera = TheRenderManager->InvViewMatrix;
	const D3DXVECTOR3 camRight(camera._11, camera._12, camera._13), camUp(camera._21, camera._22, camera._23), camForward(camera._31, camera._32, camera._33);
	const float tanX = fabsf(TheRenderManager->InvProjMatrix._11), tanY = fabsf(TheRenderManager->InvProjMatrix._22);
	const float normX = 1.0f / sqrtf(1.0f + tanX * tanX), normY = 1.0f / sqrtf(1.0f + tanY * tanY);
	// UNOFFICIAL LampRanking 1 (with forward shadows on): lamps ranked by what they light of the scene -- brightness x how far
	// their light reaches / distance -- instead of nearest first. Many small bulbs near you used to push a big room lamp
	// out of the LightPoints cap as you strafed, and its shadows vanished.
	const int lampRanking = viewTest ? Settings->Forward.LampRanking : 0;
	// UNOFFICIAL LogLamps: every lamp that gains or loses its shadow is logged with the reason (diagnostic).
	const bool logLamps = Settings->Forward.LogLamps;
	struct LampNote { const ShadowSceneLight* light; int reason; float distance, key, radius; D3DXVECTOR4 position, colour; int carry; };
	static LampNote notes[128];
	int noteCount = 0;
	auto note = [&](const ShadowSceneLight* light, int reason, float distance, float key, NiPointLight* source) {
		if (!logLamps || noteCount >= 128 || !source) return;
		notes[noteCount++] = { light, reason, distance, key, source->Spec.r, source->m_worldTransform.pos.toD3DXVEC4(),
			D3DXVECTOR4(source->Diff.r, source->Diff.g, source->Diff.b, source->Dimmer), (int)source->CanCarry };
	};
	// UNOFFICIAL InvisibleLightShadowStrength (2026-10-20, with forward shadows on, indoors): a light with no glowing lamp
	// within FixtureDistance (LampFixtures.h: the level designers' invisible fill and key lights, most of New Vegas's interior
	// lights) casts that share of a shadow; at 0 none, and it leaves its cube map to lamps that are really there (it still
	// lights as before and still counts for the darkening, as a fill light does). LogLamps lists every light's answer once.
	const bool indoorsForward = viewTest && !GameState.isExterior;
	const float invisibleStrength = indoorsForward ? Settings->Forward.InvisibleLightShadowStrength : 1.0f;
	const bool classifyLamps = indoorsForward && (invisibleStrength < 1.0f || Settings->Forward.InvisibleLightSize > 0.0f ||
		!Settings->Forward.LampModelShadows || Settings->Forward.InvisibleShadowFromLamp > 0.0f || !LampFixtures::NoShadowWords.empty() || logLamps ||
		(Settings->Forward.ShadowClusterEnabled && Settings->Forward.ShadowClusterInvisibleOnly));	// (they need the cell's lamps listed)
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
		// UNOFFICIAL 2026-10-16 (bug hunt): an entry without a light (the loop below checks the same) was dereferenced.
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
		// (A lamp that had a shadow last frame keeps counting a little beyond the edge of the view: LampSwitchMargin.)
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
	memset(ExtraLights, 0, sizeof(ExtraLights));	// UNOFFICIAL: the mask's extra lamps, filled below with forward shadows

	// ShadowManager::RenderShadowMaps only renders cubemaps for the first LightPoints slots.
	// Filling past that gives the shader a live position and colour for a face that is never
	// redrawn, so it samples whatever that cubemap last held -- a shadow frozen from an earlier
	// frame or cell. Lights beyond the cap fall through to the non-shadowing tracked list.
	// (UNOFFICIAL: with forward shadows indoors, up to ShadowCubeMapsMax -- PointShadowForward::ShadowedSlots)
	const bool allSlots = PointShadowForward::UsesAllSlots(TheShaderManager->GameState.isExterior);
	// UNOFFICIAL ExteriorLightPoints: outdoors above 12, that many lamps get shadows, every slot sampled (in batches).
	const bool batched = PointShadowForward::ExteriorBatched(TheShaderManager->GameState.isExterior);
	const int ShadowLightsMax = batched ? PointShadowForward::ShadowedSlots(true) : min(Settings->LightPoints, allSlots ? (int)ShadowCubeMapsMax : (int)ShadowCubeMapsScreen);
	memset(BatchLightPosition, 0, sizeof(BatchLightPosition));
	memset(BatchLightColor, 0, sizeof(BatchLightColor));

	// get the data for all tracked lights
	int ShadowIndex = 0;
	int LightIndex = 0;
	TheShadowManager->PointLightsNum = 0;
	ShadowSceneLight* ShadowCasters[ShadowCubeMapsMax] = { NULL }; // shadow casting lights, nearest first

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

	// UNOFFICIAL 2026-10-26 ShadowClusterEnabled ([Shaders.ShadowsInteriors.Forward]): indoors, lamps closer together than
	// ShadowClusterDistance share one shadow. The brightest lamp of each group keeps its shadow; the others light exactly as
	// before but without a shadow of their own, so a room of many bulbs no longer runs out of shadow slots. Off: as before.
	std::vector<const void*> clusterMembers;
	if (Settings->Forward.ShadowClusterEnabled && !TheShaderManager->GameState.isExterior && Settings->Forward.ShadowClusterDistance > 0.0f) {
		struct Cand { const void* key; NiPointLight* light; float brightness; };
		std::vector<Cand> cands;
		for (auto& e : SceneLights) {
			NiPointLight* L = e.second->sourceLight;
			if (!L || L->EffectType != NiDynamicEffect::EffectTypes::POINT_LIGHT || L->Spec.r * Settings->LightRadiusMult <= 10) continue;
			// A lamp switched off for the main view (+0x110 = 0xFF: the game's view test, or FNV-Occlusion-Culling's bLampCulling
			// when its whole reach is covered) is never a group's shadow: the next-brightest lit lamp near it takes the group.
			if (e.second->bIsEnabled == 0xFF) continue;
			cands.push_back({ e.second, L, (L->Diff.r + L->Diff.g + L->Diff.b) * L->Dimmer });
		}
		std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.brightness > b.brightness; });
		const float d2max = Settings->Forward.ShadowClusterDistance * Settings->Forward.ShadowClusterDistance;
		// UNOFFICIAL 2026-10-29 (user: "not all invisible lights just ones where there's a lot in one place. vault 21 like 900 lights"):
		// ShadowClusterInvisibleOnly: a lamp that is really there (a glowing part within FixtureDistance, LampFixtures) keeps its own
		// shadow; only the level designers' invisible fill lights join a neighbour's. ShadowClusterMinLights: and only where at least that
		// many lights (itself included) stand within ShadowClusterDistance, so rooms with a few lights keep every shadow. 0 / off: as before.
		const bool invisibleOnly = Settings->Forward.ShadowClusterInvisibleOnly;
		const int minLights = Settings->Forward.ShadowClusterMinLights;
		std::vector<bool> mayJoin(cands.size(), true);
		for (size_t m = 0; m < cands.size(); m++) {
			if (invisibleOnly && !LampFixtures::Invisible(cands[m].light, Settings->Forward.FixtureDistance)) { mayJoin[m] = false; continue; }
			if (minLights > 1) {
				const NiPoint3& mp = cands[m].light->m_worldTransform.pos;
				int neighbours = 0;
				for (size_t o = 0; o < cands.size() && neighbours < minLights; o++) {
					const NiPoint3& op = cands[o].light->m_worldTransform.pos;
					const float dx = op.x - mp.x, dy = op.y - mp.y, dz = op.z - mp.z;
					if (dx * dx + dy * dy + dz * dz < d2max) neighbours++;
				}
				if (neighbours < minLights) mayJoin[m] = false;
			}
		}
		std::vector<bool> taken(cands.size(), false);
		for (size_t r = 0; r < cands.size(); r++) {
			if (taken[r]) continue;	// r is a representative: it takes every untaken lamp near it
			const NiPoint3& rp = cands[r].light->m_worldTransform.pos;
			for (size_t m = r + 1; m < cands.size(); m++) {
				if (taken[m] || !mayJoin[m]) continue;
				const NiPoint3& mp = cands[m].light->m_worldTransform.pos;
				const float dx = mp.x - rp.x, dy = mp.y - rp.y, dz = mp.z - rp.z;
				if (dx * dx + dy * dy + dz * dz < d2max) { taken[m] = true; clusterMembers.push_back(cands[m].key); }
			}
		}
		static int logCountdown = 0;
		if (Settings->Forward.LogLamps && --logCountdown <= 0) {
			logCountdown = 300;
			Logger::Log("ShadowCluster: %u candidate lamps, %u share a brighter neighbour's shadow (%u groups), distance %.0f",
				(unsigned)cands.size(), (unsigned)clusterMembers.size(), (unsigned)(cands.size() - clusterMembers.size()), Settings->Forward.ShadowClusterDistance);
		}
	}

	auto v = SceneLights.begin();
	for (int i = 0; i < TrackedLightsMax + ShadowCubeMapsMax; i++) {
		// set null values if we reached the end of lights in the scene and current index is lower than max amount
		if (v == SceneLights.end()) {
			// (Unused shadow slots stay cleared: they are assigned after this loop.)
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
			if (fillLightRadius > 0.0f && Light->Spec.r > fillLightRadius) {	// UNOFFICIAL FillLightRadius, see above
				// (FillLightShadowStrength above 0: it keeps a shadow, that much of a full one: PointShadowForward::Publish)
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

			if (classifyLamps && CastShadow) {	// UNOFFICIAL InvisibleLightShadowStrength, see above
				const bool invisible = LampFixtures::Invisible(Light, Settings->Forward.FixtureDistance);
				if (invisible) CastShadow = invisibleStrength > 0.0f;
				static std::unordered_map<const void*, unsigned> loggedKind;	// light -> glow list generation it was logged for
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

			if (CastShadow && !clusterMembers.empty() && std::find(clusterMembers.begin(), clusterMembers.end(), (const void*)v->second) != clusterMembers.end())
				CastShadow = false;	// UNOFFICIAL ShadowClusterEnabled: its group's brightest lamp casts the shadow

			if (CastShadow && ShadowIndex >= ShadowLightsMax && radius > 10) ShadowPool::NoteCandidate(v->second);	// UNOFFICIAL 2026-10-29 ShadowPool
			if (CastShadow && ShadowIndex < ShadowLightsMax && radius > 10) {
				// add found light to the ranked list of lights that cast shadows (slots are assigned below)
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

	// Give every caster a cubemap slot, each light keeping the slot it had last frame (PointShadowSlots.h): a light that
	// only changed rank no longer forces its cubemap to be redrawn. The slot order does not change the image, except for
	// the last slot, which PointShadows.fx lights without a shadow lookup: it keeps the farthest caster, as the plain
	// distance order gave it, and only slots 0..10 take part in the stable assignment.
	{
		const int casters = TheShadowManager->PointLightsNum;
		const int sampledSlots = allSlots || batched ? ShadowLightsMax : min(ShadowLightsMax, (int)ShadowCubeMapsSampled);
		const int stableCasters = min(casters, sampledSlots);
		const void* ranked[ShadowCubeMapsMax] = {};
		const void* assigned[ShadowCubeMapsMax] = {};
		for (int r = 0; r < stableCasters; r++) ranked[r] = ShadowCasters[r];
		AssignStablePointShadowSlots(previousSlots, ranked, stableCasters, sampledSlots, assigned);
		// LogLamps: the lamps that gained or lost their shadow since last frame, and why. Only lamps in this frame's notes are
		// read (a lamp gone from the game's list may be freed: only its address is printed).
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
			if (!shadowLight) continue; // position and colour stay cleared
			NiPointLight* Light = shadowLight->sourceLight;
			D3DXVECTOR4 LightPos = Light->m_worldTransform.pos.toD3DXVEC4();
			LightPos.w = Light->Spec.r * Settings->LightRadiusMult;
			BatchLightPosition[s] = LightPos;
			BatchLightColor[s] = D3DXVECTOR4(Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer);
			if (s >= ShadowCubeMapsScreen) continue;	// (the screen-space passes' arrays end at 12; see below)
			if (batched && s == ShadowCubeMapsScreen - 1) continue;	// (batched: slot 11 is the second batch's, with its shadow)
			ShadowsConstants->ShadowLightPosition[s] = LightPos;
			LightColor[s] = D3DXVECTOR4(Light->Diff.r, Light->Diff.g, Light->Diff.b, Light->Dimmer);
		}
		// UNOFFICIAL (2026-10-08): the darkening mask (KeepDarkening, PointShadows.fx) knows 12 shadow lamps and 12 tracked
		// ones. Lamps in the forward slots past 12 go to the front of the tracked list, so what they light is not darkened as
		// unlit; what no longer fits there (forward slots past 24, the tracked lamps they push off) goes to the extra list,
		// which the mask reads too (TESR_ExtraLights, the next ExtraLightsMax: the mask then knows the 40 nearest lamps). One
		// register per lamp: w packs the radius (whole units) and the weight the mask gives it, luma(colour) x dimmer.
		if (allSlots && sampledSlots > ShadowCubeMapsScreen) {
			NiPointLight* keepLight[TrackedLightsMax] = {};
			D3DXVECTOR4 keepPos[TrackedLightsMax], keepColour[TrackedLightsMax];
			int kept = 0, extra = 0;
			auto take = [&](NiPointLight* light, const D3DXVECTOR4& pos, const D3DXVECTOR4& colour) {
				if (kept < TrackedLightsMax) { keepLight[kept] = light; keepPos[kept] = pos; keepColour[kept] = colour; kept++; }
				else if (extra < ExtraLightsMax && pos.w >= 1.0f) {
					const float weight = (0.2126f * colour.x + 0.7152f * colour.y + 0.0722f * colour.z) * colour.w;	// Helpers.hlsl luma
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


// UNOFFICIAL ExteriorLightPoints (2026-10-13): the screen-space point-shadow pass reads 11 shadowed lamps at once (ps_3_0 has 16
// samplers), so outdoors past those the shadow slots are drawn in batches of 11 more: the slots' cube maps put in the first
// 11 cube samplers' places (the samplers read through these pointers at each bind) and their lamps in the first 11 entries,
// then the pass's AddShadowedLamps technique adds their light to the buffer (blend one, one). The sum is saturated where it
// is read, as the single pass saturated its own; the lamps without shadows and the flashlight are in the first batch only.
void ShaderManager::RenderPointShadowBatches() {
	ShadowsExteriorEffect* shadows = Effects.ShadowsExteriors;
	const int slots = PointShadowForward::ShadowedSlots(true);
	const int batchSize = ShadowCubeMapsSampled;	// 11
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
			shadows->Textures.ShadowCubeMapTexture[k] = live ? cube : savedCube[k];	// (an unused entry keeps a valid cube bound)
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
float LampContactStrength();	// UNOFFICIAL 2026-10-16 (effects\ShadowsExterior.cpp)
bool LampMaskHalfRes();	// UNOFFICIAL 2026-10-16 MaskHalfRes (effects\ShadowsExterior.cpp)
IDirect3DTexture9* LampMaskHalfTexture();
IDirect3DSurface9* LampMaskHalfSurface();

/*
* UNOFFICIAL 2026-10-18 LampMaskScissor (contribution audit, work\bench\deep-roadmap\evidence\contrib-LOG.md). The lamp mask
* (PointShadows.fx Shadow(), technique MergedPointShadows) is the sum of the lamps' light at each pixel's reconstructed position
* world_pos = TESR_CameraPosition + toWorld(uv) * depth. Every lamp term is exactly 0 where that position is not inside the
* lamp's sphere (its radius = the .w the shader divides by): a shadowed slot returns 0 when !(distance < 1) (w 0 included: the
* distance is then INF or NaN), slot 11 and the tracked lamps go through GetPointLightAtten / LampReach, both exactly 0 from
* distance 1 on, the flashlight's attenuation is saturate((1 - s)^2 / (1 + 5 s)) = 0 at s = 1, and saturate(0) = 0. Since
* world_pos lies on the pixel's own view ray (toWorld uses the view matrix's axes and 1 / P00, 1 / P11, exactly what is read
* here), a pixel whose ray misses every lamp's sphere is written 0. So: the screen rectangle holding the projections of the lamps'
* spheres (each sphere's world-aligned bounding box, widened by 2 % + 2 units, its 8 corners projected, +-1 pixel) is the only
* place the pass can write anything but 0. No lamp (or none on screen): the target is just cleared (the pass's own first step);
* otherwise cleared, then the pass drawn with the scissor test on that rectangle. A box reaching behind the camera, a lamp list
* this does not cover (the extra lamps), an unusual radius or camera: the whole pass, as before.
* Returns 0 = draw as before, 1 = nothing to draw (clear only), 2 = draw inside 'rect'.
*/
static int LampMaskRect(RECT& rect, int& lampCount) {
	lampCount = 0;
	if (TheShaderManager->ExtraLights[0].w != 0.0f) return 0;	// (the extra lamps: forward shadows indoors only; not covered)
	D3DXVECTOR4 lamps[ShadowCubeMapsScreen + TrackedLightsMax + 1];
	for (int i = 0; i < ShadowCubeMapsScreen; i++)
		if (TheShaderManager->Effects.ShadowsExteriors->Constants.ShadowLightPosition[i].w != 0.0f) lamps[lampCount++] = TheShaderManager->Effects.ShadowsExteriors->Constants.ShadowLightPosition[i];
	for (int i = 0; i < TrackedLightsMax; i++)
		if (TheShaderManager->LightPosition[i].w != 0.0f) lamps[lampCount++] = TheShaderManager->LightPosition[i];
	if (TheShaderManager->SpotLightPosition[0].w != 0.0f) lamps[lampCount++] = TheShaderManager->SpotLightPosition[0];
	if (lampCount == 0) return 1;
	// The camera as the shader sees it, read from the same memory its constants are uploaded from at this moment
	// (TESR_ViewTransform = viewMatrix, TESR_ProjectionTransform = projMatrix, TESR_CameraPosition). The effect gets the matrices
	// as stored (Depth.hlsl's projectPosition multiplies a row vector by TESR_ProjectionTransform, D3D style, and works), so
	// toWorld's TESR_ViewTransform[0..2][2] is the view matrix's third column, the camera's forward axis (right, up: columns 1, 2).
	const D3DXMATRIX& V = TheRenderManager->viewMatrix;
	const D3DXMATRIX& P = TheRenderManager->projMatrix;
	const D3DXVECTOR3 R(V._11, V._21, V._31), U(V._12, V._22, V._32), F(V._13, V._23, V._33);
	// Sanity: a rotation (unit, orthogonal axes) and a perspective projection; anything else draws the pass whole.
	if (fabsf(D3DXVec3Length(&F) - 1.0f) > 1e-3f || fabsf(D3DXVec3Length(&R) - 1.0f) > 1e-3f || fabsf(D3DXVec3Length(&U) - 1.0f) > 1e-3f ||
		fabsf(D3DXVec3Dot(&F, &R)) > 1e-3f || fabsf(D3DXVec3Dot(&F, &U)) > 1e-3f || !(P._11 > 0.0f) || !(P._22 > 0.0f) || P._44 != 0.0f) {
		static bool reported = false;
		if (!reported) { reported = true; Logger::Log("LAMP MASK SCISSOR: unusual camera matrices (forward %.4f %.4f %.4f, P44 %.3f): the pass is drawn whole", F.x, F.y, F.z, P._44); }
		return 0;
	}
	{	// once: the forward axis next to the camera's (InvViewMatrix, the world camera) for the record
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
		// fixes-a #14: the box's corners in camera space (x right, y up, z forward). A pixel's reconstructed position has z = its depth,
		// which is never behind the near plane, so only the part of the box with z >= kNearClip can meet a pixel: a box entirely behind
		// that costs nothing (the lamp is skipped), and a box that straddles it is clipped to it (the 12 edges that cross the plane
		// give the new vertices; the hull of a convex polygon's projection is the hull of its vertices' projections). The old code
		// drew the whole pass for either, so one lamp behind the camera switched the scissor off. kNearClip is a tenth of the world
		// near plane NVR sets (2.5, CameraManager.cpp), so nothing visible is cut (the in-game LampMaskScissorCheck compares it).
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
		if (inFront == 0) continue;	// the whole box is behind the camera: this lamp reaches no pixel
		auto grow = [&](float x, float y, float z) {
			const float u = (x / z * P._11 + 1.0f) * 0.5f * W;	// toWorld inverted: 2u - 1 = (x / z) * P00
			const float v = (1.0f - y / z * P._22) * 0.5f * H;	// 2v - 1 = -(y / z) * P11
			lx0 = (std::min)(lx0, u); lx1 = (std::max)(lx1, u); ly0 = (std::min)(ly0, v); ly1 = (std::max)(ly1, v);
		};
		for (int corner = 0; corner < 8; corner++) {
			if (!front[corner]) continue;
			grow(cam[corner].x, cam[corner].y, cam[corner].z);
			if (inFront == 8) continue;
			for (int bit = 1; bit < 8; bit <<= 1) {	// an edge from this corner to one behind the plane: its crossing point
				const int other = corner ^ bit;
				if (front[other]) continue;
				const float t = (kNearClip - cam[corner].z) / (cam[other].z - cam[corner].z);
				grow(cam[corner].x + (cam[other].x - cam[corner].x) * t, cam[corner].y + (cam[other].y - cam[corner].y) * t, kNearClip);
			}
		}
		if (lx1 < -1.0f || ly1 < -1.0f || lx0 > W + 1.0f || ly0 > H + 1.0f) continue;	// off screen: no pixel's ray meets it
		any = true;
		minX = (std::min)(minX, lx0); maxX = (std::max)(maxX, lx1); minY = (std::min)(minY, ly0); maxY = (std::max)(maxY, ly1);
	}
	if (!any) return 1;
	rect.left = (LONG)(std::max)(0.0f, floorf(minX) - 1.0f);
	rect.top = (LONG)(std::max)(0.0f, floorf(minY) - 1.0f);
	rect.right = (LONG)(std::min)(W, ceilf(maxX) + 1.0f);
	rect.bottom = (LONG)(std::min)(H, ceilf(maxY) + 1.0f);
	if (rect.right <= rect.left || rect.bottom <= rect.top) return 1;
	if (rect.left == 0 && rect.top == 0 && rect.right == (LONG)W && rect.bottom == (LONG)H) return 0;	// the whole screen anyway
	return 2;
}

#if NVR_TEST_TOOLS
/*
* DIAGNOSTIC [Main.Main.Performance] LampMaskScissorCheck 1 (development builds only; release builds leave it out): every 8th
* frame in which LampMaskScissor cleared or scissored the lamp mask, that buffer is read back, the whole pass is drawn over it
* as without the switch, read back too, and the two are compared texel by texel (G16R16). The frame then goes on with the whole
* pass's buffer, so the picture is the old one. Exact in any scene, moving or not (both from the same frame's inputs). A
* test-only GPU stall.
*/
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
	TheShaderManager->RenderEffectToRT(surface, TheShaderManager->Effects.PointShadows, true);	// the whole pass, as before
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
/*
* DIAGNOSTIC [Main.Main.Performance] InteriorShadowFoldCheck N (development builds only): every Nth frame in which InteriorShadowFold
* would apply, the frame takes the old path (the separate ShadowsInteriors pass, then the fog), and the scene as it was before that
* pass is kept (FoldCheckScene). After the fog the result is read back, the folded fog pass is drawn from the kept scene into a spare
* target with the same AO / sky inputs (VolumetricFogEffect sceneOverride / targetOverride), read back too, and the two are compared
* bit for bit (16-bit float RGB). The frame goes on with the old result. Exact in any scene, moving or not (same frame, same inputs).
*/
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
		FoldCheckSceneSurface = nullptr;	// (leaked on purpose: test only)
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
	{ Fog->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Fog); }
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
				const unsigned diff = a > b ? a - b : b - a;	// in 16-bit float steps (same sign)
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
	PixelInspector::OwnTargetScope inspect(Effect, RenderTarget, techniqueIndex);	// UNOFFICIAL 2026-10-26 (pixel inspector)
	Effect->Render(Device, RenderTarget, RenderTarget, techniqueIndex, clearRenderTarget, RenderTarget);
};


// UNOFFICIAL 2026-10-27 RenderScale v2: the chain's first step on its own, so TAAEffect::UpscaleAfterEffects can redraw both at full
// size after its upscale. In a rect frame (RenderRect::On) both draw into the rect (EffectRecord / ApplyViewport).
void ShaderManager::RenderDepthAndNormals(IDirect3DSurface9* RenderTarget) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	static GpuTimer depthTimer("Depth combine");
	static GpuTimer normalsTimer("Normal reconstruction");
	Device->SetStreamSource(0, FrameVertex, 0, sizeof(FrameVS));
	Device->SetFVF(FrameFVF);
	// When the normals pass would run anyway, try producing depth and normals in one draw.
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
		// UNOFFICIAL 2026-10-23 (sharednormals; [Main.Main.Quality] SmoothNormals): the buffer smoothed in place, once, for every reader.
		Effects.Normals->RenderSmooth(Device);
		// UNOFFICIAL 2026-10-28 [Main.Main.Quality] CapturedNormals (core/CapturedNormals.h): the surfaces' own smooth normals written into
		// the buffer, for every reader. Not after an upscale to the screen (the normals were drawn at the internal resolution).
		if (CapturedNormals::MergeWanted() && !TAAEffect::UpscaledBeforeEffects())
			CapturedNormals::MergeIntoNormals(Device, Effects.Normals->Textures.NormalsSurface, Effects.CombineDepth->Textures.CombinedDepthTexture);
		Device->SetRenderTarget(0, RenderTarget);
	}
	if (RenderRect::Active()) Device->SetStreamSource(0, FrameVertex, 0, sizeof(FrameVS));	// the rect quad (ApplyViewport) does not outlive this
}

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

	// UNOFFICIAL 2026-10-26 pixel inspector (core\PixelInspector.h, off by default): the scene as the game drew it, before any NVR effect.
	PixelInspector::CaptureIf("Scene in (HDR, before NVR)", RenderTarget);

	// render post process normals for use by shaders
	RenderDepthAndNormals(RenderTarget);
	// UNOFFICIAL 2026-10-29 [Main.Main.Quality] ObjectMotion (core/ObjectMotion.h): the world-space move of what moved since last frame, for
	// every temporal reader (TAA, FSR's motion vectors). Off: nothing runs (TESR_ObjectMotionData.x = 0).
	if (ObjectMotion::Live) {
		static GpuTimer objectMotionTimer("Object motion");
		GpuProfileScope gpu(objectMotionTimer, Device);
		ObjectMotion::Render(Device, Effects.CombineDepth->Textures.CombinedDepthTexture);
		Device->SetRenderTarget(0, RenderTarget);
		Device->SetStreamSource(0, FrameVertex, 0, sizeof(FrameVS));
		Device->SetFVF(FrameFVF);
	}
	// UNOFFICIAL 2026-10-26 pixel inspector: the linear depth and the view normal every effect reads.
	PixelInspector::CaptureIf("Depth (TESR_DepthBuffer, linear / farZ)", Effects.CombineDepth->Textures.CombinedDepthTexture, PixelInspector::Depth);
	PixelInspector::CaptureIf("View normal (TESR_NormalsBuffer)", Effects.Normals->Textures.NormalsTexture, PixelInspector::Normal);
	// UNOFFICIAL 2026-10-27 FitToDepth ([Shaders.ShadowsExteriors.Main], core\SunDepthFit.h): how far the visible surfaces reach,
	// counted on a grid of 8 x 8 pixel blocks by occlusion queries read back frames later (no wait). Off: nothing runs.
	if (SunDepthFit::Wanted()) {
		static GpuTimer depthFitTimer("Sun depth fit");
		GpuProfileScope gpu(depthFitTimer, Device);
		SunDepthFit::Measure(Device);
		Device->SetRenderTarget(0, RenderTarget);
	}
#if NVR_TEST_TOOLS
	OrthoProbe::AfterCombine(Device);	// UNOFFICIAL 2026-10-17 test tools: the depth the effects read, after an ortho dump
#endif

	// UNOFFICIAL interior forward point-light shadows (PointShadowForward.h): the object shaders shadow each lamp this
	// frame, so the screen-space shadows are not applied again. KeepDarkening keeps their darkening of what no tracked lamp
	// reaches: the mask is drawn without its cube-map lookups (TESR_ShadowFade.z 0 makes PointShadows.fx count every lamp
	// as unblocked) and applied in one pass (no blur unless DarkeningBlur). Without it, neither runs; the buffer is then
	// cleared to "no shadow" for the effects that read it (indoors only Underwater).
	// (Only while the PBR object shaders, which draw the forward shadows, are on and were drawing this frame: with PBR off
	// the old screen-space shadows run instead of none at all.)
	const bool forwardInterior = !GameState.isExterior && Effects.ShadowsInteriors->Enabled && PointShadowForward::Active &&
		PointShadowForward::BoundThisFrame && Shaders.PBR && Shaders.PBR->Enabled;
	const bool keepDarkening = !forwardInterior || Effects.ShadowsExteriors->Settings.Interiors.Forward.KeepDarkening;

	// UNOFFICIAL (2026-10-07 review): the "no shadow" clear of the buffer is done once, not on every frame that draws no
	// shadow pass (a full-screen clear and two target switches for a buffer that already held the clear value).
	static bool shadowBufferClear = false;
	// UNOFFICIAL 2026-10-28 (NVR DXVK) the ray tracing core's point of the frame (RtCore.h): the depth and normals exist, the sun
	// shadows come next; indoors and outdoors, with or without the shadow passes. Nothing happens unless an RT effect is on (and the
	// fork, a GPU with ray tracing and the helper are there).
	{
		static bool rtRegistered = false;
		if (!rtRegistered) { RtSunShadows::Register(); Effects.Reflections->RtRegister(); RtLampShadows::Register(); AmbientOcclusionEffect::RtRegister(); rtRegistered = true; }	// UNOFFICIAL 2026-10-28 RT sun + RT reflections + RT lamp shadows + RT AO/sky
		RtCore::Frame(Device);
	}
	// render a shadow pass for point lights
	if (forwardInterior && !keepDarkening) {
		if (!shadowBufferClear) { Effects.ShadowsExteriors->clearShadowsBuffer(); shadowBufferClear = true; }
	}
	else if ((GameState.isExterior && Effects.ShadowsExteriors->Enabled) || (!GameState.isExterior && Effects.ShadowsInteriors->Enabled)) {
		shadowBufferClear = false;	// the passes below write it
		{
			GpuProfileScope gpu(pointShadowTimer, Device);
			const float pointShadowsWere = Effects.ShadowsExteriors->Constants.ShadowFade.z;
			if (forwardInterior) Effects.ShadowsExteriors->Constants.ShadowFade.z = 0.0f;
			// UNOFFICIAL 2026-10-16 MaskHalfRes: with forward lamp shadows the mask is only the lamps' reach (smooth): drawn at half
			// resolution (PointShadows.fx technique 3, mask + depth) and upsampled depth-aware into the buffer (technique 4).
			static EffectRecord::CachedHandle halfCache, upCache;
			const bool halfMask = forwardInterior && LampMaskHalfRes() &&
				Effects.PointShadows->TechniqueHandle(halfCache, "LampMaskHalf") == Effects.PointShadows->Effect->GetTechnique(3) &&
				Effects.PointShadows->TechniqueHandle(upCache, "LampMaskUpsample") == Effects.PointShadows->Effect->GetTechnique(4) &&
				Effects.PointShadows->Effect->GetTechnique(4) != NULL;
			// The stock/custom shader remains compatible: it has no named merged
			// technique, so lights 6-11 still take the original second pass.
			static EffectRecord::CachedHandle mergedTechnique;	// UNOFFICIAL: looked up once per loaded effect
			const bool mergedPointShadows = Effects.PointShadows->TechniqueHandle(mergedTechnique, "MergedPointShadows") != NULL;
			// UNOFFICIAL 2026-10-18 LampMaskScissor ([Main.Main.Performance], LampMaskRect above): the mask is drawn only inside the
			// screen rectangle its lamps' spheres can reach (nothing at all with no lamp on screen); every other pixel keeps the
			// clear's 0, which is what the pass writes there. Only on the plain path (not MaskHalfRes, not the outdoor batches,
			// merged technique present, the effect drawing).
			RECT maskRect = {};
			int lampCount = 0, maskMode = 0;
			if (TheSettingManager->SettingsMain.Main.LampMaskScissor && !halfMask && mergedPointShadows &&
				!PointShadowForward::ExteriorBatched(GameState.isExterior) &&
				Effects.PointShadows->Enabled && Effects.PointShadows->Effect && Effects.PointShadows->ShouldRender())
				maskMode = LampMaskRect(maskRect, lampCount);
			// UNOFFICIAL 2026-10-27 RenderScale v2: the rectangle is in full-screen pixels; a rect frame draws the mask into its top-left part.
			if (maskMode == 2 && RenderRect::Active()) {
				maskRect.left = (LONG)floorf((float)maskRect.left * RenderRect::ScaleX);
				maskRect.top = (LONG)floorf((float)maskRect.top * RenderRect::ScaleY);
				maskRect.right = (std::min)((LONG)ceilf((float)maskRect.right * RenderRect::ScaleX), (LONG)RenderRect::Width(TheRenderManager->width));
				maskRect.bottom = (std::min)((LONG)ceilf((float)maskRect.bottom * RenderRect::ScaleY), (LONG)RenderRect::Height(TheRenderManager->height));
			}
			static int loggedMaskMode = -1;
			if (maskMode != loggedMaskMode) {
				// fixes-a #51: the first 20 switches only (it was a line per switch, ~380 an hour while turning); more with the F10 profile
				static int stateLines = 0;
				if (stateLines < 20 || GpuTimer::Enabled) {
					Logger::Log("LAMP MASK STATE: %s (%d lamps in its lists)", maskMode == 1 ? "SKIPPED, no lamp reaches the screen: the buffer cleared to 0, as the pass would write" :
						maskMode == 2 ? "SCISSORED to the lamps' screen rectangle" : "drawn whole", lampCount);
					if (++stateLines == 20 && !GpuTimer::Enabled) Logger::Log("LAMP MASK STATE: no more lines (20 logged; the F10 profile logs them all)");
				}
				loggedMaskMode = maskMode;
			}
			if (GpuTimer::Enabled) {	// a line every 240 frames with the profile on: how often each case ran
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
				// The pass's own clear, on the whole target (the scissor test off: a clear honours it). SetRenderTarget resets the
				// scissor rectangle to the whole target, as RenderEffectToRT leaves it.
				IDirect3DSurface9* maskSurface = Effects.ShadowsExteriors->Textures.ShadowPassSurface;
				DWORD scissorWas = FALSE;
				RECT scissorRectWas = {};	// fixes-a #15: saved and restored like LampScissor.h (it no longer relies on SetRenderTarget's reset)
				Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissorWas);
				const bool haveRectWas = SUCCEEDED(Device->GetScissorRect(&scissorRectWas));
				Device->SetRenderTarget(0, maskSurface);
				Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
				Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0L);	// (EffectRecord::Render's clear)
				if (maskMode == 2) {
					// The pass itself, unchanged, inside the rectangle: RenderEffectToRT without its SetRenderTarget (which would reset
					// the rectangle). Its target is not the frame chain's, so EffectRecord::Render takes the plain path (no target
					// switch); no technique state touches the scissor, and the effect's Begin/End restores what its passes change.
					Device->SetScissorRect(&maskRect);
					Device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
					Effects.PointShadows->Render(Device, maskSurface, maskSurface, 0, false, maskSurface);
					Device->SetRenderTarget(0, maskSurface);	// the rectangle back to the whole target
				}
				Device->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorWas);
				if (haveRectWas) Device->SetScissorRect(&scissorRectWas);
#if NVR_TEST_TOOLS
				LampMaskSelfCheck(Device, maskSurface, maskMode, maskRect);	// DIAGNOSTIC LampMaskScissorCheck (off: returns at once)
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
			if (PointShadowForward::ExteriorBatched(GameState.isExterior)) RenderPointShadowBatches();	// UNOFFICIAL ExteriorLightPoints
			if (!mergedPointShadows && Effects.ShadowsExteriors->Settings.Interiors.LightPoints > 6)
				RenderEffectToRT(Effects.ShadowsExteriors->Textures.ShadowPassSurface, Effects.PointShadows2, false);
			Effects.ShadowsExteriors->Constants.ShadowFade.z = pointShadowsWere;
		}
		// UNOFFICIAL 2026-10-16 lamp contact shadows ([Shaders.ShadowsInteriors.Main] ContactShadows): PointShadows.fx technique 2
		// (LampContact) multiplies the lamp mask just written by the contact term. Indoors; only with that technique there (a
		// custom PointShadows.fx without it draws nothing more).
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
			static EffectRecord::CachedHandle fusedCache;	// UNOFFICIAL: looked up once per loaded effect
			D3DXHANDLE fusedTechnique = Effects.SunShadows->TechniqueHandle(fusedCache, "ForwardContactShadows");
			const bool fusedForwardContact = Effects.ShadowsExteriors->Settings.Exteriors.ForwardShadows &&
				fusedTechnique != NULL && Effects.SunShadows->Effect->GetTechnique(1) == fusedTechnique &&
				!RtSunShadows::Replacing();	// UNOFFICIAL 2026-10-28 RT sun shadows Mode 1: the deferred technique (its apply fades the cascades out near)
			// UNOFFICIAL Sharp ([Shaders.ShadowsExteriors.ScreenSpace]): the one-march technique (third in SunShadows.fx.hlsl).
			static EffectRecord::CachedHandle sharpCache;
			D3DXHANDLE sharpTechnique = Effects.SunShadows->TechniqueHandle(sharpCache, "ForwardContactSharp");
			const bool sharpContact = fusedForwardContact && Effects.ShadowsExteriors->Settings.ScreenSpace.Sharp && sharpTechnique != NULL &&
				Effects.SunShadows->Effect->GetTechnique(2) == sharpTechnique;
			// UNOFFICIAL 2026-10-16 SkipWhenFaded (dawn cost): while the sun's shadows are fully faded and nothing reads these
			// passes (ShadowsExteriorEffect::FadedContactUnread), they are skipped; the buffer keeps the lamps' pass above.
			ShadowsExteriorEffect* sunShadows = Effects.ShadowsExteriors;
			const bool skipContact = sunShadows->FadedContactUnread();
			static int loggedSkip = -1;
			if ((int)skipContact != loggedSkip) {
				Logger::Log("SUN FADE STATE: contact shadows %s", skipContact ? "SKIPPED" : sunShadows->SunFaded ?
					"drawn (an effect reads them: lamp shadows outdoors, blood flow, wet world, underwater or the debug view)" : "drawn");
				loggedSkip = skipContact;
			}
			if (!skipContact) {
				// The stock technique's last pass (no fused technique in an old SunShadows.fx) would take the cascades over while
				// the forward lookups are off for the fade: it gets the forward value, as without the switch.
				const float forwardWas = sunShadows->Constants.ForwardData.x;
				if (sunShadows->SunFaded && sunShadows->Settings.Exteriors.ForwardShadows) sunShadows->Constants.ForwardData.x = 0.0f;
				RenderEffectToRT(sunShadows->Textures.ShadowPassSurface, Effects.SunShadows, false, sharpContact ? 2 : fusedForwardContact ? 1 : 0);
				sunShadows->Constants.ForwardData.x = forwardWas;
				RtSunShadows::Apply(Device);	// UNOFFICIAL 2026-10-28: the traced sun visibility into the red channel (nothing when not traced)
			}
#if NVR_TEST_TOOLS
			else if (sunShadows->Settings.Exteriors.SkipWhenFadedCheck) {
				// DIAGNOSTIC SkipWhenFadedCheck: the skipped passes' buffer set to 0 (full shadow, no lamp light), so a reader the
				// checks missed changes the picture.
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

	// UNOFFICIAL 2026-10-19 SunFar: the land's and the clouds' shadows as a factor the sun-shadow composites below take (outdoors,
	// while either is on; otherwise it marks the factor unused and draws nothing).
	Effects.SunFar->RenderFactor(Device, RenderTarget);
	if (Effects.SunFar->Constants.Apply.x > 0.5f) FlickerWatch::Flags |= FlickerWatch::kSunFar;	// UNOFFICIAL 2026-10-23 test only

	Device->SetRenderTarget(0, RenderTarget);
	if (Effects.SunFar->Settings.FlickerWatch && GameState.isExterior) FlickerWatch::OnProbe(FlickerWatch::World, Device, RenderTarget);	// (2026-10-25) test only

	// Start the copy-free chain (FrameChain), or seed the rendered texture for the legacy path in
	// which every effect keeps it equal to the render target. TESR_SourceBuffer is refreshed by
	// each effect that actually samples it (EffectRecord::usesSourceBuffer). Effects are handed
	// TheTextureManager->RenderedSurface at call time because the chain swaps it.
	struct ChainGuard { ~ChainGuard() { TheShaderManager->Chain.End(); } } chainGuard;
	if (!Chain.Begin(RenderTarget))
		Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);

	// UNOFFICIAL 2026-10-20 MaterialAware reflections (core/ReflMaterial.h): the materials the world scene left in the scene's alpha,
	// copied out before the first effect draws over it.
	Effects.Reflections->CaptureMaterial(Device);
	Device->SetRenderTarget(0, RenderTarget);

	// UNOFFICIAL the screen-space skin blur ([Shaders.Skin.Scattering] ScreenSpace, SkinScattering.h): first, on the scene exactly as
	// drawn: it recognises skin by comparing each pixel with the colour the skin shader wrote, which any effect before it would
	// change. Shadows, AO and fog then apply to the scattered skin like everything else. Nothing runs with it off.
	if (Effects.SkinScattering && Shaders.Skin && Shaders.Skin->Active()) {	// Render checks the rest (ShouldRender resolves the targets: once)
		static GpuTimer skinScatterTimer("Skin scattering");
		GpuProfileScope gpu(skinScatterTimer, Device);
		{ Effects.SkinScattering->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.SkinScattering); }	// pass 2 reads the unblurred scene
	}
	// Composite apply: the exterior sun-shadow composite and the AO combine are per-pixel
	// operations on the scene, applied just before fog. When no effect that normally runs between
	// them would render this frame, the fog reconstruct applies them in the same pass (same maths,
	// same order), saving two full-resolution read/write passes of the HDR frame.
	auto wouldRender = [](EffectRecord* effect) { return effect && effect->Enabled && effect->Effect && effect->ShouldRender(); };
	// UNOFFICIAL, optional: volumetric clouds over the sky (VolumetricClouds.h). First, so the shadow and AO applies and
	// the fog all see them as part of the sky, and the composite apply below is not affected.
	// UNOFFICIAL physical sky: its table for the sky shader (read next frame), first, so the clouds can use it this frame too.
	if (Effects.SkyView && Effects.SkyView->ShouldRender()) {
		static GpuTimer skyViewTimer("Physical sky table");
		GpuProfileScope gpu(skyViewTimer, Device);
		Effects.SkyView->RenderTable(Device, RenderTarget);
	}
	if (wouldRender(Effects.VolumetricClouds)) {
		GpuProfileScope gpu(cloudsTimer, Device);
		Effects.VolumetricClouds->RenderClouds(Device, RenderTarget, TheTextureManager->RenderedSurface, SourceSurface);
	}
	// UNOFFICIAL AmbientLight (AmbientLight.h): AO and bounce light in one pass. While it renders it replaces AmbientOcclusion and
	// BounceLight: both are switched off for the rest of this call (and back as they were when it returns), so every path below
	// (the merged shadow + AO apply, the bounce folded into the specular) sees them off.
	const bool ambientLight = wouldRender(Effects.AmbientLight);
	struct EnabledGuard { EffectRecord* effect; bool was; ~EnabledGuard() { if (effect) effect->Enabled = was; } };
	EnabledGuard aoGuard{ ambientLight ? Effects.AmbientOcclusion : nullptr, Effects.AmbientOcclusion->Enabled };
	EnabledGuard bounceGuard{ ambientLight ? Effects.BounceLight : nullptr, Effects.BounceLight->Enabled };
	if (ambientLight) Effects.AmbientOcclusion->Enabled = Effects.BounceLight->Enabled = false;
	// UNOFFICIAL 2026-10-26 (NVR DXVK, deep/fork-hiz; core/HiZ.h): the Hi-Z pyramid, one compute dispatch after the depth and normals,
	// only for the readers that want it this frame ([Shaders.Reflections.Main] HiZ, [Shaders.AmbientLight.Main] HiZ, both Vulkan passes).
	// (Reflections alone builds it right before its own passes, below: built here, the dispatch slowed AmbientLight's prepare by ~0.015 ms.)
	HiZ::Build(Device, DxvkCompute::PassesOn() && ambientLight && Effects.AmbientLight->HiZWanted());
	AmbientOcclusionEffect* AO = Effects.AmbientOcclusion;
	VolumetricFogEffect* Fog = Effects.VolumetricFog;
	const bool shadowApplies = GameState.isExterior && wouldRender(Effects.ShadowsExteriors);
	const bool aoApplies = wouldRender(AO);
	const bool effectsBetween = wouldRender(Effects.SnowAccumulation) || wouldRender(Effects.WetWorld) || wouldRender(Effects.BloodFlow) ||
		wouldRender(Effects.Flashlight) || wouldRender(Effects.Specular) || wouldRender(Effects.Underwater) ||
		wouldRender(Effects.BounceLight) || wouldRender(Effects.Reflections) || ambientLight; // must see the scene with its shadows and AO applied
	// UNOFFICIAL 2026-10-18 InteriorShadowFold ([Main.Main.Performance]; evidence\intshadow-LOG.md): indoors with the forward lamp
	// shadows, the darkening of what no tracked lamp reaches (KeepDarkening) was its own full-resolution pass below, unblurred
	// (no DarkeningBlur): ShadowsInteriors' dedicated last pass, a per-pixel function of the scene, the lamp mask and the depth.
	// The fog composite now does it first, with the same function (Includes/InteriorShadowApply.hlsl) and its result rounded to
	// 16-bit float as that pass's target stored it (VolumetricFog.fx CompositeFogInterior), when nothing between them reads the
	// picture: the effects above, and the Reflections surroundings cube, whose capture (RenderCubeOnly, below) reads the scene
	// even when no floor reflects. Only where that pass would take its dedicated path (the generic one blurs).
	// OFF by default (2026-10-18): the same-frame check (InteriorShadowFoldCheck) still finds a few pixels a frame next to the void
	// (ceiling gaps, the far plane: the fog's sky test) differing by a few 16-bit steps; everywhere else it matched bit for bit.
	const bool interiorFoldable = !GameState.isExterior && keepDarkening && forwardInterior &&
		!Effects.ShadowsExteriors->Settings.Interiors.Forward.DarkeningBlur && TheSettingManager->SettingsMain.Main.InteriorShadowFold &&
		!(Effects.Reflections->Enabled && Effects.Reflections->Effect && Effects.Reflections->CubeWanted()) &&
		Effects.ShadowsInteriors->DedicatedUsable(RenderTarget, TheTextureManager->RenderedSurface) && Fog->CanCompositeInterior() &&
		(TheSettingManager->SettingsMain.Main.InteriorShadowFoldRound != 0 || Fog->HalfStoreMode(Device) >= 0);	// (measured once)
	// DIAGNOSTIC InteriorShadowFoldCheck (test builds): on a check frame the old path runs and the fold is compared with it.
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
	auto interiorApply = [&]() {	// the separate pass, as it ran before (and still runs when the fold cannot)
		Effects.ShadowsInteriors->skipBlur = forwardInterior && !Effects.ShadowsExteriors->Settings.Interiors.Forward.DarkeningBlur;
		{ Effects.ShadowsInteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, true, SourceSurface); PixelInspector::AfterFrame(Effects.ShadowsInteriors); }
		Effects.ShadowsInteriors->skipBlur = false;
	};
	if (!GameState.isExterior) {
		static int loggedFold = -1;
		const int fold = (composite && interiorApplies) || foldCheckFrame ? 1 : 0;	// (a check frame draws the old way on purpose)
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
	// UNOFFICIAL: when effects between keep the composite out of the fog pass, the shadow and AO applies still run
	// together, in one pass right after the AO (nothing reads the scene between them unless snow accumulates).
	const bool pairApply = !composite && shadowApplies && aoApplies && !wouldRender(Effects.SnowAccumulation) &&
		Fog->CanApplyShadowAO();
	AO->deferredReady = false;
	AO->skyReady = false;	// UNOFFICIAL Skylighting v2: set again by this frame's AO render
	// UNOFFICIAL 2026-10-14: with AmbientLight on, the sun-shadow apply goes into its passes (nothing reads the scene between them
	// unless snow accumulates); if it does not render after all, the shadow pass runs after it (below).
	const bool shadowIntoAmbientLight = ambientLight && shadowApplies && !composite && !pairApply && !wouldRender(Effects.SnowAccumulation) &&
		!TheSettingManager->SettingsMain.Main.DisableCompositeApply;
	if (GameState.isExterior) {	// UNOFFICIAL 2026-10-23 FlickerWatch (test only): which pass applies the sun shadow this frame
		if (composite && shadowApplies) FlickerWatch::Flags |= FlickerWatch::kComposite;
		if (pairApply) FlickerWatch::Flags |= FlickerWatch::kPair;
		if (shadowIntoAmbientLight) FlickerWatch::Flags |= FlickerWatch::kIntoAL;
		if (!(composite && shadowApplies) && !pairApply && !shadowIntoAmbientLight) FlickerWatch::Flags |= FlickerWatch::kShadowPass;
		if (Effects.ShadowsExteriors->Constants.ShadowBlur.y > 0.5f) FlickerWatch::Flags |= FlickerWatch::kLodUpdated;
		if (GameState.isCellChanged) FlickerWatch::Flags |= FlickerWatch::kCellChanged;
		ShadowsExteriorEffect* sx = Effects.ShadowsExteriors;
		if (sx->SunGlideValid && GetTickCount64() - sx->SunGlideStart < (ULONGLONG)(1000.0f * sx->Settings.SunSmoothing.GlideSeconds))
			FlickerWatch::Flags |= FlickerWatch::kSunGlide;
		FlickerWatch::Hour = ShaderConst.GameTime.y;
		FlickerWatch::SunZ = sx->Constants.SmoothedSunDir.z;
	}

	{
		GpuProfileScope gpu(shadowApplyTimer, Device);
		if (GameState.isExterior) {
			if (!(composite && shadowApplies) && !pairApply && !shadowIntoAmbientLight)
				{ Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.ShadowsExteriors); }
		}
		else if (keepDarkening && !(composite && interiorApplies)) {
			// With forward shadows on, the darkening alone: unblurred unless DarkeningBlur.
#if NVR_TEST_TOOLS
			if (foldCheckFrame) Device->StretchRect(TheTextureManager->RenderedSurface, NULL, FoldCheckSceneSurface, NULL, D3DTEXF_NONE);	// the scene before it
#endif
			interiorApply();
		}
	}

	// UNOFFICIAL 2026-10-27 Persistent wetness and snow (CoverMap.h): the cover map's update (its own targets), before its readers
	// SnowAccumulation and WetWorld; then they draw with its technique while it holds data. Both settings off: nothing here runs.
	if (TheCoverMap && (TheCoverMap->Settings.wet || TheCoverMap->Settings.snow)) {
		static GpuTimer coverMapTimer("Cover map update");
		GpuProfileScope gpu(coverMapTimer, Device);
		TheCoverMap->RenderUpdate(Device, RenderTarget);
	}
	{
		GpuProfileScope gpu(snowAccumulationTimer, Device);
		// UNOFFICIAL 2026-10-28 CapturedNormals: a frame already upscaled to the screen before the effects (RenderScale with LowResEffects
		// off) no longer matches the normals drawn at the internal resolution: the readers keep the depth-rebuilt normal then. (With
		// LowResEffects on the effects run in the drawn rect before the upscale, as the normals were drawn: they match.)
		if (TAAEffect::UpscaledBeforeEffects()) CapturedNormals::Data.x = 0.0f;
		// UNOFFICIAL 2026-10-28: the frame's jitter in an FSR / DLSS LowResEffects frame (0 otherwise): snow builds its world positions along
		// the jittered pixel's own ray (SnowAccumulation.fx.hlsl SnowRay), as the clouds' composite reads at it.
		{
			const D3DXVECTOR2 jitterUV = Effects.TAA && Effects.SnowAccumulation->FollowJitter ? Effects.TAA->LowResJitterUV() : D3DXVECTOR2(0.0f, 0.0f);
			Effects.SnowAccumulation->Extra.z = jitterUV.x;
			Effects.SnowAccumulation->Extra.w = jitterUV.y;
		}
		{
			Effects.SnowAccumulation->Render(Device, RenderTarget, TheTextureManager->RenderedSurface,
				Effects.SnowAccumulation->Technique(TheCoverMap ? TheCoverMap->SnowTechnique(Effects.SnowAccumulation) : 0), false, SourceSurface);	// UNOFFICIAL 2026-10-28 Look
			PixelInspector::AfterFrame(Effects.SnowAccumulation);
		}
	}
	{
		GpuProfileScope gpu(aoTimer, Device);
		AO->deferCombine = (composite || pairApply) && aoApplies;
		{ AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(AO); }
		AO->deferCombine = false;
		if (pairApply) {
			static GpuTimer mergedApplyTimer("  Shadow + AO apply (merged)");
			bool merged = false;
			if (AO->deferredReady) {
				GpuProfileScope gpuMerged(mergedApplyTimer, Device);
				Fog->compositeSkyTexture = AO->SkyTexture();	// UNOFFICIAL Skylighting v2 (null unless on)
				merged = Fog->RenderShadowAO(Device, RenderTarget, TheTextureManager->RenderedSurface, true, AO->ResultTexture());
				Fog->compositeSkyTexture = nullptr;
			}
			if (!merged) {
				// The two passes as before, in their order: shadows, then the AO (its combine alone if it was deferred).
				{ Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.ShadowsExteriors); }
				if (AO->deferredReady) {
					AO->combineOnly = true;
					{ AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(AO); }
					AO->combineOnly = false;
				}
				else { AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(AO); }
			}
			AO->deferredReady = false;
		}
		if (composite && aoApplies && !AO->deferredReady) {
			// Dedicated AO was unavailable or failed. Restore the original order before
			// its legacy path reads the scene for luminance-dependent AO strength.
			composite = false;
			if (shadowApplies)
				{ Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.ShadowsExteriors); }
			if (interiorApplies) interiorApply();	// UNOFFICIAL InteriorShadowFold: left to the fog, so run in its place
			{ AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(AO); }
		}
	}
	// UNOFFICIAL: the bounce light's full-resolution combine goes into the specular's, one pass for both, when nothing renders
	// between them (the wet world and the flashlight read and change the image there).
	const bool specularRenders = wouldRender(Effects.Specular) && Effects.Specular->Textures.LightSurface;
	// (Not with the bounce light's NeonSpill on: its own combine has the spill's debug views and street smoothing.)
	const bool bounceIntoSpecular = specularRenders && wouldRender(Effects.BounceLight) && !wouldRender(Effects.WetWorld) &&
		!wouldRender(Effects.Flashlight) && Effects.Specular->CanTakeBounce() && !Effects.BounceLight->SpillOn() &&
		!Effects.BounceLight->HardEdgesOn();
	if (ambientLight) {
		// UNOFFICIAL: AO and bounce light in one pass, on the shadowed scene, where the bounce light ran.
		static GpuTimer ambientLightTimer("Ambient light (AO + bounce)");
		GpuProfileScope gpu(ambientLightTimer, Device);
		// UNOFFICIAL 2026-10-20 (skyamb) Skylighting (AmbientOcclusion.h): the AO's sky pass first, at AmbientLight's half resolution;
		// AmbientLight's Apply takes its factor.
		// UNOFFICIAL 2026-10-21 (alrocks): AmbientLight's Prepare and its normal smoothing first, so the sky test reads the smoothed normal.
		Effects.AmbientLight->RenderPrepare(Device);
		if (AmbientOcclusionEffect::SkyV2Active() && AO->Effect) {
			AO->lastLowRes = false;
			AO->RenderSkyVisibility(Device, Effects.AmbientLight->SmoothedNormals());
			Device->SetRenderTarget(0, RenderTarget);
		}
		Effects.AmbientLight->foldShadow = shadowIntoAmbientLight;
		Effects.AmbientLight->shadowApplied = false;
		{ Effects.AmbientLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.AmbientLight); }
		Effects.AmbientLight->foldShadow = false;
		if (Effects.AmbientLight->shadowApplied) FlickerWatch::Flags |= FlickerWatch::kALApplied;
		if (shadowIntoAmbientLight && !Effects.AmbientLight->shadowApplied) {
			FlickerWatch::Flags |= FlickerWatch::kShadowPass;
			{ Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.ShadowsExteriors); }
		}
	}
	if (Effects.SunFar->Settings.FlickerWatch && GameState.isExterior) {	// (2026-10-25) test only: after the sun-shadow / AO / AmbientLight step
		FlickerWatch::OnProbe(FlickerWatch::Lit, Device, TheTextureManager->RenderedSurface);
		Device->SetRenderTarget(0, RenderTarget);
	}
#if NVR_TEST_TOOLS
	SettingShots::CaptureStage(Device, TheTextureManager->RenderedSurface);	// UNOFFICIAL, test only: 'stage' shots (ambfix 2026-10-18)
#endif
	Effects.BounceLight->deferredReady = false;
	if (Effects.BounceLight->Enabled) {
		// UNOFFICIAL, optional: one bounce of screen-space indirect light, on the shadowed and AO'd scene.
		static GpuTimer bounceLightTimer("Bounce light");
		GpuProfileScope gpu(bounceLightTimer, Device);
		Effects.BounceLight->deferCombine = bounceIntoSpecular;
		{ Effects.BounceLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.BounceLight); }
		Effects.BounceLight->deferCombine = false;
	}
	{
		// UNOFFICIAL, optional: blood that runs and dries, and pools under bodies (BloodFlow.h). On the shadowed, AO'd scene,
		// before the wet world and the fog. Costs nothing when no blood is near.
		static GpuTimer bloodFlowTimer("Blood flow");
		GpuProfileScope gpu(bloodFlowTimer, Device);
		{ Effects.BloodFlow->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.BloodFlow); }
	}
	{
		GpuProfileScope gpu(materialEffectsTimer, Device);
		// UNOFFICIAL HudDrops: the camera's cover test while it rains outdoors (Render.cpp HudRain reads its answer).
		if (Effects.WetWorld->Enabled && Effects.WetWorld->Constants.Data.x > 0.0f && GameState.isExterior && !GameState.isUnderwater &&
			TheSettingManager->GetSettingF("Shaders.WetWorld.Main", "HudDrops") > 0.0f) {
			Effects.WetWorld->RenderCoverTest(Device);
			Device->SetRenderTarget(0, RenderTarget);
		}
		{
			Effects.WetWorld->Render(Device, RenderTarget, TheTextureManager->RenderedSurface,
				TheCoverMap ? TheCoverMap->WetTechnique(Effects.WetWorld, Effects.WetWorld->MainTechnique()) : Effects.WetWorld->MainTechnique(), false, SourceSurface);
			PixelInspector::AfterFrame(Effects.WetWorld);
		}
		// Beam march first, into its own half res buffer, so the Flashlight Combine pass can
		// read it. Control.x already folds the effect toggle, the per view toggle and the
		// strength together, so this one test gates the whole thing.
		if (Effects.FlashlightBeam->Constants.Control.x > 0.0f) {
			RenderEffectToRT(Effects.FlashlightBeam->Textures.VolumetricSurface, Effects.FlashlightBeam, true);
			Device->SetRenderTarget(0, RenderTarget);
		}
		{ Effects.Flashlight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, Effects.Flashlight->selectedPass, true, SourceSurface); PixelInspector::AfterFrame(Effects.Flashlight); }
		// Specular's two quarter-resolution steps (the blocks' normals, then the light they reflect) into its own buffers,
		// then its main technique adds that to the image in the chain.
		if (specularRenders) {
			RenderEffectToRT(Effects.Specular->Textures.NormalsSurface, Effects.Specular, false, 1);
			Effects.Specular->BindExtras(Device, true);	// UNOFFICIAL 2026-10-19: sky visibility + SunFar for the light step
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
				// The two as before, in their order: the bounce light's combine (alone, from its result), then the specular.
				Effects.BounceLight->combineOnly = true;
				{ Effects.BounceLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.BounceLight); }
				Effects.BounceLight->combineOnly = false;
			}
			Effects.BounceLight->deferredReady = false;
		}
		if (!specularDone) { Effects.Specular->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Specular); }
		{ Effects.Underwater->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Underwater); }
	}
#if NVR_TEST_TOOLS
	EffectSelfTest::Capture(EffectSelfTest::AfterSpill, TheTextureManager->RenderedSurface);	// UNOFFICIAL, diagnostic: NeonSpillSelfTest
#endif
	{
		// UNOFFICIAL: screen-space reflections on the ground, of the scene with every light, shadow and highlight on it,
		// before the fog (which then hazes the reflection with the surface). Half-resolution passes into the effect's own
		// buffers, then its composite in the chain.
		static GpuTimer reflectionsTimer("Reflections (SSR)");
		GpuProfileScope gpu(reflectionsTimer, Device);
		if (wouldRender(Effects.Reflections)) {
			// UNOFFICIAL 2026-10-26 (deep/fork-hiz): the Hi-Z pyramid for the trace, once a frame (nothing new if AmbientLight's HiZ built it).
			if (DxvkCompute::PassesOn() && Effects.Reflections->HiZWanted()) HiZ::Build(Device, true);
			Effects.Reflections->RenderBuffers(Device);
			Device->SetRenderTarget(0, RenderTarget);
			Effects.Reflections->BindMaterial();	// UNOFFICIAL 2026-10-20 MaterialAware (stage 15; nothing without it)
			{ Effects.Reflections->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Reflections); }
			Effects.Reflections->ClearPassTextures();
		}
		else if (Effects.Reflections->Enabled && Effects.Reflections->Effect && Effects.Reflections->CubeWanted()) {
			// No floor reflects now, but metal and the env maps may read the surroundings cube: keep it up.
			Effects.Reflections->RenderCubeOnly(Device);
			Device->SetRenderTarget(0, RenderTarget);
		}
	}
	// UNOFFICIAL 2026-10-14 froxel grid ([Shaders.VolumetricLight.Main] Froxels): the volumetric light's grid is built before
	// the fog, which reads it (its sunlight shaded along the view) and does the volumetric light's composite in its own last
	// pass; the volumetric light below then only draws its composite if the fog could not.
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
		Fog->compositeInteriorShadow = composite && interiorApplies;	// UNOFFICIAL InteriorShadowFold
		const int foldRound = TheSettingManager->SettingsMain.Main.InteriorShadowFoldRound;	// 0: as this GPU stores (HalfStoreMode)
		Fog->compositeRound = foldRound == 1 ? 1.0f : foldRound == 2 ? 2.0f : foldRound == 3 ? 0.0f : Fog->halfStoreMode == 2 ? 2.0f : 0.0f;
		Fog->compositeAO = composite && AO->deferredReady;
		Fog->compositeAOTexture = AO->ResultTexture();
		Fog->compositeSkyTexture = Fog->compositeAO ? AO->SkyTexture() : nullptr;	// UNOFFICIAL Skylighting v2 (null unless on)
		Fog->compositeApplied = false;
		{ Fog->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Fog); }
		Fog->compositeSkyTexture = nullptr;
		const bool applied = Fog->compositeApplied;
#if NVR_TEST_TOOLS
		const bool checkAO = Fog->compositeAO && applied;
		IDirect3DTexture9* checkAOTexture = Fog->compositeAOTexture;
		IDirect3DTexture9* checkSky = checkAO ? AO->SkyTexture() : nullptr;
#endif
		Fog->compositeShadow = Fog->compositeAO = Fog->compositeApplied = Fog->compositeInteriorShadow = false;
		if (composite && !applied) {
			// Failed composite fog leaves the scene untouched. Restore shadows -> AO -> fog.
			if (shadowApplies)
				{ Effects.ShadowsExteriors->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.ShadowsExteriors); }
			if (interiorApplies) interiorApply();
			if (AO->deferredReady) {
				AO->combineOnly = true;
				{ AO->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(AO); }
				AO->combineOnly = false;
			}
			{ Fog->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Fog); }
		}
#if NVR_TEST_TOOLS
		if (foldCheckFrame && (composite ? applied : !aoApplies)) InteriorFoldSelfCheck(Device, RenderTarget, SourceSurface, checkAO, checkAOTexture, checkSky);
#endif
		AO->deferredReady = false;
	}
	{
		// UNOFFICIAL: volumetric sun light, after the fog (it darkens the fog's evenly lit air where the sun is shaded).
		// The half-resolution march, history and blur into the effect's own buffers, then the composite in the chain.
		GpuProfileScope gpu(volumetricLightTimer, Device);
		if (lightGrid) {
			if (!Fog->gridApplied)
				{ Effects.VolumetricLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 8, false, SourceSurface); PixelInspector::AfterFrame(Effects.VolumetricLight); }
		}
		else if (wouldRender(Effects.VolumetricLight)) {
			Effects.VolumetricLight->RenderBuffers(Device);
			Device->SetRenderTarget(0, RenderTarget);
			{ Effects.VolumetricLight->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, Effects.VolumetricLight->CompositeTechnique(), false, SourceSurface); PixelInspector::AfterFrame(Effects.VolumetricLight); }
		}
		Fog->gridTexture = nullptr;
	}
	{
		// UNOFFICIAL (from Physically-Based-Reloaded, Balebandro / Alejandro Martinez; ported again 2026-10-18, sandport): dust
		// storms, after the fog (which it thickens into dust), in HDR before the god rays, exposure and bloom, so the sun shafts shine
		// through and the dust is lit and blooms with the scene. Nothing is drawn outside a storm (SandstormEffect::ShouldRender).
		GpuProfileScope gpu(sandstormTimer, Device);
		// UNOFFICIAL 2026-10-18 (sandfast): FastPath (the default) draws the dust grid and the half-resolution streaks and glow into
		// the effect's own buffers, then its composite (technique 5) in the chain; otherwise the full-resolution original (technique 0).
		if (wouldRender(Effects.Sandstorm) && Effects.Sandstorm->UseFastPath()) {
			Effects.Sandstorm->RenderBuffers(Device);
			Device->SetRenderTarget(0, RenderTarget);
			{ Effects.Sandstorm->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 5, false, SourceSurface); PixelInspector::AfterFrame(Effects.Sandstorm); }
		}
		else { Effects.Sandstorm->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Sandstorm); }
	}
	{
		GpuProfileScope gpu(godRaysTimer, Device);
		{ Effects.GodRays->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, true, SourceSurface); PixelInspector::AfterFrame(Effects.GodRays); }
	}

	{
		GpuProfileScope gpu(hdrTimer, Device);
		// calculate average luma for use by shaders
		if (avglumaRequired) {
			GpuProfileScope gpuLuma(avgLumaTimer, Device);
			// UNOFFICIAL 2026-10-23 (NVR DXVK): the compute version when AvgLumaCompute is on and the NVR DXVK extension is
			// present (effects\AvgLuma.cpp); otherwise, and when it fails, the D3D9 pass as before.
			const bool computed = Effects.AvgLuma->RenderCompute(Device);
			if (!computed) {
				RenderEffectToRT(Effects.AvgLuma->Textures.AvgLumaSurface, Effects.AvgLuma, NULL);
				Device->SetRenderTarget(0, RenderTarget); 	// restore device used for effects
			}
			Effects.AvgLuma->CheckCompute(Device, !computed && TheSettingManager->SettingsMain.Main.AvgLumaComputeCheck > 0);	// test tool, off by default
		}
		{
			GpuProfileScope gpuExposure(exposureTimer, Device);
			// UNOFFICIAL (postchain): with MaxBrightness 0 (the Night default) the pass returns delinearize(linearize(c)), which is c
			// again at the buffer's precision (c is a half float; the round trip's 1e-6 error rounds back to it): a full-screen
			// read and write for nothing. Skipped, the image is left where it is (no chain commit).
			if (!(TheSettingManager->SettingsMain.Main.SkipIdentityExposure && Effects.Exposure->Constants.Data.y == 0.0f))
				{ Effects.Exposure->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Exposure); }
		}
		{
			GpuProfileScope gpuBloom(bloomTimer, Device);
			Effects.Bloom->RenderBloomBuffer(RenderTarget);
		}
	}

	{
		GpuProfileScope gpu(preColorTimer, Device);
		{ Effects.Lens->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Lens); }
		if (Effects.LUT->Settings.PreTonemapping)
			{ Effects.LUT->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.LUT); }
	}

	timer.LogTime("ShaderManager::RenderEffectsPreTonemapping");
}


// For A/B runs: the Performance and ReducedQuality switches in one log line, written when the F10 profile starts and
// again whenever one of them changes while it runs, so each stretch of timings can be matched to its settings.
// Lists whatever the defaults file has in those two sections, so new switches show up without touching this.
// UNOFFICIAL (2026-10-11): the game's active image-space effects (TES::activeIMODs: fades, night eye, chem tints, the
// wait-menu fade), logged with F10. For a screen tint or glare that stays when every NVR effect is off: the game's own
// final shader blends these in, so a stuck one shows here. The instance layout is not known, so each instance is named by
// its vtable, the image-space modifier form it points at is found by scanning its first words for a readable form of that
// type, and its first 12 words are dumped for decoding.
static const char* ImageSpaceInstanceKind(UInt32 vtable) {	// vtables from nvse/class_vtbls.h (kVtbl_ImageSpaceModifierInstance*)
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
	// Close the frame-time window at the change, so no FRAME TIMES line mixes frames from before and after.
	if (!force) TheFrameTimeMonitor().Flush();
	lastLine = line;
	Logger::Log("SWITCHES%s", line.c_str());
}

/*
* Renders the effect that have been set to enabled.
*/
void ShaderManager::RenderEffects(IDirect3DSurface9* RenderTarget) {
	VolumetricSmoke::FrameNumber++;   // the smoke is fetched once a frame (VolumetricSmoke::Fetch)
	// F10 profiling toggle and the frame interval run before the RenderEffects check, so an
	// effects-off run still logs its real frame time for comparison. This is the last NVR call
	// of the frame, after the pre-tonemap chain, so the toggle takes effect from the next frame.
	static CpuTimer frameIntervalTimer("Frame interval (CPU)");
	if (Player->parentCell && !InterfaceManager->IsActive(Menu::kMenuType_Loading) && Global->OnKeyDown(0x44)) {
		GpuTimer::Enabled = !GpuTimer::Enabled;
		Logger::Log("GPU PROFILE P78 test (forward quality + culling + lamp trims) %s (F10), effects %s, D3D9 runtime: %s", GpuTimer::Enabled ? "enabled" : "paused",
			TheSettingManager->SettingsMain.Main.RenderEffects ? "on" : "OFF", TheRenderManager->D3D9RuntimeDescription());
		LogActiveImageSpaceEffects();
		if (!GpuTimer::Enabled) TheFrameTimeMonitor().Flush(); // report the frames collected so far
		else LogActiveSwitches(true);
	}
	if (GpuTimer::Enabled) {
		static unsigned switchCheckFrame = 0;
		if (++switchCheckFrame >= 30) { switchCheckFrame = 0; LogActiveSwitches(false); }
		// Frames within three seconds of a cell change or loading screen are counted separately: the
		// hitches there are expected (streaming), the ones in steady play are what hurt the 1% lows.
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
	// UNOFFICIAL 2026-10-26 pixel inspector: the picture out of the tonemapper, before NVR's post effects.
	PixelInspector::CaptureIf("Tonemapped (before the post effects)", TheTextureManager->RenderedSurface);
	// UNOFFICIAL: the clipping view's modes 2 and 3 compare with the picture as it arrives here, straight out of the tonemapper.
	Effects.Debug->CaptureClipStart(Device, RenderTarget);

	// UNOFFICIAL 2026-10-14: neighbouring per-pixel passes in one. Coloring then the LUT (Coloring.fx.hlsl's ColoringLUT), and Cinema
	// then the sky dither (Cinema.fx.hlsl's CinemaSkyDither) when nothing renders between them (image adjust idle, no body camera).
	// Same maths; the colour is just not rounded to the buffer's precision in between.
	auto renders = [](EffectRecord* effect) { return effect && effect->Enabled && effect->Effect && effect->ShouldRender(); };
	const bool colorLUT = renders(Effects.Coloring) && !Effects.LUT->Settings.PreTonemapping && renders(Effects.LUT) &&
		Effects.Coloring->Effect->GetTechniqueByName("ColoringLUT");
	const bool cinemaDither = renders(Effects.Cinema) && renders(Effects.SkyDither) && !renders(Effects.ImageAdjust) &&
		!renders(Effects.Bodycam) && Effects.Cinema->Effect->GetTechniqueByName("CinemaSkyDither");

	// Name the effect that will render last so its final pass writes the game target directly and
	// the chain ends without a copy (FrameChain::SetFinalEffect). Same order as the calls below; the
	// tests are the ones EffectRecord::Render applies. A wrong guess is repaired in FrameChain::Owns.
	{
		EffectRecord* const order[] = {
			Effects.Rain, Effects.Snow, Effects.BloomLegacy, Effects.HeatHaze, Effects.VolumetricSmoke, Effects.BarrelHaze, Effects.Coloring, Effects.LUT, Effects.DepthOfField,
			Effects.MotionBlur, Effects.BloodLens, Effects.WaterLens, Effects.LowHF, Effects.DitherBuster,
			Effects.SMAA, Effects.FXAA, Effects.Sharpening, Effects.Cinema, Effects.ImageAdjust, Effects.Bodycam, Effects.SkyDither, Effects.Debug,
			TheCoverMap };	// UNOFFICIAL 2026-10-27: the cover map's debug view (PersistentDebugView), last
		EffectRecord* last = nullptr;
		for (EffectRecord* effect : order)
			if (effect && effect->Enabled && effect->Effect && effect->ShouldRender() &&
				!(colorLUT && effect == Effects.LUT) && !(cinemaDither && effect == Effects.SkyDither)) last = effect;
		if (Effects.Debug->ClipViewActive()) last = Effects.Debug; // UNOFFICIAL: the clipping view draws after everything
		Chain.SetFinalEffect(last);
	}

	// UNOFFICIAL TAA (upstream #78, via Physically-Based-Reloaded; off by default) first: after tonemapping, so it resolves
	// LDR values that cannot ghost as HDR highlights do, and ahead of everything below. Rain and snow are particles with no
	// depth of their own to reproject by, DoF and motion blur want the stable image as input, and the lens effects and
	// cinema overlay are fixed to the screen. TAAEffect::Render manages its own targets and syncs the frame chain first.
	{
		GpuProfileScope gpu(taaTimer, Device);
		{ Effects.TAA->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.TAA); }
	}

	{
		GpuProfileScope gpu(weatherTimer, Device);
		{ Effects.Rain->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Rain); }
		{ Effects.Snow->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Snow); }
		// UNOFFICIAL 2026-10-26: GPU-simulated rain / snow drops (off by default; needs NVR DXVK 1.5-particles, nothing without it)
		{
			static GpuTimer gpuParticlesTimer("GPU particles (sim + draw)");
			GpuProfileScope gpuParticles(gpuParticlesTimer, Device);
			GpuParticles::Render(Device, RenderTarget);
		}
		{ Effects.BloomLegacy->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.BloomLegacy); }
	}
	{
		// UNOFFICIAL (from Physically-Based-Reloaded): distant ground shimmering on hot afternoons. Before the gun smoke,
		// so the smoke near the camera is not pulled into the shimmer of what is behind it.
		GpuProfileScope gpu(heatHazeTimer, Device);
		{ Effects.HeatHaze->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.HeatHaze); }
	}

	// GunFX: volumetric smoke first, then the heat haze distorts the result.
	{ Effects.VolumetricSmoke->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, nullptr); PixelInspector::AfterFrame(Effects.VolumetricSmoke); }
	{ Effects.BarrelHaze->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, nullptr); PixelInspector::AfterFrame(Effects.BarrelHaze); }

	// screenspace coloring/blurring effects get rendered last
	{
		GpuProfileScope gpu(colorTimer, Device);
		{ Effects.Coloring->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, colorLUT ? 1 : 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Coloring); }
		if (!Effects.LUT->Settings.PreTonemapping && !colorLUT)
			{ Effects.LUT->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.LUT); }
	}
	{
		GpuProfileScope gpu(dofTimer, Device);
		// Distant blur does not need the six-pass autofocus/bokeh pipeline.
		const UINT technique = !Effects.DepthOfField->Constants.Enabled && Effects.DepthOfField->Constants.Blur.x ? 1 : 0;
		// The distant-only technique reads TESR_RenderedBuffer and never TESR_SourceBuffer, so it
		// skips the full-resolution copy the full pipeline needs.
		{ Effects.DepthOfField->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, technique, false, technique == 1 ? nullptr : SourceSurface); PixelInspector::AfterFrame(Effects.DepthOfField); }
	}
	{
		GpuProfileScope gpu(motionBlurTimer, Device);
		{ Effects.MotionBlur->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.MotionBlur); }
	}

	// lens effects
	{
		GpuProfileScope gpu(lensTimer, Device);
		{ Effects.BloodLens->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.BloodLens); }
		{ Effects.WaterLens->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.WaterLens); }
		{ Effects.LowHF->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.LowHF); }
	}
	{
		GpuProfileScope gpu(ditherTimer, Device);
		{ Effects.DitherBuster->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, Effects.DitherBuster->TechniqueIndex(), false, SourceSurface); PixelInspector::AfterFrame(Effects.DitherBuster); }
	}
	{
		GpuProfileScope gpu(smaaTimer, Device);
		{ Effects.SMAA->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.SMAA); }
	}
	{
		GpuProfileScope gpu(fxaaTimer, Device);
		{ Effects.FXAA->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.FXAA); }
	}
	{
		GpuProfileScope gpu(sharpenTimer, Device);
		{ Effects.Sharpening->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Sharpening); }
	}
	{
		GpuProfileScope gpu(cinemaTimer, Device);
		{ Effects.Cinema->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, cinemaDither ? 1 : 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Cinema); }
	}
	{
		GpuProfileScope gpu(imageAdjustTimer, Device);
		{ Effects.ImageAdjust->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.ImageAdjust); }
	}
	{
		// UNOFFICIAL, testbed: the body-camera look, on the finished picture (the sky dither still comes after it).
		GpuProfileScope gpu(bodycamTimer, Device);
		{ Effects.Bodycam->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Bodycam); }
	}
	{
		// UNOFFICIAL: the last colour change before the 8-bit screen, so its noise breaks up the sky's bands exactly
		// where they form (anything after it, e.g. sharpening, would amplify the noise).
		GpuProfileScope gpu(skyDitherTimer, Device);
		if (!cinemaDither) { Effects.SkyDither->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.SkyDither); }
	}
	// UNOFFICIAL 2026-10-27 FitToDepthDebug ([Shaders.ShadowsExteriors.Main], core\SunDepthFit.h): the sun cascades coloured, or the
	// measured blocks, over the finished picture. 0 (the default): nothing runs.
	if (SunDepthFit::DebugView > 0) SunDepthFit::RenderView(Device, RenderTarget);
	RtSunShadows::RenderDebug(Device, RenderTarget);	// UNOFFICIAL 2026-10-28 [Shaders.ShadowsExteriors.RT] DebugView 4 (nothing otherwise)
	{
		GpuProfileScope gpu(imageAdjustTimer, Device);
		{ Effects.Debug->Render(Device, RenderTarget, TheTextureManager->RenderedSurface, 0, false, SourceSurface); PixelInspector::AfterFrame(Effects.Debug); }
		Effects.Debug->RenderClipView(Device, RenderTarget); // UNOFFICIAL: [Shaders.Debug.Main] ClipView
	}
	if (TheCoverMap) TheCoverMap->RenderDebug(Device, RenderTarget, SourceSurface);	// UNOFFICIAL 2026-10-27 (PersistentDebugView; does nothing when 0)
	if (Effects.SunFar->Settings.FlickerWatch && GameState.isExterior)	// UNOFFICIAL 2026-10-23 test only (core\FlickerWatch.h)
		FlickerWatch::OnFrame(Device, RenderTarget, Effects.SunFar->Settings.FlickerThreshold);
	else FlickerWatch::Flags = 0;

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

	// UNOFFICIAL: a menu entry with only settings (Shaders.<Name>.Status Enabled, e.g. ContactHardening, which
	// ShadowManager reads each frame) just flips its setting.
	// SetMenuShaderEnabled picks the setting (Status, or [Shaders.<Name>.Main] Enabled for Cloth/CloudShadows; none for Foliage).
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

// The predicted last effect wrote the finished image into the game target, but another effect is
// about to render after it. Put the image back where the chain expects it (Surf[Current]) so that
// effect samples the right thing; a wrong prediction costs one copy instead of a wrong image.
void FrameChain::ReclaimFinal() {
	if (!FinalWritten) return;
	TheRenderManager->device->StretchRect(GameTarget, NULL, Surf[Current], NULL, D3DTEXF_NONE);
	FinalWritten = false;
	FinalEffect = nullptr;
}

// Point the TESR_RenderedBuffer slot at the current image. Effect samplers follow the slot
// (TextureRecord::TextureRef), so this is all a swap needs.
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

	// One set of NVR textures per target format (pre-tonemap HDR and post-tonemap LDR).
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
	if (index < 0) return false; // a third format: stay on the legacy path rather than churn
	Pair& pair = Pairs[index];

	// Use the game target itself as one buffer when it is a texture level (not the back buffer).
	// Single-level only: NVR's own buffers have no mip chain, and effects that sample the rendered
	// buffer at reduced size (average luma, bloom downsample) must not pick up stale mip levels.
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
	Writes++; LastWrite = 1;	// UNOFFICIAL 2026-10-26 (pixel inspector)
	Current ^= 1;
	Publish();
}

void FrameChain::Sync() {
	if (!Active) return;
	Writes++; LastWrite = 3;	// UNOFFICIAL 2026-10-26 (pixel inspector): the effect then draws into the game target
	ReclaimFinal();
	IDirect3DDevice9* Device = TheRenderManager->device;
	if (Surf[Current] == GameTarget) {
		// The game target already holds the image; move the current image to the other buffer
		// so the legacy path does not sample the texture it renders into.
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
		// Shows whether a chain ends on the wrong buffer (~0 ms when it does not): the HDR chain (game
		// texture) copies when its pass count is odd, the LDR chain (back buffer) unless its last pass
		// wrote the game target itself (FinalWritten).
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
