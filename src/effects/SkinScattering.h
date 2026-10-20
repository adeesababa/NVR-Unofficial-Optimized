#pragma once
#include <unordered_map>

class SkinScatteringEffect : public EffectRecord
{
public:
	SkinScatteringEffect() : EffectRecord("SkinScattering") {};

	static const int KernelSamples = 21;

	struct SkinScatteringStruct {
		D3DXVECTOR4		Data;
		D3DXVECTOR4		Kernel[KernelSamples];
		D3DXVECTOR4		Debug;
	};
	SkinScatteringStruct	Constants;

	struct ProfileStruct {
		float		Width;
		float		DepthFollow;
		float		AlbedoDetail;
		D3DXVECTOR3	Strength;
		D3DXVECTOR3	Falloff;
		bool		ScreenSpace;
	};
	ProfileStruct	Profile = {};
	ProfileStruct	ExteriorProfile = {};
	ProfileStruct	InteriorProfile = {};
	bool			ProfileIsInterior = false;
	bool			KernelDirty = true;
	bool			SkinShaderDebug = false;
	bool			ScreenSpace = true;

	IDirect3DTexture9*	Texture = nullptr;
	IDirect3DSurface9*	TextureSurface = nullptr;
	IDirect3DSurface9*	MSAASurface = nullptr;
	IDirect3DTexture9*	AlbedoTexture = nullptr;
	IDirect3DSurface9*	AlbedoTextureSurface = nullptr;
	IDirect3DSurface9*	AlbedoMSAASurface = nullptr;
	D3DSURFACE_DESC		TargetDesc = {};
	bool				Supported = false;
	int					Recreations = 0;

	bool		InWorldScene = false;
	bool		Bound = false;
	bool		HighlightsBound = false;
	bool		ClearedThisFrame = false;
	bool		SkinDrawn = false;
	DWORD		SavedWriteMask1 = 0xF;
	DWORD		SavedWriteMask2 = 0xF;

	void	BeginFrame();
	void	BindForSkinDraw(NiD3DPixelShaderEx* PixelShader);
	bool	BindForGameHighlights(NiD3DPixelShaderEx* PixelShader);
	void	Unbind();

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();

private:
	std::unordered_map<IDirect3DPixelShader9*, std::pair<UINT, bool>> WritesColor1;
	bool	EnsureTarget(const D3DSURFACE_DESC& SceneDesc);
	void	ReleaseTarget();
	void	ClearTarget(IDirect3DSurface9* SceneTarget);
	void	CalculateKernel();
};
