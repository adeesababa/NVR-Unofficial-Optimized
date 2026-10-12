#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;

static void Check(HRESULT hr, const char* what) {
	if (FAILED(hr)) { char text[200]; sprintf_s(text, "%s failed: %08lx", what, hr); throw std::runtime_error(text); }
}

static const UINT W = 96, H = 64;
static const float FarZ = 200000.0f;

static ComPtr<ID3DXEffect> Load(IDirect3DDevice9* device, const char* path) {
	ComPtr<ID3DXEffect> effect; ComPtr<ID3DXBuffer> errors;
	HRESULT hr = D3DXCreateEffectFromFileA(device, path, NULL, NULL, 0, NULL, &effect, &errors);
	if (errors) std::puts((const char*)errors->GetBufferPointer());
	Check(hr, path);
	return effect;
}

struct Case { const char* name; bool exterior; float sunAmount; float distantFogHeight; };

static void SetConstants(ID3DXEffect* e, const Case& c) {
	auto v = [&](const char* name, float x, float y, float z, float w) { D3DXVECTOR4 value(x, y, z, w); e->SetVector(name, &value); };
	v("TESR_ReciprocalResolution", 1.0f / W, 1.0f / H, 0, 0);
	v("TESR_CameraData", 5, FarZ, 0, 0);
	v("TESR_DepthConstants", 0, 0, 0, 0);
	v("TESR_CameraPosition", 1000, 2000, 800, 1);
	v("TESR_FogColor", 0.6f, 0.65f, 0.7f, 1);
	v("TESR_FogData", 500, 60000, 0.5f, 1.5f);
	D3DXVECTOR3 sun(0.3f, 0.5f, 0.25f); D3DXVec3Normalize(&sun, &sun);
	v("TESR_SunPosition", sun.x, sun.y, sun.z, 1);
	v("TESR_SunDirection", sun.x, sun.y, sun.z, 1);
	v("TESR_SunColor", 1, 0.9f, 0.8f, 1);
	v("TESR_SunDiskColor", 1, 0.85f, 0.7f, 1);
	v("TESR_SunAmbient", 0.4f, 0.4f, 0.45f, 1);
	v("TESR_HorizonColor", 0.7f, 0.75f, 0.8f, 1);
	v("TESR_SkyLowColor", 0.5f, 0.6f, 0.8f, 1);
	v("TESR_SkyColor", 0.3f, 0.45f, 0.8f, 1);
	v("TESR_SkyData", 1, 2, 1, 1);
	v("TESR_SunsetColor", 1, 0.6f, 0.3f, 1);
	v("TESR_SunAmount", c.sunAmount, 0, 0, 1);
	v("TESR_VolumetricFogData", 0.2f, 1.7f, 1.0f, 1.6f);
	if (c.exterior) {
		v("TESR_VolumetricFogLow", 1.7f, 0.5f, 0.9f, 0.2f);
		v("TESR_VolumetricFogHigh", 0.6f, 50, 0.4f, 4.0f);
		v("TESR_VolumetricFogSimple", 3.0f, 1.6f, 2.0f, 20);
		v("TESR_VolumetricFogBlend", 0.3f, 1.0f, 1.3f, 0.5f);
		v("TESR_VolumetricFogHeight", c.distantFogHeight, 3.0f, 0.2f, 1);
	}
	else {
		v("TESR_VolumetricFogLow", 0.5f, 0.0f, 0, 0);
		v("TESR_VolumetricFogHigh", 0.6f, 50, 0.4f, 0);
		v("TESR_VolumetricFogSimple", 0.4f, 0.35f, 1, 0);
		v("TESR_VolumetricFogBlend", 0, 1.0f, 1.3f, 0.2f);
		v("TESR_VolumetricFogHeight", 0, 0.0f, 0.5f, 0);
	}
	D3DXMATRIX proj, view;
	D3DXMatrixPerspectiveFovLH(&proj, 1.2f, float(W) / H, 5, FarZ);
	D3DXVECTOR3 eye(0, 0, 0), at(0.8f, 0.55f, -0.08f), up(0, 0, 1);
	D3DXMatrixLookAtLH(&view, &eye, &at, &up);
	e->SetMatrix("TESR_ProjectionTransform", &proj);
	e->SetMatrix("TESR_ViewTransform", &view);
}

static std::vector<float> Render(IDirect3DDevice9* device, ID3DXEffect* effect, D3DXHANDLE technique, const Case& c,
	IDirect3DTexture9* source, IDirect3DTexture9* depth) {
	ComPtr<IDirect3DTexture9> target; ComPtr<IDirect3DSurface9> rt, readback;
	Check(device->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &target, NULL), "target");
	Check(target->GetSurfaceLevel(0, &rt), "rt");
	Check(device->CreateOffscreenPlainSurface(W, H, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, NULL), "readback");
	Check(device->SetRenderTarget(0, rt.Get()), "set rt");
	D3DVIEWPORT9 vp = { 0, 0, W, H, 0, 1 }; device->SetViewport(&vp);
	SetConstants(effect, c);
	Check(effect->SetTechnique(technique), "technique");
	struct Vertex { float x, y, z, u, v; };
	float ox = 0.5f / W, oy = 0.5f / H;
	Vertex quad[] = { {-1, 1, 1, ox, oy}, {-1, -1, 1, ox, 1 + oy}, {1, 1, 1, 1 + ox, oy}, {1, -1, 1, 1 + ox, 1 + oy} };
	device->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
	Check(device->BeginScene(), "begin");
	Check(device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 64, 64, 64), 1.0f, 0), "clear");
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_ZENABLE, FALSE);
	UINT passes = 0; Check(effect->Begin(&passes, 0), "effect begin"); Check(effect->BeginPass(0), "pass");
	device->SetTexture(0, source); device->SetTexture(2, depth);
	Check(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)), "draw");
	effect->EndPass(); effect->End(); device->EndScene();
	Check(device->GetRenderTargetData(rt.Get(), readback.Get()), "readback copy");
	D3DLOCKED_RECT lock; Check(readback->LockRect(&lock, NULL, D3DLOCK_READONLY), "lock");
	std::vector<float> out(W * H * 4);
	for (UINT y = 0; y < H; y++) memcpy(&out[y * W * 4], (char*)lock.pBits + y * lock.Pitch, W * 16);
	readback->UnlockRect();
	return out;
}

int main(int argc, char** argv) {
	if (argc != 3) { std::puts("usage: fog_legacy_compare <old fx> <new fx>"); return 2; }
	try {
		HWND window = CreateWindowExA(0, "STATIC", "fog test", WS_OVERLAPPED, 0, 0, 64, 64, NULL, NULL, GetModuleHandle(NULL), NULL);
		ComPtr<IDirect3D9> d3d; d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
		D3DDISPLAYMODE display = {}; d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &display);
		D3DPRESENT_PARAMETERS pp = {}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
		pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = display.Format;
		ComPtr<IDirect3DDevice9> device;
		Check(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device), "device");

		ComPtr<IDirect3DTexture9> source, depth;
		Check(device->CreateTexture(W, H, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &source, NULL), "source");
		Check(device->CreateTexture(W, H, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &depth, NULL), "depth");
		D3DLOCKED_RECT s, d; source->LockRect(0, &s, NULL, 0); depth->LockRect(0, &d, NULL, 0);
		for (UINT y = 0; y < H; y++) for (UINT x = 0; x < W; x++) {
			float* sp = (float*)((char*)s.pBits + y * s.Pitch) + x * 4;
			float* dp = (float*)((char*)d.pBits + y * d.Pitch) + x * 4;
			sp[0] = float(x) / W; sp[1] = float(y) / H; sp[2] = 0.5f + 0.4f * sinf(x * 0.3f + y * 0.2f); sp[3] = 1;
			float t = float(x + y * W) / (W * H);
			dp[0] = y < H / 6 ? 1.0f : 0.0005f + 0.6f * t * t; dp[1] = dp[2] = 0; dp[3] = 1;
		}
		source->UnlockRect(0); depth->UnlockRect(0);

		ComPtr<ID3DXEffect> oldFx = Load(device.Get(), argv[1]), newFx = Load(device.Get(), argv[2]);
		D3DXHANDLE oldTech = oldFx->GetTechnique(0), newTech = newFx->GetTechniqueByName("VolumetricFogLegacy");
		if (!newTech) throw std::runtime_error("VolumetricFogLegacy technique missing");
		const Case cases[] = {
			{ "exterior day", true, 0.7f, 0.3f },
			{ "exterior sunset", true, 0.35f, 0.3f },
			{ "exterior night", true, 0.02f, 0.3f },
			{ "exterior DistantFogHeight 0", true, 0.7f, 0.0f },
			{ "interior", false, 0.7f, 0.0f },
		};
		int failures = 0;
		for (const Case& c : cases) {
			std::vector<float> a = Render(device.Get(), oldFx.Get(), oldTech, c, source.Get(), depth.Get());
			std::vector<float> b = Render(device.Get(), newFx.Get(), newTech, c, source.Get(), depth.Get());
			float worst = 0, changed = 0; int nan = 0;
			for (size_t i = 0; i < a.size(); i++) {
				if (i % 4 == 3) continue;
				if (!std::isfinite(a[i]) || !std::isfinite(b[i])) { nan++; continue; }
				worst = max(worst, fabsf(a[i] - b[i]));
			}
			for (UINT y = 0; y < H; y++) for (UINT x = 0; x < W; x++) {
				float r = a[(y * W + x) * 4]; changed = max(changed, fabsf(r - float(x) / W));
			}
			bool ok = worst < 2e-3f && nan == 0;
			std::printf("%s %-28s max difference %.6f (fog moved the image up to %.3f), non-finite %d\n",
				ok ? "PASS" : "FAIL", c.name, worst, changed, nan);
			if (!ok) failures++;
			const UINT probes[][2] = { {10, 5}, {48, 20}, {80, 40}, {30, 60} };
			for (auto& q : probes) {
				size_t i = (q[1] * W + q[0]) * 4;
				std::printf("     pixel %2u,%2u  old %.4f %.4f %.4f  new %.4f %.4f %.4f\n", q[0], q[1], a[i], a[i + 1], a[i + 2], b[i], b[i + 1], b[i + 2]);
			}
		}
		{
			std::vector<float> a = Render(device.Get(), oldFx.Get(), oldTech, cases[0], source.Get(), depth.Get());
			std::vector<float> b = Render(device.Get(), newFx.Get(), newFx->GetTechnique(0), cases[0], source.Get(), depth.Get());
			float worst = 0;
			for (size_t i = 0; i < a.size(); i++) if (i % 4 != 3 && std::isfinite(b[i])) worst = max(worst, fabsf(a[i] - b[i]));
			std::printf("%s control (current fog vs old) max difference %.4f (must be large)\n", worst > 0.01f ? "PASS" : "FAIL", worst);
			if (worst <= 0.01f) failures++;
		}
		DestroyWindow(window);
		return failures ? 1 : 0;
	}
	catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
