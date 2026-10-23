#include "Normals.h"

void NormalsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_NormalsData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_NormalsSmoothData", &SmoothData);	// UNOFFICIAL 2026-10-23 (sharednormals)
}

void NormalsEffect::UpdateConstants() {
	SmoothData.x = (float)max(0, min(TheSettingManager->SettingsMain.Main.SmoothNormals, 256));	// UNOFFICIAL 2026-10-23 (sharednormals), live
}

// UNOFFICIAL 2026-10-23 (sharednormals): see Normals.h. Pass X reads TESR_NormalsBuffer (stage 1) into the spare texture, pass Y reads
// the spare (stage 2) back into TESR_NormalsBuffer; the stage that is also the pass's target is unbound first. Nothing drawn (false)
// when off, when the effect is missing, or when a draw fails (logged once).
bool NormalsEffect::RenderSmooth(IDirect3DDevice9* Device) {
	smoothed = false;
	const int reach = TheSettingManager->SettingsMain.Main.SmoothNormals;
	if (reach <= 0 || !Enabled || !Effect || !ShouldRender() || !Textures.NormalsTexture || !Textures.NormalsSurface) return false;
	if (!smoothTried) {
		smoothTried = true;
		TheTextureManager->InitTexture("TESR_NormalsSmoothTemp", &SmoothTempTexture, &SmoothTempSurface, TheRenderManager->width, TheRenderManager->height, D3DFMT_A16B16G16R16F);
	}
	D3DXHANDLE technique = Effect->GetTechnique(0);
	D3DXHANDLE smooth = TechniqueHandle(techSmooth, "NormalsSmooth");
	D3DXTECHNIQUE_DESC description = {};
	if (!technique || !smooth || !SmoothTempTexture || !SmoothTempSurface || FAILED(Effect->GetTechniqueDesc(smooth, &description)) || description.Passes != 2) return false;

	static GpuTimer timer("  Normal smoothing (shared)");
	GpuProfileScope gpu(timer, Device);
	IDirect3DSurface9* target = nullptr;
	Device->GetRenderTarget(0, &target);
	Effect->SetTechnique(smooth);
	SetCT();
	UINT passes = 0;
	HRESULT result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		for (UINT p = 0; p < 2 && SUCCEEDED(result); ++p) {
			IDirect3DTexture9* read = p == 0 ? Textures.NormalsTexture : SmoothTempTexture;
			IDirect3DSurface9* write = p == 0 ? SmoothTempSurface : Textures.NormalsSurface;
			Device->SetTexture(1, p == 0 ? read : nullptr);
			Device->SetTexture(2, p == 1 ? read : nullptr);
			TheRenderManager->renderState->NoteDeviceTexture(1, p == 0 ? read : nullptr);
			TheRenderManager->renderState->NoteDeviceTexture(2, p == 1 ? read : nullptr);
			result = Device->SetRenderTarget(0, write);
			if (SUCCEEDED(result)) result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Effect->EndPass();
		}
		Effect->End();
	}
	Effect->SetTechnique(technique);
	Device->SetTexture(1, nullptr);
	Device->SetTexture(2, nullptr);
	TheRenderManager->renderState->NoteDeviceTexture(1, nullptr);
	TheRenderManager->renderState->NoteDeviceTexture(2, nullptr);
	Device->SetRenderTarget(0, target);
	if (target) target->Release();
	smoothed = SUCCEEDED(result);
	static int reported = -1;
	if ((smoothed ? 1 : 0) != reported) {
		reported = smoothed ? 1 : 0;
		Logger::Log("UNOFFICIAL SmoothNormals: %s", smoothed ? "on (TESR_NormalsBuffer smoothed in place after the normals pass; AmbientLight's own NormalSmoothing skipped)" : "NOT drawn (a draw failed)");
	}
	return smoothed;
}
	
void NormalsEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_NormalsBuffer", &Textures.NormalsTexture, &Textures.NormalsSurface, TheRenderManager->width, TheRenderManager->height, D3DFMT_A16B16G16R16F);
}