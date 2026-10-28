#pragma once

// UNOFFICIAL 2026-10-28 STAND-IN for the RT core's scene (session rt-sun-core, docs/next/FORK-RT-CORE.md, ID3D9NvrExtRt "1.8-rt"):
// the one scene NVR builds every frame from the game's draws, in camera-relative world space. RtLampShadows needs only these
// three answers from it; this file is replaced by the core's own when the branch is rebased onto it. Until then there is no
// scene, so RT lamp shadows stay off (and the shaders are exactly what they were).
//
// What RtLampShadows expects of the core:
//  - Address(): the scene's GPU address (ID3D9NvrExtRtScene::GetAddress), already valid when RenderManager initialises (the
//    scene created at startup with its MaxInstances, so the address never changes); 0 = no ray tracing on this device.
//  - BuiltThisFrame(): BuildScene was recorded this frame BEFORE the world's draws (the lamp shaders trace it during them).
//  - Each instance's Mask from RtLampShadows::InstanceMask (0x01 everything, 0x02 lamp glass, 0x04 lamp models, 0x08 actors).

namespace RtScene {

	inline UINT64 Address() { return 0; }

	inline bool BuiltThisFrame() { return false; }

	inline const char* Status() { return "the RT core is not in this build"; }

}
