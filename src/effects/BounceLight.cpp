#include "BounceLight.h"
#include "BounceLightMath.h"
#include "../core/GpuProfiler.h"

void BounceLightEffect::UpdateConstants() {
}

void BounceLightEffect::UpdateSettings() {
	Constants.Data.x = TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "Strength");
	Constants.Data.y = TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "Radius");
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "DrawDistance");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "BlurRadius");
	quarterResolution = TheSettingManager->GetSettingI("Shaders.BounceLight.Main", "QuarterResolution") != 0;
	// UNOFFICIAL (a key missing from an older settings file reads 0: off, as before).
	offScreenLight = std::clamp(TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "OffScreenLight"), 0.0f, 4.0f);
	multiBounce = std::clamp(TheSettingManager->GetSettingF("Shaders.BounceLight.Main", "MultiBounce"), 0.0f, 2.0f);
	temporal = TheSettingManager->GetSettingI("Shaders.BounceLight.Main", "Temporal") != 0;
	halfSamples = TheSettingManager->GetSettingI("Shaders.BounceLight.Main", "HalfSamples") != 0;
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

// UNOFFICIAL, Temporal: two more low-resolution buffers (last frame's gather is read from one while this frame's goes to the
// other; about 1.8 MB each at a quarter of 1440p, 7.4 MB at half), cleared so nothing undefined is ever read back.
void BounceLightEffect::EnsureHistory(bool quarter) {
	bool& tried = quarter ? quarterHistoryTried : halfHistoryTried;
	if (tried) return;
	tried = true;
	const int width = quarter ? (TheRenderManager->width + 3) / 4 : (TheRenderManager->width + 1) / 2;
	const int height = quarter ? (TheRenderManager->height + 3) / 4 : (TheRenderManager->height + 1) / 2;
	IDirect3DTexture9** textures = quarter ? quarterHistory : halfHistory;
	IDirect3DSurface9** surfaces = quarter ? quarterHistorySurface : halfHistorySurface;
	TheTextureManager->InitTexture(quarter ? "NVR_BounceHistoryQuarter0" : "NVR_BounceHistory0", &textures[0], &surfaces[0], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture(quarter ? "NVR_BounceHistoryQuarter1" : "NVR_BounceHistory1", &textures[1], &surfaces[1], width, height, D3DFMT_A16B16G16R16F);
	for (int i = 0; i < 2; i++) if (surfaces[i]) TheRenderManager->device->ColorFill(surfaces[i], NULL, D3DCOLOR_ARGB(0, 0, 0, 0));
}

// The game's colours are display-encoded (sRGB); the bounce works in linear light.
static float BounceLinear(float c) {
	c = std::clamp(c, 0.0f, 1.0f);
	return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

// Prepare, gather, blur X and blur Y at half or quarter resolution, then the combine at full resolution into the
// frame, the same way the dedicated ambient occlusion runs.
void BounceLightEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
	IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; lastValid = historyValid = false; return; }
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
	// UNOFFICIAL: the technique for the options on (the first one, as before, when all are off), and the history buffers
	// for Temporal. Anything missing (an older BounceLight.fx.hlsl, a buffer that could not be made): a step back, logged once.
	D3DXHANDLE technique = Effect->GetTechnique(0);
	IDirect3DTexture9* const* historyTexture = nullptr;
	IDirect3DSurface9* const* historySurface = nullptr;
	const bool extras = offScreenLight > 0.0f || multiBounce > 0.0f || temporal;
	D3DXHANDLE temporalHandle = NULL, ambientHandle = NULL, reprojectHandle = NULL;
	if (extras) {
		temporalHandle = Effect->GetParameterByName(NULL, "NVR_BounceTemporal");
		ambientHandle = Effect->GetParameterByName(NULL, "NVR_BounceAmbient");
		reprojectHandle = Effect->GetParameterByName(NULL, "NVR_BounceReproject");
		D3DXHANDLE wanted = NULL;
		if (temporal) {
			EnsureHistory(quarter);
			IDirect3DTexture9* const* textures = quarter ? quarterHistory : halfHistory;
			IDirect3DSurface9* const* surfaces = quarter ? quarterHistorySurface : halfHistorySurface;
			wanted = Effect->GetTechniqueByName(halfSamples ? "BounceLightTemporalHalf" : "BounceLightTemporal");
			if (wanted && textures[0] && textures[1] && surfaces[0] && surfaces[1]) { historyTexture = textures; historySurface = surfaces; }
			else wanted = NULL;
		}
		if (!wanted) wanted = Effect->GetTechniqueByName("BounceLightPlus");
		if (wanted && temporalHandle && ambientHandle && reprojectHandle) technique = wanted;
		else historyTexture = nullptr;
		static D3DXHANDLE reportedTechnique = NULL;
		if (technique != reportedTechnique) {
			reportedTechnique = technique;
			if (technique == Effect->GetTechnique(0))
				Logger::Log("UNOFFICIAL BounceLight: OffScreenLight, MultiBounce and Temporal need the BounceLight.fx.hlsl that came with this DLL; running without them.");
			else Logger::Log("UNOFFICIAL BounceLight: off-screen light %.2f, extra bounces %.2f, last frame's bounce reused: %s.",
				offScreenLight, multiBounce, historyTexture ? (halfSamples ? "yes, 4 samples a frame" : "yes, 8 samples a frame") : "no");
		}
	}
	const bool plus = technique != Effect->GetTechnique(0);
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC half = {};
	if (!layoutHandle || !scene || !lowTexture[0] || !lowTexture[1] || !lowSurface[0] || !lowSurface[1] ||
		!prepTexture || !prepSurface || FAILED(Effect->GetTechniqueDesc(technique, &description)) ||
		description.Passes != 5 || FAILED(Device->GetViewport(&original)) || FAILED(lowSurface[0]->GetDesc(&half))) {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL BounceLight: buffers missing or BounceLight.fx.hlsl is not the matching version (5 passes), effect skipped.");
		reported = true;
		renderTime = 0;
		lastValid = historyValid = false;
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

	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	// deferCombine: passes 0-3 only (the combine is SpecularEffect::RenderWithBounce's); combineOnly: pass 4 only, from them.
	const bool onlyCombine = combineOnly && deferredReady && deferredTexture == lowTexture[0];
	// UNOFFICIAL: what last frame left can be used if its low-resolution passes ran at this resolution a moment ago, in
	// the same cell (a load or a door means another place); the depth test in the shader catches the rest.
	const double now = TheFrameRateManager->Time;
	const bool continuous = lastValid && lastQuarter == quarter && now - lastTime >= 0.0 && now - lastTime < 0.5 &&
		!TheShaderManager->GameState.isCellChanged;
	const bool useHistory = historyTexture != nullptr;
	const int writeIndex = historyIndex ^ 1; // this frame's gather goes to the history buffer last frame's did not
	if (plus && !onlyCombine) {
		static const float HistoryShare = 0.9f; // last frame's share for a spot that has not moved (less as it moves)
		D3DXVECTOR4 temporalData(useHistory && continuous && historyValid ? HistoryShare : 0.0f, continuous ? multiBounce : 0.0f,
			(float)(frameNumber % 1024), 0.0f);
		// The ambient light: the weather's outdoors, the cell's own indoors (as the lit particles take it); as a mid-grey
		// surface (0.25 in linear light) would send it.
		D3DXVECTOR4 ambient = TheShaderManager->ShaderConst.sunAmbient;
		if (!TheShaderManager->GameState.isExterior && Player && Player->parentCell && Player->parentCell->IsInterior() &&
			Player->parentCell->lighting)
			ambient = Player->parentCell->lighting->ambient.toD3DXVECTOR4();
		D3DXVECTOR4 ambientData(0.25f * BounceLinear(ambient.x), 0.25f * BounceLinear(ambient.y), 0.25f * BounceLinear(ambient.z), offScreenLight);
		D3DXMATRIX reproject;
		D3DXMatrixIdentity(&reproject);
		if (continuous) BounceLightReprojection(TheRenderManager->viewMatrix, TheRenderManager->InvViewMatrix, lastView, lastInvView, lastProj, &reproject);
		Effect->SetVector(temporalHandle, &temporalData);
		Effect->SetVector(ambientHandle, &ambientData);
		Effect->SetMatrix(reprojectHandle, &reproject);
	}
	// Separate timers per resolution, so the log tells the two apart.
	static GpuTimer halfTimers[5] = { GpuTimer("  Bounce prepare (half)"), GpuTimer("  Bounce gather (half)"),
		GpuTimer("  Bounce blur X (half)"), GpuTimer("  Bounce blur Y (half)"), GpuTimer("  Bounce combine (full)") };
	static GpuTimer quarterTimers[5] = { GpuTimer("  Bounce prepare (quarter)"), GpuTimer("  Bounce gather (quarter)"),
		GpuTimer("  Bounce blur X (quarter)"), GpuTimer("  Bounce blur Y (quarter)"), GpuTimer("  Bounce combine (full, from quarter)") };
	GpuTimer* passTimers = quarter ? quarterTimers : halfTimers;
	const UINT firstPass = onlyCombine ? 4 : 0, endPass = deferCombine ? 4 : 5;
	UINT passes = 0;
	HRESULT result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		for (UINT p = firstPass; p < endPass && SUCCEEDED(result); ++p) {
			const bool combine = p == 4;
			// Prepare writes the packed buffer; the gather reads it and writes 0, blur X reads 0 and writes 1,
			// blur Y reads 1 and writes 0, the combine reads 0.
			// UNOFFICIAL: with the other techniques Prepare also reads 0 (last frame's result: the extra bounces); with
			// Temporal the gather also reads last frame's history buffer and writes the other one, which blur X reads.
			IDirect3DSurface9* destination = combine ? finalTarget : p == 0 ? prepSurface :
				p == 1 && useHistory ? historySurface[writeIndex] : lowSurface[p == 2 ? 1 : 0];
			Device->SetTexture(5, nullptr);
			Device->SetTexture(6, nullptr);
			if (plus) Device->SetTexture(7, nullptr);
			result = Device->SetRenderTarget(0, destination);
			if (SUCCEEDED(result)) result = Device->SetViewport(combine ? &original : &reduced);
			if (SUCCEEDED(result)) result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			if (p == 0 && plus) Device->SetTexture(7, lowTexture[0]); // NVR_BounceHistory: last frame's result
			if (p == 1) {
				Device->SetTexture(6, prepTexture);
				if (useHistory) Device->SetTexture(7, historyTexture[historyIndex]); // NVR_BounceHistory: last frame's gather
			}
			else if (p == 2 && useHistory) Device->SetTexture(5, historyTexture[writeIndex]);
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
	if (plus) Device->SetTexture(7, nullptr);
	Device->SetTexture(2, TheTextureManager->SourceTexture);
	// UNOFFICIAL: what this frame's low-resolution passes left is next frame's "last frame".
	if (SUCCEEDED(result) && firstPass == 0) {
		lastView = TheRenderManager->viewMatrix;
		lastInvView = TheRenderManager->InvViewMatrix;
		lastProj = TheRenderManager->projMatrix;
		lastValid = true;
		lastQuarter = quarter;
		lastTime = now;
		frameNumber++;
		historyValid = useHistory;
		if (useHistory) historyIndex = writeIndex;
	}
	else if (FAILED(result)) lastValid = historyValid = false;
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	if (SUCCEEDED(result)) {
		if (endPass == 4) {
			deferredReady = true;
			deferredTexture = lowTexture[0];
			deferredLayout = layout;
		}
		else if (chained) chain.Commit();
		else Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	}
	else {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL BounceLight: render failed (%08lx).", result);
		reported = true;
	}
	renderTime = timer.LogTime("BounceLight::Render");
}
