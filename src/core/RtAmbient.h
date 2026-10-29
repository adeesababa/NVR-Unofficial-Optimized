#pragma once
#include "DxvkCompute.h"
#include "RtCore.h"

/*
* UNOFFICIAL 2026-10-28 (NVR DXVK fork, hybrid RT item 2 of 4; docs/next/FORK-RT-AO.md) [Shaders.AmbientOcclusion.RT]: ambient
* occlusion and sky visibility from rays traced against the scene instead of the screen (GTAO / SSAO) and the top-down ortho map
* (Skylighting v2).
* Where the rays run: in the shared RT core's 64-bit helper (NvrRtHelper.exe; docs/next/FORK-RT-CORE.md), because Windows' 32-bit
* Vulkan drivers offer no ray queries to the game. AmbientOcclusionEffect is the core's user "RT ambient occlusion"; at the core's point
* of the frame (RtCore::Frame, before the sky, AO and AmbientLight passes) its RtRecord:
*   1. RtAOInput.frag: per half-resolution texel the view distance and the world normal -> NVR_RTAOInput (G32R32F);
*   2. RtCore CopyIn / AddPass / CopyOut: the helper runs Compute\Rt\RtAOTrace.comp (RayCount AO rays, SkyRays sky rays) and the
*      result comes back as NVR_RTAORaw (A16B16G16R16F: AO visibility, sky visibility, covered, DebugView 3's value);
*   3. NVR's own passes read it: AmbientLightEstimateRT.frag / AmbientOcclusionEstimateRT.frag in the screen estimate's place (the blurs
*      and Accumulate / AmbientOcclusionTemporalRT.frag after them are the denoiser) and AmbientOcclusionSkyRT.frag in SkyVisibilityV2's.
* Off by default. Whenever the core cannot trace (native D3D9, stock DXVK, no helper, a GPU without ray queries such as the GTX 1070)
* Ready() is false and every pass is exactly the old one: the current AO is the fallback and the A/B. Header only.
*/
namespace RtAmbient {
	struct Settings {
		bool	Enabled = false;	// Enabled
		int		RayCount = 4;		// [1-16] AO rays per half-resolution pixel and frame
		float	Distance = 120.0f;	// [8-1000] AO ray length, world units (about 70 = 1 m)
		bool	Sky = true;			// also the sky visibility (needs Skylighting on, as the ortho version)
		int		SkyRays = 16;		// 8 or 16 (one or two rings round the normal + straight up)
		float	SkyDistance = 800.0f;	// [64-8000] sky ray length (Skylighting's Distance: 800)
		int		DebugView = 0;		// 1 RT visibility (before strength), 2 RT sky visibility, 3 rays that started inside something / hit distance,
									// 4 / 5 the helper's input (view distance / 2000, normal up), test only
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
		Current.DebugView = std::clamp(TheSettingManager->GetSettingI(s, "DebugView"), 0, 5);
	}

	// RtAOTrace.comp's uniform block (set 0, binding 1 in the helper), std140.
	struct TraceConstants {
		D3DXMATRIX	viewToWorld;	// NVR's view -> scene directions: rows right, up, forward of the view (row-vector matrix)
		D3DXVECTOR4	projection;		// x 1 / P00, y 1 / P11, zw the trace grid's size
		D3DXVECTOR4	origin;			// xyz the view origin in scene space (RtCore::Eye(), 0), w the frame number (0-1023)
		D3DXVECTOR4	ao;				// x distance, y rays (0 none), z 1 = move the noise each frame
		D3DXVECTOR4	sky;			// x distance, y rays (0 none)
	};

	// The RT core (RtCore.h) runs: the helper is ready on a GPU with ray tracing. Off = NVR's own passes.
	inline bool CoreReady() { return RtCore::Running(); }

	// The RT AO / sky passes run this frame (logged once per change of state).
	inline bool Ready() {
		static int reported = -1;
		const int state = !Current.Enabled ? 0 : !DxvkCompute::PassesOn() ? 1 : !CoreReady() ? 2 : 3;
		if (state != reported && (state != 0 || reported > 0)) {
			const char* text[] = { "off", "on but the NVR DXVK passes are off ([Main.Main.Performance] NvrDxvkPasses): NVR's AO runs",
				"on but the RT core is not running (see its log line: no NvrRtHelper, no fork 0028, or a GPU without ray tracing): NVR's AO runs",
				"running (ray-traced occlusion in place of the screen estimate)" };
			Logger::Log("UNOFFICIAL RT AO: %s.", text[state]);
		}
		reported = state;
		return state == 3;
	}
	inline bool SkyReady() { return Current.Sky && Ready(); }
}
