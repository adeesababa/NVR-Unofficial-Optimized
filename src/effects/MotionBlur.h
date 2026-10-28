#pragma once

class MotionBlurEffect : public EffectRecord
{
public:
	MotionBlurEffect() : EffectRecord("MotionBlur") { ZeroMemory(&Constants, sizeof(Constants)); };

	struct ValuesStruct {
		float BlurCutOff;
		float GaussianWeight;
		float BlurScale;
		float BlurOffsetMax;
	};

	struct MotionBlurSettings {
		ValuesStruct ThirdPerson;
		ValuesStruct FirstPerson;
	};
	MotionBlurSettings Settings;

	struct MotionBlurStruct {
		D3DXVECTOR4		BlurParams;
		D3DXVECTOR4		Data;
		float			oldAngleX;
		float			oldAngleZ;
		float			oldAmountX;
		float			oldAmountY;
		float			oldoldAmountX;
		float			oldoldAmountY;
	};
	MotionBlurStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
	bool	ShouldRender();
};