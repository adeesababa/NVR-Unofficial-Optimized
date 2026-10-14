#include "SkyView.h"

void SkyViewEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_SkyViewLUT", &Textures.TableTexture, &Textures.TableSurface, 128, 64, D3DFMT_A16B16G16R16F);
}

bool SkyViewEffect::ShouldRender() {
	SkyShaders* sky = TheShaderManager->Shaders.Sky;
	return Effect && Textures.TableSurface && sky && sky->PhysicalSettings.Enabled && sky->Constants.PhysSky.x > 0.0f &&
		TheShaderManager->GameState.isExterior;
}

// Into the table, then back to the frame's target.
void SkyViewEffect::RenderTable(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget) {
	TheShaderManager->RenderEffectToRT(Textures.TableSurface, this, false, 0);
	Device->SetRenderTarget(0, RenderTarget);
}
