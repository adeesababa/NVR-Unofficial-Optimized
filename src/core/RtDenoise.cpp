// UNOFFICIAL 2026-10-29 (NVR DXVK) the shared RT denoiser (RtDenoise.h). Passes: Compute\RtDenoiseTemporal.frag, Compute\RtDenoiseAtrous.frag.

#include "RtDenoise.h"
#include "DxvkCompute.h"

#include <cmath>
#include <string>

namespace RtDenoise {
	struct Denoiser {
		std::string			name;
		IDirect3DTexture9*	history[2] = {};	// RGBA16F: value, second moment, history length, view depth (ping-pong by frame)
		IDirect3DTexture9*	chain[2] = {};		// RGBA16F: the a-trous ping-pong
		UINT				width = 0, height = 0;
		UInt32				frame = 0;
		bool				hasHistory = false;
		D3DXMATRIX			prevViewProj;
		D3DXVECTOR3			prevCamera = { 0.0f, 0.0f, 0.0f };
	};

	namespace {
		ID3D9NvrExtPipeline* temporal = nullptr;
		ID3D9NvrExtPipeline* atrous = nullptr;
		bool tried = false;

		IDirect3DBaseTexture9* Texture(const char* name) {
			auto found = TheTextureManager->TextureNames.find(name);
			return found == TheTextureManager->TextureNames.end() || !found->second ? nullptr : *found->second;
		}
		const D3DXVECTOR4* Constant(const char* name) {
			auto found = TheShaderManager->ConstantsTable.find(name);
			return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
		}

		ID3D9NvrExtPipeline* Make(const char* name, UINT images, UINT pushSize) {
			std::vector<char> spirv;
			ID3D9NvrExt* ext = DxvkCompute::Extension();
			if (!ext || !DxvkCompute::LoadSpirv(name, spirv)) return nullptr;
			NVR_EXT_BINDING_DESC bindings[4] = {};
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
			desc.ColorTargetCount = 1;
			desc.Blend = NVR_EXT_BLEND_NONE;
			desc.ColorWriteMask = 0xFu;
			desc.pDebugName = name;
			ID3D9NvrExtPipeline* pipeline = nullptr;
			if (FAILED(ext->CreateGraphicsPipeline(&desc, &pipeline))) pipeline = nullptr;
			if (!pipeline) Logger::Log("UNOFFICIAL NVR DXVK: RT denoiser: the pass %s could not be made (Compute\\%s.spv; the DXVK log says why)", name, name);
			return pipeline;
		}

		bool Draw(ID3D9NvrExtPipeline* pipeline, IDirect3DSurface9* target, IUnknown* const* images, UINT count, const void* push, UINT pushSize) {
			NVR_EXT_RESOURCE_BINDING bindings[4] = {};
			for (UINT i = 0; i < count; i++) { bindings[i].Slot = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].pResource = images[i]; }
			bindings[count].Slot = 0; bindings[count].Type = NVR_EXT_BIND_SAMPLER; bindings[count].pResource = DxvkCompute::PointClamp();
			NVR_EXT_DRAW_DESC draw = {};
			draw.pPipeline = pipeline;
			draw.TargetCount = 1;
			draw.Targets[0].pResource = target;
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
		d->name = Name ? Name : "RT denoiser";
		return d;
	}

	bool Run(Denoiser* d, IDirect3DDevice9* Device, IDirect3DBaseTexture9* Noisy, IDirect3DSurface9* Output, const Settings& S) {
		if (!d || !Noisy || !Output || !DxvkCompute::Extension() || !DxvkCompute::PointClamp()) return false;
		if (!tried) {
			tried = true;
			temporal = Make("RtDenoiseTemporal", 3, 160);
			atrous = Make("RtDenoiseAtrous", 2, 48);
		}
		IDirect3DBaseTexture9* depth = Texture("TESR_DepthBuffer");
		IDirect3DBaseTexture9* normals = Texture("TESR_NormalsBuffer");
		const D3DXVECTOR4* cameraData = Constant("TESR_CameraData");
		if (!temporal || !atrous || !depth || !normals || !cameraData) return false;

		const UINT scale = S.Downscale == 2 ? 2u : 1u;
		const UINT w = (std::max)(1u, (UINT)TheRenderManager->width / scale), h = (std::max)(1u, (UINT)TheRenderManager->height / scale);
		if (d->width != w || d->height != h || !d->history[0]) {
			for (IDirect3DTexture9*& t : d->history) if (t) { t->Release(); t = nullptr; }
			for (IDirect3DTexture9*& t : d->chain) if (t) { t->Release(); t = nullptr; }
			for (IDirect3DTexture9*& t : d->history) Device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t, nullptr);
			for (IDirect3DTexture9*& t : d->chain) Device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t, nullptr);
			d->width = w; d->height = h;
			d->hasHistory = false;
			if (!d->history[0] || !d->history[1] || !d->chain[0] || !d->chain[1]) return false;
		}

		// The camera: this frame's basis (as RtSunShadows / SunShadows.glsl), last frame's view-projection and the camera's move since
		const D3DXMATRIX& view = TheRenderManager->viewMatrix;
		const D3DXMATRIX& proj = TheRenderManager->projMatrix;
		const D3DXVECTOR3 camera(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
		const D3DXVECTOR3 move = camera - d->prevCamera;
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

		IDirect3DSurface9* current = nullptr;
		Device->GetRenderTarget(0, &current);	// a pass may not draw into the device's current target: park it on a chain texture
		IDirect3DTexture9* last = d->history[d->frame & 1];
		IDirect3DTexture9* now = d->history[(d->frame + 1) & 1];
		bool ok = true;
		IDirect3DSurface9* nowSurface = Surface(now);
		IDirect3DSurface9* park = Surface(d->chain[1]);
		Device->SetRenderTarget(0, park);
		IUnknown* temporalImages[3] = { Noisy, last, depth };
		ok = nowSurface && Draw(temporal, nowSurface, temporalImages, 3, &t, sizeof(t));

		// a-trous: history -> chain[0] -> chain[1] -> ... ; the last one into Output
		struct { float params[4], size[4]; UInt32 samplerIndex, pad[3]; } a = {};
		a.params[1] = S.DepthSigma; a.params[2] = S.NormalPower; a.params[3] = S.ValueSigma;
		a.size[0] = 1.0f / w; a.size[1] = 1.0f / h;
		IDirect3DTexture9* source = now;
		const int iterations = (std::max)(0, (std::min)(S.Iterations, 5));
		if (ok && iterations == 0) {
			IUnknown* images[2] = { now, normals };
			a.params[0] = 0.0f;	// step 0: every tap is the centre (a copy of the temporal result)
			ok = Draw(atrous, Output, images, 2, &a, sizeof(a));
		}
		for (int i = 0; ok && i < iterations; i++) {
			const bool lastPass = i == iterations - 1;
			IDirect3DTexture9* target = d->chain[i & 1];
			IDirect3DSurface9* targetSurface = lastPass ? nullptr : Surface(target);
			if (!lastPass && targetSurface == park) Device->SetRenderTarget(0, nowSurface);	// never draw into the parked target
			a.params[0] = (float)(1 << i);
			IUnknown* images[2] = { source, normals };
			ok = Draw(atrous, lastPass ? Output : targetSurface, images, 2, &a, sizeof(a));
			if (targetSurface) targetSurface->Release();
			source = target;
		}
		Device->SetRenderTarget(0, current);
		if (current) current->Release();
		if (park) park->Release();
		if (nowSurface) nowSurface->Release();

		d->prevViewProj = TheRenderManager->ViewProjMatrix;
		d->prevCamera = camera;
		d->hasHistory = ok;
		d->frame++;
		return ok;
	}
}
