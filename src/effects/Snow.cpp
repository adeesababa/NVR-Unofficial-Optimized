#include "Snow.h"

void SnowEffect::UpdateConstants() {
	// Snow fall
	if (TheShaderManager->GameState.isSnow && Constants.SnowAnimator.switched == false) {
		// it just started snowing
		TheShaderManager->Effects.WetWorld->Constants.PuddlesAnimator.Start(0.3f, 0.f); // fade out any puddles if they exist
		Constants.SnowAnimator.switched = true;
		Constants.SnowAnimator.Initialize(0.f);
		Constants.SnowAnimator.Start(0.5f, 1.f);
	}
	else if (!TheShaderManager->GameState.isSnow && Constants.SnowAnimator.switched) {
		// it just stopped snowing
		Constants.SnowAnimator.switched = false;
		Constants.SnowAnimator.Start(0.2f, 0.f);
	}
	Constants.Data.x = Constants.SnowAnimator.GetValue();

	if (Constants.Data.x) TheShaderManager->orthoRequired = true; // mark ortho map calculation as necessary
}

void SnowEffect::UpdateSettings() {
	Constants.Data.z = TheSettingManager->GetSettingF("Shaders.Snow.Main", "Speed");
}

void SnowEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_SnowData", &Constants.Data);
}

bool SnowEffect::ShouldRender() {
	return Constants.Data.x > 0.0f &&
		TheShaderManager->GameState.isExterior &&
		!TheShaderManager->GameState.isUnderwater;
};

// UNOFFICIAL 2026-10-25 (NVR DXVK): the constants are read from the same registered memory the D3D9 path uploads
// (ConstantsTable; SetPixelShaderConstantF copies it raw), so the pass sees the registers the technique sees.
static const D3DXVECTOR4* RegisteredConstant(const char* name) {
	auto found = TheShaderManager->ConstantsTable.find(name);
	return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
}

// UNOFFICIAL 2026-10-25 (NVR DXVK): Snow.frag; the three textures and the constants the technique reads (EffectRecord::ExtTexturePass).
// Push block (196 bytes, padded to 208 like the shader's block): the registers of the two matrices the shader reads (the
// shadow matrix's four, the view matrix's first three: toWorld reads no more), then the float4s, then the sampler index.
bool SnowEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	const D3DXVECTOR4* shadowToLight = RegisteredConstant("TESR_ShadowCameraToLightTransformOrtho");
	const D3DXVECTOR4* view = RegisteredConstant("TESR_ViewTransform");
	const D3DXVECTOR4* projection = RegisteredConstant("TESR_ProjectionTransform");
	const D3DXVECTOR4* cameraData = RegisteredConstant("TESR_CameraData");
	const D3DXVECTOR4* gameTime = RegisteredConstant("TESR_GameTime");
	const D3DXVECTOR4* sunColor = RegisteredConstant("TESR_SunColor");
	const D3DXVECTOR4* sunAmbient = RegisteredConstant("TESR_SunAmbient");
	if (!shadowToLight || !view || !projection || !cameraData || !gameTime || !sunColor || !sunAmbient) return false;

	struct Push {
		D3DXVECTOR4 shadowToLight[4];	// 0: TESR_ShadowCameraToLightTransformOrtho, registers 0-3
		D3DXVECTOR4 view[3];			// 64: TESR_ViewTransform, columns 0-2 (HLSL M[i][j] is element (i+1, j+1): SetMatrix)
		D3DXVECTOR4 gameTime;			// 112: TESR_GameTime
		D3DXVECTOR4 snowData;			// 128: TESR_SnowData
		D3DXVECTOR4 sunColor;			// 144: TESR_SunColor
		D3DXVECTOR4 sunAmbient;			// 160: TESR_SunAmbient
		D3DXVECTOR4 projectionFar;		// 176: x TESR_ProjectionTransform[0][0], y [1][1], z TESR_CameraData.y
		UInt32		samplerIndex;		// 192: LINEAR, CLAMP (filled in by ExtTexturePass)
		UInt32		pad[3];
	} push = {};
	memcpy(push.shadowToLight, shadowToLight, sizeof(push.shadowToLight));
	for (int c = 0; c < 3; c++)	// 2026-10-25: the columns toWorld reads (the memory rows were a transposed view ray)
		push.view[c] = D3DXVECTOR4(((const float*)&view[0])[c], ((const float*)&view[1])[c], ((const float*)&view[2])[c], ((const float*)&view[3])[c]);
	push.gameTime = *gameTime;
	push.snowData = Constants.Data;
	push.sunColor = *sunColor;
	push.sunAmbient = *sunAmbient;
	push.projectionFar = D3DXVECTOR4(projection[0].x, projection[1].y, cameraData->y, 0.0f);

	IDirect3DBaseTexture9* textures[] = {
		TheTextureManager->RenderedTexture,
		TheShaderManager->Effects.CombineDepth ? TheShaderManager->Effects.CombineDepth->Textures.CombinedDepthTexture : nullptr,
		TheShaderManager->Effects.ShadowsExteriors ? TheShaderManager->Effects.ShadowsExteriors->ShadowMapOrthoTexture : nullptr };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "Snow", textures, _countof(textures), &push, sizeof(push), 192);
}