#pragma once

class PointShadowsEffect : public EffectRecord
{
public:
	PointShadowsEffect() : EffectRecord("PointShadows") {};

	struct PointShadowsStruct {
	};
	PointShadowsStruct	Constants;

	void	UpdateConstants() {};
	void	RegisterConstants() {};
	void	UpdateSettings() {};

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	bool	ExtDraw(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, IDirect3DSurface9* SourceBuffer,
				ExtPass& pass, const char* spvName, NVR_EXT_BLEND blend, IDirect3DBaseTexture9* const* textures, UINT cubeCount, UINT textureCount,
				ID3D9NvrExtSampler* first, ID3D9NvrExtSampler* second, UINT mode, bool clear);
	ExtPass				extMerged, extBatch, extContact, extUpsample;
	std::map<std::string, ExtPass> extVariants;
	ExtUniformBuffer	extUniforms;
};
