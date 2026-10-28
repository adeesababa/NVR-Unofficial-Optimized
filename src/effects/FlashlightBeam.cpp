#include "FlashlightBeam.h"
#include "../core/DxvkCompute.h"

void FlashlightBeamEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_VolumetricControl", &Constants.Control);
}

void FlashlightBeamEffect::RegisterTextures() {
	// Half resolution: the march is the expensive part and the shaft has no fine detail.
	// The Flashlight Combine pass reads it through a bilinear sampler, which upsamples it.
	TheTextureManager->InitTexture("TESR_VolumetricBuffer", &Textures.VolumetricTexture, &Textures.VolumetricSurface,
		MaxQuality::Reduced(TheRenderManager->width, 2), MaxQuality::Reduced(TheRenderManager->height, 2), D3DFMT_A16B16G16R16F);
}

void FlashlightBeamEffect::UpdateSettings() {
	// UpdateConstants only runs while this effect is enabled, so switching it off would
	// otherwise leave the last gate value standing and the Flashlight Combine pass would
	// keep compositing a stale buffer. UpdateSettings runs on every settings change
	// regardless of enabled state, and before UpdateConstants, so clearing it here both
	// closes the gate on disable and leaves the enabled path free to set it again.
	Constants.Control = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);

	Settings.FirstPerson = TheSettingManager->GetSettingI("Shaders.FlashlightBeam.Main", "FirstPerson");
	Settings.ThirdPerson = TheSettingManager->GetSettingI("Shaders.FlashlightBeam.Main", "ThirdPerson");
	Settings.Strength = TheSettingManager->GetSettingF("Shaders.FlashlightBeam.Main", "Strength");
	Settings.Anisotropy = TheSettingManager->GetSettingF("Shaders.FlashlightBeam.Main", "Anisotropy");
	Settings.HeightFalloff = TheSettingManager->GetSettingF("Shaders.FlashlightBeam.Main", "HeightFalloff");
	Settings.MaxDistance = TheSettingManager->GetSettingF("Shaders.FlashlightBeam.Main", "MaxDistance");
	Settings.FirstPersonStrength = TheSettingManager->GetSettingF("Shaders.FlashlightBeam.Main", "FirstPersonStrength");
	Settings.FirstPersonOffsetRight = TheSettingManager->GetSettingF("Shaders.FlashlightBeam.Main", "FirstPersonOffsetRight");
	Settings.FirstPersonOffsetDown = TheSettingManager->GetSettingF("Shaders.FlashlightBeam.Main", "FirstPersonOffsetDown");
}

void FlashlightBeamEffect::UpdateConstants() {
	// Strength folds the effect toggle, the per view toggle and the configured strength into
	// one value, so a single > 0 test gates both the march and the Combine composite. That
	// matters because the buffer persists between frames: without the gate on the composite
	// side, the last shaft rendered would keep bleeding in after the beam is switched off.
	bool viewEnabled = (Player && Player->isThirdPerson) ? Settings.ThirdPerson : Settings.FirstPerson;
	bool lightOn = TheShaderManager->Effects.Flashlight->Enabled && TheShaderManager->Effects.Flashlight->spotLightActive;
	float strength = (Enabled && viewEnabled && lightOn) ? Settings.Strength : 0.0f;

	Constants.Control = D3DXVECTOR4(strength, Settings.Anisotropy, Settings.HeightFalloff, Settings.MaxDistance);
}

struct FlashlightBeamUniforms {
	D3DXVECTOR4	volumetricControl;
	D3DXVECTOR4	volumetricData;
	D3DXVECTOR4	spotLightPosition;
	D3DXVECTOR4	spotLightColor;
	D3DXVECTOR4	spotLightDirection;
	D3DXVECTOR4	sunColor;
	D3DXVECTOR4	reciprocalResolution;
	D3DXVECTOR4	cameraPosition;
	D3DXVECTOR4	cameraData;
	D3DXMATRIX	flashLightViewProj;
	D3DXMATRIX	viewTransform;
	D3DXMATRIX	projectionTransform;
};
static_assert(sizeof(FlashlightBeamUniforms) == 336, "FlashlightBeam.frag declares a 336-byte block");

struct FlashlightBeamPush {
	UInt32	pointSamplerIndex;
	UInt32	cookieSamplerIndex;
};

static const NVR_EXT_BINDING_DESC FlashlightBeamBindings[] = {
	{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },
	{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },
	{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
	{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 0 },
	{ 0, 0, NVR_EXT_BIND_SAMPLER,        1, NVR_EXT_VIEW_2D, FALSE, 4 },
};

static const D3DXVECTOR4* RegisteredConstant(const char* name) {
	auto found = TheShaderManager->ConstantsTable.find(name);
	return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
}

bool FlashlightBeamEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	std::string depthName = "TESR_DepthBuffer";
	IDirect3DBaseTexture9** depthSlot = TheTextureManager->GetTextureSlotByName(depthName);
	IDirect3DBaseTexture9* depth = depthSlot ? *depthSlot : nullptr;
	IDirect3DBaseTexture9* cookie = ExtFileTexture("TESR_SpotLightTexture");

	const D3DXVECTOR4* volumetricControl = RegisteredConstant("TESR_VolumetricControl");
	const D3DXVECTOR4* volumetricData = RegisteredConstant("TESR_VolumetricData");
	const D3DXVECTOR4* spotLightPosition = RegisteredConstant("TESR_SpotLightPosition");
	const D3DXVECTOR4* spotLightColor = RegisteredConstant("TESR_SpotLightColor");
	const D3DXVECTOR4* spotLightDirection = RegisteredConstant("TESR_SpotLightDirection");
	const D3DXVECTOR4* sunColor = RegisteredConstant("TESR_SunColor");
	const D3DXVECTOR4* reciprocalResolution = RegisteredConstant("TESR_ReciprocalResolution");
	const D3DXVECTOR4* cameraPosition = RegisteredConstant("TESR_CameraPosition");
	const D3DXVECTOR4* cameraData = RegisteredConstant("TESR_CameraData");
	const D3DXVECTOR4* lightViewProj = RegisteredConstant("TESR_FlashLightViewProjTransform");
	const D3DXVECTOR4* view = RegisteredConstant("TESR_ViewTransform");
	const D3DXVECTOR4* projection = RegisteredConstant("TESR_ProjectionTransform");
	if (!depth || !cookie || !volumetricControl || !volumetricData || !spotLightPosition || !spotLightColor || !spotLightDirection ||
		!sunColor || !reciprocalResolution || !cameraPosition || !cameraData || !lightViewProj || !view || !projection) return false;

	if (!extPass.Tried) {
		extPass.Tried = true;
		extPass.Pipeline = DxvkCompute::CreateGraphicsPipeline("FlashlightBeam", FlashlightBeamBindings, _countof(FlashlightBeamBindings), sizeof(FlashlightBeamPush), true);
	}
	ID3D9NvrExtSampler* pointSampler = DxvkCompute::PointClamp();
	ID3D9NvrExtSampler* cookieSampler = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_CLAMP);
	if (!extPass.Pipeline || !pointSampler || !cookieSampler) return false;

	FlashlightBeamUniforms uniforms = {};
	uniforms.volumetricControl = *volumetricControl;
	uniforms.volumetricData = *volumetricData;
	uniforms.spotLightPosition = *spotLightPosition;
	uniforms.spotLightColor = *spotLightColor;
	uniforms.spotLightDirection = *spotLightDirection;
	uniforms.sunColor = *sunColor;
	uniforms.reciprocalResolution = *reciprocalResolution;
	uniforms.cameraPosition = *cameraPosition;
	uniforms.cameraData = *cameraData;
	memcpy(&uniforms.flashLightViewProj, lightViewProj, sizeof(D3DXMATRIX));
	memcpy(&uniforms.viewTransform, view, sizeof(D3DXMATRIX));
	memcpy(&uniforms.projectionTransform, projection, sizeof(D3DXMATRIX));
	ID3D9NvrExtBuffer* uniformBuffer = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	if (!uniformBuffer) return false;

	NVR_EXT_RESOURCE_BINDING resources[5] = {};
	resources[0].Slot = 0; resources[0].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[0].pResource = depth;
	resources[1].Slot = 1; resources[1].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[1].pResource = cookie;
	resources[2].Slot = 0; resources[2].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[2].pResource = uniformBuffer;
	resources[3].Slot = 0; resources[3].Type = NVR_EXT_BIND_SAMPLER; resources[3].pResource = pointSampler;
	resources[4].Slot = 1; resources[4].Type = NVR_EXT_BIND_SAMPLER; resources[4].pResource = cookieSampler;
	FlashlightBeamPush push = {};
	IDirect3DSurface9* targets[] = { RenderTarget };
	return ExtDrawTo(extPass.Pipeline, targets, _countof(targets), resources, _countof(resources), &push, sizeof(push), nullptr, ExtUv::ScreenQuad);
}
