#include "VolumetricLight.h"
#include "../core/GpuProfiler.h"

static void ReadLook(const char* section, VolumetricLightEffect::LookStruct& look) {
	look.Strength = std::clamp(TheSettingManager->GetSettingF(section, "Strength"), 0.0f, 10.0f);
	look.Darken = std::clamp(TheSettingManager->GetSettingF(section, "Darken"), 0.0f, 1.0f);
	look.Density = std::clamp(TheSettingManager->GetSettingF(section, "Density"), 0.0f, 10.0f);
	look.MaxDistance = std::clamp(TheSettingManager->GetSettingF(section, "MaxDistance"), 100.0f, 30000.0f);
	look.Anisotropy = std::clamp(TheSettingManager->GetSettingF(section, "Anisotropy"), 0.0f, 0.9f);
}

void VolumetricLightEffect::UpdateSettings() {
	const char* section = "Shaders.VolumetricLight.Main";
	ReadLook(section, Settings.Day);
	ReadLook("Shaders.VolumetricLight.Night", Settings.Night);
	Settings.Steps = std::clamp(TheSettingManager->GetSettingI(section, "Steps"), 4, 48);
	Settings.Temporal = std::clamp(TheSettingManager->GetSettingF(section, "Temporal"), 0.0f, 0.97f);
	Settings.BlurRadius = std::clamp(TheSettingManager->GetSettingF(section, "BlurRadius"), 0.0f, 8.0f);
	Settings.RainStrength = std::clamp(TheSettingManager->GetSettingF(section, "RainStrength"), 0.0f, 1.0f);
	Settings.Froxels = TheSettingManager->GetSettingI(section, "Froxels") != 0;
	Settings.FogVolume = TheSettingManager->GetSettingI("Shaders.VolumetricFog.Main", "FogVolume") != 0;
}

void VolumetricLightEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_VolumetricLightData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_VolumetricLightData2", &Constants.Data2);
	TheShaderManager->RegisterConstant("TESR_VolumetricLightData3", &Constants.Data3);
	TheShaderManager->RegisterConstant("TESR_VolumetricLightLayout", &Constants.Layout);
	TheShaderManager->RegisterConstant("TESR_VolumetricLightReproject", (D3DXVECTOR4*)&Constants.Reproject);
	TheShaderManager->RegisterConstant("TESR_VolumetricLightGridLayout", &Constants.GridLayout);
	TheShaderManager->RegisterConstant("TESR_VolumetricLightGridData", &Constants.GridData);
}

void VolumetricLightEffect::RegisterTextures() {
	const UINT w = (TheRenderManager->width + 1) / 2, h = (TheRenderManager->height + 1) / 2;
	TheTextureManager->InitTexture("TESR_VolumetricLightMarch", &Textures.MarchTexture, &Textures.MarchSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_VolumetricLightBuffer", &Textures.BlurTexture, &Textures.BlurSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_VolumetricLightTemp", &Textures.TempTexture, &Textures.TempSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_VolumetricLightHistory", &Textures.HistoryTexture, &Textures.HistorySurface, w, h, D3DFMT_A16B16G16R16F);
	Constants.Layout = D3DXVECTOR4(1.0f / w, 1.0f / h, 0.0f, 0.0f);

	// UNOFFICIAL 2026-10-14 froxel grid: one column per 16 x 16 pixels, its 64 slices in 8 x 8 tiles (1280 x 720 at 1440p,
	// 7.4 MB each). Made whatever the setting, so it can be switched on in game.
	const UINT gw = (TheRenderManager->width + 15) / 16, gh = (TheRenderManager->height + 15) / 16;
	TheTextureManager->InitTexture("TESR_VolumetricLightGrid", &Textures.GridTexture, &Textures.GridSurface, gw * 8, gh * 8, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_VolumetricLightGridTemp", &Textures.GridTempTexture, &Textures.GridTempSurface, gw * 8, gh * 8, D3DFMT_A16B16G16R16F);
	Constants.GridLayout = D3DXVECTOR4((float)gw, (float)gh, 1.0f / (gw * 8), 1.0f / (gh * 8));
}

void VolumetricLightEffect::UpdateConstants() {
	frame++;

	// Day and night looks, blended by the time of day (GameState.isDayTime: 1 day, 0 night, smooth through dawn/dusk).
	const float day = std::clamp(TheShaderManager->GameState.isDayTime, 0.0f, 1.0f);
	const LookStruct& d = Settings.Day;
	const LookStruct& n = Settings.Night;

	// Fade: the sun shadows' own fade (ShadowFade.x, 1 = no shadows: sunrise, sunset, a dim moon), less in rain, as set.
	const ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	float fade = 1.0f - std::clamp(shadows->Constants.ShadowFade.x, 0.0f, 1.0f);
	fade *= std::lerp(1.0f, Settings.RainStrength, std::clamp(TheShaderManager->Effects.Rain->Constants.RainAnimator.GetValue(), 0.0f, 1.0f));

	Constants.Data.x = std::lerp(n.Strength, d.Strength, day);
	Constants.Data.y = std::lerp(n.Darken, d.Darken, day);
	Constants.Data.z = std::lerp(n.Density, d.Density, day) * 0.001f;
	Constants.Data.w = std::lerp(n.MaxDistance, d.MaxDistance, day);
	Constants.Data2.x = (float)Settings.Steps;
	Constants.Data2.y = std::lerp(n.Anisotropy, d.Anisotropy, day);
	Constants.Data2.z = fade;
	Constants.Data3.y = Settings.BlurRadius;

	// The grid's lit share is scaled to about 1 for a view through the whole grid (the half-float grid would round the
	// small per-cell values of thin air away), and divided back out where it is read.
	const float depth = Constants.Data.z * Constants.Data.w;
	Constants.GridData.y = depth > 1.0e-4f ? 1.0f / (1.0f - expf(-depth)) : 1.0f;
	VolumetricFogEffect* fog = TheShaderManager->Effects.VolumetricFog;
	Constants.GridData.x = Settings.FogVolume && fog && fog->Enabled && !fog->IsLegacy() ? 1.0f : 0.0f;
}

bool VolumetricLightEffect::ShouldRender() {
	const ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	return TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater &&
		shadows && shadows->Constants.ShadowFade.y > 0.0f &&	// the sun's shadow maps are drawn this frame
		Constants.Data2.z > 0.001f && (Constants.Data.x > 0.0f || Constants.Data.y > 0.0f) &&
		Textures.MarchSurface && Textures.BlurSurface && Textures.TempSurface && Textures.HistorySurface;
}

// The half-resolution passes, into the effect's own buffers (techniques: 1 March, 3 Temporal, 2 Blur X, 4 Blur Y).
// With Temporal on: March -> March buffer, Temporal -> Temp (copied to History for next frame), Blur X -> March,
// Blur Y -> Buffer. Off: March -> Temp, then the same blurs. The composite (technique 0) then reads Buffer.
void VolumetricLightEffect::RenderBuffers(IDirect3DDevice9* Device) {
	if (CompositeTechnique() == 8) {
		// UNOFFICIAL 2026-10-14 froxel grid: light the cells, then the two summing passes (techniques 5, 6, 7).
		static GpuTimer gridTimers[3] = { GpuTimer("  VL grid light"), GpuTimer("  VL grid sum rows"), GpuTimer("  VL grid sum carry") };
		auto gridPass = [&](IDirect3DSurface9* target, UINT technique) {
			GpuProfileScope gpuPass(gridTimers[technique - 5], Device);
			Device->SetRenderTarget(0, target);
			Render(Device, target, target, technique, false, target);
		};
		// The fog's 3D noise on stage 9 (NVR_FogNoise) while the cells are lit, for the fog volume.
		static IDirect3DBaseTexture9* noise = nullptr;
		static bool noiseLoaded = false;
		if (!noiseLoaded) {
			noiseLoaded = true;
			noise = TheTextureManager->GetFileTexture("Data\\Textures\\Effects\\NoiseVolume.dds", TextureRecord::TextureRecordType::VolumeBuffer);
		}
		const bool bindNoise = Constants.GridData.x > 0.5f && noise;
		if (bindNoise) {
			Device->SetTexture(9, noise);
			TheRenderManager->renderState->NoteDeviceTexture(9, noise);
			const DWORD states[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP}, {D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP},
				{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_LINEAR} };
			for (const auto& s : states) TheRenderManager->SetSamplerState(9, (D3DSAMPLERSTATETYPE)s[0], s[1]);
		}
		gridPass(Textures.GridSurface, 5);
		if (bindNoise) {
			Device->SetTexture(9, nullptr);
			TheRenderManager->renderState->NoteDeviceTexture(9, nullptr);
		}
		gridPass(Textures.GridTempSurface, 6);
		gridPass(Textures.GridSurface, 7);
		haveHistory = false;	// the march's history is stale when it is switched back
		lastRenderedFrame = frame;
		return;
	}
	const bool temporal = Settings.Temporal > 0.0f;
	const D3DXMATRIX& view = TheRenderManager->viewMatrix;
	const D3DXMATRIX& invView = TheRenderManager->InvViewMatrix;

	// Last frame's result is usable if this effect drew it, and the camera did not jump (load, fast travel, a cut).
	bool usable = temporal && haveHistory && lastRenderedFrame + 1 == frame;
	if (usable) {
		const double dx = (double)invView._41 - lastInvView._41, dy = (double)invView._42 - lastInvView._42,
			dz = (double)invView._43 - lastInvView._43;
		usable = dx * dx + dy * dy + dz * dz < 500.0 * 500.0;
	}
	if (usable) {
		// Camera-relative world position p -> last frame's view: (p + cameraNow - cameraLast) * Rlast, then last frame's
		// projection. Worked out round the two camera positions in double precision (the Mojave's coordinates run past
		// 100 000 units, where float rounding would lose the few units the camera moves between frames).
		const double move[3] = { (double)invView._41 - lastInvView._41, (double)invView._42 - lastInvView._42,
			(double)invView._43 - lastInvView._43 };
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
	Constants.Data2.w = temporal ? (float)(frame % 64) : 0.0f;

	// UNOFFICIAL 2026-10-13: F10 timers per pass (technique 1 march, 3 temporal, 2 / 4 blur).
	static GpuTimer passTimers[5] = { GpuTimer("  VL composite"), GpuTimer("  VL march (half)"), GpuTimer("  VL blur X (half)"),
		GpuTimer("  VL temporal (half)"), GpuTimer("  VL blur Y (half)") };
	auto pass = [&](IDirect3DSurface9* target, UINT technique) {
		GpuProfileScope gpuPass(passTimers[technique], Device);
		Device->SetRenderTarget(0, target);
		Render(Device, target, target, technique, false, target);
	};
	if (temporal) {
		pass(Textures.MarchSurface, 1);
		pass(Textures.TempSurface, 3);
		Device->StretchRect(Textures.TempSurface, NULL, Textures.HistorySurface, NULL, D3DTEXF_NONE);
	}
	else
		pass(Textures.TempSurface, 1);
	pass(Textures.MarchSurface, 2);
	pass(Textures.BlurSurface, 4);

	haveHistory = temporal;
	lastRenderedFrame = frame;
	lastView = view;
	lastInvView = invView;
	lastProj = TheRenderManager->projMatrix;
}
