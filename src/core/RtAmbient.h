#pragma once
#include "DxvkCompute.h"
#include "RtCore.h"

namespace RtAmbient {
	struct Settings {
		bool	Enabled = false;
		int		RayCount = 4;
		float	Distance = 120.0f;
		bool	Sky = true;
		int		SkyRays = 16;
		float	SkyDistance = 800.0f;
		int		DebugView = 0;
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

	struct TraceConstants {
		D3DXMATRIX	viewToWorld;
		D3DXVECTOR4	projection;
		D3DXVECTOR4	origin;
		D3DXVECTOR4	ao;
		D3DXVECTOR4	sky;
	};

	inline bool CoreReady() { return RtCore::Running(); }

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
