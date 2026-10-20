#include "Grass.h"
#include "../core/NewGrass.h"
#include "GrassCollision.h"
#include "../core/GrassWind.h"
#include "../core/PlantWind.h"

void GrassShaders::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_GrassScale", &Constants.Scale);
	PlantWind::RegisterConstants();
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
	NewGrass::ReadSettings();
	GrassCollision::ReadSettings();
	GrassWind::ReadSettings();
}

void GrassShaders::UpdateConstants() {
	GrassCollision::Update();
	const SandstormEffect* Sandstorm = TheShaderManager->Effects.Sandstorm;
	const float storm = Sandstorm && Sandstorm->Enabled && Sandstorm->GrassStorm[0] > 0.0f ? Sandstorm->Intensity : 0.0f;
	GrassWind::Update(Tes ? Tes->sky : nullptr, TheShaderManager->GameState.isExterior, storm);
}

void GrassShaders::SetForDraw(NiD3DVertexShaderEx* VertexShader, NiD3DPixelShaderEx* PixelShader) {
	if (!Enabled || !VertexShader || !VertexShader->Name || memcmp(VertexShader->Name, "GRASS", 5)) return;
	NewGrass::LastVariant = strlen(VertexShader->Name) >= 11 ? VertexShader->Name[10] : 0;
	LastBindNVR = false;
	if (!VertexShader->ShaderHandleBackup || VertexShader->ShaderHandle == VertexShader->ShaderHandleBackup) return;
	LastBindNVR = true;

	const bool NVRPixel = PixelShader && PixelShader->ShaderHandleBackup && PixelShader->ShaderHandle != PixelShader->ShaderHandleBackup;
	const SandstormEffect* Sandstorm = TheShaderManager->Effects.Sandstorm;
	const float Pairing[4] = { NVRPixel || !GameShaderFix ? 1.0f : 0.0f, Sandstorm ? Sandstorm->GrassStorm[0] : 0.0f,
		GrassWind::On ? GrassWind::ClockNow : Sandstorm ? Sandstorm->GrassStorm[1] : 0.0f, (float)DrawPart };
	TheRenderManager->device->SetVertexShaderConstantF(19, Pairing, 1);
	for (int i = 0; i < 4; i++) LastPairing[i] = Pairing[i];
	GrassCollision::Bind(TheRenderManager->device);
	GrassWind::BindCards(TheRenderManager->device);
	GrassWind::LogState(TheRenderManager->device);
}
