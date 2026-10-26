#include "DepthOfField.h"
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-25 (NVR DXVK)

void DepthOfFieldEffect::UpdateConstants() {
	TheShaderManager->avglumaRequired = true;

	ValuesStruct* category = &Settings.FirstPerson;
	if (TheCameraManager->IsVanity())
		category = &Settings.VanityView;
	else if (!TheCameraManager->IsFirstPerson())
		category = &Settings.ThirdPerson;

	int Mode = category->Mode;
	int dofActive = category->Enabled;

	// disable based on settings/context
	if ((Mode == 1 && (TheShaderManager->GameState.isDialog || TheShaderManager->GameState.isPersuasion)) ||
		(Mode == 2 && (!TheShaderManager->GameState.isDialog)) ||
		(Mode == 3 && (!TheShaderManager->GameState.isPersuasion)) ||
		(Mode == 4 && (!TheShaderManager->GameState.isDialog || !TheShaderManager->GameState.isPersuasion))) dofActive = 0;

	Constants.Enabled = dofActive;

	Constants.Blur.x = category->DistantBlur;
	Constants.Blur.y = category->DistantBlurStartRange;
	Constants.Blur.z = category->DistantBlurEndRange;
	Constants.Blur.w = category->BaseBlurRadius;
	Constants.Data.x = category->focusDistance * dofActive;
	Constants.Data.y = category->Radius;
	Constants.Data.z = category->DiameterRange;
	Constants.Data.w = category->NearBlurCutOff;

}

void DepthOfFieldEffect::UpdateSettings() {
	Settings.FirstPerson.DistantBlur = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "DistantBlur");
	Settings.FirstPerson.DistantBlurStartRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "DistantBlurStartRange");
	Settings.FirstPerson.DistantBlurEndRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "DistantBlurEndRange");
	Settings.FirstPerson.BaseBlurRadius = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "BaseBlurRadius");
	Settings.FirstPerson.focusDistance = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "BlurFallOff");
	Settings.FirstPerson.Radius = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "Radius");
	Settings.FirstPerson.DiameterRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "DiameterRange");
	Settings.FirstPerson.NearBlurCutOff = TheSettingManager->GetSettingF("Shaders.DepthOfField.FirstPersonView", "NearBlurCutOff");
	Settings.FirstPerson.Mode = TheSettingManager->GetSettingI("Shaders.DepthOfField.FirstPersonView", "Mode");
	Settings.FirstPerson.Enabled = TheSettingManager->GetSettingI("Shaders.DepthOfField.FirstPersonView", "Enabled");

	Settings.ThirdPerson.DistantBlur = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "DistantBlur");
	Settings.ThirdPerson.DistantBlurStartRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "DistantBlurStartRange");
	Settings.ThirdPerson.DistantBlurEndRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "DistantBlurEndRange");
	Settings.ThirdPerson.BaseBlurRadius = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "BaseBlurRadius");
	Settings.ThirdPerson.focusDistance = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "BlurFallOff");
	Settings.ThirdPerson.Radius = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "Radius");
	Settings.ThirdPerson.DiameterRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "DiameterRange");
	Settings.ThirdPerson.NearBlurCutOff = TheSettingManager->GetSettingF("Shaders.DepthOfField.ThirdPersonView", "NearBlurCutOff");
	Settings.ThirdPerson.Mode = TheSettingManager->GetSettingI("Shaders.DepthOfField.ThirdPersonView", "Mode");
	Settings.ThirdPerson.Enabled = TheSettingManager->GetSettingI("Shaders.DepthOfField.ThirdPersonView", "Enabled");

	Settings.VanityView.DistantBlur = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "DistantBlur");
	Settings.VanityView.DistantBlurStartRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "DistantBlurStartRange");
	Settings.VanityView.DistantBlurEndRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "DistantBlurEndRange");
	Settings.VanityView.BaseBlurRadius = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "BaseBlurRadius");
	Settings.VanityView.focusDistance = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "BlurFallOff");
	Settings.VanityView.Radius = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "Radius");
	Settings.VanityView.DiameterRange = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "DiameterRange");
	Settings.VanityView.NearBlurCutOff = TheSettingManager->GetSettingF("Shaders.DepthOfField.VanityView", "NearBlurCutOff");
	Settings.VanityView.Mode = TheSettingManager->GetSettingI("Shaders.DepthOfField.VanityView", "Mode");
	Settings.VanityView.Enabled = TheSettingManager->GetSettingI("Shaders.DepthOfField.VanityView", "Enabled");
}

void DepthOfFieldEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_DepthOfFieldBlur", &Constants.Blur);
	TheShaderManager->RegisterConstant("TESR_DepthOfFieldData", &Constants.Data);
}

bool DepthOfFieldEffect::ShouldRender() {
	if (Constants.Enabled) return true;
	if (!Constants.Blur.x) return false;
	// UNOFFICIAL: distant blur only. Its pass (DistantOnly) leaves every pixel nearer than DistantBlurStartRange unchanged,
	// and nothing is drawn beyond the camera's far plane, so when the far plane is nearer than the blur start (interiors,
	// 30000 by default) the pass would only copy the image: skip it (~0.16 ms at 1440p).
	const float farZ = TheRenderManager->CameraData.y;
	const bool skip = farZ > 0.0f && farZ <= Constants.Blur.y;
	static int logged = -1;
	if (GpuTimer::Enabled && logged != (int)skip) {
		logged = skip;
		Logger::Log("UNOFFICIAL distant blur %s: camera far plane %.0f, blur starts at %.0f", skip ? "skipped (nothing that far)" : "runs", farZ, Constants.Blur.y);
	}
	return !skip;
};

// The push block of every DepthOfField pass (the GLSL declares the same one: DepthOfField.frag for technique 1, DepthOfField.glsl for
// technique 0). The matrices are the D3DXMATRIX memory the D3D9 constants come from; the sampler's heap index is at 208.
struct DofPush {
	D3DXMATRIX viewTransform, projectionTransform;
	D3DXVECTOR4 reciprocalResolution, blur, data, cameraPosition, cameraData;
	UInt32 samplerIndex;
	float useBokeh;	// BokehBlur's useBokeh (1 or 0); the other passes ignore it
	UInt32 pad[2];
};

// UNOFFICIAL 2026-10-25 (NVR DXVK): the two techniques as Vulkan passes. DistantBlurOnly (technique 1, the common gameplay case) is one
// pass: DepthOfField.frag is DistantOnly line for line. AutofocusBokeh (technique 0) is six passes through the frame chain, as Render
// runs them: DoF (blur amounts), halfRes (the quadrant mosaic), BokehBlur twice with bokeh and once without, Combine (last, may write the
// game target). Each GLSL file is its .fx.hlsl pass line for line. Only the first pass copies the image before the effect into
// TESR_SourceBuffer (ExtFullScreen copies it per call); the others get no SourceBuffer, as the D3D9 passes read it unchanged.
bool DepthOfFieldEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (ClearRenderTarget) return false;
	DofPush push = {};
	push.viewTransform = TheRenderManager->viewMatrix;
	push.projectionTransform = TheRenderManager->projMatrix;
	push.reciprocalResolution = TheShaderManager->ShaderConst.ReciprocalResolution;
	push.blur = Constants.Blur;
	push.data = Constants.Data;
	push.cameraPosition = TheRenderManager->CameraPosition;
	push.cameraData = TheRenderManager->CameraData;

	IDirect3DBaseTexture9* depth = TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr;
	if (techniqueIndex == 1) {
		IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture, depth };
		return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "DepthOfField", textures, _countof(textures), &push, sizeof(push), 208);
	}
	if (techniqueIndex != 0) return false;

	// Technique 0: the chain must be the one this effect renders into, and TESR_SourceBuffer the image before it (as Render sets it up).
	if (!SourceBuffer || SourceBuffer == RenderTarget || SourceBuffer != TheTextureManager->SourceSurface || !TheTextureManager->SourceTexture) return false;
	FrameChain& chain = TheShaderManager->Chain;
	if (!chain.Owns(RenderTarget, RenderedSurface)) return false;
	IDirect3DBaseTexture9* avgLuma = TheShaderManager->Effects.AvgLuma ? TheShaderManager->Effects.AvgLuma->Textures.AvgLumaTexture : nullptr;
	if (!depth || !avgLuma) return false;

	// The four pipelines are made before anything is drawn, so a refused one leaves the frame as the D3D9 technique finds it.
	// Textures bind in order (set 1, binding i); the LINEAR/CLAMP sampler (slot 0) takes its heap index from the push block at 208.
	auto make = [](ExtPass& pass, const char* spvName, UINT textureCount) {
		if (pass.Tried) return;
		pass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[3] = {};
		for (UINT i = 0; i < textureCount; i++) {
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i; bindings[i].ViewType = NVR_EXT_VIEW_2D;
		}
		bindings[textureCount].Type = NVR_EXT_BIND_SAMPLER; bindings[textureCount].Slot = 0; bindings[textureCount].PushOffset = 208;
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvName, bindings, textureCount + 1, sizeof(DofPush), true);
	};
	make(extFocus, "DepthOfFieldFocus", 2);
	make(extMosaic, "DepthOfFieldMosaic", 2);
	make(extBokeh, "DepthOfFieldBokeh", 1);
	make(extCombine, "DepthOfFieldCombine", 2);
	ID3D9NvrExtSampler* sampler = DxvkCompute::LinearClamp();
	if (!extFocus.Pipeline || !extMosaic.Pipeline || !extBokeh.Pipeline || !extCombine.Pipeline || !sampler) return false;

	// One pass through the chain (as ExtFullScreen does it): the textures in binding order, then the sampler at slot 0.
	auto draw = [&](ExtPass& pass, IDirect3DBaseTexture9* const* textures, UINT textureCount, IDirect3DSurface9* source, bool lastPass, float useBokeh) {
		push.useBokeh = useBokeh;
		NVR_EXT_RESOURCE_BINDING resources[3] = {};
		for (UINT i = 0; i < textureCount; i++) { resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i]; }
		resources[textureCount].Slot = 0; resources[textureCount].Type = NVR_EXT_BIND_SAMPLER; resources[textureCount].pResource = sampler;
		return ExtFullScreen(Device, RenderTarget, TheTextureManager->RenderedSurface, source, pass.Pipeline, resources, textureCount + 1, &push, sizeof(push), lastPass);
	};

	// The DoF pass reads the depth and the focus and writes the blur amounts into the chain. Nothing is drawn yet if it fails,
	// so the D3D9 technique runs as before.
	IDirect3DBaseTexture9* focusTextures[] = { depth, avgLuma };
	if (!draw(extFocus, focusTextures, 2, SourceBuffer, false, 0.0f)) return false;

	// From here on the chain holds an image: a pass that fails leaves it as the last pass drew it (ExtFullScreen logs the failure and
	// the effect's Vulkan path is off from the next frame). The D3D9 passes cannot pick up from there, so the frame keeps what is there.
	// halfRes: TESR_RenderedBuffer is the blur amounts now, TESR_SourceBuffer the image before the effect.
	IDirect3DBaseTexture9* mosaicTextures[] = { TheTextureManager->RenderedTexture, TheTextureManager->SourceTexture };
	if (!draw(extMosaic, mosaicTextures, 2, nullptr, false, 0.0f)) return true;
	// BokehBlur twice with bokeh, then once as a plain blur; each reads the chain's current image.
	const float bokehPasses[] = { 1.0f, 1.0f, 0.0f };
	for (float useBokeh : bokehPasses) {
		IDirect3DBaseTexture9* bokehTextures[] = { TheTextureManager->RenderedTexture };
		if (!draw(extBokeh, bokehTextures, 1, nullptr, false, useBokeh)) return true;
	}
	// Combine writes the frame, straight into the game target when the chain predicts this effect last.
	IDirect3DBaseTexture9* combineTextures[] = { TheTextureManager->RenderedTexture, TheTextureManager->SourceTexture };
	draw(extCombine, combineTextures, 2, nullptr, true, 0.0f);
	return true;
}
