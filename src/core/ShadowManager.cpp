#define ShadowMapFarPlane 32768;
#include "ShadowFaceCull.h"
#include "PointShadowSchedule.h"
#include "PointShadowForward.h"

struct SkinnedBoundEntry { UInt32 frame; bool ok; NiBound bound; };
static std::unordered_map<NiGeometry*, SkinnedBoundEntry> SkinnedBounds;
static UInt32 SkinnedBoundFrame = 0;
static unsigned statSkinnedCasters = 0, statSkinnedFaces = 0, statSkinnedNoBound = 0, statSkinnedOutOfRange = 0;

static bool SkinnedWorldBound(NiGeometry* geometry, NiBound& out) {
	SkinnedBoundEntry& entry = SkinnedBounds[geometry];
	if (entry.frame == SkinnedBoundFrame) { out = entry.bound; return entry.ok; }
	entry.frame = SkinnedBoundFrame;
	entry.ok = false;
	NiSkinInstance* skin = geometry->skinInstance;
	NiSkinData* data = skin ? skin->SkinData : nullptr;
	if (!data || !data->BoneData || !skin->BoneObjects || data->Bones == 0) return false;
	D3DXVECTOR3 center(0.0f, 0.0f, 0.0f);
	float radius = -1.0f;
	for (UInt32 i = 0; i < data->Bones; i++) {
		NiAVObject* bone = skin->BoneObjects[i];
		if (!bone) return false;
		const NiTransform& t = bone->m_worldTransform;
		const NiBound& b = data->BoneData[i].Bound;
		if (!(b.Radius >= 0.0f && b.Radius < 1.0e6f) || !(t.scale > 0.0f && t.scale < 1.0e3f)) return false;
		const NiPoint3 scaled = { b.Center.x * t.scale, b.Center.y * t.scale, b.Center.z * t.scale };
		const NiPoint3 rotated = t.rot * scaled;
		const D3DXVECTOR3 c(rotated.x + t.pos.x, rotated.y + t.pos.y, rotated.z + t.pos.z);
		const float r = b.Radius * t.scale;
		if (radius < 0.0f) { center = c; radius = r; continue; }
		const D3DXVECTOR3 d = c - center;
		const float dist = D3DXVec3Length(&d);
		if (dist + r <= radius) continue;
		if (dist + radius <= r) { center = c; radius = r; continue; }
		const float merged = (dist + radius + r) * 0.5f;
		center += d * ((merged - radius) / dist);
		radius = merged;
	}
	if (radius < 0.0f) return false;
	entry.bound.Center.x = center.x; entry.bound.Center.y = center.y; entry.bound.Center.z = center.z;
	entry.bound.Radius = radius * 1.02f + 2.0f;
	entry.ok = true;
	out = entry.bound;
	return true;
}

static bool TouchesShadowFace(NiAVObject* object, const NiPoint3* light,
                              const D3DXVECTOR3& direction) {
	NiGeometry* geometry = object->IsGeometry() ? static_cast<NiGeometry*>(object) : nullptr;
	if (geometry && geometry->skinInstance) {
		NiBound skinned;
		if (!TheSettingManager->SettingsMain.Main.SkinnedShadowFaceTest || !SkinnedWorldBound(geometry, skinned)) return true;
		return ShadowSphereTouchesFace(skinned.Center.x - light->x, skinned.Center.y - light->y, skinned.Center.z - light->z,
			skinned.Radius, direction.x, direction.y, direction.z);
	}
	NiBound* bound = object->m_kWorldBound;
	if (!bound) return true;
	return ShadowSphereTouchesFace(bound->Center.x - light->x,
		bound->Center.y - light->y, bound->Center.z - light->z, bound->Radius,
		direction.x, direction.y, direction.z);
}

static UInt64 HashPointShadowBytes(UInt64 hash, const void* data, size_t size) {
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
	return hash;
}

struct CasterMotion { UInt32 seen = 0, changed = 0; UInt64 transform = 0; bool known = false; };
static std::unordered_map<NiGeometry*, CasterMotion> CasterMotions;
static const UInt32 MovingSettleFrames = 60;

static bool RecentlyMoved(NiGeometry* geo) {
	CasterMotion& motion = CasterMotions[geo];
	if (motion.seen != SkinnedBoundFrame || !motion.known) {
		const UInt64 transform = HashPointShadowBytes(1469598103934665603ULL, &geo->m_worldTransform, sizeof(geo->m_worldTransform));
		if (motion.known && transform != motion.transform) motion.changed = SkinnedBoundFrame;
		motion.transform = transform;
		motion.known = true;
		motion.seen = SkinnedBoundFrame;
	}
	return motion.changed && SkinnedBoundFrame - motion.changed < MovingSettleFrames;
}

static bool IsMovingCaster(NiGeometry* geo, NiShadeProperty* shade) {
	if (!geo) return false;
	if (geo->skinInstance || (shade && shade->m_eShaderType == NiShadeProperty::kProp_SpeedTreeLeaf)) return true;
	return RecentlyMoved(geo);
}

static void PointShadowCasterState(ShadowSceneLight* light, UInt64& hash, bool& staticCasters) {
	hash = 1469598103934665603ULL;
	staticCasters = light && light->kGeometryList.start;
	if (!staticCasters) return;

	for (auto entry = light->kGeometryList.start; entry; entry = entry->next) {
		NiGeometry* geo = entry->data;
		NiShadeProperty* shade = geo ? static_cast<NiShadeProperty*>(geo->GetProperty(NiProperty::kType_Shade)) : nullptr;
		if (IsMovingCaster(geo, shade)) {
			staticCasters = false;
			continue;
		}
		UInt64 casterHash = 1469598103934665603ULL;
		casterHash = HashPointShadowBytes(casterHash, &geo, sizeof(geo));
		if (!geo) continue;
		casterHash = HashPointShadowBytes(casterHash, &geo->m_flags, sizeof(geo->m_flags));
		casterHash = HashPointShadowBytes(casterHash, &geo->m_worldTransform, sizeof(geo->m_worldTransform));
		if (geo->m_kWorldBound) casterHash = HashPointShadowBytes(casterHash, geo->m_kWorldBound, sizeof(*geo->m_kWorldBound));

		NiMaterialProperty* material = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));
		if (shade) {
			casterHash = HashPointShadowBytes(casterHash, &shade->m_usFlags, sizeof(shade->m_usFlags));
			casterHash = HashPointShadowBytes(casterHash, &shade->m_eShaderType, sizeof(shade->m_eShaderType));
		}
		if (material) casterHash = HashPointShadowBytes(casterHash, &material->fAlpha, sizeof(material->fAlpha));
		hash = PointShadowAddCasterHash(hash, casterHash);
	}
}

static bool ActorsOnlyBroken = false;
static bool ActorsOnlySupported(IDirect3DDevice9* device) {
	static int supported = -1;
	if (supported < 0) {
		supported = 0;
		D3DCAPS9 caps = {};
		IDirect3D9* d3d = nullptr;
		D3DDEVICE_CREATION_PARAMETERS creation = {};
		D3DDISPLAYMODE mode = {};
		if (SUCCEEDED(device->GetDeviceCaps(&caps)) && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) && SUCCEEDED(device->GetDirect3D(&d3d)) &&
			SUCCEEDED(device->GetCreationParameters(&creation)) && SUCCEEDED(device->GetDisplayMode(0, &mode)))
			supported = SUCCEEDED(d3d->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
				D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_CUBETEXTURE, D3DFMT_R32F)) ? 1 : 0;
		if (d3d) d3d->Release();
		Logger::Log(supported ? "UNOFFICIAL RedrawActorsOnly: available (min blend into R32F cube maps)"
			: "UNOFFICIAL RedrawActorsOnly: this GPU cannot min-blend into R32F cube maps; lamps are redrawn whole, as before");
	}
	return supported == 1 && !ActorsOnlyBroken;
}

static void AdoptStaticCubeMap(ShadowsExteriorEffect* shadows, PointShadowSlotState* staticSlots, int slot, const void* light) {
	if (staticSlots[slot].valid && staticSlots[slot].light == light) return;
	for (int k = 0; k < ShadowCubeMapsMax; k++) {
		if (k == slot || !staticSlots[k].valid || staticSlots[k].light != light) continue;
		std::swap(shadows->Textures.ShadowCubeMapStaticTexture[slot], shadows->Textures.ShadowCubeMapStaticTexture[k]);
		for (int f = 0; f < 6; f++) std::swap(shadows->Textures.ShadowCubeMapStaticSurface[slot][f], shadows->Textures.ShadowCubeMapStaticSurface[k][f]);
		std::swap(staticSlots[slot], staticSlots[k]);
		return;
	}
}

/*
* Initializes the Shadow Manager by grabbing the relevant settings and shaders, and setting up map sizes.
*/
#include "GpuProfiler.h"

constexpr unsigned SunCascadeUpdatePeriod(int cascade, bool limitFrequency, int nearInterval = 1, bool staggered = false) {
	if (cascade == ShadowManager::MapNear) return nearInterval == 2 ? 2 : 1;
	if (staggered) {
		if (cascade == ShadowManager::MapMiddle) return 4;
		if (cascade == ShadowManager::MapFar || cascade == ShadowManager::MapLod) return 8;
		return 1;
	}
	if (limitFrequency && cascade == ShadowManager::MapLod) return 4;
	return 1;
}

static_assert(SunCascadeUpdatePeriod(ShadowManager::MapNear, true) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapMiddle, true) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapFar, true) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapLod, true) == 4 &&
	SunCascadeUpdatePeriod(ShadowManager::MapLod, false) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapNear, true, 1, true) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapMiddle, true, 1, true) == 4 &&
	SunCascadeUpdatePeriod(ShadowManager::MapFar, true, 1, true) == 8 &&
	SunCascadeUpdatePeriod(ShadowManager::MapLod, false, 1, true) == 8 &&
	SunCascadeUpdatePeriod(ShadowManager::MapNear, true, 2) == 2 && SunCascadeUpdatePeriod(ShadowManager::MapNear, false, 2) == 2 &&
	SunCascadeUpdatePeriod(ShadowManager::MapMiddle, true, 2) == 1 && SunCascadeUpdatePeriod(ShadowManager::MapMiddle, true, 2, true) == 4,
	"Sun cascade update schedule changed unexpectedly");

constexpr unsigned SunCascadeUpdatePhase(int cascade) {
	if (cascade == ShadowManager::MapMiddle) return 1;
	if (cascade == ShadowManager::MapFar) return 3;
	if (cascade == ShadowManager::MapLod) return 7;
	return 0;
}

constexpr bool SunCascadeUpdatesOnFrame(int cascade, unsigned frame, unsigned period) {
	return frame % period == SunCascadeUpdatePhase(cascade) % period;
}

static_assert(SunCascadeUpdatesOnFrame(ShadowManager::MapNear, 5, 1) &&
	SunCascadeUpdatesOnFrame(ShadowManager::MapMiddle, 5, 4) && !SunCascadeUpdatesOnFrame(ShadowManager::MapMiddle, 0, 4) &&
	SunCascadeUpdatesOnFrame(ShadowManager::MapFar, 3, 8) && SunCascadeUpdatesOnFrame(ShadowManager::MapLod, 7, 8) &&
	SunCascadeUpdatesOnFrame(ShadowManager::MapFar, 0, 1),
	"Sun cascade stagger changed unexpectedly");

constexpr bool OneSunCascadePerFrame() {
	for (unsigned frame = 0; frame < 8; ++frame) {
		unsigned drawn = 0;
		for (int cascade = ShadowManager::MapNear; cascade < ShadowManager::MapOrtho; ++cascade)
			drawn += SunCascadeUpdatesOnFrame(cascade, frame, SunCascadeUpdatePeriod(cascade, true, 2, true)) ? 1 : 0;
		if (drawn != 1) return false;
	}
	return true;
}
static_assert(OneSunCascadePerFrame(), "StaggeredSunShadows with NearCascadeInterval 2 should leave one sun cascade per frame");

void ShadowManager::Initialize() {
	
	Logger::Log("Starting the shadows manager...");
	TheShadowManager = new ShadowManager();

	// setup the shadow render passes for the shadowmaps
	TheShadowManager->geometryPass = new ShadowRenderPass();
	TheShadowManager->alphaPass = new AlphaShadowRenderPass();
	TheShadowManager->skinnedGeoPass = new SkinnedGeoShadowRenderPass();
	TheShadowManager->speedTreePass = new SpeedTreeShadowRenderPass();
	TheShadowManager->terrainLODPass = new TerrainLODPass();

	// load the shaders
	TheShadowManager->ShadowMapVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ShadowMap.vso", "Shadows\\");
	TheShadowManager->ShadowMapPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowMap.pso", "Shadows\\");
	TheShadowManager->ShadowCubeMapVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ShadowCubeMap.vso", "Shadows\\");
	TheShadowManager->ShadowCubeMapPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowCubeMap.pso", "Shadows\\");

    TheShadowManager->ShadowMapBlurVertex = (ShaderRecordVertex*) ShaderRecord::LoadShader("ShadowMapBlur.vso", "Shadows\\");
    TheShadowManager->ShadowMapBlurPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowMapBlur.pso", "Shadows\\");

	TheShadowManager->ShadowMapClearPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowMapClear.pso", "Shadows\\");

	ShaderRecord* shadowShaders[] = { TheShadowManager->ShadowMapVertex, TheShadowManager->ShadowMapPixel,
		TheShadowManager->ShadowCubeMapVertex, TheShadowManager->ShadowCubeMapPixel, TheShadowManager->ShadowMapBlurVertex,
		TheShadowManager->ShadowMapBlurPixel, TheShadowManager->ShadowMapClearPixel };
	TheShadowManager->ShadowShadersLoaded = true;
	for (ShaderRecord* shader : shadowShaders) {
		if (shader) shader->ClearSamplers = false;
		else TheShadowManager->ShadowShadersLoaded = false;
	}
	if (!TheShadowManager->ShadowShadersLoaded)
		Logger::Log("[ERROR]: Could not load one or more of the ShadowMap generation shaders (Shaders\\NewVegasReloaded\\Shaders\\Shadows). "
			"Shadow maps are disabled. Reinstall the mod.");

	TheShaderManager->RegisterConstant("TESR_ContactHardeningData", &TheShadowManager->ContactHardeningData);
	TheShadowManager->ContactHardeningCompiled = TheSettingManager->GetSettingI("Shaders.ContactHardening.Status", "Enabled") != 0;

	UINT ShadowCubeMapSize = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.ShadowCubeMapSize;
	TheShadowManager->ShadowCubeMapViewPort = { 0, 0, ShadowCubeMapSize, ShadowCubeMapSize, 0.0f, 1.0f };

	TheShadowManager->shadowMapsRenderTime = 0;
}


/*
* Returns the given object ref's NiNode if it passes the test for excluded form types, otherwise returns NULL.
*/
NiNode* ShadowManager::GetRefNode(TESObjectREFR* Ref, ShadowsExteriorEffect::FormsStruct* Forms) {
	
	if (!Ref) return NULL;
	if (Ref->flags & TESForm::FormFlags::kFormFlags_NotCastShadows) return NULL;

	// The form filter runs before GetNode() and the extra-data walk below, both of which are the
	// expensive part of this function. Called once per reference per cascade, so a reference the
	// filter rejects should cost only the flag test and the switch.
	TESForm* Form = Ref->baseForm;
	UInt8 TypeID = Form->formType;
	switch (TypeID) {
	case TESForm::FormType::kFormType_Land:
		return NULL; // land is handled separately
		break;
	case TESForm::FormType::kFormType_Activator:
		if (!Forms->Activators) return NULL; 
		break;
	case TESForm::FormType::kFormType_Apparatus:
		if (!Forms->Apparatus) return NULL;
		break;
	case TESForm::FormType::kFormType_Book:
		if (!Forms->Books) return NULL;
		break;
	case TESForm::FormType::kFormType_Container:
		if (!Forms->Containers) return NULL;
		break;
	case TESForm::FormType::kFormType_Door:
		if (!Forms->Doors) return NULL;
		break;
	case TESForm::FormType::kFormType_Misc:
		if (!Forms->Misc) return NULL;
		break;
	case TESForm::FormType::kFormType_Tree:
		if (!Forms->Trees) return NULL;
		break;
	case TESForm::FormType::kFormType_Furniture:
		if (!Forms->Furniture) return NULL;
		break;
	case TESForm::FormType::kFormType_NPC:
	case TESForm::FormType::kFormType_Creature:
	case TESForm::FormType::kFormType_LeveledCreature:
		if (!Forms->Actors) return NULL;  // D3D9 breaks on actors for some unknown reason
		break;
	case TESForm::FormType::kFormType_Stat:
	case TESForm::FormType::kFormType_StaticCollection:
	case TESForm::FormType::kFormType_MoveableStatic:
		//return NULL;
		if (!Forms->Statics) return NULL;
		break;
	default:
		break;
	}

	// GetNode() is a plain pointer read, so it stays here. The refraction test is NOT: it calls
	// into the engine to walk the reference's extra-data list. Callers apply IsRefracting()
	// after the cascade test instead, where only the references that survive it pay the cost --
	// measured at roughly 2 acceptances per 3400 references for the Near cascade.
	return Ref->GetNode();
}

// Split out of GetRefNode. Walks the extra-data list, so call it as late as possible.
bool ShadowManager::IsRefracting(TESObjectREFR* Ref) {
	ExtraRefractionProperty* RefractionExtraProperty = (ExtraRefractionProperty*)Ref->extraDataList.GetExtraData(BSExtraData::ExtraDataType::kExtraData_RefractionProperty);
	float Refraction = RefractionExtraProperty ? (1 - RefractionExtraProperty->refractionAmount) : 0.0f;
	return Refraction >= 0.5;
}


// Detech shader flags that we do not want.
bool ShadowManager::CheckShaderFlags(NiGeometry* Geometry) {
	BSShaderProperty* shaderProp = static_cast<BSShaderProperty*>(Geometry->GetProperty(NiProperty::kType_Shade));

	if (!shaderProp)
		return false;

	return !(shaderProp->GetFlag(BSSP_REFRACTION) ||
		shaderProp->GetFlag(BSSP_FIRE_REFRACTION) ||
		shaderProp->GetFlag(BSSP_DECAL) ||
		shaderProp->GetFlag(BSSP_DYNAMIC_DECAL));
}


// Detect which pass the object must be added to
void ShadowManager::AccumObject(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLODLand) {
	NiGeometry* geo = static_cast<NiGeometry*>(NiObject);
	if (!geo->shader) return; // skip Geometry without a shader

	if (!CheckShaderFlags(geo))
		return;

#if defined(OBLIVION)
	if (geo->m_pcName && !memcmp(geo->m_pcName, "Torch", 5)) return; // No torch geo, it is too near the light and a bad square is rendered.
#endif

	if (skinnedGeoPass->AccumObject(geo)) {}
	else if (speedTreePass->AccumObject(geo)) {}
	else if (Forms->Lod && isLODLand && terrainLODPass->AccumObject(geo)) {}
	else if (Forms->AlphaEnabled && alphaPass->AccumObject(geo)) {}
	else geometryPass->AccumObject(geo);
}


// go through the Object children and sort the ones that will be rendered based on their properties
void ShadowManager::AccumChildren(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLand, bool isLOD, NiFrustumPlanes *arPlanes) {
	if (!NiObject) return;

	std::vector<NiAVObject*>& containers = containersScratch;
	containers.clear();

	NiAVObject* child;
	NiAVObject* object;
	NiNode* Node;

	//list all objects contained, or sort the object if not a container
	if (!NiObject->IsGeometry())
		containers.push_back(NiObject);
	else
		AccumObject(NiObject, Forms, isLand && isLOD);
		

	// Gather geometry
	while (!containers.empty()) {
    	object = containers.back();
    	containers.pop_back();

		if (!object) continue;

		Node = object->IsNiNode();
    	if (!Node || Node->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling containers
		if (!isLand && Node->GetWorldBoundRadius() < Forms->MinRadius) continue;

		if (Node->IsKindOf<NiSwitchNode>()) {
			// NiSwitchNode - only render active children (if exists) to the shadow map.
			NiSwitchNode* SwitchNode = static_cast<NiSwitchNode*>(Node);
			if (SwitchNode->m_iIndex < 0)
				continue;

			child = Node->m_children.data[SwitchNode->m_iIndex];
			if (!child->IsGeometry())
				containers.push_back(child);
			else
				AccumObject(child, Forms, false);
			continue;
		}

		for (int i = 0; i < Node->m_children.end; i++) {
			child = Node->m_children.data[i];
			if (!child || child->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling children
			if (!isLand && child->GetWorldBoundRadius() < Forms->MinRadius) continue;

			// Frustum culling.
			if (arPlanes && (isLand || isLOD)) {
				BSMultiBoundNode* multibound = child->IsMultiBoundNode();

				if (multibound && !multibound->spMultiBound->spShape->WithinFrustum(*arPlanes)) continue;
			}
			else if (arPlanes && !child->WithinFrustum(arPlanes)) continue;

			if (child->IsFadeNode() && static_cast<BSFadeNode*>(child)->FadeAlpha < 0.75f) continue; // stop rendering fadenodes below a certain opacity
			if (!child->IsGeometry())
				containers.push_back(child);
			else
				AccumObject(child, Forms, isLand && isLOD);
		}
	}
}

// Go through accumulations and render found objects
void ShadowManager::RenderAccums() {
	geometryPass->RenderAccum();
	terrainLODPass->RenderAccum();
	alphaPass->RenderAccum();
	skinnedGeoPass->RenderAccum();
	speedTreePass->RenderAccum();
}


void ShadowManager::RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;

	ShadowMap->ShadowCameraToLight = (*ViewProj);
	TheCameraManager->SetFrustum(&ShadowMap->ShadowMapFrustum, ViewProj);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_DEPTHBIAS, (DWORD)0.0f, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, (DWORD)0.0f, RenderStateArgs);

	if (ShadowMap->Forms.Lod) {
		AccumChildren(BGSTerrainManager::GetRootLandLODNode(), &ShadowMap->Forms, true, true, &ShadowMap->ShadowMapFrustumPlanes);
		AccumChildren(BGSTerrainManager::GetRootObjectLODNode(), &ShadowMap->Forms, false, true, &ShadowMap->ShadowMapFrustumPlanes);
	}

	if (Player->GetWorldSpace()) {
		GridCellArray* CellArray = Tes->gridCellArray;
		UInt32 CellArraySize = CellArray->size * CellArray->size;

		for (UInt32 i = 0; i < CellArraySize; i++) {
			AccumExteriorCell(CellArray->GetCell(i), ShadowMap);
		}
	}
	else {
		AccumExteriorCell(Player->parentCell, ShadowMap);
	}

	Device->SetViewport(&ShadowMap->ShadowMapViewPort);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

	if (ShadowMap->CustomClearRequired)
		ClearShadowCascade(&ShadowMap->ShadowMapViewPort, &ShadowMap->ClearColor);

	RenderAccums();
}


void ShadowManager::AccumExteriorCell(TESObjectCELL* Cell, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap) {
	if (!Cell || Cell->IsInterior())
		return;

	if (ShadowMap->Forms.Terrain)
		AccumChildren(Cell->GetChildNode(TESObjectCELL::kCellNode_Land), &ShadowMap->Forms, true, false, &ShadowMap->ShadowMapFrustumPlanes);

	TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First;
	while (Entry) {
		NiNode* RefNode = GetRefNode(Entry->item, &ShadowMap->Forms);

		if (!RefNode) {
			Entry = Entry->next;
			continue;
		}

		if (RefNode->WithinFrustum(&ShadowMap->ShadowMapFrustumPlanes) && !IsRefracting(Entry->item))
			AccumChildren(RefNode, &ShadowMap->Forms, false, false, &ShadowMap->ShadowMapFrustumPlanes);

		Entry = Entry->next;
	}
}


void ShadowManager::RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex) {
	NiSpotLight* pNiLight = Lights[LightIndex];
	if (pNiLight == NULL || !pNiLight->CastShadows) return;

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::InteriorsStruct* Settings = &Shadows->Settings.Interiors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	D3DXMATRIX View, Proj;

	NiPoint3* LightPos = &pNiLight->m_worldTransform.pos;
	float Radius = pNiLight->Spec.r;

#if defined(OBLIVION)
	if (pNiLight->CanCarry)
		Radius = 256.0f;
#endif

	D3DXVECTOR3 Up = D3DXVECTOR3(0, 0, 1);
	D3DXVECTOR3 Eye = LightPos->toD3DXVEC3();
	Eye.x -= TheRenderManager->CameraPosition.x;
	Eye.y -= TheRenderManager->CameraPosition.y;
	Eye.z -= TheRenderManager->CameraPosition.z;
	Shadows->Constants.ShadowCubeMapLightPosition.x = Eye.x;
	Shadows->Constants.ShadowCubeMapLightPosition.y = Eye.y;
	Shadows->Constants.ShadowCubeMapLightPosition.z = Eye.z;
	Shadows->Constants.ShadowCubeMapLightPosition.w = Radius;
	Shadows->Constants.Data.z = Radius;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(pNiLight->OuterSpotAngle * 2), 1.0f, 0.1f, Radius);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, 0, RenderStateArgs);

	D3DXVECTOR3 CameraDirection = D3DXVECTOR3(pNiLight->m_worldTransform.rot.data[0][0], pNiLight->m_worldTransform.rot.data[1][0], pNiLight->m_worldTransform.rot.data[2][0]);
	D3DXVECTOR3 At = Eye + CameraDirection;

	TList<TESObjectREFR>::Entry* Entry = &Player->parentCell->objectList.First;
	while (Entry) {
		NiNode* RefNode = GetRefNode(Entry->item, &Settings->Forms);
		if (!RefNode) {
			Entry = Entry->next;
			continue;
		}

		// Detect if the object is in front of the light in the direction of the current face
		// TODO: improve to base on frustum
		D3DXVECTOR3 ObjectPos = RefNode->m_worldTransform.pos.toD3DXVEC3();
		D3DXVECTOR3 ObjectToLight = ObjectPos - LightPos->toD3DXVEC3();

		D3DXVec3Normalize(&ObjectToLight, &ObjectToLight);
		bool inFront = D3DXVec3Dot(&ObjectToLight, &CameraDirection) > 0;
		if (inFront && RefNode->GetDistance(LightPos) <= Radius + RefNode->GetWorldBoundRadius()) 
			AccumChildren(RefNode, &Settings->Forms, false, false);

		Entry = Entry->next;
	}

	D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
	Shadows->Constants.ShadowViewProj = View * Proj;
	TheShaderManager->SpotLightWorldToLightMatrix[LightIndex] = (Shadows->Constants.ShadowViewProj);

	Device->SetRenderTarget(0, Shadows->Textures.ShadowSpotlightSurface[LightIndex]);
	Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);

	Device->SetViewport(&ShadowCubeMapViewPort);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

	RenderAccums();
}


static bool PlayerLampOffset(const ShadowSceneLight* light, const NiPoint3* lightPos, D3DXVECTOR3& offset) {
	offset = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	bool now = false;
	const ShadowsExteriorEffect::InteriorsStruct& interiors = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors;
	if (Player && light && lightPos && interiors.PlayerInsideLamp) {
		const float scale = Player->scale > 0.0f ? Player->scale : 1.0f;
		const float margin = interiors.PlayerLampMargin;
		const NiPoint3& feet = Player->pos;
		float top = feet.z + 128.0f * scale;
		if (NiNode* node = Player->GetNode())
			if (NiBound* bound = node->GetWorldBound())
				top = (std::min)(bound->Center.z + bound->Radius, feet.z + 200.0f * scale);
		const float zTop = top + margin, zBottom = feet.z - margin, reach = 32.0f * scale + margin;
		const float dx = lightPos->x - feet.x, dy = lightPos->y - feet.y, d = sqrtf(dx * dx + dy * dy);
		if (d < reach && lightPos->z > zBottom && lightPos->z < zTop) {
			now = true;
			auto smooth = [](float a, float b, float x) { const float s = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return s * s * (3.0f - 2.0f * s); };
			const float depth = 1.0f - d / reach;
			const float across = reach * (1.0f - smooth(0.5f, 1.0f, depth));
			const float height = lightPos->z + (zTop - lightPos->z) * smooth(0.0f, 0.5f, depth);
			const float ux = d > 0.001f ? dx / d : 0.0f, uy = d > 0.001f ? dy / d : 0.0f;
			const float ends = (std::min)(std::clamp((zTop - lightPos->z) / margin, 0.0f, 1.0f), std::clamp((lightPos->z - zBottom) / margin, 0.0f, 1.0f));
			offset = D3DXVECTOR3(feet.x + ux * across - lightPos->x, feet.y + uy * across - lightPos->y, height - lightPos->z) * ends;
		}
	}
	static const ShadowSceneLight* inside[ShadowCubeMapsMax] = {};
	int found = -1, empty = -1;
	for (int i = 0; i < ShadowCubeMapsMax; i++) {
		if (inside[i] == light) found = i;
		else if (!inside[i] && empty < 0) empty = i;
	}
	if (now && found < 0 && empty >= 0) inside[empty] = light;
	if (!now && found >= 0) inside[found] = nullptr;
	if (now != (found >= 0) && lightPos && interiors.Forward.LogLamps)
		Logger::Log("UNOFFICIAL PlayerInsideLamp: lamp %p at %.0f %.0f %.0f %s", (const void*)light, lightPos->x, lightPos->y, lightPos->z,
			now ? "is inside the player: the player is left out of its cube map" : "is outside the player again");
	return now;
}

bool ShadowManager::RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex, CubeLayer Layer) {
	if (Lights[LightIndex] == NULL) return true; // No light at current index
	
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::InteriorsStruct* Settings = &Shadows->Settings.Interiors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;

	if (Layer == CubeLayer::MovingOnto) {
		for (int Face = 0; Face < 6; Face++) {
			IDirect3DSurface9* from = Shadows->Textures.ShadowCubeMapStaticSurface[LightIndex][Face];
			IDirect3DSurface9* to = Shadows->Textures.ShadowCubeMapSurface[LightIndex][Face];
			if (!from || !to || FAILED(Device->StretchRect(from, NULL, to, NULL, D3DTEXF_NONE))) return false;
		}
	}
	float Radius = 0.0f;
	float MinRadius = Settings->Forms.MinRadius;
	NiPoint3* LightPos = NULL;
	D3DXMATRIX View, Proj;
	D3DXVECTOR3 Eye, At, Up, CameraDirection;

	NiPointLight* pNiLight = Lights[LightIndex]->sourceLight;

	LightPos = &pNiLight->m_worldTransform.pos;
	Radius = pNiLight->Spec.r * Shadows->Settings.Interiors.LightRadiusMult;
#if defined(OBLIVION)
	if (pNiLight->CanCarry)
		Radius = 256.0f;
#endif
	Eye.x = LightPos->x - TheRenderManager->CameraPosition.x;
	Eye.y = LightPos->y - TheRenderManager->CameraPosition.y;
	Eye.z = LightPos->z - TheRenderManager->CameraPosition.z;
	Shadows->Constants.ShadowCubeMapLightPosition.x = Eye.x;
	Shadows->Constants.ShadowCubeMapLightPosition.y = Eye.y;
	Shadows->Constants.ShadowCubeMapLightPosition.z = Eye.z;
	Shadows->Constants.ShadowCubeMapLightPosition.w = Radius;
	Shadows->Constants.Data.z = Radius;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(90.0f), 1.0f, 0.1f, Radius);
	D3DXVECTOR3 playerOffset;
	const bool lampInsidePlayer = PlayerLampOffset(Lights[LightIndex], LightPos, playerOffset);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
	
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS, RenderStateArgs);

	static const D3DXVECTOR3 FaceDirection[6] = { D3DXVECTOR3(1.0f, 0.0f, 0.0f), D3DXVECTOR3(-1.0f, 0.0f, 0.0f),
		D3DXVECTOR3(0.0f, 1.0f, 0.0f), D3DXVECTOR3(0.0f, -1.0f, 0.0f), D3DXVECTOR3(0.0f, 0.0f, -1.0f), D3DXVECTOR3(0.0f, 0.0f, 1.0f) };
	static const D3DXVECTOR3 FaceUp[6] = { D3DXVECTOR3(0.0f, 1.0f, 0.0f), D3DXVECTOR3(0.0f, 1.0f, 0.0f),
		D3DXVECTOR3(0.0f, 0.0f, 1.0f), D3DXVECTOR3(0.0f, 0.0f, -1.0f), D3DXVECTOR3(0.0f, 1.0f, 0.0f), D3DXVECTOR3(0.0f, 1.0f, 0.0f) };
	const bool useGeometryList = Lights[LightIndex]->kGeometryList.start != nullptr;
	cubeCasters.clear();
	if (useGeometryList) {
		// Since this is pure geometry, getting reference data will be difficult (read: slow)
		for (auto iter = Lights[LightIndex]->kGeometryList.start; iter; iter = iter->next) {
			NiGeometry* geo = iter->data;
			if (!geo || geo->m_flags & NiAVObject::APP_CULLED)
				continue;

			BSShaderProperty* shaderProp = static_cast<BSShaderProperty*>(geo->GetProperty(NiProperty::kType_Shade));
			NiMaterialProperty* matProp = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));

			if (!shaderProp)
				continue;

			if (Layer != CubeLayer::All && IsMovingCaster(geo, shaderProp) != (Layer == CubeLayer::MovingOnto))
				continue;

			// Skip refraction and fire refraction.
			if (!CheckShaderFlags(geo))
				continue;

			bool isFirstPerson = shaderProp->m_usFlags.GetBit(NiShadeProperty::kFirstPerson);
			bool isThirdPerson = shaderProp->m_usFlags.GetBit(NiShadeProperty::kThirdPerson);

			// Skip objects if they are barely visible.
			if ((matProp && matProp->fAlpha < 0.05f))
				continue;

			// Also skip viewmodel due to issues, and render player's model only in 3rd person
			if (isFirstPerson) continue;

			if (!Player->isThirdPerson && !Settings->PlayerShadowFirstPerson && isThirdPerson)
				continue;

			if (Player->isThirdPerson && !Settings->PlayerShadowThirdPerson && isThirdPerson)
				continue;

			if (lampInsidePlayer && isThirdPerson) continue;

			unsigned char faces = 63;
			if (shaderProp->IsLightingProperty()) {
				if (geo->skinInstance && TheSettingManager->SettingsMain.Main.SkinnedShadowFaceTest) {
					NiBound skinned;
					if (SkinnedWorldBound(geo, skinned)) {
						const float dx = skinned.Center.x - LightPos->x, dy = skinned.Center.y - LightPos->y, dz = skinned.Center.z - LightPos->z;
						if (sqrtf(dx * dx + dy * dy + dz * dz) - skinned.Radius > Radius) { statSkinnedOutOfRange++; continue; }
					}
					else statSkinnedNoBound++;
				}
				faces = 0;
				for (int face = 0; face < 6; face++)
					if (TouchesShadowFace(geo, LightPos, FaceDirection[face])) faces |= (unsigned char)(1 << face);
				if (!faces) continue;
			}
			if (geo->skinInstance) {
				statSkinnedCasters++;
				for (int face = 0; face < 6; face++) if (faces & (1 << face)) statSkinnedFaces++;
			}

			RenderPass* pass = nullptr;
			int test = skinnedGeoPass->TestObject(geo);
			if (test == 1) pass = skinnedGeoPass;
			if (!test) { test = speedTreePass->TestObject(geo); if (test == 1) pass = speedTreePass; }
			if (!test && Settings->Forms.AlphaEnabled) { test = alphaPass->TestObject(geo); if (test == 1) pass = alphaPass; }
			if (!test) { test = geometryPass->TestObject(geo); if (test == 1) pass = geometryPass; }
			if (!pass) continue;
			cubeCasters.push_back({ geo, pass, faces });
		}
	}

	if (Layer == CubeLayer::MovingOnto) {
		RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MIN, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE, RenderStateArgs);
	}

	if (Settings->Forward.LogLamps && useGeometryList && Layer != CubeLayer::MovingOnto) {
		static std::unordered_map<const void*, bool> loggedLamps;
		static unsigned casterLines = 0;
		const ShadowSceneLight* lamp = Lights[LightIndex];
		if (casterLines < 600 && !loggedLamps[lamp]) {
			loggedLamps[lamp] = true;
			const float nearLimit = (std::max)(48.0f, 0.2f * pNiLight->Spec.r);
			int nearCount = 0;
			for (const CubeCaster& caster : cubeCasters) {
				const NiBound* b = caster.geometry->m_kWorldBound;
				if (!b) continue;
				const float dx = b->Center.x - LightPos->x, dy = b->Center.y - LightPos->y, dz = b->Center.z - LightPos->z;
				if (sqrtf(dx * dx + dy * dy + dz * dz) - b->Radius < nearLimit) nearCount++;
			}
			casterLines++;
			Logger::Log("CASTERS lamp %p at %.0f %.0f %.0f, reach %.0f, cube radius %.0f, %s: %u casters, %d within %.0f of the lamp:",
				(const void*)lamp, LightPos->x, LightPos->y, LightPos->z, pNiLight->Spec.r, Radius,
				Layer == CubeLayer::StaticOnly ? "static layer" : "whole", (unsigned)cubeCasters.size(), nearCount, nearLimit);
			int listed = 0;
			for (const CubeCaster& caster : cubeCasters) {
				NiGeometry* geo = caster.geometry;
				const NiBound* b = geo->m_kWorldBound;
				if (!b || listed >= 14 || casterLines >= 600) continue;
				const float dx = b->Center.x - LightPos->x, dy = b->Center.y - LightPos->y, dz = b->Center.z - LightPos->z;
				const float gap = sqrtf(dx * dx + dy * dy + dz * dz) - b->Radius;
				if (gap >= nearLimit) continue;
				NiShadeProperty* shade = static_cast<NiShadeProperty*>(geo->GetProperty(NiProperty::kType_Shade));
				NiAlphaProperty* alpha = static_cast<NiAlphaProperty*>(geo->GetProperty(NiProperty::kType_Alpha));
				NiMaterialProperty* material = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));
				const char* passName = caster.pass == skinnedGeoPass ? "skinned" : caster.pass == speedTreePass ? "speedtree" : caster.pass == alphaPass ? "alpha-tested" : "opaque";
				listed++;
				casterLines++;
				Logger::Log("CASTERS   %s %p: shader type %d, alpha %s%s, material alpha %.2f, drawn as %s, bound radius %.0f, %.0f from the lamp%s",
					geo->m_pcName ? geo->m_pcName : "(unnamed)", (const void*)geo, shade ? (int)shade->m_eShaderType : -1,
					alpha && (alpha->flags & NiAlphaProperty::ALPHA_BLEND_MASK) ? "blended" : "not blended",
					alpha && (alpha->flags & NiAlphaProperty::TEST_ENABLE_MASK) ? ", tested" : "", material ? material->fAlpha : -1.0f,
					passName, b->Radius, gap, gap < 0.0f ? " (around the lamp)" : "");
			}
		}
	}

	for (int Face = 0; Face < 6; Face++) {
		CameraDirection = FaceDirection[Face];
		Up = FaceUp[Face];
		At = Eye + CameraDirection;

		if (useGeometryList) {
			const unsigned char bit = (unsigned char)(1 << Face);
			for (const CubeCaster& caster : cubeCasters)
				if (caster.faces & bit) caster.pass->GeometryList.push(caster.geometry);
		}
		else {
			// old form based geo accumulation when the one perform by the game has not handled this light
			TList<TESObjectREFR>::Entry* Entry = &Player->parentCell->objectList.First;
			while (Entry) {
				if (NiNode* RefNode = GetRefNode(Entry->item, &Settings->Forms)) {
					// Detect if the object is in front of the light in the direction of the current face
					// TODO: improve to base on frustum
					D3DXVECTOR3 ObjectPos = RefNode->m_worldTransform.pos.toD3DXVEC3();
					D3DXVECTOR3 ObjectToLight = ObjectPos - LightPos->toD3DXVEC3();

					D3DXVec3Normalize(&ObjectToLight, &ObjectToLight);
					bool inFront = D3DXVec3Dot(&ObjectToLight, &CameraDirection) > 0;
					if (RefNode->GetDistance(LightPos) <= Radius + RefNode->GetWorldBoundRadius() && !IsRefracting(Entry->item))
						AccumChildren(RefNode, &Settings->Forms, false, false);
				}
				Entry = Entry->next;
			}
		}



		D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
		Shadows->Constants.ShadowViewProj = View * Proj;

		Device->SetRenderTarget(0, Layer == CubeLayer::StaticOnly ? Shadows->Textures.ShadowCubeMapStaticSurface[LightIndex][Face]
			: Shadows->Textures.ShadowCubeMapSurface[LightIndex][Face]);
		Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);

		Device->SetViewport(&ShadowCubeMapViewPort);
		Device->Clear(0L, NULL, Layer == CubeLayer::MovingOnto ? D3DCLEAR_ZBUFFER : D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

		RenderAccums();
	}

	if (Layer == CubeLayer::MovingOnto) {
		RenderState->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	}
	return true;
}


static void FlagShaderPropertyRecurse(NiAVObject* apObject, UInt32 auiFlags, bool abSet) {
	if (!apObject)
		return;

	if (apObject->IsGeometry()) {
		NiGeometry* pGeometry = static_cast<NiGeometry*>(apObject);
		NiShadeProperty* shaderProperty = static_cast<NiShadeProperty*>(pGeometry->GetProperty(NiProperty::kType_Shade));
		if (shaderProperty) {
			if (abSet)
				shaderProperty->m_usFlags.Set(auiFlags);
			else
				shaderProperty->m_usFlags.Clear(auiFlags);
		}

	}
	else if (apObject->IsNiNode()) {
		NiNode* pNiNode = static_cast<NiNode*>(apObject);
		for (UInt32 i = 0; i < pNiNode->m_children.end; i++) {
			FlagShaderPropertyRecurse(pNiNode->m_children.data[i], auiFlags, abSet);
		}
	}
}

static SInt32 frames = -1;
static void FlagPlayerGeometry() {
	frames++;

	// Run this function every 50 frames, or on launch
	if (frames > 50 || frames == -1) {
		if (Player->firstPersonNiNode)
			FlagShaderPropertyRecurse(Player->firstPersonNiNode, NiShadeProperty::kFirstPerson, true);

		NiNode* node = Player->GetNode();
		if (node)
			FlagShaderPropertyRecurse(node, NiShadeProperty::kThirdPerson, true);

		frames = 0;
	}
}

void ShadowManager::RecalculateBillboardVectors(D3DXVECTOR3* SunDir) {
	D3DXVECTOR3 WorldUp = D3DXVECTOR3(0.0f, 0.0f, 1.0f);

	// Calculate BillboardRight as perpendicular to SunDir and WorldUp
	D3DXVECTOR3 BillboardRightVec;
	D3DXVec3Cross(&BillboardRightVec, &WorldUp, SunDir);

	// Handle case where sun is directly above/below
	if (D3DXVec3LengthSq(&BillboardRightVec) < 0.0001f) {
		BillboardRightVec = D3DXVECTOR3(1.0f, 0.0f, 0.0f);
	}
	D3DXVec3Normalize(&BillboardRightVec, &BillboardRightVec);

	// Calculate BillboardUp perpendicular to both SunDir and BillboardRightVec
	D3DXVECTOR3 BillboardUpVec;
	D3DXVec3Cross(&BillboardUpVec, SunDir, &BillboardRightVec);
	D3DXVec3Normalize(&BillboardUpVec, &BillboardUpVec);

	// Set shader constants
	BillboardRight = NiVector4(BillboardRightVec.x, BillboardRightVec.y, BillboardRightVec.z, 0.0f);
	BillboardUp = NiVector4(BillboardUpVec.x, BillboardUpVec.y, BillboardUpVec.z, 0.0f);
}

/*
* Renders the different shadow maps: Near, Far, Ortho.
*/
void ShadowManager::CollectMovers(const D3DXVECTOR3& SunDir) {
	FrameMovers.clear();
	MoverShadowStretch = min(sqrtf(SunDir.x * SunDir.x + SunDir.y * SunDir.y) / max(SunDir.z, 0.1f), 10.0f);
	auto add = [this](NiNode* node) {
		if (!node || node->m_flags & NiAVObject::APP_CULLED) return;
		NiBound* bound = node->GetWorldBound();
		if (bound) FrameMovers.push_back(D3DXVECTOR4(bound->Center.x, bound->Center.y, bound->Center.z, bound->Radius));
	};
	add(Player->GetNode());
	if (!Player->GetWorldSpace() || !Tes || !Tes->gridCellArray) return;
	GridCellArray* CellArray = Tes->gridCellArray;
	const UInt32 CellArraySize = CellArray->size * CellArray->size;
	for (UInt32 c = 0; c < CellArraySize; c++) {
		TESObjectCELL* Cell = CellArray->GetCell(c);
		if (!Cell || Cell->IsInterior()) continue;
		for (TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First; Entry; Entry = Entry->next) {
			TESObjectREFR* Ref = Entry->item;
			if (!Ref || !Ref->baseForm) continue;
			const UInt8 type = Ref->baseForm->formType;
			if (type == TESForm::FormType::kFormType_NPC || type == TESForm::FormType::kFormType_Creature ||
				type == TESForm::FormType::kFormType_LeveledCreature)
				add(Ref->GetNode());
		}
	}
}

bool ShadowManager::MoversInCascade(int cascade, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, float InnerDepth) {
	if (!ShadowMap->Forms.Actors || FrameMovers.empty()) return false;
	const D3DXVECTOR3 camera = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
	const D3DXVECTOR3 forward(TheRenderManager->CameraForward.x, TheRenderManager->CameraForward.y, TheRenderManager->CameraForward.z);
	const D3DXVECTOR3 moved = camera - CascadeRefreshCamera[cascade];
	const float pad = D3DXVec3Length(&moved) + 64.0f;
	NiFrustumPlanes* planes = &ShadowMap->ShadowMapFrustumPlanes;
	for (const D3DXVECTOR4& mover : FrameMovers) {
		const D3DXVECTOR3 toMover = D3DXVECTOR3(mover.x, mover.y, mover.z) - camera;
		const float shadowReach = mover.w * (1.0f + 2.0f * MoverShadowStretch) + 64.0f;
		if (D3DXVec3Dot(&toMover, &forward) + shadowReach < 0.9f * InnerDepth) continue;
		bool inside = true;
		for (UInt32 p = 0; p < NiFrustumPlanes::MaxPlanes && inside; p++) {
			if (!planes->IsPlaneActive(p)) continue;
			const NiPlane& plane = planes->CullingPlanes[p];
			const float distance = plane.Normal.x * mover.x + plane.Normal.y * mover.y + plane.Normal.z * mover.z - plane.Constant;
			if (distance < -(mover.w + pad)) inside = false;
		}
		if (inside) return true;
	}
	return false;
}

void ShadowManager::LogSunShadowStats(bool cachedDistant) {
	if (!GpuTimer::Enabled || !cachedDistant) { SunStats = {}; return; }
	SunStats.Characters += (unsigned)FrameMovers.size();
	if (++SunStats.Frames < 240) return;
	const float perFrame = 100.0f / SunStats.Frames;
	Logger::Log("CACHED DISTANT SHADOWS %u frames: redrawn %% of frames (scheduled + for characters): near %.0f, "
		"middle %.0f + %.0f, far %.0f + %.0f, LOD %.0f + %.0f; characters tracked %.1f",
		SunStats.Frames, SunStats.Scheduled[MapNear] * perFrame,
		SunStats.Scheduled[MapMiddle] * perFrame, SunStats.ForCharacters[MapMiddle] * perFrame,
		SunStats.Scheduled[MapFar] * perFrame, SunStats.ForCharacters[MapFar] * perFrame,
		SunStats.Scheduled[MapLod] * perFrame, SunStats.ForCharacters[MapLod] * perFrame,
		SunStats.Characters / (float)SunStats.Frames);
	SunStats = {};
}

void ShadowManager::RenderShadowMaps() {
	PointShadowForward::Begin();
	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // cancel out if rendering effects is disabled
	if ((++SkinnedBoundFrame & 1023) == 0) {
		SkinnedBounds.clear();
		for (auto it = CasterMotions.begin(); it != CasterMotions.end();)
			it = SkinnedBoundFrame - it->second.seen > 1024 ? CasterMotions.erase(it) : std::next(it);
	}

	// track point lights for interiors and exteriors
	ShadowSceneLight* ShadowLights[ShadowCubeMapsMax] = { NULL };
	NiPointLight* Lights[TrackedLightsMax] = { NULL };
	NiSpotLight* SpotLights[SpotLightsMax] = { NULL };

	TheShaderManager->GetNearbyLights(ShadowLights, Lights, SpotLights);

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::ExteriorsStruct* ShadowsExteriors = &Shadows->Settings.Exteriors;
	ShadowsExteriorEffect::InteriorsStruct* ShadowsInteriors = &Shadows->Settings.Interiors;

	bool isExterior = TheShaderManager->GameState.isExterior;// || currentCell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior; // exterior flag currently broken
	bool ExteriorEnabled = isExterior && TheShaderManager->Effects.ShadowsExteriors->Enabled && ShadowsExteriors->Enabled;
	bool InteriorEnabled = !isExterior && TheShaderManager->Effects.ShadowsInteriors->Enabled;

	// early out in case shadow rendering is not required
	if (!ExteriorEnabled && !InteriorEnabled && !TheShaderManager->orthoRequired || !ShadowShadersLoaded) {
		return;
	}
	if (!Player->parentCell) return;

	auto timer = TimeLogger();
	static CpuTimer shadowMapsCpuTimer("Shadow maps (CPU)");
	CpuProfileScope shadowMapsCpu(shadowMapsCpuTimer);

	// prepare some pointers to the device and surfaces
	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	static GpuTimer sunCascadesTimer("Sun cascade geometry");
	static GpuTimer atlasResolveTimer("Shadow atlas resolve");
	static GpuTimer atlasFilterTimer("Shadow atlas prefilter");
	static GpuTimer orthoMapTimer("Ortho shadow map");
	static GpuTimer pointMapsTimer("Point shadow cubemaps");
	static GpuTimer flashlightMapsTimer("Flashlight shadow maps");
	IDirect3DSurface9* DepthSurface = NULL;
	IDirect3DSurface9* RenderSurface = NULL;
	D3DVIEWPORT9 viewport;

	D3DXVECTOR4* ShadowData = &TheShaderManager->Effects.ShadowsExteriors->Constants.Data;
	D3DXVECTOR4* OrthoData = &TheShaderManager->Effects.ShadowsExteriors->Constants.OrthoData;
	Device->GetDepthStencilSurface(&DepthSurface);
	Device->GetRenderTarget(0, &RenderSurface);
	Device->GetViewport(&viewport);	

	DWORD zfunc;
	Device->GetRenderState(D3DRS_ZFUNC, &zfunc); // backup in case of inverted depth

	DWORD oldStencilEnable, oldStencilRef, oldStencilFunc;
	DWORD oldAlphaRef, oldNormalizeNormals, oldPointSize;

	Device->GetRenderState(D3DRS_STENCILENABLE, &oldStencilEnable);
	Device->GetRenderState(D3DRS_STENCILREF, &oldStencilRef);
	Device->GetRenderState(D3DRS_STENCILFUNC, &oldStencilFunc);
	Device->GetRenderState(D3DRS_ALPHAREF, &oldAlphaRef);
	Device->GetRenderState(D3DRS_NORMALIZENORMALS, &oldNormalizeNormals);
	Device->GetRenderState(D3DRS_POINTSIZE, &oldPointSize);

	if (1.0 - NiDX9Renderer::GetSingleton()->m_fZClear) // inverted depth
		RenderState->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL, RenderStateArgs);
	else
		RenderState->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_STENCILENABLE, 1, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_STENCILREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_NORMALIZENORMALS, 1, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_POINTSIZE, 810365505, RenderStateArgs);

	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();
	
	D3DXVECTOR4 PlayerPosition = Player->pos.toD3DXVEC4();
	TESObjectCELL* currentCell = Player->parentCell;

	// Flag player geometry so we can control if it should be rendered in shadow cubemaps
	FlagPlayerGeometry();

	// Render directional shadows for Sun/Moon
	NiNode* PlayerNode = Player->GetNode();
	D3DXVECTOR3 At;
	At.x = PlayerNode->m_worldTransform.pos.x - TheRenderManager->CameraPosition.x;
	At.y = PlayerNode->m_worldTransform.pos.y - TheRenderManager->CameraPosition.y;
	At.z = PlayerNode->m_worldTransform.pos.z - TheRenderManager->CameraPosition.z;

	// Render all shadow maps
	Device->BeginScene();

	// Quantize sun direction angle to reduce shimmer by a large factor.
	D3DXVECTOR3 SunDir = Shadows->CalculateSmoothedSunDir();

	if (isExterior && (ExteriorEnabled || TheShaderManager->orthoRequired)) {

		// Update cascade depths based on current camera.
		Shadows->GetCascadeDepths();

		geometryPass->VertexShader = ShadowMapVertex;
		geometryPass->PixelShader = ShadowMapPixel;
		alphaPass->VertexShader = ShadowMapVertex;
		alphaPass->PixelShader = ShadowMapPixel;
		skinnedGeoPass->VertexShader = ShadowMapVertex;
		skinnedGeoPass->PixelShader = ShadowMapPixel;
		speedTreePass->VertexShader = ShadowMapVertex;
		speedTreePass->PixelShader = ShadowMapPixel;
		terrainLODPass->VertexShader = ShadowMapVertex;
		terrainLODPass->PixelShader = ShadowMapPixel;

		if (ContactHardeningCompiled) {
			const bool on = Shadows->Settings.ContactHardening.Enabled;
			ContactHardeningData.x = on ? 0.00925f * Shadows->Settings.ContactHardening.SunSize : 0.0f;
			ContactHardeningData.y = Shadows->Settings.ContactHardening.MaxSoftness;
			static float logged = -1.0f;
			if (ContactHardeningData.x != logged) {
				Logger::Log("UNOFFICIAL contact hardening %s (penumbra %.4f per unit of distance, max %.0f texels)",
					on ? "ON" : "OFF", ContactHardeningData.x, ContactHardeningData.y);
				logged = ContactHardeningData.x;
			}
		}
		else {
			static bool hinted = false;
			if (!hinted && Shadows->Settings.ContactHardening.Enabled) {
				Logger::Log("UNOFFICIAL contact hardening was turned on after startup: restart the game once to apply it.");
				hinted = true;
			}
		}
		if (ExteriorEnabled && SunDir.z > 0.0f) {
			// Recalculate billboard vectors for speedtree leaves shader.
			RecalculateBillboardVectors(&SunDir);

			ShadowData->z = 0; // set shader constant to identify other shadow maps
			auto shadowMapTimer = TimeLogger();

			if (Shadows->ShadowAtlasSurfaceMSAA)
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurfaceMSAA);
			else
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurface);

			Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);

			unsigned updatedCascades = 0;
			{
			static CpuTimer sunCascadesCpuTimer("Sun cascades (CPU)");
			CpuProfileScope cpu(sunCascadesCpuTimer);
			GpuProfileScope gpu(sunCascadesTimer, Device);
			const bool staggered = TheSettingManager->SettingsMain.Main.StaggeredSunShadows;
			const bool cachedDistant = TheSettingManager->SettingsMain.Main.CachedDistantShadows && !staggered;
			if (cachedDistant) CollectMovers(SunDir);
			for (int i = MapNear; i < MapOrtho; i++) {
				ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[i];
				const bool cachedCascade = cachedDistant && i != MapNear;
				const unsigned updatePeriod = SunCascadeUpdatePeriod(i, Shadows->Settings.ShadowMaps.LimitFrequency,
					TheSettingManager->SettingsMain.Main.NearCascadeInterval, staggered || cachedCascade);

				bool update = ForceAllCascades || SunCascadeUpdatesOnFrame(i, FrameCounter, updatePeriod);
				const float innerDepth = i > MapNear ? ((const float*)Shadows->Constants.ShadowMapRadius)[i - 1] : 0.0f;
				if (update) SunStats.Scheduled[i]++;
				else if (cachedCascade && (CascadeHadMover[i] || MoversInCascade(i, ShadowMap, innerDepth))) {
					update = true;
					SunStats.ForCharacters[i]++;
				}
				if (update) {
					updatedCascades |= 1u << i;
					Shadows->Constants.ShadowViewProj = Shadows->GetCascadeViewProj(ShadowMap, &SunDir);
					RenderShadowMap(ShadowMap, &Shadows->Constants.ShadowViewProj);
					CascadeRefreshCamera[i] = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
					CascadeHadMover[i] = cachedCascade && MoversInCascade(i, ShadowMap, innerDepth);
				}
				else {
					D3DXVECTOR3 newCameraTranslation = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
					D3DXVECTOR3 difference = newCameraTranslation - ShadowMap->CameraTranslation;
					D3DXMATRIX translationMatrix;
					D3DXMatrixTranslation(&translationMatrix, difference.x, difference.y, difference.z);
					ShadowMap->ShadowCameraToLight = translationMatrix * ShadowMap->ShadowCameraToLight;
					ShadowMap->CameraTranslation = newCameraTranslation;
					if (i == MapNear) {
						ShadowMap->ShadowMapCascadeCenterRadius.x -= difference.x;
						ShadowMap->ShadowMapCascadeCenterRadius.y -= difference.y;
						ShadowMap->ShadowMapCascadeCenterRadius.z -= difference.z;
					}

				}

				std::string message = "ShadowManager::RenderShadowMap ";
				message += std::to_string(i);
				shadowMapTimer.LogTime(message.c_str());
			}
			ForceAllCascades = false;
			LogSunShadowStats(cachedDistant);
			}

			// Resolve MSAA.
			if (Shadows->ShadowAtlasSurfaceMSAA) {
				GpuProfileScope gpu(atlasResolveTimer, Device);
				bool partialResolveFailed = false;
				for (int i = MapNear; i < MapOrtho; ++i) {
					if (!(updatedCascades & (1u << i))) continue;
					const D3DVIEWPORT9& viewport = Shadows->ShadowMaps[i].ShadowMapViewPort;
					RECT region = {(LONG)viewport.X, (LONG)viewport.Y,
						(LONG)(viewport.X + viewport.Width), (LONG)(viewport.Y + viewport.Height)};
					if (FAILED(Device->StretchRect(Shadows->ShadowAtlasSurfaceMSAA, &region,
						Shadows->ShadowAtlasSurface, &region, D3DTEXF_NONE))) {
						partialResolveFailed = true;
						break;
					}
				}
				if (partialResolveFailed) {
					Device->StretchRect(Shadows->ShadowAtlasSurfaceMSAA, NULL,
						Shadows->ShadowAtlasSurface, NULL, D3DTEXF_NONE);
					updatedCascades = (1u << MapOrtho) - 1;
				}
			}

			if (Shadows->Settings.ShadowMaps.Prefilter) {
				GpuProfileScope gpu(atlasFilterTimer, Device);
				BlurShadowAtlas(updatedCascades);
			}

			if (Shadows->Settings.ShadowMaps.Mipmaps)
				Shadows->ShadowAtlasTexture->GenerateMipSubLevels();
		}

		// render ortho map if one of the effects using ortho is active
		if (TheShaderManager->orthoRequired) {
			GpuProfileScope gpu(orthoMapTimer, Device);
			auto shadowMapTimer = TimeLogger();

			ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[MapOrtho];

			if (!Shadows->Settings.OrthoMap.LimitFrequency || !((FrameCounter + 2) % 4)) {
				Device->SetRenderTarget(0, Shadows->ShadowMapOrthoSurface);
				Device->SetDepthStencilSurface(Shadows->ShadowMapOrthoDepthSurface);

				ShadowData->z = 1; // identify ortho map in shader constant
				D3DXVECTOR3 OrthoDir = D3DXVECTOR3(0.05f, 0.05f, 1.0f);
				Shadows->Constants.ShadowViewProj = Shadows->GetCascadeViewProj(ShadowMap, &OrthoDir);

				RenderShadowMap(ShadowMap, &Shadows->Constants.ShadowViewProj);
			}
			else {
				D3DXVECTOR3 newCameraTranslation = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
				D3DXVECTOR3 difference = newCameraTranslation - ShadowMap->CameraTranslation;
				D3DXMATRIX translationMatrix;
				D3DXMatrixTranslation(&translationMatrix, difference.x, difference.y, difference.z);
				ShadowMap->ShadowCameraToLight = translationMatrix * ShadowMap->ShadowCameraToLight;
				ShadowMap->CameraTranslation = newCameraTranslation;
			}

			OrthoData->x = Shadows->Settings.OrthoMap.Distance * 2;
			OrthoData->y = ShadowMap->ShadowMapInverseResolution;
	
			shadowMapTimer.LogTime("ShadowManager::RenderShadowMap Ortho");
		}
	}

	// Render shadow maps for point lights
	bool usePointLights = (TheShaderManager->GameState.isDayTime > 0.5) ? ShadowsExteriors->UsePointShadowsDay : ShadowsExteriors->UsePointShadowsNight;

	AlphaEnabled = ShadowsInteriors->Forms.AlphaEnabled;
	geometryPass->VertexShader = ShadowCubeMapVertex;
	geometryPass->PixelShader = ShadowCubeMapPixel;
	alphaPass->VertexShader = ShadowCubeMapVertex;
	alphaPass->PixelShader = ShadowCubeMapPixel;
	skinnedGeoPass->VertexShader = ShadowCubeMapVertex;
	skinnedGeoPass->PixelShader = ShadowCubeMapPixel;
	speedTreePass->VertexShader = ShadowCubeMapVertex;
	speedTreePass->PixelShader = ShadowCubeMapPixel;

	auto shadowMapTimer = TimeLogger();
	if ((isExterior && usePointLights) || (!isExterior && InteriorEnabled)) {
		GpuProfileScope gpu(pointMapsTimer, Device);
		static PointShadowSlotState slots[ShadowCubeMapsMax];
		static unsigned scheduleFrame = 0, statFrames = 0, statPresent = 0, statRedrawn = 0, statStaticReused = 0;
		static unsigned statReasons[(int)PointShadowRedraw::Count] = {};
		const unsigned interval = (unsigned)TheSettingManager->SettingsMain.Main.PointShadowInterval;
		static PointShadowSlotState staticSlots[ShadowCubeMapsMax];
		static unsigned statStaticDrawn = 0, statMovingOnly = 0, statWhole = 0;
		const bool split = ShadowsInteriors->RedrawActorsOnly && ActorsOnlySupported(Device);

		const int shadowedSlots = PointShadowForward::ShadowedSlots(isExterior);
		for (int i = 0; i < shadowedSlots; i++) {
			ShadowSceneLight* shadowLight = ShadowLights[i];
			if (!shadowLight || !Shadows->EnsureCubeMap(i)) { slots[i].valid = false; continue; }

			PointShadowSlotState now;
			NiPointLight* pointLight = shadowLight->sourceLight;
			now.light = shadowLight;
			now.texture = Shadows->Textures.ShadowCubeMapTexture[i];
			now.cell = currentCell;
			now.x = pointLight->m_worldTransform.pos.x;
			now.y = pointLight->m_worldTransform.pos.y;
			now.z = pointLight->m_worldTransform.pos.z;
#if defined(OBLIVION)
			now.radius = pointLight->CanCarry ? 256.0f : pointLight->Spec.r * ShadowsInteriors->LightRadiusMult;
#else
			now.radius = pointLight->Spec.r * ShadowsInteriors->LightRadiusMult;
#endif
			PointShadowCasterState(shadowLight, now.casterHash, now.staticCasters);
			now.valid = true;
			if (GpuTimer::Enabled) statPresent++;
			const PointShadowRedraw why = PointShadowRedrawReason(slots[i], now, scheduleFrame, i, interval);
			if (why == PointShadowRedraw::None) {
				if (GpuTimer::Enabled && now.staticCasters) statStaticReused++;
				continue;
			}

			const bool hasList = shadowLight->kGeometryList.start != nullptr;
			const bool moving = hasList && !now.staticCasters;
			PointShadowSlotState staticNow = now;
			staticNow.staticCasters = true;
			staticNow.texture = nullptr;
			if (split && hasList) {
				AdoptStaticCubeMap(Shadows, staticSlots, i, shadowLight);
				if (moving) Shadows->EnsureStaticCubeMap(i);
				staticNow.texture = Shadows->Textures.ShadowCubeMapStaticTexture[i];
			}
			const PointShadowDraw draw = PointShadowDrawPlan(split && hasList, staticSlots[i], staticNow, moving);
			if (draw == PointShadowDraw::StaticThenMoving) {
				RenderShadowCubeMap(ShadowLights, i, CubeLayer::StaticOnly);
				staticSlots[i] = staticNow;
				if (GpuTimer::Enabled) statStaticDrawn++;
			}
			bool drawn = false;
			if (draw != PointShadowDraw::All) {
				drawn = RenderShadowCubeMap(ShadowLights, i, CubeLayer::MovingOnto);
				if (drawn && GpuTimer::Enabled) statMovingOnly++;
				if (!drawn) {
					ActorsOnlyBroken = true;
					Logger::Log("UNOFFICIAL RedrawActorsOnly: copying a static cube map failed; lamps are redrawn whole from now on, as before");
				}
			}
			// Render targets set in function due to rendering multiple faces.
			if (!drawn) {
				RenderShadowCubeMap(ShadowLights, i);
				if (GpuTimer::Enabled) statWhole++;
			}
			slots[i] = now;
			if (GpuTimer::Enabled) { statRedrawn++; statReasons[(int)why]++; }

			std::string message = "ShadowManager::RenderShadowCubeMap ";
			message += std::to_string(i);
			shadowMapTimer.LogTime(message.c_str());
		}
		scheduleFrame++;
		if (!isExterior) {
			PointShadowForward::Publish(slots, ShadowLights, shadowedSlots, Shadows->Textures.ShadowCubeMapTexture);
			PointShadowForward::FrameStats();
		}

		if (GpuTimer::Enabled && ++statFrames >= 240) {
			const float perFrame = 1.0f / statFrames;
			Logger::Log("POINT SHADOWS interval %u: %.1f lights present, %.1f cubemaps redrawn, %.1f static cubemaps reused per frame (%u frames); "
				"redrawn because: scheduled %.2f, casters %.2f, new %.2f, other light %.2f, moved %.2f, resized %.2f, cell %.2f, texture %.2f",
				interval, statPresent * perFrame, statRedrawn * perFrame, statStaticReused * perFrame, statFrames,
				statReasons[(int)PointShadowRedraw::Scheduled] * perFrame, statReasons[(int)PointShadowRedraw::CastersChanged] * perFrame,
				statReasons[(int)PointShadowRedraw::NewSlot] * perFrame,
				statReasons[(int)PointShadowRedraw::OtherLight] * perFrame, statReasons[(int)PointShadowRedraw::Moved] * perFrame,
				statReasons[(int)PointShadowRedraw::Resized] * perFrame, statReasons[(int)PointShadowRedraw::OtherCell] * perFrame,
				statReasons[(int)PointShadowRedraw::OtherTexture] * perFrame);
			Logger::Log("POINT SHADOWS skinned casters (SkinnedShadowFaceTest %s): %.1f per frame drawn into %.1f faces (all faces would be %.1f), %.1f skipped beyond the lamp's reach, %.1f without a usable skeleton bound",
				TheSettingManager->SettingsMain.Main.SkinnedShadowFaceTest ? "on" : "off", statSkinnedCasters * perFrame, statSkinnedFaces * perFrame,
				statSkinnedCasters * perFrame * 6.0f, statSkinnedOutOfRange * perFrame, statSkinnedNoBound * perFrame);
			unsigned rigidMoving = 0;
			for (const auto& entry : CasterMotions)
				if (entry.second.seen == SkinnedBoundFrame && entry.second.changed && SkinnedBoundFrame - entry.second.changed < MovingSettleFrames) rigidMoving++;
			Logger::Log("POINT SHADOWS RedrawActorsOnly %s: per frame %.1f lamps redrawn whole, %.1f only their people (a copy of the walls and furniture, plus %.1f static layers drawn); %u rigid pieces moving now (held weapons, doors...)",
				split ? "on" : (ShadowsInteriors->RedrawActorsOnly ? "unavailable" : "off"), statWhole * perFrame, statMovingOnly * perFrame, statStaticDrawn * perFrame, rigidMoving);
			statSkinnedCasters = statSkinnedFaces = statSkinnedOutOfRange = statSkinnedNoBound = 0;
			statFrames = statPresent = statRedrawn = statStaticReused = 0;
			statStaticDrawn = statMovingOnly = statWhole = 0;
			for (unsigned& r : statReasons) r = 0;
		}
	}

	if (TheShaderManager->Effects.Flashlight->Enabled && TheShaderManager->Effects.Flashlight->spotLightActive && TheShaderManager->Effects.Flashlight->Settings.renderShadows) {
		GpuProfileScope gpu(flashlightMapsTimer, Device);
		// render shadow maps for spotlights
		
		for (int i = 0; i < SpotLightsMax; i++) {
			if (!SpotLights[i] || SpotLights[i]->Spec.r == 0) continue; //bypass lights with no radius

			// Render targets set in function.
			RenderShadowSpotlight(SpotLights, i);

			std::string message = "ShadowManager::RenderShadowSpotLight";
			message += std::to_string(i);
			shadowMapTimer.LogTime(message.c_str());
		}
	}

	// reset renderer to previous state
	Device->SetDepthStencilSurface(DepthSurface);
	Device->SetRenderTarget(0, RenderSurface);
	Device->SetViewport(&viewport);
	Device->SetRenderState(D3DRS_ZFUNC, zfunc);
	Device->SetRenderState(D3DRS_STENCILENABLE, oldStencilEnable);
	Device->SetRenderState(D3DRS_STENCILREF, oldStencilRef);
	Device->SetRenderState(D3DRS_STENCILFUNC, oldStencilFunc);
	Device->SetRenderState(D3DRS_ALPHAREF, oldAlphaRef);
	Device->SetRenderState(D3DRS_NORMALIZENORMALS, oldNormalizeNormals);
	Device->SetRenderState(D3DRS_POINTSIZE, oldPointSize);

	//release smart pointers to prevent memory leak
	if (DepthSurface) DepthSurface->Release();
	if (RenderSurface) RenderSurface->Release();

	if (TheSettingManager->SettingsMain.Develop.DebugMode && !InterfaceManager->IsActive(Menu::MenuType::kMenuType_Console)) {
		if (Global->OnKeyDown(0x17)) { // TODO: setting for debug key ?
			char Filename[MAX_PATH];

			time_t CurrentTime = time(NULL);
			GetCurrentDirectoryA(MAX_PATH, Filename);
			strcat(Filename, "\\Test");
			if (GetFileAttributesA(Filename) == INVALID_FILE_ATTRIBUTES) CreateDirectoryA(Filename, NULL);
			D3DXSaveSurfaceToFileA(".\\Test\\shadowmapatlas.jpg", D3DXIFF_JPG, Shadows->ShadowAtlasSurface, NULL, NULL);
			D3DXSaveSurfaceToFileA(".\\Test\\shadowmaportho.jpg", D3DXIFF_JPG, Shadows->ShadowMapOrthoSurface, NULL, NULL);

			InterfaceManager->ShowMessage("Textures taken!");
		}
	}

	Device->EndScene();

	FrameCounter = (FrameCounter + 1) % 8;
	shadowMapsRenderTime = timer.LogTime("ShadowManager::RenderShadowMaps");
}


/*
 * Clear a part of the shadow map atlas based on the shadow mapping mode.
 * 
 * Note: Render target, view port should be set beforehand. Scene has to be already being rendered.
 */
void ShadowManager::ClearShadowCascade(D3DVIEWPORT9* ViewPort, D3DXVECTOR4* ClearColor) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* TargetShadowMap = Shadows->ShadowAtlasSurface;

	Device->SetDepthStencilSurface(NULL);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetPixelShader(ShadowMapClearPixel->ShaderHandle, false);

	Device->SetPixelShaderConstantF(0, (const float*) ClearColor, 1);

	// Draw a full-screen quad (inside the viewport)
	struct VERTEX { float x, y, z, rhw; };
	VERTEX vertices[] = {
		{ (float)ViewPort->X - 0.5f, (float)ViewPort->Y - 0.5f, 0.5f, 1.0f },
		{ (float)(ViewPort->X + ViewPort->Width) - 0.5f, (float)ViewPort->Y - 0.5f, 0.5f, 1.0f },
		{ (float)ViewPort->X - 0.5f, (float)(ViewPort->Y + ViewPort->Height) - 0.5f, 0.5f, 1.0f },
		{ (float)(ViewPort->X + ViewPort->Width) - 0.5f, (float)(ViewPort->Y + ViewPort->Height) - 0.5f, 0.5f, 1.0f }
	};

	RenderState->SetFVF(D3DFVF_XYZRHW, false);

	Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(VERTEX));

	Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
}


/*
* Filters the Shadow Map of given index using a 2 pass gaussian blur
*/
void ShadowManager::BlurShadowAtlas(unsigned cascadeMask) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	
	IDirect3DDevice9* Device = TheRenderManager->device;
    NiDX9RenderState* RenderState = TheRenderManager->renderState;

	// Ping-pong: horizontal pass atlas -> scratch, vertical pass scratch -> atlas.
	//
	// Neither pass may sample the atlas while ShadowAtlasSurface -- level 0 of that same
	// texture -- is the bound render target. Reading a bound render target is undefined in
	// D3D9 and a read/write feedback loop on Vulkan under DXVK. This Gaussian is the only
	// filtering the shadow maps get (the cascade lookup is a single tap), so anything that
	// compromises it shows up directly as hard, unfiltered texels along every shadow edge.
	if (!cascadeMask || !Shadows->ShadowAtlasBlurTexture || !Shadows->ShadowAtlasBlurSurface) return;

	DWORD oldScissorEnabled = FALSE;
	RECT oldScissor = {};
	if (FAILED(Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissorEnabled)) ||
		FAILED(Device->GetScissorRect(&oldScissor))) return;

    Device->SetDepthStencilSurface(NULL);
    RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
    RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
    RenderState->SetVertexShader(ShadowMapBlurVertex->ShaderHandle, false);
    RenderState->SetPixelShader(ShadowMapBlurPixel->ShaderHandle, false);
	RenderState->SetFVF(FrameFVF, false);
	Device->SetStreamSource(0, Shadows->ShadowAtlasVertexBuffer, 0, sizeof(FrameVS));

	// Pass map resolution to shader as a constant
	ShadowMapBlurPixel->SetShaderConstantF(0, &Shadows->Constants.ShadowBlur, 1);

	// blur in two passes, horizontally then vertically
	D3DXVECTOR4 Blur[2] = {
		D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f),
		D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f),
	};
	IDirect3DTexture9* Source[2] = { Shadows->ShadowAtlasTexture, Shadows->ShadowAtlasBlurTexture };
	IDirect3DSurface9* Target[2] = { Shadows->ShadowAtlasBlurSurface, Shadows->ShadowAtlasSurface };

	for (int i = 0; i < 2; i++) {
		// Unbind the previous pass's target before it becomes this pass's source, so the
		// two are never bound as texture and render target at the same time.
		RenderState->SetTexture(0, nullptr);
		Device->SetRenderTarget(0, Target[i]);
		RenderState->SetTexture(0, Source[i]);

		// set blur direction shader constants
		ShadowMapBlurPixel->SetShaderConstantF(1, &Blur[i], 1);

		RenderState->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE, RenderStateArgs);
		for (int cascade = MapNear; cascade < MapOrtho; ++cascade) {
			if (!(cascadeMask & (1u << cascade))) continue;
			const D3DVIEWPORT9& viewport = Shadows->ShadowMaps[cascade].ShadowMapViewPort;
			RECT region = {(LONG)viewport.X, (LONG)viewport.Y,
				(LONG)(viewport.X + viewport.Width), (LONG)(viewport.Y + viewport.Height)};
			Device->SetScissorRect(&region);
			Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
		}
	}

	RenderState->SetTexture(0, nullptr);
	Device->SetScissorRect(&oldScissor);
	RenderState->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissorEnabled, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
    RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
}

