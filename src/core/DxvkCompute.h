#pragma once


#include "NvrDxvkExt.h"

class DxvkCompute {
public:
	static void					Initialize(IDirect3DDevice9* device);
	static bool					Available() { return Ext != nullptr && !Failed; }
	static ID3D9NvrExt*			Extension() { return Ext; }
	static const NVR_EXT_INFO&	Info() { return ExtInfo; }
	static const char*			Status();

	static bool					LoadSpirv(const char* Name, std::vector<char>& Words);
	static bool					HasSpirv(const char* Name);

	static ID3D9NvrExtPipeline*	CreatePipeline(const char* Name, const NVR_EXT_BINDING_DESC* Bindings, UINT Count, UINT PushSize, bool UsesSamplers);
	static ID3D9NvrExtPipeline*	CreateGraphicsPipeline(const char* Name, const NVR_EXT_BINDING_DESC* Bindings, UINT Count, UINT PushSize, bool UsesSamplers, UINT TargetCount = 1, NVR_EXT_BLEND Blend = NVR_EXT_BLEND_NONE, bool FailAll = true);
	static ID3D9NvrExtSampler*	LinearClamp();
	static ID3D9NvrExtSampler*	PointClamp();
	static ID3D9NvrExtSampler*	LinearWrap();
	static ID3D9NvrExtSampler*	PointWrap();
	static ID3D9NvrExtSampler*	MipSampler(bool Anisotropic, NVR_EXT_ADDRESS Address);
	static bool					PassesOn();
	static bool					VariantsOn();
	static bool					StencilOn();
	static bool					KeptOnD3D9(const char* Name);
	static ID3D9NvrExtBuffer*	CreateBuffer(UINT Size, UINT Flags, const char* Name);
	static ID3D9NvrExtBuffer*	CreateBufferNoFail(UINT Size, UINT Flags, const char* Name);
	static void					PreLoad(const NVR_EXT_RESOURCE_BINDING* Bindings, UINT Count);

	static void					ProfileTick(bool On);

	static bool					ReplacementsOn();
	static bool					RegisterReplacement(const char* Name, const void* Bytecode, UINT Size, const char* Template = nullptr, const D3DXMACRO* Defines = nullptr);
	static ID3D9NvrExtSampler*	CreateSampler(NVR_EXT_FILTER Filter, NVR_EXT_ADDRESS Address);

	static bool					DisplaceOn();
	static unsigned				RegisterDisplace(const char* Name, const char* TemplateName, const D3DXMACRO* Defines, bool Vertex, const void* Bytecode, UINT Size);
	static void					DisplaceTick();
	enum : unsigned { kDisplaceLand = 1, kDisplaceLandShadow = 2, kDisplaceObjects = 3 };
	static void					DisplaceBind(unsigned Family);
	static bool					DisplaceObjectsLayout();
	static bool					DisplaceDepthWanted();
	static void					DisplaceSceneDepth(bool Valid, float ScaleX, float ScaleY);
	static void					NoteLandPixelShader(IDirect3DPixelShader9* Shader, const D3DXMACRO* Defines);
	static int					LandLayersOf(IDirect3DPixelShader9* Shader);
	static bool					DisplaceShadowsWanted();
	static unsigned				DisplaceShadowLayers();
	static float				DisplaceReach();
	static bool					DisplaceBlendInShadows();
	static void					DisplaceShadowBegin(bool ObjectBlend, float ScaleX, float ScaleY);
	static void					DisplaceShadowEnd();
	static void					DisplaceShadowsDrawn(unsigned Pieces);
	enum : int { kDisplaceWorld = 0, kDisplaceReflection = 1, kDisplaceOther = 2 };
	static void					DisplaceLandPass(NiGeometry* Geometry, UInt16 PassEnum, int Context);
	static bool					DisplaceContactFade(float& Start, float& End);

	static void					Fail(const char* Reason);

	template<class T> static void Drop(T*& Object) { if (Object) { Object->Release(); Object = nullptr; } }

private:
	static ID3D9NvrExt*			Ext;
	static NVR_EXT_INFO			ExtInfo;
	static bool					Failed;
	static char					StatusText[160];
	static ID3D9NvrExtSampler*	SamplerLinear;
	static ID3D9NvrExtSampler*	SamplerPoint;
	static ID3D9NvrExtSampler*	SamplerWrap;
	static ID3D9NvrExtSampler*	SamplerPointWrap;
};
