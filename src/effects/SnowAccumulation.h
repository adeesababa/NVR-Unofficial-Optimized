#pragma once

class SnowAccumulationEffect : public EffectRecord
{
public:
	SnowAccumulationEffect() : EffectRecord("SnowAccumulation") {
		Constants.SnowAccumulationAnimator.Initialize(0);
		Constants.Data.w = 0.0f;
	};

	struct SnowAccumulationStruct {
		Animator		SnowAccumulationAnimator;
		D3DXVECTOR4		Data;
		D3DXVECTOR4		Color;
	};
	SnowAccumulationStruct	Constants;
	D3DXVECTOR4		Extra = { 0.0f, 0.0f, 0.0f, 0.0f };

	bool	CapturedNormals = false;
	bool	FollowJitter = true;
	int		Look = 0;

	float increaseRate;
	float decreaseRate;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
	UINT	Technique(UINT persistent);

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	ExtPass				extPass;
	ExtUniformBuffer	extUniforms;
};