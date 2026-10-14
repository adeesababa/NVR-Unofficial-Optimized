#pragma once

#include <atomic>
#include "EffectSelfTestCore.h"
#include "SettingShots.h"

namespace EffectSelfTest {

	enum Point { AfterSpill, Final, Points };
	enum { Settle = 60, Measure = 30 };
	inline const char* const PointNames[Points] = { "after bounce light, before tonemap", "final picture, includes eye adaptation" };
	inline const char* const PointFiles[Points] = { "spill", "final" };

	using SelfTestCore::BounceValues;
	using SelfTestCore::Read;
	using SelfTestCore::Write;
	inline float DefaultValue(const char* key, float fallback) {
		try {
			const tomlValue& v = TheSettingManager->Config.DefaultConfig.at("_Shaders").at("BounceLight").at("Main").at(key);
			if (v.is_floating()) return (float)v.as_floating();
			if (v.is_integer()) return (float)v.as_integer();
		}
		catch (...) {}
		return fallback;
	}
	inline void ReadDefaults(BounceValues& v) {
		v.neonSpill = std::clamp(DefaultValue("NeonSpill", 0.0f), 0.0f, 20.0f);
		if (v.neonSpill <= 0.0f) v.neonSpill = 1.0f;
		v.spillReach = DefaultValue("NeonSpillReach", 1.0f); v.spillReach = v.spillReach > 0.0f ? std::clamp(v.spillReach, 0.25f, 4.0f) : 1.0f;
		v.spillLightLevel = DefaultValue("NeonSpillLightLevel", 1.5f); v.spillLightLevel = v.spillLightLevel > 0.0f ? std::clamp(v.spillLightLevel, 0.25f, 50.0f) : 1.5f;
		v.spillNear = std::clamp(DefaultValue("NeonSpillNear", 0.5f), 0.0f, 4.0f);
		v.spillFar = std::clamp(DefaultValue("NeonSpillFar", 1.0f), 0.0f, 4.0f);
		v.spillCap = std::clamp(DefaultValue("NeonSpillBrightCap", 16.0f), 0.0f, 100.0f);
		v.spillMemorySeconds = std::clamp(DefaultValue("NeonSpillMemory", 1.0f), 0.0f, 5.0f);
		v.spillHoldAfterTurn = std::clamp(DefaultValue("NeonSpillHoldAfterTurn", 0.0f), 0.0f, 5.0f);
		v.spillSeparateDepth = std::clamp(DefaultValue("NeonSpillSeparateDepth", 0.0f), 0.0f, 1.0f);
		v.spillCapSceneLight = std::clamp(DefaultValue("NeonSpillCapSceneLight", 0.0f), 0.0f, 1.0f);
		v.spillMaskSpillOnly = std::clamp(DefaultValue("NeonSpillMaskSpillOnly", 0.0f), 0.0f, 1.0f);
		v.spillHardEdges = std::clamp(DefaultValue("NeonSpillHardEdges", 0.0f), 0.0f, 1.0f);
		v.spillDebug = 0;
	}

	inline BounceValues Defaults;
	inline void WithoutFive(BounceValues& v) {
		v.spillSeparateDepth = v.spillHardEdges = v.spillCapSceneLight = v.spillMaskSpillOnly = 0.0f;
		v.spillHoldAfterTurn = 0.0f;
	}
	struct Config { const char* name; const char* slug; void (*apply)(BounceValues&); bool bloomOff = false; bool pictureOnly = false; };
	inline const Config Configs[] = {
		{ "spill off", "spill-off", [](BounceValues& v) { v.neonSpill = 0.0f; } },
		{ "your settings", "yours", [](BounceValues&) {} },
		{ "your settings, half strength", "yours-half", [](BounceValues& v) { v.neonSpill *= 0.5f; } },
		{ "defaults", "defaults", [](BounceValues& v) { v = Defaults; } },
		{ "defaults, half strength", "defaults-half", [](BounceValues& v) { v = Defaults; v.neonSpill *= 0.5f; } },
		{ "defaults, NeonSpill 0.01", "defaults-neonspill-0.01", [](BounceValues& v) { v = Defaults; v.neonSpill = 0.01f; } },
		{ "defaults without the five round-6 keys", "defaults-without-five", [](BounceValues& v) { v = Defaults; WithoutFive(v); } },
		{ "defaults without SeparateDepth", "defaults-without-separatedepth", [](BounceValues& v) { v = Defaults; v.spillSeparateDepth = 0.0f; } },
		{ "defaults without CapSceneLight and MaskSpillOnly", "defaults-without-cap-mask", [](BounceValues& v) { v = Defaults; v.spillCapSceneLight = v.spillMaskSpillOnly = 0.0f; } },
		{ "spill off, bloom off", "spill-off-bloom-off", [](BounceValues& v) { v.neonSpill = 0.0f; }, true },
		{ "your settings, bloom off", "yours-bloom-off", [](BounceValues&) {}, true },
		{ "your settings, NeonSpillDebug 2 (the bounce light alone)", "yours-debug2-light-only", [](BounceValues& v) { v.spillDebug = 2; }, false, true },
	};
	enum { ConfigCount = sizeof(Configs) / sizeof(Configs[0]), Reference = 0, YourSettings = 1, YoursHalf = 2, DefaultSettings = 3, DefaultsHalf = 4, DefaultsEpsilon = 5, BloomReference = 9, YoursBloomOff = 10,
		MeasuredConfigs = YoursBloomOff + 1, FirstPictureOnly = MeasuredConfigs,
		MainConfigs = BloomReference, MainBlocks = 2 * (MainConfigs - 1) + 1 + 3 + (ConfigCount - FirstPictureOnly),
		MaxBlocks = SelfTestCore::MaxBlocks };
	static_assert(ConfigCount <= SelfTestCore::MaxConfigs && MainBlocks + 2 * (MeasuredConfigs - 1) + 2 <= MaxBlocks, "SelfTestCore limits");
	static_assert(MeasuredConfigs + 2 <= 16, "the light sheet: the measured configurations + the drift and mask tiles in 4 x 4");
	static_assert(Configs[BloomReference].bloomOff && Configs[YoursBloomOff].bloomOff && Configs[FirstPictureOnly].pictureOnly, "the configuration indices");
	inline bool PictureOnly(int config) { return config >= FirstPictureOnly && config < ConfigCount; }
	inline bool IsReference(int config) { return config == Reference || config == BloomReference; }
	inline int ReferenceOf(int config) { return Configs[config].bloomOff ? BloomReference : Reference; }
	inline bool BloomBracket = false;
	inline SelfTestCore::FrameOverride Override;

	inline SelfTestCore::ShotReader Shots;
	inline bool ShotsOn = false, ShotFormatLogged = false;
	inline bool ShotTaken[ConfigCount] = {};
	inline std::atomic<int> ShotsInFlight{ 0 };
	struct ShotJob { std::vector<unsigned> bgra; unsigned width = 0, height = 0; int config = 0; ULONGLONG readMs = 0; char path[MAX_PATH] = ""; char run[32] = ""; };

	inline bool LastSetting = false;
	inline bool Running = false;
	inline bool Overridden = false;
	inline bool CaptureFrame = false;
	inline bool Captured[Points] = {};
	inline SelfTestCore::Schedule Plan;
	inline SelfTestCore::Reader Readers[Points];
	inline SelfTestCore::Series Series[Points];
	inline SelfTestCore::PointData Data[Points];
	inline std::vector<float> Frame;
	inline BounceValues User;
	inline float SpillOnStrength = 0.0f;
	inline float Threshold = 0.0f;
	inline TESObjectCELL* Cell = nullptr;
	inline NiPoint3 StartPos = {}, StartRot = {};
	inline bool MovedBlock[MaxBlocks] = {};
	inline float SceneLight[MaxBlocks] = {}, Hour[MaxBlocks] = {};
	inline UInt32 Weather[MaxBlocks] = {};
	inline bool Retrying = false;
	inline bool Retried[ConfigCount] = {}, NotMeasurable[ConfigCount] = {};
	inline SelfTestCore::Intrusion FirstCheck[ConfigCount], SecondCheck[ConfigCount];
	inline unsigned Missed = 0;
	inline double StartTime = 0.0;
	inline char RunName[32] = "";
	inline void Report();
	inline void Summary();
	inline void StartPictures();
	inline bool ShotDiskCheck();
	inline std::atomic<bool> PicturesBusy{ false };

	enum : UInt32 { kLinkStart = 0x4E560001, kLinkSpot = 0x4E560002, kLinkStarted = 0x4E560011, kLinkStopped = 0x4E560012, kLinkDone = 0x4E560013, kLinkPictures = 0x4E560014 };
	struct SpotInfo { UInt32 size; UInt32 cell, world, interior; float x, y, z, rotX, rotZ, hour; UInt32 weather, weaponOut, firstPerson; };
	inline NVSEMessagingInterface* LinkMessaging = nullptr;
	inline UInt32 LinkHandle = 0;
	inline bool RemoteStart = false;
	inline bool RemoteRun = false;
	inline std::atomic<bool> PicturesWritten{ false };
	inline char PicturesRun[32] = "";
	inline void Notify(UInt32 type, const char* text) {
		if (!RemoteRun || !LinkMessaging) return;
		LinkMessaging->Dispatch(LinkHandle, type, (void*)text, (UInt32)strlen(text) + 1, "NVRAutoTest");
	}

	inline void Free() {
		for (int p = 0; p < Points; p++) { Readers[p].Release(); Series[p].Reset(); Data[p].Reset(); }
		Shots.Release();
		Frame.clear(); Frame.shrink_to_fit();
	}

	inline void Reload() {
		if (BounceLightEffect* bounce = TheShaderManager->Effects.BounceLight) bounce->UpdateSettings();
	}

	inline void Stop(const char* why) {
		Logger::Log("NEON SELF TEST %s: stopped, %s. Nothing measured; tick the box again to retry.", RunName, why);
		Notify(kLinkStopped, why);
		Running = false;
		Free();
		Reload();
	}

	inline UInt32 CurrentWeather() { Sky* sky = Tes ? Tes->sky : nullptr; return sky && sky->firstWeather ? sky->firstWeather->refID : 0; }

	inline void AutoTestMessage(NVSEMessagingInterface::Message* msg);
	inline void RegisterLink() {
		const bool ok = LinkMessaging && LinkMessaging->RegisterListener(LinkHandle, "NVRAutoTest", AutoTestMessage);
		Logger::Log("NEON SELF TEST: auto-test link: listener for the plugin NVRAutoTest %s", ok ? "REGISTERED (an auto-test run)" : "not registered (no NVRAutoTest plugin: normal play)");
	}
	inline void AutoTestMessage(NVSEMessagingInterface::Message* msg) {
		if (!msg) return;
		Logger::Log("NEON SELF TEST: auto-test link: message %08X from %s (%u bytes)", msg->type, msg->sender ? msg->sender : "?", msg->dataLen);
		if (msg->type == kLinkStart) {
			if (SettingShots::Requested()) { SettingShots::Request(LinkMessaging, LinkHandle); Logger::Log("SETTING SHOTS: the auto-test plugin asks for the shot list"); return; }
			RemoteStart = true; Logger::Log("NEON SELF TEST: the auto-test plugin asks for a run"); return;
		}
		if (msg->type != kLinkSpot || msg->dataLen != sizeof(SpotInfo) || !msg->data) return;
		SpotInfo* s = (SpotInfo*)msg->data;
		if (s->size != sizeof(SpotInfo)) return;
		memset((char*)s + sizeof(UInt32), 0, sizeof(SpotInfo) - sizeof(UInt32));
		if (!Player || !Player->parentCell) { Logger::Log("NVRAUTOTEST SPOT: no game loaded"); return; }
		TESWorldSpace* world = Player->GetWorldSpace();
		s->cell = Player->parentCell->refID; s->world = world ? world->refID : 0; s->interior = Player->parentCell->IsInterior() ? 1 : 0;
		s->x = Player->pos.x; s->y = Player->pos.y; s->z = Player->pos.z; s->rotX = Player->rot.x; s->rotZ = Player->rot.z;
		s->hour = TheShaderManager->ShaderConst.GameTime.y; s->weather = CurrentWeather();
		s->weaponOut = Player->process && Player->process->IsWeaponOut() ? 1 : 0; s->firstPerson = Player->IsFirstPerson() ? 1 : 0;
		Logger::Log("NVRAUTOTEST SPOT: cell %08X, world %08X, interior %u, position %.2f %.2f %.2f, angles x %.4f z %.4f rad, game hour %.3f, weather %08X, weapon out %u, first person %u",
			s->cell, s->world, s->interior, s->x, s->y, s->z, s->rotX, s->rotZ, s->hour, s->weather, s->weaponOut, s->firstPerson);
	}

	inline void BeginFrame() {
		if (PicturesWritten.exchange(false) && LinkMessaging)
			LinkMessaging->Dispatch(LinkHandle, kLinkPictures, PicturesRun, (UInt32)strlen(PicturesRun) + 1, "NVRAutoTest");
		const bool setting = TheSettingManager->SettingsMain.Main.NeonSpillSelfTest;
		static bool first = true;
		if (first) { first = false; LastSetting = setting; }
		const bool remote = RemoteStart && !Running;
		RemoteStart = false;
		const bool start = (setting && !LastSetting || remote) && !Running;
		LastSetting = setting;
		if (!Running && !start) return;
		if (Running && TheRenderManager->device->TestCooperativeLevel() != D3D_OK) { Stop("the device was lost (alt-tab?)"); return; }
		if (start) RemoteRun = remote;
		BounceLightEffect* bounce = TheShaderManager->Effects.BounceLight;
		CaptureFrame = false;
		Captured[AfterSpill] = Captured[Final] = false;
		if (start) {
			SYSTEMTIME t; GetLocalTime(&t);
			sprintf_s(RunName, "%02u-%02u-%02u", (unsigned)t.wHour, (unsigned)t.wMinute, (unsigned)t.wSecond);
			if (!bounce || !bounce->Enabled || !bounce->Effect) {
				Logger::Log("NEON SELF TEST %s: Bounce Light is off. Turn it on (and its NeonSpill) and tick the box again.", RunName);
				Notify(kLinkStopped, "Bounce Light is off");
				return;
			}
			if (!Player || !Player->parentCell) { Logger::Log("NEON SELF TEST %s: no game loaded.", RunName); Notify(kLinkStopped, "no game loaded"); return; }
			if (PicturesBusy || ShotsInFlight > 0) { Logger::Log("NEON SELF TEST %s: the last run's pictures are still being written; tick the box again in a moment.", RunName); Notify(kLinkStopped, "the last run's pictures are still being written"); return; }
			if (TheRenderManager->device->TestCooperativeLevel() != D3D_OK) { Logger::Log("NEON SELF TEST %s: the device is lost; tick the box again in a moment.", RunName); Notify(kLinkStopped, "the device is lost"); return; }
			Free();
			Read(bounce, User);
			ReadDefaults(Defaults);
			SpillOnStrength = User.neonSpill > 0.0f ? User.neonSpill : 2.0f;
			Threshold = 0.0f;
			Plan = SelfTestCore::Schedule();
			Plan.settle = Settle; Plan.measure = Measure;
			for (int c = 1; c < MainConfigs; c++) { Plan.Add(Reference); Plan.Add(c); }
			Plan.Add(Reference);
			BloomEffect* bloom = TheShaderManager->Effects.Bloom;
			BloomBracket = bloom && bloom->Enabled && bloom->Effect && TheShaderManager->Shaders.Tonemapping;
			if (BloomBracket) { Plan.Add(BloomReference); Plan.Add(YoursBloomOff); Plan.Add(BloomReference); }
			for (int c = FirstPictureOnly; c < ConfigCount; c++) Plan.Add(c);
			for (int c = 0; c < ConfigCount; c++) ShotTaken[c] = false;
			ShotsOn = ShotDiskCheck();
			ShotFormatLogged = false;
			for (int b = 0; b < MaxBlocks; b++) { MovedBlock[b] = false; SceneLight[b] = Hour[b] = 0.0f; Weather[b] = 0; }
			Retrying = false;
			for (int c = 0; c < ConfigCount; c++) { Retried[c] = NotMeasurable[c] = false; FirstCheck[c] = SecondCheck[c] = SelfTestCore::Intrusion(); }
			Missed = 0;
			Cell = Player->parentCell;
			StartTime = TheFrameRateManager->Time;
			Running = true;
			Logger::Log("NEON SELF TEST %s: started. Stand still, hands off the mouse and keys, about 30-35 seconds; the picture changes between settings. "
				"Make sure nobody is walking near you (console 'tai' stops them; 'tai' again after). "
				"%d blocks (spill off between each of %d settings) x (%d frames to settle + %d read), read at a quarter of the screen size; "
				"a setting disturbed by something moving runs once more at the end; the last %d blocks are pictures only.%s",
				RunName, Plan.Blocks(), (int)ConfigCount - 1, (int)Settle, (int)Measure, (int)(ConfigCount - FirstPictureOnly),
				BloomBracket ? " Three blocks before them run without bloom." : " NVR's bloom is off: no bloom-off bracket.");
			Logger::Log("NEON SELF TEST %s: your settings: NeonSpill %.2f%s, Reach %.2f, LightLevel %.2f, Near %.2f, Far %.2f, BrightCap %.1f, Memory %.1f s, "
				"SeparateDepth %.0f, HardEdges %.0f, HoldAfterTurn %.2f s, CapSceneLight %.0f, MaskSpillOnly %.0f, Debug %d (shown as 0 during the test).",
				RunName, User.neonSpill, User.neonSpill > 0.0f ? "" : " (off: the test uses 2.0 for the spill-on settings)", User.spillReach, User.spillLightLevel,
				User.spillNear, User.spillFar, User.spillCap, User.spillMemorySeconds, User.spillSeparateDepth, User.spillHardEdges, User.spillHoldAfterTurn,
				User.spillCapSceneLight, User.spillMaskSpillOnly, User.spillDebug);
			Notify(kLinkStarted, RunName);
			Logger::Log("NEON SELF TEST %s: defaults (from the defaults toml): NeonSpill %.2f, Reach %.2f, LightLevel %.2f, Near %.2f, Far %.2f, BrightCap %.1f, Memory %.1f s, "
				"SeparateDepth %.0f, HardEdges %.0f, HoldAfterTurn %.2f s, CapSceneLight %.0f, MaskSpillOnly %.0f.",
				RunName, Defaults.neonSpill, Defaults.spillReach, Defaults.spillLightLevel, Defaults.spillNear, Defaults.spillFar, Defaults.spillCap, Defaults.spillMemorySeconds,
				Defaults.spillSeparateDepth, Defaults.spillHardEdges, Defaults.spillHoldAfterTurn, Defaults.spillCapSceneLight, Defaults.spillMaskSpillOnly);
		}
		if (!Player || Player->parentCell != Cell) { Stop("you changed place"); return; }
		if (InterfaceManager->IsActive(Menu::kMenuType_Loading)) { Stop("a loading screen came up"); return; }
		if (!bounce || !bounce->Enabled || !bounce->Effect) { Stop("Bounce Light was turned off"); return; }
		BounceValues now;
		Read(bounce, now);
		if (!(now == User)) { Stop("a Bounce Light setting changed during the test"); return; }
		const int config = Plan.Config();
		BounceValues v = User;
		v.neonSpill = SpillOnStrength;
		v.spillDebug = 0;
		Configs[config].apply(v);
		Write(bounce, v);
		Overridden = true;
		TonemappingShaders* tonemap = TheShaderManager->Shaders.Tonemapping;
		if (Configs[config].bloomOff && BloomBracket) Override.Set(&tonemap->Constants.BloomData.x, 0.0f);
		CaptureFrame = Plan.Measuring();
		if (Plan.FirstMeasure()) { StartPos = Player->pos; StartRot = Player->rot; }
		else if (CaptureFrame) {
			const float dx = Player->pos.x - StartPos.x, dy = Player->pos.y - StartPos.y, dz = Player->pos.z - StartPos.z;
			if (dx * dx + dy * dy + dz * dz > 64.0f || fabsf(Player->rot.x - StartRot.x) > 0.0052f || fabsf(Player->rot.z - StartRot.z) > 0.0052f)
				MovedBlock[Plan.Block()] = true;
		}
	}

	inline bool ShotDiskCheck() {
		char folder[MAX_PATH] = "", windows[MAX_PATH] = "";
		GetFullPathNameA(".\\NVR-Unofficial-Optimized-Logs\\", MAX_PATH, folder, nullptr);
		GetWindowsDirectoryA(windows, MAX_PATH);
		ULARGE_INTEGER free = {};
		const bool known = GetDiskFreeSpaceExA(folder, &free, nullptr, nullptr) != 0;
		const bool system = toupper((unsigned char)folder[0]) == toupper((unsigned char)windows[0]);
		const unsigned long long need = (system ? 2ull : 1ull) << 30;
		const bool on = known && free.QuadPart >= need;
		Logger::Log("NEON SELF TEST %s: shots %s: %d full-size PNGs to %s (about 4-7 MB each at 2560x1440); %.1f GB free on that drive, %llu GB needed%s",
			RunName, on ? "ON" : "OFF", (int)ConfigCount, folder, known ? free.QuadPart / 1073741824.0 : -1.0, need >> 30,
			on ? "" : " - the numbers still run, without shots");
		return on;
	}

	inline DWORD WINAPI ShotWorker(LPVOID parameter) {
		ShotJob* job = (ShotJob*)parameter;
		const ULONGLONG start = GetTickCount64();
		const bool saved = SelfTestCore::SavePng(job->path, job->width, job->height, job->bgra);
		WIN32_FILE_ATTRIBUTE_DATA info = {};
		const unsigned long long size = saved && GetFileAttributesExA(job->path, GetFileExInfoStandard, &info) ? ((unsigned long long)info.nFileSizeHigh << 32 | info.nFileSizeLow) : 0;
		Logger::Log("NEON SELF TEST %s: shot %02d '%s'%s -> %s %s (%ux%u, %llu KB, read in %llu ms, written in %llu ms)", job->run, job->config, Configs[job->config].name,
			PictureOnly(job->config) ? " (picture only)" : "", job->path, saved ? "saved" : "NOT saved (the PNG write failed)", job->width, job->height, size >> 10,
			job->readMs, GetTickCount64() - start);
		delete job;
		ShotsInFlight--;
		return 0;
	}

	inline void TakeShot(int config, IDirect3DSurface9* surface) {
		if (!ShotsOn || config < 0 || config >= ConfigCount || ShotTaken[config]) return;
		ShotTaken[config] = true;
		if (ShotsInFlight >= 4) { Logger::Log("NEON SELF TEST %s: shot %02d '%s' skipped: 4 shots still being written", RunName, config, Configs[config].name); return; }
		ShotJob* job = new (std::nothrow) ShotJob();
		if (!job) { Logger::Log("NEON SELF TEST %s: shot %02d skipped: no memory", RunName, config); return; }
		const ULONGLONG start = GetTickCount64();
		if (!Shots.Read(TheRenderManager->device, surface, job->bgra, job->width, job->height)) {
			Logger::Log("NEON SELF TEST %s: shot %02d '%s' NOT taken: %s", RunName, config, Configs[config].name, Shots.error ? Shots.error : "?");
			delete job;
			return;
		}
		job->readMs = GetTickCount64() - start;
		if (!ShotFormatLogged) {
			ShotFormatLogged = true;
			Logger::Log("NEON SELF TEST %s: shots read from the output surface: %ux%u %s, %s", RunName, Shots.desc.Width, Shots.desc.Height,
				SelfTestCore::FormatName(Shots.desc.Format), Shots.desc.MultiSampleType != D3DMULTISAMPLE_NONE ? "multisampled (resolved first)" : "not multisampled");
		}
		job->config = config;
		strcpy_s(job->run, RunName);
		sprintf_s(job->path, ".\\NVR-Unofficial-Optimized-Logs\\NeonSelfTest_%s_shot_%02d_%s.png", RunName, config, Configs[config].slug);
		ShotsInFlight++;
		HANDLE thread = CreateThread(nullptr, 0, ShotWorker, job, 0, nullptr);
		if (!thread) { ShotsInFlight--; delete job; Logger::Log("NEON SELF TEST %s: shot %02d skipped: no thread", RunName, config); return; }
		CloseHandle(thread);
	}

	inline void Capture(int point, IDirect3DSurface9* surface) {
		if (!Running || !CaptureFrame || Captured[point] || !surface) return;
		Captured[point] = true;
		SelfTestCore::Reader& reader = Readers[point];
		if (!reader.Read(TheRenderManager->device, surface, Frame)) {
			static bool told[Points] = {};
			if (!told[point]) { told[point] = true; Logger::Log("NEON SELF TEST: cannot read the %s: %s", PointNames[point], reader.error ? reader.error : "?"); }
			return;
		}
		if (!Series[point].Add(Frame, reader.width, reader.height)) { Stop("the screen size changed"); return; }
		if (point == Final && Plan.LastMeasure()) TakeShot(Plan.Config(), surface);
	}

	inline void FinishBlock(int block) {
		BounceLightEffect* bounce = TheShaderManager->Effects.BounceLight;
		const int config = Plan.ConfigOf(block);
		const bool reference = IsReference(config);
		if (bounce) {
			const float threshold = bounce->SpillThreshold();
			SceneLight[block] = bounce->spillLightLevel > 0.0f ? threshold / bounce->spillLightLevel : 0.0f;
			if (config == YourSettings && Threshold <= 0.0f) Threshold = SceneLight[block] * Defaults.spillLightLevel;
		}
		Hour[block] = TheShaderManager->ShaderConst.GameTime.y;
		Weather[block] = CurrentWeather();
		Logger::Log("NEON SELF TEST %s block %d (%s): scene light %.3f, game hour %.2f, weather %08X, frames read %u/%u%s", RunName, block + 1,
			Configs[config].name, SceneLight[block], Hour[block], Weather[block], Series[AfterSpill].frames, (unsigned)Measure,
			MovedBlock[block] ? ", YOU MOVED" : "");
		if (PictureOnly(config)) { for (int p = 0; p < Points; p++) Series[p].Reset(); return; }
		for (int p = 0; p < Points; p++) {
			SelfTestCore::PointData& d = Data[p];
			if (!Series[p].frames) { Series[p].Reset(); continue; }
			SelfTestCore::BlockStats stats;
			stats.From(Series[p]);
			Series[p].Reset();
			if (block == 0) { SelfTestCore::FirstReference(d, std::move(stats), p == AfterSpill); continue; }
			if (!stats.Valid(d.width, d.height)) continue;
			if (!reference) {
				if (config == YourSettings && !d.masks) {
					if (p == AfterSpill) {
						SelfTestCore::BuildMasks(d, Threshold);
						Logger::Log("NEON SELF TEST %s: the sign mask uses the defaults' light test: %.3f (LightLevel %.2f x the scene's own light %.3f), the same pixels in every setting",
							RunName, Threshold, Defaults.spillLightLevel, SceneLight[block]);
					}
					else if (Data[AfterSpill].masks && Data[AfterSpill].width == d.width && Data[AfterSpill].height == d.height) {
						d.mask = Data[AfterSpill].mask; d.masks = true; d.threshold = Threshold;
					}
				}
				SelfTestCore::ConfigurationBlock(d, std::move(stats));
			}
			else {
				const int before = Plan.ConfigOf(block - 1);
				if (IsReference(before) || PictureOnly(before)) {
					if (config == BloomReference && before == Reference) {
						d.results[BloomReference] = SelfTestCore::Compare(d, d.previous, d.previous, stats, BloomReference);
						d.results[BloomReference].moved = MovedBlock[block - 1] || MovedBlock[block];
					}
					SelfTestCore::NextReference(d, std::move(stats), BloomReference, config == Reference);
				}
				else {
					SelfTestCore::NextReference(d, std::move(stats), before, config == Reference);
					d.results[before].moved = MovedBlock[block - 2] || MovedBlock[block - 1] || MovedBlock[block];
				}
			}
		}
		if (block == 0) for (int p = 0; p < Points; p++) Data[p].results[Reference].moved = MovedBlock[0];
	}

	inline bool PlanRetries() {
		int added = 0;
		for (int c = 1; c < ConfigCount; c++) {
			if (IsReference(c) || PictureOnly(c) || (c == YoursBloomOff && !BloomBracket)) continue;
			FirstCheck[c] = SelfTestCore::CheckIntrusion(Data[AfterSpill], c, ConfigCount);
			if (!FirstCheck[c].flagged) continue;
			Retried[c] = true;
			for (int p = 0; p < Points; p++) Data[p].firstTry[c] = Data[p].results[c];
			const int reference = ReferenceOf(c);
			if (Plan.ConfigOf(Plan.Blocks() - 1) != reference) Plan.Add(reference);
			Plan.Add(c); Plan.Add(reference);
			added++;
			Logger::Log("NEON SELF TEST %s: setting %d '%s' was disturbed (%.1f%% of the lit picture 10%%+ darker, flicker x%.1f of spill off where the spill does not reach): "
				"someone walked past? It runs once more at the end.", RunName, c, Configs[c].name, FirstCheck[c].darker, FirstCheck[c].flickerRatio);
		}
		if (added) Logger::Log("NEON SELF TEST %s: %d setting(s) run again, about %d s more; keep standing still.", RunName, added, added * 2 * (Settle + Measure) / 60 + 1);
		return added > 0;
	}
	inline void CheckRetries() {
		for (int c = 1; c < ConfigCount; c++) {
			if (!Retried[c]) continue;
			SecondCheck[c] = SelfTestCore::CheckIntrusion(Data[AfterSpill], c, ConfigCount);
			NotMeasurable[c] = SecondCheck[c].flagged;
		}
	}

	inline void EndFrame(bool output) {
		if (Overridden) {
			Overridden = false;
			if (BounceLightEffect* bounce = TheShaderManager->Effects.BounceLight) Write(bounce, User);
		}
		Override.Restore();
		if (!Running || !output) return;
		if (CaptureFrame && !(Captured[AfterSpill] && Captured[Final])) {
			if (++Missed > 300) Stop("the effects did not run for 300 frames (a menu open, or the effects off?)");
			return;
		}
		Missed = 0;
		if (Plan.Measuring() && Plan.LastMeasure()) FinishBlock(Plan.Block());
		Plan.step++;
		if (Plan.Done()) {
			if (!Retrying) { Retrying = true; if (PlanRetries()) return; }
			else CheckRetries();
			Running = false;
			Report();
			Free();
			Reload();
		}
	}

	inline const char* Status(int config, bool moved) {
		static char text[200];
		text[0] = 0;
		if (config > 0 && config < ConfigCount && NotMeasurable[config])
			sprintf_s(text, " | NOT MEASURABLE HERE: disturbed twice (something moving: %.1f%% darker, flicker x%.1f)", SecondCheck[config].darker, SecondCheck[config].flickerRatio);
		else if (config > 0 && config < ConfigCount && Retried[config])
			sprintf_s(text, " | run again: the first try was disturbed (%.1f%% darker, flicker x%.1f), this is the second", FirstCheck[config].darker, FirstCheck[config].flickerRatio);
		if (moved) strcat_s(text, " | YOU MOVED: less reliable");
		return text;
	}

	inline void LogResult(const SelfTestCore::Result& r, int config, int point) {
		if (!r.valid) { Logger::Log("NEON SELF TEST %s [%s] %d %s: nothing read", RunName, PointNames[point], config, Configs[config].name); return; }
		const SelfTestCore::Figures& f = r.stable;
		char masks[160] = "";
		if (f.masks) sprintf_s(masks, " | lights x%.2f, colour %.0f%% | ring round them x%.2f, colour %.0f%%", f.lightsRatio, 100.0f * f.lightsColour, f.ringRatio, 100.0f * f.ringColour);
		Logger::Log("NEON SELF TEST %s [%s] %d %s: light x%.4f, %.1f%% of the lit picture 50%%+ brighter (%.1f%% 10%%+, %.1f%% 10%%+ darker)%s | "
			"flicker %.2f%% a frame (spill off %.2f%%, %+.2f), flickering pixels %.1f%% (spill off %.1f%%)%s",
			RunName, PointNames[point], config, Configs[config].name, f.whole, f.brighter50, f.brighter10, f.darker10, masks,
			f.flicker, f.refFlicker, f.flicker - f.refFlicker, f.flickerPixels, f.refFlickerPixels, Status(config, r.moved));
	}

	inline void Report() {
		Logger::Log("NEON SELF TEST %s: done in %.1f s.", RunName, TheFrameRateManager->Time - StartTime);
		float low = 1e30f, high = 0.0f;
		const int blocks = Plan.Blocks();
		for (int b = 0; b < blocks; b++) if (SceneLight[b] > 0.0f) { low = (std::min)(low, SceneLight[b]); high = (std::max)(high, SceneLight[b]); }
		const float change = high > 0.0f && low < 1e30f ? 100.0f * (high - low) / low : 0.0f;
		Logger::Log("NEON SELF TEST %s: the scene's own light %s %.1f%% during the test (%.3f to %.3f); game hour %.2f to %.2f; weather %08X to %08X%s",
			RunName, change > 2.0f ? "CHANGED" : "changed", change, low < 1e30f ? low : 0.0f, high, Hour[0], Hour[blocks - 1], Weather[0], Weather[blocks - 1],
			change > 2.0f ? " - more than 2%: a sunset or a weather change; the bracketing cancels an even change only" : "");
		for (int p = 0; p < Points; p++) {
			SelfTestCore::PointData& d = Data[p];
			if (d.refLuma.empty()) { Logger::Log("NEON SELF TEST %s [%s]: nothing read", RunName, PointNames[p]); continue; }
			SelfTestCore::FinishRun(d);
			char noise[512] = ""; size_t used = 0; bool pass = true; int inner = 0;
			for (int k = 1; k + 1 < d.references; k++) {
				const double all = SelfTestCore::ReferenceRatio(d, k, false), steady = SelfTestCore::ReferenceRatio(d, k, true);
				if (used < sizeof(noise) - 40) used += sprintf_s(noise + used, sizeof(noise) - used, " %.4f (whole %.4f)", steady, all);
				if (fabs(steady - 1.0) > 0.01) pass = false;
				inner++;
			}
			const int steadyRegions = SelfTestCore::Regions - d.excludedCount;
			if (steadyRegions < 32) pass = false;
			Logger::Log("NEON SELF TEST %s [%s]: noise floor (each inner spill-off block against the two round it: steady regions within 1 +- 0.01, "
				"and at least 32 of 64 regions steady: %d): %s; largest steady deviation %.4f;%s",
				RunName, PointNames[p], steadyRegions, inner ? (pass ? "PASS" : "FAIL") : "not measured", SelfTestCore::NoiseLevel(d), noise);
			Logger::Log("NEON SELF TEST %s [%s]: %d of %d regions changed by themselves between spill-off blocks (animated things, the sky): left out of the figures below",
				RunName, PointNames[p], d.excludedCount, (int)SelfTestCore::Regions);
			const SelfTestCore::Figures& off = d.results[Reference].stable;
			if (d.masks) Logger::Log("NEON SELF TEST %s [%s]: the signs: light pixels (the defaults' light test %.3f, the same in every setting) %.1f%% of the picture, the ring within %d px of them %.1f%%",
				RunName, PointNames[p], d.threshold, off.lightsShare, (int)SelfTestCore::RingRadius * SelfTestCore::Scale, off.ringShare);
			else Logger::Log("NEON SELF TEST %s [%s]: no light/ring figures (the spill's light test was not known)", RunName, PointNames[p]);
			for (int c = 0; c < ConfigCount; c++) {
				if (PictureOnly(c)) Logger::Log("NEON SELF TEST %s [%s] %d %s: picture only, no numbers (shot %02d)", RunName, PointNames[p], c, Configs[c].name, c);
				else LogResult(d.results[c], c, p);
			}
			for (int c = 1; c < ConfigCount; c++) {
				if (!Retried[c] || !d.firstTry[c].valid) continue;
				const SelfTestCore::Figures f = SelfTestCore::Sum(d, d.firstTry[c], true);
				Logger::Log("NEON SELF TEST details %s [%s] %d %s, the disturbed first try: light x%.3f, %.1f%% 10%%+ darker, flicker %.2f%% (spill off %.2f%%)",
					RunName, PointFiles[p], c, Configs[c].name, f.whole, f.darker10, f.flicker, f.refFlicker);
			}
			for (int c = 1; c < ConfigCount; c++) {
				const SelfTestCore::Result& r = d.results[c];
				if (!r.valid) continue;
				char text[SelfTestCore::Grid * 8 + 16];
				for (int gy = 0; gy < SelfTestCore::Grid; gy++) {
					size_t at = 0; text[0] = 0;
					for (int gx = 0; gx < SelfTestCore::Grid; gx++)
						at += sprintf_s(text + at, sizeof(text) - at, "%5.2f%c", r.ratio[gy][gx], d.excluded[gy * SelfTestCore::Grid + gx] ? '*' : ' ');
					Logger::Log("NEON SELF TEST details %s [%s] %d %s, light by region (* = changed by itself, left out), row %d: %s", RunName, PointFiles[p], c, Configs[c].name, gy + 1, text);
				}
				Logger::Log("NEON SELF TEST details %s [%s] %d %s: whole picture with every region x%.3f; strongest regions (row, column) %d,%d x%.2f | %d,%d x%.2f | %d,%d x%.2f",
					RunName, PointFiles[p], c, Configs[c].name, r.all.whole, r.top[0] / 8 + 1, r.top[0] % 8 + 1, r.ratio[r.top[0] / 8][r.top[0] % 8],
					r.top[1] / 8 + 1, r.top[1] % 8 + 1, r.ratio[r.top[1] / 8][r.top[1] % 8], r.top[2] / 8 + 1, r.top[2] % 8 + 1, r.ratio[r.top[2] / 8][r.top[2] % 8]);
			}
		}
		Summary();
		Notify(kLinkDone, RunName);
		StartPictures();
	}

	inline void SummaryLine(const SelfTestCore::PointData& d, int config, const char* who) {
		const SelfTestCore::Result& r = d.results[config];
		if (!r.valid) return;
		const SelfTestCore::Figures& f = r.stable;
		char masks[256] = "";
		if (f.masks) sprintf_s(masks, " The signs themselves: x%.2f brighter, %.0f%% of their colour kept; round them (between the tubes): x%.2f brighter, %.0f%% of the colour kept.",
			f.lightsRatio, 100.0f * f.lightsColour, f.ringRatio, 100.0f * f.ringColour);
		const double noise = SelfTestCore::NoiseLevel(d);
		char added[96];
		if (fabs(f.whole - 1.0) < 2.0 * noise) sprintf_s(added, "adds no light beyond the noise (%+.1f%%, noise +-%.1f%%)", 100.0 * (f.whole - 1.0), 100.0 * noise);
		else sprintf_s(added, "adds %.0f%% light", 100.0 * (f.whole - 1.0));
		Logger::Log("NEON SELF TEST %s SUMMARY, %s (before eye adaptation): the spill %s; %.1f%% of the lit picture gets 50%%+ brighter.%s "
			"Flicker: %.2f%% a frame against %.2f%% with the spill off (%+.2f); flickering pixels %.1f%% against %.1f%%.%s",
			RunName, who, added, f.brighter50, masks, f.flicker, f.refFlicker, f.flicker - f.refFlicker, f.flickerPixels, f.refFlickerPixels,
			Status(config, r.moved));
	}

	inline void Summary() {
		const SelfTestCore::PointData& d = Data[AfterSpill];
		if (!d.results[Reference].valid) return;
		SummaryLine(d, YourSettings, "your settings");
		SummaryLine(d, DefaultSettings, "the defaults");
		const SelfTestCore::Result& eps = d.results[DefaultsEpsilon], &full = d.results[DefaultSettings], &half = d.results[DefaultsHalf];
		if (eps.valid && !NotMeasurable[DefaultsEpsilon]) {
			Logger::Log("NEON SELF TEST %s SUMMARY: switching the spill on changes the picture by itself by %+.2f%% (defaults with NeonSpill 0.01; noise +-%.2f%%)",
				RunName, 100.0 * (eps.stable.whole - 1.0), 100.0 * SelfTestCore::NoiseLevel(d));
			if (full.valid && half.valid && !NotMeasurable[DefaultSettings] && !NotMeasurable[DefaultsHalf] && fabs(full.stable.whole - eps.stable.whole) > 1e-6) {
				const double linear = (full.stable.whole - half.stable.whole) / (full.stable.whole - eps.stable.whole);
				Logger::Log("NEON SELF TEST %s SUMMARY: linearity at the defaults (full - half) / (full - NeonSpill 0.01) = %.3f (0.4 to 0.6 expected)", RunName, linear);
			}
		}
		const double noise = SelfTestCore::NoiseLevel(d);
		char same[1024] = ""; size_t used = 0;
		for (int a = 1; a < ConfigCount; a++)
			for (int b = a + 1; b < ConfigCount; b++) {
				const SelfTestCore::Result& ra = d.results[a], &rb = d.results[b];
				if (!ra.valid || !rb.valid || NotMeasurable[a] || NotMeasurable[b] || IsReference(a) || IsReference(b)) continue;
				if (fabs(ra.stable.whole - rb.stable.whole) < 2.0 * noise && used < sizeof(same) - 80)
					used += sprintf_s(same + used, sizeof(same) - used, "%s'%s' = '%s'", used ? ", " : "", Configs[a].name, Configs[b].name);
			}
		Logger::Log("NEON SELF TEST %s SUMMARY: the same within noise (light differs by less than 2 x %.4f): %s", RunName, noise, used ? same : "none");
		const SelfTestCore::PointData& f = Data[Final];
		const SelfTestCore::Result& withBloom = f.results[YourSettings], &noBloom = f.results[YoursBloomOff];
		if (BloomBracket && withBloom.valid && noBloom.valid && withBloom.stable.masks && noBloom.stable.masks && !NotMeasurable[YoursBloomOff])
			Logger::Log("NEON SELF TEST %s SUMMARY: bloom, final picture, your settings: the signs keep %.0f%% of their colour with bloom and %.0f%% without "
				"(the ring round them %.0f%% / %.0f%%; brightness x%.2f / x%.2f). The difference is bloom's; what is lost without bloom is the tonemap's.",
				RunName, 100.0f * withBloom.stable.lightsColour, 100.0f * noBloom.stable.lightsColour, 100.0f * withBloom.stable.ringColour,
				100.0f * noBloom.stable.ringColour, withBloom.stable.lightsRatio, noBloom.stable.lightsRatio);
		else if (!BloomBracket) Logger::Log("NEON SELF TEST %s SUMMARY: bloom: not measured (NVR's bloom is off)", RunName);
		const SelfTestCore::Result& pre1 = d.results[YourSettings], &pre11 = d.results[YoursBloomOff];
		if (BloomBracket && pre1.valid && pre11.valid && pre1.stable.whole > 0.0) {
			const double check = pre11.stable.whole / pre1.stable.whole;
			Logger::Log("NEON SELF TEST %s SUMMARY: bloom bracket check: before tonemap, bloom-off yours vs yours: x%.4f (should be 1 within noise, +-%.4f)%s",
				RunName, check, 2.0 * noise, fabs(check - 1.0) <= 2.0 * noise ? "" : " - OUTSIDE THE NOISE: the bloom-off numbers are not trustworthy");
		}
		char flags[512] = ""; used = 0;
		for (int c = 1; c < ConfigCount; c++) {
			if (NotMeasurable[c]) used += sprintf_s(flags + used, sizeof(flags) - used, "%s'%s' not measurable here", used ? ", " : "", Configs[c].name);
			else if (Retried[c]) used += sprintf_s(flags + used, sizeof(flags) - used, "%s'%s' disturbed once (the re-run is used)", used ? ", " : "", Configs[c].name);
		}
		int moved = 0; for (int b = 0; b < Plan.Blocks(); b++) if (MovedBlock[b]) moved++;
		Logger::Log("NEON SELF TEST %s SUMMARY: disturbances: %s; blocks where you moved: %d.", RunName, used ? flags : "none", moved);
		int leastFlicker = -1, mostColour = -1;
		for (int c = 1; c < ConfigCount; c++) {
			const SelfTestCore::Result& r = d.results[c];
			if (!r.valid || NotMeasurable[c] || IsReference(c)) continue;
			if (leastFlicker < 0 || r.stable.flicker - r.stable.refFlicker < d.results[leastFlicker].stable.flicker - d.results[leastFlicker].stable.refFlicker) leastFlicker = c;
			if (r.stable.masks && (mostColour < 0 || r.stable.ringColour > d.results[mostColour].stable.ringColour)) mostColour = c;
		}
		if (leastFlicker > 0)
			Logger::Log("NEON SELF TEST %s SUMMARY: least added flicker with '%s' (%+.2f, adds %.0f%% light).", RunName, Configs[leastFlicker].name,
				d.results[leastFlicker].stable.flicker - d.results[leastFlicker].stable.refFlicker, 100.0 * (d.results[leastFlicker].stable.whole - 1.0));
		if (mostColour > 0)
			Logger::Log("NEON SELF TEST %s SUMMARY: most colour kept round the signs with '%s' (%.0f%%, adds %.0f%% light).", RunName, Configs[mostColour].name,
				100.0f * d.results[mostColour].stable.ringColour, 100.0 * (d.results[mostColour].stable.whole - 1.0));
	}

	struct PictureJob { SelfTestCore::PointData data[Points]; char run[32]; bool remote = false; };
	inline DWORD WINAPI PictureWorker(LPVOID parameter) {
		PictureJob* job = (PictureJob*)parameter;
		static const char* const Tiles[2] = {
			"tiles: spill off, then settings 1-10 against the spill-off blocks round them (blue darker, red brighter, full colour = 33%), "
			"the last spill-off block against the first (what changed by itself), the light pixels red and the ring round them yellow (regions left out edged dark red)",
			"tiles: spill off, then the flicker of spill off and settings 1-10 (black steady, yellow = 3% of the spill-off brightness a frame or more), the regions left out" };
		for (int p = 0; p < Points; p++) {
			const SelfTestCore::PointData& d = job->data[p];
			if (d.refLuma.empty()) continue;
			for (int kind = 0; kind < 2; kind++) {
				char path[MAX_PATH];
				sprintf_s(path, ".\\NVR-Unofficial-Optimized-Logs\\NeonSelfTest_%s_%s_%s.png", job->run, PointFiles[p], kind ? "flicker" : "light");
				Logger::Log("NEON SELF TEST %s: making picture %s (%ux%u)", job->run, path, d.width * 4, d.height * 4);
				const ULONGLONG start = GetTickCount64();
				std::vector<unsigned> sheet;
				const bool built = kind ? SelfTestCore::BuildFlickerSheet(sheet, d, MeasuredConfigs, 20.0) : SelfTestCore::BuildLightSheet(sheet, d, MeasuredConfigs, 20.0);
				const ULONGLONG builtAt = GetTickCount64();
				const bool saved = built && SelfTestCore::SavePng(path, d.width * 4, d.height * 4, sheet);
				Logger::Log("NEON SELF TEST %s: picture %s %s (built in %llu ms, written in %llu ms) - %s", job->run, path,
					saved ? "saved" : built ? "NOT saved (the PNG write failed)" : "NOT saved (bad data, or over the 20 s limit)",
					builtAt - start, GetTickCount64() - builtAt, Tiles[kind]);
			}
		}
		for (int i = 0; i < 1200 && ShotsInFlight > 0; i++) Sleep(50);
		Logger::Log("NEON SELF TEST %s: pictures done.%s", job->run, ShotsInFlight > 0 ? " (some shots were still being written after 60 s)" : "");
		if (job->remote) { strcpy_s(PicturesRun, job->run); PicturesWritten = true; }
		delete job;
		PicturesBusy = false;
		return 0;
	}

	inline void StartPictures() {
		PictureJob* job = new (std::nothrow) PictureJob();
		if (!job) { Logger::Log("NEON SELF TEST %s: no memory for the pictures; the numbers above stand.", RunName); Notify(kLinkPictures, "NOT written: no memory"); return; }
		for (int p = 0; p < Points; p++) job->data[p] = std::move(Data[p]);
		strcpy_s(job->run, RunName);
		job->remote = RemoteRun;
		PicturesBusy = true;
		HANDLE thread = CreateThread(nullptr, 0, PictureWorker, job, 0, nullptr);
		if (!thread) {
			PicturesBusy = false;
			delete job;
			Logger::Log("NEON SELF TEST %s: could not start the picture thread; the numbers above stand.", RunName);
			Notify(kLinkPictures, "NOT written: no thread");
			return;
		}
		CloseHandle(thread);
		Logger::Log("NEON SELF TEST %s: writing the pictures in the background (the game goes on).", RunName);
	}
}
