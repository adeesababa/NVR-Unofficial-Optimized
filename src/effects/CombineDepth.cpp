#include "CombineDepth.h"


void CombineDepthEffect::UpdateConstants() {
	NiCamera* Camera = WorldSceneGraph->camera;

	if (!johnnyguitar || !JG_GetClipDist) {
		Constants.viewNearZ = Camera->Frustum.Near;
	}
	else {
		float nearZ = JG_GetClipDist();

		Constants.viewNearZ = nearZ ? max(nearZ, 0.3)  : Camera->Frustum.Near;
	}
}

void CombineDepthEffect::RegisterTextures() {
	const D3DFORMAT format = TheSettingManager->SettingsMain.Main.SlimDepthBuffer ? D3DFMT_R32F : D3DFMT_G32R32F;
	TheTextureManager->InitTexture("TESR_DepthBuffer", &Textures.CombinedDepthTexture, &Textures.CombinedDepthSurface, TheRenderManager->width, TheRenderManager->height, format);
	Logger::Log("Depth buffer: %s.", format == D3DFMT_R32F ? "R32F (single channel)" : "G32R32F");
}