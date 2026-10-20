#pragma once

#include <cmath>

namespace GrassWind {

	struct SettingsStruct {
		bool	Enabled = false;
		float	GustStrength = 2.0f;
		float	GustSize = 1200.0f;
		float	GustSpeed = 300.0f;
		float	Flutter = 0.3f;
		bool	UseGameWind = true;
		float	Direction = 0.0f;
		float	TimeOffset = 0.0f;
		int		Log = 0;
	};
	inline SettingsStruct S;

	inline float Lanes[4] = {};
	inline float ClockNow = 0.0f;
	inline double Clock = 0.0;
	inline double Rate = 0.0;
	inline double ClockSeconds = 0.0;
	inline float LastHour = -1.0f;
	inline UInt32 LastFrame = 0xFFFFFFFF;
	inline bool On = false;
	inline bool LanesSent = false;
	inline UInt32 Frames = 0;
	inline float WindDir[2] = { 0.0f, 1.0f }, WindSpeed = 0.0f, Speed = 0.0f;
	inline bool LoggedOnce = false;
	inline bool ClockWanted = false;

	static const double kWrap = 8.0 * 3.14159265358979323846;

	inline void ReadSettings() {
		const char* s = "Shaders.Grass.Wind";
		S.Enabled = TheSettingManager->GetSettingI(s, "Enabled") != 0;
		S.GustStrength = std::clamp(TheSettingManager->GetSettingF(s, "GustStrength"), 0.0f, 10.0f);
		S.GustSize = std::clamp(TheSettingManager->GetSettingF(s, "GustSize"), 50.0f, 20000.0f);
		S.GustSpeed = std::clamp(TheSettingManager->GetSettingF(s, "GustSpeed"), 0.0f, 3000.0f);
		S.Flutter = std::clamp(TheSettingManager->GetSettingF(s, "Flutter"), 0.0f, 10.0f);
		S.UseGameWind = TheSettingManager->GetSettingI(s, "UseGameWind") != 0;
		S.Direction = TheSettingManager->GetSettingF(s, "Direction");
		S.TimeOffset = TheSettingManager->GetSettingF(s, "TimeOffset");
		S.Log = TheSettingManager->GetSettingI(s, "Log");
	}

	inline double AdvanceClock() {
		TimeGlobals* g = TimeGlobals::Get();
		if (!g || !g->GameHour) return 0.0;
		const float hour = g->GameHour->data;
		const double scale = g->TimeScale && g->TimeScale->data > 0.0f ? (double)g->TimeScale->data : 30.0;
		double dt = 0.0;
		if (LastHour >= 0.0f) {
			double dh = (double)hour - (double)LastHour;
			if (dh < -12.0) dh += 24.0;
			if (dh > 0.0) dt = (std::min)(dh * 3600.0 / scale, 0.1);
		}
		LastHour = hour;
		ClockSeconds += dt;
		return dt;
	}

	inline void Update(const Sky* sky, bool exterior, float stormIntensity) {
		const UInt32 frame = TheFrameRateManager ? TheFrameRateManager->FrameNumber : 0;
		if (frame == LastFrame) return;
		LastFrame = frame;
		On = S.Enabled && exterior;
		if (!On && !(ClockWanted && exterior)) { for (float& l : Lanes) l = 0.0f; LastHour = -1.0f; return; }

		float a = S.Direction * 0.0174532925f, w = 0.5f;
		if (S.UseGameWind && sky) { a = sky->windDirection; w = std::clamp(sky->windSpeed, 0.0f, 1.0f); }
		WindDir[0] = sinf(a); WindDir[1] = cosf(a); WindSpeed = w;
		const double k = 2.0 * 3.14159265358979323846 / S.GustSize;
		Lanes[0] = (float)(WindDir[0] * k); Lanes[1] = (float)(WindDir[1] * k);
		Lanes[2] = S.GustStrength; Lanes[3] = S.Flutter;
		if (!On) for (float& l : Lanes) l = 0.0f;

		Speed = S.GustSpeed * (0.4f + 1.2f * w);
		const double ours = 4.0 * Speed * k, storm = 8.4, i = std::clamp((double)stormIntensity, 0.0, 1.0);
		Rate = ours + (storm - ours) * i;
		const double dt = AdvanceClock();
		Clock = fmod(Clock + dt * Rate, kWrap);
		double now = fmod(Clock + (double)S.TimeOffset * Rate, kWrap);
		if (now < 0.0) now += kWrap;
		ClockNow = (float)now;
	}

	inline void BindCards(IDirect3DDevice9* device) {
		if (!On && !LanesSent) return;
		float rows[16];
		memcpy(rows, &TheRenderManager->InvViewMatrix, sizeof(rows));
		if (On) { rows[3] = Lanes[0]; rows[7] = Lanes[1]; rows[11] = Lanes[2]; rows[15] = Lanes[3]; }
		device->SetVertexShaderConstantF(252, rows, 4);
		LanesSent = On;
	}

	inline void LogState(IDirect3DDevice9* device) {
		if (!S.Log || ++Frames < 600) return;
		Frames = 0;
		const NiAVObject* geometry = *(NiAVObject**)(*(void**)0x011F91E0);
		float c4[4] = {}, c252[16] = {};
		const bool got = SUCCEEDED(device->GetVertexShaderConstantF(4, c4, 1)) && SUCCEEDED(device->GetVertexShaderConstantF(252, c252, 4));
		const D3DXMATRIX& m = TheRenderManager->InvViewMatrix;
		Logger::Log("UNOFFICIAL Grass wind: %s; weather wind direction %.3f (%.3f %.3f) speed %.3f; game grass c4 %s%.3f %.3f size %.2f phase %.3f; "
			"lanes %.5f %.5f %.2f %.2f, clock %.3f (rate %.3f rad/s, gusts %.0f units/s); c252-c255 .w %.5f %.5f %.2f %.2f (matrix's own %.1f %.1f %.1f %.1f); "
			"grass geometry at %.0f %.0f %.0f",
			On ? "on" : "off", atan2f(WindDir[0], WindDir[1]), WindDir[0], WindDir[1], WindSpeed, got ? "" : "(not readable) ", c4[0], c4[1], c4[2], c4[3],
			Lanes[0], Lanes[1], Lanes[2], Lanes[3], ClockNow, Rate, Speed, c252[3], c252[7], c252[11], c252[15], m._14, m._24, m._34, m._44,
			geometry ? geometry->m_worldTransform.pos.x : 0.0f, geometry ? geometry->m_worldTransform.pos.y : 0.0f, geometry ? geometry->m_worldTransform.pos.z : 0.0f);
	}
}
