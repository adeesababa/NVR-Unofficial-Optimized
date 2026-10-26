# Debugging tools on NVR DXVK: pixel inspector and shader counters (2026-10-26)

Branch `deep/fork-debug-pixel` (off `deep/fork-combo-1026`). Code only: built, not run in game yet (bulk list below).
Everything is off by default, and does nothing without the NVR DXVK d3d9.dll with extension 1.5-debug (fork patch 0015).
On native D3D9 or stock DXVK the log says once that it "needs NVR DXVK" and the window says the same; nothing is created.

## What you get

### 1. Pixel inspector
Pick one screen pixel. For that pixel NVR records the value it has at every stage it can see in a frame:
- the scene as the game drew it, before any NVR effect (HDR);
- the linear depth (`TESR_DepthBuffer`, also shown as units from the camera) and the view normal (`TESR_NormalsBuffer`, also decoded);
- the picture after each NVR effect that drew this frame, in drawing order ("after AmbientLight", "after VolumetricFog", ...);
- the buffers effects draw into themselves (passes drawn with RenderEffectToRT): the shadow passes show up as "PointShadows t0 -> shadow
  buffer", "SunShadows t2 -> shadow buffer" and so on (these are the shadow terms);
- the picture out of the tonemapper, before the post effects;
- the final picture before the HUD.
For each stage you get the centre texel exactly as stored (r g b a, no filtering) and the mean of the 3x3 around it, plus the texture's size
(half-resolution buffers map the pixel by its position on screen, like a full-screen pass reading them).

Where it shows:
- In game: open the NVR menu. While the inspector is on, a window "NVR Pixel Inspector" appears with the table, a pink cross marks the
  pixel, "Pick with the mouse" then a click on the picture (not on a menu window) chooses the pixel, "Log now" writes the table to the log.
- In the log: `PIXEL INSPECTOR pixel (x, y) of WxH, frame N (...)` and one `PIXEL INSPECTOR   [NN] <stage> ...` line per stage,
  when the pixel changes (and the first result after switching on), and every `LogEvery` frames if that is set.
- In a shot list: `inspect <x> <y>` switches it on for that shot at that pixel (undone after the shot like any `set`).

Cost when on: one tiny compute dispatch (one 3x3 workgroup) per stage, about 20-40 per frame, plus a copy of at most 10 KB. The results
are read back about three frames later and the game never waits for them. Off: each hook is a single true/false test.

### 2. Shader counters / "printf" for a Vulkan pass
A reusable helper for any NVR Vulkan pass (GLSL: `src/hlsl/NewVegas/Compute/Counters.glsl`; C++: `src/core/ShaderCounters.h`):
- `NVR_COUNT(i)` / `NVR_COUNT_N(i, n)`: add to one of 32 named counters (atomic adds).
- `NVR_PROBE(slot, vec4)`: at the one chosen pixel (the pixel inspector's X, Y, mapped into the pass's own target), keep 4 floats in one of
  8 slots. Like a printf for one pixel.
- The pass gets a second build with `-DNVR_COUNTERS=1` (its own .spv); the normal .spv contains none of it, so with the setting off nothing
  changes and nothing costs.
- The C++ side resets the counter block before the pass, copies it out after, reads it back a few frames later without waiting, and logs
  the per-frame averages once a second: `SHADER COUNTERS <name> (per frame): N frames read (... skipped) | <counter> <average> ... ||
  probe pixel (x, y): <probe> (a b c d) ...`. The same line shows in the pixel inspector window.

Wired as the example: the Reflections Hi-Z trace (`ReflectionsTraceHiZ.frag`, built a second time as `ReflectionsTraceHiZCount.spv`).
Counters: rays; how each walk ended (walked to the end / off screen, walk hit, hit thrown away, hit by the fixed-step rest,
out of cells with nothing found, not started: the DebugView 9 rules); cells walked; the DebugView 10 skips (facing along the ray, the
surface's own plane, behind by more than the thickness); pixels with no reflection. Probe at the inspector's pixel: end rule, cells
walked, hit distance, confidence; the three skip counts and the roughness; the hit position, depth and reflection amount.
Cost while on: atomic adds in every trace pixel (about 5 per pixel at half resolution), not measured yet (the bulk list times it).

## Settings (all off by default; in the menu with "Developer options" ticked, under Main > Develop)
`[Main.Develop.PixelInspector]`
- `Enabled = false`: the pixel inspector.
- `X = 960`, `Y = 540`: the pixel, in the game's render pixels (0,0 = top left). Also the shader counters' probe pixel (even with the
  inspector off).
- `LogEvery = 0`: also log the table every N frames (0 = only when the pixel changes or "Log now").

`[Main.Develop.ShaderCounters]`
- `ReflectionsHiZ = false`: the Hi-Z trace runs its counting build (needs NvrDxvkPasses, Reflections HiZ, outdoors).

Both sections are left out of presets (like `[Main.Develop.Main]`).

## The fork side: patch 0015 (extension 1.5-debug)
`tools/nvr-dxvk/patches/0015-NVR-extension-1.5-debug-readback-that-never-waits.patch`, a new interface `ID3D9NvrExtReadback`
queried from `ID3D9NvrExt` (a d3d9.dll without it answers E_NOINTERFACE and nothing else changes):
- `CreateReadbackBuffer(size)`: a buffer in host-visible memory, used only as a copy destination.
- `CopyToReadback(dst, src, size)`: records the copy in order with everything before it and remembers its place in DXVK's command stream.
- `TryReadback(buffer, ...)`: copies the bytes out only if that copy has already run on the GPU; otherwise returns "not yet" at once
  (no flush, no wait). NVR keeps a ring of 4 such buffers and reads each one about three frames after its copy; if all 4 still wait, that
  frame is simply not copied (shown as "skipped").
- Also: buffers made for storage now list the vertex and fragment stages too, so a pass's fragment shader can add into a counter buffer
  with DXVK's barriers covering it (needed by the counters).
The old `ReadBuffer` (waits for the GPU) is unchanged and is not used by these tools.
Fork DLL built by the GitHub workflow (Actions run 40389008551, all patches 0001-0015 applied):
`G:\_NVR-build-scratch\nvr-dxvk-debugpixel\d3d9.dll` (+ `dxvk.conf` with `d3d9.customVendorId = 1002` for depth).

## Files
- `src/core/PixelInspector.{h,cpp}`: the inspector (stages, the copy dispatch, readback, log, ImGui window, picker).
- `src/core/ShaderCounters.h`: `DebugReadbackRing` (the non-waiting ring) and `ShaderCounters`.
- `src/hlsl/NewVegas/Compute/PixelInspect.comp` (+ `.spv`), `Counters.glsl`, `ReflectionsTraceHiZ.frag` (counters inside `#ifdef`),
  `ReflectionsTraceHiZCount.spv`. `ReflectionsTraceHiZ.spv` is not rebuilt (the normal trace is byte for byte the old one).
- `tools/nvr-dxvk/compile-debug.sh`: builds the two .spv (glslang + spirv-val); the `nvr-dxvk.yml` workflow runs it and uploads
  them as the artifact `nvr-debug-spv` (WSL was not available, so the committed .spv come from that artifact).
- Hooks: `EffectRecord::Render` (after an effect drew), `ShaderManager` (scene in, depth, normal, tonemapped), `Hooks/Render.cpp` (final +
  end of frame), `ImGuiManager::BuildUI` (window), `SettingShots.h` (`inspect`), `effects/Reflections.cpp` (counters example).

## How to add counters to another pass
1. In its .frag/.comp: `#include "Counters.glsl"` and put `NVR_COUNT(...)` / `NVR_PROBE(...)` where you want them.
2. Build a second .spv with `-DNVR_COUNTERS=1` (add a line to `tools/nvr-dxvk/compile-debug.sh`).
3. In the effect: a `ShaderCounters` object with the counter and probe names; when your setting is on, make the counting pipeline with
   the normal bindings plus `ShaderCounters::Binding(slot)`, call `Begin(probeX, probeY)` before the draw (null = draw the normal
   pipeline), bind `ShaderCounters::Bind(slot, buffer)`, call `End()` after. See `effects/Reflections.cpp`.

## Installing (when it is tested)
- `NewVegasReloaded.dll` -> `<game>\Data\nvse\Plugins\`.
- `src/hlsl/NewVegas/Compute/PixelInspect.spv` and `ReflectionsTraceHiZCount.spv` -> `<game>\Data\Shaders\NewVegasReloaded\Compute\`.
- The fork `d3d9.dll` from `G:\_NVR-build-scratch\nvr-dxvk-debugpixel` (a slot copy only; the main game stays native D3D9).

## Test list (for the coordinator; no game was run for this branch)
`bench\foundations\lists\fork1026-debugpixel-bulk.txt`: 8 shots, 300 frames, og-desert: settle; all off; the inspector at the centre,
the sky and the ground (LogEvery 120 in the first); the Hi-Z trace without and with counters, and with counters plus a probe pixel.
Pass: the pictures of off vs inspector, and of counters off vs on, are identical; the log has the PIXEL INSPECTOR tables and the
SHADER COUNTERS lines; no "needs NVR DXVK", no "could not be set up", no DXVK `NVR ext:` errors.

## Not tested / open
- Nothing has run on a GPU: not the fork's new methods, not the copy shader, not the counting trace. The DLLs build (NVR with MSBuild,
  the fork on GitHub with every patch applied); the shaders pass spirv-val.
- The "render target bound" handling (a stage's texture is unbound around its copy and put back, with the viewport and scissor) is new
  code on the game's own state; a wrong viewport or target right after a stage would point at it.
- Passes drawn only inside an effect's own Vulkan port (its intermediate buffers) do not show as separate stages.
- Up to 64 stages per frame; more are counted as refused.
- The counters' atomic adds have contention on 12 addresses; if that proves slow, a subgroup add first would cut it.

## First run (2026-10-26, coordinator's bulk run 20261026-161607-slotC, og-desert, combo e781aae8, fork CI 0001-0015)
- Works: the 1.5-debug readback was found, the inspector set itself up, tables arrived within a few frames, nothing waited (ring:
  0 skipped). Values make sense: sky pixel depth 1.0 (= farZ 353840) with normal (0, 0, -1); ground at y 1000 is 302 units away with
  normal (0.01, 1.00, 0.06); the centre 9738 units. Colours follow the chain (scene in 0.43/0.34/0.23 HDR -> tonemapped 0.61/0.46/0.28
  -> Cinema 0.59/0.44/0.25 = final).
- Pictures: inspector on vs off and counters on vs off differ only by the scene's own frame-to-frame noise (adjacent shots: about 18-19k
  pixels over 8 levels, mean 1.6, the same as two inspector shots against each other; shots further apart drift more).
- Counters: `SHADER COUNTERS` once a second, ~105 frames read per second, 0 skipped; the probe at (480, 400) arrived. Cost: the trace
  went from p50 0.25 ms to 0.58 ms with the counting build (+0.33 ms at 1080p, half-resolution trace).
- What the Hi-Z counters showed (per frame, 219.6k traced rays): 219.2k (99.8 %) walked to the end / off the screen with nothing found,
  340 (0.16 %) walk hits, 0 thrown away, 0 out of cells, 26 not started; about 11 cells per ray, and 10 of those skipped as "behind by more
  than the thickness" (2.20M a frame) against 0.37 facing and 0.14 own plane. The probe pixel (ground, 2835 units): end rule 0 after 10
  cells, 9 of them thickness skips. So nearly every ray rejects the cells it passes as "behind": the thickness test is the suspect.
- Fixed after it (commit after 9c6fbf14): (1) the frame effects whose class overrides Render (AmbientLight, VolumetricFog, GodRays,
  SMAA, SunShadows, ...) were missing, and with the chain on the game texture the picture was labelled "own target": frame stages now
  come from ShaderManager's call sites, taken only when the frame was written (FrameChain write count), and own targets from
  RenderEffectToRT; (2) the reflection capture's cube faces filled the 64 stages: cube faces are left out; (3) a ring slot left from the
  previous switch-on was logged as the next shot's "first result" (old pixel): results from before the switch-on are dropped.

## Second run (2026-10-26, 20261026-170842-slotC, combo a3f41491 = d33554ce + Hi-Z pyramid v2)
- Inspector fixes confirmed: 18-24 stages with SunShadows' shadow buffer, AmbientLight, VolumetricFog, Reflections (its 4 own passes and
  the frame after it), Exposure, SMAA, Cinema; no cube faces; the counters+inspect shot's first result is its own pixel (960, 800).
  One spurious "after SnowAccumulation" stage (same values as the scene) on the first frame after a switch-on: fixed (the frame-write
  count is taken at the end of every frame).
- Pictures within noise (adjacent pairs 16-20k pixels over 8 levels, mean 1.5-1.65, same as inspector-vs-inspector).
- Counters run with the new layout (0 skipped, probe (480, 400) arrives) but report every one of the 518,400 trace pixels as "not started"
  and none as "no reflection"; the probe's depth reads 0.0037 units where the inspector shows 2841. Cause (Hi-Z branch, not the counters):
  the pyramid buffer is a READ-ONLY storage buffer on buffer slot 0, and the extension puts read-only storage buffers and uniform buffers
  in the same DXVK slot range (NvrExtBufferSlotBase + slot), so it replaces the trace's uniform constants (slot 0): farZ and the
  matrices are read from the pyramid. The normal Hi-Z trace has the same binding table, so it is broken the same way. Fix: give the
  pyramid any buffer slot other than 0 (e.g. 1) in ReflHiZBinding and in the resource it binds.
- With every pixel hitting the same two counters the counting build cost +1.0 ms (trace 0.17 -> 1.19 ms); in the first run, with the
  rules spread over many counters, +0.33 ms.
