#pragma once
#include <unordered_set>

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
	// UNOFFICIAL 2026-10-17 (TB-1, terrain blending shadow band): while Terrain Blending is on, the land is drawn into the sun
	// shadow maps LandDrop game units lower than it is (ShadowManager::RenderShadowMap sets it to BlendDistance * ShadowDrop;
	// 0 = off, nothing changes). The land fades away over the part of an object it covers by less than BlendDistance; with the
	// land at its true height that part sits in the land's own shadow, so the fade revealed a darkened strip. A plane lowered
	// by d moves its sun-map depth by d / sin(sun elevation), exactly what a point buried by d is behind the surface. The
	// shader is untouched: the offset is in the caster's world matrix (UpdateShadowWorld).
	static inline float LandDrop = 0.0f;
	// UNOFFICIAL 2026-10-21 (sunshadow) SunCascadeLeanDraws: set by ShadowManager around a sun cascade's draws. Each caster after a
	// pass's first uploads only the shader constants whose values changed (SetCTLean), and the cloth / plant wind calls are left
	// out for a caster they would leave as it is (WindLeavesStill). The same draws with the same values: see RenderPass.cpp.
	static inline bool Lean = false;
	static inline std::unordered_set<NiGeometry*> LandSet;	// this map's land geometry (filled while its cells' land is accumulated)
	static void UpdateShadowWorld(NiGeometry* Geo);
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