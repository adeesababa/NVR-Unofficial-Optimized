#include "VolumetricSmoke.h"

// The buffer step 1 adds the segments' optical depth into (full resolution, so the smoke's edge against the gun and
// walls stays sharp), sampled by step 2 as TESR_SmokeBuffer.
void VolumetricSmokeEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_SmokeBuffer", &VolumetricSmoke::Texture, &VolumetricSmoke::Surface,
		TheRenderManager->width, TheRenderManager->height, D3DFMT_A16B16G16R16F);
}
