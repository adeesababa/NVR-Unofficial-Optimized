#pragma once
// UNOFFICIAL 2026-10-28 (NVR DXVK, RT reflections; docs/next/FORK-RT-REFLECTIONS.md): what the RT reflections need from the shared RT
// core (session rt-sun-core, docs/next/FORK-RT-CORE.md, src/core/RtCore.*). The rays run in the core's 64-bit helper process
// (NvrRtHelper.exe: AMD's 32-bit Vulkan driver has no ray tracing); NVR shares images in and out through fork patch 0026.
// One small header so that the effect's code does not change when the core's API is settled: only these bodies do.
// Until the core is on this branch every answer is "no", so the RT path is off everywhere (on the GTX 1070, which has no Vulkan RT
// at all, the core itself must answer no) and Reflections draws exactly as before.
#include "NvrDxvkExt.h"

namespace RtCoreClient {
	struct Image {
		IDirect3DTexture9*	Texture;
		bool				Output;		// false: NVR wrote it this frame, the helper reads it; true: the helper writes it, NVR reads it after
	};
	// The helper is alive, its GPU traces rays, and this frame's scene is being built.
	inline bool Ready() { return false; }
	// One helper pass: Compute\RT\<Name>.spv as a compute shader, DispatchX x DispatchY groups, with the RT core's fixed bindings (set 0:
	// 0 the scene, 1 Constants, 2.. Images in this order). True: the outputs are in their D3D9 textures for the next NVR pass to read.
	inline bool Trace(const char* Name, const Image* Images, UINT Count, UINT DispatchX, UINT DispatchY, const void* Constants, UINT Size) {
		(void)Name; (void)Images; (void)Count; (void)DispatchX; (void)DispatchY; (void)Constants; (void)Size; return false;
	}
	// Camera-relative world (NVR's effect space) -> the scene's space: the offset to add (0 if the scene is camera-relative too).
	inline D3DXVECTOR3 Offset() { return D3DXVECTOR3(0.0f, 0.0f, 0.0f); }
	// Per-instance materials (albedo) readable through the scene's custom index. Requested, not there.
	inline bool HasMaterials() { return false; }
}
