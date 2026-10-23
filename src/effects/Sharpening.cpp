#include "Sharpening.h"
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-23 (NVR DXVK)

// UNOFFICIAL: outdoors ([Shaders.Sharpening.Main]) and indoors ([Shaders.Sharpening.Interiors]) each have their own
// values; the one for the current cell is published every frame.
void SharpeningEffect::UpdateConstants() {
	Constants.Data = TheShaderManager->GameState.isExterior ? Exterior : Interior;
}

void SharpeningEffect::UpdateSettings(){
	Exterior = D3DXVECTOR4(TheSettingManager->GetSettingF("Shaders.Sharpening.Main", "Strength"),
		TheSettingManager->GetSettingF("Shaders.Sharpening.Main", "Clamp"),
		TheSettingManager->GetSettingF("Shaders.Sharpening.Main", "Offset"), 0.0f);
	Interior = D3DXVECTOR4(TheSettingManager->GetSettingF("Shaders.Sharpening.Interiors", "Strength"),
		TheSettingManager->GetSettingF("Shaders.Sharpening.Interiors", "Clamp"),
		TheSettingManager->GetSettingF("Shaders.Sharpening.Interiors", "Offset"), 0.0f);
	Constants.Data = TheShaderManager->GameState.isExterior ? Exterior : Interior;
}

void SharpeningEffect::RegisterConstants(){
	TheShaderManager->RegisterConstant("TESR_SharpeningData", &Constants.Data);
}

// UNOFFICIAL 2026-10-23 (NVR DXVK): Sharpening.frag; the two textures and the constants the technique reads.
bool SharpeningEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	struct Push { D3DXVECTOR4 data, reciprocalResolution, cameraData; UInt32 samplerIndex; UInt32 pad[3]; };
	if (!extTried) {
		extTried = true;
		static const NVR_EXT_BINDING_DESC bindings[] = {
			{ 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE, 0, NVR_EXT_VIEW_2D, FALSE, 0 },		// t_rendered
			{ 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE, 1, NVR_EXT_VIEW_2D, FALSE, 0 },		// t_depth
			{ 0, 0, NVR_EXT_BIND_SAMPLER,       0, NVR_EXT_VIEW_2D, FALSE, 48 },		// samplerIndex
		};
		extPipeline = DxvkCompute::CreateGraphicsPipeline("Sharpening", bindings, _countof(bindings), sizeof(Push), true);
	}
	IDirect3DTexture9* depth = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
	ID3D9NvrExtSampler* sampler = DxvkCompute::LinearClamp();
	if (!extPipeline || !depth || !sampler || !TheTextureManager->RenderedTexture) return false;
	Push push = {};
	push.data = Constants.Data;
	push.reciprocalResolution = TheShaderManager->ShaderConst.ReciprocalResolution;
	push.cameraData = TheRenderManager->CameraData;
	NVR_EXT_RESOURCE_BINDING resources[] = {
		{ 0, NVR_EXT_BIND_SAMPLED_IMAGE, TheTextureManager->RenderedTexture, 0, 0, 0, FALSE },
		{ 1, NVR_EXT_BIND_SAMPLED_IMAGE, depth,                             0, 0, 0, FALSE },
		{ 0, NVR_EXT_BIND_SAMPLER,       sampler,                           0, 0, 0, FALSE },
	};
	return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, extPipeline, resources, _countof(resources), &push, sizeof(push));
}

