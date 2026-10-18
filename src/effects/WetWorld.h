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
		D3DXVECTOR4		Look = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f);
	};
	WetWorldStruct	Constants;

	float increaseRate;
	float decreaseRate;

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

	bool	fastPaths = false;
	bool	fixedTrace = false;
	UINT	MainTechnique() const {
		if (fixedTrace && Effect && Effect->GetTechnique(fastPaths ? 4 : 3)) return fastPaths ? 4 : 3;
		return fastPaths && Effect && Effect->GetTechnique(2) ? 2 : 0;
	}
};
