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
		D3DXVECTOR4		Sun;
		D3DXVECTOR4		Legacy;
		D3DXVECTOR4		LegacyLow;
		D3DXVECTOR4		LegacyHigh;
		D3DXVECTOR4		LegacySimple;
		D3DXVECTOR4		LegacyBlend;
		D3DXVECTOR4		LegacyHeight;
		D3DXVECTOR4		LegacyData;
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
	CachedHandle techCompositeFog, techDedicatedFog, techApplyShadowAO, techPackedFog, paramCompositeFlags, paramCompositeAOTexel, paramFogLayout;
	CachedHandle techCompositeFogLegacy, techDedicatedFogLegacy, techPackedFogLegacy;
	bool	IsLegacy() const { return Constants.Legacy.x > 0.5f; }
	int		classicLook = 0;
	bool	IsClassic() const { return classicLook > 0 && !IsLegacy(); }
	bool	referenceTest = false;
	bool	PlainOnly() const { return referenceTest || (IsClassic() && classicLook == 2); }
	CachedHandle techCompositeFogClassic, techDedicatedFogClassic, techCompositeFogInteriorClassic;
	D3DXHANDLE	CompositeTechnique() { return IsLegacy() ? TechniqueHandle(techCompositeFogLegacy, "CompositeFogLegacy") : IsClassic() ? TechniqueHandle(techCompositeFogClassic, "CompositeFogClassic") : TechniqueHandle(techCompositeFog, "CompositeFog"); }
	D3DXHANDLE	DedicatedTechnique() { return IsLegacy() ? TechniqueHandle(techDedicatedFogLegacy, "DedicatedFogLegacy") : IsClassic() ? TechniqueHandle(techDedicatedFogClassic, "DedicatedFogClassic") : TechniqueHandle(techDedicatedFog, "DedicatedFog"); }
	D3DXHANDLE	PackedTechnique() { return IsLegacy() ? TechniqueHandle(techPackedFogLegacy, "PackedFogLegacy") : IsClassic() ? NULL : TechniqueHandle(techPackedFog, "PackedFog"); }
	CachedHandle techCompositeFogInterior, techCompositeFogInteriorLegacy;
	D3DXHANDLE	CompositeInteriorTechnique() { return IsLegacy() ? TechniqueHandle(techCompositeFogInteriorLegacy, "CompositeFogInteriorLegacy") : IsClassic() ? TechniqueHandle(techCompositeFogInteriorClassic, "CompositeFogInteriorClassic") : TechniqueHandle(techCompositeFogInterior, "CompositeFogInterior"); }
	UINT	PlainTechniqueIndex(UINT techniqueIndex);
	IDirect3DTexture9* fogTexture[2] = {};
	IDirect3DSurface9* fogSurface[2] = {};

	bool	compositeShadow = false;
	bool	compositeAO = false;
	IDirect3DTexture9* compositeAOTexture = nullptr;
	bool	compositeInteriorShadow = false;
	float	compositeRound = 0.0f;
	IDirect3DTexture9* sceneOverride = nullptr;
	IDirect3DSurface9* targetOverride = nullptr;
	IDirect3DTexture9* compositeSkyTexture = nullptr;
	bool	compositeApplied = false;
	IDirect3DTexture9* gridTexture = nullptr;
	bool	gridApplied = false;
	CachedHandle paramGridFlags;
	bool	CanComposite(IDirect3DSurface9* aoSurface);
	bool	CanCompositeInterior() { return !PlainOnly() && CompositeInteriorTechnique() != NULL; }
	int		HalfStoreMode(IDirect3DDevice9* Device);
	int		halfStoreMode = -2;
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
