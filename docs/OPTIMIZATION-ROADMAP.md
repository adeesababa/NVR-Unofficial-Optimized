# NVR UNOFFICIAL Optimized: optimization roadmap (P40 and later)

Written 2026-09-29 by a planning-only session (no code changed) after reading `HANDOFF.md`, the P39
profiler logs quoted there, and the source. It is the work list for the coding sessions that follow.
Read it together with `HANDOFF.md` (session state, paths, rules) and `docs/UNOFFICIAL-TECHNICAL.md`
(what has shipped and what was tried and rejected).

Everything marked **estimate** is a guess from the code; only F10 logs from the user's PC count.
Nothing in this document is measured yet unless it quotes a P-number log.

## Status

| Build | Items | State |
|---|---|---|
| P40 | Phase 1 I1 (world scene split by family, `GPU SPLIT` lines), I2 (first uses and longest bind gap in `FRAME SPIKE` lines), I3 (merged depth/normals self-check by readback, replaces the HRESULT idea: see F4), I4 (reflection and first-person passes split the same way; binds per pass), I5 (`SHADER BINDS` lines: binds, pixel shader changes, NVR constant uploads, CPU in `SetShadersHook`); 3B as switch `CheapReflections`; 4A as switch `NearCascadeInterval` | **run in game 2026-09-29, results in section 1b**. `CheapReflections` -0.14 ms, `NearCascadeInterval 2` -0.14 ms GPU (both below their estimates); I3 not exercised yet |
| P40 (same build) | Fixes from a tester's P37 log: packages now ship `Effects/Includes/Normals.hlsl` (missing since P8: SunShadows and VolumetricFog failed to compile on installs without it, and the exterior shadow apply then darkened the whole image by the Darkness setting); the exterior apply is skipped while SunShadows is not running; `tools/check-package.ps1`; packages carry the complete shader folders | built and shipped in the same P40 zip; the tester's data point: on a fast GPU under DXVK at 1080p the world scene was 79% of the GPU frame and the frame was CPU-bound (interval 9.85 ms vs GPU 6.57 ms), but the game's own CPU work, not NVR's (see 1b: NVR's shader-bind CPU cost is under 0.1 ms) |
| P41 | 3A step 1: indefinitely reuse a point-light cubemap when its complete caster list is static and its geometry/state hash is unchanged; dynamic or unavailable lists keep the existing interval schedule; F10 counters for static reuse and caster invalidation | in-game clean, no visible shadow change; only 1-2/11 lights qualified: cubemaps -0.13 to -0.17 ms GPU, but hash scan +0.08 to +0.13 ms Shadow maps CPU; proves the mechanism but justifies the static/live overlay for a material gain |
| P42 | 3A step 2: cache static point-shadow geometry in a second R32F cubemap and composite skinned/animated casters into the sampled cubemap every frame with MIN blending; live kill switch and automatic P41 fallback | rejected as built: the game's caster-list traversal order changes, so the order-sensitive hash rebuilt all five mixed static layers every frame; overlay on vs off: cubemaps 1.86 vs 1.01 ms, Shadow CPU 1.69 vs 1.26 ms, GPU frame 13.79 vs 13.01 ms |
| P43 | Make the P42 static-caster hash order-independent while retaining per-caster transform/material/visibility invalidation | rejected: 4-5 mixed static layers still genuinely invalidated every frame; overlay on vs off: cubemaps 1.48 vs 0.69 ms, Shadow CPU 1.28 vs 0.85 ms, GPU frame 13.17 vs 12.55 ms; user also saw darker areas |
| P44 | Retire the failed static/live overlay and its 66 MiB textures/setting; retain P41 whole-static reuse with the safer unordered caster hash | built and unit-tested; returns to the visually clean, faster fallback before continuing 3C |
| P45 | Fixes from two community logs: no startup crash without the shadow shaders, adapter and depth-resolve log lines, per-slot LUT size, exact-size LUT loading and `.cube` LUTs; zip carries all textures | pushed and released (tag p45); not yet run in game |
| P46 | Log files: one timestamped log per launch in `NVR-Unofficial-Optimized-Logs\` (25 kept), `[HH:MM:SS.mmm]` on every line, one `fwrite` per line | ran fine in game (user, 2026-09-29) |
| P47 | 3J step 2: F10 splits `SLS` into variant groups and logs how many lights the 4+ light shaders use per draw. 3C and 2C measured and rejected (see those items). New `tests/game_shaders.cpp` (old vs new game shader: bit-exact pixels on the GPU + timing) | built; waiting for an interior + exterior F10 run |
| P48 | Not an optimization: opt-in enhanced water style step 1 (animated ocean-wave volume texture, shallow/distance calming, softer Fresnel), classic output byte-identical; missing-texture errors logged once (a tester's log had 53,000 repeats); opt-in `CheapUnderwaterTerrain` (no terrain parallax below the water line: the wading/swimming frame drop was NVR terrain 3.5 -> 5.6-7.4 ms; submerged terrain 51-78% cheaper in the harness, bit-identical when off) | enhanced water looked good in game; underwater terrain switch waiting for a test |
| P49 | Fix for flickering shadows on moving NPCs (player report, reproduced): the P21 staggered sun-cascade refresh (middle every 4th, far/LOD every 8th frame) is now the opt-in `StaggeredSunShadows` (off; default = upstream schedule, about +0.5 ms GPU outdoors); `PointShadowInterval` default 1 again, with a one-time reset of saved 2s; log names the Windows GPU next to DXVK's disguised one. Same change pushed to PR #76 as commit 6 | built; the user reproduced the flicker, fix not yet run in game |
| P50 | Not an optimization: exposure flicker on the Strip (user isolated it to Exposure). Adapt speeds 50 -> 0.2 (confirmed in game via the menu); saved old defaults moved once through a hidden `[_Unofficial] SettingsVersion` (replaces P49's marker). Same default change pushed to PR #76 | built, not yet run in game |
| P51 | Not an optimization: enhanced water step 2, foam (whitecaps from the crest channel, a lacy shore/object band from the water depth, new `nvr_foam.dds` from `make_water_waves.exe --foam`); off with the enhanced style, classic water byte-identical | removed before release (kept on branch experiment/enhanced-water) |
| P52 | Fix: enhanced water on placed water (WATER001/018) was a flat swaying mirror because `WATER001.vso` does not write the world position; it is now rebuilt from TEXCOORD0 + camera position in every water pixel shader | removed before release (kept on branch experiment/enhanced-water) |
| P53 | Enhanced water: softer, see-through, drifting foam with fading whitecap trails; new `Clarity` (0.5); real-water reflection curve (Schlick, FresnelPower 5, saved 2.5 moved once) without the classic brighter-sky extra reflection | removed before release (kept on branch experiment/enhanced-water) |
| P54 | Diagnostic: water reflection probe (screenshot key saves the game's reflection map and logs both passes' cameras, viewports and transforms) to find why reflections squash with camera pitch | ran: reflection camera orientation and projection are the exact mirror; the level-view map looks mirrored about the wrong height |
| P55 | Reflection probe v2: logs the game's camera position globals in each pass, the first object drawn, and every water plane's height | built, not yet run in game |
| P59 | Sun-shadow draw count (research profiler: far ~800 and LOD ~450 draws per frame on the Strip). ReducedQuality `CachedDistantShadows`, off by default (middle/far/LOD staggered, but a cascade that can hold a character's shadow is redrawn every frame, so no NPC flicker). A/B log lines (`SWITCHES`, FRAME TIMES window cut at each change). A distant size cut (`DistantShadowMinSize`) was tried and dropped: no effect | ran: ~0.1 ms on the crowded Strip (middle/far forced nearly every frame), up to ~1.2 ms GPU + 0.7 ms CPU with nobody in range |
| P60 | 4F: ReducedQuality `TerrainParallaxLite`, off by default: 8 steps + one secant step instead of the contact refinement, two parallax shadow taps. Harness: terrain shader -19..-28% (HighQuality off -7..-13%) | ran: desert terrain 3.74 -> 2.59 ms, ~81 -> 89 fps; visual change minimal (user) |
| P61 | TerrainParallaxLite becomes `ParallaxLite` and also covers parallax objects (rocks, cliffs, walls), in their own function so the default is unchanged (harness -26..-28%, mean difference 0.17/255); a saved TerrainParallaxLite carries over. README tip: MaxDistance 1024 | ran: rocky desert terrain 6.2 -> 4.1 ms with lite (65 -> 73.5 fps), objects 0.63 -> 0.57 ms, MaxDistance 1024 another 0.8 ms (70.4 -> 74.5 fps), no visible difference (user) |

---

## 0. Ground rules (unchanged from the previous 39 builds)

- **Lossless by default.** Any change that alters the image ships as an opt-in switch under
  `[_Main.Main.ReducedQuality]` in `resource/NewVegasReloaded.dll.defaults.toml`, off by default,
  with an honest comment on what it costs visually. Same-image changes go under
  `[_Main.Main.Performance]`, on by default. Settings only exist if they are in the defaults file.
- **"Bit-identical" is a test result, not an opinion.** Use a GPU comparison harness (see
  `outputs\_superseded\p39-godrays-gpu-test` and `p39-contact-mask-experiment`, both modelled on
  `tests/ao_device.vcxproj`) or a CPU model test (`tests/*.cpp`, run by `tools/test-local.ps1`).
- **One build number per batch** (`P<n>` in `NewVegasReloaded.rc` and the F10 log line), package
  `..\..\outputs\NVR-Unofficial-Optimized-P<n>.zip` (since P40: built from scratch with the complete
  shader folders, and `tools\check-package.ps1` must pass), exact install list for the user, F10 test,
  then push only when the user says so. Release title `P<n>`, tag `p<n>`.
- **Measure before and after** on the same route (Rivet City hangar interior, Rivet City exterior,
  daytime). Report medians of the 120-frame windows and the `FRAME TIMES` percentiles. Say
  "unmeasured" when it is.
- **Do not redo** anything in `docs/UNOFFICIAL-TECHNICAL.md` "Tried and rejected" or in
  `HANDOFF.md` section 6 of the P39 brief. Section 9 of this document repeats the list.
- **Keep `HANDOFF.md` current** at the end of every session (it is outside git; it holds paths).

---

## 1. Where the frame goes now, and what has never been attributed

P39, GTX 1070, 2560x1440, native D3D9, medians of 120-frame windows, GPU milliseconds.

| Bucket | Exterior (Rivet City, day) | Interior (hangar, 11 shadow lights) |
|---|---|---|
| Game frame total | 10.74 | 10.93 |
| World scene (game draws, NVR shaders) | 4.12 | 4.16 |
| Pre-scene (shadow maps + game pre-work) | 1.90 | 1.29 (cubemaps 1.20) |
| Water reflections (game) | 1.03 | - |
| Sun contact shadows | 0.89 (march 0.28, blur H 0.30, blur V 0.33) | - |
| Volumetric fog (estimate + composite) | 0.77 | 0.55 |
| Shadow apply (interior) | - | 1.20 (blur H 0.43, blur V 0.45, apply 0.33) |
| Point shadow lighting | - | 0.78 |
| Ambient occlusion | 0.42 | 0.59 |
| Luma + exposure + bloom | 0.44 | 0.46 |
| Sun cascade geometry / atlas prefilter | 0.42 / 0.36 | - |
| God rays | 0.35 | - |
| Game image space (tonemap) | 0.32 | 0.33 |
| Depth resolves / combine / normals | 0.22 / 0.16 / 0.18 | 0.14 / 0.17 / 0.18 |
| DOF / FXAA / Cinema / dither / LDR copies | 0.16 / 0.15 / 0.14 / 0.11 / 0.12 | 0.16 / 0.13 / 0.14 / 0.11 / 0.11 |

Frame-time tail (P39): interior 1% low 53-60 fps; exterior steady play 1% low 39.5-57 fps,
p99 14-17.5 ms against a p50 of about 10.7 ms. Steady-play spikes of 27 and 41 ms coincided with
the first use of an NVR game-shader variant (a `... Successfully bound` log line); the 41 ms one
had 24.5 ms inside the game's water reflection call.

The unattributed part is the important one. About **5.9 ms of the 10.7 ms exterior frame** (world
scene 4.1, reflections 1.0, shadow-map geometry 0.8) is drawn by the game's own render loop with
NVR's replacement pixel shaders (`src/hlsl/NewVegas/Shaders/*`), and no log has ever split it by
shader family, by MSAA, or against the vanilla shaders. NVR's own post passes add up to only
about 4.3 ms exterior / 4.6 ms interior, and that profile is flat (the largest item is 0.9 ms).
Everything below is ordered around that fact: measure the big unmeasured bucket first, then do the
structural items, then polish the flat tail.

## 1b. Measured in P40 (user log 2026-09-29, GTX 1070, 2560x1440, native; medians of 120-frame windows)

The world-scene split works: its per-pass totals equal the `World scene (game)` and
`Water reflections (game)` timers exactly.

| Pass | Total | By shader family (NVR = NVR's replacement shader bound) |
|---|---|---|
| Exterior world scene | 5.04 | lit objects `SLS` NVR 1.87, terrain NVR 1.84 (0.2-3.2 by view), grass vanilla 0.31, `ISCOPY` vanilla 0.23 (a game image-space copy inside the world pass), sky NVR 0.18, terrain LOD NVR 0.12, `SLS` vanilla 0.09, terrain fade 0.07, water NVR 0.07, parallax objects 0.04 |
| Exterior reflections | 1.10 | terrain LOD NVR 0.33, sky NVR 0.23, `SLS` vanilla 0.21, `ISBLUR` vanilla 0.20 (the game's reflection blur), `SLS` NVR 0.09 |
| Interior world scene (hangar) | 4.99 | lit objects `SLS` NVR **3.97**, `SLS` vanilla 0.83, rest 0.2 |

Consequences for this list:
- **Interior: NVR's object shader is the biggest single cost in the interior frame (3.97 ms)**, larger
  than cubemaps (1.32 GPU + 1.30 CPU), shadow apply (1.25) or point lighting (0.74). New item 3J.
- **Exterior: objects and terrain are about equal (~1.9 each).** The terrain share of parallax is still
  unknown (the parallax A/B in this log was confounded by camera movement): repeat it standing still.
- **Exterior PBR A/B (follow-up log, standing still):** switching PBR off moved `SLS` from NVR to
  vanilla and reduced it 0.98 -> 0.74 ms; world scene 2.37 -> 2.13 ms, whole GPU frame 8.47 ->
  8.25 ms, and frame interval 8.64 -> 8.43 ms. The roughly 0.24 ms shader delta is real but small.
  This was outdoors.
- **Interior PBR A/B (second follow-up log, stable windows):** PBR on -> off reduced total `SLS`
  6.67 -> 6.11 ms (-0.56), world scene 6.94 -> 6.41, whole GPU frame 13.90 -> 13.33, and
  frame interval 14.10 -> 13.52 ms. PBR adds about 9% to lit-object shading and 4% to the frame in
  this view. Cubemaps stayed 1.42 -> 1.44 ms GPU and Shadow maps CPU 1.35 -> 1.38, so the comparison
  was stable. `SLS` work is worthwhile but bounded below the cubemap target in this scene.
- **F2 / 5A / 5B are dropped:** 27-46 binds per frame outdoors and 231 in the hangar, and all of
  `SetShadersHook` (including the game's own shader setup) costs 0.06-0.09 ms of CPU per frame.
- **Lead A / 2A (warm-up) is not supported:** the two steady-play spikes (86 ms, 24 ms) had no first
  shader use and no stall between binds (< 1 ms). Keep 2A parked unless a cold-cache test (0.8) shows
  first uses inside spikes.
- **2C (logger) gets evidence:** the worst frame inside `SetShadersHook` was 11 ms, at the hangar load,
  where 36 first-bind log lines were written with `fflush` each.
- **3B `CheapReflections` measured -0.14 ms** (estimate 0.2-0.5); **4A `NearCascadeInterval 2` measured
  -0.14 ms GPU** (cascade geometry -0.06, prefilter -0.085; CPU ~0; estimate 0.3-0.4). Both stay opt-in.
- Settings levers seen in the split (lossy, advice only): `ISBLUR` 0.20 ms in reflections
  (`iWaterBlurAmount`), reflection terrain LOD and sky 0.56 ms together.

---

## 2. Findings from reading the code (new; not in HANDOFF.md)

- **F1. The water reflection pass runs full-cost shaders when `ForceReflections` is off.**
  (P40 correction: the `ForceReflections` branch's `ShadowData->x = -1` does not switch sun shadows off
  either; the exterior shaders test `TESR_ShadowForwardData.x` (c133), not `TESR_ShadowData.x`. And
  its overrides only reach a shader through `SetCT`, i.e. on a pixel-shader change. `CheapReflections`
  uses c133 and forces the upload on the first bind after each change.)
  `RenderReflectionsHook` (`src/NewVegas/Hooks/Render.cpp`) only disables forward sun shadows
  (`ShadowData->x = -1`) and terrain parallax (`TerrainParallaxData->x = 0`) in the
  `ForceReflections == true` branch. The user runs it off, so every object the game draws into the
  1024x1024, `iWaterMultisamples = 4` reflection map evaluates cascaded shadows and parallax. The
  reflection is then blurred by the game (`iWaterBlurAmount`). See 3B.
- **F2. Frame-invariant constants are re-uploaded on every pixel-shader switch.**
  `ShaderRecord::SetCT` (`src/core/ShaderRecord.cpp`) is called from `SetShadersHook` whenever the
  bound pixel shader changes. For a forward-shadow object shader it issues roughly 20
  `SetPixelShaderConstantF` calls (two 4x4 inverse matrices, four cascade matrices, four cascade
  centres, shadow/format/fade/blur/forward data, PBR data, nine SH irradiance registers) that are
  identical for every draw of the frame, plus sixteen `SetTexture(i, NULL)` calls. All of them are
  pinned at c100 and above (`Shaders/Includes/Shadow.hlsl`, `SkyAmbient.hlsl`), where the game
  never writes, so they persist across draws; `ShadowsExteriorEffect::UpdateConstants` already
  relies on that for c129/c133. CPU cost only; see 5A/5B.
- **F3. Shadow-map draws re-upload every constant per object.** `RenderPass::RenderAccum`
  (`src/core/RenderPass.cpp`) calls `PixelShader->SetCT()` and `VertexShader->SetCT()` per
  geometry; only the world matrix (and bones) change per object. CPU only; see 5C.
- **F4. The merged depth+normals draw's result code is never read.** (P40 correction: it is read;
  `RenderWithNormals` chains every HRESULT including `DrawPrimitive` and logs a failure, and the P31
  log said "active", so every call returned success. P40 instead checks by reading back a marker
  pixel of the normals target before and after the draw.) `EffectRecord::Render`
  ignores the `HRESULT` of `DrawPrimitive`, and `CombineDepthEffect::RenderWithNormals` does not
  log `SetRenderTarget(1, ...)` failures. "Silently dropped by the driver" may be a validation
  reject (`D3DERR_INVALIDCALL`), which the retail runtime returns without drawing. A ten-line
  diagnostic (I3) settles it.
- **F5. Terrain parallax is the largest suspected pixel cost in the world scene, and it is
  unmeasured.** `Shaders/Includes/Parallax.hlsl`: with `HighQuality = true` the march runs up to
  16 steps plus a contact-refinement pass of the same length, and every step calls
  `getTerrainHeight`, which fetches the alpha of **every** blended terrain texture (up to 7) and
  evaluates a `pow` per texture; the self-shadow term fetches four more heights per texture with
  `if (quality > ...)` tests that ps_3_0 flattens (all four are fetched). `MaxDistance = 2048`
  keeps it to the near terrain, but near terrain is a large share of the 1440p screen. Phase 0
  measures it in five minutes; 3C lists lossless trims.
- **F6. Exposure is a full-resolution HDR read+write (0.15 ms) for a per-pixel formula**
  (`Effects/Exposure.fx.hlsl`) whose only consumers are the bloom downsample and the game's
  tonemap shader, which NVR replaces (`Shaders/ISHDRBLENDINSHADERCIN*.pso.hlsl`). See 3D.
- **F7. The post-tonemap chain starts with a copy of the back buffer (0.11 ms)** because the
  game's tonemap writes to the back buffer, which cannot be sampled
  (`FrameChain::Begin`, seed-copy mode). NVR controls the call (`ProcessImageSpaceShadersHook`) and
  could hand the game a texture target instead. See 3E.
- **F8. Point-light shadow cubemaps store camera-independent distance and re-render all static
  geometry on every redraw** (`ShadowManager::RenderShadowCubeMap`: six faces, full clear, the
  light's whole `kGeometryList`). Static geometry could be rendered once per light and kept; only
  skinned/moving geometry needs per-frame work. This replaces the lossy `PointShadowInterval`
  with something both faster and more accurate. Full design in 3A.
- **F9. `Logger::Log` writes and `fflush`es synchronously on the render thread**
  (`src/base/Logger.cpp`). Steady play logs nothing, so this is not a per-frame cost, but the
  bursts at cell changes (12 or more first-bind lines plus WORLD TRACE lines) land on the render
  thread. Hygiene item, see 2C.
- **F10. All NVR `CreatePixelShader`/`CreateVertexShader` calls happen when the game loads its
  shader package** (`src/NewVegas/Hooks/ShaderIO.cpp` -> `ShaderManager::LoadShader` ->
  `ShaderRecord::LoadShader`), i.e. at startup, not at first draw. NVR's own first-bind work in
  `SetCT` is a few map lookups. So the 25-40 ms first-use spikes are inside the D3D9 driver at the
  first *draw* with a new shader plus state combination (NVIDIA's D3D9 driver specialises pixel
  shaders on state such as alpha test, fog, sRGB, render-target format and multisample type).
  The reflection-pass spike fits: the same pixel shader first drawn into a different target
  format with `iWaterMultisamples = 4` is a new specialisation. Phase 0 has the zero-code test.
- **F11. No game shader samples `TESR_RenderedBuffer` or `TESR_DepthBuffer`** (grep over
  `Shaders/`), so the per-bind `StretchRect`/depth-resolve paths in `ShaderRecord::SetCT`
  (`HasRenderedBuffer`, `HasDepthBuffer`) are dead for New Vegas. No hidden cost there.
- **F12. Interior lighting pass** (`Effects/PointShadows.fx.hlsl`): up to 11 per-pixel distance
  tests with cubemap fetches behind dynamic branches, `LightRadiusMult = 1.5` and
  `LightPoints = 12` in the defaults. 0.78 ms is mostly real work (interiors are lit everywhere);
  the per-pixel `luma(TESR_LightColor[i]) * w` weights could be precomputed on the CPU but that
  is a few ALU per light. Low value; listed for completeness.

---

## 3. Phase 0: zero-code measurements (the user runs these; do them first)

Each test: same route, F10 on for 30-60 s, F10 off, quit. Since P46 each launch writes its own log to
`NVR-Unofficial-Optimized-Logs\` in the game folder (25 kept, lines timestamped), so nothing needs to be
copied before relaunching; before P46, copy `NewVegasReloaded.log` first. Compare medians of the named timers and the `FRAME TIMES` line. Live settings can be
flipped with F10 running (10 s each state); ini and `Status` changes need a restart.

| # | Test | What to read | What it decides |
|---|---|---|---|
| 0.1 | `iMultiSample=4` -> `0` in `FalloutPrefs.ini`, FXAA on (already the user's AA) | `World scene (game)`, `Depth resolves`, `Game image space`, frame interval, 1% low | The single biggest zero-code lever. MSAA 4x at 1440p multiplies colour and depth traffic of every game draw; the NVAPI depth resolves are multisample resolves. **Estimate 1-2 ms.** Also unlocks 3F and makes the MergedDepthNormals mismatch hypothesis testable. |
| 0.2 | `iWaterMultisamples=4` -> `0` | `Water reflections (game)` | Sizes the reflection map's MSAA. |
| 0.3 | `bUseWaterReflectionsMisc/Statics/Trees/Actors` (FalloutPrefs.ini) all 0, then back | `Water reflections (game)` | What the 1.03 ms actually draws (sky+LOD only, or objects). Decides 3B's value. |
| 0.4 | Menu `Shaders > Terrain > Parallax`: `Enabled` off; then on with `HighQuality` off; then `Shadows` off (all live) | `World scene (game)` in a daytime exterior with near terrain in view | Sizes F5. If parallax is 1 ms or more, 3C and a `TerrainParallax` quality tier become priorities. |
| 0.5 | Menu `Shaders > ShadowsExteriors > Main > ForwardShadows` false (live) | `World scene (game)` drop vs the `Shadow apply` pass that appears | Cost of the per-object cascade lookup in the object/terrain/grass shaders. |
| 0.6 | Defaults/user toml: `[_Shaders.PBR.Status] Enabled=false`, same for `Terrain`, `Grass`, `Sky`, `Skin` (restart; keep a backup of the toml) | `World scene (game)` with vanilla shaders | The whole cost of NVR's replacement shaders in one number. Everything NVR-specific in the world scene is bounded by this. Restore afterwards. |
| 0.7 | `Shaders > ShadowsExteriors > ShadowMaps`: `Mode` 2 -> 1 (EVSM2), and `CascadeResolution` one step down (restart) | `Sun cascade geometry`, `Shadow atlas prefilter`, `World scene (game)` | Atlas bytes per texel drive the prefilter (0.36) and the forward lookup. Lossy; informs the 4C advice. |
| 0.8 | NVIDIA Control Panel: `Shader Cache Size` = 10 GB or Unlimited (default is driver-chosen and can be small), `Threaded optimization` = On. Then: walk the same route **twice in one session** (F10 on for both), and again in a **second session** | `FRAME SPIKE` lines and `Successfully bound` lines per pass; steady-play 1% low per pass | If pass 2 and session 2 have no spikes at the same places, the hitches are driver first-use compiles that the disk cache absorbs (then only a fresh install ever hitches). If they recur every session, build 2A. |
| 0.9 | Interior: `LightPoints` 12 -> 8, `LightRadiusMult` 1.5 -> 1.0 (live) | `Point shadow cubemaps`, `Point shadow lighting`, `Shadow apply` | Sizes what the interior tiers can gain; both are lossy settings. |
| 0.10 | `Shaders > DitherBuster` off while FXAA is on (live) | `Dither buster` (0.11) and a look at foliage/alpha-test dither | Whether two edge filters are needed once FXAA is on. |

Record the results in `HANDOFF.md` as a table; they re-rank everything below.

---

## 4. Phase 1: instrumentation (small code, no image change)

- **I1. World scene split by shader family.** In `SetShadersHook` (`Hooks/Render.cpp`), when
  `TheShaderManager->GetShaderCollection(Pass->PixelShader->Name)` (or "vanilla/other") differs
  from the family of the previous pass, end the running family timer and begin the new one.
  `GpuTimer` supports one active pair at a time per timer and a ring of 12 slots; a frame has
  dozens of family transitions, so either raise `RingSize` for these timers or add an
  "accumulate several intervals per frame" mode that sums completed intervals into one sample
  per frame. Report as nested lines under `World scene (game)`: terrain, object (PBR), grass,
  sky, water, skin, tree, tonemap, vanilla. Caveat to write in the log header: intervals measured
  at shader-switch granularity include GPU idle gaps caused by the CPU, so they are relative
  numbers. This is the tool for the whole of lead B in the P39 brief.
- **I2. First-use attribution.** Keep a per-`ShaderRecord` "first SetCT frame" and, in
  `SetShadersHook`, a CPU stopwatch from one call to the next. When a frame is a `FRAME SPIKE`,
  list the shader names whose first bind happened in that frame and the longest inter-call gap
  (which is where the draw blocked). This turns the P39 correlation into a measurement.
- **I3. MRT diagnostics.** In `CombineDepthEffect::RenderWithNormals` log, once, the `HRESULT` of
  `SetRenderTarget(1, ...)`, `BeginPass`, and `DrawPrimitive`, plus `D3DRS_COLORWRITEENABLE1`
  and the two targets' formats and sizes. If it is `D3DERR_INVALIDCALL`, fix the cause (most
  likely a write-mask or format rule); a working merged pass removes the 0.18 ms normals pass.
- **I4. Reflection pass content.** Reuse I1 with an "inside RenderReflections" flag so the
  reflection map's cost is split by family too, and count draws per reflection pass.
- **I5. Shader-switch CPU cost.** A `CpuTimer` accumulating the time spent in `SetShadersHook`
  per frame and a count of `SetCT` calls, logged with the CPU PROFILE lines. Sizes F2 before 5A.

Deliverable of Phase 1: an updated cost table in `HANDOFF.md` with the world scene split by family
and the answers to I2/I3.

---

## 5. Phase 2: the 1% lows

- **2A. Shader warm-up (build only if 0.8 shows the hitches recur every session).**
  After the shader package has loaded and whenever a loading screen is up
  (`InterfaceManager->IsActive(kMenuType_Loading)`), draw one off-screen, one-triangle primitive
  per NVR pixel shader (and per replaced vertex shader) with realistic state, a few shaders per
  frame so the loading screen absorbs the compiles:
  - render target of the game's HDR format and multisample type, depth-stencil bound; a second
    round with the reflection map's format/multisample type when water reflections are on;
  - the render states the game uses for that family (alpha test on and off for object shaders,
    fog state as the game sets it, sRGB write off), TESR textures bound through `SetCT`;
  - a generic vs_3_0 pass-through vertex shader that writes every semantic the pixel shaders read
    (COLOR0/1, TEXCOORD0-7) is enough if the driver specialises pixel shaders independently of
    the vertex shader; verify with I2.
  - Persist the set of shader names that were actually used in the session to
    `logs\shader-usage.txt` and warm those first next session.
  Success criterion: the steady-play `FRAME SPIKE` count on the route drops and I2 no longer
  reports first binds inside spikes. If a first attempt gains nothing, stop and note it; the
  driver's specialisation key may include things this cannot reproduce.
- **2B. Cell-change audit.** On `isCellChanged` frames NVR sets `SettingsChanged` and every
  effect and collection re-reads its settings (`UpdateSettings`, hundreds of string-keyed
  lookups), and `ShadowsExteriorEffect::UpdateSettings` may recreate textures. Put a `CpuTimer`
  around the block in `ShaderManager::UpdateConstants` and look at the cell-change spikes' timer
  lists. Fix only what shows up (most likely it is a few hundred microseconds and the 30-130 ms
  spikes are the game's streaming).
- **2C. Logger hygiene.** Buffer log lines in memory and flush from `NiDX9Renderer__Do_EndFrame`
  at most once per 100 ms, or on a background thread; after 2A is validated, demote the
  `Successfully bound` line to a counter. Low value on its own; cheap. (P46 already writes each line
  with one `fwrite` and stamps it with the time of day, which shows how long a burst of log lines
  takes; the `fflush` per line is unchanged.)
  **Measured in P47 and rejected:** a raw write of one log line to the game drive costs about 2 us
  (benchmark, one write per line), and the P46 log shows several lines within the same millisecond, so
  the 36 first-bind lines at a cell change cost well under 0.1 ms; the 11 ms `SetShaders` frame at the
  hangar load was something else. The F10 report bursts (40-90 lines, 3-13 ms, every 120 frames while
  F10 runs) are the report's own work, not the writes.
- **2D. Far/LOD cascade frames.** The 8-frame stagger puts the far cascade on frame 3 and LOD on
  frame 7 (`ShadowManager.cpp`, `SunCascadeUpdatePhase`). Their GPU max (P28: worst cascade
  1.2-1.4 ms) is a periodic +1 ms that lands in p99. If I1 confirms the period in the frame-time
  series, time-slice the far cascade over two frames into a scratch atlas region (draw half the
  cells per frame, blur and copy on completion). Medium effort, p99 only.
- **2E. Outside NVR:** streaming and script hitches are the game's. Do not spend NVR time on them
  beyond confirming with the CPU timers that NVR was idle in those frames (P38/P39 already did).

---

## 6. Phase 3: lossless GPU work, ranked by expected gain per effort

### 3A. Cached static point-light shadow cubemaps (interiors; exteriors with point shadows on)

P41 implements the conservative first rung: cache the existing whole cubemap indefinitely only when
the light's complete caster list has no skinned/wind-animated geometry and its pointer/transform/bound/
visibility/material hash is unchanged. Dynamic or unavailable lists retain `PointShadowInterval` exactly.
This needs no second texture or blend path. Measure the new `POINT SHADOWS` static-reuse counter first;
only build the more complex static/live overlay below if dynamic-list fallback leaves substantial cost.

P41 result: dynamic-list fallback did leave substantial cost. In the interior test only 1-2 of 11
lights qualified, reducing 6.5 redraws/frame to 5.5-6.0 and cubemap GPU time by 0.13-0.17 ms, while
the per-frame hashes added 0.08-0.13 ms CPU. Shadows looked unchanged. The static/live overlay below
is therefore justified if its extra rendering-state/resource risk is accepted; also avoid hashing
the whole dynamic list once a dynamic caster has been found.

P42 implements the overlay. Each sampled slot gets a second R32F cubemap (about 66 MiB total at
512). Complete mixed lists hash only their static geometry; the static layer rebuilds when that hash,
light, position, radius, cell or texture changes. Its six faces are copied into the sampled cubemap and
skinned/animated-leaf geometry is drawn over them every frame with MIN blending and a fresh Z buffer.
Moving lights and unavailable lists use the P41/full-redraw schedule. `PointShadowStaticOverlay` is a
live Performance switch; missing blend support or a failed allocation/copy disables the path safely.
The `POINT SHADOWS` line reports full redraws, static rebuilds and dynamic overlays.

P42's in-game A/B exposed an invalid assumption: the five mixed lights rebuilt their static layers
every frame even though `casters 0.00` remained stable in P41. The game rebuilds the geometry list
without promising traversal order, while P42 fed entries through one sequential FNV hash. That made
an unchanged set look changed. P43 hashes each static caster's state independently and combines those
hashes commutatively; unit checks cover order independence and state-change invalidation.

P43 proved order was not the whole problem: four to five mixed layers still changed and rebuilt every
frame. Overlay on -> off medians were 1.48 -> 0.69 ms cubemap GPU, 1.28 -> 0.85 ms shadow CPU and
13.17 -> 12.55 ms whole GPU frame. The user also saw appealing but darker areas, which violates the
lossless requirement. Properly tracking non-skinned moving/fading geometry would add substantially more
state and still target less than 0.7 ms in this scene. P44 therefore retires the overlay, its setting and
roughly 66 MiB cache allocation. P41 whole-static reuse remains, with an order-independent set hash.

Today (`ShadowManager::RenderShadowMaps` -> `RenderShadowCubeMap`): each redraw clears six
512x512 R32F faces and draws every geometry in the light's `kGeometryList` that touches the face.
Measured P39 interior: 1.20 ms GPU and about 1.5 ms CPU for 6-7 redraws per frame at interval 2.
The cubemap stores `length(light - vertex) / radius` (`Shadows/ShadowCubeMap.pso.hlsl`), which
does not depend on the camera, so a face only changes when its casters move.

Design:
1. Per slot keep two cube textures: `Static[i]` (cached) and `Live[i]` (what the lighting pass
   samples this frame). `TESR_ShadowCubeMapBuffer<i>` is bound by name through a
   `TextureManager` slot (`TextureRef`), so pointing the slot at either texture per frame needs no
   shader change; effect samplers follow the slot (`EffectRecord::RebindSlotTextures`).
2. Classify each geometry in the light's list: **dynamic** if it has a `skinInstance` (actors),
   or if its `m_worldTransform`, `fAlpha`, `APP_CULLED` flag or `BSFadeNode` alpha changed since
   the cache was built (doors, moving platforms, fading objects); otherwise **static**.
3. Cache key per slot: light pointer, position, radius, cell, the set of static geometry pointers
   and a hash of their transforms/alphas. Rebuild `Static[i]` (six faces, with the Z buffer, as
   today) when the key changes. A door opening changes its transform every frame while it moves,
   so that light rebuilds every frame while the door moves (today's cost) and is cached again
   when it stops.
4. Per frame per light: if there are no dynamic casters in range, point the slot at `Static[i]`
   and do nothing. Otherwise `StretchRect` the six static faces into `Live[i]` (6 MB per light,
   estimate 0.03 ms) and draw the dynamic geometry on top with **MIN blending**
   (`D3DRS_ALPHABLENDENABLE = TRUE`, `D3DRS_BLENDOP = D3DBLENDOP_MIN`, `SRCBLEND = ONE`,
   `DESTBLEND = ONE`, Z test off): the cubemap holds the nearest distance, so min-blending is
   exactly the depth test, and no per-light depth surface is needed. Point the slot at `Live[i]`.
5. Keep `PointShadowInterval` as the fallback path and for the dynamic overlay if wanted.
6. `POINT SHADOWS` counters: add "static rebuilds", "dynamic overlays", "untouched" per frame.

Verification: GPU comparison of `Static`/`Live` faces against a full redraw for a static scene
(bit-identical expected), and a scene with an actor (identical where the actor casts). In game:
`Point shadow cubemaps` and `Shadow maps (CPU)` in the hangar. **Estimate: 1.2 ms GPU + 1.5 ms CPU
-> 0.1-0.4 ms each in an occupied room, near zero in an empty one.** Also more accurate than
interval 2 for moving actors. Largest single item in this document; medium-high effort (300-500
lines in `ShadowManager.cpp`, `PointShadowSchedule.h`, `ShadowsExterior.cpp` textures).

### 3B. Reflection pass without sun shadows and parallax (F1)

Apply the `ForceReflections == true` branch's constant overrides in both branches of
`RenderReflectionsHook`, behind a `ReducedQuality` switch `CheapReflections` (default off: reflected
objects lose their cascaded sun shadows and parallax; vanilla reflections never had either, and the
map is blurred by the game). Constants are only re-sent on a shader switch, so write c128
(`TESR_ShadowData`) and the terrain parallax register directly with `SetPixelShaderConstantF`
before the game's call, the way `ShadowsExteriorEffect::UpdateConstants` does for c129/c133, and
restore after. Verify with `Water reflections (game)`; **estimate 0.2-0.5 ms at the water spots**,
depending on 0.3. Small effort.

### 3C. Terrain parallax, lossless trims (F5; do after 0.4)

In `Shaders/Includes/Parallax.hlsl` (terrain path):
- Hoist the per-texture blend weights `pow(abs(blends[i]), 1 + blendFactor)` out of the march
  loop: they do not depend on the sample position, so every call of `getTerrainHeight` recomputes
  the same seven values. Bit-identical by construction (same operations, same inputs).
- Put `[branch]` on the `quality > 0.25/0.5/0.75` tests in `getParallaxShadowMultipler`;
  `tex2Dgrad` is legal inside dynamic flow control, so the far half of the terrain stops fetching
  four extra heights per texture.
- Consider early-out of the whole shadow term when `TESR_TerrainParallaxData.y` is off (already
  there) and when `lightTS.z <= 0`.
Then add an opt-in `TerrainParallaxLite` if 0.4 shows a big number: steps 8, no contact
refinement, shadows at two taps (lossy, honest description). Compile-check with
`ao_device.exe --compile-files` is not available for `.hlsl` game shaders; use `fxc /T ps_3_0`
with the template defines from `src/effects/Terrain.cpp` and a visual A/B in game.

**Result (P47): nothing lossless left; rejected.** `tools\test-game-shaders.ps1` compiles the terrain
shader with the game's own compiler (D3DX43) and disassembles it: the compiler already computes the
blend-weight powers and `rcp(blendPower)` once, before the `rep` loop, and already makes `weights > 0`
and the three `quality > ...` tests real branches. The one remaining exact change, skipping the height
fetch of textures without a height map (`status` is the same for the whole draw), is bit-identical only
when written as the old `log2`/`mul`/`exp2` (a `pow` rounds differently), and is then 22-37% SLOWER on
the GTX 1070 (nested branch around the fetch inside the loop). Measured on full-screen terrain at
2560x1440 (synthetic scene, `TERRAIN_SETTINGS_TIMES=1`): parallax is 45-64% of the terrain shader's
cost (TEX_COUNT 1: 1.34 -> 0.74 ms off; 7: 6.25 -> 2.26 ms), `HighQuality` off saves only 6-12%,
parallax shadows off 10-17%, height blend nothing measurable. In game terrain reached 3.8-4.6 ms on open ground
(P46 log). Only lossy options remain (4F, or the existing settings).

### 3D. Fold the exposure pass (F6)

Move `Exposure.fx.hlsl`'s per-pixel formula (linearize, divide by `lumaDiff`, lerp by
`maxBrightness`, delinearize) into the two consumers of the exposed image: the tonemap shaders
`ISHDRBLENDINSHADERCIN.pso.hlsl` / `CINAM` (apply to the `DestBlend` read) and the first bloom
downsample pass (apply per fetched texel before averaging). Constants: `TESR_ExposureData` and the
average-luma sampler must be declared in both (check free registers and samplers). Order in
`RenderEffectsPreTonemapping`: average luma stays where it is; only the full-screen pass goes.
Not bit-identical: the old path rounds through an fp16 texture between delinearize and linearize,
the folded path does not, so the result is equal or slightly better. Treat as lossless after a
visual A/B. Watch `Chain end copy (HDR)`: removing a pass flips the pre-tonemap chain's parity; if
the copy appears, plan the pass count (third chain buffer) or leave the pass. **Estimate 0.12 ms
net.** Medium effort, fiddly.

### 3E. Remove the LDR seed copy (F7)

In `ProcessImageSpaceShadersHook`, when `DestinationTarget` is NULL (final output), pass an
NVR-owned `BSRenderedTexture` of the back buffer's size and format (create it with the game's own
`CreateBSRenderedTexture`, the function `CreateSaveTextureHook` wraps), so the game's tonemap
writes into a texture. Run the LDR chain in game-texture mode seeded from it and make the final
effect (or, when no effect renders, a single copy) write the back buffer. After the game's call,
check `GetRenderTarget(0)` once: if the game ignored the destination, log it and keep the seed
copy. **Estimate 0.11 ms.** Medium effort, some risk around the game's UI/present expectations.

### 3F. Depth resolve and combine trims (after 0.1; requires MSAA off)

- In third person `RenderWorldSceneGraphHook` clears Z and resolves an empty view-model depth,
  then `CombineDepth` merges it: skip the second resolve and make the combine a copy when
  `IsFirstPerson == 0`. Small.
- When the game's depth-stencil is already an INTZ texture (`TheTextureManager->DepthTextureINTZ`
  set in `RenderManager::ResolveDepthBuffer`, which happens with DepthResolve.dll-style setups and
  no MSAA), the view-model resolve and the combine can be one full-screen pass that reads the live
  INTZ surface (unbind it as DS during the pass) and the world copy. **Estimate 0.1-0.2 ms.**

### 3G. Merged depth+normals (after I3)

If I3 shows a rejected call, fix it (write masks `D3DRS_COLORWRITEENABLE1`, matching sizes,
format support for RT1) and re-enable `MergedDepthNormals` by default once the F10 `Depth combine`
timer reads above zero and AO/fog/god rays are unchanged. Saves the 0.18 ms normals pass.

### 3H. LDR chain fusions that stay exact

`Cinema` is per-pixel except chromatic aberration (three offset reads). When
`TESR_CinemaSettings.z == 0` it can be applied at the end of the previous pass (FXAA or DOF)
exactly; with aberration on it cannot (it would need three FXAA evaluations). `DitherBuster` and
`FXAA` are both neighbourhood filters and cannot be fused without changing the result.
`ImageAdjust` is already skipped when identity. Low value (0.1 ms, only for some settings).

### 3I. The game's own image-space pipeline (investigate only)

`Game image space` is 0.32 ms. With NVR bloom on, the tonemap ignores the vanilla bloom
(`TESR_BloomExtraData.x`), but the game still runs its HDR downsample/blur passes to produce it.
Find out what those passes cost (I1 will show them under "tonemap"/"vanilla") before considering
hooking them out; the game's eye adaptation and `Cinematic.y` may come from that chain.

### 3J. Interior object shading (new from the P40 split; measure first)

In the Rivet City hangar, NVR's object shaders (`SLS*` from `ObjectTemplate.hlsl`) took 3.97 ms of the
4.99 ms world scene. Interior objects are lit by the multi-light variants (`LIGHTS` 2/3/4/9) and by the
game's additive light passes (`ONLY_LIGHT` variants, SLS2037-2043, which redraw an object once per
extra group of lights), so the cost is PBR lighting times lights times passes.
1. No code: in the hangar, standing still, switch the PBR shaders off and on in the menu (live) with
   F10 running; the split then shows `SLS NVR` turning into `SLS vanilla` for the same view. That is
   NVR's extra cost over the game's shaders.
2. Small instrumentation: label `SLS` by template variant (base / `ONLY_LIGHT` light pass / `LIGHTS`
   count) instead of one family, to see whether the base passes or the additive light passes cost most.
3. Then look for exact trims in the per-light code (work repeated per light that does not depend on the
   light, early-outs for lights out of range) with a CPU model or GPU comparison test, before anything
   lossy.

**Step 2 built in P47.** F10 now labels `SLS` by group: `SLS 1-3 lights` (2000-2028), `SLS 4+ lights`
(2029-2036), `SLS light pass` (2037-2044, the additive passes), `SLS diffuse pt` (2045-2046),
`SLS specular` (2047-2056), `SLS other`. Earlier logs' `SLS` is the sum of these. Every 120 frames a
`SLS 4+ LIGHTS in use per draw` line gives, per shader, how many lights its draws used (read back from
c2.w / c3.w at the next bind in the same pass) and the share of light slots computed for nothing.

**Candidate 3J-a, decided by that line:** the 4+ light shaders compute every light slot and multiply the
unused ones by 0 (`(1 >= lightsUsed ? 0.0 : 1.0) * getPointLightLightingAtt(...)`). Skipping them with
`[branch]` on the same conditions was built and measured with `tests/game_shaders.cpp` (full screen,
2560x1440): 2 of 6 slots used -29% (SLS2029/2030), 2 of 4 -11% (SLS2031-2033), all slots used +2 to +4%
(branch overhead). Not bit-identical: the compiler fuses the final `* PI` into a multiply-add inside the
branches, so used lights differ by 1-4 float ulps (at most 5e-7, far below the fp16 scene buffer's
precision). Ship only if the in-game line shows a real share of wasted slots.
**P47 in-game result (user logs 2026-09-29 04:51 exterior, 04:54 Rivet City interior view, native):
3J-a REJECTED.** Interior: about 53 draws per frame use the 4+ light shaders, and only 3-4% of their light
slots are unused (SLS2034 3 of 3 slots x42/frame, SLS2031 2-4 lights, SLS2029 5-6 lights); outdoors
there was one such draw per frame. The branch overhead (+2-4% when all slots are used) would exceed that.
Interior world scene 7.15 ms by group (medians of 32 windows): `SLS specular` NVR 2.82, `SLS diffuse pt`
NVR 1.16, `SLS other` vanilla 1.15, `SLS light pass` NVR 0.98, `SLS 4+ lights` NVR 0.36, `SLS 1-3 lights`
NVR 0.35, rest < 0.1 (point cubemaps 0.74 GPU, shadow maps CPU 0.94). Exterior world scene 2.48 ms:
`SLS 1-3 lights` 0.99, sky 0.34, terrain 0.32 (short view), grass 0.31. So indoors the cost is the
game's extra lighting passes (specular + diffuse point + light passes = 4.96 ms), not the base passes.
Next (3J step 3): exact early-out per light where the pixel is outside the light's radius (vanillaAttSq
is exactly 0 there, so `att * X` adds +0); measure with `tests/game_shaders.cpp` (scene with lights
partly out of range) and in game against P47 in the same spot. Also find out which vanilla shaders make
up `SLS other` (names outside 2000-2056). Note: slot 3's condition
is `2 > lightsUsed`, the same threshold as slot 2 (`1 >= lightsUsed`), so a draw with 2 lights also adds
slot 3 (probably an upstream off-by-one; kept as is, the vanilla shaders were not checked).

---

## 7. Phase 4: opt-in lossy items (one `ReducedQuality` switch each, off by default)

- **4A. `NearCascadeInterval` (1-2).** Refresh the near sun cascade every second frame using the
  translation lock the middle/far cascades already use between refreshes
  (`ShadowManager.cpp`, `SunCascadeUpdatePeriod`/`Phase`). Moving casters lag one frame, exactly
  like `PointShadowInterval = 2`. Saves about half of `Sun cascade geometry` (0.42) and of the
  near-cascade share of `Shadow atlas prefilter` (0.36) plus CPU. **Estimate 0.3-0.4 ms average.**
  Small effort.
- **4B. `CheapReflections`** (3B).
- **4C. Shadow atlas advice**, no code: EVSM2 (`Mode = 1`) halves atlas bytes; document the trade
  (light bleeding) in the README once 0.7 has numbers.
- **4D. Interior shadow blur at half resolution** with a depth-aware upsample. The user said to skip
  this for now (P39). Keep the design note: the two 0.45 ms blurs are fetch-bound (24 fetches per
  pixel); half resolution cuts them to a quarter.
- **4E. Cubemap size by distance** (512 near, 256 far) and R16F faces (halves clear and write
  bytes; precision about one unit in a thousand of the radius, needs a bias check).
- **4F. `TerrainParallaxLite`** (3C).
- **4G. DitherBuster off with FXAA**: settings advice after 0.10.

---

## 8. Phase 5: CPU-side work (helps weaker CPUs and DXVK; invisible on the 9800X3D while GPU-bound)

- **5A. Upload frame-invariant TESR_ constants once per frame** (F2). Mark constants pinned at
  c100 and above (pixel) and the vertex-side equivalents as "frame invariant", upload them once
  after `ShaderManager::UpdateConstants` (before the world render, and again after NVR's own
  effects have run, since `EffectRecord::SetCT` through `ID3DXEffect` may write the same
  registers), and skip them in `ShaderRecord::SetCT`. Constants pinned below c100 (water, sky,
  tonemap) stay per switch. Lossless. Measure with I5.
- **5B. Sampler clearing.** Replace the sixteen `SetTexture(i, NULL)` per switch with clearing only
  the samplers an NVR shader set since the last clear (bitmask), or check whether
  `NiDX9RenderState::SetTexture` already skips redundant sets and leave it if so.
- **5C. Shadow-map per-object uploads** (F3): in `RenderPass::RenderAccum` upload the shared
  constants once per pass and only `TESR_ShadowWorldTransform`/bones/`ShadowData` per object.
- **5D. Shadow-map candidate cache.** `AccumExteriorCell` walks every reference of all loaded
  cells per cascade per frame. Cache per cell the references that pass `GetRefNode`'s form filter
  (node pointer, bound), rebuild on cell change or every 30 frames, and share the walk between
  cascades updated in the same frame. `Shadow maps (CPU)` avg 1.6 ms, max 5-6 ms.
- **5E. Per-frame heap allocations** in `ShadowManager::RenderShadowMaps` (`std::string message`
  per cascade/cubemap) and `EffectRecord::Render` (`std::string name`) when `logperf` is off.
- **5F. `GetNearbyLights`**: `std::map` per frame -> fixed array and sort.
- **5G. Sky SH projection** (`SkyShaders::UpdateConstants`, 512 samples per frame on the CPU):
  recompute only when the sun direction or sky colours change.

---

## 9. Considered and rejected or deferred (do not redo without new evidence)

- Contact-shadow blur skip mask (P39: bit-identical but slower).
- Fold exposure into the god-ray combine: the average-luma pass reads the scene after god rays, so
  the exposure value would change (image change).
- Fuse Cinema with chromatic aberration into FXAA: needs three FXAA evaluations per pixel.
- Light-volume rendering for the interior point-shadow pass: interiors are lit almost everywhere
  and the per-pixel branch already skips out-of-range lights.
- Depth pre-pass for the world scene: NVR does not control the game's draw order; high risk.
- Temporal AO or temporal anything: no motion vectors, ghosting.
- DXVK as the stutter fix: 35% slower on this GPU (P38 A/B); revisit only if 0.8 proves recurring
  driver compiles and 2A fails.
- Half-resolution contact-shadow march: the two blurs are two thirds of the cost.
- Bloom passes 8 -> 6: the small passes are a few microseconds.
- Bilinear tap pairing in the depth-aware blurs: the per-tap depth rejection makes pairing inexact.
- Removing the `Normals` pass by reconstructing normals in every consumer: trades bandwidth for
  ALU in AO/contact shadows; not clearly a win, revisit only if I3 fails.
- 5A / 5B (per-switch constant uploads, sampler clearing): P40 measured only 27-46 shader binds per
  frame outdoors and 231 in the hangar, and 0.06-0.09 ms of CPU for all of `SetShadersHook`.
- 2A shader warm-up: parked. In P40 the steady-play spikes had no first shader use and no stall
  between binds; revive only if a cold-driver-cache test shows first uses inside spikes.

---

## 10. Per-item checklist and session duties

For every item shipped:
1. State the expected gain as an estimate; report the measured medians after the user's log.
2. Same-image items: a GPU or CPU comparison test in `tests/` or `outputs\_superseded`, or a
   written argument why it is identical by construction (and a visual A/B by the user).
3. Image-changing items: a `ReducedQuality` switch, default off, honest toml comment, README line,
   `docs/UNOFFICIAL-TECHNICAL.md` paragraph.
4. Kill switch or fallback path for anything that touches the game's render loop.
5. `tools/test-local.ps1` passes; changed effects compile with `ao_device.exe --compile-files`
   (D3DX43) and `fxc /T fx_2_0`; changed game shaders with `fxc /T ps_3_0`/`vs_3_0` and the
   template defines.
6. Package with exact install paths; log markers `UNOFFICIAL ...` for new paths.
7. Update `HANDOFF.md` (status, measurements, what to ask the user next) and this file (move
   items to "done" with their measured numbers, or to section 9 with the reason).

Suggested order (revised after P47): 3C and 2C are closed (measured, nothing to gain). Next: the P47
interior + exterior F10 run decides 3J (which SLS group costs most; 3J-a if the 4+ light draws waste
slots), then 3J step 3 on the costliest group with `tests/game_shaders.cpp`, then 3D/3E.

Previous suggested order (after the P40 logs): the exterior and interior PBR A/Bs are done; the targeted
terrain parallax off/on A/B standing still outdoors remains for 3C -> P41: 3A cached
static cubemaps (still the largest lossless item: 1.32 ms GPU + 1.30 ms CPU in the hangar), 2C logger
buffering (cell-change hitch), 3J step 2 (SLS split by variant) -> 3J step 3 / 3C depending on the
A/Bs -> 3D/3E. The Phase 0 restart tests (MSAA 0, iWaterMultisamples 0, NVIDIA shader cache) remain
useful whenever the user has time.
