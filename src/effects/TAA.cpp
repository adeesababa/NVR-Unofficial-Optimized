#include <algorithm>

#include "TAA.h"

void TAAEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_TAAData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_TAAPrevProjection", &Constants.PrevProjection);
	TheShaderManager->RegisterConstant("TESR_TAACameraDelta", &Constants.CameraDelta);
	TheShaderManager->RegisterConstant("TESR_TAAWeapon", &Constants.Weapon);
	TheShaderManager->RegisterConstant("TESR_TAASharpen", &Constants.Sharpen);
	TheShaderManager->RegisterConstant("TESR_TAAPrevViewTransform", (D3DXVECTOR4*)&Constants.PrevViewTransform);
}

void TAAEffect::RegisterTextures() {}

bool TAAEffect::EnsureTextures() {
	if (Textures.ResolveSurface && Textures.HistorySurface) return true;
	if (texturesFailed) return false;
	int width = TheRenderManager->width;
	int height = TheRenderManager->height;

	if (!Textures.ResolveSurface)
		TheTextureManager->InitTexture("TESR_TAAResolveBuffer", &Textures.ResolveTexture, &Textures.ResolveSurface, width, height, D3DFMT_A16B16G16R16F);
	if (!Textures.HistorySurface)
		TheTextureManager->InitTexture("TESR_TAAHistoryBuffer", &Textures.HistoryTexture, &Textures.HistorySurface, width, height, D3DFMT_A16B16G16R16F);
	texturesFailed = !Textures.ResolveSurface || !Textures.HistorySurface;
	if (texturesFailed) Logger::Log("[ERROR] TAA : could not create its buffers; TAA stays off");
	return !texturesFailed;
}

void TAAEffect::UpdateSettings() {
	float weight = TheSettingManager->GetSettingF("Shaders.TAA.Main", "HistoryWeight");
	Settings.HistoryWeight = weight > 0.0f ? std::clamp(weight, 0.5f, 0.97f) : 0.9f;

	float gamma = TheSettingManager->GetSettingF("Shaders.TAA.Main", "ClipGamma");
	Settings.ClipGamma = gamma > 0.0f ? std::clamp(gamma, 0.5f, 2.5f) : 1.0f;

	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.TAA.Main", "DebugView"), 0, 5);

	Settings.Jitter = TheSettingManager->GetSettingI("Shaders.TAA.Main", "Jitter") != 0;

	Settings.WeaponTAA = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.Main", "WeaponTAA"), 0.0f, 1.0f);

	Settings.Sharpness = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.Main", "Sharpness"), 0.0f, 1.0f);
	Settings.MipBias = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.Main", "MipBias"), -1.5f, 0.0f);
}

void TAAEffect::SetMipBias(float bias) {
	DWORD value = *(DWORD*)&bias;
	for (DWORD sampler = 0; sampler < 16; sampler++)
		TheRenderManager->device->SetSamplerState(sampler, D3DSAMP_MIPMAPLODBIAS, value);
}

void TAAEffect::UpdateConstants() {
	Constants.Data.x = Settings.HistoryWeight;
	Constants.Data.y = Settings.ClipGamma;
	Constants.Data.w = (float)Settings.DebugView;
	Constants.Weapon = D3DXVECTOR4(Settings.WeaponTAA, 0.0f, 0.0f, 0.0f);
	Constants.Sharpen = D3DXVECTOR4(Settings.Sharpness, 0.0f, 0.0f, 0.0f);
}

void TAAEffect::RememberCamera() {
	Constants.PrevViewTransform = TheRenderManager->viewMatrix;
	Constants.PrevProjection = D3DXVECTOR4(TheRenderManager->projMatrix._11, TheRenderManager->projMatrix._22, 0.0f, 0.0f);
	prevCameraPosition = TheRenderManager->CameraPosition;
}

bool TAAEffect::DrawTechnique(const char* TechniqueName) {
	D3DXHANDLE technique = Effect->GetTechniqueByName(TechniqueName);
	if (!technique) {
		Logger::Log("[ERROR] TAA : technique %s not found", TechniqueName);
		return false;
	}

	Effect->SetTechnique(technique);
	UINT passes;
	Effect->Begin(&passes, 0);
	Effect->BeginPass(0);
	TheRenderManager->device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
	Effect->EndPass();
	Effect->End();
	return true;
}

static float Halton(int index, int base) {
	float result = 0.0f;
	float fraction = 1.0f / (float)base;
	while (index > 0) {
		result += (float)(index % base) * fraction;
		index /= base;
		fraction /= (float)base;
	}
	return result;
}

bool TAAEffect::WillResolveThisFrame() {
	return Enabled && Effect != nullptr && ShouldRender() && Settings.Jitter &&
		Textures.ResolveSurface && Textures.HistorySurface &&
		TheSettingManager->SettingsMain.Main.RenderEffects &&
		Player && Player->parentCell &&
		!TheShaderManager->GameState.OverlayIsOn;
}

void TAAEffect::BeginJitter() {
	EndJitter();

	if (!WillResolveThisFrame()) return;
	NiCamera* camera = WorldSceneGraph ? WorldSceneGraph->camera : nullptr;
	if (!camera || camera->Frustum.Ortho) return;

	int index = (jitterIndex % JitterSequenceLength) + 1;
	float jitterX = Halton(index, 2) - 0.5f;
	float jitterY = Halton(index, 3) - 0.5f;

	frustumShiftX = jitterX * (camera->Frustum.Right - camera->Frustum.Left) / (float)TheRenderManager->width;
	frustumShiftY = jitterY * (camera->Frustum.Top - camera->Frustum.Bottom) / (float)TheRenderManager->height;

	camera->Frustum.Left += frustumShiftX;
	camera->Frustum.Right += frustumShiftX;
	camera->Frustum.Top += frustumShiftY;
	camera->Frustum.Bottom += frustumShiftY;

	jitterCamera = camera;
	jitterActive = true;
	TheRenderManager->SetupSceneCamera();

	if (Settings.MipBias < 0.0f) {
		SetMipBias(Settings.MipBias);
		mipBiasActive = true;
	}
}

void TAAEffect::EndJitter() {
	if (mipBiasActive) {
		SetMipBias(0.0f);
		mipBiasActive = false;
	}
	if (!jitterActive) return;

	if (jitterCamera) {
		jitterCamera->Frustum.Left -= frustumShiftX;
		jitterCamera->Frustum.Right -= frustumShiftX;
		jitterCamera->Frustum.Top -= frustumShiftY;
		jitterCamera->Frustum.Bottom -= frustumShiftY;
	}

	jitterActive = false;
	jitterCamera = nullptr;
	frustumShiftX = 0.0f;
	frustumShiftY = 0.0f;
	TheRenderManager->SetupSceneCamera();
}

void TAAEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || Effect == nullptr || !ShouldRender() || !EnsureTextures()) {
		historyValid = false;
		renderTime = 0.0f;
		return;
	}

	auto timer = TimeLogger();
	TheShaderManager->Chain.Sync();
	RenderedSurface = TheTextureManager->RenderedSurface;

	if (TheShaderManager->GameState.isCellChanged) historyValid = false;

	if (!historyValid) RememberCamera();

	D3DXVECTOR4 delta = TheRenderManager->CameraPosition - prevCameraPosition;
	Constants.CameraDelta = D3DXVECTOR4(delta.x, delta.y, delta.z, 0.0f);
	Constants.Data.z = historyValid ? 1.0f : 0.0f;

	if (SourceBuffer) Device->StretchRect(RenderTarget, NULL, SourceBuffer, NULL, D3DTEXF_NONE);
	SetCT();

	IDirect3DTexture9* const buffers[2] = { Textures.ResolveTexture, Textures.HistoryTexture };
	IDirect3DSurface9* const surfaces[2] = { Textures.ResolveSurface, Textures.HistorySurface };
	const int write = historyIndex ^ 1;
	Device->SetTexture(1, buffers[historyIndex]);
	Device->SetRenderTarget(0, surfaces[write]);
	bool resolved = DrawTechnique("Resolve");

	Device->SetRenderTarget(0, RenderTarget);
	bool debugDrawn = resolved && Settings.DebugView > 0 && DrawTechnique("Debug");

	if (resolved) Device->SetTexture(1, buffers[write]);

	if (resolved && (debugDrawn || DrawTechnique("Output"))) {
		if (RenderedSurface) Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
		RememberCamera();
		historyValid = true;
		historyIndex = write;
		jitterIndex = (jitterIndex + 1) % JitterSequenceLength;
	}
	else {
		historyValid = false;
	}

	renderTime = timer.LogTime("EffectRecord::Render TAA");
}
