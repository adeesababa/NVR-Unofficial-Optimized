#pragma once

class BloomEffect : public EffectRecord
{
public:
	BloomEffect() : EffectRecord("Bloom") {};

	static const int MaxPasses = 8;

	struct BloomSettings {
		float Strength;
		int Passes;
		float PassBlending;
		float Spread;
		float GlowBoost;
	};
	struct BloomSettingsStruct {
		D3DXVECTOR4		Resolution[MaxPasses];
		BloomSettings	Main;
		BloomSettings	Night;
		BloomSettings	Interiors;
		float			GlowLightLevel;
		bool			LinearLight;
		bool			AntiFlicker;
	};
	BloomSettingsStruct Settings;

	struct BloomStruct {
		D3DXVECTOR4		Data;
		D3DXVECTOR4		ExtraData;
		D3DXVECTOR4		Resolution;
		D3DXVECTOR4		Glow;
		D3DXVECTOR4		GlowExtra;
	};
	BloomStruct		Constants;

	struct BloomTexturesStruct {
		IDirect3DTexture9* BloomTexture[MaxPasses];
		IDirect3DSurface9* BloomSurface[MaxPasses];
		IDirect3DVertexBuffer9* BloomVertexBuffer[MaxPasses];
	};
	BloomTexturesStruct		Textures;

	IDirect3DVertexBuffer9*	RectVertexBuffer[MaxPasses] = {};
	D3DXVECTOR3				RectVertexKey[MaxPasses] = {};
	IDirect3DVertexBuffer9*	LevelQuad(int i);

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool    SwitchEffect();

	void    RenderPass(IDirect3DDevice9* Device, UINT techniqueIndex, bool ClearRenderTarget);
	void	RenderBloomBuffer(IDirect3DSurface9* RenderTarget);

	bool	RenderBloomExt(IDirect3DDevice9* Device, int passes);
	ExtPass	extDownFirst, extDown, extUp, extUpLast;
	bool	extLevelsTried = false;
	struct ExtLevelsStruct {
		IDirect3DTexture9* Texture[MaxPasses - 1];
		IDirect3DSurface9* Surface[MaxPasses - 1];
	};
	ExtLevelsStruct	extLevels = {};
};