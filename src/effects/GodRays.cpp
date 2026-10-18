#include "GodRays.h"
#include "../core/GpuProfiler.h"
#include "../core/TestTools.h"	// NVR_TEST_TOOLS (off in release builds)

void GodRaysEffect::UpdateConstants() {
	Constants.Data.z = std::lerp(nightMult, dayMult, TheShaderManager->GameState.transitionCurve);
	Constants.Ray.w = rayVisibility * sunGlareEnabled ? TheShaderManager->ShaderConst.sunGlare : 1.0;
}

void GodRaysEffect::UpdateSettings(){
	dayMult = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "DayMultiplier");
	nightMult = TheSettingManager->GetSettingF("Shaders.GodRays.Main", "NightMultiplier");
	sunGlareEnabled = TheSettingManager->GetSettingI("Shaders.GodRays.Main", "SunGlareEnabled");
	skipEmpty = TheSettingManager->GetSettingI("Shaders.GodRays.Main", "SkipEmpty") != 0;	// UNOFFICIAL 2026-10-16
	skipIdle = TheSettingManager->GetSettingI("Shaders.GodRays.Main", "SkipIdle") != 0;	// UNOFFICIAL 2026-10-18
	probeLevel = TheSettingManager->GetSettingI("Shaders.GodRays.Main", "ProbeLog");

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

static float HalfToFloat(UInt16 h) {
	const int e = (h >> 10) & 31, m = h & 1023;
	float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexpf((float)(m + 1024), e - 25);
	return (h & 0x8000) ? -v : v;
}

#if NVR_TEST_TOOLS
// The largest channel of the last 1 x 1 reduction result (one texel read back: a test-only GPU stall).
static bool ReadMax(IDirect3DDevice9* Device, IDirect3DSurface9* surface, float& value) {
	static IDirect3DSurface9* read = nullptr;
	if (!surface) return false;
	if (!read && FAILED(Device->CreateOffscreenPlainSurface(1, 1, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &read, NULL))) return false;
	if (FAILED(Device->GetRenderTargetData(surface, read))) return false;
	D3DLOCKED_RECT locked = {};
	if (FAILED(read->LockRect(&locked, NULL, D3DLOCK_READONLY))) return false;
	const UInt16* p = (const UInt16*)locked.pBits;
	value = (std::max)(HalfToFloat(p[0]), (std::max)(HalfToFloat(p[1]), HalfToFloat(p[2])));
	read->UnlockRect();
	return true;
}
#endif

/*
* DIAGNOSTIC [Shaders.GodRays.Main] ProbeLog 1 (grnoon 2026-10-18; development builds only, release builds leave it out): every 8th frame,
* the largest value of the sky mask (pass 0: scene + glare on sky pixels) and of the light mask (pass 1, what the blurs spread) is read
* back and logged with the sun's place: the light mask is exactly 0 (the rays then add exactly nothing) when no sky pixel's five-tap
* average clears the threshold, which at a high sun is lerp(2, 0, sunHeight^8) ~ 2 in linear light. skyMax <= threshold proves it.
*/
bool GodRaysEffect::Probe(IDirect3DDevice9* Device, IDirect3DTexture9* const* textures, UINT width, UINT height, int set, bool lowRes) {
#if NVR_TEST_TOOLS
	static int check = 0;
	static unsigned frame = 0;
	if ((frame % 60) == 0) check = TheSettingManager->GetSettingI("Shaders.GodRays.Main", "ProbeLog");
	frame++;
	if (check <= 0 || (frame % 8) != 0 || maxSteps[set] < 1) return false;
	float skyMax = -1.0f, maskMax = -1.0f;
	IDirect3DSurface9* last = maxSurface[set][maxSteps[set] - 1];
	if (RunReduce(Device, textures[0], width, height, set)) ReadMax(Device, last, skyMax);
	if (RunReduce(Device, textures[1], width, height, set)) ReadMax(Device, last, maskMax);
	const ShaderConstants& c = TheShaderManager->ShaderConst;
	const D3DXMATRIX& P = TheRenderManager->projMatrix;
	const float z = c.ViewSpaceLightDir.z;
	const float ndcX = z > 1e-4f ? c.ViewSpaceLightDir.x * P._11 / z : 99.0f, ndcY = z > 1e-4f ? c.ViewSpaceLightDir.y * P._22 / z : 99.0f;
	const float sunZ = c.SunPosition.z, sunHeight = 1.0f - (sunZ > 0.0f ? sunZ : 0.0f);
	const float threshold = 2.0f * (1.0f - powf(fabsf(sunHeight), 8.0f));
	const float azimuth = atan2f(c.SunPosition.x, c.SunPosition.y) * 57.29578f, elevation = asinf(sunZ < -1.0f ? -1.0f : sunZ > 1.0f ? 1.0f : sunZ) * 57.29578f;
	Logger::Log("GODRAYS PROBE: hour %.2f sun az %.1f el %.1f view (%.3f %.3f %.3f) ndc (%.2f %.2f) %s skyMax %.4f maskMax %.6f threshold %.3f %s",
		c.GameTime.y, azimuth, elevation, c.ViewSpaceLightDir.x, c.ViewSpaceLightDir.y, c.ViewSpaceLightDir.z, ndcX, ndcY,
		z <= 0.0f ? "behind" : (fabsf(ndcX) <= 1.0f && fabsf(ndcY) <= 1.0f) ? "ON-SCREEN" : "off-screen", skyMax, maskMax, threshold,
		lowRes ? "lowres" : "half");
	return check >= 2 && maskMax == 0.0f;	// ProbeLog 2: the combine's input and output are compared too (CombineCheck), on frames with an empty mask
#else
	return false;
#endif
}

#if NVR_TEST_TOOLS
// DIAGNOSTIC (ProbeLog 2): the combine pass with an all-zero ray buffer should return its input; read both back and compare texel by
// texel (fp16 bits). Input = the surface the combine samples, output = the surface it wrote. A test-only GPU stall.
static void CombineCheck(IDirect3DDevice9* Device, IDirect3DSurface9* input, IDirect3DSurface9* output) {
	static IDirect3DSurface9* readA = nullptr;
	static IDirect3DSurface9* readB = nullptr;
	static D3DSURFACE_DESC have = {};
	static unsigned checks = 0;
	D3DSURFACE_DESC d = {};
	if (!input || !output || FAILED(input->GetDesc(&d)) || d.Format != D3DFMT_A16B16G16R16F) return;
	if (!readA || d.Width != have.Width || d.Height != have.Height) {
		if (readA) { readA->Release(); readA = nullptr; }
		if (readB) { readB->Release(); readB = nullptr; }
		if (FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &readA, NULL)) ||
			FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &readB, NULL))) {
			if (readA) { readA->Release(); readA = nullptr; }
			return;
		}
		have = d;
	}
	if (FAILED(Device->GetRenderTargetData(input, readA)) || FAILED(Device->GetRenderTargetData(output, readB))) return;
	D3DLOCKED_RECT la = {}, lb = {};
	if (FAILED(readA->LockRect(&la, NULL, D3DLOCK_READONLY))) return;
	if (FAILED(readB->LockRect(&lb, NULL, D3DLOCK_READONLY))) { readA->UnlockRect(); return; }
	unsigned differ = 0, ulp1 = 0, ulp2 = 0, ulp3 = 0, signs = 0, alphaDiffer = 0;
	float inA0 = 1e30f, inA1 = -1e30f, outA0 = 1e30f, outA1 = -1e30f, worstIn = 0.0f, worstOut = 0.0f;
	unsigned maxUlp = 0;
	for (UINT y = 0; y < d.Height; y++) {
		const UInt16* pa = (const UInt16*)((const UInt8*)la.pBits + (size_t)y * la.Pitch);
		const UInt16* pb = (const UInt16*)((const UInt8*)lb.pBits + (size_t)y * lb.Pitch);
		for (UINT x = 0; x < d.Width; x++, pa += 4, pb += 4) {
			bool any = false;
			unsigned worst = 0;
			for (int c = 0; c < 3; c++) {
				if (pa[c] == pb[c]) continue;
				any = true;
				if ((pa[c] ^ pb[c]) & 0x8000) { signs++; worst = 0xFFFF; continue; }
				const unsigned ulp = pa[c] > pb[c] ? pa[c] - pb[c] : pb[c] - pa[c];
				if (ulp > worst) { worst = ulp; if (ulp > maxUlp) { maxUlp = ulp; worstIn = HalfToFloat(pa[c]); worstOut = HalfToFloat(pb[c]); } }
			}
			if (any) { differ++; if (worst == 1) ulp1++; else if (worst == 2) ulp2++; else ulp3++; }
			const float ai = HalfToFloat(pa[3]), ao = HalfToFloat(pb[3]);
			if (pa[3] != pb[3]) alphaDiffer++;
			inA0 = (std::min)(inA0, ai); inA1 = (std::max)(inA1, ai); outA0 = (std::min)(outA0, ao); outA1 = (std::max)(outA1, ao);
		}
	}
	readB->UnlockRect();
	readA->UnlockRect();
	if (++checks <= 40)
		Logger::Log("GODRAYS COMBINE CHECK (mask empty): %ux%u texels, colour differs in %u (%.3f %%): worst ulp 1: %u, 2: %u, 3+: %u, sign flips %u, max %u ulp (in %.5f -> out %.5f); alpha in %.3f..%.3f out %.3f..%.3f, alpha differs in %u",
			d.Width, d.Height, differ, 100.0 * differ / ((double)d.Width * d.Height), ulp1, ulp2, ulp3, signs, maxUlp, worstIn, worstOut, inA0, inA1, outA0, outA1, alphaDiffer);
}
#endif

/*
* UNOFFICIAL 2026-10-18 SkipIdle (helper 'grnoon'; look option, off by default). Measured in game at a real high sun (game hours 10, 13
* and 16, 40+ headings): the light mask is exactly 0 - so the rays add exactly nothing - whenever the sun's disc is not on screen: sky
* pixels there are at most ~0.8 (linear) against the threshold ~2, while the disc reads 5+ and saturates the mask. The picture of an
* empty frame is then the input (the 'effect off' picture). Skipping needs the emptiness known on the CPU before the passes are
* issued, so it works from a check one or two frames old, guarded by the sun's place:
*   - the sun is at least 20 degrees up (below that the threshold falls and sunsets cast real rays),
*   - the sun is at least 12 degrees outside the view frustum (in the horizontal or the vertical plane): its disc and halo cannot
*     reach the picture, and a sun that turns into view is seen on the very frame it does (no lag for the one thing that matters),
*   - the last finished check (sky mask maximum, 4 frames apart, answered by an occlusion query that is polled without waiting) was
*     below 0.8 x the light mask's threshold, which proves the light mask 0 (a five-tap average of the sky mask cannot exceed its maximum).
* Not exact in one case: something else makes the sky bright within the few frames between two checks (a lightning flash).
*/
bool GodRaysEffect::IdleGuard() const {
	const ShaderConstants& c = TheShaderManager->ShaderConst;
	const D3DXMATRIX& P = TheRenderManager->projMatrix;
	const float x = c.ViewSpaceLightDir.x, y = c.ViewSpaceLightDir.y, z = c.ViewSpaceLightDir.z;
	if (!std::isfinite(x + y + z) || !std::isfinite(c.SunPosition.z) || !(P._11 > 0.01f) || !(P._22 > 0.01f)) return false;
	if (c.SunPosition.z < 0.342f) return false;	// sin 20 degrees
	const float margin = 0.2094f;	// 12 degrees
	const float halfX = atanf(1.0f / P._11), halfY = atanf(1.0f / P._22);	// the frustum's half angles
	// angles of the sun direction in the view's horizontal and vertical planes (z = forward; behind the camera they pass 90 degrees)
	return atan2f(fabsf(x), z) >= halfX + margin || atan2f(fabsf(y), z) >= halfY + margin;
}

// The last check's answer, when it has arrived (never waits): updates idleEmptyKnown. The query counts the pixels of the 1 x 1 test draw
// that survived (the sky mask's maximum was at least the limit, or NaN): 0 = empty.
void GodRaysEffect::IdlePoll() {
	if (!idleQuery || !idlePending) return;
	DWORD pixels = 0;
	const HRESULT result = idleQuery->GetData(&pixels, sizeof(DWORD), 0);
	if (result == S_FALSE) {
		if (++idleAge > 8) idleEmptyKnown = false;	// too old to trust
		return;
	}
	idlePending = false;
	if (result != S_OK) { idleEmptyKnown = false; return; }
	idleEmptyKnown = pixels == 0;
	idleLastPixels = pixels;
#if NVR_TEST_TOOLS
	if (idleTruth >= 0.0f) {	// ProbeLog 3: the read-back truth taken when the check was issued
		const bool truthBright = !(idleTruth < idleLimitAtIssue);
		idleValidated++;
		if (truthBright != (pixels != 0)) {
			idleValidateBad++;
			if (idleValidateBad <= 8) Logger::Log("GODRAYS IDLE QUERY CHECK: DISAGREE: query counted %u pixels, read-back sky max %.4f, limit %.4f", (unsigned)pixels, idleTruth, idleLimitAtIssue);
		}
		else if (truthBright) idleValidateBright++;
		if (idleValidated % 25 == 0)
			Logger::Log("GODRAYS IDLE QUERY CHECK: %u results, %u agree with the read-back, %u disagree (%u of the agreeing were bright)", idleValidated, idleValidated - idleValidateBad, idleValidateBad, idleValidateBright);
		idleTruth = -1.0f;
	}
#endif
}

// Max-reduces the sky mask (textures[0] after pass 0) and asks, with an occlusion query around a 1 x 1 test draw, whether that maximum is
// below the limit. The answer is read by IdlePoll a frame or two later.
bool GodRaysEffect::IdleIssue(IDirect3DDevice9* Device, IDirect3DTexture9* skyMask, UINT width, UINT height, int set) {
	if (idlePending || idleQueryBroken || maxSteps[set] < 2) return false;	// the last answer is not read yet: next time
	if (!idleQuery && FAILED(Device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &idleQuery))) { idleQuery = nullptr; idleQueryBroken = true; Logger::Log("God rays SkipIdle: no occlusion queries on this device; the option does nothing."); return false; }
	D3DXHANDLE technique = TechniqueHandle(techIdle, "GodRaysIdleTest");
	D3DXHANDLE skipHandle = ParameterHandle(paramSkip, "NVR_GodRaysSkip");
	if (!technique || !skipHandle) return false;
	const float sunHeight = 1.0f - (std::max)(TheShaderManager->ShaderConst.SunPosition.z, 0.0f);
	idleLimit = 0.8f * 2.0f * (1.0f - powf(fabsf(sunHeight), 8.0f));	// the light mask's threshold lerp(2, 0, sunHeight^8), with a margin
	if (!RunReduce(Device, skyMask, width, height, set)) return false;
#if NVR_TEST_TOOLS
	idleTruth = -1.0f;
	if (probeLevel >= 3) {	// validation: the true maximum by a (blocking) read-back, compared when the answer arrives
		float truth = -1.0f;
		if (ReadMax(Device, maxSurface[set][maxSteps[set] - 1], truth)) { idleTruth = truth; idleLimitAtIssue = idleLimit; }
	}
#endif
	UINT passes = 0;
	HRESULT result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) { const D3DXVECTOR4 skip(0.0f, 0.0f, idleLimit, 0.0f); Effect->SetVector(skipHandle, &skip); result = Effect->Begin(&passes, 0); }
	if (FAILED(result)) return false;
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	const D3DVIEWPORT9 viewport = { 0, 0, 1, 1, 0.0f, 1.0f };
	result = Device->SetRenderTarget(0, maxSurface[set][0]);	// any small target: it is not needed again this frame
	if (SUCCEEDED(result)) result = Device->SetViewport(&viewport);
	if (SUCCEEDED(result)) result = Effect->BeginPass(0);
	if (SUCCEEDED(result)) {
		Device->SetTexture(6, maxTexture[set][maxSteps[set] - 1]);
		idleQuery->Issue(D3DISSUE_BEGIN);
		result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
		idleQuery->Issue(D3DISSUE_END);
		Effect->EndPass();
		Device->SetTexture(6, nullptr);
	}
	Effect->End();
	if (FAILED(result)) return false;
	idlePending = true;
	idleAge = 0;
	idleSinceCheck = 0;
	return true;
}

// DIAGNOSTIC (development builds): the idle skip's counts every 120 frames of an exterior day with the F10 profile on.
void GodRaysEffect::IdleStats() {
#if NVR_TEST_TOOLS
	if (!skipIdle || !GpuTimer::Enabled) return;
	if (++idleWindow < 120) return;
	Logger::Log("GODRAYS IDLE: last %u frames: skipped %u, check frames %u (of which only the check %u), full %u, guard refused %u; last query pixels %u limit %.3f, known empty %d",
		idleWindow, idleSkipped, idleChecks, idleCheckOnly, idleFull, idleGuardRefused, (unsigned)idleLastPixels, idleLimit, idleEmptyKnown ? 1 : 0);
	idleWindow = idleSkipped = idleChecks = idleCheckOnly = idleFull = idleGuardRefused = 0;
#endif
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
	bool combineCheck = false;	// DIAGNOSTIC ProbeLog 2
	bool heavySkipped = false;	// UNOFFICIAL SkipIdle: this frame's check found the sky empty and the blurs and combine were left out
	const bool checkOnly = idleCheckFrame && idleEmptyKnown;	// a check frame of an idle sky: the sky mask and the question, nothing else
	UINT passes = 0;
	for (int stage = 0; stage < 2 && SUCCEEDED(result); stage++) {
	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	if (skipHandle) { const D3DXVECTOR4 skip(maskReduced ? 1.0f : 0.0f, skipIdle ? 1.0f : 0.0f, idleLimit, 0.0f); Effect->SetVector(skipHandle, &skip); }
	result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		static GpuTimer passTimers[6] = { GpuTimer("  GR sky mask (half)"), GpuTimer("  GR light mask (half)"),
			GpuTimer("  GR blur 1 (half)"), GpuTimer("  GR blur 2 (half)"), GpuTimer("  GR blur 3 (half)"),
			GpuTimer("  GR combine (full)") };
		const UINT first = stage == 0 ? 0 : 2, last = stage == 0 ? (passes < 2 ? passes : (checkOnly ? 1 : 2)) : passes;
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
	if (stage == 0 && SUCCEEDED(result)) {
		// UNOFFICIAL SkipIdle: the question about the sky (first: it uses the same reduction textures as the two below, which then overwrite it).
		// A check frame of an idle sky stops here: nothing else is drawn and the picture is left as it is.
		if (idleCheckFrame) {
			idleChecks++;
			IdleIssue(Device, textures[0], reduced.Width, reduced.Height, set);
			if (checkOnly) { heavySkipped = true; break; }
		}
		combineCheck = Probe(Device, textures, reduced.Width, reduced.Height, set, lowRes);	// DIAGNOSTIC (off: returns at once)
		// After the light mask (in textures[1]): its maximum, when SkipEmpty is on. A failure only means no skip this frame.
		if (skipEmpty && skipHandle) maskReduced = RunReduce(Device, textures[1], reduced.Width, reduced.Height, set);
	}
	}
#if NVR_TEST_TOOLS
	if (combineCheck && SUCCEEDED(result) && !heavySkipped) CombineCheck(Device, RenderedSurface, finalTarget);
#endif
	Device->SetTexture(6, nullptr);
	Device->SetTexture(5, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetViewport(&original);
	Device->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissorEnabled);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	if (SUCCEEDED(result) && !heavySkipped) {
		if (chained) chain.Commit();
		else result = Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	}
	idleCheckSkipped = heavySkipped;
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
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; idleEmptyKnown = false; return; }

	// UNOFFICIAL SkipIdle (see IdleGuard): with the sun well outside the view and the sky known empty, draw nothing (the 'effect off'
	// picture, no chain commit), except every 4th frame, which re-checks the sky (sky mask + its maximum, 0.15 ms).
	idleCheckFrame = false;
	idleCheckSkipped = false;
	if (skipIdle && !dedicatedRenderFailed) {
		IdlePoll();	// answers are taken whatever the guard says
		if (IdleGuard()) {
			idleSinceCheck++;
			if (idleEmptyKnown && idleSinceCheck < 4) { idleSkipped++; renderTime = 0; IdleStats(); return; }
			idleCheckFrame = true;
		}
		else {
			idleEmptyKnown = false;
			idleGuardRefused++;
			if (probeLevel >= 3 && (++idleValidateTick % 8) == 0) idleCheckFrame = true;	// DIAGNOSTIC validation: checks (not used) where the guard refuses
		}
	}
	else idleEmptyKnown = false;

	{
		auto timer = TimeLogger();
		if (RenderDedicated(Device, RenderTarget, RenderedSurface)) {
			if (idleCheckSkipped) idleCheckOnly++; else idleFull++;
			renderTime = timer.LogTime("GodRays::DedicatedRender");
			IdleStats();
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
