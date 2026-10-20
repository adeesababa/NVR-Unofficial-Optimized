#include "Hooks.h"
#include "../../core/PointShadowForward.h"
#include "../../core/ClothWind.h"	// UNOFFICIAL 2026-10-19 cloth round 5: the partition draw hook

void AttachHooks() {

	SettingsMainStruct* SettingsMain = &TheSettingManager->SettingsMain;

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)ReadSetting, &ReadSettingHook);
	DetourAttach(&(PVOID&)WriteSetting, &WriteSettingHook);
	DetourAttach(&(PVOID&)NewMain, &NewMainHook);
	DetourAttach(&(PVOID&)InitializeRenderer, &InitializeRendererHook);
	DetourAttach(&(PVOID&)NewTES, &NewTESHook);
	DetourAttach(&(PVOID&)NewPlayerCharacter, &NewPlayerCharacterHook);
	DetourAttach(&(PVOID&)NewSceneGraph, &NewSceneGraphHook);
	DetourAttach(&(PVOID&)NewMainDataHandler, &NewMainDataHandlerHook);
	DetourAttach(&(PVOID&)NewMenuInterfaceManager, &NewMenuInterfaceManagerHook);
	DetourAttach(&(PVOID&)NewQueuedModelLoader, &NewQueuedModelLoaderHook);
	DetourAttach(&(PVOID&)CreateVertexShader, &CreateVertexShaderHook);
	DetourAttach(&(PVOID&)CreatePixelShader, &CreatePixelShaderHook);
	DetourAttach(&(PVOID&)SetShaderPackage, &SetShaderPackageHook);
	DetourAttach(&(PVOID&)Render, &RenderHook);
	DetourAttach(&(PVOID&)ProcessImageSpaceShaders, &ProcessImageSpaceShadersHook);
	DetourAttach(&(PVOID&)RenderWorldSceneGraph, &RenderWorldSceneGraphHook);
	DetourAttach(&(PVOID&)RenderBlended, &RenderBlendedHook);   // UNOFFICIAL sandport: glass writes no depth in a sandstorm (ThroughBlended)
	DetourAttach(&(PVOID&)RenderFirstPerson, &RenderFirstPersonHook);
	DetourAttach(&(PVOID&)SetShaders, &SetShadersHook);
	DetourAttach(&(PVOID&)SetSamplerState, &SetSamplerStateHook);
	AttachGrassFirstRenderPassHook();	// UNOFFICIAL 2026-10-17 GrassFirst (Render.cpp): pass-through unless DrawFirst is on
	LampReachMargin = (std::max)(0.0f, TheSettingManager->GetSettingF("Main.Main.Misc", "LampReachMargin"));
	if (LampReachMargin > 0.0f) {	// UNOFFICIAL, Render.cpp
		const LONG attached = DetourAttach(&(PVOID&)BoundWithinLightRadius, &BoundWithinLightRadiusHook);
		Logger::Log("UNOFFICIAL LampReachMargin %.0f: hook %s (%ld), first bytes %02X %02X %02X", LampReachMargin, attached == NO_ERROR ? "attached" : "FAILED", attached,
			((const UInt8*)0x00B9DAE0)[0], ((const UInt8*)0x00B9DAE0)[1], ((const UInt8*)0x00B9DAE0)[2]);
	}
	// UNOFFICIAL interior forward shadows: each object's own lamps (PointShadowForward.h).
	// (Also for ScopeFix outdoors, HiddenObjectCulling, LampScissor and the material rules, which work on the same per-object call; their
	// switches are live only while this hook is in, i.e. with any of these on at startup.)
	if (TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "Enabled") ||
		TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "ScopeFix") ||
		SettingsMain->Main.HiddenObjectCulling || SettingsMain->Main.LampScissor ||
		TheSettingManager->GetSettingI("Shaders.PBR.MaterialRules", "Enabled")) {
		DetourAttach(&(PVOID&)RenderPassStandard, &RenderPassStandardHook);
		DetourAttach(&(PVOID&)RenderPassSkinned, &RenderPassSkinnedHook);
	}

	DetourAttach(&(PVOID&)GetWaterHeightLOD, &GetWaterHeightLODHook);
	// Always hooked so the reflection pass can be profiled; it only changes rendering when
	// ForceReflections is enabled (see RenderReflectionsHook).
	DetourAttach(&(PVOID&)RenderReflections, &RenderReflectionsHook);

	DetourAttach(&(PVOID&)RenderPipboy, &RenderPipboyHook);
	DetourAttach(&(PVOID&)ShowDetectorWindow, &ShowDetectorWindowHook);
	DetourAttach(&(PVOID&)LoadForm, &LoadFormHook);
	if (SettingsMain->FlyCam.Enabled) DetourAttach(&(PVOID&)UpdateFlyCam, &UpdateFlyCamHook);
	DetourTransactionCommit();

	// Vanilla shader specific hooks.
	kSkyShaderConstantsDetour.ReplaceVirtualFunc(0x10AFE94, SkyShader__UpdateConstants);
	// UNOFFICIAL Conor's skin shader ([Shaders.Skin.NewSkin] Enabled at startup, Shaders.cpp): SkinShader's per-draw slots bind the
	// skin scattering targets and mute the engine's second skin highlights, and interior faces take SkinShader's own passes.
	// Off: nothing is hooked or patched.
	if (SkinShaders::NewSkinEnabled()) {
		kSkinPrepareGeometryDetour.ReplaceVirtualFunc(0x10BB980 + 27 * 4, SkinShader__PrepareGeometryForRendering);
		kSkinPostGeometryDetour.ReplaceVirtualFunc(0x10BB980 + 35 * 4, SkinShader__PostGeometry);
		InstallFaceGenInteriorPatch();
		Logger::Log("UNOFFICIAL NewSkin: Conor's skin shader on (SkinTemplate.hlsl + SkinScattering)%s",
			GetModuleHandle(L"VanillaPlusSkin.dll") ? "; it replaces Vanilla Plus Skin's shaders" : "");
	}

	// UNOFFICIAL KeepLampsInView (PointShadowForward.h): the game's one lamp view test call, with forward shadows, unless another
	// plugin has taken it.
	if (TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Forward", "Enabled")) {
		const UInt32 site = PointShadowForward::kTestFrustumCallSite;
		if (*(UInt8*)site == 0xE8 && site + 5 + *(SInt32*)(site + 1) == PointShadowForward::kTestFrustumCull) {
			WriteRelCall(site, (UInt32)&PointShadowForward::TestFrustumCullHook);
			Logger::Log("UNOFFICIAL KeepLampsInView: the game's lamp view test (ShadowSceneNode::PreOnVisible) is checked against the render camera");
		}
		else Logger::Log("UNOFFICIAL KeepLampsInView: the call at %08X is not the game's lamp view test (another plugin?); not hooked", site);
	}

	InstallGrassFirstHooks();	// UNOFFICIAL 2026-10-17 GrassFirst (Render.cpp): pass-through unless [Shaders.Grass.Main] DrawFirst = true
	// UNOFFICIAL 2026-10-19 cloth round 5 (core/ClothWind.h DrawPartition): the game's two per-partition draw calls in
	// BSBatchRenderer::RenderPassImmediately_Skinned, so worn cloth can have its per-vertex weights in vertex stream 1. Only when the
	// cloth code is compiled in ([Shaders.Cloth.Main] Enabled at startup) and both calls are the game's; otherwise mask 4 stays.
	if (TheSettingManager->GetSettingI("Shaders.Cloth.Main", "Enabled")) {
		auto isDrawPartition = [](UInt32 site) { return *(UInt8*)site == 0xE8 && site + 5 + *(SInt32*)(site + 1) == 0x00E6D310; };
		if (isDrawPartition(0x00B992E3) && isDrawPartition(0x00B99339)) {
			WriteRelCall(0x00B992E3, (UInt32)&ClothWind::DrawPartitionHook);
			WriteRelCall(0x00B99339, (UInt32)&ClothWind::DrawPartitionHook);
			ClothWind::DrawHookOn = true;
			Logger::Log("UNOFFICIAL cloth wind: the skinned partition draws are hooked (per-vertex weights of worn cloth, [Shaders.Cloth.Main] UseWeights)");
		}
		else Logger::Log("UNOFFICIAL cloth wind: the calls at 00B992E3 / 00B99339 are not the game's partition draw (another plugin?); worn cloth keeps the skeleton mask");
	}
	WriteRelCall(0xBE0B73, NiD3DVertexShaderEx::Free);
	WriteRelCall(0xBE0AF3, NiD3DPixelShaderEx::Free);
	SafeWrite32(0x00E7624D, sizeof(RenderManager));
	SafeWrite32(0x00466606, sizeof(TESWeatherEx));
	SafeWrite32(0x0046CF9B, sizeof(TESWeatherEx));

	SafeWrite8(0xB575AA, 0x75);				// Prevent shader package destruction

	// UNOFFICIAL MoreLampsPerObject (2026-10-13): BSShaderPPLightingProperty::AddPass_Opt (0x00BDE9B0) gives a specular
	// many-lamp pass (0xD0, 0xD1: most lit surfaces indoors) at most 3-4 entries, the first light and 2-3 lamps (ADD ECX,2 at
	// 0x00BDEA3D; the others get 5-6 or 6-7) and drops every lamp past that, with no extra pass for them. The list is sorted by
	// brightness and lamps go in and out of view, so which 2-3 an object kept changed as the player moved: a wall piece next
	// to a lamp lit by it, then not. ADD ECX,5: 6-7 entries, as the other many-lamp passes; the shaders take 6
	// (SPECULAR_LAMPS6, ShaderRecord.cpp). Written only if the bytes are the game's.
	if (TheSettingManager->GetSettingI("Main.Main.Misc", "MoreLampsPerObject")) {
		const UInt8* site = (const UInt8*)0x00BDEA3D;
		if (site[0] == 0x83 && site[1] == 0xC1 && site[2] == 0x02) {
			SafeWrite8(0x00BDEA3F, 0x05);
			Logger::Log("UNOFFICIAL MoreLampsPerObject: specular many-lamp passes take up to 6 lamps (the game: 3)");
		}
		else Logger::Log("UNOFFICIAL MoreLampsPerObject: the game's code at 00BDEA3D is not as expected (another plugin?); not changed");
	}
	SafeWrite8(0x008751C0, 0);				// Stops to clear the depth buffer when rendering the 1st person node
	SafeWrite16(0x0086A170, 0x9090);		// Avoids to pause the game when ALT-TAB

	WriteRelJump(Jumpers::DetectorWindow::CreateTreeViewHook, DetectorWindowCreateTreeViewHook);
	WriteRelJump(Jumpers::DetectorWindow::DumpAttributesHook, DetectorWindowDumpAttributesHook);
	WriteRelJump(Jumpers::DetectorWindow::ConsoleCommandHook, DetectorWindowConsoleCommandHook);
	WriteRelCall(Jumpers::DetectorWindow::SetNodeName, DetectorWindowSetNodeName);
	kRenderInterfaceDetour.ReplaceCall(Jumpers::RenderInterface::Hook, RenderInterfaceHook);
	WriteRelJump(0x871290, RenderShadowMapHook);
	//SafeWriteJump(Jumpers::Shadows::RenderShadowMap1Hook,		(UInt32)RenderShadowMap1Hook);
	//SafeWriteJump(Jumpers::Shadows::AddCastShadowFlagHook, (UInt32)AddCastShadowFlagHook);
	WriteRelJump(Jumpers::Shadows::LeavesNodeNameHook, LeavesNodeNameHook);
	WriteRelCall(Jumpers::MainMenuMusic::Fix1, MainMenuMusicFix);
	WriteRelCall(Jumpers::MainMenuMusic::Fix2, MainMenuMusicFix);

	WriteRelJump(0x004E4C3B, 0x004E4C42); // Fixes reflections when cell water height is not like worldspace water height
	WriteRelJump(0x004E4DA4, 0x004E4DAC); // Fixes reflections on the distant water
	WriteRelCall(0x00875B86, 0x00710AB0); // Sets the world fov at the end of 1st person rendering
	WriteRelCall(0x00875B9D, 0x00710AB0); // Sets the world fov at the end of 1st person rendering
	WriteRelJump(0x00C03F49, 0x00C03F5A); // Fixes wrong rendering for image space effects

	WriteRelCall(0x9BB158, MuzzleLightCullingFix);
	WriteRelCall(0x879061, CreateSaveTextureHook); // Fixes image corruption in save screenshots when using DXVK with the HDR mod 

	if (TheSettingManager->SettingsMain.Main.ReplaceIntro) 
		WriteRelJump(Jumpers::SetTileShaderConstants::Hook, SetTileShaderConstantsHook);

	if (TheSettingManager->SettingsMain.Main.RemovePrecipitations) {
		WriteRelJump(0x0063AFC4, 0x0063AFD8);
		WriteRelJump(0x0063A5CB, 0x0063A5DE);
	}

	// Vanilla fog remover.
	for (UInt32 uiAddress : {0x6335EE, 0xB795FA, 0xB7AE86, 0xB7B539, 0xB7C3AB, 0xB86738, 0xBAA43B, 0xBB1B5B, 0xBB1FA5, 0xBB670E, 0xBBDF26, 0xBBE3EC, 0xBC6E33, 0xBD4BED }) {
		WriteRelCall(uiAddress, ShadowSceneNode__GetFogPropertyEx);
	}

	// End frame hook for effect reload.
	SafeWrite32(0x10EE63C, (UInt32)NiDX9Renderer__Do_EndFrame);

	if (TheSettingManager->SettingsMain.Main.RemoveUnderwater)
		WriteRelCall(0x4EC8EE, WaterFogRemover);

	//SafeWriteCall(0xB7DBAC, (UInt32)ShadowLightShader__UpdateLights);


	if (SettingsMain->FlyCam.Enabled) {
		WriteRelJump(Jumpers::FlyCam::UpdateForwardFlyCamHook, UpdateForwardFlyCamHook);
		WriteRelJump(Jumpers::FlyCam::UpdateBackwardFlyCamHook, UpdateBackwardFlyCamHook);
		WriteRelJump(Jumpers::FlyCam::UpdateRightFlyCamHook, UpdateRightFlyCamHook);
		WriteRelJump(Jumpers::FlyCam::UpdateLeftFlyCamHook, UpdateLeftFlyCamHook);
	}

}
