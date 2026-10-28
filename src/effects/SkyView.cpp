#include "SkyView.h"
#include "../core/DxvkCompute.h"

void SkyViewEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_SkyViewLUT", &Textures.TableTexture, &Textures.TableSurface, 128, 64, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SkyTransLUT", &Textures.TransTexture, &Textures.TransSurface, 256, 64, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SkyMSLUT", &Textures.MSTexture, &Textures.MSSurface, 32, 32, D3DFMT_A16B16G16R16F);
}

bool SkyViewEffect::ShouldRender() {
	SkyShaders* sky = TheShaderManager->Shaders.Sky;
	return Effect && Textures.TableSurface && sky && sky->PhysicalSettings.Enabled && sky->Constants.PhysSky.x > 0.0f &&
		TheShaderManager->GameState.isExterior;
}

void SkyViewEffect::RenderTable(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget) {
	SkyShaders* sky = TheShaderManager->Shaders.Sky;
	const bool ms = sky->PhysicalSettings.MultipleScattering && Textures.TransSurface && Textures.MSSurface;
	const bool exact = ms || (sky->PhysicalSettings.ExactSunlight && Textures.TransSurface);
	UINT technique = 0;
	if (exact) {
		const float haze = sky->Constants.PhysSky.w, albedo = sky->PhysicalSettings.GroundAlbedo;
		const bool changed = !transBuilt || haze != builtHaze || (ms && (!msBuilt || albedo != builtAlbedo));
		if (changed) {
			static GpuTimer buildTimer("Physical sky tables (static)", 1);
			const bool firstDraw = !transBuilt || (ms && !msBuilt);
			GpuProfileScope gpu(buildTimer, Device);
			TheShaderManager->RenderEffectToRT(Textures.TransSurface, this, false, 1);
			transBuilt = true;
			if (ms) {
				TheShaderManager->RenderEffectToRT(Textures.MSSurface, this, false, 2);
				msBuilt = true;
				builtAlbedo = albedo;
			}
			if (!ms && haze != builtHaze) msBuilt = false;
			builtHaze = haze;
			if (firstDraw) Logger::Log("UNOFFICIAL physical sky: drew the %s table(s) (haze %.2f, ground albedo %.2f)", ms ? "sun transmittance and multiple scattering" : "sun transmittance", haze, albedo);
		}
		technique = ms ? 4 : 3;
	}
	TheShaderManager->RenderEffectToRT(Textures.TableSurface, this, false, technique);
	Device->SetRenderTarget(0, RenderTarget);
}

struct SkyViewPush {
	D3DXVECTOR4	physSky;
	D3DXVECTOR4	physSkySun;
	UInt32		samplerIndex;
	UInt32		pad;
	float		uvShift[2];
};

bool SkyViewEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex > 4 || ClearRenderTarget || !RenderTarget) return false;
	SkyShaders* sky = TheShaderManager->Shaders.Sky;
	if (!sky) return false;

	static const char* const names[5] = { "SkyView", "SkyTrans", "SkyMS", "SkyViewX", "SkyViewXMS" };
	const UINT textures = techniqueIndex == 4 ? 2 : techniqueIndex >= 2 ? 1 : 0;
	const UINT resourceCount = textures ? textures + 1 : 0;
	ExtPass& pass = extPass[techniqueIndex];
	if (!pass.Tried) {
		pass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[3] = {};
		for (UINT i = 0; i < textures; i++) {
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i; bindings[i].ViewType = NVR_EXT_VIEW_2D;
		}
		if (textures) { bindings[textures].Type = NVR_EXT_BIND_SAMPLER; bindings[textures].Slot = 0; bindings[textures].PushOffset = 32; }
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(names[techniqueIndex], bindings, resourceCount, sizeof(SkyViewPush), textures != 0);
	}
	ID3D9NvrExtSampler* sampler = textures ? DxvkCompute::LinearClamp() : nullptr;
	if (!pass.Pipeline || (textures && !sampler)) return false;

	IDirect3DBaseTexture9* inputs[2] = { Textures.TransTexture, Textures.MSTexture };
	NVR_EXT_RESOURCE_BINDING resources[3] = {};
	for (UINT i = 0; i < textures; i++) {
		if (!inputs[i]) return false;
		resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = inputs[i];
	}
	if (textures) { resources[textures].Slot = 0; resources[textures].Type = NVR_EXT_BIND_SAMPLER; resources[textures].pResource = sampler; }

	SkyViewPush push = {};
	push.physSky = sky->Constants.PhysSky;
	push.physSkySun = sky->Constants.PhysSkySun;
	D3DSURFACE_DESC desc;
	if (FAILED(RenderTarget->GetDesc(&desc)) || !desc.Width || !desc.Height) return false;
	push.uvShift[0] = 0.5f / (float)TheRenderManager->width - 0.5f / (float)desc.Width;
	push.uvShift[1] = 0.5f / (float)TheRenderManager->height - 0.5f / (float)desc.Height;
	if (!ExtDrawTo(pass.Pipeline, &RenderTarget, 1, resources, resourceCount, &push, sizeof(push), nullptr, ExtUv::TexelCentre)) return false;
	if (++ExtFrames == 1) Logger::Log("UNOFFICIAL NVR DXVK: %s runs as Vulkan passes (the physical sky's tables).", Name);
	return true;
}
