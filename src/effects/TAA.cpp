
#include <algorithm>

#include "TAA.h"
#include "../core/DxvkCompute.h"
#include "../core/Upscaler.h"

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

	const bool dlss = TheSettingManager->GetSettingI("Shaders.TAA.DLSS", "Enabled") != 0;
	FSRSettings.Enabled = dlss || TheSettingManager->GetSettingI("Shaders.TAA.FSR", "Enabled") != 0;
	FSRSettings.Sharpness = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.FSR", "Sharpness"), 0.0f, 1.0f);
	FSRSettings.WeaponReactive = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.FSR", "WeaponReactive"), 0.0f, 1.0f);
	const float scale = TheSettingManager->GetSettingF(dlss ? "Shaders.TAA.DLSS" : "Shaders.TAA.FSR", "RenderScale");
	FSRSettings.RenderScale = scale > 0.0f ? std::clamp(scale, 0.33f, 1.0f) : 1.0f;
	FSRSettings.Upscaler = dlss ? 1 : 0;
	FSRSettings.DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.TAA.FSR", "DebugView"), 0, 3);
	FSRSettings.LowResEffects = TheSettingManager->GetSettingI("Shaders.TAA.FSR", "LowResEffects") != 0;
	Upscaler::DlssOptions(TheSettingManager->GetSettingI("Shaders.TAA.DLSS", "HelperTest") != 0,
		std::clamp(TheSettingManager->GetSettingI("Shaders.TAA.DLSS", "JitterFlip"), 0, 3));
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

namespace {
	struct TAAUniforms {
		D3DXMATRIX	projection, view, prevView;
		D3DXVECTOR4	reciprocalResolution, cameraData, data, prevProjection, cameraDelta, weapon, sharpen;
	};
	struct TAAPush { UInt32 pointIndex, linearIndex; };

	const NVR_EXT_BINDING_DESC TAABindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE, 1, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE, 2, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 1, 3, NVR_EXT_BIND_SAMPLED_IMAGE, 3, NVR_EXT_VIEW_2D,   FALSE, 0 },
		{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D,  FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D,  FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        1, NVR_EXT_VIEW_2D,  FALSE, 4 },
	};
	const UINT TAATextureCount = 4;

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

bool TAAEffect::ExtDraw(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, int write) {
	const D3DXVECTOR4* projection = Registered("TESR_ProjectionTransform");
	const D3DXVECTOR4* view = Registered("TESR_ViewTransform");
	const D3DXVECTOR4* reciprocal = Registered("TESR_ReciprocalResolution");
	const D3DXVECTOR4* cameraData = Registered("TESR_CameraData");
	if (!projection || !view || !reciprocal || !cameraData) return false;

	IDirect3DBaseTexture9* source = TheTextureManager->SourceTexture;
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* viewModel = SlotTexture("TESR_DepthBufferViewModel");
	if (!source || !depth || !viewModel) return false;

	TAAUniforms uniforms = {};
	memcpy(&uniforms.projection, projection, sizeof(D3DXMATRIX));
	memcpy(&uniforms.view, view, sizeof(D3DXMATRIX));
	uniforms.prevView = Constants.PrevViewTransform;
	uniforms.reciprocalResolution = *reciprocal;
	uniforms.cameraData = *cameraData;
	uniforms.data = Constants.Data;
	uniforms.prevProjection = Constants.PrevProjection;
	uniforms.cameraDelta = Constants.CameraDelta;
	uniforms.weapon = Constants.Weapon;
	uniforms.sharpen = Constants.Sharpen;
	ID3D9NvrExtBuffer* buffer = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	if (!buffer) return false;

	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	if (!point || !linear) return false;

	static const char* const spvNames[3] = { "TAAResolve", "TAADebug", "TAAOutput" };
	auto draw = [&](int index, IDirect3DBaseTexture9* const* textures, IDirect3DSurface9* const* targets) -> bool {
		ExtPass& pass = extPasses[index];
		if (!pass.Tried) {
			pass.Tried = true;
			pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvNames[index], TAABindings, _countof(TAABindings), sizeof(TAAPush), true);
		}
		if (!pass.Pipeline) return false;
		NVR_EXT_RESOURCE_BINDING resources[_countof(TAABindings)] = {};
		for (UINT i = 0; i < TAATextureCount; i++) {
			resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
		}
		resources[4].Slot = 0; resources[4].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[4].pResource = buffer;
		resources[5].Slot = 0; resources[5].Type = NVR_EXT_BIND_SAMPLER; resources[5].pResource = point;
		resources[6].Slot = 1; resources[6].Type = NVR_EXT_BIND_SAMPLER; resources[6].pResource = linear;
		TAAPush push = {};
		return ExtDrawTo(pass.Pipeline, targets, 1, resources, _countof(resources), &push, sizeof(push));
	};

	IDirect3DBaseTexture9* history[2] = { Textures.ResolveTexture, Textures.HistoryTexture };
	IDirect3DSurface9* surfaces[2] = { Textures.ResolveSurface, Textures.HistorySurface };
	IDirect3DBaseTexture9* resolveInputs[TAATextureCount] = { source, history[historyIndex], depth, viewModel };
	IDirect3DSurface9* resolveTarget = surfaces[write];
	if (!draw(0, resolveInputs, &resolveTarget)) return false;

	IDirect3DBaseTexture9* outputInputs[TAATextureCount] = { source, history[write], depth, viewModel };
	const bool shown = Settings.DebugView > 0 ? draw(1, resolveInputs, &RenderTarget) : draw(2, outputInputs, &RenderTarget);
	if (shown && ++ExtFrames == 1) Logger::Log("UNOFFICIAL NVR DXVK: %s runs as a Vulkan pass.", Name);
	return shown;
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

void TAAEffect::BeginJitter(bool firstWorld) {
	EndJitter();
	frameJittered = false;

	if (UpscaleBegin(firstWorld)) {
		NiCamera* camera = Up.Camera;
		Up.Jittered = false;
		if (Settings.Jitter && camera) {
			const int phases = (std::max)(1, (int)Up.JitterPhases);
			const int index = (Up.JitterIndex % phases) + 1;
			const float jitterX = Halton(index, 2) - 0.5f;
			const float jitterY = Halton(index, 3) - 0.5f;
			frustumShiftX = jitterX * (camera->Frustum.Right - camera->Frustum.Left) / (float)Up.RenderWidth;
			frustumShiftY = jitterY * (camera->Frustum.Top - camera->Frustum.Bottom) / (float)Up.RenderHeight;
			camera->Frustum.Left += frustumShiftX;
			camera->Frustum.Right += frustumShiftX;
			camera->Frustum.Top += frustumShiftY;
			camera->Frustum.Bottom += frustumShiftY;
			jitterCamera = camera;
			jitterActive = true;
			Up.JitterX = jitterX;
			Up.JitterY = jitterY;
			Up.Jittered = true;
			TheRenderManager->SetupSceneCamera();
		}
		const float scale = (float)Up.RenderWidth / (float)(std::max)(Up.Width, 1u);
		const float bias = (std::max)(-3.0f, log2f(scale) + (Settings.Jitter ? Settings.MipBias : 0.0f));
		if (bias < 0.0f) {
			SetMipBias(bias);
			mipBiasActive = true;
		}
		return;
	}

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
	frameJitterX = jitterX;
	frameJitterY = jitterY;
	frameJittered = true;
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
	UpscaleRestorePort();
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

	if (Up.Upscaled) {
		Up.Upscaled = false;
		historyValid = false;
		lastPathFSR = false;
		frameJittered = false;
		renderTime = 0.0f;
		return;
	}

	auto timer = TimeLogger();
	TheShaderManager->Chain.Sync();
	RenderedSurface = TheTextureManager->RenderedSurface;

	const bool useFSR = FSRWanted();
	if (useFSR != lastPathFSR) historyValid = false;

	if (TheShaderManager->GameState.isCameraCut) historyValid = false;

	if (!historyValid) RememberCamera();

	D3DXVECTOR4 delta = TheRenderManager->CameraPosition - prevCameraPosition;
	Constants.CameraDelta = D3DXVECTOR4(delta.x, delta.y, delta.z, 0.0f);
	Constants.Data.z = historyValid ? 1.0f : 0.0f;

	if (SourceBuffer) Device->StretchRect(RenderTarget, NULL, SourceBuffer, NULL, D3DTEXF_NONE);

	const int write = historyIndex ^ 1;
	bool fsrShown = useFSR && FSRDraw(Device, RenderTarget, !historyValid);
	if (useFSR && !fsrShown) {
		historyValid = false;
		RememberCamera();
		Constants.CameraDelta = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
		Constants.Data.z = 0.0f;
	}
	bool shown = fsrShown || (!ExtFailed && DxvkCompute::PassesOn() && ExtDraw(Device, RenderTarget, write));
	if (shown) {
		Device->SetRenderTarget(0, RenderTarget);
	}
	else {
		SetCT();
		IDirect3DTexture9* const buffers[2] = { Textures.ResolveTexture, Textures.HistoryTexture };
		IDirect3DSurface9* const surfaces[2] = { Textures.ResolveSurface, Textures.HistorySurface };
		Device->SetTexture(1, buffers[historyIndex]);
		Device->SetRenderTarget(0, surfaces[write]);
		bool resolved = DrawTechnique("Resolve");

		Device->SetRenderTarget(0, RenderTarget);
		bool debugDrawn = resolved && Settings.DebugView > 0 && DrawTechnique("Debug");

		if (resolved) Device->SetTexture(1, buffers[write]);

		shown = resolved && (debugDrawn || DrawTechnique("Output"));
	}

	if (shown) {
		if (RenderedSurface) Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
		RememberCamera();
		historyValid = true;
		if (!fsrShown) historyIndex = write;
		jitterIndex = (jitterIndex + 1) % JitterSequenceLength;
	}
	else {
		historyValid = false;
	}
	lastPathFSR = fsrShown;
	frameJittered = false;

	renderTime = timer.LogTime("EffectRecord::Render TAA");
}
