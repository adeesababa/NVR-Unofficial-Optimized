#pragma once

namespace LampPassTest {
	inline int Mode = 0;
	inline UInt32 SkippedLamp = 0, SkippedSpecular = 0, Frames = 0;

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

	inline void BeginFrame() {
		Mode = TheSettingManager->SettingsMain.Main.LampPassTestSkip;
		if (!Mode) { SkippedLamp = SkippedSpecular = Frames = 0; return; }
		if (++Frames >= 300) {
			Logger::Log("LAMP PASS TEST (mode %d, picture wrong on purpose): skipped per frame %.1f extra lamp passes, %.1f highlight passes",
				Mode, SkippedLamp / (float)Frames, SkippedSpecular / (float)Frames);
			SkippedLamp = SkippedSpecular = Frames = 0;
		}
	}

	inline bool Skip(const void* pass, bool world) {
		if (!Mode || !world || !pass) return false;
		const UInt16 passEnum = *(const UInt16*)((const UInt8*)pass + 0x04);
		if ((Mode & 1) && IsLampPass(passEnum)) { SkippedLamp++; return true; }
		if ((Mode & 2) && IsSpecularPass(passEnum)) { SkippedSpecular++; return true; }
		return false;
	}
}
