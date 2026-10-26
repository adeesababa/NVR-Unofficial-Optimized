#pragma once

// UNOFFICIAL 2026-10-26 (NVR DXVK, deep/fork-hiz; docs\next\FORK-1026-HIZ.md): the hierarchical depth pyramid (Hi-Z).
// Built once per frame in ONE compute dispatch (Compute\HiZBuild.comp), right after the depth and normals, and only when a reader
// wants it: [Shaders.Reflections.Main] HiZ (the hierarchical ray march, Compute\ReflectionsTraceHiZ.frag) or [Shaders.AmbientLight.Main]
// HiZ (far horizon taps on coarse levels, Compute\AmbientLightEstimateHiZ.frag). Both are Vulkan-pass paths: they need the NVR DXVK
// d3d9.dll and [Main.Main.Performance] NvrDxvkPasses; without them nothing is built and the effects run as before.
//
// NVR_HiZ: G32R32F, Levels levels; level 0 is half the screen rounded up to a multiple of 64 (1920x1080 -> 960x576), each level half the
// one above (down to 15x9). Texel (i, j) of level k covers the full-resolution pixels [i, i+1) x [j, j+1) * 2^(k+1):
// r = the nearest linear depth there (TESR_DepthBuffer's value, depth / farZ), g = the farthest. The weapon's pixels are left out (a cell
// that is all weapon: r 4, g 0). F10 timer "  Hi-Z pyramid".
//
// A failure (shader file missing, pipeline or texture refused, dispatch failed) turns the pyramid off for the session (logged once); the
// readers then take their normal paths. It does not turn the other NVR DXVK paths off.

#include "DxvkCompute.h"

class HiZ {
public:
	static const UINT Levels = 7;
	// ShaderManager, after the depth and normals: builds this frame's pyramid when Wanted (once a frame; a second call does nothing).
	static void					Build(IDirect3DDevice9* Device, bool Wanted);
	// This frame's pyramid, or null (not wanted, not built, failed): the reader takes its normal path.
	static IDirect3DTexture9*	Current();
	static bool					Failed() { return failed; }

private:
	static bool					Ensure();
	static void					Release();

	static IDirect3DTexture9*	texture;
	static ID3D9NvrExtPipeline*	pipeline;
	static UINT					width, height;		// level 0
	static UINT					groupsX, groupsY;
	static UINT					builtFrame;
	static bool					built;
	static bool					failed;
	static UINT					frames;
};
