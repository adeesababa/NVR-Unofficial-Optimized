#pragma once
// UNOFFICIAL 2026-10-28 (NVR DXVK, RT reflections; docs/next/FORK-RT-REFLECTIONS.md): what the RT reflections need from the shared RT
// core (session rt-sun-core, docs/next/FORK-RT-CORE.md): is this frame's TLAS there, how a pass binds it, and where it sits relative
// to the camera. One small header so that the effect's code does not change when the core's API is settled: only these bodies do.
// Until the core lands on this branch every answer is "no", so the RT path is detected off everywhere (the GTX 1070 has no Vulkan RT
// at all; there the core itself must answer no) and Reflections draws exactly as before.
#include "NvrDxvkExt.h"

namespace RtCoreClient {
	// The TLAS of this frame is built and may be read by an NVR pass recorded after this point.
	inline bool Ready() { return false; }
	// The binding a pass declares for the TLAS at (set, binding) on resource slot `slot`. False: the core has no TLAS binding.
	inline bool Binding(UINT set, UINT binding, UINT slot, NVR_EXT_BINDING_DESC& desc) { (void)set; (void)binding; (void)slot; desc = {}; return false; }
	// The resource for that binding this frame. False: not there.
	inline bool Resource(UINT slot, NVR_EXT_RESOURCE_BINDING& resource) { (void)slot; resource = {}; return false; }
	// Camera-relative world (NVR's effect space) -> TLAS space: the offset to add (0 if the TLAS is camera-relative too).
	inline D3DXVECTOR3 Offset() { return D3DXVECTOR3(0.0f, 0.0f, 0.0f); }
	// Per-instance materials (albedo) readable through the TLAS's custom index (ReflectionsRT.glsl rtHitAlbedo). Requested, not there.
	inline bool HasMaterials() { return false; }
}
