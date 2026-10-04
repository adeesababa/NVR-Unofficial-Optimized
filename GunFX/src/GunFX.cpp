// GunFX: an xNVSE plugin for Fallout: New Vegas. Smoke from the gun barrel, with the game's own
// temporary-effect spawner (the one impact effects use):
//   - a small puff at the muzzle on each shot (optional);
//   - after you stop firing, a thin wisp rising from the barrel like a lit cigarette, longer the more you fired.
//   - a soft orange glow at the muzzle while the barrel is hot.
// Settings in GunFX.ini next to the DLL.
//
// Game addresses (FalloutNV.exe 1.4.0.525):
//   TESObjectWEAP::Fire                0x00523150  thiscall (weapon, actor); called from 0x008BADE9 (attacks),
//                                      0x0087BA71 (queued fire task) and 0x005DA5F8 (FireWeapon script command)
//   Actor::GetProjectileNode           0x008BE0A0  thiscall (actor): the gun's ProjectileNode, where its bullets start
//   BSTempEffectParticle::Spawn        0x006890B0  cdecl (cell, lifetime, model, up direction, position, scale, flags, parent)
//   PlayerCharacter singleton pointer  0x011DEA3C
#include <windows.h>
#include "IniDefaults.h"
#include <cstdio>
#include <share.h>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <string>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

typedef unsigned int UInt32;
typedef unsigned char UInt8;

// xNVSE plugin interface (only what is used).
struct PluginInfo { UInt32 infoVersion; const char* name; UInt32 version; };
struct NVSEInterface {
	UInt32 nvseVersion, runtimeVersion, editorVersion, isEditor;
	bool (*RegisterCommand)(void*);
	void (*SetOpcodeBase)(UInt32);
	void* (*QueryInterface)(UInt32);
	UInt32 (*GetPluginHandle)(void);
};
struct NVSEMessage { const char* sender; UInt32 type; UInt32 dataLen; void* data; };
struct NVSEMessagingInterface {
	UInt32 version;
	bool (*RegisterListener)(UInt32 listener, const char* sender, void (*handler)(NVSEMessage*));
};
static const UInt32 kInterface_Messaging = 2;
static const UInt32 kMessage_MainGameLoop = 20;

struct NiPoint3 { float x, y, z; };

struct MuzzleFlash {
	bool bEnabled, bMPSEnabled, bUpdateLight, gap03;
	float fEnableTimer, fDurationTimer;
	UInt8* node;          // NiNode at the muzzle (NiRefObject: ref count at +4); world translation at +0x8C
	void* light;
	void* projectile;
	void* sourceWeap;
	UInt8* sourceActor;   // TESObjectREFR; parent cell at +0x40
};

static struct {
	// Puff on each shot.
	int puff;
	char puffModel[MAX_PATH];
	float puffLifetime, puffScale, puffInterval;
	int puffPlayerOnly;
	// Wisp driven by barrel heat (your own gun).
	int trail;
	char trailModel[MAX_PATH];
	float trailScale, heatPerShot, coolPerSecond, maxHeat, heatStart, heatFull, maxRate, fileRate, effectSeconds, tailSeconds;
	int trailFlags, trailSmokeOnly, trailFollow;
	float constantRate, sway, swaySpeed, coolDelay, wispStopDelay;
	// After-fire trail ([Trail]): a thin wisp that rises only once you stop firing.
	int after;
	char afterModel[MAX_PATH];
	float afterScale, afterDelay, afterMinHeat, afterFullHeat, afterMaxRate, afterSway, afterSwaySpeed;
	int glow, glowPreview;
	float glowScale, glowLength, glowStart, glowHeatSpan, glowRiseSeconds;
	float glowRadius, glowForwardLength, glowMinLength, glowSpreadShots, glowSpreadCoolSeconds;
	float glowOffsetX, glowOffsetY, glowOffsetZ;
	char puffNode[128], wispNode[128], ejectNode[128], ejectModel[MAX_PATH];
	NiPoint3 puffOffset, wispOffset, ejectOffset;
	int eject, haze, hazePreview, hazeMask, logWeaponNodes, blast;
	float blastPixels, blastRadius, blastSeconds;
	float ejectScale, ejectLifetime, ejectInterval, ejectHotScale, ejectBurst, puffBurst;
	float hazeStart, hazeFull, hazeRise, hazePixels, hazeHeight, hazeWidth, hazeSpeed, hazeLength, hazeMinSize;
	int flags, logEvents;
} Settings;

static FILE* Log = nullptr;
static void LogLine(const char* format, ...) {
	if (!Log) return;
	va_list args; va_start(args, format); vfprintf(Log, format, args); va_end(args);
	fputc('\n', Log); fflush(Log);
}
static bool LogEvent() { if (Settings.logEvents <= 0) return false; Settings.logEvents--; return true; }

// Master switches from NVR's menu (Main > GunFX), set every frame through GunFX_SetSwitches: bit 0 puff, 1 heat
// smoke strand, 2 ejection smoke, 3 barrel glow, 4 heat haze, 5 muzzle blast, 6 energy weapons too, 7 after-fire trail. All on until NVR says otherwise, so
// GunFX also works on its own (then only the INI decides).
static volatile LONG NvrSwitches = -1;
static bool Switch(int bit) { return (NvrSwitches >> bit) & 1; }
// Energy weapons (lasers, plasma, ...) get no GunFX unless the menu switch EnergyWeapons (bit 6) is on. Decided from
// the weapon's own data, so modded weapons count too: it uses the Energy Weapons skill (TESObjectWEAP actor value at
// +0x15C, 34) or its type is energy pistol/rifle (+0xF4: 4, 7).
static bool IsEnergyWeapon(void* weapon) {
	if (!weapon) return false;
	const UInt8 type = *((UInt8*)weapon + 0xF4);
	return *(UInt32*)((UInt8*)weapon + 0x15C) == 34 || type == 4 || type == 7;
}
static bool GunAllowed(void* weapon) { return !IsEnergyWeapon(weapon) || Switch(6); }

typedef void* (__cdecl* SpawnFn)(void* cell, float lifetime, const char* model, NiPoint3 up, NiPoint3 position,
	float scale, UInt32 flags, void* parent);
static const SpawnFn SpawnEffect = (SpawnFn)0x006890B0;
static void* Player() { return *(void**)0x011DEA3C; }

// Keeps a game node alive while it is remembered between frames (released with the game's own DeleteThis at zero).
static void NodeAddRef(UInt8* node) { if (node) InterlockedIncrement((volatile LONG*)(node + 4)); }
static void NodeRelease(UInt8* node) {
	if (node && InterlockedDecrement((volatile LONG*)(node + 4)) == 0) {
		void** vtable = *(void***)node;
		((void(__thiscall*)(void*))vtable[1])(node);
	}
}

// Hides the solid parts of a spawned effect (meshes such as the ashtray under the cigarette smoke), leaving its
// particle systems. Uses the game's own type checks: NiObject vtable 8 IsTriStrips, 9 IsTriShape, 12 IsParticlesGeom,
// 3 IsNiNode; NiNode children at +0x9C (NiTArray: data +4, end +0xA); NiAVObject flags at +0x30 (bit 0 = culled).
static int HideSolidMeshes(UInt8* object, int depth) {
	if (!object || depth > 8) return 0;
	void** vtable = *(void***)object;
	typedef void* (__thiscall* IsFn)(void*);
	if (((IsFn)vtable[12])(object)) return 0; // particles: keep
	if (((IsFn)vtable[8])(object) || ((IsFn)vtable[9])(object)) { *(UInt32*)(object + 0x30) |= 1; return 1; }
	int hidden = 0;
	if (((IsFn)vtable[3])(object)) {
		UInt8** children = *(UInt8***)(object + 0x9C + 4);
		const unsigned short count = *(unsigned short*)(object + 0x9C + 0xA);
		for (unsigned short i = 0; children && i < count; i++) hidden += HideSolidMeshes(children[i], depth + 1);
	}
	return hidden;
}

// Switches off the emitters of a spawned effect, so no new smoke appears; the smoke already in the air keeps rising and
// fading. NiObjectNET controller at +0x0C; NiTimeController flags at +0x08 (0x8 = active), next controller at +0x30;
// NiObject vtable 2 GetRTTI -> NiRTTI { const char* name }. Only a controller named NiPSysEmitterCtlr is touched.
static bool LoggedControllers = false;
static const char* RttiName(UInt8* object) {
	typedef const char** (__thiscall* RttiFn)(void*);
	const char** rtti = ((RttiFn)(*(void***)object)[2])(object);
	return rtti && *rtti ? *rtti : "?";
}
static int StopEmitters(UInt8* object, int depth) {
	if (!object || depth > 8) return 0;
	void** vtable = *(void***)object;
	typedef void* (__thiscall* IsFn)(void*);
	int stopped = 0;
	if (((IsFn)vtable[12])(object)) {
		char names[256] = "";
		for (UInt8* ctrl = *(UInt8**)(object + 0x0C); ctrl; ctrl = *(UInt8**)(ctrl + 0x30)) {
			const char* name = RttiName(ctrl);
			if (!LoggedControllers && strlen(names) + strlen(name) + 2 < sizeof(names)) { strcat_s(names, name); strcat_s(names, " "); }
			if (!strcmp(name, "NiPSysEmitterCtlr")) { *(unsigned short*)(ctrl + 0x08) &= ~0x8; stopped++; }
		}
		if (!LoggedControllers) { LoggedControllers = true; LogLine("particle system controllers: %s", names); }
		return stopped;
	}
	if (((IsFn)vtable[3])(object)) {
		UInt8** children = *(UInt8***)(object + 0x9C + 4);
		const unsigned short count = *(unsigned short*)(object + 0x9C + 0xA);
		for (unsigned short i = 0; children && i < count; i++) stopped += StopEmitters(children[i], depth + 1);
	}
	return stopped;
}

// Borrowed nodes are used only synchronously while the visible model is alive.
static UInt8* FindNamedNode(UInt8* object, const char* wanted, int depth = 0) {
	if (!object || !wanted || !*wanted || depth > 24) return nullptr;
	const char* name = *(const char**)(object + 8);
	if (name && (!_stricmp(name, wanted) || (!strncmp(name, "##", 2) && !_stricmp(name + 2, wanted)))) return object;
	typedef void* (__thiscall* IsFn)(void*);
	if (!((IsFn)(*(void***)object)[3])(object)) return nullptr;
	UInt8** children = *(UInt8***)(object + 0xA0);
	const unsigned short count = *(unsigned short*)(object + 0xA6);
	for (unsigned short i = 0; children && i < count; ++i)
		if (UInt8* found = FindNamedNode(children[i], wanted, depth + 1)) return found;
	return nullptr;
}
static NiPoint3 NodePosition(UInt8* node, NiPoint3 offset) {
	NiPoint3 at = *(NiPoint3*)(node + 0x8C);
	const float* r = (const float*)(node + 0x68);
	const float scale = *(float*)(node + 0x98);
	at.x += scale * (r[0]*offset.x + r[1]*offset.y + r[2]*offset.z);
	at.y += scale * (r[3]*offset.x + r[4]*offset.y + r[5]*offset.z);
	at.z += scale * (r[6]*offset.x + r[7]*offset.y + r[8]*offset.z);
	return at;
}
static void LogNodeNames(UInt8* node, int& budget, int depth = 0) {
	if (!node || budget <= 0 || depth > 24) return;
	--budget;
	const char* name = *(const char**)(node + 8);
	if (name && *name) LogLine("weapon node: %s", name);
	typedef void* (__thiscall* IsFn)(void*);
	if (!((IsFn)(*(void***)node)[3])(node)) return;
	UInt8** children = *(UInt8***)(node + 0xA0);
	const unsigned short count = *(unsigned short*)(node + 0xA6);
	for (unsigned short i = 0; children && i < count && budget > 0; ++i) LogNodeNames(children[i], budget, depth + 1);
}
static UInt8* VisibleRoot();
static UInt8* SmokeAnchor(const char* name, UInt8* fallback) {
	UInt8* named = FindNamedNode(VisibleRoot(), name);
	return named ? named : fallback;
}

// ---- Look tuning from the INI ------------------------------------------------------------------------------------
// The smoke's look lives in the effect file; after spawning, these values are rewritten in the effect's own copy.
// Nothing about the modifiers' memory layout is assumed: their list is found in the particle system by checking
// that every entry is a NiPSys/BSPSys object, and each value is located by searching the modifier for the exact
// numbers the file was built with (tools\make-effects.py). Only places that match are written; misses are logged.
struct Look {             // file values (what to look for) and INI values (what to write)
	float speed, radius, radiusVar, life, lifeVar;   // NiPSysEmitter: radius, radius variation, life, life variation
	float grow, fade;                                // NiPSysGrowFadeModifier
	float strength, turbulence, turbulenceScale;     // NiPSysGravityModifier (negative strength rises)
	float percents[6];                               // BSPSysSimpleColorModifier: fade in/out, colour 1 end, 2 start, 2 end, 3 start
	float opacity;                                   // middle colour's alpha
};
static const Look WispFile = { 12.0f, 5.0f, 1.5f, 2.5f, 0.6f, 2.2f, 0.0f, -20.0f, 0.03f, 5.0f, { 0.08f, 0.15f, 0.2f, 0.5f, 0.51f, 0.8f }, 0.35f };
static const Look PuffFile = { 5.0f, 3.0f, 1.0f, 1.3f, 0.3f, 1.2f, 0.0f, -6.0f, 0.035f, 5.0f, { 0.03f, 0.15f, 0.0f, 0.06f, 0.51f, 0.8f }, 0.35f };
// The strand: a dense stream of small faint puffs that blend into one thin strand (barrelstrand.nif, emits 40/s).
static const Look StrandFile = { 10.0f, 3.0f, 0.7f, 2.6f, 0.3f, 2.6f, 0.0f, -9.0f, 0.02f, 5.0f, { 0.05f, 0.15f, 0.02f, 0.12f, 0.45f, 0.92f }, 0.15f };
static const Look* WispBuilt = &WispFile;   // the numbers the chosen wisp model was built with (set from sModel)
static Look WispLook = WispFile, PuffLook = PuffFile;

static bool InImage(const void* p, UInt32 lo, UInt32 hi) { return (UInt32)p >= lo && (UInt32)p < hi; }
// Name of a game object's class, or null when the pointer does not look like one (vtable in .rdata, code in .text).
static const char* SafeRtti(UInt8* object) {
	if (!object || IsBadReadPtr(object, 8)) return nullptr;
	void** vtable = *(void***)object;
	if (!InImage(vtable, 0x00FDF000, 0x01183000) || !InImage(vtable[2], 0x00401000, 0x00FDF000)) return nullptr;
	return RttiName(object);
}
static bool IsModifier(UInt8* object) {
	const char* n = SafeRtti(object);
	return n && (!strncmp(n, "NiPSys", 6) || !strncmp(n, "BSPSys", 6)) && strstr(n, "Ctlr") == nullptr;
}
static bool Near(float a, float b) { return fabsf(a - b) < 0.0005f; }
static float* FindFloats(UInt8* object, const float* want, const int* gaps, int count) {
	// want[k] at byte offset gaps[k] from the match; searched in the first 0x80 bytes of the object.
	for (int at = 8; at <= 0x80; at += 4) {
		bool ok = true;
		for (int k = 0; k < count && ok; k++) ok = Near(*(float*)(object + at + gaps[k]), want[k]);
		if (ok) return (float*)(object + at);
	}
	return nullptr;
}

static int ApplyLookToSystem(UInt8* psys, const Look& file, const Look& look, char* report, size_t reportSize) {
	// The modifiers: a pointer field of the particle system that leads either to an array of modifiers or to a linked
	// list of them (NiTListItem: next +0, previous +4, item +8). Every entry must be a NiPSys/BSPSys object.
	UInt8* list[32]; int count = 0;
	const char* kind = "";
	for (int off = 0x20; off < 0x200 && !count; off += 4) {
		UInt8** data = *(UInt8***)(psys + off);
		if (!data || IsBadReadPtr(data, 16)) continue;
		int n = 0;
		while (n < 32 && !IsBadReadPtr(data + n, 4) && IsModifier(data[n])) { list[n] = data[n]; n++; }
		if (n >= 3) { count = n; kind = "array"; break; }
		n = 0;
		for (UInt8** node = data; node && n < 32 && !IsBadReadPtr(node, 12) && IsModifier((UInt8*)node[2]); node = (UInt8**)node[0]) list[n++] = (UInt8*)node[2];
		if (n >= 3) { count = n; kind = "list"; break; }
	}
	if (!count) { strcat_s(report, reportSize, "modifier list not found"); return 0; }
	char found[48]; sprintf_s(found, "(%d modifiers in a %s) ", count, kind); strcat_s(report, reportSize, found);
	int written = 0;
	// Copies of one effect share some modifier data with each other, so a later copy may already hold the values
	// written into an earlier one; the place found the first time (by the file's numbers) is remembered per type.
	static int emitterAt[2] = {}, growAt = 0, gravityAt = 0, colourAt = 0;
	auto locate = [](UInt8* m, int& learned, const float* want, const int* gaps, int n) -> float* {
		if (float* f = FindFloats(m, want, gaps, n)) { learned = (int)((UInt8*)f - m); return f; }
		return learned ? (float*)(m + learned) : nullptr;
	};
	for (int i = 0; i < count; i++) {
		UInt8* m = list[i]; const char* name = SafeRtti(m);
		if (!strcmp(name, "NiPSysCylinderEmitter") || !strcmp(name, "NiPSysSphereEmitter")) {
			const float want[] = { file.radius, file.radiusVar, file.life, file.lifeVar }; const int gaps[] = { 0, 4, 8, 12 };
			if (float* f = locate(m, emitterAt[name[6] == 'S'], want, gaps, 4)) { f[0] = look.radius; f[1] = look.radiusVar; f[2] = look.life; f[3] = look.lifeVar; written++; strcat_s(report, reportSize, "size+life "); }
		}
		else if (!strcmp(name, "NiPSysGrowFadeModifier")) {
			const float want[] = { file.grow, file.fade }; const int gaps[] = { 0, 8 };
			if (float* f = locate(m, growAt, want, gaps, 2)) { f[0] = look.grow; f[2] = look.fade; written++; strcat_s(report, reportSize, "grow "); }
		}
		else if (!strcmp(name, "NiPSysGravityModifier")) {
			const float want[] = { file.strength, file.turbulence, file.turbulenceScale }; const int gaps[] = { 0, 8, 12 };
			if (float* f = locate(m, gravityAt, want, gaps, 3)) { f[0] = look.strength; f[2] = look.turbulence; f[3] = look.turbulenceScale; written++; strcat_s(report, reportSize, "rise+curl "); }
		}
		else if (!strcmp(name, "BSPSysSimpleColorModifier")) {
			const int gaps[] = { 0, 4, 8, 12, 16, 20 };
			if (float* f = locate(m, colourAt, file.percents, gaps, 6)) {
				for (int k = 0; k < 6; k++) f[k] = look.percents[k];
				f[6 + 4 + 3] = look.opacity;   // colour 2 alpha (6 percents, then three RGBA colours)
				written++; strcat_s(report, reportSize, "fade+opacity ");
			}
		}
	}
	return written;
}

// Applies a look to every particle system of a spawned effect (root at effect + 0x18). Returns the parts written.
static int ApplyLook(UInt8* effect, const Look& file, const Look& look, char* report, size_t reportSize) {
	int written = 0;
	__try {
		UInt8* stack[16]; int top = 0;
		if (UInt8* root = *(UInt8**)(effect + 0x18)) stack[top++] = root;
		while (top) {
			UInt8* object = stack[--top];
			void** vtable = *(void***)object;
			typedef void* (__thiscall* IsFn)(void*);
			if (((IsFn)vtable[12])(object)) written += ApplyLookToSystem(object, file, look, report, reportSize);
			else if (((IsFn)vtable[3])(object)) {
				UInt8** children = *(UInt8***)(object + 0x9C + 4);
				const unsigned short count = *(unsigned short*)(object + 0x9C + 0xA);
				for (unsigned short i = 0; children && i < count && top < 16; i++) if (children[i]) stack[top++] = children[i];
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { strcat_s(report, reportSize, "(stopped by a fault)"); }
	return written;
}

static void ReadLook(const char* section, const Look& file, Look& look);

// ---- Puff on each shot -----------------------------------------------------------------------------------------
static struct { void* actor; ULONGLONG tick; } LastPuff[16] = {};

static void* SpawnBurst(void* cell, float lifetime, const char* model, NiPoint3 at, float scale, float seconds, ULONGLONG now);
static void Puff(MuzzleFlash* flash, ULONGLONG now) {
	if (!Settings.puff || !Switch(0)) return;
	if (Settings.puffPlayerOnly && flash->sourceActor != Player()) return;
	void* cell = *(void**)(flash->sourceActor + 0x40);
	if (!cell) return;
	int slot = -1, oldest = 0;
	for (int i = 0; i < 16; i++) {
		if (LastPuff[i].actor == flash->sourceActor) { slot = i; break; }
		if (LastPuff[i].tick < LastPuff[oldest].tick) oldest = i;
	}
	// Automatic guns (TESObjectWEAP flags at +0x100, 0x2 = automatic): at most one puff every fMinInterval, so a burst
	// builds a haze; other guns: a puff on every shot.
	const bool automatic = flash->sourceWeap && (*((UInt8*)flash->sourceWeap + 0x100) & 0x2);
	if (automatic && slot >= 0 && now - LastPuff[slot].tick < (ULONGLONG)(Settings.puffInterval * 1000.0f)) return;
	if (slot < 0) slot = oldest;
	LastPuff[slot].actor = flash->sourceActor;
	LastPuff[slot].tick = now;
	const NiPoint3 position = NodePosition(flash->node, Settings.puffOffset);
	void* effect = SpawnBurst(cell, Settings.puffLifetime, Settings.puffModel, position, Settings.puffScale, Settings.puffBurst, now);
	// v24: the NIF already contains this look. Avoid heuristic writes into a live particle object.
	if (LogEvent()) LogLine("puff at (%.1f, %.1f, %.1f) -> %s (NIF look)", position.x, position.y, position.z,
		effect ? "created" : "NOT created");
}

// ---- Wisp driven by barrel heat ---------------------------------------------------------------------------------
// Each shot heats the barrel by heatPerShot; it cools by coolPerSecond. One wisp effect follows the muzzle, and its
// smoke rate is set every frame from the heat: nothing below heatStart, rising smoothly to maxRate at heatFull, and
// falling the same way as the barrel cools; the smoke already in the air always rises and fades out on its own.
// The rate is the value of the emitter controller's float interpolator (NiSingleInterpController interpolator at
// +0x34, NiFloatInterpolator value at +0x0C); it is only written after checking that it holds the file's rate.
static float Heat = 0.0f;
static float VisualHeat = 0.0f;
static float VisualHaze = 0.0f;
static float SpreadHeat = 0.0f;       // slow, separate spread toward the rear of the gun
static ULONGLONG LastShot = 0;          // the last shot of the current gun
static ULONGLONG LastMuzzleSeen = 0;
static ULONGLONG LastLoop = 0;
static UInt8* TrailNode = nullptr;     // the muzzle of the last shot (kept alive with a reference)
static void* TrailActor = nullptr;
static CRITICAL_SECTION HeatLock;
static bool HeatLockReady = false;
static UInt8* HeatNode = nullptr;       // retained for NVR's render-time sample
static float HeatStrength = 0.0f, HeatLength = 0.0f, HeatRadius = 0.0f, HeatForwardLength = 0.0f;
static float HeatOffsetX = 0.0f, HeatOffsetY = 0.0f, HeatOffsetZ = 0.0f;
static float HazePublished[8] = {};
static float HazeBlast[2] = {};       // muzzle blast: displacement in pixels now (fades after each shot), radius in pixels
static float HazeWorld[6] = {};       // muzzle (with the glow offsets) and rearward axis, world space, game-loop time

static UInt8* FollowEffect = nullptr;  // the running wisp (kept alive with a reference)
static ULONGLONG FollowUntil = 0;      // when it stops following (just before the game removes it)
static float* RateValue = nullptr;     // its emitter's smoke rate, when found
static UInt8* RateController = nullptr; // its emitter controller (kept switched on while it is ours)
static Look FollowLook;                // the look written into the running wisp
static bool FollowHeld = false;        // the game held its own reference to the running wisp when it was spawned
// The after-fire trail: a second following effect, like the wisp, that only smokes after the last shot.
static UInt8* AfterEffect = nullptr;
static ULONGLONG AfterUntil = 0;
static float* AfterRate = nullptr;
static UInt8* AfterController = nullptr;
static bool AfterHeld = false;
static const int GlowPoints = 4;
static NiPoint3 GlowBack = { 0.0f, -1.0f, 0.0f };
static bool GlowUnavailable = false;   // fail closed if the glow mesh is missing
static ULONGLONG NextGlowPulse = 0;

// Each gun keeps its own heat: switching guns puts the current one's heat aside (it keeps cooling while away) and
// brings back the new one's, cooled by the time it was not in use.
struct GunHeat { void* weapon; float heat, spread; ULONGLONG since; };
static GunHeat Guns[16] = {};
static void* CurrentWeapon = nullptr;
static void ReleaseWisp(const char* why);
static void SwitchGun(void* weapon, ULONGLONG now) {
	if (weapon == CurrentWeapon) return;
	int slot = -1, oldest = 0;
	for (int i = 0; i < 16; i++) {
		if (Guns[i].weapon == CurrentWeapon && CurrentWeapon) { Guns[i].heat = Heat; Guns[i].spread = SpreadHeat; Guns[i].since = now; }
		if (Guns[i].since < Guns[oldest].since) oldest = i;
	}
	if (CurrentWeapon) {
		bool stored = false;
		for (int i = 0; i < 16; i++) stored |= Guns[i].weapon == CurrentWeapon;
		if (!stored) Guns[oldest] = GunHeat{ CurrentWeapon, Heat, SpreadHeat, now };
	}
	Heat = 0.0f;
	SpreadHeat = 0.0f;
	for (int i = 0; i < 16; i++) if (Guns[i].weapon == weapon) slot = i;
	if (slot >= 0) {
		const float awaySeconds = (now - Guns[slot].since) / 1000.0f;
		const float cooled = Guns[slot].heat - awaySeconds * Settings.coolPerSecond;
		Heat = cooled > 0.0f ? cooled : 0.0f;
		const float spread = Guns[slot].spread - awaySeconds / Settings.glowSpreadCoolSeconds;
		SpreadHeat = spread > 0.0f ? spread : 0.0f;
	}
	if (CurrentWeapon) ReleaseWisp("switched guns");
	NextGlowPulse = 0;
	VisualHeat = VisualHaze = 0.0f;
	CurrentWeapon = weapon;
}

static void OnPlayerShot(MuzzleFlash* flash, ULONGLONG now) {
	SwitchGun(flash->sourceWeap, now);
	LastShot = now;
	LastMuzzleSeen = now;
	Heat = Heat + Settings.heatPerShot > Settings.maxHeat ? Settings.maxHeat : Heat + Settings.heatPerShot;
	SpreadHeat += 1.0f / Settings.glowSpreadShots;
	if (SpreadHeat > 1.0f) SpreadHeat = 1.0f;
	if (TrailNode != flash->node) {
		NodeAddRef(flash->node);
		NodeRelease(TrailNode);
		TrailNode = flash->node;
	}
	TrailActor = flash->sourceActor;
}

static void LoadSettings();
static char IniPath[MAX_PATH];
static FILETIME IniWritten = {};
static char WeaponIniPath[MAX_PATH] = "";   // the equipped gun's settings file ("" = none), see UpdateGunSettings
static FILETIME WeaponIniWritten = {};
static int OwnGunValues(const char* path);
static ULONGLONG LastIniCheck = 0;

// Re-reads GunFX.ini when it is saved while the game runs (checked once a second), so settings can be tried
// without restarting.
static void ReloadIniIfChanged(ULONGLONG now) {
	if (now - LastIniCheck < 1000) return;
	LastIniCheck = now;
	WIN32_FILE_ATTRIBUTE_DATA info;
	if (WeaponIniPath[0] && GetFileAttributesExA(WeaponIniPath, GetFileExInfoStandard, &info) &&
		CompareFileTime(&info.ftLastWriteTime, &WeaponIniWritten) != 0) {
		WeaponIniWritten = info.ftLastWriteTime;
		LogLine("this gun's settings file changed, reloading (%d own value(s))", OwnGunValues(WeaponIniPath));
		LoadSettings();
	}
	if (!GetFileAttributesExA(IniPath, GetFileExInfoStandard, &info)) return;
	if (CompareFileTime(&info.ftLastWriteTime, &IniWritten) == 0) return;
	const bool first = IniWritten.dwLowDateTime == 0 && IniWritten.dwHighDateTime == 0;
	IniWritten = info.ftLastWriteTime;
	if (first) return;
	LogLine("GunFX.ini changed, reloading");
	LoadSettings();
	GlowUnavailable = false;
	if (FollowEffect) LogLine("running wisp keeps its baked NIF look");
}


// Our own effect files (Meshes\GunFX, or the older Meshes\BarrelSmoke paths).
static bool OwnModel(const char* model) {
	return _strnicmp(model, "GunFX", 5) == 0 || _strnicmp(model, "BarrelSmoke", 11) == 0;
}

static float* FindRate(UInt8* object, int depth, bool ownModel, UInt8** controller) {
	if (!object || depth > 8) return nullptr;
	void** vtable = *(void***)object;
	typedef void* (__thiscall* IsFn)(void*);
	if (((IsFn)vtable[12])(object)) {
		for (UInt8* ctrl = *(UInt8**)(object + 0x0C); ctrl; ctrl = *(UInt8**)(ctrl + 0x30)) {
			if (strcmp(RttiName(ctrl), "NiPSysEmitterCtlr")) continue;
			UInt8* interp = *(UInt8**)(ctrl + 0x34);
			if (!interp || strcmp(RttiName(interp), "NiFloatInterpolator")) return nullptr;
			float* value = (float*)(interp + 0x0C);
			if (!ownModel && fabsf(*value - Settings.fileRate) >= 0.01f) return nullptr;
			*controller = ctrl;
			return value;
		}
		return nullptr;
	}
	if (((IsFn)vtable[3])(object)) {
		UInt8** children = *(UInt8***)(object + 0x9C + 4);
		const unsigned short count = *(unsigned short*)(object + 0x9C + 0xA);
		for (unsigned short i = 0; children && i < count; i++)
			if (float* found = FindRate(children[i], depth + 1, ownModel, controller)) return found;
	}
	return nullptr;
}

// ---- Bursts (puff, ejection port) ----------------------------------------------------------------------------------
// A burst's emitter must start when it is spawned. A NIF switch "on for 0.06 s, then off" follows the controller's
// cycle on the game clock, not the spawn, so most bursts stayed silent and then several emitted together when the
// window came round. These effects keep the switch always on (as the strand does), and the plugin sets the emitter's
// rate at the spawn and to 0 after fBurstSeconds, the same rate control the wisp uses.
struct Burst { UInt8* effect; float* rate; UInt8* controller; ULONGLONG offAt; };
static Burst Bursts[64] = {};
static float BurstRate(const char* model) {
	char lower[MAX_PATH];
	strcpy_s(lower, model);
	_strlwr_s(lower);
	if (strstr(lower, "barrelport")) return 160.0f;   // the rates the files were built with
	if (strstr(lower, "barrelpuff")) return 60.0f;
	return 40.0f;
}
static void EndBurst(Burst& b) {
	if (b.rate) *b.rate = 0.0f;
	NodeRelease(b.effect);
	b = Burst{};
}
static void* SpawnBurst(void* cell, float lifetime, const char* model, NiPoint3 at, float scale, float seconds, ULONGLONG now) {
	void* effect = SpawnEffect(cell, lifetime, model, NiPoint3{ 0.0f, 0.0f, 1.0f }, at, scale, (UInt32)Settings.flags, nullptr);
	if (!effect) return nullptr;
	UInt8* root = *(UInt8**)((UInt8*)effect + 0x18);
	UInt8* controller = nullptr;
	float* rate = root ? FindRate(root, 0, OwnModel(model), &controller) : nullptr;
	if (!rate || !controller) return effect;   // not ours: the file decides
	*rate = BurstRate(model);
	*(unsigned short*)(controller + 0x08) |= 0x8;
	int slot = 0;
	for (int i = 0; i < 64; i++) {
		if (!Bursts[i].effect) { slot = i; break; }
		if (Bursts[i].offAt < Bursts[slot].offAt) slot = i;
	}
	if (Bursts[slot].effect) EndBurst(Bursts[slot]);
	NodeAddRef((UInt8*)effect);
	Bursts[slot] = Burst{ (UInt8*)effect, rate, controller, now + (ULONGLONG)(seconds * 1000.0f) };
	return effect;
}
static void UpdateBursts(ULONGLONG now) {
	for (int i = 0; i < 64; i++) if (Bursts[i].effect && now >= Bursts[i].offAt) EndBurst(Bursts[i]);
}

// Lets the running wisp go: its emitter is switched off and the smoke in the air rises and fades where it is.
static void ReleaseWisp(const char* why) {
	if (!FollowEffect) return;
	UInt8* root = *(UInt8**)(FollowEffect + 0x18);
	if (root && FollowUntil > GetTickCount64()) StopEmitters(root, 0);
	if (why) LogLine("wisp released: %s (heat %.1f)", why, Heat);
	NodeRelease(FollowEffect);
	FollowEffect = nullptr;
	RateValue = nullptr;
	RateController = nullptr;
}

// Lets the after-fire trail go (its smoke in the air keeps rising and fading).
static void ReleaseAfter(const char* why) {
	if (!AfterEffect) return;
	if (AfterRate) *AfterRate = 0.0f;
	if (why) LogLine("after-fire trail released: %s (heat %.1f)", why, Heat);
	NodeRelease(AfterEffect);
	AfterEffect = nullptr;
	AfterRate = nullptr;
	AfterController = nullptr;
}

static NiPoint3 GlowDirection(UInt8* node) {
	if (!node) return GlowBack;
	const NiPoint3 muzzle = *(NiPoint3*)(node + 0x8C);
	UInt8* parent = *(UInt8**)(node + 0x18);
	NiPoint3 back = parent ? *(NiPoint3*)(parent + 0x8C) : muzzle;
	back.x -= muzzle.x; back.y -= muzzle.y; back.z -= muzzle.z;
	float length = sqrtf(back.x * back.x + back.y * back.y + back.z * back.z);
	if (length < 2.0f || length > 100.0f) {
		// Fallback to the node's local -Y direction when its parent is at the muzzle too.
		const float* rot = (float*)(node + 0x68);
		back = NiPoint3{ -rot[1], -rot[4], -rot[7] };
		length = sqrtf(back.x * back.x + back.y * back.y + back.z * back.z);
	}
	return length > 0.01f ? NiPoint3{ back.x / length, back.y / length, back.z / length } : GlowBack;
}

static NiPoint3 GlowAt(int index, int count) {
	NiPoint3 at = *(NiPoint3*)(TrailNode + 0x8C);
	const float distance = count > 1 ? Settings.glowLength * index / (count - 1) : 0.0f;
	at.x += GlowBack.x * distance;
	at.y += GlowBack.y * distance;
	at.z += GlowBack.z * distance;
	return at;
}

static bool InFirstPersonModel(UInt8* node);
static NiPoint3 WorldPoint(UInt8* node, NiPoint3 at) {
	if (!node || !InFirstPersonModel(node)) return at;
	const float* pos = (const float*)((UInt8*)Player() + 0x30);
	const float dx = at.x - pos[0], dy = at.y - pos[1], dz = at.z - pos[2];
	const bool local = dx * dx + dy * dy + dz * dz > 1000.0f * 1000.0f;
	static int logged = 0;
	if (logged < 3) {
		logged++;
		LogLine("first-person point (%.1f, %.1f, %.1f), player at (%.1f, %.1f, %.1f): %s", at.x, at.y, at.z, pos[0], pos[1], pos[2],
			local ? "player-relative, converted to world" : "world space");
	}
	if (local) { at.x += pos[0]; at.y += pos[1]; at.z += pos[2]; }
	return at;
}

static void PublishHeat(bool muzzleAlive, float strength) {
	const float haze = Settings.haze && Switch(4) ? (Settings.hazePreview ? 1.0f : VisualHaze) : 0.0f;
	if (!Settings.glow || !Switch(3)) strength = 0.0f;
	// Muzzle blast: a quick wide shimmer on every shot, independent of the barrel heat; full at the shot, fading over
	// fSeconds (an exponential fade, gone after about three times that).
	const float since = LastShot ? (GetTickCount64() - LastShot) / 1000.0f : 1e9f;
	float pulse = Settings.blast && Switch(5) && since < Settings.blastSeconds * 4.0f ? expf(-since / Settings.blastSeconds) : 0.0f;
	if (pulse < 0.02f) pulse = 0.0f;
	UInt8* next = muzzleAlive && (strength > 0.01f || haze > 0.001f || pulse > 0.0f) ? TrailNode : nullptr;
	NodeAddRef(next);
	EnterCriticalSection(&HeatLock);
	UInt8* old = HeatNode;
	HeatNode = next;
	HeatStrength = next ? strength : 0.0f;
	HeatLength = Settings.glowPreview ? Settings.glowLength :
		Settings.glowMinLength + (Settings.glowLength - Settings.glowMinLength) * SpreadHeat;
	HeatRadius = Settings.glowRadius;
	HeatForwardLength = Settings.glowForwardLength;
	HeatOffsetX = Settings.glowOffsetX;
	HeatOffsetY = Settings.glowOffsetY;
	HeatOffsetZ = Settings.glowOffsetZ;
	HazePublished[0] = next ? haze : 0.0f;
	HazePublished[1] = Settings.hazePixels;
	// Size grows with the heat too: fMinSize of full size when the haze first shows, full size at full heat (the
	// strength already follows the heat in the shader).
	const float hazeSize = Settings.hazeMinSize + (1.0f - Settings.hazeMinSize) * (haze < 0.0f ? 0.0f : haze > 1.0f ? 1.0f : haze);
	HazePublished[2] = Settings.hazeHeight * hazeSize;
	HazePublished[3] = Settings.hazeWidth * hazeSize;
	HazePublished[4] = Settings.hazeSpeed;
	HazePublished[5] = Settings.hazeLength * hazeSize;
	HazePublished[6] = (float)Settings.hazeMask;
	HazeBlast[0] = next ? Settings.blastPixels * pulse : 0.0f;
	HazeBlast[1] = Settings.blastRadius;
	if (next) {
		NiPoint3 muzzle = *(NiPoint3*)(next + 0x8C);
		const float* rot = (const float*)(next + 0x68);
		muzzle.x += rot[0] * Settings.glowOffsetX + rot[1] * Settings.glowOffsetY + rot[2] * Settings.glowOffsetZ;
		muzzle.y += rot[3] * Settings.glowOffsetX + rot[4] * Settings.glowOffsetY + rot[5] * Settings.glowOffsetZ;
		muzzle.z += rot[6] * Settings.glowOffsetX + rot[7] * Settings.glowOffsetY + rot[8] * Settings.glowOffsetZ;
		muzzle = WorldPoint(next, muzzle);
		const NiPoint3 back = GlowDirection(next);
		HazeWorld[0] = muzzle.x; HazeWorld[1] = muzzle.y; HazeWorld[2] = muzzle.z;
		HazeWorld[3] = back.x; HazeWorld[4] = back.y; HazeWorld[5] = back.z;
	}
	LeaveCriticalSection(&HeatLock);
	NodeRelease(old);
}

static bool SpawnGlow(float heatFraction) {
	void* cell = TrailActor ? *(void**)((UInt8*)TrailActor + 0x40) : nullptr;
	if (!cell) return false;
	const int points = 1 + (int)(heatFraction * (GlowPoints - 1));
	for (int i = 0; i < points; i++) {
		const NiPoint3 position = GlowAt(i, points);
		void* effect = SpawnEffect(cell, 0.65f, "GunFX\\barrelglow.nif",
			NiPoint3{ 0.0f, 0.0f, 1.0f }, position, Settings.glowScale * (1.0f - 0.12f * i),
			(UInt32)Settings.trailFlags, nullptr);
		if (!effect) { LogLine("barrel glow NOT created; disabled until INI reload"); GlowUnavailable = true; return false; }
	}
	if (LogEvent()) LogLine("barrel glow pulse: %d point(s) across %.1f units", points, Settings.glowLength);
	return true;
}

static bool SpawnWisp(ULONGLONG now, const NiPoint3& position) {
	void* cell = TrailActor ? *(void**)((UInt8*)TrailActor + 0x40) : nullptr;
	if (!cell) return false;
	void* effect = SpawnEffect(cell, Settings.effectSeconds, Settings.trailModel, NiPoint3{ 0.0f, 0.0f, 1.0f }, position,
		Settings.trailScale, (UInt32)Settings.trailFlags, nullptr);
	if (!effect) { LogLine("wisp %s NOT created", Settings.trailModel); return false; }
	UInt8* root = *(UInt8**)((UInt8*)effect + 0x18);   // the cloned model (BSTempEffectParticle::Init loads it while spawning)
	if (Settings.trailSmokeOnly && root) HideSolidMeshes(root, 0);
	FollowEffect = (UInt8*)effect;
	NodeAddRef(FollowEffect);
	FollowHeld = *(volatile LONG*)(FollowEffect + 4) > 1;
	// v24: use the NIF's baked look; do not scan and modify the live modifier list.
	if (LogEvent()) LogLine("wisp uses baked NIF look");
	FollowUntil = now + (ULONGLONG)(Settings.effectSeconds * 1000.0f) - 150;
	// Our own effects (in Meshes\GunFX) are always taken over, whatever rate they start with; other models only
	// when their rate matches fFileRate.
	const bool ownModel = OwnModel(Settings.trailModel);
	RateController = nullptr;
	RateValue = root ? FindRate(root, 0, ownModel, &RateController) : nullptr;
	const float startRate = RateValue ? *RateValue : -1.0f;
	const unsigned short startFlags = RateController ? *(unsigned short*)(RateController + 0x08) : 0;
	if (RateValue) { *RateValue = 0.0f; *(unsigned short*)(RateController + 0x08) |= 0x8; }
	LogLine("wisp %s at (%.1f, %.1f, %.1f) for up to %.0f s, heat %.1f (game reference %s); smoke rate %s (started at %.1f, emitter %s; controller %p, rate %p)",
		Settings.trailModel, position.x, position.y, position.z, Settings.effectSeconds, Heat, FollowHeld ? "yes" : "no",
		RateValue ? "under control" : "NOT found (on/off only; check fFileRate matches the file)", startRate,
		(startFlags & 0x8) ? "on" : "OFF", RateController, RateValue);
	return true;
}

// Spawns the after-fire trail at the barrel (same source node and offsets as the wisp); its rate starts at 0.
static bool SpawnAfter(ULONGLONG now, const NiPoint3& position) {
	void* cell = TrailActor ? *(void**)((UInt8*)TrailActor + 0x40) : nullptr;
	if (!cell) return false;
	void* effect = SpawnEffect(cell, Settings.effectSeconds, Settings.afterModel, NiPoint3{ 0.0f, 0.0f, 1.0f }, position,
		Settings.afterScale, (UInt32)Settings.trailFlags, nullptr);
	if (!effect) { LogLine("after-fire trail %s NOT created", Settings.afterModel); return false; }
	UInt8* root = *(UInt8**)((UInt8*)effect + 0x18);
	AfterEffect = (UInt8*)effect;
	NodeAddRef(AfterEffect);
	AfterHeld = *(volatile LONG*)(AfterEffect + 4) > 1;
	AfterUntil = now + (ULONGLONG)(Settings.effectSeconds * 1000.0f) - 150;
	AfterController = nullptr;
	AfterRate = root ? FindRate(root, 0, OwnModel(Settings.afterModel), &AfterController) : nullptr;
	if (AfterRate) { *AfterRate = 0.0f; *(unsigned short*)(AfterController + 0x08) |= 0x8; }
	LogLine("after-fire trail %s, heat %.1f; smoke rate %s", Settings.afterModel, Heat, AfterRate ? "under control" : "NOT found");
	return true;
}

// ---- Ribbon report (test) -----------------------------------------------------------------------------------------
// Logs what the running ribbon holds, once a second, iRibbonReports times. The game's ribbon data (BSStripPSysData,
// read from BSStripPSysData::UpdatePoints 0xC26120 and NiDX9Renderer::Do_RenderParticleStrips 0xE70720): positions
// +0x20, colours +0x28, radii +0x4C, active particles +0x50 (ushort), sizes +0x54, particle info +0x70 (0x1C each: age
// +0xC, life span +0x10), ribbon per particle +0x7C (0x20 each: points +0, count +4, ring size +0x10, oldest +0x14,
// next write +0x18), max points +0x80; a point is 0x84 bytes: position +0, colour +0x6C, size +0x7C, radius +0x80.
static int RibbonReports = 0;
static ULONGLONG LastRibbonReport = 0;
static UInt8* FindStripSystem(UInt8* object, int depth) {
	if (!object || depth > 8) return nullptr;
	void** vtable = *(void***)object;
	typedef void* (__thiscall* IsFn)(void*);
	if (((IsFn)vtable[12])(object)) return strcmp(RttiName(object), "BSStripParticleSystem") ? nullptr : object;
	if (((IsFn)vtable[3])(object)) {
		UInt8** children = *(UInt8***)(object + 0x9C + 4);
		const unsigned short count = *(unsigned short*)(object + 0x9C + 0xA);
		for (unsigned short i = 0; children && i < count; i++) if (UInt8* found = FindStripSystem(children[i], depth + 1)) return found;
	}
	return nullptr;
}
static void ReportRibbon(UInt8* root, ULONGLONG now) {
	if (RibbonReports <= 0 || now - LastRibbonReport < 1000) return;
	LastRibbonReport = now;
	RibbonReports--;
	__try {
		UInt8* psys = FindStripSystem(root, 0);
		if (!psys) { LogLine("ribbon: no ribbon particle system in this effect"); return; }
		UInt8* data = nullptr;
		for (int off = 0x9C; off <= 0xD0 && !data; off += 4) {
			UInt8* p = *(UInt8**)(psys + off);
			const char* n = SafeRtti(p);
			if (n && !strcmp(n, "BSStripPSysData")) data = p;
		}
		if (!data) { LogLine("ribbon: data not found"); return; }
		const NiPoint3 muzzle = TrailNode ? *(NiPoint3*)(TrailNode + 0x8C) : NiPoint3{};
		const NiPoint3 world = *(NiPoint3*)(psys + 0x8C);
		const float* bound = *(float**)(psys + 0x20);
		const unsigned short active = *(unsigned short*)(data + 0x50), maxPoints = *(unsigned short*)(data + 0x80);
		LogLine("ribbon @%llu: %u strands (max points %u); system at (%.0f, %.0f, %.0f) scale %.2f flags %#x; muzzle (%.0f, %.0f, %.0f); bound %s",
			now % 100000, active, maxPoints, world.x, world.y, world.z, *(float*)(psys + 0x98), *(UInt32*)(psys + 0x30),
			muzzle.x, muzzle.y, muzzle.z, bound && !IsBadReadPtr(bound, 16) ? "" : "none");
		if (bound && !IsBadReadPtr(bound, 16))
			LogLine("  bound centre (%.0f, %.0f, %.0f) radius %.1f", bound[0], bound[1], bound[2], bound[3]);
		UInt8* info = *(UInt8**)(data + 0x70);
		UInt8* strips = *(UInt8**)(data + 0x7C);
		NiPoint3* positions = *(NiPoint3**)(data + 0x20);
		float* colours = *(float**)(data + 0x28);
		int totalPoints = 0;
		for (unsigned short i = 0; strips && i < active; i++) totalPoints += *(unsigned short*)(strips + i * 0x20 + 4);
		LogLine("  points in all strands: %d", totalPoints);
		for (unsigned short i = 0; strips && info && i < active && i < 4; i++) {
			UInt8* s = strips + i * 0x20;
			UInt8* points = *(UInt8**)s;
			const int count = *(unsigned short*)(s + 4), ring = *(int*)(s + 0x10), oldest = *(int*)(s + 0x14), next = *(int*)(s + 0x18);
			const float age = *(float*)(info + i * 0x1C + 0xC), life = *(float*)(info + i * 0x1C + 0x10);
			const NiPoint3 head = positions ? positions[i] : NiPoint3{};
			const float headAlpha = colours ? colours[i * 4 + 3] : -1.0f;
			LogLine("  strand %u: age %.2f of %.2f s, head (%.0f, %.0f, %.0f) alpha %.2f, %d points (ring %d, oldest %d, next %d)",
				i, age, life, head.x, head.y, head.z, headAlpha, count, ring, oldest, next);
			if (!points || ring <= 0 || count <= 0) continue;
			const int idx[3] = { oldest % ring, (oldest + count / 2) % ring, ((next - 1) % ring + ring) % ring };
			const char* label[3] = { "oldest", "middle", "newest" };
			for (int k = 0; k < 3; k++) {
				UInt8* pt = points + idx[k] * 0x84;
				const NiPoint3 at = *(NiPoint3*)pt;
				const float* c = (float*)(pt + 0x6C);
				LogLine("    %s point (%.0f, %.0f, %.0f) [%.0f above muzzle, %.0f away] width %.2f (size %.2f x radius %.2f) colour %.2f %.2f %.2f alpha %.2f",
					label[k], at.x, at.y, at.z, at.z - muzzle.z,
					sqrtf((at.x - muzzle.x) * (at.x - muzzle.x) + (at.y - muzzle.y) * (at.y - muzzle.y) + (at.z - muzzle.z) * (at.z - muzzle.z)),
					*(float*)(pt + 0x7C) * *(float*)(pt + 0x80), *(float*)(pt + 0x7C), *(float*)(pt + 0x80), c[0], c[1], c[2], c[3]);
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { LogLine("ribbon: report stopped by a fault"); }
}

// The current visible model is either the first-person model (PlayerCharacter +0x694) or the player's 3D
// (renderData +0x64 -> niNode +0x14).
static UInt8* VisibleRoot() {
	UInt8* player = (UInt8*)Player();
	if (!player) return nullptr;
	if (*(player + 0x64C)) {
		UInt8* renderData = *(UInt8**)(player + 0x64);
		return renderData ? *(UInt8**)(renderData + 0x14) : nullptr;
	}
	return *(UInt8**)(player + 0x694);
}
typedef UInt8* (__thiscall* GetFireNodeFn)(void* weapon, UInt8* root);
static const GetFireNodeFn GetFireNode = (GetFireNodeFn)0x00525700;
typedef bool(__thiscall* IsWeaponDrawnFn)(void* actor);
static const IsWeaponDrawnFn IsWeaponDrawn = (IsWeaponDrawnFn)0x008A16D0;   // Actor::IsWeaponDrawn (IsWeaponOut condition)


static void HandleShots(ULONGLONG now);   // below, with the hooks

// Actor::GetCurrentWeapon 0x008A1710 (thiscall actor) -> TESObjectWEAP (as in Actor::GetProjectileNode). TESObjectWEAP:
// form ID +0x0C, name (TESFullName at +0x30, BSString data) +0x34, weapon type +0xF4.
typedef void* (__thiscall* GetCurrentWeaponFn)(void* actor);
static const GetCurrentWeaponFn GetEquippedWeapon = (GetCurrentWeaponFn)0x008A1710;
static void* SettingsWeapon = nullptr;
static const char* const GunSections[] = { "Muzzle", "Puff", "Wisp", "Trail", "Glow", "Haze", "Blast", "Ejection" };
static bool SkippedGunKey(const char* key) {   // test switches, not meant per gun
	return !_stricmp(key, "iRibbonReports") || !_stricmp(key, "bFollowMuzzle") || !_stricmp(key, "fConstantRate") ||
		!_stricmp(key, "iFlags") || !_stricmp(key, "fFileRate");
}
static void WriteGunIni(const char* path, const char* name, UInt32 formId) {
	FILE* f = nullptr;
	if (fopen_s(&f, path, "w") || !f) { LogLine("could not create %s", path); return; }
	fprintf(f, "; GunFX settings for %s (form %08X).\n", name, formId);
	fprintf(f, "; Every line starting with ; uses the value from the main GunFX.ini (shown here as it was when this\n");
	fprintf(f, "; file was made). To give this gun its own value, delete the ; at the start of the line and change the value.\n");
	fprintf(f, "; Put the ; back to return to the main setting. Changes apply while the game runs. Delete this file to start over.\n");
	char keys[8192], value[512];
	for (const char* section : GunSections) {
		fprintf(f, "\n[%s]\n", section);
		GetPrivateProfileStringA(section, NULL, "", keys, sizeof(keys), IniPath);
		for (const char* k = keys; *k; k += strlen(k) + 1) {
			if (SkippedGunKey(k)) continue;
			GetPrivateProfileStringA(section, k, "", value, sizeof(value), IniPath);
			fprintf(f, ";%s=%s\n", k, value);
		}
	}
	fclose(f);
}
static int OwnGunValues(const char* path) {
	int own = 0;
	char keys[8192];
	for (const char* section : GunSections) {
		GetPrivateProfileStringA(section, NULL, "", keys, sizeof(keys), path);
		for (const char* k = keys; *k; k += strlen(k) + 1) own++;
	}
	return own;
}
static void UpdateGunSettings() {
	void* player = Player();
	void* weapon = player ? GetEquippedWeapon(player) : nullptr;
	if (weapon == SettingsWeapon) return;
	SettingsWeapon = weapon;
	if (weapon && *((UInt8*)weapon + 0xF4) >= 3 && *((UInt8*)weapon + 0xF4) <= 9) SwitchGun(weapon, GetTickCount64());
	WeaponIniPath[0] = 0;
	WeaponIniWritten = FILETIME{};
	const UInt8 type = weapon ? *((UInt8*)weapon + 0xF4) : 0;
	if (weapon && type >= 3 && type <= 9) {
		const UInt32 formId = *(UInt32*)((UInt8*)weapon + 0x0C);
		const char* raw = *(const char**)((UInt8*)weapon + 0x34);
		char name[96];
		if (raw && *raw) strncpy_s(name, raw, _TRUNCATE); else sprintf_s(name, "Weapon %08X", formId);
		char file[96];
		int n = 0;
		for (const char* c = name; *c && n < 80; c++) file[n++] = strchr("\\/:*?\"<>|", *c) || (unsigned char)*c < 32 ? '_' : *c;
		while (n > 0 && (file[n - 1] == ' ' || file[n - 1] == '.')) n--;
		file[n] = 0;
		if (!n) sprintf_s(file, "Weapon %08X", formId);
		char folder[MAX_PATH];
		strcpy_s(folder, IniPath);
		char* slash = strrchr(folder, 0x5C);
		if (slash) slash[1] = 0;
		strcat_s(folder, "GunFX");
		CreateDirectoryA(folder, NULL);
		strcat_s(folder, "\\Weapons");
		CreateDirectoryA(folder, NULL);
		sprintf_s(WeaponIniPath, "%s\\%s.ini", folder, file);
		if (GetFileAttributesA(WeaponIniPath) == INVALID_FILE_ATTRIBUTES) {
			char older[MAX_PATH];
			strcpy_s(older, IniPath);
			char* cut = strrchr(older, 0x5C);
			if (cut) cut[1] = 0;
			sprintf_s(older + strlen(older), MAX_PATH - strlen(older), "BarrelSmoke\\Weapons\\%s.ini", file);
			if (CopyFileA(older, WeaponIniPath, TRUE)) LogLine("took over the BarrelSmoke settings of %s", name);
			else WriteGunIni(WeaponIniPath, name, formId);
			LogLine("created per-gun settings: GunFX\\Weapons\\%s.ini", file);
		}
		WIN32_FILE_ATTRIBUTE_DATA info;
		if (GetFileAttributesExA(WeaponIniPath, GetFileExInfoStandard, &info)) WeaponIniWritten = info.ftLastWriteTime;
		LogLine("equipped %s: settings from GunFX\\Weapons\\%s.ini (%d own value(s)), the rest from GunFX.ini",
			name, file, OwnGunValues(WeaponIniPath));
	}
	LoadSettings();
}

static void MainLoop() {
	const ULONGLONG now = GetTickCount64();
	ReloadIniIfChanged(now);
	UpdateGunSettings();
	HandleShots(now);
	UpdateBursts(now);
	const float dt = LastLoop ? (now - LastLoop) / 1000.0f : 0.0f;
	LastLoop = now;
	if (!Settings.trail || !Switch(1)) ReleaseWisp(nullptr);

	const float span = Settings.heatFull - Settings.heatStart > 0.01f ? Settings.heatFull - Settings.heatStart : 0.01f;
	float t = (Heat - Settings.heatStart) / span;
	t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	float rate = Settings.maxRate * t * t * (3.0f - 2.0f * t);   // smooth start and finish
	if (Settings.constantRate > 0.0f) rate = Settings.constantRate;  // test switch: fixed rate, no heat
	// The heat smoke only runs while you fire: fStopDelay seconds after the last shot it stops (the smoke already in
	// the air drifts off) and the after-fire trail takes over. fStopDelay 0 = it keeps going until the barrel cools.
	const bool firing = Settings.wispStopDelay <= 0.0f || (LastShot && now - LastShot < (ULONGLONG)(Settings.wispStopDelay * 1000.0f));
	if (!firing && Settings.constantRate <= 0.0f) {
		rate = 0.0f;
		if (FollowEffect) ReleaseWisp("stopped firing");
	}
	// The shader rises over a sustained burst, independently of the smoke's heatFull.
	float glowT = (Heat - Settings.glowStart) / Settings.glowHeatSpan;
	glowT = glowT < 0.0f ? 0.0f : glowT > 1.0f ? 1.0f : glowT;
	glowT = glowT * glowT * (3.0f - 2.0f * glowT);

	const bool muzzleExists = TrailNode && TrailActor == Player();
	const bool drawn = muzzleExists && IsWeaponDrawn(Player());
	UInt8* root = drawn ? VisibleRoot() : nullptr;
	UInt8* liveNode = root && CurrentWeapon ? GetFireNode(CurrentWeapon, root) : nullptr;
	if (liveNode && liveNode != TrailNode) {
		NodeAddRef(liveNode);
		NodeRelease(TrailNode);
		TrailNode = liveNode;
	}
	if (liveNode) LastMuzzleSeen = now;
	if (liveNode) GlowBack = GlowDirection(liveNode);
	// An animation may briefly remove ProjectileNode; keep the held node through that gap.
	const bool muzzleAlive = drawn && TrailNode && (liveNode || now - LastMuzzleSeen < 250);
	// The game can end a temporary effect early (cell change, loading, too many effects); then it drops its reference.
	const bool dropped = FollowEffect && FollowHeld && *(volatile LONG*)(FollowEffect + 4) <= 1;
	if (FollowEffect && (!muzzleAlive || dropped || now > FollowUntil))
		ReleaseWisp(!muzzleExists ? "the muzzle left the scene" : !drawn ? "gun put away" : !muzzleAlive ? "the muzzle is not on the gun you see"
			: dropped ? "the game removed it" : "its time is up");
	if (TrailNode && !muzzleAlive) {
		NodeRelease(TrailNode);
		TrailNode = nullptr;
		TrailActor = nullptr;
	}
	if (Settings.trail && Switch(1) && rate > 0.01f && muzzleAlive) {
		// A wisp close to its end hands over to a new one, so the smoke never stops mid-stream.
		if (FollowEffect && now + (ULONGLONG)(Settings.tailSeconds * 1000.0f) > FollowUntil) ReleaseWisp("handing over to a new one");
		if (!FollowEffect) SpawnWisp(now, NodePosition(SmokeAnchor(Settings.wispNode, TrailNode), Settings.wispOffset));
	}
	// The P66 object shader draws the heat on the gun itself; skip the old free-floating glow particles.
	if (FollowEffect) {
		UInt8* root = *(UInt8**)(FollowEffect + 0x18);
		if (root && Settings.trailFollow) {
			// A slow, smooth sideways sway of the source (two mixed waves per axis); the rising smoke keeps the path
			// it was born on, so the strand snakes as it climbs.
			NiPoint3 at = NodePosition(SmokeAnchor(Settings.wispNode, TrailNode), Settings.wispOffset);
			if (Settings.sway > 0.0f) {
				const float t = (float)(now % 3600000) / 1000.0f * Settings.swaySpeed * 6.2831853f;
				at.x += Settings.sway * (sinf(t) + 0.5f * sinf(t * 2.3f + 1.0f)) / 1.5f;
				at.y += Settings.sway * (sinf(t * 0.8f + 2.0f) + 0.5f * sinf(t * 1.9f)) / 1.5f;
			}
			*(NiPoint3*)(root + 0x58) = at;
		}
		if (RateValue) { *RateValue = rate; *(unsigned short*)(RateController + 0x08) |= 0x8; }
		if (root) ReportRibbon(root, now);
		else if (rate <= 0.01f) ReleaseWisp("cooled down (no rate control)");
	}
	// After-fire trail: from fStartDelay seconds after the last shot, while the barrel is still warm. Its rate follows
	// the heat that is left (full at fFullHeat, none below fMinHeat), so it thins out as the gun cools; firing again
	// stops it at once (the smoke already in the air drifts away).
	{
		const bool resting = LastShot && now - LastShot >= (ULONGLONG)(Settings.afterDelay * 1000.0f);
		const float afterSpan = Settings.afterFullHeat - Settings.afterMinHeat > 0.01f ? Settings.afterFullHeat - Settings.afterMinHeat : 0.01f;
		float a = (Heat - Settings.afterMinHeat) / afterSpan;
		a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
		const float afterRate = resting ? Settings.afterMaxRate * a * a * (3.0f - 2.0f * a) : 0.0f;
		const bool afterOn = Settings.after && Switch(7) && muzzleAlive && GunAllowed(CurrentWeapon);
		const bool afterDropped = AfterEffect && AfterHeld && *(volatile LONG*)(AfterEffect + 4) <= 1;
		if (AfterEffect && (!afterOn || afterDropped || now > AfterUntil || !resting))
			ReleaseAfter(!afterOn ? nullptr : afterDropped ? "the game removed it" : !resting ? "firing again" : "its time is up");
		if (afterOn && afterRate > 0.01f) {
			if (AfterEffect && now + (ULONGLONG)(Settings.tailSeconds * 1000.0f) > AfterUntil) ReleaseAfter("handing over to a new one");
			if (!AfterEffect) SpawnAfter(now, NodePosition(SmokeAnchor(Settings.wispNode, TrailNode), Settings.wispOffset));
		}
		if (AfterEffect) {
			UInt8* afterRoot = *(UInt8**)(AfterEffect + 0x18);
			if (afterRoot && TrailNode) {
				NiPoint3 at = NodePosition(SmokeAnchor(Settings.wispNode, TrailNode), Settings.wispOffset);
				if (Settings.afterSway > 0.0f) {
					const float w = (float)(now % 3600000) / 1000.0f * Settings.afterSwaySpeed * 6.2831853f;
					at.x += Settings.afterSway * (sinf(w * 1.1f + 0.7f) + 0.5f * sinf(w * 2.7f)) / 1.5f;
					at.y += Settings.afterSway * (sinf(w * 0.9f + 1.3f) + 0.5f * sinf(w * 2.1f + 0.4f)) / 1.5f;
				}
				*(NiPoint3*)(afterRoot + 0x58) = at;
			}
			if (AfterRate) { *AfterRate = afterRate; *(unsigned short*)(AfterController + 0x08) |= 0x8; }
		}
	}
	if (now - LastShot >= (ULONGLONG)(Settings.coolDelay * 1000.0f)) Heat -= dt * Settings.coolPerSecond;
	if (Heat < 0.0f) Heat = 0.0f;
	// The slow footprint counts sustained fire; cooling it during a long burst
	// would make low-rate weapons unable to grow at all.
	if (now - LastShot >= 5000) SpreadHeat -= dt / Settings.glowSpreadCoolSeconds;
	if (SpreadHeat < 0.0f) SpreadHeat = 0.0f;
	if (!muzzleAlive) VisualHeat = 0.0f;
	else {
		const float step = dt < 0.05f ? dt : 0.05f;
		const float seconds = glowT > VisualHeat ? Settings.glowRiseSeconds : 2.0f;
		VisualHeat += (glowT - VisualHeat) * step / (seconds + step);
	}
	float hazeT = (Heat - Settings.hazeStart) / (Settings.hazeFull - Settings.hazeStart);
	hazeT = hazeT < 0 ? 0 : hazeT > 1 ? 1 : hazeT;
	hazeT = hazeT*hazeT*(3.0f-2.0f*hazeT);
	if (!muzzleAlive) VisualHaze = 0;
	else {
		const float step = dt < 0.05f ? dt : 0.05f;
		VisualHaze += (hazeT-VisualHaze)*step/(Settings.hazeRise+step);
	}
	PublishHeat(muzzleAlive, Settings.glowPreview ? 1.0f : VisualHeat);
}

static void MessageHandler(NVSEMessage* msg) { if (msg->type == kMessage_MainGameLoop) MainLoop(); }

// ---- Hooks -----------------------------------------------------------------------------------------------------
// Every shot of a ranged weapon goes through TESObjectWEAP::Fire, with or without a muzzle flash. The hook only notes
// the shot (one call comes from the game's task queue, possibly on another thread); the game loop handles it.
typedef void (__thiscall* FireFn)(void* weapon, void* actor);
static FireFn OriginalFire[3] = {};
static const UInt32 CallSites[3] = { 0x008BADE9, 0x0087BA71, 0x005DA5F8 };
struct PendingShot { UInt8* actor; void* weapon; bool eject; };
static PendingShot Pending[32];
static int PendingCount = 0;
static CRITICAL_SECTION PendingLock;
static void NoteShot(void* weapon, void* actor) {
	if (!weapon || !actor) return;
	EnterCriticalSection(&PendingLock);
	if (PendingCount < 32) Pending[PendingCount++] = PendingShot{ (UInt8*)actor, weapon, false };
	LeaveCriticalSection(&PendingLock);
}
// TESObjectWEAP::EjectShellCasing 0x00524DB0, thiscall (weapon, actor): the game throws out a casing. Called from
// TESObjectWEAP::Fire for automatic guns (0x00524A93, at the shot) and from the actor's animation when it reaches its
// eject point for other guns (0x008BB4C5: later than the shot on bolt-action, lever and pump guns).
static FireFn OriginalEject[2] = {};
static const UInt32 EjectSites[2] = { 0x00524A93, 0x008BB4C5 };
static void NoteEject(void* weapon, void* actor) {
	if (!weapon || !actor) return;
	EnterCriticalSection(&PendingLock);
	if (PendingCount < 32) Pending[PendingCount++] = PendingShot{ (UInt8*)actor, weapon, true };
	LeaveCriticalSection(&PendingLock);
}
static void __fastcall EjectHook0(void* weapon, void*, void* actor) { OriginalEject[0](weapon, actor); NoteEject(weapon, actor); }
static void __fastcall EjectHook1(void* weapon, void*, void* actor) { OriginalEject[1](weapon, actor); NoteEject(weapon, actor); }
static void __fastcall FireHook0(void* weapon, void*, void* actor) { OriginalFire[0](weapon, actor); NoteShot(weapon, actor); }
static void __fastcall FireHook1(void* weapon, void*, void* actor) { OriginalFire[1](weapon, actor); NoteShot(weapon, actor); }
static void __fastcall FireHook2(void* weapon, void*, void* actor) { OriginalFire[2](weapon, actor); NoteShot(weapon, actor); }
typedef UInt8* (__thiscall* GetProjectileNodeFn)(void* actor);
static const GetProjectileNodeFn GetProjectileNode = (GetProjectileNodeFn)0x008BE0A0;
// TESObjectWEAP::GetFireNode finds the weapon's fire node (its ProjectileNode) by name under a model.
static int LoggedShots = 0;

// True when a node belongs to the player's first-person model (PlayerCharacter firstPersonNiNode at +0x694;
// NiAVObject parent at +0x18).
static bool InFirstPersonModel(UInt8* node) {
	UInt8* firstPerson = *(UInt8**)((UInt8*)Player() + 0x694);
	for (int depth = 0; node && depth < 32; depth++, node = *(UInt8**)(node + 0x18)) if (node == firstPerson) return true;
	return false;
}

// A shot: smoke at the gun's ProjectileNode (for the player, in the model you see: first or third person). Guns only:
// TESObjectWEAP weapon type at +0xF4, 3..9 = pistols, rifles, automatics, energy weapons, handle weapons, launchers
// (not grenades, mines or thrown weapons).
static void OnShot(UInt8* actor, void* weapon, ULONGLONG now) {
	const UInt8 type = *((UInt8*)weapon + 0xF4);
	if (Settings.puffPlayerOnly && actor != Player()) return;
	if (!GunAllowed(weapon)) return;   // energy weapon with the EnergyWeapons switch off: no smoke, no heat
	if (type < 3 || type > 9) {
		static void* loggedSkipped[16] = {};
		static int skipped = 0;
		bool seen = false;
		for (int i = 0; i < skipped; i++) seen |= loggedSkipped[i] == weapon;
		if (!seen && skipped < 16) { loggedSkipped[skipped++] = weapon; LogLine("shot with weapon type %u: not a gun, no smoke", type); }
		return;
	}
	// The player: the gun's ProjectileNode in the model you see (the game's own answer is the third-person one, also
	// in first person). Others: the game's projectile node.
	UInt8* node = actor == Player() ? (VisibleRoot() ? GetFireNode(weapon, VisibleRoot()) : nullptr) : GetProjectileNode(actor);
	static void* loggedWeapons[64] = {};
	bool seen = false;
	for (int i = 0; i < LoggedShots; i++) seen |= loggedWeapons[i] == weapon;
	if (!seen && LoggedShots < 64) {
		if (actor == Player() && node && Settings.logWeaponNodes) {
			int budget = 64;
			UInt8* parent = *(UInt8**)(node + 0x18);
			LogNodeNames(parent ? parent : node, budget);
		}
		loggedWeapons[LoggedShots++] = weapon;
		const char* name = node ? *(const char**)(node + 0x08) : nullptr;
		LogLine("first shot of a gun: weapon type %u%s, projectile node %s%s", type, (*((UInt8*)weapon + 0x100) & 0x2) ? " automatic" : "",
			node ? (name ? name : "(no name)") : "NOT found (no smoke)",
			!node || actor != Player() ? "" : InFirstPersonModel(node) ? " (first-person model)" : " (third-person model)");
	}
	if (!node) return;
	MuzzleFlash shot = {};
	shot.node = node;
	shot.sourceActor = actor;
	shot.sourceWeap = weapon;
	if (actor == Player()) {
		OnPlayerShot(&shot, now);
		MuzzleFlash smoke = shot;
		smoke.node = SmokeAnchor(Settings.puffNode, node);
		// Puff() handles the configured local offset.
		Puff(&smoke, now);
	} else Puff(&shot, now);
}

// A casing thrown out by the game: a small puff of smoke at the ejection port of the gun you see. The first ejections
// and every failure are written to the log (with the position next to the muzzle's, for comparison).
static int LoggedEjects = 0, LoggedEjectsView[2] = {};
// The game's own lookup (as in TESObjectWEAP::EjectShellCasing): BSUtilities::GetObjectByName (cdecl root, name) on
// PlayerCharacter::GetCurrentNode, the model in use (first or third person). The game passes its literal
// "ShellCasingNode" at 0x0102CB00; another name from the INI is passed as written.
typedef UInt8* (__cdecl* GetObjectByNameFn)(UInt8* root, const char* name);
static const GetObjectByNameFn GetObjectByName = (GetObjectByNameFn)0x004AAE30;
typedef UInt8* (__thiscall* GetCurrentNodeFn)(void* player);
static const GetCurrentNodeFn GetCurrentNode = (GetCurrentNodeFn)0x00950BE0;
static void OnEject(UInt8* actor, void* weapon, ULONGLONG now) {
	if (!Settings.eject || !Switch(2) || actor != Player() || !GunAllowed(weapon)) return;
	const UInt8 type = *((UInt8*)weapon + 0xF4);
	if (type < 3 || type > 9) return;
	static ULONGLONG lastEject = 0;
	if (now - lastEject < (ULONGLONG)(Settings.ejectInterval * 1000)) return;
	const char* name = !_stricmp(Settings.ejectNode, "ShellCasingNode") ? (const char*)0x0102CB00 : Settings.ejectNode;
	char hashed[136];
	sprintf_s(hashed, "##%s", Settings.ejectNode);
	UInt8* root = VisibleRoot();
	UInt8* port = root ? GetObjectByName(root, name) : nullptr;
	if (!port && root) port = GetObjectByName(root, hashed);
	if (!port) port = FindNamedNode(root, Settings.ejectNode);
	if (!port) {
		UInt8* current = GetCurrentNode(Player());
		if (current && current != root) { root = current; port = GetObjectByName(root, name); }
	}
	if (!port) {
		if (LoggedEjects < 40) { LoggedEjects++; LogLine("ejection: node %s NOT found on the %s model (no smoke)", Settings.ejectNode,
			*((UInt8*)Player() + 0x64C) ? "third-person" : "first-person"); }
		return;
	}
	lastEject = now;
	void* cell = *(void**)(actor + 0x40);
	const NiPoint3 at = WorldPoint(port, NodePosition(port, Settings.ejectOffset));
	// A hot gun vents more smoke: the burst grows from fScale (cold) to fScale x fHotScale at full barrel heat.
	float hot = Heat / (Settings.heatFull > 0.01f ? Settings.heatFull : 0.01f);
	hot = hot < 0.0f ? 0.0f : hot > 1.0f ? 1.0f : hot;
	const float scale = Settings.ejectScale * (1.0f + (Settings.ejectHotScale - 1.0f) * hot);
	void* effect = cell ? SpawnBurst(cell, Settings.ejectLifetime, Settings.ejectModel, at, scale, Settings.ejectBurst, now) : nullptr;
	const int view = InFirstPersonModel(port) ? 0 : 1;
	if (LoggedEjectsView[view] < 8 || !effect) {
		LoggedEjectsView[view]++;
		UInt8* muzzle = root ? GetFireNode(weapon, root) : nullptr;
		const NiPoint3 m = muzzle ? *(NiPoint3*)(muzzle + 0x8C) : NiPoint3{};
		LogLine("ejection smoke at (%.1f, %.1f, %.1f) on the %s model, %.1f units from the muzzle; %s, scale %.2f, %.1f s -> %s",
			at.x, at.y, at.z, InFirstPersonModel(port) ? "first-person" : "third-person",
			muzzle ? sqrtf((at.x - m.x) * (at.x - m.x) + (at.y - m.y) * (at.y - m.y) + (at.z - m.z) * (at.z - m.z)) : -1.0f,
			Settings.ejectModel, scale, Settings.ejectLifetime, effect ? "created" : (cell ? "NOT created" : "no cell"));
	}
}
static void HandleShots(ULONGLONG now) {
	PendingShot shots[32];
	EnterCriticalSection(&PendingLock);
	const int count = PendingCount;
	memcpy(shots, Pending, count * sizeof(PendingShot));
	PendingCount = 0;
	LeaveCriticalSection(&PendingLock);
	for (int i = 0; i < count; i++) {
		if (shots[i].eject) OnEject(shots[i].actor, shots[i].weapon, now);
		else OnShot(shots[i].actor, shots[i].weapon, now);
	}
}

// Replaces the call at a call site, keeping whatever it called before (another plugin's hook included).
static bool HookCall(UInt32 site, void* hook, FireFn* original) {
	UInt8* p = (UInt8*)site;
	if (p[0] != 0xE8) { LogLine("call site %08X is not a call (byte %02X); skipped", site, p[0]); return false; }
	*original = (FireFn)(site + 5 + *(int*)(p + 1));
	DWORD old;
	VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
	*(int*)(p + 1) = (int)((UInt32)hook - (site + 5));
	VirtualProtect(p, 5, old, &old);
	LogLine("hooked the weapon fire call at %08X (was %08X)", site, (UInt32)*original);
	return true;
}

// ---- Settings --------------------------------------------------------------------------------------------------
// ---- Per-gun settings ------------------------------------------------------------------------------------------
// Each gun gets Data\NVSE\Plugins\GunFX\Weapons\<gun name>.ini the first time it is equipped. Its lines start
// with ';' (= use the main GunFX.ini); a line without ';' gives that gun its own value. While a gun is equipped,
// every setting is read from its file first, then from the main INI. [Main] always comes from the main INI.
static UINT LayerInt(const char* section, const char* key, INT fallback, const char* path) {
	if (WeaponIniPath[0] && path == IniPath && _stricmp(section, "Main")) {
		char value[64];
		GetPrivateProfileStringA(section, key, "\x01", value, sizeof(value), WeaponIniPath);
		if (value[0] != 1) return GetPrivateProfileIntA(section, key, fallback, WeaponIniPath);
	}
	return GetPrivateProfileIntA(section, key, fallback, path);
}
static DWORD LayerStr(const char* section, const char* key, const char* fallback, char* out, DWORD size, const char* path) {
	if (WeaponIniPath[0] && path == IniPath && key && _stricmp(section, "Main")) {
		const DWORD n = GetPrivateProfileStringA(section, key, "\x01", out, size, WeaponIniPath);
		if (out[0] != 1) return n;
	}
	return GetPrivateProfileStringA(section, key, fallback, out, size, path);
}

static float IniFloat(const char* section, const char* key, const char* fallback) {
	char buffer[64];
	LayerStr(section, key, fallback, buffer, sizeof(buffer), IniPath);
	return (float)atof(buffer);
}
static void IniModel(const char* section, const char* key, const char* fallback, char* out) {
	LayerStr(section, key, fallback, out, MAX_PATH, IniPath);
	for (char* c = out; *c; c++) if (*c == '/') *c = 0x5C; // the game's archives expect backslashes
	if (!_strnicmp(out, "BarrelSmoke\\", 12)) {      // older setting: the effects now live in Meshes\GunFX
		char renamed[MAX_PATH];
		sprintf_s(renamed, "GunFX\\%s", out + 12);
		strcpy_s(out, MAX_PATH, renamed);
	}
}

static void ReadLook(const char* section, const Look& file, Look& look) {
	char fallback[32];
	auto get = [&](const char* key, float value) { sprintf_s(fallback, "%g", value); return IniFloat(section, key, fallback); };
	look = file;
	look.strength = -get("fRise", -file.strength);            // upward push (positive rises)
	look.turbulence = get("fCurl", file.turbulence);
	look.turbulenceScale = get("fCurlScale", file.turbulenceScale);
	look.radius = get("fSize", file.radius);
	look.radiusVar = get("fSizeVariation", file.radiusVar);
	look.life = get("fSmokeLife", file.life);
	look.lifeVar = get("fSmokeLifeVariation", file.lifeVar);
	look.grow = get("fGrowSeconds", file.grow);
	look.fade = get("fShrinkSeconds", file.fade);
	look.opacity = get("fOpacity", file.opacity);
	look.percents[3] = get("fFadeIn", file.percents[3]);       // share of its life when a puff is fully visible
	if (look.percents[2] > look.percents[3]) look.percents[2] = look.percents[3];
	look.percents[4] = get("fFadeStart", file.percents[4]);    // share of its life when a puff starts fading out
	look.percents[5] = get("fFadeEnd", file.percents[5]);      // ...and is fully gone
}

// ---- Smoke look from the INI, baked into a copy of the effect file ------------------------------------------------
// Writing into live particle modifiers crashed the game, so the look is never changed at run time. Instead, when the
// settings load, the effect file is copied to Data\Meshes\GunFX\Generated\<name>_<hash>.nif with the INI's look values
// written into it, and that copy is spawned. The hash covers every value, so each look gets its own file and the
// game's model cache never hands back an older one. Keys that are missing keep the file's own values; when nothing
// differs, the original file is used. NIF 20.2.0.7 layout (as in tools\niftweak.py): emitter radius/variation/life/
// variation at +53/+57/+61/+65 of the block; grow/fade modifier grow +13, fade +19; gravity strength +33, turbulence
// +41, turbulence scale +45; simple colour modifier six shares of life at +13, middle colour's alpha at +65.
struct SmokeLook { float size, sizeVar, life, lifeVar, grow, shrink, rise, curl, curlScale, opacity, fadeIn, fadeStart, fadeEnd; };
static bool ReadWholeFile(const char* path, std::vector<UInt8>& out) {
	FILE* f = nullptr;
	if (fopen_s(&f, path, "rb") || !f) return false;
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(size > 0 ? size : 0);
	const bool ok = size > 0 && fread(out.data(), 1, size, f) == (size_t)size;
	fclose(f);
	return ok;
}
// Block offsets and type names of a NIF file. False when the layout is not the expected one.
static bool NifBlocks(const std::vector<UInt8>& d, std::vector<std::string>& types, std::vector<size_t>& at, std::vector<UInt32>& sizes) {
	size_t p = 0;
	while (p < d.size() && d[p] != '\n') p++;
	p++;
	auto need = [&](size_t n) { return p + n <= d.size(); };
	if (!need(4 + 1 + 4 + 4 + 4)) return false;
	p += 4 + 1;
	p += 4;                                   // user version
	const UInt32 blocks = *(const UInt32*)&d[p]; p += 4;
	p += 4;                                   // user version 2
	for (int i = 0; i < 3; i++) { if (!need(1)) return false; p += 1 + d[p]; }
	if (!need(2)) return false;
	const unsigned short typeCount = *(const unsigned short*)&d[p]; p += 2;
	std::vector<std::string> names;
	for (unsigned short i = 0; i < typeCount; i++) {
		if (!need(4)) return false;
		const UInt32 n = *(const UInt32*)&d[p]; p += 4;
		if (!need(n)) return false;
		names.emplace_back((const char*)&d[p], n); p += n;
	}
	if (blocks > 4096 || !need(blocks * 6)) return false;
	std::vector<unsigned short> index(blocks);
	for (UInt32 i = 0; i < blocks; i++) { index[i] = *(const unsigned short*)&d[p]; p += 2; }
	sizes.resize(blocks);
	for (UInt32 i = 0; i < blocks; i++) { sizes[i] = *(const UInt32*)&d[p]; p += 4; }
	if (!need(8)) return false;
	const UInt32 strings = *(const UInt32*)&d[p]; p += 8;
	for (UInt32 i = 0; i < strings; i++) {
		if (!need(4)) return false;
		const UInt32 n = *(const UInt32*)&d[p]; p += 4 + n;
	}
	if (!need(4)) return false;
	const UInt32 groups = *(const UInt32*)&d[p]; p += 4 + 4 * groups;
	types.clear(); at.clear();
	for (UInt32 i = 0; i < blocks; i++) {
		if (index[i] >= names.size() || p + sizes[i] > d.size()) return false;
		types.push_back(names[index[i]]);
		at.push_back(p);
		p += sizes[i];
	}
	return true;
}
static float& NifFloat(std::vector<UInt8>& d, size_t at) { return *(float*)&d[at]; }
// Reads (get=true) or writes the look of every particle system in the file. Returns how many parts were found.
static int NifLook(std::vector<UInt8>& d, SmokeLook& look, bool get) {
	std::vector<std::string> types; std::vector<size_t> at; std::vector<UInt32> sizes;
	if (!NifBlocks(d, types, at, sizes)) return 0;
	int parts = 0;
	auto field = [&](size_t offset, float& value) { if (get) value = NifFloat(d, offset); else NifFloat(d, offset) = value; };
	for (size_t i = 0; i < types.size(); i++) {
		const std::string& t = types[i];
		const size_t q = at[i];
		if ((t == "NiPSysCylinderEmitter" || t == "NiPSysSphereEmitter" || t == "NiPSysBoxEmitter") && sizes[i] >= 69) {
			field(q + 53, look.size); field(q + 57, look.sizeVar); field(q + 61, look.life); field(q + 65, look.lifeVar); parts++;
		}
		else if (t == "NiPSysGrowFadeModifier" && sizes[i] >= 23) { field(q + 13, look.grow); field(q + 19, look.shrink); parts++; }
		else if (t == "NiPSysGravityModifier" && sizes[i] >= 49) {
			float strength = -look.rise;
			field(q + 33, strength);
			if (get) look.rise = -strength;
			field(q + 41, look.curl); field(q + 45, look.curlScale); parts++;
		}
		else if (t == "BSPSysSimpleColorModifier" && sizes[i] >= 85) {
			float shares[6];
			for (int k = 0; k < 6; k++) shares[k] = NifFloat(d, q + 13 + 4 * k);
			if (get) { look.fadeIn = shares[3]; look.fadeStart = shares[4]; look.fadeEnd = shares[5]; look.opacity = NifFloat(d, q + 65); }
			else {
				NifFloat(d, q + 13 + 4 * 3) = look.fadeIn;
				if (shares[2] > look.fadeIn) NifFloat(d, q + 13 + 4 * 2) = look.fadeIn;   // colour 1 must end before colour 2 starts
				NifFloat(d, q + 13 + 4 * 4) = look.fadeStart;
				NifFloat(d, q + 13 + 4 * 5) = look.fadeEnd;
				NifFloat(d, q + 65) = look.opacity;
			}
			parts++;
		}
	}
	return parts;
}
// The model for a section: the file itself, or a generated copy with the INI's look. `model` is updated in place.
static void BakeLook(const char* section, char* model) {
	char data[MAX_PATH];                                       // ...\Data\ from ...\Data\NVSE\Plugins\GunFX.ini
	strcpy_s(data, IniPath);
	for (int up = 0; up < 3; up++) { char* cut = strrchr(data, 0x5C); if (cut) *cut = 0; }
	char source[MAX_PATH];
	sprintf_s(source, "%s\\Meshes\\%s", data, model);
	std::vector<UInt8> file;
	if (!ReadWholeFile(source, file)) { LogLine("%s look: %s is not a loose file, its own look is used", section, model); return; }
	SmokeLook built = {};
	if (NifLook(file, built, true) < 4) { LogLine("%s look: %s has an unexpected layout, its own look is used", section, model); return; }
	SmokeLook look = built;
	char fallback[32];
	auto get = [&](const char* key, float value) { sprintf_s(fallback, "%g", value); return IniFloat(section, key, fallback); };
	look.size = get("fSize", built.size);              look.sizeVar = get("fSizeVariation", built.sizeVar);
	look.life = get("fSmokeLife", built.life);         look.lifeVar = get("fSmokeLifeVariation", built.lifeVar);
	look.grow = get("fGrowSeconds", built.grow);       look.shrink = get("fShrinkSeconds", built.shrink);
	look.rise = get("fRise", built.rise);              look.curl = get("fCurl", built.curl);
	look.curlScale = get("fCurlScale", built.curlScale);
	look.opacity = get("fOpacity", built.opacity);     look.fadeIn = get("fFadeIn", built.fadeIn);
	look.fadeStart = get("fFadeStart", built.fadeStart); look.fadeEnd = get("fFadeEnd", built.fadeEnd);
	const float* a = (const float*)&built; const float* b = (const float*)&look;
	int changed = 0;
	for (size_t k = 0; k < sizeof(SmokeLook) / sizeof(float); k++) changed += fabsf(a[k] - b[k]) > 1e-5f;
	if (!changed) { LogLine("%s look: %s as built", section, model); return; }
	UInt32 hash = 2166136261u;                                 // FNV-1a over the model name and the values
	for (const char* c = model; *c; c++) hash = (hash ^ (UInt8)tolower(*c)) * 16777619u;
	for (size_t k = 0; k < sizeof(SmokeLook); k++) hash = (hash ^ ((const UInt8*)&look)[k]) * 16777619u;
	char base[MAX_PATH];
	const char* slash = strrchr(model, 0x5C);
	strcpy_s(base, slash ? slash + 1 : model);
	if (char* dot = strrchr(base, '.')) *dot = 0;
	char generated[MAX_PATH], folder[MAX_PATH], target[MAX_PATH];
	sprintf_s(generated, "GunFX\\Generated\\%s_%08x.nif", base, hash);
	sprintf_s(folder, "%s\\Meshes\\GunFX\\Generated", data);
	sprintf_s(target, "%s\\Meshes\\%s", data, generated);
	if (GetFileAttributesA(target) == INVALID_FILE_ATTRIBUTES) {
		NifLook(file, look, false);
		CreateDirectoryA(folder, NULL);
		FILE* f = nullptr;
		if (fopen_s(&f, target, "wb") || !f) { LogLine("%s look: could not write %s, its own look is used", section, generated); return; }
		fwrite(file.data(), 1, file.size(), f);
		fclose(f);
	}
	LogLine("%s look: %d value(s) from the settings -> %s", section, changed, generated);
	strcpy_s(model, MAX_PATH, generated);
}

static void LoadSettings() {
	GetModuleFileNameA((HMODULE)&__ImageBase, IniPath, MAX_PATH);
	char* slash = strrchr(IniPath, 0x5C);
	strcpy_s(slash ? slash + 1 : IniPath, MAX_PATH - (slash ? (slash + 1 - IniPath) : 0), "GunFX.ini");

	char defaults[MAX_PATH];
	sprintf_s(defaults, "%s.defaults", IniPath);
	int migrated = MergeIniDefaults(defaults, IniPath);
	if (migrated > 0) LogLine("bundled INI defaults applied (created file or added %d missing keys)", migrated);
	if (migrated < 0) LogLine("INI defaults migration incomplete; check file permissions or configuration size");
	Settings.puff = LayerInt("Puff", "bEnabled", 1, IniPath);
	IniModel("Puff", "sModel", "GunFX/barrelpuff.nif", Settings.puffModel);
	Settings.puffLifetime = IniFloat("Puff", "fLifetime", "1.8");
	Settings.puffScale = IniFloat("Puff", "fScale", "1.0");
	Settings.puffInterval = IniFloat("Puff", "fMinInterval", "0.05");
	Settings.puffPlayerOnly = LayerInt("Puff", "bPlayerOnly", 1, IniPath);

	Settings.trail = LayerInt("Wisp", "bEnabled", 1, IniPath);
	IniModel("Wisp", "sModel", "GunFX/barrelstrand.nif", Settings.trailModel);
	Settings.trailScale = IniFloat("Wisp", "fScale", "2.7");
	Settings.heatPerShot = IniFloat("Wisp", "fHeatPerShot", "0.5");
	Settings.coolPerSecond = IniFloat("Wisp", "fCoolPerSecond", "3.0");
	Settings.coolDelay = IniFloat("Wisp", "fCoolDelay", "1.5");
	Settings.wispStopDelay = IniFloat("Wisp", "fStopDelay", "0.5");
	Settings.maxHeat = IniFloat("Wisp", "fMaxHeat", "15");
	Settings.heatStart = IniFloat("Wisp", "fHeatStart", "3");
	Settings.heatFull = IniFloat("Wisp", "fHeatFull", "12");
	Settings.maxRate = IniFloat("Wisp", "fMaxRate", "150");
	Settings.fileRate = IniFloat("Wisp", "fFileRate", "20");
	{
		// A strand model carries its own build numbers (the INI's fFileRate is for the other models).
		char lower[MAX_PATH];
		strcpy_s(lower, Settings.trailModel);
		_strlwr_s(lower);
		const bool strand = strstr(lower, "barrelstrand") != nullptr;
		WispBuilt = strand ? &StrandFile : &WispFile;
		if (strand) Settings.fileRate = 40.0f;
	}
	Settings.sway = IniFloat("Wisp", "fSway", "1");
	Settings.swaySpeed = IniFloat("Wisp", "fSwaySpeed", "0.1");
	Settings.effectSeconds = IniFloat("Wisp", "fEffectSeconds", "30");
	Settings.tailSeconds = IniFloat("Wisp", "fTailSeconds", "3.5");
	// Flag 4 makes the game take the lifetime from the model's own animation.
	Settings.trailFlags = LayerInt("Wisp", "iFlags", 3, IniPath);
	Settings.trailSmokeOnly = LayerInt("Wisp", "bSmokeOnly", 0, IniPath);
	Settings.trailFollow = LayerInt("Wisp", "bFollowMuzzle", 1, IniPath);      // test switch
	Settings.constantRate = IniFloat("Wisp", "fConstantRate", "0");                       // test switch
	RibbonReports = LayerInt("Wisp", "iRibbonReports", 0, IniPath);          // test switch
	Settings.glow = LayerInt("Glow", "bEnabled", 1, IniPath);
	Settings.glowPreview = LayerInt("Glow", "bPreview", 0, IniPath);
	Settings.glowScale = IniFloat("Glow", "fScale", "1.3");
	Settings.glowLength = IniFloat("Glow", "fLength", "9");
	if (Settings.glowLength < 0.0f) Settings.glowLength = 0.0f;
	if (Settings.glowLength > 40.0f) Settings.glowLength = 40.0f;
	Settings.glowStart = IniFloat("Glow", "fStartHeat", "1.0");
	Settings.glowHeatSpan = IniFloat("Glow", "fHeatSpan", "12.0");
	if (!(Settings.glowHeatSpan >= 1.0f && Settings.glowHeatSpan <= 30.0f)) Settings.glowHeatSpan = 12.0f;
	Settings.glowRiseSeconds = IniFloat("Glow", "fRiseSeconds", "2.8");
	if (!(Settings.glowRiseSeconds >= 0.1f && Settings.glowRiseSeconds <= 10.0f)) Settings.glowRiseSeconds = 2.8f;
	Settings.glowRadius = IniFloat("Glow", "fRadius", "3.2");
	if (!(Settings.glowRadius >= 1.0f && Settings.glowRadius <= 20.0f)) Settings.glowRadius = 3.2f;
	Settings.glowForwardLength = IniFloat("Glow", "fForwardLength", "7.0");
	if (!(Settings.glowForwardLength >= 1.0f && Settings.glowForwardLength <= 30.0f)) Settings.glowForwardLength = 7.0f;
	Settings.glowMinLength = IniFloat("Glow", "fMinLength", "2.5");
	if (!(Settings.glowMinLength >= 0.0f && Settings.glowMinLength <= Settings.glowLength)) Settings.glowMinLength = Settings.glowLength < 2.5f ? Settings.glowLength : 2.5f;
	Settings.glowSpreadShots = IniFloat("Glow", "fSpreadShots", "1000.0");
	if (!(Settings.glowSpreadShots >= 1.0f && Settings.glowSpreadShots <= 100000.0f)) Settings.glowSpreadShots = 1000.0f;
	Settings.glowSpreadCoolSeconds = IniFloat("Glow", "fSpreadCoolSeconds", "900.0");
	if (!(Settings.glowSpreadCoolSeconds >= 1.0f && Settings.glowSpreadCoolSeconds <= 86400.0f)) Settings.glowSpreadCoolSeconds = 900.0f;
	Settings.glowOffsetX = IniFloat("Glow", "fOffsetX", "0.0");
	Settings.glowOffsetY = IniFloat("Glow", "fOffsetY", "0.0");
	Settings.glowOffsetZ = IniFloat("Glow", "fOffsetZ", "0.0");
	if (!(Settings.glowOffsetX >= -30.0f && Settings.glowOffsetX <= 30.0f)) Settings.glowOffsetX = 0.0f;
	if (!(Settings.glowOffsetY >= -30.0f && Settings.glowOffsetY <= 30.0f)) Settings.glowOffsetY = 0.0f;
	if (!(Settings.glowOffsetZ >= -30.0f && Settings.glowOffsetZ <= 30.0f)) Settings.glowOffsetZ = 0.0f;

	LayerStr("Puff", "sNode", "", Settings.puffNode, sizeof(Settings.puffNode), IniPath);
	LayerStr("Wisp", "sNode", "", Settings.wispNode, sizeof(Settings.wispNode), IniPath);
	LayerStr("Ejection", "sNode", "ShellCasingNode", Settings.ejectNode, sizeof(Settings.ejectNode), IniPath);
	auto offset = [](const char* section) {
		NiPoint3 v = {IniFloat(section,"fOffsetX","0"), IniFloat(section,"fOffsetY","0"), IniFloat(section,"fOffsetZ","0")};
		if (!std::isfinite(v.x) || fabsf(v.x)>100) v.x=0;
		if (!std::isfinite(v.y) || fabsf(v.y)>100) v.y=0;
		if (!std::isfinite(v.z) || fabsf(v.z)>100) v.z=0;
		return v;
	};
	// One offset for everything at the barrel tip (puff, heat smoke, after-fire trail), so they always line up.
	Settings.puffOffset=offset("Muzzle"); Settings.wispOffset=Settings.puffOffset; Settings.ejectOffset=offset("Ejection");
	auto bounded = [](const char* section, const char* key, const char* fallback, float low, float high) {
		float v=IniFloat(section,key,fallback);
		return std::isfinite(v) && v>=low && v<=high ? v : (float)atof(fallback);
	};
	Settings.logWeaponNodes=LayerInt("Main","bLogWeaponNodes",1,IniPath);
	Settings.eject = LayerInt("Ejection","bEnabled",1,IniPath);
	IniModel("Ejection","sModel","GunFX/barrelport.nif",Settings.ejectModel);
	Settings.after = LayerInt("Trail", "bEnabled", 1, IniPath);
	IniModel("Trail", "sModel", "GunFX/barreltrail.nif", Settings.afterModel);
	Settings.afterScale = IniFloat("Trail", "fScale", "2.0");
	Settings.afterDelay = IniFloat("Trail", "fStartDelay", "0.5");
	Settings.afterMinHeat = IniFloat("Trail", "fMinHeat", "2");
	Settings.afterFullHeat = IniFloat("Trail", "fFullHeat", "10");
	Settings.afterMaxRate = IniFloat("Trail", "fMaxRate", "60");
	Settings.afterSway = IniFloat("Trail", "fSway", "1.5");
	Settings.afterSwaySpeed = IniFloat("Trail", "fSwaySpeed", "0.15");
	BakeLook("Trail", Settings.afterModel);
	BakeLook("Puff", Settings.puffModel);
	BakeLook("Wisp", Settings.trailModel);
	BakeLook("Ejection", Settings.ejectModel);
	// A changed look applies at once: the running heat smoke / trail hands over to a new effect with it (the smoke
	// already in the air drifts off). Each keeps its own look; they never share settings.
	static char lastWisp[MAX_PATH] = "", lastAfter[MAX_PATH] = "";
	if (lastWisp[0] && _stricmp(lastWisp, Settings.trailModel)) ReleaseWisp("its look changed");
	if (lastAfter[0] && _stricmp(lastAfter, Settings.afterModel)) ReleaseAfter("its look changed");
	strcpy_s(lastWisp, Settings.trailModel);
	strcpy_s(lastAfter, Settings.afterModel);
	Settings.ejectScale=bounded("Ejection","fScale","1.0",0.01f,10);
	Settings.ejectLifetime=bounded("Ejection","fLifetime","1.5",0.1f,5);
	Settings.ejectHotScale=bounded("Ejection","fHotScale","1.8",0.1f,10);
	Settings.ejectBurst=bounded("Ejection","fBurstSeconds","0.06",0.01f,2);
	Settings.puffBurst=bounded("Puff","fBurstSeconds","0.12",0.01f,2);
	Settings.ejectInterval=bounded("Ejection","fMinInterval","0",0,2);
	Settings.haze=LayerInt("Haze","bEnabled",1,IniPath);
	Settings.hazePreview=LayerInt("Haze","bPreview",0,IniPath);
	Settings.hazeMask=LayerInt("Haze","bShowMask",0,IniPath);
	Settings.blast=LayerInt("Blast","bEnabled",1,IniPath);
	Settings.blastPixels=bounded("Blast","fStrengthPixels","6",0,30);
	Settings.blastRadius=bounded("Blast","fRadiusPixels","70",5,400);
	Settings.blastSeconds=bounded("Blast","fSeconds","0.06",0.01f,1);
	Settings.hazeStart=bounded("Haze","fStartHeat","3",0,100);
	Settings.hazeFull=bounded("Haze","fFullHeat","12",0.1f,100);
	if (Settings.hazeFull <= Settings.hazeStart) Settings.hazeFull=Settings.hazeStart+1;
	Settings.hazeRise=bounded("Haze","fRiseSeconds","1.5",0.05f,20);
	Settings.hazePixels=bounded("Haze","fStrengthPixels","3",0,8);
	Settings.hazeHeight=bounded("Haze","fHeightPixels","90",1,200);
	Settings.hazeWidth=bounded("Haze","fWidthPixels","20",1,100);
	Settings.hazeSpeed=bounded("Haze","fSpeed","1",0.01f,10);
	Settings.hazeLength=bounded("Haze","fLength","8",0.1f,40);
	Settings.hazeMinSize=bounded("Haze","fMinSize","0.35",0,1);
	LogLine("haze %s: %.2f pixels; ejection %s: node %s; smoke anchors puff '%s', wisp '%s'", Settings.haze?"on":"off",
		Settings.hazePixels,Settings.eject?"on":"off",Settings.ejectNode,Settings.puffNode,Settings.wispNode);
	Settings.flags = LayerInt("Main", "iFlags", 3, IniPath);
	ReadLook("Wisp", *WispBuilt, WispLook);
	ReadLook("Puff", PuffFile, PuffLook);
	Settings.logEvents = LayerInt("Main", "iLogEvents", 20, IniPath);

	LogLine("puff: %s, model %s, %.2f s, scale %.2f, every %.2f s at most, player only %d", Settings.puff ? "on" : "off",
		Settings.puffModel, Settings.puffLifetime, Settings.puffScale, Settings.puffInterval, Settings.puffPlayerOnly);
	LogLine("wisp: %s, model %s, scale %.2f, heat +%.2f per shot, cools %.2f/s, max %.1f; smoke from heat %.1f, full (%.1f/s) at %.1f",
		Settings.trail ? "on" : "off", Settings.trailModel, Settings.trailScale, Settings.heatPerShot, Settings.coolPerSecond,
		Settings.maxHeat, Settings.heatStart, Settings.maxRate, Settings.heatFull);
	LogLine("barrel shader heat: %s, preview %s, length %.1f..%.1f, radius %.1f, forward %.1f, offset (%.1f, %.1f, %.1f), spread %.0f shots, cool %.0f s; starts at heat %.1f, full over %.1f heat, rise %.1f s", Settings.glow ? "on" : "off", Settings.glowPreview ? "on" : "off",
		Settings.glowMinLength, Settings.glowLength, Settings.glowRadius, Settings.glowForwardLength,
		Settings.glowOffsetX, Settings.glowOffsetY, Settings.glowOffsetZ,
		Settings.glowSpreadShots, Settings.glowSpreadCoolSeconds, Settings.glowStart, Settings.glowHeatSpan, Settings.glowRiseSeconds);
}

static bool ReadHeatForNVR(float* out, const void* weaponNode, bool extended) {
	if (!out || !weaponNode || !HeatLockReady) return false;
	EnterCriticalSection(&HeatLock);
	UInt8* node = HeatNode;
	NodeAddRef(node);
	const float strength = HeatStrength, span = HeatLength;
	const float radius = HeatRadius, forward = HeatForwardLength;
	const float offsetX = HeatOffsetX, offsetY = HeatOffsetY, offsetZ = HeatOffsetZ;
	LeaveCriticalSection(&HeatLock);
	if (!node) return false;
	bool belongs = false;
	UInt8* parent = node;
	for (int depth = 0; parent && depth < 32; depth++, parent = *(UInt8**)(parent + 0x18)) {
		if (parent == weaponNode) { belongs = true; break; }
	}
	if (belongs) {
		NiPoint3 muzzle = *(NiPoint3*)(node + 0x8C);
		if (extended) {
			// Offset in ProjectileNode's own axes so it follows the gun's animation.
			const float* rot = (const float*)(node + 0x68);
			muzzle.x += rot[0] * offsetX + rot[1] * offsetY + rot[2] * offsetZ;
			muzzle.y += rot[3] * offsetX + rot[4] * offsetY + rot[5] * offsetZ;
			muzzle.z += rot[6] * offsetX + rot[7] * offsetY + rot[8] * offsetZ;
		}
		const NiPoint3 back = GlowDirection(node);
		out[0] = muzzle.x; out[1] = muzzle.y; out[2] = muzzle.z; out[3] = strength;
		out[4] = back.x; out[5] = back.y; out[6] = back.z; out[7] = span;
		if (extended) { out[8] = radius; out[9] = forward; }
	}
	NodeRelease(node);
	return belongs;
}

extern "C" {
__declspec(dllexport) bool __cdecl GunFX_GetHeatV2(float out[8], const void* weaponNode) {
	return ReadHeatForNVR(out, weaponNode, false);
}

__declspec(dllexport) bool __cdecl GunFX_GetHeatV3(float out[10], const void* weaponNode) {
	return ReadHeatForNVR(out, weaponNode, true);
}

__declspec(dllexport) bool __cdecl GunFX_GetHaze(float out[8], const void* weaponNode) {
	float heat[10];
	if (!out || !ReadHeatForNVR(heat, weaponNode, true)) return false;
	EnterCriticalSection(&HeatLock);
	memcpy(out, HazePublished, sizeof(HazePublished));
	LeaveCriticalSection(&HeatLock);
	return out[0] > 0.001f;
}

// As GunFX_GetHaze, plus out[8..10] the muzzle and out[11..13] the rearward axis in world space, from the game
// loop (for the first-person model, whose render-time positions are in another space).
__declspec(dllexport) bool __cdecl GunFX_GetHazeV2(float out[14], const void* weaponNode) {
	float heat[10];
	if (!out || !ReadHeatForNVR(heat, weaponNode, true)) return false;
	EnterCriticalSection(&HeatLock);
	memcpy(out, HazePublished, sizeof(HazePublished));
	memcpy(out + 8, HazeWorld, sizeof(HazeWorld));
	LeaveCriticalSection(&HeatLock);
	return out[0] > 0.001f;
}

// As V2, plus out[14] the muzzle blast displacement in pixels right now and out[15] its radius in pixels. True while
// either the barrel haze or the blast is showing.
__declspec(dllexport) bool __cdecl GunFX_GetHazeV3(float out[16], const void* weaponNode) {
	float heat[10];
	if (!out || !ReadHeatForNVR(heat, weaponNode, true)) return false;
	EnterCriticalSection(&HeatLock);
	memcpy(out, HazePublished, sizeof(HazePublished));
	memcpy(out + 8, HazeWorld, sizeof(HazeWorld));
	memcpy(out + 14, HazeBlast, sizeof(HazeBlast));
	LeaveCriticalSection(&HeatLock);
	return out[0] > 0.001f || out[14] > 0.01f;
}

// NVR's menu switches (see Switch()); called by NVR every frame.
__declspec(dllexport) void __cdecl GunFX_SetSwitches(UInt32 bits) {
	if ((LONG)bits != NvrSwitches) {
		InterlockedExchange(&NvrSwitches, (LONG)bits);
		LogLine("NVR menu switches: puff %d, heat smoke %d, ejection smoke %d, glow %d, haze %d, muzzle blast %d, energy weapons %d, after-fire trail %d",
			bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, (bits >> 3) & 1, (bits >> 4) & 1, (bits >> 5) & 1, (bits >> 6) & 1, (bits >> 7) & 1);
	}
}

__declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
	info->infoVersion = 1;
	info->name = "GunFX";
	info->version = 54;
	return !nvse->isEditor && nvse->runtimeVersion == 0x040020D0; // 1.4.0.525
}

__declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface* nvse) {
	if (nvse->isEditor) return true;
	Log = _fsopen("GunFX.log", "w", _SH_DENYWR); // readable while the game runs
	LogLine("GunFX 1.4 (one [Muzzle] offset for the puff, heat smoke and trail; new defaults)");
	LoadSettings();
	if (GetModuleHandleA("BarrelSmoke.dll")) {
		LogLine("BarrelSmoke.dll is also installed: GunFX replaces it. Remove BarrelSmoke.dll from Data\\NVSE\\Plugins; GunFX stays off until then.");
		return true;
	}
	InitializeCriticalSection(&PendingLock);
	InitializeCriticalSection(&HeatLock);
	HeatLockReady = true;
	HookCall(CallSites[0], (void*)FireHook0, &OriginalFire[0]);
	HookCall(CallSites[1], (void*)FireHook1, &OriginalFire[1]);
	HookCall(CallSites[2], (void*)FireHook2, &OriginalFire[2]);
	HookCall(EjectSites[0], (void*)EjectHook0, &OriginalEject[0]);
	HookCall(EjectSites[1], (void*)EjectHook1, &OriginalEject[1]);
	NVSEMessagingInterface* messaging = (NVSEMessagingInterface*)nvse->QueryInterface(kInterface_Messaging);
	const bool listening = messaging && messaging->RegisterListener(nvse->GetPluginHandle(), "NVSE", MessageHandler);
	LogLine(listening ? "listening to the game loop" : "could not register for the game loop: no wisp");
	return true;
}
}
