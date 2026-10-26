#pragma once
#include <vector>
#include "NvrDxvkExt.h"

class EffectRecord : public ShaderProgram {
public:
	EffectRecord(const char* effectName);
	virtual ~EffectRecord();

	virtual void			SetCT();
	virtual void			CreateCT(ID3DXBuffer* ShaderSource, ID3DXConstantTable* ConstantTable);
	virtual void			UpdateConstants() {};
	virtual void			UpdateSettings() {};
	virtual void			RegisterConstants() {};
	virtual void			RegisterTextures() {};
	virtual bool			ShouldRender() { return true; }; // reimplement in subclasses to disable render under certain conditions
	virtual void			BeforeDraw(IDirect3DDevice9* Device) {};	// UNOFFICIAL 2026-10-22: right before each pass's draw (legacy path; SunFar's HazardLog)
	virtual bool			SwitchEffect();
	virtual void			Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer);
	// UNOFFICIAL 2026-10-23 (NVR DXVK, core\DxvkCompute.h): an effect ported to the extension's full-screen passes overrides
	// ExtRender; Render calls it first and returns when it drew (true). It must draw the same picture as the D3D9 technique
	// (its GLSL in src\hlsl\NewVegas\Compute, kept beside the .fx.hlsl). ExtFullScreen does one pass through the frame
	// chain exactly as Render does (the chain's spare texture, the direct final write, or the legacy copy): the pipeline's
	// inputs are bound by the caller; TESR_RenderedBuffer is TheTextureManager->RenderedTexture at that moment.
	// ExtFailed: a failed pass turns the effect's ported path off for the session (logged once); Render then uses D3D9.
	virtual bool			ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) { return false; }
	// last: false for a pass that is not the effect's last (a multi-pass port): it never writes straight into the game target.
	bool					ExtFullScreen(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, IDirect3DSurface9* SourceBuffer,
								ID3D9NvrExtPipeline* Pipeline, const NVR_EXT_RESOURCE_BINDING* Bindings, UINT BindingCount, const void* Push, UINT PushSize, bool last = true);
	bool					ExtFailed = false;
	UInt32					ExtFrames = 0;
	// The common case: a pass whose fragment shader reads N textures (set 1, binding 0..N-1, slots 0..N-1) through one sampler
	// (slot 0, its heap index at SamplerPushOffset of the push block; WrapSamplerPushOffset > 0: a LINEAR/WRAP sampler at
	// slot 1 for file textures) and a push block of constants. The pipeline is made on first use from
	// Shaders\NewVegasReloaded\Compute\<SpvName>.spv and kept in Pass.
	struct ExtPass { ID3D9NvrExtPipeline* Pipeline = nullptr; bool Tried = false; };
	bool					ExtTexturePass(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, IDirect3DSurface9* SourceBuffer,
								ExtPass& Pass, const char* SpvName, IDirect3DBaseTexture9* const* Textures, UINT TextureCount,
								const void* Push, UINT PushSize, UINT SamplerPushOffset, bool PointSampler = false, UINT WrapSamplerPushOffset = 0, bool PointWrapSampler = false, bool Last = true);
	// UNOFFICIAL 2026-10-25: a pass into the effect's OWN targets (1-4 surfaces of one size, e.g. its half-resolution buffers),
	// optional viewport rectangle; the frame chain is not touched. A multi-pass port chains these and ends with ExtFullScreen
	// (or ExtTexturePass) for the pass that writes the frame. A failure sets ExtFailed (logged), as ExtFullScreen does.
	bool					ExtDrawTo(ID3D9NvrExtPipeline* Pipeline, IDirect3DSurface9* const* Targets, UINT TargetCount, const NVR_EXT_RESOURCE_BINDING* Bindings,
								UINT BindingCount, const void* Push, UINT PushSize, const RECT* Viewport = nullptr);
	// UNOFFICIAL 2026-10-25: constants over the 240 push bytes (big kernels, matrices): Data uploaded into a uniform buffer the
	// effect keeps (made on first use, remade when Size changes; recorded in order with the draws, no waits). Bind the returned
	// buffer as NVR_EXT_BIND_UNIFORM_BUFFER (GLSL: layout(set = 2, binding = 0, std140) uniform ...). Null: no extension or failed.
	struct ExtUniformBuffer { ID3D9NvrExtBuffer* Buffer = nullptr; UINT Size = 0; bool Tried = false; };
	ID3D9NvrExtBuffer*		ExtUniforms(ExtUniformBuffer& Uniforms, const void* Data, UINT Size);
	// The loaded file texture behind one of the effect's samplers (e.g. "TESR_BloodLensSampler"), null if none.
	IDirect3DBaseTexture9*	ExtFileTexture(const char* SamplerName);
	// ClearSampler is inherited from ShaderProgram -- game shaders need the same thing.
	void					DisposeEffect();
	bool					LoadEffect();

	bool 					IsLoaded();
	bool					Enabled;
	bool					usesSourceBuffer = true; // false when the effect declares no TESR_SourceBuffer sampler
	// True when a pass can leave destination pixels unwritten or depends on their contents
	// (clip/discard, blending, stencil, colour write masks). Under the frame chain such passes
	// get their destination pre-filled with the current image, matching the old in-place result.
	bool					needsPrefill = true;
	// Per technique and pass, filled when only clip/discard set needsPrefill: whether that pass's
	// compiled pixel shader can kill pixels (texkill). A pass that cannot writes every pixel, so a
	// pre-filled destination would be overwritten entirely (the match was in code no pass of it
	// uses, such as a shared include's helpers). Empty: every pass follows needsPrefill.
	std::vector<std::vector<bool>> passPrefill;
	bool					PassNeedsPrefill(UINT techniqueIndex, UINT pass) const;
	// Set by an effect around one Render call whose passes will write every pixel of the target (no scissor, discard
	// or blending that frame): the frame chain then skips the pre-fill copy (VolumetricSmoke over most of the screen).
	bool					writesWholeTarget = false;
	void					RebindSlotTextures();
	float					renderTime;
	float					constantUpdateTime;

	// UNOFFICIAL: D3DX finds a technique or parameter by name with a string scan on every call, and the chain did about
	// 25 of those a frame. A handle is fixed for the life of the ID3DXEffect, so each is looked up once per loaded effect
	// (LoadGeneration changes whenever the effect is loaded or disposed) and kept in a CachedHandle the caller owns.
	struct CachedHandle { unsigned Generation = 0; D3DXHANDLE Handle = NULL; };
	unsigned				LoadGeneration = 1;
	D3DXHANDLE				TechniqueHandle(CachedHandle& cache, const char* name);
	D3DXHANDLE				ParameterHandle(CachedHandle& cache, const char* name);
	// UNOFFICIAL 2026-10-19: textures a caller wants on stages for its non-TESR (NVR_) samplers during its next Render calls. Render
	// sets them again right after every BeginPass (as VolumetricFog and AmbientLight bind theirs by hand there): a texture set on the
	// device before the pass began did not reach the pass (SunFar's cloud layers, Specular's sky visibility, 2026-10-19 tests).
	// Null entries are left alone; ClearPassTextures unbinds the ones set and empties the list.
	IDirect3DBaseTexture9*	PassTextures[16] = {};
	void					SetPassTexture(UINT stage, IDirect3DBaseTexture9* texture) { if (stage < 16) PassTextures[stage] = texture; }
	void					BindPassTextures();
	void					ClearPassTextures();

	ID3DXEffect* Effect;
	const char* Name;
};
