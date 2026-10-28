#pragma once

// UNOFFICIAL 2026-10-27: the game's own MSAA and NVR. With iMultiSample on, the world is drawn into multisampled targets and every
// depth-based NVR effect reads one depth per pixel: RESZ (AMD, and DXVK on any GPU, which resolves it with VK_RESOLVE_MODE_SAMPLE_ZERO)
// copies the FIRST sample only, while the colour on screen is the average of all of them. Wherever a pixel holds two surfaces -- every
// edge, and all over grass, whose cut-outs become coverage masks with MSAA (transparency multisampling) -- colour and depth disagree:
// user and a tester, MSAA 4 on DXVK: crusty speckle in the cloud shadows on grass, a broken shadow ring round the player; gone with MSAA
// off. D3D9 shaders cannot read the samples, so NVR cannot fix that per effect. It says so once in the log and once on screen
// (NvrMsaaWarningTick). It does not switch MSAA off itself: reading iMultiSample as 0 here (tried the same day) left the game half
// multisampled -- the water reflection still 4x -- and the grass shaders were never handed to NVR (no sun shadow on grass).
static int GameMSAAWarned = 0;	// the iMultiSample the INI asks for, when it is on (0: off)
void NvrMsaaWarningTick() {
	static bool shown = false;
	if (!GameMSAAWarned || shown || !Player || !Player->parentCell || !InterfaceManager) return;
	shown = true;
	InterfaceManager->ShowMessage("NVR: turn the game's anti-aliasing off (launcher options). With it on, NVR's shadows and effects get speckled.");
}

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
	else if (!strcmp(Setting->Name, "iMultiSample:Display") && Setting->iValue > 0 && !TheSettingManager->SettingsMain.Main.ForceMSAA) {
		if (!GameMSAAWarned)
			Logger::Log("WARNING: the game's own anti-aliasing is on (iMultiSample %d). NVR's effects read one depth sample per pixel, which with "
				"MSAA disagrees with the colour: speckled cloud shadows on grass, broken shadows round the player, patches on characters. Turn "
				"Antialiasing off in the game's launcher (Options); NVR's TAA / FSR / DLSS smooth the edges instead.", Setting->iValue);
		GameMSAAWarned = Setting->iValue;
	}
	// UNOFFICIAL 2026-10-27 (user: "still no grass shadows"): NVR's grass shaders replace the game's shader model 3 grass (GRASS23x000-007);
	// with b30GrassVS 0 the game takes its older GRASS2000-2023 set, which NVR leaves alone: no NVR lighting, shadow or wind on grass at all.
	// The launcher writes 0 whenever it sees DXVK's adapter name (it rewrote the user's FalloutPrefs.ini that way the same day), so it is
	// read as 1 here. (Read once, at start, for the grass shader set; unlike iMultiSample nothing else reads it.)
	else if (!strcmp(Setting->Name, "b30GrassVS:Grass") && !Setting->iValue) {
		Logger::Log("UNOFFICIAL b30GrassVS was 0 in the game's INI (the launcher writes 0 under DXVK): read as 1, so NVR's grass shaders are used");
		Setting->iValue = 1;
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
	return (*WriteSetting)(This, Setting);
	
}

void MainMenuMusicFix(char* DestinationBuffer, size_t Size, char* SourceBuffer) {

	char Buffer[64] = { NULL };

	strcpy(Buffer, "Data\\Music\\Special\\");
	strcat(Buffer, MainMenuMusic);
	strcat(Buffer, ".mp3");
	strcpy(DestinationBuffer, Buffer);

}