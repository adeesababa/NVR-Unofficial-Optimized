#include "Grass.h"

void GrassShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_GrassScale", &Constants.Scale);
}

// iMinGrassSize, fTexturePctThreshold, fGrass*Distance and fGrassWindMagnitude* are consumed
// at cell load: writing them affects only cells loaded afterwards.
void GrassShaders::UpdateSettings() {
	if (!Enabled) return;

	Constants.Scale.x = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleX");
	Constants.Scale.y = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleY");
	Constants.Scale.z = TheSettingManager->GetSettingF("Shaders.Grass.Main", "ScaleZ");
	GameShaderFix = TheSettingManager->GetSettingI("Shaders.Grass.Main", "GameShaderFix");
	DrawFirst = TheSettingManager->GetSettingI("Shaders.Grass.Main", "DrawFirst");
	DrawFirstRestore = TheSettingManager->GetSettingI("Shaders.Grass.Main", "DrawFirstRestore");
	DrawFirstCull = TheSettingManager->GetSettingI("Shaders.Grass.Main", "DrawFirstCull");
}

void GrassShaders::UpdateConstants() {}

// UNOFFICIAL 2026-10-17 grass study: every pass that sets grass shaders up (SetShadersHook; the game does it once for a run of
// grass draws). Tells NVR's grass vertex shaders (c19, GrassPairing in GRASS23x00*.vso) which pixel shader reads them: NVR's
// GRASS23x000TMS.pso (transparency multisampling on) takes the light colours and amounts apart; the game's own GRASS23x000.pso
// (the INI default, no transparency multisampling) needs them multiplied together as the game wrote them. GameShaderFix = false
// gives the P80 behaviour (the bare colours to the game's shader: too bright) for A/B tests.
void GrassShaders::SetForDraw(NiD3DVertexShaderEx* VertexShader, NiD3DPixelShaderEx* PixelShader) {
	if (!Enabled || !VertexShader || !VertexShader->Name || memcmp(VertexShader->Name, "GRASS", 5)) return;
	LastBindNVR = false;
	if (!VertexShader->ShaderHandleBackup || VertexShader->ShaderHandle == VertexShader->ShaderHandleBackup) return;	// the game's own VS
	LastBindNVR = true;

	const bool NVRPixel = PixelShader && PixelShader->ShaderHandleBackup && PixelShader->ShaderHandle != PixelShader->ShaderHandleBackup;
	// w: GrassFirst's part (DrawPart; Render.cpp namespace GrassFirst): 0 every tuft, 1 the near tufts, 2 the fading ones.
	const float Pairing[4] = { NVRPixel || !GameShaderFix ? 1.0f : 0.0f, 0.0f, 0.0f, (float)DrawPart };
	TheRenderManager->device->SetVertexShaderConstantF(19, Pairing, 1);
	for (int i = 0; i < 4; i++) LastPairing[i] = Pairing[i];
}
