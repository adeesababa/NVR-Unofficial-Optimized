#include "AmbientLight.h"

void AmbientLightEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_AmbientLightData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_AmbientLightData2", &Constants.Data2);
}

void AmbientLightEffect::UpdateSettings() {
	Constants.Data.x = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "Radius"), 10.0f, 1000.0f);
	Constants.Data.y = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "OcclusionStrength"), 0.0f, 4.0f);
	Constants.Data.z = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "BounceStrength"), 0.0f, 10.0f);
	Constants.Data.w = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "DrawDistance"), 500.0f, 50000.0f);
	Constants.Data2.x = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "DarkestOcclusion"), 0.0f, 1.0f);
	Constants.Data2.y = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "BrightnessFade"), 0.0f, 4.0f);
	Constants.Data2.z = std::clamp(TheSettingManager->GetSettingF("Shaders.AmbientLight.Main", "BlurRadius"), 0.5f, 4.0f);
	Constants.Data2.w = (float)std::clamp(TheSettingManager->GetSettingI("Shaders.AmbientLight.Main", "DebugView"), 0, 6);
}

// Half resolution, made the first time it runs (about 14 MB at 1440p: one A32B32G32R32F, two A16B16G16R16F).
void AmbientLightEffect::EnsureBuffers() {
	if (buffersTried) return;
	buffersTried = true;
	const int width = (TheRenderManager->width + 1) / 2;
	const int height = (TheRenderManager->height + 1) / 2;
	TheTextureManager->InitTexture("NVR_AmbientLightPrep", &prepTexture, &prepSurface, width, height, D3DFMT_A32B32G32R32F);
	TheTextureManager->InitTexture("NVR_AmbientLight0", &bufferTexture[0], &bufferSurface[0], width, height, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("NVR_AmbientLight1", &bufferTexture[1], &bufferSurface[1], width, height, D3DFMT_A16B16G16R16F);
}

bool AmbientLightEffect::ShouldRender() {
	return Enabled && Effect && !TheShaderManager->GameState.isUnderwater;
}

void AmbientLightEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
	IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
	IDirect3DSurface9* SourceBuffer) {
	if (!ShouldRender()) { renderTime = 0; return; }
	EnsureBuffers();
	D3DXHANDLE layoutHandle = ParameterHandle(paramLayout, "NVR_ALLayout");
	IDirect3DTexture9* scene = TheTextureManager->RenderedTexture;
	D3DXTECHNIQUE_DESC description = {};
	D3DVIEWPORT9 original = {};
	D3DSURFACE_DESC half = {};
	D3DXHANDLE technique = Effect->GetTechnique(0);
	if (!layoutHandle || !scene || !prepTexture || !prepSurface || !bufferTexture[0] || !bufferTexture[1] || !bufferSurface[0] ||
		!bufferSurface[1] || !technique || FAILED(Effect->GetTechniqueDesc(technique, &description)) || description.Passes != 5 ||
		FAILED(Device->GetViewport(&original)) || FAILED(bufferSurface[0]->GetDesc(&half))) {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL AmbientLight: buffers missing or AmbientLight.fx.hlsl is not the matching version (5 passes), effect skipped.");
		reported = true;
		renderTime = 0;
		return;
	}
	auto timer = TimeLogger();
	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);
	IDirect3DSurface9* finalTarget = chained ? chain.Output() : RenderTarget;
	D3DVIEWPORT9 reduced = original;
	reduced.Width = half.Width;
	reduced.Height = half.Height;
	D3DXVECTOR4 layout((float)half.Width / original.Width, (float)half.Height / original.Height, 1.0f / half.Width, 1.0f / half.Height);
	IDirect3DSurface9* depthSurface = nullptr;
	Device->GetDepthStencilSurface(&depthSurface);
	if (!TheSettingManager->SettingsMain.Main.ReShadeDepthCompat) Device->SetDepthStencilSurface(nullptr);

	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(layoutHandle, &layout);
	D3DXHANDLE flagsHandle = ParameterHandle(paramFlags, "NVR_ALFlags");
	const bool shadow = foldShadow && flagsHandle;
	if (flagsHandle) {
		D3DXVECTOR4 flags(shadow ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		Effect->SetVector(flagsHandle, &flags);
	}
	static GpuTimer passTimers[5] = { GpuTimer("  Ambient light prepare (half)"), GpuTimer("  Ambient light estimate (half)"),
		GpuTimer("  Ambient light blur X (half)"), GpuTimer("  Ambient light blur Y (half)"), GpuTimer("  Ambient light apply (full)") };
	UINT passes = 0;
	HRESULT result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		for (UINT p = 0; p < 5 && SUCCEEDED(result); ++p) {
			// Prepare -> prep; Estimate (prep) -> 0; blur X (0) -> 1; blur Y (1) -> 0; Apply (0) -> the frame.
			const bool apply = p == 4;
			IDirect3DSurface9* destination = apply ? finalTarget : p == 0 ? prepSurface : bufferSurface[p == 2 ? 1 : 0];
			Device->SetTexture(5, nullptr);
			Device->SetTexture(6, nullptr);
			result = Device->SetRenderTarget(0, destination);
			if (SUCCEEDED(result)) result = Device->SetViewport(apply ? &original : &reduced);
			if (SUCCEEDED(result)) result = Effect->BeginPass(p);
			if (FAILED(result)) break;
			if (p > 0) Device->SetTexture(5, prepTexture);
			if (p == 2 || p == 4) Device->SetTexture(6, bufferTexture[0]);
			else if (p == 3) Device->SetTexture(6, bufferTexture[1]);
			Device->SetTexture(1, scene);	// TESR_SourceBuffer slot: the current image
			{
				GpuProfileScope gpu(passTimers[p], Device);
				result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			}
			HRESULT endResult = Effect->EndPass();
			if (SUCCEEDED(result)) result = endResult;
		}
		Effect->End();
	}
	Device->SetTexture(5, nullptr);
	Device->SetTexture(6, nullptr);
	Device->SetTexture(1, TheTextureManager->SourceTexture);
	Device->SetRenderTarget(0, RenderTarget);
	Device->SetViewport(&original);
	Device->SetDepthStencilSurface(depthSurface);
	if (depthSurface) depthSurface->Release();
	if (SUCCEEDED(result)) {
		if (chained) chain.Commit();
		else Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_NONE);
		shadowApplied = shadow;
	}
	else {
		static bool reported = false;
		if (!reported) Logger::Log("UNOFFICIAL AmbientLight: render failed (%08lx).", result);
		reported = true;
	}
	renderTime = timer.LogTime("AmbientLight::Render");
}
