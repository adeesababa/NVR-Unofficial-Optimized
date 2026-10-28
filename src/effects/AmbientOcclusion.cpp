#include "AmbientOcclusion.h"
#include "../core/GpuProfiler.h"
#include "../core/DxvkCompute.h"
#include "../core/RenderRect.h"

static D3DXVECTOR4 GTAOData(0.0f, 0.0f, 0.0f, 64.0f);
static EffectRecord::CachedHandle techGTAO;
static D3DXVECTOR4 SkyData(0.0f, 1.0f, 600.0f, 0.0001f);
static bool SkyEnabled = false;
static D3DXVECTOR4 SkyData2(0.0f, 1.0f, 0.0f, 0.0f);
static D3DXVECTOR4 SkyData3(0.0f, 0.0f, 1.0f, 0.0f);
static D3DXVECTOR4 SkyData4(800.0f, 40.0f, 0.0f, 0.0f);
static float SkyAmbientScale = 1.0f;
static D3DXVECTOR4 SkyData5(0.0f, 0.0f, 0.0f, 1.0e6f);
static float SkyLightCap = 0.0f;
static float SkyOpenSides = 0.0f;
static int SkyVersion = 2;

static float LinearLuma(const D3DXVECTOR4& c) {
	return 0.2126f * powf(max(c.x, 0.0f), 2.2f) + 0.7152f * powf(max(c.y, 0.0f), 2.2f) + 0.0722f * powf(max(c.z, 0.0f), 2.2f);
}

bool AmbientOcclusionEffect::SkyV2Active() { return SkyData2.x > 0.0f; }
D3DXVECTOR2 AmbientOcclusionEffect::SkyTakeComposite() {
	const bool limit = SkyLightCap > 0.0f && SkyData3.w < 0.5f;
	return limit ? D3DXVECTOR2(SkyLightCap * SkyData2.y * SkyData2.z, 0.0f) : D3DXVECTOR2(0.0f, 1.0e6f);
}
bool AmbientOcclusionEffect::SkyHostedByAmbientLight() {
	AmbientLightEffect* al = TheShaderManager->Effects.AmbientLight;
	AmbientOcclusionEffect* ao = TheShaderManager->Effects.AmbientOcclusion;
	return al && al->ShouldRender() && ao && ao->Effect;
}
static bool FastPaths = false;
static EffectRecord::CachedHandle techFast, techGTAOFast;

void AmbientOcclusionEffect::UpdateConstants() {
	UpdateSkyConstants();
}

void AmbientOcclusionEffect::UpdateSkyConstants() {
	const bool hosted = SkyHostedByAmbientLight();
	const bool sky = SkyEnabled && TheShaderManager->GameState.isExterior && (hosted || (Enabled && Constants.Enabled));
	SkyData.x = sky && !hosted && SkyVersion == 1 ? 1.0f : 0.0f;
	SkyData2.x = sky && (hosted || SkyVersion == 2) ? 1.0f : 0.0f;
	SkyData2.z = LinearLuma(TheShaderManager->ShaderConst.sunAmbient) * SkyAmbientScale;
	SkyData2.w = LinearLuma(TheShaderManager->ShaderConst.sunColor);
	{
		const D3DXVECTOR2 take = SkyTakeComposite();
		SkyData5 = D3DXVECTOR4(take.x, SkyOpenSides, 0.0f, take.y);
	}
	if (SkyData.x > 0.0f) TheShaderManager->orthoRequired = true;
	if (SkyData2.x > 0.0f) TheShaderManager->orthoSkyRequired = true;
	static bool skyWasActive = false;
	TheShaderManager->orthoSkyFresh = SkyData2.x > 0.0f && !skyWasActive;
	skyWasActive = SkyData2.x > 0.0f;
}

void AmbientOcclusionEffect::RegisterConstants() {
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionAOData"] = &Constants.AOData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionData"] = &Constants.Data;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionGTAO"] = &GTAOData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky"] = &SkyData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky2"] = &SkyData2;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky3"] = &SkyData3;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky4"] = &SkyData4;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky5"] = &SkyData5;
}

void AmbientOcclusionEffect::RegisterTextures() {
	const int width = MaxQuality::Reduced(TheRenderManager->width, 2);
	const int height = MaxQuality::Reduced(TheRenderManager->height, 2);
	TheTextureManager->InitTexture("NVR_AOBuffer0", &aoTexture[0], &aoSurface[0], width, height, D3DFMT_G16R16F);
	TheTextureManager->InitTexture("NVR_AOBuffer1", &aoTexture[1], &aoSurface[1], width, height, D3DFMT_G16R16F);
	const int lowWidth = MaxQuality::Reduced(TheRenderManager->width, 4);
	const int lowHeight = MaxQuality::Reduced(TheRenderManager->height, 4);
	TheTextureManager->InitTexture("NVR_AOBufferLow0", &aoTextureLow[0], &aoSurfaceLow[0], lowWidth, lowHeight, D3DFMT_G16R16F);
	TheTextureManager->InitTexture("NVR_AOBufferLow1", &aoTextureLow[1], &aoSurfaceLow[1], lowWidth, lowHeight, D3DFMT_G16R16F);
}

bool AmbientOcclusionEffect::UseLowRes() const {
	return TheSettingManager->SettingsMain.Main.AOLowRes &&
		aoTextureLow[0] && aoTextureLow[1] && aoSurfaceLow[0] && aoSurfaceLow[1];
}

void AmbientOcclusionEffect::UpdateSettings() {
	const char* sectionName = TheShaderManager->GameState.isExterior?"Shaders.AmbientOcclusion.Exteriors":"Shaders.AmbientOcclusion.Interiors";

	Constants.Enabled = TheSettingManager->GetSettingI(sectionName, "Enabled");
	Constants.AOData.x = TheSettingManager->GetSettingF(sectionName, "Samples");
	Constants.AOData.y = TheSettingManager->GetSettingF(sectionName, "StrengthMultiplier");
	Constants.AOData.z = TheSettingManager->GetSettingF(sectionName, "ClampStrength");
	Constants.AOData.w = TheSettingManager->GetSettingF(sectionName, "Range");
	Constants.Data.x = TheSettingManager->GetSettingF(sectionName, "AngleBias");
	Constants.Data.y = TheSettingManager->GetSettingF(sectionName, "LumThreshold");
	Constants.Data.z = TheSettingManager->GetSettingF(sectionName, "BlurDropThreshold");
	Constants.Data.w = TheSettingManager->GetSettingF(sectionName, "BlurRadiusMultiplier");
	GTAOData.x = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.GTAO", "Enabled") ? 1.0f : 0.0f;
	GTAOData.w = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.GTAO", "MaxRadius"), 4.0f, 256.0f);
	SkyEnabled = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "Enabled");
	FastPaths = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.FastPaths", "Enabled") != 0;
	SkyData.y = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "Strength"), 0.0f, 1.0f);
	SkyData.z = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "Distance"), 64.0f, 4000.0f);
	SkyVersion = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "Version") == 1 ? 1 : 2;
	SkyReflections = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "Reflections") != 0;
	SkyMinReflection = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "MinReflection"), 0.0f, 1.0f);
	SkyLightCap = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "LightCap"), 0.0f, 10.0f);
	SkyOpenSides = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "OpenSides"), 0.0f, 1.0f);
	SkyData2.y = SkyData.y;
	SkyData3.w = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "DebugView"), 0, 4);
	SkyAmbientScale = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "AmbientScale"), 0.0f, 10.0f);
	SkyData3.z = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "ShareApply") ? 1.0f : 0.0f;
	SkyData3.x = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "DepthNormals") ? 1.0f : 0.0f;
	SkyData3.y = 1.0f / std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "OpenThreshold"), 0.5f, 1.0f);
	SkyData4.x = SkyData.z;
	SkyData4.y = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientOcclusion.Skylighting", "SoftWidth"), 1.0f, 500.0f);
}

namespace {
	struct AOPush {
		D3DXVECTOR2 axis;
		UInt32 gtaoEstimate;
		UInt32 linearSampler;
		UInt32 pointSampler;
		UInt32 pointWrapSampler;
	};
	struct AOUniforms {
		D3DXMATRIX projection, invProjection, invView, shadowOrtho;
		D3DXVECTOR4 aoData, data, gtao, sky, sky2, sky3, sky4, sky5, smoothedSunDir, shadowFade, reciprocalResolution, fogData, fogColor,
			depthConstants, cameraData, layout, skyLayout;
	};

	const D3DXVECTOR4* RegisteredConstant(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}

	IDirect3DBaseTexture9* SlotTexture(const char* name) {
		std::string slotName = name;
		IDirect3DBaseTexture9** slot = TheTextureManager->GetTextureSlotByName(slotName);
		return slot ? *slot : nullptr;
	}

	void MakePipeline(EffectRecord::ExtPass& pass, const char* spvName, UINT textureCount) {
		if (pass.Tried) return;
		pass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[9] = {};
		UINT count = 0;
		for (UINT i = 0; i < textureCount; i++, count++) {
			bindings[count].Set = 1; bindings[count].Binding = i; bindings[count].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[count].Slot = i; bindings[count].ViewType = NVR_EXT_VIEW_2D;
		}
		bindings[count].Set = 2; bindings[count].Binding = 0; bindings[count].Type = NVR_EXT_BIND_UNIFORM_BUFFER; bindings[count].Slot = 0; count++;
		for (UINT s = 0; s < 3; s++, count++) { bindings[count].Type = NVR_EXT_BIND_SAMPLER; bindings[count].Slot = s; bindings[count].PushOffset = 12 + 4 * s; }
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvName, bindings, count, sizeof(AOPush), true);
	}

	bool Resources(NVR_EXT_RESOURCE_BINDING* resources, UINT& count, IDirect3DBaseTexture9* const* textures, UINT textureCount, ID3D9NvrExtBuffer* uniforms) {
		ID3D9NvrExtSampler* samplers[3] = { DxvkCompute::LinearClamp(), DxvkCompute::PointClamp(), DxvkCompute::PointWrap() };
		if (!uniforms || !samplers[0] || !samplers[1] || !samplers[2]) return false;
		count = 0;
		for (UINT i = 0; i < textureCount; i++, count++) {
			if (!textures[i]) return false;
			resources[count].Slot = i; resources[count].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[count].pResource = textures[i];
		}
		resources[count].Slot = 0; resources[count].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[count].pResource = uniforms; count++;
		for (UINT s = 0; s < 3; s++, count++) { resources[count].Slot = s; resources[count].Type = NVR_EXT_BIND_SAMPLER; resources[count].pResource = samplers[s]; }
		return true;
	}

	GpuTimer& AOPassTimer(UINT p) {
		static GpuTimer passTimers[4] = { GpuTimer("  AO estimate (half)"), GpuTimer("  AO blur X (half)"),
			GpuTimer("  AO blur Y (half)"), GpuTimer("  AO combine (full)") };
		return passTimers[p < 4 ? p : 3];
	}
}

bool AmbientOcclusionEffect::ExtConstants(ExtUniformBuffer& Uniforms, const D3DXVECTOR4& Layout, const D3DXVECTOR4& SkyLayout) {
	const D3DXVECTOR4* projection = RegisteredConstant("TESR_ProjectionTransform");
	const D3DXVECTOR4* invProjection = RegisteredConstant("TESR_InvProjectionTransform");
	const D3DXVECTOR4* invView = RegisteredConstant("TESR_InvViewTransform");
	const D3DXVECTOR4* reciprocalResolution = RegisteredConstant("TESR_ReciprocalResolution");
	const D3DXVECTOR4* fogData = RegisteredConstant("TESR_FogData");
	const D3DXVECTOR4* fogColor = RegisteredConstant("TESR_FogColor");
	const D3DXVECTOR4* depthConstants = RegisteredConstant("TESR_DepthConstants");
	const D3DXVECTOR4* cameraData = RegisteredConstant("TESR_CameraData");
	if (!projection || !invProjection || !invView || !reciprocalResolution || !fogData || !fogColor || !depthConstants || !cameraData) return false;
	const D3DXVECTOR4* shadowOrtho = RegisteredConstant("TESR_ShadowCameraToLightTransformOrtho");
	const D3DXVECTOR4* smoothedSunDir = RegisteredConstant("TESR_SmoothedSunDir");
	const D3DXVECTOR4* shadowFade = RegisteredConstant("TESR_ShadowFade");
	AOUniforms uniforms = {};
	memcpy(&uniforms.projection, projection, sizeof(D3DXMATRIX));
	memcpy(&uniforms.invProjection, invProjection, sizeof(D3DXMATRIX));
	memcpy(&uniforms.invView, invView, sizeof(D3DXMATRIX));
	if (shadowOrtho) memcpy(&uniforms.shadowOrtho, shadowOrtho, sizeof(D3DXMATRIX));
	uniforms.aoData = Constants.AOData;
	uniforms.data = Constants.Data;
	uniforms.gtao = GTAOData;
	uniforms.sky = SkyData;
	uniforms.sky2 = SkyData2;
	uniforms.sky3 = SkyData3;
	uniforms.sky4 = SkyData4;
	uniforms.sky5 = SkyData5;
	if (smoothedSunDir) uniforms.smoothedSunDir = *smoothedSunDir;
	if (shadowFade) uniforms.shadowFade = *shadowFade;
	uniforms.reciprocalResolution = *reciprocalResolution;
	uniforms.fogData = *fogData;
	uniforms.fogColor = *fogColor;
	uniforms.depthConstants = *depthConstants;
	uniforms.cameraData = *cameraData;
	uniforms.layout = Layout;
	uniforms.skyLayout = SkyLayout;
	return ExtUniforms(Uniforms, &uniforms, sizeof(uniforms)) != nullptr;
}

bool AmbientOcclusionEffect::ExtDedicated(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	IDirect3DTexture9* const* Textures, IDirect3DSurface9* const* Surfaces, const D3DXVECTOR4& Layout, bool GTAOEstimate, IDirect3DTexture9* OrthoMap, IDirect3DTexture9* SkyVis) {
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = SlotTexture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* noise = ExtFileTexture("TESR_BlueNoiseSampler");
	IDirect3DBaseTexture9* scene = TheTextureManager->RenderedTexture;
	if (!depth || !normals || !noise || !scene || (SkyData.x > 0.0f && !OrthoMap)) return false;
	ID3D9NvrExtPipeline* estimate = ExtVariant(extVariants, extEstimate, "AmbientOcclusionEstimate", GTAOEstimate ? "g1" : "g0", [](const char* name) {
		EffectRecord::ExtPass made;
		MakePipeline(made, name, 4);
		return made.Pipeline;
	});
	MakePipeline(extBlur, "AmbientOcclusionBlur", 3);
	MakePipeline(extCombine, "AmbientOcclusionCombine", 4);
	if (!estimate || !extBlur.Pipeline || !extCombine.Pipeline || !ExtConstants(extUniforms, Layout, D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f))) return false;
	const UINT firstPass = combineOnly ? 3 : 0;
	const UINT endPass = deferCombine ? 3 : 4;
	for (UINT p = firstPass; p < endPass; ++p) {
		AOPush push = {};
		push.axis = p == 1 ? D3DXVECTOR2(1.0f, 0.0f) : D3DXVECTOR2(0.0f, 1.0f);
		push.gtaoEstimate = GTAOEstimate ? 1 : 0;
		NVR_EXT_RESOURCE_BINDING resources[8] = {};
		UINT count = 0;
		GpuProfileScope gpu(AOPassTimer(p), Device);
		if (p == 3) {
			IDirect3DBaseTexture9* inputs[4] = { Textures[0], depth, scene, SkyVis ? (IDirect3DBaseTexture9*)SkyVis : depth };
			if (!Resources(resources, count, inputs, 4, extUniforms.Buffer)) return false;
			return ExtFullScreen(Device, RenderTarget, RenderedSurface, nullptr, extCombine.Pipeline, resources, count, &push, sizeof(push), false);
		}
		bool drawn;
		if (p == 0) {
			IDirect3DBaseTexture9* inputs[4] = { depth, normals, noise, OrthoMap ? (IDirect3DBaseTexture9*)OrthoMap : depth };
			drawn = Resources(resources, count, inputs, 4, extUniforms.Buffer) && ExtDrawTo(estimate, &Surfaces[0], 1, resources, count, &push, sizeof(push));
		}
		else {
			IDirect3DBaseTexture9* inputs[3] = { Textures[p == 1 ? 0 : 1], depth, normals };
			drawn = Resources(resources, count, inputs, 3, extUniforms.Buffer) && ExtDrawTo(extBlur.Pipeline, &Surfaces[p == 1 ? 1 : 0], 1, resources, count, &push, sizeof(push));
		}
		if (!drawn) return false;
	}
	return true;
}

bool AmbientOcclusionEffect::ExtSkyVisibility(IDirect3DSurface9* Target, const D3DSURFACE_DESC& Desc, IDirect3DTexture9* OrthoMap, IDirect3DTexture9* SunMask, IDirect3DTexture9* SmoothedNormals) {
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = SlotTexture("TESR_NormalsBuffer");
	if (!depth || !normals || !OrthoMap || !Desc.Width || !Desc.Height) return false;
	MakePipeline(extSky, "AmbientOcclusionSky", 5);
	const D3DXVECTOR4 skyLayout(1.0f / Desc.Width, 1.0f / Desc.Height, SmoothedNormals ? 1.0f : 0.0f, 0.0f);
	if (!extSky.Pipeline || !ExtConstants(extSkyUniforms, D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f), skyLayout)) return false;
	IDirect3DBaseTexture9* inputs[5] = { depth, normals, OrthoMap, SunMask ? (IDirect3DBaseTexture9*)SunMask : depth,
		SmoothedNormals ? (IDirect3DBaseTexture9*)SmoothedNormals : depth };
	NVR_EXT_RESOURCE_BINDING resources[9] = {};
	UINT count = 0;
	AOPush push = {};
	return Resources(resources, count, inputs, 5, extSkyUniforms.Buffer) && ExtDrawTo(extSky.Pipeline, &Target, 1, resources, count, &push, sizeof(push));
}

void AmbientOcclusionEffect::RenderSkyVisibility(IDirect3DDevice9* Device, IDirect3DTexture9* smoothedNormals) {
	IDirect3DTexture9* orthoMap = TheShaderManager->Effects.ShadowsExteriors ? TheShaderManager->Effects.ShadowsExteriors->ShadowMapOrthoTexture : nullptr;
	IDirect3DTexture9* sunMask = TheShaderManager->Effects.ShadowsExteriors && TheShaderManager->Effects.ShadowsExteriors->Enabled ?
		TheShaderManager->Effects.ShadowsExteriors->Textures.ShadowPassTexture : nullptr;
	D3DXHANDLE technique = TechniqueHandle(techSky, "SkyVisibilityV2");
	static bool createTried[2] = { false, false };
	const int which = lastLowRes ? 1 : 0;
	static D3DFORMAT createdFormat[2] = { D3DFMT_UNKNOWN, D3DFMT_UNKNOWN };
	const D3DFORMAT wanted = SkyReflections || SkyLightCap > 0.0f ? D3DFMT_A16B16G16R16F : D3DFMT_G16R16F;
	if (createTried[which] && createdFormat[which] != wanted) {
		IDirect3DTexture9*& texture = lastLowRes ? skyTextureLow : skyTexture;
		IDirect3DSurface9*& surface = lastLowRes ? skySurfaceLow : skySurface;
		if (surface) { surface->Release(); surface = nullptr; }
		if (texture) { texture->Release(); texture = nullptr; }
		skyReady = false;
		createTried[which] = false;
	}
	if (technique && orthoMap && !createTried[which]) {
		createTried[which] = true;
		createdFormat[which] = wanted;
		if (lastLowRes)
			TheTextureManager->InitTexture("NVR_SkyVisBufferLow", &skyTextureLow, &skySurfaceLow, MaxQuality::Reduced(TheRenderManager->width, 4), MaxQuality::Reduced(TheRenderManager->height, 4), wanted);
		else
			TheTextureManager->InitTexture("NVR_SkyVisBuffer", &skyTexture, &skySurface, MaxQuality::Reduced(TheRenderManager->width, 2), MaxQuality::Reduced(TheRenderManager->height, 2), wanted);
	}
	IDirect3DSurface9* target = lastLowRes ? skySurfaceLow : skySurface;
	D3DSURFACE_DESC desc = {};
	if (!technique || !orthoMap || !target || FAILED(target->GetDesc(&desc))) return;
	static GpuTimer skyTimer("  Sky visibility v2");
	GpuProfileScope gpu(skyTimer, Device);
	D3DVIEWPORT9 original = {};
	Device->GetViewport(&original);
	D3DVIEWPORT9 quarter = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
	SkyData4.z = sunMask ? 1.0f : 0.0f;
	Effect->SetTechnique(technique);
	D3DXHANDLE smoothHandle = ParameterHandle(paramSkySmooth, "NVR_SkySmooth");
	if (!smoothHandle) smoothedNormals = nullptr;
	if (smoothHandle) { D3DXVECTOR4 smooth(smoothedNormals ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f); Effect->SetVector(smoothHandle, &smooth); }
	HRESULT result = E_FAIL;
	if (!ExtFailed && DxvkCompute::PassesOn() && ExtSkyVisibility(target, desc, orthoMap, sunMask, smoothedNormals)) result = S_OK;
	else {
	const bool wasEnabled = Enabled;
	Enabled = true;
	SetCT();
	Enabled = wasEnabled;
	UINT passes = 0;
	IDirect3DSurface9* depthSurface = nullptr;
	Device->GetDepthStencilSurface(&depthSurface);
	Device->SetDepthStencilSurface(nullptr);
	result = Device->SetRenderTarget(0, target);
	if (SUCCEEDED(result)) result = Device->SetViewport(&quarter);
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		result = Effect->BeginPass(0);
		if (SUCCEEDED(result)) {
			Device->SetTexture(6, orthoMap);
			Device->SetTexture(8, sunMask);
			if (smoothedNormals) { Device->SetTexture(9, smoothedNormals); TheRenderManager->renderState->NoteDeviceTexture(9, smoothedNormals); }
			TheRenderManager->renderState->NoteDeviceTexture(6, orthoMap);
			TheRenderManager->renderState->NoteDeviceTexture(8, sunMask);
			RenderRect::ApplyViewport(Device);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Effect->EndPass();
		}
		Effect->End();
	}
	Device->SetTexture(6, nullptr);
	Device->SetTexture(8, nullptr);
	if (smoothedNormals) { Device->SetTexture(9, nullptr); TheRenderManager->renderState->NoteDeviceTexture(9, nullptr); }
	TheRenderManager->renderState->NoteDeviceTexture(6, nullptr);
	TheRenderManager->renderState->NoteDeviceTexture(8, nullptr);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	}
	skyReady = SUCCEEDED(result);
	static D3DFORMAT reported = D3DFMT_UNKNOWN;
	if (skyReady && reported != desc.Format) {
		Logger::Log("UNOFFICIAL Skylighting v2 active: sky factor %ux%u %s (sun mask %s), applied to the sky's share%s.", desc.Width, desc.Height,
			desc.Format == D3DFMT_A16B16G16R16F ? "A16B16G16R16F (b: the visibility, for the sky reflections and LightCap)" : "G16R16F", sunMask ? "yes" : "no",
			SkyHostedByAmbientLight() ? " (in AmbientLight's apply)" : "");
		reported = desc.Format;
	}
}

bool AmbientOcclusionEffect::ShouldRender() {
	return Constants.Enabled && !bNVAOLoaded;
}

void AmbientOcclusionEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
	IDirect3DSurface9* SourceBuffer) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }
	if (!combineOnly) lastLowRes = UseLowRes();
	if (!combineOnly && SkyData2.x > 0.0f) {
		RenderSkyVisibility(Device);
		Device->SetRenderTarget(0, RenderTarget);
	}
	IDirect3DTexture9* const* textures = lastLowRes ? aoTextureLow : aoTexture;
	IDirect3DSurface9* const* surfaces = lastLowRes ? aoSurfaceLow : aoSurface;
	const UINT divisor = MaxQuality::Divisor(lastLowRes ? 4 : 2);
	D3DXHANDLE technique = TechniqueHandle(techDedicated, "DedicatedAO");
	if (FastPaths) {
		D3DXHANDLE fast = TechniqueHandle(techFast, "DedicatedAOFast");
		if (fast) technique = fast;
	}
	bool gtaoTechnique = false;
	if (GTAOData.x > 0.5f) {
		D3DXHANDLE gtao = FastPaths ? TechniqueHandle(techGTAOFast, "DedicatedGTAOFast") : NULL;
		if (!gtao) gtao = TechniqueHandle(techGTAO, "DedicatedGTAO");
		if (gtao) { technique = gtao; gtaoTechnique = true; }
	}
	D3DXHANDLE layoutHandle = ParameterHandle(paramLayout, "NVR_AOLayout");
	D3DXTECHNIQUE_DESC description = {};
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC target = {}, scratch = {}, aoTarget = {};
	IDirect3DSurface9* depthSurface = nullptr;
	if (packedAOFailed || !technique || !layoutHandle || FAILED(Effect->GetTechniqueDesc(technique, &description)) ||
		description.Passes != 4 || !RenderedSurface || !SourceBuffer || !textures[0] || !textures[1] ||
		!surfaces[0] || !surfaces[1] ||
		FAILED(Device->GetViewport(&original)) || FAILED(RenderTarget->GetDesc(&target)) ||
		FAILED(RenderedSurface->GetDesc(&scratch)) || FAILED(surfaces[0]->GetDesc(&aoTarget)) ||
		original.X || original.Y ||
		!((original.Width == target.Width && original.Height == target.Height) || (RenderRect::Active() &&
			original.Width == RenderRect::Width(target.Width) && original.Height == RenderRect::Height(target.Height))) ||
		scratch.Width != target.Width || scratch.Height != target.Height ||
		aoTarget.Width != (target.Width + divisor - 1) / divisor || aoTarget.Height != (target.Height + divisor - 1) / divisor ||
		aoTarget.Format != D3DFMT_G16R16F ||
		target.MultiSampleType != D3DMULTISAMPLE_NONE) {
		if (deferCombine) return;
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	original.Width = target.Width;
	original.Height = target.Height;
	auto timer = TimeLogger();
	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);
	IDirect3DSurface9* finalTarget = chained ? chain.Output() : RenderTarget;
	D3DVIEWPORT9 reduced = original;
	reduced.Width = aoTarget.Width;
	reduced.Height = aoTarget.Height;
	D3DXVECTOR4 layout((float)reduced.Width / original.Width,
		(float)reduced.Height / original.Height, 1.0f / reduced.Width, 1.0f / reduced.Height);
	IDirect3DTexture9* scene = TheTextureManager->RenderedTexture;
	IDirect3DTexture9* skyVis = !deferCombine ? SkyTexture() : nullptr;
	SkyData4.w = skyVis ? 1.0f : 0.0f;
	IDirect3DTexture9* orthoMap = SkyData.x > 0.0f && TheShaderManager->Effects.ShadowsExteriors ? TheShaderManager->Effects.ShadowsExteriors->ShadowMapOrthoTexture : nullptr;
	static bool reported[2] = {};
	auto reportActive = [&]() {
		if (reported[lastLowRes]) return;
		Logger::Log("UNOFFICIAL dedicated AO active: %ux%u G16R16F ping-pong%s.", reduced.Width, reduced.Height,
			lastLowRes ? " (AOLowRes)" : "");
		reported[lastLowRes] = true;
	};
	if (!ExtFailed && DxvkCompute::PassesOn() && ExtDedicated(Device, RenderTarget, RenderedSurface, textures, surfaces, layout, gtaoTechnique, orthoMap, skyVis)) {
		if (deferCombine) deferredReady = true;
		reportActive();
		renderTime = timer.LogTime(deferCombine ? "AmbientOcclusion::Deferred" : "AmbientOcclusion::PackedAO");
		return;
	}
	HRESULT result = scene ? Device->GetDepthStencilSurface(&depthSurface) : E_FAIL;
	if (SUCCEEDED(result) && !ReShadeIntegration::KeepDepthBound()) result = Device->SetDepthStencilSurface(nullptr);
	if (FAILED(result)) {
		if (depthSurface) depthSurface->Release();
		return;
	}
	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	Device->SetTexture(6, orthoMap);
	TheRenderManager->renderState->NoteDeviceTexture(6, orthoMap);
	UINT passes = 0;
	result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		const UINT firstPass = combineOnly && passes ? passes - 1 : 0;
		const UINT endPass = deferCombine && passes ? passes - 1 : passes;
		for (UINT p = firstPass; p < endPass && SUCCEEDED(result); ++p) {
			const bool combine = p == passes - 1;
			Device->SetTexture(5, nullptr);
			IDirect3DSurface9* destination = combine ? finalTarget : surfaces[p == 1 ? 1 : 0];
			result = Device->SetRenderTarget(0, destination);
			if (SUCCEEDED(result)) result = Device->SetViewport(combine ? &original : &reduced);
			if (FAILED(result)) break;
			result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			IDirect3DBaseTexture9* stage5 = p == 1 ? textures[0] : p == 2 ? textures[1] : combine ? textures[0] : nullptr;
			if (stage5) Device->SetTexture(5, stage5);
			if (combine && skyVis) {
				Device->SetTexture(7, skyVis);
				TheRenderManager->renderState->NoteDeviceTexture(7, skyVis);
			}
			Device->SetTexture(2, scene);
			TheRenderManager->renderState->NoteDeviceTexture(5, stage5);
			TheRenderManager->renderState->NoteDeviceTexture(2, scene);
			RenderRect::ApplyViewport(Device);
			{
				GpuProfileScope gpu(AOPassTimer(p), Device);
				result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			}
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	Device->SetTexture(5, nullptr);
	if (orthoMap) { Device->SetTexture(6, nullptr); TheRenderManager->renderState->NoteDeviceTexture(6, nullptr); }
	if (skyVis) { Device->SetTexture(7, nullptr); TheRenderManager->renderState->NoteDeviceTexture(7, nullptr); }
	Device->SetTexture(2, TheTextureManager->SourceTexture);
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	if (SUCCEEDED(result) && deferCombine) {
		deferredReady = true;
		reportActive();
		renderTime = timer.LogTime("AmbientOcclusion::Deferred");
		return;
	}
	if (SUCCEEDED(result)) {
		if (chained) chain.Commit();
		else result = Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
	}
	if (FAILED(result)) {
		Device->StretchRect(RenderedSurface, NULL, RenderTarget, NULL, D3DTEXF_NONE);
		packedAOFailed = true;
		Logger::Log("Packed AO failed (%08lx); using legacy AO until restart.", result);
		if (deferCombine) return;
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	reportActive();
	renderTime = timer.LogTime("AmbientOcclusion::PackedAO");
}
