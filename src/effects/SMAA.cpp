#include <algorithm>

#include "SMAA.h"

void SMAAEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SMAAResolution", &Constants.Resolution);
	TheShaderManager->RegisterConstant("TESR_SMAAData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SMAADepthData", &Constants.DepthData);
	TheShaderManager->RegisterConstant("TESR_SMAAPred", &Constants.Pred);
};

void SMAAEffect::RegisterTextures() {
	int width = TheRenderManager->width;
	int height = TheRenderManager->height;

	TheTextureManager->InitTexture("TESR_SMAA_Edges", &Textures.SMAA_Edges_Texture, &Textures.SMAA_Edges_Surface, width, height, D3DFMT_A8R8G8B8);
	TheTextureManager->InitTexture("TESR_SMAA_Blend", &Textures.SMAA_Blend_Texture, &Textures.SMAA_Blend_Surface, width, height, D3DFMT_A8R8G8B8);
};

void SMAAEffect::UpdateSettings() {
	Settings.Main.EdgeDetection = static_cast<Input>(std::clamp(TheSettingManager->GetSettingI("Shaders.SMAA.Main", "EdgeDetection"), 0, 3));
	// A key absent from the toml reads as 0, which the clamps below would floor to the bottom of
	// each range rather than to the documented default - a much more sensitive threshold and a
	// far shorter search than intended, silently, for anyone who updates the plugin without the
	// defaults file. Fall back to the SMAA_PRESET_ULTRA values instead, matching what the shader
	// does when the constants themselves are missing.
	float threshold = TheSettingManager->GetSettingF("Shaders.SMAA.Main", "Threshold");
	Settings.Main.Threshold = threshold > 0.0f ? std::clamp(threshold, 0.005f, 0.5f) : 0.05f;

	float depthThreshold = TheSettingManager->GetSettingF("Shaders.SMAA.Main", "DepthThreshold");
	Settings.Main.DepthThreshold = depthThreshold > 0.0f ? std::clamp(depthThreshold, 0.0001f, 0.5f) : 0.005f;

	int searchSteps = TheSettingManager->GetSettingI("Shaders.SMAA.Main", "MaxSearchSteps");
	Settings.Main.MaxSearchSteps = searchSteps > 0 ? (float)std::clamp(searchSteps, 4, 112) : 32.0f;
	Settings.Main.SubpixelShift = std::clamp(TheSettingManager->GetSettingF("Shaders.SMAA.Main", "SubpixelShift"), -2.0f, 2.0f);
	Settings.Main.DepthOffset = std::clamp(TheSettingManager->GetSettingF("Shaders.SMAA.Main", "DepthOffset"), -2.0f, 2.0f);
	float localContrast = TheSettingManager->GetSettingF("Shaders.SMAA.Main", "LocalContrast");
	Settings.Main.LocalContrast = localContrast > 0.0f ? std::clamp(localContrast, 1.0f, 20.0f) : 2.0f;
	Settings.Main.Predication = TheSettingManager->GetSettingI("Shaders.SMAA.Main", "Predication") != 0;
	const float predScale = TheSettingManager->GetSettingF("Shaders.SMAA.Main", "PredicationScale");
	Settings.Main.PredicationScale = predScale > 0.0f ? std::clamp(predScale, 1.0f, 8.0f) : 2.0f;
	const float predStrength = TheSettingManager->GetSettingF("Shaders.SMAA.Main", "PredicationStrength");
	Settings.Main.PredicationStrength = std::clamp(predStrength > 0.0f ? predStrength : 0.4f, 0.0f, 1.0f);
	Settings.Main.DeDither = TheSettingManager->GetSettingI("Shaders.SMAA.Main", "DeDither") != 0;
};

void SMAAEffect::UpdateConstants() {
	Constants.Resolution.x = 1.0f / (float) TheRenderManager->width;
	Constants.Resolution.y = 1.0f / (float) TheRenderManager->height;
	// SMAA_RT_METRICS is float4(1/width, 1/height, width, height) - see the definition in the
	// include. z and w were filled the other way round, and because SMAABlendingWeightCalculation
	// derives pixcoord from the same swapped pair, the error is self consistent per axis rather
	// than obviously broken: horizontal edge distances came out scaled by height/width and
	// vertical ones by width/height. At 16:9 that is 44% short horizontally, so every edge looked
	// shorter than it was and got less blending, and 78% long vertically, which clamps against
	// SMAA_AREATEX_MAX_DISTANCE. It reads as SMAA being too weak, in every edge detection mode,
	// because it is downstream of all three.
	Constants.Resolution.z = (float)TheRenderManager->width;
	Constants.Resolution.w = (float)TheRenderManager->height;

	Constants.Data.x = Settings.Main.Threshold;
	Constants.Data.y = Settings.Main.DepthThreshold;
	Constants.Data.z = Settings.Main.MaxSearchSteps;
	Constants.Data.w = Settings.Main.SubpixelShift;
	Constants.DepthData.x = Settings.Main.DepthOffset;
	Constants.DepthData.y = Settings.Main.LocalContrast;
	Constants.DepthData.z = 0.0f;
	Constants.DepthData.w = 0.0f;
	Constants.Pred = D3DXVECTOR4(Settings.Main.Predication ? 1.0f : 0.0f, Settings.Main.PredicationScale, Settings.Main.PredicationStrength, Settings.Main.DeDither ? 1.0f : 0.0f);
};

/*
 * Render SMAA by performing the three main passes (edge detection, blending weight calculation, and final blending). 
 */
void SMAAEffect::Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (!Enabled) {
		renderTime = 0.0f;
		return; // skip rendering if the effect is disabled
	}

	auto timer = TimeLogger();

	IDirect3DSurface9* previousDepth = nullptr;
	if (!Textures.StencilSurface && !Textures.StencilFailed) {
		if (FAILED(Device->CreateDepthStencilSurface(TheRenderManager->width, TheRenderManager->height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &Textures.StencilSurface, NULL))) {
			Textures.StencilSurface = nullptr;
			Textures.StencilFailed = true;
			Logger::Log("UNOFFICIAL SMAA: no depth-stencil surface of its own; uses the bound one as before");
		}
	}
	const bool ownStencil = Textures.StencilSurface != nullptr && !ReShadeIntegration::KeepDepthBound();
	if (ownStencil) {
		if (FAILED(Device->GetDepthStencilSurface(&previousDepth))) previousDepth = nullptr;
		Device->SetDepthStencilSurface(Textures.StencilSurface);
	}

	// Clear the stencil buffer.
	Device->Clear(0, nullptr, D3DCLEAR_STENCIL, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

	SetCT();

	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(RenderTarget, RenderedSurface);

	EdgesDetectionPass(Settings.Main.EdgeDetection);
	BlendingWeightsCalculationPass();
	NeighborhoodBlendingPass(chained ? chain.Output() : RenderTarget);
	if (ownStencil) Device->SetDepthStencilSurface(previousDepth);
	if (previousDepth) previousDepth->Release();

	if (chained) {
		chain.Commit();
		Device->SetRenderTarget(0, RenderTarget);
	}
	else if (RenderedSurface) Device->StretchRect(RenderTarget, NULL, RenderedSurface, NULL, D3DTEXF_LINEAR);

	renderTime = timer.LogTime("EffectRecord::Render SMAA");
}

void SMAAEffect::EdgesDetectionPass(Input input) {
    IDirect3DDevice9* Device = TheRenderManager->device;

    // Set the render target and clear both the color and the stencil buffers.
    Device->SetRenderTarget(0, Textures.SMAA_Edges_Surface);
    Device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

    // Select the technique accordingly.
    switch (input) {
    case INPUT_LUMA:
        Effect->SetTechnique(TechniqueHandle(techLuma, "LumaEdgeDetection"));
        break;
    case INPUT_COLOR:
        Effect->SetTechnique(TechniqueHandle(techColor, "ColorEdgeDetection"));
        break;
    case INPUT_DEPTH:
        Effect->SetTechnique(TechniqueHandle(techDepth, "DepthEdgeDetection"));
        break;
    case INPUT_LUMADEPTH:
        if (Settings.Main.Predication || Settings.Main.DeDither) {
            D3DXHANDLE stable = TechniqueHandle(techLumaDepthStable, "LumaDepthEdgeDetectionStable");
            Effect->SetTechnique(stable ? stable : TechniqueHandle(techLumaDepth, "LumaDepthEdgeDetection"));
        } else
            Effect->SetTechnique(TechniqueHandle(techLumaDepth, "LumaDepthEdgeDetection"));
        break;
    default:
		return;
    }

    UINT passes;
    Effect->Begin(&passes, 0);
    Effect->BeginPass(0);
	Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
    Effect->EndPass();
    Effect->End();
}


void SMAAEffect::BlendingWeightsCalculationPass() {
	IDirect3DDevice9* Device = TheRenderManager->device;

    // Set the render target and clear it.
    Device->SetRenderTarget(0, Textures.SMAA_Blend_Surface);
    Device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

    Effect->SetTechnique(TechniqueHandle(techBlend, "BlendWeightCalculation"));

	UINT passes;
	Effect->Begin(&passes, 0);
	Effect->BeginPass(0);
	Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
	Effect->EndPass();
	Effect->End();
}


void SMAAEffect::NeighborhoodBlendingPass(IDirect3DSurface9* RenderTarget) {
	IDirect3DDevice9* Device = TheRenderManager->device;

    Device->SetRenderTarget(0, RenderTarget);
    Effect->SetTechnique(TechniqueHandle(techNeighborhood, "NeighborhoodBlending"));

	UINT passes;
	Effect->Begin(&passes, 0);
	Effect->BeginPass(0);
	Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
	Effect->EndPass();
	Effect->End();
}
