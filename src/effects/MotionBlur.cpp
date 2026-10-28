#include "MotionBlur.h"
#include "../core/DxvkCompute.h"

void MotionBlurEffect::UpdateConstants() {
	static unsigned lastFrame = ~0u;
	if (TheFrameRateManager->FrameNumber == lastFrame) return;
	lastFrame = TheFrameRateManager->FrameNumber;

	ValuesStruct category = Settings.FirstPerson;
	if (!TheCameraManager->IsFirstPerson()) category = Settings.ThirdPerson;

	float AngleZ = D3DXToDegree(Player->rot.z);
	float AngleX = D3DXToDegree(Player->rot.x);
	float fMotionBlurAmtX = Constants.oldAngleZ - AngleZ;
	float fMotionBlurAmtY = Constants.oldAngleX - AngleX;
	float fBlurDistScratchpad = fMotionBlurAmtX + 360.0f;
	float fBlurDistScratchpad2 = (AngleZ - Constants.oldAngleZ + 360.0f) * -1.0f;

	if (abs(fMotionBlurAmtX) > abs(fBlurDistScratchpad))
		fMotionBlurAmtX = fBlurDistScratchpad;
	else if (abs(fMotionBlurAmtX) > abs(fBlurDistScratchpad2))
		fMotionBlurAmtX = fBlurDistScratchpad2;

	if (pow(fMotionBlurAmtX, 2) + pow(fMotionBlurAmtY, 2) < category.BlurCutOff) {
		fMotionBlurAmtX = 0.0f;
		fMotionBlurAmtY = 0.0f;
	}

	Constants.Data.x = (Constants.oldoldAmountX + Constants.oldAmountX + fMotionBlurAmtX) / 3.0f;
	Constants.Data.y = (Constants.oldoldAmountY + Constants.oldAmountY + fMotionBlurAmtY) / 3.0f;
	Constants.oldAngleZ = AngleZ;
	Constants.oldAngleX = AngleX;
	Constants.oldoldAmountX = Constants.oldAmountX;
	Constants.oldoldAmountY = Constants.oldAmountY;
	Constants.oldAmountX = fMotionBlurAmtX;
	Constants.oldAmountY = fMotionBlurAmtY;
	Constants.BlurParams.x = category.GaussianWeight;
	Constants.BlurParams.y = category.BlurScale;
	Constants.BlurParams.z = category.BlurOffsetMax;
}

void MotionBlurEffect::UpdateSettings() {
	Settings.FirstPerson.BlurCutOff = TheSettingManager->GetSettingF("Shaders.MotionBlur.FirstPersonView", "BlurCutOff");
	Settings.FirstPerson.GaussianWeight = TheSettingManager->GetSettingF("Shaders.MotionBlur.FirstPersonView", "GaussianWeight");
	Settings.FirstPerson.BlurScale = TheSettingManager->GetSettingF("Shaders.MotionBlur.FirstPersonView", "BlurScale");
	Settings.FirstPerson.BlurOffsetMax = TheSettingManager->GetSettingF("Shaders.MotionBlur.FirstPersonView", "BlurOffsetMax");

	Settings.ThirdPerson.BlurCutOff = TheSettingManager->GetSettingF("Shaders.MotionBlur.ThirdPersonView", "BlurCutOff");
	Settings.ThirdPerson.GaussianWeight = TheSettingManager->GetSettingF("Shaders.MotionBlur.ThirdPersonView", "GaussianWeight");
	Settings.ThirdPerson.BlurScale = TheSettingManager->GetSettingF("Shaders.MotionBlur.ThirdPersonView", "BlurScale");
	Settings.ThirdPerson.BlurOffsetMax = TheSettingManager->GetSettingF("Shaders.MotionBlur.ThirdPersonView", "BlurOffsetMax");
}

void MotionBlurEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_MotionBlurParams", &Constants.BlurParams);
	TheShaderManager->RegisterConstant("TESR_MotionBlurData", &Constants.Data);
}

bool MotionBlurEffect::ShouldRender()
{
	return Constants.Data.x || Constants.Data.y; 
};

bool MotionBlurEffect::ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) {
	if (techniqueIndex != 0) return false;
	struct Push { D3DXVECTOR4 reciprocalResolution, blurParams, blurData; UInt32 samplerIndex; UInt32 pad[3]; } push = {};
	push.reciprocalResolution = TheShaderManager->ShaderConst.ReciprocalResolution;
	push.blurParams = Constants.BlurParams;
	push.blurData = Constants.Data;
	for (int pass = 0; pass < 2; pass++) {
		IDirect3DBaseTexture9* textures[] = { TheTextureManager->RenderedTexture };
		if (!ExtTexturePass(Device, RenderTarget, pass ? TheTextureManager->RenderedSurface : RenderedSurface, SourceBuffer, extPass, "MotionBlur", textures, _countof(textures), &push, sizeof(push), 48,
			false, 0, false, pass == 1))
			return pass == 1;
	}
	return true;
}
