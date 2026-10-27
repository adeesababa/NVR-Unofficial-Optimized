#pragma once

// UNOFFICIAL 2026-10-27 [Main.Main.Misc] AllowGameMSAA (false by default). With the game's own MSAA on, the world is drawn into multisampled
// targets and every depth-based NVR effect reads one depth per pixel: RESZ (AMD, and DXVK on any GPU, which resolves it with
// VK_RESOLVE_MODE_SAMPLE_ZERO) copies the FIRST sample only, while the colour on screen is the average of all of them. Wherever a pixel
// holds two surfaces -- every edge, and all over grass, whose cut-outs become coverage masks with MSAA (transparency multisampling) --
// colour and depth disagree, and the surface directions rebuilt from that depth jump from pixel to pixel: user and a tester, MSAA 4 on
// DXVK: crusty speckle in the cloud shadows on grass, a broken shadow ring round the player, patches on characters; gone with MSAA off.
// D3D9 shaders cannot read the samples of a multisampled image, so NVR cannot fix that per effect. The game's MSAA is switched off for
// the session instead (NVR's TAA / FSR / DLSS smooth the edges); the INI files keep the player's value (WriteSettingHook writes it back).
static int GameMSAARequested = -1;	// the iMultiSample the INI asked for, when NVR switched it off (-1: not switched off)

bool (__thiscall* ReadSetting)(INISettingCollection*, GameSetting*) = (bool (__thiscall*)(INISettingCollection*, GameSetting*))Hooks::ReadSetting;
bool __fastcall ReadSettingHook(INISettingCollection* This, UInt32 edx, GameSetting* Setting) {
	
	bool r = (*ReadSetting)(This, Setting);

	if (!strcmp(Setting->Name, "iLocation X:Display") || !strcmp(Setting->Name, "iLocation Y:Display"))
		Setting->iValue = 0;
	else if (!strcmp(Setting->Name, "bFull Screen:Display"))
		TheSettingManager->SetWindowedMode(Setting->iValue);
	else if (!strcmp(Setting->Name, "SIntroSequence:General") && TheSettingManager->SettingsMain.Main.ReplaceIntro)
		Setting->pValue = (char*)IntroMovie;
	else if (!strcmp(Setting->Name, "SMainMenuMovie:General") && TheSettingManager->SettingsMain.Main.ReplaceIntro)
		Setting->pValue = (char*)MainMenuMovie;
	else if ((!strcmp(Setting->Name, "SMainMenuMusic:General") || !strcmp(Setting->Name, "STitleMusic:Loading")) && TheSettingManager->SettingsMain.Main.ReplaceIntro)
		Setting->pValue = (char*)MainMenuMusic;
	else if (!strcmp(Setting->Name, "bDoCanopyShadowPass:Display") || !strcmp(Setting->Name, "bDoActorShadows:Display") || !strcmp(Setting->Name, "iActorShadowCountExt:Display") || !strcmp(Setting->Name, "iActorShadowCountInt:Display"))
		Setting->iValue = 0;
	else if (!strcmp(Setting->Name, "iMultiSample:Display") && Setting->iValue < 2 && TheSettingManager->SettingsMain.Main.ForceMSAA)
		Setting->iValue = 2;
	else if (!strcmp(Setting->Name, "iMultiSample:Display") && Setting->iValue > 0 && !TheSettingManager->SettingsMain.Main.ForceMSAA &&
		!TheSettingManager->SettingsMain.Main.AllowGameMSAA) {
		GameMSAARequested = Setting->iValue;
		Logger::Log("UNOFFICIAL AllowGameMSAA false: the game's own MSAA (iMultiSample %d in its INI) is off for this session. NVR's effects read one "
			"depth sample per pixel, which with MSAA disagrees with the colour (speckled cloud shadows on grass, broken shadows round the player); "
			"NVR's TAA / FSR / DLSS smooth the edges instead. The INI keeps %d. [Main.Main.Misc] AllowGameMSAA = true keeps the game's MSAA.",
			Setting->iValue, Setting->iValue);
		Setting->iValue = 0;
	}
	else if (!strcmp(Setting->Name, "bDoHighDynamicRange:BlurShaderHDR"))
		Setting->iValue = 1;
	else if (!strcmp(Setting->Name, "bUseBlurShader:BlurShader"))
		Setting->iValue = 0;
	else if (!strcmp(Setting->Name, "bUseWaterHiRes:Water"))
		Setting->iValue = 1;
	else if (!strcmp(Setting->Name, "bForceHighDetailReflections:Water"))
		Setting->iValue = 0;
	return r;

}

bool (__thiscall* WriteSetting)(INISettingCollection*, GameSetting*) = (bool (__thiscall*)(INISettingCollection*, GameSetting*))Hooks::WriteSetting;
bool __fastcall WriteSettingHook(INISettingCollection* This, UInt32 edx, GameSetting* Setting) {

	if (!strcmp(Setting->Name, "fNearDistance:Display") || !strcmp(Setting->Name, "fNear1stPersonDistance:Display"))
		return true;
	else if (!strcmp(Setting->Name, "fDefaultFOV:Display") || !strcmp(Setting->Name, "fDefaultWorldFOV:Display") || !strcmp(Setting->Name, "fDefault1stPersonFOV:Display") || !strcmp(Setting->Name, "fPipboy1stPersonFOV:Display"))
		return true;
	else if (!strcmp(Setting->Name, "fLODFadeOutMultActors:LOD") || !strcmp(Setting->Name, "fLODFadeOutMultItems:LOD") || !strcmp(Setting->Name, "fLODFadeOutMultObjects:LOD"))
		return true;
	else if (!strcmp(Setting->Name, "SIntroSequence:General") || !strcmp(Setting->Name, "SMainMenuMovie:General") || !strcmp(Setting->Name, "SMainMenuMusic:General") || !strcmp(Setting->Name, "STitleMusic:Loading"))
		return true;
	else if (GameMSAARequested >= 0 && !strcmp(Setting->Name, "iMultiSample:Display") && Setting->iValue == 0) {
		// UNOFFICIAL 2026-10-27 AllowGameMSAA: the INI keeps the player's own value (see ReadSettingHook)
		Setting->iValue = GameMSAARequested;
		const bool r = (*WriteSetting)(This, Setting);
		Setting->iValue = 0;
		return r;
	}
	return (*WriteSetting)(This, Setting);
	
}

void MainMenuMusicFix(char* DestinationBuffer, size_t Size, char* SourceBuffer) {

	char Buffer[64] = { NULL };

	strcpy(Buffer, "Data\\Music\\Special\\");
	strcat(Buffer, MainMenuMusic);
	strcat(Buffer, ".mp3");
	strcpy(DestinationBuffer, Buffer);

}