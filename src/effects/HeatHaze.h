#pragma once

class HeatHazeEffect : public EffectRecord
{
public:
	HeatHazeEffect() : EffectRecord("HeatHaze") { ZeroMemory(&Constants, sizeof(Constants)); };

	struct HeatHazeSettingsStruct {
		float	Strength;
		float	StartDistance;
		float	FullDistance;
		float	HorizonBand;
		float	NoiseScale;
		float	Speed;
		float	PeakHour;
		float	CloudyScale;
	};
	HeatHazeSettingsStruct	Settings;

	struct HeatHazeStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Noise;
	};
	HeatHazeStruct	Constants;

	float	Heat = 0.0f;
	float	WeatherHeat = 1.0f;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
};
