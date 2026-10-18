#include "SkyView.h"

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
