#include "SunFar.h"
#include "../core/LandLight.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

static const char* CloudShadowsSection = "Shaders.CloudShadows.Main";

static IDirect3DTexture9* WaterDepthMap() {
	BSRenderedTexture* rendered = *(BSRenderedTexture**)0x011C7B68;
	if (!rendered || !rendered->RenderedTextures[0] || !rendered->RenderedTextures[0]->rendererData) return nullptr;
	return rendered->GetD3DTexture(0);
}

void SunFarEffect::UpdateSettings() {
	Settings.Clouds = TheSettingManager->GetSettingI(CloudShadowsSection, "Enabled") != 0;
	Settings.CloudStrength = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "Strength"), 0.0f, 1.0f);
	Settings.CloudDensity = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "Density"), 0.0f, 8.0f);
	Settings.CloudHeight = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "Height"), 1000.0f, 1000000.0f);
	Settings.CloudTile = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "TileSize"), 1000.0f, 10000000.0f);
	Settings.CloudSpeed = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "Speed"), 0.0f, 20.0f);
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(CloudShadowsSection, "DebugView"), 0, 4);
	Settings.WaterFade = TheSettingManager->GetSettingI(CloudShadowsSection, "WaterShadows") != 0;
	Settings.WaterDeepShare = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "WaterDeepShare"), 0.0f, 1.0f);
	Settings.TargetFix = TheSettingManager->GetSettingI(CloudShadowsSection, "TargetFix") != 0;
	Settings.HazardLog = TheSettingManager->GetSettingI(CloudShadowsSection, "HazardLog");
	Settings.FlickerWatch = TheSettingManager->GetSettingI(CloudShadowsSection, "FlickerWatch");
	Settings.FlickerThreshold = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "FlickerThreshold"), 0.01f, 0.5f);
	Settings.OldSunModel = TheSettingManager->GetSettingI(CloudShadowsSection, "OldSunModel") != 0;
}

void SunFarEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SunFarData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SunFarClouds", &Constants.Clouds);
	TheShaderManager->RegisterConstant("TESR_SunFarCloudOffset", &Constants.CloudOffset[0]);
	TheShaderManager->RegisterConstant("TESR_SunFarCloudWeight", &Constants.CloudWeight);
	TheShaderManager->RegisterConstant("TESR_SunFarCloudBlend", &Constants.CloudBlend);
	TheShaderManager->RegisterConstant("TESR_SunFarApply", &Constants.Apply);
	TheShaderManager->RegisterConstant("TESR_SunFarVolume", &Constants.Volume);
	TheShaderManager->RegisterConstant("TESR_SunFarWater", &Constants.Water);
	TheShaderManager->RegisterConstant("TESR_SunFarLand", &Constants.Land);
}

void SunFarEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_SunFarBuffer", &Texture, &Surface, TheRenderManager->width, TheRenderManager->height, D3DFMT_A8R8G8B8);
}

static bool IsHorizonTexture(const char* path) {
	if (!path) return false;
	std::string lower(path);
	for (char& c : lower) c = (char)tolower((unsigned char)c);
	return lower.find("horizon") != std::string::npos;
}

static IDirect3DBaseTexture9* D3DTextureOf(NiTexture* texture) {
	return texture && texture->rendererData ? texture->rendererData->dTexture : nullptr;
}

void SunFarEffect::UpdateConstants() {
	const PBRShaders* pbr = TheShaderManager->Shaders.PBR;
	Constants.Data.z = pbr && pbr->Enabled ? (std::max)(pbr->Constants.Linear.x, 1.0f) : 1.0f;
	Constants.Data.w = 1.0f / Constants.Data.z;
	const ShadowsExteriorEffect* shadowsFx = TheShaderManager->Effects.ShadowsExteriors;
	Constants.Data.y = (float)(std::max)(Settings.DebugView, shadowsFx ? shadowsFx->TerrainShadows.Settings.DebugView : 0);
	Constants.Data.x = 0.0f;
	Constants.Water = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	LandLight::Now++;
	Constants.Land = LandLight::Frame && LandLight::Now - LandLight::Frame < 30 ?
		D3DXVECTOR4(LandLight::Dir[0], LandLight::Dir[1], LandLight::Dir[2], 1.0f) : D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	cloudsReady = false;
	for (int i = 0; i < 4; i++) layerTextures[i] = blendTextures[i] = nullptr;
	Constants.CloudWeight = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	Constants.CloudBlend = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);

	static ULONGLONG lastTick = 0;
	const ULONGLONG tick = GetTickCount64();
	const double dt = lastTick ? (std::min)((double)(tick - lastTick) / 1000.0, 0.1) : 0.0;
	lastTick = tick;

	Sky* sky = Sky::Get();
	VolumetricCloudsEffect* volumetric = TheShaderManager->Effects.VolumetricClouds;
	const bool volumetricShadows = volumetric && volumetric->Enabled && volumetric->Constants.Extra.x > 0.0f;
	Constants.Volume = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	if (volumetric && TheShaderManager->GameState.isExterior) {
		const float strength = volumetric->ShadowStrengthForSunFar();
		if (strength > 0.0f) Constants.Volume = D3DXVECTOR4(strength, (std::max)(volumetric->Constants.Extra.y, 1000.0f),
			(std::max)(volumetric->Constants.Detail.z, 256.0f), volumetric->Constants.Shape.x);
	}
	std::string state;
	struct StateLog { std::string& s; ~StateLog() { static std::string last; if (s != last) { last = s; if (!s.empty()) Logger::Log("UNOFFICIAL cloud shadows: %s", s.c_str()); } } } stateLog{ state };
	if (!Settings.Clouds || Settings.CloudStrength <= 0.0f) return;
	if (!sky || !sky->clouds) { state = "off: no sky clouds object"; return; }
	if (volumetricShadows) { state = "off: the volumetric clouds draw their own shadows"; return; }
	if (!TheShaderManager->GameState.isExterior) return;

	const float sunZ = TheShaderManager->ShaderConst.SunDir.z;
	const float fade = std::clamp((sunZ - 0.05f) / 0.2f, 0.0f, 1.0f);
	if (fade <= 0.0f) { state = "faded out: the sun is too low"; return; }

	TESWeather* weather = sky->firstWeather;
	const float windSpeed = std::clamp(sky->windSpeed, 0.0f, 1.0f);
	const float windAngle = sky->windDirection;
	const int count = (std::min)((int)sky->clouds->numLayers, 4);
	UINT size = 512;
	char note[160];
	sprintf_s(note, "%d layers:", count);
	state = note;
	for (int i = 0; i < count; i++) {
		NiGeometry* layer = (NiGeometry*)sky->clouds->layers[i];
		if (!layer) { state += " (none)"; continue; }
		SkyShaderProperty* property = layer->propertyState.GetShadeProperty<SkyShaderProperty>();
		if (!property || property->eSkyObjectType != SkyShaderProperty::SO_CLOUDS) { state += " (not a cloud layer)"; continue; }
		state += std::string(" ") + (property->strTextureName.m_data ? property->strTextureName.m_data : "(no name)");
		IDirect3DBaseTexture9* base = D3DTextureOf(property->spBaseTexture);
		if (!base) { state += " (not loaded)"; continue; }
		if (IsHorizonTexture(property->strTextureName.m_data)) { state += " (horizon: left out)"; continue; }
		layerTextures[i] = base;
		IDirect3DBaseTexture9* incoming = D3DTextureOf(property->spBlendTexture);
		blendTextures[i] = incoming ? incoming : base;
		((float*)&Constants.CloudWeight)[i] = Settings.CloudDensity;
		((float*)&Constants.CloudBlend)[i] = incoming ? std::clamp(property->fBlendValue, 0.0f, 1.0f) : 0.0f;
		if (base->GetType() == D3DRTYPE_TEXTURE) {
			D3DSURFACE_DESC desc;
			if (SUCCEEDED(((IDirect3DTexture9*)base)->GetLevelDesc(0, &desc))) size = (std::max)(size, (UINT)desc.Width);
		}
		const float layerSpeed = weather ? 0.5f + weather->cloudSpeed[i] / 255.0f : 1.0f;
		const double unitsPerSecond = Settings.CloudSpeed * 700.0 * (0.25 + windSpeed) * layerSpeed;
		const double move = unitsPerSecond * dt / Settings.CloudTile;
		drift[i][0] = fmod(drift[i][0] + cos((double)windAngle) * move, 64.0);
		drift[i][1] = fmod(drift[i][1] + sin((double)windAngle) * move, 64.0);
		cloudsReady = true;
	}
	if (!cloudsReady) return;
	for (int i = 0; i < 4; i++) {
		float* offset = (float*)&Constants.CloudOffset[i / 2];
		offset[(i & 1) * 2] = (float)drift[i][0] + 0.37f * i;
		offset[(i & 1) * 2 + 1] = (float)drift[i][1] + 0.61f * i;
	}
	Constants.Data.x = Settings.CloudStrength * fade;
	const float blur = Settings.CloudHeight * 0.0093f * 2.0f;
	const float texel = Settings.CloudTile / (float)size;
	Constants.Clouds = D3DXVECTOR4(Settings.CloudHeight, 1.0f / Settings.CloudTile, (std::max)(0.0f, log2f((std::max)(blur / texel, 1.0f))), 0.0f);
}

bool SunFarEffect::ShouldRender() {
	if (!TheShaderManager->GameState.isExterior || !Texture || !Surface) return false;
	ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	if (!shadows || !shadows->Enabled) return false;
	return shadows->TerrainShadows.Active() || Constants.Data.x > 0.0f || Constants.Volume.x > 0.0f || (Constants.Data.y > 0.0f && (shadows->TerrainShadows.Settings.Enabled || Settings.Clouds));
}

static void SunFarStageList(IDirect3DDevice9* Device, IDirect3DBaseTexture9* own, UInt32& mask, UInt32& cacheMask, char* out, size_t size) {
	mask = cacheMask = 0;
	out[0] = 0;
	for (UInt32 stage = 0; stage < 16; stage++) {
		IDirect3DBaseTexture9* bound = nullptr;
		Device->GetTexture(stage, &bound);
		if (bound) bound->Release();
		if ((UInt32)TheRenderManager->renderState->unk10A0[stage] == (UInt32)own) cacheMask |= 1u << stage;
		if (bound != own) continue;
		mask |= 1u << stage;
		char item[16];
		sprintf_s(item, " s%u", stage);
		strcat_s(out, size, item);
	}
	if (!mask) strcat_s(out, size, " none");
}

static struct SunFarHazardStats {
	unsigned frames = 0, beforeFrames = 0, drawFrames = 0, cacheOdd = 0, lines = 0;
	UInt32 lastBefore = ~0u, lastDraw = ~0u;
	std::string lastStates;
} hazard;

void SunFarEffect::BeforeDraw(IDirect3DDevice9* Device) {
	if (!Settings.HazardLog || !Texture) return;
	UInt32 mask, cacheMask;
	char stages[128];
	SunFarStageList(Device, Texture, mask, cacheMask, stages, sizeof(stages));
	if (mask) hazard.drawFrames++;
	static const struct { D3DRENDERSTATETYPE state; const char* name; } states[] = {
		{ D3DRS_ZENABLE, "Z" }, { D3DRS_ZWRITEENABLE, "ZWrite" }, { D3DRS_SCISSORTESTENABLE, "Scissor" }, { D3DRS_ALPHABLENDENABLE, "Blend" },
		{ D3DRS_ALPHATESTENABLE, "AlphaTest" }, { D3DRS_STENCILENABLE, "Stencil" }, { D3DRS_CULLMODE, "Cull" }, { D3DRS_COLORWRITEENABLE, "ColorWrite" },
		{ D3DRS_SRGBWRITEENABLE, "sRGBWrite" }, { D3DRS_FILLMODE, "Fill" } };
	std::string text;
	char item[64];
	for (auto& s : states) {
		DWORD value = 0;
		Device->GetRenderState(s.state, &value);
		sprintf_s(item, " %s %lu", s.name, value);
		text += item;
	}
	D3DVIEWPORT9 vp = {};
	RECT scissor = {};
	Device->GetViewport(&vp);
	Device->GetScissorRect(&scissor);
	IDirect3DSurface9* depth = nullptr;
	Device->GetDepthStencilSurface(&depth);
	if (depth) depth->Release();
	sprintf_s(item, " viewport %lu %lu %lux%lu", vp.X, vp.Y, vp.Width, vp.Height);
	text += item;
	sprintf_s(item, " scissor %ld %ld %ld %ld", scissor.left, scissor.top, scissor.right, scissor.bottom);
	text += item;
	text += depth ? " depth bound" : " no depth";
	if ((mask != hazard.lastDraw || text != hazard.lastStates) && hazard.lines < 120) {
		hazard.lines++;
		Logger::Log("SUNFAR HAZARD at draw (frame %u): target on%s; states%s", hazard.frames, stages, text.c_str());
	}
	hazard.lastDraw = mask;
	hazard.lastStates = text;
}

void SunFarEffect::RenderFactor(IDirect3DDevice9* Device, IDirect3DSurface9* Scene) {
	Constants.Apply.x = 0.0f;
	VolumetricCloudsEffect* volumetric = TheShaderManager->Effects.VolumetricClouds;
	if (volumetric) volumetric->shadowsViaSunFar = false;
	if (!Enabled || !Effect || !ShouldRender()) return;
	static GpuTimer timer("Sun far shadows (land, clouds)");
	GpuProfileScope gpu(timer, Device);
	for (int i = 0; i < 4; i++) {
		SetPassTexture(5 + i, layerTextures[i]);
		SetPassTexture(9 + i, blendTextures[i]);
	}
	const bool volume = volumetric && Constants.Volume.x > 0.0f;
	if (volume) {
		volumetric->RenderShadowMap(Device);
		SetPassTexture(13, volumetric->Textures.ShadowTexture);
	}
	IDirect3DTexture9* sceneTexture = nullptr;
	if (Scene && FAILED(Scene->GetContainer(IID_IDirect3DTexture9, (void**)&sceneTexture))) sceneTexture = nullptr;
	if (sceneTexture) SetPassTexture(15, sceneTexture);
	IDirect3DTexture9* waterDepth = Settings.WaterFade || Settings.DebugView == 3 ? WaterDepthMap() : nullptr;
	Constants.Water = waterDepth ? D3DXVECTOR4(Settings.WaterFade ? 1.0f : 0.0f, Settings.WaterDeepShare, 1.0f, 0.0f) : D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	Constants.Water.w = Settings.OldSunModel || !sceneTexture ? 1.0f : 0.0f;
	if (waterDepth) SetPassTexture(14, waterDepth);
	if (Settings.HazardLog) {
		hazard.frames++;
		UInt32 mask, cacheMask;
		char stages[128];
		SunFarStageList(Device, Texture, mask, cacheMask, stages, sizeof(stages));
		if (mask) hazard.beforeFrames++;
		if (mask != cacheMask) hazard.cacheOdd++;
		if (mask != hazard.lastBefore && hazard.lines < 120) {
			hazard.lines++;
			Logger::Log("SUNFAR HAZARD before the pass (frame %u): target on%s (game cache mask %04X; mountain shadows' ground %s; fix %s)",
				hazard.frames, stages, cacheMask, TheShaderManager->Effects.ShadowsExteriors->TerrainShadows.GroundTexture ? "bound on s4" : "missing: s4 not rebound",
				Settings.TargetFix ? "on" : "off");
		}
		hazard.lastBefore = mask;
		if (hazard.frames % 600 == 0)
			Logger::Log("SUNFAR HAZARD summary: %u frames, target bound before the pass in %u, at the draw in %u, device/cache disagree in %u (fix %s)",
				hazard.frames, hazard.beforeFrames, hazard.drawFrames, hazard.cacheOdd, Settings.TargetFix ? "on" : "off");
	}
	static const D3DRENDERSTATETYPE fixStates[7] = { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_ALPHATESTENABLE, D3DRS_STENCILENABLE, D3DRS_CULLMODE };
	static const DWORD fixValues[7] = { D3DZB_FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE };
	DWORD savedStates[7] = {};
	if (Settings.TargetFix) {
		for (UInt32 stage = 0; stage < 16; stage++) {
			IDirect3DBaseTexture9* bound = nullptr;
			Device->GetTexture(stage, &bound);
			if (bound) bound->Release();
			if (bound != Texture) continue;
			Device->SetTexture(stage, nullptr);
			TheRenderManager->renderState->NoteDeviceTexture(stage, nullptr);
		}
		for (int i = 0; i < 7; i++) {
			Device->GetRenderState(fixStates[i], &savedStates[i]);
			Device->SetRenderState(fixStates[i], fixValues[i]);
		}
	}
	TheShaderManager->RenderEffectToRT(Surface, this, false, 0);
	if (Settings.TargetFix)
		for (int i = 0; i < 7; i++) Device->SetRenderState(fixStates[i], savedStates[i]);
	ClearPassTextures();
	if (sceneTexture) sceneTexture->Release();
	Constants.Apply.x = 1.0f;
	if (volume) volumetric->shadowsViaSunFar = true;
	static int waterReported = -1;
	if ((Settings.WaterFade ? (waterDepth ? 1 : 0) : 2) != waterReported) {
		waterReported = Settings.WaterFade ? (waterDepth ? 1 : 0) : 2;
		D3DSURFACE_DESC desc = {};
		if (waterDepth) waterDepth->GetLevelDesc(0, &desc);
		Logger::Log("UNOFFICIAL sun far shadows on water: %s (depth map %p %ux%u format %u)", waterReported == 2 ? "left alone (WaterShadows off)" :
			waterReported == 1 ? "faded in by the water's depth" : "left alone: the game has no water depth map", waterDepth, desc.Width, desc.Height, (unsigned)desc.Format);
	}
	static bool reported = false;
	if (!reported) {
		reported = true;
		Logger::Log("UNOFFICIAL sun far shadows: drawn (land %s, clouds %s, volumetric clouds %s); the sun-shadow composites take the factor",
			TheShaderManager->Effects.ShadowsExteriors->TerrainShadows.Active() ? "on" : "off", Constants.Data.x > 0.0f ? "on" : "off", volume ? "on" : "off");
	}
}
