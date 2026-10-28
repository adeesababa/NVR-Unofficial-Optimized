#include "BloomLegacy.h"
#include "../core/DxvkCompute.h"

void BloomLegacyEffect::UpdateConstants() {
	ValuesStruct* category = &Settings.Main;
	if (!TheShaderManager->GameState.isExterior) category = &Settings.Interiors;

	Constants.Data.x = category->Luminance;
	Constants.Data.y = category->MiddleGray;
	Constants.Data.z = category->WhiteCutOff;
	Constants.Values.x = category->BloomIntensity;
	Constants.Values.y = category->OriginalIntensity;
	Constants.Values.z = category->BloomSaturation;
	Constants.Values.w = category->OriginalSaturation;
}

void BloomLegacyEffect::UpdateSettings() {
	Settings.Main.Luminance = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Exteriors", "Luminance");
	Settings.Main.MiddleGray = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Exteriors", "MiddleGray");
	Settings.Main.WhiteCutOff = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Exteriors", "WhiteCutOff");
	Settings.Main.BloomIntensity = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Exteriors", "BloomIntensity");
	Settings.Main.OriginalIntensity = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Exteriors", "OriginalIntensity");
	Settings.Main.BloomSaturation = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Exteriors", "BloomSaturation");
	Settings.Main.OriginalSaturation = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Exteriors", "OriginalSaturation");

	Settings.Interiors.Luminance = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Interiors", "Luminance");
	Settings.Interiors.MiddleGray = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Interiors", "MiddleGray");
	Settings.Interiors.WhiteCutOff = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Interiors", "WhiteCutOff");
	Settings.Interiors.BloomIntensity = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Interiors", "BloomIntensity");
	Settings.Interiors.OriginalIntensity = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Interiors", "OriginalIntensity");
	Settings.Interiors.BloomSaturation = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Interiors", "BloomSaturation");
	Settings.Interiors.OriginalSaturation = TheSettingManager->GetSettingF("Shaders.BloomLegacy.Interiors", "OriginalSaturation");
}

bool BloomLegacyEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	if (!SourceBuffer || SourceBuffer == RenderTarget || SourceBuffer != TheTextureManager->SourceSurface || !TheTextureManager->SourceTexture) return false;

	struct BloomPush { D3DXVECTOR4 bloomData; UInt32 samplerIndex; UInt32 pad[3]; };
	struct BlurPush { D3DXVECTOR4 reciprocalResolution; D3DXVECTOR2 offsetMask; UInt32 samplerIndex; UInt32 pad; };
	struct CombinePush { D3DXVECTOR4 bloomValues; UInt32 samplerIndex; UInt32 pad[3]; };

	auto make = [](ExtPass& pass, const char* spvName, UINT textureCount, UINT samplerOffset, UINT pushSize) {
		if (pass.Tried) return;
		pass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[3] = {};
		for (UINT i = 0; i < textureCount; i++) {
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i; bindings[i].ViewType = NVR_EXT_VIEW_2D;
		}
		bindings[textureCount].Type = NVR_EXT_BIND_SAMPLER; bindings[textureCount].Slot = 0; bindings[textureCount].PushOffset = samplerOffset;
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvName, bindings, textureCount + 1, pushSize, true);
	};
	make(extBloom, "BloomLegacyBloom", 1, 16, sizeof(BloomPush));
	make(extBlur, "BloomLegacyBlur", 1, 24, sizeof(BlurPush));
	make(extCombine, "BloomLegacyCombine", 2, 16, sizeof(CombinePush));
	ID3D9NvrExtSampler* sampler = DxvkCompute::LinearClamp();
	if (!extBloom.Pipeline || !extBlur.Pipeline || !extCombine.Pipeline || !sampler) return false;

	auto draw = [&](ExtPass& pass, IDirect3DBaseTexture9* const* textures, UINT textureCount, IDirect3DSurface9* source, const void* push, UINT pushSize, bool lastPass) {
		NVR_EXT_RESOURCE_BINDING resources[3] = {};
		for (UINT i = 0; i < textureCount; i++) { resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i]; }
		resources[textureCount].Slot = 0; resources[textureCount].Type = NVR_EXT_BIND_SAMPLER; resources[textureCount].pResource = sampler;
		return ExtFullScreen(Device, RenderTarget, TheTextureManager->RenderedSurface, source, pass.Pipeline, resources, textureCount + 1, push, pushSize, lastPass);
	};

	BloomPush bloom = {};
	bloom.bloomData = Constants.Data;
	BlurPush blur = {};
	blur.reciprocalResolution = TheShaderManager->ShaderConst.ReciprocalResolution;
	CombinePush combine = {};
	combine.bloomValues = Constants.Values;

	IDirect3DBaseTexture9* image = TheTextureManager->RenderedTexture;
	if (!draw(extBloom, &image, 1, SourceBuffer, &bloom, sizeof(bloom), false)) return false;

	const D3DXVECTOR2 offsetMasks[2] = { D3DXVECTOR2(1.0f, 0.0f), D3DXVECTOR2(0.0f, 1.0f) };
	for (UINT i = 0; i < 2; i++) {
		blur.offsetMask = offsetMasks[i];
		image = TheTextureManager->RenderedTexture;
		if (!draw(extBlur, &image, 1, nullptr, &blur, sizeof(blur), false)) return false;
	}

	IDirect3DBaseTexture9* combineTextures[2] = { TheTextureManager->RenderedTexture, TheTextureManager->SourceTexture };
	return draw(extCombine, combineTextures, 2, nullptr, &combine, sizeof(combine), true);
}

void BloomLegacyEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_BloomLegacyData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_BloomLegacyValues", &Constants.Values);
}