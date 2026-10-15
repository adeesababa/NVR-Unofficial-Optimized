#pragma once

// UNOFFICIAL, TEST ONLY (2026-10-12): A/B shots for the auto-test (work\NVRAutoTest, run_autotest.py --shots <file>). When the
// test plugin asks NVR for a run (EffectSelfTest's link, kLinkStart) and the launcher has put Data\NVSE\Plugins\NVRAutoTest.shots.txt
// in the game folder, NVR runs this instead of the neon self-test. Each shot in the file changes some settings IN MEMORY ONLY
// (never saved: the menu's Save is the only writer, and the test ends the game without it), waits a number of frames, saves the
// finished picture (before the HUD) as NVR-Unofficial-Optimized-Logs\NeonSelfTest_<run>_ab_<NN>_<slug>.png (the name the launcher
// collects), and puts every setting back before the next shot. Without the file (normal play) nothing here ever runs.
//
// The file, one command per line ('#' starts a comment):
//   frames 120                                   frames each shot waits before its picture (default 120: history buffers settle)
//   profile                                      switches the F10 GPU profile on for the run (its per-second lines go to the log)
//   shot <slug>                                  starts a shot; its picture is named after the slug
//   set <Section> <Key> <value>                  e.g. set Shaders.Reflections.Main GroundSheen 0.8 (typed as the setting is)
//   effect <Name> <0|1>                          an effect or shader collection off or on, as the menu's switch (e.g. effect Reflections 1)
//   console <command>                            a console command, run at the start of the shot's first frame (set GameHour to 13,
//                                                player.setangle x -30, fw 0015BD42, ...). NOT undone: it stays for the later shots,
//                                                and the place check allows a move during a shot that runs one.
//   final                                        (2026-10-15) every picture is the back buffer at ReShade's end of the frame
//                                                (after ReShade's effects and the HUD) instead of NVR's picture before the HUD;
//                                                needs ReShade's D3D9 runtime (ReShadeIntegration.cpp calls CaptureFinal).
// Shots start from the user's settings each time: a shot without changes is the user's own picture.
#include <atomic>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "EffectSelfTestCore.h"
#include "GpuProfiler.h"

namespace SettingShots {

	inline const char* const RequestFile = "Data\\NVSE\\Plugins\\NVRAutoTest.shots.txt";
	enum : UInt32 { kLinkStarted = 0x4E560011, kLinkStopped = 0x4E560012, kLinkPictures = 0x4E560014 };	// EffectSelfTest's link numbers

	struct Change { bool effect = false, console = false; std::string section, key, value; };
	struct Shot { std::string slug; std::vector<Change> changes; };
	struct Undo { bool effect = false; std::string section, key; UInt32 type = 0; bool b = false; int i = 0; float f = 0.0f; };
	struct Job { std::vector<unsigned> bgra; unsigned width = 0, height = 0; char path[MAX_PATH] = ""; };

	inline std::vector<Shot> List;
	inline int Frames = 120;
	inline bool Pending = false, Running = false, AllQueued = false;
	inline size_t Index = 0;
	inline int Frame = 0;
	inline std::vector<Undo> Undos;
	inline SelfTestCore::ShotReader Reader;
	inline std::atomic<int> InFlight{ 0 };
	inline char Run[32] = "";
	inline NVSEMessagingInterface* Messaging = nullptr;
	inline UInt32 Handle = 0;
	inline TESObjectCELL* Cell = nullptr;
	inline std::vector<std::string> Commands;	// this shot's console commands, run at the start of its first frame
	inline bool Moves = false;					// this shot ran console commands: the player may change place
	inline bool FinalPicture = false;			// 'final': pictures from the back buffer at ReShade's present
	inline bool WantFinal = false;				// the shot's frames are done: the next CaptureFinal takes it

	inline bool Requested() { return GetFileAttributesA(RequestFile) != INVALID_FILE_ATTRIBUTES; }

	inline void Notify(UInt32 type, const char* text) {
		if (Messaging) Messaging->Dispatch(Handle, type, (void*)text, (UInt32)strlen(text) + 1, "NVRAutoTest");
	}

	// The link's start, when the request file is there (EffectSelfTest::AutoTestMessage).
	inline void Request(NVSEMessagingInterface* messaging, UInt32 handle) { Messaging = messaging; Handle = handle; Pending = true; }

	inline bool Parse(std::string& error) {
		List.clear(); Frames = 120; FinalPicture = false; WantFinal = false;
		std::ifstream in(RequestFile);
		if (!in) { error = "the shot list could not be read"; return false; }
		std::string line;
		int number = 0;
		while (std::getline(in, line)) {
			number++;
			const size_t hash = line.find('#');
			if (hash != std::string::npos) line.resize(hash);
			std::istringstream words(line);
			std::string command;
			if (!(words >> command)) continue;
			if (command == "frames") { words >> Frames; Frames = std::clamp(Frames, 2, 1200); }
			else if (command == "profile") GpuTimer::Enabled = true;	// UNOFFICIAL 2026-10-13: the F10 GPU profile, on for the whole run
			else if (command == "final") FinalPicture = true;
			else if (command == "shot") { Shot s; words >> s.slug; if (s.slug.empty()) s.slug = "shot"; List.push_back(s); }
			else if (command == "console") {
				if (List.empty()) { error = "line " + std::to_string(number) + ": a command before the first 'shot'"; return false; }
				std::string rest;
				std::getline(words, rest);
				const size_t start = rest.find_first_not_of(" 	");
				if (start == std::string::npos) { error = "line " + std::to_string(number) + ": empty console command"; return false; }
				Change c; c.console = true; c.value = rest.substr(start);
				List.back().changes.push_back(c);
			}
			else if (command == "set" || command == "effect") {
				if (List.empty()) { error = "line " + std::to_string(number) + ": a change before the first 'shot'"; return false; }
				Change c;
				c.effect = command == "effect";
				if (c.effect) words >> c.section >> c.value;
				else words >> c.section >> c.key >> c.value;
				if (c.section.empty() || c.value.empty() || (!c.effect && c.key.empty())) { error = "line " + std::to_string(number) + ": incomplete"; return false; }
				List.back().changes.push_back(c);
			}
			else { error = "line " + std::to_string(number) + ": unknown command '" + command + "'"; return false; }
		}
		if (List.empty()) { error = "no shots in the list"; return false; }
		if (List.size() > 40) { error = "more than 40 shots"; return false; }
		return true;
	}

	inline bool Truthy(const std::string& v) { return v == "1" || v == "true" || v == "on" || v == "yes"; }

	// One shot's changes, the old values kept for Revert.
	inline void Apply(const Shot& s) {
		Commands.clear();
		Moves = false;
		for (const Change& c : s.changes) {
			if (c.console) { Commands.push_back(c.value); Moves = true; continue; }
			if (c.effect) {
				const bool want = Truthy(c.value);
				if (TheSettingManager->GetMenuShaderEnabled(c.section.c_str()) != want) {
					Undo u; u.effect = true; u.section = c.section; Undos.push_back(u);
					TheShaderManager->SwitchShaderStatus(c.section.c_str());
				}
				Logger::Log("SETTING SHOTS %s: shot '%s': effect %s %s", Run, s.slug.c_str(), c.section.c_str(), want ? "on" : "off");
				continue;
			}
			SettingManager::Configuration::ConfigNode node;
			if (!TheSettingManager->Config.FillNode(&node, c.section.c_str(), c.key.c_str())) {
				Logger::Log("SETTING SHOTS %s: shot '%s': no setting %s %s (skipped)", Run, s.slug.c_str(), c.section.c_str(), c.key.c_str());
				continue;
			}
			Undo u; u.section = c.section; u.key = c.key; u.type = node.Type; u.b = node.BoolValue; u.i = node.IntValue; u.f = node.FloatValue;
			Undos.push_back(u);
			switch (node.Type) {
			case SettingManager::Configuration::Boolean: TheSettingManager->SetSetting(c.section.c_str(), c.key.c_str(), Truthy(c.value)); break;
			case SettingManager::Configuration::Integer: TheSettingManager->SetSetting(c.section.c_str(), c.key.c_str(), atoi(c.value.c_str())); break;
			default: TheSettingManager->SetSetting(c.section.c_str(), c.key.c_str(), (float)atof(c.value.c_str())); break;
			}
			Logger::Log("SETTING SHOTS %s: shot '%s': %s %s = %s (was %s)", Run, s.slug.c_str(), c.section.c_str(), c.key.c_str(), c.value.c_str(), node.Value);
		}
		TheSettingManager->LoadSettings();
	}

	// Every change of the shot undone, newest first.
	inline void Revert() {
		for (auto u = Undos.rbegin(); u != Undos.rend(); ++u) {
			if (u->effect) { TheShaderManager->SwitchShaderStatus(u->section.c_str()); continue; }
			switch (u->type) {
			case SettingManager::Configuration::Boolean: TheSettingManager->SetSetting(u->section.c_str(), u->key.c_str(), u->b); break;
			case SettingManager::Configuration::Integer: TheSettingManager->SetSetting(u->section.c_str(), u->key.c_str(), u->i); break;
			default: TheSettingManager->SetSetting(u->section.c_str(), u->key.c_str(), u->f); break;
			}
		}
		const bool any = !Undos.empty();
		Undos.clear();
		if (any) TheSettingManager->LoadSettings();
	}

	inline void Stop(const char* why) {
		Logger::Log("SETTING SHOTS %s: stopped, %s", Run, why);
		Revert();
		Running = false;
		Notify(kLinkStopped, why);
	}

	inline DWORD WINAPI Worker(LPVOID parameter) {
		Job* job = (Job*)parameter;
		const bool saved = SelfTestCore::SavePng(job->path, job->width, job->height, job->bgra);
		Logger::Log("SETTING SHOTS: %s %s (%ux%u)", job->path, saved ? "saved" : "NOT saved", job->width, job->height);
		delete job;
		InFlight--;
		return 0;
	}

	// Render.cpp, right after ShaderManager::UpdateConstants and EffectSelfTest::BeginFrame.
	inline void BeginFrame() {
		if (AllQueued && InFlight == 0) {
			AllQueued = false;
			Reader.Release();
			Logger::Log("SETTING SHOTS %s: done, every picture written", Run);
			Notify(kLinkPictures, Run);
		}
		if (Pending && !Running) {
			Pending = false;
			SYSTEMTIME t; GetLocalTime(&t);
			sprintf_s(Run, "%02u-%02u-%02u", (unsigned)t.wHour, (unsigned)t.wMinute, (unsigned)t.wSecond);
			std::string error;
			if (!Player || !Player->parentCell) { Notify(kLinkStopped, "no game loaded"); return; }
			if (!Parse(error)) { Logger::Log("SETTING SHOTS %s: %s", Run, error.c_str()); Notify(kLinkStopped, error.c_str()); return; }
			Running = true; Index = 0; Frame = 0; Cell = Player->parentCell;
			Logger::Log("SETTING SHOTS %s: started, %u shots, %d frames each (settings changed in memory only, never saved)", Run, (unsigned)List.size(), Frames);
			Notify(kLinkStarted, Run);
			Apply(List[0]);
			return;
		}
		if (!Running) return;
		if (!Commands.empty()) {
			for (const std::string& line : Commands) {
				Logger::Log("SETTING SHOTS %s: shot '%s': console %s", Run, List[Index].slug.c_str(), line.c_str());
				if (g_ConsoleInterface) g_ConsoleInterface->RunScriptLine(line.c_str(), nullptr);
			}
			Commands.clear();
		}
		if (Moves && Player) Cell = Player->parentCell;
		if (!Player || Player->parentCell != Cell) { Stop("the player changed place"); return; }
		if (TheRenderManager->device->TestCooperativeLevel() != D3D_OK) { Reader.Release(); Stop("the device was lost"); return; }
		Frame++;
	}

	inline void TakeShot(IDirect3DSurface9* surface) {
		const Shot& shot = List[Index];
		Job* job = new (std::nothrow) Job();
		if (job && Reader.Read(TheRenderManager->device, surface, job->bgra, job->width, job->height)) {
			sprintf_s(job->path, ".\\NVR-Unofficial-Optimized-Logs\\NeonSelfTest_%s_ab_%02u_%s.png", Run, (unsigned)Index, shot.slug.c_str());
			InFlight++;
			HANDLE thread = CreateThread(nullptr, 0, Worker, job, 0, nullptr);
			if (thread) CloseHandle(thread);
			else { InFlight--; delete job; Logger::Log("SETTING SHOTS %s: shot '%s' not written: no thread", Run, shot.slug.c_str()); }
		}
		else {
			Logger::Log("SETTING SHOTS %s: shot '%s' NOT taken: %s", Run, shot.slug.c_str(), Reader.error ? Reader.error : "no memory");
			delete job;
		}
		Revert();
		Index++;
		Frame = 0;
		if (Index < List.size()) Apply(List[Index]);
		else { Running = false; AllQueued = true; }
	}

	// Render.cpp, the finished picture before the HUD (where the neon self-test takes its Final shots).
	inline void Capture(IDirect3DSurface9* surface) {
		if (!Running || Frame < Frames || !surface) return;
		if (FinalPicture) { WantFinal = true; return; }	// taken at the present instead, the settings still the shot's
		TakeShot(surface);
	}

	// ReShadeIntegration.cpp, ReShade's reshade_present event: the frame as presented (ReShade's effects and the HUD in it).
	inline void CaptureFinal() {
		if (!Running || !WantFinal) return;
		WantFinal = false;
		IDirect3DSurface9* backBuffer = nullptr;
		if (FAILED(TheRenderManager->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer) { Stop("no back buffer for a final picture"); return; }
		TakeShot(backBuffer);
		backBuffer->Release();
	}
}
