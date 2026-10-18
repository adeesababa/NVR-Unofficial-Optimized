#pragma once
#include <string>
#include "TestTools.h"

namespace LampsInOnePass {

	constexpr UInt32 kCompareSite = 0x00BE00E0;
	constexpr UInt32 kOptCapByte = 0x00BDEA3F;
	constexpr UInt32 kEnabledPasses = 0x011F91D8;
	constexpr UInt32 kRebuildBit = 0x10000000;

	inline bool Applied = false;
	inline bool RefusedLogged = false;
	inline unsigned Switches = 0;

	inline void BeginFrame() {
		const bool want = TheSettingManager->GetSettingI("Main.Main.Misc", "LampsInOnePass") != 0;
		if (want == Applied) return;
		const UInt8* code = (const UInt8*)kCompareSite;
		if (code[0] != 0x83 || code[1] != 0xC0 || (code[2] != 0x02 && code[2] != 0x05)) {
			if (!RefusedLogged) Logger::Log("UNOFFICIAL LampsInOnePass: the game's code at %08X is not as expected (another plugin?); not changed", kCompareSite);
			RefusedLogged = true;
			return;
		}
		if (want && *(const UInt8*)kOptCapByte != 0x05) {
			if (!RefusedLogged) Logger::Log("UNOFFICIAL LampsInOnePass: needs MoreLampsPerObject (the game's cap of 3 would drop lamps); not changed");
			RefusedLogged = true;
			return;
		}
		SafeWrite8(kCompareSite + 2, want ? 0x05 : 0x02);
		*(UInt32*)kEnabledPasses ^= kRebuildBit;
		Applied = want;
		Switches++;
		Logger::Log("UNOFFICIAL LampsInOnePass: %s (shiny objects with up to %s lamps in one pass; every object's passes rebuilt)",
			want ? "on" : "off", want ? "5-6" : "2-3");
	}

#if NVR_TEST_TOOLS
	namespace Census {
		struct PassView { NiGeometry* geometry; UInt16 passEnum; UInt8 unk06, unk07, unk08, numLights, maxLights, unk0B; void** lights; };

		inline bool IsLampPass(UInt16 e) { return (e >= 0x132 && e <= 0x13C && e != 0x137) || (e >= 0x13E && e <= 0x148 && e != 0x143); }
		inline bool IsHighlightPass(UInt16 e) {
			switch (e) {
			case 0x15A: case 0x15C: case 0x15D: case 0x160: case 0x162: case 0x163:
			case 0x166: case 0x168: case 0x169: case 0x16C: case 0x16E: case 0x16F:
			case 0x172: case 0x174: case 0x175:
				return true;
			}
			return false;
		}
		inline bool IsOptPass(UInt16 e) { return e >= 0xC3 && e <= 0xD6; }
		inline bool FirstLightIsLamp(UInt16 e) { return e == 0xCA || e == 0xD1; }

		inline unsigned ShaderSlots(const char* ps) {
			if (!ps || strncmp(ps, "SLS", 3)) return 0;
			const int n = atoi(ps + 3);
			if (n == 2029 || n == 2030) return 6;
			if (n >= 2031 && n <= 2033) return 4;
			if (n >= 2034 && n <= 2036) return *(const UInt8*)kOptCapByte == 0x05 ? 6 : 3;
			return 0;
		}

		inline unsigned CountLamps(const void* property) {
			unsigned n = 0;
			for (const UInt32* node = *(const UInt32* const*)((const UInt8*)property + 0x60); node; node = (const UInt32*)node[0]) {
				const UInt8* light = (const UInt8*)node[2];
				if (!light || *(const UInt16*)(light + 0x110) == 0xFF) continue;
				const UInt8* niLight = *(const UInt8* const*)(light + 0xF8);
				if (!niLight || (niLight[0x30] & 1) || light[0xEC]) continue;
				n++;
			}
			return n;
		}

		constexpr unsigned kWindow = 112;
		inline unsigned Frames = 0;
		inline bool WindowState = false;
		inline unsigned long long WorldDraws = 0, LampPasses = 0, HighlightPasses = 0, TexturePasses = 0, FogPasses = 0;
		inline unsigned long long OptDraws[0x14] = {}, OptLights[0x14][16] = {};
		inline unsigned long long Dropped = 0, DroppedDraws = 0, DroppedSkipCompare = 0, OverSlots = 0, OverSlotDraws = 0;
		inline std::string ShaderOf[0x14];
		inline unsigned long long Multi = 0, ShinyByLamps[10] = {}, ShinyFaceGen = 0, Plain = 0, Flagged = 0, NotPP = 0, Lighting30 = 0;
		inline unsigned long long ShinySkinned = 0;
		constexpr unsigned kSeen = 8192;
		inline const void* SeenKey[kSeen] = {};
		inline unsigned SeenFrame[kSeen] = {};
		inline unsigned FrameId = 1;
		inline UInt32 BoundPass = 0xFFFFFFFF;
		inline const char* BoundPS = nullptr;
		inline const char* BoundVS = nullptr;

		inline bool SkipsCompare(const NiGeometry* geometry, UInt32 flags0) {
			const NiAlphaProperty* alpha = geometry ? geometry->propertyState.m_spAlphaProperty : nullptr;
			return (flags0 & 0x20) || (alpha && (alpha->flags & 1));
		}

		inline void Reset() {
			Frames = 0; WorldDraws = LampPasses = HighlightPasses = TexturePasses = FogPasses = 0;
			memset(OptDraws, 0, sizeof(OptDraws)); memset(OptLights, 0, sizeof(OptLights));
			Dropped = DroppedDraws = DroppedSkipCompare = OverSlots = OverSlotDraws = 0;
			Multi = ShinyFaceGen = Plain = Flagged = NotPP = ShinySkinned = Lighting30 = 0; memset(ShinyByLamps, 0, sizeof(ShinyByLamps));
		}

		inline void Report() {
			if (!Frames) return;
			const double f = Frames;
			char line[1024]; int n = 0;
			n += sprintf_s(line + n, sizeof(line) - n, "LAMPS IN ONE PASS census (%s), %u frames, per frame: world draws %.0f | OPT passes",
				WindowState ? "on" : "off", Frames, WorldDraws / f);
			for (int i = 0; i < 0x14; i++) if (OptDraws[i]) n += sprintf_s(line + n, sizeof(line) - n, " %02X:%.1f", 0xC3 + i, OptDraws[i] / f);
			n += sprintf_s(line + n, sizeof(line) - n, " | extra lamp passes %.1f, highlight passes %.1f, texture passes %.1f, fog passes %.1f",
				LampPasses / f, HighlightPasses / f, TexturePasses / f, FogPasses / f);
			Logger::Log("%s", line);
			n = 0;
			n += sprintf_s(line + n, sizeof(line) - n, "LAMPS IN ONE PASS census (%s): multi-pass objects %.1f a frame: shiny by lamps", WindowState ? "on" : "off", Multi / f);
			for (int k = 0; k < 10; k++) if (ShinyByLamps[k]) n += sprintf_s(line + n, sizeof(line) - n, " %d%s:%.1f", k, k == 9 ? "+" : "", ShinyByLamps[k] / f);
			n += sprintf_s(line + n, sizeof(line) - n, " (of them skinned %.1f) | shiny FaceGen %.1f | not shiny %.1f | parallax/hair/glow/MSN %.1f | Lighting30 property %.1f | other property %.1f",
				ShinySkinned / f, ShinyFaceGen / f, Plain / f, Flagged / f, Lighting30 / f, NotPP / f);
			Logger::Log("%s", line);
			Logger::Log("LAMPS IN ONE PASS census (%s): OPT lights dropped by the game's cap %.2f a frame in %.2f draws (alpha/Single_Pass objects %.2f, all others %.2f); "
				"lights past the shader's slots %.2f a frame in %.2f draws; bUseOptPasses %u, switches so far %u",
				WindowState ? "on" : "off", Dropped / f, DroppedDraws / f, DroppedSkipCompare / f, (Dropped - DroppedSkipCompare) / f, OverSlots / f, OverSlotDraws / f,
				(unsigned)*(const UInt8*)0x011AD80D, Switches);
			for (int i = 0; i < 0x14; i++) {
				if (!OptDraws[i]) continue;
				n = 0;
				n += sprintf_s(line + n, sizeof(line) - n, "LAMPS IN ONE PASS   OPT %02X (%s) drawn with %s: %.1f a frame, lights per draw", 0xC3 + i,
					Pointers::Functions::GetPassDescription(0xC3 + i), ShaderOf[i].empty() ? "?" : ShaderOf[i].c_str(), OptDraws[i] / f);
				for (int k = 0; k < 16; k++) if (OptLights[i][k]) n += sprintf_s(line + n, sizeof(line) - n, " %d:%.1f", k, OptLights[i][k] / f);
				Logger::Log("%s", line);
			}
			Reset();
		}

		inline void NewFrame() {
			FrameId++;
			if (!GpuTimer::Enabled) { if (Frames) Reset(); return; }
			if (Frames && WindowState != Applied) Report();
			WindowState = Applied;
			if (++Frames >= kWindow) Report();
		}

		inline void Note(const void* passPtr, bool world) {
			if (!world || !passPtr || !GpuTimer::Enabled) return;
			const PassView* pass = (const PassView*)passPtr;
			const UInt16 e = pass->passEnum;
			WorldDraws++;
			if (e >= 0x14A && e <= 0x150) { TexturePasses++; return; }
			if (e >= 0x252 && e <= 0x255) { FogPasses++; return; }
			NiGeometry* geometry = pass->geometry;
			const BSShaderProperty* property = geometry ? (const BSShaderProperty*)geometry->propertyState.m_spShadeProperty : nullptr;
			if (IsOptPass(e)) {
				const unsigned i = e - 0xC3;
				OptDraws[i]++;
				OptLights[i][min((unsigned)pass->numLights, 15u)]++;
				const bool skips = property && SkipsCompare(geometry, property->ulFlags[0]);
				if (pass->maxLights > pass->numLights) {
					Dropped += pass->maxLights - pass->numLights; DroppedDraws++;
					if (skips) DroppedSkipCompare += pass->maxLights - pass->numLights;
				}
				if (BoundPass == e) {
					if (ShaderOf[i].empty()) ShaderOf[i] = std::string(BoundPS ? BoundPS : "?") + "/" + (BoundVS ? BoundVS : "?");
					const unsigned slots = ShaderSlots(BoundPS);
					const unsigned used = FirstLightIsLamp(e) ? (pass->numLights ? pass->numLights - 1u : 0u) : pass->numLights;
					if (slots && used > slots) { OverSlots += used - slots; OverSlotDraws++; }
				}
				return;
			}
			const bool lamp = IsLampPass(e), highlight = !lamp && IsHighlightPass(e);
			if (lamp) LampPasses++;
			if (highlight) HighlightPasses++;
			if (!lamp && !highlight) return;
			unsigned h = (unsigned)(((uintptr_t)geometry >> 4) * 2654435761u) & (kSeen - 1);
			for (unsigned probe = 0; probe < 32; probe++, h = (h + 1) & (kSeen - 1)) {
				if (SeenFrame[h] == FrameId && SeenKey[h] == geometry) return;
				if (SeenFrame[h] != FrameId) { SeenFrame[h] = FrameId; SeenKey[h] = geometry; break; }
			}
			Multi++;
			const UInt32 vtable = property ? *(const UInt32*)property : 0;
			if (vtable == 0x10B9910) { Lighting30++; return; }
			if (vtable != 0x10AE0D0) { NotPP++; return; }
			const UInt32 flags0 = property->ulFlags[0];
			const BSShaderPPLightingProperty* lighting = (const BSShaderPPLightingProperty*)property;
			const bool glow = !(flags0 & 0x400) && lighting->ppTextures[2] && lighting->ppTextures[2][0];
			if ((flags0 & (0x800 | 0x1000 | 0x40000)) || glow) { Flagged++; return; }
			if (!(flags0 & 1)) { Plain++; return; }
			if (flags0 & 0x400) { ShinyFaceGen++; return; }
			ShinyByLamps[min(CountLamps(property), 9u)]++;
			if (flags0 & 2) ShinySkinned++;
		}

		inline void OnBind(UInt32 passIndex, const char* ps, const char* vs) { BoundPass = passIndex; BoundPS = ps; BoundVS = vs; }
	}
#endif
}
