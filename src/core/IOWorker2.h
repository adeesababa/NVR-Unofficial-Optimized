#pragma once

#include <atomic>
#include "../base/SafeWrite.h"

namespace IOWorker2 {
	static const DWORD kCountPush = 0x00C3DA7A;
	static const DWORD kSelectSlot = 0x010C165C;
	static const DWORD kSelectStub = 0x008D0560;
	static const DWORD kFilterSite = 0x00C41201;
	static const DWORD kIOManagerPtr = 0x01202D98, kModelLoaderPtr = 0x011C3B3C;
	static const DWORD kVtQueuedModel = 0x01016890, kVtQueuedTexture = 0x01016788;
	static const DWORD kMapOffsets[] = { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x20 };

	inline DWORD FilterBack = 0x00C41208, FilterSkip = 0x00C41274;
	inline bool Armed = false;
	inline volatile LONG PriorityDone = 0, MapsBad = 0, MapsLogged = 0;
	inline std::atomic<long> Woken{ 0 }, Approved{ 0 }, Refused{ 0 }, SlotRefused{ 0 };
	inline DWORD LastLogTick = 0;

	inline DWORD Peek(DWORD a) { __try { return a > 0x10000 ? *(const DWORD*)(uintptr_t)a : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }

	inline volatile DWORD W1 = 0;
	inline DWORD Worker1() {
		if (W1) return W1;
		const DWORD io = Peek(kIOManagerPtr);
		if (!io || Peek(io + 0x4C) < 2) return 0;
		const DWORD w = Peek(Peek(io + 0x50) + 4);
		if (w && Peek(w + 0x30) == io) W1 = w;
		return W1;
	}

	inline bool SlotsOk() {
		if (MapsBad) return false;
		const DWORD ml = Peek(kModelLoaderPtr);
		if (!ml) return false;
		for (DWORD off : kMapOffsets) {
			const DWORD map = Peek(ml + off), tsim = map ? Peek(map + 0x14) : 0;
			const DWORD cap = tsim ? Peek(tsim) : 0, tls = tsim ? Peek(tsim + 4) : 0, used = tsim ? Peek(tsim + 0x0C) : 0;
			if (!tsim || cap < 1 || cap > 64 || tls >= 1088 || used > cap + 64) {
				if (!InterlockedExchange(&MapsBad, 1)) Logger::Log("UNOFFICIAL IOWorker2: ModelLoader map +%02X not as expected (map %08X, slots %08X: capacity %u, used %u); worker 1 takes no tasks", off, map, tsim, cap, used);
				return false;
			}
			if (TlsGetValue(tls)) continue;
			if (used + 2 > cap) return false;
		}
		return true;
	}

	inline bool __cdecl MayRun(DWORD self, DWORD task) {
		if (!self || !task || self != Worker1()) return true;
		if (*(const volatile LONG*)(task + 0x0C) != 1) return true;
		const DWORD vt = *(const DWORD*)task;
		if (vt != kVtQueuedModel && vt != kVtQueuedTexture) { Refused++; return false; }
		if (!SlotsOk()) { SlotRefused++; return false; }
		Approved++;
		return true;
	}

	__declspec(naked) inline void FilterStub() {
		__asm {
			mov edx, [ebp - 0x34]
			push edx
			push dword ptr [ebp - 0x88]
			call MayRun
			add esp, 8
			test al, al
			jz skip
			mov edx, [ebp - 0x34]
			cmp dword ptr [edx + 0x0C], 1
			jmp dword ptr [FilterBack]
		skip:
			jmp dword ptr [FilterSkip]
		}
	}

	inline UInt32 __fastcall Select(void* io, void* edx, void* task) {
		const DWORD w1 = Worker1();
		if (w1 && task) {
			const DWORD vt = *(const DWORD*)task;
			if (vt == kVtQueuedModel || vt == kVtQueuedTexture) {
				if (!PriorityDone && !InterlockedExchange(&PriorityDone, 1)) SetThreadPriority(*(HANDLE*)(w1 + 0x04), THREAD_PRIORITY_BELOW_NORMAL);
				volatile LONG* pending = (volatile LONG*)(w1 + 0x0C);
				if (*pending == 0) {
					ReleaseSemaphore(*(HANDLE*)(w1 + 0x10), 1, nullptr);
					InterlockedIncrement((LONG*)pending);
					Woken++;
				}
			}
		}
		return 0;
	}

	inline void Install() {
		SettingManager::Configuration::ConfigNode node;
		const bool want = TheSettingManager->Config.FillNode(&node, "Main.Main.Performance", "IOWorker2") && node.IntValue > 0;
		if (!want) return;
		static const unsigned char kPush[] = { 0x6A, 0x01, 0x6A, 0x0A };
		static const unsigned char kSite[] = { 0x8B, 0x55, 0xCC, 0x83, 0x7A, 0x0C, 0x01, 0x75, 0x6A };
		const DWORD io = Peek(kIOManagerPtr), slot = Peek(kSelectSlot);
		const bool push = !memcmp((const void*)kCountPush, kPush, sizeof(kPush)), site = !memcmp((const void*)kFilterSite, kSite, sizeof(kSite));
		if (io || !push || slot != kSelectStub || !site) {
			Logger::Log("UNOFFICIAL IOWorker2: NOT armed (IOManager already created %s, worker-count bytes %s, vtable slot %08X %s, sweep check bytes %s): the game keeps its one loading thread",
				io ? "YES" : "no", push ? "as expected" : "DIFFERENT", slot, slot == kSelectStub ? "as expected" : "DIFFERENT", site ? "as expected" : "DIFFERENT");
			return;
		}
		SafeWrite32(kSelectSlot, (SIZE_T)&Select);
		unsigned char jump[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
		*(DWORD*)(jump + 1) = (DWORD)(uintptr_t)&FilterStub - (kFilterSite + 5);
		SafeWriteBuf(kFilterSite, jump, sizeof(jump));
		SafeWrite8(kCountPush + 1, 0x02);
		FlushInstructionCache(GetCurrentProcess(), (void*)kFilterSite, sizeof(jump));
		Armed = true;
		Logger::Log("UNOFFICIAL IOWorker2: armed (EXPERIMENTAL): the game gets a second loading thread for model and texture tasks");
	}

	inline void Tick() {
		if (!Armed) return;
		const DWORD now = GetTickCount();
		static bool told = false;
		if (!told) {
			const DWORD w1 = Worker1();
			if (!w1) return;
			told = true; LastLogTick = now;
			Logger::Log("UNOFFICIAL IOWorker2: the IOManager has %u workers; worker 1 = thread %u", Peek(Peek(kIOManagerPtr) + 0x4C), Peek(w1 + 0x08));
			return;
		}
		if (now - LastLogTick < 60000) return;
		LastLogTick = now;
		Logger::Log("UNOFFICIAL IOWorker2: last minute: worker 1 woken %ld times, took %ld model / texture tasks, left %ld other tasks to worker 0, %ld refused for map slots",
			Woken.exchange(0), Approved.exchange(0), Refused.exchange(0), SlotRefused.exchange(0));
	}
}
