#pragma once

#include <cstdarg>
#include <unordered_set>
#include "../../core/GpuProfiler.h"
#include "../../core/GpuTimeline.h"

// Everything from the start of the game's render call up to the world scene: NVR shadow maps,
// the game's own pre-scene work (water reflection/refraction/depth maps and anything else).
static GpuTimer PreSceneTimer("Pre-scene (to world scene)");
static bool PreSceneTimerActive = false;
static void EndPreSceneTimer() {
	if (!PreSceneTimerActive) return;
	PreSceneTimer.End();
	PreSceneTimerActive = false;
}

// ---- F10: the game's own draws split by shader family; first shader uses; shader-bind cost ----
// The world scene, the water reflection map and the first-person model are drawn by the game's render
// loop, most families with NVR's replacement pixel shaders. SetShadersHook sees every geometry pass being
// set up. While F10 profiling is on it puts a GPU timestamp wherever the shader family changes inside one
// of those three passes, and the GpuTimeline adds up the GPU time per family ("GPU SPLIT" lines). It also
// counts binds per frame ("SHADER BINDS" lines) and remembers which D3D pixel shaders have already drawn in
// which pass, so a FRAME SPIKE line can name a shader used for the first time in that frame and the
// longest stall between two binds (the draws of a pass happen between its bind and the next one, so a
// driver compiling a shader at its first draw shows up there).
namespace ShaderSplit {
	enum Context : unsigned char { World, Reflections, FirstPerson, ContextCount, Outside = ContextCount };
	static const char* const ContextNames[ContextCount + 1] = { "world scene", "reflections", "first person", "other" };
	static const unsigned MaxLabels = 32; // label 0: from the start of the pass to its first shader bind
	static_assert(ContextCount * MaxLabels * 2 <= GpuTimeline::KeyCount, "split keys do not fit the timeline");

	static char LabelNames[MaxLabels][16] = { "(pass setup)" };
	static unsigned LabelCount = 1;
	static std::unordered_map<const void*, unsigned char> LabelOfShader;
	static GpuTimeline Timeline("World scene split");

	static unsigned char CurrentContext = Outside;
	static unsigned char CurrentKey = GpuTimeline::NoKey;
	static unsigned char SavedContext[8], SavedKey[8];
	static unsigned Depth = 0;

	// First use of a D3D pixel shader in a pass. Tracked all session, F10 or not, so "first" means first.
	static std::unordered_set<UInt64> Seen;
	static const void* LastHandle = nullptr;
	static unsigned char LastHandleContext = Outside;
	static unsigned FirstUsesSession = 0;

	struct Counters {
		unsigned Binds[ContextCount + 1];			// SetShaders calls (one per geometry pass)
		unsigned PixelChanges[ContextCount + 1];	// ... that changed the pixel shader
		unsigned NvrUploads[ContextCount + 1];		// ... to an NVR shader, so ShaderRecord::SetCT ran
		unsigned FamilyChanges[ContextCount + 1];	// timeline marks
		unsigned FirstUses;
		double HookMs;								// CPU time inside SetShadersHook
	};
	static Counters Frame = {}, Window = {};
	static unsigned WindowFrames = 0;
	static double WindowMaxHookMs = 0.0;
	static bool FrameProfiled = false;

	// This frame's first uses and its longest gap between two binds, for FRAME SPIKE lines.
	struct BindInfo { const char* Shader; unsigned char Context; bool Nvr; bool FirstUse; };
	static BindInfo FirstUseBinds[4];
	static BindInfo LastBind = {}, MaxGapBind = {};
	static double LastBindEnd = 0.0, MaxGapMs = 0.0;

	static unsigned char KeyOf(unsigned context, unsigned label, bool nvr) { return (unsigned char)((context * MaxLabels + label) * 2 + (nvr ? 1 : 0)); }

	static void Append(char* buffer, size_t size, size_t& used, const char* format, ...);

	// The lit-object pixel shaders (ObjectTemplate.hlsl, src/effects/PBR.h) split by variant group (P47, roadmap 3J):
	// 2000-2028 sun + up to 3 lights, 2029-2036 up to 6 lights in one pass, 2037-2044 the additive light passes that
	// redraw an object for more lights, 2045-2046 diffuse point lights, 2047-2056 the specular passes.
	static const char* SlsGroup(const char* name) {
		const int number = atoi(name + 3);
		if (number >= 2000 && number <= 2028) return "SLS 1-3 lights";
		if (number >= 2029 && number <= 2036) return "SLS 4+ lights";
		if (number >= 2037 && number <= 2044) return "SLS light pass";
		if (number >= 2045 && number <= 2046) return "SLS diffuse pt";
		if (number >= 2047 && number <= 2056) return "SLS specular";
		return "SLS other";
	}

	// Lights in use by the 4+ light shaders (roadmap 3J). They compute every light slot and multiply the unused ones
	// by 0; how many slots a draw really uses decides whether skipping them pays (tests/game_shaders.cpp: -11 to -29%
	// of their cost with 2 lights, +2 to +4% with all slots used). The game sets the count (EmittanceColor.a = c2.w,
	// or PSLightColor[0].a = c3.w in the OPT variants) after binding the shader, so it is read back from the device at
	// the next bind in the same pass, when that draw is done. Only while F10 runs.
	struct MultiLightShader { int Number; unsigned Slots; bool Opt; };
	static const MultiLightShader MultiLights[] = { { 2029, 6, false }, { 2030, 6, false }, { 2031, 4, false }, { 2032, 4, true },
		{ 2033, 4, false }, { 2034, 3, false }, { 2035, 3, true }, { 2036, 3, false } };
	static const unsigned MultiLightCount = sizeof(MultiLights) / sizeof(MultiLights[0]);
	static unsigned LightsInUse[MultiLightCount][8] = {};	// [shader][lights in use; 7 = 7 or more]
	static unsigned LightsReadFailures = 0;
	static int PendingMultiLight = -1;
	static unsigned char PendingMultiLightContext = Outside;

	static int MultiLightIndex(const char* name) {
		if (!name || strncmp(name, "SLS", 3)) return -1;
		const int number = atoi(name + 3);
		for (unsigned i = 0; i < MultiLightCount; ++i) if (MultiLights[i].Number == number) return (int)i;
		return -1;
	}

	static void SampleLightsInUse(unsigned char context) {
		const int pending = PendingMultiLight;
		PendingMultiLight = -1;
		if (pending < 0 || context != PendingMultiLightContext) return;
		float constants[8];
		if (FAILED(TheRenderManager->device->GetPixelShaderConstantF(2, constants, 2))) { LightsReadFailures++; return; }
		const float used = MultiLights[pending].Opt ? constants[7] : constants[3];
		const unsigned bucket = used >= 0.5f && used < 6.5f ? (unsigned)(used + 0.5f) : used >= 6.5f ? 7 : 0;
		LightsInUse[pending][bucket]++;
	}

	// Light slots the old code computes for a draw with `used` lights, of which a skipping version would compute
	// only these: slot 1 always, slot 2 if used > 1, slot 3 if used >= 2 (sic, the shader's own condition), slot 4 if
	// used >= 3, slot 5 if used >= 4, slot 6 if used >= 5.
	static unsigned NeededSlots(unsigned slots, unsigned used) {
		unsigned needed = 1 + (used > 1) + (used >= 2);
		if (slots > 3) needed += used >= 3;
		if (slots > 4) needed += (used >= 4) + (used >= 5);
		return needed < slots ? needed : slots;
	}

	static void ReportLightsInUse() {
		char line[1024] = {};
		size_t used = 0;
		unsigned draws = 0;
		double computed = 0.0, needed = 0.0;
		for (unsigned i = 0; i < MultiLightCount; ++i) {
			unsigned total = 0;
			for (unsigned b = 0; b < 8; ++b) total += LightsInUse[i][b];
			if (!total) continue;
			draws += total;
			Append(line, sizeof(line), used, " | SLS%d (%u slots):", MultiLights[i].Number, MultiLights[i].Slots);
			for (unsigned b = 0; b < 8; ++b) {
				if (!LightsInUse[i][b]) continue;
				Append(line, sizeof(line), used, " %u%s x%u", b, b == 7 ? "+" : "", LightsInUse[i][b]);
				computed += (double)MultiLights[i].Slots * LightsInUse[i][b];
				needed += (double)NeededSlots(MultiLights[i].Slots, b) * LightsInUse[i][b];
			}
		}
		if (draws)
			Logger::Log("SLS 4+ LIGHTS in use per draw, %u draws: light slots computed for nothing %.0f%%%s", draws,
				100.0 * (computed - needed) / computed, line);
		if (LightsReadFailures) Logger::Log("SLS 4+ LIGHTS: the device did not return shader constants %u times", LightsReadFailures);
		memset(LightsInUse, 0, sizeof(LightsInUse));
		LightsReadFailures = 0;
	}

	// Family of a game pixel shader: the terrain templates by name, the lit-object variant groups (SlsGroup),
	// otherwise the leading letters of the shader name (PAR = parallax objects, SKIN, SM3 = hair and eyes, STLEAF =
	// tree leaves, GRASS, SKY, WATER, ...). Worked out once per shader.
	static unsigned char LabelFor(const NiD3DPixelShader* shader) {
		auto found = LabelOfShader.find(shader);
		if (found != LabelOfShader.end()) return found->second;
		char name[16] = {};
		const char* source = shader ? shader->Name : nullptr;
		const char* terrain = source && TheShaderManager->Shaders.Terrain ? TheShaderManager->Shaders.Terrain->GetTemplate(source).Name : nullptr;
		if (!source) strcpy_s(name, "(no shader)");
		else if (terrain) strcpy_s(name, !strcmp(terrain, "TerrainLODTemplate") ? "TERRAIN LOD" : !strcmp(terrain, "TerrainFadeTemplate") ? "TERRAIN FADE" : "TERRAIN");
		else if (!strncmp(source, "SLS", 3) && source[3] >= '0' && source[3] <= '9') strcpy_s(name, SlsGroup(source));
		else if (!strncmp(source, "SM3", 3)) strcpy_s(name, "SM3");
		else if (!strncmp(source, "SKY", 3)) strcpy_s(name, "SKY");
		else if (!strncmp(source, "WATER", 5)) strcpy_s(name, "WATER");
		else if (!strncmp(source, "ISHDR", 5) || !strncmp(source, "HDR", 3)) strcpy_s(name, "HDR");
		else {
			size_t n = 0;
			while (n < sizeof(name) - 1 && source[n] >= 'A' && source[n] <= 'Z') { name[n] = source[n]; n++; }
			if (!n) strcpy_s(name, "(other)");
		}
		unsigned char label = 0;
		for (unsigned i = 1; i < LabelCount; ++i)
			if (!strcmp(LabelNames[i], name)) { label = (unsigned char)i; break; }
		if (!label) {
			if (LabelCount < MaxLabels - 1) { strcpy_s(LabelNames[LabelCount], name); label = (unsigned char)LabelCount++; }
			else { label = MaxLabels - 1; strcpy_s(LabelNames[label], "(more)"); }
		}
		LabelOfShader[shader] = label;
		return label;
	}

	static void Append(char* buffer, size_t size, size_t& used, const char* format, ...) {
		if (used >= size) return;
		va_list args;
		va_start(args, format);
		const int written = _vsnprintf_s(buffer + used, size - used, _TRUNCATE, format, args);
		va_end(args);
		used = written < 0 ? size : used + written;
	}

	static void SpikeDetail(char* buffer, size_t size) {
		if (!FrameProfiled) return;
		unsigned binds = 0;
		for (unsigned c = 0; c <= ContextCount; ++c) binds += Frame.Binds[c];
		size_t used = 0;
		Append(buffer, size, used, " | shader binds %u (%.2f ms CPU), first uses %u", binds, Frame.HookMs, Frame.FirstUses);
		for (unsigned i = 0; i < Frame.FirstUses && i < 4; ++i)
			Append(buffer, size, used, "%s%s (%s, %s)", i ? ", " : ": ", FirstUseBinds[i].Shader, FirstUseBinds[i].Nvr ? "NVR" : "vanilla", ContextNames[FirstUseBinds[i].Context]);
		if (MaxGapBind.Shader)
			Append(buffer, size, used, " | longest gap between binds %.1f ms, after %s (%s, %s%s)", MaxGapMs, MaxGapBind.Shader,
				MaxGapBind.Nvr ? "NVR" : "vanilla", ContextNames[MaxGapBind.Context], MaxGapBind.FirstUse ? ", first use" : "");
	}

	static void ReportSplit(const GpuTimeline& timeline) {
		struct Row { double Avg, Max; unsigned Label; bool Nvr; };
		for (unsigned c = 0; c < ContextCount; ++c) {
			Row rows[MaxLabels * 2];
			unsigned count = 0;
			double total = 0.0;
			for (unsigned label = 0; label < MaxLabels; ++label) {
				for (unsigned nvr = 0; nvr < 2; ++nvr) {
					const unsigned key = KeyOf(c, label, nvr != 0);
					const double avg = timeline.AverageMs(key);
					if (avg <= 0.0) continue;
					rows[count++] = { avg, timeline.MaxMs(key), label, nvr != 0 };
					total += avg;
				}
			}
			if (!count) continue;
			std::sort(rows, rows + count, [](const Row& a, const Row& b) { return a.Avg > b.Avg; });
			Logger::Log("GPU SPLIT %s, %u frames: %.4f ms per frame by shader family (NVR = NVR's replacement shader, vanilla = the game's own)",
				ContextNames[c], timeline.WindowFrames(), total);
			for (unsigned i = 0; i < count; ++i)
				Logger::Log("GPU SPLIT   %-12s %-14s %-7s avg %.4f ms  max %.4f  (%4.1f%%)", ContextNames[c], LabelNames[rows[i].Label],
					rows[i].Nvr ? "NVR" : "vanilla", rows[i].Avg, rows[i].Max, 100.0 * rows[i].Avg / total);
		}
		Logger::Log("GPU SPLIT   most family changes in one frame %u; frames dropped (over %u changes) %u, rejected (clock) %u",
			timeline.MostMarks(), GpuTimeline::MaxMarks, timeline.DroppedFrames(), timeline.RejectedFrames());
	}

	static void ReportCounters() {
		const double n = WindowFrames;
		char line[768] = {};
		size_t used = 0;
		Append(line, sizeof(line), used, "SHADER BINDS per frame (%u frames): binds / pixel shader changes / of those to NVR shaders / family changes:", WindowFrames);
		for (unsigned c = 0; c <= ContextCount; ++c)
			Append(line, sizeof(line), used, "%s %s %.0f / %.0f / %.0f / %.0f", c ? " |" : "", ContextNames[c], Window.Binds[c] / n, Window.PixelChanges[c] / n,
				Window.NvrUploads[c] / n, Window.FamilyChanges[c] / n);
		Logger::Log("%s", line);
		Logger::Log("SHADER BINDS   CPU inside SetShaders avg %.3f ms max %.3f per frame | first shader uses %u in these frames, %u this session",
			Window.HookMs / n, WindowMaxHookMs, Window.FirstUses, FirstUsesSession);
		ReportLightsInUse();
	}

	static void BeginFrame(IDirect3DDevice9* device) {
		static bool connected = false;
		if (!connected) {
			FrameTimeMonitor::SpikeDetail = &SpikeDetail;
			Timeline.OnReport = &ReportSplit;
			connected = true;
		}
		Depth = 0;
		CurrentContext = Outside;
		CurrentKey = GpuTimeline::NoKey;
		Frame = {};
		PendingMultiLight = -1;
		LastBind = { "(frame start)", Outside, false, false };
		MaxGapBind = {};
		MaxGapMs = 0.0;
		FrameProfiled = GpuTimer::Enabled && !InterfaceManager->IsActive(Menu::kMenuType_Loading);
		LastBindEnd = FrameProfiled ? CpuTimer::NowMs() : 0.0;
		if (FrameProfiled) Timeline.BeginFrame(device);
	}

	static void EndFrame() {
		Timeline.EndFrame();
		if (!FrameProfiled || !GpuTimer::Enabled) return;
		for (unsigned c = 0; c <= ContextCount; ++c) {
			Window.Binds[c] += Frame.Binds[c];
			Window.PixelChanges[c] += Frame.PixelChanges[c];
			Window.NvrUploads[c] += Frame.NvrUploads[c];
			Window.FamilyChanges[c] += Frame.FamilyChanges[c];
		}
		Window.FirstUses += Frame.FirstUses;
		Window.HookMs += Frame.HookMs;
		if (Frame.HookMs > WindowMaxHookMs) WindowMaxHookMs = Frame.HookMs;
		if (++WindowFrames >= 120) {
			ReportCounters();
			Window = {};
			WindowFrames = 0;
			WindowMaxHookMs = 0.0;
		}
	}

	// The game pass being drawn (nests: the stack restores the outer pass and its current family).
	static void BeginContext(Context context) {
		if (Depth < 8) { SavedContext[Depth] = CurrentContext; SavedKey[Depth] = CurrentKey; }
		Depth++;
		CurrentContext = context;
		CurrentKey = Timeline.InFrame() ? KeyOf(context, 0, false) : GpuTimeline::NoKey;
		Timeline.Mark(CurrentKey);
	}

	static void EndContext() {
		if (!Depth) return;
		Depth--;
		CurrentContext = Depth < 8 ? SavedContext[Depth] : (unsigned char)Outside;
		CurrentKey = Depth < 8 ? SavedKey[Depth] : GpuTimeline::NoKey;
		Timeline.Mark(CurrentKey);
	}

	// Called from SetShadersHook once the pixel shader for the pass is chosen. Start: when the hook began.
	static void OnBind(const NiD3DPixelShader* shader, const IDirect3DPixelShader9* previous, double start) {
		const unsigned char context = CurrentContext;
		const IDirect3DPixelShader9* handle = shader ? shader->ShaderHandle : nullptr;
		const bool nvr = handle && handle != (const IDirect3DPixelShader9*)shader->ShaderHandleBackup;
		bool firstUse = false;
		if (handle && (handle != LastHandle || context != LastHandleContext)) {
			LastHandle = handle;
			LastHandleContext = context;
			firstUse = Seen.insert(((UInt64)(uintptr_t)handle << 2) | (context & 3)).second;
			if (firstUse) FirstUsesSession++;
		}
		if (!FrameProfiled) return;

		// The previous pass drew between the end of its bind and the start of this one.
		if (LastBindEnd > 0.0 && start - LastBindEnd > MaxGapMs) { MaxGapMs = start - LastBindEnd; MaxGapBind = LastBind; }
		Frame.Binds[context]++;
		if (handle != previous) {
			Frame.PixelChanges[context]++;
			if (nvr) Frame.NvrUploads[context]++;
		}
		LastBind = { shader ? shader->Name : "(no pixel shader)", context, nvr, firstUse };
		if (firstUse && Frame.FirstUses++ < 4) FirstUseBinds[Frame.FirstUses - 1] = LastBind;

		// The previous pass's draw is done: read its lights in use if it was a 4+ light shader, then note this one.
		SampleLightsInUse(context);
		const int multiLight = context != Outside ? MultiLightIndex(shader ? shader->Name : nullptr) : -1;
		if (multiLight >= 0) { PendingMultiLight = multiLight; PendingMultiLightContext = context; }

		if (context == Outside || !Timeline.InFrame()) return;
		const unsigned char key = KeyOf(context, LabelFor(shader), nvr);
		if (key != CurrentKey) {
			Timeline.Mark(key);
			CurrentKey = key;
			Frame.FamilyChanges[context]++;
		}
	}

	static void EndBind(double start) {
		const double now = CpuTimer::NowMs();
		Frame.HookMs += now - start;
		LastBindEnd = now;
	}
}

// ---- Which of the game's render entry points ran this frame ----
// The depth buffers NVR's effects read are refreshed only inside RenderWorldSceneGraphHook. After a save is
// loaded straight into an interior the game has been seen to render frames without entering it at all
// (the F10 log then shows no 'World scene (game)' / 'Depth resolves' samples and 'Pre-scene' equal to the
// whole frame), and the effects run on a stale depth buffer: the scene comes out almost black. This
// records what happened on the first frames after each cell change, and lets ProcessImageSpaceShadersHook
// skip NVR's effects while the world scene keeps going missing (the WorldSceneGuard switch).
static bool WorldRenderedThisFrame = false;
static unsigned WorldMissStreak = 0;        // consecutive earlier frames without a world scene render
static unsigned FrameWorldCalls = 0, FrameFirstPersonCalls = 0, FrameImageSpaceCalls = 0;
static bool FrameWorldArgsKnown = false;
static int FrameWorldArgs[3] = {};          // IsFirstPerson, WireFrame, Arg4 of the first call this frame
static const unsigned WorldGuardFrames = 10;

// The world is not drawn behind the main menu or a loading screen, so those frames say nothing about it.
static bool WorldRenderExpected() {
	return Player && Player->parentCell && !InterfaceManager->IsActive(Menu::MenuType::kMenuType_Main) &&
		!InterfaceManager->IsActive(Menu::MenuType::kMenuType_Loading);
}

static bool WorldSceneGuardActive() {
	return !TheSettingManager->SettingsMain.Main.DisableWorldSceneGuard && !WorldRenderedThisFrame &&
		WorldMissStreak >= WorldGuardFrames && WorldRenderExpected();
}

static void ReportWorldRender(BSRenderedTexture* RenderedTexture, int Arg2, int Arg3) {
	static TESObjectCELL* lastCell = nullptr;
	static unsigned frame = 0, logged = 0, tracePending = 0;
	static bool guardLogged = false;
	frame++;

	TESObjectCELL* cell = Player ? Player->parentCell : nullptr;
	if (!cell) { WorldMissStreak = 0; lastCell = nullptr; return; } // no cell: nothing to report
	if (!WorldRenderExpected()) { WorldMissStreak = 0; return; }     // main menu / loading screen
	if (cell != lastCell) { lastCell = cell; tracePending = 3; }

	const bool missed = !WorldRenderedThisFrame;
	const bool started = missed && WorldMissStreak == 0;
	const bool recovered = !missed && WorldMissStreak >= 3;
	const bool guarded = missed && WorldMissStreak >= WorldGuardFrames;

	if (logged < 80 && (tracePending || started || recovered || (guarded && !guardLogged))) {
		const char* name = cell->GetEditorName();
		Logger::Log("WORLD TRACE frame %u: cell %08X '%s' interior=%d behaveLikeExterior=%d | world scene calls %u%s | first person %u, image space %u | "
			"render args rt=%p %d %d | menuBackgroundReady=%d mainMenu=%d loading=%d | miss streak %u%s%s",
			frame, cell->refID, name ? name : "?", cell->IsInterior() ? 1 : 0, (cell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior) ? 1 : 0,
			FrameWorldCalls, FrameWorldArgsKnown ? (FrameWorldArgs[0] ? " (first-person pass)" : " (world pass)") : " (none)",
			FrameFirstPersonCalls, FrameImageSpaceCalls, RenderedTexture, Arg2, Arg3,
			TESMain::IsMenuBackgroundReady() ? 1 : 0, InterfaceManager->IsActive(Menu::MenuType::kMenuType_Main) ? 1 : 0,
			InterfaceManager->IsActive(Menu::MenuType::kMenuType_Loading) ? 1 : 0, WorldMissStreak,
			started ? " [WORLD SCENE NOT RENDERED]" : "", recovered ? " [world scene rendering again]" : "");
		logged++;
		if (tracePending) tracePending--;
		if (guarded) guardLogged = true;
	}
	if (recovered) guardLogged = false;

	WorldMissStreak = missed ? WorldMissStreak + 1 : 0;
}

void (__thiscall* Render)(Main*, BSRenderedTexture*, int, int) = (void (__thiscall*)(Main*, BSRenderedTexture*, int, int))Hooks::Render;
void __fastcall RenderHook(Main* This, UInt32 edx, BSRenderedTexture* RenderedTexture, int Arg2, int Arg3) {
	
	SettingsMainStruct* SettingsMain = &TheSettingManager->SettingsMain;

	TheFrameRateManager->UpdatePerformance();
	TheCameraManager->SetSceneGraph();
	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();

	TheShaderManager->UpdateConstants();

	// Reset the material pass queue for the frame. Passing false when the flashlight is
	// not lit leaves it inactive, so the scene walk in RenderWorldSceneGraphHook costs
	// nothing while the light is off.
	FlashlightEffect* Flashlight = TheShaderManager->Effects.Flashlight;
	MaterialPass::BeginFrame(Flashlight->Enabled && Flashlight->spotLightActive);

	//if (SettingsMain->Develop.TraceShaders && InterfaceManager->IsActive(Menu::MenuType::kMenuType_None) && Global->OnKeyDown(SettingsMain->Develop.TraceShaders) && DWNode::Get() == NULL) DWNode::Create();
	// Whole game frame on the GPU (scene, reflections, NVR effects, image space). Comparing it
	// with the individual buckets shows how much GPU time is not attributed to any of them.
	static GpuTimer frameTimer("Game frame total");
	GpuProfileScope gpu(frameTimer, TheRenderManager->device);
	PreSceneTimerActive = PreSceneTimer.Begin(TheRenderManager->device);
	WorldRenderedThisFrame = false;
	FrameWorldCalls = FrameFirstPersonCalls = FrameImageSpaceCalls = 0;
	FrameWorldArgsKnown = false;
	ShaderSplit::BeginFrame(TheRenderManager->device);
	(*Render)(This, RenderedTexture, Arg2, Arg3);
	ShaderSplit::EndFrame();
	EndPreSceneTimer();
	ReportWorldRender(RenderedTexture, Arg2, Arg3);

}

// Set when NVR changes, mid-frame, a TESR_ constant that game shaders read (CheapReflections). Constants
// reach a game shader only in ShaderRecord::SetCT, which runs when the pixel shader CHANGES, so the next
// bind is told that no pixel shader is bound: an NVR shader then uploads even if the game keeps the one
// already bound. Nothing else changes (the game still sets the device shader itself).
static bool ForcePixelConstants = false;

// ReducedQuality CheapReflections: the water reflection map is drawn without the forward sun-shadow
// lookup (TESR_ShadowForwardData.x = 1, the forward path's own off switch) and without terrain parallax
// (TESR_TerrainParallaxData.x = 0), like the game's own reflections. Both are restored, and uploaded
// again on the next bind, when the pass ends.
class CheapReflectionScope {
public:
	CheapReflectionScope() {
		if (!TheSettingManager->SettingsMain.Main.CheapReflections) return;
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		TerrainShaders* terrain = TheShaderManager->Shaders.Terrain;
		if (!shadows || !terrain) return;
		Forward = &shadows->Constants.ForwardData.x;
		Parallax = &terrain->ParallaxConstants.Data.x;
		SavedForward = *Forward;
		SavedParallax = *Parallax;
		*Forward = 1.0f;
		*Parallax = 0.0f;
		ForcePixelConstants = true;
		static bool announced = false;
		if (!announced) {
			Logger::Log("UNOFFICIAL cheap reflections: water reflection map drawn without sun shadows and terrain parallax.");
			announced = true;
		}
	}
	~CheapReflectionScope() {
		if (!Forward) return;
		*Forward = SavedForward;
		*Parallax = SavedParallax;
		ForcePixelConstants = true;
	}

private:
	float* Forward = nullptr;
	float* Parallax = nullptr;
	float SavedForward = 0.0f;
	float SavedParallax = 0.0f;
};

// ReducedQuality CheapUnderwaterTerrain decides per pixel from the camera-relative world position, which the terrain
// shader reconstructs with the MAIN camera's matrices. In the water reflection pass (mirrored camera) that position
// is meaningless, so the water line test is switched off (TESR_TerrainParallaxExtraData.w = -FLT_MAX) for the pass.
class UnderwaterTerrainReflectionScope {
public:
	UnderwaterTerrainReflectionScope() {
		TerrainShaders* terrain = TheShaderManager->Shaders.Terrain;
		if (!TheSettingManager->SettingsMain.Main.CheapUnderwaterTerrain || !terrain) return;
		Waterline = &terrain->ParallaxConstants.ExtraData.w;
		Saved = *Waterline;
		*Waterline = -FLT_MAX;
		ForcePixelConstants = true;
	}
	~UnderwaterTerrainReflectionScope() {
		if (!Waterline) return;
		*Waterline = Saved;
		ForcePixelConstants = true;
	}

private:
	float* Waterline = nullptr;
	float Saved = 0.0f;
};

// Water reflection probe (diagnostic, P54). The game draws the water reflection map with its own camera inside
// RenderReflections, and the water shaders look the map up as if that camera were an exact mirror of the main one.
// A player saw the reflection squashed when looking level and right when looking down, so the two apparently differ.
// After an NVR screenshot (ScreenshotKey), the next frame records, at the first shader bind of the world pass and of
// the reflection pass, the render target, viewport, D3D view and projection transforms and the cameras; the log gets
// both and the reflection map is saved next to the screenshot as "<name> reflection.png".
extern char LastScreenshotBase[MAX_PATH];
extern char LastScreenshotName[80];
extern bool ScreenshotTakenThisFrame;

namespace ReflectionProbe {
	struct CameraShot {
		bool valid = false;
		NiPoint3 pos = {};
		float rot[3][3] = {};
		NiFrustum frustum;
	};
	struct PassShot {
		bool valid = false;
		D3DVIEWPORT9 viewport = {};
		D3DSURFACE_DESC target = {};
		D3DXMATRIX view, proj;
		HRESULT viewHr = E_FAIL, projHr = E_FAIL;
		CameraShot scene, hook;
		NiPoint3 worldTranslate = {}, location = {};  // the game's camera globals the pass renders relative to
		char geometry[64] = {};
		NiPoint3 geometryPos = {};
		DWORD clipEnable = 0, cullMode = 0, zFunc = 0, zEnable = 0;
		float clip[2][4] = {};
	};
	// Every object drawn in the recorded reflection pass (name, position, pixel shader, clip planes at its bind).
	struct DrawnObject { char name[48]; char shader[24]; NiPoint3 pos; DWORD clipEnable; float clip0[4]; };
	static DrawnObject Drawn[64];
	static int DrawnCount = 0, DrawnTotal = 0;
	static bool DrawnDone = false;  // one reflection pass only: set once the world pass follows a recorded one
	static PassShot Shots[2];  // 0: world scene, 1: reflections
	static IDirect3DSurface9* ReflectionTarget = nullptr;
	static NiCamera* HookCamera = nullptr;  // the camera the game passed to RenderReflections
	static int ArmedFrames = 0;
	static char Base[MAX_PATH], Name[80];

	static void Copy(CameraShot& shot, NiCamera* camera) {
		if (!camera) return;
		shot.pos = camera->m_worldTransform.pos;
		memcpy(shot.rot, camera->m_worldTransform.rot.data, sizeof(shot.rot));
		shot.frustum = camera->Frustum;
		shot.valid = true;
	}

	static void OnBind(NiGeometry* geometry, const char* pixelShader) {
		if (!ArmedFrames) return;
		const unsigned char context = ShaderSplit::CurrentContext;
		const int slot = context == ShaderSplit::World ? 0 : context == ShaderSplit::Reflections ? 1 : -1;
		IDirect3DDevice9* device = TheRenderManager->device;
		if (slot == 0 && Shots[1].valid) DrawnDone = true;
		if (slot == 1 && !DrawnDone) {
			DrawnTotal++;
			if (DrawnCount < 64) {
				DrawnObject& d = Drawn[DrawnCount++];
				strncpy_s(d.name, geometry && geometry->m_pcName ? geometry->m_pcName : "(none)", _TRUNCATE);
				strncpy_s(d.shader, pixelShader ? pixelShader : "(none)", _TRUNCATE);
				d.pos = geometry ? geometry->m_worldTransform.pos : NiPoint3{};
				device->GetRenderState(D3DRS_CLIPPLANEENABLE, &d.clipEnable);
				if (FAILED(device->GetClipPlane(0, d.clip0))) memset(d.clip0, 0, sizeof(d.clip0));
			}
		}
		if (slot < 0 || Shots[slot].valid) return;
		PassShot& s = Shots[slot];
		device->GetViewport(&s.viewport);
		IDirect3DSurface9* target = nullptr;
		if (SUCCEEDED(device->GetRenderTarget(0, &target)) && target) {
			target->GetDesc(&s.target);
			if (slot == 1) { if (ReflectionTarget) ReflectionTarget->Release(); ReflectionTarget = target; }
			else target->Release();
		}
		s.viewHr = device->GetTransform(D3DTS_VIEW, &s.view);
		s.projHr = device->GetTransform(D3DTS_PROJECTION, &s.proj);
		Copy(s.scene, WorldSceneGraph ? WorldSceneGraph->camera : nullptr);
		if (slot == 1) Copy(s.hook, HookCamera);
		device->GetRenderState(D3DRS_CLIPPLANEENABLE, &s.clipEnable);
		device->GetRenderState(D3DRS_CULLMODE, &s.cullMode);
		device->GetRenderState(D3DRS_ZFUNC, &s.zFunc);
		device->GetRenderState(D3DRS_ZENABLE, &s.zEnable);
		for (DWORD i = 0; i < 2; i++) if (FAILED(device->GetClipPlane(i, s.clip[i]))) memset(s.clip[i], 0, sizeof(s.clip[i]));
		s.worldTranslate = *Pointers::Generic::CameraWorldTranslate;
		s.location = *Pointers::Generic::CameraLocation;
		if (geometry) {
			strncpy_s(s.geometry, geometry->m_pcName ? geometry->m_pcName : "(no name)", _TRUNCATE);
			s.geometryPos = geometry->m_worldTransform.pos;
		}
		s.valid = true;
	}

	static void LogCamera(const char* label, const CameraShot& c) {
		if (!c.valid) { Logger::Log("UNOFFICIAL reflection probe   %s: none", label); return; }
		// NVR's convention (RenderManager::SetupSceneCamera): columns 0, 1, 2 are forward, up, right.
		const float fx = c.rot[0][0], fy = c.rot[1][0], fz = c.rot[2][0];
		Logger::Log("UNOFFICIAL reflection probe   %s: pos %.1f %.1f %.1f | forward %.4f %.4f %.4f (pitch %.2f deg) | up %.4f %.4f %.4f | right %.4f %.4f %.4f | frustum L %.4f R %.4f T %.4f B %.4f N %.2f F %.0f",
			label, c.pos.x, c.pos.y, c.pos.z, fx, fy, fz, D3DXToDegree(asinf(max(-1.0f, min(1.0f, fz)))),
			c.rot[0][1], c.rot[1][1], c.rot[2][1], c.rot[0][2], c.rot[1][2], c.rot[2][2],
			c.frustum.Left, c.frustum.Right, c.frustum.Top, c.frustum.Bottom, c.frustum.Near, c.frustum.Far);
	}

	static void LogPass(const char* label, const PassShot& s) {
		if (!s.valid) { Logger::Log("UNOFFICIAL reflection probe %s: not drawn in the recorded frames", label); return; }
		Logger::Log("UNOFFICIAL reflection probe %s: target %ux%u format %u, viewport %lu %lu %lux%lu depth %.2f-%.2f",
			label, s.target.Width, s.target.Height, (unsigned)s.target.Format, s.viewport.X, s.viewport.Y, s.viewport.Width, s.viewport.Height, s.viewport.MinZ, s.viewport.MaxZ);
		if (SUCCEEDED(s.viewHr)) {
			const D3DXMATRIX& v = s.view;
			Logger::Log("UNOFFICIAL reflection probe   D3D view: %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.1f %.1f %.1f %.4f",
				v._11, v._12, v._13, v._14, v._21, v._22, v._23, v._24, v._31, v._32, v._33, v._34, v._41, v._42, v._43, v._44);
		}
		else Logger::Log("UNOFFICIAL reflection probe   D3D view: not available (hr %08lX)", (unsigned long)s.viewHr);
		if (SUCCEEDED(s.projHr)) {
			const D3DXMATRIX& p = s.proj;
			Logger::Log("UNOFFICIAL reflection probe   D3D projection: %.4f %.4f %.4f %.4f | %.4f %.4f %.4f %.4f | %.4f %.4f %.6f %.4f | %.4f %.4f %.4f %.4f",
				p._11, p._12, p._13, p._14, p._21, p._22, p._23, p._24, p._31, p._32, p._33, p._34, p._41, p._42, p._43, p._44);
		}
		else Logger::Log("UNOFFICIAL reflection probe   D3D projection: not available (hr %08lX)", (unsigned long)s.projHr);
		LogCamera("scene camera", s.scene);
		if (&s == &Shots[1]) LogCamera("camera passed to RenderReflections", s.hook);
		Logger::Log("UNOFFICIAL reflection probe   game camera globals: world translate %.1f %.1f %.1f, location %.1f %.1f %.1f | first geometry '%s' at %.1f %.1f %.1f",
			s.worldTranslate.x, s.worldTranslate.y, s.worldTranslate.z, s.location.x, s.location.y, s.location.z,
			s.geometry, s.geometryPos.x, s.geometryPos.y, s.geometryPos.z);
		Logger::Log("UNOFFICIAL reflection probe   states: clip planes enabled 0x%lX, plane 0 %.6f %.6f %.6f %.6f, plane 1 %.6f %.6f %.6f %.6f | cull %lu, zfunc %lu, zenable %lu, engine Z clear %.1f",
			s.clipEnable, s.clip[0][0], s.clip[0][1], s.clip[0][2], s.clip[0][3], s.clip[1][0], s.clip[1][1], s.clip[1][2], s.clip[1][3],
			s.cullMode, s.zFunc, s.zEnable, NiDX9Renderer::GetSingleton()->m_fZClear);
	}

	static void LogDrawn() {
		Logger::Log("UNOFFICIAL reflection probe: %d binds in the recorded reflection pass (first %d listed)", DrawnTotal, DrawnCount);
		for (int i = 0; i < DrawnCount; i++) {
			const DrawnObject& d = Drawn[i];
			Logger::Log("UNOFFICIAL reflection probe   drawn %2d: '%s' (%s) at %.0f %.0f %.0f | clip 0x%lX %.6f %.6f %.6f %.6f",
				i, d.name, d.shader, d.pos.x, d.pos.y, d.pos.z, d.clipEnable, d.clip0[0], d.clip0[1], d.clip0[2], d.clip0[3]);
		}
	}

	// Which water planes the game knows, and their heights: the reflection must be mirrored about the one in view.
	static void LogWater() {
		if (!Player || !Player->parentCell) return;
		TESWaterForm* form = nullptr;
		const float chosen = (Tes && WorldSceneGraph) ? Tes->GetWaterHeight(Player, WorldSceneGraph, &form) : 0.0f;
		Logger::Log("UNOFFICIAL reflection probe water: player z %.1f, cell water height %.1f, NVR's GetWaterHeight %.1f",
			Player->pos.z, Player->parentCell->GetWaterHeight(), chosen);
		WaterManager* water = Tes ? Tes->waterManager : nullptr;
		if (!water) return;
		Logger::Log("UNOFFICIAL reflection probe water manager: groups %u, unk24 %.3f, unk38 %.3f, unk98 %.3f, unk34 %u, unk9C %u",
			water->waterGroups.count, water->unk24, water->unk38, water->unk98, (unsigned)water->unk34, (unsigned)water->unk9C);
		DNode<WaterGroup>* node = water->waterGroups.first;
		for (UInt32 i = 0; node && i < water->waterGroups.count && i < 8; i++, node = node->next) {
			WaterGroup* group = node->data;
			if (!group) continue;
			char planes[256] = "";
			DNode<TESObjectREFR>* plane = group->waterPlanes.first;
			for (UInt32 j = 0; plane && j < group->waterPlanes.count && j < 4; j++, plane = plane->next) {
				NiNode* planeNode = plane->data ? plane->data->GetNode() : nullptr;
				NiBound* bound = planeNode ? planeNode->GetWorldBound() : nullptr;
				char entry[64];
				if (bound) sprintf_s(entry, " [z %.1f r %.0f]", bound->Center.z, bound->Radius);
				else strcpy_s(entry, " [no node]");
				strcat_s(planes, entry);
			}
			Logger::Log("UNOFFICIAL reflection probe water group %u: vector04 %.3f %.3f %.3f %.1f | vector14 %.3f %.3f %.3f %.1f | %u planes%s | bytes %u %u %u %u %u",
				i, group->vector04.x, group->vector04.y, group->vector04.z, group->vector04.w,
				group->vector14.x, group->vector14.y, group->vector14.z, group->vector14.w,
				group->waterPlanes.count, planes, group->byte5C, group->byte5D, group->byte5E, group->byte5F, group->byte60);
		}
	}

	static void Report() {
		Logger::Log("UNOFFICIAL reflection probe for screenshot %s:", Name);
		LogPass("world pass", Shots[0]);
		LogPass("reflection pass", Shots[1]);
		LogDrawn();
		LogWater();
		if (ReflectionTarget) {
			char file[MAX_PATH];
			sprintf_s(file, "%s reflection.png", Base);
			const HRESULT hr = D3DXSaveSurfaceToFileA(file, D3DXIFF_PNG, ReflectionTarget, NULL, NULL);
			Logger::Log("UNOFFICIAL reflection probe: reflection map %s as \"%s reflection.png\" (hr %08lX)", SUCCEEDED(hr) ? "saved" : "NOT saved", Name, (unsigned long)hr);
			ReflectionTarget->Release();
			ReflectionTarget = nullptr;
		}
	}

	// Once per frame, after the screenshot check.
	static void EndFrame() {
		if (ScreenshotTakenThisFrame) {
			ScreenshotTakenThisFrame = false;
			strcpy_s(Base, LastScreenshotBase);
			strcpy_s(Name, LastScreenshotName);
			Shots[0] = PassShot();
			Shots[1] = PassShot();
			DrawnCount = DrawnTotal = 0;
			DrawnDone = false;
			if (ReflectionTarget) { ReflectionTarget->Release(); ReflectionTarget = nullptr; }
			ArmedFrames = 30;  // the reflection pass only runs while water is in view
			return;
		}
		if (!ArmedFrames) return;
		if ((Shots[0].valid && Shots[1].valid) || --ArmedFrames == 0) {
			Report();
			ArmedFrames = 0;
		}
	}
}

// GunFX master switches (menu Main > GunFX, all off by default): sent to GunFX.dll, which runs the effects, about five
// times a second. Bits: 0 puff, 1 heat smoke strand, 2 ejection smoke, 3 barrel glow, 4 heat haze, 5 muzzle blast,
// 6 energy weapons too (off = no GunFX on lasers, plasma, ...), 7 after-fire trail, 8 volumetric heat smoke/trail.
namespace GunFXSwitches {
	static void Update() {
		static ULONGLONG last = 0;
		const ULONGLONG now = GetTickCount64();
		if (now - last < 200) return;
		last = now;
		typedef void (__cdecl* SetFn)(UInt32);
		static SetFn set = nullptr;
		if (!set) {
			HMODULE gunfx = GetModuleHandleA("GunFX.dll");
			if (gunfx) set = (SetFn)GetProcAddress(gunfx, "GunFX_SetSwitches");
			if (!set) return;
		}
		static const char* const keys[] = { "Puff", "HeatSmoke", "EjectionSmoke", "BarrelGlow", "HeatHaze", "MuzzleBlast", "EnergyWeapons", "AfterFireTrail", "VolumetricSmoke" };
		UInt32 bits = 0;
		for (UInt32 i = 0; i < 9; ++i)
			if (TheSettingManager->GetSettingI("Main.GunFX.Main", keys[i])) bits |= 1u << i;
		set(bits);
	}
}

namespace BarrelHeat {
	typedef bool (__cdecl* GetHeatFn)(float out[10], const void* weaponNode);
	static GetHeatFn GetHeat = nullptr;
	static bool Active = false;
	static bool ProbeFrame = false;
	static float ProbeHeat[10] = {};
	static bool ProbeBridge = false;
	static unsigned ProbeDraws = 0, ProbeUploads = 0, ProbeSamples = 0;
	static ULONGLONG LastProbe = 0, LastProbeHeat = 0;

	static void Clear() {
		if (!Active) return;
		const float off[12] = {};
		TheRenderManager->device->SetVertexShaderConstantF(171, off, 3);
		TheRenderManager->device->SetPixelShaderConstantF(171, off, 3);
		Active = false;
	}

	static bool ToGunLocal(NiGeometry* geometry, float state[10], const char*& failure) {
		// Rigid weapon vertices are in this geometry's local space in ObjectTemplate.
		// Put the live muzzle there too, without consulting either camera projection.
		if (geometry->skinInstance || fabsf(geometry->m_worldTransform.scale) < 0.001f) { failure = "skin or scale"; return false; }
		const NiTransform& transform = geometry->m_worldTransform;
		auto toLocal = [&](const D3DXVECTOR3& point) {
			const float dx = point.x - transform.pos.x, dy = point.y - transform.pos.y, dz = point.z - transform.pos.z;
			const float inverseScale = 1.0f / transform.scale;
			return D3DXVECTOR3(
				(transform.rot.data[0][0] * dx + transform.rot.data[1][0] * dy + transform.rot.data[2][0] * dz) * inverseScale,
				(transform.rot.data[0][1] * dx + transform.rot.data[1][1] * dy + transform.rot.data[2][1] * dz) * inverseScale,
				(transform.rot.data[0][2] * dx + transform.rot.data[1][2] * dy + transform.rot.data[2][2] * dz) * inverseScale);
		};
		const D3DXVECTOR3 muzzle = toLocal(D3DXVECTOR3(state[0], state[1], state[2]));
		const D3DXVECTOR3 back = toLocal(D3DXVECTOR3(state[0] + state[4] * state[7], state[1] + state[5] * state[7],
			state[2] + state[6] * state[7]));
		D3DXVECTOR3 axis = back - muzzle;
		const float length = D3DXVec3Length(&axis);
		if (!std::isfinite(muzzle.x) || !std::isfinite(muzzle.y) || !std::isfinite(muzzle.z) ||
			!std::isfinite(length) || length < 1.0f || length > 100.0f) { failure = "local transform"; return false; }
		state[0] = muzzle.x; state[1] = muzzle.y; state[2] = muzzle.z;
		state[4] = axis.x / length; state[5] = axis.y / length; state[6] = axis.z / length;
		state[7] = length;
		return true;
	}

	// Temporary v40 diagnostics: sample one third-person frame per second.
	static void BeginWorldProbe() {
		ProbeFrame = false;
		if (!Player || !Player->isThirdPerson || ProbeSamples >= 120) return;
		const ULONGLONG now = GetTickCount64();
		if (now - LastProbe < 1000) return;
		LastProbe = now;
		if (!GetHeat) {
			HMODULE smoke = GetModuleHandleA("GunFX.dll");
			if (smoke) GetHeat = (GetHeatFn)GetProcAddress(smoke, "GunFX_GetHeatV3");
		}
		NiNode* weapon = Player->ActorSkinInfo ? Player->ActorSkinInfo->WeaponNode : nullptr;
		memset(ProbeHeat, 0, sizeof(ProbeHeat));
		ProbeBridge = weapon && GetHeat && GetHeat(ProbeHeat, weapon);
		if (ProbeBridge && ProbeHeat[3] > 0.01f) LastProbeHeat = now;
		if (!LastProbeHeat || now - LastProbeHeat > 15000) return;
		ProbeFrame = true;
		ProbeDraws = ProbeUploads = 0;
		Logger::Log("BarrelHeat probe BEGIN %u: bridge %s, heat %.3f, weapon %p", ++ProbeSamples,
			ProbeBridge ? "yes" : "no", ProbeHeat[3], weapon);
	}

	static void ProbeDraw(NiGeometry* geometry, NiD3DVertexShaderEx* vs, NiD3DPixelShaderEx* ps) {
		if (!ProbeFrame || !geometry || !Player->ActorSkinInfo) return;
		NiNode* weapon = Player->ActorSkinInfo->WeaponNode;
		NiAVObject* node = geometry;
		for (int depth = 0; node && node != weapon && depth < 32; ++depth) node = node->m_parent;
		if (!weapon || node != weapon) return;
		if (++ProbeDraws > 64) return;
		float local[10];
		memcpy(local, ProbeHeat, sizeof(local));
		const char* failure = ProbeBridge ? "none" : "no bridge";
		const bool aligned = ProbeBridge && ToGunLocal(geometry, local, failure);
		NiBound bound = {};
		if (geometry->geomData) bound = geometry->geomData->m_kBound;
		float ambient[4] = {};
		DWORD blending = 0, source = 0, destination = 0;
		auto device = TheRenderManager->device;
		device->GetPixelShaderConstantF(1, ambient, 1);
		device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blending);
		device->GetRenderState(D3DRS_SRCBLEND, &source);
		device->GetRenderState(D3DRS_DESTBLEND, &destination);
		IDirect3DVertexShader9* boundVS = nullptr;
		IDirect3DPixelShader9* boundPS = nullptr;
		device->GetVertexShader(&boundVS);
		device->GetPixelShader(&boundPS);
		Logger::Log("BarrelHeat probe surface %.48s: VS %s NVR=%d bound=%d, PS %s NVR=%d bound=%d; local=%s (%s) muzzle (%.2f %.2f %.2f) axis (%.3f %.3f %.3f) bound (%.2f %.2f %.2f r%.2f) scale %.3f alpha %.3f blend %u/%u/%u",
			geometry->m_pcName ? geometry->m_pcName : "(unnamed)", vs && vs->Name ? vs->Name : "none",
			vs && vs->ShaderHandleBackup && vs->ShaderHandle != vs->ShaderHandleBackup, vs && boundVS == vs->ShaderHandle,
			ps && ps->Name ? ps->Name : "none", ps && ps->ShaderHandleBackup && ps->ShaderHandle != ps->ShaderHandleBackup,
			ps && boundPS == ps->ShaderHandle, aligned ? "yes" : "no", failure,
			local[0], local[1], local[2], local[4], local[5], local[6], bound.Center.x, bound.Center.y, bound.Center.z,
			bound.Radius, geometry->m_worldTransform.scale, ambient[3], blending, source, destination);
		if (boundVS) boundVS->Release();
		if (boundPS) boundPS->Release();
	}

	static void EndWorldProbe() {
		if (ProbeFrame) Logger::Log("BarrelHeat probe END: gun draws %u, heat uploads %u", ProbeDraws, ProbeUploads);
		ProbeFrame = false;
	}

	static void SetForDraw(NiGeometry* geometry, NiD3DVertexShaderEx* vertexShader, NiD3DPixelShaderEx* pixelShader) {
		const bool firstPerson = ShaderSplit::CurrentContext == ShaderSplit::FirstPerson;
		if (!firstPerson && ShaderSplit::CurrentContext != ShaderSplit::World) return;
		if (!firstPerson) ProbeDraw(geometry, vertexShader, pixelShader);
		if (!pixelShader || !pixelShader->Name || !pixelShader->ShaderHandleBackup ||
			pixelShader->ShaderHandle == pixelShader->ShaderHandleBackup) return;
		const char* name = pixelShader->Name;
		if (strncmp(name, "SLS20", 5) || strlen(name) != 11 || strcmp(name + 7, ".pso") ||
			(name[5] - '0') * 10 + name[6] - '0' > 56) return;
		// Indoor lighting can use SLS2037-2044 for the only opaque draw.
		// Check the actual blend state instead of guessing the pass from its shader name.
		// Blended light/specular redraws must not apply the glow again.
		if (!firstPerson) {
			DWORD alphaBlend = FALSE;
			if (FAILED(TheRenderManager->device->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphaBlend)) || alphaBlend) return;
		}
		// The local coordinates used by the heat pixel shader come from its matching
		// ObjectTemplate vertex shader. Shadow's interpolated sentinel is unrelated.
		const char* vertexName = vertexShader ? vertexShader->Name : nullptr;
		const bool paired = vertexName && vertexShader->ShaderHandleBackup &&
			vertexShader->ShaderHandle != vertexShader->ShaderHandleBackup &&
			!strncmp(vertexName, "SLS20", 5) && strlen(vertexName) == 11 && !strcmp(vertexName + 7, ".vso") &&
			(vertexName[5] - '0') * 10 + vertexName[6] - '0' <= 56;

		float state[12] = {};
		NiNode* weaponNode = nullptr;
		if (Player) {
			SkinInfo* skin = firstPerson ? Player->firstPersonSkinInfo : Player->ActorSkinInfo;
			if (skin) weaponNode = skin->WeaponNode;
		}
		if (geometry && weaponNode && paired) {
			NiAVObject* node = geometry;
			for (int depth = 0; node && depth < 32; ++depth, node = node->m_parent) {
				if (node == weaponNode) {
					if (!GetHeat) {
						HMODULE smoke = GetModuleHandleA("GunFX.dll");
						if (smoke) GetHeat = (GetHeatFn)GetProcAddress(smoke, "GunFX_GetHeatV3");
					}
					if (GetHeat && GetHeat(state, weaponNode)) {
						BarrelHaze::CaptureWorld(state, firstPerson, weaponNode);
						const char* failure = "none";
						const bool aligned = ToGunLocal(geometry, state, failure);
						if (!aligned) state[3] = 0.0f;
						else {
							const float scale = fabsf(geometry->m_worldTransform.scale);
							state[8] /= scale;
							state[9] /= scale;
							static unsigned hazeLogged[2] = {};
							static ULONGLONG hazeLoggedAt[2] = {};
							const unsigned hazeView = firstPerson ? 0 : 1;
							const ULONGLONG hazeNow = GetTickCount64();
							if (BarrelHaze::Captured == hazeNow && hazeLogged[hazeView] < 8 && hazeNow - hazeLoggedAt[hazeView] > 1000) {
								hazeLogged[hazeView]++;
								hazeLoggedAt[hazeView] = hazeNow;
								Logger::Log("BarrelHaze %s: UV (%.3f %.3f) -> (%.3f %.3f), view depth %.1f, heat %.2f, push %.2f px, plume %.0fx%.0f px%s",
									firstPerson ? "first-person" : "third-person", BarrelHaze::Line.x, BarrelHaze::Line.y,
									BarrelHaze::Line.z, BarrelHaze::Line.w, BarrelHaze::Animation.w, BarrelHaze::Data.x,
									BarrelHaze::Data.y, BarrelHaze::Data.z, BarrelHaze::Data.w,
									firstPerson ? " (first-person field of view)" : "");
							}
						}
						static bool logged[2] = {};
						const unsigned context = ShaderSplit::CurrentContext == ShaderSplit::FirstPerson ? 0 : 1;
						if (!logged[context] && (state[3] > 0.25f || !aligned)) {
							logged[context] = true;
							Logger::Log("BarrelHeat: %s weapon surface %s, heat %.2f, local alignment %s (%s), skinned %s", context ? "third-person" : "first-person",
								geometry->m_pcName ? geometry->m_pcName : "(unnamed)", state[3], aligned ? "yes" : "fallback", failure, geometry->skinInstance ? "yes" : "no");
						}
					}
					break;
				}
			}
		}
		if (state[3] <= 0.0f) return;
		TheRenderManager->device->SetVertexShaderConstantF(171, state, 3);
		TheRenderManager->device->SetPixelShaderConstantF(171, state, 3);
		Active = true;
		if (!firstPerson && ProbeFrame) ++ProbeUploads;
	}
}

void (__thiscall* SetShaders)(BSShader*, UInt32) = (void (__thiscall*)(BSShader*, UInt32))Hooks::SetShaders;
void __fastcall SetShadersHook(BSShader* This, UInt32 edx, UInt32 PassIndex) {
	BarrelHeat::Clear();
	const bool profiling = ShaderSplit::FrameProfiled;
	const double bindStart = profiling ? CpuTimer::NowMs() : 0.0;
	NiGeometry* Geometry = *(NiGeometry**)(*(void**)0x011F91E0);
	NiD3DPass* Pass = *(NiD3DPass**)0x0126F74C;
	NiD3DVertexShaderEx* VertexShader = (NiD3DVertexShaderEx*)Pass->VertexShader;
	NiD3DPixelShaderEx* PixelShader = (NiD3DPixelShaderEx*)Pass->PixelShader;
	IDirect3DVertexShader9* VertexShader2 = TheRenderManager->renderState->GetVertexShader();
	IDirect3DPixelShader9* PixelShader2 = TheRenderManager->renderState->GetPixelShader();

	if (VertexShader) {
		VertexShader->SetupShader(VertexShader2);
	}
	else {
		Logger::Log("Error getting vertex shader for pass %s", Pointers::Functions::GetPassDescription(PassIndex));
	}
	if (PixelShader) {
		PixelShader->SetupShader(ForcePixelConstants ? nullptr : PixelShader2);
		ForcePixelConstants = false;
	}
	else {
		Logger::Log("Error getting pixel shader for pass %s", Pointers::Functions::GetPassDescription(PassIndex));
	}
	// Diagnostic (P57): the water reflection with the game's own shaders. NVR's replacements take constants made for
	// the main camera; the reflection camera sits mirrored below the water. SetupShader picks the handle the game
	// binds next, so switching it here keeps the game's state tracking right; the next bind picks NVR's again.
	if (TheSettingManager->SettingsMain.Main.GameShadersInReflections && ShaderSplit::CurrentContext == ShaderSplit::Reflections) {
		if (VertexShader && VertexShader->ShaderHandleBackup) VertexShader->ShaderHandle = (IDirect3DVertexShader9*)VertexShader->ShaderHandleBackup;
		if (PixelShader && PixelShader->ShaderHandleBackup) PixelShader->ShaderHandle = (IDirect3DPixelShader9*)PixelShader->ShaderHandleBackup;
	}
	// UNOFFICIAL lit particles (Shaders.Particles): the NOLIGHT replacements are shader model 3, and D3D9 cannot pair a
	// 3.0 shader with the game's 2.x one. Where only one side of a NOLIGHT draw has a replacement -- NOLIGHTTEXVC.pso
	// behind the game's glow/muzzle-flash vertex shaders, or NOLIGHT016/017.vso in front of the flame/spark pixel
	// shader -- the draw gets the game's own pair. Skipped entirely while the collection is off.
	if (VertexShader && PixelShader && TheShaderManager->Shaders.Particles && TheShaderManager->Shaders.Particles->Enabled) {
		const bool nvrVertex = VertexShader->ShaderHandleBackup && VertexShader->ShaderHandle != VertexShader->ShaderHandleBackup;
		const bool nvrPixel = PixelShader->ShaderHandleBackup && PixelShader->ShaderHandle != PixelShader->ShaderHandleBackup;
		auto particleFamily = [](const char* name) { return name && (!strncmp(name, "NOLIGHT", 7) || !strncmp(name, "GDECAL", 6)); };
		const bool family = particleFamily(VertexShader->Name) || particleFamily(PixelShader->Name);
		// The first-person pass (the player's own muzzle flash, Pip-Boy glow) always gets the game's pair.
		if (family && ShaderSplit::CurrentContext == ShaderSplit::FirstPerson) {
			if (nvrVertex) VertexShader->ShaderHandle = (IDirect3DVertexShader9*)VertexShader->ShaderHandleBackup;
			if (nvrPixel) PixelShader->ShaderHandle = (IDirect3DPixelShader9*)PixelShader->ShaderHandleBackup;
		}
		else if (nvrVertex != nvrPixel && family) {
			if (nvrVertex) VertexShader->ShaderHandle = (IDirect3DVertexShader9*)VertexShader->ShaderHandleBackup;
			else PixelShader->ShaderHandle = (IDirect3DPixelShader9*)PixelShader->ShaderHandleBackup;
		}
	}
	ShaderSplit::OnBind(PixelShader, PixelShader2, bindStart);
	ReflectionProbe::OnBind(Geometry, PixelShader ? PixelShader->Name : nullptr);

	// trace pipeline active shaders
	if (TheSettingManager->SettingsMain.Develop.DebugMode && !InterfaceManager->IsActive(Menu::MenuType::kMenuType_Console) && Global->OnKeyDown(TheSettingManager->SettingsMain.Develop.TraceShaders)) {
		char Name[256];
		sprintf(Name, "Pass %i %s, %s (%s %s)", PassIndex, Pointers::Functions::GetPassDescription(PassIndex), Geometry->m_pcName, VertexShader->Name, PixelShader->Name);
		if (VertexShader->ShaderHandle == VertexShader->ShaderHandleBackup) strcat(Name, " - Vertex: vanilla");
		if (PixelShader->ShaderHandle == PixelShader->ShaderHandleBackup) strcat(Name, " - Pixel: vanilla");
		Logger::Log("%s", Name);
		InterfaceManager->ShowMessage("Shaders Traced");
		//DWNode::AddNode(Name, Geometry->m_parent, Geometry);
	}
	(*SetShaders)(This, PassIndex);
	BarrelHeat::SetForDraw(Geometry, VertexShader, PixelShader);
	if (profiling) ShaderSplit::EndBind(bindStart);

}

HRESULT (__thiscall* SetSamplerState)(NiDX9RenderState*, UInt32, D3DSAMPLERSTATETYPE, UInt32, UInt8) = (HRESULT (__thiscall*)(NiDX9RenderState*, UInt32, D3DSAMPLERSTATETYPE, UInt32, UInt8))Hooks::SetSamplerState;
HRESULT __fastcall SetSamplerStateHook(NiDX9RenderState* This, UInt32 edx, UInt32 Sampler, D3DSAMPLERSTATETYPE Type, UInt32 Value, UInt8 Save) {

	UInt16* TypeMap = (UInt16*)0x126F92C;
	HRESULT r = D3D_OK;

	if (TypeMap[Type] < 5)
		r = (*SetSamplerState)(This, Sampler, Type, Value, Save);
	else
		r = TheRenderManager->device->SetSamplerState(Sampler, Type, Value);
	return r;

}

void (__thiscall* RenderWorldSceneGraph)(Main*, Sun*, UInt8, UInt8, UInt8) = (void (__thiscall*)(Main*, Sun*, UInt8, UInt8, UInt8))Hooks::RenderWorldSceneGraph;
void __fastcall RenderWorldSceneGraphHook(Main* This, UInt32 edx, Sun* SkySun, UInt8 IsFirstPerson, UInt8 WireFrame, UInt8 Arg4) {
	EndPreSceneTimer();
	WorldRenderedThisFrame = true;
	if (!FrameWorldArgsKnown) { FrameWorldArgsKnown = true; FrameWorldArgs[0] = IsFirstPerson; FrameWorldArgs[1] = WireFrame; FrameWorldArgs[2] = Arg4; }
	FrameWorldCalls++;
	{
		// Game geometry drawn with NVR's replacement shaders, including per-object sun shadows.
		static GpuTimer worldTimer("World scene (game)");
		GpuProfileScope gpu(worldTimer, TheRenderManager->device);
		ShaderSplit::BeginContext(ShaderSplit::World);
		GunFXSwitches::Update();
		BarrelHaze::Reset();
		BarrelHeat::BeginWorldProbe();
		(*RenderWorldSceneGraph)(This, SkySun, IsFirstPerson, WireFrame, Arg4);
		BarrelHeat::EndWorldProbe();
		BarrelHeat::Clear();
		ShaderSplit::EndContext();
	}

	// Re-light nearby statics inside the flashlight cone. This has to happen here, before
	// the viewmodel depth handling below clears the Z buffer: the pass draws with depth
	// testing on and depth writes off, so with a cleared Z buffer it would draw straight
	// through world geometry.
	MaterialPass::CaptureScene(WorldSceneGraph);
	MaterialPass::RenderWorld();

	const bool bPipBoyOpen = InterfaceManager->IsPipBoyOpen();
	const bool bPipBoyLive = (TheGameMenuManager->IsLiveMenu && TheGameMenuManager->IsLiveMenu(Menu::kMenuType_BigFour, false, false) == GameMenuManager::MenuPauseState::MENU_LIVE);

	static GpuTimer depthResolveTimer("Depth resolves");
	GpuProfileScope gpuResolve(depthResolveTimer, TheRenderManager->device);
	if (!bPipBoyOpen || bPipBoyLive)
		TheRenderManager->ResolveDepthBuffer(TheTextureManager->DepthTexture); // disable updating the world buffer when pipboy is out

	if (!IsFirstPerson) {
		// clear the viewmodel depth buffer
		TheRenderManager->Clear(NULL, NiRenderer::kClear_ZBUFFER);
		TheRenderManager->ResolveDepthBuffer(TheTextureManager->DepthTextureViewModel);
	}
}

void (__thiscall* RenderFirstPerson)(Main*, NiDX9Renderer*, NiGeometry*, Sun*, BSRenderedTexture*) = (void (__thiscall*)(Main*, NiDX9Renderer*, NiGeometry*, Sun*, BSRenderedTexture*))Hooks::RenderFirstPerson;
void __fastcall RenderFirstPersonHook(Main* This, UInt32 edx, NiDX9Renderer* Renderer, NiGeometry* Geo, Sun* SkySun, BSRenderedTexture* RenderedTexture) {
	// Clear the depth buffer before rendering first person model to prevent clipping with world objects & other artefacts
	static GpuTimer firstPersonTimer("First person (game)");
	GpuProfileScope gpu(firstPersonTimer, TheRenderManager->device);
	FrameFirstPersonCalls++;
	TheRenderManager->Clear(NULL, NiRenderer::kClear_ZBUFFER);
	//ThisCall(0x00874C10, Global);
	ShaderSplit::BeginContext(ShaderSplit::FirstPerson);
	(*RenderFirstPerson)(This, Renderer, Geo, SkySun, RenderedTexture);
	BarrelHeat::Clear();
	ShaderSplit::EndContext();
	TheRenderManager->ResolveDepthBuffer(TheTextureManager->DepthTextureViewModel);
}

void (__thiscall* RenderReflections)(WaterManager*, NiCamera*, ShadowSceneNode*) = (void (__thiscall*)(WaterManager*, NiCamera*, ShadowSceneNode*))Hooks::RenderReflections;
void __fastcall RenderReflectionsHook(WaterManager* This, UInt32 edx, NiCamera* Camera, ShadowSceneNode* SceneNode) {
	ReflectionProbe::HookCamera = Camera;
	if (!TheSettingManager->SettingsMain.Main.ForceReflections) {
		// Hooked for profiling in this mode: the game's reflection pass, unchanged unless CheapReflections is on.
		static GpuTimer reflectionsTimer("Water reflections (game)");
		static CpuTimer reflectionsCpuTimer("Water reflections (CPU)");
		CpuProfileScope cpu(reflectionsCpuTimer);
		GpuProfileScope gpu(reflectionsTimer, TheRenderManager->device);
		CheapReflectionScope cheap;
		UnderwaterTerrainReflectionScope underwaterTerrain;
		ShaderSplit::BeginContext(ShaderSplit::Reflections);
		(*RenderReflections)(This, Camera, SceneNode);
		ShaderSplit::EndContext();
		return;
	}
	
	D3DXVECTOR4* ShadowData = &TheShaderManager->Effects.ShadowsExteriors->Constants.Data;
	float ShadowDataBackup = ShadowData->x;

	D3DXVECTOR4* TerrainParallaxData = &TheShaderManager->Shaders.Terrain->ParallaxConstants.Data;
	float TerrainParallaxBackup = TerrainParallaxData->x;

	if (DWNode::Get()) DWNode::AddNode("BEGIN REFLECTIONS RENDERING", NULL, NULL);
	ShadowData->x = -1.0f; // Disables the shadows rendering for water reflections (the geo is rendered with the same shaders used in the normal scene!)
	TerrainParallaxData->x = 0;
	{
		// ForceReflections renders the full world again into the water reflection map.
		static GpuTimer reflectionsTimer("Water reflections (game)");
		static CpuTimer reflectionsCpuTimer("Water reflections (CPU)");
		CpuProfileScope cpu(reflectionsCpuTimer);
		GpuProfileScope gpu(reflectionsTimer, TheRenderManager->device);
		CheapReflectionScope cheap;
		UnderwaterTerrainReflectionScope underwaterTerrain;
		ShaderSplit::BeginContext(ShaderSplit::Reflections);
		(*RenderReflections)(This, Camera, SceneNode);
		ShaderSplit::EndContext();
	}
	ShadowData->x = ShadowDataBackup;
	TerrainParallaxData->x = TerrainParallaxBackup;
	if (DWNode::Get()) DWNode::AddNode("END REFLECTIONS RENDERING", NULL, NULL);
}

void (__thiscall* RenderPipboy)(Main*, NiGeometry*, NiDX9Renderer*) = (void (__thiscall*)(Main*, NiGeometry*, NiDX9Renderer*))Hooks::RenderPipboy;
void __fastcall RenderPipboyHook(Main* This, UInt32 edx, NiGeometry* Geo, NiDX9Renderer* Renderer) {
	WorldSceneGraph->UpdateParticleShaderFoV(Player->firstPersonFoV);
//	Player->SetFoV(Player->firstPersonFoV);
	(*RenderPipboy)(This, Geo, Renderer);
}

float (__thiscall* GetWaterHeightLOD)(TESWorldSpace*) = (float (__thiscall*)(TESWorldSpace*))Hooks::GetWaterHeightLOD;
float __fastcall GetWaterHeightLODHook(TESWorldSpace* This, UInt32 edx) {
	
	float r = This->waterHeight;
	if (*(void**)This == (void*)0x0103195C) r = TheShaderManager->Shaders.Water->Constants.Default.waterSettings.x;
	return r;

}

bool bSkippedRender_RenderedMenu = false;
bool bDoneRender_LockPickMenu = false;

void(__cdecl* ProcessImageSpaceShaders)(NiDX9Renderer*, BSRenderedTexture*, BSRenderedTexture*) = (void(__cdecl*)(NiDX9Renderer*, BSRenderedTexture*, BSRenderedTexture*))Hooks::ProcessImageSpaceShaders;
void __cdecl ProcessImageSpaceShadersHook(NiDX9Renderer* Renderer, BSRenderedTexture* SourceTarget, BSRenderedTexture* DestinationTarget) {
	bool bLiveRenderedMenu = false; // FORenderedMenu, FOPipBoyManager
	bool bLive3DMenu = false; // Normal menus, but 3D, lockpick etc
	if (TESMain::IsMenuBackgroundReady() && TheGameMenuManager->IsLiveMenu && InterfaceManager->currentMode != 1) {
		const bool bLockPickMenu = LockPickMenu::GetSingleton() && TheGameMenuManager->IsLiveMenu(Menu::kMenuType_LockPick, false, false) == GameMenuManager::MENU_LIVE;
		const bool bPipBoyLive = InterfaceManager->IsPipBoyOpen() && TheGameMenuManager->IsLiveMenu(Menu::kMenuType_BigFour, false, false) == GameMenuManager::MenuPauseState::MENU_LIVE;
		const bool bRenderedMenuLive = InterfaceManager->pRenderedMenu && TheGameMenuManager->IsLiveMenu(InterfaceManager->menuStack[0], false, false) == GameMenuManager::MenuPauseState::MENU_LIVE;
		bLiveRenderedMenu = bPipBoyLive || bRenderedMenuLive;
		bLive3DMenu = bLockPickMenu;
	}

	if (bLive3DMenu) {
		if (bDoneRender_LockPickMenu) {
			bDoneRender_LockPickMenu = false;
			ProcessImageSpaceShaders(Renderer, SourceTarget, DestinationTarget);
			return;
		}
		else {
			bDoneRender_LockPickMenu = true;
		}
	}
	else {
		bDoneRender_LockPickMenu = false;
	}
	
	if (bLiveRenderedMenu) {
		if (!bSkippedRender_RenderedMenu) {
			bSkippedRender_RenderedMenu = true;
			ProcessImageSpaceShaders(Renderer, SourceTarget, DestinationTarget);
			return;
		}
		else {
			bSkippedRender_RenderedMenu = false;
		}
	}
	else {
		bSkippedRender_RenderedMenu = false;
	}

	FrameImageSpaceCalls++;
	if (WorldSceneGuardActive()) {
		// No world scene has been rendered for several frames, so the depth buffers NVR's effects need are
		// stale and they would darken the image. Let the game's own image space run on its own.
		static bool announced = false;
		if (!announced) { Logger::Log("UNOFFICIAL world scene guard: no world scene render for %u frames, NVR effects skipped until it returns.", WorldMissStreak); announced = true; }
		ProcessImageSpaceShaders(Renderer, SourceTarget, DestinationTarget);
		return;
	}

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* GameSurface = NULL;
	IDirect3DSurface9* OutputSurface = NULL;
	
	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();
	TheShaderManager->UpdateConstants();

	if (SourceTarget && TheSettingManager->SettingsMain.Main.RenderPreTonemapping) {
		SourceTarget->GetD3DTexture(0)->GetSurfaceLevel(0, &GameSurface); // get the surface from the game render target

		// Disable render state settings that create artefacts
		RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILMASK, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILWRITEMASK, 255, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILREF, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_COLORWRITEENABLE, 15, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_NORMALIZENORMALS, D3DZB_FALSE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_POINTSIZE, 810365505, RenderStateArgs); // fix flickering linked to alpha somehow

		TheShaderManager->RenderEffectsPreTonemapping(GameSurface);
	
	}

	{
		static GpuTimer imageSpaceTimer("Game image space");
		GpuProfileScope gpu(imageSpaceTimer, Device);
		ProcessImageSpaceShaders(Renderer, SourceTarget, DestinationTarget);
	}

	if (!DestinationTarget && TheRenderManager->currentRTGroup) {
		OutputSurface = TheRenderManager->currentRTGroup->RenderTargets[0]->data->Surface;
		if (!TheSettingManager->SettingsMain.Main.RenderPreTonemapping) TheShaderManager->RenderEffectsPreTonemapping(OutputSurface);
		TheShaderManager->RenderEffects(OutputSurface);
		TheRenderManager->CheckAndTakeScreenShot(OutputSurface, TheSettingManager->SettingsMain.Main.HDRScreenshot);
		ReflectionProbe::EndFrame();
	}

	if (GameSurface) GameSurface->Release();
}

static void RenderMainMenuMovie() {

	if (TheSettingManager->SettingsMain.Main.ReplaceIntro && InterfaceManager->IsActive(Menu::MenuType::kMenuType_Main))
		TheBinkManager->Render(MainMenuMovie);
	else
		TheBinkManager->Close();

}

CallDetour kRenderInterfaceDetour;
void __fastcall RenderInterfaceHook(void* apThis, void*, void* apCuller, bool abPipboyVisible) {
	RenderMainMenuMovie();
	ImGuiManager::NewFrame();
	ThisCall(kRenderInterfaceDetour.GetOverwrittenAddr(), apThis, apCuller, abPipboyVisible);
	ImGuiManager::Render();
}

static void SetTileShaderConstants() {
	
	float ViewProj[16];
	NiVector4 TintColor = { 1.0f, 1.0f, 1.0f, 0.0f };

	if (InterfaceManager->IsActive(Menu::MenuType::kMenuType_Main)) {
		TheRenderManager->device->GetVertexShaderConstantF(0, ViewProj, 4);
		if ((int)ViewProj[3] == -1 && (int)ViewProj[7] == 1 && (int)ViewProj[15] == 1) TheRenderManager->device->SetPixelShaderConstantF(0, (const float*)&TintColor, 1);
	}

}

__declspec(naked) void SetTileShaderConstantsHook() {

	__asm {
		pushad
		call	SetTileShaderConstants
		popad
		cmp		byte ptr [esi + 0xAC], 0
		jmp		Jumpers::SetTileShaderConstants::Return
	}

}

void* (__thiscall* ShowDetectorWindow)(DetectorWindow*, HWND, HINSTANCE, NiNode*, char*, int, int, int, int) = (void* (__thiscall*)(DetectorWindow*, HWND, HINSTANCE, NiNode*, char*, int, int, int, int))::Hooks::ShowDetectorWindow;
void* __fastcall ShowDetectorWindowHook(DetectorWindow* This, UInt32 edx, HWND Handle, HINSTANCE Instance, NiNode* RootNode, char* FormCaption, int X, int Y, int Width, int Height) {
	
	NiAVObject* Object = NULL;
	void* r = NULL;

	r = (ShowDetectorWindow)(This, Handle, Instance, RootNode, (char*)"Pipeline detector by Alenet", X, Y, 1280, 1024);
	for (int i = 0; i < RootNode->m_children.end; i++) {
		NiNode* Node = (NiNode*)RootNode->m_children.data[i];
		Node->m_children.data[0] = NULL;
		Node->m_children.data[1] = NULL;
		Node->m_children.end = 0;
		Node->m_children.numObjs = 0;
	}
	return r;

}

void DetectorWindowSetNodeName(char* Buffer, int Size, char* Format, char* ClassName, char* Name, float LPosX, float LPosY, float LPosZ) {

	sprintf(Buffer, "%s", Name);

}

static void DetectorWindowCreateTreeView(HWND TreeView) {

	HFONT Font = CreateFontA(14, 0, 0, 0, FW_DONTCARE, NULL, NULL, NULL, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
	SendMessageA(TreeView, WM_SETFONT, (WPARAM)Font, TRUE);
	SendMessageA(TreeView, TVM_SETBKCOLOR, NULL, 0x001E1E1E);
	SendMessageA(TreeView, TVM_SETTEXTCOLOR, NULL, 0x00DCDCDC);

}

__declspec(naked) void DetectorWindowCreateTreeViewHook() {

	__asm {
		pushad
		push	eax
		call	DetectorWindowCreateTreeView
		pop		eax
		popad
		mov     ecx, [ebp - 0x48]
		mov		[ecx + 0x0C], eax
		mov     esp, ebp
		pop     ebp
		jmp		Jumpers::DetectorWindow::CreateTreeViewReturn
	}

}

void DetectorWindowDumpAttributes(HWND TreeView, UInt32 Msg, WPARAM wParam, LPTVINSERTSTRUCTA lParam) {

	TVITEMEXA Item = { NULL };
	char T[260] = { '\0' };

	Item.pszText = T;
	Item.mask = TVIF_TEXT;
	Item.hItem = (HTREEITEM)SendMessageA(TreeView, TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)lParam->hParent);
	Item.cchTextMax = 260;
	SendMessageA(TreeView, TVM_GETITEMA, 0, (LPARAM)&Item);
	if (!memcmp(Item.pszText, "Pass", 4))
		SendMessageA(TreeView, TVM_DELETEITEM, 0, (LPARAM)lParam->hParent);
	else
		if (strlen(Item.pszText)) SendMessageA(TreeView, Msg, wParam, (LPARAM)lParam);

}

__declspec(naked) void DetectorWindowDumpAttributesHook() {

	__asm {
		call	DetectorWindowDumpAttributes
		add		esp, 16
		jmp		Jumpers::DetectorWindow::DumpAttributesReturn
	}

}

__declspec(naked) void DetectorWindowConsoleCommandHook() {

	__asm {
		call	DWNode::Create
		jmp		Jumpers::DetectorWindow::ConsoleCommandReturn
	}

}

// Enables culling of muzzle flashes so they don't stay after firing
void __fastcall MuzzleLightCullingFix(MuzzleFlash* This) {
	if (This->light) {
		if (!This->bEnabled) {
			This->light->m_flags |= 1;
		}
		else {
			This->light->m_flags &= ~1;
		}
	}
	ThisCall(0x9BB8A0, This);
}

BSFogProperty* __cdecl ShadowSceneNode__GetFogPropertyEx(UInt32 aeType) {
	if (TheShaderManager->Effects.VolumetricFog->Enabled && !TheShaderManager->GameState.isUnderwater) {
		aeType = 1;  // Use UI scene node and thus render no fog.
	}

	return BSShaderManager::GetShadowSceneNode(static_cast<BSShaderManager::SceneGraphType>(aeType))->fogProperty;
}

NiPoint2* __fastcall WaterFogRemover(NiPoint2* point, void*, float x, float y)
{
	point->x = 0.f;
	point->y = 0.f;
	return point;
}

// Compatibility patch for DXVK 16bits buffer upgrade.
typedef bool(__cdecl* DisableFormatUpgradeFunc)();
typedef bool(__cdecl* EnableFormatUpgradeFunc)();

BSRenderedTexture* (__cdecl* CreateBSRenderedTexture)(BSString*, const UInt32, const UInt32, NiTexture::FormatPrefs*, UInt32, bool, NiDepthStencilBuffer*, UInt32, UInt32) = (BSRenderedTexture * (__cdecl*)(BSString*, const UInt32, const UInt32, NiTexture::FormatPrefs*, UInt32, bool, NiDepthStencilBuffer*, UInt32, UInt32))Hooks::CreateRenderedTexture;
BSRenderedTexture* __cdecl CreateSaveTextureHook(BSString* apName, const UInt32 uiWidth, const UInt32 uiHeight, NiTexture::FormatPrefs* kPrefs, 
	UInt32 eMSAAPref, bool bUseDepthStencil, NiDepthStencilBuffer* pkDSBuffer, UInt32 a7, UInt32 uiBackgroundColor) {
	HMODULE hDLL = GetModuleHandle(L"d3d9.dll");

	// If the loaded library is DXVK-HDR (https://github.com/EndlesslyFlowering/dxvk), these will pass
	DisableFormatUpgradeFunc disable = (DisableFormatUpgradeFunc)GetProcAddress(hDLL, "DXVK_D3D9_HDR_DisableRenderTargetUpgrade");
	EnableFormatUpgradeFunc enable = (DisableFormatUpgradeFunc)GetProcAddress(hDLL, "DXVK_D3D9_HDR_EnableRenderTargetUpgrade");

	if (disable)
		disable(); // Temporarily disable the format upgrade for the texture
	BSRenderedTexture* pTexture = CreateBSRenderedTexture(apName, uiWidth, uiHeight, kPrefs, eMSAAPref, bUseDepthStencil, pkDSBuffer, a7, uiBackgroundColor);
	if (enable)
		enable(); // Restore the format upgrade functionality 

	return pTexture;
}

// 0xE69660
bool __fastcall NiDX9Renderer__Do_EndFrame(NiDX9Renderer* apThis, void*) {
	bool bResult = ThisStdCall<bool>(0xE69660, apThis);

	// Reload effects if queued.
	if (TheShaderManager && TheShaderManager->EffectReloadQueued) {
		TheShaderManager->ReloadEffects();
		TheShaderManager->EffectReloadQueued = false;
	}

	return bResult;
}


// Code to increase all lights strength
//__forceinline NiVector4* GetConstant(int index) {
//	return &((NiVector4*)0x11FA0C0)[index];
//}
//
//__forceinline NiColorAlpha* GetLightConstant(int index) {
//	return reinterpret_cast<NiColorAlpha*>(GetConstant(index));
//}
//
//__forceinline void ScaleColor(NiColorAlpha* Color, float scale) {
//	Color->r *= scale;
//	Color->g *= scale;
//	Color->b *= scale;
//}
//
//void __fastcall ShadowLightShader__UpdateLights(void* apThis, void*, void* apShaderProp, void* apRenderPass, D3DXMATRIX aMatrix, void* apTransform, UInt32 aeRenderPassType, void* apSkinInstance) {
//	ThisCall(0xB78A90, apThis, apShaderProp, apRenderPass, aMatrix, apTransform, aeRenderPassType, apSkinInstance);
	//Logger::Log("scaling light by %f", TheShaderManager->ShaderConst.HDR.PointLightMult);
	//NiColorAlpha* pColor;

	// ambient light is constant 0
	//ScaleColor(GetLightConstant(0), TheShaderManager->ShaderConst.HDR.PointLightMult);

	// pointlight registers go from 0 to 10
	//for (UInt32 i = 0; i < 12; i++) {
	//	ScaleColor(GetLightConstant(i), TheShaderManager->ShaderConst.HDR.PointLightMult);
	//}

	// emittance color is index 27
	//ScaleColor(GetLightConstant(27), TheShaderManager->ShaderConst.HDR.PointLightMult);
//}

