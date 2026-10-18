#pragma once

#include <atomic>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "EffectSelfTestCore.h"
#include "GpuProfiler.h"

namespace SettingShots {

	inline const char* const RequestFile = "Data\\NVSE\\Plugins\\NVRAutoTest.shots.txt";
	enum : UInt32 { kLinkStarted = 0x4E560011, kLinkStopped = 0x4E560012, kLinkPictures = 0x4E560014 };

	struct Change { bool effect = false, console = false; std::string section, key, value; };
	struct Shot { std::string slug; std::vector<Change> changes; };
	struct Undo { bool effect = false; std::string section, key; UInt32 type = 0; bool b = false; int i = 0; float f = 0.0f; std::string s; };
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
	inline std::vector<std::string> Commands;
	inline bool Moves = false;
	inline bool FinalPicture = false;
	inline std::atomic<int> FinalState{ 0 };
	inline Job* FinalJob = nullptr;

	inline bool Requested() { return GetFileAttributesA(RequestFile) != INVALID_FILE_ATTRIBUTES; }

	inline void Notify(UInt32 type, const char* text) {
		if (Messaging) Messaging->Dispatch(Handle, type, (void*)text, (UInt32)strlen(text) + 1, "NVRAutoTest");
	}

	inline void Request(NVSEMessagingInterface* messaging, UInt32 handle) { Messaging = messaging; Handle = handle; Pending = true; }

	inline bool WorldCapture = false;
	inline size_t WorldTaken = (size_t)-1;
	inline bool StageCapture = false;
	inline size_t StageTaken = (size_t)-1;
	inline IDirect3DSurface9* WorldResolve = nullptr;
	inline IDirect3DSurface9* WorldReadback = nullptr;
	inline D3DSURFACE_DESC WorldDesc = {};

	inline bool Parse(std::string& error) {
		List.clear(); Frames = 120; FinalPicture = false; WorldCapture = false; WorldTaken = (size_t)-1; StageCapture = false; StageTaken = (size_t)-1;
		if (FinalState.load() == 3) { delete FinalJob; FinalJob = nullptr; }
		FinalState.store(0);
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
			else if (command == "profile") GpuTimer::Enabled = true;
			else if (command == "final") FinalPicture = true;
			else if (command == "world") WorldCapture = true;
			else if (command == "stage") StageCapture = true;
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
		if (List.size() > 80) { error = "more than 80 shots"; return false; }
		return true;
	}

	inline bool Truthy(const std::string& v) { return v == "1" || v == "true" || v == "on" || v == "yes"; }

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
			u.s = node.LongValue.empty() ? node.Value : node.LongValue;
			Undos.push_back(u);
			switch (node.Type) {
			case SettingManager::Configuration::Boolean: TheSettingManager->SetSetting(c.section.c_str(), c.key.c_str(), Truthy(c.value)); break;
			case SettingManager::Configuration::Integer: TheSettingManager->SetSetting(c.section.c_str(), c.key.c_str(), atoi(c.value.c_str())); break;
			case SettingManager::Configuration::String: TheSettingManager->SetSettingS(c.section.c_str(), c.key.c_str(), c.value.substr(0, 79).c_str()); break;
			default: TheSettingManager->SetSetting(c.section.c_str(), c.key.c_str(), (float)atof(c.value.c_str())); break;
			}
			Logger::Log("SETTING SHOTS %s: shot '%s': %s %s = %s (was %s)", Run, s.slug.c_str(), c.section.c_str(), c.key.c_str(), c.value.c_str(), node.Value);
		}
		TheSettingManager->LoadSettings();
	}

	inline void Revert() {
		for (auto u = Undos.rbegin(); u != Undos.rend(); ++u) {
			if (u->effect) { TheShaderManager->SwitchShaderStatus(u->section.c_str()); continue; }
			switch (u->type) {
			case SettingManager::Configuration::Boolean: TheSettingManager->SetSetting(u->section.c_str(), u->key.c_str(), u->b); break;
			case SettingManager::Configuration::Integer: TheSettingManager->SetSetting(u->section.c_str(), u->key.c_str(), u->i); break;
			case SettingManager::Configuration::String: TheSettingManager->SetSettingS(u->section.c_str(), u->key.c_str(), u->s.substr(0, 79).c_str()); break;
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

	inline void ReleaseWorld() {
		if (WorldResolve) { WorldResolve->Release(); WorldResolve = nullptr; }
		if (WorldReadback) { WorldReadback->Release(); WorldReadback = nullptr; }
		WorldDesc = {};
	}

	inline void SaveTarget(IDirect3DDevice9* device, IDirect3DSurface9* target, const char* suffix);
	inline void CaptureWorld(IDirect3DDevice9* device, IDirect3DSurface9* target) {
		if (!Running || !WorldCapture || !device || !target || Index >= List.size() || WorldTaken == Index || Frame < Frames) return;
		WorldTaken = Index;
		SaveTarget(device, target, "");
	}
	inline void CaptureStage(IDirect3DDevice9* device, IDirect3DSurface9* target) {
		if (!Running || !StageCapture || !device || !target || Index >= List.size() || StageTaken == Index || Frame < Frames) return;
		StageTaken = Index;
		SaveTarget(device, target, "-al");
	}
	inline void SaveTarget(IDirect3DDevice9* device, IDirect3DSurface9* target, const char* suffix) {
		const char* slug = List[Index].slug.c_str();
		target->AddRef();
		D3DSURFACE_DESC d = {};
		target->GetDesc(&d);
		const UInt32 bpp = d.Format == D3DFMT_A16B16G16R16F ? 8 : d.Format == D3DFMT_A32B32G32R32F ? 16 :
			(d.Format == D3DFMT_A8R8G8B8 || d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A2R10G10B10 || d.Format == D3DFMT_A2B10G10R10) ? 4 : 0;
		bool ok = bpp != 0;
		if (ok && (!WorldReadback || WorldDesc.Width != d.Width || WorldDesc.Height != d.Height || WorldDesc.Format != d.Format || WorldDesc.MultiSampleType != d.MultiSampleType)) {
			ReleaseWorld();
			ok = (d.MultiSampleType == D3DMULTISAMPLE_NONE || SUCCEEDED(device->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &WorldResolve, NULL))) &&
				SUCCEEDED(device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &WorldReadback, NULL));
			if (ok) WorldDesc = d;
		}
		IDirect3DSurface9* source = target;
		if (ok && WorldResolve) { ok = SUCCEEDED(device->StretchRect(target, NULL, WorldResolve, NULL, D3DTEXF_NONE)); source = WorldResolve; }
		ok = ok && SUCCEEDED(device->GetRenderTargetData(source, WorldReadback));
		D3DLOCKED_RECT lock = {};
		ok = ok && SUCCEEDED(WorldReadback->LockRect(&lock, NULL, D3DLOCK_READONLY));
		char path[MAX_PATH];
		sprintf_s(path, ".\\NVR-Unofficial-Optimized-Logs\\NeonSelfTest_%s_ab_%02u_%s%s.world", Run, (unsigned)Index, slug, suffix);
		bool saved = false;
		if (ok) {
			FILE* f = nullptr;
			if (!fopen_s(&f, path, "wb") && f) {
				const UInt32 header[5] = { 0x5752564E, d.Width, d.Height, (UInt32)d.Format, bpp };
				saved = fwrite(header, sizeof(header), 1, f) == 1;
				for (UInt32 y = 0; saved && y < d.Height; y++) saved = fwrite((const unsigned char*)lock.pBits + (size_t)y * lock.Pitch, (size_t)d.Width * bpp, 1, f) == 1;
				fclose(f);
			}
			WorldReadback->UnlockRect();
		}
		target->Release();
		Logger::Log("SETTING SHOTS %s: shot '%s': world target %ux%u format %u msaa %u -> %s %s", Run, slug, d.Width, d.Height, (unsigned)d.Format,
			(unsigned)d.MultiSampleType, path, saved ? "saved" : "NOT saved");
	}

	inline void QueueJob(Job* job);
	inline void NextShot();

	inline void BeginFrame() {
		if (AllQueued && InFlight == 0) {
			AllQueued = false;
			Reader.Release();
			ReleaseWorld();
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
		if (FinalState.load() == 3) {
			Job* job = FinalJob;
			FinalJob = nullptr;
			FinalState.store(0);
			QueueJob(job);
			NextShot();
			if (!Running) return;
		}
		if (!Commands.empty()) {
			for (const std::string& line : Commands) {
				Logger::Log("SETTING SHOTS %s: shot '%s': console %s", Run, List[Index].slug.c_str(), line.c_str());
				if (g_ConsoleInterface) g_ConsoleInterface->RunScriptLine(line.c_str(), nullptr);
			}
			Commands.clear();
		}
		if (Moves && Player) Cell = Player->parentCell;
		if (!Player || Player->parentCell != Cell) { Stop("the player changed place"); return; }
		if (TheRenderManager->device->TestCooperativeLevel() != D3D_OK) { Reader.Release(); ReleaseWorld(); Stop("the device was lost"); return; }
		Frame++;
	}

	inline void QueueJob(Job* job) {
		const Shot& shot = List[Index];
		sprintf_s(job->path, ".\\NVR-Unofficial-Optimized-Logs\\NeonSelfTest_%s_ab_%02u_%s.png", Run, (unsigned)Index, shot.slug.c_str());
		InFlight++;
		HANDLE thread = CreateThread(nullptr, 0, Worker, job, 0, nullptr);
		if (thread) CloseHandle(thread);
		else { InFlight--; delete job; Logger::Log("SETTING SHOTS %s: shot '%s' not written: no thread", Run, shot.slug.c_str()); }
	}

	inline void NextShot() {
		Revert();
		Index++;
		Frame = 0;
		if (Index < List.size()) Apply(List[Index]);
		else { Running = false; AllQueued = true; }
	}

	inline void TakeShot(IDirect3DSurface9* surface) {
		Job* job = new (std::nothrow) Job();
		if (job && Reader.Read(TheRenderManager->device, surface, job->bgra, job->width, job->height)) QueueJob(job);
		else {
			Logger::Log("SETTING SHOTS %s: shot '%s' NOT taken: %s", Run, List[Index].slug.c_str(), Reader.error ? Reader.error : "no memory");
			delete job;
		}
		NextShot();
	}

	inline void Capture(IDirect3DSurface9* surface) {
		if (!Running || Frame < Frames || !surface) return;
		if (FinalPicture) { int idle = 0; FinalState.compare_exchange_strong(idle, 1); return; }
		TakeShot(surface);
	}

	inline void CaptureFinal() {
		int wanted = 1;
		if (!Running || !FinalState.compare_exchange_strong(wanted, 0)) return;
		IDirect3DSurface9* backBuffer = nullptr;
		if (FAILED(TheRenderManager->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || !backBuffer) { Stop("no back buffer for a final picture"); return; }
		TakeShot(backBuffer);
		backBuffer->Release();
	}

	inline bool BeginFinalCapture() { int wanted = 1; return Running && FinalState.compare_exchange_strong(wanted, 2); }
	inline void EndFinalCapture(Job* job) {
		if (!job) { FinalState.store(1); return; }
		FinalJob = job;
		FinalState.store(3);
	}
}
