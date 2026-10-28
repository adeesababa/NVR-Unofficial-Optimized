#include "AmbientOcclusion.h"
#include "../core/GpuProfiler.h"
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-25 (NVR DXVK)
#include "../core/RenderRect.h"	// UNOFFICIAL 2026-10-27 RenderScale v2
#include "../core/RtAmbient.h"
#include "../core/RtCore.h"	// (the RT core: AddUser / CopyIn / AddPass / CopyOut)	// UNOFFICIAL 2026-10-28 (fork RT) [Shaders.AmbientOcclusion.RT]
#include "BounceLightMath.h"	// (the RT AO's temporal pass)

// UNOFFICIAL 2026-10-16 GTAO ([Shaders.AmbientOcclusion.GTAO]; AmbientOcclusion.fx.hlsl GTAOValue): x 1 on (the DedicatedGTAO
// technique instead of DedicatedAO), w largest search radius in pixels. Kept out of AmbientOcclusion.h.
static D3DXVECTOR4 GTAOData(0.0f, 0.0f, 0.0f, 64.0f);
static EffectRecord::CachedHandle techGTAO;
// UNOFFICIAL 2026-10-17 Skylighting ([Shaders.AmbientOcclusion.Skylighting]; AmbientOcclusion.fx.hlsl SkyVisibility): x 1 on (exteriors
// only, set each frame), y strength, z ray length in units, w depth slack in ortho map units. Needs the ortho map, which is asked for here.
static D3DXVECTOR4 SkyData(0.0f, 1.0f, 600.0f, 0.0001f);
static bool SkyEnabled = false;
// UNOFFICIAL 2026-10-17 Skylighting v2 ([Shaders.AmbientOcclusion.Skylighting] Version 2; deep roadmap 3.4, evidence skylight2-LOG.md).
// Its own pass (SkyVisibilityV2) at the AO's resolution works out the factor for the SKY's share of each pixel's light only and
// writes it to a target the size of the AO's; the composites (VolumetricFog.fx CompositeAO, AmbientOcclusion.fx DedicatedCombine)
// multiply by it, upsampled with the AO's own weights (no new constants: the fog's composite pass has c219-c223 left with the
// physical sky). v1 multiplied the AO, i.e. the whole pixel with its sunlight. Sky2: x 1 on (exteriors, AO on), y strength, z the
// sky (ambient) light's luminance (linear, x AmbientScale), w the sun's (linear). Sky3: x 1 normal from the depth, y 1 / OpenThreshold,
// z 1 share apply (0 = on the whole pixel, for A/B), w debug view (1 visibility, 2 sky share, 3 final factor, 4 covered = 0). Sky4: x ray length (units), y soft width of the map
// test (units), z 1 = the sun shadow mask is bound for the pass (stage 8), w 1 = the factor is bound for DedicatedCombine (stage 7).
static D3DXVECTOR4 SkyData2(0.0f, 1.0f, 0.0f, 0.0f);
static D3DXVECTOR4 SkyData3(0.0f, 0.0f, 1.0f, 0.0f);
static D3DXVECTOR4 SkyData4(800.0f, 40.0f, 0.0f, 0.0f);
static float SkyAmbientScale = 1.0f;
// UNOFFICIAL 2026-10-20 (skyamb; user: under a neon canopy at dusk the lamp-lit wall and the glowing bulbs went nearly black): the share
// (Includes/SkyShare.hlsl) knows only the sun and the sky, so in shade everything counts as sky light and a pixel under a wide cover
// (V ~ 0) lost all of it, lamps and glow included. [Shaders.AmbientOcclusion.Skylighting] LightCap: the composites take away at most
// (1 - V) x LightCap x Strength x the sky's light on a white surface (linear luminance of TESR_SunAmbient x AmbientScale), whatever the share says,
// so light the sky cannot explain (lamps, glow, emissive) stays. Sky5: x that light (linear luminance), w 0; LightCap 0 or a debug
// view: x 0, w 1e6 (no limit: the factor as before). Needs the sky target's b (the visibility): A16B16G16R16F while it is on.
static D3DXVECTOR4 SkyData5(0.0f, 0.0f, 0.0f, 1.0e6f);
static float SkyLightCap = 0.0f;
static float SkyOpenSides = 0.0f;	// UNOFFICIAL 2026-10-20 (skyamb) OpenSides (Sky5.y; Includes/SkyVisibility.hlsl)
static int SkyVersion = 2;
// UNOFFICIAL 2026-10-28 (fork RT) [Shaders.AmbientOcclusion.RT] (core/RtAmbient.h): the RT AO's temporal pass (AmbientOcclusionTemporalRT.frag),
// AmbientLight's Accumulate rule: RtReproject this frame's view space -> last frame's clip (BounceLightMath.h); RtTemporal x last frame's
// share (0.9, 0 without a usable history), y the motion in AO pixels that halves it, z the frame number, w the RT DebugView (the RT
// passes read it).
static D3DXMATRIX RtReproject;
static D3DXVECTOR4 RtTemporal(0.0f, 4.0f, 0.0f, 0.0f);
static D3DXMATRIX RtLastView, RtLastInvView, RtLastProj;
static double RtLastTime = -1.0;
static bool RtHistoryValid = false;
static UInt32 RtFrame = 0;

static float LinearLuma(const D3DXVECTOR4& c) {
	return 0.2126f * powf(max(c.x, 0.0f), 2.2f) + 0.7152f * powf(max(c.y, 0.0f), 2.2f) + 0.0722f * powf(max(c.z, 0.0f), 2.2f);
}

bool AmbientOcclusionEffect::SkyV2Active() { return SkyData2.x > 0.0f; }
// UNOFFICIAL 2026-10-20 (skyamb) LightCap (SkyData5, Includes/SkyTake.hlsl): (the most light the sky may take: LightCap x Strength x the
// sky's linear luminance x AmbientScale (SkyData2.z), no limit).
D3DXVECTOR2 AmbientOcclusionEffect::SkyTakeComposite() {
	const bool limit = SkyLightCap > 0.0f && SkyData3.w < 0.5f;	// (debug views: the raw values)
	return limit ? D3DXVECTOR2(SkyLightCap * SkyData2.y * SkyData2.z, 0.0f) : D3DXVECTOR2(0.0f, 1.0e6f);
}
// UNOFFICIAL 2026-10-20 (skyamb): AmbientLight renders this frame (its ShouldRender: on, loaded, not under water) and hosts the sky test
// (this effect's file loaded: the sky pass and the sky target are this effect's).
bool AmbientOcclusionEffect::SkyHostedByAmbientLight() {
	AmbientLightEffect* al = TheShaderManager->Effects.AmbientLight;
	AmbientOcclusionEffect* ao = TheShaderManager->Effects.AmbientOcclusion;
	return al && al->ShouldRender() && ao && ao->Effect;
}
// UNOFFICIAL 2026-10-17 FastPaths ([Shaders.AmbientOcclusion.FastPaths] Enabled): the DedicatedAOFast technique = DedicatedAO with
// its far-pixel early returns made real (the effect compiler had flattened them: sky and distant land ran the whole kernel and
// both blurs). Same picture. With GTAO on it picks DedicatedGTAOFast instead. Off, or an effect file without the technique:
// DedicatedAO / DedicatedGTAO as before.
static bool FastPaths = false;
static EffectRecord::CachedHandle techFast, techGTAOFast;

void AmbientOcclusionEffect::UpdateConstants() {
	UpdateSkyConstants();
}

// UNOFFICIAL 2026-10-20 (skyamb): the sky part of UpdateConstants, also run by ShaderManager every frame this effect is off. With
// AmbientLight rendering, the sky test runs for it (Version 2 whatever Version says: Version 1 darkens through this effect's own AO,
// which does not run then), whatever this effect's Enabled settings are; otherwise as before (this effect on, its Enabled setting).
void AmbientOcclusionEffect::UpdateSkyConstants() {
	const bool hosted = SkyHostedByAmbientLight();
	const bool sky = SkyEnabled && TheShaderManager->GameState.isExterior && (hosted || (Enabled && Constants.Enabled));
	SkyData.x = sky && !hosted && SkyVersion == 1 ? 1.0f : 0.0f;
	SkyData2.x = sky && (hosted || SkyVersion == 2) ? 1.0f : 0.0f;
	SkyData2.z = LinearLuma(TheShaderManager->ShaderConst.sunAmbient) * SkyAmbientScale;	// (Includes/SkyShare.hlsl SkyShare)
	SkyData2.w = LinearLuma(TheShaderManager->ShaderConst.sunColor);
	{	// UNOFFICIAL 2026-10-20 (skyamb) LightCap (above)
		const D3DXVECTOR2 take = SkyTakeComposite();
		SkyData5 = D3DXVECTOR4(take.x, SkyOpenSides, 0.0f, take.y);
	}
	if (SkyData.x > 0.0f) TheShaderManager->orthoRequired = true;	// UNOFFICIAL Skylighting v1 reads the ortho map
	if (SkyData2.x > 0.0f && !RtAmbient::SkyReady()) TheShaderManager->orthoSkyRequired = true;	// v2: amortised + clean map (ShadowManager); not for the RT sky (2026-10-28)
	static bool skyWasActive = false;
	TheShaderManager->orthoSkyFresh = SkyData2.x > 0.0f && !skyWasActive;
	skyWasActive = SkyData2.x > 0.0f;
}

void AmbientOcclusionEffect::RegisterConstants() {
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionAOData"] = &Constants.AOData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionData"] = &Constants.Data;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionGTAO"] = &GTAOData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky"] = &SkyData;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky2"] = &SkyData2;	// UNOFFICIAL Skylighting v2
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky3"] = &SkyData3;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky4"] = &SkyData4;
	TheShaderManager->ConstantsTable["TESR_AmbientOcclusionSky5"] = &SkyData5;	// UNOFFICIAL 2026-10-20 (skyamb) LightCap
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
	// UNOFFICIAL Skylighting v2: the factor at the AO's resolution (half, or quarter with AOLowRes); only written while v2 is on.
	// fixes-b #3: NOT created here (about 4.4 MiB, 17.6 MiB with Max Quality, for nothing while Skylighting is off): the one the
	// first v2 draw needs is created in RenderSkyVisibility.
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
	SkyReflections = TheSettingManager->GetSettingI("Shaders.AmbientOcclusion.Skylighting", "Reflections") != 0;	// UNOFFICIAL 2026-10-19
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
	// UNOFFICIAL 2026-10-28 (fork RT): [Shaders.AmbientOcclusion.RT] (AmbientLight reads it too). Its DebugView 2 (the RT sky visibility)
	// shows through Skylighting's view 1 (the visibility), so the composites draw it as they draw the ortho version's.
	RtAmbient::UpdateSettings();
	if (RtAmbient::Current.Enabled && RtAmbient::Current.DebugView == 2) SkyData3.w = 1.0f;
}

/*
* UNOFFICIAL 2026-10-25 (NVR DXVK, [Main.Main.Performance] NvrDxvkPasses): the Dedicated techniques' passes (Estimate, BlurX, BlurY,
* Combine; the Fast and GTAO variants by the same shaders: the Fast ones compute the same picture) and SkyVisibilityV2 as Vulkan
* passes (Compute\AmbientOcclusion*.frag, the .fx.hlsl line for line): the estimate, the blurs and the sky pass into the effect's own
* targets (ExtDrawTo), the combine through the frame chain (ExtFullScreen). The constants go into one uniform buffer per call
* (AmbientOcclusion.glsl's block); the blur axis and the estimate's kind into the push block. The legacy technique (older effect
* files, or after a failure) stays D3D9.
*/
namespace {
	struct AOPush {
		D3DXVECTOR2 axis;				// 0: DedicatedBlur's axis
		UInt32 gtaoEstimate;			// 8: 1 = GTAOEstimate
		UInt32 linearSampler;			// 12: LINEAR, CLAMP (filled in by DXVK)
		UInt32 pointSampler;			// 16: POINT, CLAMP
		UInt32 pointWrapSampler;		// 20: POINT, WRAP
	};
	struct AOUniforms {
		D3DXMATRIX projection, invProjection, invView, shadowOrtho;	// as stored (see AmbientOcclusion.glsl)
		D3DXVECTOR4 aoData, data, gtao, sky, sky2, sky3, sky4, sky5, smoothedSunDir, shadowFade, reciprocalResolution, fogData, fogColor,
			depthConstants, cameraData, layout, skyLayout;
		D3DXMATRIX rtReproject;			// UNOFFICIAL 2026-10-28 (fork RT): read only by the RT passes (AmbientOcclusion.glsl)
		D3DXVECTOR4 rtTemporal;
	};


	// The constants from the same registered memory the D3D9 path uploads (SetCT); null when not registered.
	const D3DXVECTOR4* RegisteredConstant(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}

	IDirect3DBaseTexture9* SlotTexture(const char* name) {
		std::string slotName = name;
		IDirect3DBaseTexture9** slot = TheTextureManager->GetTextureSlotByName(slotName);
		return slot ? *slot : nullptr;
	}

	// Textures in set 1 (binding i, slot i), the constants in set 2 binding 0, the three samplers' heap indices at push offsets 12-20.
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

	// The resources of one draw, in MakePipeline's order. False: a texture or sampler is missing.
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

	// UNOFFICIAL 2026-10-28 (fork RT): MakePipeline with Targets (the temporal pass writes two) that never fails the other Vulkan passes
	// (FailAll false; a missing .spv is checked first): the RT pass is then simply not drawn and NVR's own pass runs.
	void MakePipelineRT(EffectRecord::ExtPass& pass, const char* spvName, UINT textureCount, UINT targets) {
		if (pass.Tried) return;
		pass.Tried = true;
		if (!DxvkCompute::HasSpirv(spvName)) { Logger::Log("UNOFFICIAL RT AO: Compute\\%s.spv is missing; NVR's own pass runs.", spvName); return; }
		NVR_EXT_BINDING_DESC bindings[9] = {};
		UINT count = 0;
		for (UINT i = 0; i < textureCount; i++, count++) {
			bindings[count].Set = 1; bindings[count].Binding = i; bindings[count].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[count].Slot = i; bindings[count].ViewType = NVR_EXT_VIEW_2D;
		}
		bindings[count].Set = 2; bindings[count].Binding = 0; bindings[count].Type = NVR_EXT_BIND_UNIFORM_BUFFER; bindings[count].Slot = 0; count++;
		for (UINT s = 0; s < 3; s++, count++) { bindings[count].Type = NVR_EXT_BIND_SAMPLER; bindings[count].Slot = s; bindings[count].PushOffset = 12 + 4 * s; }
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvName, bindings, count, sizeof(AOPush), true, targets, NVR_EXT_BLEND_NONE, false);
		if (!pass.Pipeline) Logger::Log("UNOFFICIAL RT AO: Compute\\%s.spv could not be made into a pass; NVR's own pass runs.", spvName);
	}

	// Sub-pass timings (nested inside "Ambient occlusion") to direct further AO work; either path.
	GpuTimer& AOPassTimer(UINT p) {
		static GpuTimer passTimers[4] = { GpuTimer("  AO estimate (half)"), GpuTimer("  AO blur X (half)"),
			GpuTimer("  AO blur Y (half)"), GpuTimer("  AO combine (full)") };
		return passTimers[p < 4 ? p : 3];
	}
}

// This call's constants (the sky pass and the AO passes each keep a buffer: both are drawn in one frame). Layout: NVR_AOLayout;
// SkyLayout: xy 1 / the sky target's size, z NVR_SkySmooth.x.
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
	// ShadowsExteriors' (absent without it: 0, as the effect's unset parameters)
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
	uniforms.rtReproject = RtReproject;
	uniforms.rtTemporal = RtTemporal;
	uniforms.rtTemporal.w = (float)RtAmbient::Current.DebugView;
	return ExtUniforms(Uniforms, &uniforms, sizeof(uniforms)) != nullptr;
}

// The Dedicated technique's passes as Render's D3D9 loop draws them: 0 Estimate -> AO 0, 1 BlurX AO 0 -> AO 1, 2 BlurY AO 1 -> AO 0,
// 3 Combine AO 0 -> the frame chain (never the direct final write: the D3D9 combine commits). combineOnly: the combine alone;
// deferCombine: without it. False: nothing of the frame was committed (the D3D9 passes draw it; ExtFailed is set when a pass failed).
bool AmbientOcclusionEffect::ExtDedicated(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	IDirect3DTexture9* const* Textures, IDirect3DSurface9* const* Surfaces, const D3DXVECTOR4& Layout, bool GTAOEstimate, IDirect3DTexture9* OrthoMap, IDirect3DTexture9* SkyVis) {
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = SlotTexture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* noise = ExtFileTexture("TESR_BlueNoiseSampler");
	IDirect3DBaseTexture9* scene = TheTextureManager->RenderedTexture;
	if (!depth || !normals || !noise || !scene || (SkyData.x > 0.0f && !OrthoMap)) return false;
	// UNOFFICIAL 2026-10-26: PackedEstimate's or GTAOEstimate's own variant (Compute\variants.txt), else the general shader.
	ID3D9NvrExtPipeline* estimate = ExtVariant(extVariants, extEstimate, "AmbientOcclusionEstimate", GTAOEstimate ? "g1" : "g0", [](const char* name) {
		EffectRecord::ExtPass made;
		MakePipeline(made, name, 4);
		return made.Pipeline;
	});
	MakePipeline(extBlur, "AmbientOcclusionBlur", 3);
	MakePipeline(extCombine, "AmbientOcclusionCombine", 4);
	// UNOFFICIAL 2026-10-28 (fork RT): the RT estimate's temporal constants (into this call's uniforms, below) while it runs.
	D3DSURFACE_DESC aoDesc = {};
	const bool rt = !combineOnly && RtAmbient::Ready() && SUCCEEDED(Surfaces[0]->GetDesc(&aoDesc));
	if (rt) {
		const double now = TheFrameRateManager->Time;
		const bool continuous = RtHistoryValid && rtHistoryWidth == aoDesc.Width && rtHistoryHeight == aoDesc.Height && now - RtLastTime >= 0.0 &&
			now - RtLastTime < 0.5 && !TheShaderManager->GameState.isCameraCut && !RenderRect::ScaleChanged();
		D3DXMatrixIdentity(&RtReproject);
		if (continuous) BounceLightReprojection(TheRenderManager->viewMatrix, TheRenderManager->InvViewMatrix, RtLastView, RtLastInvView, RtLastProj, &RtReproject);
		RtTemporal = D3DXVECTOR4(continuous ? 0.9f : 0.0f, 4.0f, (float)(RtFrame % 1024), 0.0f);
	}
	if (!estimate || !extBlur.Pipeline || !extCombine.Pipeline || !ExtConstants(extUniforms, Layout, D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f))) return false;
	const UINT firstPass = combineOnly ? 3 : 0;
	const UINT endPass = deferCombine ? 3 : 4;
	for (UINT p = firstPass; p < endPass; ++p) {
		AOPush push = {};
		push.axis = p == 1 ? D3DXVECTOR2(1.0f, 0.0f) : D3DXVECTOR2(0.0f, 1.0f);	// (io.xy, io.yx)
		push.gtaoEstimate = GTAOEstimate ? 1 : 0;
		NVR_EXT_RESOURCE_BINDING resources[8] = {};
		UINT count = 0;
		GpuProfileScope gpu(AOPassTimer(p), Device);
		if (p == 3) {
			// Stand-ins for what the pass does not read this frame (SkyData4.w 0: no sky factor).
			IDirect3DBaseTexture9* inputs[4] = { Textures[0], depth, scene, SkyVis ? (IDirect3DBaseTexture9*)SkyVis : depth };
			if (!Resources(resources, count, inputs, 4, extUniforms.Buffer)) return false;
			return ExtFullScreen(Device, RenderTarget, RenderedSurface, nullptr, extCombine.Pipeline, resources, count, &push, sizeof(push), false);
		}
		bool drawn;
		if (p == 0 && rt && ExtEstimateRT(Device, Textures, Surfaces, aoDesc)) drawn = true;	// UNOFFICIAL 2026-10-28 (fork RT): into AO 0 like Estimate
		else if (p == 0) {
			IDirect3DBaseTexture9* inputs[4] = { depth, normals, noise, OrthoMap ? (IDirect3DBaseTexture9*)OrthoMap : depth };	// (no Skylighting v1: never read)
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

// SkyVisibilityV2 into Target (the sky target the D3D9 pass draws). The D3D9 pass's stage 8 and 9 textures: null = not read this frame.
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

// UNOFFICIAL 2026-10-28 (fork RT, [Shaders.AmbientOcclusion.RT]; core/RtAmbient.h, docs/next/FORK-RT-CORE.md): this effect is the RT
// core's user "RT ambient occlusion". At the core's point of the frame (RtCore::Frame: depth and normals exist; the sky, AO and
// AmbientLight passes come later in the frame) RtRecord draws RtAOInput.frag (view distance + world normal per half-resolution texel)
// into NVR_RTAOInput, copies it into the shared input image, adds the helper's RtAOTrace.comp pass and copies its output back into
// NVR_RTAORaw (AO visibility, sky visibility, covered, DebugView 3's value). RtRaw: that texture if it was traced this frame, else null
// (the callers keep NVR's own passes).
namespace {
	int RtImageIn = -1, RtImageOut = -1, RtShader = -1;
	UInt32 RtTracedFrame = 0xFFFFFFFF;
	UInt32 RtReduced(UInt32 size) { return MaxQuality::Reduced(size, 2); }

	bool RtWants() {
		AmbientOcclusionEffect* ao = TheShaderManager->Effects.AmbientOcclusion;
		return RtAmbient::Current.Enabled && DxvkCompute::PassesOn() && ao && ao->Effect;
	}
	void RtSetup() {
		const UINT w = RtReduced(TheRenderManager->width), h = RtReduced(TheRenderManager->height);
		RtImageIn = RtCore::AddImage("NVR RT ao input", w, h, NVR_EXT_SHARED_RG32F);
		RtImageOut = RtCore::AddImage("NVR RT ao", w, h, NVR_EXT_SHARED_RGBA16F);
	}
	void RtRecordUser(IDirect3DDevice9* Device) {
		if (AmbientOcclusionEffect* ao = TheShaderManager->Effects.AmbientOcclusion) ao->RtRecord(Device);
	}
}

void AmbientOcclusionEffect::RtRegister() {
	RtShader = RtCore::AddShader("RtAOTrace");
	RtCore::AddUser({ "RT ambient occlusion", RtWants, RtSetup, RtRecordUser });
}

IDirect3DTexture9* AmbientOcclusionEffect::RtRaw() {
	return RtTracedFrame == TheFrameRateManager->FrameNumber && RtAmbient::Ready() ? rtRawTexture : nullptr;
}

void AmbientOcclusionEffect::RtRecord(IDirect3DDevice9* Device) {
	static bool failed = false;
	static UInt32 traced = 0;
	if (failed || !Effect || RtImageIn < 0 || RtImageOut < 0 || RtShader < 0) return;
	const UINT width = RtReduced(TheRenderManager->width), height = RtReduced(TheRenderManager->height);
	if (!rtInputTexture || rtWidth != width || rtHeight != height) {
		DxvkCompute::Drop(rtInputSurface); DxvkCompute::Drop(rtInputTexture); DxvkCompute::Drop(rtRawSurface); DxvkCompute::Drop(rtRawTexture);
		TheTextureManager->InitTexture("NVR_RTAOInput", &rtInputTexture, &rtInputSurface, width, height, D3DFMT_G32R32F);
		TheTextureManager->InitTexture("NVR_RTAORaw", &rtRawTexture, &rtRawSurface, width, height, D3DFMT_A16B16G16R16F);
		rtWidth = width;
		rtHeight = height;
	}
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = SlotTexture("TESR_NormalsBuffer");
	MakePipelineRT(extInputRT, "RtAOInput", 2, 1);
	if (!rtInputTexture || !rtRawTexture || !depth || !normals || !extInputRT.Pipeline) return;
	const D3DXVECTOR4 layout(1.0f, 1.0f, 1.0f / width, 1.0f / height);
	static GpuTimer inputTimer("  AO RT input (half)");
	{
		GpuProfileScope gpu(inputTimer, Device);
		IDirect3DBaseTexture9* inputs[2] = { depth, normals };
		NVR_EXT_RESOURCE_BINDING resources[6] = {};
		UINT count = 0;
		AOPush push = {};
		const bool wasFailed = ExtFailed;
		if (!ExtConstants(extRtUniforms, layout, D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f)) || !Resources(resources, count, inputs, 2, extRtUniforms.Buffer) ||
			!ExtDrawTo(extInputRT.Pipeline, &rtInputSurface, 1, resources, count, &push, sizeof(push))) {
			ExtFailed = wasFailed;
			failed = true;
			Logger::Log("UNOFFICIAL RT AO: the RT input pass failed to draw; NVR's own AO runs from now on.");
			return;
		}
	}
	// The trace's constants (RtAOTrace.comp). View -> scene directions as RtSunShadows does it: the view matrix's columns are right (0),
	// up (1) and forward (2); as rows of a row-vector matrix (cb.M * v in the GLSL = v x M).
	const RtAmbient::Settings& rt = RtAmbient::Current;
	const D3DXMATRIX& view = TheRenderManager->viewMatrix;
	const D3DXMATRIX& proj = TheRenderManager->projMatrix;
	RtAmbient::TraceConstants c = {};
	c.viewToWorld._11 = view._11; c.viewToWorld._12 = view._21; c.viewToWorld._13 = view._31;
	c.viewToWorld._21 = view._12; c.viewToWorld._22 = view._22; c.viewToWorld._23 = view._32;
	c.viewToWorld._31 = view._13; c.viewToWorld._32 = view._23; c.viewToWorld._33 = view._33;
	c.viewToWorld._44 = 1.0f;
	c.projection = D3DXVECTOR4(1.0f / proj._11, 1.0f / proj._22, (float)width, (float)height);
	const NiPoint3* location = Pointers::Generic::CameraLocation;
	c.origin = D3DXVECTOR4(location->x - TheRenderManager->CameraPosition.x, location->y - TheRenderManager->CameraPosition.y,
		location->z - TheRenderManager->CameraPosition.z, (float)(TheFrameRateManager->FrameNumber % 1024));
	c.ao = D3DXVECTOR4(rt.Distance, (float)rt.RayCount, 1.0f, 0.0f);	// (both consumers keep a history: the noise moves)
	c.sky = D3DXVECTOR4(rt.SkyDistance, rt.Sky && SkyData2.x > 0.0f ? (float)rt.SkyRays : 0.0f, 0.0f, 0.0f);
	const int images[2] = { RtImageIn, RtImageOut };
	if (!RtCore::CopyIn(RtImageIn, rtInputTexture) ||
		!RtCore::AddPass(RtShader, images, 2, 0x2, &c, sizeof(c), (width + 7) / 8, (height + 7) / 8) ||
		!RtCore::CopyOut(RtImageOut, rtRawSurface)) return;
	RtTracedFrame = TheFrameRateManager->FrameNumber;
	if (traced++ == 0)
		Logger::Log("UNOFFICIAL RT AO: traced (%ux%u, %d AO rays of %.0f units, sky %s).", width, height, rt.RayCount, rt.Distance,
			c.sky.y > 0.0f ? "on" : "off (Skylighting off or RT Sky off)");
}

// UNOFFICIAL 2026-10-28 (fork RT, [Shaders.AmbientOcclusion.RT]; core/RtAmbient.h): Estimate's place in ExtDedicated. The traced
// visibility (RtRaw) as Estimate's result into AO 1 (AmbientOcclusionEstimateRT.frag), then the temporal pass (AmbientOcclusionTemporalRT.frag) AO 1 + last frame's history -> AO 0 (what
// the blurs read, as Estimate's) + this frame's history. False: nothing usable was drawn (Estimate runs); a failed draw does not take the
// other Vulkan passes down (ExtFailed is put back) but ends the RT AO for the session.
bool AmbientOcclusionEffect::ExtEstimateRT(IDirect3DDevice9* Device, IDirect3DTexture9* const* Textures, IDirect3DSurface9* const* Surfaces, const D3DSURFACE_DESC& Target) {
	static bool rtFailed = false;
	if (rtFailed) return false;
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	if (!depth || !Textures[1] || !Surfaces[0] || !Surfaces[1]) return false;
	if (rtHistoryWidth != Target.Width || rtHistoryHeight != Target.Height) {
		for (int i = 0; i < 2; i++) { DxvkCompute::Drop(rtHistorySurface[i]); DxvkCompute::Drop(rtHistoryTexture[i]); }
		TheTextureManager->InitTexture("NVR_AORTHistory0", &rtHistoryTexture[0], &rtHistorySurface[0], Target.Width, Target.Height, D3DFMT_G16R16F);
		TheTextureManager->InitTexture("NVR_AORTHistory1", &rtHistoryTexture[1], &rtHistorySurface[1], Target.Width, Target.Height, D3DFMT_G16R16F);
		rtHistoryWidth = Target.Width;
		rtHistoryHeight = Target.Height;
		RtHistoryValid = false;
		RtTemporal.x = 0.0f;
	}
	if (!rtHistoryTexture[0] || !rtHistoryTexture[1] || !rtHistorySurface[0] || !rtHistorySurface[1]) return false;
	IDirect3DTexture9* raw = RtRaw();
	if (!raw) return false;
	MakePipelineRT(extEstimateRT, "AmbientOcclusionEstimateRT", 2, 1);
	MakePipelineRT(extTemporalRT, "AmbientOcclusionTemporalRT", 3, 2);
	if (!extEstimateRT.Pipeline || !extTemporalRT.Pipeline) return false;
	static GpuTimer rtTimers[2] = { GpuTimer("  AO RT estimate (half)"), GpuTimer("  AO RT temporal (half)") };
	const bool wasFailed = ExtFailed;
	const int read = rtHistoryIndex, write = rtHistoryIndex ^ 1;
	NVR_EXT_RESOURCE_BINDING resources[8] = {};
	UINT count = 0;
	AOPush push = {};
	bool drawn;
	{
		GpuProfileScope gpu(rtTimers[0], Device);
		IDirect3DBaseTexture9* inputs[2] = { depth, raw };
		drawn = Resources(resources, count, inputs, 2, extUniforms.Buffer) && ExtDrawTo(extEstimateRT.Pipeline, &Surfaces[1], 1, resources, count, &push, sizeof(push));
	}
	if (drawn) {
		GpuProfileScope gpu(rtTimers[1], Device);
		IDirect3DBaseTexture9* inputs[3] = { Textures[1], rtHistoryTexture[read], depth };
		IDirect3DSurface9* targets[2] = { Surfaces[0], rtHistorySurface[write] };
		drawn = Resources(resources, count, inputs, 3, extUniforms.Buffer) && ExtDrawTo(extTemporalRT.Pipeline, targets, 2, resources, count, &push, sizeof(push));
	}
	if (!drawn) {
		ExtFailed = wasFailed;
		rtFailed = true;
		Logger::Log("UNOFFICIAL RT AO: the RT estimate failed to draw; AmbientOcclusion's own estimate runs from now on.");
		return false;
	}
	rtHistoryIndex = write;
	RtHistoryValid = true;
	RtLastTime = TheFrameRateManager->Time;
	RtLastView = TheRenderManager->viewMatrix;
	RtLastInvView = TheRenderManager->InvViewMatrix;
	RtLastProj = TheRenderManager->projMatrix;
	RtFrame++;
	return true;
}

// UNOFFICIAL 2026-10-28 (fork RT) [Shaders.AmbientOcclusion.RT] Sky: SkyVisibilityV2's place, rays instead of the ortho map
// (AmbientOcclusionSky.frag's inputs less the map; AmbientOcclusionSkyRT.frag). Same failure rule as ExtEstimateRT.
bool AmbientOcclusionEffect::ExtSkyVisibilityRT(IDirect3DSurface9* Target, IDirect3DTexture9* SunMask, IDirect3DTexture9* SmoothedNormals) {
	static bool rtFailed = false;
	D3DSURFACE_DESC desc = {};
	IDirect3DBaseTexture9* depth = SlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = SlotTexture("TESR_NormalsBuffer");
	if (rtFailed || !depth || !normals || !Target || FAILED(Target->GetDesc(&desc)) || !desc.Width || !desc.Height) return false;
	IDirect3DTexture9* raw = RtRaw();
	if (!raw) return false;
	MakePipelineRT(extSkyRT, "AmbientOcclusionSkyRT", 5, 1);
	const D3DXVECTOR4 skyLayout(1.0f / desc.Width, 1.0f / desc.Height, SmoothedNormals ? 1.0f : 0.0f, 0.0f);
	if (!extSkyRT.Pipeline || !ExtConstants(extSkyUniforms, D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f), skyLayout)) return false;
	IDirect3DBaseTexture9* inputs[5] = { depth, normals, SunMask ? (IDirect3DBaseTexture9*)SunMask : depth,
		SmoothedNormals ? (IDirect3DBaseTexture9*)SmoothedNormals : depth, raw };
	NVR_EXT_RESOURCE_BINDING resources[9] = {};
	UINT count = 0;
	AOPush push = {};
	const bool wasFailed = ExtFailed;
	if (Resources(resources, count, inputs, 5, extSkyUniforms.Buffer) && ExtDrawTo(extSkyRT.Pipeline, &Target, 1, resources, count, &push, sizeof(push))) return true;
	ExtFailed = wasFailed;
	rtFailed = true;
	Logger::Log("UNOFFICIAL RT AO: the RT sky pass failed to draw; Skylighting's ortho map pass runs from now on.");
	return false;
}

/*
* UNOFFICIAL 2026-10-17 Skylighting v2: the sky pass (technique SkyVisibilityV2) into the target the size of this frame's AO target,
* with the ortho map on stage 6 and the sun shadow mask on stage 8. Called from Render before the AO passes; sets skyReady.
* UNOFFICIAL 2026-10-20 (skyamb): also called by ShaderManager just before AmbientLight while that hosts the sky test.
*/
void AmbientOcclusionEffect::RenderSkyVisibility(IDirect3DDevice9* Device, IDirect3DTexture9* smoothedNormals) {
	IDirect3DTexture9* orthoMap = TheShaderManager->Effects.ShadowsExteriors ? TheShaderManager->Effects.ShadowsExteriors->ShadowMapOrthoTexture : nullptr;
	IDirect3DTexture9* sunMask = TheShaderManager->Effects.ShadowsExteriors && TheShaderManager->Effects.ShadowsExteriors->Enabled ?
		TheShaderManager->Effects.ShadowsExteriors->Textures.ShadowPassTexture : nullptr;
	D3DXHANDLE technique = TechniqueHandle(techSky, "SkyVisibilityV2");
	// fixes-b #3: create this resolution's sky target on first use (once; a failure is logged once and v2 stays off)
	static bool createTried[2] = { false, false };
	const int which = lastLowRes ? 1 : 0;
	// UNOFFICIAL 2026-10-19: the sky reflections ([Shaders.AmbientOcclusion.Skylighting] Reflections) need a third channel (b, the
	// visibility itself); without them the target keeps the two it had (half the bytes to write and to read back). Switching
	// Reflections recreates this resolution's target (its users take it from SkyTexture() each frame).
	// UNOFFICIAL 2026-10-20 (skyamb): LightCap reads the b too (Includes/SkyTake.hlsl).
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
	const bool rtSky = RtAmbient::SkyReady();	// UNOFFICIAL 2026-10-28 (fork RT): the RT sky needs no ortho map
	if (technique && (orthoMap || rtSky) && !createTried[which]) {
		createTried[which] = true;
		createdFormat[which] = wanted;
		if (lastLowRes)
			TheTextureManager->InitTexture("NVR_SkyVisBufferLow", &skyTextureLow, &skySurfaceLow, MaxQuality::Reduced(TheRenderManager->width, 4), MaxQuality::Reduced(TheRenderManager->height, 4), wanted);
		else
			TheTextureManager->InitTexture("NVR_SkyVisBuffer", &skyTexture, &skySurface, MaxQuality::Reduced(TheRenderManager->width, 2), MaxQuality::Reduced(TheRenderManager->height, 2), wanted);
	}
	IDirect3DSurface9* target = lastLowRes ? skySurfaceLow : skySurface;
	D3DSURFACE_DESC desc = {};
	if (!technique || !(orthoMap || rtSky) || !target || FAILED(target->GetDesc(&desc))) return;
	static GpuTimer skyTimer("  Sky visibility v2");
	GpuProfileScope gpu(skyTimer, Device);
	D3DVIEWPORT9 original = {};
	Device->GetViewport(&original);
	D3DVIEWPORT9 quarter = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
	SkyData4.z = sunMask ? 1.0f : 0.0f;
	Effect->SetTechnique(technique);
	// UNOFFICIAL 2026-10-21 (alrocks): the smoothed normal from AmbientLight's prep (stage 9) when it was drawn this frame.
	D3DXHANDLE smoothHandle = ParameterHandle(paramSkySmooth, "NVR_SkySmooth");
	if (!smoothHandle) smoothedNormals = nullptr;
	if (smoothHandle) { D3DXVECTOR4 smooth(smoothedNormals ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f); Effect->SetVector(smoothHandle, &smooth); }
	// UNOFFICIAL 2026-10-25 (NVR DXVK): the Vulkan pass first (AmbientOcclusionSky.frag); the D3D9 pass when it is off or failed.
	HRESULT result = E_FAIL;
	if (rtSky && !ExtFailed && ExtSkyVisibilityRT(target, sunMask, smoothedNormals)) result = S_OK;	// UNOFFICIAL 2026-10-28 (fork RT)
	else if (!orthoMap) result = E_FAIL;	// (the RT sky failed and no map was drawn for it: no factor this frame)
	else if (!ExtFailed && DxvkCompute::PassesOn() && ExtSkyVisibility(target, desc, orthoMap, sunMask, smoothedNormals)) result = S_OK;
	else {
	// UNOFFICIAL 2026-10-20 (skyamb): SetCT does nothing while this effect is off, and it is off whenever AmbientLight hosts the sky
	// test (ShaderManager switches it off for the frame; the user may have it off too): the pass then ran with no depth, normals or
	// constants (measured: no darkening, the debug views did not change). On for the call.
	const bool wasEnabled = Enabled;
	Enabled = true;
	SetCT();
	Enabled = wasEnabled;
	UINT passes = 0;
	IDirect3DSurface9* depthSurface = nullptr;	// no depth-stencil while this draws (restored below)
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
			RenderRect::ApplyViewport(Device);	// UNOFFICIAL 2026-10-27 RenderScale v2
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
	static D3DFORMAT reported = D3DFMT_UNKNOWN;	// (again when Reflections switches the format)
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
	// combineOnly reapplies this frame's deferred estimate, so it keeps that estimate's resolution.
	if (!combineOnly) lastLowRes = UseLowRes();
	if (!combineOnly && SkyData2.x > 0.0f) {	// UNOFFICIAL Skylighting v2: this frame's sky visibility first
		RenderSkyVisibility(Device);
		Device->SetRenderTarget(0, RenderTarget);
	}
	IDirect3DTexture9* const* textures = lastLowRes ? aoTextureLow : aoTexture;
	IDirect3DSurface9* const* surfaces = lastLowRes ? aoSurfaceLow : aoSurface;
	const UINT divisor = MaxQuality::Divisor(lastLowRes ? 4 : 2);	// UNOFFICIAL: 1 with Max Quality
	D3DXHANDLE technique = TechniqueHandle(techDedicated, "DedicatedAO");
	if (FastPaths) {	// UNOFFICIAL 2026-10-17 FastPaths (above)
		D3DXHANDLE fast = TechniqueHandle(techFast, "DedicatedAOFast");
		if (fast) technique = fast;
	}
	bool gtaoTechnique = false;	// (NVR DXVK: which estimate the Vulkan pass computes)
	if (GTAOData.x > 0.5f) {	// UNOFFICIAL 2026-10-16: the same passes with the GTAO estimate, when the effect file has them
		D3DXHANDLE gtao = FastPaths ? TechniqueHandle(techGTAOFast, "DedicatedGTAOFast") : NULL;	// UNOFFICIAL 2026-10-17 FastPaths
		if (!gtao) gtao = TechniqueHandle(techGTAO, "DedicatedGTAO");
		if (gtao) { technique = gtao; gtaoTechnique = true; }
	}
	D3DXHANDLE layoutHandle = ParameterHandle(paramLayout, "NVR_AOLayout");
	D3DXTECHNIQUE_DESC description = {};
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC target = {}, scratch = {}, aoTarget = {};
	IDirect3DSurface9* depthSurface = nullptr;
	// Older/custom effect files keep the legacy path. Never guess their pass layout.
	if (packedAOFailed || !technique || !layoutHandle || FAILED(Effect->GetTechniqueDesc(technique, &description)) ||
		description.Passes != 4 || !RenderedSurface || !SourceBuffer || !textures[0] || !textures[1] ||
		!surfaces[0] || !surfaces[1] ||
		FAILED(Device->GetViewport(&original)) || FAILED(RenderTarget->GetDesc(&target)) ||
		FAILED(RenderedSurface->GetDesc(&scratch)) || FAILED(surfaces[0]->GetDesc(&aoTarget)) ||
		original.X || original.Y ||
		// UNOFFICIAL 2026-10-27 RenderScale v2: in a rect frame an earlier pass may have left the target's drawn part as the viewport
		!((original.Width == target.Width && original.Height == target.Height) || (RenderRect::Active() &&
			original.Width == RenderRect::Width(target.Width) && original.Height == RenderRect::Height(target.Height))) ||
		scratch.Width != target.Width || scratch.Height != target.Height ||
		aoTarget.Width != (target.Width + divisor - 1) / divisor || aoTarget.Height != (target.Height + divisor - 1) / divisor ||
		aoTarget.Format != D3DFMT_G16R16F ||
		target.MultiSampleType != D3DMULTISAMPLE_NONE) {
		// Let the caller restore deferred shadows before running legacy AO.
		if (deferCombine) return;
		EffectRecord::Render(Device, RenderTarget, RenderedSurface, techniqueIndex, ClearRenderTarget, SourceBuffer);
		return;
	}
	// (RenderScale v2: the layout below and the final restore are for the whole target; ApplyViewport sets the drawn part per draw.)
	original.Width = target.Width;
	original.Height = target.Height;
	auto timer = TimeLogger();
	// Under the frame chain the combine writes the chain's spare texture, which then becomes the
	// current image; otherwise it writes the render target and is copied back as before.
	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);
	IDirect3DSurface9* finalTarget = chained ? chain.Output() : RenderTarget;
	D3DVIEWPORT9 reduced = original;
	reduced.Width = aoTarget.Width;
	reduced.Height = aoTarget.Height;
	D3DXVECTOR4 layout((float)reduced.Width / original.Width,
		(float)reduced.Height / original.Height, 1.0f / reduced.Width, 1.0f / reduced.Height);
	// The combine reads the scene through the TESR_SourceBuffer slot (s2). RenderedSurface already
	// equals the render target when AO starts and nothing here writes it before the final copy,
	// so bind it there instead of copying the full frame into SourceBuffer.
	IDirect3DTexture9* scene = TheTextureManager->RenderedTexture;
	// UNOFFICIAL Skylighting v2, DedicatedCombine (not deferred): the sky factor on stage 7.
	IDirect3DTexture9* skyVis = !deferCombine ? SkyTexture() : nullptr;
	SkyData4.w = skyVis ? 1.0f : 0.0f;
	// UNOFFICIAL 2026-10-17 Skylighting: the ortho map on stage 6 (NVR_OrthoMap in the effect) for the estimate pass.
	IDirect3DTexture9* orthoMap = SkyData.x > 0.0f && TheShaderManager->Effects.ShadowsExteriors ? TheShaderManager->Effects.ShadowsExteriors->ShadowMapOrthoTexture : nullptr;
	static bool reported[2] = {};
	auto reportActive = [&]() {
		if (reported[lastLowRes]) return;
		Logger::Log("UNOFFICIAL dedicated AO active: %ux%u G16R16F ping-pong%s.", reduced.Width, reduced.Height,
			lastLowRes ? " (AOLowRes)" : "");
		reported[lastLowRes] = true;
	};
	// UNOFFICIAL 2026-10-25 (NVR DXVK): the four passes as Vulkan passes first (ExtDedicated); false: nothing of the frame was
	// committed and the D3D9 passes below draw it.
	if (!ExtFailed && DxvkCompute::PassesOn() && ExtDedicated(Device, RenderTarget, RenderedSurface, textures, surfaces, layout, gtaoTechnique, orthoMap, skyVis)) {
		if (deferCombine) deferredReady = true;	// nothing was written to the frame; the fog pass applies ResultTexture()
		reportActive();
		renderTime = timer.LogTime(deferCombine ? "AmbientOcclusion::Deferred" : "AmbientOcclusion::PackedAO");
		return;
	}
	HRESULT result = scene ? Device->GetDepthStencilSurface(&depthSurface) : E_FAIL;
	// UNOFFICIAL ReShadeDepthCompat: leave the game's depth-stencil bound (a single smaller target is legal with it).
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
			if (combine && skyVis) {	// UNOFFICIAL Skylighting v2 (upsampled with the AO's weights)
				Device->SetTexture(7, skyVis);
				TheRenderManager->renderState->NoteDeviceTexture(7, skyVis);
			}
			Device->SetTexture(2, scene); // TESR_SourceBuffer slot
			// UNOFFICIAL: keep the game's texture cache truthful (NiDX9RenderState::NoteDeviceTexture)
			TheRenderManager->renderState->NoteDeviceTexture(5, stage5);
			TheRenderManager->renderState->NoteDeviceTexture(2, scene);
			RenderRect::ApplyViewport(Device);	// UNOFFICIAL 2026-10-27 RenderScale v2: the AO targets and the frame keep to their rect
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
		// Nothing was written to the frame; the fog pass applies ResultTexture().
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
		// RenderedSurface is only written by the final copy, so it still holds the input.
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
