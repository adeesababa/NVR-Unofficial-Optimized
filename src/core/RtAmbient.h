#pragma once
#include "DxvkCompute.h"

/*
* UNOFFICIAL 2026-10-28 (NVR DXVK fork, hybrid ray tracing item 2 of 4; docs/next/FORK-RT-AO.md) [Shaders.AmbientOcclusion.RT]:
* ambient occlusion and sky visibility from short ray queries against the fork's acceleration structures (the shared RT core:
* docs/next/FORK-RT-CORE.md) instead of the screen (GTAO / SSAO) and the top-down ortho map (Skylighting v2).
* - AO: a few cosine-weighted rays per half-resolution pixel from the depth and normal, Distance long, take the place of the
*   Estimate pass of AmbientLight (AmbientLightEstimateRT.frag) or of AmbientOcclusion (AmbientOcclusionEstimateRT.frag). The noise is
*   taken out by what follows Estimate anyway: the edge-aware blurs (spatial) and, for AmbientLight, its Accumulate pass (reprojected
*   history, neighbour clamp: temporal); AmbientOcclusion gets the same temporal pass of its own (AmbientOcclusionTemporalRT.frag).
* - Sky: SkyRays rays per sky-target pixel, SkyDistance long, in a fixed set of directions round the normal (the ortho version's two
*   rings), in place of the SkyVisibilityV2 pass (AmbientOcclusionSkyRT.frag): same target, same factor, so AmbientLight's and the
*   fog's composites take it unchanged. Fixed directions: no noise to filter (the ortho version's look, with real occluders).
* Off by default. Whenever ray queries are not there (native D3D9, stock DXVK, an NVR DXVK without the RT core, a GPU without
*   VK_KHR_ray_query such as the GTX 1070) Ready() is false and every pass is exactly the old one: the current AO is the fallback and
*   the A/B. Header only: AmbientLight and AmbientOcclusion both read it.
*
* The RT core binding (set 3, binding 0, `accelerationStructureEXT u_tlas`; Common/RtCommon.glsl) and the TLAS space (Origin) are the
* only places that depend on the core's API: see the "RT core" functions below.
*/
namespace RtAmbient {
	struct Settings {
		bool	Enabled = false;	// Enabled
		int		RayCount = 4;		// [1-16] AO rays per half-resolution pixel and frame
		float	Distance = 120.0f;	// [8-1000] AO ray length, world units (about 70 = 1 m)
		bool	Sky = true;			// also the sky visibility (needs Skylighting on, as the ortho version)
		int		SkyRays = 16;		// 8 or 16 (one or two rings round the normal + straight up)
		float	SkyDistance = 800.0f;	// [64-8000] sky ray length (Skylighting's Distance: 800)
		int		DebugView = 0;		// 1 RT visibility (before strength), 2 RT sky visibility, 3 rays that started inside something (red) / hit distance
	};
	inline Settings Current;

	inline void UpdateSettings() {
		const char* s = "Shaders.AmbientOcclusion.RT";
		Current.Enabled = TheSettingManager->GetSettingI(s, "Enabled") != 0;
		Current.RayCount = std::clamp(TheSettingManager->GetSettingI(s, "RayCount"), 1, 16);
		Current.Distance = std::clamp(TheSettingManager->GetSettingF(s, "Distance"), 8.0f, 1000.0f);
		Current.Sky = TheSettingManager->GetSettingI(s, "Sky") != 0;
		Current.SkyRays = TheSettingManager->GetSettingI(s, "SkyRays") <= 8 ? 8 : 16;
		Current.SkyDistance = std::clamp(TheSettingManager->GetSettingF(s, "SkyDistance"), 64.0f, 8000.0f);
		Current.DebugView = std::clamp(TheSettingManager->GetSettingI(s, "DebugView"), 0, 3);
	}

	// ---- RT core (the only part that depends on the shared core's API; see docs/next/FORK-RT-CORE.md) ----
	// Ray queries usable this frame: the extension reports them and the core has a TLAS for this frame. Until the core's branch is
	// merged there is nothing to ask: false, so nothing changes.
	inline bool CoreReady() {
		return false;
	}
	// The TLAS as the RT shaders declare it (Common/RtCommon.glsl: set 3, binding 0) and as a draw binds it.
	inline bool TlasBinding(NVR_EXT_BINDING_DESC& Desc) {
		(void)Desc;
		return false;
	}
	inline bool TlasResource(NVR_EXT_RESOURCE_BINDING& Resource) {
		(void)Resource;
		return false;
	}
	// Camera-relative world (NVR's: the view's rotation only) + Origin() = the TLAS's space. Zero if the TLAS is camera-relative.
	inline D3DXVECTOR4 Origin() {
		return D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	// ---- end of the RT core part ----

	// The RT AO / sky passes run this frame (logged once per change of state).
	inline bool Ready() {
		static int reported = -1;
		const int state = !Current.Enabled ? 0 : !DxvkCompute::PassesOn() ? 1 : !CoreReady() ? 2 : 3;
		if (state != reported && (state != 0 || reported > 0)) {
			const char* text[] = { "off", "on but the NVR DXVK passes are off ([Main.Main.Performance] NvrDxvkPasses): NVR's AO runs",
				"on but ray queries are not available (no RT core in this d3d9.dll, or a GPU without VK_KHR_ray_query): NVR's AO runs",
				"running (ray-traced occlusion in place of the screen estimate)" };
			Logger::Log("UNOFFICIAL RT AO: %s.", text[state]);
		}
		reported = state;
		return state == 3;
	}
	inline bool SkyReady() { return Current.Sky && Ready(); }

	// The push block of the RT passes (RtCommon.glsl RtPush, at the offset the pass's own block ends): 32 bytes.
	struct Push {
		D3DXVECTOR4 rt;		// x AO distance, y AO ray count, z sky distance, w sky ray count
		D3DXVECTOR4 origin;	// xyz Origin(), w DebugView
	};
	inline Push MakePush() {
		Push p;
		p.rt = D3DXVECTOR4(Current.Distance, (float)Current.RayCount, Current.SkyDistance, (float)Current.SkyRays);
		p.origin = Origin();
		p.origin.w = (float)Current.DebugView;
		return p;
	}
}
