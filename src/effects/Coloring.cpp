#include "Coloring.h"
#include "LUT.h"
#include "../core/DxvkCompute.h"

void ColoringEffect::UpdateConstants() {
	SettingsColoringStruct* scs = TheSettingManager->GetSettingsColoring(Player->parentCell->GetEditorName());
	if (!scs && TheShaderManager->GameState.isExterior) scs = TheSettingManager->GetSettingsColoring(Player->GetWorldSpace()->GetEditorName());
	if (!scs) scs = TheSettingManager->GetSettingsColoring("Default");

	Constants.Data.x = scs->Strength;
	Constants.Data.y = scs->BaseGamma;
	Constants.Data.z = scs->Fade;
	Constants.Data.w = scs->Contrast;
	Constants.Values.x = scs->Saturation;
	Constants.Values.y = scs->Bleach;
	Constants.Values.z = scs->BleachLuma;
	Constants.Values.w = scs->Linearization;
	Constants.ColorCurve.x = scs->ColorCurve;
	Constants.ColorCurve.y = scs->ColorCurveR;
	Constants.ColorCurve.z = scs->ColorCurveG;
	Constants.ColorCurve.w = scs->ColorCurveB;
	Constants.EffectGamma.x = scs->EffectGamma;
	Constants.EffectGamma.y = scs->EffectGammaR;
	Constants.EffectGamma.z = scs->EffectGammaG;
	Constants.EffectGamma.w = scs->EffectGammaB;
}

void ColoringEffect::UpdateSettings() {}

void ColoringEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_ColoringColorCurve", &Constants.ColorCurve);
	TheShaderManager->RegisterConstant("TESR_ColoringEffectGamma", &Constants.EffectGamma);
	TheShaderManager->RegisterConstant("TESR_ColoringData", &Constants.Data);
	TheShaderManager->RegisterConstant("TESR_ColoringValues", &Constants.Values);
}

bool ColoringEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex > 1) return false;
	const bool withLUT = techniqueIndex == 1;
	LUTEffect* lut = TheShaderManager->Effects.LUT;
	struct Push { D3DXVECTOR4 colorCurve, effectGamma, data, values, lutData, lutBlend; UInt32 samplerIndex; UInt32 lutPass; } push = {};
	push.colorCurve = Constants.ColorCurve;
	push.effectGamma = Constants.EffectGamma;
	push.data = Constants.Data;
	push.values = Constants.Values;
	push.lutData = withLUT ? lut->Constants.Data : D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	push.lutBlend = withLUT ? lut->Constants.Blend : D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	push.lutPass = withLUT ? 1 : 0;
	IDirect3DBaseTexture9* current = TheTextureManager->RenderedTexture;
	IDirect3DBaseTexture9* textures[] = { current,
		(withLUT && lut->DayTexture) ? lut->DayTexture : current,
		(withLUT && lut->NightTexture) ? lut->NightTexture : current,
		(withLUT && lut->InteriorTexture) ? lut->InteriorTexture : current };
	return ExtTexturePass(Device, RenderTarget, RenderedSurface, SourceBuffer, extPass, "Coloring", textures, _countof(textures), &push, sizeof(push), 96);
}