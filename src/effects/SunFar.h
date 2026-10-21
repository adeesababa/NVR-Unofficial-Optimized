#pragma once

// UNOFFICIAL 2026-10-19 SunFar (Effects\SunFar.fx.hlsl): what blocks the sun beyond the shadow maps' reach, as a factor per
// pixel (TESR_SunFarBuffer) that the sun-shadow composites multiply the picture by: the land's shadow (TerrainShadows.h,
// [Shaders.ShadowsExteriors.TerrainShadows]) and the game's cloud layers' shadow ([Shaders.CloudShadows.Main]). Both take
// only the sun's share of each pixel's light. After Community Shaders' "Terrain Shadows" and "Cloud Shadows" (GPL-3.0) and
// Conor's (mrfury97) NVR port of the first (GPL-3.0-or-later); credits in the shader file.
class SunFarEffect : public EffectRecord
{
public:
	SunFarEffect() : EffectRecord("SunFar") {};

	struct SettingsStruct {
		bool	Clouds;          // cloud shadows on
		float	CloudStrength;   // how much of the sun a fully clouded layer takes
		float	CloudDensity;    // the layers' cover (their textures' alpha) times this
		float	CloudHeight;     // the cloud layer's height above the camera (game units)
		float	CloudTile;       // world units one cloud texture covers
		float	CloudSpeed;      // drift speed scale
		int		DebugView;
	};
	SettingsStruct	Settings = {};

	struct ConstantsStruct {
		D3DXVECTOR4	Data;            // x: cloud strength (0 off), y: debug view, z: linear lighting gamma (1: off)
		D3DXVECTOR4	Clouds;          // x: height above the camera, y: 1 / tile, z: mip level, w: rotation (radians)
		D3DXVECTOR4	CloudOffset[2];  // per layer drift in uv
		D3DXVECTOR4	CloudWeight;     // per layer share (0: no texture)
		D3DXVECTOR4	CloudBlend;      // per layer blend toward the incoming texture
		D3DXVECTOR4	Apply;           // x: 1 while TESR_SunFarBuffer holds this frame's factor (the composites read it then)
		D3DXVECTOR4	Volume;          // UNOFFICIAL 2026-10-21 the volumetric clouds' shadow: x strength (0 off), y map width (m), z map texels, w cloud base (m)
	};
	ConstantsStruct	Constants = {};

	IDirect3DTexture9*	Texture = nullptr;   // TESR_SunFarBuffer, A8R8G8B8, screen size
	IDirect3DSurface9*	Surface = nullptr;

	void	UpdateSettings();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateConstants();
	bool	ShouldRender();
	// Once a frame outdoors, after the sun's contact shadows (ShaderManager): draws the factor, or marks it unused.
	void	RenderFactor(IDirect3DDevice9* Device);
	// For the composites that bind it themselves (VolumetricFog, AmbientLight): the factor's texture when it holds this frame's.
	IDirect3DTexture9*	FactorTexture() const { return Constants.Apply.x > 0.5f ? Texture : nullptr; }

private:
	IDirect3DBaseTexture9*	layerTextures[4] = {};
	IDirect3DBaseTexture9*	blendTextures[4] = {};
	double	drift[4][2] = {};       // uv drift per layer, accumulated
	double	lastDays = -1.0;        // game days at the last update (the drift follows the game's clock)
	bool	cloudsReady = false;    // the sky's layers were found this frame
};
