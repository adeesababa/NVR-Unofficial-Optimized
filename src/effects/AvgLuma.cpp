#include "AvgLuma.h"
#include "../core/DxvkCompute.h"
#include <cmath>

void AvgLumaEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_AvgLumaBuffer", &Textures.AvgLumaTexture, &Textures.AvgLumaSurface, 1, 1, D3DFMT_A16B16G16R16F);
}

// UNOFFICIAL 2026-10-23 (NVR DXVK): see AvgLuma.h and src\hlsl\NewVegas\Compute\AvgLuma.comp.

namespace {
	// Bindings as AvgLuma.comp declares them (set 1; the sampler heap is set 0 binding 0).
	const NVR_EXT_BINDING_DESC AvgLumaBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },		// t_color: TESR_RenderedBuffer
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },		// t_depth: TESR_DepthBuffer
		{ 1, 2, NVR_EXT_BIND_STORAGE_BUFFER, 0, NVR_EXT_VIEW_2D, TRUE,  0 },		// Partials
		{ 1, 3, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, FALSE, 0 },		// StateIn
		{ 1, 4, NVR_EXT_BIND_STORAGE_BUFFER, 2, NVR_EXT_VIEW_2D, TRUE,  0 },		// StateOut
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 20 },		// samplerIndex at push offset 20
	};
	// AvgLumaHistogram.comp: the same, plus the 256-bin histogram (slot 3).
	const NVR_EXT_BINDING_DESC AvgLumaHistogramBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_STORAGE_BUFFER, 0, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 3, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 4, NVR_EXT_BIND_STORAGE_BUFFER, 2, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 5, NVR_EXT_BIND_STORAGE_BUFFER, 3, NVR_EXT_VIEW_2D, TRUE,  0 },		// Histogram (256 uints)
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 20 },
	};
	const UINT AvgLumaStateBytes = 32;	// vec4 cur; uvec2 packed; float average; float pad (histogram: the exact average)
	const UINT AvgLumaPackedOffset = 16;

	float HalfToFloat(UInt16 h) {
		UInt32 sign = (h >> 15) & 1, exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
		float value;
		if (exponent == 0) value = ldexpf((float)mantissa, -24);
		else if (exponent == 31) value = mantissa ? NAN : INFINITY;
		else value = ldexpf((float)(mantissa + 1024), (int)exponent - 25);
		return sign ? -value : value;
	}
}

void AvgLumaEffect::ComputeRelease() {
	DxvkCompute::Drop(computePipeline);
	DxvkCompute::Drop(computePartials);
	DxvkCompute::Drop(computeState[0]);
	DxvkCompute::Drop(computeState[1]);
	DxvkCompute::Drop(computeSampler);
	DxvkCompute::Drop(histPipeline);
	DxvkCompute::Drop(histBins);
	histTried = false;
	computeOk = false;
}

// UNOFFICIAL 2026-10-26 (fork-only): see AvgLuma.h.
bool AvgLumaEffect::HistogramReady() {
	if (histPipeline && histBins) return true;
	if (histTried || histFailed || !computeOk) return false;
	histTried = true;
	histPipeline = DxvkCompute::CreatePipeline("AvgLumaHistogram", AvgLumaHistogramBindings, _countof(AvgLumaHistogramBindings), sizeof(ComputePush), true);
	histBins = DxvkCompute::CreateBuffer(256 * 4, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "AvgLuma histogram");
	if (!histPipeline || !histBins) {
		DxvkCompute::Drop(histPipeline);
		DxvkCompute::Drop(histBins);
		histFailed = true;
		Logger::Log("UNOFFICIAL AvgLuma: histogram path could not be set up; the plain average is used");
		return false;
	}
	Logger::Log("UNOFFICIAL AvgLuma: histogram path ready (256 bins, log2 luma -16..4)");
	return true;
}

bool AvgLumaEffect::ComputeReady() {
	if (computeOk) {
		if (computeWidth == TheRenderManager->width && computeHeight == TheRenderManager->height) return true;
		ComputeRelease();	// resolution changed: the partials buffer is sized per tile count
		computeTried = false;
	}
	if (computeTried || !DxvkCompute::Available()) return false;
	computeTried = true;

	computeWidth = TheRenderManager->width;
	computeHeight = TheRenderManager->height;
	computeGroupsX = (computeWidth + 15) / 16;
	computeGroupsY = (computeHeight + 15) / 16;

	computePipeline = DxvkCompute::CreatePipeline("AvgLuma", AvgLumaBindings, _countof(AvgLumaBindings), sizeof(ComputePush), true);
	computePartials = DxvkCompute::CreateBuffer(computeGroupsX * computeGroupsY * 4, NVR_EXT_BUFFER_STORAGE, "AvgLuma partials");
	computeState[0] = DxvkCompute::CreateBuffer(AvgLumaStateBytes, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_SRC | NVR_EXT_BUFFER_TRANSFER_DST, "AvgLuma state 0");
	computeState[1] = DxvkCompute::CreateBuffer(AvgLumaStateBytes, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_SRC | NVR_EXT_BUFFER_TRANSFER_DST, "AvgLuma state 1");
	computeSampler = DxvkCompute::CreateSampler(NVR_EXT_FILTER_LINEAR, NVR_EXT_ADDRESS_CLAMP);	// the pass's samplers: LINEAR, CLAMP
	if (!computePipeline || !computePartials || !computeState[0] || !computeState[1] || !computeSampler) {
		ComputeRelease();
		Logger::Log("UNOFFICIAL AvgLuma: compute path could not be set up; the D3D9 pass is used");
		return false;
	}
	// Both histories start at zero, like the texture does.
	DxvkCompute::Extension()->ClearBuffer(computeState[0], 0, 0, 0);
	DxvkCompute::Extension()->ClearBuffer(computeState[1], 0, 0, 0);
	computeCurrent = 0;
	computeOk = true;
	Logger::Log("UNOFFICIAL AvgLuma: compute path ready (%ux%u, %ux%u tiles of 16x16)", computeWidth, computeHeight, computeGroupsX, computeGroupsY);
	return true;
}

bool AvgLumaEffect::RenderCompute(IDirect3DDevice9* Device) {
	// UNOFFICIAL 2026-10-26: the histogram (fork-only, [Shaders.Exposure.Histogram] Histogram) runs on this path too, with or
	// without AvgLumaCompute; without the extension, or if it fails, the frame keeps the exact average / the D3D9 pass.
	const ExposureEffect* exposure = TheShaderManager->Effects.Exposure;
	const bool histogramWanted = exposure && exposure->Histogram.Histogram && !histFailed;
	if (!TheSettingManager->SettingsMain.Main.AvgLumaCompute && !histogramWanted) return false;
	if (!Enabled || !ShouldRender()) return false;
	if (!ComputeReady()) return false;
	const bool histogram = histogramWanted && HistogramReady();
	if (!histogram && !TheSettingManager->SettingsMain.Main.AvgLumaCompute) return false;

	IDirect3DTexture9* color = TheTextureManager->RenderedTexture;		// the chain's current image (what the pass samples as TESR_RenderedBuffer)
	IDirect3DTexture9* depth = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
	if (!color || !depth || !Textures.AvgLumaTexture) return false;

	ID3D9NvrExt* ext = DxvkCompute::Extension();
	ID3D9NvrExtBuffer* stateIn = computeState[computeCurrent];
	ID3D9NvrExtBuffer* stateOut = computeState[computeCurrent ^ 1];

	ComputePush push = {};
	push.width = computeWidth;
	push.height = computeHeight;
	push.groupsX = computeGroupsX;
	push.groupsY = computeGroupsY;
	push.motionBlurData = TheShaderManager->Effects.MotionBlur->Constants.Data;
	push.depthOfFieldData = TheShaderManager->Effects.DepthOfField->Constants.Data;
	push.gameTime = TheShaderManager->ShaderConst.GameTime;
	push.exposureData = TheShaderManager->Effects.Exposure->Constants.Data;
	push.cameraData = TheRenderManager->CameraData;
	if (histogram) {
		push.lowFraction = exposure->Histogram.LowPercent * 0.01f;
		push.highFraction = exposure->Histogram.HighPercent * 0.01f;
	}

	NVR_EXT_RESOURCE_BINDING bindings[] = {
		{ 0, NVR_EXT_BIND_SAMPLED_IMAGE,  color,           0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_SAMPLED_IMAGE,  depth,           0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_STORAGE_BUFFER, computePartials, 0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_STORAGE_BUFFER, stateIn,         0, 0, 0, FALSE },
		{ 2, NVR_EXT_BIND_STORAGE_BUFFER, stateOut,        0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_SAMPLER,        computeSampler,  0, 0, 0, FALSE },
		{ 3, NVR_EXT_BIND_STORAGE_BUFFER, histBins,        0, 0, 0, FALSE },	// histogram only
	};
	NVR_EXT_DISPATCH_DESC dispatch = {};
	dispatch.pPipeline = histogram ? histPipeline : computePipeline;
	dispatch.BindingCount = histogram ? _countof(bindings) : _countof(bindings) - 1;
	dispatch.pBindings = bindings;
	dispatch.pPushData = &push;
	dispatch.PushDataSize = sizeof(push);

	HRESULT result = S_OK;
	if (histogram) result = ext->ClearBuffer(histBins, 0, 0, 0);	// the bins start at zero every frame
	push.phase = 0;		// one partial sum per 16x16 tile (histogram: and the tile's bins)
	dispatch.GroupsX = computeGroupsX; dispatch.GroupsY = computeGroupsY; dispatch.GroupsZ = 1;
	if (SUCCEEDED(result)) result = ext->Dispatch(&dispatch);
	if (SUCCEEDED(result)) {
		push.phase = 1;	// the total, then the pass's per-frame logic
		dispatch.GroupsX = 1; dispatch.GroupsY = 1;
		result = ext->Dispatch(&dispatch);
	}
	const bool checking = TheSettingManager->SettingsMain.Main.AvgLumaComputeCheck > 0;
	if (SUCCEEDED(result) && !checking)	// in check mode the D3D9 pass keeps the texture
		result = ext->CopyBufferToTexture(Textures.AvgLumaTexture, 0, 0, stateOut, AvgLumaPackedOffset);
	if (FAILED(result) && histogram) {
		// Only the histogram is given up: the plain average (or the D3D9 pass) takes this and every later frame.
		Logger::Log("UNOFFICIAL AvgLuma: histogram dispatch failed (%08lX); the plain average is used from now on", result);
		DxvkCompute::Drop(histPipeline);
		DxvkCompute::Drop(histBins);
		histFailed = true;
		return false;
	}
	if (FAILED(result)) {
		Logger::Log("UNOFFICIAL AvgLuma: compute dispatch failed (%08lX); the D3D9 pass is used from now on", result);
		ComputeRelease();
		DxvkCompute::Fail("an AvgLuma dispatch failed");
		return false;
	}
	computeCurrent ^= 1;
	lastHistogram = histogram;
	if (++computeFrames == 1) Logger::Log("UNOFFICIAL AvgLuma: compute path running (exact average of %ux%u pixels).", computeWidth, computeHeight);
	if (histogram && ++histFrames == 1)
		Logger::Log("UNOFFICIAL AvgLuma: histogram path running (darkest %.1f %% and brightest %.1f %% of the pixels left out).", exposure->Histogram.LowPercent, exposure->Histogram.HighPercent);
	if (!histogram) histFrames = 0;
	return !checking;	// checking: the caller also runs the D3D9 pass
}

void AvgLumaEffect::CheckCompute(IDirect3DDevice9* Device, bool computeRan) {
	const int every = TheSettingManager->SettingsMain.Main.AvgLumaComputeCheck;
	if (every <= 0 || !computeRan || !computeOk) return;
	if (++checkFrames % (UINT)every) return;

	// Compute's result (waits for the GPU: test only)...
	struct { float cur[4]; UInt32 packed[2]; float average; float pad; } state = {};
	ID3D9NvrExtBuffer* stateOut = computeState[computeCurrent];	// written last
	if (FAILED(DxvkCompute::Extension()->ReadBuffer(stateOut, 0, sizeof(state), &state))) {
		Logger::Log("UNOFFICIAL AvgLuma check: ReadBuffer failed");
		return;
	}
	// ...and the D3D9 pass's texture.
	float tex[4] = { NAN, NAN, NAN, NAN };
	IDirect3DSurface9* sys = nullptr;
	if (SUCCEEDED(Device->CreateOffscreenPlainSurface(1, 1, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys, NULL))) {
		D3DLOCKED_RECT rect;
		if (SUCCEEDED(Device->GetRenderTargetData(Textures.AvgLumaSurface, sys)) && SUCCEEDED(sys->LockRect(&rect, NULL, D3DLOCK_READONLY))) {
			const UInt16* h = (const UInt16*)rect.pBits;
			for (int i = 0; i < 4; i++) tex[i] = HalfToFloat(h[i]);
			sys->UnlockRect();
		}
		sys->Release();
	}
	if (lastHistogram)	// the histogram path: its trimmed average next to the exact one (both from this frame's compute) and the 112 taps
		Logger::Log("UNOFFICIAL AvgLuma check (frame %u): histogram average %.6f (exact %.6f, ratio %.3f) -> luma %.6f animated %.6f | D3D9 112 taps: luma %.6f animated %.6f | diff luma %+.6f",
			checkFrames, state.average, state.pad, state.pad > 0.0f ? state.average / state.pad : 0.0f, state.cur[0], state.cur[1], tex[0], tex[1], state.cur[0] - tex[0]);
	else
	Logger::Log("UNOFFICIAL AvgLuma check (frame %u): compute exact average %.6f -> luma %.6f animated %.6f focus %.5f | D3D9 112 taps: luma %.6f animated %.6f focus %.5f | diff luma %+.6f",
		checkFrames, state.average, state.cur[0], state.cur[1], state.cur[2], tex[0], tex[1], tex[2], state.cur[0] - tex[0]);
}
