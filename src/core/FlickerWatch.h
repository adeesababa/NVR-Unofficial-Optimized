#pragma once

#include <cmath>
#include <cstdio>

namespace FlickerWatch {

	enum : unsigned {
		kComposite = 1,
		kPair = 2,
		kIntoAL = 4,
		kALApplied = 8,
		kShadowPass = 16,
		kSunFar = 32,
		kALHistory = 64,
		kLodUpdated = 128,
		kCellChanged = 256,
		kALRendered = 512,
	};
	inline unsigned Flags = 0;

	struct Slot { IDirect3DSurface9* rt = nullptr; IDirect3DSurface9* mem = nullptr; bool pending = false; unsigned frame = 0, flags = 0; float dt = 0.0f; };
	inline Slot Ring[3];
	inline D3DSURFACE_DESC Desc = {};
	inline unsigned Frame = 0, Lines = 0, Flagged = 0, Measured = 0;
	inline bool HavePrev = false, Failed = false;
	inline float PrevTop = 0.0f, PrevBottom = 0.0f, MaxDrop = 0.0f;
	inline unsigned PrevFlags = 0, PrevFrame = 0;
	static const UINT W = 160, H = 90;

	inline void Release() {
		for (Slot& s : Ring) {
			if (s.rt) s.rt->Release();
			if (s.mem) s.mem->Release();
			s = Slot();
		}
		HavePrev = false;
	}

	inline float Luma8(const UInt8* p) { return (0.2126f * p[2] + 0.7152f * p[1] + 0.0722f * p[0]) / 255.0f; }

	inline bool Measure(Slot& s, float& top, float& bottom) {
		if (FAILED(TheRenderManager->device->GetRenderTargetData(s.rt, s.mem))) return false;
		D3DLOCKED_RECT lock = {};
		if (FAILED(s.mem->LockRect(&lock, NULL, D3DLOCK_READONLY))) return false;
		double sumTop = 0.0, sumBottom = 0.0;
		for (UINT y = 0; y < H; y++) {
			const UInt8* row = (const UInt8*)lock.pBits + (size_t)y * lock.Pitch;
			double sum = 0.0;
			for (UINT x = 0; x < W; x++) {
				if (Desc.Format == D3DFMT_A16B16G16R16F) {
					const D3DXFLOAT16* h = (const D3DXFLOAT16*)(row + x * 8);
					float rgb[3];
					D3DXFloat16To32Array(rgb, h, 3);
					sum += 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
				}
				else sum += Luma8(row + x * 4);
			}
			if (y < H / 3) sumTop += sum; else if (y >= H / 2) sumBottom += sum;
		}
		s.mem->UnlockRect();
		top = (float)(sumTop / (W * (H / 3)));
		bottom = (float)(sumBottom / (W * (H - H / 2)));
		return true;
	}

	inline void FlagText(unsigned f, char* out, size_t size) {
		sprintf_s(out, size, "%s%s%s%s%s%s%s%s%s%s", f & kComposite ? " composite" : "", f & kPair ? " pair" : "", f & kIntoAL ? " intoAL" : "",
			f & kALApplied ? " ALapplied" : "", f & kShadowPass ? " shadowpass" : "", f & kSunFar ? " sunfar" : "", f & kALHistory ? " ALhistory" : "",
			f & kLodUpdated ? " lodupd" : "", f & kCellChanged ? " cellchg" : "", f & kALRendered ? " ALran" : "");
	}

	inline void OnFrame(IDirect3DDevice9* device, IDirect3DSurface9* target, float threshold) {
		const unsigned flags = Flags;
		Flags = 0;
		if (Failed || !device || !target) return;
		D3DSURFACE_DESC d = {};
		target->GetDesc(&d);
		if (d.Format != D3DFMT_A8R8G8B8 && d.Format != D3DFMT_X8R8G8B8 && d.Format != D3DFMT_A16B16G16R16F) {
			Failed = true;
			Logger::Log("FLICKER WATCH: picture format %d not handled, off", (int)d.Format);
			return;
		}
		if (!Ring[0].rt || d.Format != Desc.Format) {
			Release();
			Desc = d;
			for (Slot& s : Ring)
				if (FAILED(device->CreateRenderTarget(W, H, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &s.rt, NULL)) ||
					FAILED(device->CreateOffscreenPlainSurface(W, H, d.Format, D3DPOOL_SYSTEMMEM, &s.mem, NULL))) {
					Release();
					Failed = true;
					Logger::Log("FLICKER WATCH: could not make the read-back surfaces, off");
					return;
				}
			Logger::Log("FLICKER WATCH: on (picture %ux%u format %d, threshold %.3f)", d.Width, d.Height, (int)d.Format, threshold);
		}
		static double lastTime = 0.0;
		const double now = TheFrameRateManager->Time;
		Slot& write = Ring[Frame % 3];
		if (SUCCEEDED(device->StretchRect(target, NULL, write.rt, NULL, D3DTEXF_LINEAR)) || SUCCEEDED(device->StretchRect(target, NULL, write.rt, NULL, D3DTEXF_NONE))) {
			write.pending = true;
			write.frame = Frame;
			write.flags = flags;
			write.dt = (float)((now - lastTime) * 1000.0);
		}
		lastTime = now;
		Slot& read = Ring[(Frame + 1) % 3];
		Frame++;
		if (!read.pending) return;
		read.pending = false;
		float top, bottom;
		if (!Measure(read, top, bottom)) { HavePrev = false; return; }
		Measured++;
		if (HavePrev && read.frame == PrevFrame + 1 && PrevBottom > 0.01f && PrevTop > 0.01f) {
			const float change = bottom / PrevBottom - 1.0f, topChange = top / PrevTop - 1.0f;
			if (fabsf(change) > threshold && fabsf(topChange) < threshold * 0.5f) {
				Flagged++;
				if (-change > MaxDrop) MaxDrop = -change;
				if (Lines < 300) {
					Lines++;
					char was[160], is[160];
					FlagText(PrevFlags, was, sizeof(was));
					FlagText(read.flags, is, sizeof(is));
					Logger::Log("FLICKER frame %u: lower half %+.1f%% (%.4f -> %.4f), upper %+.1f%%; %.1f ms; before:%s | this:%s",
						read.frame, change * 100.0f, PrevBottom, bottom, topChange * 100.0f, read.dt, was, is);
				}
			}
			else if ((read.flags & ~(kLodUpdated | kCellChanged)) != (PrevFlags & ~(kLodUpdated | kCellChanged)) && Lines < 300) {
				Lines++;
				char was[160], is[160];
				FlagText(PrevFlags, was, sizeof(was));
				FlagText(read.flags, is, sizeof(is));
				Logger::Log("FLICKER WATCH path change frame %u (lower half %+.1f%%, upper %+.1f%%; %.1f ms): before:%s | this:%s",
					read.frame, change * 100.0f, topChange * 100.0f, read.dt, was, is);
			}
		}
		HavePrev = true;
		PrevTop = top; PrevBottom = bottom; PrevFlags = read.flags; PrevFrame = read.frame;
		if (Measured % 600 == 0)
			Logger::Log("FLICKER WATCH summary: %u frames measured, %u flagged, largest drop %.1f%%", Measured, Flagged, MaxDrop * 100.0f);
	}
}
