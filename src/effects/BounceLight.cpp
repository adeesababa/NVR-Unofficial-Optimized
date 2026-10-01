#include "BounceLight.h"
#include "../core/GpuProfiler.h"

void BounceLightEffect::UpdateConstants() {
}

void BounceLightEffect::UpdateSettings() {
	Constants.Data.x = TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "Strength");
	Constants.Data.y = TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "Radius");
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "DrawDistance");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "BlurRadius");
	quarterResolution = TheSettingManager->GetSettingI("Shaders.BounceLight.Main", "QuarterResolution") != 0;
}

void BounceLightEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_BounceLightData", &Constants.Data);
}

// Nothing at startup: the buffers (about 37 MB at 1440p for both resolutions) are made the first time the effect
// renders at a resolution (EnsureBuffers), so an install that never turns it on pays no video memory.
void BounceLightEffect::RegisterTextures() {
}

void BounceLightEffect::EnsureBuffers(bool quarter) {
	if (quarter && !quarterTried) {
		quarterTried = true;
		const int width = (TheRenderManager->width + 3) / 4;
		const int height = (TheRenderManager->height + 3) / 4;
		TheTextureManager->InitTexture("NVR_BounceBufferQuarter0", &quarterTexture[0], &quarterSurface[0], width, height, D3DFMT_A16B16G16R16F);
		TheTextureManager->InitTexture("NVR_BounceBufferQuarter1", &quarterTexture[1], &quarterSurface[1], width, height, D3DFMT_A16B16G16R16F);
		TheTextureManager->InitTexture("NVR_BouncePrepQuarter", &quarterPrepTexture, &quarterPrepSurface, width, height, D3DFMT_A32B32G32R32F);
	}
	if (!quarter && !halfTried) {
		halfTried = true;
		const int width = (TheRenderManager->width + 1) / 2;
		const int height = (TheRenderManager->height + 1) / 2;
		TheTextureManager->InitTexture("NVR_BounceBuffer0", &halfTexture[0], &halfSurface[0], width, height, D3DFMT_A16B16G16R16F);
		TheTextureManager->InitTexture("NVR_BounceBuffer1", &halfTexture[1], &halfSurface[1], width, height, D3DFMT_A16B16G16R16F);
		TheTextureManager->InitTexture("NVR_BouncePrep", &halfPrepTexture, &halfPrepSurface, width, height, D3DFMT_A32B32G32R32F);
	}
}

// Prepare, gather, blur X and blur Y at half or quarter resolution, then the combine at full resolution into the
// frame, the same way the dedicated ambient occlusion runs.
void BounceLightEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
	IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }
	EnsureBuffers(quarterResolution);
	if (quarterResolution && !quarterTexture[0]) EnsureBuffers(false); // quarter failed: half resolution instead
	D3DXHANDLE layoutHandle = Effect->GetParameterByName(NULL, "NVR_BounceLayout");
	D3DXTECHNIQUE_DESC description = {};
	IDirect3DTexture9* scene = TheTextureManager->RenderedTexture;
	const bool quarter = quarterResolution && quarterTexture[0] && quarterTexture[1] && quarterSurface[0] && quarterSurface[1] &&
		quarterPrepTexture && quarterPrepSurface;
	IDirect3DTexture9* const* lowTexture = quarter ? quarterTexture : halfTexture;
	IDirect3DSurface9* const* lowSurface = quarter ? quarterSurface : halfSurface;
	IDirect3DTexture9* prepTexture = quarter ? quarterPrepTexture : halfPrepTexture;
	IDirect3DSurface9* prepSurface = quarter ? quarterPrepSurface : halfPrepSurface;
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC half = {};
	if (!layoutHandle || !scene || !lowTexture[0] || !lowTexture[1] || !lowSurface[0] || !lowSurface[1] ||
		!prepTexture || !prepSurface || FAILED(Effect->GetTechniqueDesc(Effect->GetTechnique(0), &description)) ||
		description.Passes != 5 || FAILED(Device->GetViewport(&original)) || FAILED(lowSurface[0]->GetDesc(&half))) {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL BounceLight: buffers missing or BounceLight.fx.hlsl is not the matching version (5 passes), effect skipped.");
		reported = true;
		renderTime = 0;
		return;
	}
	auto timer = TimeLogger();
	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);
	IDirect3DSurface9* finalTarget = chained ? chain.Output() : RenderTarget;
	D3DVIEWPORT9 reduced = original;
	reduced.Width = half.Width;
	reduced.Height = half.Height;
	D3DXVECTOR4 layout((float)half.Width / original.Width, (float)half.Height / original.Height,
		1.0f / half.Width, 1.0f / half.Height);
	IDirect3DSurface9* depthSurface = nullptr;
	Device->GetDepthStencilSurface(&depthSurface);
	Device->SetDepthStencilSurface(nullptr);

	Effect->SetTechnique(Effect->GetTechnique(0));
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	// Separate timers per resolution, so the log tells the two apart.
	static GpuTimer halfTimers[5] = { GpuTimer("  Bounce prepare (half)"), GpuTimer("  Bounce gather (half)"),
		GpuTimer("  Bounce blur X (half)"), GpuTimer("  Bounce blur Y (half)"), GpuTimer("  Bounce combine (full)") };
	static GpuTimer quarterTimers[5] = { GpuTimer("  Bounce prepare (quarter)"), GpuTimer("  Bounce gather (quarter)"),
		GpuTimer("  Bounce blur X (quarter)"), GpuTimer("  Bounce blur Y (quarter)"), GpuTimer("  Bounce combine (full, from quarter)") };
	GpuTimer* passTimers = quarter ? quarterTimers : halfTimers;
	UINT passes = 0;
	HRESULT result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		for (UINT p = 0; p < 5 && SUCCEEDED(result); ++p) {
			const bool combine = p == 4;
			// Prepare writes the packed buffer; the gather reads it and writes 0, blur X reads 0 and writes 1,
			// blur Y reads 1 and writes 0, the combine reads 0.
			IDirect3DSurface9* destination = combine ? finalTarget : p == 0 ? prepSurface : lowSurface[p == 2 ? 1 : 0];
			Device->SetTexture(5, nullptr);
			Device->SetTexture(6, nullptr);
			result = Device->SetRenderTarget(0, destination);
			if (SUCCEEDED(result)) result = Device->SetViewport(combine ? &original : &reduced);
			if (SUCCEEDED(result)) result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			if (p == 1) Device->SetTexture(6, prepTexture);
			else if (p == 2 || p == 4) Device->SetTexture(5, lowTexture[0]);
			else if (p == 3) Device->SetTexture(5, lowTexture[1]);
			Device->SetTexture(2, scene); // TESR_SourceBuffer slot: the current image
			{
				GpuProfileScope gpu(passTimers[p], Device);
				result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			}
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	Device->SetTexture(5, nullptr);
	Device->SetTexture(6, nullptr);
	Device->SetTexture(2, TheTextureManager->SourceTexture);
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	if (SUCCEEDED(result)) {
		if (chained) chain.Commit();
		else Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	}
	else {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL BounceLight: render failed (%08lx).", result);
		reported = true;
	}
	renderTime = timer.LogTime("BounceLight::Render");
}
