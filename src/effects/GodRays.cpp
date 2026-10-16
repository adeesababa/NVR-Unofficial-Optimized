#include "GodRays.h"
#include "../core/GpuProfiler.h"

void GodRaysEffect::UpdateConstants() {
	Constants.Data.z = std::lerp(nightMult, dayMult, TheShaderManager->GameState.transitionCurve);
	Constants.Ray.w = rayVisibility * sunGlareEnabled ? TheShaderManager->ShaderConst.sunGlare : 1.0;
}

void GodRaysEffect::UpdateSettings(){
	dayMult = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "DayMultiplier");
	nightMult = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "NightMultiplier");
	sunGlareEnabled = TheSettingManager->GetSettingI("Shaders.GodRays.Main", "SunGlareEnabled");
	skipEmpty = TheSettingManager->GetSettingI("Shaders.GodRays.Main", "SkipEmpty") != 0;	// UNOFFICIAL 2026-10-16

	Constants.Ray.x = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "RayIntensity");
	Constants.Ray.y = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "RayLength");
	Constants.Ray.z = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "RayDensity");
	rayVisibility = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "RayVisibility");
	Constants.RayColor.x = TheSettingManager->GetSettingF("Shaders.GodRays.Coloring", "RayR");
	Constants.RayColor.y = TheSettingManager->GetSettingF("Shaders.GodRays.Coloring", "RayG");
	Constants.RayColor.z = TheSettingManager->GetSettingF("Shaders.GodRays.Coloring", "RayB");
	Constants.RayColor.w = TheSettingManager->GetSettingF("Shaders.GodRays.Coloring", "Saturate");
	Constants.Data.x = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "LightShaftPasses");
	Constants.Data.y = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "Luminance");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "TimeEnabled");
}

void GodRaysEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_GodRaysRay", &Constants.Ray);
	TheShaderManager->RegisterConstant("TESR_GodRaysRayColor", &Constants.RayColor);
	TheShaderManager->RegisterConstant("TESR_GodRaysData", &Constants.Data);
}

void GodRaysEffect::RegisterTextures() {
	const int width = MaxQuality::Reduced(TheRenderManager->width, 2);
	const int height = MaxQuality::Reduced(TheRenderManager->height, 2);
	TheTextureManager->InitTexture("NVR_GodRaysBuffer0", &raysTexture[0], &raysSurface[0], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GodRaysBuffer1", &raysTexture[1], &raysSurface[1], width, height, D3DFMT_A16B16G16R16F);
	const int lowWidth = MaxQuality::Reduced(TheRenderManager->width, 4);
	const int lowHeight = MaxQuality::Reduced(TheRenderManager->height, 4);
	TheTextureManager->InitTexture("NVR_GodRaysBufferLow0", &raysTextureLow[0], &raysSurfaceLow[0], lowWidth, lowHeight, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_GodRaysBufferLow1", &raysTextureLow[1], &raysSurfaceLow[1], lowWidth, lowHeight, D3DFMT_A16B16G16R16F);

	// UNOFFICIAL SkipEmpty: the reduction chain for each ray size, each step an eighth (rounded up) of the one before, down to 1 x 1.
	static const char* names[2][4] = { { "NVR_GodRaysMax0", "NVR_GodRaysMax1", "NVR_GodRaysMax2", "NVR_GodRaysMax3" },
		{ "NVR_GodRaysMaxLow0", "NVR_GodRaysMaxLow1", "NVR_GodRaysMaxLow2", "NVR_GodRaysMaxLow3" } };
	const UINT sizes[2][2] = { { (UINT)width, (UINT)height }, { (UINT)lowWidth, (UINT)lowHeight } };
	for (int set = 0; set < 2; set++) {
		UINT w = sizes[set][0], h = sizes[set][1];
		maxSteps[set] = 0;
		while ((w > 1 || h > 1) && maxSteps[set] < 4) {
			w = (w + 7) / 8; h = (h + 7) / 8;
			const int i = maxSteps[set]++;
			maxWidth[set][i] = w; maxHeight[set][i] = h;
			TheTextureManager->InitTexture(names[set][i], &maxTexture[set][i], &maxSurface[set][i], w, h, D3DFMT_A16B16G16R16F);
		}
		if (w > 1 || h > 1) maxSteps[set] = 0;	// a screen too large for four steps: no skip
	}
}

// UNOFFICIAL SkipEmpty: the light mask's maximum down to 1 x 1 (the last texture of the set). False if anything failed: the blurs
// then run as before.
bool GodRaysEffect::RunReduce(IDirect3DDevice9* Device, IDirect3DTexture9* source, UINT width, UINT height, int set) {
	D3DXHANDLE technique = TechniqueHandle(techReduce, "GodRaysMaxReduce");
	D3DXHANDLE reduceHandle = ParameterHandle(paramReduce, "NVR_GodRaysReduce");
	if (!technique || !reduceHandle || maxSteps[set] < 1 || !source) return false;
	for (int i = 0; i < maxSteps[set]; i++) if (!maxTexture[set][i] || !maxSurface[set][i]) return false;
	static GpuTimer reduceTimer("  GR max reduce");
	GpuProfileScope gpu(reduceTimer, Device);
	UINT passes = 0;
	HRESULT result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (FAILED(result)) return false;
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	IDirect3DTexture9* from = source;
	UINT w = width, h = height;
	for (int i = 0; i < maxSteps[set] && SUCCEEDED(result); i++) {
		const D3DXVECTOR4 inverse(1.0f / w, 1.0f / h, 0.0f, 0.0f);
		Effect->SetVector(reduceHandle, &inverse);
		const D3DVIEWPORT9 viewport = { 0, 0, maxWidth[set][i], maxHeight[set][i], 0.0f, 1.0f };
		result = Device->SetRenderTarget(0, maxSurface[set][i]);
		if (SUCCEEDED(result)) result = Device->SetViewport(&viewport);
		if (SUCCEEDED(result)) result = Effect->BeginPass(0);
		if (FAILED(result)) break;
		Device->SetTexture(5, from);
		result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
		const HRESULT endResult = Effect->EndPass();
		if (SUCCEEDED(result)) result = endResult;
		Device->SetTexture(5, nullptr);
		from = maxTexture[set][i]; w = maxWidth[set][i]; h = maxHeight[set][i];
	}
	Effect->End();
	return SUCCEEDED(result);
}

bool GodRaysEffect::ShouldRender() {
	return TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater && TheShaderManager->GameState.dayLight > 0.5;
}

/*
* Five half-resolution (or, with GodRaysLowRes, quarter-resolution) passes ping-pong between two
* dedicated targets, then a single full-resolution combine. The shaders work in [0, 1] UVs and take
* the target size from NVR_GodRaysLayout, so both sizes use the same technique. Relies on
* RenderedSurface already holding the current scene, which every preceding effect guarantees by
* copying its result there. Returns false without touching the render target if the dedicated
* path is unavailable.
*/
bool GodRaysEffect::RenderDedicated(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface) {
	if (dedicatedRenderFailed) return false;
	const bool lowRes = TheSettingManager->SettingsMain.Main.GodRaysLowRes &&
		raysTextureLow[0] && raysTextureLow[1] && raysSurfaceLow[0] && raysSurfaceLow[1];
	IDirect3DTexture9** textures = lowRes ? raysTextureLow : raysTexture;
	IDirect3DSurface9** surfaces = lowRes ? raysSurfaceLow : raysSurface;
	const UINT divisor = MaxQuality::Divisor(lowRes ? 4 : 2);	// UNOFFICIAL: 1 with Max Quality
	D3DXHANDLE technique = TechniqueHandle(techDedicated, "DedicatedGodRays");
	D3DXHANDLE layoutHandle = ParameterHandle(paramLayout, "NVR_GodRaysLayout");
	D3DXTECHNIQUE_DESC description = {};
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC target = {}, scratch = {}, rays = {};
	if (!technique || !layoutHandle || FAILED(Effect->GetTechniqueDesc(technique, &description)) ||
		description.Passes != 6 || !RenderedSurface || !textures[0] || !textures[1] ||
		!surfaces[0] || !surfaces[1] ||
		FAILED(Device->GetViewport(&original)) || FAILED(RenderTarget->GetDesc(&target)) ||
		FAILED(RenderedSurface->GetDesc(&scratch)) || FAILED(surfaces[0]->GetDesc(&rays)) ||
		original.X || original.Y ||
		original.Width != target.Width || original.Height != target.Height ||
		scratch.Width != target.Width || scratch.Height != target.Height ||
		rays.Width != (target.Width + divisor - 1) / divisor || rays.Height != (target.Height + divisor - 1) / divisor ||
		target.MultiSampleType != D3DMULTISAMPLE_NONE)
		return false;

	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);
	IDirect3DSurface9* finalTarget = chained ? chain.Output() : RenderTarget;
	D3DVIEWPORT9 reduced = original;
	reduced.Width = rays.Width;
	reduced.Height = rays.Height;
	D3DXVECTOR4 layout((float)reduced.Width / original.Width,
		(float)reduced.Height / original.Height, 1.0f / reduced.Width, 1.0f / reduced.Height);
	IDirect3DSurface9* depthSurface = nullptr;
	DWORD oldScissorEnabled = FALSE;
	HRESULT result = Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissorEnabled);
	if (SUCCEEDED(result)) result = Device->GetDepthStencilSurface(&depthSurface);
	// UNOFFICIAL ReShadeDepthCompat: leave the game's depth-stencil bound (a single smaller target is legal with it).
	if (SUCCEEDED(result) && !ReShadeIntegration::KeepDepthBound()) result = Device->SetDepthStencilSurface(nullptr);
	if (FAILED(result)) {
		if (depthSurface) depthSurface->Release();
		dedicatedRenderFailed = true;
		return false;
	}

	// pass -> destination: 0 SkyMask->A, 1 LightMask->B, 2 Blur1->A, 3 Blur2->B, 4 Blur3->A, 5 Combine->RT
	// UNOFFICIAL SkipEmpty: passes 0-1, then the light mask's maximum (RunReduce, its own technique), then passes 2-5.
	const int set = lowRes ? 1 : 0;
	D3DXHANDLE skipHandle = ParameterHandle(paramSkip, "NVR_GodRaysSkip");
	bool maskReduced = false;
	UINT passes = 0;
	for (int stage = 0; stage < 2 && SUCCEEDED(result); stage++) {
	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	if (skipHandle) { const D3DXVECTOR4 skip(maskReduced ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f); Effect->SetVector(skipHandle, &skip); }
	result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		static GpuTimer passTimers[6] = { GpuTimer("  GR sky mask (half)"), GpuTimer("  GR light mask (half)"),
			GpuTimer("  GR blur 1 (half)"), GpuTimer("  GR blur 2 (half)"), GpuTimer("  GR blur 3 (half)"),
			GpuTimer("  GR combine (full)") };
		const UINT first = stage == 0 ? 0 : 2, last = stage == 0 ? (passes < 2 ? passes : 2) : passes;
		for (UINT p = first; p < last && SUCCEEDED(result); ++p) {
			GpuProfileScope gpuPass(passTimers[p < 6 ? p : 5], Device);
			const bool combine = p == passes - 1;
			Device->SetTexture(5, nullptr);
			result = Device->SetRenderTarget(0, combine ? finalTarget : surfaces[p & 1]);
			if (SUCCEEDED(result)) result = Device->SetViewport(combine ? &original : &reduced);
			if (SUCCEEDED(result)) result = Device->SetRenderState(D3DRS_SCISSORTESTENABLE, combine ? oldScissorEnabled : FALSE);
			if (FAILED(result)) break;
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			if (p > 0) Device->SetTexture(5, textures[combine ? 0 : (p - 1) & 1]);
			if (p >= 2) Device->SetTexture(6, maskReduced ? maxTexture[set][maxSteps[set] - 1] : nullptr);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	// After the light mask (in textures[1]): its maximum, when SkipEmpty is on. A failure only means no skip this frame.
	if (stage == 0 && SUCCEEDED(result) && skipEmpty && skipHandle)
		maskReduced = RunReduce(Device, textures[1], reduced.Width, reduced.Height, set);
	}
	Device->SetTexture(6, nullptr);
	Device->SetTexture(5, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetViewport(&original);
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissorEnabled);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	if (SUCCEEDED(result)) {
		if (chained) chain.Commit();
		else result = Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	}
	if (FAILED(result)) {
		// RenderedSurface is only written by the final copy, so it still holds the input.
		Device->StretchRect(RenderedSurface, NULL, RenderTarget, NULL, D3DTEXF_NONE);
		dedicatedRenderFailed = true;
		Logger::Log("Dedicated god rays failed (%08lx); using packed path until restart.", result);
		return false;
	}
	static bool reported[2] = {};
	if (!reported[lowRes]) {
		Logger::Log("UNOFFICIAL dedicated god rays active: %ux%u A16B16G16R16F ping-pong, six passes%s.",
			reduced.Width, reduced.Height, lowRes ? " (GodRaysLowRes)" : "");
		reported[lowRes] = true;
	}
	return true;
}

void GodRaysEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
	IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }

	{
		auto timer = TimeLogger();
		if (RenderDedicated(Device, RenderTarget, RenderedSurface)) {
			renderTime = timer.LogTime("GodRays::DedicatedRender");
			return;
		}
	}

	D3DXHANDLE technique = Effect->GetTechnique(techniqueIndex);
	D3DXTECHNIQUE_DESC description = {};
	D3DSURFACE_DESC target = {}, scratch = {};
	DWORD oldScissorEnabled = FALSE;
	RECT oldScissor = {};
	if (packedRenderFailed || !technique || !RenderedSurface || !SourceBuffer ||
		FAILED(Effect->GetTechniqueDesc(technique, &description)) || description.Passes != 6 ||
		FAILED(RenderTarget->GetDesc(&target)) || FAILED(RenderedSurface->GetDesc(&scratch)) ||
		target.Width != scratch.Width || target.Height != scratch.Height ||
		FAILED(Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissorEnabled)) ||
		FAILED(Device->GetScissorRect(&oldScissor))) {
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}

	auto timer = TimeLogger();
	TheShaderManager->Chain.Sync(); // the packed path reads the render target itself
	RECT packed = {0, 0, (LONG)(target.Width / 2), (LONG)(target.Height / 2)};
	D3DRECT packedClear = {packed.left, packed.top, packed.right, packed.bottom};
	HRESULT result = Device->StretchRect(RenderTarget, NULL, SourceBuffer, NULL, D3DTEXF_LINEAR);
	if (SUCCEEDED(result)) result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) SetCT();
	UINT passes = 0;
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		for (UINT p = 0; p < passes && SUCCEEDED(result); ++p) {
			const bool combine = p == passes - 1;
			if (combine) {
				result = Device->SetScissorRect(&oldScissor);
				if (SUCCEEDED(result)) result = Device->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissorEnabled);
				if (SUCCEEDED(result) && ClearRenderTarget)
					result = Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
			}
			else {
				result = Device->SetScissorRect(&packed);
				if (SUCCEEDED(result)) result = Device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
				if (SUCCEEDED(result) && ClearRenderTarget)
					result = Device->Clear(1, &packedClear, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
			}
			if (FAILED(result)) break;
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
			if (SUCCEEDED(result)) result = Device->StretchRect(RenderTarget, combine ? NULL : &packed,
				RenderedSurface, combine ? NULL : &packed, D3DTEXF_LINEAR);
		}
		Effect->End();
	}
	Device->SetScissorRect(&oldScissor);
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissorEnabled);

	if (FAILED(result)) {
		Device->StretchRect(SourceBuffer, NULL, RenderTarget, NULL, D3DTEXF_NONE);
		Device->StretchRect(SourceBuffer, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
		packedRenderFailed = true;
		Logger::Log("Packed god rays failed (%08lx); using legacy path until restart.", result);
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL packed god rays active: %ux%u intermediate region.", packed.right, packed.bottom);
		reported = true;
	}
	renderTime = timer.LogTime("GodRays::PackedRender");
}
