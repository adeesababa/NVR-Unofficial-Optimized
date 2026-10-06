#include "VolumetricSmoke.h"

void VolumetricSmokeEffect::RegisterTextures() {
	using namespace VolumetricSmoke;
	FullState.Forget();
	HalfState.Forget();
	LowState.Forget();
	HazeState.Forget();
	TheTextureManager->InitTexture("TESR_SmokeBuffer", &Texture, &Surface, TheRenderManager->width, TheRenderManager->height, D3DFMT_A16B16G16R16F);
	FullTexture = Texture;
	FullSurface = Surface;
	TheTextureManager->InitTexture("TESR_SmokeBufferLow", &LowTexture, &LowSurface, (TheRenderManager->width + 7) / 8,
		(TheRenderManager->height + 7) / 8, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SmokeHaze", &HazeTexture, &HazeSurface, (TheRenderManager->width + 3) / 4,
		(TheRenderManager->height + 3) / 4, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SmokeCut", &CutTexture, &CutSurface, (TheRenderManager->width + 3) / 4,
		(TheRenderManager->height + 3) / 4, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SmokeEnv", &EnvTexture, &EnvSurface, 1, 1, D3DFMT_A16B16G16R16F);
	const UINT w = (TheRenderManager->width + 1) / 2, h = (TheRenderManager->height + 1) / 2;
	if (SUCCEEDED(TheRenderManager->device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &HalfTexture, NULL)))
		HalfTexture->GetSurfaceLevel(0, &HalfSurface);
	else {
		HalfTexture = nullptr;
		Logger::Log("VolumetricSmoke: no half-resolution buffer (%ux%u); full resolution only", w, h);
	}
}
