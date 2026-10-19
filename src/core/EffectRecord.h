#pragma once
#include <vector>

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
	virtual bool			SwitchEffect();
	virtual void			Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer);
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
