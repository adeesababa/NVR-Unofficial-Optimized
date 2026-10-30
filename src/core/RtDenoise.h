#pragma once

// UNOFFICIAL 2026-10-29 (NVR DXVK) the shared RT denoiser: for one-sample-a-pixel ray traced signals (soft sun shadows, AO, sky,
// lamp shadows, reflection masks). SVGF / ReBLUR-style, on the game's GPU with NVR DXVK passes: a temporal pass (the signal blended
// with last frame's history, reprojected through the depth and last frame's camera; disocclusions restart) and 3 edge-stopping
// a-trous passes (steps 1, 2, 4; depth, view normal and variance-guided value weights). One channel (.r) for now.
// docs\next\FORK-RT-SUN.md "Denoiser". Use:
//   static RtDenoise::Denoiser* d = RtDenoise::Create("my effect");
//   RtDenoise::Run(d, Device, noisyTexture, outputSurface, settings);   // once a frame, after the noisy signal is there
// Run keeps its own history (2 RGBA16F screen textures + 2 for the a-trous chain); false: not run (no NVR DXVK, a pass missing):
// use the noisy signal. Camera cuts (a jump of the camera) restart the history by themselves.

namespace RtDenoise {
	struct Settings {
		float	MaxHistory = 16.0f;		// frames the temporal blend averages at most (more: smoother, slower to react)
		float	DepthTolerance = 0.03f;	// relative depth mismatch that counts as a different surface (disocclusion)
		int		Iterations = 3;			// a-trous passes (steps 1, 2, 4, ...): 0 = temporal only
		float	DepthSigma = 1.0f;
		float	NormalPower = 64.0f;
		float	ValueSigma = 4.0f;
		bool	Reset = false;			// drop the history this frame
		int		Downscale = 1;			// 1 full screen, 2 half (Noisy and Output must then be w/2 x h/2; ~4x cheaper; the caller upsamples)
	};
	struct Denoiser;

	Denoiser*	Create(const char* Name);
	bool		Run(Denoiser* D, IDirect3DDevice9* Device, IDirect3DBaseTexture9* Noisy, IDirect3DSurface9* Output, const Settings& S);
}
