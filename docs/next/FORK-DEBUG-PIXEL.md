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
- Effects that override `Render` without calling the base one (TAA, SMAA, DepthOfField, Bodycam, ShadowsInteriors, SunShadows) and
  passes drawn only inside an effect's own Vulkan port (its intermediate buffers) do not show as separate stages; the frame after the
  next recorded effect still includes their result.
- Up to 64 stages per frame; more are counted as refused.
- The counters' atomic adds have contention on 12 addresses; if that proves slow, a subgroup add first would cut it.
