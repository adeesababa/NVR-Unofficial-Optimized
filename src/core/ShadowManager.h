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
	bool					OrthoSkipBlendOnly = false;
	void					RenderAccums();
	void					RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj);
	void					BeginShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj, bool keepLandSet = false);
	void					WalkShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap);
	void					DrawShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, bool keepLandSet = false);
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
	enum class CubeLayer { All, StaticOnly, MovingOnto };
	bool					RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex, CubeLayer Layer = CubeLayer::All);
	void					RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex);
	void					RenderShadowMaps();
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
	ShaderRecordPixel*		ShadowMapOpaquePixel;
	ShaderRecordPixel*		ShadowGridColorPixel = nullptr;
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
	std::vector<NiNode*>		fallbackRefsScratch;
	struct CubeCaster { NiGeometry* geometry; RenderPass* pass; unsigned char faces; };
	std::vector<CubeCaster>		cubeCasters;
	int						PointLightsNum;
	float					shadowMapsRenderTime;
	bool					ShadowShadersLoaded;
	int						FrameCounter;
	bool					ForceAllCascades = true;
	bool					SunCascadesSkipped = false;
	bool					SunAtlasDrawn = false;
	std::vector<D3DXVECTOR4>	FrameMovers;
	D3DXVECTOR3				CascadeRefreshCamera[4] = {};
	bool					CascadeHadMover[4] = {};
	float					MoverShadowStretch = 0.0f;
	D3DXVECTOR4				ContactHardeningData = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	bool					ContactHardeningCompiled = false;
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
