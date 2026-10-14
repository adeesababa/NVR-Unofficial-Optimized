#pragma once

// UNOFFICIAL 2026-10-14: ambient occlusion and bounce light in one pass (Effects\AmbientLight.fx.hlsl). Off by default
// ([Shaders.AmbientLight.Status] Enabled); while it renders, ShaderManager skips the AmbientOcclusion and BounceLight effects
// (it replaces both). Four half-resolution passes into the buffers below, then the apply in the frame chain.
class AmbientLightEffect : public EffectRecord
{
public:
	AmbientLightEffect() : EffectRecord("AmbientLight") {};

	struct ConstantsStruct {
		D3DXVECTOR4	Data;	// x: radius, y: occlusion strength, z: bounce strength, w: draw distance
		D3DXVECTOR4	Data2;	// x: darkest occlusion, y: brightness above which it fades, z: blur radius, w: debug view
	};
	ConstantsStruct	Constants = {};

	IDirect3DTexture9*	prepTexture = nullptr;		// depth, normal and colour per half-resolution pixel (A32B32G32R32F)
	IDirect3DSurface9*	prepSurface = nullptr;
	IDirect3DTexture9*	bufferTexture[2] = {};		// rgb bounce, a visibility (A16B16G16R16F), ping-pong for the blur
	IDirect3DSurface9*	bufferSurface[2] = {};
	bool				buffersTried = false;
	CachedHandle		paramLayout;

	void	EnsureBuffers();
	void	UpdateConstants() {};
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
