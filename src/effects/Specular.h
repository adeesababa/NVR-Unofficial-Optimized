#pragma once


struct SpecularUniforms {
	float viewTransform[16], projectionTransform[16];
	float shadowNear[16], shadowMiddle[16], shadowFar[16], shadowLod[16];
	float reciprocalResolution[4], specularData[4], specularEffects[4], sunDirection[4], sunColor[4], skyColor[4], horizonColor[4];
	float waterSettings[4], shadowFade[4], bounceData[4], bounceLayout[4], specularExtra[4];
	float depthConstants[4], cameraData[4], cameraPosition[4];
	float smoothedSunDir[4], shadowFormatData[4], shadowBlur[4];
	float shadowNearCenter[4], shadowMiddleCenter[4], shadowFarCenter[4], shadowLodCenter[4];
};

struct SpecularPush {
	UInt32 pointSampler;
	UInt32 linearSampler;
	UInt32 pad[2];
};

class SpecularEffect : public EffectRecord
{
public:
	SpecularEffect() : EffectRecord("Specular") {};

	struct SpecularStruct {
		D3DXVECTOR4		EffectStrength;
		D3DXVECTOR4		Data;
	};
	SpecularStruct	Constants;

	struct LookStruct {
		float SunStrength;
		float Roughness;
		float SkyReflections;
		float DistanceFade;
		float SpecularAA;
		float Reflectance;
	};
	struct SettingsSpecularStruct {
		LookStruct Exterior;
		LookStruct Rain;
	};
	SettingsSpecularStruct Settings;

	struct TexturesStruct {
		IDirect3DTexture9* NormalsTexture = nullptr;
		IDirect3DSurface9* NormalsSurface = nullptr;
		IDirect3DTexture9* LightTexture = nullptr;
		IDirect3DSurface9* LightSurface = nullptr;
	};
	TexturesStruct	Textures;
	CachedHandle techBounce, paramBounceData, paramBounceLayout;
	CachedHandle paramExtra;
	void	BindExtras(IDirect3DDevice9* Device, bool on);

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();

	bool	CanTakeBounce();
	bool	RenderWithBounce(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		IDirect3DTexture9* bounce, const D3DXVECTOR4& data, const D3DXVECTOR4& layout);

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	bool	ExtRenderBounce(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		IDirect3DBaseTexture9* bounce, const D3DXVECTOR4& data, const D3DXVECTOR4& layout);

private:
	ExtPass					extPrep, extLight, extAdd, extBounce;
	ExtUniformBuffer		extUniforms;
	IDirect3DBaseTexture9*	extraSkyVisibility = nullptr;
	IDirect3DBaseTexture9*	extraSunFar = nullptr;
	D3DXVECTOR4				extraValues = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	const void*				ConstantValue(const char* name);
	IDirect3DBaseTexture9*	TextureOf(const char* name);
	bool					FillUniforms(SpecularUniforms& uniforms);
};
