#include <algorithm>
#include <cstddef>

#include "Bloom.h"
#include "../core/DxvkCompute.h"	// UNOFFICIAL 2026-10-25 (NVR DXVK)
#include "../core/RenderRect.h"	// UNOFFICIAL 2026-10-27 RenderScale v2

void BloomEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_BloomData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_BloomExtraData", &Constants.ExtraData);
	TheShaderManager->RegisterConstant("TESR_BloomResolution", &Constants.Resolution);
	TheShaderManager->RegisterConstant("TESR_BloomGlow", &Constants.Glow);
	TheShaderManager->RegisterConstant("TESR_BloomGlowExtra", &Constants.GlowExtra);
};

void BloomEffect::RegisterTextures() {
	std::string bufferName = "TESR_BloomBuffer";
	int width = TheRenderManager->width;
	int height = TheRenderManager->height;
	int multiple = 1;
	for (int i = 0; i < MaxPasses; i++) {
		std::string name = (i>0?bufferName + std::to_string(multiple):bufferName);
		multiple *= 2;

		width /= 2;
		height /= 2;

		//Logger::Log("creating %s multiple %i %i:%i", name, multiple, TheRenderManager->width / multiple, TheRenderManager->height / multiple);
		Settings.Resolution[i] = D3DXVECTOR4(
			width, 
			height, 
			1.0f / (float)(width), 
			1.0f / (float)(height)
		);

		TheTextureManager->InitTexture(name.c_str(), &Textures.BloomTexture[i], &Textures.BloomSurface[i], Settings.Resolution[i].x, Settings.Resolution[i].y, D3DFMT_A16B16G16R16F);
		TheShaderManager->CreateFrameVertex(width, height, &Textures.BloomVertexBuffer[i]);
	}
};

void BloomEffect::UpdateSettings() {
	Settings.Main.Passes = TheSettingManager->GetSettingF("Shaders.Bloom.Main", "Passes");
	Settings.Main.PassBlending = TheSettingManager->GetSettingF("Shaders.Bloom.Main", "PassBlending");

	Settings.Night.Passes = TheSettingManager->GetSettingF("Shaders.Bloom.Night", "Passes");
	Settings.Night.PassBlending = TheSettingManager->GetSettingF("Shaders.Bloom.Night", "PassBlending");

	Settings.Interiors.Passes = TheSettingManager->GetSettingF("Shaders.Bloom.Interiors", "Passes");
	Settings.Interiors.PassBlending = TheSettingManager->GetSettingF("Shaders.Bloom.Interiors", "PassBlending");

	// UNOFFICIAL (a key missing from an older settings file reads 0: as before).
	const char* sections[3] = { "Shaders.Bloom.Main", "Shaders.Bloom.Night", "Shaders.Bloom.Interiors" };
	BloomSettings* periods[3] = { &Settings.Main, &Settings.Night, &Settings.Interiors };
	for (int i = 0; i < 3; i++) {
		periods[i]->Spread = std::clamp(TheSettingManager->GetSettingF(sections[i], "Spread"), -0.75f, 2.0f);
		periods[i]->GlowBoost = std::clamp(TheSettingManager->GetSettingF(sections[i], "GlowBoost"), 0.0f, 20.0f);
	}
	Settings.GlowLightLevel = TheSettingManager->GetSettingF("Shaders.Bloom.Main", "GlowLightLevel");
	Settings.GlowLightLevel = Settings.GlowLightLevel > 0.0f ? std::clamp(Settings.GlowLightLevel, 0.25f, 50.0f) : 1.5f;
	Settings.LinearLight = TheSettingManager->GetSettingI("Shaders.Bloom.Main", "LinearLight") != 0;
	Settings.AntiFlicker = TheSettingManager->GetSettingI("Shaders.Bloom.Main", "AntiFlicker") != 0;
};




// UNOFFICIAL GlowBoost: the brightness (linear) of a white surface in the scene's own light: the sky's ambient plus the sun's
// or moon's light outdoors, the cell's ambient plus its directional light indoors (as BounceLightEffect::SpillThreshold).
static float BloomSceneLight() {
	D3DXVECTOR4 ambient = TheShaderManager->ShaderConst.sunAmbient, direct = TheShaderManager->ShaderConst.sunColor;
	if (!TheShaderManager->GameState.isExterior && Player && Player->parentCell && Player->parentCell->IsInterior() &&
		Player->parentCell->lighting) {
		ambient = Player->parentCell->lighting->ambient.toD3DXVECTOR4();
		direct = Player->parentCell->lighting->directional.toD3DXVECTOR4();
	}
	auto linear = [](float c) { c = (std::max)(c, 0.0f); return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); };
	return 0.2126f * linear(ambient.x + direct.x) + 0.7152f * linear(ambient.y + direct.y) + 0.0722f * linear(ambient.z + direct.z);
}

void BloomEffect::UpdateConstants() {
	// .x and .y updated dynamically to reflect filterRadius.
	Constants.Data.x = 0.005f;
	Constants.Data.y = 0.005f * ((float)TheRenderManager->width / TheRenderManager->height);  // Scaled for non-square resolutions.
	Constants.Data.z = std::clamp(TheShaderManager->GetTransitionValue(Settings.Main.PassBlending, Settings.Night.PassBlending, Settings.Interiors.PassBlending), 0.0f, 1.0f);
	Constants.Data.w = 1.0f / std::clamp(TheShaderManager->GetTransitionValue(Settings.Main.Passes, Settings.Night.Passes, Settings.Interiors.Passes), 2.0f, 8.0f);

	// UNOFFICIAL Spread: the last upsample divides by the sum of the levels' weights (exactly .w as before at weight 1).
	const float spread = 1.0f + std::clamp(TheShaderManager->GetTransitionValue(Settings.Main.Spread, Settings.Night.Spread, Settings.Interiors.Spread), -0.75f, 2.0f);
	Constants.Glow.x = spread;
	Constants.Glow.y = Constants.Data.w;
	if (spread != 1.0f) {
		const int passes = std::clamp((int)TheShaderManager->GetTransitionValue(Settings.Main.Passes, Settings.Night.Passes, Settings.Interiors.Passes), 2, 8);
		float total = 0.0f, weight = 1.0f;
		for (int i = 0; i < passes; i++) { total += weight; weight *= spread; }
		Constants.Glow.y = 1.0f / total;
	}
	Constants.Glow.z = (std::max)(0.0f, TheShaderManager->GetTransitionValue(Settings.Main.GlowBoost, Settings.Night.GlowBoost, Settings.Interiors.GlowBoost));
	Constants.Glow.w = Settings.GlowLightLevel * BloomSceneLight();
	Constants.GlowExtra.x = Settings.AntiFlicker ? 1.0f : 0.0f;
	Constants.GlowExtra.y = Settings.LinearLight ? 1.0f : 0.0f;

	// Flag for tonemapping.
	Constants.ExtraData.x = Enabled && (!TheShaderManager->GameState.OverlayIsOn);
};


bool BloomEffect::SwitchEffect() {
	EffectRecord::SwitchEffect();

	if (!Enabled) {
		// Clear the bloom buffer.
		IDirect3DDevice9* Device = TheRenderManager->device;
		Device->ColorFill(Textures.BloomSurface[0], NULL, D3DCOLOR_ARGB(255, 0, 0, 0));
	}

	Constants.ExtraData.x = Enabled;

	return Enabled;
}

/*
* Renders a single bloom pass to current render target, using the passed in technique. Clears target if specified.
*/
void BloomEffect::RenderPass(IDirect3DDevice9* Device, UINT techniqueIndex, bool ClearRenderTarget) {
	try {
		D3DXHANDLE technique = Effect->GetTechnique(techniqueIndex);
		Effect->SetTechnique(technique);
		SetCT(); // update the constant table
		UINT Passes;
		Effect->Begin(&Passes, NULL);
		for (UINT p = 0; p < Passes; p++) {
			if (ClearRenderTarget) Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0L);
			Effect->BeginPass(p);
			Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Effect->EndPass();
		}
		Effect->End();
	}
	catch (const std::exception& e) {
		Logger::Log("Error during rendering of effect %s: %s", Name, e.what());
	}
}

// UNOFFICIAL 2026-10-25 (NVR DXVK): the push block of every Bloom*.frag (Bloom.glsl), the same layout.
struct BloomExtPush {
	D3DXVECTOR4	data;			// TESR_BloomData
	D3DXVECTOR4	resolution;		// TESR_BloomResolution
	D3DXVECTOR4	glow;			// TESR_BloomGlow
	D3DXVECTOR4	glowExtra;		// TESR_BloomGlowExtra
	UInt32		samplerIndex;	// LINEAR, CLAMP: the extension writes the sampler heap index here (PushOffset)
	UInt32		pad[3];
};

/*
* UNOFFICIAL 2026-10-25 (NVR DXVK): the chain as Vulkan passes (src\hlsl\NewVegas\Compute\Bloom*.frag). The same passes, the same
* constants (the Data.xy and Resolution changes included), each one a pass into its own target (ExtDrawTo; the frame chain is not
* touched). The D3D9 upsample reads a level and writes that same level (the sum over it), which a Vulkan pass cannot do: the
* downsample levels below the top one go to extLevels, and the upsamples write the chain's own levels, so nothing is read and
* written at once. The top level is the downsample's in both paths. Only BloomTexture[0] is read outside the chain (Lens, Rain,
* Precipitations, the game's HDR blend shaders, Debug). Returns false when a pass is refused; the D3D9 chain then does the frame
* from the same image.
*/
bool BloomEffect::RenderBloomExt(IDirect3DDevice9* Device, int passes) {
	// The pipelines are made on first use. Bindings: the textures in order (set 1, binding i, slot i), then the sampler.
	auto make = [](ExtPass& pass, const char* spvName, UINT textureCount) {
		if (pass.Tried) return;
		pass.Tried = true;
		NVR_EXT_BINDING_DESC bindings[3] = {};
		for (UINT i = 0; i < textureCount; i++) {
			bindings[i].Set = 1; bindings[i].Binding = i; bindings[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; bindings[i].Slot = i; bindings[i].ViewType = NVR_EXT_VIEW_2D;
		}
		bindings[textureCount].Type = NVR_EXT_BIND_SAMPLER; bindings[textureCount].Slot = 0; bindings[textureCount].PushOffset = (UINT)offsetof(BloomExtPush, samplerIndex);
		pass.Pipeline = DxvkCompute::CreateGraphicsPipeline(spvName, bindings, textureCount + 1, sizeof(BloomExtPush), true);
	};
	make(extDownFirst, "BloomDownFirst", 1);
	make(extDown, "BloomDown", 1);
	make(extUp, "BloomUp", 2);
	make(extUpLast, "BloomUpLast", 2);
	ID3D9NvrExtSampler* sampler = DxvkCompute::LinearClamp();
	if (!extDownFirst.Pipeline || !extDown.Pipeline || !extUp.Pipeline || !extUpLast.Pipeline || !sampler) return false;

	// The downsample levels below the top: the chain's own format and sizes, made once when this path first runs.
	if (!extLevelsTried) {
		extLevelsTried = true;
		for (int i = 0; i < MaxPasses - 1; i++) {
			std::string name = "NVR_BloomLevel" + std::to_string(i);
			TheTextureManager->InitTexture(name.c_str(), &extLevels.Texture[i], &extLevels.Surface[i], (int)Settings.Resolution[i].x, (int)Settings.Resolution[i].y, D3DFMT_A16B16G16R16F);
		}
	}
	for (int i = 0; i < passes - 1; i++) {
		if (!extLevels.Texture[i] || !extLevels.Surface[i]) return false;
	}

	// Level i as the chain holds it between its passes: the downsample's own texture below the top, the chain's at the top.
	auto levelTexture = [&](int i) -> IDirect3DBaseTexture9* { return i < passes - 1 ? extLevels.Texture[i] : Textures.BloomTexture[i]; };
	auto levelSurface = [&](int i) -> IDirect3DSurface9* { return i < passes - 1 ? extLevels.Surface[i] : Textures.BloomSurface[i]; };

	// One pass into one target: the textures in binding order, then the LINEAR/CLAMP sampler.
	auto draw = [&](ExtPass& pass, IDirect3DBaseTexture9* const* textures, UINT textureCount, IDirect3DSurface9* target, const BloomExtPush& push) {
		NVR_EXT_RESOURCE_BINDING resources[3] = {};
		for (UINT i = 0; i < textureCount; i++) { resources[i].Slot = i; resources[i].Type = NVR_EXT_BIND_SAMPLED_IMAGE; resources[i].pResource = textures[i]; }
		resources[textureCount].Slot = 0; resources[textureCount].Type = NVR_EXT_BIND_SAMPLER; resources[textureCount].pResource = sampler;
		return ExtDrawTo(pass.Pipeline, &target, 1, resources, textureCount + 1, &push, sizeof(push), nullptr, ExtUv::TexelCentre);	// D3D9: a quad per level (BloomVertexBuffer)
	};
	auto pushConstants = [this]() {
		BloomExtPush push = {};
		push.data = Constants.Data;
		push.resolution = Constants.Resolution;
		push.glow = Constants.Glow;
		push.glowExtra = Constants.GlowExtra;
		return push;
	};

	// Downsample: level 0 from the image, each further level from the one above (techniques 0 to passes - 1 of the D3D9 chain).
	for (int i = 0; i < passes; i++) {
		Constants.Resolution = Settings.Resolution[i];
		IDirect3DBaseTexture9* textures[1] = { i == 0 ? TheTextureManager->RenderedTexture : levelTexture(i - 1) };
		if (!draw(i == 0 ? extDownFirst : extDown, textures, 1, levelSurface(i), pushConstants())) return false;
	}

	// Upsample from the top down: level i gets the level above it (the chain's BloomTexture[i + 1]) with its own downsample added.
	// The last one (i = 0, technique 14 of the D3D9 chain) is the frame's bloom.
	for (int i = passes - 2; i >= 0; i--) {
		if (Constants.Data.z > 0.0f) {
			Constants.Data.x = Settings.Resolution[i + 1].z; // Pixel size x axis of the upsampled texture.
			Constants.Data.y = Settings.Resolution[i + 1].w; // Pixel size y axis of the upsampled texture.
		}
		Constants.Resolution = Settings.Resolution[i];
		IDirect3DBaseTexture9* textures[2] = { Textures.BloomTexture[i + 1], levelTexture(i) };
		if (!draw(i == 0 ? extUpLast : extUp, textures, 2, Textures.BloomSurface[i], pushConstants())) return false;
	}
	return true;
}

/*
* Renders the final bloom buffer by iteratively downsampling and upsampling with intermediary blur.
*/
// UNOFFICIAL 2026-10-27 RenderScale v2: in a rect frame each level draws into its top-left Width x Height part, with a quad of its own
// (uv 0..Scale plus this level's half texel, as BloomVertexBuffer has); call after SetRenderTarget. Otherwise the level's usual quad.
IDirect3DVertexBuffer9* BloomEffect::LevelQuad(int i) {
	if (!RenderRect::Active()) return Textures.BloomVertexBuffer[i];
	const UINT w = (UINT)Settings.Resolution[i].x, h = (UINT)Settings.Resolution[i].y;
	const D3DXVECTOR3 key(RenderRect::ScaleX, RenderRect::ScaleY, (float)w);
	if (!RectVertexBuffer[i] || RectVertexKey[i] != key) {
		if (RectVertexBuffer[i]) { RectVertexBuffer[i]->Release(); RectVertexBuffer[i] = nullptr; }
		TheShaderManager->CreateFrameVertex(w, h, &RectVertexBuffer[i], RenderRect::ScaleX, RenderRect::ScaleY);
		RectVertexKey[i] = key;
	}
	const D3DVIEWPORT9 port = { 0, 0, (std::max)(1u, RenderRect::Width(w)), (std::max)(1u, RenderRect::Height(h)), 0.0f, 1.0f };
	TheRenderManager->device->SetViewport(&port);
	return RectVertexBuffer[i] ? RectVertexBuffer[i] : Textures.BloomVertexBuffer[i];
}

void BloomEffect::RenderBloomBuffer(IDirect3DSurface9* RenderTarget) {
	if (!Enabled) {
		renderTime = 0.0f;
		return; // skip rendering if the effect is disabled
	}

	auto timer = TimeLogger();

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;

	const int passes = std::clamp((int) TheShaderManager->GetTransitionValue(Settings.Main.Passes, Settings.Night.Passes, Settings.Interiors.Passes), 2, 8);

	// UNOFFICIAL 2026-10-25 (NVR DXVK): [Main.Main.Performance] NvrDxvkPasses runs the chain as Vulkan passes (RenderBloomExt); otherwise,
	// or when a pass is refused, the D3D9 chain below does the frame.
	if (!ExtFailed && DxvkCompute::PassesOn() && RenderBloomExt(Device, passes)) {
		Device->SetStreamSource(0, TheShaderManager->FrameVertex, 0, sizeof(FrameVS)); // as the D3D9 chain leaves it
		Device->SetRenderTarget(0, RenderTarget);
		renderTime = timer.LogTime("EffectRecord::ExtRender Bloom");
		return;
	}

	int passNumber = 0;

	//progressively blur & downsample
	for (int i = 0; i < passes; i++) {
		Device->SetRenderTarget(0, Textures.BloomSurface[i]);  // Render to correct bloom buffer.
		Device->SetStreamSource(0, LevelQuad(i), 0, sizeof(FrameVS)); // Set correct vertex buffer for given resolution (UNOFFICIAL RenderScale v2: after the target, LevelQuad sets the viewport).

		Constants.Resolution = Settings.Resolution[i];
		RenderPass(Device, passNumber, true);
		passNumber++;
	}

	passNumber += 2 * (MaxPasses - passes);

	//progressively blur & upsample and combine
	for (int i = passes - 2; i >= 0; i--) {
		Device->SetRenderTarget(0, Textures.BloomSurface[i]);  // Render to correct bloom buffer.
		Device->SetStreamSource(0, LevelQuad(i), 0, sizeof(FrameVS)); // Set correct vertex buffer for given resolution (UNOFFICIAL RenderScale v2: after the target, LevelQuad sets the viewport).

		if (Constants.Data.z > 0.0f) {
			Constants.Data.x = Settings.Resolution[i+1].z; // Pixel size x axis of the upsampled texture.
			Constants.Data.y = Settings.Resolution[i+1].w; // Pixel size y axis of the upsampled texture.
		}
		Constants.Resolution = Settings.Resolution[i];
		RenderPass(Device, passNumber, false);
		passNumber++;
	}

	Device->SetStreamSource(0, TheShaderManager->FrameVertex, 0, sizeof(FrameVS)); // Reset vertex buffer for other effects.
	Device->SetRenderTarget(0, RenderTarget);  // Reset render target for other effects.
	// UNOFFICIAL 2026-10-27 RenderScale v2: the top level is also read after the upscale, at full size (the game's tonemap shader,
	// the rain): stretched over the whole buffer then (user: "a ghosted image" over the screen with tonemapping on).
	RenderRect::RescaleAfterUpscale(Textures.BloomSurface[0]);

	renderTime = timer.LogTime("EffectRecord::Render Bloom");
}
