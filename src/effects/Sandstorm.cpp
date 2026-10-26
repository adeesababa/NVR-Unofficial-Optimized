#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "Sandstorm.h"
#include "../core/GpuProfiler.h"
#include "../core/MaxQuality.h"

static const double TwoPi = 6.283185307179586;

void SandstormEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SandstormData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SandstormWind", &Constants.Wind);
	TheShaderManager->RegisterConstant("TESR_SandstormShape", &Constants.Shape);
	TheShaderManager->RegisterConstant("TESR_SandstormColor", &Constants.Color);
	TheShaderManager->RegisterConstant("TESR_SandstormCover", &Constants.Cover);
	TheShaderManager->RegisterConstant("TESR_SandstormLights", &Constants.Lights);
	TheShaderManager->RegisterConstant("TESR_SandstormEmissive", &Constants.Emissive);
	TheShaderManager->RegisterConstant("TESR_SandstormWater", &Constants.Water);
	TheShaderManager->RegisterConstant("TESR_SandstormQuality", &Constants.Quality);
	TheShaderManager->RegisterConstant("TESR_LeafStorm", &Constants.LeafStorm);
	TheShaderManager->RegisterConstant("TESR_SandstormGridLayout", &Constants.GridLayout);
	TheShaderManager->RegisterConstant("TESR_SandstormLowLayout", &Constants.LowLayout);
}

void SandstormEffect::RegisterTextures() {
	TheTextureManager->RegisterTexture("TESR_SandstormGrid", (IDirect3DBaseTexture9**)&Textures.GridTexture);
	TheTextureManager->RegisterTexture("TESR_SandstormGridTemp", (IDirect3DBaseTexture9**)&Textures.GridTempTexture);
	TheTextureManager->RegisterTexture("TESR_SandstormStreaks", (IDirect3DBaseTexture9**)&Textures.StreaksTexture);
	TheTextureManager->RegisterTexture("TESR_SandstormGlow", (IDirect3DBaseTexture9**)&Textures.GlowTexture);
}

bool SandstormEffect::UseFastPath() {
	if (!Settings.FastPath || Constants.Water.x > 0.0f) return false;
	if (!TexturesTried) {
		TexturesTried = true;
		const UINT column = MaxQuality::Active ? 8 : 16;
		const UINT gw = (TheRenderManager->width + column - 1) / column, gh = (TheRenderManager->height + column - 1) / column;
		const UINT w = MaxQuality::Reduced(TheRenderManager->width, 2), h = MaxQuality::Reduced(TheRenderManager->height, 2);
		const UINT qw = MaxQuality::Reduced(TheRenderManager->width, 4), qh = MaxQuality::Reduced(TheRenderManager->height, 4);
		TheTextureManager->InitTexture("TESR_SandstormGrid", &Textures.GridTexture, &Textures.GridSurface, gw * 8, gh * 8, D3DFMT_A16B16G16R16F);
		TheTextureManager->InitTexture("TESR_SandstormGridTemp", &Textures.GridTempTexture, &Textures.GridTempSurface, gw * 8, gh * 8, D3DFMT_A16B16G16R16F);
		TheTextureManager->InitTexture("TESR_SandstormStreaks", &Textures.StreaksTexture, &Textures.StreaksSurface, w, h, D3DFMT_A16B16G16R16F);
		TheTextureManager->InitTexture("TESR_SandstormGlow", &Textures.GlowTexture, &Textures.GlowSurface, qw, qh, D3DFMT_A16B16G16R16F);
		Constants.GridLayout = D3DXVECTOR4((float)gw, (float)gh, 1.0f / (gw * 8), 1.0f / (gh * 8));
		Constants.LowLayout = D3DXVECTOR4(1.0f / w, 1.0f / h, 1.0f / qw, 1.0f / qh);
		bool ok = Textures.GridSurface && Textures.GridTempSurface && Textures.StreaksSurface && Textures.GlowSurface;
		if (!ok) {
			auto release = [](IDirect3DTexture9*& tex, IDirect3DSurface9*& surf) {
				if (surf) { surf->Release(); surf = nullptr; }
				if (tex) { tex->Release(); tex = nullptr; }
			};
			release(Textures.GridTexture, Textures.GridSurface);
			release(Textures.GridTempTexture, Textures.GridTempSurface);
			release(Textures.StreaksTexture, Textures.StreaksSurface);
			release(Textures.GlowTexture, Textures.GlowSurface);
			if (++TexturesFailed < 3) TexturesTried = false;
		}
		Logger::Log("UNOFFICIAL Sandstorm: fast path %s (dust grid %u x %u columns x 64 slices, streaks at %u x %u, glow at %u x %u)",
			ok ? "on" : "FAILED to make its buffers, the full-resolution path draws instead", gw, gh, w, h, qw, qh);
	}
	return Textures.GridSurface && Textures.GridTempSurface && Textures.StreaksSurface && Textures.GlowSurface;
}

void SandstormEffect::RenderBuffers(IDirect3DDevice9* Device) {
	static GpuTimer timers[5] = { GpuTimer("  Sandstorm dust grid"), GpuTimer("  Sandstorm grid sum rows"), GpuTimer("  Sandstorm grid sum carry"),
		GpuTimer("  Sandstorm streaks (half)"), GpuTimer("  Sandstorm glow (quarter)") };
	auto pass = [&](IDirect3DSurface9* target, UINT technique, GpuTimer& timer) {
		GpuProfileScope gpuPass(timer, Device);
		Device->SetRenderTarget(0, target);
		Render(Device, target, target, technique, false, target);
	};
	pass(Textures.GridSurface, 1, timers[0]);
	pass(Textures.GridTempSurface, 2, timers[1]);
	pass(Textures.GridSurface, 3, timers[2]);
	if (Settings.StreakStrength > 0.0f) pass(Textures.StreaksSurface, 4, timers[3]);
	if (Settings.LightScattering > 0.0f) pass(Textures.GlowSurface, 6, timers[4]);
}

void SandstormEffect::UpdateSettings() {
	const char* main = "Shaders.Sandstorm.Main";
	SettingManager::Configuration::ConfigNode keywordNode;
	std::string k;
	if (TheSettingManager->Config.FillNode(&keywordNode, main, "Keyword")) k = keywordNode.LongValue.empty() ? std::string(keywordNode.Value) : keywordNode.LongValue;
	k.erase(std::remove(k.begin(), k.end(), '"'), k.end());
	if (k.empty()) k = "sandstorm,duststorm";
	for (auto& c : k) c = (char)tolower((unsigned char)c);
	strncpy_s(Settings.Keyword, k.c_str(), _TRUNCATE);
	LastWeather = LastSecond = nullptr;

	Settings.Visibility = std::clamp(TheSettingManager->GetSettingF(main, "Visibility"), 200.0f, 30000.0f);
	Settings.HeightFalloff = std::clamp(TheSettingManager->GetSettingF(main, "HeightFalloff"), 200.0f, 50000.0f);
	Settings.Billowing = std::clamp(TheSettingManager->GetSettingF(main, "Billowing"), 0.0f, 1.0f);
	Settings.NoiseScale = std::clamp(TheSettingManager->GetSettingF(main, "NoiseScale"), 100.0f, 10000.0f);
	Settings.WindSpeed = std::clamp(TheSettingManager->GetSettingF(main, "WindSpeed"), 0.0f, 5000.0f);
	Settings.MinWindSpeed = std::clamp(TheSettingManager->GetSettingF(main, "MinWindSpeed"), 0.0f, 5000.0f);
	Settings.StreakStrength = std::clamp(TheSettingManager->GetSettingF(main, "StreakStrength"), 0.0f, 4.0f);
	Settings.DustColor[0] = std::clamp(TheSettingManager->GetSettingF(main, "DustColorR"), 0.0f, 1.0f);
	Settings.DustColor[1] = std::clamp(TheSettingManager->GetSettingF(main, "DustColorG"), 0.0f, 1.0f);
	Settings.DustColor[2] = std::clamp(TheSettingManager->GetSettingF(main, "DustColorB"), 0.0f, 1.0f);
	Settings.SunScattering = std::clamp(TheSettingManager->GetSettingF(main, "SunScattering"), 0.0f, 10.0f);
	Settings.Brightness = std::clamp(TheSettingManager->GetSettingF(main, "Brightness"), 0.0f, 10.0f);
	Settings.FadeSpeed = std::clamp(TheSettingManager->GetSettingF(main, "FadeSpeed"), 0.01f, 10.0f);
	Settings.GrassWind = std::clamp(TheSettingManager->GetSettingF(main, "GrassWind"), 1.0f, 10.0f);
	Settings.TreeWind = std::clamp(TheSettingManager->GetSettingF(main, "TreeWind"), 1.0f, 10.0f);
	Settings.LightScattering = std::clamp(TheSettingManager->GetSettingF(main, "LightScattering"), 0.0f, 20.0f);
	Settings.EmissiveThreshold = std::clamp(TheSettingManager->GetSettingF(main, "EmissiveThreshold"), 0.0f, 50.0f);
	Settings.EmissiveDistance = std::clamp(TheSettingManager->GetSettingF(main, "EmissiveDistance"), 0.0f, 5000.0f);
	Settings.Steps = (float)std::clamp(TheSettingManager->GetSettingI(main, "Steps"), 4, 12);
	Settings.Octaves = (float)std::clamp(TheSettingManager->GetSettingI(main, "Octaves"), 1, 3);
	Settings.SoftEdges = TheSettingManager->GetSettingI(main, "SoftEdges") != 0;
	Settings.FineShelter = TheSettingManager->GetSettingI(main, "FineShelter") != 0;
	Settings.ThroughBlended = TheSettingManager->GetSettingI(main, "ThroughBlended") != 0;
	Settings.CharactersKeepDepth = TheSettingManager->GetSettingI(main, "CharactersKeepDepth") != 0;
	Settings.ForceStorm = TheSettingManager->GetSettingI(main, "ForceStorm") != 0;
	Settings.FastPath = TheSettingManager->GetSettingI(main, "FastPath") != 0;
	Settings.FixedTime = TheSettingManager->GetSettingF(main, "FixedTime");
}

bool SandstormEffect::IsSandstorm(TESWeather* Weather) {
	if (!Weather) return false;
	const char* name = ((TESWeatherEx*)Weather)->GetEditorName();
	if (!name || !*name) return false;
	std::string lower = name;
	for (auto& c : lower) c = (char)tolower((unsigned char)c);
	const std::string keywords = Settings.Keyword;
	size_t at = 0;
	while (at <= keywords.size()) {
		size_t comma = keywords.find(',', at);
		if (comma == std::string::npos) comma = keywords.size();
		std::string word = keywords.substr(at, comma - at);
		word.erase(0, word.find_first_not_of(' '));
		word.erase(word.find_last_not_of(' ') + 1);
		if (!word.empty() && lower.find(word) != std::string::npos) return true;
		at = comma + 1;
	}
	return false;
}

void SandstormEffect::ClearWind() {
	GrassStorm[0] = GrassStorm[1] = 0.0f;
	Constants.LeafStorm = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f);
}

void SandstormEffect::UpdateConstants() {
	const bool newFrame = TheFrameRateManager->FrameNumber != LastFrame;
	LastFrame = TheFrameRateManager->FrameNumber;
	float elapsed = newFrame ? std::clamp((float)TheFrameRateManager->ElapsedTime, 0.0f, 0.1f) : 0.0f;
	Sky* WorldSky = Tes ? Tes->sky : nullptr;
	bool exterior = TheShaderManager->GameState.isExterior;

	float target = 0.0f;
	if (WorldSky && exterior) {
		TESWeather* first = WorldSky->firstWeather;
		TESWeather* second = WorldSky->secondWeather;
		if (first != LastWeather || second != LastSecond) {
			FirstIsStorm = IsSandstorm(first);
			SecondIsStorm = second && second != first && IsSandstorm(second);
			const char* firstName = first ? ((TESWeatherEx*)first)->GetEditorName() : nullptr;
			const char* secondName = second ? ((TESWeatherEx*)second)->GetEditorName() : nullptr;
			if (first != LoggedWeather || second != LoggedSecond) Logger::Log("Sandstorm: weather %s%s, from %s%s", firstName && *firstName ? firstName : "(no name)", FirstIsStorm ? " (a sandstorm)" : "",
				secondName && *secondName ? secondName : "(none)", SecondIsStorm ? " (a sandstorm)" : "");
			LoggedWeather = first;
			LoggedSecond = second;
			LastWeather = first;
			LastSecond = second;
		}
		float pct = std::clamp(WorldSky->weatherPercent, 0.0f, 1.0f);
		bool transition = second && second != first;
		if (FirstIsStorm) target += transition ? pct : 1.0f;
		if (SecondIsStorm) target += 1.0f - pct;
		target = min(target, 1.0f);
	}
	if (Settings.ForceStorm && exterior) target = 1.0f;

	float step = Settings.FadeSpeed * elapsed;
	Intensity = Intensity < target ? min(target, Intensity + step) : max(target, Intensity - step);
	if (!exterior) Intensity = 0.0f;

	float windDirection = WorldSky ? WorldSky->windDirection : 0.0f;
	if (windDirection > 6.3f) windDirection *= 0.0174532925f;
	float windSpeed = WorldSky ? std::clamp(WorldSky->windSpeed, 0.0f, 1.0f) : 0.5f;

	Constants.Data = D3DXVECTOR4(Intensity, 3.0f / Settings.Visibility, Settings.Visibility, Settings.HeightFalloff);
	Constants.Wind.x = sinf(windDirection);
	Constants.Wind.y = cosf(windDirection);
	Constants.Wind.z = Settings.MinWindSpeed + (Settings.WindSpeed - Settings.MinWindSpeed) * windSpeed;
	Time = fmod(Time + elapsed, 3600.0);
	if (Settings.FixedTime >= 0.0f) Time = min(Settings.FixedTime, 3600.0f);
	Constants.Wind.w = (float)Time;
	Constants.Shape = D3DXVECTOR4(Settings.Billowing, Settings.NoiseScale, Settings.StreakStrength, Settings.SunScattering);
	Constants.Color = D3DXVECTOR4(Settings.DustColor[0], Settings.DustColor[1], Settings.DustColor[2], Settings.Brightness);
	Constants.Lights = D3DXVECTOR4(Settings.LightScattering, 0.0f, 0.0f, 0.0f);
	Constants.Emissive = D3DXVECTOR4(Settings.EmissiveThreshold, Settings.EmissiveDistance, 0.0f, 0.0f);
	Constants.Quality = D3DXVECTOR4(Settings.Steps, Settings.Octaves, Settings.SoftEdges ? 1.0f : 0.0f, Settings.FineShelter ? 1.0f : 0.0f);

	if (Intensity > 0.0f) {
		GrassStorm[0] = Intensity * (Settings.GrassWind - 1.0f);
		GrassStorm[1] = (float)fmod(Time * 8.4, 4.0 * TwoPi);
		const double leafPhase = fmod(Time * 13.0, TwoPi);
		Constants.LeafStorm = D3DXVECTOR4(1.0f + (Settings.TreeWind - 1.0f) * Intensity, Intensity * 0.12f * Settings.TreeWind,
			(float)(leafPhase > TwoPi * 0.5 ? leafPhase - TwoPi : leafPhase), 0.0f);
	}
	else ClearWind();

	Underwater = TheShaderManager->GameState.isUnderwater && Tes && Player && Player->parentCell && WorldSceneGraph;
	Constants.Water = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	if (Underwater && Intensity > 0.0f) {
		TESWaterForm* water = nullptr;
		const float surface = Tes->GetWaterHeight(Player, WorldSceneGraph, &water);
		Constants.Water = D3DXVECTOR4(1.0f, max(surface - TheRenderManager->CameraPosition.z, 0.0f), 0.0f, 0.0f);
		static bool logged = false;
		if (!logged) {
			Logger::Log("Sandstorm: underwater, the surface at %.0f, the camera at %.0f", surface, TheRenderManager->CameraPosition.z);
			logged = true;
		}
	}

	ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	bool cover = shadows && shadows->Enabled && Intensity > 0.0f && Settings.StreakStrength > 0.0f;
	if (cover) TheShaderManager->orthoRequired = true;
	Constants.Cover = D3DXVECTOR4(cover ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
}

bool SandstormEffect::ShouldRender() {
	return Intensity > 0.001f;
}
