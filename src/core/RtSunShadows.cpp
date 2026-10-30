// UNOFFICIAL 2026-10-28 (NVR DXVK) RT sun shadows: RtSunShadows.h; the trace shader is Compute\Rt\RtSunShadow.comp (run by the helper),
// the combine pass Compute\RtSunCombine.frag (a fork pass in the game).

#include "RtSunShadows.h"
#include "RtCore.h"
#include "RtDenoise.h"
#include "DxvkCompute.h"
#include "GpuProfiler.h"

namespace {
	const char* const kSection = "Shaders.ShadowsExteriors.RT";

	struct Settings {
		bool	enabled = false;
		int		mode = 0;				// 0 combine, 1 replace
		float	strength = 1.0f;
		float	range = 6000.0f;
		float	normalBias = 2.0f;
		float	normalBiasPerUnit = 0.0005f;
		float	startBias = 1.0f;
		float	rayLength = 30000.0f;
		float	sunAngle = 0.0f;		// degrees across the sun's disc: 0 = one hard ray, > 0 = rays jittered inside it + the denoiser
		bool	denoise = true;
		bool	halfResolution = false;
		int		denoiseKernel = 5;			// the shared denoiser's a-trous kernel: 5 (5 x 5) or 3 (3 x 3, cheaper)		// trace a quarter of the pixels (2 x 2 blocks), denoise and upsample
		int		debugView = 0;			// 1 RT only, 2 traced not applied, 3 hit distance, 4 the raw visibility over the picture, 5 state codes, 6 primary rays (4-6: grey over the picture)
	} S;

	// The trace pass's uniform block (RtSunShadow.comp, std140)
	struct PassConstants {
		float size[4], forward[4], right[4], up[4], origin[4], sun[4], bias[4], range[4], alpha[4], full[4];
	};

	int			imageDepth = -1, imageNormals = -1, imageVisibility = -1, shader = -1, hitDebugShader = -1;
	IDirect3DTexture9*	visibilityTexture = nullptr;
	IDirect3DTexture9*	denoisedTexture = nullptr;		// SunAngle > 0: the denoised visibility (R16F)
	IDirect3DSurface9*	denoisedSurface = nullptr;
	IDirect3DTexture9*	signalTexture = nullptr;		// what Apply / RenderDebug use this frame (the denoised one when it ran)
	RtDenoise::Denoiser* denoiser = nullptr;
	IDirect3DSurface9*	visibilitySurface = nullptr;
	UINT		textureWidth = 0, textureHeight = 0;
	bool		tracedThisFrame = false;
	UInt32		frames = 0;
	D3DXVECTOR4	replaceConstant = { 0.0f, 0.0f, 0.0f, 0.0f };
	ID3D9NvrExtPipeline* combine[3] = {};	// [0] multiply, [1] write red (DebugView 1 / 3), [2] write all (DebugView 4, grey)
	bool		combineTried[3] = {};
	bool		tracedForDebug = false;		// this frame's visibility is there for RenderDebug

	void ReadSettings() {
		S.enabled = TheSettingManager->GetSettingI(kSection, "Enabled") != 0;
		S.mode = TheSettingManager->GetSettingI(kSection, "Mode");
		S.strength = TheSettingManager->GetSettingF(kSection, "Strength");
		S.range = (std::max)(500.0f, TheSettingManager->GetSettingF(kSection, "Range"));
		S.normalBias = TheSettingManager->GetSettingF(kSection, "NormalBias");
		S.normalBiasPerUnit = TheSettingManager->GetSettingF(kSection, "NormalBiasPerUnit");
		S.startBias = TheSettingManager->GetSettingF(kSection, "StartBias");
		S.rayLength = (std::max)(100.0f, TheSettingManager->GetSettingF(kSection, "RayLength"));
		S.debugView = TheSettingManager->GetSettingI(kSection, "DebugView");
		S.sunAngle = (std::min)((std::max)(TheSettingManager->GetSettingF(kSection, "SunAngle"), 0.0f), 10.0f);
		S.denoise = TheSettingManager->GetSettingI(kSection, "Denoise") != 0;
		S.halfResolution = TheSettingManager->GetSettingI(kSection, "HalfResolution") != 0;
		S.denoiseKernel = TheSettingManager->GetSettingI(kSection, "DenoiseKernel") == 3 ? 3 : 5;
	}

	const D3DXVECTOR4* Constant(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
	IDirect3DBaseTexture9* Texture(const char* name) {
		auto found = TheTextureManager->TextureNames.find(name);
		return found == TheTextureManager->TextureNames.end() || !found->second ? nullptr : *found->second;
	}

	bool Wants() {
		ReadSettings();
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		return S.enabled && TheShaderManager->GameState.isExterior && shadows && shadows->Enabled && shadows->Settings.Exteriors.Enabled;
	}

	void Setup() {
		const UINT w = TheRenderManager->width, h = TheRenderManager->height;
		imageDepth = RtCore::AddImage("NVR RT sun depth", w, h, NVR_EXT_SHARED_R32F);
		imageNormals = RtCore::AddImage("NVR RT sun normals", w, h, NVR_EXT_SHARED_RGBA16F);
		imageVisibility = RtCore::AddImage("NVR RT sun visibility", w, h, NVR_EXT_SHARED_R16F);
	}

	void Record(IDirect3DDevice9* Device) {
		tracedThisFrame = false;
		if (imageDepth < 0 || imageNormals < 0 || imageVisibility < 0 || shader < 0) return;
		const UINT fw = TheRenderManager->width, fh = TheRenderManager->height;
		const UINT scale = S.halfResolution ? 2u : 1u;
		const UINT w = (std::max)(1u, fw / scale), h = (std::max)(1u, fh / scale);	// as RtDenoise's Downscale	// the traced size (the images stay full size: the top-left part)
		if (!visibilityTexture || textureWidth != w || textureHeight != h) {
			if (visibilitySurface) { visibilitySurface->Release(); visibilitySurface = nullptr; }
			if (visibilityTexture) { visibilityTexture->Release(); visibilityTexture = nullptr; }
			TheTextureManager->InitTexture("TESR_RtSunShadow", &visibilityTexture, &visibilitySurface, w, h, D3DFMT_R16F);
			textureWidth = w;
			textureHeight = h;
			if (!visibilityTexture) return;
		}
		IDirect3DBaseTexture9* depth = Texture("TESR_DepthBuffer");
		IDirect3DBaseTexture9* normals = Texture("TESR_NormalsBuffer");
		const D3DXVECTOR4* cameraData = Constant("TESR_CameraData");
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		if (!depth || !normals || !cameraData || !shadows) return;

		const D3DXMATRIX& view = TheRenderManager->viewMatrix;
		const D3DXMATRIX& proj = TheRenderManager->projMatrix;
		PassConstants c = {};
		c.size[0] = (float)w; c.size[1] = (float)h; c.size[2] = 1.0f / w; c.size[3] = 1.0f / h;
		c.full[0] = (float)fw; c.full[1] = (float)fh;	// depth / normals are read at full size
		// SunShadows.glsl toWorld(): the view matrix's columns are right (0), up (1) and forward (2)
		c.forward[0] = view._13; c.forward[1] = view._23; c.forward[2] = view._33; c.forward[3] = cameraData->y;
		c.right[0] = view._11 / proj._11; c.right[1] = view._21 / proj._11; c.right[2] = view._31 / proj._11;
		c.up[0] = view._12 / proj._22; c.up[1] = view._22 / proj._22; c.up[2] = view._32 / proj._22;
		const D3DXVECTOR3 eye = RtCore::Eye();	// the scene is relative to the eye (not Pointers::Generic::CameraLocation)
		c.origin[0] = eye.x; c.origin[1] = eye.y; c.origin[2] = eye.z;
		const D3DXVECTOR4& sun = shadows->Constants.SmoothedSunDir;	// toward the sun, as the cascades use it
		c.sun[0] = sun.x; c.sun[1] = sun.y; c.sun[2] = sun.z;
		c.sun[3] = shadows->Constants.ShadowFade.x < 1.0f ? 1.0f : 0.0f;
		c.bias[0] = S.normalBias; c.bias[1] = S.normalBiasPerUnit; c.bias[2] = S.startBias; c.bias[3] = S.rayLength;
		c.alpha[0] = RtCore::AlphaTested() ? 1.0f : 0.0f;
		c.alpha[1] = (float)(frames & 1023);								// the jitter's frame
		c.alpha[2] = tanf(0.5f * S.sunAngle * 0.0174532925f);				// the sun cone's half-angle tangent (0: hard)
		c.range[0] = S.range; c.range[1] = 0.15f * S.range; c.range[2] = 255.0f; c.range[3] = S.debugView == 3 ? 1.0f : S.debugView == 5 ? 2.0f : S.debugView == 6 ? 3.0f : 0.0f;

		if (!RtCore::CopyIn(imageDepth, depth) || !RtCore::CopyIn(imageNormals, normals)) return;
		const int atlas = RtCore::AlphaAtlasImage();
		const int albedoAtlas = RtCore::AlbedoAtlasImage();
		const int glowAtlas = RtCore::GlowAtlasImage();
		if ((S.debugView == 7 || (S.debugView == 8 && glowAtlas >= 0)) && albedoAtlas >= 0 && atlas >= 0 && hitDebugShader >= 0) {	// the hit textures check (RtHitDebug.comp)
			if (S.debugView == 8) c.range[3] = 4.0f;	// 2026-10-30: the glow atlas (GlowTextures)
			const int images[4] = { imageVisibility, atlas, albedoAtlas, glowAtlas >= 0 ? glowAtlas : albedoAtlas };
			if (!RtCore::AddPass(hitDebugShader, images, 4, 0x1, &c, sizeof(c), (w + 7) / 8, (h + 7) / 8)) return;
		}
		else {
			const int images[4] = { imageDepth, imageNormals, imageVisibility, c.alpha[0] > 0.0f && atlas >= 0 ? atlas : imageVisibility };	// [3]: binding 5
			if (!RtCore::AddPass(shader, images, 4, 0x4, &c, sizeof(c), (w + 7) / 8, (h + 7) / 8)) return;
		}
		tracedThisFrame = RtCore::CopyOut(imageVisibility, visibilitySurface, w, h);
		if (tracedThisFrame && frames++ == 0)
			Logger::Log("UNOFFICIAL NVR DXVK: RT sun shadows traced (%ux%u, Mode %d: %s, range %.0f, DebugView %d).", w, h, S.mode,
				S.mode == 1 ? "replace the cascades inside the range" : "combine with the cascades", S.range, S.debugView);
		if (tracedThisFrame && frames == 31)
			Logger::Log("UNOFFICIAL NVR DXVK: RT sun constants: forward %.3f %.3f %.3f (farZ %.0f), right %.3f %.3f %.3f, up %.3f %.3f %.3f, origin %.2f %.2f %.2f, "
				"sun %.3f %.3f %.3f (up %.0f), camera position %.0f %.0f %.0f", c.forward[0], c.forward[1], c.forward[2], c.forward[3], c.right[0], c.right[1],
				c.right[2], c.up[0], c.up[1], c.up[2], c.origin[0], c.origin[1], c.origin[2], c.sun[0], c.sun[1], c.sun[2], c.sun[3],
				TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
	}

	ID3D9NvrExtPipeline* CombinePipeline(int which) {
		if (combineTried[which]) return combine[which];
		combineTried[which] = true;
		std::vector<char> spirv;
		ID3D9NvrExt* ext = DxvkCompute::Extension();
		if (!ext || !DxvkCompute::LoadSpirv("RtSunCombine", spirv)) return nullptr;
		NVR_EXT_BINDING_DESC bindings[2] = {
			{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
			{ 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, 4 },
		};
		NVR_EXT_GRAPHICS_PIPELINE_DESC desc = {};
		desc.pFragmentSpirv = spirv.data();
		desc.FragmentSpirvSize = (UINT)spirv.size();
		desc.BindingCount = 2;
		desc.pBindings = bindings;
		desc.PushConstantSize = 16;
		desc.UsesSamplerHeap = TRUE;
		desc.SamplerHeapSet = 0;
		desc.SamplerHeapBinding = 0;
		desc.ColorTargetCount = 1;
		desc.Blend = which == 0 ? NVR_EXT_BLEND_MULTIPLY : NVR_EXT_BLEND_NONE;
		desc.ColorWriteMask = which == 2 ? 0u : 0x1u;	// red only: the sun's shadow; green (the lamps) untouched. DebugView 4: all
		desc.pDebugName = which == 0 ? "RtSunCombine (multiply)" : which == 1 ? "RtSunCombine (write)" : "RtSunCombine (debug grey)";
		if (FAILED(ext->CreateGraphicsPipeline(&desc, &combine[which]))) {
			combine[which] = nullptr;
			Logger::Log("UNOFFICIAL NVR DXVK: RT sun shadows: the combine pass could not be made (the DXVK log says why)");
		}
		return combine[which];
	}
}

namespace RtSunShadows {
	void Register() {
		shader = RtCore::AddShader("RtSunShadow");
		hitDebugShader = RtCore::AddShader("RtHitDebug");
		RtCore::AddUser({ "RT sun shadows", Wants, Setup, Record });
	}

	void Apply(IDirect3DDevice9* Device) {
		const bool traced = tracedThisFrame;
		tracedThisFrame = false;
		tracedForDebug = traced;
		signalTexture = visibilityTexture;
		// Soft shadows: the jittered one-ray visibility through the shared denoiser (RtDenoise.h)
		if (traced && visibilityTexture && S.sunAngle > 0.0f && S.denoise) {
			const UINT w = textureWidth, h = textureHeight;	// the traced size (half with HalfResolution)
			static UINT denoisedWidth = 0, denoisedHeight = 0;
			if (!denoisedTexture || denoisedWidth != w || denoisedHeight != h) {
				denoisedWidth = w; denoisedHeight = h;
				if (denoisedSurface) { denoisedSurface->Release(); denoisedSurface = nullptr; }
				if (denoisedTexture) { denoisedTexture->Release(); denoisedTexture = nullptr; }
				Device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F, D3DPOOL_DEFAULT, &denoisedTexture, nullptr);
				if (denoisedTexture) denoisedTexture->GetSurfaceLevel(0, &denoisedSurface);
			}
			if (!denoiser) denoiser = RtDenoise::Create("RT sun shadows");
			RtDenoise::Settings settings;
			settings.Downscale = S.halfResolution ? 2 : 1;
			settings.Kernel = S.denoiseKernel;
			if (denoisedSurface && RtDenoise::Run(denoiser, Device, visibilityTexture, denoisedSurface, settings)) signalTexture = denoisedTexture;
		}
		if (!traced || S.debugView == 2 || S.debugView >= 4 || !signalTexture) return;
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		IDirect3DSurface9* target = shadows->Textures.ShadowPassSurface;
		ID3D9NvrExtPipeline* pipeline = CombinePipeline(S.debugView == 1 || S.debugView == 3 ? 1 : 0);
		ID3D9NvrExtSampler* point = S.halfResolution ? DxvkCompute::LinearClamp() : DxvkCompute::PointClamp();	// half: bilinear up
		if (!pipeline || !point || !target) return;

		static GpuTimer timer("  RT sun shadows combine");
		GpuProfileScope gpu(timer, Device);
		// A pass may not draw into the device's current render target: park the device on another surface meanwhile
		IDirect3DSurface9* current = nullptr;
		Device->GetRenderTarget(0, &current);
		SunShadowsEffect* sunShadows = TheShaderManager->Effects.SunShadows;
		const bool park = current == target && sunShadows && sunShadows->scratchSurface[0];
		if (park) Device->SetRenderTarget(0, sunShadows->scratchSurface[0]);

		struct { float strength; UInt32 samplerIndex, grey, pad; } push = { S.debugView ? 1.0f : (std::min)((std::max)(S.strength, 0.0f), 1.0f), 0, 0, 0 };
		NVR_EXT_RESOURCE_BINDING bindings[2] = {};
		bindings[0].Slot = 0; bindings[0].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[0].pResource = signalTexture;
		bindings[1].Slot = 0; bindings[1].Type = NVR_EXT_BIND_SAMPLER; bindings[1].pResource = point;
		NVR_EXT_DRAW_DESC draw = {};
		draw.pPipeline = pipeline;
		draw.TargetCount = 1;
		draw.Targets[0].pResource = target;
		draw.BindingCount = 2;
		draw.pBindings = bindings;
		draw.pPushData = &push;
		draw.PushDataSize = sizeof(push);
		DxvkCompute::Extension()->Draw(&draw);

		if (park) Device->SetRenderTarget(0, current);
		if (current) current->Release();
	}

	void RenderDebug(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget) {
		const bool traced = tracedForDebug;
		tracedForDebug = false;
		if (!traced || S.debugView < 4 || !signalTexture || !RenderTarget) return;
		ID3D9NvrExtPipeline* pipeline = CombinePipeline(2);
		ID3D9NvrExtSampler* point = S.halfResolution ? DxvkCompute::LinearClamp() : DxvkCompute::PointClamp();	// half: bilinear up
		SunShadowsEffect* sunShadows = TheShaderManager->Effects.SunShadows;
		if (!pipeline || !point || !sunShadows || !sunShadows->scratchSurface[0]) return;
		IDirect3DSurface9* current = nullptr;
		Device->GetRenderTarget(0, &current);
		Device->SetRenderTarget(0, sunShadows->scratchSurface[0]);	// a pass may not draw into the device's current target
		struct { float strength; UInt32 samplerIndex, grey, pad; } push = { 1.0f, 0, 1, 0 };
		NVR_EXT_RESOURCE_BINDING bindings[2] = {};
		bindings[0].Slot = 0; bindings[0].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[0].pResource = S.debugView == 4 ? signalTexture : visibilityTexture;
		bindings[1].Slot = 0; bindings[1].Type = NVR_EXT_BIND_SAMPLER; bindings[1].pResource = point;
		NVR_EXT_DRAW_DESC draw = {};
		draw.pPipeline = pipeline;
		draw.TargetCount = 1;
		draw.Targets[0].pResource = RenderTarget;
		draw.BindingCount = 2;
		draw.pBindings = bindings;
		draw.pPushData = &push;
		draw.PushDataSize = sizeof(push);
		DxvkCompute::Extension()->Draw(&draw);
		Device->SetRenderTarget(0, current);
		if (current) current->Release();
	}

	bool Replacing() { return S.enabled && S.mode == 1 && S.debugView != 2 && S.debugView < 4 && RtCore::Running() && TheShaderManager->GameState.isExterior; }

	float ReplaceRange() {
		replaceConstant.x = Replacing() ? S.range : 0.0f;
		return replaceConstant.x;
	}

	D3DXVECTOR4* ReplaceConstant() { return &replaceConstant; }
}
