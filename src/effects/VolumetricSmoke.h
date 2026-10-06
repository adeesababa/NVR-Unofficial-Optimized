#pragma once

#include "VolumetricSmokeQuads.h"
#include "../core/GpuProfiler.h"

namespace VolumetricSmoke {
    inline const int FetchMax = 720;
    inline const float NearFade = 6.0f;
    inline VolumetricSmokeQuads::Vertex Vertices[FetchMax * 6];
    inline const float BigArea = 0.01f;
    inline VolumetricSmokeQuads::Vertex VerticesBig[FetchMax * 6];
    inline int CountBig = 0;
    inline float Shaded = 0.0f;
    inline D3DXVECTOR4 Info = {}, Params = {}, Flash = {}, FlashColor = {}, Look = {};
    inline int Count = 0;
    inline unsigned FrameNumber = 0, FetchedFrame = ~0u;
    inline LARGE_INTEGER EnvAt = {};
    inline RECT Scissor = {};
    inline IDirect3DTexture9* Texture = nullptr;
    inline IDirect3DSurface9* Surface = nullptr;
    inline IDirect3DTexture9* FullTexture = nullptr;
    inline IDirect3DSurface9* FullSurface = nullptr;
    inline IDirect3DTexture9* HalfTexture = nullptr;
    inline IDirect3DSurface9* HalfSurface = nullptr;
    inline IDirect3DTexture9* LowTexture = nullptr;
    inline IDirect3DSurface9* LowSurface = nullptr;
    inline IDirect3DTexture9* HazeTexture = nullptr;
    inline IDirect3DTexture9* CutTexture = nullptr;
    inline IDirect3DSurface9* CutSurface = nullptr;
    inline IDirect3DSurface9* HazeSurface = nullptr;
    inline IDirect3DTexture9* EnvTexture = nullptr;
    inline IDirect3DSurface9* EnvSurface = nullptr;
    inline const int HazeMax = 40;
    inline D3DXVECTOR4 HazeShape[HazeMax] = {}, HazeLook[HazeMax] = {};
    inline int HazeCount = 0;
    inline const int BulletMax = 16;
    inline D3DXVECTOR4 BulletFrom[BulletMax] = {}, BulletWay[BulletMax] = {};
    inline int BulletCount = 0;
    inline D3DXVECTOR2 BulletLook(0.0f, 0.0f);
    inline float BulletMoving = 0.0f;
    inline IDirect3DVertexDeclaration9* Declaration = nullptr;

    struct BufferState {
        IDirect3DSurface9* Known = nullptr;
        bool Dirty = true;
        bool NeedsClear(IDirect3DSurface9* surface) const { return Dirty || Known != surface; }
        void Cleared(IDirect3DSurface9* surface) { Known = surface; Dirty = false; }
        void Drawn(IDirect3DSurface9* surface) { Known = surface; Dirty = true; }
        void Forget() { Known = nullptr; Dirty = true; }
    };
    inline BufferState FullState, HalfState, LowState, HazeState;

    struct EffectHandles {
        ID3DXEffect* Owner = nullptr;
        D3DXHANDLE SegmentsTechnique = NULL, HazeTechnique = NULL, EnvTechnique = NULL;
        D3DXHANDLE SmokeInfo = NULL, SmokeParams = NULL, SmokeFlash = NULL, SmokeFlashColor = NULL, SmokeLook = NULL;
        D3DXHANDLE BulletFrom = NULL, BulletWay = NULL, BulletMove = NULL, HazeShape = NULL, HazeLook = NULL, HazeInfo = NULL, EnvInfo = NULL;
    };
    inline EffectHandles CachedHandles;
    inline const EffectHandles& ResolveHandles(ID3DXEffect* effect) {
        if (CachedHandles.Owner == effect) return CachedHandles;
        EffectHandles found;
        found.Owner = effect;
        found.SegmentsTechnique = effect->GetTechniqueByName("VolumetricSmokeSegments");
        found.HazeTechnique = effect->GetTechniqueByName("VolumetricSmokeHaze");
        found.EnvTechnique = effect->GetTechniqueByName("VolumetricSmokeEnv");
        found.SmokeInfo = effect->GetParameterByName(NULL, "SmokeInfo");
        found.SmokeParams = effect->GetParameterByName(NULL, "SmokeParams");
        found.SmokeFlash = effect->GetParameterByName(NULL, "SmokeFlash");
        found.SmokeFlashColor = effect->GetParameterByName(NULL, "SmokeFlashColor");
        found.SmokeLook = effect->GetParameterByName(NULL, "SmokeLook");
        found.BulletFrom = effect->GetParameterByName(NULL, "BulletFrom");
        found.BulletWay = effect->GetParameterByName(NULL, "BulletWay");
        found.BulletMove = effect->GetParameterByName(NULL, "BulletMove");
        found.HazeShape = effect->GetParameterByName(NULL, "HazeShape");
        found.HazeLook = effect->GetParameterByName(NULL, "HazeLook");
        found.HazeInfo = effect->GetParameterByName(NULL, "HazeInfo");
        found.EnvInfo = effect->GetParameterByName(NULL, "EnvInfo");
        effect->AddRef();
        if (CachedHandles.Owner) CachedHandles.Owner->Release();
        CachedHandles = found;
        return CachedHandles;
    }

    inline void ClearBuffer(IDirect3DDevice9* device, IDirect3DSurface9* surface, BufferState& state) {
        device->SetRenderTarget(0, surface);
        device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
        state.Cleared(surface);
    }

    inline int Fetch() {
        if (FetchedFrame == FrameNumber) return Count + CountBig + HazeCount;
        FetchedFrame = FrameNumber;
        static CpuTimer fetchCpuTimer("Smoke fetch (CPU)");
        CpuProfileScope fetchCpu(fetchCpuTimer);
        const ULONGLONG now = GetTickCount64();
        typedef int (__cdecl* GetFn)(float*, int, float*);
        typedef void (__cdecl* FlashFn)(float*);
        typedef int (__cdecl* HazeFn)(float*, int);
        typedef void (__cdecl* ViewFn)(const float*);
        typedef int (__cdecl* BulletsFn)(float*, int);
        typedef void (__cdecl* BulletLookFn)(float*);
        typedef float (__cdecl* MovingFn)();
        static GetFn get = nullptr;
        static FlashFn getFlash = nullptr;
        static HazeFn getHaze = nullptr;
        static ViewFn setView = nullptr;
        static BulletsFn getBullets = nullptr;
        static BulletLookFn getBulletLook = nullptr;
        static MovingFn getMoving = nullptr;
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
                getHaze = (HazeFn)GetProcAddress(gunfx, "GunFX_GetSmokeHaze");
                setView = (ViewFn)GetProcAddress(gunfx, "GunFX_SetView");
                getBullets = (BulletsFn)GetProcAddress(gunfx, "GunFX_GetBullets");
                getBulletLook = (BulletLookFn)GetProcAddress(gunfx, "GunFX_GetBulletLook");
                getMoving = (MovingFn)GetProcAddress(gunfx, "GunFX_GetMoving");
            }
        }
        if (setView) {
            const D3DXMATRIX& view = TheRenderManager->InvViewMatrix;
            const D3DXVECTOR4& eye = TheRenderManager->CameraPosition;
            const float l = sqrtf(view._31 * view._31 + view._32 * view._32 + view._33 * view._33);
            if (l > 0.5f) {
                const float v[6] = { eye.x, eye.y, eye.z, view._31 / l, view._32 / l, view._33 / l };
                setView(v);
            }
        }
        Count = CountBig = HazeCount = BulletCount = 0;
        if (!get || !FullSurface) return 0;
        static float records[FetchMax * 16];
        float params[8] = {};
        int total = get(records, FetchMax, params);
        if (total < 0 || total > FetchMax) total = 0;
        static float haze[HazeMax * 8];
        const bool hazeOn = TheSettingManager->GetSettingI("Main.GunFX.Main", "SmokeHaze") != 0;
        const bool tunnelsOn = TheSettingManager->GetSettingI("Main.GunFX.Main", "BulletTunnels") != 0;
        int hazeTotal = hazeOn && getHaze && HazeSurface && EnvSurface ? getHaze(haze, HazeMax) : 0;
        if (hazeTotal < 0 || hazeTotal > HazeMax) hazeTotal = 0;
        if (!total && !hazeTotal) return 0;
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
        float bounds[4] = {};
        Shaded = 0.0f;
        const bool lowRes = LowSurface && TheSettingManager->GetSettingI("Main.GunFX.Main", "VolumetricSmokeLowRes");
        if (total)
            Count = VolumetricSmokeQuads::Build(records, stride, total, TheRenderManager->ViewProjMatrix, sx, sy, NearFade, Vertices, FetchMax, bounds,
                lowRes ? VerticesBig : nullptr, &CountBig, BigArea, &Shaded);
        for (int i = 0; i < hazeTotal; i++) {
            const float* b = haze + i * 8;
            if (b[3] <= 0.0f || b[4] <= 0.0f) continue;
            HazeShape[HazeCount] = D3DXVECTOR4(b[0] + inv._41 - camera.x, b[1] + inv._42 - camera.y, b[2] + inv._43 - camera.z, b[3]);
            HazeLook[HazeCount] = D3DXVECTOR4(b[4], 0.0f, 0.0f, 0.0f);
            HazeCount++;
        }
        if ((Count || CountBig || HazeCount) && getBullets && tunnelsOn) {
            static float bullets[BulletMax * 8];
            int n = getBullets(bullets, BulletMax);
            if (getBulletLook) { float look[2] = {}; getBulletLook(look); BulletLook = D3DXVECTOR2(look[0], look[1]); }
            if (n < 0 || n > BulletMax) n = 0;
            for (int i = 0; i < n; i++) {
                const float* b = bullets + i * 8;
                BulletFrom[BulletCount] = D3DXVECTOR4(b[0] + inv._41 - camera.x, b[1] + inv._42 - camera.y, b[2] + inv._43 - camera.z, b[3]);
                BulletWay[BulletCount] = D3DXVECTOR4(b[4], b[5], b[6], b[7]);
                BulletCount++;
            }
        }
        BulletMoving = getMoving ? getMoving() : 0.0f;
        if (!(BulletMoving >= 0.0f && BulletMoving <= 1.0f)) BulletMoving = 0.0f;
        float flash[8] = {};
        if (getFlash) getFlash(flash);
        Flash = D3DXVECTOR4(flash[0] + inv._41 - camera.x, flash[1] + inv._42 - camera.y, flash[2] + inv._43 - camera.z, flash[3] > 0.0f ? flash[3] : 0.0f);
        FlashColor = D3DXVECTOR4(flash[4], flash[5], flash[6], flash[7]);
        TESObjectCELL* cell = Player ? Player->parentCell : nullptr;
        const bool outdoors = cell && (!cell->IsInterior() || (cell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior));
        Info = D3DXVECTOR4((float)Count, params[4] > 0.5f ? 1.0f : 0.0f, outdoors ? 1.0f : 0.0f, params[5] > 0.5f ? 1.0f : 0.0f);
        Params = D3DXVECTOR4(params[0], params[1], params[2], params[3]);
        Look = D3DXVECTOR4(params[6], params[7], 1.0f, 0.0f);
        const float w = (float)TheRenderManager->width, h = (float)TheRenderManager->height;
        if (Count || CountBig) {
            Scissor.left = (LONG)(bounds[0] * w > 2.0f ? floorf(bounds[0] * w) - 2.0f : 0.0f);
            Scissor.top = (LONG)(bounds[1] * h > 2.0f ? floorf(bounds[1] * h) - 2.0f : 0.0f);
            Scissor.right = (LONG)(bounds[2] * w + 2.0f < w ? ceilf(bounds[2] * w) + 2.0f : w);
            Scissor.bottom = (LONG)(bounds[3] * h + 2.0f < h ? ceilf(bounds[3] * h) + 2.0f : h);
            if (Scissor.right <= Scissor.left || Scissor.bottom <= Scissor.top) Count = CountBig = 0;
        }
        if (HazeCount) Scissor = { 0, 0, (LONG)w, (LONG)h };
        static ULONGLONG loggedAt = 0;
        static int logged = 0;
        if (logged < 60 && now - loggedAt >= 1000) {
            loggedAt = now;
            logged++;
            D3DXVECTOR4 clip;
            const D3DXVECTOR4 p(records[0], records[1], records[2], 1.0f);
            D3DXVec4Transform(&clip, &p, &TheRenderManager->ViewProjMatrix);
            const float depth = clip.w != 0.0f ? clip.w : 1.0f;
            Logger::Log("VolumetricSmoke: %d draws for %d segments (%d of them big ones at an eighth of the resolution), %d haze blobs, %.1f screens' worth of pixels shaded, area %ld,%ld-%ld,%ld of %.0fx%.0f; first segment end (%.1f %.1f %.1f) radius %.2f density %.4f -> view depth %.1f, screen (%.2f %.2f); %s%s",
                Count + CountBig, total, CountBig, HazeCount, Shaded, Scissor.left, Scissor.top, Scissor.right, Scissor.bottom, w, h,
                records[0] - inv._41 + camera.x, records[1] - inv._42 + camera.y, records[2] - inv._43 + camera.z, records[3], records[8],
                clip.w, 0.5f + 0.5f * clip.x / depth, 0.5f - 0.5f * clip.y / depth, outdoors ? "outdoors" : "indoors", Info.y > 0.5f ? ", debug view" : "");
        }
        return Count + CountBig + HazeCount;
    }
}

class VolumetricSmokeEffect : public EffectRecord {
public:
    VolumetricSmokeEffect() : EffectRecord("VolumetricSmoke") {}
    void RegisterTextures() override;
    bool ShouldRender() override { return VolumetricSmoke::Fetch() > 0; }
    void Render(IDirect3DDevice9* device, IDirect3DSurface9* target, IDirect3DSurface9* rendered,
        UINT technique, bool clear, IDirect3DSurface9* source) override {
        Enabled = Effect != nullptr;
        if (!Enabled || !ShouldRender()) { renderTime = 0; return; }
        using namespace VolumetricSmoke;
        static CpuTimer renderCpuTimer("Smoke render (CPU)"), clearsCpuTimer("Smoke clears (CPU)"),
            segmentsCpuTimer("Smoke seg draw (CPU)"), lowCpuTimer("Smoke low draw (CPU)"), hazeCpuTimer("Smoke haze (CPU)"),
            envCpuTimer("Smoke env (CPU)"), compositeCpuTimer("Smoke composite (CPU)");
        CpuProfileScope renderCpu(renderCpuTimer);
        const int test = TheSettingManager->GetSettingI("Main.GunFX.Main", "VolumetricSmokeTest");
        if (test == 4) { renderTime = 0; return; }
        const EffectHandles& fx = ResolveHandles(Effect);
        D3DXHANDLE segments = fx.SegmentsTechnique;
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
        const bool half = HalfSurface && TheSettingManager->GetSettingI("Main.GunFX.Main", "VolumetricSmokeHalfRes");
        Texture = half ? HalfTexture : FullTexture;
        Surface = half ? HalfSurface : FullSurface;
        Look.z = half ? 2.0f : 1.0f;
        Look.w = (half || CountBig) ? 1.0f : 0.0f;
        static GpuTimer segmentsTimer("Volumetric smoke: segments"), compositeTimer("Volumetric smoke: composite"),
            hazeTimer("Volumetric smoke: haze");
        D3DXVECTOR4 info = Info, flash = Flash;
        info.x = test == 1 ? -1.0f : 0.0f;
        if (test == 2) flash.w = 0.0f;
        Effect->SetVector(fx.SmokeInfo, &info);
        Effect->SetVector(fx.SmokeParams, &Params);
        Effect->SetVector(fx.SmokeFlash, &flash);
        Effect->SetVector(fx.SmokeFlashColor, &FlashColor);
        Effect->SetVector(fx.SmokeLook, &Look);
        const D3DXVECTOR4 hazeInfo((float)HazeCount, (float)BulletCount, BulletLook.x, BulletLook.y);
        Effect->SetVector(fx.HazeInfo, &hazeInfo);
        if (fx.BulletMove) { const D3DXVECTOR4 move(BulletMoving, 0.0f, 0.0f, 0.0f); Effect->SetVector(fx.BulletMove, &move); }
        if (BulletCount) {
            Effect->SetVectorArray(fx.BulletFrom, BulletFrom, BulletMax);
            Effect->SetVectorArray(fx.BulletWay, BulletWay, BulletMax);
        }

        IDirect3DSurface9* previousTarget = nullptr;
        IDirect3DVertexDeclaration9* previousDeclaration = nullptr;
        IDirect3DVertexBuffer9* previousStream = nullptr;
        UINT previousOffset = 0, previousStride = 0;
        device->GetRenderTarget(0, &previousTarget);
        device->GetVertexDeclaration(&previousDeclaration);
        device->GetStreamSource(0, &previousStream, &previousOffset, &previousStride);
        device->SetRenderTarget(0, Surface);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        BufferState& mainState = half ? HalfState : FullState;
        const bool hazeOverwrites = HazeCount && previousStream && HazeSurface;
        {
            CpuProfileScope clearsCpu(clearsCpuTimer);
            bool movedTarget = false;
            if (mainState.NeedsClear(Surface)) {
                device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
                mainState.Cleared(Surface);
            }
            if (LowSurface && LowState.NeedsClear(LowSurface)) { ClearBuffer(device, LowSurface, LowState); movedTarget = true; }
            if (HazeSurface && !hazeOverwrites && HazeState.NeedsClear(HazeSurface)) { ClearBuffer(device, HazeSurface, HazeState); movedTarget = true; }
            if (movedTarget) device->SetRenderTarget(0, Surface);
        }
        Effect->SetTechnique(segments);
        SetCT();
        device->SetTexture(2, nullptr);
        device->SetTexture(4, nullptr);
        device->SetTexture(5, nullptr);
        device->SetTexture(6, nullptr);
        UINT passes = 0;
        {
            GpuProfileScope segmentsScope(segmentsTimer, device);
            if (Count) {
                CpuProfileScope segmentsCpu(segmentsCpuTimer);
                mainState.Drawn(Surface);
                if (SUCCEEDED(Effect->Begin(&passes, 0))) {
                    Effect->BeginPass(0);
                    device->SetVertexDeclaration(Declaration);
                    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, Count * 2, Vertices, sizeof(VolumetricSmokeQuads::Vertex));
                    Effect->EndPass();
                    Effect->End();
                }
            }
            if (CountBig && LowSurface) {
                CpuProfileScope lowCpu(lowCpuTimer);
                LowState.Drawn(LowSurface);
                device->SetRenderTarget(0, LowSurface);
                const D3DXVECTOR4 lowLook(Look.x, Look.y, 8.0f, 1.0f);
                Effect->SetVector(fx.SmokeLook, &lowLook);
                if (SUCCEEDED(Effect->Begin(&passes, 0))) {
                    Effect->BeginPass(0);
                    device->SetVertexDeclaration(Declaration);
                    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, CountBig * 2, VerticesBig, sizeof(VolumetricSmokeQuads::Vertex));
                    Effect->EndPass();
                    Effect->End();
                }
                Effect->SetVector(fx.SmokeLook, &Look);
            }
        }
        if (previousDeclaration) device->SetVertexDeclaration(previousDeclaration);
        device->SetStreamSource(0, previousStream, previousOffset, previousStride);
        if (previousStream) {
            auto fullScreen = [&](D3DXHANDLE pass, IDirect3DSurface9* into) -> bool {
                if (!pass || !into) return false;
                device->SetRenderTarget(0, into);
                Effect->SetTechnique(pass);
                if (!SUCCEEDED(Effect->Begin(&passes, 0))) return false;
                Effect->BeginPass(0);
                device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
                Effect->EndPass();
                Effect->End();
                return true;
            };
            const bool cutNeeded = BulletCount && (Count || CountBig) && CutSurface;
            if (HazeCount || cutNeeded) {
                GpuProfileScope hazeScope(hazeTimer, device);
                CpuProfileScope hazeCpu(hazeCpuTimer);
                const D3DXVECTOR4 hazeLook(Look.x, Look.y, 4.0f, 1.0f);
                Effect->SetVectorArray(fx.HazeShape, HazeShape, HazeMax);
                Effect->SetVectorArray(fx.HazeLook, HazeLook, HazeMax);
                Effect->SetVector(fx.SmokeLook, &hazeLook);
                if (HazeSurface) HazeState.Drawn(HazeSurface);
                device->SetTexture(2, Texture);
                device->SetTexture(4, LowSurface ? LowTexture : nullptr);
                device->SetTexture(7, nullptr);
                DWORD oldWriteMask1 = 0xF;
                device->GetRenderState(D3DRS_COLORWRITEENABLE1, &oldWriteMask1);
                if (CutSurface) {
                    device->SetRenderTarget(1, CutSurface);
                    device->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
                }
                const bool drew = fullScreen(fx.HazeTechnique, HazeSurface);
                if (CutSurface) {
                    device->SetRenderTarget(1, nullptr);
                    device->SetRenderState(D3DRS_COLORWRITEENABLE1, oldWriteMask1);
                }
                device->SetTexture(2, nullptr);
                device->SetTexture(4, nullptr);
                if (!drew && HazeSurface) ClearBuffer(device, HazeSurface, HazeState);
                Effect->SetVector(fx.SmokeLook, &Look);
            }
            {
                CpuProfileScope envCpu(envCpuTimer);
                LARGE_INTEGER now, frequency;
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&frequency);
                const double dt = EnvAt.QuadPart ? double(now.QuadPart - EnvAt.QuadPart) / double(frequency.QuadPart) : 1e9;
                EnvAt = now;
                const D3DXVECTOR4 envInfo(dt > 0.0 && dt < 1.0 ? 1.0f - expf(-(float)dt / 0.3f) : 1.0f, 0.0f, 0.0f, 0.0f);
                Effect->SetVector(fx.EnvInfo, &envInfo);
                if (envInfo.x >= 1.0f && EnvSurface) {
                    device->SetRenderTarget(0, EnvSurface);
                    device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
                }
                fullScreen(fx.EnvTechnique, EnvSurface);
            }
        }
        device->SetRenderTarget(0, previousTarget);
        if (previousTarget) previousTarget->Release();
        if (previousDeclaration) previousDeclaration->Release();
        if (previousStream) previousStream->Release();

        const long long screen = (long long)TheRenderManager->width * TheRenderManager->height;
        const bool whole = 2LL * (Scissor.right - Scissor.left) * (Scissor.bottom - Scissor.top) >= screen;
        if (!whole) {
            device->SetScissorRect(&Scissor);
            device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        }
        if (test != 3) {
            GpuProfileScope composite(compositeTimer, device);
            CpuProfileScope compositeCpu(compositeCpuTimer);
            writesWholeTarget = whole;
            EffectRecord::Render(device, target, rendered, technique, clear, source);
            writesWholeTarget = false;
        }
        if (!whole) device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    }
};
