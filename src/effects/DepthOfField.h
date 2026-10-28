#pragma once

class DepthOfFieldEffect : public EffectRecord
{
public:
	DepthOfFieldEffect() : EffectRecord("DepthOfField") {};

	struct ValuesStruct {
		float DistantBlur;
		float DistantBlurStartRange;
		float DistantBlurEndRange;
		float BaseBlurRadius;
		float focusDistance;
		float Radius;
		float DiameterRange;
		float NearBlurCutOff;
		int Mode;
		bool Enabled;
	};

	struct DepthOfFieldSettingsStruct {
		ValuesStruct	FirstPerson;
		ValuesStruct	ThirdPerson;
		ValuesStruct	VanityView;
	};
	DepthOfFieldSettingsStruct Settings;

	struct DepthOfFieldStruct {
		bool			Enabled;
		D3DXVECTOR4		Blur;
		D3DXVECTOR4		Data;
	};
	DepthOfFieldStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
	ExtPass	extFocus, extMosaic, extBokeh, extCombine;

	struct BokehStruct {
		bool	Bokeh;
		int		BokehShape;
		float	BokehThreshold;
		float	BokehIntensity;
		float	BokehSize;
		float	BokehRotation;
		int		BokehMaxSprites;
	};
	BokehStruct	Bokeh = {};
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	bool	BokehWanted() const { return Bokeh.Bokeh && !bokehFailed; }
	bool	BokehReady();
	bool	BokehScatter(IDirect3DDevice9* Device, IDirect3DBaseTexture9* depth);
	void	BokehRelease();
	ID3D9NvrExtPipeline*	bokehScatter = nullptr;
	ID3D9NvrExtPipeline*	bokehCombine = nullptr;
	ID3D9NvrExtPipeline*	bokehResolve = nullptr;
	IDirect3DTexture9*		bokehTexture = nullptr;
	IDirect3DSurface9*		bokehSurface = nullptr;
	ID3D9NvrExtBuffer*		bokehSprites = nullptr;
	ID3D9NvrExtBuffer*		bokehAccum = nullptr;
	UINT					bokehW = 0, bokehH = 0, bokehMax = 0;
	bool					bokehTried = false;
	bool					bokehFailed = false;
	UINT					bokehFrames = 0;
};