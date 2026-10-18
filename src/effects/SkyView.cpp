#include "SkyView.h"

void SkyViewEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_SkyViewLUT", &Textures.TableTexture, &Textures.TableSurface, 128, 64, D3DFMT_A16B16G16R16F);
	// UNOFFICIAL 2026-10-18 ExactSunlight / MultipleScattering: the static tables (SkyView.fx.hlsl techniques 1 and 2). Declared in the effect as
	// TESR_SkyTransLUT then TESR_SkyMSLUT: samplers bind by declaration order.
	TheTextureManager->InitTexture("TESR_SkyTransLUT", &Textures.TransTexture, &Textures.TransSurface, 256, 64, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SkyMSLUT", &Textures.MSTexture, &Textures.MSSurface, 32, 32, D3DFMT_A16B16G16R16F);
}

bool SkyViewEffect::ShouldRender() {
	SkyShaders* sky = TheShaderManager->Shaders.Sky;
	return Effect && Textures.TableSurface && sky && sky->PhysicalSettings.Enabled && sky->Constants.PhysSky.x > 0.0f &&
		TheShaderManager->GameState.isExterior;
}

// Into the table, then back to the frame's target.
void SkyViewEffect::RenderTable(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget) {
	SkyShaders* sky = TheShaderManager->Shaders.Sky;
	const bool ms = sky->PhysicalSettings.MultipleScattering && Textures.TransSurface && Textures.MSSurface;
	const bool exact = ms || (sky->PhysicalSettings.ExactSunlight && Textures.TransSurface);
	UINT technique = 0;		// 0: the closed-form sky, exactly as before
	if (exact) {
		const float haze = sky->Constants.PhysSky.w, albedo = sky->PhysicalSettings.GroundAlbedo;
		const bool changed = !transBuilt || haze != builtHaze || (ms && (!msBuilt || albedo != builtAlbedo));
		// fixes-a #48: no 256-frame refresh any more (it cost ~0.45 ms on that frame with MS on and bought nothing: NVR's targets are
		// D3DPOOL_DEFAULT render targets that are never released, so their contents cannot be lost in a running session; if a Reset
		// path is ever added it must clear transBuilt and msBuilt).
		if (changed) {
			static GpuTimer buildTimer("Physical sky tables (static)", 1);
			const bool firstDraw = !transBuilt || (ms && !msBuilt);	// fixes-a #50: log only when a table is first made, not on every drag step
			GpuProfileScope gpu(buildTimer, Device);
			TheShaderManager->RenderEffectToRT(Textures.TransSurface, this, false, 1);
			transBuilt = true;
			if (ms) {
				TheShaderManager->RenderEffectToRT(Textures.MSSurface, this, false, 2);
				msBuilt = true;
				builtAlbedo = albedo;
			}
			// fixes-a #49: with MS off a Haze change redraws only the transmittance table; the MS table (drawn for the old haze)
			// must be drawn again when MS is next turned on
			if (!ms && haze != builtHaze) msBuilt = false;
			builtHaze = haze;
			if (firstDraw) Logger::Log("UNOFFICIAL physical sky: drew the %s table(s) (haze %.2f, ground albedo %.2f)", ms ? "sun transmittance and multiple scattering" : "sun transmittance", haze, albedo);
		}
		technique = ms ? 4 : 3;
	}
	TheShaderManager->RenderEffectToRT(Textures.TableSurface, this, false, technique);
	Device->SetRenderTarget(0, RenderTarget);
}
