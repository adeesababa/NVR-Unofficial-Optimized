#pragma once

class SandstormEffect : public EffectRecord
{
public:
	SandstormEffect() : EffectRecord("Sandstorm") {
		Constants.Data = D3DXVECTOR4(0, 0, 0, 0);
		Constants.Dust = D3DXVECTOR4(0, 0, 0, 0);
		Constants.Wind = D3DXVECTOR4(0, 0, 0, 0);
		Constants.Grains = D3DXVECTOR4(0, 0, 0, 0);
		StormAnimator.Initialize(0);
	};

	struct SandstormSettingsStruct {
		float	Density;
		float	HeightFalloff;
		float	DustColorR;
		float	DustColorG;
		float	DustColorB;
		float	StreakStrength;
		float	StreakSpeed;
		float	StreakScale;
		float	NearStreakDistance;
		float	SkyDarken;
		float	GustStrength;
		float	WindThreshold;
		float	FogDistanceThreshold;
		bool	ForceStorm;
	};
	SandstormSettingsStruct	Settings;

	struct SandstormStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Dust;
		D3DXVECTOR4	Wind;
		D3DXVECTOR4	Grains;
	};
	SandstormStruct	Constants;

	Animator	StormAnimator;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();

private:
	bool	IsStormNow();
	double	GustTime = 0.0;
	double	StreakTravel = 0.0;
};
