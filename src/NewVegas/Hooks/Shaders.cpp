#pragma once

// 0xB89D80
VirtFuncDetour kSkyShaderConstantsDetour;
void __fastcall SkyShader__UpdateConstants(SkyShader* apThis, void*, const NiPropertyState* apProperties) {
    const SkyShaderProperty* pShaderProp = apProperties->GetShadeProperty<SkyShaderProperty const>();
    uint32_t eSkyObjectType = pShaderProp->eSkyObjectType;
    TheShaderManager->ShaderConst.skyObjectID = eSkyObjectType;

    ThisCall(kSkyShaderConstantsDetour.GetOverwrittenAddr(), apThis, apProperties);

    TheRenderManager->device->SetPixelShaderConstantF(20, (const float*)&TheShaderManager->ShaderConst.skyObjectID, 1);
}

VirtFuncDetour kSkinPrepareGeometryDetour;
VirtFuncDetour kSkinPostGeometryDetour;

static bool  sSkinHighlightMuted = false;
static DWORD sSkinSavedWriteMask = 0xF;

static void* sFaceGenStage2 = nullptr;

static bool BindFaceGenMaps(void* apThis, NiGeometry* Geometry, NiD3DPass* Pass) {
    if (!Geometry || !Pass || Pass->StageCount < 4) return false;
    BSShaderPPLightingProperty* Property = static_cast<BSShaderPPLightingProperty*>(Geometry->GetProperty(NiProperty::PropertyType::kType_Shade));
    if (!Property || !(Property->ulFlags[0] & BSShaderProperty::FaceGen)) return false;
    if (!Property->ppTextures[0] || !Property->ppTextures[0][1]) return false;

    void** Stages = *(void***)((UInt8*)Pass + 0x24);
    if (!Stages || !Stages[2] || !Stages[3]) return false;

    ThisCall(0xB7C0A0, apThis, Property, 2, 1);
    ThisCall(0xB7C0E0, apThis, Property, 3, 1);
    ThisCall(0xBE2170, apThis);
    sFaceGenStage2 = Stages[2];
    return true;
}

void* __fastcall SkinShader__PrepareGeometryForRendering(void* apThis, void*, void* apGeometry, void* apPartition, void* apRendererData, void* apState) {
    void* Result = (void*)ThisCall(kSkinPrepareGeometryDetour.GetOverwrittenAddr(), apThis, apGeometry, apPartition, apRendererData, apState);

    NiD3DPass* Pass = *(NiD3DPass**)0x0126F74C;
    NiD3DPixelShaderEx* PixelShader = Pass ? (NiD3DPixelShaderEx*)Pass->PixelShader : nullptr;
    const UInt16 PassType = *(UInt16*)0x011F91E4;

    const bool NVRSkin = TheShaderManager->Shaders.Skin->Enabled && TheSettingManager->SettingsMain.Main.RenderEffects;
    const bool SkinPixelShader = PixelShader && PixelShader->Name && !memcmp(PixelShader->Name, "SKIN", 4);
    SkinScatteringEffect* Blur = TheShaderManager->Effects.SkinScattering;
    const bool BlurRuns = Blur && Blur->Enabled && Blur->Supported && Blur->ScreenSpace;
    const bool GameHighlightPass = NVRSkin && !SkinPixelShader && PassType >= 0x154 && PassType <= 0x177;
    if (GameHighlightPass && !sSkinHighlightMuted) {
        const bool Keep = !TheShaderManager->Shaders.Skin->MuteGameHighlights && (!BlurRuns || Blur->BindForGameHighlights(PixelShader));
        if (!Keep) {
            TheRenderManager->device->GetRenderState(D3DRS_COLORWRITEENABLE, &sSkinSavedWriteMask);
            TheRenderManager->device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            sSkinHighlightMuted = true;
        }
    }

    SkinScatteringEffect* Scattering = TheShaderManager->Effects.SkinScattering;
    if (Scattering && SkinPixelShader)
        Scattering->BindForSkinDraw(PixelShader);

    bool FaceGenBound = false;
    if (NVRSkin && TheShaderManager->Shaders.Skin->BindFaceGenMaps && SkinPixelShader && !memcmp(PixelShader->Name, "SKIN200", 7) && PixelShader->Name[7] >= '4' && PixelShader->Name[7] <= '7')
        FaceGenBound = BindFaceGenMaps(apThis, (NiGeometry*)apGeometry, Pass);

    if (TheSettingManager->SettingsMain.Develop.DebugMode && Global->OnKeyDown(TheSettingManager->SettingsMain.Develop.TraceShaders)) {
        Logger::Log("SkinHook: pass %s (0x%X), pixel shader %s: highlights muted %d, copied %d, scattering bound %d",
            Pointers::Functions::GetPassDescription(PassType), (UInt32)PassType,
            (PixelShader && PixelShader->Name) ? PixelShader->Name : "(none)",
            sSkinHighlightMuted ? 1 : 0, (Scattering && Scattering->HighlightsBound) ? 1 : 0, (Scattering && Scattering->Bound) ? 1 : 0);
    }
    static int sLogLeft = -1;
    if (sLogLeft < 0) sLogLeft = (std::max)(0, TheSettingManager->GetSettingI("Shaders.Skin.Debug", "LogDraws"));
    if (sLogLeft > 0 && Scattering && Scattering->InWorldScene) {
        sLogLeft--;
        DWORD Blend = 0, Src = 0, Dst = 0, Write = 0;
        TheRenderManager->device->GetRenderState(D3DRS_ALPHABLENDENABLE, &Blend);
        TheRenderManager->device->GetRenderState(D3DRS_SRCBLEND, &Src);
        TheRenderManager->device->GetRenderState(D3DRS_DESTBLEND, &Dst);
        TheRenderManager->device->GetRenderState(D3DRS_COLORWRITEENABLE, &Write);
        const void* RenderPass = *(void**)0x011F91E0;
        const UInt16 PassEnum = RenderPass ? *(const UInt16*)((const UInt8*)RenderPass + 0x04) : 0xFFFF;
        const bool Nvr = PixelShader && PixelShader->ShaderHandleBackup && PixelShader->ShaderHandle != PixelShader->ShaderHandleBackup;
        Logger::Log("SKIN DRAW %s: eCurrentPass 0x%X (%s), render pass 0x%X (%s), PS %s (%s), blend %d %d->%d, write 0x%X: muted %d, highlights copied %d, scatter bound %d, facegen %d",
            ((NiGeometry*)apGeometry)->m_pcName ? ((NiGeometry*)apGeometry)->m_pcName : "?", (UInt32)PassType, Pointers::Functions::GetPassDescription(PassType),
            (UInt32)PassEnum, Pointers::Functions::GetPassDescription(PassEnum), (PixelShader && PixelShader->Name) ? PixelShader->Name : "(none)",
            Nvr ? "NVR" : "game", (int)Blend, (int)Src, (int)Dst, (UInt32)Write, sSkinHighlightMuted ? 1 : 0, Scattering->HighlightsBound ? 1 : 0, Scattering->Bound ? 1 : 0, FaceGenBound ? 1 : 0);
    }

    const float ScreenSpace[4] = { (Scattering && Scattering->Bound) ? 1.0f : 0.0f, FaceGenBound ? 1.0f : 0.0f, 0.0f, 0.0f };
    TheRenderManager->device->SetPixelShaderConstantF(194, ScreenSpace, 1);
    return Result;
}

void __fastcall SkinShader__PostGeometry(void* apThis, void*, void* apProperties) {
    ThisCall(kSkinPostGeometryDetour.GetOverwrittenAddr(), apThis, apProperties);
    if (TheShaderManager->Effects.SkinScattering) TheShaderManager->Effects.SkinScattering->Unbind();
    if (sFaceGenStage2) {
        *(void**)((UInt8*)sFaceGenStage2 + 0x08) = nullptr;
        sFaceGenStage2 = nullptr;
    }
    if (sSkinHighlightMuted) {
        TheRenderManager->device->SetRenderState(D3DRS_COLORWRITEENABLE, sSkinSavedWriteMask);
        sSkinHighlightMuted = false;
    }
}

static UInt8 FaceGenInteriorFlag = 0;

void InstallFaceGenInteriorPatch() {
    static const UInt8 Expected[5] = { 0xA0, 0x27, 0x94, 0x1F, 0x01 };
    if (memcmp((const void*)0xBE018B, Expected, sizeof(Expected)) != 0) {
        Logger::Log("[WARNING] Skin: unexpected code at 0xBE018B; interior faces keep vanilla's passes");
        return;
    }
    SafeWrite32(0xBE018C, (UInt32)&FaceGenInteriorFlag);
}

void UpdateFaceGenInteriorFlag() {
    const UInt8 Interior = *(UInt8*)0x011F9427;
    const bool NVRSkin = TheShaderManager->Shaders.Skin && TheShaderManager->Shaders.Skin->Enabled && TheSettingManager->SettingsMain.Main.RenderEffects;
    FaceGenInteriorFlag = NVRSkin ? 0 : Interior;
}
