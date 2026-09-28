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
	// Effects recompute the post-projection depth from the linear channel (Depth.hlsl
	// projectedDepthFromLinear), so a single 32-bit channel carries everything they read.
	const D3DFORMAT format = TheSettingManager->SettingsMain.Main.SlimDepthBuffer ? D3DFMT_R32F : D3DFMT_G32R32F;
	TheTextureManager->InitTexture("TESR_DepthBuffer", &Textures.CombinedDepthTexture, &Textures.CombinedDepthSurface, TheRenderManager->width, TheRenderManager->height, format);
	Logger::Log("UNOFFICIAL depth buffer: %s.", format == D3DFMT_R32F ? "R32F (single channel)" : "G32R32F");
}

bool CombineDepthEffect::RenderWithNormals(IDirect3DDevice9* Device, IDirect3DSurface9* NormalsSurface) {
	if (mergedNormalsFailed || TheSettingManager->SettingsMain.Main.DisableMergedNormals ||
		!Enabled || !Effect || !ShouldRender() || !NormalsSurface || !Textures.CombinedDepthSurface)
		return false;
	D3DXHANDLE technique = Effect->GetTechniqueByName("DepthNormals");
	D3DSURFACE_DESC depthDesc = {}, normalsDesc = {};
	D3DCAPS9 caps = {};
	if (!technique || FAILED(Device->GetDeviceCaps(&caps)) || caps.NumSimultaneousRTs < 2 ||
		FAILED(Textures.CombinedDepthSurface->GetDesc(&depthDesc)) || FAILED(NormalsSurface->GetDesc(&normalsDesc)) ||
		depthDesc.Width != normalsDesc.Width || depthDesc.Height != normalsDesc.Height) {
		mergedNormalsFailed = true;
		return false;
	}
	// R32F depth is 32 bits per pixel, the normals are 64: mixing sizes needs this cap.
	const bool sameBitDepth = (depthDesc.Format == D3DFMT_G32R32F) == (normalsDesc.Format == D3DFMT_A16B16G16R16F);
	if (!sameBitDepth && !(caps.PrimitiveMiscCaps & D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)) {
		mergedNormalsFailed = true;
		Logger::Log("Merged depth/normals unavailable: MRT with different bit depths not supported.");
		return false;
	}

	auto timer = TimeLogger();
	DWORD oldWriteMask1 = 0xF;
	Device->GetRenderState(D3DRS_COLORWRITEENABLE1, &oldWriteMask1);
	HRESULT result = Device->SetRenderTarget(0, Textures.CombinedDepthSurface);
	if (SUCCEEDED(result)) result = Device->SetRenderTarget(1, NormalsSurface);
	if (SUCCEEDED(result)) result = Device->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
	if (SUCCEEDED(result)) result = Effect->SetTechnique(technique);
	if (SUCCEEDED(result)) {
		SetCT();
		UINT passes = 0;
		result = Effect->Begin(&passes, 0);
		if (SUCCEEDED(result)) {
			result = Effect->BeginPass(0);
			if (SUCCEEDED(result)) {
				result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
				Effect->EndPass();
			}
			Effect->End();
		}
	}
	Device->SetRenderTarget(1, nullptr);
	Device->SetRenderState(D3DRS_COLORWRITEENABLE1, oldWriteMask1);
	if (FAILED(result)) {
		mergedNormalsFailed = true;
		Logger::Log("Merged depth/normals failed (%08lx); using separate passes until restart.", result);
		return false;
	}
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL merged depth/normals active: one pass, two render targets.");
		reported = true;
	}
	renderTime = timer.LogTime("CombineDepth::RenderWithNormals");
	return true;
}