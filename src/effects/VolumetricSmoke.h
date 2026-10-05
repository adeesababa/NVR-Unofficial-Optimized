#pragma once

#include "VolumetricSmokeQuads.h"

// GunFX volumetric smoke (prototype). GunFX.dll keeps its smoke as chains of points and hands over one record per tube
// segment (GunFX_GetVolumeSmoke). Step 1 draws every segment that can be on screen as a quad over the area it reaches,
// adding its optical depth into TESR_SmokeBuffer; step 2 (the effect chain's pass) lights the total and lays it over
// the image, only inside the area the smoke covers (scissor). The cost follows the smoke's size on screen.
namespace VolumetricSmoke {
    inline const int FetchMax = 640;      // what GunFX sends at most (four chains and two twin arms of 96 points, plus tendrils)
    inline const float NearFade = 6.0f;   // the shader hides smoke closer to the camera than this (game units)
    inline VolumetricSmokeQuads::Vertex Vertices[FetchMax * 6];
    inline D3DXVECTOR4 Info = {}, Params = {}, Flash = {}, FlashColor = {}, Look = {};
    inline int Count = 0;                 // segments drawn this frame
    inline ULONGLONG FetchedAt = 0;
    inline RECT Scissor = {};
    inline IDirect3DTexture9* Texture = nullptr;
    inline IDirect3DSurface9* Surface = nullptr;
    inline IDirect3DVertexDeclaration9* Declaration = nullptr;

    // Segments for this frame (fetched once; ShouldRender and Render both ask).
    inline int Fetch() {
        const ULONGLONG now = GetTickCount64();
        if (FetchedAt == now) return Count;
        FetchedAt = now;
        // GunFX_GetVolumeSmoke2 gives 16 floats per record (with the smoke's own noise coordinate and the age at both
        // ends); an older GunFX only has GunFX_GetVolumeSmoke (12). GunFX_GetSmokeFlash: the muzzle flash's light.
        typedef int (__cdecl* GetFn)(float*, int, float*);
        typedef void (__cdecl* FlashFn)(float*);
        static GetFn get = nullptr;
        static FlashFn getFlash = nullptr;
        static int stride = 12;
        static bool looked = false;
        if (!looked) {
            looked = true;
            HMODULE gunfx = GetModuleHandleA("GunFX.dll");
            if (gunfx) {
                get = (GetFn)GetProcAddress(gunfx, "GunFX_GetVolumeSmoke2");
                if (get) stride = 16;
                else get = (GetFn)GetProcAddress(gunfx, "GunFX_GetVolumeSmoke");
                getFlash = (FlashFn)GetProcAddress(gunfx, "GunFX_GetSmokeFlash");
            }
        }
        Count = 0;
        if (!get || !Surface) return 0;
        static float records[FetchMax * 16];
        float params[8] = {};
        int total = get(records, FetchMax, params);
        if (total < 0 || total > FetchMax) total = 0;
        if (!total) return 0;
        // GunFX's points are absolute world positions; NVR's world space is centred on the camera (objects are drawn at
        // their position minus CameraPosition, and the eye sits at the inverse view's origin). Convert every end into it.
        const D3DXVECTOR4& camera = TheRenderManager->CameraPosition;
        const D3DXMATRIX& inv = TheRenderManager->InvViewMatrix;
        for (int i = 0; i < total; i++) {
            float* r = records + i * stride;
            for (int e = 0; e < 2; e++) {
                r[e * 4] += inv._41 - camera.x;
                r[e * 4 + 1] += inv._42 - camera.y;
                r[e * 4 + 2] += inv._43 - camera.z;
            }
        }
        const float sx = TheRenderManager->InvProjMatrix._11 != 0.0f ? 1.0f / TheRenderManager->InvProjMatrix._11 : 1.0f;
        const float sy = TheRenderManager->InvProjMatrix._22 != 0.0f ? 1.0f / TheRenderManager->InvProjMatrix._22 : 1.0f;
        float bounds[4];
        Count = VolumetricSmokeQuads::Build(records, stride, total, TheRenderManager->ViewProjMatrix, sx, sy, NearFade, Vertices, FetchMax, bounds);
        // The muzzle flash (x, y, z absolute, light, r, g, b, reach), into NVR's space like the smoke.
        float flash[8] = {};
        if (getFlash) getFlash(flash);
        Flash = D3DXVECTOR4(flash[0] + inv._41 - camera.x, flash[1] + inv._42 - camera.y, flash[2] + inv._43 - camera.z, flash[3] > 0.0f ? flash[3] : 0.0f);
        FlashColor = D3DXVECTOR4(flash[4], flash[5], flash[6], flash[7]);
        TESObjectCELL* cell = Player ? Player->parentCell : nullptr;
        const bool outdoors = cell && (!cell->IsInterior() || (cell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior));
        Info = D3DXVECTOR4((float)Count, params[4] > 0.5f ? 1.0f : 0.0f, outdoors ? 1.0f : 0.0f, params[5] > 0.5f ? 1.0f : 0.0f);
        Params = D3DXVECTOR4(params[0], params[1], params[2], params[3]);
        Look = D3DXVECTOR4(params[6], params[7], 0.0f, 0.0f);   // colour by thickness and age (older GunFX: 0, plain grey)
        const float w = (float)TheRenderManager->width, h = (float)TheRenderManager->height;
        if (Count) {
            Scissor.left = (LONG)(bounds[0] * w > 2.0f ? floorf(bounds[0] * w) - 2.0f : 0.0f);
            Scissor.top = (LONG)(bounds[1] * h > 2.0f ? floorf(bounds[1] * h) - 2.0f : 0.0f);
            Scissor.right = (LONG)(bounds[2] * w + 2.0f < w ? ceilf(bounds[2] * w) + 2.0f : w);
            Scissor.bottom = (LONG)(bounds[3] * h + 2.0f < h ? ceilf(bounds[3] * h) + 2.0f : h);
            if (Scissor.right <= Scissor.left || Scissor.bottom <= Scissor.top) Count = 0;
        }
        // Where the smoke is (once a second, 20 times per session): for checking positions in the log.
        static ULONGLONG loggedAt = 0;
        static int logged = 0;
        if (logged < 20 && now - loggedAt >= 1000) {
            loggedAt = now;
            logged++;
            D3DXVECTOR4 clip;
            const D3DXVECTOR4 p(records[0], records[1], records[2], 1.0f);
            D3DXVec4Transform(&clip, &p, &TheRenderManager->ViewProjMatrix);
            const float depth = clip.w != 0.0f ? clip.w : 1.0f;
            Logger::Log("VolumetricSmoke: %d of %d segments on screen, area %ld,%ld-%ld,%ld of %.0fx%.0f; first segment end (%.1f %.1f %.1f) radius %.2f density %.4f -> view depth %.1f, screen (%.2f %.2f); %s%s",
                Count, total, Scissor.left, Scissor.top, Scissor.right, Scissor.bottom, w, h,
                records[0] - inv._41 + camera.x, records[1] - inv._42 + camera.y, records[2] - inv._43 + camera.z, records[3], records[8],
                clip.w, 0.5f + 0.5f * clip.x / depth, 0.5f - 0.5f * clip.y / depth, outdoors ? "outdoors" : "indoors", Info.y > 0.5f ? ", debug view" : "");
        }
        return Count;
    }
}

class VolumetricSmokeEffect : public EffectRecord {
public:
    VolumetricSmokeEffect() : EffectRecord("VolumetricSmoke") {}
    void RegisterTextures() override;   // VolumetricSmoke.cpp (needs the texture manager)
    bool ShouldRender() override { return VolumetricSmoke::Fetch() > 0; }
    void Render(IDirect3DDevice9* device, IDirect3DSurface9* target, IDirect3DSurface9* rendered,
        UINT technique, bool clear, IDirect3DSurface9* source) override {
        // GunFX.ini holds the tuning; the switch is Main > GunFX > VolumetricSmoke.
        Enabled = Effect != nullptr;
        if (!Enabled || !ShouldRender()) { renderTime = 0; return; }
        using namespace VolumetricSmoke;
        D3DXHANDLE segments = Effect->GetTechniqueByName("VolumetricSmokeSegments");
        if (!segments) return;
        if (!Declaration) {
            const D3DVERTEXELEMENT9 elements[] = {
                { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
                { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
                { 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
                { 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
                { 0, 64, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 },
                D3DDECL_END() };
            if (FAILED(device->CreateVertexDeclaration(elements, &Declaration))) { Declaration = nullptr; return; }
        }
        Effect->SetVector(Effect->GetParameterByName(NULL, "SmokeInfo"), &Info);
        Effect->SetVector(Effect->GetParameterByName(NULL, "SmokeParams"), &Params);
        Effect->SetVector(Effect->GetParameterByName(NULL, "SmokeFlash"), &Flash);
        Effect->SetVector(Effect->GetParameterByName(NULL, "SmokeFlashColor"), &FlashColor);
        Effect->SetVector(Effect->GetParameterByName(NULL, "SmokeLook"), &Look);

        // Step 1: the segments' optical depth into TESR_SmokeBuffer. Everything changed here is put back afterwards
        // (the effect's Begin/End restores the render states it sets).
        IDirect3DSurface9* previousTarget = nullptr;
        IDirect3DVertexDeclaration9* previousDeclaration = nullptr;
        IDirect3DVertexBuffer9* previousStream = nullptr;
        UINT previousOffset = 0, previousStride = 0;
        device->GetRenderTarget(0, &previousTarget);
        device->GetVertexDeclaration(&previousDeclaration);
        device->GetStreamSource(0, &previousStream, &previousOffset, &previousStride);
        device->SetRenderTarget(0, Surface);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
        Effect->SetTechnique(segments);
        SetCT();
        device->SetTexture(2, nullptr);   // TESR_SmokeBuffer is the target here
        UINT passes = 0;
        if (SUCCEEDED(Effect->Begin(&passes, 0))) {
            Effect->BeginPass(0);
            device->SetVertexDeclaration(Declaration);
            device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, Count * 2, Vertices, sizeof(VolumetricSmokeQuads::Vertex));
            Effect->EndPass();
            Effect->End();
        }
        device->SetRenderTarget(0, previousTarget);
        if (previousDeclaration) device->SetVertexDeclaration(previousDeclaration);
        device->SetStreamSource(0, previousStream, previousOffset, previousStride);
        if (previousTarget) previousTarget->Release();
        if (previousDeclaration) previousDeclaration->Release();
        if (previousStream) previousStream->Release();

        // Step 2: light the total and lay it over the image, inside the smoke's area.
        device->SetScissorRect(&Scissor);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        EffectRecord::Render(device, target, rendered, technique, clear, source);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    }
};
