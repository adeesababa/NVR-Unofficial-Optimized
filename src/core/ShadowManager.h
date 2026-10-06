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
	void					RenderAccums();
	void					RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj);
	void					AccumExteriorCell(TESObjectCELL* Cell, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap);
	void					RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex);
	void					RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex);
	void					RenderShadowMaps();
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
	int						PointLightsNum;
	float					shadowMapsRenderTime;
	bool					ShadowShadersLoaded;
	int						FrameCounter;
	bool					ForceAllCascades = true;
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
