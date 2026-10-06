#include "Sharpening.h"

void SharpeningEffect::UpdateConstants() {
	Constants.Data = TheShaderManager->GameState.isExterior ? Exterior : Interior;
}

void SharpeningEffect::UpdateSettings(){
	Exterior = D3DXVECTOR4(TheSettingManager->GetSettingF("Shaders.Sharpening.Main", "Strength"),
		TheSettingManager->GetSettingF("Shaders.Sharpening.Main", "Clamp"),
		TheSettingManager->GetSettingF("Shaders.Sharpening.Main", "Offset"), 0.0f);
	Interior = D3DXVECTOR4(TheSettingManager->GetSettingF("Shaders.Sharpening.Interiors", "Strength"),
		TheSettingManager->GetSettingF("Shaders.Sharpening.Interiors", "Clamp"),
		TheSettingManager->GetSettingF("Shaders.Sharpening.Interiors", "Offset"), 0.0f);
	Constants.Data = TheShaderManager->GameState.isExterior ? Exterior : Interior;
}

void SharpeningEffect::RegisterConstants(){
	TheShaderManager->RegisterConstant("TESR_SharpeningData", &Constants.Data);
}