#pragma once

// UNOFFICIAL, optional (off by default; Shaders.BounceLight): one bounce of screen-space indirect light
// (BounceLight.fx.hlsl). Runs in the pre-tonemap chain right after ambient occlusion. Gather and blur run at
// half (or, with QuarterResolution, quarter) resolution in the buffers below; only the combine is full resolution.
class BounceLightEffect : public EffectRecord
{
public:
	BounceLightEffect() : EffectRecord("BounceLight") {};

	struct BounceLightStruct {
		D3DXVECTOR4		Data;	// x: strength, y: radius, z: draw distance, w: blur radius
	};
	BounceLightStruct	Constants;
	bool				quarterResolution = false; // [Shaders.BounceLight.Main] QuarterResolution
	IDirect3DTexture9*	halfTexture[2] = {};
	IDirect3DSurface9*	halfSurface[2] = {};
	IDirect3DTexture9*	quarterTexture[2] = {};
	IDirect3DSurface9*	quarterSurface[2] = {};
	// Depth, normal and colour packed per low-resolution pixel for the gather (A32B32G32R32F).
	IDirect3DTexture9*	halfPrepTexture = nullptr;
	IDirect3DSurface9*	halfPrepSurface = nullptr;
	IDirect3DTexture9*	quarterPrepTexture = nullptr;
	IDirect3DSurface9*	quarterPrepSurface = nullptr;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	EnsureBuffers(bool quarter);
	bool	halfTried = false;
	bool	quarterTried = false;
	void	UpdateSettings();
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
