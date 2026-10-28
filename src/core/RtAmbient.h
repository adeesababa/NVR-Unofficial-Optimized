#pragma once
#include "DxvkCompute.h"

/*
* UNOFFICIAL 2026-10-28 (NVR DXVK fork, hybrid RT item 2 of 4; docs/next/FORK-RT-AO.md) [Shaders.AmbientOcclusion.RT]: ambient
* occlusion and sky visibility from rays traced against the scene instead of the screen (GTAO / SSAO) and the top-down ortho map
* (Skylighting v2).
* Where the rays run: in the shared RT core's 64-bit helper (NvrRtHelper.exe; docs/next/FORK-RT-CORE.md), because AMD's 32-bit Vulkan
* driver offers no ray queries to the game. Per frame (AmbientOcclusionEffect::RtRaw, once, for whichever pass asks first):
*   1. RtAOInput.frag: per half-resolution texel the view distance and the world normal -> NVR_RTAOInput (G32R32F);
*   2. Trace(): the core shares it with the helper, which runs RtAOTrace.comp (RayCount AO rays, SkyRays sky rays) and returns
*      NVR_RTAORaw (A16B16G16R16F: AO visibility, sky visibility, covered, DebugView 3's value);
*   3. NVR's own passes read it: AmbientLightEstimateRT.frag / AmbientOcclusionEstimateRT.frag in the screen estimate's place (the blurs
*      and Accumulate / AmbientOcclusionTemporalRT.frag after them are the denoiser) and AmbientOcclusionSkyRT.frag in SkyVisibilityV2's.
* Off by default. Whenever the core cannot trace (native D3D9, stock DXVK, no helper, a GPU without ray queries such as the GTX 1070)
* Ready() is false and every pass is exactly the old one: the current AO is the fallback and the A/B. Header only.
*
* The core-dependent part is CoreReady() and Trace() below (the core's RtCore API); the rest only uses NVR's own Vulkan passes.
*/
namespace RtAmbient {
	struct Settings {
		bool	Enabled = false;	// Enabled
		int		RayCount = 4;		// [1-16] AO rays per half-resolution pixel and frame
		float	Distance = 120.0f;	// [8-1000] AO ray length, world units (about 70 = 1 m)
		bool	Sky = true;			// also the sky visibility (needs Skylighting on, as the ortho version)
		int		SkyRays = 16;		// 8 or 16 (one or two rings round the normal + straight up)
		float	SkyDistance = 800.0f;	// [64-8000] sky ray length (Skylighting's Distance: 800)
		int		DebugView = 0;		// 1 RT visibility (before strength), 2 RT sky visibility, 3 rays that started inside something / hit distance
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

	// RtAOTrace.comp's uniform block (set 0, binding 1 in the helper), std140.
	struct TraceConstants {
		D3DXMATRIX	viewToWorld;	// NVR's view -> camera-relative world (TESR_InvViewTransform as stored; its translation is not used)
		D3DXVECTOR4	projection;		// x 1 / P00, y 1 / P11, zw the trace grid's size
		D3DXVECTOR4	origin;			// xyz camera-relative world -> the TLAS's space, w the frame number (0-1023)
		D3DXVECTOR4	ao;				// x distance, y rays (0 none), z 1 = move the noise each frame
		D3DXVECTOR4	sky;			// x distance, y rays (0 none)
	};

	// ---- RT core (the only part that depends on the shared core's API; see docs/next/FORK-RT-CORE.md) ----
	// The helper runs and traces for this device. Until the core's branch is merged there is nothing to ask: false, nothing changes.
	inline bool CoreReady() {
		return false;
	}
	// Runs the trace pass Compute\<Shader>.spv in the helper over Width x Height: Input shared in (set 0 binding 2), Constants as its
	// uniform block (binding 1), the result (binding 3) copied back into Output. False: no result this frame (NVR's own passes run).
	inline bool Trace(const char* Shader, IDirect3DTexture9* Input, IDirect3DTexture9* Output, const void* Constants, UINT Size, UINT Width, UINT Height) {
		(void)Shader; (void)Input; (void)Output; (void)Constants; (void)Size; (void)Width; (void)Height;
		return false;
	}
	// Camera-relative world + Origin() = the TLAS's space (the camera's world position if the TLAS is in world units; 0 if camera-relative).
	inline D3DXVECTOR3 Origin() {
		return D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	}
	// ---- end of the RT core part ----

	// The RT AO / sky passes run this frame (logged once per change of state).
	inline bool Ready() {
		static int reported = -1;
		const int state = !Current.Enabled ? 0 : !DxvkCompute::PassesOn() ? 1 : !CoreReady() ? 2 : 3;
		if (state != reported && (state != 0 || reported > 0)) {
			const char* text[] = { "off", "on but the NVR DXVK passes are off ([Main.Main.Performance] NvrDxvkPasses): NVR's AO runs",
				"on but the RT core cannot trace (no NvrRtHelper, or a GPU without ray queries): NVR's AO runs",
				"running (ray-traced occlusion in place of the screen estimate)" };
			Logger::Log("UNOFFICIAL RT AO: %s.", text[state]);
		}
		reported = state;
		return state == 3;
	}
	inline bool SkyReady() { return Current.Sky && Ready(); }
}
