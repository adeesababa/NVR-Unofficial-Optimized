// UNOFFICIAL 2026-10-29 (NVR DXVK) the shared RT denoiser (RtDenoise.h). Passes: Compute\RtDenoiseTemporal.frag, Compute\RtDenoiseAtrous.frag.

#include "RtDenoise.h"
#include "DxvkCompute.h"
#include "GpuProfiler.h"
#include "Upscaler.h"	// UNOFFICIAL 2026-10-30 DLSS RR (RayReconstructionWantsRaw)

#include <cmath>
#include <string>

namespace RtDenoise {
	struct Denoiser {
		std::string			name;
		std::string			effect;					// UNOFFICIAL 2026-10-30 the caller's own name (the DLSS RR bypass' log line)
		UInt32				rawSeen = 0;			// UNOFFICIAL 2026-10-30 rawGeneration when this one last ran (below)
		bool				rawLogged = false;
		IDirect3DTexture9*	history[2] = {};	// RGBA16F: value, second moment, history length, view depth (ping-pong by frame)
		IDirect3DTexture9*	chain[2] = {};		// RGBA16F: the a-trous ping-pong
		// RGB mode (Settings.Channels 3): A = (rgb, history length) in history / chain, B = (luma second moment, view depth) here
		IDirect3DTexture9*	historyB[2] = {};
		IDirect3DTexture9*	chainB[2] = {};
		IDirect3DTexture9*	scratchB = nullptr;		// the last a-trous pass' B output (not needed after)
		IDirect3DTexture9*	park = nullptr;			// 8 x 8: the device's render target while the passes run (a pass may not draw into it)
		int					channels = 0;
		GpuTimer*			timer = nullptr;			// "RT denoise: <name>" in the GPU profile
		UINT				width = 0, height = 0;
		UInt32				frame = 0;
		bool				hasHistory = false;
		D3DXMATRIX			prevViewProj;
		D3DXVECTOR3			prevCamera = { 0.0f, 0.0f, 0.0f };
	};

	namespace {
		ID3D9NvrExtPipeline* temporal = nullptr;
		ID3D9NvrExtPipeline* atrous = nullptr;
		ID3D9NvrExtPipeline* temporalRgb = nullptr;
		ID3D9NvrExtPipeline* atrousRgb = nullptr;
		bool tried = false;
		// UNOFFICIAL 2026-10-30 DLSS RR: counts the calls Run skipped for RR (0 while it never did). A denoiser that ran before it changed
		// holds a history from before those frames, also one its caller did not call meanwhile (RtLampShadows stops at rank 0).
		UInt32 rawGeneration = 0;

		IDirect3DBaseTexture9* Texture(const char* name) {
			auto found = TheTextureManager->TextureNames.find(name);
			return found == TheTextureManager->TextureNames.end() || !found->second ? nullptr : *found->second;
		}
		const D3DXVECTOR4* Constant(const char* name) {
			auto found = TheShaderManager->ConstantsTable.find(name);
			return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
		}

		ID3D9NvrExtPipeline* Make(const char* name, UINT images, UINT pushSize, UINT targets = 1) {
			std::vector<char> spirv;
			ID3D9NvrExt* ext = DxvkCompute::Extension();
			if (!ext || !DxvkCompute::LoadSpirv(name, spirv)) return nullptr;
			NVR_EXT_BINDING_DESC bindings[8] = {};	// images + the sampler
			for (UINT i = 0; i < images; i++) bindings[i] = { 1, i, NVR_EXT_BIND_SAMPLED_IMAGE, i, NVR_EXT_VIEW_2D, FALSE, 0 };
			bindings[images] = { 0, 0, NVR_EXT_BIND_SAMPLER, 0, NVR_EXT_VIEW_2D, FALSE, pushSize - 16 };	// samplerIndex: the last 16 bytes
			NVR_EXT_GRAPHICS_PIPELINE_DESC desc = {};
			desc.pFragmentSpirv = spirv.data();
			desc.FragmentSpirvSize = (UINT)spirv.size();
			desc.BindingCount = images + 1;
			desc.pBindings = bindings;
			desc.PushConstantSize = pushSize;
			desc.UsesSamplerHeap = TRUE;
			desc.SamplerHeapSet = 0;
			desc.SamplerHeapBinding = 0;
			desc.ColorTargetCount = targets;
			desc.Blend = NVR_EXT_BLEND_NONE;
			desc.ColorWriteMask = 0xFu;
			desc.pDebugName = name;
			ID3D9NvrExtPipeline* pipeline = nullptr;
			if (FAILED(ext->CreateGraphicsPipeline(&desc, &pipeline))) pipeline = nullptr;
			if (!pipeline) Logger::Log("UNOFFICIAL NVR DXVK: RT denoiser: the pass %s could not be made (Compute\\%s.spv; the DXVK log says why)", name, name);
			return pipeline;
		}

		bool Draw(ID3D9NvrExtPipeline* pipeline, IDirect3DSurface9* target, IUnknown* const* images, UINT count, const void* push, UINT pushSize,
			IDirect3DSurface9* target2 = nullptr) {
			NVR_EXT_RESOURCE_BINDING bindings[8] = {};	// images + the sampler
			for (UINT i = 0; i < count; i++) { bindings[i].Slot = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].pResource = images[i]; }
			bindings[count].Slot = 0; bindings[count].Type = NVR_EXT_BIND_SAMPLER; bindings[count].pResource = DxvkCompute::PointClamp();
			NVR_EXT_DRAW_DESC draw = {};
			draw.pPipeline = pipeline;
			draw.TargetCount = target2 ? 2 : 1;
			draw.Targets[0].pResource = target;
			draw.Targets[1].pResource = target2;
			draw.BindingCount = count + 1;
			draw.pBindings = bindings;
			draw.pPushData = push;
			draw.PushDataSize = pushSize;
			return SUCCEEDED(DxvkCompute::Extension()->Draw(&draw));
		}

		IDirect3DSurface9* Surface(IDirect3DTexture9* t) {
			IDirect3DSurface9* s = nullptr;
			return t && SUCCEEDED(t->GetSurfaceLevel(0, &s)) ? s : nullptr;
		}
	}

	Denoiser* Create(const char* Name) {
		Denoiser* d = new Denoiser();
		d->effect = Name ? Name : "RT denoiser";
		d->name = std::string("RT denoise: ") + d->effect;
		d->timer = new GpuTimer(d->name.c_str());
		return d;
	}

	bool Run(Denoiser* d, IDirect3DDevice9* Device, IDirect3DBaseTexture9* Noisy, IDirect3DSurface9* Output, const Settings& S) {
		// UNOFFICIAL 2026-10-30 DLSS Ray Reconstruction (docs\next\FORK2-DLSS-RR.md): while RR denoises the frame (Upscaler::
		// RayReconstructionWantsRaw: RR ran last frame, RayReconstruction on, RRKeepDenoisers off) no pass runs and the caller takes its
		// noisy signal, as it does whenever this returns false. Every denoiser's history is dropped then (rawGeneration), so the first
		// frame denoised again starts afresh instead of reprojecting from a camera of frames ago.
		if (Upscaler::RayReconstructionWantsRaw()) {
			rawGeneration++;
			if (d && !d->rawLogged) {
				d->rawLogged = true;
				Logger::Log("UNOFFICIAL DLSS RR: %s: its own denoiser is skipped (RR denoises the raw signal)", d->effect.c_str());
			}
			return false;
		}
		if (!d || !Noisy || !Output || !DxvkCompute::Extension() || !DxvkCompute::PointClamp()) return false;
		if (!tried) {
			tried = true;
			temporal = Make("RtDenoiseTemporal", 3, 160);
			atrous = Make("RtDenoiseAtrous", 3, 48);
			temporalRgb = Make("RtDenoiseTemporalRgb", 4, 160, 2);
			atrousRgb = Make("RtDenoiseAtrousRgb", 3, 48, 2);
		}
		const bool rgb = S.Channels == 3;
		if (rgb && (!temporalRgb || !atrousRgb)) return false;
		IDirect3DBaseTexture9* depth = Texture("TESR_DepthBuffer");
		IDirect3DBaseTexture9* normals = Texture("TESR_NormalsBuffer");
		const D3DXVECTOR4* cameraData = Constant("TESR_CameraData");
		if (!temporal || !atrous || !depth || !normals || !cameraData) return false;

		const UINT scale = S.Downscale == 2 ? 2u : 1u;
		const UINT w = (std::max)(1u, (UINT)TheRenderManager->width / scale), h = (std::max)(1u, (UINT)TheRenderManager->height / scale);
		if (d->width != w || d->height != h || !d->history[0] || d->channels != S.Channels) {
			IDirect3DTexture9** all[] = { &d->history[0], &d->history[1], &d->chain[0], &d->chain[1], &d->historyB[0], &d->historyB[1],
				&d->chainB[0], &d->chainB[1], &d->scratchB, &d->park };
			for (IDirect3DTexture9** t : all) if (*t) { (*t)->Release(); *t = nullptr; }
			const int count = rgb ? 9 : 4;
			for (int i = 0; i < count; i++) Device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, all[i], nullptr);
			Device->CreateTexture(8, 8, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d->park, nullptr);
			d->width = w; d->height = h;
			d->channels = S.Channels;
			d->hasHistory = false;
			for (int i = 0; i < count; i++) if (!*all[i]) return false;
			if (!d->park) return false;
		}

		// The camera: this frame's basis (as RtSunShadows / SunShadows.glsl), last frame's view-projection and the camera's move since
		const D3DXMATRIX& view = TheRenderManager->viewMatrix;
		const D3DXMATRIX& proj = TheRenderManager->projMatrix;
		const D3DXVECTOR3 camera(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
		const D3DXVECTOR3 move = camera - d->prevCamera;
		if (d->rawSeen != rawGeneration) { d->rawSeen = rawGeneration; d->hasHistory = false; }	// UNOFFICIAL 2026-10-30 DLSS RR (above)
		const bool cut = !d->hasHistory || S.Reset || D3DXVec3Length(&move) > 512.0f;	// a jump: a camera cut, a load, a teleport

		struct {
			float prevViewProj[16];
			float forward[4], right[4], up[4], move[4], params[4];
			UInt32 samplerIndex, pad[3];
		} t = {};
		memcpy(t.prevViewProj, &d->prevViewProj, 64);
		t.forward[0] = view._13; t.forward[1] = view._23; t.forward[2] = view._33; t.forward[3] = cameraData->y;
		t.right[0] = view._11 / proj._11; t.right[1] = view._21 / proj._11; t.right[2] = view._31 / proj._11;
		t.up[0] = view._12 / proj._22; t.up[1] = view._22 / proj._22; t.up[2] = view._32 / proj._22;
		t.move[0] = move.x; t.move[1] = move.y; t.move[2] = move.z; t.move[3] = cut ? 1.0f : 0.0f;
		t.params[0] = (std::max)(S.MaxHistory, 1.0f); t.params[1] = S.DepthTolerance; t.params[2] = 1.0f / w; t.params[3] = 1.0f / h;

		GpuProfileScope gpu(*d->timer, Device);
		IDirect3DSurface9* current = nullptr;
		Device->GetRenderTarget(0, &current);	// a pass may not draw into the device's current target: park it on an 8 x 8 texture
		IDirect3DSurface9* park = Surface(d->park);
		Device->SetRenderTarget(0, park);
		struct { float params[4], size[4]; UInt32 samplerIndex, pad[3]; } a = {};
		a.params[1] = S.DepthSigma; a.params[2] = S.NormalPower; a.params[3] = S.ValueSigma;
		a.size[0] = 1.0f / w; a.size[1] = 1.0f / h; a.size[2] = S.Ids ? 1.0f : 0.0f; a.size[3] = S.Kernel == 3 ? 1.0f : 0.0f;
		const int iterations = (std::max)(0, (std::min)(S.Iterations, 5));
		const UInt32 last = d->frame & 1, now = (d->frame + 1) & 1;
		bool ok = true;
		if (!rgb) {
			IDirect3DSurface9* nowSurface = Surface(d->history[now]);
			IUnknown* temporalImages[3] = { Noisy, d->history[last], depth };
			ok = nowSurface && Draw(temporal, nowSurface, temporalImages, 3, &t, sizeof(t));
			if (nowSurface) nowSurface->Release();
			// a-trous: history -> chain[0] -> chain[1] -> ... ; the last one into Output (no iterations: a copy, step 0)
			IDirect3DTexture9* source = d->history[now];
			for (int i = 0; ok && i < (std::max)(iterations, 1); i++) {
				const bool lastPass = i == (std::max)(iterations, 1) - 1;
				IDirect3DSurface9* target = lastPass ? nullptr : Surface(d->chain[i & 1]);
				a.params[0] = iterations ? (float)(1 << i) : 0.0f;
				IUnknown* images[3] = { source, normals, S.Ids ? (IUnknown*)S.Ids : (IUnknown*)normals };	// [2]: a stand-in without ids
				ok = Draw(atrous, lastPass ? Output : target, images, 3, &a, sizeof(a));
				if (target) target->Release();
				source = d->chain[i & 1];
			}
		}
		else {
			IDirect3DSurface9* nowA = Surface(d->history[now]);
			IDirect3DSurface9* nowB = Surface(d->historyB[now]);
			IUnknown* temporalImages[4] = { Noisy, d->history[last], depth, d->historyB[last] };
			ok = nowA && nowB && Draw(temporalRgb, nowA, temporalImages, 4, &t, sizeof(t), nowB);
			if (nowA) nowA->Release();
			if (nowB) nowB->Release();
			IDirect3DTexture9* sourceA = d->history[now];
			IDirect3DTexture9* sourceB = d->historyB[now];
			IDirect3DSurface9* scratch = Surface(d->scratchB);
			for (int i = 0; ok && i < (std::max)(iterations, 1); i++) {
				const bool lastPass = i == (std::max)(iterations, 1) - 1;
				IDirect3DSurface9* targetA = lastPass ? nullptr : Surface(d->chain[i & 1]);
				IDirect3DSurface9* targetB = lastPass ? nullptr : Surface(d->chainB[i & 1]);
				a.params[0] = iterations ? (float)(1 << i) : 0.0f;
				IUnknown* images[3] = { sourceA, normals, sourceB };
				ok = Draw(atrousRgb, lastPass ? Output : targetA, images, 3, &a, sizeof(a), lastPass ? scratch : targetB);
				if (targetA) targetA->Release();
				if (targetB) targetB->Release();
				sourceA = d->chain[i & 1];
				sourceB = d->chainB[i & 1];
			}
			if (scratch) scratch->Release();
		}
		Device->SetRenderTarget(0, current);
		if (current) current->Release();
		if (park) park->Release();

		d->prevViewProj = TheRenderManager->ViewProjMatrix;
		d->prevCamera = camera;
		d->hasHistory = ok;
		d->frame++;
		return ok;
	}
}
