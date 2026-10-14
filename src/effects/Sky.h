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
		D3DXVECTOR4		CloudVolumeData;
		D3DXVECTOR4		CloudVolumeData2;
		D3DXVECTOR4		CloudVolumeLight;
		// Order-2 spherical harmonic irradiance for the sky, for the ambient in the lighting
		// shaders. 9 coefficients, already convolved with the clamped-cosine kernel.
		D3DXVECTOR4		Irradiance[9];
		D3DXVECTOR4		PhysSky = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 1.0f);
		D3DXVECTOR4		PhysSkySun = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f);
	};
	struct PhysicalSettingsStruct { bool Enabled; float Brightness, WeatherTint, Haze; };
	PhysicalSettingsStruct PhysicalSettings = { false, 1.0f, 0.35f, 1.0f };
	SkyStruct Constants;
	bool useSunDiskColor;
	struct ProjectionInputs { D3DXVECTOR4 sunPos, skyColor, skyLowColor, horizonColor, skyData, sunDiskColor, sunsetColor, physical; float sunAmount, saturation, exterior; };
	ProjectionInputs lastProjection = {};
	bool lastProjectionValid = false;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
};