#include "VolumetricClouds.h"
#include "../core/FrameClock.h"
#include "../core/GpuProfiler.h"

void VolumetricCloudsEffect::UpdateSettings() {
	const char* section = "Shaders.VolumetricClouds.Main";
	Settings.Base = std::clamp(TheSettingManager->GetSettingF(section, "CloudBase"), 200.0f, 8000.0f);
	Settings.Thickness = std::clamp(TheSettingManager->GetSettingF(section, "Thickness"), 100.0f, 6000.0f);
	Settings.Coverage[0] = std::clamp(TheSettingManager->GetSettingF(section, "CoveragePleasant"), 0.0f, 1.0f);
	Settings.Coverage[1] = std::clamp(TheSettingManager->GetSettingF(section, "CoverageCloudy"), 0.0f, 1.0f);
	Settings.Coverage[2] = std::clamp(TheSettingManager->GetSettingF(section, "CoverageRainy"), 0.0f, 1.0f);
	Settings.Coverage[3] = std::clamp(TheSettingManager->GetSettingF(section, "CoverageSnow"), 0.0f, 1.0f);
	Settings.TypeBias[0] = 0.2f; Settings.TypeBias[1] = 0.0f; Settings.TypeBias[2] = -0.4f; Settings.TypeBias[3] = -0.4f;
	Settings.Density = std::clamp(TheSettingManager->GetSettingF(section, "Density"), 0.05f, 5.0f);
	Settings.ShapeSize = std::clamp(TheSettingManager->GetSettingF(section, "ShapeSize"), 500.0f, 50000.0f);
	Settings.DetailSize = std::clamp(TheSettingManager->GetSettingF(section, "DetailSize"), 50.0f, 10000.0f);
	Settings.WeatherSize = std::clamp(TheSettingManager->GetSettingF(section, "WeatherSize"), 2000.0f, 200000.0f);
	Settings.Erosion = std::clamp(TheSettingManager->GetSettingF(section, "Erosion"), 0.0f, 1.0f);
	Settings.SunStrength = std::clamp(TheSettingManager->GetSettingF(section, "SunStrength"), 0.0f, 10.0f);
	Settings.AmbientStrength = std::clamp(TheSettingManager->GetSettingF(section, "AmbientStrength"), 0.0f, 10.0f);
	Settings.Forward = std::clamp(TheSettingManager->GetSettingF(section, "ForwardScattering"), 0.0f, 0.95f);
	Settings.Steps = std::clamp(TheSettingManager->GetSettingI(section, "Steps"), 8, 128);
	Settings.MaxDistance = std::clamp(TheSettingManager->GetSettingF(section, "MaxDistance"), 5000.0f, 200000.0f);
	Settings.HazeDistance = std::clamp(TheSettingManager->GetSettingF(section, "HazeDistance"), 1000.0f, 200000.0f);
	Settings.WindSpeed = std::clamp(TheSettingManager->GetSettingF(section, "WindSpeed"), 0.0f, 20.0f);
	Settings.Evolution = std::clamp(TheSettingManager->GetSettingF(section, "Evolution"), 0.0f, 50.0f);
	Settings.History = std::clamp(TheSettingManager->GetSettingF(section, "History"), 0.0f, 0.95f);
	Settings.Brightness = std::clamp(TheSettingManager->GetSettingF(section, "Brightness"), 0.0f, 5.0f);
	Settings.EdgeSharpness = std::clamp(TheSettingManager->GetSettingF(section, "EdgeSharpness"), 1.0f, 8.0f);
	Settings.ShapeSmoothing = std::clamp(TheSettingManager->GetSettingF(section, "ShapeSmoothing"), 0.0f, 3.0f);
	Settings.Turbulence = std::clamp(TheSettingManager->GetSettingF(section, "Turbulence"), 0.0f, 2.0f);
	Settings.DetailSmoothness = std::clamp(TheSettingManager->GetSettingF(section, "DetailSmoothness"), 0.0f, 1.0f);
	Settings.HideGameClouds = TheSettingManager->GetSettingI(section, "HideGameClouds") != 0;
	Settings.GameTime = TheSettingManager->GetSettingI(section, "GameTime") != 0;
	Settings.Shadows = std::clamp(TheSettingManager->GetSettingF(section, "Shadows"), 0.0f, 1.0f);
	Settings.ShadowRange = std::clamp(TheSettingManager->GetSettingF(section, "ShadowRange"), 4000.0f, 100000.0f);
	Settings.WaterReflections = std::clamp(TheSettingManager->GetSettingF(section, "WaterReflections"), 0.0f, 2.0f);
	Settings.ShadowDebug = TheSettingManager->GetSettingI(section, "ShadowDebug") != 0;
	Settings.ReflectionDebug = std::clamp(TheSettingManager->GetSettingI(section, "ReflectionDebug"), 0, 2);
	Settings.FineDetail = std::clamp(TheSettingManager->GetSettingF(section, "FineDetail"), 0.0f, 1.0f);
	Settings.FineDetailSize = std::clamp(TheSettingManager->GetSettingF(section, "FineDetailSize"), 10.0f, 5000.0f);
	Settings.Sharpness = std::clamp(TheSettingManager->GetSettingF(section, "Sharpness"), 0.0f, 1.0f);
	Settings.FromWeatherTextures = std::clamp(TheSettingManager->GetSettingF(section, "FromWeatherTextures"), 0.0f, 1.0f);
	coverage = -1.0f;
}

void VolumetricCloudsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_CloudsShape", &Constants.Shape);
	TheShaderManager->RegisterConstant("TESR_CloudsNoise", &Constants.Noise);
	TheShaderManager->RegisterConstant("TESR_CloudsLight", &Constants.Light);
	TheShaderManager->RegisterConstant("TESR_CloudsWind", &Constants.Wind);
	TheShaderManager->RegisterConstant("TESR_CloudsMarch", &Constants.March);
	TheShaderManager->RegisterConstant("TESR_CloudsLayout", &Constants.Layout);
	TheShaderManager->RegisterConstant("TESR_CloudsBlocks", &Constants.Blocks);
	TheShaderManager->RegisterConstant("TESR_CloudsHide", &Constants.Hide);
	TheShaderManager->RegisterConstant("TESR_CloudsLook", &Constants.Look);
	TheShaderManager->RegisterConstant("TESR_CloudsPrevViewProj", (D3DXVECTOR4*)&Constants.PrevViewProj);
	TheShaderManager->RegisterConstant("TESR_CloudsExtra", &Constants.Extra);
	TheShaderManager->RegisterConstant("TESR_CloudsDetail", &Constants.Detail);
	TheShaderManager->RegisterConstant("TESR_CloudsReflectInvVP", (D3DXVECTOR4*)&Constants.ReflectInvVP);
	TheShaderManager->RegisterConstant("TESR_CloudsHistoryViewProj", (D3DXVECTOR4*)&Constants.HistoryViewProj);
	TheShaderManager->RegisterConstant("TESR_CloudsReflectData", &Constants.ReflectData);
	TheShaderManager->RegisterConstant("TESR_CloudsMotion", &Constants.Motion);
}

void VolumetricCloudsEffect::RegisterTextures() {
	int resolution = std::clamp(TheSettingManager->GetSettingI("Shaders.VolumetricClouds.Main", "Resolution"), 0, 2);
	if (resolution == 0 && TheSettingManager->GetSettingI("Shaders.VolumetricClouds.Main", "HalfResolution")) resolution = 1;
	const UINT divisor = resolution == 2 ? 1 : resolution == 1 ? 2 : 4;
	lowWidth = (TheRenderManager->width + divisor - 1) / divisor;
	lowHeight = (TheRenderManager->height + divisor - 1) / divisor;
	blockWidth = (lowWidth + 1) / 2;
	blockHeight = (lowHeight + 1) / 2;
	TheTextureManager->InitTexture("TESR_CloudCurrent", &Textures.CurrentTexture, &Textures.CurrentSurface, blockWidth, blockHeight, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_CloudHistory0", &Textures.HistoryTexture[0], &Textures.HistorySurface[0], lowWidth, lowHeight, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_CloudHistory1", &Textures.HistoryTexture[1], &Textures.HistorySurface[1], lowWidth, lowHeight, D3DFMT_A16B16G16R16F);
	Textures.HistoryRead = Textures.HistoryTexture[0];
	TheTextureManager->RegisterTexture("TESR_CloudHistory", (IDirect3DBaseTexture9**)&Textures.HistoryRead);
	const int shadowResolution = TheSettingManager->GetSettingI("Shaders.VolumetricClouds.Main", "ShadowResolution");
	shadowMapSize = shadowResolution >= 2048 ? 2048 : shadowResolution >= 1024 ? 1024 : shadowResolution >= 512 ? 512 : 256;
	TheTextureManager->InitTexture("TESR_CloudShadow", &Textures.ShadowTexture, &Textures.ShadowSurface, shadowMapSize, shadowMapSize, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_CloudProbe", &Textures.ProbeTexture, &Textures.ProbeSurface, 1, 1, D3DFMT_A8R8G8B8);
	TheRenderManager->device->CreateOffscreenPlainSurface(1, 1, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &probeReadback, nullptr);
	Constants.Layout.x = 1.0f / lowWidth;
	Constants.Layout.y = 1.0f / lowHeight;
	Constants.Blocks.x = 1.0f / blockWidth;
	Constants.Blocks.y = 1.0f / blockHeight;
	D3DXMatrixIdentity(&lastViewProj);
}

static int WeatherIndex(TESWeather* weather) {
	if (!weather) return 0;
	switch (weather->GetWeatherType()) {
	case TESWeather::WeatherType::kType_Cloudy: return 1;
	case TESWeather::WeatherType::kType_Rainy: return 2;
	case TESWeather::WeatherType::kType_Snow: return 3;
	default: return 0;
	}
}

static std::string NormalTexturePath(const char* path) {
	std::string s = path ? path : "";
	for (char& c : s) c = c == '/' ? '\\' : (char)tolower((unsigned char)c);
	if (s.rfind("data\\", 0) == 0) s.erase(0, 5);
	if (s.rfind("textures\\", 0) == 0) s.erase(0, 9);
	return s;
}

static bool HasAlpha(D3DFORMAT format) {
	switch (format) {
	case D3DFMT_DXT1: case D3DFMT_X8R8G8B8: case D3DFMT_R8G8B8: case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_L8: return false;
	default: return true;
	}
}

static SkyShaderProperty* CloudLayer(Sky* sky, int j) {
	NiGeometry* layer = (NiGeometry*)sky->clouds->layers[j];
	if (!layer) return nullptr;
	SkyShaderProperty* property = layer->propertyState.GetShadeProperty<SkyShaderProperty>();
	return property && property->eSkyObjectType == SkyShaderProperty::SO_CLOUDS ? property : nullptr;
}

void VolumetricCloudsEffect::LearnWeatherFill(Sky* sky) {
	TESWeather* weather = sky ? sky->firstWeather : nullptr;
	if (!weather || !sky->clouds || weatherFill.count(weather)) return;
	float layers[4] = { 0, 0, 0, 0 };
	float clear = 1.0f;
	bool unreadable = false;
	for (int i = 0; i < 4; i++) {
		const std::string path = NormalTexturePath(weather->textureLayers[i].ddsPath.m_data);
		if (path.empty() || path == "sky\\alpha.dds") continue;
		auto cached = textureFill.find(path);
		if (cached == textureFill.end()) {
			NiTexture* match = nullptr;
			for (int j = 0; j < 4 && !match; j++) {
				SkyShaderProperty* layer = CloudLayer(sky, j);
				if (!layer || !layer->spBaseTexture || !layer->strTextureName.m_data) continue;
				NiTexture* texture = layer->spBaseTexture;
				const std::string loaded = NormalTexturePath(layer->strTextureName.m_data);
				const size_t start = loaded.size() - path.size();
				if (loaded.size() >= path.size() && loaded.compare(start, path.size(), path) == 0 && (start == 0 || loaded[start - 1] == '\\')) match = texture;
			}
			if (!match || !match->rendererData || !match->rendererData->dTexture) {
				if (++learnTries == 120) {
					Logger::Log("UNOFFICIAL VolumetricClouds: weather %08X: its cloud layer '%s' is not among the sky's loaded cloud textures yet:", weather->refID, path.c_str());
					for (int j = 0; j < 4; j++) {
						SkyShaderProperty* layer = CloudLayer(sky, j);
						NiTexture* texture = layer ? layer->spBaseTexture : nullptr;
						Logger::Log("UNOFFICIAL VolumetricClouds:   sky cloud layer %d: %s%s", j, layer && layer->strTextureName.m_data ? layer->strTextureName.m_data : "(none)",
							layer && !(texture && texture->rendererData && texture->rendererData->dTexture) ? " (no texture yet)" : "");
					}
					for (int k = 0; k < 4; k++) Logger::Log("UNOFFICIAL VolumetricClouds:   weather layer %d: %s", k, weather->textureLayers[k].ddsPath.m_data ? weather->textureLayers[k].ddsPath.m_data : "(none)");
				}
				return;
			}
			D3DSURFACE_DESC desc = {};
			IDirect3DBaseTexture9* base = match->rendererData->dTexture;
			if (base->GetType() != D3DRTYPE_TEXTURE || FAILED(((IDirect3DTexture9*)base)->GetLevelDesc(0, &desc)) || !HasAlpha(desc.Format)) {
				textureFill.emplace(path, -1.0f);
				Logger::Log("UNOFFICIAL VolumetricClouds: cloud texture %s: no alpha (format %u): left out", path.c_str(), (UINT)desc.Format);
				continue;
			}
			const UINT size = (std::max)(desc.Width, desc.Height);
			float lod = 0.0f;
			while ((size >> (UINT)(lod + 1.0f)) >= 16 && lod + 1.0f < (float)base->GetLevelCount()) lod += 1.0f;
			base->AddRef();
			probeQueue.push_back({ path, base, lod, desc.Width, desc.Height });
			textureFill.emplace(path, -2.0f);
			return;
		}
		if (cached->second == -2.0f) return;
		if (cached->second < 0.0f) { unreadable = true; continue; }
		layers[i] = cached->second;
		clear *= 1.0f - cached->second;
	}
	const float fill = 1.0f - clear;
	weatherFill[weather] = unreadable && fill == 0.0f ? -1.0f : fill;
	float coverage, rain, bias;
	WeatherLook(weather, coverage, rain, bias);
	const char* name = weather->GetEditorName();
	static const char* types[4] = { "pleasant", "cloudy", "rainy", "snow" };
	Logger::Log("UNOFFICIAL VolumetricClouds: weather %s (%08X, %s): the game's cloud layers fill %.2f %.2f %.2f %.2f, sky %.2f -> coverage %.2f",
		name && *name ? name : "(no editor id)", weather->refID, types[WeatherIndex(weather)], layers[0], layers[1], layers[2], layers[3], fill, coverage);
}

static float Piecewise(float x, const float (*points)[2], int count) {
	if (x <= points[0][0]) return points[0][1];
	for (int i = 1; i < count; i++)
		if (x <= points[i][0]) return std::lerp(points[i - 1][1], points[i][1], (x - points[i - 1][0]) / (points[i][0] - points[i - 1][0]));
	return points[count - 1][1];
}

void VolumetricCloudsEffect::WeatherLook(TESWeather* weather, float& coverage, float& rain, float& bias) {
	const int type = WeatherIndex(weather);
	coverage = Settings.Coverage[type];
	rain = type == 2 || type == 3 ? 1.0f : 0.0f;
	bias = Settings.TypeBias[type];
	auto found = weather ? weatherFill.find(weather) : weatherFill.end();
	if (found == weatherFill.end() || found->second < 0.0f || Settings.FromWeatherTextures <= 0.0f) return;
	const float fill = found->second;
	const float coveragePoints[4][2] = { { 0.0f, Settings.Coverage[0] * 0.5f }, { 0.13f, Settings.Coverage[0] }, { 0.5f, Settings.Coverage[1] }, { 0.85f, Settings.Coverage[2] } };
	const float biasPoints[3][2] = { { 0.13f, Settings.TypeBias[0] }, { 0.5f, Settings.TypeBias[1] }, { 0.85f, Settings.TypeBias[2] } };
	float fromTextures = Piecewise(fill, coveragePoints, 4);
	float biasFromTextures = Piecewise(fill, biasPoints, 3);
	if (rain > 0.0f) { fromTextures = (std::max)(fromTextures, coverage); biasFromTextures = (std::min)(biasFromTextures, bias); }
	coverage = std::lerp(coverage, fromTextures, Settings.FromWeatherTextures);
	bias = std::lerp(bias, biasFromTextures, Settings.FromWeatherTextures);
}

void VolumetricCloudsEffect::UpdateConstants() {
	Sky* sky = Tes ? Tes->sky : nullptr;
	static double lastFrameTime = -1.0;
	const float dt = (float)(ElapsedOncePerFrame(lastFrameTime) * TunedClockRate);

	if (dt > 0.0f) LearnWeatherFill(sky);
	TESWeather* current = sky ? sky->firstWeather : nullptr;
	if (current != lastWeather) {
		learnTries = 0;
		if (lastWeather && current && (!sky->secondWeather || sky->weatherPercent >= 0.99f)) snapWait = 1.0f;
		lastWeather = current;
	}
	float targetCoverage = Settings.Coverage[0], targetRain = 0.0f, targetType = Settings.TypeBias[0];
	if (current) {
		float nowCoverage, nowRain, nowBias;
		WeatherLook(current, nowCoverage, nowRain, nowBias);
		targetCoverage = nowCoverage; targetRain = nowRain; targetType = nowBias;
		if (sky->secondWeather && sky->secondWeather != current) {
			float beforeCoverage, beforeRain, beforeBias;
			WeatherLook(sky->secondWeather, beforeCoverage, beforeRain, beforeBias);
			const float t = std::clamp(sky->weatherPercent, 0.0f, 1.0f);
			targetCoverage = std::lerp(beforeCoverage, nowCoverage, t);
			targetRain = std::lerp(beforeRain, nowRain, t);
			targetType = std::lerp(beforeBias, nowBias, t);
		}
	}
	bool snap = coverage < 0.0f || TheShaderManager->GameState.isCellChanged;
	if (snapWait >= 0.0f) {
		snapWait -= dt;
		if (snapWait < 0.0f || weatherFill.count(current)) { snap = true; snapWait = -1.0f; }
	}
	if (snap) { coverage = targetCoverage; rain = targetRain; typeBias = targetType; }
	const float ease = 1.0f - expf(-dt / 20.0f);
	coverage += (targetCoverage - coverage) * ease;
	rain += (targetRain - rain) * ease;
	typeBias += (targetType - typeBias) * ease;

	const float windSpeed = sky ? std::clamp(sky->windSpeed, 0.0f, 1.0f) : 0.2f;
	const float windAngle = sky ? sky->windDirection : 0.0f;
	const float speed = Settings.WindSpeed * (15.0f + 45.0f * windSpeed);
	float advance = dt;
	if (Settings.GameTime) {
		TimeGlobals* clock = TimeGlobals::Get();
		const double days = clock && clock->GameDaysPassed ? (double)clock->GameDaysPassed->data : -1.0;
		const double scale = clock && clock->TimeScale && clock->TimeScale->data > 0.0f ? (double)clock->TimeScale->data : 30.0;
		if (days >= 0.0) {
			const double now = days * 86400.0 / scale;
			const float dayCount = (float)days;
			const double quantum = (double)(nextafterf(dayCount, FLT_MAX) - dayCount) * 86400.0 / scale;
			const double lead = (std::max)(0.25, 2.0 * quantum);
			static bool toldQuantum = false;
			if (!toldQuantum) { toldQuantum = true; Logger::Log("UNOFFICIAL VolumetricClouds: game day %.3f: the game's clock moves in steps of %.3f s of play here; the clouds run smoothly between them", days, quantum); }
			if (lastCloudTime < 0.0 || now < lastCloudTime - 60.0 - lead || now - lastCloudTime > 30.0 * 86400.0) {
				const double total = fmod(now, 1.0e7);
				windX = fmod(cos((double)windAngle) * speed * total, 900000.0);
				windY = fmod(sin((double)windAngle) * speed * total, 900000.0);
				windZ = fmod(Settings.Evolution * total, 900000.0);
				lastCloudTime = now;
				advance = 0.0f;
			}
			else if (now - lastCloudTime > (std::max)(2.0, 4.0 * quantum) + dt) {
				const double step = now - lastCloudTime;
				if (step > 120.0) Logger::Log("UNOFFICIAL VolumetricClouds: the game clock moved %.2f hours at once (wait, sleep, travel): the clouds move on with it (%.0f m).", step * scale / 3600.0, step * speed);
				advance = (float)step;
				lastCloudTime = now;
			}
			else {
				double next = lastCloudTime + dt;
				if (next > now + lead) next = (std::max)(lastCloudTime, now + lead);
				else if (next < now - lead) next += (now - lead - next) * (std::min)(1.0, 2.0 * dt);
				advance = (float)(next - lastCloudTime);
				lastCloudTime = next;
			}
		}
	}
	const double lastX = windX, lastY = windY;
	windX = fmod(windX + cos((double)windAngle) * speed * advance, 900000.0);
	windY = fmod(windY + sin((double)windAngle) * speed * advance, 900000.0);
	windZ = fmod(windZ + Settings.Evolution * advance, 900000.0);
	wind = D3DXVECTOR3((float)windX, (float)windY, (float)windZ);
	double moveX = windX - lastX, moveY = windY - lastY;
	if (fabs(moveX) > 450000.0) moveX -= copysign(900000.0, moveX);
	if (fabs(moveY) > 450000.0) moveY -= copysign(900000.0, moveY);
	if (dt > 0.0f) { Constants.Motion.x = (float)moveX; Constants.Motion.y = (float)moveY; }
	Constants.Motion.w = Settings.Sharpness;
	Constants.Extra = D3DXVECTOR4(Textures.ShadowSurface ? Settings.Shadows : 0.0f, Settings.ShadowRange, (float)Settings.ReflectionDebug, Settings.ShadowDebug ? 1.0f : 0.0f);

	Constants.Shape = D3DXVECTOR4(Settings.Base, Settings.Thickness, coverage, Settings.Density);
	Constants.Noise = D3DXVECTOR4(Settings.ShapeSize, Settings.DetailSize, Settings.Erosion, Settings.WeatherSize);
	Constants.Light = D3DXVECTOR4(Settings.SunStrength, Settings.AmbientStrength, Settings.Forward, rain);
	Constants.Wind = D3DXVECTOR4(wind.x, wind.y, wind.z, typeBias);
	Constants.March = D3DXVECTOR4((float)Settings.Steps, Settings.MaxDistance, (float)frame, Settings.HazeDistance);
	Constants.Layout.w = Settings.Brightness;
	Constants.Look = D3DXVECTOR4(Settings.EdgeSharpness, Settings.ShapeSmoothing, Settings.Turbulence, Settings.DetailSmoothness);
	Constants.Detail = D3DXVECTOR4(Settings.FineDetail, Settings.FineDetailSize, (float)shadowMapSize, 0.0f);

	HideGameClouds = (Enabled && Effect && Settings.HideGameClouds && ShouldRender()) ? 1.0f : 0.0f;
	HideGameCloudsInReflection = HideGameClouds > 0.0f && ReflectionWanted() ? 1.0f : 0.0f;
	Constants.Hide.x = HideGameClouds;
}

bool VolumetricCloudsEffect::ShouldRender() {
	return TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater &&
		Textures.CurrentSurface && Textures.HistorySurface[0] && Textures.HistorySurface[1];
}

void VolumetricCloudsEffect::ProbeLayer(IDirect3DDevice9* Device) {
	if (probeQueue.empty() || !Textures.ProbeSurface || !probeReadback) return;
	ProbeJob job = probeQueue.front();
	probeQueue.erase(probeQueue.begin());
	float fill = -1.0f;
	Device->SetTexture(12, job.texture);
	TheRenderManager->renderState->NoteDeviceTexture(12, job.texture);
	const DWORD states[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP},
		{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_LINEAR} };
	for (const auto& state : states) TheRenderManager->SetSamplerState(12, (D3DSAMPLERSTATETYPE)state[0], state[1]);
	Constants.Motion.z = job.lod;
	TheShaderManager->RenderEffectToRT(Textures.ProbeSurface, this, false, 5);
	Device->SetTexture(12, nullptr);
	TheRenderManager->renderState->NoteDeviceTexture(12, nullptr);
	D3DLOCKED_RECT locked;
	if (SUCCEEDED(Device->GetRenderTargetData(Textures.ProbeSurface, probeReadback)) && SUCCEEDED(probeReadback->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
		fill = (float)(*(const UInt32*)locked.pBits >> 24) / 255.0f;
		probeReadback->UnlockRect();
	}
	job.texture->Release();
	textureFill[job.path] = fill;
	Logger::Log("UNOFFICIAL VolumetricClouds: cloud texture %s (%ux%u, read at mip %.0f): %.0f%% cloud%s", job.path.c_str(), job.width, job.height,
		job.lod, fill * 100.0f, fill < 0.0f ? " (could not be read: left out)" : "");
}

void VolumetricCloudsEffect::RenderClouds(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, IDirect3DSurface9* SourceSurface) {
	const D3DXMATRIX viewProj = TheRenderManager->ViewProjMatrix;
	const double now = TheFrameRateManager->Time;
	const bool continuous = lastRenderTime >= 0.0 && now - lastRenderTime < 0.25 && !TheShaderManager->GameState.isCellChanged;
	static const float order[4][2] = { {0, 0}, {1, 1}, {1, 0}, {0, 1} };
	Constants.PrevViewProj = continuous ? lastViewProj : viewProj;
	Constants.Layout.z = continuous ? Settings.History : 0.0f;
	Constants.Blocks.z = order[frame % 4][0];
	Constants.Blocks.w = order[frame % 4][1];
	Constants.March.z = (float)frame;

	Textures.HistoryRead = Textures.HistoryTexture[read];
	ProbeLayer(Device);
	static GpuTimer marchTimer("  Clouds march (quarter, 1 in 4)"), temporalTimer("  Clouds temporal (quarter)"),
		shadowTimer("  Clouds shadow map"), compositeTimer("  Clouds composite (full)");
	{ GpuProfileScope gpu(marchTimer, Device); TheShaderManager->RenderEffectToRT(Textures.CurrentSurface, this, false, 1); }
	{ GpuProfileScope gpu(temporalTimer, Device); TheShaderManager->RenderEffectToRT(Textures.HistorySurface[1 - read], this, false, 2); }
	if (Constants.Extra.x > 0.0f) { GpuProfileScope gpu(shadowTimer, Device); TheShaderManager->RenderEffectToRT(Textures.ShadowSurface, this, false, 4); }
	read = 1 - read;
	Textures.HistoryRead = Textures.HistoryTexture[read];
	Device->SetRenderTarget(0, RenderTarget);
	{ GpuProfileScope gpu(compositeTimer, Device); Render(Device, RenderTarget, RenderedSurface, 0, false, SourceSurface); }

	lastViewProj = viewProj;
	lastRenderTime = now;
	frame++;
}

bool VolumetricCloudsEffect::ReflectionWanted() const {
	return Enabled && Effect && Settings.WaterReflections > 0.0f && TheShaderManager->GameState.isExterior &&
		!TheShaderManager->GameState.isUnderwater && lastRenderTime >= 0.0 && TheFrameRateManager->Time - lastRenderTime < 0.25;
}

namespace {
	typedef HRESULT(__stdcall* StretchRectFn)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
	StretchRectFn OriginalStretchRect = nullptr;
	void* RuntimeStretchRect = nullptr;
	VolumetricCloudsEffect* ResolveOwner = nullptr;
	HRESULT __stdcall CloudsStretchRect(IDirect3DDevice9* device, IDirect3DSurface9* source, const RECT* sourceRect, IDirect3DSurface9* dest,
		const RECT* destRect, D3DTEXTUREFILTERTYPE filter) {
		if (ResolveOwner) ResolveOwner->BeforeResolve(device, source);
		return OriginalStretchRect(device, source, sourceRect, dest, destRect, filter);
	}
}

void VolumetricCloudsEffect::BeforeResolve(IDirect3DDevice9* Device, IDirect3DSurface9* source) {
	if (reflectTarget && source == reflectTarget) {
		static int told = 0;
		if (told++ < 3) Logger::Log("UNOFFICIAL VolumetricClouds: the reflection is copied for the water (StretchRect): clouds drawn first");
		DrawIntoReflection(Device);
	}
}

void VolumetricCloudsEffect::CaptureReflection(IDirect3DDevice9* Device) {
	if (reflectNoted || !ReflectionWanted()) return;
	reflectNoted = true;
	void** table = *(void***)Device;
	if (!RuntimeStretchRect) RuntimeStretchRect = table[34];
	if (table[34] == RuntimeStretchRect) {
		DWORD old = 0;
		if (VirtualProtect(&table[34], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
			OriginalStretchRect = (StretchRectFn)table[34];
			table[34] = (void*)&CloudsStretchRect;
			VirtualProtect(&table[34], sizeof(void*), old, &old);
			ResolveOwner = this;
			static int told = 0;
			if (told++ < 2) Logger::Log("UNOFFICIAL VolumetricClouds: StretchRect hooked (the clouds go into the reflection before the game copies it for the water)");
		}
	}
	else if (table[34] != (void*)&CloudsStretchRect) {
		static bool told = false;
		if (!told) { told = true; Logger::Log("UNOFFICIAL VolumetricClouds: another program owns StretchRect: the clouds are drawn after the reflection pass"); }
	}
	if (FAILED(Device->GetRenderTarget(0, &reflectTarget))) { reflectTarget = nullptr; return; }
	if (FAILED(Device->GetDepthStencilSurface(&reflectDepth))) reflectDepth = nullptr;
	if (FAILED(Device->GetTransform(D3DTS_VIEW, &reflectView)) || FAILED(Device->GetTransform(D3DTS_PROJECTION, &reflectProj)) || !reflectDepth) {
		reflectTarget->Release(); reflectTarget = nullptr;
		if (reflectDepth) { reflectDepth->Release(); reflectDepth = nullptr; }
	}
}

void VolumetricCloudsEffect::EndReflectionPass(IDirect3DDevice9* Device) {
	DrawIntoReflection(Device);
	reflectNoted = false;
}

void VolumetricCloudsEffect::DrawIntoReflection(IDirect3DDevice9* Device) {
	if (!reflectTarget) return;
	IDirect3DSurface9* target = reflectTarget; IDirect3DSurface9* depth = reflectDepth;
	reflectTarget = nullptr; reflectDepth = nullptr;
	D3DSURFACE_DESC desc;
	if (ReflectionWanted() && SUCCEEDED(target->GetDesc(&desc)) && (reflectState || SUCCEEDED(Device->CreateStateBlock(D3DSBT_ALL, &reflectState)))) {
		const bool reversed = TheRenderManager->IsReversedDepth();
		D3DXMATRIX rotation = reflectView;
		rotation._41 = rotation._42 = rotation._43 = 0.0f;
		D3DXMATRIX viewProj;
		D3DXMatrixMultiply(&viewProj, &rotation, &reflectProj);
		if (D3DXMatrixInverse(&Constants.ReflectInvVP, nullptr, &viewProj)) {
			Constants.HistoryViewProj = lastViewProj;
			Constants.ReflectData = D3DXVECTOR4(std::clamp(Settings.WaterReflections, 0.0f, 1.0f), (float)desc.Width, (float)desc.Height, reversed ? 0.0f : 1.0f);
			if (!reflectLogged) {
				reflectLogged = true;
				Logger::Log("UNOFFICIAL VolumetricClouds: drawing the clouds into the water reflection (%ux%u, format %u, multisample %u, %s depth)",
					desc.Width, desc.Height, (unsigned)desc.Format, (unsigned)desc.MultiSampleType, reversed ? "reversed" : "normal");
				Logger::Log("UNOFFICIAL VolumetricClouds: reflection view rows %.3f %.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f | t %.1f %.1f %.1f; proj 11 %.3f 22 %.3f 33 %.5f 43 %.3f; det %.3f; main view row3 %.3f %.3f %.3f",
					reflectView._11, reflectView._12, reflectView._13, reflectView._21, reflectView._22, reflectView._23, reflectView._31, reflectView._32, reflectView._33,
					reflectView._41, reflectView._42, reflectView._43, reflectProj._11, reflectProj._22, reflectProj._33, reflectProj._43, D3DXMatrixDeterminant(&rotation),
					TheRenderManager->viewMatrix._31, TheRenderManager->viewMatrix._32, TheRenderManager->viewMatrix._33);
			}
			reflectState->Capture();
			IDirect3DSurface9* oldTarget = nullptr; IDirect3DSurface9* oldDepth = nullptr;
			Device->GetRenderTarget(0, &oldTarget);
			Device->GetDepthStencilSurface(&oldDepth);
			Device->SetRenderTarget(0, target);
			Device->SetDepthStencilSurface(depth);
			D3DVIEWPORT9 viewport = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
			Device->SetViewport(&viewport);
			Device->SetStreamSource(0, TheShaderManager->FrameVertex, 0, 5 * sizeof(float));
			Device->SetFVF(FrameFVF);
			Effect->SetTechnique(Effect->GetTechnique(3));
			SetCT();
			UINT passes = 0;
			if (SUCCEEDED(Effect->Begin(&passes, 0))) {
				Effect->BeginPass(0);
				Device->SetRenderState(D3DRS_ZENABLE, Settings.ReflectionDebug == 2 ? D3DZB_FALSE : D3DZB_TRUE);
				Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
				Device->SetRenderState(D3DRS_ZFUNC, reversed ? D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL);
				Device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
				Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
				Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
				Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
				Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
				Device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
				Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
				Device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
				Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
				Device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
				Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
				Effect->EndPass();
				Effect->End();
			}
			Device->SetRenderTarget(0, oldTarget);
			Device->SetDepthStencilSurface(oldDepth);
			if (oldTarget) oldTarget->Release();
			if (oldDepth) oldDepth->Release();
			reflectState->Apply();
			for (UInt32 c = 0; c < TextureShaderValuesCount; c++) {
				const UInt32 stage = TextureShaderValues[c].RegisterIndex;
				if (stage >= 16) continue;
				IDirect3DBaseTexture9* texture = nullptr;
				Device->GetTexture(stage, &texture);
				TheRenderManager->renderState->NoteDeviceTexture(stage, texture);
				if (texture) texture->Release();
				for (int i = 1; i < SamplerStatesMax; i++) {
					DWORD value = 0;
					if (SUCCEEDED(Device->GetSamplerState(stage, (D3DSAMPLERSTATETYPE)i, &value)))
						TheRenderManager->SetSamplerState(stage, (D3DSAMPLERSTATETYPE)i, value);
				}
			}
		}
	}
	target->Release();
	if (depth) depth->Release();
}
