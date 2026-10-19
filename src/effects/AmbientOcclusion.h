#pragma once

class AmbientOcclusionEffect : public EffectRecord
{
public:
	AmbientOcclusionEffect() : EffectRecord("AmbientOcclusion") {};

	bool bNVAOLoaded = false;
	bool packedAOFailed = false;
	// Composite apply (set by ShaderManager around one Render call):
	// deferCombine -- run estimate and blurs only, leaving the result in ResultTexture() for the
	//                 fog pass to apply; deferredReady reports that it did.
	// combineOnly  -- run just the combine from ResultTexture() (fallback if the fog pass could not).
	bool deferCombine = false;
	bool deferredReady = false;
	bool combineOnly = false;
	IDirect3DTexture9* aoTexture[2] = {};
	IDirect3DSurface9* aoSurface[2] = {};
	// Quarter-resolution pair, used instead when [Main.Main.ReducedQuality] AOLowRes is on.
	IDirect3DTexture9* aoTextureLow[2] = {};
	IDirect3DSurface9* aoSurfaceLow[2] = {};
	// Which pair the last estimate wrote; combineOnly and the fog composite must read the same one.
	bool lastLowRes = false;
	CachedHandle techDedicated, paramLayout;	// UNOFFICIAL: looked up once per loaded effect
	// UNOFFICIAL 2026-10-17 Skylighting v2: the sky factor at the AO's resolution (G16R16F: factor, depth), written by the
	// SkyVisibilityV2 technique each frame v2 is on; skyReady says this frame's is there (the composites read it only then).
	IDirect3DTexture9* skyTexture = nullptr;
	IDirect3DSurface9* skySurface = nullptr;
	IDirect3DTexture9* skyTextureLow = nullptr;	// (with AOLowRes: the AO's quarter size)
	IDirect3DSurface9* skySurfaceLow = nullptr;
	bool skyReady = false;
	CachedHandle techSky;
	static bool SkyV2Active();
	IDirect3DTexture9* SkyTexture() const { return skyReady ? (lastLowRes ? skyTextureLow : skyTexture) : nullptr; }
	// UNOFFICIAL 2026-10-19 Skylighting v3 phase 1 ([Shaders.AmbientOcclusion.Skylighting] Reflections): the same texture (its b: the
	// sky visibility) for the sky reflections (Specular, Reflections), while that is on; and how much reflection stays fully hidden.
	bool	SkyReflections = false;
	float	SkyMinReflection = 0.1f;
	IDirect3DTexture9* SkyReflectionTexture() const { return SkyReflections && Enabled ? SkyTexture() : nullptr; }
	void RenderSkyVisibility(IDirect3DDevice9* Device);

	bool	UseLowRes() const;
	IDirect3DSurface9* NextResultSurface() const { return UseLowRes() ? aoSurfaceLow[0] : aoSurface[0]; }
	IDirect3DTexture9* ResultTexture() const { return lastLowRes ? aoTextureLow[0] : aoTexture[0]; }

	struct AmbientOcclusionStruct {
		bool			Enabled;
		D3DXVECTOR4		AOData;
		D3DXVECTOR4		Data;
	};
	AmbientOcclusionStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	void Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
