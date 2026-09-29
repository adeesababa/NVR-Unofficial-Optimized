# UNOFFICIAL Optimized build: technical notes

Plain-language overview: see the top of [README.md](../README.md). This page lists what was changed
and how, for anyone who wants the details. Measurements are from one PC (GTX 1070, 2560x1440,
native Direct3D 9) using the built-in F10 profiler; other hardware will differ.

## Effect chain and copies
- **Frame chain.** Effects used to render into the game's target and then copy the whole frame
  into `TESR_RenderedBuffer` so the next effect could read it. Each chain (pre- and post-tonemap)
  now alternates between two textures and swaps which one is "current"; samplers follow the slot
  (`TextureRecord::TextureRef`). Passes that clip, discard, blend or use stencil are detected from
  the preprocessed source and get their destination pre-filled, so output is unchanged.
  The pre-tonemap chain uses the game's own single-level target as one of the two buffers.
- Effects that never sample `TESR_SourceBuffer` no longer refresh it (the distant-only DOF mode reads
  `TESR_RenderedBuffer` instead, so it makes no full-frame copy).
- **Direct final pass.** The post-tonemap chain works in NVR-owned textures because the back buffer
  cannot be sampled. The effect that will render last is predicted (`FrameChain::SetFinalEffect`) and
  its last pass renders straight into the game target, so the chain has no copy-back to do. If another
  effect renders after all, `FrameChain::Owns` restores the image first (one copy, never a wrong image).
- **Identity LUT skip.** A colour-grading LUT whose texels are checked at load time to be an exact
  identity (the shipped `neutral_lut.png`) is not applied; a real LUT always is.
- **Composite apply.** The exterior sun-shadow composite and the AO combine are applied inside the
  volumetric-fog reconstruct pass (same maths and order) instead of as two separate full-resolution
  passes, falling back automatically when an effect that runs between them is active.

## Individual effects
- **Ambient occlusion:** dedicated half-resolution `G16R16F` ping-pong targets; both kernels
  evaluated together; depth-aware four-sample upsample. (About 2.3 ms -> 0.5 ms.)
- **Volumetric fog:** half-resolution estimate written as multiply/add coefficients to two MRTs,
  applied to the full-resolution scene (no scene detail lost); closed-form height-fog integral
  instead of a 32-step loop.
- **God rays:** dedicated half-resolution targets, early exit in the radial march, attenuation
  at reduced resolution. (About 1.6 ms -> 0.5 ms.) The sky-mask pass returns black straight away for
  every pixel that is not sky (its result is multiplied by a sky flag, so it is exactly zero there)
  instead of fetching the scene and computing the sun glare first; the output is bit-identical
  (checked on the GPU against the previous shader for sky/world mixes, HDR values and sun positions).
- **Contact shadows:** ping-pong through scratch targets (removes a read/write feedback loop),
  N.L fade and distance-scaled bias (fixes horizontal black lines), blur skips depth reads when all
  taps are equal.
- **Depth buffer:** single `R32F` channel; the post-projection value is recomputed in the shaders
  (`projectedDepthFromLinear` in `Includes/Depth.hlsl`).
- **Interior point-light shadows:** the deferred shadow apply blurs the shadow term on the two
  `G16R16` scratch targets and touches the HDR frame only in its last pass (was: a full-frame copy,
  three HDR clears and two HDR blurs); the lighting pass skips the cubemap read for lights that cannot
  reach a pixel (their contribution is exactly zero beyond the radius). Optional
  `PointShadowInterval` redraws each light's cubemap every N frames (a cubemap stores distance from
  the light, so it is camera independent; it is redrawn at once when its light, position, radius or
  cell changes). Lights keep the same cubemap slot from frame to frame (`PointShadowSlots.h`): the
  slots used to follow the distance ranking, so two lights of similar distance swapping rank swapped
  slots and forced both cubemaps to be redrawn at once. The slot order does not change the image, except
  that the last slot is lit without a shadow lookup (`PointShadows.fx`), so it keeps the farthest of twelve
  casters as before, and its cubemap, which nothing samples, is no longer drawn. (Measured in one interior
  with the `POINT SHADOWS` counters: redraws caused by a light changing slot are about 0.05 per frame; the
  redraws above the every-N-frames schedule, about 1 per frame with 11 lights at interval 2, come from two
  lights whose position changes every frame, most likely the game's flicker movement.)
- **SMAA:** cheaper combined edge detection, explicit stencil state. **DitherBuster:** single pass.
  **Point shadows:** merged passes.
- **Shadow cascades:** staggered refresh (middle every 4 frames, far/LOD every 8, spread over the
  8-frame cycle), only updated cascades resolved/prefiltered, cheaper face culling, batched bone
  uploads.

## In-game switches (defaults file `NewVegasReloaded.dll.defaults.toml`)
Settings only exist if they are present in the defaults file, so always install it with the DLL.

`[_Main.Main.Performance]` - same image, on by default:
`FrameChain`, `CompositeApply`, `ChainUsesGameTexture`, `WorldSceneGuard`, `SlimDepthBuffer` (restart required).
`MergedDepthNormals` is off by default: on a GTX 1070 (native D3D9) the driver silently skipped the
two-render-target draw, leaving depth and normals stale. Every call in that path already returned
success (the retail runtime returns S_OK for a draw the driver rejects), so since P40 the path checks
itself the first time it runs in a session: it fills the normals target with a marker colour, reads the
centre pixel back before and after the draw (one 1x1 copy and readback, a one-off stall of a few
milliseconds) and logs `UNOFFICIAL merged depth/normals check`. If the marker is still there, the draw
did not run and the path switches itself off until restart (separate passes, correct image). Two
`UNOFFICIAL merged depth/normals state` lines record the targets' formats and sizes, the game's
depth-stencil, the MRT capability bits and the render states the draw ran with.

`[_Main.Main.ReducedQuality]` - change the image, off by default (except `PointShadowInterval`), apply immediately:
- `FXAA` - lean console-style FXAA (after FXAA 3.11), 5 texture reads per pixel, 9 on edges, after
  SMAA. Measured 0.14-0.16 ms vs SMAA 0.9-1.2 ms; meant to replace SMAA.
- `GodRaysLowRes` - god rays at quarter instead of half resolution.
- `AOLowRes` - ambient occlusion at quarter instead of half resolution.
- `PointShadowInterval` (1-4, **default 2**) - redraw point-light shadow cubemaps every N frames; 1 = every frame.
- `CheapReflections` (P40) - the water reflection map is drawn without the forward sun-shadow lookup
  (`TESR_ShadowForwardData.x = 1`, the forward path's own off switch) and without terrain parallax
  (`TESR_TerrainParallaxData.x = 0`). NVR's constants reach a game shader only when
  `ShaderRecord::SetCT` runs, which happens when the pixel shader changes; so the first bind after the
  values change (at the start and at the end of the reflection pass) is told that no pixel shader is
  bound, which makes an NVR shader upload them even if the game keeps the same shader across the
  boundary. Note: the older `ForceReflections` path sets `TESR_ShadowData.x = -1`, which the current
  exterior shaders no longer read (they test `TESR_ShadowForwardData.x`), so it never switched sun
  shadows off in reflections; that path is left as it was.
- `NearCascadeInterval` (1-2, P40) - at 2 the near sun cascade is redrawn on even frames only. The
  limited cascades already use odd frames (middle 1 and 5, far 3, LOD 7 of the 8-frame cycle), so every
  frame then draws exactly one sun cascade (checked by a `static_assert`). Between redraws the cached
  near cascade is kept locked to the camera translation like the others, and its selection sphere
  (camera-relative centre) is moved with the camera too.
- Measured in P40 (GTX 1070, 1440p, Rivet City exterior, same view): `CheapReflections` 1.15 -> 1.01 ms
  for the reflection pass (-0.14 ms); `NearCascadeInterval 2` -0.14 ms GPU (cascade geometry 0.29 ->
  0.23, atlas prefilter 0.27 -> 0.19), no measurable CPU change.

## Static point-shadow cubemap reuse (P41, P44)

Each point-light cubemap stores distance from the light divided by its radius, so it is independent of
the camera. P41 hashes the complete geometry list for each light once per frame: geometry identity,
world transform, world bound, cull/property flags and material alpha. If the list is complete, contains
no skinned geometry or wind-animated tree leaves, and the hash is unchanged, its existing cubemap is
reused indefinitely—even when `PointShadowInterval = 1`. A transform, visibility, alpha, light, radius,
cell or texture change redraws it immediately. Missing lists and lists containing dynamic geometry keep
the previous interval schedule unchanged. The F10 `POINT SHADOWS` line reports static reuses and caster
invalidations. This first, conservative cache does not yet split static and dynamic casters into two
textures.

P42 adds that split for complete mixed lists. A second R32F cubemap per sampled light holds the static
casters. Each frame its six faces are copied to the cubemap already sampled by the lighting shader;
skinned geometry and animated tree leaves are then rendered over it with `D3DBLENDOP_MIN`, which keeps
the nearer normalised distance. Static geometry changes rebuild the cache. Moving lights, incomplete
lists, disabled `PointShadowStaticOverlay`, unsupported R32F blending, or a failed copy retain the P41
full-redraw/interval path. The setting takes effect live, while allocating the roughly 66 MiB cache
requires a restart. F10 reports static-layer rebuilds and dynamic overlays separately.

The P42/P43 static/live overlay experiment was rejected. The first test rebuilt all five mixed static
layers each frame; making its hash order-independent still left four to five layers genuinely changing
and rebuilding. It was 0.79-0.85 ms slower in cubemap GPU time, about 0.43 ms slower on the CPU, used
roughly 66 MiB more video memory, and produced visibly darker areas. P44 removes the second textures,
MIN-blend path and setting. It keeps P41's conservative whole-static cache, with the harmless improvement
that its caster-set hash no longer depends on list traversal order.

## Exterior shadow apply needs SunShadows (P40)
The exterior shadow apply (`ShadowsExteriors.fx`, or the composite apply in the fog pass) multiplies
the scene by the red channel of `TESR_PointShadowBuffer`, which `PointShadows` fills with the
point-light term and `SunShadows` then overwrites with sun visibility. A tester's P37 log showed
`SunShadows.fx.hlsl` failing to compile (`undeclared identifier 'GetWorldNormalLod'`: our zips had
never shipped the changed `Effects/Includes/Normals.hlsl`), so outdoors the channel stayed near 0
and the apply darkened every pixel to `1 - Darkness`, exactly the reported "Darkness slider darkens
the whole screen unless it is 0". `ShadowsExteriorEffect::ShouldRender` now skips the exterior
apply while `SunShadows` is not loaded or switched off, with one `UNOFFICIAL exterior shadow apply
skipped` log line. Since P40 the zips carry the repo's complete shader folders (plus
`Textures/NewVegasReloaded/LUTs/neutral_lut.png`, the one texture the fork added) instead of only the
files changed since the fork base, and `tools/check-package.ps1` verifies a package before it is zipped
(every tracked shader file present and current, no extra files, DLL and defaults current). Since P45
they also carry all of `resource/Textures` (about 18 MB zipped), so a zip is a complete install without
NVR underneath.

## Robustness fixes from user logs (P45)
- **Crash at startup without the shadow shaders.** `ShadowManager::Initialize` set `ClearSamplers` on
  the seven shadow-map shader records before checking that they had loaded; on an install without
  `Shaders/NewVegasReloaded/Shaders/Shadows` (a user who installed an overlay zip with no NVR
  underneath) they were null and the game crashed right after `Starting the shadows manager...`. A
  missing shader now disables shadow maps with an `[ERROR]` log line. `ShadowMapClear.pso` is now part
  of that check too.
- **GPU vendor message.** "AMD/Intel detected" was logged whenever the D3D9 layer offers the RESZ depth
  format, which DXVK does on every GPU, so an RTX 4070 Ti under DXVK was reported as AMD/Intel. The log
  now names the adapter (`Graphics adapter: ... (vendor ..., device ...)`) and says which depth resolve
  is used and why (`Depth resolve: RESZ (DXVK, any GPU brand)`, `RESZ (AMD/Intel driver)` or `NVAPI`).
- **LUTs.** The shader used the day LUT's cell count for the night and interior LUTs too, so LUTs of
  different sizes scrambled each other; each slot now has its own (`TESR_LUTData.x/z/w`). A texture
  that is not an N*N x N strip is not used (log line), and a slot with no usable LUT (missing file,
  wrong shape) passes colours through: before, a missing `neutral_lut.png` left the samplers empty
  and the pass would have returned black. Same result as before for any correctly shaped LUTs.
- **LUT loading** (`src/effects/LUTFile.h`, tested in `tests/lut_identity.cpp`). LUT images used to go
  through the plain D3DX loader, which rounds each side up to a power of two and resamples: a 33-point
  strip (1089x33) came out 2048x64 (test output: "old loader 2048x64, new loader 1089x33"), which the
  shader then reads as a different, scrambled 64-point LUT - the likely cause of a user's speckled,
  teal/orange screenshot with a custom "Top Gun" LUT. Images now load at their exact size with one
  level and no filtering (the old loader stays as a fallback for devices without non-power-of-two
  textures). `.cube` files (Adobe/Resolve text, red fastest) are read directly into the same strip
  layout, 8-bit, clamped to 0..1; 1D LUTs and input domains other than 0..1 are rejected with a log
  line. The test checks that a graded .cube sampled through a CPU model of `LUT.fx.hlsl` matches the
  LUT's own trilinear lookup within one 8-bit step, which pins down the axis order.

## World scene guard and trace
NVR refreshes its depth buffers only inside its hook on the game's `RenderWorldSceneGraph`. Once, after
a save was loaded straight into an interior, the game rendered frames without calling it (the F10 log
showed no `World scene (game)` / `Depth resolves` samples and `Pre-scene` equal to the whole frame), so
the effects ran on a stale depth buffer and the image came out almost black until another cell was
entered. It did not happen again in later tests, and the cause is not known. With `WorldSceneGuard` on,
NVR's effects are skipped after 10 consecutive frames without a world scene render (never on the main
menu or a loading screen) and resume as soon as one happens. `WORLD TRACE` lines in the log (at most 80
per session) record which render calls ran on the first frames after each cell change, to help find the
trigger if it recurs.

## Profiler
F10 toggles GPU/CPU timing (a small red dot and "PROF" appear in the top-right corner while it runs). Every 120 frames the averages are written to `NewVegasReloaded.log` as
`GPU PROFILE ...` and `CPU PROFILE ...` lines; `Frame interval (CPU)` is the real frame time.
Queries are read asynchronously and never flush the GPU. Only the two timestamp queries are required; the
optional disjoint and frequency queries may be missing or fail (timing continues, assuming a 1 GHz clock), so
the timers also work under DXVK (checked with DXVK 2.6.1). Failures are logged with their HRESULT. Indented names are sub-timers nested in the
line above them (contact shadow passes, fog estimate/composite, god-ray passes, exposure/bloom, chain
end copies, interior shadow blur/apply). With profiling on, `POINT SHADOWS ...` lines report how many
point-light cubemaps are redrawn per frame and why (scheduled refresh, new light, other light in the
slot, moved, resized, cell change).

### World scene split by shader family (P40)
About half of the frame is drawn by the game's own render loop (world scene, water reflection map,
first-person model), most of it with NVR's replacement pixel shaders. `SetShadersHook` sees every
geometry pass being set up; while profiling, it issues one GPU timestamp wherever the shader family
changes inside one of those three passes (`src/core/GpuTimeline.h`: one timestamp per change, not a
begin/end pair, up to 1024 per frame, six frames in flight, read without flushing). Every 120 frames
`GPU SPLIT <pass>` lines give the average and maximum GPU time per family, sorted, split into `NVR`
(NVR's replacement shader was bound) and `vanilla` (the game's own). The family is the terrain template
(`TERRAIN`, `TERRAIN LOD`, `TERRAIN FADE`) or the leading letters of the game's shader name (`SLS` lit
objects, `PAR` parallax objects, `SKIN`, `SM3` hair and eyes, `STLEAF` tree leaves, `GRASS`, `SKY`,
`WATER`, ...); `(pass setup)` is the time from the start of the pass to its first shader bind. The
timestamps sit in the command stream, so an interval includes any time the GPU waited for the CPU
inside it: exact for a GPU-bound frame, relative otherwise. The per-pass totals should match the
`World scene (game)`, `Water reflections (game)` and `First person (game)` timers.

`SHADER BINDS` lines (every 120 frames) give, per pass, the geometry passes set up per frame, how many
changed the pixel shader, how many of those went to an NVR shader (each such change uploads NVR's
constants, `ShaderRecord::SetCT`), and the family changes; plus the CPU time spent inside
`SetShadersHook`. The hook also remembers which Direct3D pixel shader has already drawn in which pass
(all session, profiling or not), so each `FRAME SPIKE` line now ends with the shaders used for the first
time in that frame and the longest gap between two binds (the draws of a pass happen between its bind
and the next one; a driver compiling a shader variant at its first draw would show up there).

### Frame-time statistics
While profiling, every 1200 frames (and when F10 is pressed again, if at least 200 frames were collected) the log
gets a `FRAME TIMES` line: average, p50 / p95 / p99 / p99.9 / max frame time, the 1% and 0.1% low frame rate (the
average fps of the slowest 1% / 0.1% of frames) and a spike count. A second line repeats the p99 and lows for
steady play only, leaving out frames within three seconds of a cell change or loading screen. A frame much slower
than a typical one is logged as `FRAME SPIKE`, with the NVR CPU timers that were slow in it (at most 60 per
session). These use only CPU timing, so they work identically on native Direct3D 9 and under DXVK. The startup
log names the runtime in use: `D3D9 runtime: DXVK (...)` or `system Direct3D 9 ...`.

### Tried and rejected: contact-shadow blur skip mask
The two contact-shadow blurs already return the centre pixel without their depth reads when the twelve
taps agree (spread < 1e-4). A per-tile "flat" mask (min/max of the march output per 8x8 pixels, dilated by
one tile, so the blurs could skip even the tap fetches) was built and checked on the GPU at 2560x1440: the
output was bit-identical to the plain blurs for every test input, but the whole was slower. With 77% of the
tiles flat the masked blurs saved only 0.03-0.08 ms of about 0.3 ms, building the mask cost 0.14-0.16 ms, and
the total came out 0.22-0.27 ms slower; with 32% flat tiles 0.31-0.35 ms slower. That points to these passes
being limited by memory traffic (source, depth and output, about 12 bytes per pixel) rather than by the tap
fetches, so skipping fetches barely helps; a fixed cost per pass is not the cause (a dependent 1x1 pass costs
0.0014 ms). For such passes fewer bytes per pixel or fewer passes is what counts.

## Log markers
Lines starting with `UNOFFICIAL` report which optimized paths are active, e.g.
`UNOFFICIAL frame chain ...`, `UNOFFICIAL composite apply active ...`,
`UNOFFICIAL dedicated AO active ...`, `UNOFFICIAL depth buffer: R32F (single channel)`.

## Building
Visual Studio 2019 (v142), Release | Win32, DirectX SDK (June 2010). Effect shaders are compiled at
runtime by the game from `Data\Shaders\NewVegasReloaded\Effects\`; delete the `Cache` folder there
if a shader change does not seem to apply.
