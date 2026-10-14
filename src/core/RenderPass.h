#pragma once

class RenderPass {
public:
	RenderPass() {};
	virtual ~RenderPass() {
		VertexShader = NULL;
		PixelShader = NULL;
	};

	ShaderRecordVertex* VertexShader;
	ShaderRecordPixel* PixelShader;

	// UNOFFICIAL 2026-10-13: draw every caster double-sided ([Shaders.ShadowsExteriors.Main] TwoSidedCasters, sun shadow maps
	// only; ShadowManager::RenderShadowMap sets it round its draws). Each object's own cull mode otherwise skips one-sided walls
	// seen from behind (the game uses them where the player never sees their other side), so the sun and the volumetric light
	// shone straight through them. Closed objects come out the same either way: the face nearest the sun wins the depth test.
	static inline bool TwoSided = false;
	// UNOFFICIAL 2026-10-14: while the light grid's sun map is drawn (ShadowManager, ShadowGridColor.pso), each caster's diffuse
	// texture is bound at s0 so the map records its colour; TESR_ShadowData.w says whether one was bound.
	static inline bool BindDiffuse = false;
	static void BindDiffuseOf(NiGeometry* Geo);

	std::stack<NiGeometry*>	GeometryList;
	virtual bool AccumObject(NiGeometry* Geo) { return true; };
	// UNOFFICIAL: the test behind AccumObject without the push, so a caller can sort geometry once and push it several
	// times (ShadowManager::RenderShadowCubeMap, one walk for six faces): 0 = not this pass, 1 = this pass draws it,
	// 2 = this pass claims it but has nothing to draw (a skinned geometry with no renderable partition).
	virtual int TestObject(NiGeometry* Geo) { return 1; };
	virtual void UpdateConstants(NiGeometry* Geo) {};
	virtual void RenderGeometry(NiGeometry* Geo);
	virtual void RegisterConstants() {};

	void DrawGeometryBuffer(NiGeometry* Geo, NiGeometryBufferData* GeoData);
	void DrawSkinnedGeometryBuffer(NiGeometry* Geo, NiGeometryBufferData* GeoData, NiSkinPartition::Partition* Partition);
	void RenderAccum();
};


class ShadowRenderPass : public RenderPass {
public:
	ShadowRenderPass();

	struct ConstantsStruct {
	};
	ConstantsStruct Constants;

	bool AccumObject(NiGeometry* Geo);
	int TestObject(NiGeometry* Geo);
	void RegisterConstants();
	void UpdateConstants(NiGeometry* Geo);
};

class AlphaShadowRenderPass : public RenderPass {
public:
	AlphaShadowRenderPass();

	struct ConstantsStruct {
		IDirect3DBaseTexture9* DiffuseMap;
	};
	ConstantsStruct Constants;

	bool AccumObject(NiGeometry* Geo);
	int TestObject(NiGeometry* Geo);
	void RegisterConstants();
	void UpdateConstants(NiGeometry* Geo);
};


class SkinnedGeoShadowRenderPass : public RenderPass {
public:
	SkinnedGeoShadowRenderPass();

	struct ConstantsStruct {
		D3DXVECTOR4* BoneMatrices;
	};
	ConstantsStruct Constants;

	bool AccumObject(NiGeometry* Geo);
	int TestObject(NiGeometry* Geo);
	void RegisterConstants();
	void UpdateConstants(NiGeometry* Geo);
	void RenderGeometry(NiGeometry* Geo);
};


class SpeedTreeShadowRenderPass : public RenderPass {
public:
	SpeedTreeShadowRenderPass();

	struct ConstantsStruct {
		D3DXVECTOR4* BillboardRight;
		D3DXVECTOR4* BillboardUp;
		D3DXVECTOR4* LeafBase;
		IDirect3DBaseTexture9* DiffuseMap;
	};
	ConstantsStruct Constants;

	bool AccumObject(NiGeometry* Geo);
	int TestObject(NiGeometry* Geo);
	void RegisterConstants(); 
	void UpdateConstants(NiGeometry* Geo);
};


class TerrainLODPass : public RenderPass {
public:
	TerrainLODPass();

	struct ConstantsStruct {
		D3DXMATRIX	WorldTranspose;
		D3DXVECTOR4 LODLandParams;
		D3DXVECTOR4 HighDetailRange;
	};
	ConstantsStruct Constants;

	bool AccumObject(NiGeometry* Geo);
	int TestObject(NiGeometry* Geo);
	void UpdateConstants(NiGeometry* Geo);
};