#include "Reflections.h"
#include "../core/GpuProfiler.h"
#include "../core/ReflMaterial.h"
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-25 (NVR DXVK)
#include "../core/HiZ.h"	// UNOFFICIAL 2026-10-26 (NVR DXVK, deep/fork-hiz)

void ReflectionsEffect::UpdateSettings() {
	// UNOFFICIAL 2026-10-18: the frames drawn with the settings that are about to be replaced. 2026-10-19: only while the F10
	// profile runs (as the 240-frame report): every settings change reruns this, so dragging a menu slider wrote dozens of lines.
	if (GpuTimer::Enabled) ReportHistory("settings changed");
	const char* section = "Shaders.Reflections.Main";
	Settings.GroundSheen = std::clamp(TheSettingManager->GetSettingF(section, "GroundSheen"), 0.0f, 2.0f);
	Settings.RainWetness = std::clamp(TheSettingManager->GetSettingF(section, "RainWetness"), 0.0f, 2.0f);
	Settings.InteriorFloors = std::clamp(TheSettingManager->GetSettingF(section, "InteriorFloors"), 0.0f, 2.0f);
	Settings.Roughness = std::clamp(TheSettingManager->GetSettingF(section, "Roughness"), 0.02f, 1.0f);
	Settings.WetRoughness = std::clamp(TheSettingManager->GetSettingF(section, "WetRoughness"), 0.02f, 1.0f);
	Settings.MaxDistance = std::clamp(TheSettingManager->GetSettingF(section, "MaxDistance"), 200.0f, 20000.0f);
	Settings.Steps = std::clamp(TheSettingManager->GetSettingI(section, "Steps"), 8, 64);
	Settings.Thickness = std::clamp(TheSettingManager->GetSettingF(section, "Thickness"), 0.1f, 10.0f);
	Settings.Temporal = std::clamp(TheSettingManager->GetSettingF(section, "Temporal"), 0.0f, 0.95f);
	Settings.BlurRadius = std::clamp(TheSettingManager->GetSettingF(section, "BlurRadius"), 0.0f, 8.0f);
	Settings.DrawDistance = std::clamp(TheSettingManager->GetSettingF(section, "DrawDistance"), 500.0f, 50000.0f);
	Settings.SkyFallback = TheSettingManager->GetSettingI(section, "SkyFallback") != 0;
	Settings.CubeMap = TheSettingManager->GetSettingI(section, "CubeMap") != 0;
	Settings.CubeCapture = std::clamp(TheSettingManager->GetSettingF(section, "CubeCapture"), 0.02f, 1.0f);
	Settings.CubeFade = std::clamp(TheSettingManager->GetSettingF(section, "CubeFade"), 0.0f, 0.2f);
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(section, "DebugView"), 0, 10);	// 9, 10: UNOFFICIAL 2026-10-26 the Hi-Z walk's end rule / skips (HiZ)	// 8: SpecularOcclusion (diagnostic)	// 4: UNOFFICIAL 2026-10-19 the rain's wet mask; 5, 6 the light grid; 7 the material buffer
	Settings.GridFallback = TheSettingManager->GetSettingI(section, "GridFallback") != 0;
	Settings.GridBrightness = std::clamp(TheSettingManager->GetSettingF(section, "GridBrightness"), 0.0f, 4.0f);
	// UNOFFICIAL 2026-10-20 MaterialAware (core/ReflMaterial.h): needs the shaders compiled with it (on at startup, with the _rmaos maps).
	Settings.MaterialAware = ReflMaterial::CompiledIn() ? std::clamp(TheSettingManager->GetSettingI(section, "MaterialAware"), 0, 2) : 0;
	Settings.MaterialReflect = std::clamp(TheSettingManager->GetSettingF(section, "MaterialReflect"), 0.0f, 2.0f);
	Settings.MaterialMaxRoughness = std::clamp(TheSettingManager->GetSettingF(section, "MaterialMaxRoughness"), 0.05f, 1.0f);
	// UNOFFICIAL 2026-10-21 ReflectAdd: live, but only compiled into the effect when above 0 at startup (ReflMaterial::AddCompiledIn).
	Settings.ReflectAdd = ReflMaterial::AddCompiledIn() ? std::clamp(TheSettingManager->GetSettingF(section, "ReflectAdd"), 0.0f, 1.0f) : 0.0f;
	// UNOFFICIAL 2026-10-21 SpecularOcclusion: live, compiled into the effect only when above 0 at startup (ReflMaterial::OccCompiledIn).
	Settings.SpecularOcclusion = ReflMaterial::OccCompiledIn() ? std::clamp(TheSettingManager->GetSettingF(section, "SpecularOcclusion"), 0.0f, 1.0f) : 0.0f;
	Settings.SpecularOcclusionLevel = std::clamp(TheSettingManager->GetSettingF(section, "SpecularOcclusionLevel"), 0.05f, 4.0f);
	Settings.HiZ = TheSettingManager->GetSettingI(section, "HiZ") != 0;	// UNOFFICIAL 2026-10-26 (NVR DXVK, core/HiZ.h)
	// UNOFFICIAL 2026-10-25 Reflectivity: the PBR section's one reflectivity control reaches the floor reflections too (a missing key: 1).
	SettingManager::Configuration::ConfigNode node;
	Settings.ReflectivityExterior = TheSettingManager->Config.FillNode(&node, "Shaders.PBR.Main", "ReflectivityExterior") ? std::clamp(node.FloatValue, 0.0f, 3.0f) : 1.0f;
	Settings.ReflectivityInterior = TheSettingManager->Config.FillNode(&node, "Shaders.PBR.Main", "ReflectivityInterior") ? std::clamp(node.FloatValue, 0.0f, 3.0f) : 1.0f;
	Settings.HiZInteriorsTest = TheSettingManager->GetSettingI(section, "HiZInteriorsTest") != 0;	// test only: HiZ indoors too
}

// UNOFFICIAL 2026-10-26 (deep/fork-hiz): outdoors only for now. Indoors (Lucky 38, a 3rd-person view looking down) the walk found no
// hits on the near floor where the fixed-step trace finds the rail and the counter (bulk runs 2026-10-26); the old trace stays there.
bool ReflectionsEffect::HiZWanted() const {
	return Settings.HiZ && !ExtFailed && !hizTraceFailed && (TheShaderManager->GameState.isExterior || Settings.HiZInteriorsTest);
}

void ReflectionsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_ReflectionsData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_ReflectionsData2", &Constants.Data2);
	TheShaderManager->RegisterConstant("TESR_ReflectionsData3", &Constants.Data3);
	TheShaderManager->RegisterConstant("TESR_ReflectionsData4", &Constants.Data4);
	TheShaderManager->RegisterConstant("TESR_ReflectionsLayout", &Constants.Layout);
	TheShaderManager->RegisterConstant("TESR_ReflectionsReproject", (D3DXVECTOR4*)&Constants.Reproject);
	TheShaderManager->RegisterConstant("TESR_ReflectionsCube", &Constants.Cube);
	TheShaderManager->RegisterConstant("TESR_ReflectionsCube2", &Constants.Cube2);
	TheShaderManager->RegisterConstant("TESR_ReflectionsGrid", &Constants.Grid);
	TheShaderManager->RegisterConstant("TESR_ReflectionsGrid2", &Constants.Grid2);
	TheShaderManager->RegisterConstant("TESR_ReflectionsGrid3", &Constants.Grid3);
	TheShaderManager->RegisterConstant("TESR_ReflectionsMaterial", &Constants.Material);
}

void ReflectionsEffect::RegisterTextures() {
	const UINT w = MaxQuality::Reduced(TheRenderManager->width, 2), h = MaxQuality::Reduced(TheRenderManager->height, 2);
	TheTextureManager->InitTexture("TESR_ReflectionsTrace", &Textures.TraceTexture, &Textures.TraceSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_ReflectionsBuffer", &Textures.BufferTexture, &Textures.BufferSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_ReflectionsTemp", &Textures.TempTexture, &Textures.TempSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_ReflectionsHistory", &Textures.HistoryTexture, &Textures.HistorySurface, w, h, D3DFMT_A16B16G16R16F);
	Constants.Layout = D3DXVECTOR4(1.0f / w, 1.0f / h, 0.0f, 0.0f);
	// UNOFFICIAL 2026-10-20 MaterialAware: the material buffer (full resolution, 8 bits a channel: r roughness, g metal, b known), only
	// when the shaders were compiled with it.
	if (ReflMaterial::CompiledIn() &&
		(FAILED(TheRenderManager->device->CreateTexture(TheRenderManager->width, TheRenderManager->height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
			D3DPOOL_DEFAULT, &Textures.MaterialTexture, NULL)) || FAILED(Textures.MaterialTexture->GetSurfaceLevel(0, &Textures.MaterialSurface)))) {
		Logger::Log("UNOFFICIAL Reflections: the material buffer could not be created; MaterialAware is off.");
		if (Textures.MaterialTexture) { Textures.MaterialTexture->Release(); Textures.MaterialTexture = nullptr; }
		Textures.MaterialSurface = nullptr;
	}

	// The surroundings cube (FP16, about 0.6 MB in all). If any part fails, the cube stays off (CubeMap is ignored).
	IDirect3DDevice9* device = TheRenderManager->device;
	bool ok = true;
	for (int c = 0; c < 2 && ok; c++) {
		ok = SUCCEEDED(device->CreateCubeTexture(CubeSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &Cubes.Capture[c], NULL));
		for (int f = 0; f < 6 && ok; f++) ok = SUCCEEDED(Cubes.Capture[c]->GetCubeMapSurface((D3DCUBEMAP_FACES)f, 0, &Cubes.CaptureFace[c][f]));
	}
	if (ok) ok = SUCCEEDED(device->CreateCubeTexture(CubeSize, CubeLevels, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &Cubes.Env, NULL));
	for (UINT m = 0; m < CubeLevels && ok; m++)
		for (int f = 0; f < 6 && ok; f++) ok = SUCCEEDED(Cubes.Env->GetCubeMapSurface((D3DCUBEMAP_FACES)f, m, &Cubes.EnvFace[m][f]));
	if (!ok) {
		Logger::Log("UNOFFICIAL Reflections: the surroundings cube could not be created; reflections use the sky only.");
		if (Cubes.Env) { Cubes.Env->Release(); Cubes.Env = nullptr; }
	}
	TheTextureManager->RegisterTexture("TESR_ReflectionsCapture", &Cubes.CaptureLastSlot);
	TheTextureManager->RegisterTexture("TESR_ReflectionsCaptureNew", &Cubes.CaptureNewSlot);
	TheTextureManager->RegisterTexture("TESR_ReflectionsEnv", &Cubes.EnvSlot);
	TheTextureManager->RegisterTexture("TESR_ReflectionsGridLight", &GridLightSlot);	// UNOFFICIAL 2026-10-19 (GridStep)
	TheTextureManager->RegisterTexture("TESR_ReflectionsGridColor", &GridColorSlot);
}

// UNOFFICIAL 2026-10-18: one line on what the trace's history and the surroundings cube did since the last report (only when it drew).
void ReflectionsEffect::ReportHistory(const char* when) {
	if (!stats.drawn && !cubeSteps) return;
	char text[256];
	stats.Describe(text, sizeof(text));
	Logger::Log("UNOFFICIAL Reflections temporal history (%s, Temporal %.2f): %s; cube: %u updates, %u emptied first; light grid: read on %u of %u frames with GridFallback on",
		when, Settings.Temporal, text, cubeSteps, cubeEmptied, gridUsedFrames, gridFrames);
	stats.Reset();
	cubeSteps = cubeEmptied = 0;
	gridFrames = gridUsedFrames = 0;
}

// UNOFFICIAL 2026-10-19 GridFallback: AmbientLight's light grid as the far / off-screen fallback of the trace. Read only while
// AmbientLight keeps a grid up (on, GridBounce above 0) and it was finished a moment ago (it is redrawn every few frames, and
// at once when it moves); its corner goes with the finished grid. AmbientLight draws before this effect in the frame, so
// the grid read here is this frame's (or the last one it made). Nothing extra is drawn: the trace reads what is there.
void ReflectionsEffect::GridStep() {
	const AmbientLightEffect* ambient = TheShaderManager->Effects.AmbientLight;
	const UINT now = TheFrameRateManager->FrameNumber;
	// Indoors only (user 2026-10-19: outdoors it measured ~nothing, rays there go to the sky and the cube).
	bool use = Settings.GridFallback && !TheShaderManager->GameState.isExterior && ambient && ambient->Enabled && ambient->Effect && ambient->gridBounce > 0.0f &&
		ambient->gridValid && now - ambient->gridRenderedFrame <= 8 && !TheShaderManager->GameState.isCellChanged;
	IDirect3DTexture9* light = use ? ambient->gridAccum[ambient->gridAccumIndex] : nullptr;
	IDirect3DTexture9* colour = use && ambient->gridColorValid ? ambient->gridAccumColor[ambient->gridAccumIndex] : nullptr;
	use = use && light;
	if (Settings.GridFallback) {
		gridFrames++;
		if (use) gridUsedFrames++;
	}
	GridLightSlot = light;
	GridColorSlot = colour;
	Constants.Grid2 = D3DXVECTOR4(use ? 1.0f : 0.0f, Settings.GridBrightness, colour ? 1.0f : 0.0f, 0.0f);
	if (!use) return;
	const NiPoint3 camera = WorldSceneGraph->camera->m_worldTransform.pos;
	Constants.Grid = D3DXVECTOR4((float)(ambient->gridOrigin[0] - camera.x), (float)(ambient->gridOrigin[1] - camera.y),
		(float)(ambient->gridOrigin[2] - camera.z), ambient->gridCell);
	Constants.Grid3 = D3DXVECTOR4(ambient->sceneLight.x, ambient->sceneLight.y, ambient->sceneLight.z, 0.0f);
}

void ReflectionsEffect::UpdateConstants() {
	const bool exterior = TheShaderManager->GameState.isExterior;

	// Wetness: the rain falling now. UNOFFICIAL 2026-10-19 (P81 tester report: "wet ground / reflections stay after rain in
	// shadowed areas"): it used to follow Wet World's puddle level too, which dries over minutes, so ALL flat ground kept
	// mirroring long after the rain (seen most in shade, where the bright surroundings stand out against the dark ground)
	// while Wet World itself had dried everything but the puddles in hollows. The puddles keep their own wet look (Wet World).
	const float wet = std::clamp(TheShaderManager->Effects.Rain->Constants.RainAnimator.GetValue(), 0.0f, 1.0f);

	const float reflectivity = exterior ? Settings.ReflectivityExterior : Settings.ReflectivityInterior;	// UNOFFICIAL 2026-10-25
	Constants.Data.x = (exterior ? Settings.GroundSheen : Settings.InteriorFloors) * reflectivity;
	Constants.Data.y = exterior ? wet * Settings.RainWetness : 0.0f;
	// UNOFFICIAL 2026-10-19: the rain wets only open ground (the ortho map's roof test, as Wet World): kept up while it rains here.
	Constants.Data4.y = Constants.Data.y > 0.0f ? 1.0f : 0.0f;
	if (Constants.Data4.y > 0.0f) TheShaderManager->orthoRequired = true;
	Constants.Data.z = Settings.Roughness;
	Constants.Data.w = Settings.WetRoughness;
	Constants.Data2.x = Settings.MaxDistance;
	Constants.Data2.y = (float)Settings.Steps;
	Constants.Data2.z = Settings.Thickness;
	Constants.Data3.y = Settings.BlurRadius;
	Constants.Data3.z = (float)Settings.DebugView;
	Constants.Data3.w = exterior && Settings.SkyFallback ? 1.0f : 0.0f;
	Constants.Data4.x = Settings.DrawDistance;
	Constants.Data4.z = Settings.ReflectAdd;
	Constants.Data4.w = Settings.SpecularOcclusion;
	Constants.Layout.z = Settings.SpecularOcclusionLevel;
	// UNOFFICIAL 2026-10-20 MaterialAware: x is set again by CaptureMaterial (1 only when the buffer was filled this frame).
	Constants.Material = D3DXVECTOR4(Settings.MaterialAware == 1 && materialFilled ? 1.0f : 0.0f, Settings.MaterialMaxRoughness, Settings.MaterialReflect * reflectivity, 0.0f);
}

// UNOFFICIAL 2026-10-20 MaterialAware (core/ReflMaterial.h): the world scene left each known material in the scene's alpha; it is
// copied out into the material buffer (technique 7) at the start of the chain, before any effect writes the scene.
void ReflectionsEffect::CaptureMaterial(IDirect3DDevice9* Device) {
	materialFilled = false;
	Constants.Material.x = 0.0f;
	if (!Enabled || !Effect || !Textures.MaterialSurface || Settings.MaterialAware <= 0 || TheShaderManager->GameState.isUnderwater) return;
	static GpuTimer timer("Reflections material copy");
	GpuProfileScope gpu(timer, Device);
	materialOnly = true;
	Device->SetRenderTarget(0, Textures.MaterialSurface);
	Render(Device, Textures.MaterialSurface, Textures.MaterialSurface, 7, false, Textures.MaterialSurface);
	materialOnly = false;
	materialFilled = true;
	Constants.Material.x = Settings.MaterialAware == 1 ? 1.0f : 0.0f;
}

void ReflectionsEffect::BindMaterial() {
	if (!materialFilled || !Textures.MaterialTexture) return;
	SetPassTexture(15, Textures.MaterialTexture);	// set after each pass begins (EffectRecord::PassTextures)
	const DWORD point[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
		{D3DSAMP_MAGFILTER, D3DTEXF_POINT}, {D3DSAMP_MINFILTER, D3DTEXF_POINT}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
	for (const auto& s : point) TheRenderManager->SetSamplerState(15, (D3DSAMPLERSTATETYPE)s[0], s[1]);
}

bool ReflectionsEffect::ShouldRender() {
	if (cubeOnly || materialOnly) return true;	// RenderCubeOnly's / CaptureMaterial's own passes
	const NormalsEffect* normals = TheShaderManager->Effects.Normals;
	return !TheShaderManager->GameState.isUnderwater && normals && normals->Enabled &&
		(Constants.Data.x > 0.0f || Constants.Data.y > 0.0f || (Constants.Material.x > 0.0f && Constants.Material.z > 0.0f)) &&
		Textures.TraceSurface && Textures.BufferSurface && Textures.TempSurface && Textures.HistorySurface;
}

// The half-resolution passes (techniques: 1 Trace, 3 Temporal, 2 Blur X, 4 Blur Y), as VolumetricLight's:
// Temporal on: Trace -> Trace buffer, Temporal -> Temp (copied to History), Blur X -> Trace, Blur Y -> Buffer.
// Off: Trace -> Temp, then the same blurs. The composite (technique 0) then reads Buffer.
void ReflectionsEffect::RenderBuffers(IDirect3DDevice9* Device) {
	const bool temporal = Settings.Temporal > 0.0f;
	const D3DXMATRIX& view = TheRenderManager->viewMatrix;
	const D3DXMATRIX& invView = TheRenderManager->InvViewMatrix;

	// Last frame's result is usable if this effect drew it, and the camera did not jump (load, fast travel, a cut).
	// UNOFFICIAL 2026-10-18: counted in rendered frames (FrameNumber), not UpdateConstants calls (two a frame, which made this never pass).
	const UINT now = TheFrameRateManager->FrameNumber;
	const bool drewLastFrame = lastRenderedFrame + 1 == now;
	bool usable = temporal && haveHistory && drewLastFrame;
	const double move[3] = { (double)invView._41 - lastInvView._41, (double)invView._42 - lastInvView._42,
		(double)invView._43 - lastInvView._43 };
	if (usable) usable = move[0] * move[0] + move[1] * move[1] + move[2] * move[2] < 500.0 * 500.0;
	stats.Note(temporal, haveHistory, drewLastFrame, usable);
	if (GpuTimer::Enabled && stats.drawn >= 240) ReportHistory("240 frames");
	if (usable) {
		// Camera-relative world -> last frame's view, in double round the two camera positions (see VolumetricLight.cpp).
		D3DXMATRIX toLastView;
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) toLastView.m[i][j] = lastView.m[i][j];
			toLastView.m[i][3] = 0.0f;
		}
		for (int j = 0; j < 3; j++)
			toLastView.m[3][j] = (float)(move[0] * lastView.m[0][j] + move[1] * lastView.m[1][j] + move[2] * lastView.m[2][j]);
		toLastView.m[3][3] = 1.0f;
		D3DXMatrixMultiply(&Constants.Reproject, &toLastView, &lastProj);
	}
	Constants.Data3.x = usable ? Settings.Temporal : 0.0f;
	Constants.Data2.w = temporal ? (float)(now % 64) : 0.0f;

	// The surroundings cube, before the trace reads it; the light grid's constants.
	CubeStep(Device);
	GridStep();

	auto pass = [&](IDirect3DSurface9* target, UINT technique) {
		Device->SetRenderTarget(0, target);
		Render(Device, target, target, technique, false, target);
	};
	// UNOFFICIAL 2026-10-19 Skylighting v3 phase 1 ([Shaders.AmbientOcclusion.Skylighting] Reflections; after Community Shaders' Skylighting
	// MixSpecular): the sky visibility on stage 14 for the trace (11 is TESR_OrthoMapBuffer, 12-13 the GridFallback grid), whose sky fallback fades where the sky is hidden.
	AmbientOcclusionEffect* ao = TheShaderManager->Effects.AmbientOcclusion;
	IDirect3DTexture9* skyVis = ao ? ao->SkyReflectionTexture() : nullptr;
	static CachedHandle skyHandle;
	if (D3DXHANDLE handle = ParameterHandle(skyHandle, "NVR_ReflectionsSky")) {
		D3DXVECTOR4 sky(skyVis ? 1.0f : 0.0f, ao ? ao->SkyMinReflection : 0.1f, 0.0f, 0.0f);
		Effect->SetVector(handle, &sky);
		extSky = sky;	// UNOFFICIAL 2026-10-25 (NVR DXVK): the Vulkan trace reads the same
	}
	else { skyVis = nullptr; extSky = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f); }
	extSkyTexture = skyVis;
	BindMaterial();	// UNOFFICIAL 2026-10-20 MaterialAware (stage 15)
	if (skyVis) {
		SetPassTexture(14, skyVis);	// set after each pass begins (EffectRecord::PassTextures)
		const DWORD linear[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
			{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
		for (const auto& s : linear) TheRenderManager->SetSamplerState(14, (D3DSAMPLERSTATETYPE)s[0], s[1]);
	}
	{
		// UNOFFICIAL 2026-10-26 (deep/fork-hiz): the trace on its own F10 line (inside "Reflections (SSR)"), for the HiZ A/B.
		static GpuTimer traceTimer("  Reflections trace (half)");
		GpuProfileScope gpu(traceTimer, Device);
		pass(temporal ? Textures.TraceSurface : Textures.TempSurface, 1);
	}
	if (temporal) {
		pass(Textures.TempSurface, 3);
		Device->StretchRect(Textures.TempSurface, NULL, Textures.HistorySurface, NULL, D3DTEXF_NONE);
	}
	pass(Textures.TraceSurface, 2);
	pass(Textures.BufferSurface, 4);
	ClearPassTextures();

	haveHistory = temporal;
	lastRenderedFrame = now;
	lastView = view;
	lastInvView = invView;
	lastProj = TheRenderManager->projMatrix;
}

// The cube is kept up whenever CubeMap is on (and it exists), floors reflecting or not: metal and the env maps read it too
// ([Shaders.PBR.Metal] / [Shaders.PBR.EnvMaps] CubeReflections). Not under water (what is on screen there is not the
// surroundings).
bool ReflectionsEffect::CubeWanted() {
	return Settings.CubeMap && Cubes.Env && !TheShaderManager->GameState.isUnderwater;
}

bool ReflectionsEffect::CubeReady() const {
	return Enabled && cubeValid && TheFrameRateManager->FrameNumber - cubeFrame <= 1;
}

void ReflectionsEffect::RenderCubeOnly(IDirect3DDevice9* Device) {
	cubeOnly = true;
	CubeStep(Device);
	cubeOnly = false;
}

// One cube update. Emptied first when it was not kept up last frame, in another cell, or after a jump (load, fast travel):
// what it held belongs somewhere else.
void ReflectionsEffect::CubeStep(IDirect3DDevice9* Device) {
	const bool cubeOn = CubeWanted();
	Constants.Cube2 = D3DXVECTOR4(Settings.CubeCapture, Settings.CubeFade, (float)(CubeLevels - 1), cubeOn ? 1.0f : 0.0f);
	if (!cubeOn) { cubeValid = false; return; }
	const D3DXMATRIX& invView = TheRenderManager->InvViewMatrix;
	const double dx = (double)invView._41 - cubeInvView._41, dy = (double)invView._42 - cubeInvView._42,
		dz = (double)invView._43 - cubeInvView._43;
	void* cell = Player ? Player->parentCell : nullptr;
	const bool jumped = dx * dx + dy * dy + dz * dz > 1000.0 * 1000.0;
	// UNOFFICIAL 2026-10-18: FrameNumber, not UpdateConstants calls: with those (two a frame) 'cubeFrame + 1 != frame' was true every
	// frame, so the cube was emptied every frame and never built up what was seen around.
	const UINT now = TheFrameRateManager->FrameNumber;
	const bool empty = !cubeValid || cubeFrame + 1 != now || cell != cubeCell || jumped;
	cubeSteps++;
	if (empty) cubeEmptied++;
	RenderCube(Device, empty);
	cubeCell = cell;
	cubeValid = true;
	cubeFrame = now;
	cubeInvView = invView;
}

// The surroundings cube (techniques 5 Capture, 6 Filter): each face of the new capture cube from last frame's and the
// image; then every face of every mip of the reflection cube from the new capture. 6 + 42 small draws. The techniques
// unbind the cube they write (Texture[n] = NoTexture), so a cube is never read and written in the same pass.
void ReflectionsEffect::RenderCube(IDirect3DDevice9* Device, bool reset) {
	const int write = captureIndex ^ 1;
	Cubes.CaptureLastSlot = Cubes.Capture[captureIndex];
	Cubes.CaptureNewSlot = Cubes.Capture[write];
	Cubes.EnvSlot = Cubes.Env;
	for (int f = 0; f < 6; f++) {
		IDirect3DSurface9* face = Cubes.CaptureFace[write][f];
		Constants.Cube = D3DXVECTOR4((float)f, (float)CubeSize, 0.0f, reset ? 1.0f : 0.0f);
		Device->SetRenderTarget(0, face);
		Render(Device, face, face, 5, false, nullptr);
	}
	captureIndex = write;
	for (UINT m = 0; m < CubeLevels; m++) {
		for (int f = 0; f < 6; f++) {
			IDirect3DSurface9* face = Cubes.EnvFace[m][f];
			Constants.Cube = D3DXVECTOR4((float)f, (float)(CubeSize >> m), (float)m, 0.0f);
			Device->SetRenderTarget(0, face);
			Render(Device, face, face, 6, false, nullptr);
		}
	}
}

// --- The Vulkan passes (NVR DXVK) -----------------------------------------------------------------------------------
// UNOFFICIAL 2026-10-25 (NVR DXVK): all eight techniques as Compute\Reflections<Technique>.frag (Reflections.glsl holds what they
// share), line for line. Render calls this per technique as it calls the D3D9 technique: the half-resolution passes, the cube
// faces and the material copy draw into the target the caller set (ExtDrawTo), the composite through the frame chain
// (ExtFullScreen). Every pass declares the same bindings: what a technique does not read, and its own target, get a placeholder
// (the depth buffer, or the other capture cube), as the techniques unbind their target (Texture[n] = NoTexture). The constants
// (four matrices: 560 bytes) go to a uniform buffer, uploaded only when they change (the 48 cube draws differ only in
// TESR_ReflectionsCube, which is in the push block). Sampler states as the .fx declares them; TESR_ReflectionsEnv's LINEAR mip
// filter needs extension 1.3 (MipSampler), otherwise, or without the three cubes, the effect keeps its D3D9 techniques.
namespace {
	// The Constants block of Reflections.glsl (std140; the matrices as the effect's SetMatrix uploads them).
	struct ReflUniforms {
		D3DXMATRIX	projection, view, shadowOrtho, reproject;
		D3DXVECTOR4	reciprocalResolution, skyColor, horizonColor, waterSettings;
		D3DXVECTOR4	data, data2, data3, data4, layout, cube2, grid, grid2, grid3, material;
		D3DXVECTOR4	depthConstants, cameraData, cameraPosition, sky, flags;
	};
	ReflUniforms ReflUploaded;	// what the uniform buffer holds (one effect instance)
	struct ReflPush { D3DXVECTOR4 cube; UInt32 linearIndex, pointIndex, mipIndex, pass; };

	// Set 1: the textures in the .fx's sampler order (binding = slot); set 2: the constants; the samplers' heap indices at 16, 20, 24.
	const NVR_EXT_BINDING_DESC ReflBindings[] = {
		{ 1, 0,  NVR_EXT_BIND_SAMPLED_IMAGE, 0,  NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_RenderedBuffer
		{ 1, 1,  NVR_EXT_BIND_SAMPLED_IMAGE, 1,  NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_DepthBuffer
		{ 1, 2,  NVR_EXT_BIND_SAMPLED_IMAGE, 2,  NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_NormalsBuffer
		{ 1, 3,  NVR_EXT_BIND_SAMPLED_IMAGE, 3,  NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_DepthBufferViewModel
		{ 1, 4,  NVR_EXT_BIND_SAMPLED_IMAGE, 4,  NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_ReflectionsTrace
		{ 1, 5,  NVR_EXT_BIND_SAMPLED_IMAGE, 5,  NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_ReflectionsBuffer
		{ 1, 6,  NVR_EXT_BIND_SAMPLED_IMAGE, 6,  NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_ReflectionsTemp
		{ 1, 7,  NVR_EXT_BIND_SAMPLED_IMAGE, 7,  NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_ReflectionsHistory
		{ 1, 8,  NVR_EXT_BIND_SAMPLED_IMAGE, 8,  NVR_EXT_VIEW_CUBE, FALSE, 0 },	// TESR_ReflectionsCapture
		{ 1, 9,  NVR_EXT_BIND_SAMPLED_IMAGE, 9,  NVR_EXT_VIEW_CUBE, FALSE, 0 },	// TESR_ReflectionsCaptureNew
		{ 1, 10, NVR_EXT_BIND_SAMPLED_IMAGE, 10, NVR_EXT_VIEW_CUBE, FALSE, 0 },	// TESR_ReflectionsEnv
		{ 1, 11, NVR_EXT_BIND_SAMPLED_IMAGE, 11, NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_OrthoMapBuffer
		{ 1, 12, NVR_EXT_BIND_SAMPLED_IMAGE, 12, NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_ReflectionsGridLight
		{ 1, 13, NVR_EXT_BIND_SAMPLED_IMAGE, 13, NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_ReflectionsGridColor
		{ 1, 14, NVR_EXT_BIND_SAMPLED_IMAGE, 14, NVR_EXT_VIEW_2D,   FALSE, 0 },	// NVR_ReflectionsSkyVis
		{ 1, 15, NVR_EXT_BIND_SAMPLED_IMAGE, 15, NVR_EXT_VIEW_2D,   FALSE, 0 },	// NVR_ReflectionsMaterial
		{ 2, 0,  NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D,   FALSE, 0 },	// Constants
		{ 0, 0,  NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D,   FALSE, 16 },	// LINEAR, CLAMP
		{ 0, 0,  NVR_EXT_BIND_SAMPLER,        1, NVR_EXT_VIEW_2D,   FALSE, 20 },	// POINT, CLAMP
		{ 0, 0,  NVR_EXT_BIND_SAMPLER,        2, NVR_EXT_VIEW_2D,   FALSE, 24 },	// LINEAR, CLAMP, MIPFILTER LINEAR
	};
	const UINT ReflTextureCount = 16;
	// UNOFFICIAL 2026-10-26 (deep/fork-hiz): ReflectionsTraceHiZ.frag = the same bindings + NVR_HiZ on set 1, binding 16 (slot 16).
	const NVR_EXT_BINDING_DESC ReflHiZBinding = { 1, 16, NVR_EXT_BIND_SAMPLED_IMAGE, 16, NVR_EXT_VIEW_2D, FALSE, 0 };

	IDirect3DBaseTexture9* SlotTexture(const char* name) {
		std::string slotName = name;
		IDirect3DBaseTexture9** slot = TheTextureManager->GetTextureSlotByName(slotName);
		return slot ? *slot : nullptr;
	}
	// The constants from the same registered memory the effect's SetCT uploads.
	const D3DXVECTOR4* Registered(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
}

bool ReflectionsEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex > 7 || ClearRenderTarget || (techniqueIndex == 7 && !ReflMaterial::CompiledIn())) return false;
	static const char* const spvNames[8] = { "ReflectionsComposite", "ReflectionsTrace", "ReflectionsBlur", "ReflectionsTemporal", "ReflectionsBlur",
		"ReflectionsCapture", "ReflectionsFilter", "ReflectionsMaterial" };
	// UNOFFICIAL 2026-10-26 (deep/fork-hiz) HiZ: the trace as the hierarchical march when this frame's pyramid is there (core/HiZ.h).
	IDirect3DTexture9* hiz = techniqueIndex == 1 && HiZWanted() ? HiZ::Current() : nullptr;
	if (hiz && !extTraceHiZ.Tried) {
		extTraceHiZ.Tried = true;
		NVR_EXT_BINDING_DESC bindings[_countof(ReflBindings) + 1];
		memcpy(bindings, ReflBindings, sizeof(ReflBindings));
		bindings[_countof(ReflBindings)] = ReflHiZBinding;
		std::vector<char> words;	// checked first: a missing file must not turn the other Vulkan passes off
		if (DxvkCompute::LoadSpirv("ReflectionsTraceHiZ", words))
			extTraceHiZ.Pipeline = DxvkCompute::CreateGraphicsPipeline("ReflectionsTraceHiZ", bindings, _countof(bindings), sizeof(ReflPush), true);
		if (!extTraceHiZ.Pipeline) {
			hizTraceFailed = true;
			Logger::Log("UNOFFICIAL Reflections: the Hi-Z trace could not be set up; the normal trace is used.");
		}
	}
	if (!extTraceHiZ.Pipeline) hiz = nullptr;
	ExtPass& pass = hiz ? extTraceHiZ : extPasses[techniqueIndex == 4 ? 2 : techniqueIndex];
	if (!pass.Tried) {
		pass.Tried = true;
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvNames[techniqueIndex], ReflBindings, _countof(ReflBindings), sizeof(ReflPush), true);
	}
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	ID3D9NvrExtSampler* mip = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_CLAMP);
	if (!pass.Pipeline || !linear || !point || !mip) return false;

	const D3DXVECTOR4* projection = Registered("TESR_ProjectionTransform");
	const D3DXVECTOR4* view = Registered("TESR_ViewTransform");
	const D3DXVECTOR4* shadowOrtho = Registered("TESR_ShadowCameraToLightTransformOrtho");
	const D3DXVECTOR4* reciprocal = Registered("TESR_ReciprocalResolution");
	const D3DXVECTOR4* skyColor = Registered("TESR_SkyColor");
	const D3DXVECTOR4* horizonColor = Registered("TESR_HorizonColor");
	const D3DXVECTOR4* waterSettings = Registered("TESR_WaterSettings");
	const D3DXVECTOR4* depthConstants = Registered("TESR_DepthConstants");
	const D3DXVECTOR4* cameraData = Registered("TESR_CameraData");
	const D3DXVECTOR4* cameraPosition = Registered("TESR_CameraPosition");
	if (!projection || !view || !shadowOrtho || !reciprocal || !skyColor || !horizonColor || !waterSettings || !depthConstants || !cameraData || !cameraPosition)
		return false;

	// The textures as SetCT binds them. Those read without a guard must exist; the guarded ones (the light grid, the sky
	// visibility, the material buffer, the ortho map without the roof test) take the placeholder when absent, which is never read.
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = SlotTexture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* viewModel = SlotTexture("TESR_DepthBufferViewModel");
	IDirect3DBaseTexture9* ortho = SlotTexture("TESR_OrthoMapBuffer");
	if (!depth || !normals || !viewModel || !TheTextureManager->RenderedTexture || (!ortho && Constants.Data4.y > 0.5f) ||
		!Cubes.Capture[0] || !Cubes.Capture[1] || !Cubes.Env)
		return false;
	const bool materialBound = materialFilled && Textures.MaterialTexture != nullptr;	// BindMaterial's test
	IDirect3DBaseTexture9* textures[ReflTextureCount] = {
		TheTextureManager->RenderedTexture, depth, normals, viewModel,
		Textures.TraceTexture, Textures.BufferTexture, Textures.TempTexture, Textures.HistoryTexture,
		Cubes.CaptureLastSlot ? Cubes.CaptureLastSlot : Cubes.Capture[0], Cubes.CaptureNewSlot ? Cubes.CaptureNewSlot : Cubes.Capture[1], Cubes.Env,
		ortho ? ortho : depth, GridLightSlot ? GridLightSlot : depth, GridColorSlot ? GridColorSlot : depth,
		extSkyTexture ? extSkyTexture : depth, materialBound ? Textures.MaterialTexture : depth };
	// A pass never reads its own target.
	switch (techniqueIndex) {
		case 1: textures[4] = textures[6] = depth; break;	// Trace -> Trace (Temporal on) or Temp
		case 2: textures[4] = depth; break;					// Blur X -> Trace
		case 3: textures[6] = depth; break;					// Temporal -> Temp
		case 4: textures[5] = depth; break;					// Blur Y -> Buffer
		case 5: textures[9] = textures[8]; break;			// Capture -> a face of the new capture cube
		case 6: textures[10] = textures[9]; break;			// Filter -> a face of the reflection cube
		case 7: textures[15] = depth; break;				// MaterialCapture -> the material buffer
	}
	for (UINT i = 0; i < ReflTextureCount; i++) if (!textures[i]) return false;

	ReflUniforms uniforms = {};
	memcpy(&uniforms.projection, projection, sizeof(D3DXMATRIX));
	memcpy(&uniforms.view, view, sizeof(D3DXMATRIX));
	memcpy(&uniforms.shadowOrtho, shadowOrtho, sizeof(D3DXMATRIX));
	uniforms.reproject = Constants.Reproject;
	uniforms.reciprocalResolution = *reciprocal;
	uniforms.skyColor = *skyColor;
	uniforms.horizonColor = *horizonColor;
	uniforms.waterSettings = *waterSettings;
	uniforms.data = Constants.Data;
	uniforms.data2 = Constants.Data2;
	uniforms.data3 = Constants.Data3;
	uniforms.data4 = Constants.Data4;
	uniforms.layout = Constants.Layout;
	uniforms.cube2 = Constants.Cube2;
	uniforms.grid = Constants.Grid;
	uniforms.grid2 = Constants.Grid2;
	uniforms.grid3 = Constants.Grid3;
	uniforms.material = Constants.Material;
	uniforms.depthConstants = *depthConstants;
	uniforms.cameraData = *cameraData;
	uniforms.cameraPosition = *cameraPosition;
	uniforms.sky = extSky;
	uniforms.flags = D3DXVECTOR4(ReflMaterial::CompiledIn() ? 1.0f : 0.0f, ReflMaterial::AddCompiledIn() ? 1.0f : 0.0f,
		ReflMaterial::OccCompiledIn() ? 1.0f : 0.0f, materialBound ? 1.0f : 0.0f);
	ID3D9NvrExtBuffer* buffer = extUniforms.Buffer;
	if (!buffer || memcmp(&uniforms, &ReflUploaded, sizeof(uniforms))) {
		buffer = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
		if (!buffer) return false;
		ReflUploaded = uniforms;
	}

	NVR_EXT_RESOURCE_BINDING resources[_countof(ReflBindings) + 1] = {};
	for (UINT i = 0; i < ReflTextureCount; i++) {
		resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
	}
	resources[16].Slot = 0; resources[16].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[16].pResource = buffer;
	resources[17].Slot = 0; resources[17].Type = NVR_EXT_BIND_SAMPLER; resources[17].pResource = linear;
	resources[18].Slot = 1; resources[18].Type = NVR_EXT_BIND_SAMPLER; resources[18].pResource = point;
	resources[19].Slot = 2; resources[19].Type = NVR_EXT_BIND_SAMPLER; resources[19].pResource = mip;
	UINT resourceCount = _countof(ReflBindings);
	if (hiz) {
		resources[resourceCount].Slot = 16; resources[resourceCount].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[resourceCount].pResource = hiz;
		resourceCount++;
		if (++hizTraceFrames == 1) Logger::Log("UNOFFICIAL Reflections: the trace runs as the Hi-Z march (HiZ).");
	}
	ReflPush push = {};
	push.cube = Constants.Cube;
	push.pass = techniqueIndex == 4 ? 1 : (techniqueIndex == 0 && Cubes.EnvSlot) ? 1 : 0;	// Blur Y; the composite: the cube is bound

	if (techniqueIndex == 0)
		return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, pass.Pipeline, resources, resourceCount, &push, sizeof(push));
	return ExtDrawTo(pass.Pipeline, &RenderTarget, 1, resources, resourceCount, &push, sizeof(push), nullptr, ExtUv::ScreenQuad);	// FrameVS into a smaller target
}
