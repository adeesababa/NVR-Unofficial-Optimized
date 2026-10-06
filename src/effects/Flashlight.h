#pragma once

class FlashlightEffect : public EffectRecord
{
public:
	FlashlightEffect() : EffectRecord("Flashlight") {
		spotLightActive = false;
		SpotLight = nullptr;
		aimingHoldUntil = 0.0;
	};

	// UNOFFICIAL: the light's look, outdoors ([Shaders.Flashlight.Main]) and indoors ([Shaders.Flashlight.Interiors]);
	// ApplyLook copies the one for the current cell into Settings every frame.
	struct FlashlightLook {
		NiColor		Color;
		float		Dimmer;
		float		ConeAngle;
		float		Distance;
		float		NearFade;
		float		HotspotLimit;
		float		CookieStrength;
	};
	FlashlightLook	Looks[2]; // 0: outdoors, 1: interiors
	void			ApplyLook(bool exterior);

	struct FlashlightSettingsStruct {
		NiColor		Color;
		float		Dimmer;
		float		ConeAngle;
		float		Distance;
		bool		renderShadows;
		NiPoint3	Offset;
		bool		attachToWeapon;
		float		NearFade;
		float		HotspotLimit;
		float		CookieStrength;
		bool		softEdges;
		int			edgeFix;	// UNOFFICIAL: depth-aware edges, 0 off, 1 the lit surfaces, 2 also the beam's shaft ([Shaders.Flashlight.Main] EdgeFix)

		// Forward re-light of nearby static geometry, drawn by MaterialPass
		struct MaterialLightStruct {
			bool	Enabled;
			float	Intensity;
			float	Specular;
			float	NormalStrength;
			int		MaxGeometry;
			int		DebugMode;
		};
		MaterialLightStruct	MaterialLight;
	};
	FlashlightSettingsStruct	Settings;

	struct FlashlightStruct {
		D3DXMATRIX	FlashlightViewProj;
		D3DXVECTOR4	Position;
		D3DXVECTOR4	Direction;
		D3DXVECTOR4	Color;
		D3DXVECTOR4	Tuning;		// x near fade, y soft edges, z hotspot limit, w cookie strength
		D3DXVECTOR4	Composite;	// x source buffer is already linear; UNOFFICIAL (EdgeFix): y 1 = also the beam's shaft, zw 1 / beam buffer size
	};
	FlashlightStruct	Constants;

	NiSpotLight* SpotLight;
	bool	spotLightActive;
	double	aimingHoldUntil;	// real time the aiming latch holds until, see UpdateConstants
	int		selectedPass;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();

	void	GetFlashlightViewProj();
	void	PublishLightConstants(bool abActive);
	int		TechniqueIndex(const char* name);	// UNOFFICIAL: -1 when the loaded effect has no technique of that name
};