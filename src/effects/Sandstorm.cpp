#include <algorithm>
#include <cmath>

#include "Sandstorm.h"

static const float DayFogLuma = 0.3f;

static const double NoisePeriod = 256.0;

static float ToLinear(float c) {
	c = std::clamp(c, 0.0f, 1.0f);
	return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float Hash1(float n) {
	float p = n * 0.1031f;
	p -= floorf(p);
	p *= p + 33.33f;
	p *= p + p;
	return p - floorf(p);
}

static float Noise1(float x, float period) {
	float i = floorf(x);
	float f = x - i;
	float u = f * f * (3.0f - 2.0f * f);
	float i0 = i - period * floorf(i / period);
	float i1 = i0 + 1.0f;
	i1 -= period * floorf(i1 / period);
	return Hash1(i0) + (Hash1(i1) - Hash1(i0)) * u;
}

void SandstormEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SandstormData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SandstormDust", &Constants.Dust);
	TheShaderManager->RegisterConstant("TESR_SandstormWind", &Constants.Wind);
	TheShaderManager->RegisterConstant("TESR_SandstormGrains", &Constants.Grains);
}

void SandstormEffect::UpdateSettings() {
	Settings.Density = max(0.0f, TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "Density"));
	Settings.HeightFalloff = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "HeightFalloff"), 1.0f, 100000.0f);
	Settings.DustColorR = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "DustColorR"), 0.0f, 1.0f);
	Settings.DustColorG = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "DustColorG"), 0.0f, 1.0f);
	Settings.DustColorB = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "DustColorB"), 0.0f, 1.0f);
	Settings.StreakStrength = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "StreakStrength"), 0.0f, 1.0f);
	Settings.StreakSpeed = max(0.0f, TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "StreakSpeed"));
	Settings.StreakScale = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "StreakScale"), 1.0f, 1000.0f);
	Settings.NearStreakDistance = max(1.0f, TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "NearStreakDistance"));
	Settings.SkyDarken = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "SkyDarken"), 0.0f, 1.0f);
	Settings.GustStrength = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "GustStrength"), 0.0f, 1.0f);
	Settings.WindThreshold = std::clamp(TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "WindThreshold"), 0.0f, 255.0f);
	Settings.FogDistanceThreshold = max(0.0f, TheSettingManager->GetSettingF("Shaders.Sandstorm.Main", "FogDistanceThreshold"));
	Settings.ForceStorm = TheSettingManager->GetSettingI("Shaders.Sandstorm.Main", "ForceStorm") != 0;
}

bool SandstormEffect::IsStormNow() {
	if (Settings.ForceStorm) return true;
	Sky* WorldSky = Tes ? Tes->sky : nullptr;
	if (!WorldSky) return false;
	TESWeather* Weather = WorldSky->weatherPercent >= 0.5f ? WorldSky->firstWeather : WorldSky->secondWeather;
	if (!Weather) Weather = WorldSky->firstWeather;
	if (!Weather) return false;
	if (Weather->GetWeatherType() == TESWeather::WeatherType::kType_Rainy || Weather->GetWeatherType() == TESWeather::WeatherType::kType_Snow) return false;
	if ((float)Weather->GetWindSpeed() < Settings.WindThreshold) return false;
	return WorldSky->fogFarPlane < Settings.FogDistanceThreshold;
}

void SandstormEffect::UpdateConstants() {
	bool storm = IsStormNow();
	if (storm && !StormAnimator.switched) {
		StormAnimator.switched = true;
		StormAnimator.Start(0.05f, 1);
	}
	else if (!storm && StormAnimator.switched) {
		StormAnimator.switched = false;
		StormAnimator.Start(0.07f, 0);
	}
	Constants.Data.x = StormAnimator.GetValue();

	Sky* WorldSky = Tes ? Tes->sky : nullptr;
	float windAngle = WorldSky ? WorldSky->windDirection : 0.0f;
	float windSpeed = std::clamp(TheShaderManager->ShaderConst.windSpeed, 0.0f, 1.0f);
	Constants.Wind.x = cosf(windAngle);
	Constants.Wind.y = sinf(windAngle);
	Constants.Wind.z = windSpeed;
	Constants.Wind.w = 0.0f;

	double elapsed = std::clamp((double)TheFrameRateManager->ElapsedTime, 0.0, 0.1);
	GustTime += elapsed;
	StreakTravel += elapsed * Settings.StreakSpeed * (0.5 + windSpeed);

	float gust = max(0.0f, 1.0f + Settings.GustStrength * (2.0f * Noise1((float)fmod(GustTime * 0.2, 720.0), 720.0f) - 1.0f));

	const D3DXVECTOR4& fog = TheShaderManager->ShaderConst.fogColor;
	float fogLuma = 0.2126f * ToLinear(fog.x) + 0.7152f * ToLinear(fog.y) + 0.0722f * ToLinear(fog.z);
	float light = std::clamp(fogLuma / DayFogLuma, 0.0f, 1.0f);
	Constants.Dust.x = ToLinear(Settings.DustColorR) * light;
	Constants.Dust.y = ToLinear(Settings.DustColorG) * light;
	Constants.Dust.z = ToLinear(Settings.DustColorB) * light;
	Constants.Dust.w = Settings.StreakStrength * gust;

	Constants.Data.y = Settings.Density * gust;
	Constants.Data.z = Settings.HeightFalloff;
	Constants.Data.w = Settings.SkyDarken;

	Constants.Grains.x = Settings.StreakScale;
	Constants.Grains.y = Settings.NearStreakDistance;
	Constants.Grains.z = (float)fmod(StreakTravel * Settings.StreakScale * 0.06, NoisePeriod);
	Constants.Grains.w = (float)fmod(StreakTravel * 1.6 * Settings.StreakScale * 0.625, NoisePeriod);
}

bool SandstormEffect::ShouldRender() {
	return false;
	return Constants.Data.x > 0.0f &&
		TheShaderManager->GameState.isExterior &&
		!TheShaderManager->GameState.isUnderwater;
}
