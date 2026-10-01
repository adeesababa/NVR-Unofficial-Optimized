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
- **Shadow cascades:** only updated cascades resolved/prefiltered, cheaper face culling, batched bone
  uploads. P21-P48 also refreshed the cascades on a staggered schedule (middle every 4 frames, far/LOD
  every 8); that changed the shadows of moving people, so since P49 it is the opt-in
  `StaggeredSunShadows` (see "Sun-cascade schedule (P49)").

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
  other cascades refresh on odd frames (LOD 3 and 7 by default; with `StaggeredSunShadows` middle 1
  and 5, far 3, LOD 7 of the 8-frame cycle, so every frame then draws exactly one sun cascade, checked
  by a `static_assert`). Between redraws the cached
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

## Sun-cascade schedule and point-shadow default (P49)
A player on the Strip (under DXVK) reported flickering shadows on NPCs, and it reproduced on native
D3D9. Two default-on changes let the shadow of a moving person lag behind the person, so on the stale
frames the body sits partly inside its own old shadow and flickers:
- **Sun cascades.** Upstream NVR redraws the near, middle and far cascades every frame, and with
  `ShadowsExteriors > ShadowMaps > LimitFrequency` only the LOD cascade every 4th frame. P21 made that
  middle every 4th and far/LOD every 8th (with shadow MSAA, P28 also without). With the default
  `Distance = 6000` and `CascadeLambda = 0.9`, the near cascade ends at about 225 units (3 m), middle at
  about 610 (9 m), far at about 1750 (25 m), so most people you see on the Strip are in the middle or far
  cascade. P49 restores the upstream schedule by default; `[Main.Main.ReducedQuality] StaggeredSunShadows`
  (off by default) brings back the P21-P48 schedule (it applies whatever `LimitFrequency` says). Only the
  cascades redrawn that frame are resolved and prefiltered, as before. The P21 measurement of the stagger
  was about -0.5 ms GPU (cascade geometry 1.02 -> 0.66-0.72, resolve 0.36 -> 0.25-0.27, prefilter
  0.53 -> 0.38-0.41; GTX 1070, 1440p).
- **CachedDistantShadows (P59, ReducedQuality, off).** The stagger for middle/far/LOD, plus a forced redraw
  of a cascade while a character's shadow can be sampled from it. `ShadowManager::CollectMovers` gathers
  the player and the NPC/creature references of the loaded exterior cells (world bound spheres, culled
  nodes skipped). `MoversInCascade` counts a mover for cascade i when (a) the cascade gathers actors,
  (b) its view depth plus a shadow reach (`radius * (1 + 2 * horizontal/vertical sun slope, capped at
  10)` + 64) passes 90% of cascade i-1's end depth (`Constants.ShadowMapRadius`), since nearer on-screen
  points are shaded by the sharper cascades (spheres enclose their slices; only the outer 10% blends),
  and (c) its sphere is not outside any active plane of the cascade's last shadow frustum, widened by the
  camera's movement since that redraw. A cascade that had a mover at its last redraw redraws once more,
  so a character walking out does not leave a stale shadow. Other dynamic objects are treated as still.
  Ignored while `StaggeredSunShadows` is on. Measured on the Strip (StripTops, 38 characters tracked):
  middle and far were redrawn every frame (75% / 88% of frames for characters), so only the LOD cascade was
  saved: geometry 0.96 -> 0.91 ms, prefilter 0.67 -> 0.60, ~88.5 -> ~89.5 fps. In a moment with nobody in
  range the earlier run showed geometry 0.92 -> 0.34, prefilter 0.77 -> 0.33, Sun cascades CPU 1.0 -> 0.29.
- **Tried and dropped in P59: a distant size cut.** A ReducedQuality `DistantShadowMinSize` (far/LOD skip
  nodes below a world-space bound radius) left out only 2 nodes per frame at 30 units on the Strip, with no
  timer change: `FormsFar/FormsLod MinRadius = 10` texels already cuts small references, and most clutter is
  geometry inside larger nodes, which the radius test never sees. Larger values would cut cars and crates.
- **ParallaxLite, terrain (P60 as TerrainParallaxLite; ReducedQuality, off).** P61 merged it with the objects' lite path below into one switch; `Configuration::UpdateOldDefaults` (settings version 61) carries a saved TerrainParallaxLite over. `TESR_TerrainParallaxData.w = 2` (Terrain.cpp; overrides
  HighQuality). `Parallax.hlsl`: 8 coarse steps, and on a hit, instead of the contact refinement (a second march of
  up to `numSteps` through the bracketing step, at least one more batch of four height lookups), `LITE_SECANT_STEPS`
  (1) secant step: the height where the chord between the bracket's samples meets the ray, keeping the half that
  still brackets it. Parallax shadows: taps at 1 and 1/2 of the ray, each counted twice. The weights the caller
  blends with come from the last lookup, which for lite is the secant one at the hit. `tests/game_shaders.cpp`
  (full-screen terrain, 2560x1440, GTX 1070) times it against the defaults and 8 steps, reports the picture
  difference and writes `build\shader-test\parallax-full.png` / `-lite.png`: lite -19..-28% (8 steps -7..-13%),
  mean difference 1.9-2.4/255 (8 steps 0.9-1.7) on its noisy synthetic height maps. Secant steps 0: -23..-37% at
  3-4x the difference; 2: -16..-22%; 3: no faster than 8 steps. The other settings stay bit-identical.
  In game (open desert, standing still): GPU SPLIT TERRAIN 3.74 -> 2.59 ms, world scene 6.19 -> 4.96, game frame
  total 12.26 -> 11.04, ~81 -> 89 fps; the user rated the visual change minimal (a matter of taste in the small shadows).
- **ParallaxLite, terrain distance (P61).** Terrain.cpp caps `TESR_TerrainParallaxExtraData.x` (MaxDistance) at
  1024 * screen height / 1440 while ParallaxLite is on (a bump's height on screen goes with screen height / distance,
  so the visible cutoff is the same at any resolution: 768 at 1080p, 1536 at 4K); a lower MaxDistance still applies.
  In game at 1440p (log 04-37-40, rocky desert, lite on): MaxDistance 2048 -> 1024 terrain 4.53 -> 3.77 ms, 70.4 ->
  74.5 fps, no visible difference in same-spot screenshots. Harness equivalent (MaxDistance halved): -40..-66% total
  lite terrain time, but its noisy, mostly distant test ground overstates the picture change.
- **ParallaxLite, objects (P61; tested as a separate ObjectParallaxLite).** `TESR_ParallaxData.w = 1` (POM.cpp, per frame). ParallaxTemplate's
  `getParallaxCoords` branches at the top into `getParallaxCoordsObjectLite`: up to 8 steps in batches of four and
  `LITE_SECANT_STEPS` secant steps, same start, bounds and final interpolation. A separate function because sharing
  the loop with the lite code made the default 4-9% slower; the parallax shadow taps are left alone for the same
  reason (one fetch each, the lite branch cost more than it saved). `tests/game_shaders.cpp` "parallax" (PAR2000 and
  PAR2009 over a scene from head-on/near to grazing/far, smooth height map): off bit-identical, +0.8..3.1% (at the
  noise level of identical-code runs); lite -26..-28%, mean difference 0.17/255, 2% of pixels over 2/255. In game the
  desert cells that prompted it (log 2026-09-30 03-53-52) spent 1.2-2.2 ms in PAR shaders and 3.8-4.5 ms in terrain
  (TEX_COUNT 4-7 variants SLS2116-SLS2146, many blended ground textures).
- **A/B log lines (P59, F10 only).** `SWITCHES performance: ... | reduced quality: ...` lists every key of those
  two defaults sections when the profile starts and whenever one changes (checked every 30 frames); a change
  also ends the current `FRAME TIMES` window, so no window mixes two settings. `CACHED DISTANT SHADOWS` (every
  240 frames while that switch is on): per cascade the % of frames redrawn on schedule + because of
  characters, and the characters tracked.
- **Point shadows.** `PointShadowInterval` defaults to 1 again (earlier builds: 2). The menu's Save writes
  every setting, so a saved config usually holds the old default; it is reset once to 1 (see "Old saved
  defaults" under P50). Still lights keep their cubemaps at any interval (P44), so interval 1 costs little
  in rooms without moving people.
- **GPU name in the log.** DXVK's `hideNvidiaGpu` option reports an NVIDIA card to the game as
  "AMD Radeon RX 6700 XT" (vendor 1002, device 73DF), which a player took for a misdetection. The log now
  says `Graphics adapter as reported to the game: ...`, adds `Windows display adapters: ...` from
  `EnumDisplayDevicesA` (user32, not replaced by DXVK), and explains when the two differ. Nothing in NVR
  depends on the name.

## Exposure flicker and old saved defaults (P50)
The user saw the whole screen flicker slightly darker and brighter on the Strip and isolated it to the
Exposure effect. `AvgLuma.fx` measures the frame's brightness from 112 single texels, averages it over 8
frames (`(luma + 7 * old) / 8`, a per-frame filter, so its time constant shrinks as the frame rate rises:
about 0.25 s at 35 fps, 0.07 s at 113 fps), and then limits the change to `dt * DarkAdaptSpeed` /
`dt * LightAdaptSpeed` brightness units. The default speeds of 50 allow any change within a frame, so the
exposure followed blinking neon and bright signs sliding across the sample points. Neither shader nor the
effect changed in this fork, but its higher frame rate made the flicker about three times faster. P50
sets all six adapt speeds (Main, Night, Interiors) to 0.2 per second; the user confirmed in game that this
stops the flicker. The rate limit uses the frame time, so it behaves the same at any frame rate.

**Old saved defaults.** `Configuration::UpdateOldDefaults` (start of every `LoadSettings`) moves a saved
value that equals a replaced default to the new one, once: `PointShadowInterval` 2 -> 1 (version 49) and
the six adapt speeds 50 -> 0.2 (version 50). The user config records its version in
`[_Unofficial] SettingsVersion`; that key is not in the defaults file, so the menu does not list it, and
Save writes it with the rest of the config. A config without it counts as version 0. The log line
`UNOFFICIAL settings: old defaults saved by an earlier build moved to the new defaults: ...` lists what
changed. (P49 briefly used the absence of `StaggeredSunShadows` as the marker; P50 replaces that.)

## Log files (P46)
Before P46 the plugin wrote one `NewVegasReloaded.log` to the current folder, overwritten at every
start, so a log was lost as soon as the game was started again. Now `Logger::InitializeRotating`
(`src/base/Logger.cpp`, rules in `src/base/LogFiles.h`) writes a new
`NVR-Unofficial-Optimized-Logs\NewVegasReloaded_YYYY-MM-DD_HH-MM-SS.log` (local start time) in the
folder of the game's exe, falling back to the current folder and then to the old single file if the
folder or file cannot be created. At each start the oldest logs are deleted so that 25 remain including
the new one; only files whose names match that pattern exactly are counted or deleted (a second start in
the same second gets `-2`). A `NewVegasReloaded.log` left in the game folder by an older build is moved
into the folder once, named after its last write time. Every line starts with `[HH:MM:SS.mmm] ` (local
time) and is written with a single `fwrite`, so lines from different threads cannot interleave; lines
longer than 2 KB are formatted on the heap instead of being cut. The first line gives the start date and
the log's folder and file name (not the full path, which can contain the player's Windows user name).
The in-game log window shows the lines without the time. `tests/log_files.cpp` checks the names, which
files may be deleted (other files and folders in the log folder are never touched), 30 launches into a
fake game folder, the move of the old log and long lines around the buffer size. Writing is cheap: a
raw write of one line to the game drive takes about 2 us, so buffering the log (roadmap 2C) was
measured and dropped in P47.

## Enhanced water style (P48-P53, removed)
An experimental opt-in water style (animated FFT ocean waves, foam, a clarity slider, a real-water reflection curve)
was built in P48 and P51-P53 and removed again before release: it went beyond this build's goal of the same
picture, faster. It is kept on the local branch `experiment/enhanced-water`. The classic water shaders are as in
P45. The water reflection probe (P54-P57) stays: see "Reflection probe" below.

## Water reflection probe (P54-P57, diagnostic)
A player's clip showed the reflection of far buildings squashed into a thin
strip when looking level and the right height when looking down: the same water reflects different things depending
on the camera pitch, which looks like the reflection "scrolling". The game draws the reflection map with its own
camera inside `RenderReflections`, and the water shaders look it up as if that camera were an exact mirror of the
main one (`1 - v` of the main camera's projection). To see how the game's reflection camera differs, pressing the NVR
screenshot key arms `ReflectionProbe` (`NewVegas/Hooks/Render.cpp`): in the next frame the first shader bind of the
world pass and of the reflection pass record the render target, viewport, D3D view/projection transforms
(`GetTransform`), the scene camera and the camera passed to `RenderReflections`; the log gets `UNOFFICIAL reflection
probe` lines and the reflection map is saved next to the screenshot as `<name> reflection.png`. Nothing changes
otherwise.

## Cheap underwater terrain (P48, ReducedQuality, off by default)
A player reported the frame rate dropping sharply when wading and swimming, with either water style. The F10 split
(standing in the Potomac, native, GTX 1070, 1440p) showed the world scene rising from 5.6 to 7.4-9.5 ms, all of it
in `TERRAIN NVR` (3.5 -> 5.6-7.4 ms); the water surface itself stayed at 0.1-0.6 ms. With the eyes low, the lake bed
and banks fill the screen at close range, where the terrain parallax works hardest (up to 16 steps plus refinement
and parallax shadows per pixel), and shores blend many ground textures (the shader's cost grows steeply with the
count). Switching NVR's terrain shader off at that spot gave 16.9 -> 12.9 ms per frame (game terrain shader 1.8 ms
instead of 5.6). `CheapUnderwaterTerrain` keeps NVR's terrain lighting but skips parallax and parallax shadows on
ground below the water line: `Terrain.cpp` puts the camera-relative height of the water the player is in or looking
at (`TES::GetWaterHeight`, only while a water plane is loaded nearby, 10 units lower so the shoreline keeps its
parallax) into `TESR_TerrainParallaxExtraData.w` (-FLT_MAX when off), and `TerrainTemplate.hlsl` compares it with the
pixel's camera-relative height (the same reconstruction the forward sun shadows use, now computed first) and branches
around the two parallax calls. The water line test is switched off during the water reflection pass (mirrored
camera, where that reconstruction is meaningless). `tests/game_shaders.cpp`: with the switch off the terrain output is
bit-identical to before in all five parallax configurations; with everything under water it equals parallax and
parallax shadows off, and full-screen terrain costs 51-78% less (TEX_COUNT 1: 1.31 -> 0.65 ms, 7: 5.80 -> 1.28 ms).

## Sun step glide and cross-fade (P62-P64, SunSmoothing)
Upstream's `QuantizeSun` snaps the sun direction to 15-degree steps (about one in-game hour) against shadow-edge
crawl, and `SmoothSun` only interpolates changes below `MaxJumpAngle` (5 degrees), so every step was an instant jump.
`ShadowsExteriorEffect::CalculateSmoothedSunDir` now slides a step of under 1.5 step sizes (measured from the previous
target) over `GlideSeconds` of real time (smoothstep-eased normalized lerp, `GetTickCount64`); bigger changes (wait,
sleep, load) snap. `CrossFade` (compiled in with `SUN_CROSSFADE` only when on at startup, forward shadows only): at a
step `StartSunCrossFade` copies the atlas (`TESR_ShadowAtlasOld`, same size and format) with the matrices, centres
and camera translations of its cascades, forces every cascade to redraw for the new direction, and `Shadow.hlsl`
blends `GetOldSunShadow` (the same cascade choice on the kept data, re-translated each frame by the camera movement,
c146-c166, s10/s15) into the new shadow over `GlideSeconds`. Off, the game shaders are bit-identical to before.

## Optional visual extras (P64, all off by default)
- **Lit particles and blood** (`Shaders.Particles`, a shader collection). The game draws blood, smoke, dust and debris
  particle systems (`BSSM_NOLIGHTING_PSYS`, `_SUBTEX_OFFSET`: NOLIGHT016/017.vso), blood spray cards
  (`_TexVC_FALLOFF`: NOLIGHT006.vso) with the unlit NOLIGHTTEXVC.pso, and geometry decals (blood on characters,
  GDECAL/GDECALS) with an unlit decal shader. The replacements are the game's shaders ported to shader model 3 (from
  their disassembly) plus lighting: ambient (the sky's outdoors, the cell's indoors) and the 24 tracked point lights
  per vertex (`Includes/ParticleLight.hlsl`), the sun through the forward sun shadow per pixel. Falloff cards are lit
  only when normally blended (the additive ones -- muzzle and impact flashes, sparks -- use the game's fade-to-black
  fog mode). Flames and sparks use NOLIGHTTEXVCPMA.pso, not replaced. `SetShadersHook` gives a NOLIGHT/GDECAL draw the
  game's own pair when only one side has a replacement (a 3.0 shader cannot pair with a 2.x one) and always in the
  first-person pass. Off, the collection uses the game's shaders.
- **Bounce light** (`Shaders.BounceLight`, an effect after ambient occlusion): one bounce of screen-space indirect
  light at quarter (or half) resolution -- a prepare pass packs depth, octahedral normal and colour per low-resolution
  pixel into one A32B32G32R32F texel, an 8-sample gather on a disc capped at 6% of the screen width, depth-aware blur
  and upsample. Buffers are created on first use.
- **Contact-hardening sun shadows** (`Shaders.ContactHardening`, compiled in with `CONTACT_HARDENING` only when on at
  startup): Near and Middle cascades, EVSM4. A blocker search on the positive-exponent moments estimates the caster's
  depth (variance soft shadow mapping), the penumbra is the caster distance times the sun's size, and the moments are
  averaged over a 12-tap disc that wide before the Chebyshev test.

## Fixes (P63-P65)
- **Actors going light or dark as the player crossed a sun-shadow edge** (forward shadows, P65): the vertex
  shaders rebuild each point's camera-relative world position for the forward sun shadow from clip space through
  NVR's inverse projection, which is made once per frame from the camera's near and far planes. Actors drawn with
  other planes came back with their depth squashed towards the camera, so the whole actor read the shadow at the
  player's position. `GetShadowWorldPos` (Includes/Shadow.hlsl) now scales the point to view depth `clip.w` along its
  ray: exact for any perspective near/far, and the same point as before when they match. Covers every user of it
  (objects, skin, hair, terrain, grass, trees, lit particles and blood decals).
- **WetWorld puddles** never reflected the sun or lamps: upstream left their roughness on the developer
  `TESR_DebugVar` values (0 in every install, so GGX was 0) and passed a float as the sun direction; both restored to
  the values before that change. Upstream also set the puddle `Amount`/`Increase`/`Decrease` defaults to 0 (unchanged).
- **Settings migration** now runs right after the settings file is read (it ran a moment before that, so the
  first pass skipped it), and copies saved Sharpening and Flashlight values into their new `Interiors` sections.
- **Section names of 40 characters or more** overflow `ConfigNode::Section[40]` and lose every menu change; the new
  sections stay shorter.

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
F10 toggles GPU/CPU timing (a small red dot and "PROF" appear in the top-right corner while it runs). Every 120 frames the averages are written to the log (see Log files) as
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

Since P47 the lit-object family is split by variant group (by shader number, `src/effects/PBR.h`):
`SLS 1-3 lights` (2000-2028, sun plus up to three lights), `SLS 4+ lights` (2029-2036, up to six lights
in one pass), `SLS light pass` (2037-2044, the additive passes that redraw an object for further
lights), `SLS diffuse pt` (2045-2046), `SLS specular` (2047-2056) and `SLS other`; the `SLS` of earlier
logs is their sum. With the `SHADER BINDS` lines comes a `SLS 4+ LIGHTS in use per draw` line: for each
of those shaders, how many lights its draws used, read back from the device (`EmittanceColor.a`, c2.w,
or `PSLightColor[0].a`, c3.w, in the OPT variants) at the next bind in the same pass, once the draw has
used it; and the share of light slots computed for nothing, since these shaders compute every slot and
multiply the unused ones by 0. That share decides whether skipping unused slots pays (roadmap 3J-a).

### Game shader A/B test (P47)
`tools\test-game-shaders.ps1` (`tests/game_shaders.cpp`, needs a D3D9 GPU) extracts the committed game
shaders from git and compiles them and the working copy's with the game's own compiler (D3DX43
preprocess plus `D3DXCompileShader`, the same defines as `ShaderRecord::LoadShader`). It draws both over
varied test scenes into a 32-bit float target and requires every pixel to be bit-identical, checks that
the scenes have no NaNs and that the feature under test (parallax, parallax shadows, extra lights)
visibly changes them, and times both on full-screen draws at 2560x1440. It covers the terrain shaders
(TEX_COUNT 1-7, five parallax settings) and the 4+ light object shaders (0-6 lights in use).
`--asm-template <file> <profile> <out> NAME=VALUE...` writes the compiled assembly of any game shader
template. Findings so far (details in the roadmap, items 3C and 3J): the compiler already hoists the
terrain parallax loop's invariant work, and skipping a terrain texture's height fetch when it has no
height map is bit-identical but 22-37% slower, so the terrain shader was left unchanged.

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
