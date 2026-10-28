#pragma once
#include <string>
#include "TestTools.h"
#include "ManyLamps.h"

namespace LampsInOnePass {

	constexpr UInt32 kCompareSite = 0x00BE00E0;
	constexpr UInt32 kOptCapByte = 0x00BDEA3F;
	constexpr UInt32 kEnabledPasses = 0x011F91D8;
	constexpr UInt32 kRebuildBit = 0x10000000;
	constexpr UInt32 kGlowSite = 0x00BE0134;
	constexpr UInt32 kEmitSite = 0x00B7BAFA;

	inline bool Applied = false;
	inline UInt8 AppliedImm = 0x02;
	inline bool RefusedLogged = false;
	inline unsigned Switches = 0;
	inline bool GlowApplied = false, GlowRefusedLogged = false;
	inline UInt32 AmbientEmitEnums[0x300 / 32] = {};

	inline void ApplyGlow(bool manyLampsOn) {
		const bool want = manyLampsOn && TheSettingManager->GetSettingI("Main.Main.Misc", "ManyLampsGlow") != 0;
		if (want == GlowApplied) return;
		const UInt8* code = (const UInt8*)kGlowSite;
		static const UInt8 kCmp[] = { 0x80, 0x7C, 0x24, 0x3C, 0x00 };
		const bool known = !memcmp(code - 5, kCmp, sizeof(kCmp)) && ((code[0] == 0x75 && code[1] == 0x45) || (code[0] == 0x90 && code[1] == 0x90));
		if (!known) {
			if (!GlowRefusedLogged) Logger::Log("UNOFFICIAL ManyLampsGlow: the game's code at %08X is not as expected (another plugin?); not changed", kGlowSite);
			GlowRefusedLogged = true;
			return;
		}
		static const UInt8 kEmitOriginal[] = { 0x0F, 0xB7, 0x46, 0x04, 0xBA, 0xC3, 0x00, 0x00, 0x00, 0x66, 0x3B, 0xC2, 0x72, 0x0A,
			0xBA, 0xD6, 0x00, 0x00, 0x00, 0x66, 0x3B, 0xC2, 0x76, 0x0C };
		UInt8 emitPatched[sizeof(kEmitOriginal)];
		memset(emitPatched, 0x90, sizeof(emitPatched));
		static const UInt8 kEmitHead[] = { 0x0F, 0xB7, 0x46, 0x04, 0x0F, 0xA3, 0x05 };
		memcpy(emitPatched, kEmitHead, sizeof(kEmitHead));
		*(UInt32*)(emitPatched + 7) = (UInt32)AmbientEmitEnums;
		emitPatched[11] = 0x72; emitPatched[12] = (UInt8)(0x00B7BB1E - (kEmitSite + 13));
		const UInt8* emit = (const UInt8*)kEmitSite;
		if (memcmp(emit, kEmitOriginal, sizeof(kEmitOriginal)) && memcmp(emit, emitPatched, sizeof(emitPatched))) {
			if (!GlowRefusedLogged) Logger::Log("UNOFFICIAL ManyLampsGlow: the game's code at %08X is not as expected (another plugin?); not changed", kEmitSite);
			GlowRefusedLogged = true;
			return;
		}
		if (!AmbientEmitEnums[0xC3 / 32] && !AmbientEmitEnums[0xD6 / 32])
			for (UInt32 e = 0xC3; e <= 0xD6; e++)
				if (e != 0xC4 && e != 0xC7 && e != 0xCB && e != 0xCE && e != 0xD2 && e != 0xD5) AmbientEmitEnums[e / 32] |= 1u << (e % 32);
		static const UInt8 kNops[] = { 0x90, 0x90 }, kJnz[] = { 0x75, 0x45 };
		SafeWriteBuf(kEmitSite, want ? emitPatched : kEmitOriginal, sizeof(kEmitOriginal));
		SafeWriteBuf(kGlowSite, want ? kNops : kJnz, 2);
		*(UInt32*)kEnabledPasses ^= kRebuildBit;
		GlowApplied = want;
		Logger::Log("UNOFFICIAL ManyLampsGlow: %s (glow-mapped objects %s; every object's passes rebuilt)", want ? "on" : "off",
			want ? "take the one pass too, their emittance masked by the glow map" : "drawn in several passes, as the game does");
	}

	inline bool IsGlowOpt(UInt16 e) { return e == 0xC4 || e == 0xC7 || e == 0xCB || e == 0xCE || e == 0xD2 || e == 0xD5; }
	inline const void* GlowLastPass = nullptr;
	inline IDirect3DBaseTexture9* GlowLastTexture = nullptr;
	inline unsigned long long GlowDraws = 0, GlowRebound = 0, GlowCacheStale = 0, GlowLost = 0;
	inline unsigned GlowFrames = 0;

	inline void GlowForPass(const PointShadowForward::RenderPassView* pass) {
		GlowLastTexture = nullptr;
		if (!GlowApplied || !pass || !pass->geometry || !IsGlowOpt(pass->passEnum)) return;
		const BSShaderProperty* property = (const BSShaderProperty*)pass->geometry->propertyState.m_spShadeProperty;
		if (!property || *(const UInt32*)property != 0x10AE0D0) return;
		const BSShaderPPLightingProperty* lighting = (const BSShaderPPLightingProperty*)property;
		const NiSourceTexture* glow = lighting->ppTextures[2] ? lighting->ppTextures[2][0] : nullptr;
		IDirect3DBaseTexture9* texture = glow && glow->rendererData ? glow->rendererData->dTexture : nullptr;
		if (!texture) return;
		IDirect3DDevice9* device = TheRenderManager->device;
		if (GpuTimer::Enabled) {
			IDirect3DBaseTexture9* now = nullptr;
			if (SUCCEEDED(device->GetTexture(4, &now)) && now) now->Release();
			if (now != texture) {
				GlowRebound++;
				if (TheRenderManager->renderState->unk10A0[4] == (UInt32)texture) GlowCacheStale++;
			}
			GlowDraws++;
			GlowLastPass = pass; GlowLastTexture = texture;
		}
		device->SetTexture(4, texture);
		TheRenderManager->renderState->NoteDeviceTexture(4, texture);
	}

	inline void GlowAfterDraw(const void* pass) {
		if (!GlowLastTexture || pass != GlowLastPass) { GlowLastTexture = nullptr; return; }
		IDirect3DBaseTexture9* now = nullptr;
		if (SUCCEEDED(TheRenderManager->device->GetTexture(4, &now)) && now) now->Release();
		if (now != GlowLastTexture) GlowLost++;
		GlowLastTexture = nullptr;
	}

	inline void BeginFrame() {
		ApplyGlow(Applied && AppliedImm == (UInt8)ManyLamps::CompareImm);
		if (!GpuTimer::Enabled) { GlowDraws = GlowRebound = GlowCacheStale = GlowLost = 0; GlowFrames = 0; }
		else if (GlowDraws && ++GlowFrames >= 600) {
			Logger::Log("UNOFFICIAL ManyLampsGlow (F10): per frame %.1f glow one-pass draws, s4 not their glow map before NVR's bind %.1f (the game's cache showed it already: %.1f), "
				"s4 not the glow map after the draw %.2f", GlowDraws / (double)GlowFrames, GlowRebound / (double)GlowFrames, GlowCacheStale / (double)GlowFrames,
				GlowLost / (double)GlowFrames);
			GlowDraws = GlowRebound = GlowCacheStale = GlowLost = 0; GlowFrames = 0;
		}
		const UInt8* code = (const UInt8*)kCompareSite;
		const bool nvrObjectShaders = TheSettingManager->SettingsMain.Main.RenderEffects && TheShaderManager->Shaders.PBR && TheShaderManager->Shaders.PBR->Enabled;
		const bool manyLamps = ManyLamps::Active() && nvrObjectShaders && TheSettingManager->GetSettingI("Main.Main.Misc", "ManyLampsOnePass") != 0;
		const UInt8 onImm = manyLamps ? (UInt8)ManyLamps::CompareImm : (UInt8)0x05;
		const bool want = manyLamps || TheSettingManager->GetSettingI("Main.Main.Misc", "LampsInOnePass") != 0;
		if ((want ? onImm : (UInt8)0x02) == AppliedImm) return;
		if (code[0] != 0x83 || code[1] != 0xC0 || (code[2] != 0x02 && code[2] != 0x05 && code[2] != (UInt8)ManyLamps::CompareImm)) {
			if (!RefusedLogged) Logger::Log("UNOFFICIAL LampsInOnePass: the game's code at %08X is not as expected (another plugin?); not changed", kCompareSite);
			RefusedLogged = true;
			return;
		}
		if (want && *(const UInt8*)kOptCapByte != 0x05) {
			if (!RefusedLogged) Logger::Log("UNOFFICIAL LampsInOnePass: needs MoreLampsPerObject (the game's cap of 3 would drop lamps); not changed");
			RefusedLogged = true;
			return;
		}
		SafeWrite8(kCompareSite + 2, want ? onImm : 0x02);
		*(UInt32*)kEnabledPasses ^= kRebuildBit;
		Applied = want;
		AppliedImm = want ? onImm : (UInt8)0x02;
		Switches++;
		Logger::Log("UNOFFICIAL LampsInOnePass: %s (shiny objects with up to %s lamps in one pass; every object's passes rebuilt)",
			want ? "on" : "off", want ? (onImm == 0x05 ? "5-6" : "23-24") : "2-3");
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
			if (n >= 2034 && n <= 2036) return ManyLamps::Active() ? (unsigned)ManyLamps::MaxLamps : *(const UInt8*)kOptCapByte == 0x05 ? 6 : 3;
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
		inline UInt8 WindowImm = 0x02;
		inline unsigned long long WorldDraws = 0, LampPasses = 0, HighlightPasses = 0, TexturePasses = 0, FogPasses = 0;
		inline unsigned long long OptDraws[0x14] = {}, OptLights[0x14][16] = {};
		inline unsigned long long Dropped = 0, DroppedDraws = 0, DroppedSkipCompare = 0, OverSlots = 0, OverSlotDraws = 0;
		inline std::string ShaderOf[0x14];
		inline unsigned long long Multi = 0, ShinyByLamps[10] = {}, ShinyFaceGen = 0, Plain = 0, Flagged = 0, NotPP = 0, Lighting30 = 0;
		inline unsigned long long ShinySkinned = 0;
		enum Group : UInt8 { GShiny = 0, GShinyOver, GFaceGen, GPlain, GParallax, GMsn, GHair, GGlow, GLighting30, GOther, kGroups };
		inline const char* const kGroupNames[kGroups] = { "shiny within the limit", "shiny past the limit", "shiny FaceGen", "not shiny", "parallax", "MSN",
			"hair", "glow", "Lighting30", "other property" };
		inline unsigned long long GroupObjects[kGroups] = {}, GroupLampCount[kGroups] = {}, GroupLamps[kGroups] = {}, GroupHighlights[kGroups] = {},
			GroupSkipped[kGroups] = {}, GroupShiny[kGroups] = {};
		constexpr unsigned kPastMax = 40;
		inline unsigned long long PastByLamps[kPastMax + 1] = {};
		constexpr unsigned kSeen = 8192;
		inline const void* SeenKey[kSeen] = {};
		inline unsigned SeenFrame[kSeen] = {};
		inline UInt8 SeenGroup[kSeen] = {};
		inline UInt8 Classify(const BSShaderProperty* property);
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
			memset(GroupObjects, 0, sizeof(GroupObjects)); memset(GroupLampCount, 0, sizeof(GroupLampCount)); memset(GroupLamps, 0, sizeof(GroupLamps));
			memset(GroupHighlights, 0, sizeof(GroupHighlights)); memset(GroupSkipped, 0, sizeof(GroupSkipped)); memset(GroupShiny, 0, sizeof(GroupShiny));
			memset(PastByLamps, 0, sizeof(PastByLamps));
		}

		inline void Report() {
			if (!Frames) return;
			const double f = Frames;
			char line[2048]; int n = 0;
			n += sprintf_s(line + n, sizeof(line) - n, "LAMPS IN ONE PASS census (%s, one pass up to %u lamps + f), %u frames, per frame: world draws %.0f | OPT passes",
				WindowState ? "on" : "off", (unsigned)WindowImm, Frames, WorldDraws / f);
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
			n = 0;
			n += sprintf_s(line + n, sizeof(line) - n, "LAMPS IN ONE PASS census (%s) by group, per frame (objects, of them shiny, lamps per object, lamp passes, "
				"highlight passes drawn + skipped):", WindowState ? "on" : "off");
			for (int g = 0; g < kGroups; g++) if (GroupObjects[g])
				n += sprintf_s(line + n, sizeof(line) - n, " | %s %.1f, %.1f, %.1f, %.1f, %.1f + %.1f", kGroupNames[g], GroupObjects[g] / f, GroupShiny[g] / f,
					GroupLampCount[g] / (double)GroupObjects[g], GroupLamps[g] / f, (GroupHighlights[g] - GroupSkipped[g]) / f, GroupSkipped[g] / f);
			Logger::Log("%s", line);
			if (GroupObjects[GShinyOver]) {
				n = 0;
				n += sprintf_s(line + n, sizeof(line) - n, "LAMPS IN ONE PASS census (%s): shiny objects past the limit by lamps, per frame:", WindowState ? "on" : "off");
				for (unsigned k = 0; k <= kPastMax; k++) if (PastByLamps[k])
					n += sprintf_s(line + n, sizeof(line) - n, " %u%s:%.1f", k, k == kPastMax ? "+" : "", PastByLamps[k] / f);
				Logger::Log("%s", line);
			}
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
			if (Frames && (WindowState != Applied || WindowImm != AppliedImm)) Report();
			WindowState = Applied;
			WindowImm = AppliedImm;
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
			UInt8 group = kGroups;
			for (unsigned probe = 0; probe < 32; probe++, h = (h + 1) & (kSeen - 1)) {
				if (SeenFrame[h] == FrameId && SeenKey[h] == geometry) { group = SeenGroup[h]; break; }
				if (SeenFrame[h] != FrameId) { SeenFrame[h] = FrameId; SeenKey[h] = geometry; SeenGroup[h] = group = Classify(property); break; }
			}
			if (group >= kGroups) return;
			if (lamp) GroupLamps[group]++; else GroupHighlights[group]++;
		}

		inline UInt8 Classify(const BSShaderProperty* property) {
			Multi++;
			const UInt32 vtable = property ? *(const UInt32*)property : 0;
			if (vtable == 0x10B9910) { Lighting30++; GroupObjects[GLighting30]++; return GLighting30; }
			if (vtable != 0x10AE0D0) { NotPP++; GroupObjects[GOther]++; return GOther; }
			const UInt32 flags0 = property->ulFlags[0];
			const BSShaderPPLightingProperty* lighting = (const BSShaderPPLightingProperty*)property;
			const bool glow = !(flags0 & 0x400) && lighting->ppTextures[2] && lighting->ppTextures[2][0];
			const unsigned lamps = CountLamps(property);
			UInt8 group;
			if ((flags0 & (0x800 | 0x1000 | 0x40000)) || glow) {
				Flagged++;
				group = (flags0 & 0x40000) ? GHair : (flags0 & 0x1000) ? GMsn : (flags0 & 0x800) ? GParallax : GGlow;
			}
			else if (!(flags0 & 1)) { Plain++; group = GPlain; }
			else if (flags0 & 0x400) { ShinyFaceGen++; group = GFaceGen; }
			else {
				ShinyByLamps[min(lamps, 9u)]++;
				if (flags0 & 2) ShinySkinned++;
				group = lamps > WindowImm ? GShinyOver : GShiny;
				if (group == GShinyOver) PastByLamps[min(lamps, kPastMax)]++;
			}
			GroupObjects[group]++;
			GroupLampCount[group] += lamps;
			if (flags0 & 1) GroupShiny[group]++;
			return group;
		}

		inline void NoteSkipped(const void* passPtr) {
			if (!GpuTimer::Enabled || !passPtr) return;
			const void* geometry = ((const PassView*)passPtr)->geometry;
			unsigned h = (unsigned)(((uintptr_t)geometry >> 4) * 2654435761u) & (kSeen - 1);
			for (unsigned probe = 0; probe < 32; probe++, h = (h + 1) & (kSeen - 1)) {
				if (SeenFrame[h] != FrameId) return;
				if (SeenKey[h] == geometry) { if (SeenGroup[h] < kGroups) GroupSkipped[SeenGroup[h]]++; return; }
			}
		}

		inline void OnBind(UInt32 passIndex, const char* ps, const char* vs) { BoundPass = passIndex; BoundPS = ps; BoundVS = vs; }
	}
#endif
}
