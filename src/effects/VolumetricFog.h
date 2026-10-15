#pragma once

class VolumetricFogEffect : public EffectRecord
{
public:
	VolumetricFogEffect() : EffectRecord("VolumetricFog") {
		Constants.WeatherFilterAnimator.Initialize(1);
		weatherFilterWasActive = true;
		rainyDisablesSkyFilter = true;
		cloudyDisablesSkyFilter = true;
		nightMinDarkness = 1.0f - 0.35f; // matches ShadowsExteriors.Main's own NightMinDarkness default (0.35), until the first exterior settings pass sets it for real
	};

	struct VolumetricFogStruct {
		D3DXVECTOR4		Density;         // x: BaseDensity, y: WeatherImpact, z: MorningFogDip, w: SunriseSunsetBoost
		D3DXVECTOR4		Shape;           // x: HeightFalloff, y: MaxHeight, z: Extinction, w: Inscattering
		D3DXVECTOR4		Wind;            // x: WindDirX, y: WindDirY, z: WindSpeed, w: NoiseScale
		D3DXVECTOR4		Scatter;         // x: PhaseAsymmetry, y: ShadowStrength, z: NoiseStrength, w: HeightInfluence
		D3DXVECTOR4		Weather;         // x: WeatherFilterBlend (animated 0-1, 1=sky mask active), y: isExterior, z: SkyAmbientAvailable, w: FogSaturation
		D3DXVECTOR4		Aerial;          // x: AerialStrength, y: AerialRangeStart, z: AerialTintBlend, w: AerialDayFadeStart
		D3DXVECTOR4		AerialTintColor; // xyz: manual aerial tint override
		D3DXVECTOR4		Distant;         // x: DistantFogRange, y: DistantFogBlend, z: DistantFogHeight, w: EdgeAA
		D3DXVECTOR4		Global;          // x: Amount, y: NightDisableSkyMask (Night-scoped, packed here for the spare slot), z: MoonVisibility, w: MinDensityFloor
		// Its own section/settings-UI tab (Shaders.VolumetricFog.Night), separate from Main, so
		// night-specific controls don't crowd the exterior tab as more of them get added.
		D3DXVECTOR4		Night;           // x: DensityScale, y: AmountScale, z: HeightFalloffScale, w: MaxHeightOffset
		D3DXVECTOR4		NightScatter;    // x: NoiseStrengthScale, y: WindSpeedScale, z: ExtinctionScale, w: InscatteringScale
		D3DXVECTOR4		NightTint;       // xyz: multiplicative night fog color tint, w: NightAmbientStrength
		D3DXVECTOR4		Sun;             // UNOFFICIAL 2026-10-15 x: SunGlow, the fog's glow round the sun (1 = as inherited, 0 interiors)
		// Custom build: the fog from before the rewrite, for old presets ([Shaders.VolumetricFog.Main] LegacyFog).
		D3DXVECTOR4		Legacy;          // x: 1 = legacy fog (C++ only: picks the *Legacy techniques)
		D3DXVECTOR4		LegacyLow;       // x: FogSaturation, y: WeatherImpact, z: DistantFogRange, w: SunPower
		D3DXVECTOR4		LegacyHigh;      // x: HeightFogDensity, y: HeightFogFalloff, z: HeightFogDist, w: HeightFogSkyColor
		D3DXVECTOR4		LegacySimple;    // x: Extinction, y: Inscattering, z: FogNight, w: SimpleFogSkyColor
		D3DXVECTOR4		LegacyBlend;     // x: DistantFogBlend, y: HeightFogBlend, z: HeightFogRolloff, w: SimpleFogBlend
		D3DXVECTOR4		LegacyHeight;    // x: DistantFogHeight, y: HeightFogHeight, z: SimpleFogHeight, w: isExterior
		D3DXVECTOR4		LegacyData;      // x: MinimumBaseFog, y: HeightFogSaturation, z: Amount, w: HeightFogInscattering
		Animator		WeatherFilterAnimator;
	};
	VolumetricFogStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	void	UpdateLegacySettings(const char* SettingCategory);
	bool	ShouldRender();

	// cached from UpdateSettings, consumed by UpdateConstants to drive WeatherFilterAnimator
	bool	rainyDisablesSkyFilter;
	bool	cloudyDisablesSkyFilter;
	bool	weatherFilterWasActive; // edge-detect for the animator, mirrors RainAnimator.switched

	// cached from UpdateSettings (Shaders.ShadowsExteriors.Main/NightMinDarkness), consumed by
	// UpdateConstants to drive the moon-phase night-ambient ceiling
	float	nightMinDarkness;
	bool	packedFogFailed = false;
	bool	dedicatedFogFailed = false;
	// UNOFFICIAL: technique and parameter handles, looked up once per loaded effect (EffectRecord::CachedHandle).
	CachedHandle techCompositeFog, techDedicatedFog, techApplyShadowAO, techPackedFog, paramCompositeFlags, paramCompositeAOTexel, paramFogLayout;
	// Custom build: the same techniques with the legacy fog, used while LegacyFog is on.
	CachedHandle techCompositeFogLegacy, techDedicatedFogLegacy, techPackedFogLegacy;
	bool	IsLegacy() const { return Constants.Legacy.x > 0.5f; }
	D3DXHANDLE	CompositeTechnique() { return IsLegacy() ? TechniqueHandle(techCompositeFogLegacy, "CompositeFogLegacy") : TechniqueHandle(techCompositeFog, "CompositeFog"); }
	D3DXHANDLE	DedicatedTechnique() { return IsLegacy() ? TechniqueHandle(techDedicatedFogLegacy, "DedicatedFogLegacy") : TechniqueHandle(techDedicatedFog, "DedicatedFog"); }
	D3DXHANDLE	PackedTechnique() { return IsLegacy() ? TechniqueHandle(techPackedFogLegacy, "PackedFogLegacy") : TechniqueHandle(techPackedFog, "PackedFog"); }
	UINT	PlainTechniqueIndex(UINT techniqueIndex);
	// [0]: fog multiply + depth, [1]: fog add -- written together as MRTs by the estimate pass
	IDirect3DTexture9* fogTexture[2] = {};
	IDirect3DSurface9* fogSurface[2] = {};

	// Composite apply (set by ShaderManager for one Render call): the dedicated reconstruct also
	// applies the exterior sun-shadow composite and/or the deferred AO combine, saving their own
	// full-resolution passes. compositeApplied reports whether that happened.
	bool	compositeShadow = false;
	bool	compositeAO = false;
	IDirect3DTexture9* compositeAOTexture = nullptr;
	bool	compositeApplied = false;
	// UNOFFICIAL 2026-10-14 froxel grid (set by ShaderManager for the frame): the volumetric light's grid, read by the dedicated
	// path on stage 11 (NVR_FogGrid); gridApplied reports that the volumetric light's composite was done in the fog's pass.
	IDirect3DTexture9* gridTexture = nullptr;
	bool	gridApplied = false;
	CachedHandle paramGridFlags;
	bool	CanComposite(IDirect3DSurface9* aoSurface);
	// The shadow and AO applies in one pass of their own (technique ApplyShadowAO), for frames where effects between them
	// and the fog keep them out of the fog pass. Returns false, leaving the scene untouched, if it could not.
	bool	CanApplyShadowAO();
	bool	RenderShadowAO(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		bool shadow, IDirect3DTexture9* aoTexture);

	void	RegisterTextures();
	bool	RenderDedicated(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface);
	void Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
