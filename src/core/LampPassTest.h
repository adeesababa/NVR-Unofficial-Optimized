#pragma once

// UNOFFICIAL, DIAGNOSTIC (2026-10-11): how much merging lamp passes could ever save. [Main.Main.Performance]
// LampPassTestSkip skips, in the world scene, the passes a merged light pass would replace (THE PICTURE IS WRONG while
// it is on: objects lose the lamps and highlights of those passes). F10 with it off and on, same view, gives the
// upper bound of the saving. 1: the extra lamp passes (DIFFUSEPT2/3, 2 or 3 more lamps each), 2: the highlight
// passes (2x SPECULAR DIR/PT/PT2/PT3), 3: both. Pass enums as Physically-Based-Reloaded's MergedLights
// (NewVegas/Hooks/Shaders.cpp there): the forms a light-only pass can take, not the tree branch (_Sb) or hair ones.
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

	// Once a frame: the setting, and a log line every 300 frames while the test is on.
	inline void BeginFrame() {
		Mode = TheSettingManager->SettingsMain.Main.LampPassTestSkip;
		if (!Mode) { SkippedLamp = SkippedSpecular = Frames = 0; return; }
		if (++Frames >= 300) {
			Logger::Log("LAMP PASS TEST (mode %d, picture wrong on purpose): skipped per frame %.1f extra lamp passes, %.1f highlight passes",
				Mode, SkippedLamp / (float)Frames, SkippedSpecular / (float)Frames);
			SkippedLamp = SkippedSpecular = Frames = 0;
		}
	}

	// Before a pass is drawn: true = skip it.
	inline bool Skip(const void* pass, bool world) {
		if (!Mode || !world || !pass) return false;
		const UInt16 passEnum = *(const UInt16*)((const UInt8*)pass + 0x04);
		if ((Mode & 1) && IsLampPass(passEnum)) { SkippedLamp++; return true; }
		if ((Mode & 2) && IsSpecularPass(passEnum)) { SkippedSpecular++; return true; }
		return false;
	}
}
