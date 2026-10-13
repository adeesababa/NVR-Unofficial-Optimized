#pragma once

class SkyShaders : public ShaderCollection
{
public:
	SkyShaders() : ShaderCollection("Sky") {};

	struct SettingsStruct{
		float SkyMultiplierDay;
		float SkyMultiplierNight;
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
	};
	SkyStruct Constants;
	bool useSunDiskColor;
	// UNOFFICIAL: the spherical-harmonic projection in UpdateConstants (512 samples of the sky model, and UpdateConstants
	// runs twice a frame) is redone only when something it reads has changed: these are the last run's inputs. The sun's
	// direction counts as changed when it has turned by more than a tenth of a degree (it creeps every frame outdoors).
	struct ProjectionInputs { D3DXVECTOR4 sunPos, skyColor, skyLowColor, horizonColor, skyData, sunDiskColor, sunsetColor; float sunAmount, saturation; };
	ProjectionInputs lastProjection = {};
	bool lastProjectionValid = false;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};