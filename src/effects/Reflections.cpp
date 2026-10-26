#include "Reflections.h"
#include "../core/GpuProfiler.h"
#include "../core/ReflMaterial.h"

void ReflectionsEffect::UpdateSettings() {
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
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(section, "DebugView"), 0, 8);
	Settings.GridFallback = TheSettingManager->GetSettingI(section, "GridFallback") != 0;
	Settings.GridBrightness = std::clamp(TheSettingManager->GetSettingF(section, "GridBrightness"), 0.0f, 4.0f);
	Settings.MaterialAware = ReflMaterial::CompiledIn() ? std::clamp(TheSettingManager->GetSettingI(section, "MaterialAware"), 0, 2) : 0;
	Settings.MaterialReflect = std::clamp(TheSettingManager->GetSettingF(section, "MaterialReflect"), 0.0f, 2.0f);
	Settings.MaterialMaxRoughness = std::clamp(TheSettingManager->GetSettingF(section, "MaterialMaxRoughness"), 0.05f, 1.0f);
	Settings.ReflectAdd = ReflMaterial::AddCompiledIn() ? std::clamp(TheSettingManager->GetSettingF(section, "ReflectAdd"), 0.0f, 1.0f) : 0.0f;
	Settings.SpecularOcclusion = ReflMaterial::OccCompiledIn() ? std::clamp(TheSettingManager->GetSettingF(section, "SpecularOcclusion"), 0.0f, 1.0f) : 0.0f;
	Settings.SpecularOcclusionLevel = std::clamp(TheSettingManager->GetSettingF(section, "SpecularOcclusionLevel"), 0.05f, 4.0f);
	SettingManager::Configuration::ConfigNode node;
	Settings.ReflectivityExterior = TheSettingManager->Config.FillNode(&node, "Shaders.PBR.Main", "ReflectivityExterior") ? std::clamp(node.FloatValue, 0.0f, 3.0f) : 1.0f;
	Settings.ReflectivityInterior = TheSettingManager->Config.FillNode(&node, "Shaders.PBR.Main", "ReflectivityInterior") ? std::clamp(node.FloatValue, 0.0f, 3.0f) : 1.0f;
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
	if (ReflMaterial::CompiledIn() &&
		(FAILED(TheRenderManager->device->CreateTexture(TheRenderManager->width, TheRenderManager->height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
			D3DPOOL_DEFAULT, &Textures.MaterialTexture, NULL)) || FAILED(Textures.MaterialTexture->GetSurfaceLevel(0, &Textures.MaterialSurface)))) {
		Logger::Log("UNOFFICIAL Reflections: the material buffer could not be created; MaterialAware is off.");
		if (Textures.MaterialTexture) { Textures.MaterialTexture->Release(); Textures.MaterialTexture = nullptr; }
		Textures.MaterialSurface = nullptr;
	}

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
	TheTextureManager->RegisterTexture("TESR_ReflectionsGridLight", &GridLightSlot);
	TheTextureManager->RegisterTexture("TESR_ReflectionsGridColor", &GridColorSlot);
}

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

void ReflectionsEffect::GridStep() {
	const AmbientLightEffect* ambient = TheShaderManager->Effects.AmbientLight;
	const UINT now = TheFrameRateManager->FrameNumber;
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

	const float wet = std::clamp(TheShaderManager->Effects.Rain->Constants.RainAnimator.GetValue(), 0.0f, 1.0f);

	const float reflectivity = exterior ? Settings.ReflectivityExterior : Settings.ReflectivityInterior;
	Constants.Data.x = (exterior ? Settings.GroundSheen : Settings.InteriorFloors) * reflectivity;
	Constants.Data.y = exterior ? wet * Settings.RainWetness : 0.0f;
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
	Constants.Material = D3DXVECTOR4(Settings.MaterialAware == 1 && materialFilled ? 1.0f : 0.0f, Settings.MaterialMaxRoughness, Settings.MaterialReflect * reflectivity, 0.0f);
}

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
	SetPassTexture(15, Textures.MaterialTexture);
	const DWORD point[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
		{D3DSAMP_MAGFILTER, D3DTEXF_POINT}, {D3DSAMP_MINFILTER, D3DTEXF_POINT}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
	for (const auto& s : point) TheRenderManager->SetSamplerState(15, (D3DSAMPLERSTATETYPE)s[0], s[1]);
}

bool ReflectionsEffect::ShouldRender() {
	if (cubeOnly || materialOnly) return true;
	const NormalsEffect* normals = TheShaderManager->Effects.Normals;
	return !TheShaderManager->GameState.isUnderwater && normals && normals->Enabled &&
		(Constants.Data.x > 0.0f || Constants.Data.y > 0.0f || (Constants.Material.x > 0.0f && Constants.Material.z > 0.0f)) &&
		Textures.TraceSurface && Textures.BufferSurface && Textures.TempSurface && Textures.HistorySurface;
}

void ReflectionsEffect::RenderBuffers(IDirect3DDevice9* Device) {
	const bool temporal = Settings.Temporal > 0.0f;
	const D3DXMATRIX& view = TheRenderManager->viewMatrix;
	const D3DXMATRIX& invView = TheRenderManager->InvViewMatrix;

	const UINT now = TheFrameRateManager->FrameNumber;
	const bool drewLastFrame = lastRenderedFrame + 1 == now;
	bool usable = temporal && haveHistory && drewLastFrame;
	const double move[3] = { (double)invView._41 - lastInvView._41, (double)invView._42 - lastInvView._42,
		(double)invView._43 - lastInvView._43 };
	if (usable) usable = move[0] * move[0] + move[1] * move[1] + move[2] * move[2] < 500.0 * 500.0;
	stats.Note(temporal, haveHistory, drewLastFrame, usable);
	if (GpuTimer::Enabled && stats.drawn >= 240) ReportHistory("240 frames");
	if (usable) {
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

	CubeStep(Device);
	GridStep();

	auto pass = [&](IDirect3DSurface9* target, UINT technique) {
		Device->SetRenderTarget(0, target);
		Render(Device, target, target, technique, false, target);
	};
	AmbientOcclusionEffect* ao = TheShaderManager->Effects.AmbientOcclusion;
	IDirect3DTexture9* skyVis = ao ? ao->SkyReflectionTexture() : nullptr;
	static CachedHandle skyHandle;
	if (D3DXHANDLE handle = ParameterHandle(skyHandle, "NVR_ReflectionsSky")) {
		D3DXVECTOR4 sky(skyVis ? 1.0f : 0.0f, ao ? ao->SkyMinReflection : 0.1f, 0.0f, 0.0f);
		Effect->SetVector(handle, &sky);
	}
	else skyVis = nullptr;
	BindMaterial();
	if (skyVis) {
		SetPassTexture(14, skyVis);
		const DWORD linear[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
			{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
		for (const auto& s : linear) TheRenderManager->SetSamplerState(14, (D3DSAMPLERSTATETYPE)s[0], s[1]);
	}
	if (temporal) {
		pass(Textures.TraceSurface, 1);
		pass(Textures.TempSurface, 3);
		Device->StretchRect(Textures.TempSurface, NULL, Textures.HistorySurface, NULL, D3DTEXF_NONE);
	}
	else
		pass(Textures.TempSurface, 1);
	pass(Textures.TraceSurface, 2);
	pass(Textures.BufferSurface, 4);
	ClearPassTextures();

	haveHistory = temporal;
	lastRenderedFrame = now;
	lastView = view;
	lastInvView = invView;
	lastProj = TheRenderManager->projMatrix;
}

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

void ReflectionsEffect::CubeStep(IDirect3DDevice9* Device) {
	const bool cubeOn = CubeWanted();
	Constants.Cube2 = D3DXVECTOR4(Settings.CubeCapture, Settings.CubeFade, (float)(CubeLevels - 1), cubeOn ? 1.0f : 0.0f);
	if (!cubeOn) { cubeValid = false; return; }
	const D3DXMATRIX& invView = TheRenderManager->InvViewMatrix;
	const double dx = (double)invView._41 - cubeInvView._41, dy = (double)invView._42 - cubeInvView._42,
		dz = (double)invView._43 - cubeInvView._43;
	void* cell = Player ? Player->parentCell : nullptr;
	const bool jumped = dx * dx + dy * dy + dz * dz > 1000.0 * 1000.0;
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
