// CREDIT: TAA for New Vegas Reloaded by Conor. See TAA.fx.hlsl for upstream credits.
// UNOFFICIAL: brought into this build from Balebandro's Physically-Based-Reloaded fork (Alejandro Martinez), which also
// added the Sharpness and MipBias settings (fork commit a2a2333a). Off by default ([Shaders.TAA.Status] Enabled = false).

#include <algorithm>

#include "TAA.h"
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-25 (NVR DXVK)
#include "../core/Upscaler.h"		// UNOFFICIAL 2026-10-27 (DLSS helper options)

void TAAEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_TAAData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_TAAPrevProjection", &Constants.PrevProjection);
	TheShaderManager->RegisterConstant("TESR_TAACameraDelta", &Constants.CameraDelta);
	TheShaderManager->RegisterConstant("TESR_TAAWeapon", &Constants.Weapon);
	TheShaderManager->RegisterConstant("TESR_TAASharpen", &Constants.Sharpen);
	TheShaderManager->RegisterConstant("TESR_TAAPrevViewTransform", (D3DXVECTOR4*)&Constants.PrevViewTransform);
}

// UNOFFICIAL: the two full-screen FP16 buffers (about 60 MB at 1440p) are made on first use (EnsureTextures, from Render),
// not at startup, so TAA costs no video memory while it is off.
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
	// A key absent from the toml reads as 0. Both settings would be useless at 0 -- no history at
	// all, or a box collapsed onto the neighbourhood mean -- so 0 falls back to the default rather
	// than silently turning the effect into a blur or a no-op for anyone without the defaults file.
	float weight = TheSettingManager->GetSettingF("Shaders.TAA.Main", "HistoryWeight");
	Settings.HistoryWeight = weight > 0.0f ? std::clamp(weight, 0.5f, 0.97f) : 0.9f;

	float gamma = TheSettingManager->GetSettingF("Shaders.TAA.Main", "ClipGamma");
	Settings.ClipGamma = gamma > 0.0f ? std::clamp(gamma, 0.5f, 2.5f) : 1.0f;

	// 0 is off, which is also what a missing key reads as.
	Settings.DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.TAA.Main", "DebugView"), 0, 5);

	Settings.Jitter = TheSettingManager->GetSettingI("Shaders.TAA.Main", "Jitter") != 0;

	// 0 is a real value here -- the weapon skipped entirely -- so a missing key reading as 0 is taken
	// at its word rather than replaced by the default.
	Settings.WeaponTAA = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.Main", "WeaponTAA"), 0.0f, 1.0f);

	// 0 is off for both, which is also what a missing key reads as.
	Settings.Sharpness = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.Main", "Sharpness"), 0.0f, 1.0f);
	Settings.MipBias = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.Main", "MipBias"), -1.5f, 0.0f);

	// UNOFFICIAL 2026-10-26 (NVR DXVK): FSR 2 as the resolve. All three read as 0 (off) when missing.
	// UNOFFICIAL 2026-10-27 (user: "give dlss its own tab like fsr does ... toggling one disables the other"): [Shaders.TAA.DLSS]
	// Enabled runs the same upscaler stage with DLSS; FSR and DLSS are either/or (the menu switches the other off; a file with both
	// on gets DLSS).
	const bool dlss = TheSettingManager->GetSettingI("Shaders.TAA.DLSS", "Enabled") != 0;
	FSRSettings.Enabled = dlss || TheSettingManager->GetSettingI("Shaders.TAA.FSR", "Enabled") != 0;
	FSRSettings.Sharpness = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.FSR", "Sharpness"), 0.0f, 1.0f);
	FSRSettings.WeaponReactive = std::clamp(TheSettingManager->GetSettingF("Shaders.TAA.FSR", "WeaponReactive"), 0.0f, 1.0f);
	// UNOFFICIAL 2026-10-27 RenderScale (TAAUpscale.cpp). A missing key reads as 0, which means off (1) here, not "nothing".
	const float scale = TheSettingManager->GetSettingF(dlss ? "Shaders.TAA.DLSS" : "Shaders.TAA.FSR", "RenderScale");
	// UNOFFICIAL 2026-10-27 (user: "0.1 should look pretty compressed, yet it does not"): down to 0.33, FSR 2's and DLSS's lowest
	// ("Ultra Performance", 3x per axis); below that neither is made to work. Was 0.5.
	FSRSettings.RenderScale = scale > 0.0f ? std::clamp(scale, 0.33f, 1.0f) : 1.0f;
	FSRSettings.Upscaler = dlss ? 1 : 0;
	FSRSettings.DebugView = std::clamp(TheSettingManager->GetSettingI("Shaders.TAA.FSR", "DebugView"), 0, 3);
	// UNOFFICIAL 2026-10-27 DLSS through the 64-bit helper (core/UpscalerDlss.cpp keeps them; DLSS Enabled above is real DLSS,
	// with FSR 2 whenever it cannot run). Both read as 0 (off) when missing.
	Upscaler::DlssOptions(TheSettingManager->GetSettingI("Shaders.TAA.DLSS", "HelperTest") != 0,
		std::clamp(TheSettingManager->GetSettingI("Shaders.TAA.DLSS", "JitterFlip"), 0, 3));
}

// Texture mip LOD bias on every sampler while the jittered world draws. Averaging jittered frames
// already filters textures, so the usual mip level comes out softer than without TAA; a negative bias
// picks a sharper mip and TAA settles the extra shimmer that would otherwise bring. Set straight on
// the device, around the game's render state cache; should the game set this state itself for some
// texture, that sampler just goes back to the game's value. EndJitter resets all of them to 0.
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

// Record the camera that rendered the frame now in the history buffer, for next frame's
// reprojection. Called after the resolve, never before it: at that point TheRenderManager holds
// the camera the frame was drawn with, because RenderEffects rebuilds it right before this effect.
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

// --- The Vulkan passes (NVR DXVK) -----------------------------------------------------------------------------------------
// UNOFFICIAL 2026-10-25 (NVR DXVK): the resolve, the debug view and the output as Compute\TAAResolve, TAADebug and TAAOutput
// (.frag; TAA.glsl holds what they share), line for line. Same targets, same order and same inputs as the D3D9 draws in Render.
// The constants go to a uniform buffer (304 bytes); the matrices are copied as the effect's SetMatrix uploads them.
namespace {
	// The Constants block of TAA.glsl (std140; the matrices as the effect's SetMatrix uploads them).
	struct TAAUniforms {
		D3DXMATRIX	projection, view, prevView;
		D3DXVECTOR4	reciprocalResolution, cameraData, data, prevProjection, cameraDelta, weapon, sharpen;
	};
	struct TAAPush { UInt32 pointIndex, linearIndex; };	// the extension writes the sampler heap indices here

	// Set 1: the textures in the .fx's sampler order (binding = slot); set 2: the constants; the samplers' heap indices at 0 and 4.
	const NVR_EXT_BINDING_DESC TAABindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_SourceBuffer
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE, 1, NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_TAAHistoryBuffer
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE, 2, NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_DepthBuffer
		{ 1, 3, NVR_EXT_BIND_SAMPLED_IMAGE, 3, NVR_EXT_VIEW_2D,   FALSE, 0 },	// TESR_DepthBufferViewModel
		{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D,  FALSE, 0 },	// Constants
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D,  FALSE, 0 },	// POINT, CLAMP
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        1, NVR_EXT_VIEW_2D,  FALSE, 4 },	// LINEAR, CLAMP
	};
	const UINT TAATextureCount = 4;

	IDirect3DBaseTexture9* SlotTexture(const char* name) {
		std::string slotName = name;
		IDirect3DBaseTexture9** slot = TheTextureManager->GetTextureSlotByName(slotName);
		return slot ? *slot : nullptr;
	}
	// The constants from the same registered memory the effect's SetCT uploads.
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

	// One pass: the textures as the .fx binds them (a placeholder where the pass does not read one), into one target.
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

	// The same reads as the D3D9 draws: the resolve reads last frame's result (historyIndex) and writes the other buffer; the debug
	// view reads what the resolve read; the output reads this frame's resolve (the buffer that becomes next frame's history).
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

// Halton low-discrepancy sequence: the radical inverse of index in the given base. Bases 2 and 3
// give well-spread 2D points, so any run of consecutive frames covers the pixel evenly rather than
// clumping, which is why it is the standard TAA jitter pattern.
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

// The same gates RenderEffects applies before running any effect, plus TAA's own. Jitter without a
// resolve to average it is just the whole world shaking by half a pixel, so it must only be applied
// on frames where this effect is going to run.
bool TAAEffect::WillResolveThisFrame() {
	return Enabled && Effect != nullptr && ShouldRender() && Settings.Jitter &&
		Textures.ResolveSurface && Textures.HistorySurface &&
		TheSettingManager->SettingsMain.Main.RenderEffects &&
		Player && Player->parentCell &&
		!TheShaderManager->GameState.OverlayIsOn;
}

// Jitter the world render by shifting the camera frustum a fraction of a pixel.
//
// Gamebryo builds the projection from the camera's frustum, so an off-centre frustum IS a jittered
// projection -- the same _31/_32 offsets NVR's own SetupSceneCamera derives from Left+Right and
// Top+Bottom. Shifting the frustum rather than patching a matrix means every path agrees: whether
// the game rebuilds its projection from the frustum when it sets the camera, or keeps the matrix
// NVR last wrote, it gets the jittered one, because SetupSceneCamera is re-run right here.
//
// Applied around the world scene only. Shadow maps are rendered before it -- their cascades are
// fitted to the view frustum, and at the far cascade half a pixel is several world units, about a
// shadow texel, so jittering them would make shadow edges shimmer (RenderShadowMapHook logs if
// that ordering is ever violated). The first-person weapon has its own camera and stays unjittered,
// which matches TAA giving it zero motion. Everything after End -- image space, every NVR effect,
// and TAA's own reprojection -- sees the unjittered camera, exactly as without jitter.
void TAAEffect::BeginJitter(bool firstWorld) {
	EndJitter(); // never stack two shifts, whatever happened last frame
	frameJittered = false;

	// UNOFFICIAL 2026-10-27 RenderScale (TAAUpscale.cpp): the world scene at the lower internal resolution. The jitter is then FSR
	// 2's own (Halton, ffxFsr2GetJitterPhaseCount steps, in render pixels) and the mip bias adds log2 of the scale (AMD's advice:
	// textures at the screen's sharpness, not the render size's). Off: the code below, exactly as before.
	if (UpscaleBegin(firstWorld)) {
		NiCamera* camera = Up.Camera;
		Up.Jittered = false;
		if (Settings.Jitter && camera) {
			const int phases = (std::max)(1, (int)Up.JitterPhases);
			const int index = (Up.JitterIndex % phases) + 1;
			const float jitterX = Halton(index, 2) - 0.5f;
			const float jitterY = Halton(index, 3) - 0.5f;
			// The frustum spans the render viewport: one render pixel is (Right - Left) / RenderWidth.
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

	// Pixel offset in (-0.5, 0.5), from Halton(2,3) indices 1..8.
	int index = (jitterIndex % JitterSequenceLength) + 1;
	float jitterX = Halton(index, 2) - 0.5f;
	float jitterY = Halton(index, 3) - 0.5f;

	// Frustum edges are in view-plane units at distance 1, so one pixel is (Right - Left) / width.
	frustumShiftX = jitterX * (camera->Frustum.Right - camera->Frustum.Left) / (float)TheRenderManager->width;
	frustumShiftY = jitterY * (camera->Frustum.Top - camera->Frustum.Bottom) / (float)TheRenderManager->height;

	camera->Frustum.Left += frustumShiftX;
	camera->Frustum.Right += frustumShiftX;
	camera->Frustum.Top += frustumShiftY;
	camera->Frustum.Bottom += frustumShiftY;

	jitterCamera = camera;
	jitterActive = true;
	frameJitterX = jitterX;	// UNOFFICIAL 2026-10-26: FSR 2 needs this frame's offset (TAAFSR.cpp)
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
	UpscaleRestorePort();	// UNOFFICIAL 2026-10-27 RenderScale: the world camera's viewport port back to the whole screen
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
		// Whatever is in the history now will be stale, or somewhere else entirely, by the time
		// this runs again.
		historyValid = false;
		renderTime = 0.0f;
		return;
	}

	// UNOFFICIAL 2026-10-27 RenderScale (TAAUpscale.cpp): this frame's world was drawn at the lower internal resolution and
	// already upscaled (and anti-aliased) by FSR 2 before NVR's effects; there is nothing left to resolve here. TAA's own history
	// (and FSR's at native resolution) starts again whenever a frame is not upscaled.
	if (Up.Upscaled) {
		Up.Upscaled = false;
		historyValid = false;
		lastPathFSR = false;
		frameJittered = false;
		renderTime = 0.0f;
		return;
	}

	auto timer = TimeLogger();
	// UNOFFICIAL (as in the pr/nvr-optimizations merge of #78): this path renders into the game target and copies it into
	// RenderedSurface itself. With the copy-free frame chain the current image may sit in a chain texture instead, so hand
	// it to the game target first and use the current RenderedSurface, which the sync can move.
	TheShaderManager->Chain.Sync();
	RenderedSurface = TheTextureManager->RenderedSurface;

	// UNOFFICIAL 2026-10-26 (NVR DXVK): FSR 2 as the resolve when [Shaders.TAA.FSR] Enabled and the extension is there. Each path
	// keeps its own history, so switching between them starts from scratch.
	const bool useFSR = FSRWanted();
	if (useFSR != lastPathFSR) historyValid = false;

	// A new cell means a loading screen or a teleport: last frame's history shows another place.
	if (TheShaderManager->GameState.isCellChanged) historyValid = false;

	// With no usable history, make last frame's camera this frame's. The reprojection becomes the
	// identity, and the shader ignores the history anyway because Data.z is 0.
	if (!historyValid) RememberCamera();

	D3DXVECTOR4 delta = TheRenderManager->CameraPosition - prevCameraPosition;
	Constants.CameraDelta = D3DXVECTOR4(delta.x, delta.y, delta.z, 0.0f);
	Constants.Data.z = historyValid ? 1.0f : 0.0f;

	if (SourceBuffer) Device->StretchRect(RenderTarget, NULL, SourceBuffer, NULL, D3DTEXF_NONE);

	// UNOFFICIAL 2026-10-13: the two FP16 buffers take turns. Last frame's result (historyIndex) is read as the history
	// (TESR_TAAHistoryBuffer) and this frame's resolve goes into the other one, which Output then shows and which is next
	// frame's history: no full-screen copy of the resolve into a fixed history buffer.
	const int write = historyIndex ^ 1;
	// UNOFFICIAL 2026-10-25 (NVR DXVK): the passes as Vulkan passes when NvrDxvkPasses is on (ExtDraw); the D3D9 techniques otherwise,
	// and when a pass fails (the extension path is then off for the session).
	bool fsrShown = useFSR && FSRDraw(Device, RenderTarget, !historyValid);
	if (useFSR && !fsrShown) {
		// FSR failed this frame (logged; it stays off): TAA's own resolve, from scratch.
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

		// A debug view goes to the screen reading the same last frame the resolve read (stage 1 still holds it), so the view
		// sees exactly what the resolve saw, and the debug picture never becomes next frame's input.
		Device->SetRenderTarget(0, RenderTarget);
		bool debugDrawn = resolved && Settings.DebugView > 0 && DrawTechnique("Debug");

		if (resolved) Device->SetTexture(1, buffers[write]);	// Output shows this frame's resolve

		shown = resolved && (debugDrawn || DrawTechnique("Output"));
	}

	if (shown) {
		if (RenderedSurface) Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
		RememberCamera();
		historyValid = true;
		if (!fsrShown) historyIndex = write;	// FSR keeps its own ping-pong resources
		jitterIndex = (jitterIndex + 1) % JitterSequenceLength;
	}
	else {
		historyValid = false;
	}
	lastPathFSR = fsrShown;
	frameJittered = false;

	renderTime = timer.LogTime("EffectRecord::Render TAA");
}
