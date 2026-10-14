#include "VolumetricClouds.h"
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
	Settings.Steps = std::clamp(TheSettingManager->GetSettingI(section, "Steps"), 8, 64);
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
	TheShaderManager->RegisterConstant("TESR_CloudsReflectInvVP", (D3DXVECTOR4*)&Constants.ReflectInvVP);
	TheShaderManager->RegisterConstant("TESR_CloudsHistoryViewProj", (D3DXVECTOR4*)&Constants.HistoryViewProj);
	TheShaderManager->RegisterConstant("TESR_CloudsReflectData", &Constants.ReflectData);
}

void VolumetricCloudsEffect::RegisterTextures() {
	const UINT divisor = TheSettingManager->GetSettingI("Shaders.VolumetricClouds.Main", "HalfResolution") ? 2 : 4;
	lowWidth = (TheRenderManager->width + divisor - 1) / divisor;
	lowHeight = (TheRenderManager->height + divisor - 1) / divisor;
	blockWidth = (lowWidth + 1) / 2;
	blockHeight = (lowHeight + 1) / 2;
	TheTextureManager->InitTexture("TESR_CloudCurrent", &Textures.CurrentTexture, &Textures.CurrentSurface, blockWidth, blockHeight, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_CloudHistory0", &Textures.HistoryTexture[0], &Textures.HistorySurface[0], lowWidth, lowHeight, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_CloudHistory1", &Textures.HistoryTexture[1], &Textures.HistorySurface[1], lowWidth, lowHeight, D3DFMT_A16B16G16R16F);
	Textures.HistoryRead = Textures.HistoryTexture[0];
	TheTextureManager->RegisterTexture("TESR_CloudHistory", (IDirect3DBaseTexture9**)&Textures.HistoryRead);
	TheTextureManager->InitTexture("TESR_CloudShadow", &Textures.ShadowTexture, &Textures.ShadowSurface, 256, 256, D3DFMT_A16B16G16R16F);
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

void VolumetricCloudsEffect::UpdateConstants() {
	Sky* sky = Tes ? Tes->sky : nullptr;
	const float dt = std::clamp((float)TheFrameRateManager->ElapsedTime, 0.0f, 0.1f);

	float targetCoverage = Settings.Coverage[0], targetRain = 0.0f, targetType = Settings.TypeBias[0];
	if (sky && sky->firstWeather) {
		const int now = WeatherIndex(sky->firstWeather);
		const int before = sky->secondWeather ? WeatherIndex(sky->secondWeather) : now;
		const float t = sky->secondWeather ? std::clamp(sky->weatherPercent, 0.0f, 1.0f) : 1.0f;
		targetCoverage = std::lerp(Settings.Coverage[before], Settings.Coverage[now], t);
		targetRain = std::lerp(before == 2 || before == 3 ? 1.0f : 0.0f, now == 2 || now == 3 ? 1.0f : 0.0f, t);
		targetType = std::lerp(Settings.TypeBias[before], Settings.TypeBias[now], t);
	}
	if (coverage < 0.0f || TheShaderManager->GameState.isCellChanged) { coverage = targetCoverage; rain = targetRain; typeBias = targetType; }
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
			const double step = now - lastCloudTime;
			if (lastCloudTime < 0.0 || step < 0.0 || step > 30.0 * 86400.0) {
				const double total = fmod(now, 1.0e7);
				wind.x = (float)fmod(cos((double)windAngle) * speed * total, 900000.0);
				wind.y = (float)fmod(sin((double)windAngle) * speed * total, 900000.0);
				wind.z = (float)fmod(Settings.Evolution * total, 900000.0);
				advance = 0.0f;
			}
			else {
				advance = (float)step;
				if (step > 120.0) Logger::Log("UNOFFICIAL VolumetricClouds: the game clock moved %.2f hours at once (wait, sleep, travel): the clouds move on with it (%.0f m).", step * scale / 3600.0, step * speed);
			}
			lastCloudTime = now;
		}
	}
	wind.x = fmodf(wind.x + cosf(windAngle) * speed * advance, 900000.0f);
	wind.y = fmodf(wind.y + sinf(windAngle) * speed * advance, 900000.0f);
	wind.z = fmodf(wind.z + Settings.Evolution * advance, 900000.0f);
	Constants.Extra = D3DXVECTOR4(Textures.ShadowSurface ? Settings.Shadows : 0.0f, Settings.ShadowRange, (float)Settings.ReflectionDebug, Settings.ShadowDebug ? 1.0f : 0.0f);

	Constants.Shape = D3DXVECTOR4(Settings.Base, Settings.Thickness, coverage, Settings.Density);
	Constants.Noise = D3DXVECTOR4(Settings.ShapeSize, Settings.DetailSize, Settings.Erosion, Settings.WeatherSize);
	Constants.Light = D3DXVECTOR4(Settings.SunStrength, Settings.AmbientStrength, Settings.Forward, rain);
	Constants.Wind = D3DXVECTOR4(wind.x, wind.y, wind.z, typeBias);
	Constants.March = D3DXVECTOR4((float)Settings.Steps, Settings.MaxDistance, (float)frame, Settings.HazeDistance);
	Constants.Layout.w = Settings.Brightness;
	Constants.Look = D3DXVECTOR4(Settings.EdgeSharpness, Settings.ShapeSmoothing, Settings.Turbulence, Settings.DetailSmoothness);

	HideGameClouds = (Enabled && Effect && Settings.HideGameClouds && ShouldRender()) ? 1.0f : 0.0f;
	HideGameCloudsInReflection = HideGameClouds > 0.0f && ReflectionWanted() ? 1.0f : 0.0f;
	Constants.Hide.x = HideGameClouds;
}

bool VolumetricCloudsEffect::ShouldRender() {
	return TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater &&
		Textures.CurrentSurface && Textures.HistorySurface[0] && Textures.HistorySurface[1];
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
