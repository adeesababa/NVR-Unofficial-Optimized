// End-to-end test of the NVR DXVK extension on dxvk-native (Linux, SDL2 window, any Vulkan device).
// Mirrors what NVR's AvgLuma compute path does: two dispatches, a buffer-to-texture copy,
// a readback, then a D3D9 clear + present to make sure the device keeps working.
#include <windows.h>
#include <d3d9.h>
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "d3d9_nvr_ext_api.h"

static std::vector<char> readFile(const char* path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) { std::printf("cannot open %s\n", path); std::exit(2); }
  size_t n = size_t(f.tellg()); f.seekg(0);
  std::vector<char> v(n); f.read(v.data(), n); return v;
}

static float lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
static float halfToFloat(uint16_t h) {
  uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff; float v;
  if (e == 0) v = std::ldexp(float(m), -24); else if (e == 31) v = m ? NAN : INFINITY; else v = std::ldexp(float(m + 1024), int(e) - 25);
  return s ? -v : v;
}

#define CHECK(hr, what) do { HRESULT _hr = (hr); if (FAILED(_hr)) { std::printf("FAIL: %s -> 0x%08x\n", what, unsigned(_hr)); return 1; } } while (0)

struct Push {
  uint32_t width, height, groupsX, groupsY, phase, samplerIndex, pad0, pad1;
  float motionBlur[4], dof[4], gameTime[4], exposure[4], camera[4];
};

int main(int argc, char** argv) {
  const char* spvPath = argc > 1 ? argv[1] : "AvgLuma.spv";
  const UINT W = 67, H = 41; // odd sizes: partial tiles

  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window* win = SDL_CreateWindow("nvrtest", 0, 0, 64, 48, SDL_WINDOW_VULKAN);
  if (!win) { std::printf("SDL window failed: %s\n", SDL_GetError()); return 1; }

  IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = (HWND)win;
  pp.BackBufferWidth = 64; pp.BackBufferHeight = 48; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  IDirect3DDevice9* dev = nullptr;
  CHECK(d3d->CreateDevice(0, D3DDEVTYPE_HAL, (HWND)win, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev), "CreateDevice");

  ID3D9NvrExt* ext = nullptr;
  CHECK(dev->QueryInterface(__uuidof(ID3D9NvrExt), (void**)&ext), "QueryInterface(ID3D9NvrExt)");
  NVR_EXT_INFO info; ext->GetInfo(&info);
  std::printf("NVR ext %u.%u on DXVK %s, push %u, slots %u, subgroup %u, max wg %u\n",
    info.ExtVersionMajor, info.ExtVersionMinor, info.DxvkVersion, info.MaxPushBytes, info.MaxSlots, info.SubgroupSize, info.MaxWorkgroupInvocations);

  // Textures as NVR has them: HDR colour, R32F linear depth, the 1x1 result.
  IDirect3DTexture9 *color, *depth, *avg;
  CHECK(dev->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &color, nullptr), "color tex");
  CHECK(dev->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &depth, nullptr), "depth tex");
  CHECK(dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &avg, nullptr), "avg tex");
  IDirect3DSurface9 *colorS, *depthS, *avgS;
  color->GetSurfaceLevel(0, &colorS); depth->GetSurfaceLevel(0, &depthS); avg->GetSurfaceLevel(0, &avgS);

  // Fill them through plain D3D9 so the compute pass has to wait for D3D9's work.
  const float cr = 128 / 255.f, cg = 64 / 255.f, cb = 32 / 255.f, dz = 64 / 255.f;
  CHECK(dev->SetRenderTarget(0, colorS), "SetRenderTarget color");
  CHECK(dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 128, 64, 32), 1.f, 0), "clear color");
  CHECK(dev->SetRenderTarget(0, depthS), "SetRenderTarget depth");
  CHECK(dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 64, 0, 0), 1.f, 0), "clear depth");
  IDirect3DSurface9* bb; dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
  CHECK(dev->SetRenderTarget(0, bb), "SetRenderTarget bb");

  // Pipeline
  std::vector<char> spv = readFile(spvPath);
  NVR_EXT_BINDING_DESC binds[] = {
    { 1, 0, NVR_EXT_BIND_SAMPLED_IMAGE,  0, NVR_EXT_VIEW_2D, FALSE, 0 },
    { 1, 1, NVR_EXT_BIND_SAMPLED_IMAGE,  1, NVR_EXT_VIEW_2D, FALSE, 0 },
    { 1, 2, NVR_EXT_BIND_STORAGE_BUFFER, 0, NVR_EXT_VIEW_2D, TRUE,  0 },
    { 1, 3, NVR_EXT_BIND_STORAGE_BUFFER, 1, NVR_EXT_VIEW_2D, FALSE, 0 },
    { 1, 4, NVR_EXT_BIND_STORAGE_BUFFER, 2, NVR_EXT_VIEW_2D, TRUE,  0 },
    { 0, 0, NVR_EXT_BIND_SAMPLER,        0, NVR_EXT_VIEW_2D, FALSE, offsetof(Push, samplerIndex) },
  };
  NVR_EXT_COMPUTE_PIPELINE_DESC pd = {};
  pd.pSpirv = spv.data(); pd.SpirvSize = UINT(spv.size());
  pd.BindingCount = 6; pd.pBindings = binds; pd.PushConstantSize = sizeof(Push);
  pd.UsesSamplerHeap = TRUE; pd.SamplerHeapSet = 0; pd.SamplerHeapBinding = 0; pd.pDebugName = "AvgLuma test";
  ID3D9NvrExtPipeline* pipe = nullptr;
  CHECK(ext->CreateComputePipeline(&pd, &pipe), "CreateComputePipeline");

  const UINT gx = (W + 15) / 16, gy = (H + 15) / 16;
  NVR_EXT_BUFFER_DESC bd = { gx * gy * 4, NVR_EXT_BUFFER_STORAGE, "partials" };
  ID3D9NvrExtBuffer *partials, *stateA, *stateB;
  CHECK(ext->CreateBuffer(&bd, &partials), "partials");
  bd = { 32, NVR_EXT_BUFFER_STORAGE | NVR_EXT_BUFFER_TRANSFER_SRC | NVR_EXT_BUFFER_TRANSFER_DST, "stateA" };
  CHECK(ext->CreateBuffer(&bd, &stateA), "stateA");
  CHECK(ext->CreateBuffer(&bd, &stateB), "stateB");
  CHECK(ext->ClearBuffer(stateA, 0, 0, 0), "clear A");
  CHECK(ext->ClearBuffer(stateB, 0, 0, 0), "clear B");
  NVR_EXT_SAMPLER_DESC sd = { NVR_EXT_FILTER_LINEAR, NVR_EXT_ADDRESS_CLAMP };
  ID3D9NvrExtSampler* sampler; CHECK(ext->CreateSampler(&sd, &sampler), "sampler");

  Push push = {};
  push.width = W; push.height = H; push.groupsX = gx; push.groupsY = gy;
  push.dof[0] = 0.001f; push.gameTime[3] = 1.f; push.exposure[2] = 1.f; push.exposure[3] = 1.f;
  push.camera[0] = 0.1f; push.camera[1] = 1.f;

  float expectedAvg = 0.2126f * lin(cr) + 0.7152f * lin(cg) + 0.0722f * lin(cb);
  float prev[3] = { 0, 0, 0 };

  for (int frame = 0; frame < 3; frame++) {
    ID3D9NvrExtBuffer* in  = (frame & 1) ? stateB : stateA;
    ID3D9NvrExtBuffer* out = (frame & 1) ? stateA : stateB;
    NVR_EXT_RESOURCE_BINDING rb[] = {
      { 0, NVR_EXT_BIND_SAMPLED_IMAGE,  color,    0, 0, 0, FALSE },
      { 1, NVR_EXT_BIND_SAMPLED_IMAGE,  depth,    0, 0, 0, FALSE },
      { 0, NVR_EXT_BIND_STORAGE_BUFFER, partials, 0, 0, 0, FALSE },
      { 1, NVR_EXT_BIND_STORAGE_BUFFER, in,       0, 0, 0, FALSE },
      { 2, NVR_EXT_BIND_STORAGE_BUFFER, out,      0, 0, 0, FALSE },
      { 0, NVR_EXT_BIND_SAMPLER,        sampler,  0, 0, 0, FALSE },
    };
    NVR_EXT_DISPATCH_DESC dd = {};
    dd.pPipeline = pipe; dd.BindingCount = 6; dd.pBindings = rb; dd.pPushData = &push; dd.PushDataSize = sizeof(push);
    push.phase = 0; dd.GroupsX = gx; dd.GroupsY = gy; dd.GroupsZ = 1;
    CHECK(ext->Dispatch(&dd), "dispatch 0");
    push.phase = 1; dd.GroupsX = 1; dd.GroupsY = 1;
    CHECK(ext->Dispatch(&dd), "dispatch 1");
    CHECK(ext->CopyBufferToTexture(avg, 0, 0, out, 16), "CopyBufferToTexture");

    // Some D3D9 work after it, then the readback (which waits for everything).
    CHECK(dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 10, 20, 30), 1.f, 0), "clear bb");
    CHECK(dev->Present(nullptr, nullptr, nullptr, nullptr), "Present");

    float cur[4] = {}; uint32_t packed[2] = {};
    CHECK(ext->ReadBuffer(out, 0, 16, cur), "ReadBuffer cur");
    CHECK(ext->ReadBuffer(out, 16, 8, packed), "ReadBuffer packed");

    // Expected per the HLSL pass
    float newLuma = (expectedAvg + 7 * prev[0]) / 8;
    float animated = prev[1] + std::fmax(-1.f, std::fmin(1.f, newLuma - prev[1]));
    float depthV = dz * push.camera[1] * 13.f / 12.f;   // every tap reads the same value; the pass divides centre + 12 taps by 12
    float d = std::fmin(1.f, depthV / 1.0f);
    float step = std::pow(std::fmax(prev[2], d), 0.1f);
    float diff = std::fmax(-3 * step, std::fmin(step, d - prev[2]));
    float focus = prev[2] + diff;

    // The texture: GetRenderTargetData into system memory
    IDirect3DSurface9* sys; CHECK(dev->CreateOffscreenPlainSurface(1, 1, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys, nullptr), "sysmem");
    CHECK(dev->GetRenderTargetData(avgS, sys), "GetRenderTargetData");
    D3DLOCKED_RECT lr; CHECK(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY), "LockRect");
    uint16_t* hp = (uint16_t*)lr.pBits;
    float tex[4] = { halfToFloat(hp[0]), halfToFloat(hp[1]), halfToFloat(hp[2]), halfToFloat(hp[3]) };
    sys->UnlockRect(); sys->Release();

    std::printf("frame %d: compute (%.6f %.6f %.6f %.3f) expected (%.6f %.6f %.6f 1) texture (%.5f %.5f %.5f %.3f)\n",
      frame, cur[0], cur[1], cur[2], cur[3], newLuma, animated, focus, tex[0], tex[1], tex[2], tex[3]);

    bool ok = std::fabs(cur[0] - newLuma) < 1e-4f && std::fabs(cur[1] - animated) < 1e-4f && std::fabs(cur[2] - focus) < 1e-4f && cur[3] == 1.f
           && std::fabs(tex[0] - newLuma) < 2e-3f && std::fabs(tex[1] - animated) < 2e-3f && std::fabs(tex[2] - focus) < 2e-3f && tex[3] == 1.f;
    if (!ok) { std::printf("FAIL: values differ\n"); return 1; }
    prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
  }

  // Error paths must fail cleanly, not crash.
  NVR_EXT_RESOURCE_BINDING bad[] = { { 7, NVR_EXT_BIND_SAMPLED_IMAGE, color, 0, 0, 0, FALSE } };
  NVR_EXT_DISPATCH_DESC dd = {}; dd.pPipeline = pipe; dd.BindingCount = 1; dd.pBindings = bad; dd.GroupsX = dd.GroupsY = dd.GroupsZ = 1;
  if (SUCCEEDED(ext->Dispatch(&dd))) { std::printf("FAIL: undeclared slot accepted\n"); return 1; }
  NVR_EXT_COMPUTE_PIPELINE_DESC badPd = pd; badPd.SpirvSize = 8; ID3D9NvrExtPipeline* badPipe = nullptr;
  if (SUCCEEDED(ext->CreateComputePipeline(&badPd, &badPipe))) { std::printf("FAIL: bad SPIR-V accepted\n"); return 1; }

  pipe->Release(); partials->Release(); stateA->Release(); stateB->Release(); sampler->Release();
  colorS->Release(); depthS->Release(); avgS->Release(); bb->Release(); color->Release(); depth->Release(); avg->Release();
  ext->Release(); dev->Release(); d3d->Release();
  SDL_DestroyWindow(win); SDL_Quit();
  std::printf("PASS\n");
  return 0;
}
