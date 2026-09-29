// Regression test for LUTEffect's identity-LUT skip (src/effects/LUTIdentity.h).
//
//  1. The shipped neutral_lut.png, loaded by the real D3DX loader exactly as
//     TextureManager::GetFileTexture does, is recognised as an identity.
//  2. Synthetic identity strips (N = 16/32/64) are recognised; a texel off by more than one 8-bit
//     step, a different size, or an unsupported format is not.
//  3. Numerically: sampling an identity strip the way LUT.fx.hlsl does (two bilinear reads blended
//     on blue, HDR-compat scaling) returns the input, so skipping the pass changes nothing.
//
// Uses a NULLREF device (no GPU needed); falls back to REF/HAL if the runtime lacks it.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include <random>

#include "../src/effects/LUTIdentity.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); failures++; } else { std::printf("PASS: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static IDirect3DDevice9* CreateTestDevice(IDirect3D9* d3d)
{
	HWND window = GetDesktopWindow();
	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_UNKNOWN;
	pp.hDeviceWindow = window;
	const D3DDEVTYPE types[] = { D3DDEVTYPE_NULLREF, D3DDEVTYPE_REF, D3DDEVTYPE_HAL };
	for (D3DDEVTYPE type : types) {
		IDirect3DDevice9* device = nullptr;
		if (SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, type, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device)))
			return device;
	}
	return nullptr;
}

// Identity strip: texel (x = b*N + r, y = g) holds (r, g, b) / (N - 1). Memory order B, G, R, A.
static IDirect3DTexture9* MakeStrip(IDirect3DDevice9* device, UINT n, D3DFORMAT format = D3DFMT_A8R8G8B8, UINT width = 0)
{
	IDirect3DTexture9* texture = nullptr;
	if (FAILED(device->CreateTexture(width ? width : n * n, n, 1, 0, format, D3DPOOL_MANAGED, &texture, NULL))) return nullptr;
	D3DLOCKED_RECT rect;
	if (FAILED(texture->LockRect(0, &rect, NULL, 0))) { texture->Release(); return nullptr; }
	if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8) {
		const UINT texWidth = width ? width : n * n;
		for (UINT g = 0; g < n; g++) {
			BYTE* row = (BYTE*)rect.pBits + (size_t)g * rect.Pitch;
			for (UINT b = 0; b < n; b++)
				for (UINT r = 0; r < n; r++) {
					if (b * n + r >= texWidth) continue; // a deliberately narrow texture
					BYTE* px = row + (size_t)(b * n + r) * 4;
					px[0] = (BYTE)(b * 255.0f / (n - 1) + 0.5f);
					px[1] = (BYTE)(g * 255.0f / (n - 1) + 0.5f);
					px[2] = (BYTE)(r * 255.0f / (n - 1) + 0.5f);
					px[3] = 255;
				}
		}
	}
	texture->UnlockRect(0);
	return texture;
}

static void SetChannel(IDirect3DTexture9* texture, UINT x, UINT y, int channel, int delta)
{
	D3DLOCKED_RECT rect;
	texture->LockRect(0, &rect, NULL, 0);
	BYTE* px = (BYTE*)rect.pBits + (size_t)y * rect.Pitch + (size_t)x * 4 + channel;
	*px = (BYTE)std::max(0, std::min(255, (int)*px + delta));
	texture->UnlockRect(0);
}

// CPU model of SampleLUT() in LUT.fx.hlsl on a texture whose texels come from `texel(x, y)` (0..1).
struct Rgb { float r, g, b; };
static Rgb Bilinear(const std::vector<Rgb>& tex, UINT width, UINT height, float u, float v)
{
	const float x = u * width - 0.5f, y = v * height - 0.5f;
	const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
	const float fx = x - x0, fy = y - y0;
	auto at = [&](int xx, int yy) { xx = std::max(0, std::min((int)width - 1, xx)); yy = std::max(0, std::min((int)height - 1, yy)); return tex[(size_t)yy * width + xx]; };
	Rgb a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
	auto mix = [&](float p, float q, float t) { return p + (q - p) * t; };
	return { mix(mix(a.r, b.r, fx), mix(c.r, d.r, fx), fy), mix(mix(a.g, b.g, fx), mix(c.g, d.g, fx), fy), mix(mix(a.b, b.b, fx), mix(c.b, d.b, fx), fy) };
}

static Rgb SampleLUT(const std::vector<Rgb>& tex, float n, Rgb color)
{
	const float b = color.b * (n - 1.0f);
	const float bCell = std::floor(b), bFrac = b - bCell;
	const float invW = 1.0f / (n * n), invH = 1.0f / n;
	const float u1 = (bCell * n + color.r * (n - 1.0f) + 0.5f) * invW;
	const float u2 = ((bCell + 1.0f) * n + color.r * (n - 1.0f) + 0.5f) * invW;
	const float v = (color.g * (n - 1.0f) + 0.5f) * invH;
	Rgb p = Bilinear(tex, (UINT)(n * n), (UINT)n, u1, v), q = Bilinear(tex, (UINT)(n * n), (UINT)n, u2, v);
	return { p.r + (q.r - p.r) * bFrac, p.g + (q.g - p.g) * bFrac, p.b + (q.b - p.b) * bFrac };
}

int main()
{
	IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
	IDirect3DDevice9* device = d3d ? CreateTestDevice(d3d) : nullptr;
	if (!device) { std::printf("SKIP: no D3D9 device (NULLREF/REF/HAL) could be created here.\n"); return 0; }

	// 1. The shipped file through the real loader.
	IDirect3DTexture9* shipped = nullptr;
	HRESULT hr = D3DXCreateTextureFromFileA(device, "resource/Textures/NewVegasReloaded/LUTs/neutral_lut.png", &shipped);
	CHECK(SUCCEEDED(hr) && shipped, "D3DX loads neutral_lut.png (hr=%08lx)", (unsigned long)hr);
	if (shipped) {
		D3DSURFACE_DESC desc = {};
		shipped->GetLevelDesc(0, &desc);
		std::printf("      loaded as %ux%u format %u, %u levels\n", desc.Width, desc.Height, (unsigned)desc.Format, (unsigned)shipped->GetLevelCount());
		CHECK(IsIdentityLUT(shipped), "shipped neutral_lut.png is recognised as an identity LUT");
		shipped->Release();
	}

	// 2. Synthetic strips and rejections.
	for (UINT n : { 16u, 32u, 64u }) {
		IDirect3DTexture9* strip = MakeStrip(device, n);
		CHECK(strip && IsIdentityLUT(strip), "synthetic %ux%ux%u identity strip is recognised", n, n, n);
		if (strip) strip->Release();
	}
	{
		IDirect3DTexture9* strip = MakeStrip(device, 16);
		SetChannel(strip, 100, 7, 2, 1);
		CHECK(IsIdentityLUT(strip), "a texel one 8-bit step off is still an identity (rounding)");
		SetChannel(strip, 100, 7, 2, 2); // now 3 steps off
		CHECK(!IsIdentityLUT(strip), "a texel three steps off is not an identity");
		strip->Release();
	}
	{
		IDirect3DTexture9* strip = MakeStrip(device, 16);
		SetChannel(strip, 255, 15, 0, -40); // last texel, blue channel
		CHECK(!IsIdentityLUT(strip), "a change in the last texel is found");
		strip->Release();
	}
	{
		IDirect3DTexture9* strip = MakeStrip(device, 16, D3DFMT_A8R8G8B8, 250);
		CHECK(strip && !IsIdentityLUT(strip), "a strip whose width is not N*N is not an identity");
		if (strip) strip->Release();
	}
	{
		IDirect3DTexture9* strip = MakeStrip(device, 16, D3DFMT_A16B16G16R16F);
		CHECK(!strip || !IsIdentityLUT(strip), "an unsupported format is not treated as an identity");
		if (strip) strip->Release();
	}
	CHECK(!IsIdentityLUT(nullptr), "a null texture is not an identity");

	// 3. The shader's own maths on an identity strip returns its input (worst case over many colours).
	for (UINT n : { 16u, 32u, 64u }) {
		std::vector<Rgb> tex((size_t)n * n * n);
		for (UINT g = 0; g < n; g++)
			for (UINT b = 0; b < n; b++)
				for (UINT r = 0; r < n; r++) {
					auto q = [&](UINT v) { return (float)(int)(v * 255.0f / (n - 1) + 0.5f) / 255.0f; }; // 8-bit texels
					tex[(size_t)g * (n * n) + b * n + r] = { q(r), q(g), q(b) };
				}
		std::mt19937 rng(1234 + n);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f), hdr(0.0f, 8.0f);
		float worstSdr = 0.0f, worstHdr = 0.0f;
		for (int i = 0; i < 200000; i++) {
			Rgb c = { unit(rng), unit(rng), unit(rng) };
			Rgb o = SampleLUT(tex, (float)n, c);
			worstSdr = std::max(worstSdr, std::max(std::fabs(o.r - c.r), std::max(std::fabs(o.g - c.g), std::fabs(o.b - c.b))));
			// HDR compat: normalise by the brightest channel (>= 1), sample, scale back.
			Rgb h = { hdr(rng), hdr(rng), hdr(rng) };
			const float scale = std::max(std::max(h.r, h.g), std::max(h.b, 1.0f));
			Rgb oh = SampleLUT(tex, (float)n, { h.r / scale, h.g / scale, h.b / scale });
			oh = { oh.r * scale, oh.g * scale, oh.b * scale };
			worstHdr = std::max(worstHdr, std::max(std::fabs(oh.r - h.r), std::max(std::fabs(oh.g - h.g), std::fabs(oh.b - h.b))) / scale);
		}
		std::printf("      N=%u: worst |out - in| SDR %.5f (1/255 = %.5f), HDR relative to scale %.5f\n", n, worstSdr, 1.0f / 255.0f, worstHdr);
		CHECK(worstSdr <= 1.0f / 255.0f && worstHdr <= 1.0f / 255.0f, "N=%u identity strip through the LUT maths stays within one 8-bit step", n);
	}

	device->Release();
	d3d->Release();
	std::printf(failures ? "\n%d check(s) FAILED\n" : "\nAll LUT identity checks passed\n", failures);
	return failures ? 1 : 0;
}
