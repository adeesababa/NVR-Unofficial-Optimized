#pragma once

class NormalsEffect : public EffectRecord
{
public:
	NormalsEffect() : EffectRecord("Normals") {};

	struct NormalsStruct {
		D3DXVECTOR4		Data;
	};
	NormalsStruct	Constants;

	struct NormalTextures {
		IDirect3DTexture9* NormalsTexture;
		IDirect3DSurface9* NormalsSurface;
	};
	NormalTextures	Textures;

	void	UpdateConstants();
	void	UpdateSettings() {};
	void	RegisterConstants();
	void	RegisterTextures();

	// UNOFFICIAL 2026-10-23 (sharednormals; [Main.Main.Quality] SmoothNormals = reach in pixels, 0 off): the normals buffer smoothed
	// in place right after it is drawn (technique NormalsSmooth, Normals.fx.hlsl), so every effect reading TESR_NormalsBuffer gets
	// smooth normals on low-poly ground and rocks. True when drawn this frame; AmbientLight then skips its own NormalSmoothing.
	bool	RenderSmooth(IDirect3DDevice9* Device);	// ShaderManager::RenderEffectsPreTonemapping, after the normals pass
	bool	Smoothed() const { return smoothed; }
	D3DXVECTOR4			SmoothData = D3DXVECTOR4(0, 0, 0, 0);	// TESR_NormalsSmoothData: x reach
	IDirect3DTexture9*	SmoothTempTexture = nullptr;			// TESR_NormalsSmoothTemp (made on first use)
	IDirect3DSurface9*	SmoothTempSurface = nullptr;
private:
	CachedHandle		techSmooth;
	bool				smoothed = false;
	bool				smoothTried = false;
};
