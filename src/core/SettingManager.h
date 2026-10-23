#pragma once

#define TOML11_PRESERVE_COMMENTS_BY_DEFAULT
#include "../lib/toml11/toml.hpp"


struct SettingsMainStruct {

	struct MainStruct {
		bool	RemoveUnderwater;
		bool	ForceMSAA;
		bool	ForceReflections;
		bool	GameShadersInReflections; // [Main.Main.Water] diagnostic: the reflection pass uses the game's own shaders
		bool	ReflectionClipFix; // [Main.Main.Water] UNOFFICIAL: the water reflection's clip plane rebuilt by NVR at every draw of the reflection pass
		bool	RemovePrecipitations;
		bool	MemoryHeapManagement;
		bool	MemoryTextureManagement;
		bool	ReplaceIntro;
        bool    SkipFog;
        bool    RenderEffects;
        bool    RenderPreTonemapping;
        bool    DisableFrameChain; // ![Main.Main.Performance] FrameChain (copy-free effect chain)
        bool    DisableWorldSceneGuard; // ![Main.Main.Performance] WorldSceneGuard (skip NVR effects while the game renders no world scene)
        bool    MenuSceneGuard; // [Main.Main.Performance] MenuSceneGuard (UNOFFICIAL 2026-10-20: skip them at once over a menu's still background: the Pip-Boy)
        bool    DisableCompositeApply; // ![Main.Main.Performance] CompositeApply (shadow/AO apply folded into fog)
        bool    DisableChainGameTexture; // ![Main.Main.Performance] ChainUsesGameTexture
        bool    DisableMergedNormals; // ![Main.Main.Performance] MergedDepthNormals
        bool    SkipIdentityExposure; // [Main.Main.Performance] SkipIdentityExposure (no exposure pass while MaxBrightness is 0: it then leaves the picture as it is), live
        bool    AvgLumaCompute; // [Main.Main.Performance] AvgLumaCompute (UNOFFICIAL 2026-10-23: the average-luma pass as a compute reduction; needs NVR DXVK, otherwise the D3D9 pass runs as before), live
        int     AvgLumaComputeCheck; // [Main.Main.Performance] AvgLumaComputeCheck (test tool: every N frames both paths run and are logged side by side; 0 off), live
        bool    NvrDxvkPasses; // [Main.Main.Performance] NvrDxvkPasses (UNOFFICIAL 2026-10-23: the ported full-screen effects as Vulkan passes; needs NVR DXVK), live
        int     SmoothNormals; // [Main.Main.Quality] SmoothNormals (UNOFFICIAL 2026-10-23: reach in pixels of the shared normal smoothing after the normals pass; 0 off), live
        bool    LampMaskScissor; // [Main.Main.Performance] LampMaskScissor (the lamp-mask pass drawn only inside its lamps' screen rectangle, not at all with none on screen; exact), live
        bool    InteriorShadowFold; // [Main.Main.Performance] InteriorShadowFold (indoors the unblurred lamp darkening applied in the fog composite, not its own pass), live
        int     InteriorShadowFoldRound; // [Main.Main.Performance] InteriorShadowFoldRound (DIAGNOSTIC: 0 as this GPU stores 16-bit floats (HalfStoreMode), 1 none, 2 toward zero, 3 nearest even), live
        bool    CubeFaceViewCull; // [Main.Main.Performance] CubeFaceViewCull (lamp cube faces no on-screen pixel can look up are not drawn while the lamp is redrawn every frame anyway), live
        bool    CubeFaceCopySkip; // [Main.Main.Performance] CubeFaceCopySkip (RedrawActorsOnly: a lamp cube face nobody moves in is not copied again while it still holds its static copy), live
        bool    SlimDepthBuffer; // [Main.Main.Performance] SlimDepthBuffer (read once at startup)
        bool    SkipRedundantConstants; // [Main.Main.Performance] SkipRedundantConstants (ConstantFilter.h), live
        bool    ShaderWarmUp; // [Main.Main.Performance] ShaderWarmUp (ShaderWarmUp.h: the driver compiles NVR shaders at the main menu)
        bool    SkinnedShadowFaceTest; // [Main.Main.Performance] SkinnedShadowFaceTest (ShadowManager.cpp: skeleton bounds for animated casters in lamp cube maps), live
        bool    HiddenObjectCulling; // [Main.Main.Performance] HiddenObjectCulling (HiddenObjectCulling.h: skip the extra passes of objects hidden a few frames ago), live
        bool    PreDecodeCache; // [Main.Main.Performance] PreDecodeCache (LinearPreDecode.h: powf results kept, decoded copies sent per call; same bits), live
        bool    FastCasterHash; // [Main.Main.Performance] FastCasterHash (ShadowManager.cpp: lamp caster hash over 4-byte words, once per object per frame), live
        int     HiddenObjectCullingMaxAge; // [Main.Main.Performance] HiddenObjectCullingMaxAge (oldest visibility answer used, 1-7 frames), live
        bool    ReShadeDepthCompat; // [Main.Main.Performance] ReShadeDepthCompat (keep the game's depth-stencil bound through NVR's passes, for ReShade; also on while ReShade is loaded, ReShadeIntegration::KeepDepthBound), live
        bool    ReShadeNVRDepth; // [Main.Main.Performance] ReShadeNVRDepth (ReShadeIntegration.cpp: ReShade's DEPTH = NVR's combined depth, world + first-person model), live
        bool    ReShadeBeforeHUD; // [Main.Main.Performance] ReShadeBeforeHUD (ReShadeIntegration.cpp: ReShade's effects drawn after NVR's, before the HUD), live
        bool    ReShadeDepthNearRemap; // [Main.Main.Performance] ReShadeDepthNearRemap (ReShadeIntegration.cpp: first-person depths past the world near plane folded below 1; identity for g <= 0.5), live
        bool    ReShadeMenuGuard; // [Main.Main.Performance] ReShadeMenuGuard (ReShadeIntegration.cpp: the ReShadeMenuGuardEffects files paused while menus are open), live
        bool    ReShadeMenuGuardVATS; // [Main.Main.Performance] ReShadeMenuGuardVATS (the menu guard also in VATS), live
        bool    LampScissor; // [Main.Main.Performance] LampScissor (LampScissor.h: lamp-only passes drawn only inside their lamps' screen rectangle), live
        bool    LampScissorExact; // [Main.Main.Performance] LampScissorExact (LampScissor.h, 2026-10-21: the exact rectangle, also for lamps near the camera), live
        bool    NeonSpillSelfTest;    // [Shaders.BounceLight.Main] NeonSpillSelfTest (EffectSelfTest.h, DIAGNOSTIC: ticking it runs one in-game A/B test of the neon spill), live
        int     LampPassTestSkip; // [Main.Main.Performance] LampPassTestSkip (LampPassTest.h, DIAGNOSTIC: skip the passes a merged light pass would replace; 0 off), live
        bool    TrimLampPasses; // [Main.Main.Performance] TrimLampPasses (ObjectTemplate.hlsl: lamp passes drop pixels outside every lamp's reach), live
        bool    GodRaysLowRes; // [Main.Main.ReducedQuality] GodRaysLowRes (quarter instead of half resolution)
        bool    AOLowRes; // [Main.Main.ReducedQuality] AOLowRes (quarter instead of half resolution)
        bool    FXAA; // [Main.Main.ReducedQuality] FXAA (lean FXAA pass after SMAA)
        float   LampShadowMoveTolerance; // [Main.Main.ReducedQuality] LampShadowMoveTolerance (CHANGES THE IMAGE above 0: a lamp moving less than this many units from where its shadow cube was drawn keeps that cube and position), live
        int     PointShadowInterval; // [Main.Main.ReducedQuality] PointShadowInterval (redraw each point-light shadow cubemap every N frames, 1-4)
        bool    CheapReflections; // [Main.Main.ReducedQuality] CheapReflections (water reflection map drawn without sun shadows and terrain parallax)
        bool    CheapUnderwaterTerrain; // [Main.Main.ReducedQuality] CheapUnderwaterTerrain (ground below the water surface drawn without parallax)
        bool    ParallaxLite; // [Main.Main.ReducedQuality] ParallaxLite (8-step parallax with a secant step for terrain and parallax objects; terrain also two shadow taps). P60's TerrainParallaxLite.
        bool    StaggeredSunShadows; // [Main.Main.ReducedQuality] StaggeredSunShadows (middle sun-shadow cascade every 4th frame, far/Lod every 8th)
        bool    CachedDistantShadows; // [Main.Main.ReducedQuality] CachedDistantShadows (the stagger, but cascades with characters in them redraw every frame)
        int     NearCascadeInterval; // [Main.Main.ReducedQuality] NearCascadeInterval (redraw the near sun-shadow cascade every N frames, 1-2)
        bool    PresetManagerEnabled; // master on/off for automatic per-location preset resolution -- docs/preset-manager-design.md
		UInt8	AnisotropicFilter;
		UInt16	ScreenshotKey;
		bool	HDRScreenshot;
		float	FarPlaneDistance;
	};
	
	struct FrameRateStruct {
		bool	SmartControl;
		bool	SmartBackgroundProcess;
		UInt32	SmartControlFPS;
		UInt32  BackgroundThreadPriority;
		float	FlowControl;
	};

	struct CullingProcessStruct {
		bool EnableCulling;
		bool EnableReflectionCulling;
		float CullMinSize;
		float CullMinSizeReflection;
	};

	struct OcclusionCullingStruct {
		bool	Enabled;
		bool	OccludingStatic;
		bool	OccludedStatic;
		bool	OccludedDistantStatic;
		bool	OccludedDistantStaticIC;
		UInt32	OcclusionMapRatio;
		float	OccludingStaticMin;
		float	OccludedStaticMin;
		float	OccludedStaticMax;
		float	OccludedDistantStaticMax;
	};

	struct CameraModeStruct {
		bool		Enabled;
		bool		ChasingFirst;
		bool		ChasingThird;
		UInt8		Crosshair;
		NiPoint3	Offset;
		NiPoint3	AimingOffset;
		NiPoint3	DialogOffset;
		float		FoV;
	};
	
	struct IKFootStruct {
		bool		Enabled;
		float		Coeff;
		float		CoeffThigh;
		float		CoeffCalf;
		float		CoeffFoot;
		float		CoeffToe;
	};

	struct EquipmentModeStruct {
		bool		Enabled;
		bool		SleepingEquipment;
		bool		SwimmingEquipment;
		UInt16		TorchKey;
		UInt16		CombatEquipmentKey;
		float		DualBlockDelay;
		NiPoint3	ShieldOnBackPos;
		NiPoint3	ShieldOnBackRot;
		NiPoint3	WeaponOnBackPos;
		NiPoint3	WeaponOnBackRot;
		NiPoint3	TwoHandWeaponOnBackPos;
		NiPoint3	TwoHandWeaponOnBackRot;
		NiPoint3	BowOnBackPos;
		NiPoint3	BowOnBackRot;
		NiPoint3	StaffOnBackPos;
		NiPoint3	StaffOnBackRot;
		NiPoint3	TorchOnBeltPos;
		NiPoint3	TorchOnBeltRot;
	};

	struct GrassModeStruct {
		bool	Enabled;
	};

	struct MountedCombatStruct {
		bool		Enabled;
		NiPoint3	WeaponOnBackPos;
		NiPoint3	WeaponOnBackRot;
		NiPoint3	TwoHandWeaponOnBackPos;
		NiPoint3	TwoHandWeaponOnBackRot;
		NiPoint3	BowOnBackPos;
		NiPoint3	BowOnBackRot;
		NiPoint3	StaffOnBackPos;
		NiPoint3	StaffOnBackRot;
	};
	
	struct SleepingModeStruct {
		bool	Enabled;
		UInt8	Mode;
	};
	
	struct WeatherModeStruct {
		bool		Enabled;
		NiPoint3	CoeffNight;
		NiPoint3	CoeffFog;
		NiPoint3	CoeffSun;
	};
	
	struct ShadowModeStruct {
		float	NearQuality;
	};

	struct ShadersStruct {
		bool Blood;
		bool Grass;
		bool HDR;
		bool NightEye;
		bool POM;
		bool Skin;
		bool Terrain;
		bool Water;
		bool Extra;
	};


	struct MenuStruct {
		char	TextFont[40];
		char	TextFontStatus[40];
		UInt8	TextColorNormal[3];
		UInt8	TextShadowColorNormal[3];
		UInt8	TextColorSelected[3];
		UInt8	TextShadowColorSelected[3];
		UInt8	TextColorEditing[3];
		UInt8	TextShadowColorEditing[3];
		UInt8	TextColorEnabled[3];
		UInt8	TextShadowColorEnabled[3];
		UInt8	TextSize;
		UInt8	TextSizeStatus;
		UInt8	KeyEnable;
		UInt8	KeyUp;
		UInt8	KeyDown;
		UInt8	KeyLeft;
		UInt8	KeyRight;
		UInt8	KeyPageUp;
		UInt8	KeyPageDown;
		UInt8	KeyAdd;
		UInt8	KeySubtract;
		UInt8	KeySave;
		UInt8	KeyEditing;
		UInt8	MasterSwitchModifier;
		int		PositionX;
		int		PositionY;
		int		TitleColumnSize;
		int		MainItemColumnSize;
		int		ItemColumnSize;
		int		RowSpace;
		int		RowsPerPage;
		bool	UseNumpadForEditing;
	};

	struct PurgerStruct {
		bool	Enabled;
		bool	PurgeTextures;
		bool	PurgeCells;
		UInt16	Key;
		int		Time;
	};

	struct GravityStruct {
		bool	Enabled;
		float	Value;
	};

	struct DodgeStruct {
		bool	Enabled;
		bool	DoubleTap;
		UInt8	AcrobaticsLevel;
		float	DoubleTapTime;
	};
	
	struct FlyCamStruct {
		bool	Enabled;
		UInt8	KeyAdd;
		UInt8	KeySubtract;
		float	ScrollMultiplier;
		float	StepValue;
	};

	struct DevelopStruct {
		bool    DebugMode;       // enables hotkeys to print textures
		UInt8	TraceShaders;
	};

	struct TransitionsStruct {
		float TransitionCurvePower;
	};

	MainStruct					Main;
	CullingProcessStruct		CullingProcess;
	OcclusionCullingStruct		OcclusionCulling;
	CameraModeStruct			CameraMode;
	IKFootStruct				IKFoot;
	EquipmentModeStruct			EquipmentMode;
	FrameRateStruct				FrameRate;
	GrassModeStruct				GrassMode;
	MountedCombatStruct			MountedCombat;
	SleepingModeStruct			SleepingMode;
	WeatherModeStruct			WeatherMode;
	ShadowModeStruct			ShadowMode;
	ShadersStruct				Shaders;
	MenuStruct					Menu;
	PurgerStruct				Purger;
	GravityStruct				Gravity;
	DodgeStruct					Dodge;
	FlyCamStruct				FlyCam;
	DevelopStruct				Develop;
	TransitionsStruct			Transitions;
};

typedef std::vector<UInt32> FormsList;

struct SettingsWaterStruct {
	float choppiness;
	float waveWidth;
	float waveSpeed;
	float reflectivity;
	float causticsStrength;
	float causticsStrengthS;
	float shoreFactor;
	float shoreMovement;
	float turbidity;
	float inScattCoeff;
	float inExtCoeff_R;
	float inExtCoeff_G;
	float inExtCoeff_B;
	float depthDarkness;
};


struct SettingsColoringStruct {
	float Strength;
	float BaseGamma;
	float Fade;
	float Contrast;
	float Saturation;
	float Bleach;
	float BleachLuma;
	float ColorCurve;
	float ColorCurveR;
	float ColorCurveG;
	float ColorCurveB;
	float EffectGamma;
	float EffectGammaR;
	float EffectGammaG;
	float EffectGammaB;
	float Linearization;
};


struct SettingsWeatherStruct {
	char					LowerLayer[80];
	char					UpperLayer[80];
	UInt8					WindSpeed;
	UInt8					CloudSpeedLower;
	UInt8					CloudSpeedUpper;
	UInt8					TransDelta;
	UInt8					SunGlare;
	UInt8					SunDamage;
	float					FogNearDay;
	float					FogNearNight;
	float					FogFarDay;
	float					FogFarNight;
	TESWeather::ColorData	Colors[TESWeather::kNumColorTypes];
	float					HDR[14];
};

typedef std::map<std::string, SettingsWaterStruct> SettingsWaterMap;
typedef std::map<std::string, SettingsColoringStruct> SettingsColoringMap;
typedef std::map<std::string, SettingsWeatherStruct> SettingsWeatherMap;
typedef std::vector<std::string> StringList;
typedef toml::basic_value<toml::preserve_comments, std::map, std::vector> tomlValue;

class SettingManager : public SettingManagerBase {
public:
	class Configuration {
	public:
		enum NodeType {
			Boolean	 = 0,
			Integer  = 1,
			Float    = 2,
			String   = 3,
		};
		
		struct SectionPosition {
			char* Start;
			char* End;
			bool Found;
		};

		class ConfigNode {
		public:
			char		Section[40];
			char		Key[40];
			char		MidSection[80];
			char		Value[80];		// formatted text representation; still read directly by the UI layer (cut at 79 characters)
			std::string	LongValue;		// UNOFFICIAL 2026-10-17: a String setting's whole text (Value cuts it at 79: a longer one overflowed Value and crashed the game)
			std::string Description;
			bool		Reboot;
			UInt32		Type;
			bool		MaxQuality = false;	// UNOFFICIAL 2026-10-15: this value is Max Quality's, not the user's own (shown red in the menu)

			// Typed value, kept in sync with Value above. Populated for every node
			// regardless of Type (e.g. a Boolean node also gets IntValue/FloatValue)
			// so callers that read a setting through a different accessor
			// (GetSettingI on a bool, GetSettingF on an int, etc.) get the right
			// native value without re-parsing the text form.
			bool		BoolValue;
			int			IntValue;
			float		FloatValue;

			bool operator < (const ConfigNode& Node) const { return (strcoll(Key, Node.Key) < 0 ? true : false); }
		};

		typedef	std::vector<ConfigNode> SettingList;

		void			Init();
		bool			FillNode(ConfigNode* Node, const char* Section, const char* Key);
		void			FillSections(StringList* Sections, const char* ParentSection);
		void			FillSettings(SettingList* Nodes, const char* Section);
		void			SetValue(ConfigNode* Node);
		void			CreateWeatherSection(const char* WeatherName, TESWeather* Weather);
		tomlValue*		FindSection(tomlValue* table, StringList* keys);
		void			SetValueIn(tomlValue& Target, ConfigNode* Node);	// UNOFFICIAL: SetValue into the given table
		bool			IsMaxQualityKey(const char* Section, const char* Key);	// UNOFFICIAL: Max Quality sets this one (while it is on)
		static void		MergeTables(tomlValue& Into, const tomlValue& From);	// UNOFFICIAL: From's values over Into's, tables merged
		tomlValue*		UserSection(const char* Section);
		void			UpdateOldDefaults();

		tomlValue		TomlConfig;
		tomlValue		DefaultConfig;
		UInt32			DefaultsGeneration = 0;	// UNOFFICIAL 2026-10-18: counts the loads of the defaults file (Init); the menu's "New in this patch" list is rebuilt when it changes
		tomlValue		MaxQualityConfig;	// UNOFFICIAL 2026-10-15: Max Quality's settings (NewVegasReloaded.dll.maxquality.toml), read over the user's while it is on
		tomlValue		MaxQualityUser;		// UNOFFICIAL: the user's own changes to those (NewVegasReloaded.dll.maxquality.user.toml, also in MaxQualityConfig)
		bool			MaxQualityActive = false;
		bool			MaxQualityUserChanged = false;	// MaxQualityUser has changes the next Save writes
		bool			configLoaded;
		char*			Config;
		char*			ConfigB;
		UInt32			FileSize;
	};

	static void				Initialize();
	void					LoadSettings();
	void					SaveSettings();
	void					RevertSettings();
	void					SaveSettingsTo(const char* Path);
	int						GetSettingI(const char* Section, const char* Key);
	float					GetSettingF(const char* Section, const char* Key);
	char*					GetSettingS(const char* Section, const char* Key, char* Value);
	float					GetSettingTransition(const char* Section, const char* Key, bool isExterior, float isDaytime);
	void					SetSetting(const char* Section, const char* Key, float Value);
	void					SetSetting(const char* Section, const char* Key, int Value);
	void					SetSetting(const char* Section, const char* Key, bool Value);
	void					SetSettingS(const char* Section, const char* Key, const char* Value);
	void					SetSettingF(const char* Section, const char* Key, float Value);
	void					SetSetting(Configuration::ConfigNode* Node);
	void					Increment(const char* Section, const char* Key);
	void					Decrement(const char* Section, const char* Key);
	void					SetSettingWeather(const char* Section, const char* Key, float Value);
	void					FillMenuSections(StringList* Sections, const char* ParentSection);
	void					FillMenuSettings(Configuration::SettingList* Settings, const char* Section);
	void					GetFormList(const char* Name, FormsList* SettingsList);
	void					CreateNode(Configuration::ConfigNode* Node, const char* Section, const char* Key, float Value, bool Reboot);
	void					CreateNode(Configuration::ConfigNode* Node, const char* Section, const char* Key, int Value, bool Reboot);
	void					CreateNode(Configuration::ConfigNode* Node, const char* Section, const char* Key, bool Value, bool Reboot);
	void					CreateNodeS(Configuration::ConfigNode* Node, const char* Section, const char* Key, const char* Value, bool Reboot);
	enum class MenuSwitch { Status, MainEnabled, None };	// UNOFFICIAL: which setting switches a Shaders list entry (GetMenuShaderSwitch)
	MenuSwitch				GetMenuShaderSwitch(const char* Name);
	bool					GetMenuShaderEnabled(const char* Name);
	void					SetMenuShaderEnabled(const char* Name, bool enabled);
	bool					GetMenuMiscEnabled(const char* Name);
	void					SetMenuMiscEnabled(const char* Name, bool enabled);
	SettingsWaterStruct*	GetSettingsWater(const char* PlayerLocation);
	SettingsColoringStruct* GetSettingsColoring(const char* PlayerLocation);
	SettingsWeatherStruct*	GetSettingsWeather(const char* WeatherName);
	void					SetSettingsWeather(TESWeather* Weather);
	bool					IsShaderForced(const char* Name);

	template <typename T> static std::string	ToString(const T Value);
	template <typename T> static T				FromString(const char* Value);
	static void									SplitString(const char* String, const char* Delimiter, StringList* Values);
	template <typename T> static void			FillFromString(const char* String, const char* Delimiter, T* Values);

	Configuration					Config;
	UInt32							SettingsRevision = 0;	// UNOFFICIAL 2026-10-18: bumped by every SetSetting / LoadSettings; the menu's "New in this patch" list re-reads its values when it changes
	bool							GameLoading;
	bool							SettingsChanged;
	bool							hasUnsavedChanges;
	SettingsMainStruct				SettingsMain;
	SettingsWaterMap				SettingsWater;
	SettingsColoringMap				SettingsColoring;
	SettingsWeatherMap				SettingsWeather;

private:
	void					FilterMenuSections(StringList* Sections, const char* ParentSection);
};
