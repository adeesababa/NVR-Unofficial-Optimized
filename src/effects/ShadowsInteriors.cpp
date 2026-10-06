#include "ShadowsInteriors.h"
#include "../core/GpuProfiler.h"

void ShadowsInteriorsEffect::UpdateConstants() {}
void ShadowsInteriorsEffect::UpdateSettings() {}
void ShadowsInteriorsEffect::RegisterConstants() {}


void ShadowsInteriorsEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }

	FrameChain& chain = TheShaderManager->Chain;
	SunShadowsEffect* scratch = TheShaderManager->Effects.SunShadows;
	IDirect3DTexture9* shadowTexture = TheShaderManager->Effects.ShadowsExteriors->Textures.ShadowPassTexture;
	D3DXHANDLE technique = dedicatedFailed ? NULL : Effect->GetTechniqueByName("DedicatedInteriorShadows");
	D3DXTECHNIQUE_DESC description = {};
	D3DSURFACE_DESC target = {}, blur = {};
	if (!technique || !scratch || !shadowTexture || !scratch->scratchTexture[0] || !scratch->scratchTexture[1] ||
		!scratch->scratchSurface[0] || !scratch->scratchSurface[1] || !RenderedSurface ||
		FAILED(Effect->GetTechniqueDesc(technique, &description)) || description.Passes != 3 ||
		FAILED(RenderTarget->GetDesc(&target)) || FAILED(scratch->scratchSurface[0]->GetDesc(&blur)) ||
		blur.Width != target.Width || blur.Height != target.Height ||
		!chain.Owns(RenderTarget, RenderedSurface)) {
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}

	auto timer = TimeLogger();
	HRESULT result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) SetCT();
	UINT passes = 0;
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		static GpuTimer passTimers[3] = { GpuTimer("  Interior shadow blur H"), GpuTimer("  Interior shadow blur V"),
			GpuTimer("  Interior shadow apply") };
		IDirect3DBaseTexture9* source = shadowTexture;
		for (UINT p = 0; p < 3 && SUCCEEDED(result); ++p) {
			GpuProfileScope gpuPass(passTimers[p], Device);
			const bool combine = p == 2;
			Device->SetTexture(4, nullptr);
			result = Device->SetRenderTarget(0, combine ? chain.Output() : scratch->scratchSurface[p]);
			if (FAILED(result)) break;
			if (!combine) Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0L);
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			RebindSlotTextures();
			Device->SetTexture(4, source);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
			source = combine ? nullptr : scratch->scratchTexture[p];
		}
		Effect->End();
	}
	Device->SetTexture(4, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	if (FAILED(result)) {
		dedicatedFailed = true;
		Logger::Log("Dedicated interior shadows failed (%08lx); using the generic path until restart.", result);
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	chain.Commit();

	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL dedicated interior shadows active: blur on two G16R16 targets, one HDR pass, no frame copy.");
		reported = true;
	}
	renderTime = timer.LogTime("ShadowsInteriors::Dedicated");
}
