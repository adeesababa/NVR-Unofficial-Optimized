#pragma once
#include "../core/TemporalHistoryStats.h"
#include "../core/RenderRect.h"

class VolumetricLightEffect : public EffectRecord
{
public:
	VolumetricLightEffect() : EffectRecord("VolumetricLight") {};

	struct LookStruct {
		float	Strength;
		float	Darken;
		float	Density;
		float	MaxDistance;
		float	Anisotropy;
	};
	struct SettingsStruct {
		LookStruct	Day;
		LookStruct	Night;
		int		Steps;
		float	Temporal;
		float	BlurRadius;
		float	RainStrength;
		bool	Froxels;
		bool	FogVolume;
	};
	SettingsStruct	Settings;

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Data2;
		D3DXVECTOR4	Data3;
		D3DXVECTOR4	Layout;
		D3DXMATRIX	Reproject;
		D3DXVECTOR4	GridLayout;
		D3DXVECTOR4	GridData;
	};
	ConstantsStruct	Constants;

	struct TexturesStruct {
		IDirect3DTexture9* MarchTexture = nullptr;
		IDirect3DSurface9* MarchSurface = nullptr;
		IDirect3DTexture9* BlurTexture = nullptr;
		IDirect3DSurface9* BlurSurface = nullptr;
		IDirect3DTexture9* TempTexture = nullptr;
		IDirect3DSurface9* TempSurface = nullptr;
		IDirect3DTexture9* HistoryTexture = nullptr;
		IDirect3DSurface9* HistorySurface = nullptr;
		IDirect3DTexture9* GridTexture = nullptr;
		IDirect3DSurface9* GridSurface = nullptr;
		IDirect3DTexture9* GridTempTexture = nullptr;
		IDirect3DSurface9* GridTempSurface = nullptr;
	};
	TexturesStruct	Textures;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	void	RenderBuffers(IDirect3DDevice9* Device);
	UINT	CompositeTechnique() const { return Settings.Froxels && Textures.GridSurface && !RenderRect::Active() ? 8 : 0; }
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;

	ExtPass				extPasses[9];
	ExtUniformBuffer	extUniforms;

private:
	IDirect3DBaseTexture9*	fogNoise = nullptr;
	bool		fogNoiseLoaded = false;
	D3DXMATRIX	lastView, lastInvView, lastProj;
	UINT		lastRenderedFrame = 0;
	bool		haveHistory = false;
	bool		lastRect = false;
	TemporalHistoryStats	stats;
	void		ReportHistory(const char* when);
};
