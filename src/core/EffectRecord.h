#pragma once
#include <vector>
#include <map>
#include <string>
#include <functional>
#include "NvrDxvkExt.h"

class Sm6Pass;
struct Sm6PassSet;

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
	virtual void			BeforeDraw(IDirect3DDevice9* Device) {};
	virtual bool			SwitchEffect();
	virtual void			Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer);
	virtual bool			ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) { return false; }
	bool					ExtFullScreen(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, IDirect3DSurface9* SourceBuffer,
								ID3D9NvrExtPipeline* Pipeline, const NVR_EXT_RESOURCE_BINDING* Bindings, UINT BindingCount, const void* Push, UINT PushSize, bool last = true);
	bool					ExtFailed = false;
	UInt32					ExtFrames = 0;
	struct ExtPass { ID3D9NvrExtPipeline* Pipeline = nullptr; bool Tried = false; };
	bool					ExtTexturePass(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, IDirect3DSurface9* SourceBuffer,
								ExtPass& Pass, const char* SpvName, IDirect3DBaseTexture9* const* Textures, UINT TextureCount,
								const void* Push, UINT PushSize, UINT SamplerPushOffset, bool PointSampler = false, UINT WrapSamplerPushOffset = 0, bool PointWrapSampler = false, bool Last = true);
	ID3D9NvrExtPipeline*	ExtVariant(std::map<std::string, ExtPass>& Variants, ExtPass& Generic, const char* SpvName, const char* Suffix,
								const std::function<ID3D9NvrExtPipeline*(const char*)>& Make);
	enum class ExtUv { ScreenQuad, TexelCentre };
	bool					ExtShiftLogged = false;
	bool					ExtDrawTo(ID3D9NvrExtPipeline* Pipeline, IDirect3DSurface9* const* Targets, UINT TargetCount, const NVR_EXT_RESOURCE_BINDING* Bindings,
								UINT BindingCount, const void* Push, UINT PushSize, const RECT* Viewport = nullptr, ExtUv Uv = ExtUv::TexelCentre,
								const NVR_EXT_DEPTH_STENCIL* DepthStencil = nullptr);
	struct ExtUniformBuffer { ID3D9NvrExtBuffer* Buffer = nullptr; UINT Size = 0; bool Tried = false; };
	ID3D9NvrExtBuffer*		ExtUniforms(ExtUniformBuffer& Uniforms, const void* Data, UINT Size);
	IDirect3DBaseTexture9*	ExtFileTexture(const char* SamplerName);
	// ClearSampler is inherited from ShaderProgram -- game shaders need the same thing.
	void					DisposeEffect();
	bool					LoadEffect();
	static int				BuildMacros(D3DXMACRO* EffectMacros);
	struct Sm6PassSet*		Sm6Set = nullptr;
	class Sm6Pass*			Sm6For(UINT techniqueIndex, UINT pass);
	bool					Sm6Covers(UINT techniqueIndex);

	bool 					IsLoaded();
	bool					Enabled;
	bool					usesSourceBuffer = true;
	bool					needsPrefill = true;
	std::vector<std::vector<bool>> passPrefill;
	bool					PassNeedsPrefill(UINT techniqueIndex, UINT pass) const;
	bool					writesWholeTarget = false;
	void					RebindSlotTextures();
	float					renderTime;
	float					constantUpdateTime;

	struct CachedHandle { unsigned Generation = 0; D3DXHANDLE Handle = NULL; };
	unsigned				LoadGeneration = 1;
	D3DXHANDLE				TechniqueHandle(CachedHandle& cache, const char* name);
	D3DXHANDLE				ParameterHandle(CachedHandle& cache, const char* name);
	IDirect3DBaseTexture9*	PassTextures[16] = {};
	void					SetPassTexture(UINT stage, IDirect3DBaseTexture9* texture) { if (stage < 16) PassTextures[stage] = texture; }
	void					BindPassTextures();
	void					ClearPassTextures();

	ID3DXEffect* Effect;
	const char* Name;
};
