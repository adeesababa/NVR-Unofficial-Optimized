#pragma once

// UNOFFICIAL: the sun's highlight and the sky's reflection on every surface outdoors (Effects\Specular.fx.hlsl): GGX
// with Smith visibility and Schlick's Fresnel, the split-sum sky reflection, the sun's shadow from the cascades, worked
// out at a quarter of the resolution from wide-averaged normals and added to the image at full resolution.
// [Shaders.Specular.Exterior] and [Shaders.Specular.Rain], blended by how much it rains.
class SpecularEffect : public EffectRecord
{
public:
	SpecularEffect() : EffectRecord("Specular") {};

	struct SpecularStruct {
		D3DXVECTOR4		EffectStrength;	// x: specular anti-aliasing, y: reflectance at normal incidence (F0), z: buffer scale (4, 1 with Max Quality)
		D3DXVECTOR4		Data;			// x: sun highlight strength, y: roughness, z: sky reflection strength, w: fade-out distance
	};
	SpecularStruct	Constants;

	struct LookStruct {
		float SunStrength;		// the sun's highlight (1: as a real dielectric would)
		float Roughness;		// 0 mirror .. 1 matte
		float SkyReflections;	// the sky's reflection (1: as a real dielectric would)
		float DistanceFade;		// fades out by this distance (game units)
		float SpecularAA;		// how much the spread of the normals widens the highlight (0 .. 1)
		float Reflectance;		// F0, reflectance head on (0.04: most non-metals)
	};
	struct SettingsSpecularStruct {
		LookStruct Exterior;
		LookStruct Rain;
	};
	SettingsSpecularStruct Settings;

	// Quarter resolution: each 4 x 4 block's normal and depth (technique 1, SpecularPrep), and the light it reflects
	// (technique 2, SpecularLow); technique 0 adds the latter to the image.
	struct TexturesStruct {
		IDirect3DTexture9* NormalsTexture = nullptr;
		IDirect3DSurface9* NormalsSurface = nullptr;
		IDirect3DTexture9* LightTexture = nullptr;
		IDirect3DSurface9* LightSurface = nullptr;
	};
	TexturesStruct	Textures;
	CachedHandle techBounce, paramBounceData, paramBounceLayout;	// UNOFFICIAL: looked up once per loaded effect
	// UNOFFICIAL 2026-10-19: the sky visibility (Skylighting v3 phase 1) on stage 9 and the land's and clouds' shadow (SunFar) on stage
	// 10 for the light step (technique 2), with NVR_SpecularExtra; on = false unbinds them.
	CachedHandle paramExtra;
	void	BindExtras(IDirect3DDevice9* Device, bool on);

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();

	// The bounce light's full-resolution step and this effect's in one pass (technique SpecularBounce), for frames where
	// nothing renders between them. bounce: BounceLight's low-resolution result; data, layout: its constants. Returns
	// false, leaving the image untouched, if it could not.
	bool	CanTakeBounce();
	bool	RenderWithBounce(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		IDirect3DTexture9* bounce, const D3DXVECTOR4& data, const D3DXVECTOR4& layout);
};
