#pragma once

// UNOFFICIAL 2026-10-27 (NVR DXVK): the temporal upscaler behind TAA's FSR path ([Shaders.TAA.FSR]), as an interface so a
// second upscaler (NVIDIA DLSS through a 64-bit helper process: core\UpscalerDlss.cpp, docs\next\FORK2-DLSS.md) takes FSR 2's
// place in the same two calls without touching what feeds it:
//   - TAA's native AA (effects\TAAFSR.cpp): the post-tonemap frame at the screen's own resolution (render size = display size);
//   - RenderScale (effects\TAAUpscale.cpp): the world scene drawn at a lower internal resolution, upscaled before NVR's effects.
// Both build the same inputs (jittered colour, a depth, camera motion vectors, the jitter) and read Output() afterwards.
//
// FSR 2 is AMD's (FidelityFX Super Resolution 2.2.1, GPUOpen, MIT; src\hlsl\NewVegas\Compute\FSR2\LICENSE-FidelityFX-FSR2.txt).
// Everything here needs NVR DXVK (compute passes through the extension). A failure is logged once and the upscaler reports
// Failed() for the session; its callers then keep their D3D9 paths.

#include "NvrDxvkExt.h"

namespace Upscaler {

	// What an upscaler gets each frame. The textures are D3D9 render-target textures the NVR extension can bind; the
	// render-resolution ones hold this frame's picture in their top-left RenderWidth x RenderHeight (the rest is not read).
	struct Inputs {
		IDirect3DBaseTexture9*	Color = nullptr;	// the jittered picture (render resolution); linear HDR when Hdr, else the tonemapped frame
		IDirect3DBaseTexture9*	Depth = nullptr;	// r: device depth in FSR's standard convention (0 near .. 1 far), g: reactive mask [0, 1]
		IDirect3DBaseTexture9*	Motion = nullptr;	// rg: previous uv - current uv (fractions of the picture), unjittered, render resolution
		UINT	RenderWidth = 0, RenderHeight = 0;		// this frame's picture
		UINT	InputWidth = 0, InputHeight = 0;		// the input textures' size (at least the render size)
		UINT	DisplayWidth = 0, DisplayHeight = 0;	// the output
		float	JitterX = 0.0f, JitterY = 0.0f;			// this frame's sub-pixel offset in render pixels (FSR 2's sign convention)
		float	JitterPhases = 8.0f;					// length of the jitter sequence
		float	NearZ = 1.0f, FarZ = 1.0f;				// the depth's near and far planes (game units)
		float	TanHalfFovX = 1.0f, TanHalfFovY = 1.0f;
		float	DeltaSeconds = 0.0f;
		float	Sharpness = 0.0f;						// [0, 1]: the upscaler's own sharpening of its output, 0 off
		float	UnitsPerMetre = 70.0f;					// Gamebryo: about 70 units per metre
		bool	Hdr = false;							// Color is linear HDR (before tonemapping)
		bool	Reset = false;							// no usable history (first frame, a cut, a mode or size change)
	};

	class IUpscaler {
	public:
		virtual ~IUpscaler() {}
		virtual const char*			Name() const = 0;
		// Records the upscale into DXVK's command stream (no waits); the result is then in Output() (rgba16f, display size).
		// False: this frame failed (logged); Failed() says whether the upscaler is off for the session.
		virtual bool				Evaluate(IDirect3DDevice9* Device, const Inputs& In) = 0;
		virtual IDirect3DTexture9*	Output() const = 0;
		virtual bool				Failed() const = 0;
		virtual void				ReleaseResources() = 0;
	};

	// AMD FSR 2.2.1 (core\Upscaler.cpp). One shared instance: TAA's native AA and RenderScale use it in turn (each switch
	// between them is a Reset), so its textures (about 190 MB at 1080p, made on first use) exist once.
	IUpscaler*	Fsr2();

	// NVIDIA DLSS through NvrDlssHelper.exe (64-bit; core\UpscalerDlss.cpp, tools\nvr-dlss-helper). No NVIDIA code in NVR's DLL.
	// It runs FSR 2 itself while the helper starts, and from the first problem on (logged once with the reason).
	IUpscaler*	Dlss();

	// [Shaders.TAA.DLSS] HelperTest (the helper's pass-through test: the same exchange, no DLSS) and JitterFlip (bit 0
	// flips the jitter's x, bit 1 its y, for checks with NVIDIA's development DLL). Set by TAAEffect::UpdateSettings.
	void		DlssOptions(bool PassThroughTest, int JitterFlip);

	// 0 FSR 2 ([Shaders.TAA.FSR] Enabled), 1 DLSS ([Shaders.TAA.DLSS] Enabled) (Dlss(), which falls back to FSR 2 whenever DLSS cannot run).
	IUpscaler*	Choose(int setting);
}
