#include "Reflections.h"
#include "../core/GpuProfiler.h"
#include "../core/ReflMaterial.h"
#include "../core/DxvkCompute.h"
#include "../core/RenderRect.h"
#include "../core/HiZ.h"
#include "../core/ShaderCounters.h"
#include "../core/PixelInspector.h"
#include "../core/RtCoreClient.h"
#include "../core/RtCore.h"

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
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(section, "DebugView"), 0, 10);
	Settings.GridFallback = TheSettingManager->GetSettingI(section, "GridFallback") != 0;
	Settings.GridBrightness = std::clamp(TheSettingManager->GetSettingF(section, "GridBrightness"), 0.0f, 4.0f);
	Settings.MaterialAware = ReflMaterial::CompiledIn() ? std::clamp(TheSettingManager->GetSettingI(section, "MaterialAware"), 0, 2) : 0;
	Settings.MaterialReflect = std::clamp(TheSettingManager->GetSettingF(section, "MaterialReflect"), 0.0f, 2.0f);
	Settings.MaterialMaxRoughness = std::clamp(TheSettingManager->GetSettingF(section, "MaterialMaxRoughness"), 0.05f, 1.0f);
	Settings.ReflectAdd = ReflMaterial::AddCompiledIn() ? std::clamp(TheSettingManager->GetSettingF(section, "ReflectAdd"), 0.0f, 1.0f) : 0.0f;
	Settings.SpecularOcclusion = ReflMaterial::OccCompiledIn() ? std::clamp(TheSettingManager->GetSettingF(section, "SpecularOcclusion"), 0.0f, 1.0f) : 0.0f;
	Settings.SpecularOcclusionLevel = std::clamp(TheSettingManager->GetSettingF(section, "SpecularOcclusionLevel"), 0.05f, 4.0f);
	Settings.HiZ = TheSettingManager->GetSettingI(section, "HiZ") != 0;
	SettingManager::Configuration::ConfigNode node;
	Settings.ReflectivityExterior = TheSettingManager->Config.FillNode(&node, "Shaders.PBR.Main", "ReflectivityExterior") ? std::clamp(node.FloatValue, 0.0f, 3.0f) : 1.0f;
	Settings.ReflectivityInterior = TheSettingManager->Config.FillNode(&node, "Shaders.PBR.Main", "ReflectivityInterior") ? std::clamp(node.FloatValue, 0.0f, 3.0f) : 1.0f;
	Settings.HiZInteriorsTest = TheSettingManager->GetSettingI(section, "HiZInteriorsTest") != 0;
	Settings.HiZSafeCells = TheSettingManager->GetSettingI(section, "HiZSafeCells") != 0;
	HiZ::CheckEvery = std::clamp(TheSettingManager->GetSettingI(section, "HiZCheck"), 0, 100000);
	const char* rt = "Shaders.Reflections.RT";
	Settings.RtEnabled = TheSettingManager->GetSettingI(rt, "Enabled") != 0;
	Settings.RtMaxRoughness = std::clamp(TheSettingManager->GetSettingF(rt, "MaxRoughness"), 0.02f, 1.0f);
	Settings.RtDistance = std::clamp(TheSettingManager->GetSettingF(rt, "Distance"), 200.0f, 30000.0f);
	Settings.RtDebugView = std::clamp(TheSettingManager->GetSettingI(rt, "DebugView"), 0, 5);
	Settings.RtScreenTolerance = std::clamp(TheSettingManager->GetSettingF(rt, "ScreenTolerance"), 0.002f, 0.2f);
	Settings.RtEstimate = std::clamp(TheSettingManager->GetSettingF(rt, "EstimateBrightness"), 0.0f, 4.0f);
	Settings.RtSunShadows = TheSettingManager->GetSettingI(rt, "SunShadows") != 0;
	Settings.RtSplitTest = TheSettingManager->GetSettingI(rt, "SplitTest") != 0;
}

namespace {
	ReflectionsEffect* RtEffect = nullptr;
	int RtImageDir = -1, RtImageRay = -1, RtImageHit = -1, RtImageShade = -1, RtShader = -1;
	struct RtPassConstants { D3DXVECTOR4 size, forward, right, up, origin, sun, layout; };
	RtPassConstants RtPending = {};
	bool RtRecorded = false;

	UINT RtWidth() { return MaxQuality::Reduced(TheRenderManager->width, 2); }
	UINT RtHeight() { return MaxQuality::Reduced(TheRenderManager->height, 2); }
	bool RtUserWants() { return RtEffect && RtEffect->Enabled && RtEffect->Settings.RtEnabled && DxvkCompute::PassesOn(); }
	void RtUserSetup() {
		RtImageDir = RtCore::AddImage("NVR RT reflections direction", RtWidth(), RtHeight(), NVR_EXT_SHARED_RG32F);
		RtImageRay = RtCore::AddImage("NVR RT reflections ray", RtWidth(), RtHeight(), NVR_EXT_SHARED_RG32F);
		RtImageHit = RtCore::AddImage("NVR RT reflections hit", RtWidth(), RtHeight(), NVR_EXT_SHARED_RGBA16F);
		RtImageShade = RtCore::AddImage("NVR RT reflections shade", RtWidth(), RtHeight(), NVR_EXT_SHARED_RGBA16F);
	}
	void RtUserRecord(IDirect3DDevice9* Device) {
		RtRecorded = false;
		if (!RtEffect || RtImageDir < 0 || RtImageRay < 0 || RtImageHit < 0 || RtImageShade < 0 || RtShader < 0) return;
		const ReflectionsEffect::RtImagesStruct& rt = RtEffect->Rt;
		if (!rt.Dir || !rt.Ray || !rt.HitSurface || !rt.ShadeSurface) return;
		if (!RtCore::CopyIn(RtImageDir, rt.Dir) || !RtCore::CopyIn(RtImageRay, rt.Ray)) return;
		const int images[4] = { RtImageDir, RtImageRay, RtImageHit, RtImageShade };
		if (!RtCore::AddPass(RtShader, images, 4, 0xC, &RtPending, sizeof(RtPending), (RtWidth() + 7) / 8, (RtHeight() + 7) / 8)) return;
		RtRecorded = RtCore::CopyOut(RtImageHit, rt.HitSurface) && RtCore::CopyOut(RtImageShade, rt.ShadeSurface);
	}
	const RtCore::User RtUser = { "RT reflections", RtUserWants, RtUserSetup, RtUserRecord, 1 };
}

void ReflectionsEffect::RtRegister() {
	RtEffect = this;
	RtShader = RtCore::AddShader("ReflectionsRT");
	RtCore::AddUser(RtUser);
}

bool ReflectionsEffect::RtWanted() const {
	return Settings.RtEnabled && !ExtFailed && !rtTraceFailed && DxvkCompute::PassesOn() && (RtCoreClient::Ready() || Settings.RtSplitTest);
}

float ReflectionsEffect::RtSunAmount() const {
	const ShaderConstants& sc = TheShaderManager->ShaderConst;
	return TheShaderManager->GameState.isExterior && sc.SunDir.z > 0.0f ? std::clamp(sc.SunAmount.x, 0.0f, 1.0f) : 0.0f;
}

bool ReflectionsEffect::RtImages() {
	if (Rt.Shade) return true;
	IDirect3DDevice9* device = TheRenderManager->device;
	struct { IDirect3DTexture9** texture; IDirect3DSurface9** surface; D3DFORMAT format; } images[5] = {
		{ &Rt.March, &Rt.MarchSurface, D3DFMT_A16B16G16R16F }, { &Rt.Dir, &Rt.DirSurface, D3DFMT_G32R32F },
		{ &Rt.Ray, &Rt.RaySurface, D3DFMT_G32R32F }, { &Rt.Hit, &Rt.HitSurface, D3DFMT_A16B16G16R16F },
		{ &Rt.Shade, &Rt.ShadeSurface, D3DFMT_A16B16G16R16F } };
	for (auto& i : images)
		if (FAILED(device->CreateTexture(RtWidth(), RtHeight(), 1, D3DUSAGE_RENDERTARGET, i.format, D3DPOOL_DEFAULT, i.texture, NULL)) ||
			FAILED((*i.texture)->GetSurfaceLevel(0, i.surface))) {
			Logger::Log("UNOFFICIAL Reflections: the RT images could not be created; RT reflections are off.");
			rtTraceFailed = true;
			for (auto& j : images) {
				if (*j.surface) { (*j.surface)->Release(); *j.surface = nullptr; }
				if (*j.texture) { (*j.texture)->Release(); *j.texture = nullptr; }
			}
			return false;
		}
	return true;
}

bool ReflectionsEffect::RtTrace(IDirect3DDevice9* Device, IDirect3DSurface9* target) {
	if (!RtWanted() || !RtImages()) return false;
	rtRays = RtCoreClient::Ready();
	if (!ExtRender(Device, Rt.MarchSurface, Rt.MarchSurface, 8, false, Rt.MarchSurface)) return false;
	bool traced = !rtRays;
	if (rtRays) {
		const D3DXMATRIX& view = TheRenderManager->viewMatrix;
		const D3DXMATRIX& proj = TheRenderManager->projMatrix;
		const D3DXVECTOR4& sun = TheShaderManager->ShaderConst.SunDir;
		RtPending.size = D3DXVECTOR4((float)RtWidth(), (float)RtHeight(), Settings.RtDistance, Settings.RtSunShadows ? 1.0f : 0.0f);
		RtPending.forward = D3DXVECTOR4(view._13, view._23, view._33, 0.0f);
		RtPending.right = D3DXVECTOR4(view._11 / proj._11, view._21 / proj._11, view._31 / proj._11, 0.0f);
		RtPending.up = D3DXVECTOR4(view._12 / proj._22, view._22 / proj._22, view._32 / proj._22, 0.0f);
		const D3DXVECTOR3 eye = RtCore::Eye();
		RtPending.origin = D3DXVECTOR4(eye.x, eye.y, eye.z, Settings.RtDebugView == 5 ? 1.0f : 0.0f);
		D3DXVECTOR3 toSun(sun.x, sun.y, sun.z);
		D3DXVec3Normalize(&toSun, &toSun);
		RtPending.sun = D3DXVECTOR4(toSun.x, toSun.y, toSun.z, RtSunAmount() > 0.0f ? 1.0f : 0.0f);
		RtPending.layout = D3DXVECTOR4(Constants.Layout.x, Constants.Layout.y, 1.0f / TheRenderManager->width, 1.0f / TheRenderManager->height);
		static GpuTimer exchangeTimer("  Reflections RT (exchange)");
		GpuProfileScope gpu(exchangeTimer, Device);
		traced = RtCoreClient::Exchange(Device, RtUser) && RtRecorded;
	}
	if (!traced) {
		if (++rtFallbackFrames == 1) Logger::Log("UNOFFICIAL Reflections: RT is on but the RT helper traced nothing this frame; the normal trace is drawn (logged once).");
		return false;
	}
	Device->SetRenderTarget(0, target);
	if (!ExtRender(Device, target, target, 9, false, target)) return false;
	if (++rtTraceFrames == 1) Logger::Log("UNOFFICIAL Reflections: the trace runs with RT ([Shaders.Reflections.RT]%s).", rtRays ? "" : ", SplitTest: no rays");
	return true;
}

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
		ambient->gridValid && now - ambient->gridRenderedFrame <= 8 && !TheShaderManager->GameState.isCameraCut;
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
	if (Settings.RtDebugView > 0 && Settings.RtDebugView != 3 && RtWanted()) Constants.Data3.z = 6.0f;
	Constants.Data3.w = exterior && Settings.SkyFallback ? 1.0f : 0.0f;
	Constants.Data4.x = Settings.DrawDistance;
	Constants.Data4.z = Settings.ReflectAdd;
	Constants.Data4.w = Settings.SpecularOcclusion;
	Constants.Layout.z = Settings.SpecularOcclusionLevel;
	Constants.Layout.w = Settings.HiZSafeCells ? 1.0f : 0.0f;
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
	static bool lastRect = false;
	if (RenderRect::ScaleChanged() || RenderRect::Active() != lastRect) usable = false;
	lastRect = RenderRect::Active();
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
		extSky = sky;
	}
	else { skyVis = nullptr; extSky = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f); }
	extSkyTexture = skyVis;
	BindMaterial();
	if (skyVis) {
		SetPassTexture(14, skyVis);
		const DWORD linear[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
			{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
		for (const auto& s : linear) TheRenderManager->SetSamplerState(14, (D3DSAMPLERSTATETYPE)s[0], s[1]);
	}
	{
		static GpuTimer traceTimer("  Reflections trace (half)");
		GpuProfileScope gpu(traceTimer, Device);
		IDirect3DSurface9* traceTarget = temporal ? Textures.TraceSurface : Textures.TempSurface;
		if (!RtTrace(Device, traceTarget)) pass(traceTarget, 1);
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

namespace {
	struct ReflUniforms {
		D3DXMATRIX	projection, view, shadowOrtho, reproject;
		D3DXVECTOR4	reciprocalResolution, skyColor, horizonColor, waterSettings;
		D3DXVECTOR4	data, data2, data3, data4, layout, cube2, grid, grid2, grid3, material;
		D3DXVECTOR4	depthConstants, cameraData, cameraPosition, sky, flags;
	};
	ReflUniforms ReflUploaded;
	struct ReflPush { D3DXVECTOR4 cube; UInt32 linearIndex, pointIndex, mipIndex, pass; };

	const NVR_EXT_BINDING_DESC ReflBindings[] = {
		{ 1, 0,  NVR_EXT_BIND_SAMPLED_IMAGE, 0,  NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 1,  NVR_EXT_BIND_SAMPLED_IMAGE, 1,  NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 2,  NVR_EXT_BIND_SAMPLED_IMAGE, 2,  NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 3,  NVR_EXT_BIND_SAMPLED_IMAGE, 3,  NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 4,  NVR_EXT_BIND_SAMPLED_IMAGE, 4,  NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 5,  NVR_EXT_BIND_SAMPLED_IMAGE, 5,  NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 6,  NVR_EXT_BIND_SAMPLED_IMAGE, 6,  NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 7,  NVR_EXT_BIND_SAMPLED_IMAGE, 7,  NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 8,  NVR_EXT_BIND_SAMPLED_IMAGE, 8,  NVR_EXT_VIEW_CUBE, FALSE, 0 },
		{ 1, 9,  NVR_EXT_BIND_SAMPLED_IMAGE, 9,  NVR_EXT_VIEW_CUBE, FALSE, 0 },
		{ 1, 10, NVR_EXT_BIND_SAMPLED_IMAGE, 10, NVR_EXT_VIEW_CUBE, FALSE, 0 },
		{ 1, 11, NVR_EXT_BIND_SAMPLED_IMAGE, 11, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 12, NVR_EXT_BIND_SAMPLED_IMAGE, 12, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 13, NVR_EXT_BIND_SAMPLED_IMAGE, 13, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 14, NVR_EXT_BIND_SAMPLED_IMAGE, 14, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 15, NVR_EXT_BIND_SAMPLED_IMAGE, 15, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 2, 0,  NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 0, 0,  NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D,   FALSE, 16 },
		{ 0, 0,  NVR_EXT_BIND_SAMPLER,        1, NVR_EXT_VIEW_2D,   FALSE, 20 },
		{ 0, 0,  NVR_EXT_BIND_SAMPLER,        2, NVR_EXT_VIEW_2D,   FALSE, 24 },
	};
	const UINT ReflTextureCount = 16;
	const NVR_EXT_BINDING_DESC ReflHiZBinding = { 1, 16, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, FALSE, 0 };
	const UINT ReflCounterSlot = 7;
	ShaderCounters HiZCounters("Reflections Hi-Z trace",
		{ "rays", "walked to the end / off screen", "walk hit", "hit thrown away", "hit by the fixed-step rest", "out of cells, rest found nothing",
		  "not started", "cells walked", "skipped facing", "skipped own plane", "skipped behind (thickness)", "no reflection" },
		{ "endRule/cells/hitT/confidence", "facing/plane/thick/roughness", "hit.xy/depth/amount" });

	const UINT RtUniformSlot = 2;
	const NVR_EXT_BINDING_DESC ReflRtUniformBinding = { 2, 1, NVR_EXT_BIND_UNIFORM_BUFFER, RtUniformSlot, NVR_EXT_VIEW_2D, FALSE, 0 };
	struct ReflRtUniforms { D3DXVECTOR4 rt, rt2, sunDir, sunColor; };
	ReflRtUniforms RtUploaded;

	IDirect3DBaseTexture9* SlotTexture(const char* name) {
		std::string slotName = name;
		IDirect3DBaseTexture9** slot = TheTextureManager->GetTextureSlotByName(slotName);
		return slot ? *slot : nullptr;
	}
	const D3DXVECTOR4* Registered(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
}

bool ReflectionsEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex > 9 || ClearRenderTarget || (techniqueIndex == 7 && !ReflMaterial::CompiledIn())) return false;
	const bool rtPass = techniqueIndex >= 8;
	static const char* const spvNames[8] = { "ReflectionsComposite", "ReflectionsTrace", "ReflectionsBlur", "ReflectionsTemporal", "ReflectionsBlur",
		"ReflectionsCapture", "ReflectionsFilter", "ReflectionsMaterial" };
	ID3D9NvrExtBuffer* hiz = techniqueIndex == 1 && HiZWanted() ? HiZ::Current() : nullptr;
	if (hiz && !extTraceHiZ.Tried) {
		extTraceHiZ.Tried = true;
		NVR_EXT_BINDING_DESC bindings[_countof(ReflBindings) + 1];
		memcpy(bindings, ReflBindings, sizeof(ReflBindings));
		bindings[_countof(ReflBindings)] = ReflHiZBinding;
		std::vector<char> words;
		if (DxvkCompute::LoadSpirv("ReflectionsTraceHiZ", words))
			extTraceHiZ.Pipeline = DxvkCompute::CreateGraphicsPipeline("ReflectionsTraceHiZ", bindings, _countof(bindings), sizeof(ReflPush), true);
		if (!extTraceHiZ.Pipeline) {
			hizTraceFailed = true;
			Logger::Log("UNOFFICIAL Reflections: the Hi-Z trace could not be set up; the normal trace is used.");
		}
	}
	if (!extTraceHiZ.Pipeline) hiz = nullptr;
	ID3D9NvrExtBuffer* counters = nullptr;
	if (hiz && TheSettingManager->GetSettingI("Main.Develop.ShaderCounters", "ReflectionsHiZ") != 0) {
		if (!extTraceHiZCount.Tried) {
			extTraceHiZCount.Tried = true;
			NVR_EXT_BINDING_DESC bindings[_countof(ReflBindings) + 2];
			memcpy(bindings, ReflBindings, sizeof(ReflBindings));
			bindings[_countof(ReflBindings)] = ReflHiZBinding;
			bindings[_countof(ReflBindings) + 1] = ShaderCounters::Binding(ReflCounterSlot);
			if (DebugReadback::Interface() && DxvkCompute::HasSpirv("ReflectionsTraceHiZCount"))
				extTraceHiZCount.Pipeline = DxvkCompute::CreateGraphicsPipeline("ReflectionsTraceHiZCount", bindings, _countof(bindings), sizeof(ReflPush), true, 1, NVR_EXT_BLEND_NONE, false);
			if (!extTraceHiZCount.Pipeline) Logger::Log("UNOFFICIAL Reflections: the Hi-Z trace's counting build is not available (needs NVR DXVK 1.5-debug and the shader file ReflectionsTraceHiZCount.spv); the normal trace runs.");
		}
		if (extTraceHiZCount.Pipeline) {
			D3DSURFACE_DESC target = {};
			int px = -1, py = -1;
			if (RenderTarget && SUCCEEDED(RenderTarget->GetDesc(&target))) PixelInspector::PixelFor(target.Width, target.Height, px, py);
			counters = HiZCounters.Begin(px, py);
		}
	}
	ExtPass& pass = rtPass ? extRtPasses[techniqueIndex - 8] : counters ? extTraceHiZCount : hiz ? extTraceHiZ : extPasses[techniqueIndex == 4 ? 2 : techniqueIndex];
	if (!pass.Tried) {
		pass.Tried = true;
		if (rtPass) {
			static const char* const rtNames[2] = { "ReflectionsRayGen", "ReflectionsRTResolve" };
			NVR_EXT_BINDING_DESC bindings[_countof(ReflBindings) + 6];
			memcpy(bindings, ReflBindings, sizeof(ReflBindings));
			bindings[_countof(ReflBindings)] = ReflRtUniformBinding;
			for (UINT i = 0; i < 5; i++) bindings[_countof(ReflBindings) + 1 + i] = { 1, 17 + i, NVR_EXT_BIND_SAMPLED_IMAGE, ReflTextureCount + i, NVR_EXT_VIEW_2D, FALSE, 0 };
			if (DxvkCompute::HasSpirv(rtNames[techniqueIndex - 8]))
				pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(rtNames[techniqueIndex - 8], bindings, _countof(bindings), sizeof(ReflPush), true,
					techniqueIndex == 8 ? 3 : 1, NVR_EXT_BLEND_NONE, false);
			if (!pass.Pipeline) {
				rtTraceFailed = true;
				Logger::Log("UNOFFICIAL Reflections: the RT pass %s could not be set up; the normal trace is used.", rtNames[techniqueIndex - 8]);
			}
		}
		else pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvNames[techniqueIndex], ReflBindings, _countof(ReflBindings), sizeof(ReflPush), true);
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

	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = SlotTexture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* viewModel = SlotTexture("TESR_DepthBufferViewModel");
	IDirect3DBaseTexture9* ortho = SlotTexture("TESR_OrthoMapBuffer");
	if (!depth || !normals || !viewModel || !TheTextureManager->RenderedTexture || (!ortho && Constants.Data4.y > 0.5f) ||
		!Cubes.Capture[0] || !Cubes.Capture[1] || !Cubes.Env)
		return false;
	const bool materialBound = materialFilled && Textures.MaterialTexture != nullptr;
	IDirect3DBaseTexture9* textures[ReflTextureCount] = {
		TheTextureManager->RenderedTexture, depth, normals, viewModel,
		Textures.TraceTexture, Textures.BufferTexture, Textures.TempTexture, Textures.HistoryTexture,
		Cubes.CaptureLastSlot ? Cubes.CaptureLastSlot : Cubes.Capture[0], Cubes.CaptureNewSlot ? Cubes.CaptureNewSlot : Cubes.Capture[1], Cubes.Env,
		ortho ? ortho : depth, GridLightSlot ? GridLightSlot : depth, GridColorSlot ? GridColorSlot : depth,
		extSkyTexture ? extSkyTexture : depth, materialBound ? Textures.MaterialTexture : depth };
	switch (techniqueIndex) {
		case 1: case 8: case 9: textures[4] = textures[6] = depth; break;
		case 2: textures[4] = depth; break;
		case 3: textures[6] = depth; break;
		case 4: textures[5] = depth; break;
		case 5: textures[9] = textures[8]; break;
		case 6: textures[10] = textures[9]; break;
		case 7: textures[15] = depth; break;
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

	NVR_EXT_RESOURCE_BINDING resources[_countof(ReflBindings) + 6] = {};
	for (UINT i = 0; i < ReflTextureCount; i++) {
		resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
	}
	resources[16].Slot = 0; resources[16].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[16].pResource = buffer;
	resources[17].Slot = 0; resources[17].Type = NVR_EXT_BIND_SAMPLER; resources[17].pResource = linear;
	resources[18].Slot = 1; resources[18].Type = NVR_EXT_BIND_SAMPLER; resources[18].pResource = point;
	resources[19].Slot = 2; resources[19].Type = NVR_EXT_BIND_SAMPLER; resources[19].pResource = mip;
	UINT resourceCount = _countof(ReflBindings);
	if (hiz) {
		resources[resourceCount].Slot = 1; resources[resourceCount].Type = NVR_EXT_BIND_STORAGE_BUFFER; resources[resourceCount].pResource = hiz;
		resourceCount++;
		if (++hizTraceFrames == 1) Logger::Log("UNOFFICIAL Reflections: the trace runs as the Hi-Z march (HiZ).");
	}
	if (counters) resources[resourceCount++] = ShaderCounters::Bind(ReflCounterSlot, counters);
	if (rtPass) {
		const ShaderConstants& sc = TheShaderManager->ShaderConst;
		ReflRtUniforms rtu = {};
		rtu.rt = D3DXVECTOR4(rtRays ? 1.0f : 0.0f, Settings.RtMaxRoughness, Settings.RtDistance, (float)Settings.RtDebugView);
		rtu.rt2 = D3DXVECTOR4(Settings.RtScreenTolerance, Settings.RtEstimate, RtSunAmount(), 0.0f);
		rtu.sunDir = sc.SunDir;
		rtu.sunColor = sc.sunColor;
		ID3D9NvrExtBuffer* rtBuffer = extRtUniforms.Buffer;
		if (!rtBuffer || memcmp(&rtu, &RtUploaded, sizeof(rtu))) {
			rtBuffer = ExtUniforms(extRtUniforms, &rtu, sizeof(rtu));
			if (!rtBuffer) return false;
			RtUploaded = rtu;
		}
		resources[resourceCount].Slot = RtUniformSlot; resources[resourceCount].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[resourceCount].pResource = rtBuffer;
		resourceCount++;
		IDirect3DBaseTexture9* rtImages[5] = { Rt.March, Rt.Dir, Rt.Ray, Rt.Hit, Rt.Shade };
		for (UINT i = 0; i < 5; i++) {
			if (!rtImages[i]) return false;
			resources[resourceCount].Slot = ReflTextureCount + i; resources[resourceCount].Type = NVR_EXT_BIND_SAMPLED_IMAGE;
			resources[resourceCount].pResource = techniqueIndex == 8 ? depth : rtImages[i];
			resourceCount++;
		}
	}
	ReflPush push = {};
	push.cube = Constants.Cube;
	push.pass = techniqueIndex == 4 ? 1 : (techniqueIndex == 0 && Cubes.EnvSlot) ? 1 : 0;

	if (techniqueIndex == 0)
		return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, pass.Pipeline, resources, resourceCount, &push, sizeof(push));
	IDirect3DSurface9* rayGenTargets[3] = { Rt.MarchSurface, Rt.DirSurface, Rt.RaySurface };
	const bool drawn = techniqueIndex == 8 ? ExtDrawTo(pass.Pipeline, rayGenTargets, 3, resources, resourceCount, &push, sizeof(push), nullptr, ExtUv::ScreenQuad) :
		ExtDrawTo(pass.Pipeline, &RenderTarget, 1, resources, resourceCount, &push, sizeof(push), nullptr, ExtUv::ScreenQuad);
	if (counters) HiZCounters.End();
	return drawn;
}
