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
#include "GunShape.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

typedef unsigned int UInt32;
typedef unsigned char UInt8;

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
	UInt8* node;
	void* light;
	void* projectile;
	void* sourceWeap;
	UInt8* sourceActor;
};

struct VolLook { float size, sizeVar, life, lifeVar, speed, speedVar, startSize, grow, opacity, fadeIn, fadeStart, fadeEnd, rise, curl, spread; };
enum { ChainHeat, ChainTrail, ChainPuff, ChainEject, ChainCount };
static struct {
	int puff;
	char puffModel[MAX_PATH];
	float puffLifetime, puffScale, puffInterval;
	int puffPlayerOnly;
	int trail;
	char trailModel[MAX_PATH];
	float trailScale, heatPerShot, coolPerSecond, maxHeat, heatStart, heatFull, maxRate, fileRate, effectSeconds, tailSeconds;
	int trailFlags, trailSmokeOnly, trailFollow;
	float constantRate, sway, swaySpeed, coolDelay, wispStopDelay;
	int wispEmitters, afterEmitters;
	int volume;
	float volumeSize, volumeDensity, volumeRise, volumeCurl, volumeDrag, volumeSpacing, volumeNoise, volumeBrightness, volumeShade;
	int volumeDebug;
	int volumeKinds[ChainCount];
	float volumeBurstDrag, volumePuffForward, volumeBurstSize;
	int volumeBurstPoints;
	int volumeGunInFront;
	float volumeExpand, volumeThinning;
	float volumeFlash, volumeFlashRadius, volumeFlashSeconds;
	float volumeWind, volumeWindPickup, volumeWindOffset;
	int volumePlayer;
	float volumePlayerRadius, volumePlayerPush, volumeStir, volumePlayerHeight;
	float volumeDiffuse;
	float volumeBurst;
	float volumeBullet, volumeBulletReach;
	float volumeBulletOpen, volumeBulletSeconds;
	float volumePillows, volumeStreaks;
	float volumeHaze, volumeHazeLife, volumeHazeSize;
	float volumeTint;
	float volumeSmooth;
	float volumeDepth;
	float volumeDriftRight, volumeDriftAway;
	int volumeWrapGun;
	int volumeGunShape;
	float volumeGunLength, volumeGunRadius, volumeBarrelSpread, volumeGunSwirl, volumeBodySwirl, volumeSwirlLife;
	float volumeTendrils, volumeTendrilAngle, volumeTendrilSize, volumeTendrilSpeed;
	int volumeUnzip;
	float volumeFanSpeed;
	VolLook volLook[ChainCount];
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
	float ejectScale, ejectLifetime, ejectInterval, ejectHotScale, ejectBurst, puffBurst, ejectBurstRate, puffBurstRate;
	float hazeStart, hazeFull, hazeRise, hazePixels, hazeHeight, hazeWidth, hazeSpeed, hazeLength, hazeMinSize;
	int flags, logEvents;
} Settings;

static FILE* Log = nullptr;
static void LogLine(const char* format, ...) {
	if (!Log) return;
	char line[4096];
	va_list args; va_start(args, format); _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args); va_end(args);
	SYSTEMTIME t;
	GetLocalTime(&t);
	fprintf(Log, "%02d:%02d:%02d.%03d  %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
	fflush(Log);
}
static double PreciseSeconds() {
	static LARGE_INTEGER frequency = {};
	if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (double)counter.QuadPart / (double)frequency.QuadPart;
}
static bool LogEvent() { if (Settings.logEvents <= 0) return false; Settings.logEvents--; return true; }

static volatile LONG NvrSwitches = -1;
static bool Switch(int bit) { return (NvrSwitches >> bit) & 1; }
static bool IsEnergyWeapon(void* weapon) {
	if (!weapon) return false;
	const UInt8 type = *((UInt8*)weapon + 0xF4);
	return *(UInt32*)((UInt8*)weapon + 0x15C) == 34 || type == 4 || type == 7;
}
static bool GunAllowed(void* weapon) { return !IsEnergyWeapon(weapon) || Switch(6); }

typedef void* (__cdecl* SpawnFn)(void* cell, float lifetime, const char* model, NiPoint3 up, NiPoint3 position,
	float scale, UInt32 flags, void* parent);
static const SpawnFn SpawnEffect = (SpawnFn)0x006890B0;
#ifndef GUNFX_REPLAY
static void* Player() { return *(void**)0x011DEA3C; }
#else
static void* Player() { return nullptr; }
struct ReplayBodyState { bool on, indoors; NiPoint3 feet; };
static ReplayBodyState ReplayBody = {};
#endif

static void NodeAddRef(UInt8* node) { if (node) InterlockedIncrement((volatile LONG*)(node + 4)); }
static void NodeRelease(UInt8* node) {
	if (node && InterlockedDecrement((volatile LONG*)(node + 4)) == 0) {
		void** vtable = *(void***)node;
		((void(__thiscall*)(void*))vtable[1])(node);
	}
}

static int HideSolidMeshes(UInt8* object, int depth) {
	if (!object || depth > 8) return 0;
	void** vtable = *(void***)object;
	typedef void* (__thiscall* IsFn)(void*);
	if (((IsFn)vtable[12])(object)) return 0;
	if (((IsFn)vtable[8])(object) || ((IsFn)vtable[9])(object)) { *(UInt32*)(object + 0x30) |= 1; return 1; }
	int hidden = 0;
	if (((IsFn)vtable[3])(object)) {
		UInt8** children = *(UInt8***)(object + 0x9C + 4);
		const unsigned short count = *(unsigned short*)(object + 0x9C + 0xA);
		for (unsigned short i = 0; children && i < count; i++) hidden += HideSolidMeshes(children[i], depth + 1);
	}
	return hidden;
}

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

struct Look {
	float speed, radius, radiusVar, life, lifeVar;
	float grow, fade;
	float strength, turbulence, turbulenceScale;
	float percents[6];
	float opacity;
};
static const Look WispFile = { 12.0f, 5.0f, 1.5f, 2.5f, 0.6f, 2.2f, 0.0f, -20.0f, 0.03f, 5.0f, { 0.08f, 0.15f, 0.2f, 0.5f, 0.51f, 0.8f }, 0.35f };
static const Look PuffFile = { 5.0f, 3.0f, 1.0f, 1.3f, 0.3f, 1.2f, 0.0f, -6.0f, 0.035f, 5.0f, { 0.03f, 0.15f, 0.0f, 0.06f, 0.51f, 0.8f }, 0.35f };
static const Look StrandFile = { 10.0f, 3.0f, 0.7f, 2.6f, 0.3f, 2.6f, 0.0f, -9.0f, 0.02f, 5.0f, { 0.05f, 0.15f, 0.02f, 0.12f, 0.45f, 0.92f }, 0.15f };
static const Look* WispBuilt = &WispFile;
static Look WispLook = WispFile, PuffLook = PuffFile;

static bool InImage(const void* p, UInt32 lo, UInt32 hi) { return (UInt32)p >= lo && (UInt32)p < hi; }
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
	for (int at = 8; at <= 0x80; at += 4) {
		bool ok = true;
		for (int k = 0; k < count && ok; k++) ok = Near(*(float*)(object + at + gaps[k]), want[k]);
		if (ok) return (float*)(object + at);
	}
	return nullptr;
}

static int ApplyLookToSystem(UInt8* psys, const Look& file, const Look& look, char* report, size_t reportSize) {
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
				f[6 + 4 + 3] = look.opacity;
				written++; strcat_s(report, reportSize, "fade+opacity ");
			}
		}
	}
	return written;
}

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

static struct { void* actor; ULONGLONG tick; } LastPuff[16] = {};

static void* SpawnBurst(void* cell, float lifetime, const char* model, NiPoint3 at, float scale, float seconds, float rate, ULONGLONG now,
	UInt8* follow, NiPoint3 offset);
static bool VolumeWanted(int chain);
static void StartVolumeBurst(int chain, UInt8* node, const NiPoint3& offset, float seconds, float size);
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
	const bool automatic = flash->sourceWeap && (*((UInt8*)flash->sourceWeap + 0x100) & 0x2);
	if (automatic && slot >= 0 && now - LastPuff[slot].tick < (ULONGLONG)(Settings.puffInterval * 1000.0f)) return;
	if (slot < 0) slot = oldest;
	LastPuff[slot].actor = flash->sourceActor;
	LastPuff[slot].tick = now;
	const NiPoint3 position = NodePosition(flash->node, Settings.puffOffset);
	if (flash->sourceActor == Player() && VolumeWanted(ChainPuff)) {
		StartVolumeBurst(ChainPuff, flash->node, Settings.puffOffset, Settings.puffBurst, 1.0f);
		if (LogEvent()) LogLine("puff at (%.1f, %.1f, %.1f) -> volumetric", position.x, position.y, position.z);
		return;
	}
	void* effect = SpawnBurst(cell, Settings.puffLifetime, Settings.puffModel, position, Settings.puffScale, Settings.puffBurst, Settings.puffBurstRate, now,
		flash->sourceActor == Player() ? flash->node : nullptr, Settings.puffOffset);
	if (LogEvent()) LogLine("puff at (%.1f, %.1f, %.1f) -> %s (NIF look)", position.x, position.y, position.z,
		effect ? "created" : "NOT created");
}

static float Heat = 0.0f;
static float VisualHeat = 0.0f;
static float VisualHaze = 0.0f;
static float SpreadHeat = 0.0f;
static ULONGLONG LastShot = 0;
static double LastShotSeconds = 0.0;
static ULONGLONG LastMuzzleSeen = 0;
static double LastLoopSeconds = 0.0;
static UInt8* TrailNode = nullptr;
static void* TrailActor = nullptr;
static CRITICAL_SECTION HeatLock;
static bool HeatLockReady = false;
static UInt8* HeatNode = nullptr;
static float HeatStrength = 0.0f, HeatLength = 0.0f, HeatRadius = 0.0f, HeatForwardLength = 0.0f;
static float HeatOffsetX = 0.0f, HeatOffsetY = 0.0f, HeatOffsetZ = 0.0f;
static float HazePublished[8] = {};
static float HazeBlast[2] = {};
static float HazeWorld[6] = {};

static UInt8* FollowEffect = nullptr;
static ULONGLONG FollowUntil = 0;
static float* RateValue = nullptr;
static UInt8* RateController = nullptr;
static Look FollowLook;
static bool FollowHeld = false;
static UInt8* AfterEffect = nullptr;
static ULONGLONG AfterUntil = 0;
static float* AfterRate = nullptr;
static UInt8* AfterController = nullptr;
static bool AfterHeld = false;
static const int GlowPoints = 4;
static NiPoint3 GlowBack = { 0.0f, -1.0f, 0.0f };
static bool GlowUnavailable = false;
static ULONGLONG NextGlowPulse = 0;

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
	LastShotSeconds = PreciseSeconds();
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
static char WeaponIniPath[MAX_PATH] = "";
static FILETIME WeaponIniWritten = {};
static int OwnGunValues(const char* path);
static ULONGLONG LastIniCheck = 0;

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

struct Burst { UInt8* effect; float* rate; UInt8* controller; ULONGLONG offAt; UInt8* follow; NiPoint3 offset; };
static NiPoint3 WorldPoint(UInt8* node, NiPoint3 at);
static Burst Bursts[64] = {};
static float BurstRate(const char* model) {
	char lower[MAX_PATH];
	strcpy_s(lower, model);
	_strlwr_s(lower);
	if (strstr(lower, "barrelport")) return 160.0f;
	if (strstr(lower, "barrelpuff")) return 60.0f;
	return 40.0f;
}
static void EndBurst(Burst& b) {
	if (b.rate) *b.rate = 0.0f;
	NodeRelease(b.follow);
	NodeRelease(b.effect);
	b = Burst{};
}
static void* SpawnBurst(void* cell, float lifetime, const char* model, NiPoint3 at, float scale, float seconds, float burstRate, ULONGLONG now,
	UInt8* follow, NiPoint3 offset) {
	void* effect = SpawnEffect(cell, lifetime, model, NiPoint3{ 0.0f, 0.0f, 1.0f }, at, scale, (UInt32)Settings.flags, nullptr);
	if (!effect) return nullptr;
	UInt8* root = *(UInt8**)((UInt8*)effect + 0x18);
	UInt8* controller = nullptr;
	float* rate = root ? FindRate(root, 0, OwnModel(model), &controller) : nullptr;
	if (!rate || !controller) return effect;
	*rate = burstRate > 0.0f ? burstRate : BurstRate(model);
	*(unsigned short*)(controller + 0x08) |= 0x8;
	int slot = 0;
	for (int i = 0; i < 64; i++) {
		if (!Bursts[i].effect) { slot = i; break; }
		if (Bursts[i].offAt < Bursts[slot].offAt) slot = i;
	}
	if (Bursts[slot].effect) EndBurst(Bursts[slot]);
	NodeAddRef((UInt8*)effect);
	NodeAddRef(follow);
	Bursts[slot] = Burst{ (UInt8*)effect, rate, controller, now + (ULONGLONG)(seconds * 1000.0f), follow, offset };
	return effect;
}
static void UpdateBursts(ULONGLONG now) {
	for (int i = 0; i < 64; i++) {
		Burst& b = Bursts[i];
		if (!b.effect) continue;
		if (now >= b.offAt) { EndBurst(b); continue; }
		UInt8* root = *(UInt8**)(b.effect + 0x18);
		if (root && b.follow && *(volatile LONG*)(b.follow + 4) > 1)
			*(NiPoint3*)(root + 0x58) = WorldPoint(b.follow, NodePosition(b.follow, b.offset));
	}
}

struct Copies { UInt8* effect[3]; float* rate[3]; UInt8* ctrl[3]; int count; NiPoint3 last; bool hasLast; };
static Copies WispCopies = {}, AfterCopies = {};
static void SpawnCopies(Copies& c, int total, void* cell, float lifetime, const char* model, float scale, const NiPoint3& at) {
	c = Copies{};
	for (int k = 0; k < total - 1 && k < 3; k++) {
		void* effect = SpawnEffect(cell, lifetime, model, NiPoint3{ 0.0f, 0.0f, 1.0f }, at, scale, (UInt32)Settings.trailFlags, nullptr);
		if (!effect) break;
		UInt8* root = *(UInt8**)((UInt8*)effect + 0x18);
		UInt8* ctrl = nullptr;
		float* rate = root ? FindRate(root, 0, OwnModel(model), &ctrl) : nullptr;
		if (rate) { *rate = 0.0f; *(unsigned short*)(ctrl + 0x08) |= 0x8; }
		NodeAddRef((UInt8*)effect);
		c.effect[c.count] = (UInt8*)effect; c.rate[c.count] = rate; c.ctrl[c.count] = ctrl; c.count++;
	}
}
static void ReleaseCopies(Copies& c) {
	for (int k = 0; k < c.count; k++) {
		if (c.rate[k]) *c.rate[k] = 0.0f;
		NodeRelease(c.effect[k]);
	}
	c = Copies{};
}
static void PlaceCopies(Copies& c, UInt8* root, float* rate, UInt8* ctrl, const NiPoint3& at, float smoke) {
	NiPoint3 from = c.hasLast ? c.last : at;
	const float dx = at.x - from.x, dy = at.y - from.y, dz = at.z - from.z;
	if (dx * dx + dy * dy + dz * dz > 200.0f * 200.0f) from = at;
	const int total = c.count + 1;
	for (int k = 0; k < c.count; k++) {
		const float f = (float)(k + 1) / total;
		UInt8* copyRoot = *(UInt8**)(c.effect[k] + 0x18);
		if (copyRoot) {
			NiPoint3& p = *(NiPoint3*)(copyRoot + 0x58);
			p.x = from.x + (at.x - from.x) * f; p.y = from.y + (at.y - from.y) * f; p.z = from.z + (at.z - from.z) * f;
		}
		if (c.rate[k]) { *c.rate[k] = smoke / total; *(unsigned short*)(c.ctrl[k] + 0x08) |= 0x8; }
	}
	if (root) *(NiPoint3*)(root + 0x58) = at;
	if (rate) { *rate = smoke / total; *(unsigned short*)(ctrl + 0x08) |= 0x8; }
	c.last = at;
	c.hasLast = true;
}

static void ReleaseWisp(const char* why) {
	if (!FollowEffect) return;
	ReleaseCopies(WispCopies);
	UInt8* root = *(UInt8**)(FollowEffect + 0x18);
	if (root && FollowUntil > GetTickCount64()) StopEmitters(root, 0);
	if (why) LogLine("wisp released: %s (heat %.1f)", why, Heat);
	NodeRelease(FollowEffect);
	FollowEffect = nullptr;
	RateValue = nullptr;
	RateController = nullptr;
}

static void ReleaseAfter(const char* why) {
	if (!AfterEffect) return;
	ReleaseCopies(AfterCopies);
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
		const float* rot = (float*)(node + 0x68);
		back = NiPoint3{ -rot[1], -rot[4], -rot[7] };
		length = sqrtf(back.x * back.x + back.y * back.y + back.z * back.z);
	}
	return length > 0.01f ? NiPoint3{ back.x / length, back.y / length, back.z / length } : GlowBack;
}

static NiPoint3 BarrelBack(UInt8* node, float* offDegrees = nullptr) {
	const NiPoint3 toParent = GlowDirection(node);
	if (!node) return toParent;
	const float* rot = (const float*)(node + 0x68);
	NiPoint3 axis = { -rot[1], -rot[4], -rot[7] };
	const float length = sqrtf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
	if (!(length > 0.5f && length < 2.0f)) return toParent;
	axis = NiPoint3{ axis.x / length, axis.y / length, axis.z / length };
	float c = axis.x * toParent.x + axis.y * toParent.y + axis.z * toParent.z;
	c = c < -1.0f ? -1.0f : c > 1.0f ? 1.0f : c;
	if (offDegrees) *offDegrees = acosf(c) * 57.29578f;
	return c > 0.7071f ? axis : toParent;
}

struct ShapeProbe { int parts, readable, skinned, vertices, triangles, lines; NiPoint3 lo, hi; };
static NiPoint3 IntoNode(UInt8* node, const NiPoint3& w) {
	const float* R = (const float*)(node + 0x68);
	const NiPoint3 T = *(NiPoint3*)(node + 0x8C);
	const float S = *(float*)(node + 0x98) > 0.0001f ? *(float*)(node + 0x98) : 1.0f;
	const NiPoint3 d = { w.x - T.x, w.y - T.y, w.z - T.z };
	return NiPoint3{ (R[0] * d.x + R[3] * d.y + R[6] * d.z) / S, (R[1] * d.x + R[4] * d.y + R[7] * d.z) / S, (R[2] * d.x + R[5] * d.y + R[8] * d.z) / S };
}
static void ProbeShapes(UInt8* object, UInt8* root, ShapeProbe& p, int depth) {
	if (!object || depth > 24) return;
	void** vtable = *(void***)object;
	typedef void* (__thiscall* IsFn)(void*);
	const bool strips = ((IsFn)vtable[8])(object) != nullptr, shape = ((IsFn)vtable[9])(object) != nullptr;
	if (strips || shape) {
		p.parts++;
		UInt8* data = *(UInt8**)(object + 0xB8);
		const bool skinned = *(UInt8**)(object + 0xBC) != nullptr;
		p.skinned += skinned ? 1 : 0;
		const unsigned short verts = data ? *(unsigned short*)(data + 0x08) : 0;
		const NiPoint3* pos = data ? *(NiPoint3**)(data + 0x20) : nullptr;
		const unsigned short tris = data ? *(unsigned short*)(data + 0x40) : 0;
		const void* index = data ? *(void**)(data + (strips ? 0x4C : 0x48)) : nullptr;
		const UInt8 keep = data ? *(data + 0x38) : 0;
		const char* name = *(const char**)(object + 8);
		if (p.lines++ < 48)
			LogLine("gun shape probe: %s '%s': %u vertices%s, %u triangles%s, keep flags 0x%02X%s", RttiName(object), name ? name : "",
				verts, pos ? "" : " (positions not kept)", tris, index ? "" : " (indices not kept)", keep, skinned ? ", skinned" : "");
		if (!pos || !verts) return;
		p.readable++;
		p.vertices += verts;
		p.triangles += tris;
		if (skinned) return;
		const float* r = (const float*)(object + 0x68);
		const NiPoint3 t = *(NiPoint3*)(object + 0x8C);
		const float s = *(float*)(object + 0x98);
		for (unsigned short v = 0; v < verts; v++) {
			const NiPoint3& q = pos[v];
			const NiPoint3 w = { t.x + s * (r[0] * q.x + r[1] * q.y + r[2] * q.z), t.y + s * (r[3] * q.x + r[4] * q.y + r[5] * q.z),
				t.z + s * (r[6] * q.x + r[7] * q.y + r[8] * q.z) };
			const NiPoint3 l = IntoNode(root, w);
			p.lo = NiPoint3{ l.x < p.lo.x ? l.x : p.lo.x, l.y < p.lo.y ? l.y : p.lo.y, l.z < p.lo.z ? l.z : p.lo.z };
			p.hi = NiPoint3{ l.x > p.hi.x ? l.x : p.hi.x, l.y > p.hi.y ? l.y : p.hi.y, l.z > p.hi.z ? l.z : p.hi.z };
		}
		return;
	}
	if (!((IsFn)vtable[3])(object)) return;
	UInt8** children = *(UInt8***)(object + 0xA0);
	const unsigned short count = *(unsigned short*)(object + 0xA6);
	for (unsigned short i = 0; children && i < count; ++i) ProbeShapes(children[i], root, p, depth + 1);
}
static void ProbeGunShape(UInt8* root, UInt8* muzzle) {
	ShapeProbe p = {};
	p.lo = NiPoint3{ 1e9f, 1e9f, 1e9f };
	p.hi = NiPoint3{ -1e9f, -1e9f, -1e9f };
	__try { ProbeShapes(root, root, p, 0); }
	__except (EXCEPTION_EXECUTE_HANDLER) { LogLine("gun shape probe: stopped by a fault after %d part(s)", p.parts); }
	const NiPoint3 m = IntoNode(root, *(NiPoint3*)(muzzle + 0x8C));
	LogLine("gun shape probe: %d mesh part(s), %d with readable positions (%d skinned): %d vertices, %d triangles", p.parts, p.readable,
		p.skinned, p.vertices, p.triangles);
	if (p.lo.x <= p.hi.x)
		LogLine("gun shape probe: extent in the gun's own space x %.1f..%.1f, y %.1f..%.1f, z %.1f..%.1f; muzzle at (%.1f, %.1f, %.1f)",
			p.lo.x, p.hi.x, p.lo.y, p.hi.y, p.lo.z, p.hi.z, m.x, m.y, m.z);
}

struct ShapeJob { std::vector<float> tris; UInt8* root; void* weapon; GunShape* result; int parts, skipped, triangles; double ms; };
static const size_t ShapeMaxTriangles = 600000;
static GunShape* Shape = nullptr;
static UInt8* ShapeRoot = nullptr;
static void* ShapeWeapon = nullptr;
static bool ShapeBuilding = false;
static ShapeJob* volatile ShapeDone = nullptr;
static NiPoint3 ShapeMuzzle = {};
static bool ShapeAlongX = false;
static bool ShapeOn = false;
static float ShapeRot[9] = {}, ShapeScale = 1.0f;
static NiPoint3 ShapePos = {};

static void CollectShape(UInt8* object, UInt8* root, ShapeJob& job, int depth) {
	if (!object || depth > 24) return;
	if (*(UInt32*)(object + 0x30) & 1) return;
	void** vtable = *(void***)object;
	typedef void* (__thiscall* IsFn)(void*);
	const bool strips = ((IsFn)vtable[8])(object) != nullptr, shape = ((IsFn)vtable[9])(object) != nullptr;
	if (strips || shape) {
		UInt8* data = *(UInt8**)(object + 0xB8);
		const unsigned short nv = data ? *(unsigned short*)(data + 0x08) : 0;
		const NiPoint3* pos = data ? *(NiPoint3**)(data + 0x20) : nullptr;
		if (!pos || !nv || *(UInt8**)(object + 0xBC)) { job.skipped++; return; }
		const float* r = (const float*)(object + 0x68);
		const NiPoint3 t = *(NiPoint3*)(object + 0x8C);
		const float s = *(float*)(object + 0x98);
		const float* R = (const float*)(root + 0x68);
		const NiPoint3 T = *(NiPoint3*)(root + 0x8C);
		const float S = *(float*)(root + 0x98) > 0.0001f ? *(float*)(root + 0x98) : 1.0f;
		float M[9];
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++) M[i * 3 + j] = (R[i] * r[j] + R[3 + i] * r[3 + j] + R[6 + i] * r[6 + j]) * s / S;
		const NiPoint3 dt = { t.x - T.x, t.y - T.y, t.z - T.z };
		const float o[3] = { (R[0] * dt.x + R[3] * dt.y + R[6] * dt.z) / S, (R[1] * dt.x + R[4] * dt.y + R[7] * dt.z) / S,
			(R[2] * dt.x + R[5] * dt.y + R[8] * dt.z) / S };
		auto put = [&](unsigned short a) {
			const NiPoint3& q = pos[a];
			job.tris.push_back(M[0] * q.x + M[1] * q.y + M[2] * q.z + o[0]);
			job.tris.push_back(M[3] * q.x + M[4] * q.y + M[5] * q.z + o[1]);
			job.tris.push_back(M[6] * q.x + M[7] * q.y + M[8] * q.z + o[2]);
		};
		if (shape) {
			const unsigned short count = *(unsigned short*)(data + 0x40);
			const UInt32 len = *(UInt32*)(data + 0x44);
			const unsigned short* idx = *(unsigned short**)(data + 0x48);
			if (!idx || len != 3u * count || job.tris.size() / 9 + count > ShapeMaxTriangles) { job.skipped++; return; }
			for (UInt32 k = 0; k < len; k++) if (idx[k] >= nv) { job.skipped++; return; }
			for (UInt32 k = 0; k < len; k++) put(idx[k]);
		}
		else {
			const unsigned short ns = *(unsigned short*)(data + 0x44);
			const unsigned short* lens = *(unsigned short**)(data + 0x48);
			const unsigned short* idx = *(unsigned short**)(data + 0x4C);
			if (!lens || !idx) { job.skipped++; return; }
			UInt32 total = 0;
			for (unsigned short k = 0; k < ns; k++) total += lens[k];
			if (job.tris.size() / 9 + total > ShapeMaxTriangles) { job.skipped++; return; }
			for (UInt32 k = 0; k < total; k++) if (idx[k] >= nv) { job.skipped++; return; }
			const unsigned short* q = idx;
			for (unsigned short k = 0; k < ns; k++) {
				const int L = lens[k];
				for (int j = 0; j + 2 < L; j++) {
					const unsigned short a = q[j], b = q[j + 1], c = q[j + 2];
					if (a == b || b == c || a == c) continue;
					put(a); put(b); put(c);
				}
				q += L;
			}
		}
		job.parts++;
		return;
	}
	if (!((IsFn)vtable[3])(object)) return;
	UInt8** children = *(UInt8***)(object + 0xA0);
	const unsigned short count = *(unsigned short*)(object + 0xA6);
	for (unsigned short i = 0; children && i < count; ++i) CollectShape(children[i], root, job, depth + 1);
}
static bool CollectShapeSafe(UInt8* root, ShapeJob& job) {
	__try { CollectShape(root, root, job, 0); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static DWORD WINAPI ShapeWorker(LPVOID arg) {
	ShapeJob* job = (ShapeJob*)arg;
	LARGE_INTEGER f, a, b;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&a);
	try {
		GunShape* shape = new GunShape();
		if (shape->Build(job->tris.data(), job->triangles, 0.5f, 5.0f, 0.3f, 2000000)) job->result = shape;
		else delete shape;
	}
	catch (...) { job->result = nullptr; }
	QueryPerformanceCounter(&b);
	job->ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
	std::vector<float>().swap(job->tris);
	InterlockedExchangePointer((PVOID volatile*)&ShapeDone, job);
	return 0;
}

static void UpdateGunShape(UInt8* root, UInt8* muzzleNode) {
	if (ShapeJob* done = (ShapeJob*)InterlockedExchangePointer((PVOID volatile*)&ShapeDone, nullptr)) {
		ShapeBuilding = false;
		if (done->result && done->root == root && done->root == ShapeRoot && done->weapon == CurrentWeapon) {
			delete Shape;
			Shape = done->result;
			done->result = nullptr;
			LogLine("gun shape: ready - %d part(s) (%d left out), %d triangles -> map %d x %d x %d at %.2f units (%.1f MB) in %.0f ms; "
				"barrel along the gun's own axis: %s", done->parts, done->skipped, done->triangles, Shape->nx, Shape->ny, Shape->nz,
				Shape->voxel, Shape->d.size() * 4.0 / 1048576.0, done->ms, ShapeAlongX ? "yes" : "no");
		}
		else if (!done->result) LogLine("gun shape: no map could be built from the gun's model; the tube stands in");
		delete done->result;
		delete done;
	}
	if (!root || !Settings.volume || !Settings.volumeWrapGun || !Settings.volumeGunShape || ShapeBuilding) return;
	static int tries = 0;
	static ULONGLONG triedAt = 0;
	const ULONGLONG now = GetTickCount64();
	if (root == ShapeRoot && CurrentWeapon == ShapeWeapon) {
		if (Shape || tries >= 3 || now - triedAt < 3000) return;
	}
	else tries = 0;
	tries++;
	triedAt = now;
	delete Shape;
	Shape = nullptr;
	ShapeRoot = root;
	ShapeWeapon = CurrentWeapon;
	ShapeJob* job = new ShapeJob();
	job->root = root; job->weapon = CurrentWeapon; job->result = nullptr; job->parts = job->skipped = 0; job->ms = 0.0;
	const bool read = CollectShapeSafe(root, *job);
	job->triangles = (int)(job->tris.size() / 9);
	if (!read || !job->triangles) {
		LogLine("gun shape: %s; the tube stands in", read ? "the gun's model has no readable mesh" : "reading the gun's model stopped by a fault");
		delete job;
		return;
	}
	ShapeMuzzle = IntoNode(root, *(NiPoint3*)(muzzleNode + 0x8C));
	float far_x = -1e9f;
	for (size_t i = 0; i < job->tris.size(); i += 3) far_x = job->tris[i] > far_x ? job->tris[i] : far_x;
	ShapeAlongX = ShapeMuzzle.x > 0.0f && ShapeMuzzle.x > 0.85f * far_x && fabsf(ShapeMuzzle.y) < 0.3f * ShapeMuzzle.x &&
		fabsf(ShapeMuzzle.z) < 0.3f * ShapeMuzzle.x;
	ShapeBuilding = true;
	HANDLE thread = CreateThread(nullptr, 0, ShapeWorker, job, 0, nullptr);
	if (thread) CloseHandle(thread);
	else { ShapeBuilding = false; delete job; }
}

static float GunSurface(const NiPoint3& p, NiPoint3* n) {
	const float* R = ShapeRot;
	const NiPoint3 d = { p.x - ShapePos.x, p.y - ShapePos.y, p.z - ShapePos.z };
	const float l[3] = { (R[0] * d.x + R[3] * d.y + R[6] * d.z) / ShapeScale, (R[1] * d.x + R[4] * d.y + R[7] * d.z) / ShapeScale,
		(R[2] * d.x + R[5] * d.y + R[8] * d.z) / ShapeScale };
	float ln[3];
	const float dist = Shape->Sample(l, ln);
	*n = NiPoint3{ R[0] * ln[0] + R[1] * ln[1] + R[2] * ln[2], R[3] * ln[0] + R[4] * ln[1] + R[5] * ln[2], R[6] * ln[0] + R[7] * ln[1] + R[8] * ln[2] };
	return dist * ShapeScale;
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
	const float since = LastShotSeconds > 0.0 ? (float)(PreciseSeconds() - LastShotSeconds) : 1e9f;
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
	UInt8* root = *(UInt8**)((UInt8*)effect + 0x18);
	if (Settings.trailSmokeOnly && root) HideSolidMeshes(root, 0);
	FollowEffect = (UInt8*)effect;
	NodeAddRef(FollowEffect);
	FollowHeld = *(volatile LONG*)(FollowEffect + 4) > 1;
	if (LogEvent()) LogLine("wisp uses baked NIF look");
	FollowUntil = now + (ULONGLONG)(Settings.effectSeconds * 1000.0f) - 150;
	const bool ownModel = OwnModel(Settings.trailModel);
	RateController = nullptr;
	RateValue = root ? FindRate(root, 0, ownModel, &RateController) : nullptr;
	const float startRate = RateValue ? *RateValue : -1.0f;
	const unsigned short startFlags = RateController ? *(unsigned short*)(RateController + 0x08) : 0;
	if (RateValue) { *RateValue = 0.0f; *(unsigned short*)(RateController + 0x08) |= 0x8; }
	SpawnCopies(WispCopies, Settings.wispEmitters, cell, Settings.effectSeconds, Settings.trailModel, Settings.trailScale, position);
	LogLine("wisp %s at (%.1f, %.1f, %.1f) for up to %.0f s, heat %.1f (game reference %s); smoke rate %s (started at %.1f, emitter %s; controller %p, rate %p)",
		Settings.trailModel, position.x, position.y, position.z, Settings.effectSeconds, Heat, FollowHeld ? "yes" : "no",
		RateValue ? "under control" : "NOT found (on/off only; check fFileRate matches the file)", startRate,
		(startFlags & 0x8) ? "on" : "OFF", RateController, RateValue);
	return true;
}

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
	SpawnCopies(AfterCopies, Settings.afterEmitters, cell, Settings.effectSeconds, Settings.afterModel, Settings.afterScale, position);
	LogLine("after-fire trail %s, heat %.1f, %d emitter(s); smoke rate %s", Settings.afterModel, Heat, AfterCopies.count + 1,
		AfterRate ? "under control" : "NOT found");
	return true;
}

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
static const IsWeaponDrawnFn IsWeaponDrawn = (IsWeaponDrawnFn)0x008A16D0;


static void HandleShots(ULONGLONG now);

typedef void* (__thiscall* GetCurrentWeaponFn)(void* actor);
static const GetCurrentWeaponFn GetEquippedWeapon = (GetCurrentWeaponFn)0x008A1710;
static void* SettingsWeapon = nullptr;
static const char* const GunSections[] = { "Muzzle", "Puff", "Wisp", "Trail", "Volume", "Glow", "Haze", "Blast", "Ejection" };
static bool SkippedGunKey(const char* key) {
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

struct SmokePoint { NiPoint3 pos, vel, gunDir, kick; float born, life, r0, r1, grow, strength, opacity, fadeIn, fadeStart, fadeEnd, seed, tex, side, stir, spread; bool link, frayed, unzipped, clear; };
static const int ChainMax = 96;
static const char* const ChainNames[ChainCount] = { "heat smoke", "trail", "puff", "ejection" };
struct SmokeChain { SmokePoint p[ChainMax]; int count; float lastEmit; bool emitting; };
static SmokeChain Chains[ChainCount] = {};
struct SmokeSource {
	bool on;
	NiPoint3 at;
	float strength;
	float size;
	NiPoint3 push;
	NiPoint3 carry;
	NiPoint3 bore;
	float maxGap;
	float points;
	float drag;
	int cluster;
	bool drift;
	bool frays;
	int zip;
	bool haze;
};
struct SmokeBurst { float until; UInt8* node; NiPoint3 offset; float size; bool fresh; int pending; };
static SmokeBurst VolumeBursts[ChainCount] = {};
static float SmokeClock = 0.0f;
static UInt32 SmokeRandom = 12345u;
static float Random01() { SmokeRandom = SmokeRandom * 1664525u + 1013904223u; return (SmokeRandom >> 8) * (1.0f / 16777216.0f); }
static const int RecordFloats = 16;
static float VolumePublished[((ChainCount + 2) * ChainMax + 8 * 4 + 40) * RecordFloats] = {};
static NiPoint3 FlashWorld = {};
static double FlashAt = 0.0;
static int VolumePublishedCount = 0;
static float VolumeParams[8] = {};
static const int HazeFloats = 8;
static float SmokeHazePublished[40 * HazeFloats] = {};
static int SmokeHazePublishedCount = 0;
static float VolAfterRate = 0.0f;

static NiPoint3 VolumeWind = {};
static bool VolumeIndoors = false;
static bool VolumeGunOn = false;
static NiPoint3 VolumeGunMuzzle = {}, VolumeGunBack = {}, VolumeAimForward = {}, VolumeAimRight = {};
static float VolumeMuzzleDepth = 0.0f;
static NiPoint3 VolumeGunVel = {};
static float VolumeWindKeep = 1.0f;
static bool VolumePlayerOn = false;
static NiPoint3 VolumePlayerFeet = {}, VolumePlayerVel = {};
static float VolumeMoving = 0.0f;

static float PointRadius(const SmokePoint& s, float age) {
	float g = s.grow > 0.01f ? age / s.grow : 1.0f;
	g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
	g = 1.0f - (1.0f - g) * (1.0f - g);
	return (s.r0 + (s.r1 - s.r0) * g) * s.spread;
}

static float ReachRadius(const SmokePoint& s, float age) {
	const float r = PointRadius(s, age);
	return r < 6.0f ? r : 6.0f;
}

static void UpdateAirAndPlayer(float dt) {
	UInt8* player = (UInt8*)Player();
	UInt8* cell = player ? *(UInt8**)(player + 0x40) : nullptr;
#ifndef GUNFX_REPLAY
	const bool indoors = cell && (cell[0x24] & 0x01) && !(cell[0x24] & 0x80);
	UInt8* sky = *(UInt8**)0x011DEA20;
#else
	(void)cell;
	const bool indoors = ReplayBody.indoors;
	UInt8* sky = nullptr;
#endif
	VolumeIndoors = indoors;
	float speed = 0.0f, raw = 0.0f;
	if (sky && !indoors && Settings.volumeWind > 0.0f) {
		speed = *(float*)(sky + 0xCC);
		raw = *(float*)(sky + 0xD0);
		if (!(speed >= 0.0f && speed <= 4.0f)) speed = 0.0f;
		if (!(raw > -1000.0f && raw < 1000.0f)) raw = 0.0f;
	}
	const float radians = (fabsf(raw) > 6.3f ? raw * 0.01745329f : raw) + Settings.volumeWindOffset * 0.01745329f;
	VolumeWind = NiPoint3{ sinf(radians) * speed * Settings.volumeWind, cosf(radians) * speed * Settings.volumeWind, 0.0f };
	const float fadeIn = speed * 4.0f < 1.0f ? speed * 4.0f : 1.0f;
	VolumeWindKeep = expf(-Settings.volumeWindPickup * fadeIn * dt);
	static int logged = 0;
	static float loggedSpeed = -1.0f, loggedRaw = 0.0f;
	static bool loggedIndoors = false;
	if (logged < 10 && (fabsf(speed - loggedSpeed) > 0.1f || fabsf(raw - loggedRaw) > 0.5f || indoors != loggedIndoors)) {
		logged++;
		loggedSpeed = speed; loggedRaw = raw; loggedIndoors = indoors;
		LogLine("volumetric smoke wind: %s, speed %.2f, direction %.2f (raw) -> drift (%.1f, %.1f) units/s", indoors ? "indoors (none)" : "outdoors",
			speed, raw, VolumeWind.x, VolumeWind.y);
	}
	VolumePlayerOn = false;
	static NiPoint3 last = {};
	static bool haveLast = false;
#ifndef GUNFX_REPLAY
	if (!player || !Settings.volumePlayer) { haveLast = false; VolumeMoving = 0.0f; return; }
	const NiPoint3 feet = *(NiPoint3*)(player + 0x30);
#else
	if (!ReplayBody.on || !Settings.volumePlayer) { haveLast = false; VolumeMoving = 0.0f; return; }
	const NiPoint3 feet = ReplayBody.feet;
#endif
	if (haveLast && dt > 0.0001f) {
		NiPoint3 v = { (feet.x - last.x) / dt, (feet.y - last.y) / dt, (feet.z - last.z) / dt };
		if (v.x * v.x + v.y * v.y + v.z * v.z > 1500.0f * 1500.0f) v = NiPoint3{};
		const float k = 1.0f - expf(-10.0f * dt);
		VolumePlayerVel.x += (v.x - VolumePlayerVel.x) * k;
		VolumePlayerVel.y += (v.y - VolumePlayerVel.y) * k;
		VolumePlayerVel.z += (v.z - VolumePlayerVel.z) * k;
		const float pace = sqrtf(VolumePlayerVel.x * VolumePlayerVel.x + VolumePlayerVel.y * VolumePlayerVel.y);
		const float target = pace < 20.0f ? 0.0f : pace > 80.0f ? 1.0f : (pace - 20.0f) / 60.0f;
		VolumeMoving += (target - VolumeMoving) * (1.0f - expf((target > VolumeMoving ? -12.0f : -2.0f) * dt));
	}
	last = feet;
	haveLast = true;
	VolumePlayerFeet = feet;
	VolumePlayerOn = true;
}

struct Vortex { NiPoint3 pos, axis; float strength, born, life, core, half; };
static const int VortexMax = 24;
static Vortex Vortices[VortexMax] = {};
static int VortexCount = 0;
static float LastShed = -10.0f, LastBodyShed = -10.0f;

static NiPoint3 VortexFlow(const NiPoint3& p, int skip = -1) {
	NiPoint3 v = {};
	for (int i = 0; i < VortexCount; i++) {
		if (i == skip) continue;
		const Vortex& w = Vortices[i];
		float fade = 1.0f - (SmokeClock - w.born) / w.life;
		if (fade <= 0.0f) continue;
		fade = fade * fade * (3.0f - 2.0f * fade);
		const NiPoint3 r = { p.x - w.pos.x, p.y - w.pos.y, p.z - w.pos.z };
		const float ra = r.x * w.axis.x + r.y * w.axis.y + r.z * w.axis.z;
		const float half = w.half, endFade = 0.5f * w.core + 4.0f;
		if (fabsf(ra) > half + endFade) continue;
		const NiPoint3 rp = { r.x - w.axis.x * ra, r.y - w.axis.y * ra, r.z - w.axis.z * ra };
		const float core = w.core;
		const float d2 = rp.x * rp.x + rp.y * rp.y + rp.z * rp.z;
		if (d2 > 36.0f * core * core) continue;
		const float ends = fabsf(ra) > half ? 1.0f - (fabsf(ra) - half) / endFade : 1.0f;
		const float k = w.strength * fade * ends / (6.2831853f * (d2 + core * core));
		v.x += (w.axis.y * rp.z - w.axis.z * rp.y) * k;
		v.y += (w.axis.z * rp.x - w.axis.x * rp.z) * k;
		v.z += (w.axis.x * rp.y - w.axis.y * rp.x) * k;
	}
	return v;
}

static bool SmokeNearGun();
static bool SmokeNearBody();
static void ShedBodyVortices();
static void ShedVortices(float dt) {
	static NiPoint3 moves[VortexMax];
	for (int i = 0; i < VortexCount; i++) moves[i] = VortexFlow(Vortices[i].pos, i);
	int keep = 0;
	for (int i = 0; i < VortexCount; i++) {
		Vortex w = Vortices[i];
		if (SmokeClock - w.born >= w.life) continue;
		w.pos.x += (VolumeWind.x + moves[i].x) * dt; w.pos.y += (VolumeWind.y + moves[i].y) * dt; w.pos.z += moves[i].z * dt;
		Vortices[keep++] = w;
	}
	VortexCount = keep;
	ShedBodyVortices();
	if (!VolumeGunOn || !Settings.volumeWrapGun || Settings.volumeGunSwirl <= 0.0f) return;
	const NiPoint3& b = VolumeGunBack;
	NiPoint3 U = { VolumeWind.x - VolumeGunVel.x, VolumeWind.y - VolumeGunVel.y, -VolumeGunVel.z };
	const float ub = U.x * b.x + U.y * b.y + U.z * b.z;
	U = NiPoint3{ U.x - b.x * ub, U.y - b.y * ub, U.z - b.z * ub };
	const float u = sqrtf(U.x * U.x + U.y * U.y + U.z * U.z);
	if (u < 40.0f || SmokeClock - LastShed < 0.08f || VortexCount + 2 > VortexMax || !SmokeNearGun()) return;
	LastShed = SmokeClock;
	const NiPoint3 uh = { U.x / u, U.y / u, U.z / u };
	NiPoint3 side = { b.y * uh.z - b.z * uh.y, b.z * uh.x - b.x * uh.z, b.x * uh.y - b.y * uh.x };
	const float Rg = Settings.volumeGunRadius, half = 0.5f * Settings.volumeGunLength;
	const NiPoint3 behind = { VolumeGunMuzzle.x + b.x * half + uh.x * Rg * 2.0f, VolumeGunMuzzle.y + b.y * half + uh.y * Rg * 2.0f,
		VolumeGunMuzzle.z + b.z * half + uh.z * Rg * 2.0f };
	const float strength = Settings.volumeGunSwirl * u * Rg * 2.0f;
	for (int e = -1; e <= 1; e += 2) {
		Vortex& w = Vortices[VortexCount++];
		w.pos = NiPoint3{ behind.x + side.x * Rg * 1.2f * e, behind.y + side.y * Rg * 1.2f * e, behind.z + side.z * Rg * 1.2f * e };
		w.axis = b;
		w.strength = -strength * e;
		w.born = SmokeClock;
		w.life = 0.9f;
		w.core = Rg * 1.5f;
		w.half = half;
	}
}

static void ShedBodyVortices() {
	if (!VolumePlayerOn || Settings.volumeBodySwirl <= 0.0f) return;
	const NiPoint3 U = { VolumeWind.x - VolumePlayerVel.x, VolumeWind.y - VolumePlayerVel.y, 0.0f };
	const float u = sqrtf(U.x * U.x + U.y * U.y);
	if (u < 40.0f || SmokeClock - LastBodyShed < 0.12f || VortexCount + 2 > VortexMax || !SmokeNearBody()) return;
	LastBodyShed = SmokeClock;
	const NiPoint3 uh = { U.x / u, U.y / u, 0.0f };
	const NiPoint3 side = { -uh.y, uh.x, 0.0f };
	const float R = Settings.volumePlayerRadius, H = Settings.volumePlayerHeight;
	const NiPoint3 behind = { VolumePlayerFeet.x + uh.x * R * 1.1f, VolumePlayerFeet.y + uh.y * R * 1.1f, VolumePlayerFeet.z + 0.5f * H };
	const float strength = Settings.volumeBodySwirl * u * R;
	for (int e = -1; e <= 1; e += 2) {
		Vortex& w = Vortices[VortexCount++];
		w.pos = NiPoint3{ behind.x + side.x * R * 0.8f * e, behind.y + side.y * R * 0.8f * e, behind.z };
		w.axis = NiPoint3{ 0.0f, 0.0f, 1.0f };
		w.strength = -strength * e;
		w.born = SmokeClock;
		w.life = Settings.volumeSwirlLife;
		w.core = R * 0.5f;
		w.half = 0.5f * H;
	}
}

static bool VolumeWanted(int chain) { return Settings.volume && Switch(8) && Settings.volumeKinds[chain]; }

static int HazeShots = 0;
static void StartVolumeBurst(int chain, UInt8* node, const NiPoint3& offset, float seconds, float size) {
	SmokeBurst& b = VolumeBursts[chain];
	if (b.node != node) { NodeAddRef(node); NodeRelease(b.node); b.node = node; }
	b.offset = offset;
	b.size = size;
	const float until = SmokeClock + seconds;
	if (until > b.until) b.until = until;
	b.fresh = true;
	b.pending += Settings.volumeBurstPoints;
	if (chain == ChainPuff) HazeShots++;
}

static void EmitSmoke(SmokeChain& c, const SmokeSource& src, const VolLook& v) {
	if (c.count == ChainMax) {
		memmove(&c.p[0], &c.p[1], sizeof(SmokePoint) * (ChainMax - 1));
		c.count--;
		c.p[0].link = false;
	}
	SmokePoint& s = c.p[c.count];
	const float size = v.size * src.size;
	const float life = v.life + v.lifeVar * (Random01() * 2.0f - 1.0f);
	const float speed = v.speed + v.speedVar * (Random01() * 2.0f - 1.0f);
	const float tilt = v.spread * sqrtf(Random01()), turn = 6.2831853f * Random01();
	s.pos = src.at;
	s.vel = NiPoint3{ speed * sinf(tilt) * cosf(turn) + src.carry.x, speed * sinf(tilt) * sinf(turn) + src.carry.y, speed * cosf(tilt) + src.carry.z };
	const float pushSpeed = sqrtf(src.push.x * src.push.x + src.push.y * src.push.y + src.push.z * src.push.z);
	if (pushSpeed > 0.01f) {
		const NiPoint3 d = { src.push.x / pushSpeed, src.push.y / pushSpeed, src.push.z / pushSpeed };
		NiPoint3 u = fabsf(d.z) < 0.9f ? NiPoint3{ -d.y, d.x, 0.0f } : NiPoint3{ 0.0f, -d.z, d.y };
		const float ul = sqrtf(u.x * u.x + u.y * u.y + u.z * u.z);
		u = NiPoint3{ u.x / ul, u.y / ul, u.z / ul };
		const NiPoint3 w = { d.y * u.z - d.z * u.y, d.z * u.x - d.x * u.z, d.x * u.y - d.y * u.x };
		const float jt = v.spread * sqrtf(Random01()), jr = 6.2831853f * Random01();
		const float share = pushSpeed * (0.35f + 0.8f * Random01());
		const float c = cosf(jt), sn = sinf(jt) * cosf(jr), sw = sinf(jt) * sinf(jr);
		s.vel.x += share * (c * d.x + sn * u.x + sw * w.x);
		s.vel.y += share * (c * d.y + sn * u.y + sw * w.y);
		s.vel.z += share * (c * d.z + sn * u.z + sw * w.z);
	}
	s.born = SmokeClock;
	s.life = life > 0.2f ? life : 0.2f;
	s.r0 = size * v.startSize;
	s.r1 = size * Settings.volumeExpand * (1.0f + v.sizeVar * (Random01() - 0.5f) * 0.5f);
	s.grow = v.grow;
	s.strength = src.strength;
	s.opacity = v.opacity * Settings.volumeDensity;
	s.fadeIn = v.fadeIn;
	s.fadeStart = v.fadeStart;
	s.fadeEnd = v.fadeEnd;
	s.seed = Random01() * 100.0f;
	s.tex = SmokeClock * 1.5f + (s.seed - floorf(s.seed)) * 0.5f;
	s.link = c.emitting && c.count > 0;
	s.side = 0.0f;
	s.gunDir = NiPoint3{};
	s.frayed = false;
	s.unzipped = false;
	s.clear = false;
	s.stir = 0.0f;
	s.spread = 1.0f;
	s.kick = NiPoint3{};
	c.count++;
	c.lastEmit = SmokeClock;
}

static const int TendrilMax = 8, TendrilPoints = 4;
static SmokeChain Tendrils[TendrilMax] = {};
static VolLook TendrilLook[TendrilMax] = {};
static bool TendrilDrift[TendrilMax] = {};
static void SpawnTendril(const SmokePoint& parent, float parentRadius, NiPoint3 dir, const VolLook& look, bool drift) {
	if (Settings.volumeTendrils <= 0.0f) return;
	int slot = -1;
	for (int t = 0; t < TendrilMax && slot < 0; t++) if (!Tendrils[t].count) slot = t;
	if (slot < 0) return;
	const float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (dl < 0.001f) return;
	dir = NiPoint3{ dir.x / dl, dir.y / dl, dir.z / dl };
	SmokeChain& c = Tendrils[slot];
	c.count = 0;
	c.emitting = false;
	c.lastEmit = SmokeClock;
	VolLook& v = TendrilLook[slot];
	v = look;
	v.curl *= 2.0f;
	v.life *= 0.45f;
	v.lifeVar *= 0.45f;
	TendrilDrift[slot] = drift;
	const float speed = Settings.volumeTendrilSpeed;
	const float r0 = parentRadius * Settings.volumeTendrilSize;
	for (int k = 0; k < TendrilPoints; k++) {
		SmokePoint& s = c.p[c.count++];
		s = parent;
		s.pos.x += dir.x * r0 * 0.6f * k; s.pos.y += dir.y * r0 * 0.6f * k; s.pos.z += dir.z * r0 * 0.6f * k;
		const float share = speed * (0.3f + 0.35f * k);
		const float jx = (Random01() - 0.5f) * 0.9f, jy = (Random01() - 0.5f) * 0.9f, jz = (Random01() - 0.3f) * 0.9f;
		s.vel.x = parent.vel.x + (dir.x + jx) * share;
		s.vel.y = parent.vel.y + (dir.y + jy) * share;
		s.vel.z = parent.vel.z + (dir.z + jz) * share;
		s.born = SmokeClock;
		s.life = v.life * (0.8f + 0.4f * Random01());
		if (s.life < 0.3f) s.life = 0.3f;
		s.r0 = r0 * (k ? 0.85f : 1.0f);
		s.r1 = s.r0 * 2.5f;
		s.grow = s.life;
		s.opacity = parent.opacity * 0.35f;
		s.stir = parent.stir * 0.5f;
		s.spread = 1.0f;
		s.kick = NiPoint3{ parent.kick.x * 0.5f, parent.kick.y * 0.5f, parent.kick.z * 0.5f };
		s.fadeIn = 0.15f; s.fadeStart = 0.2f; s.fadeEnd = 0.9f;
		s.seed = Random01() * 100.0f;
		s.tex = parent.tex + 0.3f * k;
		s.side = 0.0f;
		s.gunDir = NiPoint3{};
		s.link = k > 0;
		s.frayed = true;
		s.unzipped = false;
	}
}

static const float UnzipAge = 1.0f;
static SmokeChain Twins[2] = {};
static float ZipSide[2] = { 1.0f, 1.0f };
static float ZipLast[2] = { -10.0f, -10.0f };
static float TwinParent[2] = { -1.0f, -1.0f };

static void Unzip(SmokeChain& c, int i, int zip, float across, float here, float rightX, float rightY, float rightZ = 0.0f) {
	SmokePoint& s = c.p[i];
	const int z = zip - 1;
	if (SmokeClock - ZipLast[z] > 0.5f) ZipSide[z] = fabsf(across) > 0.3f * here ? (across > 0.0f ? 1.0f : -1.0f) : 1.0f;
	ZipLast[z] = SmokeClock;
	s.unzipped = true;
	s.side = ZipSide[z];
	s.opacity *= 0.5f;
	if (s.stir < 0.5f) s.stir = 0.5f;
	const float fan = Settings.volumeFanSpeed * ZipSide[z];
	s.vel.x += rightX * fan;
	s.vel.y += rightY * fan;
	s.vel.z += rightZ * fan;
	SmokeChain& t = Twins[z];
	if (t.count == ChainMax) {
		memmove(&t.p[0], &t.p[1], sizeof(SmokePoint) * (ChainMax - 1));
		t.count--;
		t.p[0].link = false;
	}
	SmokePoint& w = t.p[t.count];
	w = s;
	w.side = -ZipSide[z];
	w.vel.x -= rightX * fan * 2.0f;
	w.vel.y -= rightY * fan * 2.0f;
	w.vel.z -= rightZ * fan * 2.0f;
	const bool afterOlder = i > 0 && s.link && c.p[i - 1].born == TwinParent[z];
	const bool afterNewer = i + 1 < c.count && c.p[i + 1].link && c.p[i + 1].born == TwinParent[z];
	w.link = t.count > 0 && (afterOlder || afterNewer);
	t.count++;
	TwinParent[z] = s.born;
}

static float ChooseSide(const SmokeChain& c, int i, float across, bool clear) {
	const SmokePoint& s = c.p[i];
	if (clear) return across > 0.0f ? 1.0f : -1.0f;
	if (s.side != 0.0f) return s.side;
	if (i > 0 && s.link && c.p[i - 1].side != 0.0f) return c.p[i - 1].side;
	if (i + 1 < c.count && c.p[i + 1].link && c.p[i + 1].side != 0.0f) return c.p[i + 1].side;
	return across < 0.0f ? -1.0f : 1.0f;
}

static void SpawnTendril(const SmokePoint& parent, float parentRadius, NiPoint3 dir, const VolLook& look, bool drift);
static float BodyShareOf[ChainMax];
static float BodyShare(const NiPoint3& p) {
	if (!ShapeOn || !VolumeGunOn || !VolumePlayerOn) return 1.0f;
#ifdef GUNFX_REPLAY
	if (getenv("OLD_BODY")) return 1.0f;
#endif
	const float speed = sqrtf(VolumePlayerVel.x * VolumePlayerVel.x + VolumePlayerVel.y * VolumePlayerVel.y);
	const float moving = speed < 20.0f ? 0.0f : speed > 80.0f ? 1.0f : (speed - 20.0f) / 60.0f;
	if (moving <= 0.0f) return 1.0f;
	NiPoint3 n;
	const float d = GunSurface(p, &n);
	const float nearGun = d <= 4.0f ? 1.0f : d >= 16.0f ? 0.0f : (16.0f - d) / 12.0f;
	return 1.0f - moving * nearGun;
}
static void KeepOutOfBody(SmokeChain& c, const VolLook* frayLook, bool drift, int zip, float dt) {
	if (!VolumePlayerOn) return;
	const NiPoint3& feet = VolumePlayerFeet;
	const float R = Settings.volumePlayerRadius;
	const float bottom = feet.z, top = feet.z + Settings.volumePlayerHeight;
	const float speed = sqrtf(VolumePlayerVel.x * VolumePlayerVel.x + VolumePlayerVel.y * VolumePlayerVel.y);
	const float flow = speed < 20.0f ? 0.0f : speed > 80.0f ? 1.0f : (speed - 20.0f) / 60.0f;
	const float hx = speed > 1.0f ? VolumePlayerVel.x / speed : 0.0f, hy = speed > 1.0f ? VolumePlayerVel.y / speed : 0.0f;
	const float rx = hy, ry = -hx;
	const float span = 0.5f;
	const float easeTime = 0.05f * (1.0f - flow);
	const float ease = easeTime > 0.001f ? 1.0f - expf(-dt / easeTime) : 1.0f;
	const float maxMove = (speed + 120.0f) * dt;
	for (int i = 0; i < c.count; i++) {
		SmokePoint& s = c.p[i];
		const float age = SmokeClock - s.born;
		const float full = R + 0.7f * ReachRadius(s, age);
		const float dz = s.pos.z < bottom ? bottom - s.pos.z : s.pos.z > top ? s.pos.z - top : 0.0f;
		if (dz >= full) { s.clear = true; continue; }
		const float ox = s.pos.x - feet.x, oy = s.pos.y - feet.y;
		const float dist = sqrtf(ox * ox + oy * oy);
		const float hereFull = sqrtf(full * full - dz * dz);
		if (dist >= hereFull) s.clear = true;
		const float share = BodyShareOf[i];
		if (!s.clear && age <= 0.1f) continue;
		float grown = s.clear ? 1.0f : (age - 0.1f) / span;
		grown = grown > 1.0f ? 1.0f : grown;
		const float reach = full * grown;
		const float ahead = ox * hx + oy * hy;
		const float across = ox * rx + oy * ry;
		if (zip && Settings.volumeUnzip && age >= UnzipAge && flow > 0.5f && !s.unzipped && ahead > 0.0f && dist < hereFull * 3.0f && share > 0.5f)
			Unzip(c, i, zip, across, hereFull, rx, ry);
		if (dz >= reach) continue;
		const float here = sqrtf(reach * reach - dz * dz);
		if (dist >= here) continue;
		if (!s.unzipped) s.side = ChooseSide(c, i, across, fabsf(across) > 0.25f * here);
		float nx, ny;
		if (dist > 0.01f) { nx = feet.x + ox / dist * here; ny = feet.y + oy / dist * here; }
		else { nx = feet.x + (speed > 1.0f ? rx : 1.0f) * s.side * here; ny = feet.y + (speed > 1.0f ? ry : 0.0f) * s.side * here; }
		float mx = (nx - s.pos.x) * ease * share, my = (ny - s.pos.y) * ease * share;
		const float want = sqrtf(mx * mx + my * my);
		if (want > maxMove && want > 0.0001f) {
			const float keep = maxMove / want;
			mx *= keep;
			my *= keep;
			s.opacity *= expf(-(1.0f - keep) * dt / 0.1f);
		}
		s.pos.x += mx;
		s.pos.y += my;
	}
	for (int i = 1; i < c.count; i++) {
		if (!c.p[i].link) continue;
		if (c.p[i].unzipped && c.p[i - 1].unzipped && c.p[i].side == c.p[i - 1].side) continue;
		if (BodyShareOf[i] < 0.5f || BodyShareOf[i - 1] < 0.5f) continue;
		if ((!c.p[i].clear && SmokeClock - c.p[i].born < 0.1f + span) || (!c.p[i - 1].clear && SmokeClock - c.p[i - 1].born < 0.1f + span))
			continue;
		const NiPoint3& a = c.p[i - 1].pos;
		const NiPoint3& b = c.p[i].pos;
		const float ex = b.x - a.x, ey = b.y - a.y;
		const float len2 = ex * ex + ey * ey;
		float u = len2 > 0.0001f ? ((feet.x - a.x) * ex + (feet.y - a.y) * ey) / len2 : 0.0f;
		u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
		const float px = a.x + ex * u - feet.x, py = a.y + ey * u - feet.y, pz = a.z + (b.z - a.z) * u;
		if (px * px + py * py < R * R && pz > bottom - R && pz < top + R) {
			c.p[i].link = false;
			if (frayLook) for (int e = i - 1; e <= i; e++) {
				SmokePoint& t = c.p[e];
				if (t.frayed) continue;
				t.frayed = true;
				if (Random01() > Settings.volumeTendrils) continue;
				SpawnTendril(t, PointRadius(t, SmokeClock - t.born), NiPoint3{ t.pos.x - feet.x, t.pos.y - feet.y, 0.0f }, *frayLook, drift);
			}
		}
	}
}

static float SegmentDistance2(const NiPoint3& p0, const NiPoint3& p1, const NiPoint3& q0, const NiPoint3& q1) {
	const NiPoint3 d1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z }, d2 = { q1.x - q0.x, q1.y - q0.y, q1.z - q0.z };
	const NiPoint3 r = { p0.x - q0.x, p0.y - q0.y, p0.z - q0.z };
	const float a = d1.x * d1.x + d1.y * d1.y + d1.z * d1.z, e = d2.x * d2.x + d2.y * d2.y + d2.z * d2.z;
	const float f = d2.x * r.x + d2.y * r.y + d2.z * r.z;
	float sp = 0.0f, tq = 0.0f;
	if (a <= 0.0001f && e <= 0.0001f) { sp = tq = 0.0f; }
	else if (a <= 0.0001f) { tq = f / e; tq = tq < 0.0f ? 0.0f : tq > 1.0f ? 1.0f : tq; }
	else {
		const float c = d1.x * r.x + d1.y * r.y + d1.z * r.z;
		if (e <= 0.0001f) { sp = -c / a; sp = sp < 0.0f ? 0.0f : sp > 1.0f ? 1.0f : sp; }
		else {
			const float bb = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z, den = a * e - bb * bb;
			sp = den > 0.0001f ? (bb * f - c * e) / den : 0.0f;
			sp = sp < 0.0f ? 0.0f : sp > 1.0f ? 1.0f : sp;
			tq = (bb * sp + f) / e;
			if (tq < 0.0f) { tq = 0.0f; sp = -c / a; sp = sp < 0.0f ? 0.0f : sp > 1.0f ? 1.0f : sp; }
			else if (tq > 1.0f) { tq = 1.0f; sp = (bb - c) / a; sp = sp < 0.0f ? 0.0f : sp > 1.0f ? 1.0f : sp; }
		}
	}
	const NiPoint3 cp = { p0.x + d1.x * sp - (q0.x + d2.x * tq), p0.y + d1.y * sp - (q0.y + d2.y * tq), p0.z + d1.z * sp - (q0.z + d2.z * tq) };
	return cp.x * cp.x + cp.y * cp.y + cp.z * cp.z;
}

static float GunSpeed(float* moving, float* sweeping = nullptr) {
	*moving = 0.0f;
	if (sweeping) *sweeping = 0.0f;
	if (!VolumeGunOn) return 0.0f;
	const NiPoint3& b = VolumeGunBack;
	const float gb = VolumeGunVel.x * b.x + VolumeGunVel.y * b.y + VolumeGunVel.z * b.z;
	const NiPoint3 gunCross = { VolumeGunVel.x - b.x * gb, VolumeGunVel.y - b.y * gb, VolumeGunVel.z - b.z * gb };
	const float crossSpeed = sqrtf(gunCross.x * gunCross.x + gunCross.y * gunCross.y + gunCross.z * gunCross.z);
	const float forward = VolumePlayerOn ? -(VolumePlayerVel.x * b.x + VolumePlayerVel.y * b.y + VolumePlayerVel.z * b.z) : 0.0f;
	const float speed = crossSpeed > forward ? crossSpeed : forward;
	const float m = (speed - 25.0f) / 25.0f, s = (crossSpeed - 25.0f) / 25.0f;
	*moving = m < 0.0f ? 0.0f : m > 1.0f ? 1.0f : m;
	if (sweeping) *sweeping = s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
	return speed;
}

static bool HasWay(const NiPoint3& g);
static NiPoint3 WayRound(SmokeChain& c, int i, const NiPoint3& b, const NiPoint3& side, float across);
static void KeepOutOfGun(SmokeChain& c, const VolLook* frayLook, bool drift, int zip, float dt) {
	if (!VolumeGunOn || !Settings.volumeWrapGun) return;
	const NiPoint3& m = VolumeGunMuzzle;
	const NiPoint3& b = VolumeGunBack;
	const float L = Settings.volumeGunLength, Rg = Settings.volumeGunRadius;
	float moving, sweeping;
	const float gunSpeed = GunSpeed(&moving, &sweeping);
	const float easeTime = 0.04f * (1.0f - sweeping);
	const float ease = easeTime > 0.001f ? 1.0f - expf(-dt / easeTime) : 1.0f;
	const float rb = VolumeAimRight.x * b.x + VolumeAimRight.y * b.y + VolumeAimRight.z * b.z;
	NiPoint3 right = { VolumeAimRight.x - b.x * rb, VolumeAimRight.y - b.y * rb, VolumeAimRight.z - b.z * rb };
	const float rl = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
	right = rl > 0.01f ? NiPoint3{ right.x / rl, right.y / rl, right.z / rl } : NiPoint3{ 1.0f, 0.0f, 0.0f };
	NiPoint3 up = { -b.x * b.z, -b.y * b.z, 1.0f - b.z * b.z };
	const float ul = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
	up = ul > 0.01f ? NiPoint3{ up.x / ul, up.y / ul, up.z / ul } : NiPoint3{ 0.0f, 0.0f, 1.0f };
	const NiPoint3 back = { m.x + b.x * L, m.y + b.y * L, m.z + b.z * L };
	NiPoint3 sweepDir = { VolumeGunVel.x - VolumeWind.x, VolumeGunVel.y - VolumeWind.y, VolumeGunVel.z };
	const float sb = sweepDir.x * b.x + sweepDir.y * b.y + sweepDir.z * b.z;
	sweepDir = NiPoint3{ sweepDir.x - b.x * sb, sweepDir.y - b.y * sb, sweepDir.z - b.z * sb };
	const float sweep = sqrtf(sweepDir.x * sweepDir.x + sweepDir.y * sweepDir.y + sweepDir.z * sweepDir.z);
	if (sweep > 0.01f) sweepDir = NiPoint3{ sweepDir.x / sweep, sweepDir.y / sweep, sweepDir.z / sweep };
	const NiPoint3 sweepSide = { b.y * sweepDir.z - b.z * sweepDir.y, b.z * sweepDir.x - b.x * sweepDir.z, b.x * sweepDir.y - b.y * sweepDir.x };
	for (int i = 0; i < c.count; i++) {
		SmokePoint& s = c.p[i];
		const float age = SmokeClock - s.born;
		if (ShapeOn) {
			const float r = ReachRadius(s, age);
			const NiPoint3 fm = { s.pos.x - m.x, s.pos.y - m.y, s.pos.z - m.z };
			const float fb = fm.x * b.x + fm.y * b.y + fm.z * b.z;
			const NiPoint3 fr = { fm.x - b.x * fb, fm.y - b.y * fb, fm.z - b.z * fb };
			const bool inBore = fb > 0.0f && fb < VolumeMuzzleDepth + 0.5f && fr.x * fr.x + fr.y * fr.y + fr.z * fr.z < 2.25f;
			const bool tip = inBore || fm.x * fm.x + fm.y * fm.y + fm.z * fm.z < (2.0f + r) * (2.0f + r);
			if (tip && age <= 0.1f) continue;
			NiPoint3 n;
			const float sd = GunSurface(s.pos, &n);
			const float keep = 0.6f * r;
			if (zip && Settings.volumeUnzip && age >= UnzipAge && gunSpeed > 40.0f && !s.unzipped && sd < 1.3f * keep + 1.0f)
				Unzip(c, i, zip, fm.x * right.x + fm.y * right.y + fm.z * right.z, keep + 1.0f, right.x, right.y, right.z);
			if (sd >= keep || (n.x == 0.0f && n.y == 0.0f && n.z == 0.0f)) continue;
			float grown = (age - 0.1f) / 0.4f;
			if (!tip && grown < moving) grown = moving;
			grown = grown < 0.0f ? 0.0f : grown > 1.0f ? 1.0f : grown;
			const float allowed = keep - (1.0f - grown) * (keep + 3.0f);
			if (sd >= allowed) continue;
			NiPoint3 dir = n;
			if (sd < 0.0f) {
				NiPoint3 own = up;
				if (HasWay(s.gunDir)) {
					const float gb2 = s.gunDir.x * b.x + s.gunDir.y * b.y + s.gunDir.z * b.z;
					const NiPoint3 g = { s.gunDir.x - b.x * gb2, s.gunDir.y - b.y * gb2, s.gunDir.z - b.z * gb2 };
					const float gl = sqrtf(g.x * g.x + g.y * g.y + g.z * g.z);
					if (gl > 0.05f) own = NiPoint3{ g.x / gl, g.y / gl, g.z / gl };
				}
				else if (s.side != 0.0f) own = NiPoint3{ right.x * s.side, right.y * s.side, right.z * s.side };
				const float lean = -sd < 2.0f ? -sd * 0.25f : 0.5f;
				dir = NiPoint3{ n.x + own.x * lean, n.y + own.y * lean, n.z + own.z * lean };
				const float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
				dir = dl > 0.001f ? NiPoint3{ dir.x / dl, dir.y / dl, dir.z / dl } : n;
			}
			const float step = (allowed - sd) * ease;
			s.pos.x += dir.x * step; s.pos.y += dir.y * step; s.pos.z += dir.z * step;
			continue;
		}
		const float alongRaw = (s.pos.x - m.x) * b.x + (s.pos.y - m.y) * b.y + (s.pos.z - m.z) * b.z;
		const NiPoint3 tr = { s.pos.x - m.x - b.x * alongRaw, s.pos.y - m.y - b.y * alongRaw, s.pos.z - m.z - b.z * alongRaw };
		const bool atTip = alongRaw < 0.5f || (alongRaw < 0.5f + VolumeMuzzleDepth && tr.x * tr.x + tr.y * tr.y + tr.z * tr.z < 2.25f);
		if (atTip && age <= 0.1f) continue;
		const float along = alongRaw < 0.0f ? 0.0f : alongRaw > L ? L : alongRaw;
		const NiPoint3 axisPoint = { m.x + b.x * along, m.y + b.y * along, m.z + b.z * along };
		NiPoint3 o = { s.pos.x - axisPoint.x, s.pos.y - axisPoint.y, s.pos.z - axisPoint.z };
		const float d = sqrtf(o.x * o.x + o.y * o.y + o.z * o.z);
		const float full = Rg + 0.6f * ReachRadius(s, age);
		if (zip && Settings.volumeUnzip && age >= UnzipAge && gunSpeed > 40.0f && !s.unzipped && d < full * 1.3f) {
			const float across = o.x * right.x + o.y * right.y + o.z * right.z;
			Unzip(c, i, zip, across, full, right.x, right.y, right.z);
		}
		if (d >= full) continue;
		float grown = (age - 0.1f) / 0.4f;
		if (!atTip && grown < moving) grown = moving;
		grown = grown < 0.0f ? 0.0f : grown > 1.0f ? 1.0f : grown;
		const float reach = full * grown;
		if (d >= reach) continue;
		NiPoint3 own = up;
		if (HasWay(s.gunDir)) {
			const float gb2 = s.gunDir.x * b.x + s.gunDir.y * b.y + s.gunDir.z * b.z;
			const NiPoint3 g = { s.gunDir.x - b.x * gb2, s.gunDir.y - b.y * gb2, s.gunDir.z - b.z * gb2 };
			const float gl = sqrtf(g.x * g.x + g.y * g.y + g.z * g.z);
			if (gl > 0.05f) own = NiPoint3{ g.x / gl, g.y / gl, g.z / gl };
		}
		else if (s.side != 0.0f) own = NiPoint3{ right.x * s.side, right.y * s.side, right.z * s.side };
		const float lean = 0.35f * reach - d;
		NiPoint3 dir = o;
		if (lean > 0.0f) { dir.x += own.x * lean; dir.y += own.y * lean; dir.z += own.z * lean; }
		const float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		dir = dl > 0.001f ? NiPoint3{ dir.x / dl, dir.y / dl, dir.z / dl } : own;
		NiPoint3 to = { axisPoint.x + dir.x * reach, axisPoint.y + dir.y * reach, axisPoint.z + dir.z * reach };
		if (sweep > 40.0f && d > 0.01f) {
			const float ahead = (o.x * -sweepDir.x + o.y * -sweepDir.y + o.z * -sweepDir.z) / d;
			if (ahead > 0.0f) {
				const float across = (o.x * sweepSide.x + o.y * sweepSide.y + o.z * sweepSide.z) / d;
				const NiPoint3 g = WayRound(c, i, b, sweepSide, across);
				const float w = ahead * (sweep < 80.0f ? (sweep - 40.0f) / 40.0f : 1.0f);
				const NiPoint3 round = { axisPoint.x + g.x * reach, axisPoint.y + g.y * reach, axisPoint.z + g.z * reach };
				to.x += (round.x - to.x) * w; to.y += (round.y - to.y) * w; to.z += (round.z - to.z) * w;
				NiPoint3 e = { to.x - axisPoint.x, to.y - axisPoint.y, to.z - axisPoint.z };
				const float el = sqrtf(e.x * e.x + e.y * e.y + e.z * e.z);
				if (el > 0.01f && el < reach) { to.x = axisPoint.x + e.x / el * reach; to.y = axisPoint.y + e.y / el * reach; to.z = axisPoint.z + e.z / el * reach; }
			}
		}
		s.pos.x += (to.x - s.pos.x) * ease; s.pos.y += (to.y - s.pos.y) * ease; s.pos.z += (to.z - s.pos.z) * ease;
	}
	for (int i = 1; i < c.count; i++) {
		if (!c.p[i].link) continue;
		if (c.p[i].unzipped && c.p[i - 1].unzipped && c.p[i].side == c.p[i - 1].side) continue;
		if (SmokeClock - c.p[i].born < 0.1f || SmokeClock - c.p[i - 1].born < 0.1f) continue;
		if (moving < 1.0f && (SmokeClock - c.p[i].born < 0.5f || SmokeClock - c.p[i - 1].born < 0.5f)) continue;
		{
			const NiPoint3& g0 = c.p[i - 1].gunDir;
			const NiPoint3& g1 = c.p[i].gunDir;
			if (g0.x * g1.x + g0.y * g1.y + g0.z * g1.z > 0.5f) continue;
		}
		if (ShapeOn) {
			const NiPoint3& p0 = c.p[i - 1].pos;
			const NiPoint3& p1 = c.p[i].pos;
			bool through = false;
			for (int k = 1; k <= 3 && !through; k++) {
				const float w = 0.25f * k;
				NiPoint3 nn;
				through = GunSurface(NiPoint3{ p0.x + (p1.x - p0.x) * w, p0.y + (p1.y - p0.y) * w, p0.z + (p1.z - p0.z) * w }, &nn) < 0.0f;
			}
			if (!through) continue;
		}
		else if (SegmentDistance2(c.p[i - 1].pos, c.p[i].pos, m, back) >= Rg * Rg) continue;
		c.p[i].link = false;
		if (frayLook) for (int e = i - 1; e <= i; e++) {
			SmokePoint& t = c.p[e];
			if (t.frayed) continue;
			t.frayed = true;
			if (Random01() > Settings.volumeTendrils) continue;
			NiPoint3 out;
			if (ShapeOn) GunSurface(t.pos, &out);
			else {
				float al = (t.pos.x - m.x) * b.x + (t.pos.y - m.y) * b.y + (t.pos.z - m.z) * b.z;
				al = al < 0.0f ? 0.0f : al > L ? L : al;
				out = NiPoint3{ t.pos.x - (m.x + b.x * al), t.pos.y - (m.y + b.y * al), t.pos.z - (m.z + b.z * al) };
			}
			SpawnTendril(t, PointRadius(t, SmokeClock - t.born), out, *frayLook, drift);
		}
	}
}

static bool HasWay(const NiPoint3& g) { return g.x != 0.0f || g.y != 0.0f || g.z != 0.0f; }
static NiPoint3 WayRound(SmokeChain& c, int i, const NiPoint3& b, const NiPoint3& side, float across) {
	SmokePoint& s = c.p[i];
	if (!HasWay(s.gunDir)) {
		const bool clear = fabsf(across) > 0.3f;
		if (!clear && i > 0 && s.link && HasWay(c.p[i - 1].gunDir)) s.gunDir = c.p[i - 1].gunDir;
		else if (!clear && i + 1 < c.count && c.p[i + 1].link && HasWay(c.p[i + 1].gunDir)) s.gunDir = c.p[i + 1].gunDir;
		else {
			const float sign = across < 0.0f ? -1.0f : 1.0f;
			s.gunDir = NiPoint3{ side.x * sign, side.y * sign, side.z * sign };
		}
	}
	const float gb = s.gunDir.x * b.x + s.gunDir.y * b.y + s.gunDir.z * b.z;
	NiPoint3 g = { s.gunDir.x - b.x * gb, s.gunDir.y - b.y * gb, s.gunDir.z - b.z * gb };
	const float gl = sqrtf(g.x * g.x + g.y * g.y + g.z * g.z);
	return gl > 0.05f ? NiPoint3{ g.x / gl, g.y / gl, g.z / gl } : side;
}

static int JumpsLogged = 0;
static void NoteJumps(const SmokeChain& c, const NiPoint3* before, const char* rule, float pace, float dt) {
	if (JumpsLogged >= 40) return;
	const float carried = 1.5f * pace * dt + 1.0f;
	const float limit = carried > 4.0f ? carried : 4.0f;
	for (int i = 0; i < c.count; i++) {
		const NiPoint3& p = c.p[i].pos;
		const float dx = p.x - before[i].x, dy = p.y - before[i].y, dz = p.z - before[i].z;
		const float moved2 = dx * dx + dy * dy + dz * dz;
		if (moved2 <= limit * limit) continue;
		const char* name = "tendril";
		for (int k = 0; k < ChainCount; k++) if (&c == &Chains[k]) name = ChainNames[k];
		if (&c == &Twins[0] || &c == &Twins[1]) name = "twin arm";
		JumpsLogged++;
		LogLine("volumetric smoke jump: %s point %.2f s old moved %.1f units in one frame by %s (gun moving %.0f units/s, you %.0f; frame %.0f ms)",
			name, SmokeClock - c.p[i].born, sqrtf(moved2), rule,
			sqrtf(VolumeGunVel.x * VolumeGunVel.x + VolumeGunVel.y * VolumeGunVel.y + VolumeGunVel.z * VolumeGunVel.z),
			sqrtf(VolumePlayerVel.x * VolumePlayerVel.x + VolumePlayerVel.y * VolumePlayerVel.y), dt * 1000.0f);
		return;
	}
}

static NiPoint3 PoleFlow(const NiPoint3& o, float d, float a, const NiPoint3& U, float u, const NiPoint3& way) {
	if (d < 0.01f || d >= 4.0f * a || u < 1.0f) return NiPoint3{};
	const NiPoint3 er = { o.x / d, o.y / d, o.z / d };
	const float q = d > a ? a * a / (d * d) : 1.0f, ue = U.x * er.x + U.y * er.y + U.z * er.z;
	NiPoint3 v = { q * (U.x - 2.0f * ue * er.x), q * (U.y - 2.0f * ue * er.y), q * (U.z - 2.0f * ue * er.z) };
	if (ue < 0.0f) {
		const float across = er.x * way.x + er.y * way.y + er.z * way.z;
		const float turn = u * (-ue / u) * (1.0f - fabsf(across)) * (d > a ? a / d : 1.0f);
		v.x += way.x * turn; v.y += way.y * turn; v.z += way.z * turn;
	}
	if (d > 3.0f * a) {
		const float f = 1.0f - (d - 3.0f * a) / a;
		const float s = f * f * (3.0f - 2.0f * f);
		v.x *= s; v.y *= s; v.z *= s;
	}
	return v;
}

static void NotePushes(const SmokeChain& c, const NiPoint3* before, float* stirIn, NiPoint3* stirDir, float dt) {
	if (dt <= 0.0001f) return;
	for (int i = 0; i < c.count; i++) {
		const NiPoint3 m = { (c.p[i].pos.x - before[i].x) / dt, (c.p[i].pos.y - before[i].y) / dt, (c.p[i].pos.z - before[i].z) / dt };
		const float speed = sqrtf(m.x * m.x + m.y * m.y + m.z * m.z);
		if (speed < 1.0f) continue;
		stirIn[i] += speed / 80.0f;
		stirDir[i] = NiPoint3{ stirDir[i].x + m.x, stirDir[i].y + m.y, stirDir[i].z + m.z };
	}
}

static void Diffuse(SmokeChain& c, const float* stirIn, const NiPoint3* stirDir, float dt, const VolLook* frayLook, bool drift) {
	const float k = Settings.volumeDiffuse;
	const float rise = 1.0f - expf(-dt / 0.08f), settle = expf(-dt / 0.8f);
	for (int i = 0; i < c.count; i++) {
		SmokePoint& s = c.p[i];
		const float kick = sqrtf(s.kick.x * s.kick.x + s.kick.y * s.kick.y + s.kick.z * s.kick.z);
		if (kick > 1.0f && s.spread < 3.5f) {
			float b = expf(3.0f * kick / 80.0f * dt);
			if (s.spread * b > 3.5f) b = 3.5f / s.spread;
			s.spread *= b;
			s.opacity /= powf(b, 0.4f);
		}
		if (k <= 0.0f) continue;
		const float in = stirIn[i] < 1.5f ? stirIn[i] : 1.5f;
		s.stir = in > s.stir ? s.stir + (in - s.stir) * rise : s.stir * settle;
		if (s.stir < 0.01f) continue;
		float f = expf(1.2f * k * s.stir * dt);
		if (s.spread * f > 3.0f) f = 3.0f / s.spread;
		if (f > 1.0f) { s.spread *= f; s.opacity /= powf(f, 1.6f); }
		if (frayLook && !s.frayed && s.stir > 0.7f && Random01() < Settings.volumeTendrils * s.stir * k * dt * 1.5f) {
			s.frayed = true;
			SpawnTendril(s, PointRadius(s, SmokeClock - s.born), stirDir[i], *frayLook, drift);
		}
	}
}

static void WantBurst(NiPoint3& want, const NiPoint3& out, const NiPoint3& fwd, float u, float hit) {
	const float k = Settings.volumeBurst * u * (hit > 1.0f ? 1.0f : hit);
	if (k <= 0.0f) return;
	const float facing = out.x * fwd.x + out.y * fwd.y + out.z * fwd.z;
	float outward = facing + 0.6f;
	outward = outward < 0.0f ? 0.0f : outward > 1.0f ? 1.0f : outward;
	const float along = 0.3f + 0.3f * (facing < 0.0f ? -facing : 0.0f);
	const NiPoint3 v = { (out.x * 0.85f * outward + fwd.x * along) * k, (out.y * 0.85f * outward + fwd.y * along) * k,
		(out.z * 0.85f * outward + fwd.z * along + 0.25f) * k };
	if (v.x * v.x + v.y * v.y + v.z * v.z > want.x * want.x + want.y * want.y + want.z * want.z) want = v;
}

struct Bullet { NiPoint3 from, dir; float at; };
static const int BulletMax = 16;
static Bullet Bullets[BulletMax] = {};
static int BulletNext = 0;
static NiPoint3 ViewEye = {}, ViewForward = {};
static ULONGLONG ViewAt = 0;
static void NoteBullet(const NiPoint3& muzzle) {
	if (Settings.volumeBullet <= 0.0f) return;
	NiPoint3 dir = { -VolumeGunBack.x, -VolumeGunBack.y, -VolumeGunBack.z };
	bool view = false;
	NiPoint3 eye = {}, fwd = {};
	if (HeatLockReady) {
		EnterCriticalSection(&HeatLock);
		view = ViewAt && GetTickCount64() - ViewAt < 1000;
		eye = ViewEye; fwd = ViewForward;
		LeaveCriticalSection(&HeatLock);
	}
	if (view) {
		const NiPoint3 aim = { eye.x + fwd.x * 1500.0f - muzzle.x, eye.y + fwd.y * 1500.0f - muzzle.y, eye.z + fwd.z * 1500.0f - muzzle.z };
		const float l = sqrtf(aim.x * aim.x + aim.y * aim.y + aim.z * aim.z);
		if (l > 1.0f) dir = NiPoint3{ aim.x / l, aim.y / l, aim.z / l };
	}
	const float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (dl < 0.5f) return;
	Bullets[BulletNext] = Bullet{ muzzle, NiPoint3{ dir.x / dl, dir.y / dl, dir.z / dl }, SmokeClock };
	BulletNext = (BulletNext + 1) % BulletMax;
}
static void BulletBurst(NiPoint3& want, const SmokePoint& s, float age) {
	if (Settings.volumeBullet <= 0.0f) return;
	for (const Bullet& b : Bullets) {
		const float since = SmokeClock - b.at;
		if (b.at <= 0.0f || since < 0.0f || since > 0.08f || s.born >= b.at) continue;
		const NiPoint3 rel = { s.pos.x - b.from.x, s.pos.y - b.from.y, s.pos.z - b.from.z };
		const float along = rel.x * b.dir.x + rel.y * b.dir.y + rel.z * b.dir.z;
		if (along < 0.0f || along > 1500.0f) continue;
		NiPoint3 off = { rel.x - b.dir.x * along, rel.y - b.dir.y * along, rel.z - b.dir.z * along };
		const float d = sqrtf(off.x * off.x + off.y * off.y + off.z * off.z);
		const float reach = Settings.volumeBulletReach + 0.5f * PointRadius(s, age);
		if (d >= reach) continue;
		if (d < 0.01f) {
			off = fabsf(b.dir.z) < 0.9f ? NiPoint3{ -b.dir.y, b.dir.x, 0.0f } : NiPoint3{ 1.0f, 0.0f, 0.0f };
			const float ol = sqrtf(off.x * off.x + off.y * off.y + off.z * off.z);
			off = NiPoint3{ off.x / ol * 0.01f, off.y / ol * 0.01f, off.z / ol * 0.01f };
		}
		const float dd = d > 0.01f ? d : 0.01f;
		float hit = 1.0f - d / reach;
		hit = hit * hit * (3.0f - 2.0f * hit);
		const float k = Settings.volumeBullet * 90.0f * hit;
		const NiPoint3 v = { off.x / dd * k + b.dir.x * 0.4f * k, off.y / dd * k + b.dir.y * 0.4f * k, off.z / dd * k + b.dir.z * 0.4f * k };
		if (v.x * v.x + v.y * v.y + v.z * v.z > want.x * want.x + want.y * want.y + want.z * want.z) want = v;
	}
}

static void KeepMomentum(NiPoint3& want, const NiPoint3& flow) {
	const float k = Settings.volumeBurst * 1.6f;
	if (k <= 0.0f) return;
	const float fl = sqrtf(flow.x * flow.x + flow.y * flow.y + flow.z * flow.z);
	const NiPoint3 v = { flow.x * k, flow.y * k, flow.z * k + fl * k * 0.3f };
	if (v.x * v.x + v.y * v.y + v.z * v.z > want.x * want.x + want.y * want.y + want.z * want.z) want = v;
}

static void UpdateChain(SmokeChain& c, const SmokeSource& src, float dt, const VolLook& v) {
	const float rise = v.rise * Settings.volumeRise;
	const float curl = v.curl * Settings.volumeCurl;
	const float keep = expf(-src.drag * dt);
	static float stirIn[ChainMax];
	static NiPoint3 stirDir[ChainMax];
	for (int i = 0; i < c.count; i++) {
		SmokePoint& s = c.p[i];
		const float age = SmokeClock - s.born;
		stirIn[i] = 0.0f;
		stirDir[i] = NiPoint3{};
		const float bodyShare = BodyShare(s.pos);
		BodyShareOf[i] = bodyShare;
		const float w = s.seed + SmokeClock * 0.6f;
		const float stirred = Settings.volumeDiffuse * s.stir;
		const float push = curl * (0.3f + age) * (1.0f + 3.0f * stirred);
		s.vel.x += (sinf(w + s.pos.z * 0.05f) + 0.5f * sinf(w * 2.3f + s.pos.z * 0.11f)) * push * dt;
		s.vel.y += (cosf(w * 0.8f + s.pos.z * 0.06f) + 0.5f * sinf(w * 1.7f + 1.3f)) * push * dt;
		s.vel.z += rise * dt + (sinf(w * 1.3f + 2.1f) + 0.5f * sinf(w * 2.9f + s.pos.x * 0.07f)) * push * stirred * 0.6f * dt;
		const float windKeep = keep * VolumeWindKeep;
		s.vel.x = VolumeWind.x + (s.vel.x - VolumeWind.x) * windKeep;
		s.vel.y = VolumeWind.y + (s.vel.y - VolumeWind.y) * windKeep;
		s.vel.z *= keep;
		if (VolumePlayerOn && (s.clear || age > 0.1f)) {
			const float R = Settings.volumePlayerRadius;
			float cz = s.pos.z;
			const float lo = VolumePlayerFeet.z + R, hi = VolumePlayerFeet.z + Settings.volumePlayerHeight - R;
			cz = cz < lo ? lo : cz > hi ? hi : cz;
			const float dx = s.pos.x - VolumePlayerFeet.x, dy = s.pos.y - VolumePlayerFeet.y, dz = s.pos.z - cz;
			const float reach = R + 0.7f * ReachRadius(s, age);
			const float d2 = dx * dx + dy * dy + dz * dz;
			if (d2 < reach * reach) {
				const float d = sqrtf(d2);
				float overlap = (1.0f - d / reach) * bodyShare;
				if (!s.clear && age < 0.4f) overlap *= (age - 0.1f) / 0.3f;
				const NiPoint3 out = d > 0.01f ? NiPoint3{ dx / d, dy / d, dz / d } : NiPoint3{ 1.0f, 0.0f, 0.0f };
				const float shove = Settings.volumePlayerPush * overlap;
				NiPoint3 target = { out.x * shove, out.y * shove, out.z * shove };
				const float speed = sqrtf(VolumePlayerVel.x * VolumePlayerVel.x + VolumePlayerVel.y * VolumePlayerVel.y);
				const float flow = speed < 20.0f ? 0.0f : speed > 80.0f ? 1.0f : (speed - 20.0f) / 60.0f;
				if (flow > 0.0f) {
					const float fx = VolumePlayerVel.x / speed, fy = VolumePlayerVel.y / speed;
					const float rx = fy, ry = -fx;
					const float ahead = out.x * fx + out.y * fy;
					const float across = out.x * rx + out.y * ry;
					if (!s.unzipped) s.side = ChooseSide(c, i, across, fabsf(across) > 0.25f);
					const float aside = shove + speed * 0.5f;
					NiPoint3 flowing = { rx * s.side * aside, ry * s.side * aside, target.z };
					if (ahead < 0.0f) { flowing.x += fx * speed * 0.5f; flowing.y += fy * speed * 0.5f; }
					target.x += (flowing.x - target.x) * flow;
					target.y += (flowing.y - target.y) * flow;
				}
				const float k = 1.0f - expf(-10.0f * overlap * dt);
				s.vel.x += (target.x - s.vel.x) * k;
				s.vel.y += (target.y - s.vel.y) * k;
				s.vel.z += (target.z - s.vel.z) * k;
				s.opacity *= expf(-Settings.volumeStir * overlap * dt);
				stirIn[i] += overlap;
			}
		}
		if (src.drift && VolumeGunOn) {
			float grown = age / 0.6f;
			grown = grown < 0.0f ? 0.0f : grown > 1.0f ? 1.0f : grown;
			const float k = 1.0f - expf(-1.5f * grown * dt);
			const float rx = s.vel.x - VolumeWind.x, ry = s.vel.y - VolumeWind.y;
			const float right = rx * VolumeAimRight.x + ry * VolumeAimRight.y;
			if (right < Settings.volumeDriftRight) {
				s.vel.x += VolumeAimRight.x * (Settings.volumeDriftRight - right) * k;
				s.vel.y += VolumeAimRight.y * (Settings.volumeDriftRight - right) * k;
			}
			const float away = rx * VolumeAimForward.x + ry * VolumeAimForward.y;
			if (away < Settings.volumeDriftAway) {
				s.vel.x += VolumeAimForward.x * (Settings.volumeDriftAway - away) * k;
				s.vel.y += VolumeAimForward.y * (Settings.volumeDriftAway - away) * k;
			}
		}
		NiPoint3 around = {};
		NiPoint3 burst = {};
		if (!src.haze) BulletBurst(burst, s, age);
		if (ShapeOn && VolumeGunOn && Settings.volumeWrapGun && age > 0.1f) {
			NiPoint3 n;
			const float dist = GunSurface(s.pos, &n);
			const float L = Settings.volumeGunRadius > 0.5f ? Settings.volumeGunRadius : 0.5f;
			if (dist < 4.0f * L && (n.x != 0.0f || n.y != 0.0f || n.z != 0.0f)) {
				const NiPoint3 U = { VolumeWind.x - VolumeGunVel.x, VolumeWind.y - VolumeGunVel.y, -VolumeGunVel.z };
				const float u = sqrtf(U.x * U.x + U.y * U.y + U.z * U.z);
				const float un = U.x * n.x + U.y * n.y + U.z * n.z;
				{
					const NiPoint3& gb = VolumeGunBack;
					const float slide = fabsf(U.x * gb.x + U.y * gb.y + U.z * gb.z);
					const float nearGun = dist < 2.0f * L ? 1.0f - (dist > 0.0f ? dist : 0.0f) / (2.0f * L) : 0.0f;
					const float rub = slide < 30.0f ? 0.0f : slide > 150.0f ? 1.0f : (slide - 30.0f) / 120.0f;
#ifdef GUNFX_REPLAY
					if (!getenv("OLD_BODY") && !getenv("NO_RUB"))
#endif
					stirIn[i] += 0.8f * nearGun * rub;
				}
				if (u > 40.0f && dist < 1.5f * L) {
					const float hit = 1.0f - (dist > 0.0f ? dist : 0.0f) / (1.5f * L);
					WantBurst(burst, n, NiPoint3{ -U.x / u, -U.y / u, -U.z / u }, u, hit);
				}
				if (u > 15.0f && un < 0.0f) {
					const float close = L / (L + (dist > 0.0f ? dist : 0.0f));
					NiPoint3 f = { -un * n.x * close * close, -un * n.y * close * close, -un * n.z * close * close };
					if (u > 40.0f) {
						const NiPoint3& b = VolumeGunBack;
						const float ub = U.x * b.x + U.y * b.y + U.z * b.z;
						const NiPoint3 Uc = { U.x - b.x * ub, U.y - b.y * ub, U.z - b.z * ub };
						const float uc = sqrtf(Uc.x * Uc.x + Uc.y * Uc.y + Uc.z * Uc.z);
						if (uc > 1.0f) {
							const NiPoint3 S = { (b.y * Uc.z - b.z * Uc.y) / uc, (b.z * Uc.x - b.x * Uc.z) / uc, (b.x * Uc.y - b.y * Uc.x) / uc };
							const float across = n.x * S.x + n.y * S.y + n.z * S.z;
							NiPoint3 g = WayRound(c, i, b, S, across);
							const float gn = g.x * n.x + g.y * n.y + g.z * n.z;
							g = NiPoint3{ g.x - n.x * gn, g.y - n.y * gn, g.z - n.z * gn };
							const float gl = sqrtf(g.x * g.x + g.y * g.y + g.z * g.z);
							if (gl > 0.05f) {
								const float turn = (-un) * (1.0f - fabsf(across)) * close / gl;
								f.x += g.x * turn; f.y += g.y * turn; f.z += g.z * turn;
							}
						}
					}
					float k = u < 30.0f ? (u - 15.0f) / 15.0f : 1.0f;
					if (dist > 3.0f * L) { const float e = 1.0f - (dist - 3.0f * L) / L; k *= e * e * (3.0f - 2.0f * e); }
					around.x += f.x * k; around.y += f.y * k; around.z += f.z * k;
					if (u > 40.0f) KeepMomentum(burst, NiPoint3{ f.x * k, f.y * k, f.z * k });
				}
			}
		}
		else if (VolumeGunOn && Settings.volumeWrapGun && age > 0.1f) {
			const NiPoint3& m = VolumeGunMuzzle;
			const NiPoint3& b = VolumeGunBack;
			const float along = (s.pos.x - m.x) * b.x + (s.pos.y - m.y) * b.y + (s.pos.z - m.z) * b.z;
			if (along > 0.0f && along < Settings.volumeGunLength) {
				const NiPoint3 o = { s.pos.x - (m.x + b.x * along), s.pos.y - (m.y + b.y * along), s.pos.z - (m.z + b.z * along) };
				const float d = sqrtf(o.x * o.x + o.y * o.y + o.z * o.z);
				const float a = Settings.volumeGunRadius + 0.6f * ReachRadius(s, age);
				NiPoint3 U = { VolumeWind.x - VolumeGunVel.x, VolumeWind.y - VolumeGunVel.y, -VolumeGunVel.z };
				const float ub = U.x * b.x + U.y * b.y + U.z * b.z;
				U = NiPoint3{ U.x - b.x * ub, U.y - b.y * ub, U.z - b.z * ub };
				const float u = sqrtf(U.x * U.x + U.y * U.y + U.z * U.z);
				if (u > 15.0f && d > 0.01f && d < 4.0f * a) {
					NiPoint3 way = {};
					const NiPoint3 uh = { U.x / u, U.y / u, U.z / u };
					if (u > 40.0f) {
						const NiPoint3 S = { b.y * uh.z - b.z * uh.y, b.z * uh.x - b.x * uh.z, b.x * uh.y - b.y * uh.x };
						way = WayRound(c, i, b, S, (o.x * S.x + o.y * S.y + o.z * S.z) / d);
					}
					const float ramp = u < 30.0f ? (u - 15.0f) / 15.0f : 1.0f;
					const NiPoint3 f = PoleFlow(o, d, a, U, u, way);
					around.x += f.x * ramp; around.y += f.y * ramp; around.z += f.z * ramp;
					if (u > 40.0f) KeepMomentum(burst, NiPoint3{ f.x * ramp, f.y * ramp, f.z * ramp });
					const float gap = d - a, reachOut = 1.5f * Settings.volumeGunRadius;
					if (u > 40.0f && gap < reachOut)
						WantBurst(burst, NiPoint3{ o.x / d, o.y / d, o.z / d }, NiPoint3{ -uh.x, -uh.y, -uh.z }, u, 1.0f - (gap > 0.0f ? gap : 0.0f) / reachOut);
				}
			}
		}
		if (VolumePlayerOn && Settings.volumePlayerPush > 0.0f && (s.clear || age > 0.1f)) {
			const float R = Settings.volumePlayerRadius;
			const float bottom = VolumePlayerFeet.z, top = VolumePlayerFeet.z + Settings.volumePlayerHeight;
			const float a = R + 0.7f * ReachRadius(s, age);
			const float dz = s.pos.z < bottom ? bottom - s.pos.z : s.pos.z > top ? s.pos.z - top : 0.0f;
			const NiPoint3 U = { VolumeWind.x - VolumePlayerVel.x, VolumeWind.y - VolumePlayerVel.y, 0.0f };
			const float u = sqrtf(U.x * U.x + U.y * U.y);
			const NiPoint3 o = { s.pos.x - VolumePlayerFeet.x, s.pos.y - VolumePlayerFeet.y, 0.0f };
			const float d = sqrtf(o.x * o.x + o.y * o.y);
			if (u > 15.0f && dz < a && d > 0.01f && d < 4.0f * a) {
				const NiPoint3 S = { -U.y / u, U.x / u, 0.0f };
				const float across = (o.x * S.x + o.y * S.y) / d;
				if (!s.unzipped) s.side = ChooseSide(c, i, across, fabsf(across) > 0.25f);
				const NiPoint3 way = { S.x * s.side, S.y * s.side, 0.0f };
				float k = u < 30.0f ? (u - 15.0f) / 15.0f : 1.0f;
				k *= (1.0f - dz / a) * bodyShare;
				if (!s.clear && age < 0.4f) k *= (age - 0.1f) / 0.3f;
				NiPoint3 f = PoleFlow(o, d, a, U, u, way);
				const float ue = (U.x * o.x + U.y * o.y) / d;
				const float radial = (f.x * o.x + f.y * o.y) / d;
				if (ue > 0.0f && radial < 0.0f) { f.x -= radial * o.x / d; f.y -= radial * o.y / d; }
				around.x += f.x * k; around.y += f.y * k;
				const float gap = d - a;
				if (u > 40.0f && gap < 1.2f * a)
					WantBurst(burst, NiPoint3{ o.x / d, o.y / d, 0.0f }, NiPoint3{ -U.x / u, -U.y / u, 0.0f }, u, k * (1.0f - (gap > 0.0f ? gap : 0.0f) / (1.2f * a)));
				if (u > 40.0f) KeepMomentum(burst, NiPoint3{ f.x * k, f.y * k, 0.0f });
			}
		}
		{
			const float want2 = burst.x * burst.x + burst.y * burst.y + burst.z * burst.z;
			const float have2 = s.kick.x * s.kick.x + s.kick.y * s.kick.y + s.kick.z * s.kick.z;
			if (want2 > have2) {
				const float k = 1.0f - expf(-dt / 0.05f);
				s.kick.x += (burst.x - s.kick.x) * k; s.kick.y += (burst.y - s.kick.y) * k; s.kick.z += (burst.z - s.kick.z) * k;
			}
			const float fade = expf(-dt / 0.35f);
			s.kick.x *= fade; s.kick.y *= fade; s.kick.z *= fade;
			around.x += s.kick.x; around.y += s.kick.y; around.z += s.kick.z;
		}
		const NiPoint3 swirl = VortexCount ? VortexFlow(s.pos) : NiPoint3{};
		{
			const NiPoint3 m = { around.x + swirl.x, around.y + swirl.y, around.z + swirl.z };
			stirIn[i] += sqrtf(m.x * m.x + m.y * m.y + m.z * m.z) / 80.0f;
			stirDir[i] = NiPoint3{ stirDir[i].x + m.x, stirDir[i].y + m.y, stirDir[i].z + m.z };
		}
		s.pos.x += (s.vel.x + swirl.x + around.x) * dt; s.pos.y += (s.vel.y + swirl.y + around.y) * dt; s.pos.z += (s.vel.z + swirl.z + around.z) * dt;
	}
	if (Settings.volumeSmooth > 0.0f && c.count > 2) {
		static NiPoint3 mid[ChainMax];
		for (int i = 1; i + 1 < c.count; i++) {
			const NiPoint3& p0 = c.p[i - 1].pos;
			const NiPoint3& p2 = c.p[i + 1].pos;
			mid[i] = NiPoint3{ (p0.x + p2.x) * 0.5f, (p0.y + p2.y) * 0.5f, (p0.z + p2.z) * 0.5f };
		}
		for (int i = 1; i + 1 < c.count; i++) {
			if (!c.p[i].link || !c.p[i + 1].link) continue;
			float grown = (SmokeClock - c.p[i].born) / 0.4f;
			grown = grown < 0.0f ? 0.0f : grown > 1.0f ? 1.0f : grown;
			const float k = 1.0f - expf(-Settings.volumeSmooth * grown * dt);
			SmokePoint& s = c.p[i];
			s.pos.x += (mid[i].x - s.pos.x) * k;
			s.pos.y += (mid[i].y - s.pos.y) * k;
			s.pos.z += (mid[i].z - s.pos.z) * k;
		}
	}
	static NiPoint3 before[ChainMax];
	for (int i = 0; i < c.count; i++) before[i] = c.p[i].pos;
	KeepOutOfBody(c, src.frays ? &v : nullptr, src.drift, src.zip, dt);
	NoteJumps(c, before, "your body's rule", sqrtf(VolumePlayerVel.x * VolumePlayerVel.x + VolumePlayerVel.y * VolumePlayerVel.y), dt);
	NotePushes(c, before, stirIn, stirDir, dt);
	for (int i = 0; i < c.count; i++) before[i] = c.p[i].pos;
	KeepOutOfGun(c, src.frays ? &v : nullptr, src.drift, src.zip, dt);
	NoteJumps(c, before, "the gun's rule", sqrtf(VolumeGunVel.x * VolumeGunVel.x + VolumeGunVel.y * VolumeGunVel.y + VolumeGunVel.z * VolumeGunVel.z), dt);
	NotePushes(c, before, stirIn, stirDir, dt);
	Diffuse(c, stirIn, stirDir, dt, src.frays ? &v : nullptr, src.drift);
	while (c.count && SmokeClock - c.p[0].born > c.p[0].life) {
		memmove(&c.p[0], &c.p[1], sizeof(SmokePoint) * (c.count - 1));
		c.count--;
		if (c.count) c.p[0].link = false;
	}
	if (src.on) {
		float spacing = (v.life + v.lifeVar) / (src.points > 2.0f ? src.points : 2.0f);
		if (spacing < Settings.volumeSpacing) spacing = Settings.volumeSpacing;
		const bool jumped = c.count && c.emitting && [&]() {
			const NiPoint3& q = c.p[c.count - 1].pos;
			const float dx = src.at.x - q.x, dy = src.at.y - q.y, dz = src.at.z - q.z;
			return dx * dx + dy * dy + dz * dz > 200.0f * 200.0f;
		}();
		if (jumped) c.emitting = false;
		int emit = src.cluster;
		if ((!c.emitting || SmokeClock - c.lastEmit >= spacing) && emit < 1) emit = 1;
		if (emit < 1 && src.maxGap > 0.0f && c.emitting && c.count >= 2) {
			const NiPoint3& q = c.p[c.count - 2].pos;
			const float dx = src.at.x - q.x, dy = src.at.y - q.y, dz = src.at.z - q.z;
			if (dx * dx + dy * dy + dz * dz > src.maxGap * src.maxGap) emit = 1;
		}
		for (int e = 0; e < emit; e++) { EmitSmoke(c, src, v); c.emitting = true; }
		if (src.bore.x != 0.0f || src.bore.y != 0.0f || src.bore.z != 0.0f)
			for (int i = 0; i < c.count; i++) {
				const float age = SmokeClock - c.p[i].born;
				if (age >= 0.1f) continue;
				const float k = age / 0.1f;
				c.p[i].pos = NiPoint3{ src.at.x + src.bore.x * k, src.at.y + src.bore.y * k, src.at.z + src.bore.z * k };
			}
		if (c.count) c.p[c.count - 1].pos = src.at;
	}
	c.emitting = src.on;
}

static float EndWeight(const NiPoint3& old, const NiPoint3& nw, const NiPoint3& other, float r, bool atOld = false) {
	const NiPoint3 self = { nw.x - old.x, nw.y - old.y, nw.z - old.z };
	const NiPoint3 next = atOld ? NiPoint3{ old.x - other.x, old.y - other.y, old.z - other.z }
	                            : NiPoint3{ other.x - nw.x, other.y - nw.y, other.z - nw.z };
	const float ls = sqrtf(self.x * self.x + self.y * self.y + self.z * self.z);
	const float ln = sqrtf(next.x * next.x + next.y * next.y + next.z * next.z);
	r = r > 0.05f ? r : 0.05f;
	const float cover = ln / r < 1.0f ? ln / r : 1.0f;
	const float inside = ls / r < 1.0f ? ls / r : 1.0f;
	float fill = 0.0f;
	if (ls > 0.001f && ln > 0.001f) fill = 0.5f * (1.0f - (self.x * next.x + self.y * next.y + self.z * next.z) / (ls * ln));
	return 1.0f - cover * (1.0f - fill * inside);
}

static bool SmokeNearBody() {
	const NiPoint3& f = VolumePlayerFeet;
	const float reach = Settings.volumePlayerRadius + 40.0f;
	for (int k = 0; k < ChainCount + 2; k++) {
		const SmokeChain& c = k < ChainCount ? Chains[k] : Twins[k - ChainCount];
		for (int i = 0; i < c.count; i++) {
			const NiPoint3& p = c.p[i].pos;
			if (p.z < f.z - 20.0f || p.z > f.z + Settings.volumePlayerHeight + 20.0f) continue;
			const float dx = p.x - f.x, dy = p.y - f.y;
			if (dx * dx + dy * dy < reach * reach) return true;
		}
	}
	return false;
}

static bool SmokeNearGun() {
	const NiPoint3& m = VolumeGunMuzzle;
	const NiPoint3& b = VolumeGunBack;
	const float L = Settings.volumeGunLength;
	for (int k = 0; k < ChainCount + 2; k++) {
		const SmokeChain& c = k < ChainCount ? Chains[k] : Twins[k - ChainCount];
		for (int i = 0; i < c.count; i++) {
			const NiPoint3& p = c.p[i].pos;
			float al = (p.x - m.x) * b.x + (p.y - m.y) * b.y + (p.z - m.z) * b.z;
			al = al < 0.0f ? 0.0f : al > L ? L : al;
			const float dx = p.x - (m.x + b.x * al), dy = p.y - (m.y + b.y * al), dz = p.z - (m.z + b.z * al);
			if (dx * dx + dy * dy + dz * dz < 225.0f) return true;
		}
	}
	return false;
}

static const int HazeMax = 40;
static SmokeChain Haze = {};
static void UpdateHaze(float dt) {
	VolLook look = {};
	look.size = Settings.volumeHazeSize / 1.6f;
	look.sizeVar = 0.5f;
	look.life = Settings.volumeHazeLife * (VolumeIndoors ? 1.0f : 0.4f);
	look.life = look.life > 3.0f ? look.life : 3.0f;
	look.lifeVar = 0.25f * look.life;
	look.speed = 3.0f; look.speedVar = 2.0f; look.spread = 3.1416f;
	look.startSize = 0.25f;
	look.grow = 0.4f * look.life;
	look.opacity = 0.012f * Settings.volumeHaze;
	look.fadeIn = 3.0f / look.life;
	look.fadeStart = 0.5f; look.fadeEnd = 1.0f;
	look.rise = 1.5f;
	look.curl = 0.0003f;
	SmokeSource src = {};
	src.strength = 1.0f; src.size = 1.0f; src.points = 1.0f; src.drag = 1.0f; src.haze = true;
	const float density = Settings.volumeDensity > 0.0f ? Settings.volumeDensity : 1.0f;
	while (HazeShots > 0) {
		HazeShots--;
		if (Settings.volumeHaze <= 0.0f || !VolumeGunOn) continue;
		const NiPoint3& b = VolumeGunBack;
		const NiPoint3 at = { VolumeGunMuzzle.x - b.x * 20.0f, VolumeGunMuzzle.y - b.y * 20.0f, VolumeGunMuzzle.z - b.z * 20.0f + 5.0f };
		const float add = look.opacity * density, cap = 4.0f * add;
		int best = -1, nearest = -1;
		float bestDist = 1e9f, nearestDist = 1e9f;
		for (int i = 0; i < Haze.count; i++) {
			const SmokePoint& s = Haze.p[i];
			const float age = SmokeClock - s.born;
			const float dx = s.pos.x - at.x, dy = s.pos.y - at.y, dz = s.pos.z - at.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
			if (d < nearestDist) { nearest = i; nearestDist = d; }
			if (age > 0.6f * s.life || s.opacity >= cap * 0.999f) continue;
			if (d < bestDist && d < 0.7f * PointRadius(s, age)) { best = i; bestDist = d; }
		}
		if (best >= 0) {
			SmokePoint& s = Haze.p[best];
			s.opacity = s.opacity + add < cap ? s.opacity + add : cap;
		}
		else if (Haze.count >= HazeMax && nearest >= 0) {
			SmokePoint& s = Haze.p[nearest];
			s.opacity = s.opacity + add < 2.0f * cap ? s.opacity + add : 2.0f * cap;
		}
		else if (Haze.count < HazeMax) {
			src.at = at;
			src.push = NiPoint3{ -b.x * 6.0f, -b.y * 6.0f, -b.z * 6.0f };
			Haze.emitting = false;
			EmitSmoke(Haze, src, look);
			Haze.p[Haze.count - 1].link = false;
		}
	}
	if (!Haze.count) return;
	UpdateChain(Haze, src, dt, look);
	for (int i = 0; i < Haze.count; i++) {
		SmokePoint& s = Haze.p[i];
		s.link = false;
		if (s.spread > 1.6f) s.spread = 1.6f;
		if (VolumeIndoors && VolumePlayerOn && s.vel.z > 0.0f) {
			float k = (s.pos.z - VolumePlayerFeet.z - 160.0f) / 90.0f;
			k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
			s.vel.z *= 1.0f - k;
		}
	}
	int keep = 0;
	for (int i = 0; i < Haze.count; i++)
		if (SmokeClock - Haze.p[i].born <= Haze.p[i].life) Haze.p[keep++] = Haze.p[i];
	Haze.count = keep;
}

static void UpdateVolumeSmoke(float dt, const SmokeSource sources[ChainCount]) {
	SmokeClock += dt;
	UpdateAirAndPlayer(dt);
	ShedVortices(dt);
	for (int k = 0; k < ChainCount; k++) UpdateChain(Chains[k], sources[k], dt, Settings.volLook[k]);
	for (int z = 0; z < 2; z++) {
		if (!Twins[z].count) continue;
		SmokeSource ts = {};
		ts.strength = 1.0f; ts.size = 1.0f; ts.points = 30.0f; ts.drag = Settings.volumeDrag; ts.drift = sources[z].drift; ts.frays = true;
		UpdateChain(Twins[z], ts, dt, Settings.volLook[z]);
	}
	for (int t = 0; t < TendrilMax; t++) {
		if (!Tendrils[t].count) continue;
		SmokeSource ts = {};
		ts.strength = 1.0f; ts.size = 1.0f; ts.points = 30.0f; ts.drag = 1.5f; ts.drift = TendrilDrift[t];
		UpdateChain(Tendrils[t], ts, dt, TendrilLook[t]);
	}
	UpdateHaze(dt);
	float gunMoving;
	GunSpeed(&gunMoving);
	if (Settings.volumeTendrils > 0.0f && gunMoving > 0.5f) {
		const float cosLimit = cosf(Settings.volumeTendrilAngle * 0.01745329f);
		for (int k = 0; k <= ChainTrail; k++) {
			SmokeChain& c = Chains[k];
			for (int i = 1; i + 1 < c.count; i++) {
				SmokePoint& s = c.p[i];
				if (s.frayed || !s.link || !c.p[i + 1].link) continue;
				const float age = SmokeClock - s.born;
				if (age < 0.15f || age > 0.6f * s.life) continue;
				const NiPoint3& p0 = c.p[i - 1].pos;
				const NiPoint3& p2 = c.p[i + 1].pos;
				const NiPoint3 a = { s.pos.x - p0.x, s.pos.y - p0.y, s.pos.z - p0.z };
				const NiPoint3 b = { p2.x - s.pos.x, p2.y - s.pos.y, p2.z - s.pos.z };
				const float la = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z), lb = sqrtf(b.x * b.x + b.y * b.y + b.z * b.z);
				const float thick = PointRadius(s, age) > 0.5f ? PointRadius(s, age) : 0.5f;
				if (la < thick || lb < thick) continue;
				if ((a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb) > cosLimit) continue;
				s.frayed = true;
				if (Random01() > Settings.volumeTendrils) continue;
				SpawnTendril(s, PointRadius(s, age), NiPoint3{ a.x / la - b.x / lb, a.y / la - b.y / lb, a.z / la - b.z / lb },
					Settings.volLook[k], sources[k].drift);
			}
		}
	}
	static float out[((ChainCount + 2) * ChainMax + TendrilMax * TendrilPoints + HazeMax) * RecordFloats];
	static_assert(sizeof(out) == sizeof(VolumePublished), "the published records and their copy must match");
	static float hazeOut[HazeMax * HazeFloats];
	static_assert(sizeof(hazeOut) == sizeof(SmokeHazePublished), "the published haze and its copy must match");
	int n = 0, hazeN = 0;
	for (int k = 0; k < ChainCount + 2 + TendrilMax + 1; k++) {
		const SmokeChain& c = k < ChainCount ? Chains[k] : k < ChainCount + 2 ? Twins[k - ChainCount] : k < ChainCount + 2 + TendrilMax ? Tendrils[k - ChainCount - 2] : Haze;
		const bool burst = k == ChainPuff || k == ChainEject;
		const float pillows = burst ? Settings.volumePillows : 0.0f, streaks = burst ? 0.0f : Settings.volumeStreaks;
		const float style = 16.0f * floorf((pillows < 0.0f ? 0.0f : pillows > 1.0f ? 1.0f : pillows) * 15.0f + 0.5f) +
			floorf((streaks < 0.0f ? 0.0f : streaks > 1.0f ? 1.0f : streaks) * 15.0f + 0.5f);
		float radius[ChainMax], density[ChainMax], age01[ChainMax];
		for (int i = 0; i < c.count; i++) {
			const SmokePoint& s = c.p[i];
			const float age = SmokeClock - s.born;
			const float a = age / s.life;
			float g = s.grow > 0.01f ? age / s.grow : 1.0f;
			g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
			g = 1.0f - (1.0f - g) * (1.0f - g);
			float fade = 1.0f;
			if (a < s.fadeIn && s.fadeIn > 0.001f) fade = a / s.fadeIn;
			else if (a > s.fadeStart) fade = 1.0f - (a - s.fadeStart) / (s.fadeEnd - s.fadeStart > 0.001f ? s.fadeEnd - s.fadeStart : 0.001f);
			fade = fade < 0.0f ? 0.0f : fade > 1.0f ? 1.0f : fade;
			fade = fade * fade * (3.0f - 2.0f * fade);
			const float grown = s.r0 + (s.r1 - s.r0) * g;
			const float r = grown * s.spread;
			radius[i] = r;
			const float spread = s.r0 > 0.01f && grown > s.r0 ? powf(s.r0 / grown, Settings.volumeThinning) : 1.0f;
			density[i] = s.opacity * s.strength * fade * spread / (1.7725f * (r > 0.05f ? r : 0.05f));
			const float look = a + 0.5f * (s.spread - 1.0f) + 0.4f * s.stir;
			age01[i] = look < 1.0f ? look : 1.0f;
		}
		if (&c == &Haze) {
			for (int i = 0; i < c.count && hazeN < HazeMax; i++) {
				float* o = hazeOut + hazeN * HazeFloats;
				o[0] = c.p[i].pos.x; o[1] = c.p[i].pos.y; o[2] = c.p[i].pos.z; o[3] = radius[i];
				o[4] = density[i] * 1.7725f * (radius[i] > 0.05f ? radius[i] : 0.05f);
				o[5] = o[6] = o[7] = 0.0f;
				if (o[4] > 0.0f) hazeN++;
			}
			continue;
		}
		for (int i = 0; i < c.count; i++) {
			const bool linked = c.p[i].link && i > 0;
			const bool followed = i + 1 < c.count && c.p[i + 1].link;
			if (!linked && followed) continue;
			const int j = linked ? i - 1 : i;
			const bool capB = linked && c.p[j].link && j > 0;
			float* o = out + n * RecordFloats;
			o[0] = c.p[i].pos.x; o[1] = c.p[i].pos.y; o[2] = c.p[i].pos.z; o[3] = radius[i];
			o[4] = c.p[j].pos.x; o[5] = c.p[j].pos.y; o[6] = c.p[j].pos.z; o[7] = radius[j];
			o[8] = density[i]; o[9] = density[j]; o[10] = 0.5f * (age01[i] + age01[j]);
			const float wA = followed ? EndWeight(c.p[j].pos, c.p[i].pos, c.p[i + 1].pos, radius[i]) : 1.0f;
			const float wB = capB ? EndWeight(c.p[j].pos, c.p[i].pos, c.p[j - 1].pos, radius[j], true) : 1.0f;
			o[11] = floorf(wA * 15.0f + 0.5f) + 16.0f * floorf(wB * 15.0f + 0.5f) + 256.0f * style;
			o[12] = c.p[i].tex; o[13] = c.p[j].tex; o[14] = age01[i]; o[15] = age01[j];
			n++;
		}
	}
	EnterCriticalSection(&HeatLock);
	memcpy(VolumePublished, out, n * RecordFloats * sizeof(float));
	VolumePublishedCount = n;
	memcpy(SmokeHazePublished, hazeOut, hazeN * HazeFloats * sizeof(float));
	SmokeHazePublishedCount = hazeN;
	VolumeParams[0] = Settings.volumeNoise;
	VolumeParams[1] = Settings.volumeBrightness;
	VolumeParams[2] = Settings.volumeShade;
	VolumeParams[3] = SmokeClock;
	VolumeParams[4] = Settings.volumeDebug ? 1.0f : 0.0f;
	VolumeParams[5] = Settings.volumeGunInFront ? 1.0f : 0.0f;
	VolumeParams[6] = Settings.volumeTint;
	VolumeParams[7] = Settings.volumeDepth;
	LeaveCriticalSection(&HeatLock);
	static float loggedAt = -10.0f;
	static int logged = 0;
	if (n && Player() && logged < 20 && SmokeClock - loggedAt >= 2.0f) {
		loggedAt = SmokeClock;
		logged++;
		const float* player = (const float*)((UInt8*)Player() + 0x30);
		char counts[200] = "";
		for (int k = 0; k < ChainCount; k++) {
			char one[48];
			sprintf_s(one, "%s%s %d%s", k ? ", " : "", ChainNames[k], Chains[k].count, sources[k].on ? " (smoking)" : "");
			strcat_s(counts, one);
		}
		int tendrils = 0;
		for (int t = 0; t < TendrilMax; t++) tendrils += Tendrils[t].count ? 1 : 0;
		char one[64];
		sprintf_s(one, ", twin arms %d + %d, tendrils %d", Twins[0].count, Twins[1].count, tendrils);
		strcat_s(counts, one);
		LogLine("volumetric smoke: %d segment(s); points: %s; first segment end (%.1f, %.1f, %.1f) radius %.2f, density %.4f; "
			"player at (%.1f, %.1f, %.1f)", n, counts, out[0], out[1], out[2], out[3], out[8], player[0], player[1], player[2]);
	}
}

static void NoteVolumeTime(const LARGE_INTEGER& start) {
	static LARGE_INTEGER frequency = {};
	static double total = 0.0, worst = 0.0;
	static int frames = 0, logged = 0, mostPoints = 0, mostSegments = 0;
	static ULONGLONG since = 0;
	if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	int points = 0;
	for (int k = 0; k < ChainCount; k++) points += Chains[k].count;
	for (int z = 0; z < 2; z++) points += Twins[z].count;
	for (int t = 0; t < TendrilMax; t++) points += Tendrils[t].count;
	if (!points) { frames = 0; total = worst = 0.0; since = 0; return; }
	const double ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)frequency.QuadPart;
	const ULONGLONG now = GetTickCount64();
	if (!since) since = now;
	total += ms; frames++;
	worst = ms > worst ? ms : worst;
	mostPoints = points > mostPoints ? points : mostPoints;
	mostSegments = VolumePublishedCount > mostSegments ? VolumePublishedCount : mostSegments;
	if (now - since < 5000) return;
	if (logged < 40) {
		logged++;
		LogLine("volumetric smoke CPU: %.3f ms a frame on average, worst %.3f ms, over %d frames (up to %d points, %d segments)",
			total / frames, worst, frames, mostPoints, mostSegments);
	}
	total = worst = 0.0; frames = 0; mostPoints = mostSegments = 0; since = now;
}

static void MainLoop() {
	const ULONGLONG now = GetTickCount64();
	ReloadIniIfChanged(now);
	UpdateGunSettings();
	HandleShots(now);
	UpdateBursts(now);
	const bool heatVolume = VolumeWanted(ChainHeat), trailVolume = VolumeWanted(ChainTrail);
	const double nowSeconds = PreciseSeconds();
	const float dt = LastLoopSeconds > 0.0 ? (float)(nowSeconds - LastLoopSeconds) : 0.0f;
	LastLoopSeconds = nowSeconds;
	if (!Settings.trail || !Switch(1)) ReleaseWisp(nullptr);

	const float span = Settings.heatFull - Settings.heatStart > 0.01f ? Settings.heatFull - Settings.heatStart : 0.01f;
	float t = (Heat - Settings.heatStart) / span;
	t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	float rate = Settings.maxRate * t * t * (3.0f - 2.0f * t);
	if (Settings.constantRate > 0.0f) rate = Settings.constantRate;
	const bool firing = Settings.wispStopDelay <= 0.0f || (LastShot && now - LastShot < (ULONGLONG)(Settings.wispStopDelay * 1000.0f));
	if (!firing && Settings.constantRate <= 0.0f) {
		rate = 0.0f;
		if (FollowEffect) ReleaseWisp("stopped firing");
	}
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
	const bool muzzleAlive = drawn && TrailNode && (liveNode || now - LastMuzzleSeen < 250);
	const bool dropped = FollowEffect && FollowHeld && *(volatile LONG*)(FollowEffect + 4) <= 1;
	if (FollowEffect && (!muzzleAlive || dropped || now > FollowUntil))
		ReleaseWisp(!muzzleExists ? "the muzzle left the scene" : !drawn ? "gun put away" : !muzzleAlive ? "the muzzle is not on the gun you see"
			: dropped ? "the game removed it" : "its time is up");
	if (TrailNode && !muzzleAlive) {
		NodeRelease(TrailNode);
		TrailNode = nullptr;
		TrailActor = nullptr;
	}
	if (heatVolume && FollowEffect) ReleaseWisp("volumetric smoke is on");
	if (Settings.trail && Switch(1) && rate > 0.01f && muzzleAlive && !heatVolume) {
		if (FollowEffect && now + (ULONGLONG)(Settings.tailSeconds * 1000.0f) > FollowUntil) ReleaseWisp("handing over to a new one");
		if (!FollowEffect) SpawnWisp(now, NodePosition(SmokeAnchor(Settings.wispNode, TrailNode), Settings.wispOffset));
	}
	if (FollowEffect) {
		UInt8* root = *(UInt8**)(FollowEffect + 0x18);
		if (root && Settings.trailFollow) {
			NiPoint3 at = NodePosition(SmokeAnchor(Settings.wispNode, TrailNode), Settings.wispOffset);
			if (Settings.sway > 0.0f) {
				const float t = (float)(now % 3600000) / 1000.0f * Settings.swaySpeed * 6.2831853f;
				at.x += Settings.sway * (sinf(t) + 0.5f * sinf(t * 2.3f + 1.0f)) / 1.5f;
				at.y += Settings.sway * (sinf(t * 0.8f + 2.0f) + 0.5f * sinf(t * 1.9f)) / 1.5f;
			}
			PlaceCopies(WispCopies, root, RateValue, RateController, at, rate);
		}
		else if (RateValue) { *RateValue = rate; *(unsigned short*)(RateController + 0x08) |= 0x8; }
		if (root) ReportRibbon(root, now);
		else if (rate <= 0.01f) ReleaseWisp("cooled down (no rate control)");
	}
	{
		const bool resting = LastShot && now - LastShot >= (ULONGLONG)(Settings.afterDelay * 1000.0f);
		const float afterSpan = Settings.afterFullHeat - Settings.afterMinHeat > 0.01f ? Settings.afterFullHeat - Settings.afterMinHeat : 0.01f;
		float a = (Heat - Settings.afterMinHeat) / afterSpan;
		a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
		const float afterRate = resting ? Settings.afterMaxRate * a * a * (3.0f - 2.0f * a) : 0.0f;
		const bool afterWanted = Settings.after && Switch(7) && muzzleAlive && GunAllowed(CurrentWeapon);
		const bool afterOn = afterWanted && !trailVolume;
		VolAfterRate = afterWanted ? afterRate : 0.0f;
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
				PlaceCopies(AfterCopies, afterRoot, AfterRate, AfterController, at, afterRate);
			}
			else if (AfterRate) { *AfterRate = afterRate; *(unsigned short*)(AfterController + 0x08) |= 0x8; }
		}
	}
	{
		LARGE_INTEGER volumeStart;
		QueryPerformanceCounter(&volumeStart);
		SmokeSource sources[ChainCount] = {};
		VolumeGunOn = false;
		ShapeOn = false;
		VolumeMuzzleDepth = 0.0f;
		if (muzzleAlive && TrailNode) {
			UInt8* anchor = SmokeAnchor(Settings.wispNode, TrailNode);
			const NiPoint3 muzzle = WorldPoint(anchor, NodePosition(anchor, Settings.wispOffset));
			static NiPoint3 lastMuzzle = {};
			static bool haveMuzzle = false;
			if (haveMuzzle && dt > 0.0001f) {
				NiPoint3 v = { (muzzle.x - lastMuzzle.x) / dt, (muzzle.y - lastMuzzle.y) / dt, (muzzle.z - lastMuzzle.z) / dt };
				if (v.x * v.x + v.y * v.y + v.z * v.z > 1500.0f * 1500.0f) v = NiPoint3{};
				const float kv = 1.0f - expf(-dt / 0.3f);
				VolumeGunVel.x += (v.x - VolumeGunVel.x) * kv; VolumeGunVel.y += (v.y - VolumeGunVel.y) * kv; VolumeGunVel.z += (v.z - VolumeGunVel.z) * kv;
			}
			lastMuzzle = muzzle;
			haveMuzzle = true;
			VolumeGunMuzzle = muzzle;
			float off = 0.0f;
			VolumeGunBack = BarrelBack(TrailNode, &off);
			static void* loggedGun = nullptr;
			if (CurrentWeapon != loggedGun) {
				loggedGun = CurrentWeapon;
				LogLine("volumetric smoke: the barrel's direction from %s (the muzzle node's own axis and the line to its parent are %.0f degrees apart)",
					off <= 45.0f ? "the muzzle node's own axis" : "the line to the muzzle node's parent", off);
			}
			static void* probedGun = nullptr;
			if (CurrentWeapon != probedGun) {
				probedGun = CurrentWeapon;
				UInt8* player = (UInt8*)Player();
				LogLine("gun shape probe: the %s model's gun (the game's own third-person switch: %d)",
					InFirstPersonModel(TrailNode) ? "first-person" : "third-person", player ? (int)*(player + 0x64C) : -1);
				if (UInt8* gunRoot = *(UInt8**)(TrailNode + 0x18)) ProbeGunShape(gunRoot, TrailNode);
			}
			UInt8* gunRoot = *(UInt8**)(TrailNode + 0x18);
			UpdateGunShape(gunRoot, TrailNode);
			ShapeOn = Shape && gunRoot && gunRoot == ShapeRoot;
			if (gunRoot && gunRoot == ShapeRoot) {
				memcpy(ShapeRot, gunRoot + 0x68, sizeof(ShapeRot));
				ShapePos = WorldPoint(TrailNode, *(NiPoint3*)(gunRoot + 0x8C));
				ShapeScale = *(float*)(gunRoot + 0x98) > 0.0001f ? *(float*)(gunRoot + 0x98) : 1.0f;
				if (ShapeAlongX) VolumeGunBack = NiPoint3{ -ShapeRot[0], -ShapeRot[3], -ShapeRot[6] };
			}
			static bool haveAim = false;
			const float fx = -VolumeGunBack.x, fy = -VolumeGunBack.y, fl = sqrtf(fx * fx + fy * fy);
			if (fl > 0.05f) {
				VolumeAimForward = NiPoint3{ fx / fl, fy / fl, 0.0f };
				VolumeAimRight = NiPoint3{ fy / fl, -fx / fl, 0.0f };
				haveAim = true;
			}
			VolumeGunOn = haveAim && (Settings.volumeWrapGun || Settings.volumeDriftRight != 0.0f || Settings.volumeDriftAway != 0.0f);
		}
		for (int k = 0; k < ChainCount; k++) {
			sources[k].drift = k <= ChainTrail && (Settings.volumeDriftRight != 0.0f || Settings.volumeDriftAway != 0.0f);
			sources[k].frays = true;
			sources[k].zip = k == ChainHeat ? 1 : k == ChainTrail ? 2 : 0;
			sources[k].strength = 1.0f;
			sources[k].size = k <= ChainTrail ? Settings.volumeSize : Settings.volumeBurstSize;
			sources[k].points = k <= ChainTrail ? 30.0f : 62.0f;
			sources[k].drag = k <= ChainTrail ? Settings.volumeDrag : Settings.volumeBurstDrag;
		}
		if ((heatVolume || trailVolume) && muzzleAlive && TrailNode) {
			UInt8* anchor = SmokeAnchor(Settings.wispNode, TrailNode);
			const NiPoint3 base = WorldPoint(anchor, NodePosition(anchor, Settings.wispOffset));
			NiPoint3 wispAt = base, trailAt = base;
			NiPoint3 boreOut = {};
			float atMuzzle = 1.0f;
			if (VolumeGunOn) {
				const NiPoint3& b = VolumeGunBack;
				float depth = 1.5f;
				if (ShapeOn) {
					NiPoint3 up = { -b.x * b.z, -b.y * b.z, 1.0f - b.z * b.z };
					const float ul = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
					up = ul > 0.01f ? NiPoint3{ up.x / ul, up.y / ul, up.z / ul } : NiPoint3{ 1.0f, 0.0f, 0.0f };
					const NiPoint3 side = { b.y * up.z - b.z * up.y, b.z * up.x - b.x * up.z, b.x * up.y - b.y * up.x };
					for (float s = 0.0f; s <= 6.0f; s += 0.25f) {
						bool wall = false;
						for (int k = 0; k < 4 && !wall; k++) {
							const float u = k == 0 ? 0.8f : k == 1 ? -0.8f : 0.0f, w = k == 2 ? 0.8f : k == 3 ? -0.8f : 0.0f;
							NiPoint3 nn;
							wall = GunSurface(NiPoint3{ base.x + b.x * s + up.x * u + side.x * w, base.y + b.y * s + up.y * u + side.y * w,
								base.z + b.z * s + up.z * u + side.z * w }, &nn) < 0.0f;
						}
						if (wall) { depth = s + 1.5f; break; }
					}
				}
				VolumeMuzzleDepth = depth;
				boreOut = NiPoint3{ -b.x * depth, -b.y * depth, -b.z * depth };
				wispAt = trailAt = NiPoint3{ base.x + b.x * depth, base.y + b.y * depth, base.z + b.z * depth };
			}
			if (VolumeGunOn && Settings.volumeBarrelSpread > 0.0f) {
				const float tb = (float)fmod(nowSeconds, 3600.0);
				const float wander = 0.5f + 0.5f * (0.6f * sinf(tb * 1.3f) + 0.4f * sinf(tb * 2.9f + 1.0f));
				const float back = Settings.volumeBarrelSpread * Settings.volumeGunLength * wander;
				wispAt.x += VolumeGunBack.x * back; wispAt.y += VolumeGunBack.y * back; wispAt.z += VolumeGunBack.z * back;
				if (Settings.volumeWrapGun) {
					const NiPoint3& b = VolumeGunBack;
					NiPoint3 up = { -b.x * b.z, -b.y * b.z, 1.0f - b.z * b.z };
					const float ul = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
					if (ul > 0.01f) {
						const VolLook& hv = Settings.volLook[ChainHeat];
						const float r0 = hv.size * Settings.volumeSize * hv.startSize;
						float g = back / 3.0f;
						g = g > 1.0f ? 1.0f : g;
						float lift = Settings.volumeGunRadius + 0.6f * r0;
						if (ShapeOn) {
							const float target = 0.6f * r0 + 0.1f;
							auto clear = [&](float h) {
								NiPoint3 nn;
								const float k = h / ul;
								return GunSurface(NiPoint3{ wispAt.x + up.x * k, wispAt.y + up.y * k, wispAt.z + up.z * k }, &nn) >= target;
							};
							if (clear(lift)) { while (lift > 0.2f && clear(lift - 0.2f)) lift -= 0.2f; }
							else for (int k = 0; k < 30 && !clear(lift); k++) lift += 0.2f;
						}
						atMuzzle = 1.0f - g * g * (3.0f - 2.0f * g);
						lift *= (1.0f - atMuzzle) / ul;
						wispAt.x += up.x * lift; wispAt.y += up.y * lift; wispAt.z += up.z * lift;
					}
				}
			}
			const float swayHeat = boreOut.x != 0.0f || boreOut.y != 0.0f || boreOut.z != 0.0f ? 1.0f - atMuzzle : 1.0f;
			const float swayTrail = boreOut.x != 0.0f || boreOut.y != 0.0f || boreOut.z != 0.0f ? 0.0f : 1.0f;
			const float t1 = (float)fmod(nowSeconds, 3600.0) * Settings.swaySpeed * 6.2831853f;
			wispAt.x += swayHeat * Settings.sway * (sinf(t1) + 0.5f * sinf(t1 * 2.3f + 1.0f)) / 1.5f;
			wispAt.y += swayHeat * Settings.sway * (sinf(t1 * 0.8f + 2.0f) + 0.5f * sinf(t1 * 1.9f)) / 1.5f;
			const float t2 = (float)fmod(nowSeconds, 3600.0) * Settings.afterSwaySpeed * 6.2831853f;
			trailAt.x += swayTrail * Settings.afterSway * (sinf(t2 * 1.1f + 0.7f) + 0.5f * sinf(t2 * 2.7f)) / 1.5f;
			trailAt.y += swayTrail * Settings.afterSway * (sinf(t2 * 0.9f + 1.3f) + 0.5f * sinf(t2 * 2.1f + 0.4f)) / 1.5f;
			sources[ChainHeat].on = heatVolume && Settings.trail && Switch(1) && rate > 0.01f;
			sources[ChainHeat].at = wispAt;
			sources[ChainHeat].bore = NiPoint3{ boreOut.x * atMuzzle, boreOut.y * atMuzzle, boreOut.z * atMuzzle };
			sources[ChainHeat].strength = Settings.maxRate > 0.01f ? rate / Settings.maxRate : 0.0f;
			sources[ChainTrail].on = trailVolume && VolAfterRate > 0.01f;
			sources[ChainTrail].at = trailAt;
			sources[ChainTrail].bore = boreOut;
			sources[ChainHeat].maxGap = sources[ChainTrail].maxGap = 8.0f;
			sources[ChainTrail].strength = Settings.afterMaxRate > 0.01f ? VolAfterRate / Settings.afterMaxRate : 0.0f;
		}
		for (int k = ChainPuff; k <= ChainEject; k++) {
			SmokeBurst& b = VolumeBursts[k];
			if (!b.node) continue;
			if (VolumeWanted(k) && (b.fresh || SmokeClock < b.until)) {
				sources[k].on = true;
				sources[k].at = WorldPoint(b.node, NodePosition(b.node, b.offset));
				sources[k].size = Settings.volumeBurstSize * b.size;
				sources[k].cluster = b.pending < ChainMax / 2 ? b.pending : ChainMax / 2;
				b.pending = 0;
				if (VolumeGunOn) sources[k].carry = VolumeGunVel;
				if (k == ChainPuff) {
					const bool alongModel = ShapeAlongX && TrailNode && b.node == TrailNode && *(UInt8**)(TrailNode + 0x18) == ShapeRoot;
					const NiPoint3 back = alongModel ? VolumeGunBack : BarrelBack(b.node);
					const float f = Settings.volumePuffForward;
					sources[k].push = NiPoint3{ -back.x * f, -back.y * f, -back.z * f };
				}
				b.fresh = false;
			}
			else { NodeRelease(b.node); b.node = nullptr; b.pending = 0; }
		}
		UpdateVolumeSmoke(dt < 0.1f ? dt : 0.1f, sources);
		NoteVolumeTime(volumeStart);
	}
	if (now - LastShot >= (ULONGLONG)(Settings.coolDelay * 1000.0f)) Heat -= dt * Settings.coolPerSecond;
	if (Heat < 0.0f) Heat = 0.0f;
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

static void TimedMainLoop() {
	const double start = PreciseSeconds();
	MainLoop();
	const double end = PreciseSeconds();
	const double ms = (end - start) * 1000.0;
	static double total = 0.0, worst = 0.0, windowStart = 0.0;
	static int frames = 0, slowLogged = 0, reports = 0;
	if (ms > 4.0 && slowLogged < 40) {
		slowLogged++;
		LogLine("slow game-loop step: GunFX took %.1f ms this frame", ms);
	}
	total += ms;
	frames++;
	if (ms > worst) worst = ms;
	if (windowStart <= 0.0) windowStart = end;
	if (end - windowStart < 5.0) return;
	if (reports < 60) {
		reports++;
		LogLine("GunFX per frame (whole step): %.3f ms on average, worst %.3f ms, over %d frames", total / frames, worst, frames);
	}
	total = worst = 0.0;
	frames = 0;
	windowStart = end;
}
static void MessageHandler(NVSEMessage* msg) { if (msg->type == kMessage_MainGameLoop) TimedMainLoop(); }

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
static int LoggedShots = 0;

static bool InFirstPersonModel(UInt8* node) {
	UInt8* firstPerson = *(UInt8**)((UInt8*)Player() + 0x694);
	for (int depth = 0; node && depth < 32; depth++, node = *(UInt8**)(node + 0x18)) if (node == firstPerson) return true;
	return false;
}

static void OnShot(UInt8* actor, void* weapon, ULONGLONG now) {
	const UInt8 type = *((UInt8*)weapon + 0xF4);
	if (Settings.puffPlayerOnly && actor != Player()) return;
	if (!GunAllowed(weapon)) return;
	if (type < 3 || type > 9) {
		static void* loggedSkipped[16] = {};
		static int skipped = 0;
		bool seen = false;
		for (int i = 0; i < skipped; i++) seen |= loggedSkipped[i] == weapon;
		if (!seen && skipped < 16) { loggedSkipped[skipped++] = weapon; LogLine("shot with weapon type %u: not a gun, no smoke", type); }
		return;
	}
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
		{
			const NiPoint3 at = WorldPoint(smoke.node, NodePosition(smoke.node, Settings.puffOffset));
			EnterCriticalSection(&HeatLock);
			FlashWorld = at;
			FlashAt = PreciseSeconds();
			LeaveCriticalSection(&HeatLock);
			NoteBullet(at);
		}
		Puff(&smoke, now);
	} else Puff(&shot, now);
}

static int LoggedEjects = 0, LoggedEjectsView[2] = {};
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
	float hot = Heat / (Settings.heatFull > 0.01f ? Settings.heatFull : 0.01f);
	hot = hot < 0.0f ? 0.0f : hot > 1.0f ? 1.0f : hot;
	const float scale = Settings.ejectScale * (1.0f + (Settings.ejectHotScale - 1.0f) * hot);
	const bool volume = VolumeWanted(ChainEject);
	if (volume) StartVolumeBurst(ChainEject, port, Settings.ejectOffset, Settings.ejectBurst, 1.0f + (Settings.ejectHotScale - 1.0f) * hot);
	void* effect = !volume && cell ? SpawnBurst(cell, Settings.ejectLifetime, Settings.ejectModel, at, scale, Settings.ejectBurst, Settings.ejectBurstRate, now,
		port, Settings.ejectOffset) : nullptr;
	const int view = InFirstPersonModel(port) ? 0 : 1;
	if (LoggedEjectsView[view] < 8 || (!effect && !volume)) {
		LoggedEjectsView[view]++;
		UInt8* muzzle = root ? GetFireNode(weapon, root) : nullptr;
		const NiPoint3 m = muzzle ? *(NiPoint3*)(muzzle + 0x8C) : NiPoint3{};
		LogLine("ejection smoke at (%.1f, %.1f, %.1f) on the %s model, %.1f units from the muzzle; %s, scale %.2f, %.1f s -> %s",
			at.x, at.y, at.z, InFirstPersonModel(port) ? "first-person" : "third-person",
			muzzle ? sqrtf((at.x - m.x) * (at.x - m.x) + (at.y - m.y) * (at.y - m.y) + (at.z - m.z) * (at.z - m.z)) : -1.0f,
			Settings.ejectModel, scale, Settings.ejectLifetime, volume ? "volumetric" : effect ? "created" : (cell ? "NOT created" : "no cell"));
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
	for (char* c = out; *c; c++) if (*c == '/') *c = 0x5C;
	if (!_strnicmp(out, "BarrelSmoke\\", 12)) {
		char renamed[MAX_PATH];
		sprintf_s(renamed, "GunFX\\%s", out + 12);
		strcpy_s(out, MAX_PATH, renamed);
	}
}

static void ReadLook(const char* section, const Look& file, Look& look) {
	char fallback[32];
	auto get = [&](const char* key, float value) { sprintf_s(fallback, "%g", value); return IniFloat(section, key, fallback); };
	look = file;
	look.strength = -get("fRise", -file.strength);
	look.turbulence = get("fCurl", file.turbulence);
	look.turbulenceScale = get("fCurlScale", file.turbulenceScale);
	look.radius = get("fSize", file.radius);
	look.radiusVar = get("fSizeVariation", file.radiusVar);
	look.life = get("fSmokeLife", file.life);
	look.lifeVar = get("fSmokeLifeVariation", file.lifeVar);
	look.grow = get("fGrowSeconds", file.grow);
	look.fade = get("fShrinkSeconds", file.fade);
	look.opacity = get("fOpacity", file.opacity);
	look.percents[3] = get("fFadeIn", file.percents[3]);
	if (look.percents[2] > look.percents[3]) look.percents[2] = look.percents[3];
	look.percents[4] = get("fFadeStart", file.percents[4]);
	look.percents[5] = get("fFadeEnd", file.percents[5]);
}

struct SmokeLook { float size, sizeVar, life, lifeVar, grow, shrink, rise, curl, curlScale, opacity, fadeIn, fadeStart, fadeEnd,
	startSize, speed, speedVar, spread; };
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
static bool NifBlocks(const std::vector<UInt8>& d, std::vector<std::string>& types, std::vector<size_t>& at, std::vector<UInt32>& sizes) {
	size_t p = 0;
	while (p < d.size() && d[p] != '\n') p++;
	p++;
	auto need = [&](size_t n) { return p + n <= d.size(); };
	if (!need(4 + 1 + 4 + 4 + 4)) return false;
	p += 4 + 1;
	p += 4;
	const UInt32 blocks = *(const UInt32*)&d[p]; p += 4;
	p += 4;
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
static int NifLook(std::vector<UInt8>& d, SmokeLook& look, bool get) {
	std::vector<std::string> types; std::vector<size_t> at; std::vector<UInt32> sizes;
	if (!NifBlocks(d, types, at, sizes)) return 0;
	int parts = 0;
	auto field = [&](size_t offset, float& value) { if (get) value = NifFloat(d, offset); else NifFloat(d, offset) = value; };
	for (size_t i = 0; i < types.size(); i++) {
		const std::string& t = types[i];
		const size_t q = at[i];
		if ((t == "NiPSysCylinderEmitter" || t == "NiPSysSphereEmitter" || t == "NiPSysBoxEmitter") && sizes[i] >= 69) {
			field(q + 53, look.size); field(q + 57, look.sizeVar); field(q + 61, look.life); field(q + 65, look.lifeVar);
			field(q + 13, look.speed); field(q + 17, look.speedVar); field(q + 25, look.spread); parts++;
		}
		else if (t == "NiPSysGrowFadeModifier" && sizes[i] >= 29) { field(q + 13, look.grow); field(q + 19, look.shrink); field(q + 25, look.startSize); parts++; }
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
				if (shares[2] > look.fadeIn) NifFloat(d, q + 13 + 4 * 2) = look.fadeIn;
				NifFloat(d, q + 13 + 4 * 4) = look.fadeStart;
				NifFloat(d, q + 13 + 4 * 5) = look.fadeEnd;
				NifFloat(d, q + 65) = look.opacity;
			}
			parts++;
		}
	}
	return parts;
}
static void BakeLook(const char* section, char* model) {
	char data[MAX_PATH];
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
	look.startSize = get("fStartSize", built.startSize);
	look.speed = get("fSpeed", built.speed);           look.speedVar = get("fSpeedVariation", built.speedVar);
	look.spread = get("fSpread", built.spread);
	const float* a = (const float*)&built; const float* b = (const float*)&look;
	int changed = 0;
	for (size_t k = 0; k < sizeof(SmokeLook) / sizeof(float); k++) changed += fabsf(a[k] - b[k]) > 1e-5f;
	if (!changed) { LogLine("%s look: %s as built", section, model); return; }
	UInt32 hash = 2166136261u;
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
	Settings.wispEmitters = (int)LayerInt("Wisp", "iEmitters", 4, IniPath);
	Settings.wispEmitters = Settings.wispEmitters < 1 ? 1 : Settings.wispEmitters > 4 ? 4 : Settings.wispEmitters;
	Settings.maxHeat = IniFloat("Wisp", "fMaxHeat", "15");
	Settings.heatStart = IniFloat("Wisp", "fHeatStart", "3");
	Settings.heatFull = IniFloat("Wisp", "fHeatFull", "12");
	Settings.maxRate = IniFloat("Wisp", "fMaxRate", "150");
	Settings.fileRate = IniFloat("Wisp", "fFileRate", "20");
	{
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
	Settings.trailFlags = LayerInt("Wisp", "iFlags", 3, IniPath);
	Settings.trailSmokeOnly = LayerInt("Wisp", "bSmokeOnly", 0, IniPath);
	Settings.trailFollow = LayerInt("Wisp", "bFollowMuzzle", 1, IniPath);
	Settings.constantRate = IniFloat("Wisp", "fConstantRate", "0");
	RibbonReports = LayerInt("Wisp", "iRibbonReports", 0, IniPath);
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
	Settings.afterEmitters = (int)LayerInt("Trail", "iEmitters", 4, IniPath);
	Settings.afterEmitters = Settings.afterEmitters < 1 ? 1 : Settings.afterEmitters > 4 ? 4 : Settings.afterEmitters;
	Settings.afterMinHeat = IniFloat("Trail", "fMinHeat", "2");
	Settings.afterFullHeat = IniFloat("Trail", "fFullHeat", "10");
	Settings.afterMaxRate = IniFloat("Trail", "fMaxRate", "60");
	Settings.afterSway = IniFloat("Trail", "fSway", "1.5");
	Settings.afterSwaySpeed = IniFloat("Trail", "fSwaySpeed", "0.15");
	BakeLook("Trail", Settings.afterModel);
	Settings.volume = LayerInt("Volume", "bEnabled", 1, IniPath);
	Settings.volumeSize = IniFloat("Volume", "fSizeScale", "0.25");
	Settings.volumeDensity = IniFloat("Volume", "fDensityScale", "4.0");
	Settings.volumeRise = IniFloat("Volume", "fRiseScale", "1.0");
	Settings.volumeCurl = IniFloat("Volume", "fCurlScale", "400");
	Settings.volumeDrag = IniFloat("Volume", "fDrag", "0.4");
	Settings.volumeSpacing = IniFloat("Volume", "fSpacing", "0.04");
	Settings.volumeNoise = IniFloat("Volume", "fNoise", "0.6");
	Settings.volumeBrightness = IniFloat("Volume", "fBrightness", "1.0");
	Settings.volumeShade = IniFloat("Volume", "fSelfShadow", "0.5");
	Settings.volumeDebug = LayerInt("Volume", "bDebug", 0, IniPath);
	Settings.volumeKinds[ChainHeat] = LayerInt("Volume", "bHeatSmoke", 1, IniPath);
	Settings.volumeKinds[ChainTrail] = LayerInt("Volume", "bTrail", 1, IniPath);
	Settings.volumeKinds[ChainPuff] = LayerInt("Volume", "bPuff", 1, IniPath);
	Settings.volumeKinds[ChainEject] = LayerInt("Volume", "bEjection", 1, IniPath);
	Settings.volumeBurstDrag = IniFloat("Volume", "fBurstDrag", "3.0");
	Settings.volumeBurstSize = IniFloat("Volume", "fBurstSizeScale", "1.0");
	Settings.volumeGunInFront = LayerInt("Volume", "bGunInFront", 1, IniPath);
	Settings.volumeExpand = IniFloat("Volume", "fExpand", "1.6");
	Settings.volumeFlash = IniFloat("Volume", "fFlash", "1.0");
	Settings.volumeFlashRadius = IniFloat("Volume", "fFlashRadius", "15");
	Settings.volumeFlashSeconds = IniFloat("Volume", "fFlashSeconds", "0.05");
	Settings.volumeWind = IniFloat("Volume", "fWind", "40");
	Settings.volumeWindPickup = IniFloat("Volume", "fWindPickup", "0.8");
	Settings.volumeWindOffset = IniFloat("Volume", "fWindDirectionOffset", "0");
	Settings.volumePlayer = LayerInt("Volume", "bPlayerPush", 1, IniPath);
	Settings.volumePlayerRadius = IniFloat("Volume", "fPlayerRadius", "30");
	Settings.volumePlayerHeight = IniFloat("Volume", "fPlayerHeight", "140");
	Settings.volumePlayerPush = IniFloat("Volume", "fPlayerPush", "40");
	Settings.volumeStir = IniFloat("Volume", "fStir", "1.5");
	Settings.volumeDiffuse = IniFloat("Volume", "fDiffuse", "1.0");
	Settings.volumeBurst = IniFloat("Volume", "fBurst", "0.5");
	Settings.volumeBullet = IniFloat("Volume", "fBullet", "2");
	Settings.volumeBulletReach = IniFloat("Volume", "fBulletReach", "8");
	Settings.volumeBulletOpen = IniFloat("Volume", "fBulletOpen", "0.12");
	Settings.volumeBulletSeconds = IniFloat("Volume", "fBulletSeconds", "1.4");
	if (Settings.volumeBulletOpen < 0.01f) Settings.volumeBulletOpen = 0.01f;
	if (Settings.volumeBulletSeconds < 0.2f) Settings.volumeBulletSeconds = 0.2f;
	if (Settings.volumeBulletSeconds > 3.0f) Settings.volumeBulletSeconds = 3.0f;
	Settings.volumePillows = IniFloat("Volume", "fPillows", "1");
	Settings.volumeStreaks = IniFloat("Volume", "fStreaks", "0");
	Settings.volumeHaze = IniFloat("Volume", "fHaze", "3");
	Settings.volumeHazeLife = IniFloat("Volume", "fHazeLife", "45");
	Settings.volumeHazeSize = IniFloat("Volume", "fHazeSize", "90");
	Settings.volumeTint = IniFloat("Volume", "fTint", "1.0");
	Settings.volumeSmooth = IniFloat("Volume", "fSmooth", "3.0");
	Settings.volumeDepth = IniFloat("Volume", "fDepth", "1.0");
	Settings.volumeDriftRight = IniFloat("Volume", "fDriftRight", "8");
	Settings.volumeDriftAway = IniFloat("Volume", "fDriftAway", "3");
	Settings.volumeWrapGun = LayerInt("Volume", "bWrapGun", 1, IniPath);
	Settings.volumeGunShape = LayerInt("Volume", "bGunShape", 1, IniPath);
	Settings.volumeGunLength = IniFloat("Volume", "fGunLength", "30");
	Settings.volumeGunRadius = IniFloat("Volume", "fGunRadius", "3.5");
	Settings.volumeGunSwirl = IniFloat("Volume", "fGunSwirl", "0.6");
	Settings.volumeBodySwirl = IniFloat("Volume", "fBodySwirl", "0.5");
	Settings.volumeSwirlLife = IniFloat("Volume", "fSwirlLife", "1.2");
	if (Settings.volumeSwirlLife < 0.2f) Settings.volumeSwirlLife = 0.2f;
	Settings.volumeBarrelSpread = IniFloat("Volume", "fBarrelSpread", "0.3");
	Settings.volumeTendrils = IniFloat("Volume", "fTendrils", "0.6");
	Settings.volumeTendrilAngle = IniFloat("Volume", "fTendrilAngle", "40");
	Settings.volumeTendrilSize = IniFloat("Volume", "fTendrilSize", "0.35");
	Settings.volumeTendrilSpeed = IniFloat("Volume", "fTendrilSpeed", "12");
	Settings.volumeUnzip = LayerInt("Volume", "bUnzip", 1, IniPath);
	Settings.volumeFanSpeed = IniFloat("Volume", "fFanSpeed", "30");
	if (Settings.volumeBarrelSpread < 0.0f) Settings.volumeBarrelSpread = 0.0f;
	if (Settings.volumeBarrelSpread > 1.0f) Settings.volumeBarrelSpread = 1.0f;
	Settings.volumeThinning = IniFloat("Volume", "fThinning", "0.2");
	if (Settings.volumeThinning < 0.0f) Settings.volumeThinning = 0.0f;
	if (Settings.volumeThinning > 1.0f) Settings.volumeThinning = 1.0f;
	Settings.volumeBurstPoints = (int)LayerInt("Volume", "iBurstPoints", 4, IniPath);
	if (Settings.volumeBurstPoints < 1) Settings.volumeBurstPoints = 1;
	if (Settings.volumeBurstPoints > 12) Settings.volumeBurstPoints = 12;
	Settings.volumePuffForward = IniFloat("Volume", "fPuffForward", "15");
	{
		const char* sections[ChainCount] = { "Wisp", "Trail", "Puff", "Ejection" };
		for (int k = 0; k < ChainCount; k++) {
			VolLook& v = Settings.volLook[k];
			const char* sec = sections[k];
			v.size = IniFloat(sec, "fSize", "3");            v.sizeVar = IniFloat(sec, "fSizeVariation", "0.5");
			v.life = IniFloat(sec, "fSmokeLife", "2.6");     v.lifeVar = IniFloat(sec, "fSmokeLifeVariation", "0.3");
			v.speed = IniFloat(sec, "fSpeed", "10");         v.speedVar = IniFloat(sec, "fSpeedVariation", "1");
			v.startSize = IniFloat(sec, "fStartSize", "0.3"); v.grow = IniFloat(sec, "fGrowSeconds", "2.6");
			v.opacity = IniFloat(sec, "fOpacity", "0.1");    v.fadeIn = IniFloat(sec, "fFadeIn", "0.05");
			v.fadeStart = IniFloat(sec, "fFadeStart", "0.2"); v.fadeEnd = IniFloat(sec, "fFadeEnd", "0.95");
			v.rise = IniFloat(sec, "fRise", "7");            v.curl = IniFloat(sec, "fCurl", "0.012");
			v.spread = IniFloat(sec, "fSpread", k < 2 ? "0.03" : "0.8");
			v.size *= k == ChainHeat ? Settings.trailScale : k == ChainTrail ? Settings.afterScale : IniFloat(sec, "fScale", "1.0");
		}
	}
	BakeLook("Puff", Settings.puffModel);
	BakeLook("Wisp", Settings.trailModel);
	BakeLook("Ejection", Settings.ejectModel);
	static char lastWisp[MAX_PATH] = "", lastAfter[MAX_PATH] = "";
	static int lastWispCount = 0, lastAfterCount = 0;
	if ((lastWisp[0] && _stricmp(lastWisp, Settings.trailModel)) || (lastWispCount && lastWispCount != Settings.wispEmitters))
		ReleaseWisp("its look changed");
	if ((lastAfter[0] && _stricmp(lastAfter, Settings.afterModel)) || (lastAfterCount && lastAfterCount != Settings.afterEmitters))
		ReleaseAfter("its look changed");
	lastWispCount = Settings.wispEmitters;
	lastAfterCount = Settings.afterEmitters;
	strcpy_s(lastWisp, Settings.trailModel);
	strcpy_s(lastAfter, Settings.afterModel);
	Settings.ejectScale=bounded("Ejection","fScale","1.0",0.01f,10);
	Settings.ejectLifetime=bounded("Ejection","fLifetime","1.5",0.1f,5);
	Settings.ejectHotScale=bounded("Ejection","fHotScale","1.8",0.1f,10);
	Settings.ejectBurst=bounded("Ejection","fBurstSeconds","0.06",0.01f,2);
	Settings.puffBurst=bounded("Puff","fBurstSeconds","0.12",0.01f,2);
	Settings.puffBurstRate=bounded("Puff","fBurstRate","60",1,600);
	Settings.ejectBurstRate=bounded("Ejection","fBurstRate","160",1,600);
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

__declspec(dllexport) bool __cdecl GunFX_GetHazeV2(float out[14], const void* weaponNode) {
	float heat[10];
	if (!out || !ReadHeatForNVR(heat, weaponNode, true)) return false;
	EnterCriticalSection(&HeatLock);
	memcpy(out, HazePublished, sizeof(HazePublished));
	memcpy(out + 8, HazeWorld, sizeof(HazeWorld));
	LeaveCriticalSection(&HeatLock);
	return out[0] > 0.001f;
}

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

__declspec(dllexport) void __cdecl GunFX_SetSwitches(UInt32 bits) {
	if ((LONG)bits != NvrSwitches) {
		InterlockedExchange(&NvrSwitches, (LONG)bits);
		LogLine("NVR menu switches: puff %d, heat smoke %d, ejection smoke %d, glow %d, haze %d, muzzle blast %d, energy weapons %d, after-fire trail %d, volumetric %d",
			bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, (bits >> 3) & 1, (bits >> 4) & 1, (bits >> 5) & 1, (bits >> 6) & 1, (bits >> 7) & 1, (bits >> 8) & 1);
	}
}

__declspec(dllexport) int __cdecl GunFX_GetVolumeSmoke(float* out, int maxRecords, float* params) {
	if (!out || !params || !HeatLockReady) return 0;
	EnterCriticalSection(&HeatLock);
	const int n = VolumePublishedCount < maxRecords ? VolumePublishedCount : maxRecords;
	for (int i = 0; i < n; i++) memcpy(out + i * 12, VolumePublished + i * RecordFloats, 12 * sizeof(float));
	memcpy(params, VolumeParams, sizeof(VolumeParams));
	LeaveCriticalSection(&HeatLock);
	return n;
}
__declspec(dllexport) int __cdecl GunFX_GetVolumeSmoke2(float* out, int maxRecords, float* params) {
	if (!out || !params || !HeatLockReady) return 0;
	EnterCriticalSection(&HeatLock);
	const int n = VolumePublishedCount < maxRecords ? VolumePublishedCount : maxRecords;
	memcpy(out, VolumePublished, n * RecordFloats * sizeof(float));
	memcpy(params, VolumeParams, sizeof(VolumeParams));
	LeaveCriticalSection(&HeatLock);
	return n;
}
__declspec(dllexport) void __cdecl GunFX_SetView(const float* v) {
	if (!v || !HeatLockReady) return;
	EnterCriticalSection(&HeatLock);
	ViewEye = NiPoint3{ v[0], v[1], v[2] };
	ViewForward = NiPoint3{ v[3], v[4], v[5] };
	ViewAt = GetTickCount64();
	LeaveCriticalSection(&HeatLock);
}
__declspec(dllexport) int __cdecl GunFX_GetBullets(float* out, int maxBullets) {
	if (!out || Settings.volumeBullet <= 0.0f) return 0;
	int n = 0;
	for (const Bullet& b : Bullets) {
		const float since = SmokeClock - b.at;
		if (b.at <= 0.0f || since < 0.0f || since >= Settings.volumeBulletSeconds || n >= maxBullets) continue;
		float* o = out + n++ * 8;
		o[0] = b.from.x; o[1] = b.from.y; o[2] = b.from.z; o[3] = since;
		o[4] = b.dir.x; o[5] = b.dir.y; o[6] = b.dir.z; o[7] = Settings.volumeBullet;
	}
	return n;
}
__declspec(dllexport) void __cdecl GunFX_GetBulletLook(float* out) {
	if (!out) return;
	out[0] = Settings.volumeBulletOpen;
	out[1] = Settings.volumeBulletSeconds;
}
__declspec(dllexport) float __cdecl GunFX_GetMoving() {
	return VolumeMoving;
}
__declspec(dllexport) int __cdecl GunFX_GetSmokeHaze(float* out, int maxBlobs) {
	if (!out || !HeatLockReady) return 0;
	EnterCriticalSection(&HeatLock);
	const int n = SmokeHazePublishedCount < maxBlobs ? SmokeHazePublishedCount : maxBlobs;
	memcpy(out, SmokeHazePublished, n * HazeFloats * sizeof(float));
	LeaveCriticalSection(&HeatLock);
	return n;
}
__declspec(dllexport) void __cdecl GunFX_GetSmokeFlash(float* out) {
	if (!out) return;
	memset(out, 0, 8 * sizeof(float));
	if (!HeatLockReady) return;
	EnterCriticalSection(&HeatLock);
	const NiPoint3 at = FlashWorld;
	const double shot = FlashAt;
	LeaveCriticalSection(&HeatLock);
	const float seconds = Settings.volumeFlashSeconds > 0.005f ? Settings.volumeFlashSeconds : 0.005f;
	const float since = shot > 0.0 ? (float)(PreciseSeconds() - shot) : 1e9f;
	if (since > seconds * 4.0f || Settings.volumeFlash <= 0.0f) return;
	out[0] = at.x; out[1] = at.y; out[2] = at.z;
	out[3] = Settings.volumeFlash * expf(-since / seconds);
	out[4] = 1.0f; out[5] = 0.55f; out[6] = 0.25f;
	out[7] = Settings.volumeFlashRadius;
}

__declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
	info->infoVersion = 1;
	info->name = "GunFX";
	info->version = 59;
	return !nvse->isEditor && nvse->runtimeVersion == 0x040020D0;
}

__declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface* nvse) {
	if (nvse->isEditor) return true;
	Log = _fsopen("GunFX.log", "w", _SH_DENYWR);
	LogLine("GunFX 1.39 (volumetric smoke prototype: heat smoke, trail, puff and ejection smoke; edge detail, sun and muzzle flash light, colour, wind, pushed by your body, smoothed strands, depth shading, drift aside, flows around the gun, tendrils, unzips and fans out around you and the gun, swirls behind the barrel and your body, recoil smoothed out, barrel direction from the muzzle node, no random flicks, gun shape probe, puff carried with the gun, smoke eased out of the gun and your body, air flows round your body and the gun, the gun's real shape, smoke you run into parts at once, stirred smoke spreads out and frays, heat smoke and trail flow out of the barrel, bursts when you run or swing through smoke, only a hanging trail unzips, CPU time logged, gun-smoke haze, haze in one pass of its own and stacking where you fire, bullets punch through the smoke and the haze, billowy puffs and fibrous strands, smoke timed on the precise clock, smoke inside you slides out instead of popping, clock time on each log line, hitch diagnostic: whole-step timing, smoke streams back along the gun as you move instead of being thrown aside by your body, and spreads over it, bullet holes open and fill in at your pace, and leave your own smoke near you alone while you move)");
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
