#include "../core/GpuProfiler.h"
#include "../core/DxvkCompute.h"
#include "../core/RenderRect.h"

static GpuTimer* ContactPassTimers() {
	static GpuTimer passTimers[4] = { GpuTimer("  Contact march"), GpuTimer("  Contact blur H"),
		GpuTimer("  Contact blur V"), GpuTimer("  Contact shadow apply") };
	return passTimers;
}

void SunShadowsEffect::SetCT() {
	EffectRecord::SetCT();

	// Change shadow atlas sampler state if anisotropy is supposed to be used.
	int anisotropy = TheShaderManager->Effects.ShadowsExteriors->Settings.ShadowMaps.Anisotropy;
	if (anisotropy) {
		TheRenderManager->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
		TheRenderManager->SetSamplerState(1, D3DSAMP_MAXANISOTROPY, anisotropy);
	}
}

void SunShadowsEffect::UpdateSettings() {
	fastPaths = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ScreenSpace", "FastPaths") != 0;
}

void SunShadowsEffect::RegisterTextures() {
	TheTextureManager->InitTexture("NVR_ShadowPassScratch0", &scratchTexture[0], &scratchSurface[0],
		TheRenderManager->width, TheRenderManager->height, D3DFMT_G16R16);
	TheTextureManager->InitTexture("NVR_ShadowPassScratch1", &scratchTexture[1], &scratchSurface[1],
		TheRenderManager->width, TheRenderManager->height, D3DFMT_G16R16);
}

void SunShadowsEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }
	if (fastPaths && techniqueIndex <= 1) {
		static CachedHandle fastCache[2];
		D3DXHANDLE fast = TechniqueHandle(fastCache[techniqueIndex], techniqueIndex == 0 ? "ContactFast" : "ForwardContactShadowsFast");
		if (fast && Effect->GetTechnique(techniqueIndex + 3) == fast) techniqueIndex += 3;
	}

	IDirect3DTexture9* shadowTexture = TheShaderManager->Effects.ShadowsExteriors->Textures.ShadowPassTexture;
	IDirect3DSurface9* shadowSurface = TheShaderManager->Effects.ShadowsExteriors->Textures.ShadowPassSurface;
	D3DXHANDLE technique = Effect->GetTechnique(techniqueIndex);
	D3DXTECHNIQUE_DESC description = {};
	D3DSURFACE_DESC target = {}, scratch = {};
	if (pingPongFailed || !technique || RenderTarget != shadowSurface || !shadowTexture ||
		!scratchTexture[0] || !scratchTexture[1] || !scratchSurface[0] || !scratchSurface[1] ||
		FAILED(Effect->GetTechniqueDesc(technique, &description)) || description.Passes < 2 ||
		FAILED(RenderTarget->GetDesc(&target)) || FAILED(scratchSurface[0]->GetDesc(&scratch)) ||
		target.Width != scratch.Width || target.Height != scratch.Height || target.Format != scratch.Format) {
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}

	if (!ExtFailed && !ClearRenderTarget && DxvkCompute::PassesOn()) {
		auto extTimer = TimeLogger();
		if (ExtPingPong(Device, techniqueIndex, description, shadowTexture, shadowSurface)) {
			Device->SetRenderTarget(0, RenderTarget);
			renderTime = extTimer.LogTime("SunShadows::Extension");
			return;
		}
	}

	auto timer = TimeLogger();
	HRESULT result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) SetCT();
	UINT passes = 0;
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		GpuTimer* passTimers = ContactPassTimers();
		IDirect3DTexture9* source = shadowTexture;
		for (UINT p = 0; p < passes && SUCCEEDED(result); ++p) {
			GpuProfileScope gpuPass(passTimers[p < 4 ? p : 3], Device);
			const bool last = p == passes - 1;
			IDirect3DSurface9* destination = last ? shadowSurface : scratchSurface[p & 1];
			Device->SetTexture(3, nullptr);
			result = Device->SetRenderTarget(0, destination);
			if (FAILED(result)) break;
			if (ClearRenderTarget) Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			Device->SetTexture(3, source);
			RenderRect::ApplyViewport(Device);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
			source = last ? shadowTexture : scratchTexture[p & 1];
		}
		Effect->End();
	}
	Device->SetTexture(3, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	if (FAILED(result)) {
		pingPongFailed = true;
		Logger::Log("Sun shadow ping-pong failed (%08lx); using the in-place path until restart.", result);
		return;
	}
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL contact shadows use scratch ping-pong (%u passes, no feedback loop).", passes);
		reported = true;
	}
	renderTime = timer.LogTime("SunShadows::PingPong");
}

namespace {
	enum SunPass { SunMarch, SunBlurH, SunBlurV, SunApply, SunFinalBlur, SunSharp, SunSharpFinish };

	struct SunUniforms {
		D3DXMATRIX	cascade[4], projection, invProjection, view, invView;
		D3DXVECTOR4	reciprocalResolution, smoothedSunDir, viewSpaceLightDir, formatData, screenSpaceData, shadowFade, shadowBlur,
					forwardData, center[4], cameraData, flags;
		D3DXMATRIX	charToLight;
		D3DXVECTOR4	charCenter, charData;
	};
	struct SunPush { UInt32 linearIndex, noiseIndex, pass, pad; };

	const NVR_EXT_BINDING_DESC SunBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE,  2, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 3, NVR_EXT_BIND_SAMPLED_IMAGE,  3, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 4, NVR_EXT_BIND_SAMPLED_IMAGE,  4, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 5, NVR_EXT_BIND_SAMPLED_IMAGE,  5, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        1, NVR_EXT_VIEW_2D, FALSE, 4 },
	};

	const D3DXVECTOR4* SunConstant(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
	IDirect3DBaseTexture9* SunTexture(const char* name) {
		auto found = TheTextureManager->TextureNames.find(name);
		return found == TheTextureManager->TextureNames.end() || !found->second ? nullptr : *found->second;
	}
}

bool SunShadowsEffect::ExtPingPong(IDirect3DDevice9* Device, UINT techniqueIndex, const D3DXTECHNIQUE_DESC& description,
	IDirect3DTexture9* shadowTexture, IDirect3DSurface9* shadowSurface) {
	static const char* const names[5] = { nullptr, "ForwardContactShadows", "ForwardContactSharp", "ContactFast", "ForwardContactShadowsFast" };
	static const SunPass layouts[5][4] = {
		{ SunMarch, SunBlurH, SunBlurV, SunApply },
		{ SunMarch, SunBlurH, SunFinalBlur },
		{ SunSharp, SunSharpFinish },
		{ SunMarch, SunBlurH, SunBlurV, SunApply },
		{ SunMarch, SunBlurH, SunFinalBlur },
	};
	static const UINT passCounts[5] = { 4, 3, 2, 4, 3 };
	if (techniqueIndex > 4 || description.Passes != passCounts[techniqueIndex]) return false;
	const char* name = description.Name;
	if (names[techniqueIndex] ? (!name || strcmp(name, names[techniqueIndex])) : (name && *name)) return false;

	if (extForwardGeneration != LoadGeneration) {
		extForwardGeneration = LoadGeneration;
		extForwardCompiled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "ForwardShadows") != 0;
		extCharCompiled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.ShadowMaps", "CharacterShadowMap") != 0;
	}

	static const char* const spvNames[5] = { "SunShadowsMarch", "SunShadowsBlur", "SunShadowsApply", "SunShadowsSharp", "SunShadowsSharpFinish" };
	static const UINT pipelineOf[7] = { 0, 1, 1, 2, 1, 3, 4 };
	ID3D9NvrExtPipeline* pipelines[5] = {};
	for (UINT p = 0; p < description.Passes; p++) {
		const UINT index = pipelineOf[layouts[techniqueIndex][p]];
		pipelines[index] = ExtVariant(extVariants, extPasses[index], spvNames[index], index == 2 && extCharCompiled ? "c1" : "", [](const char* name) {
			return DxvkCompute::CreateGraphicsPipeline(name, SunBindings, _countof(SunBindings), sizeof(SunPush), true);
		});
		if (!pipelines[index]) return false;
	}
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* noiseSampler = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_WRAP);
	if (!linear || !noiseSampler) return false;

	static const char* const cascadeNames[4] = { "TESR_ShadowCameraToLightTransformNear", "TESR_ShadowCameraToLightTransformMiddle",
		"TESR_ShadowCameraToLightTransformFar", "TESR_ShadowCameraToLightTransformLod" };
	static const char* const centerNames[4] = { "TESR_ShadowNearCenter", "TESR_ShadowMiddleCenter", "TESR_ShadowFarCenter", "TESR_ShadowLodCenter" };
	const D3DXVECTOR4* cascade[4];
	const D3DXVECTOR4* center[4];
	for (int i = 0; i < 4; i++) {
		cascade[i] = SunConstant(cascadeNames[i]);
		center[i] = SunConstant(centerNames[i]);
		if (!cascade[i] || !center[i]) return false;
	}
	const D3DXVECTOR4* projection = SunConstant("TESR_ProjectionTransform");
	const D3DXVECTOR4* invProjection = SunConstant("TESR_InvProjectionTransform");
	const D3DXVECTOR4* view = SunConstant("TESR_ViewTransform");
	const D3DXVECTOR4* invView = SunConstant("TESR_InvViewTransform");
	const D3DXVECTOR4* reciprocal = SunConstant("TESR_ReciprocalResolution");
	const D3DXVECTOR4* smoothedSunDir = SunConstant("TESR_SmoothedSunDir");
	const D3DXVECTOR4* viewSpaceLightDir = SunConstant("TESR_ViewSpaceLightDir");
	const D3DXVECTOR4* formatData = SunConstant("TESR_ShadowFormatData");
	const D3DXVECTOR4* screenSpaceData = SunConstant("TESR_ShadowScreenSpaceData");
	const D3DXVECTOR4* shadowFade = SunConstant("TESR_ShadowFade");
	const D3DXVECTOR4* shadowBlur = SunConstant("TESR_ShadowBlur");
	const D3DXVECTOR4* forwardData = SunConstant("TESR_ShadowForwardData");
	const D3DXVECTOR4* cameraData = SunConstant("TESR_CameraData");
	if (!projection || !invProjection || !view || !invView || !reciprocal || !smoothedSunDir || !viewSpaceLightDir || !formatData ||
		!screenSpaceData || !shadowFade || !shadowBlur || !forwardData || !cameraData)
		return false;

	IDirect3DBaseTexture9* textures[6] = { SunTexture("TESR_DepthBuffer"), SunTexture("TESR_ShadowAtlas"), SunTexture("TESR_NormalsBuffer"),
		shadowTexture, ExtFileTexture("TESR_NoiseSampler"), SunTexture("TESR_CharShadowMap") };
	if (!textures[5]) textures[5] = textures[0];
	for (IDirect3DBaseTexture9* texture : textures) if (!texture) return false;

	SunUniforms uniforms = {};
	for (int i = 0; i < 4; i++) {
		memcpy(&uniforms.cascade[i], cascade[i], sizeof(D3DXMATRIX));
		uniforms.center[i] = *center[i];
	}
	memcpy(&uniforms.projection, projection, sizeof(D3DXMATRIX));
	memcpy(&uniforms.invProjection, invProjection, sizeof(D3DXMATRIX));
	memcpy(&uniforms.view, view, sizeof(D3DXMATRIX));
	memcpy(&uniforms.invView, invView, sizeof(D3DXMATRIX));
	uniforms.reciprocalResolution = *reciprocal;
	uniforms.smoothedSunDir = *smoothedSunDir;
	uniforms.viewSpaceLightDir = *viewSpaceLightDir;
	uniforms.formatData = *formatData;
	uniforms.screenSpaceData = *screenSpaceData;
	uniforms.shadowFade = *shadowFade;
	uniforms.shadowBlur = *shadowBlur;
	uniforms.forwardData = *forwardData;
	uniforms.cameraData = *cameraData;
	uniforms.flags = D3DXVECTOR4(extForwardCompiled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
	if (const D3DXVECTOR4* nearFade = SunConstant("TESR_ShadowScreenSpaceFade")) { uniforms.flags.y = nearFade->x; uniforms.flags.z = nearFade->y; }
	if (const D3DXVECTOR4* rtReplace = SunConstant("TESR_RtSunReplace")) uniforms.flags.w = rtReplace->x;
	if (const D3DXVECTOR4* charToLight = SunConstant("TESR_CharShadowCameraToLight")) memcpy(&uniforms.charToLight, charToLight, sizeof(D3DXMATRIX));
	if (const D3DXVECTOR4* charCenter = SunConstant("TESR_CharShadowCenter")) uniforms.charCenter = *charCenter;
	if (const D3DXVECTOR4* charData = SunConstant("TESR_CharShadowData")) uniforms.charData = *charData;
	ID3D9NvrExtBuffer* buffer = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	if (!buffer) return false;

	NVR_EXT_RESOURCE_BINDING resources[_countof(SunBindings)] = {};
	for (UINT i = 0; i < 6; i++) {
		resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
	}
	resources[6].Slot = 0; resources[6].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[6].pResource = buffer;
	resources[7].Slot = 0; resources[7].Type = NVR_EXT_BIND_SAMPLER; resources[7].pResource = linear;
	resources[8].Slot = 1; resources[8].Type = NVR_EXT_BIND_SAMPLER; resources[8].pResource = noiseSampler;

	GpuTimer* passTimers = ContactPassTimers();
	Device->SetTexture(3, nullptr);
	IDirect3DTexture9* source = shadowTexture;
	for (UINT p = 0; p < description.Passes; p++) {
		GpuProfileScope gpuPass(passTimers[p < 4 ? p : 3], Device);
		const bool last = p == description.Passes - 1;
		const SunPass kind = layouts[techniqueIndex][p];
		IDirect3DSurface9* destination = last ? shadowSurface : scratchSurface[p & 1];
		if (FAILED(Device->SetRenderTarget(0, destination))) return false;
		resources[3].pResource = source;
		SunPush push = {};
		push.pass = kind == SunBlurV ? 1 : kind == SunFinalBlur ? 2 : 0;
		if (!ExtDrawTo(pipelines[pipelineOf[kind]], &destination, 1, resources, _countof(resources), &push, sizeof(push), nullptr, ExtUv::ScreenQuad)) return false;
		source = last ? shadowTexture : scratchTexture[p & 1];
	}
	if (++ExtFrames == 1) Logger::Log("UNOFFICIAL NVR DXVK: %s runs as Vulkan passes (%u passes).", Name, description.Passes);
	return true;
}
