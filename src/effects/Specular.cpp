#include "Specular.h"
#include "../core/DxvkCompute.h"
#include "../core/RenderRect.h"

void SpecularEffect::UpdateConstants() {
	const float rain = TheShaderManager->Effects.Rain->Constants.RainAnimator.GetValue();
	const LookStruct& dry = Settings.Exterior;
	const LookStruct& wet = Settings.Rain;
	Constants.Data.x = std::lerp(dry.SunStrength, wet.SunStrength, rain);
	Constants.Data.y = std::lerp(dry.Roughness, wet.Roughness, rain);
	Constants.Data.z = std::lerp(dry.SkyReflections, wet.SkyReflections, rain);
	Constants.Data.w = std::lerp(dry.DistanceFade, wet.DistanceFade, rain);
	Constants.EffectStrength.x = std::lerp(dry.SpecularAA, wet.SpecularAA, rain);
	Constants.EffectStrength.y = std::lerp(dry.Reflectance, wet.Reflectance, rain);
	Constants.EffectStrength.z = (float)MaxQuality::Divisor(4);
	Constants.EffectStrength.w = 0.0f;
}

void SpecularEffect::UpdateSettings() {
	auto read = [](const char* section, LookStruct& look) {
		look.SunStrength = std::clamp(TheSettingManager->GetSettingF(section, "SunStrength"), 0.0f, 4.0f);
		look.Roughness = std::clamp(TheSettingManager->GetSettingF(section, "Roughness"), 0.05f, 1.0f);
		look.SkyReflections = std::clamp(TheSettingManager->GetSettingF(section, "SkyReflections"), 0.0f, 4.0f);
		look.DistanceFade = std::clamp(TheSettingManager->GetSettingF(section, "DistanceFade"), 500.0f, 300000.0f);
		look.SpecularAA = std::clamp(TheSettingManager->GetSettingF(section, "SpecularAA"), 0.0f, 1.0f);
		look.Reflectance = std::clamp(TheSettingManager->GetSettingF(section, "Reflectance"), 0.0f, 0.2f);
	};
	read("Shaders.Specular.Exterior", Settings.Exterior);
	read("Shaders.Specular.Rain", Settings.Rain);
}

void SpecularEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SpecularData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_SpecularEffects", &Constants.EffectStrength);
}

void SpecularEffect::RegisterTextures() {
	const UINT w = MaxQuality::Reduced(TheRenderManager->width, 4), h = MaxQuality::Reduced(TheRenderManager->height, 4);
	TheTextureManager->InitTexture("TESR_SpecularNormals", &Textures.NormalsTexture, &Textures.NormalsSurface, w, h, D3DFMT_A16B16G16R16F);
	TheTextureManager->InitTexture("TESR_SpecularBuffer", &Textures.LightTexture, &Textures.LightSurface, w, h, D3DFMT_A16B16G16R16F);
}

bool SpecularEffect::ShouldRender() {
	return TheShaderManager->GameState.isExterior && !TheShaderManager->GameState.isUnderwater;
};

void SpecularEffect::BindExtras(IDirect3DDevice9* Device, bool on) {
	AmbientOcclusionEffect* ao = TheShaderManager->Effects.AmbientOcclusion;
	IDirect3DTexture9* sky = on && ao ? ao->SkyReflectionTexture() : nullptr;
	IDirect3DTexture9* sunFar = on && TheShaderManager->Effects.SunFar ? TheShaderManager->Effects.SunFar->FactorTexture() : nullptr;
	if (D3DXHANDLE handle = Effect ? ParameterHandle(paramExtra, "NVR_SpecularExtra") : NULL) {
		D3DXVECTOR4 extra(sky ? 1.0f : 0.0f, ao ? ao->SkyMinReflection : 0.1f, sunFar ? 1.0f : 0.0f, 0.0f);
		Effect->SetVector(handle, &extra);
		extraValues = extra;
	}
	else sky = sunFar = nullptr;
	extraSkyVisibility = sky;
	extraSunFar = sunFar;
	const DWORD linear[][2] = { {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
		{D3DSAMP_MAGFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MIPFILTER, D3DTEXF_NONE} };
	if (!on) { ClearPassTextures(); return; }
	SetPassTexture(9, sky);
	SetPassTexture(10, sunFar);
	if (sky) for (const auto& s : linear) TheRenderManager->SetSamplerState(9, (D3DSAMPLERSTATETYPE)s[0], s[1]);
	if (sunFar) for (const auto& s : linear) TheRenderManager->SetSamplerState(10, (D3DSAMPLERSTATETYPE)s[0], s[1]);
}

bool SpecularEffect::CanTakeBounce() {
	return Enabled && Effect && TechniqueHandle(techBounce, "SpecularBounce") && ParameterHandle(paramBounceData, "NVR_BounceData") &&
		ParameterHandle(paramBounceLayout, "NVR_BounceLayout");
}

bool SpecularEffect::RenderWithBounce(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	IDirect3DTexture9* bounce, const D3DXVECTOR4& data, const D3DXVECTOR4& layout) {
	if (!Enabled || !Effect || !ShouldRender() || !bounce) return false;
	FrameChain& chain = TheShaderManager->Chain;
	auto timer = TimeLogger();
	if (!ExtFailed && DxvkCompute::PassesOn() && chain.Owns(RenderTarget, RenderedSurface) &&
		ExtRenderBounce(Device, RenderTarget, RenderedSurface, bounce, data, layout)) {
		Device->SetRenderTarget(0, RenderTarget);
		renderTime = timer.LogTime("EffectRecord::ExtRender Specular (with bounce light)");
		static bool reportedExt = false;
		if (!reportedExt) {
			Logger::Log("UNOFFICIAL merged combine active: bounce light and specular in one pass.");
			reportedExt = true;
		}
		return true;
	}
	D3DXHANDLE technique = TechniqueHandle(techBounce, "SpecularBounce");
	D3DXHANDLE dataHandle = ParameterHandle(paramBounceData, "NVR_BounceData");
	D3DXHANDLE layoutHandle = ParameterHandle(paramBounceLayout, "NVR_BounceLayout");
	if (!technique || !dataHandle || !layoutHandle || !chain.Owns(RenderTarget, RenderedSurface)) return false;
	Effect->SetTechnique(technique);
	SetCT();
	Effect->SetVector(dataHandle, &data);
	Effect->SetVector(layoutHandle, &layout);
	UINT passes = 0;
	HRESULT result = Device->SetRenderTarget(0, chain.Output());
	if (SUCCEEDED(result)) result = Effect->Begin(&passes, 0);
	if (SUCCEEDED(result)) {
		result = Effect->BeginPass(0);
		if (SUCCEEDED(result)) {
			RebindSlotTextures();
			Device->SetTexture(8, bounce);
			RenderRect::ApplyViewport(Device);
			result = Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Effect->EndPass();
		}
		Effect->End();
	}
	Device->SetTexture(8, nullptr);
	Device->SetRenderTarget(0, RenderTarget);
	renderTime = timer.LogTime("EffectRecord::Render Specular (with bounce light)");
	if (FAILED(result)) return false;
	chain.Commit();
	static bool reported = false;
	if (!reported) {
		Logger::Log("UNOFFICIAL merged combine active: bounce light and specular in one pass.");
		reported = true;
	}
	return true;
}


static bool AllSet(IDirect3DBaseTexture9* const* textures, UINT count) {
	for (UINT i = 0; i < count; i++) if (!textures[i]) return false;
	return true;
}

static ID3D9NvrExtPipeline* SpecularPipeline(EffectRecord::ExtPass& pass, const char* spvName, UINT textureCount) {
	if (!pass.Tried) {
		pass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[NVR_EXT_MAX_BINDINGS] = {};
		for (UINT i = 0; i < textureCount; i++) {
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i; bindings[i].ViewType = NVR_EXT_VIEW_2D;
		}
		const UINT c = textureCount;
		bindings[c].Set = 2; bindings[c].Binding = 0; bindings[c].Type = NVR_EXT_BIND_UNIFORM_BUFFER; bindings[c].Slot = 0; bindings[c].ViewType = NVR_EXT_VIEW_2D;
		bindings[c + 1].Type = NVR_EXT_BIND_SAMPLER; bindings[c + 1].Slot = 0; bindings[c + 1].PushOffset = 0;
		bindings[c + 2].Type = NVR_EXT_BIND_SAMPLER; bindings[c + 2].Slot = 1; bindings[c + 2].PushOffset = 4;
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvName, bindings, textureCount + 3, sizeof(SpecularPush), true);
	}
	return pass.Pipeline;
}

static bool SpecularResources(NVR_EXT_RESOURCE_BINDING* resources, IDirect3DBaseTexture9* const* textures, UINT textureCount,
	ID3D9NvrExtBuffer* constants) {
	ID3D9NvrExtSampler* point = DxvkCompute::PointClamp();
	ID3D9NvrExtSampler* linear = DxvkCompute::LinearClamp();
	if (!point || !linear) return false;
	for (UINT i = 0; i < textureCount; i++) {
		resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i];
	}
	const UINT c = textureCount;
	resources[c].Slot = 0; resources[c].Type = NVR_EXT_BIND_UNIFORM_BUFFER; resources[c].pResource = constants;
	resources[c + 1].Slot = 0; resources[c + 1].Type = NVR_EXT_BIND_SAMPLER; resources[c + 1].pResource = point;
	resources[c + 2].Slot = 1; resources[c + 2].Type = NVR_EXT_BIND_SAMPLER; resources[c + 2].pResource = linear;
	return true;
}

const void* SpecularEffect::ConstantValue(const char* name) {
	for (UInt32 c = 0; c < FloatShaderValuesCount; c++) {
		ShaderFloatValue* constant = &FloatShaderValues[c];
		if (!constant->Name || strcmp(constant->Name, name)) continue;
		if (constant->Value == nullptr) constant->GetValueFromConstantTable();
		return constant->Value;
	}
	return nullptr;
}

IDirect3DBaseTexture9* SpecularEffect::TextureOf(const char* name) {
	for (UInt32 c = 0; c < TextureShaderValuesCount; c++) {
		ShaderTextureValue* sampler = &TextureShaderValues[c];
		if (!sampler->Name || strcmp(sampler->Name, name) || !sampler->Texture) continue;
		if (sampler->Texture->TextureRef) sampler->Texture->Texture = *sampler->Texture->TextureRef;
		if (!sampler->Texture->Texture) sampler->Texture->BindTexture(sampler->Name);
		return sampler->Texture->Texture;
	}
	return nullptr;
}

bool SpecularEffect::FillUniforms(SpecularUniforms& u) {
	memset(&u, 0, sizeof(u));
	bool found = true;
	auto copy = [&](const char* name, void* dst, size_t bytes) {
		if (const void* value = ConstantValue(name)) memcpy(dst, value, bytes);
		else found = false;
	};
	copy("TESR_ViewTransform", u.viewTransform, sizeof(u.viewTransform));
	copy("TESR_ProjectionTransform", u.projectionTransform, sizeof(u.projectionTransform));
	copy("TESR_ShadowCameraToLightTransformNear", u.shadowNear, sizeof(u.shadowNear));
	copy("TESR_ShadowCameraToLightTransformMiddle", u.shadowMiddle, sizeof(u.shadowMiddle));
	copy("TESR_ShadowCameraToLightTransformFar", u.shadowFar, sizeof(u.shadowFar));
	copy("TESR_ShadowCameraToLightTransformLod", u.shadowLod, sizeof(u.shadowLod));
	copy("TESR_ReciprocalResolution", u.reciprocalResolution, sizeof(u.reciprocalResolution));
	copy("TESR_SpecularData", u.specularData, sizeof(u.specularData));
	copy("TESR_SpecularEffects", u.specularEffects, sizeof(u.specularEffects));
	copy("TESR_SunDirection", u.sunDirection, sizeof(u.sunDirection));
	copy("TESR_SunColor", u.sunColor, sizeof(u.sunColor));
	copy("TESR_SkyColor", u.skyColor, sizeof(u.skyColor));
	copy("TESR_HorizonColor", u.horizonColor, sizeof(u.horizonColor));
	copy("TESR_WaterSettings", u.waterSettings, sizeof(u.waterSettings));
	copy("TESR_ShadowFade", u.shadowFade, sizeof(u.shadowFade));
	copy("TESR_DepthConstants", u.depthConstants, sizeof(u.depthConstants));
	copy("TESR_CameraData", u.cameraData, sizeof(u.cameraData));
	copy("TESR_CameraPosition", u.cameraPosition, sizeof(u.cameraPosition));
	copy("TESR_SmoothedSunDir", u.smoothedSunDir, sizeof(u.smoothedSunDir));
	copy("TESR_ShadowFormatData", u.shadowFormatData, sizeof(u.shadowFormatData));
	copy("TESR_ShadowBlur", u.shadowBlur, sizeof(u.shadowBlur));
	copy("TESR_ShadowNearCenter", u.shadowNearCenter, sizeof(u.shadowNearCenter));
	copy("TESR_ShadowMiddleCenter", u.shadowMiddleCenter, sizeof(u.shadowMiddleCenter));
	copy("TESR_ShadowFarCenter", u.shadowFarCenter, sizeof(u.shadowFarCenter));
	copy("TESR_ShadowLodCenter", u.shadowLodCenter, sizeof(u.shadowLodCenter));
	if (!found) {
		static bool reported = false;
		if (!reported) {
			Logger::Log("UNOFFICIAL NVR DXVK: Specular's constants are not all there; its D3D9 passes are used.");
			reported = true;
		}
	}
	return found;
}

bool SpecularEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex,
	bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (ClearRenderTarget || techniqueIndex > 2 || !DxvkCompute::Available()) return false;
	SpecularUniforms uniforms;
	if (!FillUniforms(uniforms)) return false;
	SpecularPush push = {};

	if (techniqueIndex == 0) {
		IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture, TextureOf("TESR_DepthBuffer"), TextureOf("TESR_NormalsBuffer"),
			TextureOf("TESR_PointShadowBuffer"), TextureOf("TESR_DepthBufferViewModel"), TextureOf("TESR_SpecularBuffer") };
		const UINT count = _countof(textures);
		if (!AllSet(textures, count)) return false;
		ID3D9NvrExtBuffer* constants = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
		ID3D9NvrExtPipeline* pipeline = SpecularPipeline(extAdd, "Specular", count);
		NVR_EXT_RESOURCE_BINDING resources[count + 3] = {};
		if (!constants || !pipeline || !SpecularResources(resources, textures, count, constants)) return false;
		return ExtFullScreen(Device, RenderTarget, RenderedSurface, SourceBuffer, pipeline, resources, count + 3, &push, sizeof(push), true);
	}

	if (techniqueIndex == 1) {
		IDirect3DBaseTexture9* textures[] = { TextureOf("TESR_DepthBuffer"), TextureOf("TESR_NormalsBuffer") };
		const UINT count = _countof(textures);
		IDirect3DSurface9* target = Textures.NormalsSurface;
		if (!AllSet(textures, count) || !target) return false;
		ID3D9NvrExtBuffer* constants = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
		ID3D9NvrExtPipeline* pipeline = SpecularPipeline(extPrep, "SpecularPrep", count);
		NVR_EXT_RESOURCE_BINDING resources[count + 3] = {};
		if (!constants || !pipeline || !SpecularResources(resources, textures, count, constants)) return false;
		return ExtDrawTo(pipeline, &target, 1, resources, count + 3, &push, sizeof(push), nullptr, ExtUv::ScreenQuad);
	}

	IDirect3DBaseTexture9* skyVisibility = extraSkyVisibility ? extraSkyVisibility : TextureOf("TESR_DepthBuffer");
	IDirect3DBaseTexture9* sunFar = extraSunFar ? extraSunFar : TextureOf("TESR_DepthBuffer");
	IDirect3DBaseTexture9* textures[] = { TextureOf("TESR_SpecularNormals"), TextureOf("TESR_ShadowAtlas"), skyVisibility, sunFar };
	const UINT count = _countof(textures);
	IDirect3DSurface9* target = Textures.LightSurface;
	if (!AllSet(textures, count) || !target) return false;
	memcpy(uniforms.specularExtra, &extraValues, sizeof(uniforms.specularExtra));
	ID3D9NvrExtBuffer* constants = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	ID3D9NvrExtPipeline* pipeline = SpecularPipeline(extLight, "SpecularLow", count);
	NVR_EXT_RESOURCE_BINDING resources[count + 3] = {};
	if (!constants || !pipeline || !SpecularResources(resources, textures, count, constants)) return false;
	return ExtDrawTo(pipeline, &target, 1, resources, count + 3, &push, sizeof(push), nullptr, ExtUv::ScreenQuad);
}

bool SpecularEffect::ExtRenderBounce(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
	IDirect3DBaseTexture9* bounce, const D3DXVECTOR4& data, const D3DXVECTOR4& layout) {
	if (!DxvkCompute::Available() || !bounce) return false;
	SpecularUniforms uniforms;
	if (!FillUniforms(uniforms)) return false;
	memcpy(uniforms.bounceData, &data, sizeof(uniforms.bounceData));
	memcpy(uniforms.bounceLayout, &layout, sizeof(uniforms.bounceLayout));
	IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture, TextureOf("TESR_DepthBuffer"), bounce, TextureOf("TESR_NormalsBuffer"),
		TextureOf("TESR_DepthBufferViewModel"), TextureOf("TESR_SpecularBuffer"), TextureOf("TESR_PointShadowBuffer") };
	const UINT count = _countof(textures);
	if (!AllSet(textures, count)) return false;
	ID3D9NvrExtBuffer* constants = ExtUniforms(extUniforms, &uniforms, sizeof(uniforms));
	ID3D9NvrExtPipeline* pipeline = SpecularPipeline(extBounce, "SpecularBounce", count);
	NVR_EXT_RESOURCE_BINDING resources[count + 3] = {};
	SpecularPush push = {};
	if (!constants || !pipeline || !SpecularResources(resources, textures, count, constants)) return false;
	return ExtFullScreen(Device, RenderTarget, RenderedSurface, nullptr, pipeline, resources, count + 3, &push, sizeof(push), false);
}
