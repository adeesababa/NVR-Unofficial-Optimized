#pragma once


class ShadowManager { // Never disposed
public:
	static void Initialize();
	
	enum ShadowMapTypeEnum {
		MapNear = 0,
		MapMiddle = 1,
		MapFar = 2,
		MapLod = 3,
		MapOrtho = 4,
	};


	NiNode*					GetRefNode(TESObjectREFR* Ref, ShadowsExteriorEffect::FormsStruct* Forms);
	void					AccumChildren(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLand, bool isLOD, NiFrustumPlanes* arPlanes = nullptr);
	bool					IsRefracting(TESObjectREFR* Ref);
	void					AccumObject(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLODLand);
	bool					OrthoSkipBlendOnly = false;	// UNOFFICIAL Skylighting v2 clean ortho map: leave out alpha-blended-only geometry (FX meshes, glass)
	void					RenderAccums();
	void					RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj);
	// UNOFFICIAL 2026-10-21 (sunshadow): RenderShadowMap in its three parts -- the state (matrices, render states, the land drop),
	// the walk (LOD roots and loaded cells into the passes' lists) and the draws (viewport, clear, the lists). keepLandSet: the sun
	// cascades' one walk filled RenderPass::LandSet for all of them, so the parts leave it alone.
	void					BeginShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj, bool keepLandSet = false);
	void					WalkShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap);
	void					DrawShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, bool keepLandSet = false);
	// UNOFFICIAL 2026-10-21 (sunshadow) SunCascadeOneWalk: one walk of the LOD roots and loaded cells for every sun cascade drawn in a
	// frame, each object tested against each cascade (bit c of a mask = cascade c), each cascade's casters kept in the order its own
	// walk would have found them, then handed to the passes cascade by cascade (PushSunCasters).
	enum SunPass : UInt8 { kSunOpaque, kSunLodLand, kSunAlpha, kSunSkinned, kSunTree };
	struct SunCaster { NiGeometry* geometry; UInt8 pass; };
	std::vector<SunCaster>	SunCasters[4];
	std::vector<std::pair<NiAVObject*, unsigned>> SunContainers;
	ShadowsExteriorEffect::FormsStruct* SunForms[4] = {};
	NiFrustumPlanes*		SunPlanes[4] = {};
	void					WalkSunCascades(unsigned mask);
	void					WalkSunCell(TESObjectCELL* Cell, unsigned mask);
	void					WalkSunChildren(NiAVObject* Object, unsigned mask, bool isLand, bool isLOD);
	void					WalkSunObject(NiAVObject* Object, unsigned mask, bool isLODLand);
	RenderPass*				SunPassOf(UInt8 pass);
	void					PushSunCasters(int cascade);
	void					AccumExteriorCell(TESObjectCELL* Cell, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap);
	// UNOFFICIAL RedrawActorsOnly (PointShadowSchedule.h): which casters RenderShadowCubeMap draws, into which cube map.
	// All: every caster into the live cube map (as before). StaticOnly: those that never move, into the slot's static cube
	// map. MovingOnto: the static cube map copied into the live one, then only the moving casters, min-blended on top.
	enum class CubeLayer { All, StaticOnly, MovingOnto };
	bool					RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex, CubeLayer Layer = CubeLayer::All);	// false: MovingOnto's copy failed, nothing drawn
	void					RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex);
	void					RenderShadowMaps();
	// UNOFFICIAL 2026-10-23 CharacterShadowMap (docs/next/CHARACTER-SHADOW-MAP-DESIGN.md): the player's own small sun shadow map, drawn
	// after the sun cascades from the same sun direction; actors only (the player, optionally other actors inside it). Sets
	// Constants.CharacterData.w = 1 when the map holds this frame's draw.
	void					RenderCharacterShadowMap(const D3DXVECTOR3& SunDir);
	void					ClearShadowCascade(D3DVIEWPORT9* ViewPort, D3DXVECTOR4* ClearColor);
	void                    BlurShadowAtlas(unsigned cascadeMask);

	ShadowRenderPass*				geometryPass;
	AlphaShadowRenderPass*			alphaPass;
	SkinnedGeoShadowRenderPass*		skinnedGeoPass;
	SpeedTreeShadowRenderPass*		speedTreePass;
	TerrainLODPass*					terrainLODPass;

	NiVector4				BillboardRight;
	NiVector4				BillboardUp;
	ShaderRecordVertex*		ShadowMapVertex;
	ShaderRecordPixel*		ShadowMapPixel;
	ShaderRecordVertex*		ShadowCubeMapVertex;
	ShaderRecordPixel*		ShadowCubeMapPixel;
	// UNOFFICIAL: ShadowMap.pso / ShadowCubeMap.pso without the alpha-test discard, for opaque casters (early-Z).
	ShaderRecordPixel*		ShadowMapOpaquePixel;
	ShaderRecordPixel*		ShadowGridColorPixel = nullptr;	// UNOFFICIAL 2026-10-14: light grid sun map, depth + colour (optional)
	ShaderRecordPixel*		ShadowCubeMapOpaquePixel;
	ShaderRecordVertex*		ShadowMapBlurVertex;
	ShaderRecordPixel*		ShadowMapBlurPixel;
	ShaderRecordPixel*		ShadowMapClearPixel;
	D3DVIEWPORT9			ShadowCubeMapViewPort;
	ShaderRecordVertex*		CurrentVertex;
	ShaderRecordPixel*		CurrentPixel;
	bool					AlphaEnabled;
	// Reused by AccumChildren. That runs per reference, per cell, per cascade, and a fresh
	// std::stack allocated a deque block on every call; clear() keeps the capacity.
	std::vector<NiAVObject*>	containersScratch;
	// UNOFFICIAL: the cube map form-based fallback's references in range of the light, collected once per light.
	std::vector<NiNode*>		fallbackRefsScratch;
	// UNOFFICIAL: RenderShadowCubeMap's casters, sorted once per light for all six faces (kept to avoid an allocation).
	struct CubeCaster { NiGeometry* geometry; RenderPass* pass; unsigned char faces; };
	std::vector<CubeCaster>		cubeCasters;
	int						PointLightsNum;
	float					shadowMapsRenderTime;
	bool					ShadowShadersLoaded;
	int						FrameCounter;
	bool					ForceAllCascades = true; // next frame refreshes every cascade (fresh or recreated atlas)
	bool					SunCascadesSkipped = false; // UNOFFICIAL 2026-10-16 SkipWhenFaded: the cascades were not drawn last frame
	bool					SunAtlasDrawn = false; // UNOFFICIAL 2026-10-16 SkipWhenFaded: the atlas holds a full draw since it was (re)made
	// ReducedQuality CachedDistantShadows: characters near the player this frame (bound centre, radius), and per sun
	// cascade where the camera was at its last redraw and whether a character was inside it then.
	std::vector<D3DXVECTOR4>	FrameMovers;
	D3DXVECTOR3				CascadeRefreshCamera[4] = {};
	bool					CascadeHadMover[4] = {};
	float					MoverShadowStretch = 0.0f;
	// UNOFFICIAL optional contact-hardening sun shadows (Shaders/Includes/Shadow.hlsl, SampleShadowAtlasContact; compiled
	// in only when on at startup). [Shaders.ContactHardening.Main/.Status]. x: penumbra width per world unit between
	// caster and receiver (0 = off), y: widest penumbra radius in atlas texels.
	D3DXVECTOR4				ContactHardeningData = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	bool					ContactHardeningCompiled = false;
	// F10 statistics for CachedDistantShadows, logged every 240 frames by LogSunShadowStats.
	struct {
		unsigned Frames, Scheduled[4], ForCharacters[4], Characters;
	}						SunStats = {};
	void					LogSunShadowStats(bool cachedDistant);
	void					CollectMovers(const D3DXVECTOR3& SunDir);
	bool					MoversInCascade(int cascade, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, float InnerDepth);

private:
	bool					CheckShaderFlags(NiGeometry* Geometry);
	void					RecalculateBillboardVectors(D3DXVECTOR3* SunDir);
};
