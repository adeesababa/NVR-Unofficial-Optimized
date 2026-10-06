#pragma once

// UNOFFICIAL, optional (off by default; Shaders.BounceLight): one bounce of screen-space indirect light
// (BounceLight.fx.hlsl). Runs in the pre-tonemap chain right after ambient occlusion. Gather and blur run at
// half (or, with QuarterResolution, quarter) resolution in the buffers below; only the combine is full resolution.
// UNOFFICIAL options (each off: as before): light guessed for samples that leave the screen, more bounces fed back from
// last frame's result, and last frame's gather reused (found again by reprojection: BounceLightMath.h).
class BounceLightEffect : public EffectRecord
{
public:
	BounceLightEffect() : EffectRecord("BounceLight") {};

	struct BounceLightStruct {
		D3DXVECTOR4		Data;	// x: strength, y: radius, z: draw distance, w: blur radius
	};
	BounceLightStruct	Constants;
	bool				quarterResolution = false; // [Shaders.BounceLight.Main] QuarterResolution
	// UNOFFICIAL, [Shaders.BounceLight.Main] OffScreenLight, MultiBounce, Temporal, HalfSamples (BounceLight.fx.hlsl's
	// techniques BounceLightPlus, BounceLightTemporal, BounceLightTemporalHalf). All off: the first technique, as before.
	float				offScreenLight = 0.0f;	// light guessed for samples off screen (0: none, as before)
	float				multiBounce = 0.0f;		// extra bounces from last frame's result (0: none, as before; 1: all of it)
	bool				temporal = false;		// reuse last frame's gather (the history buffers below)
	bool				halfSamples = false;	// with temporal: 4 samples a frame instead of 8
	// Temporal: last frame's gather and this frame's, per resolution, made the first time Temporal runs (EnsureHistory).
	IDirect3DTexture9*	halfHistory[2] = {};
	IDirect3DSurface9*	halfHistorySurface[2] = {};
	IDirect3DTexture9*	quarterHistory[2] = {};
	IDirect3DSurface9*	quarterHistorySurface[2] = {};
	bool				halfHistoryTried = false;
	bool				quarterHistoryTried = false;
	void	EnsureHistory(bool quarter);
	// What the last frame's low-resolution passes left for this one: their camera (to find last frame's bounce at the same
	// spot), and whether their final result (low buffer 0) and their gather (history buffer historyIndex) can be used.
	D3DXMATRIX			lastView, lastInvView, lastProj;
	bool				lastValid = false;
	bool				lastQuarter = false;
	bool				historyValid = false;
	int					historyIndex = 0;
	unsigned			frameNumber = 0;
	double				lastTime = 0.0;
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
	// With deferCombine, Render runs the low-resolution passes only and leaves their result for
	// SpecularEffect::RenderWithBounce (deferredReady, deferredTexture, deferredLayout); with combineOnly, the combine
	// alone, from that result (if the merged pass could not run). Set by ShaderManager.
	bool				deferCombine = false;
	bool				combineOnly = false;
	bool				deferredReady = false;
	IDirect3DTexture9*	deferredTexture = nullptr;
	D3DXVECTOR4			deferredLayout = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	void	UpdateSettings();
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
