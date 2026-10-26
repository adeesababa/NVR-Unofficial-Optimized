#pragma once
#include "PhysSkyModel.h"

class SkyShaders : public ShaderCollection
{
public:
	SkyShaders() : ShaderCollection("Sky") {};

	struct SettingsStruct{
		float SkyMultiplierDay;
		float SkyMultiplierNight;
		// UNOFFICIAL 2026-10-25 [Shaders.Sky.Main] TintDay/TintNight (R, G, B; white = no change) and MoonBrightness (1 = no change)
		D3DXVECTOR4 TintDay = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f), TintNight = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f);
		float MoonBrightness = 1.0f;
		// UNOFFICIAL 2026-10-26 [Shaders.Sky.Main] TintHorizonDay/TintHorizonNight (white = no change), TintGradientHeight, SaturationDay/
		// SaturationNight (1 = no change), TintKeepsBrightness (the tints change the colour only)
		D3DXVECTOR4 TintHorizonDay = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f), TintHorizonNight = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f);
		float TintGradientHeight = 0.3f, SaturationDay = 1.0f, SaturationNight = 1.0f;
		bool TintKeepsBrightness = true;
	};
	SettingsStruct Settings;

	struct SkyStruct {
		D3DXVECTOR4		SkyData;
		D3DXVECTOR4		SunsetColor;
		D3DXVECTOR4		CloudData;
		// UNOFFICIAL volumetric clouds (SKYTEX.pso.hlsl): x Volumetric, y Steps, z CloudHeight, w CloudThickness
		D3DXVECTOR4		CloudVolumeData;
		// x Density, y Absorption, z DetailScale, w UVScale. z and w are per game unit; their settings are per 1000
		D3DXVECTOR4		CloudVolumeData2;
		// x Scattering (g), y SilverLining, z AmbientStrength, w DetailSpeed (cells per second)
		D3DXVECTOR4		CloudVolumeLight;
		// Order-2 spherical harmonic irradiance for the sky, for the ambient in the lighting
		// shaders. 9 coefficients, already convolved with the clamped-cosine kernel.
		D3DXVECTOR4		Irradiance[9];
		// UNOFFICIAL physical sky (SKY.pso.hlsl, Includes/Atmosphere.hlsl): x share, y brightness, z weather tint, w haze
		D3DXVECTOR4		PhysSky = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 1.0f);
		D3DXVECTOR4		PhysSkySun = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f);	// xyz: the sun's real direction
		// UNOFFICIAL 2026-10-18 ClassicLook (VolumetricFog.fx TESR_SkyIrradianceGame): the same projection of the game's own sky
		// alone, as the original NVR (master) had it: no physical sky blend and no SkylightingSaturation.
		D3DXVECTOR4		IrradianceGame[9];
		// UNOFFICIAL 2026-10-25 [Shaders.Sky.Physical] LightsObjects: the sky light the lighting shaders (objects, terrain, skin, hair, grass) and
		// SunFar add: Irradiance, or with the switch off the game's own sky (IrradianceGame, SkylightingSaturation applied) while the physical
		// sky still draws the sky, the fog and the clouds.
		D3DXVECTOR4		IrradianceObjects[9];
		// UNOFFICIAL 2026-10-25: rgb the sky's tint at this hour (TintDay to TintNight on the day/night curve; white indoors)
		D3DXVECTOR4		SkyTint = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f);
		// UNOFFICIAL 2026-10-25: x the moon disc's brightness (MoonBrightness, SKYTEX.pso.hlsl)
		D3DXVECTOR4		MoonData = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f);
		// UNOFFICIAL 2026-10-26: rgb the sky's horizon tint at this hour (white indoors); SkyLook x the sky's saturation at this hour,
		// y TintGradientHeight (Shaders/Includes/Sky.hlsl SkyLook)
		D3DXVECTOR4		SkyTintHorizon = D3DXVECTOR4(1.0f, 1.0f, 1.0f, 1.0f);
		D3DXVECTOR4		SkyLook = D3DXVECTOR4(1.0f, 0.3f, 0.0f, 0.0f);
	};
	// UNOFFICIAL [Shaders.Sky.Physical]
	struct PhysicalSettingsStruct { bool Enabled; float Brightness, WeatherTint, Haze; bool ExactSunlight, MultipleScattering; float GroundAlbedo; bool LightsObjects; };
	PhysicalSettingsStruct PhysicalSettings = { false, 1.0f, 0.35f, 1.0f, false, false, 0.3f, true };
	// UNOFFICIAL 2026-10-18 ExactSunlight / MultipleScattering: the CPU twin of the sky-view tables (PhysSkyModel.h), built when the settings need it
	PhysSky::Tables physTables;
	SkyStruct Constants;
	bool useSunDiskColor;
	// UNOFFICIAL: the spherical-harmonic projection in UpdateConstants (512 samples of the sky model, and UpdateConstants
	// runs twice a frame) is redone only when something it reads has changed: these are the last run's inputs. The sun's
	// direction counts as changed when it has turned by more than a tenth of a degree (it creeps every frame outdoors).
	struct ProjectionInputs { D3DXVECTOR4 sunPos, skyColor, skyLowColor, horizonColor, skyData, sunDiskColor, sunsetColor, physical; float sunAmount, saturation, exterior; D3DXVECTOR4 physicalMS, skyTint, skyTintHorizon, skyLook; };
	ProjectionInputs lastProjection = {};
	bool lastProjectionValid = false;

	void	UpdateConstants();
	void	UpdateMoonlight();	// UNOFFICIAL 2026-10-25 MoonBrightness: the game's night directional light (ShaderManager::UpdateConstants)
	void	RegisterConstants();
	void	UpdateSettings();
};