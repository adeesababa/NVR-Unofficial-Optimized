#pragma once

// UNOFFICIAL 2026-10-28 STAND-IN for the RT core (session rt-sun-core: the 64-bit helper NvrRtHelper.exe driven by src/core/RtCore.*,
// docs/next/FORK-RT-CORE.md). RtLampShadows needs only these answers from it; this file goes when the branch is rebased onto the
// core, its four functions mapped onto the core's API. Until then there is no helper, so RT lamp shadows stay off and the shaders are
// exactly what they were.
//
// What RtLampShadows expects of the core:
//  - Available(): the helper runs and has ray tracing (decided before NVR loads its shaders).
//  - SubmitLampTrace(frame, constants): this frame, after the depth and normals are made (TESR_DepthBuffer, TESR_NormalsBuffer),
//    run Compute/Rt/RtLampTrace.spv in the helper over the frame's size with those two images shared in (bindings 2, 3), an RGBA32F
//    output (binding 4) and the constants as its uniform block (binding 1); the scene (binding 0) holds the interior's statics and
//    actors in camera-relative space for THIS frame's camera, each instance masked with RtLampShadows::InstanceMask.
//  - LampTraceResult(&frame): the newest finished result as a D3D9 texture (point-sampled by the object shaders) and the frame number
//    it was submitted with; null until one exists.

namespace RtCoreStandIn {

	inline bool Available() { return false; }

	inline const char* Status() { return "the RT core is not in this build"; }

	inline void SubmitLampTrace(UInt64 frame, const void* constants, UINT size) { (void)frame; (void)constants; (void)size; }

	inline IDirect3DTexture9* LampTraceResult(UInt64* frame) { *frame = 0; return nullptr; }

	// A 1x1 texture "lit, nothing traced" for s15 while no result exists (never read: c215.x is 0 then; D3D9 wants a 2D texture there)
	inline IDirect3DTexture9* Neutral2D() {
		static IDirect3DTexture9* neutral = nullptr;
		if (!neutral && SUCCEEDED(TheRenderManager->device->CreateTexture(1, 1, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &neutral, NULL))) {
			D3DLOCKED_RECT lock;
			if (SUCCEEDED(neutral->LockRect(0, &lock, NULL, 0))) {
				float* texel = (float*)lock.pBits;
				texel[0] = texel[1] = texel[2] = 16777215.0f; texel[3] = 0.0f;
				neutral->UnlockRect(0);
			}
		}
		return neutral;
	}

}
