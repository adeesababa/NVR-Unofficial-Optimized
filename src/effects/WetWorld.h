#pragma once

class WetWorldEffect : public EffectRecord
{
public:
	WetWorldEffect() : EffectRecord("WetWorld") {
		Constants.Data.x = 0.0f;
		Constants.Data.y = 0.0f;
		Constants.Data.z = 0.0f;
		Constants.PuddlesAnimator.Initialize(0);
	};

	struct WetWorldStruct {
		Animator		PuddlesAnimator;
		D3DXVECTOR4		Coeffs;
		D3DXVECTOR4		Data;
	};
	WetWorldStruct	Constants;

	float increaseRate;
	float decreaseRate;

	// UNOFFICIAL 2026-10-14 HudDrops (Render.cpp HudRain, Shaders/Includes/HudRain.hlsl): rain on the first-person weapon. The cover
	// test (technique 1) draws one pixel into coverSurface under an occlusion query; its count comes back a frame or two later
	// without stalling (covered: true when something is overhead).
	IDirect3DTexture9*	coverTexture = nullptr;
	IDirect3DSurface9*	coverSurface = nullptr;
	IDirect3DQuery9*	coverQuery = nullptr;
	bool				coverPending = false;
	bool				covered = false;
	void	RegisterTextures();
	void	RenderCoverTest(IDirect3DDevice9* Device);

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
};
