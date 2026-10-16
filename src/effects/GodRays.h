#pragma once

class GodRaysEffect : public EffectRecord
{
public:
	GodRaysEffect() : EffectRecord("GodRays") {};

	struct GodRaysStruct {
		D3DXVECTOR4		Ray;
		D3DXVECTOR4		RayColor;
		D3DXVECTOR4		Data;
	};
	GodRaysStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();

	float dayMult;
	float nightMult;
	bool sunGlareEnabled;
	float rayVisibility;
	bool packedRenderFailed = false;
	bool dedicatedRenderFailed = false;
	CachedHandle techDedicated, paramLayout;	// UNOFFICIAL: looked up once per loaded effect
	// UNOFFICIAL 2026-10-16 SkipEmpty: the light mask's maximum taken down to 1 x 1 in 8 x 8 steps, per ray size ([0] half, [1] quarter).
	bool skipEmpty = true;
	CachedHandle techReduce, paramReduce, paramSkip;
	IDirect3DTexture9* maxTexture[2][4] = {};
	IDirect3DSurface9* maxSurface[2][4] = {};
	UINT maxWidth[2][4] = {}, maxHeight[2][4] = {};
	int maxSteps[2] = {};
	bool RunReduce(IDirect3DDevice9* Device, IDirect3DTexture9* source, UINT width, UINT height, int set);
	IDirect3DTexture9* raysTexture[2] = {};
	IDirect3DSurface9* raysSurface[2] = {};
	// Quarter-resolution pair, used instead when [Main.Main.ReducedQuality] GodRaysLowRes is on.
	IDirect3DTexture9* raysTextureLow[2] = {};
	IDirect3DSurface9* raysSurfaceLow[2] = {};

	bool RenderDedicated(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface);
	void Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
