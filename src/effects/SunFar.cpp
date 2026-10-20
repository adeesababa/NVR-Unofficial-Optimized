#include "SunFar.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

static const char* CloudShadowsSection = "Shaders.CloudShadows.Main";

void SunFarEffect::UpdateSettings() {
	Settings.Clouds = TheSettingManager->GetSettingI(CloudShadowsSection, "Enabled") != 0;
	Settings.CloudStrength = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "Strength"), 0.0f, 1.0f);
	Settings.CloudDensity = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "Density"), 0.0f, 8.0f);
	Settings.CloudHeight = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "Height"), 1000.0f, 1000000.0f);
	Settings.CloudTile = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "TileSize"), 1000.0f, 10000000.0f);
	Settings.CloudSpeed = std::clamp(TheSettingManager->GetSettingF(CloudShadowsSection, "Speed"), 0.0f, 20.0f);
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI(CloudShadowsSection, "DebugView"), 0, 2);
}

void SunFarEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SunFarData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SunFarClouds", &Constants.Clouds);
	TheShaderManager->RegisterConstant("TESR_SunFarCloudOffset", &Constants.CloudOffset[0]);
	TheShaderManager->RegisterConstant("TESR_SunFarCloudWeight", &Constants.CloudWeight);
	TheShaderManager->RegisterConstant("TESR_SunFarCloudBlend", &Constants.CloudBlend);
	TheShaderManager->RegisterConstant("TESR_SunFarApply", &Constants.Apply);
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
	return shadows->TerrainShadows.Active() || Constants.Data.x > 0.0f || (Constants.Data.y > 0.0f && (shadows->TerrainShadows.Settings.Enabled || Settings.Clouds));
}

void SunFarEffect::RenderFactor(IDirect3DDevice9* Device) {
	Constants.Apply.x = 0.0f;
	if (!Enabled || !Effect || !ShouldRender()) return;
	static GpuTimer timer("Sun far shadows (land, clouds)");
	GpuProfileScope gpu(timer, Device);
	for (int i = 0; i < 4; i++) {
		SetPassTexture(5 + i, layerTextures[i]);
		SetPassTexture(9 + i, blendTextures[i]);
	}
	TheShaderManager->RenderEffectToRT(Surface, this, false, 0);
	ClearPassTextures();
	Constants.Apply.x = 1.0f;
	static bool reported = false;
	if (!reported) {
		reported = true;
		Logger::Log("UNOFFICIAL sun far shadows: drawn (land %s, clouds %s); the sun-shadow composites take the factor",
			TheShaderManager->Effects.ShadowsExteriors->TerrainShadows.Active() ? "on" : "off", Constants.Data.x > 0.0f ? "on" : "off");
	}
}
