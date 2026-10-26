#include "HiZ.h"
#include "GpuProfiler.h"

// UNOFFICIAL 2026-10-26 (NVR DXVK, deep/fork-hiz): see HiZ.h.

IDirect3DTexture9*		HiZ::texture = nullptr;
ID3D9NvrExtPipeline*	HiZ::pipeline = nullptr;
UINT					HiZ::width = 0;
UINT					HiZ::height = 0;
UINT					HiZ::groupsX = 0;
UINT					HiZ::groupsY = 0;
UINT					HiZ::builtFrame = 0;
bool					HiZ::built = false;
bool					HiZ::failed = false;
UINT					HiZ::frames = 0;

namespace {
	// HiZBuild.comp: set 1 = the depth, the weapon's depth, the seven levels as storage images; the sampler's heap index at 12.
	const NVR_EXT_BINDING_DESC HiZBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D, FALSE, 0 },	// TESR_DepthBuffer
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE, 1, NVR_EXT_VIEW_2D, FALSE, 0 },	// TESR_DepthBufferViewModel
		{ 1, 2, NVR_EXT_BIND_STORAGE_IMAGE, 0, NVR_EXT_VIEW_2D, TRUE,  0 },	// level 0
		{ 1, 3, NVR_EXT_BIND_STORAGE_IMAGE, 1, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 4, NVR_EXT_BIND_STORAGE_IMAGE, 2, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 5, NVR_EXT_BIND_STORAGE_IMAGE, 3, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 6, NVR_EXT_BIND_STORAGE_IMAGE, 4, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 7, NVR_EXT_BIND_STORAGE_IMAGE, 5, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 8, NVR_EXT_BIND_STORAGE_IMAGE, 6, NVR_EXT_VIEW_2D, TRUE,  0 },	// level 6
		{ 0, 0, NVR_EXT_BIND_SAMPLER,       0, NVR_EXT_VIEW_2D, FALSE, 12 },	// POINT, CLAMP
	};
	struct HiZPush { float invFullX, invFullY, invertedDepth; UINT32 pointIndex; };
	static_assert(sizeof(HiZPush) == 16, "HiZBuild.comp push block");

	const D3DXVECTOR4* RegisteredConstant(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
}

void HiZ::Release() {
	DxvkCompute::Drop(texture);
	DxvkCompute::Drop(pipeline);
}

// The texture and the pipeline, made on first use. False (and off for the session) when any of it fails.
bool HiZ::Ensure() {
	if (failed) return false;
	if (texture && pipeline) return true;
	const UINT fullW = TheRenderManager->width, fullH = TheRenderManager->height;
	groupsX = (fullW + 127) / 128;		// one workgroup per 128x128 full-resolution pixels
	groupsY = (fullH + 127) / 128;
	width = groupsX * 64;
	height = groupsY * 64;
	std::vector<char> words;
	const bool shaderThere = DxvkCompute::LoadSpirv("HiZBuild", words);	// checked first: a missing file must not turn the other paths off
	HRESULT create = shaderThere ? TheRenderManager->device->CreateTexture(width, height, Levels, D3DUSAGE_RENDERTARGET, D3DFMT_G32R32F, D3DPOOL_DEFAULT, &texture, NULL) : E_FAIL;
	if (SUCCEEDED(create) && texture && texture->GetLevelCount() == Levels)
		pipeline = DxvkCompute::CreatePipeline("HiZBuild", HiZBindings, _countof(HiZBindings), sizeof(HiZPush), true);
	if (!texture || !pipeline || texture->GetLevelCount() != Levels) {
		Logger::Log("UNOFFICIAL Hi-Z: the pyramid could not be set up (%s); Reflections and AmbientLight keep their normal paths.",
			!shaderThere ? "Compute\\HiZBuild.spv missing" : FAILED(create) || !texture ? "texture refused" : "pipeline refused");
		Release();
		failed = true;
		return false;
	}
	TheTextureManager->RegisterTexture("NVR_HiZ", (IDirect3DBaseTexture9**)&texture);
	Logger::Log("UNOFFICIAL Hi-Z: pyramid ready (%ux%u, %u levels, G32R32F, %ux%u workgroups in one dispatch).", width, height, Levels, groupsX, groupsY);
	return true;
}

void HiZ::Build(IDirect3DDevice9* Device, bool Wanted) {
	built = false;
	if (!Wanted || failed || !DxvkCompute::Available()) return;
	const UINT now = TheFrameRateManager->FrameNumber;
	if (frames && builtFrame == now) { built = true; return; }
	if (!Ensure()) return;
	IDirect3DTexture9* depth = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
	IDirect3DSurface9* depthSurface = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthSurface : nullptr;
	IDirect3DTexture9* viewModel = TheTextureManager->DepthTextureViewModel;
	const D3DXVECTOR4* depthConstants = RegisteredConstant("TESR_DepthConstants");
	if (!depth || !depthSurface || !viewModel || !depthConstants) return;
	// The extension must not read a texture that is the device's render target: never the case here (the caller set the frame's
	// target after the normals), but checked, as it costs nothing; the frame is then simply not built.
	for (DWORD i = 0; i < 4; i++) {
		IDirect3DSurface9* target = nullptr;
		if (SUCCEEDED(Device->GetRenderTarget(i, &target)) && target) {
			const bool same = target == depthSurface;
			target->Release();
			if (same) return;
		}
	}

	HiZPush push = { 1.0f / (float)TheRenderManager->width, 1.0f / (float)TheRenderManager->height, depthConstants->z, 0 };
	NVR_EXT_RESOURCE_BINDING bindings[_countof(HiZBindings)] = {};
	bindings[0] = { 0, NVR_EXT_BIND_SAMPLED_IMAGE, depth, 0, 0, 0, FALSE };
	bindings[1] = { 1, NVR_EXT_BIND_SAMPLED_IMAGE, viewModel, 0, 0, 0, FALSE };
	for (UINT k = 0; k < Levels; k++) bindings[2 + k] = { k, NVR_EXT_BIND_STORAGE_IMAGE, texture, k, 0, 0, FALSE };
	bindings[9] = { 0, NVR_EXT_BIND_SAMPLER, DxvkCompute::PointClamp(), 0, 0, 0, FALSE };
	if (!bindings[9].pResource) return;
	NVR_EXT_DISPATCH_DESC dispatch = {};
	dispatch.pPipeline = pipeline;
	dispatch.BindingCount = _countof(bindings);
	dispatch.pBindings = bindings;
	dispatch.pPushData = &push;
	dispatch.PushDataSize = sizeof(push);
	dispatch.GroupsX = groupsX;
	dispatch.GroupsY = groupsY;
	dispatch.GroupsZ = 1;
	HRESULT result;
	{
		static GpuTimer timer("  Hi-Z pyramid");
		GpuProfileScope gpu(timer, Device);
		result = DxvkCompute::Extension()->Dispatch(&dispatch);
	}
	if (FAILED(result)) {
		Logger::Log("UNOFFICIAL Hi-Z: the dispatch failed (%08lX); the pyramid is off for this session, Reflections and AmbientLight keep their normal paths.", result);
		Release();
		failed = true;
		return;
	}
	built = true;
	builtFrame = now;
	if (++frames == 1) Logger::Log("UNOFFICIAL Hi-Z: pyramid built (first frame).");
}

IDirect3DTexture9* HiZ::Current() {
	return built && !failed && builtFrame == TheFrameRateManager->FrameNumber ? texture : nullptr;
}
