#include "Normals.h"
#include "../core/DxvkCompute.h"
#include "../core/RenderRect.h"

static const D3DXVECTOR4* Registered(const char* name) {
	auto found = TheShaderManager->ConstantsTable.find(name);
	return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
}

void NormalsEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_NormalsData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_NormalsSmoothData", &SmoothData);
}

void NormalsEffect::UpdateConstants() {
	SmoothData.x = (float)max(0, min(TheSettingManager->SettingsMain.Main.SmoothNormals, 256));
}

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
	bool drawn = DxvkCompute::PassesOn() && !ExtFailed && ExtSmooth(Device);
	if (!drawn) {
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
				RenderRect::ApplyViewport(Device);
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
		drawn = SUCCEEDED(result);
	}
	smoothed = drawn;
	static int reported = -1;
	if ((smoothed ? 1 : 0) != reported) {
		reported = smoothed ? 1 : 0;
		Logger::Log("UNOFFICIAL SmoothNormals: %s", smoothed ? "on (TESR_NormalsBuffer smoothed in place after the normals pass; AmbientLight's own NormalSmoothing skipped)" : "NOT drawn (a draw failed)");
	}
	return smoothed;
}

bool NormalsEffect::ExtSmooth(IDirect3DDevice9* Device) {
	IDirect3DTexture9* depth = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
	const D3DXVECTOR4* reciprocal = Registered("TESR_ReciprocalResolution");
	const D3DXVECTOR4* cameraData = Registered("TESR_CameraData");
	const D3DXVECTOR4* projection = Registered("TESR_ProjectionTransform");
	if (!depth || !Textures.NormalsTexture || !Textures.NormalsSurface || !SmoothTempTexture || !SmoothTempSurface || !reciprocal || !cameraData || !projection) return false;

	struct Push { D3DXVECTOR4 reciprocalResolution; D3DXVECTOR4 smoothData; D3DXVECTOR4 cameraData; D3DXMATRIX projection; UInt32 samplerIndex; UInt32 alongY; } push = {};
	push.reciprocalResolution = *reciprocal;
	push.smoothData = SmoothData;
	push.cameraData = *cameraData;
	memcpy(&push.projection, projection, sizeof(D3DXMATRIX));
	for (UINT pass = 0; pass < 2; pass++) {
		push.alongY = pass;
		IDirect3DBaseTexture9* textures[] = { pass == 0 ? Textures.NormalsTexture : SmoothTempTexture, depth };
		IDirect3DSurface9* target = pass == 0 ? SmoothTempSurface : Textures.NormalsSurface;
		if (!ExtTexturePass(Device, target, nullptr, nullptr, extPassSmooth, "NormalsSmooth", textures, _countof(textures), &push, sizeof(push), 112, true)) return false;
	}
	return true;
}

bool NormalsEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0 || ClearRenderTarget) return false;
	IDirect3DBaseTexture9* depth = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
	const D3DXVECTOR4* reciprocal = Registered("TESR_ReciprocalResolution");
	const D3DXVECTOR4* cameraData = Registered("TESR_CameraData");
	const D3DXVECTOR4* invProjection = Registered("TESR_InvProjectionTransform");
	if (!depth || !reciprocal || !cameraData || !invProjection) return false;

	struct Push { D3DXVECTOR4 reciprocalResolution; D3DXVECTOR4 cameraData; D3DXMATRIX invProjection; UInt32 samplerIndex; UInt32 pad[3]; } push = {};
	push.reciprocalResolution = *reciprocal;
	push.cameraData = *cameraData;
	memcpy(&push.invProjection, invProjection, sizeof(D3DXMATRIX));
	IDirect3DBaseTexture9* textures[] = { depth };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "Normals", textures, _countof(textures), &push, sizeof(push), 96, true);
}
	
void NormalsEffect::RegisterTextures() {
	TheTextureManager->InitTexture("TESR_NormalsBuffer", &Textures.NormalsTexture, &Textures.NormalsSurface, TheRenderManager->width, TheRenderManager->height, D3DFMT_A16B16G16R16F);
}