#include "AvgLuma.h"
#include "../core/DxvkCompute.h"
#include "../core/RenderRect.h"
#include <cmath>

void AvgLumaEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_AvgLumaBuffer", &Textures.AvgLumaTexture, &Textures.AvgLumaSurface, 1, 1, D3DFMT_A16B16G16R16F);
}


namespace {
	const NVR_EXT_BINDING_DESC AvgLumaBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_STORAGE_BUFFER, 0, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 1, 3, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 4, NVR_EXT_BIND_STORAGE_BUFFER, 2, NVR_EXT_VIEW_2D, TRUE,  0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 20 },
	};
	const UINT AvgLumaStateBytes = 32;
	const UINT AvgLumaPackedOffset = 16;
	const UINT AvgLumaHistogramBins = 128;
	const float AvgLumaHistogramLog2Low = -12.0f, AvgLumaHistogramLog2High = 4.0f;

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
	DxvkCompute::Drop(histogramPipeline);
	DxvkCompute::Drop(histogramBins);
	computeOk = false;
}

bool AvgLumaEffect::ComputeReady() {
	if (computeOk) {
		if (computeWidth == TheRenderManager->width && computeHeight == TheRenderManager->height) return true;
		ComputeRelease();
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
	computeSampler = DxvkCompute::CreateSampler(NVR_EXT_FILTER_LINEAR, NVR_EXT_ADDRESS_CLAMP);
	if (!computePipeline || !computePartials || !computeState[0] || !computeState[1] || !computeSampler) {
		ComputeRelease();
		Logger::Log("UNOFFICIAL AvgLuma: compute path could not be set up; the D3D9 pass is used");
		return false;
	}
	if (DxvkCompute::HasSpirv("AvgLumaHistogram")) {
		histogramPipeline = DxvkCompute::CreatePipeline("AvgLumaHistogram", AvgLumaBindings, _countof(AvgLumaBindings), sizeof(ComputePush), true);
		histogramBins = DxvkCompute::CreateBuffer(AvgLumaHistogramBins * 4, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_DST, "AvgLuma histogram");
		if (!histogramPipeline || !histogramBins) {
			DxvkCompute::Drop(histogramPipeline);
			DxvkCompute::Drop(histogramBins);
			Logger::Log("UNOFFICIAL AvgLuma: the histogram pass could not be set up; Histogram uses the exact average");
		}
	}
	DxvkCompute::Extension()->ClearBuffer(computeState[0], 0, 0, 0);
	DxvkCompute::Extension()->ClearBuffer(computeState[1], 0, 0, 0);
	computeCurrent = 0;
	computeOk = true;
	Logger::Log("UNOFFICIAL AvgLuma: compute path ready (%ux%u, %ux%u tiles of 16x16)", computeWidth, computeHeight, computeGroupsX, computeGroupsY);
	return true;
}

bool AvgLumaEffect::RenderCompute(IDirect3DDevice9* Device) {
	const ExposureEffect* exposure = TheShaderManager->Effects.Exposure;
	const bool histogramWanted = exposure && exposure->Settings.Histogram;
	if (!TheSettingManager->SettingsMain.Main.AvgLumaCompute && !histogramWanted) return false;
	if (RenderRect::Active()) return false;
	if (!Enabled || !ShouldRender()) return false;
	if (!ComputeReady()) {
		static bool said = false;
		if (histogramWanted && !said) Logger::Log("UNOFFICIAL AvgLuma: Histogram needs NVR DXVK; the exposure uses the D3D9 average");
		said = true;
		return false;
	}
	const bool histogram = histogramWanted && histogramPipeline && histogramBins;
	if (histogramWanted && !histogram && !TheSettingManager->SettingsMain.Main.AvgLumaCompute) return false;

	IDirect3DTexture9* color = TheTextureManager->RenderedTexture;
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
	if (histogram) push.histogram = D3DXVECTOR4(exposure->Settings.HistogramLowPercent / 100.0f, exposure->Settings.HistogramHighPercent / 100.0f,
		AvgLumaHistogramLog2Low, AvgLumaHistogramLog2High);

	NVR_EXT_RESOURCE_BINDING bindings[] = {
		{ 0, NVR_EXT_BIND_SAMPLED_IMAGE,  color,           0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_SAMPLED_IMAGE,  depth,           0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_STORAGE_BUFFER, histogram ? histogramBins : computePartials, 0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_STORAGE_BUFFER, stateIn,         0, 0, 0, FALSE },
		{ 2, NVR_EXT_BIND_STORAGE_BUFFER, stateOut,        0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_SAMPLER,        computeSampler,  0, 0, 0, FALSE },
	};
	NVR_EXT_DISPATCH_DESC dispatch = {};
	dispatch.pPipeline = histogram ? histogramPipeline : computePipeline;
	dispatch.BindingCount = _countof(bindings);
	dispatch.pBindings = bindings;
	dispatch.pPushData = &push;
	dispatch.PushDataSize = sizeof(push);

	DxvkCompute::PreLoad(bindings, _countof(bindings));
	push.phase = 0;
	dispatch.GroupsX = computeGroupsX; dispatch.GroupsY = computeGroupsY; dispatch.GroupsZ = 1;
	HRESULT result = histogram ? ext->ClearBuffer(histogramBins, 0, 0, 0) : S_OK;
	if (SUCCEEDED(result)) result = ext->Dispatch(&dispatch);
	if (SUCCEEDED(result)) {
		push.phase = 1;
		dispatch.GroupsX = 1; dispatch.GroupsY = 1;
		result = ext->Dispatch(&dispatch);
	}
	const bool checking = TheSettingManager->SettingsMain.Main.AvgLumaComputeCheck > 0;
	if (SUCCEEDED(result) && !checking)
		result = ext->CopyBufferToTexture(Textures.AvgLumaTexture, 0, 0, stateOut, AvgLumaPackedOffset);
	if (FAILED(result)) {
		Logger::Log("UNOFFICIAL AvgLuma: compute dispatch failed (%08lX); the D3D9 pass is used from now on", result);
		ComputeRelease();
		DxvkCompute::Fail("an AvgLuma dispatch failed");
		return false;
	}
	computeCurrent ^= 1;
	if (histogram) {
		if (++histogramFrames == 1) Logger::Log("UNOFFICIAL AvgLuma: histogram exposure running (%ux%u pixels, %u bins; the darkest %.1f %% and the brightest %.1f %% left out).",
			computeWidth, computeHeight, AvgLumaHistogramBins, exposure->Settings.HistogramLowPercent, exposure->Settings.HistogramHighPercent);
	}
	else if (++computeFrames == 1) Logger::Log("UNOFFICIAL AvgLuma: compute path running (exact average of %ux%u pixels).", computeWidth, computeHeight);
	return !checking;
}

void AvgLumaEffect::CheckCompute(IDirect3DDevice9* Device, bool computeRan) {
	const int every = TheSettingManager->SettingsMain.Main.AvgLumaComputeCheck;
	if (every <= 0 || !computeRan || !computeOk) return;
	if (++checkFrames % (UINT)every) return;

	struct { float cur[4]; UInt32 packed[2]; float average; float pad; } state = {};
	ID3D9NvrExtBuffer* stateOut = computeState[computeCurrent];
	if (FAILED(DxvkCompute::Extension()->ReadBuffer(stateOut, 0, sizeof(state), &state))) {
		Logger::Log("UNOFFICIAL AvgLuma check: ReadBuffer failed");
		return;
	}
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
	Logger::Log("UNOFFICIAL AvgLuma check (frame %u): compute exact average %.6f -> luma %.6f animated %.6f focus %.5f | D3D9 112 taps: luma %.6f animated %.6f focus %.5f | diff luma %+.6f",
		checkFrames, state.average, state.cur[0], state.cur[1], state.cur[2], tex[0], tex[1], tex[2], state.cur[0] - tex[0]);
}
