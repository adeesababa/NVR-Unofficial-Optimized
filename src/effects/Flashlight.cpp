#include "Flashlight.h"
#include "../core/DxvkCompute.h"


void FlashlightEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_FlashLightViewProjTransform", (D3DXVECTOR4*)&Constants.FlashlightViewProj);
	TheShaderManager->RegisterConstant("TESR_FlashLightPosition", &Constants.Position);
	TheShaderManager->RegisterConstant("TESR_FlashLightDirection", &Constants.Direction);
	TheShaderManager->RegisterConstant("TESR_FlashLightColor", &Constants.Color);
	TheShaderManager->RegisterConstant("TESR_FlashLightTuning", &Constants.Tuning);
	TheShaderManager->RegisterConstant("TESR_FlashLightComposite", &Constants.Composite);
};

void FlashlightEffect::UpdateSettings() {

	Settings.attachToWeapon = TheSettingManager->GetSettingI("Shaders.Flashlight.Main", "AttachToWeapon");
	Settings.renderShadows = TheSettingManager->GetSettingI("Shaders.Flashlight.Main", "RenderShadows");
	selectedPass = Settings.renderShadows;

	Settings.Offset = NiPoint3(
		TheSettingManager->GetSettingF("Shaders.Flashlight.Main", "OffsetX"),
		TheSettingManager->GetSettingF("Shaders.Flashlight.Main", "OffsetY"),
		TheSettingManager->GetSettingF("Shaders.Flashlight.Main", "OffsetZ")
	);

	const char* lookSections[2] = { "Shaders.Flashlight.Main", "Shaders.Flashlight.Interiors" };
	for (int i = 0; i < 2; i++) {
		FlashlightLook& look = Looks[i];
		look.Color = NiColor(
			TheSettingManager->GetSettingF(lookSections[i], "ColorR"),
			TheSettingManager->GetSettingF(lookSections[i], "ColorG"),
			TheSettingManager->GetSettingF(lookSections[i], "ColorB")
		);
		look.Dimmer = TheSettingManager->GetSettingF(lookSections[i], "Dimmer");
		look.ConeAngle = TheSettingManager->GetSettingF(lookSections[i], "Angle");
		look.Distance = TheSettingManager->GetSettingF(lookSections[i], "Distance");
		look.NearFade = TheSettingManager->GetSettingF(lookSections[i], "NearFade");
		look.HotspotLimit = TheSettingManager->GetSettingF(lookSections[i], "HotspotLimit");
		look.CookieStrength = TheSettingManager->GetSettingF(lookSections[i], "CookieStrength");
	}
	Settings.softEdges = TheSettingManager->GetSettingI("Shaders.Flashlight.Main", "SoftEdges");
	Settings.edgeFix = (std::max)(0, (std::min)(2, (int)TheSettingManager->GetSettingI("Shaders.Flashlight.Main", "EdgeFix")));

	Settings.MaterialLight.Enabled = TheSettingManager->GetSettingI("Shaders.Flashlight.MaterialLight", "Enabled");
	Settings.MaterialLight.Intensity = TheSettingManager->GetSettingF("Shaders.Flashlight.MaterialLight", "Intensity");
	Settings.MaterialLight.Specular = TheSettingManager->GetSettingF("Shaders.Flashlight.MaterialLight", "Specular");
	Settings.MaterialLight.NormalStrength = TheSettingManager->GetSettingF("Shaders.Flashlight.MaterialLight", "NormalStrength");
	Settings.MaterialLight.MaxGeometry = TheSettingManager->GetSettingI("Shaders.Flashlight.MaterialLight", "MaxGeometry");
	Settings.MaterialLight.DebugMode = TheSettingManager->GetSettingI("Shaders.Flashlight.MaterialLight", "DebugMode");

	// With RenderPreTonemapping the effect chain draws onto the game's HDR scene surface,
	// whose content is already linear. Combine has to know, so it can skip the sRGB
	// decode that is only correct on the post tonemapping LDR surface.
	float sourceIsLinear = TheSettingManager->SettingsMain.Main.RenderPreTonemapping ? 1.0f : 0.0f;

	// These come purely from settings, so they are published here rather than in
	// UpdateConstants, which only runs while the effect is enabled
	ApplyLook(TheShaderManager->GameState.isExterior);
	const float beamWidth = (float)(std::max)(1, (int)TheRenderManager->width / 2);
	const float beamHeight = (float)(std::max)(1, (int)TheRenderManager->height / 2);
	Constants.Composite = D3DXVECTOR4(sourceIsLinear, Settings.edgeFix >= 2 ? 1.0f : 0.0f, 1.0f / beamWidth, 1.0f / beamHeight);

	if (!SpotLight)
		SpotLight = NiSpotLight::CreateObject();
};

void FlashlightEffect::ApplyLook(bool exterior) {
	const FlashlightLook& look = Looks[exterior ? 0 : 1];
	Settings.Color = look.Color;
	Settings.Dimmer = look.Dimmer;
	Settings.ConeAngle = look.ConeAngle;
	Settings.Distance = look.Distance;
	Settings.NearFade = look.NearFade;
	Settings.HotspotLimit = look.HotspotLimit;
	Settings.CookieStrength = look.CookieStrength;
	Constants.Tuning = D3DXVECTOR4(Settings.NearFade, Settings.softEdges ? 1.0f : 0.0f, Settings.HotspotLimit, Settings.CookieStrength);
}

int FlashlightEffect::TechniqueIndex(const char* name) {
	if (!Effect) return -1;
	D3DXHANDLE wanted = Effect->GetTechniqueByName(name);
	D3DXEFFECT_DESC desc = {};
	if (!wanted || FAILED(Effect->GetDesc(&desc))) return -1;
	for (UINT i = 0; i < desc.Techniques; i++)
		if (Effect->GetTechnique(i) == wanted) return (int)i;
	return -1;
}

void FlashlightEffect::RefreshTechniqueIndices() {
	if (techniqueGeneration == LoadGeneration) return;
	techniqueGeneration = LoadGeneration;
	edgeFixIndex = TechniqueIndex("EdgeFix");
	edgeFixShadowsIndex = TechniqueIndex("EdgeFixShadows");
}

void FlashlightEffect::UpdateConstants() {

	ApplyLook(TheShaderManager->GameState.isExterior);

	selectedPass = Settings.renderShadows ? 1 : 0;
	if (Settings.edgeFix > 0) {
		RefreshTechniqueIndices();
		const int edgeTechnique = Settings.renderShadows ? edgeFixShadowsIndex : edgeFixIndex;
		if (edgeTechnique >= 0) selectedPass = edgeTechnique;
	}

	if (!SpotLight) return;

	// based on JIP https://github.com/jazzisparis/JIP-LN-NVSE/blob/5a30ac4356ea0e93b9ff357b5031b1e420240a4d/functions_jip/jip_fn_ui.h#L1469
	UInt32 lightIsOn = ThisStdCall<bool>(0x822B90, &Player->magicTarget, (void*)&(*(MagicItem**)(ThisStdCall<uint8_t*>(0x93CCD0, NULL) + 0x18)), 1);

	spotLightActive = Enabled && lightIsOn;
	if (!spotLightActive) {
		// disable light by setting it to 0 dimmer & radius
		SpotLight->Dimmer = 0;
		SpotLight->Spec = NiColor(0, 0, 0);
		SpotLight->CastShadows = false;
		PublishLightConstants(false);
		return;
	}

	NiPoint3 WeaponPos;
	NiMatrix33 WeaponRot;
	bool melee = false;
	if (Player->process->IsWeaponOut() && Player->ActorSkinInfo) {
		melee = !Player->ActorSkinInfo->WeaponForm ||
			Player->ActorSkinInfo->WeaponForm->weaponType == TESObjectWEAP::WeaponType::kWeapType_HandToHandMelee ||
			Player->ActorSkinInfo->WeaponForm->weaponType == TESObjectWEAP::WeaponType::kWeapType_OneHandMelee ||
			Player->ActorSkinInfo->WeaponForm->weaponType == TESObjectWEAP::WeaponType::kWeapType_TwoHandMelee ||
			Player->ActorSkinInfo->WeaponForm->weaponType == TESObjectWEAP::WeaponType::kWeapType_OneHandGrenade ||
			Player->ActorSkinInfo->WeaponForm->weaponType == TESObjectWEAP::WeaponType::kWeapType_OneHandMine ||
			Player->ActorSkinInfo->WeaponForm->weaponType == TESObjectWEAP::WeaponType::kWeapType_OneHandLunchboxMine ||
			Player->ActorSkinInfo->WeaponForm->weaponType == TESObjectWEAP::WeaponType::kWeapType_OneHandThrown;
	}

	// Down the sights the light belongs on the gun, whatever AttachToWeapon says: the weapon
	// is lined up with the view there, so riding the head instead puts the beam behind the
	// thing the player is looking past.
	//
	// IsAiming() cannot be used raw for that. It is false during the attack and follow-through
	// states, so testing it directly made the light alternate between two bones with different
	// positions and different forward axes while firing - the snapping the third person path
	// was fixed for. So it is latched: aiming takes hold immediately, and releasing waits for
	// the signal to stay false for long enough to bridge the gaps inside a burst. The hold is
	// in seconds of real time rather than frames so it does not shorten as the framerate rises.
	//
	// Melee stays excluded, as it is for AttachToWeapon: there is no sight to look down and no
	// sensible place on the weapon for the light to sit.
	static const double kAimingHoldSeconds = 0.35;
	const double now = TheFrameRateManager->Time;
	if (Player->IsAiming()) aimingHoldUntil = now + kAimingHoldSeconds;
	const bool aiming = now < aimingHoldUntil;
	const bool rideWeapon = (Settings.attachToWeapon || aiming) && !melee && Player->process->IsWeaponOut();

	if (Player->isThirdPerson) {
		if (rideWeapon && Player->ActorSkinInfo && Player->ActorSkinInfo->WeaponNode) {
			WeaponPos = Player->ActorSkinInfo->WeaponNode->m_worldTransform.pos;
			WeaponRot = Player->ActorSkinInfo->WeaponNode->m_worldTransform.rot;
		}
		else if (Player->ActorSkinInfo && Player->ActorSkinInfo->HeadNode) {
			// matrix that will rotate 90 degrees on the Z then X axis
			NiMatrix33 rotation = NiMatrix33();
			rotation.data[0][0] = 0;
			rotation.data[0][1] = -1;
			rotation.data[0][2] = 0;
			rotation.data[1][0] = 0;
			rotation.data[1][1] = 0;
			rotation.data[1][2] = 1;
			rotation.data[2][0] = -1;
			rotation.data[2][1] = 0;
			rotation.data[2][2] = 0;

			WeaponPos = Player->ActorSkinInfo->HeadNode->m_worldTransform.pos;
			WeaponRot = Player->ActorSkinInfo->HeadNode->m_worldTransform.rot;
			rotation = rotation * WeaponRot; // we place the rotation matrix in the referential of the bone
			WeaponRot = WeaponRot * rotation; // we apply it
		}
		else {
			WeaponPos = WorldSceneGraph->camera->m_worldTransform.pos;
			WeaponRot = WorldSceneGraph->camera->m_worldTransform.rot;
		}
	}
	else {
		if (rideWeapon && Player->firstPersonSkinInfo && Player->firstPersonSkinInfo->WeaponNode) {
			WeaponPos = Player->firstPersonSkinInfo->WeaponNode->m_worldTransform.pos;
			WeaponRot = Player->firstPersonSkinInfo->WeaponNode->m_worldTransform.rot;
		}
		else {
			WeaponPos = WorldSceneGraph->camera->m_worldTransform.pos;
			WeaponRot = WorldSceneGraph->camera->m_worldTransform.rot;
		}
	}

	// rotate offset in the direction of the cone
	NiPoint3 offset = WeaponRot * Settings.Offset;
	WeaponPos.x += offset.x;
	WeaponPos.y += offset.y;
	WeaponPos.z += offset.z;

	if (spotLightActive) {
		// find and disable pipboy light
		NiNode* PlayerNode = Player->GetNode();
		for (UINT32 i = 0; i < PlayerNode->m_children.capacity; i++) {
			NiAVObject* childNode = PlayerNode->m_children.data[i];
			if (childNode) {
				if (childNode->GetRTTI() == (void*)0x11F4A98) {
					childNode->m_flags |= childNode->APP_CULLED;
				}
			}
		}
	}

	SpotLight->CastShadows = Settings.renderShadows;
	SpotLight->Diff = Settings.Color;
	SpotLight->Dimmer = Settings.Dimmer * 10.0;
	SpotLight->m_worldTransform.pos = WeaponPos;
	SpotLight->m_worldTransform.rot = WeaponRot;
	SpotLight->m_worldTransform.scale = 1.0f;
	SpotLight->OuterSpotAngle = Settings.ConeAngle;
	SpotLight->Spec = NiColor(Settings.Distance, 0, 0); // radius in r channel

	// Same packing as the shared TESR_SpotLight* constants: w carries radius, cone angle and dimmer
	Constants.Position = D3DXVECTOR4(WeaponPos.x, WeaponPos.y, WeaponPos.z, Settings.Distance);
	Constants.Direction = D3DXVECTOR4(WeaponRot.data[0][0], WeaponRot.data[1][0], WeaponRot.data[2][0], Settings.ConeAngle);
	Constants.Color = D3DXVECTOR4(Settings.Color.r, Settings.Color.g, Settings.Color.b, Settings.Dimmer);

	GetFlashlightViewProj();
	PublishLightConstants(true);
};

// The shared TESR_SpotLight* constants are published here rather than by the caller,
// because UpdateConstants runs more than once per frame - at frame start, again from
// GetNearbyLights during the shadow pass, and again after the world render. Each call
// re-reads the weapon bone. Publishing from the caller left the cone constants holding an
// earlier read than TESR_FlashLightViewProjTransform, so the projected cookie tracked a
// different snapshot than the cone it is projected into and slid against the beam as the
// weapon moved. Writing both from the same read removes that by construction.
void FlashlightEffect::PublishLightConstants(bool abActive) {
	if (!abActive) {
		D3DXVECTOR4 Empty = D3DXVECTOR4(0, 0, 0, 0);
		TheShaderManager->SpotLightPosition[0] = Empty;
		TheShaderManager->SpotLightDirection[0] = Empty;
		TheShaderManager->SpotLightColor[0] = Empty;
		TheShaderManager->VolumetricData = Empty;
		return;
	}

	TheShaderManager->SpotLightPosition[0] = SpotLight->m_worldTransform.pos.toD3DXVEC4();
	TheShaderManager->SpotLightPosition[0].w = SpotLight->Spec.r; // radius
	TheShaderManager->SpotLightDirection[0] = D3DXVECTOR4(
		SpotLight->m_worldTransform.rot.data[0][0],
		SpotLight->m_worldTransform.rot.data[1][0],
		SpotLight->m_worldTransform.rot.data[2][0],
		SpotLight->OuterSpotAngle); // outside angle of the light cone
	TheShaderManager->SpotLightColor[0] = D3DXVECTOR4(SpotLight->Diff.r, SpotLight->Diff.g, SpotLight->Diff.b, SpotLight->Dimmer);

	// The beam march needs parallax to produce a visible shaft. In first person the light
	// sits on the camera, so every view ray runs down the beam and there is nothing to see;
	// give the march a virtual origin pushed right and down in camera space while the
	// surface pool and the cookie keep the true position. w carries the first person
	// strength floor, which also blends the cookie flat in the march so the parallax
	// mismatch between the two positions does not show.
	FlashlightBeamEffect* Beam = TheShaderManager->Effects.FlashlightBeam;
	TheShaderManager->VolumetricData = TheShaderManager->SpotLightPosition[0];
	TheShaderManager->VolumetricData.w = 0.0f;
	if (Beam && Player && !Player->isThirdPerson && WorldSceneGraph && WorldSceneGraph->camera) {
		NiMatrix33& rot = WorldSceneGraph->camera->m_worldTransform.rot;
		float offR = Beam->Settings.FirstPersonOffsetRight;
		float offD = Beam->Settings.FirstPersonOffsetDown;
		TheShaderManager->VolumetricData.x += rot.data[0][2] * offR - rot.data[0][1] * offD;
		TheShaderManager->VolumetricData.y += rot.data[1][2] * offR - rot.data[1][1] * offD;
		TheShaderManager->VolumetricData.z += rot.data[2][2] * offR - rot.data[2][1] * offD;
		TheShaderManager->VolumetricData.w = Beam->Settings.FirstPersonStrength;
	}
}

bool FlashlightEffect::ShouldRender() { return SpotLight && spotLightActive; };


void FlashlightEffect::GetFlashlightViewProj() {
	if (!SpotLight) return;
	
	D3DXVECTOR3 Up = D3DXVECTOR3(0, 0, 1);
	D3DXVECTOR3 Eye = SpotLight->m_worldTransform.pos.toD3DXVEC3();
	D3DXVECTOR3 Direction = D3DXVECTOR3(SpotLight->m_worldTransform.rot.data[0][0], SpotLight->m_worldTransform.rot.data[1][0], SpotLight->m_worldTransform.rot.data[2][0]);
	D3DXVECTOR3 At = Eye + Direction;

	float Radius = SpotLight->Spec.r;
	D3DXMATRIX View, Proj;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(SpotLight->OuterSpotAngle * 2), 1.0f, 0.1f, Radius);
	D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);

	Constants.FlashlightViewProj = View * Proj;
}

namespace {
	struct FlashlightUniforms {
		D3DXMATRIX	flashLightViewProj;
		D3DXMATRIX	viewTransform;
		D3DXMATRIX	projectionTransform;
		D3DXVECTOR4	cameraPosition;
		D3DXVECTOR4	cameraData;
		D3DXVECTOR4	spotLightPosition;
		D3DXVECTOR4	spotLightColor;
		D3DXVECTOR4	spotLightDirection;
		D3DXVECTOR4	sunColor;
		D3DXVECTOR4	reciprocalResolution;
		D3DXVECTOR4	flashLightTuning;
		D3DXVECTOR4	flashLightComposite;
		D3DXVECTOR4	volumetricControl;
	};
	static_assert(sizeof(FlashlightUniforms) == 352, "Flashlight.glsl declares a 352-byte block");

	struct FlashlightPush {
		D3DXVECTOR2	offsetMask;
		UInt32		mode;
		UInt32		linearSampler;
		UInt32		cookieSampler;
		UInt32		pad[3];
	};

	const NVR_EXT_BINDING_DESC FlashlightBindings[] = {
		{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 2, NVR_EXT_BIND_SAMPLED_IMAGE,  2, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 3, NVR_EXT_BIND_SAMPLED_IMAGE,  3, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 4, NVR_EXT_BIND_SAMPLED_IMAGE,  4, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 5, NVR_EXT_BIND_SAMPLED_IMAGE,  5, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 1, 6, NVR_EXT_BIND_SAMPLED_IMAGE,  6, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 2, 0, NVR_EXT_BIND_UNIFORM_BUFFER, 0, NVR_EXT_VIEW_2D, FALSE, 0 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, 12 },
		{ 0, 0, NVR_EXT_BIND_SAMPLER,        1, NVR_EXT_VIEW_2D, FALSE, 16 },
	};
	const UINT FlashlightTextureCount = 7;

	IDirect3DBaseTexture9* FlashlightSlotTexture(const char* name) {
		std::string slotName = name;
		IDirect3DBaseTexture9** slot = TheTextureManager->GetTextureSlotByName(slotName);
		return slot ? *slot : nullptr;
	}
	const D3DXVECTOR4* FlashlightRegistered(const char* name) {
		auto found = TheShaderManager->ConstantsTable.find(name);
		return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
	}
}

bool FlashlightEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	const bool edgeFixShadows = edgeFixShadowsIndex >= 0 && techniqueIndex == (UINT)edgeFixShadowsIndex;
	const bool edgeFix = edgeFixShadows || (edgeFixIndex >= 0 && techniqueIndex == (UINT)edgeFixIndex);
	const bool shadows = techniqueIndex == 1 || edgeFixShadows;
	if ((techniqueIndex != 0 && !shadows && !edgeFix) || !ClearRenderTarget) return false;
	if (!SourceBuffer || SourceBuffer == RenderTarget || SourceBuffer != TheTextureManager->SourceSurface || !TheTextureManager->SourceTexture ||
		!TheTextureManager->RenderedTexture) return false;

	const D3DXVECTOR4* lightViewProj = FlashlightRegistered("TESR_FlashLightViewProjTransform");
	const D3DXVECTOR4* view = FlashlightRegistered("TESR_ViewTransform");
	const D3DXVECTOR4* projection = FlashlightRegistered("TESR_ProjectionTransform");
	const D3DXVECTOR4* cameraPosition = FlashlightRegistered("TESR_CameraPosition");
	const D3DXVECTOR4* cameraData = FlashlightRegistered("TESR_CameraData");
	const D3DXVECTOR4* spotLightPosition = FlashlightRegistered("TESR_SpotLightPosition");
	const D3DXVECTOR4* spotLightColor = FlashlightRegistered("TESR_SpotLightColor");
	const D3DXVECTOR4* spotLightDirection = FlashlightRegistered("TESR_SpotLightDirection");
	const D3DXVECTOR4* sunColor = FlashlightRegistered("TESR_SunColor");
	const D3DXVECTOR4* reciprocalResolution = FlashlightRegistered("TESR_ReciprocalResolution");
	const D3DXVECTOR4* tuning = FlashlightRegistered("TESR_FlashLightTuning");
	const D3DXVECTOR4* composite = FlashlightRegistered("TESR_FlashLightComposite");
	const D3DXVECTOR4* volumetricControl = FlashlightRegistered("TESR_VolumetricControl");
	if (!lightViewProj || !view || !projection || !cameraPosition || !cameraData || !spotLightPosition || !spotLightColor || !spotLightDirection ||
		!sunColor || !reciprocalResolution || !tuning || !composite || !volumetricControl) return false;

	IDirect3DBaseTexture9* depth = FlashlightSlotTexture("TESR_DepthBuffer");
	IDirect3DBaseTexture9* normals = FlashlightSlotTexture("TESR_NormalsBuffer");
	IDirect3DBaseTexture9* shadowMap = FlashlightSlotTexture("TESR_ShadowSpotlightBuffer0");
	IDirect3DBaseTexture9* cookie = ExtFileTexture("TESR_SpotLightTexture");
	IDirect3DBaseTexture9* volumetric = FlashlightSlotTexture("TESR_VolumetricBuffer");
	const bool beamOn = volumetricControl->x > 0.0f;
	if (!depth || !normals || !cookie || (shadows && !shadowMap) || (beamOn && !volumetric)) return false;

	auto make = [](ExtPass& pass, const char* spvName) {
		if (pass.Tried) return;
		pass.Tried = true;
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvName, FlashlightBindings, _countof(FlashlightBindings), sizeof(FlashlightPush), true);
	};
	if (shadows) {
		make(extShadows, "FlashlightShadows");
		make(extDepthBlur, "FlashlightDepthBlur");
		if (!extShadows.Pipeline || !extDepthBlur.Pipeline) return false;
	}
	make(extLight, "FlashlightLight");
	make(extBoxBlur, "FlashlightBoxBlur");
	make(extCombine, "FlashlightCombine");
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	ID3D9NvrExtSampler* mip = DxvkCompute::MipSampler(false, NVR_EXT_ADDRESS_CLAMP);
	if (!extLight.Pipeline || !extBoxBlur.Pipeline || !extCombine.Pipeline || !linear || !mip) return false;

	FlashlightUniforms uniforms = {};
	memcpy(&uniforms.flashLightViewProj, lightViewProj, sizeof(D3DXMATRIX));
	memcpy(&uniforms.viewTransform, view, sizeof(D3DXMATRIX));
	memcpy(&uniforms.projectionTransform, projection, sizeof(D3DXMATRIX));
	uniforms.cameraPosition = *cameraPosition;
	uniforms.cameraData = *cameraData;
	uniforms.spotLightPosition = *spotLightPosition;
	uniforms.spotLightColor = *spotLightColor;
	uniforms.spotLightDirection = *spotLightDirection;
	uniforms.sunColor = *sunColor;
	uniforms.reciprocalResolution = *reciprocalResolution;
	uniforms.flashLightTuning = *tuning;
	uniforms.flashLightComposite = *composite;
	uniforms.volumetricControl = *volumetricControl;
	ID3D9NvrExtBuffer* uniformBuffer = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	if (!uniformBuffer) return false;

	IDirect3DBaseTexture9* textures[FlashlightTextureCount] = { TheTextureManager->SourceTexture, nullptr, depth, normals,
		shadows ? shadowMap : depth, cookie, volumetric ? volumetric : depth };
	bool first = true;
	auto draw = [&](ExtPass& pass, UInt32 mode, const D3DXVECTOR2& offsetMask, bool last) {
		textures[1] = TheTextureManager->RenderedTexture;
		NVR_EXT_RESOURCE_BINDING resources[FlashlightTextureCount + 3] = {};
		for (UINT i = 0; i < FlashlightTextureCount; i++) {
			resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
		}
		resources[FlashlightTextureCount].Slot = 0; resources[FlashlightTextureCount].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[FlashlightTextureCount].pResource = uniformBuffer;
		resources[FlashlightTextureCount + 1].Slot = 0; resources[FlashlightTextureCount + 1].Type = NVR_EXT_BIND_SAMPLER; resources[FlashlightTextureCount + 1].pResource = linear;
		resources[FlashlightTextureCount + 2].Slot = 1; resources[FlashlightTextureCount + 2].Type = NVR_EXT_BIND_SAMPLER; resources[FlashlightTextureCount + 2].pResource = mip;
		FlashlightPush push = {};
		push.offsetMask = offsetMask;
		push.mode = mode;
		const bool drawn = ExtFullScreen(Device, RenderTarget, first ? RenderedSurface : TheTextureManager->RenderedSurface, first ? SourceBuffer : nullptr,
			pass.Pipeline, resources, _countof(resources), &push, sizeof(push), last);
		if (!drawn && !first) {
			FrameChain& chain = TheShaderManager->Chain;
			if (chain.Owns(RenderTarget, TheTextureManager->RenderedSurface)) {
				Device->StretchRect(SourceBuffer, NULL, chain.Output(), NULL, D3DTEXF_NONE);
				chain.Commit();
			}
			else
				Device->StretchRect(SourceBuffer, NULL, RenderTarget, NULL, D3DTEXF_NONE);
		}
		first = false;
		return drawn;
	};

	const D3DXVECTOR2 none(0.0f, 0.0f);
	if (shadows) {
		if (!draw(extShadows, 0, none, false)) return false;
		if (!draw(extDepthBlur, 0, D3DXVECTOR2(1.0f, 0.0f), false)) return false;
		if (!draw(extDepthBlur, 0, D3DXVECTOR2(0.0f, 1.0f), false)) return false;
	}
	if (!draw(extLight, shadows ? 0 : 1, none, false)) return false;
	if (!draw(extBoxBlur, 0, none, false)) return false;
	if (!draw(extBoxBlur, 1, none, false)) return false;
	return draw(extCombine, edgeFix ? 1 : 0, none, true);
}