#include "Rain.h"
#include "../core/DxvkCompute.h"

void RainEffect::UpdateConstants() {
	if (TheShaderManager->GameState.isRainy && Constants.RainAnimator.switched == false) {
		// it just started raining
		Constants.RainAnimator.switched = true;
		Constants.RainAnimator.Start(0.05f, 1);
	}
	else if (!TheShaderManager->GameState.isRainy && Constants.RainAnimator.switched) {
		// it just stopped raining
		Constants.RainAnimator.switched = false;
		Constants.RainAnimator.Start(0.07f, 0);
	}

	Constants.Data.x = Constants.RainAnimator.GetValue();
}

void RainEffect::UpdateSettings() {
	Constants.Data.y = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "VerticalScale");
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Speed");
	Constants.Data.w = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Opacity");

	Constants.Aspect.x = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Refraction");
	Constants.Aspect.y = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Coloring");
	Constants.Aspect.z = TheSettingManager->GetSettingF("Shaders.Precipitations.Main", "Bloom");
	Constants.Aspect.w = TheSettingManager->GetSettingI("Shaders.Precipitations.Main", "SkipHiddenLayers") != 0 ? 1.0f : 0.0f;
}

void RainEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_RainData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_RainAspect", &Constants.Aspect);
}

bool RainEffect::ShouldRender() {
	return Constants.Data.x > 0.0f &&
		TheShaderManager->GameState.isExterior &&
		!TheShaderManager->GameState.isUnderwater;
};

bool RainEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
	if (!shadows) return false;
	struct Push { D3DXMATRIX shadowCameraToLight, viewTransform; D3DXVECTOR4 viewScale, rainData, rainAspect, sunDirection, sunColor; UInt32 samplerIndex; UInt32 pad[3]; } push = {};
	push.shadowCameraToLight = shadows->ShadowMaps[ShadowsExteriorEffect::MapOrtho].ShadowCameraToLight;
	push.viewTransform = TheRenderManager->viewMatrix;
	push.viewScale = D3DXVECTOR4(TheRenderManager->projMatrix._11, TheRenderManager->projMatrix._22, TheRenderManager->CameraData.y, TheShaderManager->ShaderConst.GameTime.z);
	push.rainData = Constants.Data;
	push.rainAspect = Constants.Aspect;
	push.sunDirection = TheShaderManager->ShaderConst.SunDir;
	push.sunColor = TheShaderManager->ShaderConst.sunColor;
	IDirect3DBaseTexture9* textures[] = { TheTextureManager->SourceTexture,
		TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr,
		shadows->ShadowMapOrthoTexture,
		TheShaderManager->Effects.Bloom ? TheShaderManager->Effects.Bloom->Textures.BloomTexture[0] : nullptr };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "Precipitations", textures, _countof(textures), &push, sizeof(push), 208);
}
