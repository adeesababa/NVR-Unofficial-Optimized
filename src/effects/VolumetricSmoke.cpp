#include "VolumetricSmoke.h"

// The buffers step 1 adds the segments' optical depth into, sampled by step 2: TESR_SmokeBuffer at full resolution, or
// at half (a quarter of the pixels to shade; the smoke is soft, and step 2 blends it up smoothly), chosen each frame by
// Main > GunFX > VolumetricSmokeHalfRes (it is registered by the address of Texture, so it samples whichever buffer Texture
// points to when the step runs); TESR_SmokeBufferLow at an eighth of the resolution for the big segments (VolumetricSmoke.h);
// TESR_SmokeHaze for the haze and TESR_SmokeEnv for the room light.
void VolumetricSmokeEffect::RegisterTextures() {
	using namespace VolumetricSmoke;
	// The surfaces made below are new, so whatever the clear flags knew about the old ones does not apply (VolumetricSmoke.h).
	FullState.Forget();
	HalfState.Forget();
	LowState.Forget();
	HazeState.Forget();
	TheTextureManager->InitTexture("TESR_SmokeBuffer", &Texture, &Surface, TheRenderManager->width, TheRenderManager->height, D3DFMT_A16B16G16R16F);
	FullTexture = Texture;
	FullSurface = Surface;
	// The big, close segments (each nearly a full-screen pass) at an eighth of the resolution, added in by step 2.
	TheTextureManager->InitTexture("TESR_SmokeBufferLow", &LowTexture, &LowSurface, (TheRenderManager->width + 7) / 8,
		(TheRenderManager->height + 7) / 8, D3DFMT_A16B16G16R16F);
	// The gun-smoke haze, all of it in one pass at quarter resolution; and the room light step 2 lights the smoke with
	// (one pixel, worked out once a frame).
	TheTextureManager->InitTexture("TESR_SmokeHaze", &HazeTexture, &HazeSurface, (TheRenderManager->width + 3) / 4,
		(TheRenderManager->height + 3) / 4, D3DFMT_A16B16G16R16F);
	// The bullets' cut of the segment smoke, written with the haze (a second target of the same size and format).
	TheTextureManager->InitTexture("TESR_SmokeCut", &CutTexture, &CutSurface, (TheRenderManager->width + 3) / 4,
		(TheRenderManager->height + 3) / 4, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SmokeEnv", &EnvTexture, &EnvSurface, 1, 1, D3DFMT_A16B16G16R16F);   // (blended: half floats)
	const UINT w = (TheRenderManager->width + 1) / 2, h = (TheRenderManager->height + 1) / 2;
	if (SUCCEEDED(TheRenderManager->device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &HalfTexture, NULL)))
		HalfTexture->GetSurfaceLevel(0, &HalfSurface);
	else {
		HalfTexture = nullptr;
		Logger::Log("VolumetricSmoke: no half-resolution buffer (%ux%u); full resolution only", w, h);
	}
}
