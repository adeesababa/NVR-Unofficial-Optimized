#pragma once

typedef struct frustum {
	enum PLANE {
		PLANENEAR = 0,
		PLANEFAR = 1,
		PLANELEFT = 2,
		PLANERIGHT = 3,
		PLANETOP = 4,
		PLANEBOTTOM = 5,
	};
	PLANE PLANE;
	D3DXPLANE plane[6];
} frustum;

class ShadowsExteriorEffect : public EffectRecord
{
public:
	ShadowsExteriorEffect() : EffectRecord("ShadowsExteriors") {};

	static const int Modes = 4;	// UNOFFICIAL: 3 = PCF (plain depth, filtered at lookup)
	static const int FormatBits = 2;

	D3DFORMAT Formats[Modes][FormatBits] = {
		{D3DFMT_G16R16, D3DFMT_G32R32F},
		{D3DFMT_G16R16F, D3DFMT_G32R32F},
		{D3DFMT_A16B16G16R16F, D3DFMT_A32B32G32R32F},
		{D3DFMT_R32F, D3DFMT_R32F},	// UNOFFICIAL PCF: depth needs the 32 bits whatever Format says
	};
	
	enum ShadowMapTypeEnum {
		MapNear = 0,
		MapMiddle = 1,
		MapFar = 2,
		MapLod = 3,
		MapOrtho = 4,
	};

	//Constants
	struct ShadowStruct {
		D3DXVECTOR4		SmoothedSunDir;
		D3DXVECTOR4		Data;
		D3DXVECTOR4		FormatData;
		D3DXVECTOR4		ScreenSpaceData;
		D3DXVECTOR4		OrthoData;
		D3DXVECTOR4		ShadowFade;
		D3DXMATRIXA16	ShadowWorld;
		D3DXMATRIX		ShadowViewProj;
		D3DXMATRIX		ShadowSpotlightCameraToLight[SpotLightsMax];
		D3DXVECTOR4		ShadowCubeMapLightPosition;
		D3DXVECTOR4		ShadowLightPosition[ShadowCubeMapsScreen];	// the screen-space passes' TESR_ShadowLightPosition[12]
		D3DXVECTOR4		ShadowMapRadius;
		D3DXVECTOR4		ShadowBlur;
		// Forward sun shadows, runtime side. x: 1 when the forward path is SUPPRESSED.
		//
		// The polarity is deliberate. A constant that fails to reach a shader reads as zero,
		// and zero here means "not suppressed" -- i.e. the current, working behaviour. The
		// opposite polarity would turn a dropped constant into silently missing shadows,
		// which is exactly the failure that made this a compile-time macro in the first place.
		D3DXVECTOR4		ForwardData;
		// UNOFFICIAL SunSmoothing CrossFade: the cascades from before the last sun step, kept in camera-relative
		// space like the live ones, and x = how much of them to show (1 at the step, 0 once the fade is over).
		D3DXMATRIX		OldCameraToLight[4];
		D3DXVECTOR4		OldCenter[4];
		D3DXVECTOR4		CrossFade;
		// UNOFFICIAL TESR_PointShadowNear (PointShadows.fx): x = [Shaders.ShadowsInteriors.Main] NearFade, world units; y = EdgeFade, fraction of a lamp's radius.
		D3DXVECTOR4		PointShadowNear;
	};

	// Settings
	struct FormsStruct {
		bool				AlphaEnabled;
		bool				Activators;
		bool				Actors;
		bool				Apparatus;
		bool				Books;
		bool				Containers;
		bool				Doors;
		bool				Furniture;
		bool				Misc;
		bool				Statics;
		bool				Terrain;
		bool				Trees;
		bool				Lod;
		float				MinRadius;
		float				OrigMinRadius;
	};

	struct ShadowMapSettings {
		D3DXMATRIX				ShadowCameraToLight;
		D3DVIEWPORT9			ShadowMapViewPort;
		frustum					ShadowMapFrustum;
		NiFrustumPlanes			ShadowMapFrustumPlanes;
		D3DXVECTOR4				ShadowMapCascadeCenterRadius;
		D3DXVECTOR3				CameraTranslation;  // Camera translation at the moment the shadow matrix was calculated.
		D3DXVECTOR4				ClearColor;
		FormsStruct				Forms;
		float					ShadowMapResolution;
		float					ShadowMapInverseResolution;
		float					ShadowMapRadius;
		float					ShadowMapNear;
		bool					CustomClearRequired;
	};

	struct ShadowMapStruct {
		int					Mode;
		int					FormatBits;
		D3DFORMAT			Format;
		int					CascadeResolution;
		bool                Prefilter;
		bool				MSAA;
		bool				Mipmaps;
		bool				LimitFrequency;
		bool				LodSkipLoaded;	// UNOFFICIAL (Fable): object LOD inside the loaded cells casts no shadow (the full models do)
		int					Anisotropy;
		float				Distance;
		float				CascadeLambda;
		bool				SmoothFilter;	// UNOFFICIAL 2026-10-15: PCF (Mode 3) with a 4 x 4 cubic B-spline instead of the 3 x 3
	};

	struct OrthoStruct {
		int					Resolution;
		float				Distance;
		bool				LimitFrequency;
	};

	struct ExteriorsStruct {
		bool				Enabled;
		bool				ForwardShadows;
		bool				UsePointShadowsDay;
		bool				UsePointShadowsNight;
		int					Quality;
		float				Darkness;
		float				NightMinDarkness;
	};

	struct InteriorsStruct {
		bool				Enabled;
		bool				TorchesCastShadows;
		FormsStruct			Forms;
		int					LightPoints;
		int					ExteriorLightPoints;	// UNOFFICIAL: outdoors, lamps with shadows (above 12: the screen-space pass in batches)
		int					Quality;
		int					ShadowCubeMapSize;
		int					DrawDistance;
		float				Darkness;
		float				LightRadiusMult;
		bool				UseCastShadowFlag;
		bool				PlayerShadowThirdPerson;
		bool				PlayerShadowFirstPerson;
		// UNOFFICIAL (2026-10-07 review): read on each settings change instead of by name every frame.
		bool				PlayerInsideLamp;	// [Shaders.ShadowsInteriors.Main] PlayerInsideLamp
		bool				RedrawActorsOnly;	// UNOFFICIAL (2026-10-08) [Shaders.ShadowsInteriors.Main] RedrawActorsOnly
		float				PlayerLampMargin;	// [Shaders.ShadowsInteriors.Main] PlayerLampMargin (at least 1)
		struct ForwardStruct {	// [Shaders.ShadowsInteriors.Forward]
			bool Enabled, KeepDarkening, DarkeningBlur, FirstPerson, ScopeFix, LogLamps;
			bool CameraIndependent;		// UNOFFICIAL (2026-10-08): lamps get shadows by where you are, not where you look
			bool KeepLampsInView;		// UNOFFICIAL (2026-10-08): the game's lamp view test may not drop lamps that reach the view
			int LampRanking, DebugView;
			float LampSwitchMargin, Strength, Bias, NormalOffset, Softness, FadeIn;	// LampSwitchMargin clamped to 0..0.9
			float FillLightRadius;		// UNOFFICIAL (2026-10-08): lamps reaching farther than this are fill lights (0 = none)
			float FillLightShadowStrength;	// UNOFFICIAL (2026-10-08): how dark fill lights' shadows are, 0..1 (0 = no shadow)
			int Quality;				// UNOFFICIAL (2026-10-10): PCF taps, 0 Low (1), 1 Medium (4, as before), 2 High (10), 3 Ultra (20)
			bool ContactHardening;		// UNOFFICIAL (2026-10-10): lamp shadows sharp at contact, Softness further out (not the sun's [Shaders.ContactHardening])
			float ContactHardeningStrength, ContactHardeningMinSoftness;	// at least 0; 0..1 share of the Softness spread
		} Forward;
	};

	struct ContactHardeningStruct {	// UNOFFICIAL [Shaders.ContactHardening.Status/.Main], read on change (ShadowManager uses it every frame)
		bool				Enabled;
		float				SunSize;		// at least 0
		float				MaxSoftness;	// 1..32
		bool				Smooth;			// UNOFFICIAL 2026-10-15: smooth penumbra edges (Includes/Shadow.hlsl, PCF Mode 3)
	};

	struct ScreenSpaceStruct {
		bool				Enabled;
		float				BlurRadius;
		float				RenderDistance;
		bool				Sharp = false;	// UNOFFICIAL: SunShadows.fx.hlsl's ForwardContactSharp (one sharp march, no big blurs)
	};

	struct SunSmoothingStruct {
		bool				SmoothSun;
		bool				QuantizeSun;
		float				SmoothingFactor;
		float				YawStepSize;
		float				PitchStepSize;
		float				MaxJumpAngle;
		float				GlideSeconds; // UNOFFICIAL: real seconds one quantized step takes to slide over (0 = old behaviour)
		bool				CrossFade;    // UNOFFICIAL: fade from the old shadows to the new instead of sliding (restart)
	};

	struct SettingsShadowStruct {
		ShadowMapStruct     ShadowMaps;
		OrthoStruct			OrthoMap;
		ScreenSpaceStruct	ScreenSpace;
		ExteriorsStruct		Exteriors;
		InteriorsStruct		Interiors;
		SunSmoothingStruct  SunSmoothing;
		ContactHardeningStruct ContactHardening;
	};
	SettingsShadowStruct	Settings;
	ShadowStruct			Constants;
	ShadowMapSettings		ShadowMaps[5];

	struct ShadowTextures {
		IDirect3DTexture9* ShadowPassTexture;
		IDirect3DSurface9* ShadowPassSurface;
		IDirect3DCubeTexture9* ShadowCubeMapTexture[ShadowCubeMapsMax];
		IDirect3DSurface9* ShadowCubeMapSurface[ShadowCubeMapsMax][6];
		// UNOFFICIAL RedrawActorsOnly: per slot, the lamp's cube map with only what never moves (PointShadowSchedule.h);
		// made on first use (EnsureStaticCubeMap), never registered as a shader texture (only copied from)
		IDirect3DCubeTexture9* ShadowCubeMapStaticTexture[ShadowCubeMapsMax];
		IDirect3DSurface9* ShadowCubeMapStaticSurface[ShadowCubeMapsMax][6];
		IDirect3DTexture9* ShadowSpotlightTexture[SpotLightsMax];
		IDirect3DSurface9* ShadowSpotlightSurface[SpotLightsMax];
		IDirect3DSurface9* ShadowCubeMapDepthSurface;
	};
	ShadowTextures	Textures;

	// Main shadow atlas, used for cascades.
	IDirect3DTexture9* ShadowAtlasTexture;
	IDirect3DSurface9* ShadowAtlasSurface;
	IDirect3DSurface9* ShadowAtlasSurfaceMSAA;
	IDirect3DSurface9* ShadowAtlasDepthSurface;

	// Ping-pong target for the separable prefilter blur, so that neither pass samples the atlas
	// while the atlas is bound as the render target -- undefined in D3D9, and a read/write
	// feedback loop on Vulkan under DXVK.
	// Initialised here because they are only allocated when Prefilter is on -- unlike the
	// members above, which RegisterTextures always assigns before anything tests them.
	IDirect3DTexture9* ShadowAtlasBlurTexture = nullptr;
	IDirect3DSurface9* ShadowAtlasBlurSurface = nullptr;

	IDirect3DVertexBuffer9* ShadowAtlasVertexBuffer;

	// Ortho shadows for various effects.
	IDirect3DTexture9* ShadowMapOrthoTexture;
	IDirect3DSurface9* ShadowMapOrthoSurface;
	IDirect3DSurface9* ShadowMapOrthoDepthSurface;

	void		clearShadowsBuffer();
	bool		ShouldRender() override;
	void		UpdateConstants();
	void		UpdateSettings();
	void		RegisterConstants();
	void		RegisterTextures();
	void		RecreateTextures(bool cascades, bool ortho, bool cubemaps);
	// UNOFFICIAL (2026-10-08): slots past ShadowCubeMapsScreen get their cube map on first use (forward shadows with
	// LightPoints above 12), at the size the others were made at (the shared depth surface's). false: no cube map.
	bool		EnsureCubeMap(int slot);
	bool		EnsureStaticCubeMap(int slot);	// UNOFFICIAL RedrawActorsOnly
	UINT		CubeMapSizeCreated = 0;

	D3DXVECTOR3	CalculateSmoothedSunDir();
	// UNOFFICIAL sun glide (SunSmoothing GlideSeconds): the quantized step being slid over.
	D3DXVECTOR3	SunGlideFrom = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
	D3DXVECTOR3	SunGlideTo = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
	ULONGLONG	SunGlideStart = 0;
	bool		SunGlideValid = false;
	// UNOFFICIAL SunSmoothing CrossFade. The game shaders get the fade compiled in only when the setting was on at
	// startup (SUN_CROSSFADE), so the switch takes effect after a restart; SunCrossFadeReady says this session's
	// shaders and the copy of the atlas exist. The copy holds the atlas from the frame before the step; the matrices
	// and centres are those it was drawn with, relative to the camera position each cascade was drawn from.
	bool		SunCrossFadeReady = false;
	bool		SunCrossFading = false; // the current step fades instead of gliding
	IDirect3DTexture9* ShadowAtlasOldTexture = nullptr;
	IDirect3DSurface9* ShadowAtlasOldSurface = nullptr;
	D3DXMATRIX	SunFadeMatrix[4];
	D3DXVECTOR4	SunFadeCenter[4];
	D3DXVECTOR3	SunFadeCamera[4];
	bool		StartSunCrossFade();
	void		UpdateSunCrossFade(float oldWeight);

	void		GetCascadeDepths();
	D3DXMATRIX	GetCascadeViewProj(ShadowMapSettings* ShadowMap, D3DXVECTOR3* SunDir);

private:
	bool		texturesInitialized;

	bool		UpdateSettingsFromQuality(int quality);
};
