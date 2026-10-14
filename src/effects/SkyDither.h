#pragma once

class SkyDitherEffect : public EffectRecord
{
public:
	SkyDitherEffect() : EffectRecord("SkyDither") {};

	struct SettingsStruct {
		float	Strength;
		bool	SkyOnly;
	};
	SettingsStruct	Settings;

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
	};
	ConstantsStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender() { return Settings.Strength > 0.0f; };

private:
	UINT	frame = 0;
};
