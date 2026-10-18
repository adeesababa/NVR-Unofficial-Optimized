#pragma once
#include <string>
#include <vector>

namespace LampPassTest {
	inline int Mode = 0;
	inline UInt32 SkippedLamp = 0, SkippedSpecular = 0, Frames = 0;
	inline std::vector<UInt16> Order;
	inline int OrderFramesLeft = 0;
	inline bool OrderWanted = false, OrderDone = false;

	inline bool IsLampPass(UInt16 e) {
		return (e >= 0x132 && e <= 0x13C && e != 0x137) || (e >= 0x13E && e <= 0x148 && e != 0x143);
	}
	inline bool IsSpecularPass(UInt16 e) {
		switch (e) {
		case 0x15A: case 0x15C: case 0x15D: case 0x160: case 0x162: case 0x163:
		case 0x166: case 0x168: case 0x169: case 0x16C: case 0x16E: case 0x16F:
		case 0x172: case 0x174: case 0x175:
			return true;
		}
		return false;
	}

	inline void LogOrder() {
		if (Order.empty()) return;
		std::string line; unsigned segments = 0, specRuns = 0, specPasses = 0; bool inSpec = false;
		for (size_t i = 0; i < Order.size();) {
			size_t j = i; while (j < Order.size() && Order[j] == Order[i]) j++;
			char seg[32]; sprintf_s(seg, "%s%03X x%u", line.empty() ? "" : " | ", (unsigned)Order[i], (unsigned)(j - i)); line += seg; segments++;
			const bool spec = IsSpecularPass(Order[i]);
			if (spec) { specPasses += (unsigned)(j - i); if (!inSpec) specRuns++; }
			inSpec = spec;
			if (segments % 24 == 0) { Logger::Log("PASS ORDER ... %s", line.c_str()); line.clear(); }
			i = j;
		}
		if (!line.empty()) Logger::Log("PASS ORDER ... %s", line.c_str());
		Logger::Log("PASS ORDER frame: %u passes, %u runs; highlight passes %u in %u separate runs", (unsigned)Order.size(), segments, specPasses, specRuns);
		Order.clear();
	}

	inline void BeginFrame() {
		if (OrderWanted) { LogOrder(); OrderWanted = --OrderFramesLeft > 0; }
		else {
			const int want = TheSettingManager->GetSettingI("Main.Main.Performance", "LampPassOrderLog");
			if (want > 0 && !OrderDone) { OrderFramesLeft = want; OrderWanted = true; OrderDone = true; Order.reserve(4096); }
			else if (want <= 0) OrderDone = false;
		}
		Mode = TheSettingManager->SettingsMain.Main.LampPassTestSkip;
		if (!Mode) { SkippedLamp = SkippedSpecular = Frames = 0; return; }
		if (++Frames >= 300) {
			Logger::Log("LAMP PASS TEST (mode %d, picture wrong on purpose): skipped per frame %.1f extra lamp passes, %.1f highlight passes",
				Mode, SkippedLamp / (float)Frames, SkippedSpecular / (float)Frames);
			SkippedLamp = SkippedSpecular = Frames = 0;
		}
	}

	inline UInt16 StatesLogged[64]; inline int StatesCount = 0;
	inline void AfterDraw(const void* pass, bool world, IDirect3DDevice9* device) {
		if (!OrderWanted || !world || !pass || !device) return;
		const UInt16 e = *(const UInt16*)((const UInt8*)pass + 0x04);
		for (int i = 0; i < StatesCount; i++) if (StatesLogged[i] == e) return;
		if (StatesCount >= 64) return;
		StatesLogged[StatesCount++] = e;
		DWORD ab = 0, sb = 0, db = 0, op = 0, sep = 0, cw = 0, zf = 0, zw = 0, at = 0;
		device->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab); device->GetRenderState(D3DRS_SRCBLEND, &sb); device->GetRenderState(D3DRS_DESTBLEND, &db);
		device->GetRenderState(D3DRS_BLENDOP, &op); device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &sep); device->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
		device->GetRenderState(D3DRS_ZFUNC, &zf); device->GetRenderState(D3DRS_ZWRITEENABLE, &zw); device->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
		Logger::Log("PASS STATES %03X: blend %u src %u dest %u op %u sepalpha %u colorwrite %u zfunc %u zwrite %u alphatest %u",
			(unsigned)e, ab, sb, db, op, sep, cw, zf, zw, at);
	}

	inline bool Skip(const void* pass, bool world) {
		if (OrderWanted && world && pass && Order.size() < 20000) Order.push_back(*(const UInt16*)((const UInt8*)pass + 0x04));
		if (!Mode || !world || !pass) return false;
		const UInt16 passEnum = *(const UInt16*)((const UInt8*)pass + 0x04);
		if ((Mode & 1) && IsLampPass(passEnum)) { SkippedLamp++; return true; }
		if ((Mode & 2) && IsSpecularPass(passEnum)) { SkippedSpecular++; return true; }
		if ((Mode & 4) && passEnum >= 0x248 && passEnum <= 0x255) return true;
		return false;
	}
}
