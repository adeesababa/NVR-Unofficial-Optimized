# Fork RT item 2: ray-traced ambient occlusion and sky visibility (2026-10-28)

Branch `claude/rt-ao-sky-9f04fd` (cut from `claude/p84v3-tessring` 03b62a5a, the installed build). Part of the hybrid ray-tracing
plan: item 1 (rt-sun-core) builds the shared RT core (`docs/next/FORK-RT-CORE.md`); this branch adds only the AO/sky trace shader,
NVR's passes around it, the settings, the bench list and this note, written against that core.

## Status
- DONE: settings; the helper trace shader `RtAOTrace.comp` (SPIR-V 1.5, validated); NVR's input pass and the three passes that turn
  the traced values into AmbientLight's / AmbientOcclusion's estimate and Skylighting's sky factor (validated); the AmbientOcclusion
  temporal pass; the wiring in AmbientLight.cpp / AmbientOcclusion.cpp; the bench list. The DLL builds.
- WAITING: the RT core. Until its branch is merged, `RtAmbient::CoreReady()` / `Trace()` (src/core/RtAmbient.h) answer false, so the
  feature is detected off everywhere and every pass is exactly the old one. Those two functions (and `Origin()`) are the only code that
  depends on the core: the rebase replaces their bodies with calls to its `RtCore` class.
- NOT DONE: the iGPU bench run (correctness shots, cost per ray count). It needs the core.

## Why a helper process (rt-sun-core's finding, 2026-10-28)
AMD's 32-bit Windows Vulkan driver (the iGPU, driver 25.10.36) offers no VK_KHR_acceleration_structure / ray_query to a 32-bit process;
the 64-bit driver does. FNV and the fork's d3d9.dll are 32-bit, so no NVR Vulkan pass can trace. The core moves the BLAS/TLAS and the
trace passes into a 64-bit helper (NvrRtHelper.exe, like NvrDlssHelper) that shares images and timeline semaphores with the game (fork
patch 0026). My first version (2d7787dc) traced inside NVR's fragment passes; it was reworked into one helper compute pass + NVR passes
that only read its output (this commit). Notes: side-session-notes `RT-CORE-HELPER-2026-10-28.md`.

## What it does (plain)
- AO: a few short rays per pixel (half resolution), cosine-weighted round the surface's normal, Distance long, traced against the real
  scene. Where the screen-space AO guesses from the depth buffer (it cannot see what is off screen, behind the first surface, or how
  thick a thing is), the rays know: no light leaks through thin walls, no dark halo round things in front, the underside of a car or a
  table is dark because the car is there even when its body is off screen.
- Sky: Skylighting's sky visibility from rays instead of the top-down ortho map: overhangs, bridges, porches and the inside of buildings
  block the sky by their real shape, wherever the map's top-down view fails (open-sided canopies, things under other things).
- Both feed exactly where NVR's AO and sky light apply today: AmbientLight's occlusion (its Apply, as Estimate's result), AmbientOcclusion's
  occlusion (its combine), and the sky factor (AmbientLight's Apply and the fog's composites, through Skylighting v2's own target).

## Settings `[Shaders.AmbientOcclusion.RT]` (all live; defaults in resource/NewVegasReloaded.dll.defaults.toml)
| key | default | meaning |
|---|---|---|
| Enabled | false | on: needs the NVR DXVK, NvrDxvkPasses on, the RT core's helper and a GPU with ray queries; else logged once and nothing changes |
| RayCount | 4 | [1-16] AO rays per half-resolution pixel and frame |
| Distance | 120 | [8-1000] AO ray length, game units (~70 = 1 m) |
| Sky | true | the sky visibility from rays too (Skylighting must be on, as for the ortho version) |
| SkyRays | 16 | 8 (one ring at 60 degrees + straight up) or 16 (+ a ring at 30 degrees: the ortho version's two rings) |
| SkyDistance | 800 | [64-8000] sky ray length (Skylighting's Distance) |
| DebugView | 0 | 1 the raw RT visibility (AmbientLight's occlusion view is switched on for it); 2 the RT sky visibility (through Skylighting's view 1); 3 black where rays began inside something, else grey by the mean hit distance |
The strength, darkest value, brightness fade, draw distance (AmbientLight), StrengthMultiplier / ClampStrength / fog and far fade
(AmbientOcclusion), and Skylighting's Strength, OpenThreshold, ShareApply, AmbientScale, LightCap stay theirs, mapped the same way as for
the screen estimate, so A/B is one switch. (Skylighting's OpenSides is not used: rays see the open sides themselves.)

## How (per frame; F10 lines)
`AmbientOcclusionEffect::RtRaw`, once per frame for whichever pass asks first (the sky pass runs first, then AO or AmbientLight):
1. NEW `RtAOInput.frag` (`  AO RT input (half)`): per half-resolution texel the view distance (0 past 8000: no rays) and the world
   normal (camera-relative world, octahedral + Pack2 as AmbientLight's prep) -> NVR_RTAOInput (G32R32F).
2. The core: NVR_RTAOInput shared to the helper, which runs NEW `RtAOTrace.comp` (`  AO RT trace (helper, wait + copy)` times NVR's
   side) -> NVR_RTAORaw (A16B16G16R16F): x AO visibility (cosine-weighted, raw), y sky visibility, z 'covered' (straight up blocked),
   w DebugView 3's value. Constants (`RtAmbient::TraceConstants`): view->world rotation, 1/P00, 1/P11, grid size, TLAS origin + frame
   number, AO distance/rays, sky distance/rays (sky rays 0 while Skylighting is off).
   - AO rays: Malley cosine-weighted directions, R2-stratified, rotated per texel by interleaved gradient noise (no noise texture in the
     helper) and moved each frame by the golden ratio (both consumers keep a history). Closest hit; a hit counts fully to Distance/2 and
     fades to nothing at Distance. Start lifted by 0.75 + 0.003 x view distance units along the normal; a back face within 4 x that is
     a ray that began inside something (half-resolution depth error, tessellated bumps the TLAS does not have): open, and DebugView 3.
   - Sky rays: straight up ('covered') + one or two fixed rings of 8 (60 / 30 degrees up) weighted by their cosine to the normal, any hit
     within SkyDistance blocks; turned by a 4x4 per-texel interleave that never changes with the frame (no flicker standing still).
AmbientLight (the user's AO; AmbientOcclusion is off while AmbientLight renders):
3. NEW `AmbientLightEstimateRT.frag` in Estimate's place (`  Ambient light RT estimate (half)`): a = pow(raw visibility, 1.5 x
   OcclusionStrength) with Estimate's near/far fade, rgb 0, exactly Estimate's output.
4. Blur X / Y (edge-aware) = the spatial denoiser; Accumulate (reprojection, depth test, neighbour clamp, 0.9 history) = the temporal
   one: with 4 rays a texel averages ~40 rays over ~10 frames. Apply unchanged. With ComputeGI on, the screen search stays (its light).
AmbientOcclusion (when AmbientLight is off):
3. NEW `AmbientOcclusionEstimateRT.frag` (`  AO RT estimate (half)`) into AO target 1: GTAOValue's tail (StrengthMultiplier, fog, fade).
4. NEW `AmbientOcclusionTemporalRT.frag` (`  AO RT temporal (half)`): Accumulate's rule on AO's (ao, depth) targets: AO 1 + last
   frame's NVR_AORTHistory -> AO 0 + this frame's history (G16R16F pair at the AO target's size, made on first use).
5. Blur X / Y and Combine unchanged.
Sky (Skylighting v2, hosted as today): NEW `AmbientOcclusionSkyRT.frag` in SkyVisibilityV2's place, same target and output (SkyFactor of
the traced visibility and 'covered', depth, visibility). While the RT sky runs the ortho map is not requested.
Failure rule: a missing .spv or a failed RT pass is logged once and ends the RT path for the session; the other Vulkan passes are
untouched (ExtFailed is put back) and NVR's own estimate / sky pass runs. No result from the helper in a frame = NVR's passes that frame.

## Choices (why)
- One trace pass for AO and sky: one input, one output, one round trip to the helper per frame; the AO path and the sky path read the
  same texture. The helper only traces raw values; every NVR setting is applied in NVR's passes, so tuning needs no helper change.
- Rays from the depth buffer (not primary rays): the A/B against the screen AO then differs only in the occlusion test.
- Everything opaque (`gl_RayFlagsOpaqueEXT`): alpha-tested foliage counts as whole cards until the core offers alpha (a request to it).
- Denoise = NVR's existing passes, not a new denoiser (SVGF/A-trous): every view, fade and history rule the user knows stays the same.
  If 4 rays + history are too grainy in motion, next step is a variance-guided blur width.
- Latency: if the core returns the helper's result one frame late, the occlusion lags a frame; Accumulate's reprojection hides most of
  it (to check in the bench: fast turns, DebugView 1).

## Build / files
- DLL: VS2019 (v142) Release Win32, `/m:2` (the machine ran out of RAM with full parallelism), no new warnings, SHA256 077BD420...
  With the core missing it behaves exactly
  as 03b62a5a (the RT setting off or on).
- SPIR-V -> `Data\Shaders\NewVegasReloaded\Compute\`: RtAOInput, AmbientLightEstimateRT, AmbientOcclusionEstimateRT,
  AmbientOcclusionTemporalRT, AmbientOcclusionSkyRT (.spv, Vulkan 1.1, NVR's passes) and RtAOTrace.spv (Vulkan 1.2, ray query: the
  helper loads it by name). tools/nvr-dxvk/compile-compute.sh picks Vulkan 1.2 for files that use GL_EXT_ray_query.
- Bench list: `work\bench\foundations\lists\rt-ao-1.txt` (2026-10-03 work root): AL GTAO vs RT occlusion views, raw/inside views,
  RayCount 1/2/4/8/16, Distance 250, ortho vs RT sky, the AmbientOcclusion path. Harness: SLOT_DXVK_DEVICE / SLOT_D3DDEVICE =
  "AMD Radeon(TM) Graphics", SLOT_RES=1280x720, BENCHQ_SOLO=1, FSR off in the run toml.

## Results
### Run 1 (2026-10-28 13:37 lucky22 slot A, 13:44 og-desert slot C; list rt-ao-1, DLL 0AABEA2D, fork-0028, helper protocol 2)
- The chain works end to end on the iGPU: helper ready in 0.9-1.4 s, "RT AO: traced (640x360, 4 AO rays of 120 units)", the shared
  images 640x360 padded to 768x512 (RG32F in, RGBA16F out), no errors, no fallback. Lucky 38: 265 geometries (279k triangles, 43 MB),
  2677 instances; helper GPU time for the AO pass alone 0.30 ms (frames to 600) / 0.59 ms (to 1200, RayCount 8-16 shots) at 640x360,
  response (NVR's signal -> the helper's result) 1.9 ms. Structure builds 45 ms once at load.
- NOT a valid comparison: (1) the run toml (a copy of rt-sun's) has AmbientLight OFF, so the AmbientOcclusion path ran; (2) the list
  put each shot's settings before its `shot` line, but the harness applies the lines AFTER a shot to it: every label is one off;
  (3) lucky22's camera changes after shot 07 (third person, other view), og-desert's light changes between every shot (mean -20/255
  settle -> base): only lucky22 shots 00-07 compare.
- What 00-07 show: with RT on, the frame differs from the SSAO base only by the SSAO's darkening going away (+0.5/255, 15 % of pixels
  > 2/255); the raw view (DebugView 1) and the hit-distance view (3) equal it: the traced values read "open, nothing hit" everywhere,
  RayCount 1/2/4 alike. Suspect: the input (view distance / normal) or the ray origin. Run 2 checks the input directly.

### Run 2 (2026-10-29 04:05-04:35, lucky22 slot B; lists rt-ao-2 + rt-ao-1 v2; DLL C9A2F528 = rt-sun-core 4ba2abfd's Eye() fix + this branch)
- **Rays hit.** With the core's origin fix (rays from RtCore::Eye(), not CameraLocation - CameraPosition) the RT occlusion view shows
  real contact darkening: the foot of the vases, the wall-floor seam, under the slot machine rows and the counter, the inner corners of
  the pillars; open floor stays white. No visible leaks through walls, no missing objects among the static geometry (the player, an
  actor, is not in the core's scene: no darkening under him, as expected).
- Input checks: DebugView 4 (view distance) is a clean depth ramp (the input arrives); DebugView 3 (inside / hit distance) has no
  black: almost no ray starts inside geometry with the 0.75 + 0.003 x distance start lift.
- Same-spot A/B against AmbientLight's GTAO is NOT possible in lucky22: the third-person camera moves between shots (the player walks),
  so each shot shows another view. Needs a scene with a still first-person camera (next run).
- Cost on the iGPU (2 CUs, 1280x720, AO at 640x360, Lucky 38: 2677 instances / 265 geometries / 279k triangles):
  - NVR's side: RtAOInput 0.63 ms + AmbientLightEstimateRT 0.32 ms, in place of AmbientLight's GTAO estimate 2.33 ms.
  - The helper's trace pass: 9.3 ms a frame averaged over frames 1-600 (RayCount 1-4 shots), 14.0 ms over 600-1200 (RayCount 8, 16,
    Distance 250 and the sky shots); the core's "Ray tracing (copies + helper + waits)" timer in steady state: about 4 ms (1 ray),
    6 ms (2), 9.5 ms (4), 18 ms (8-16), 22 ms (Distance 250). Per-shot attribution of these profile lines is approximate (they print
    at the shot changes); a clean per-ray-count table needs one long still shot per count.
  - Read: the cost follows ray count x ray length; on this iGPU the RT AO is ~4x the GTAO it replaces. Expected ratio on a real RT
    GPU unknown (the iGPU only gives relative cost).

### Run 3 (2026-10-29 04:32-04:44, og-desert slot C, rt-ao-1 v2): stopped by the 480 s self-test limit after 8 shots
- The iGPU runs ~5 fps outdoors (the core's "Ray tracing" timer 24-27 ms avg, min 3.4-5.5 ms; helper 562 instances / 156 geometries /
  295k triangles); 21 shots x 120 frames do not fit. Not a crash. The sky shots were after the cut: list rt-ao-3 (60 frames a shot,
  8 shots) covers them.
- What the 8 shots show: open desert ground stays white (correct: nothing above it); the inside view is white everywhere (no ray starts
  inside geometry). BUT rt-sun-core's rtsun-5 found **land is not in the core's scene yet**: rays never hit the terrain, so objects
  standing on it get no ground contact and cliffs / hills occlude nothing. Outdoor RT AO is incomplete until the core adds land.
- Same-spot A/B still missing: the third-person camera moves between shots here too.

### Run 4 (2026-10-29 05:22-05:28, og-desert slot C, rt-ao-3; still without land in the scene)
- Completed (60 frames a shot fits the limit). Cost lines in steady state: RT AO 1 ray, no sky rays: "Ray tracing (copies + helper +
  waits)" 1.47 ms, AO RT input 0.39 ms, AmbientLight RT estimate 0.25 ms; 4 AO rays + 16 sky rays: 10.5 ms; Skylighting's ortho
  sky pass for comparison 1.46 ms. Helper at exchange 60: passes 12.7 ms (4 AO + 16 sky rays, 640x360).
- The sky comparison is NOT readable from these shots: Skylighting's DebugView 1 does not draw a grey view while AmbientLight hosts the
  sky test (the factor goes into AL's apply), the time was dusk and the camera moves between shots. Needs a sky view through
  AmbientLight (open item).
- Next: rt-ao-4 (og-desert, land now in the scene via rt-sun-core 28ea9e77, build 1852AA82): the occlusion view at 1 / 4 / 16 rays.

## Strand 2026-10-29 afternoon: AO -> shared denoiser -> GI (branch claude/rt-ao-gi from claude/rt-test-merge af12c7e7)
### Step 1, rays hit (verified, numbers)
Share of on-screen surface pixels whose raw RT visibility (RT DebugView 1, through AmbientLight's occlusion view) is below open (< 0.95):
| run | scene | rays hitting | rays starting inside (DebugView 3 black) |
|---|---|---|---|
| run 1 (10-28, before the Eye() fix) | lucky22 | ~0 % | n/a (everything read open) |
| run 2 (10-29 04:28) | lucky22 | 53 % | 0.2 % of pixels below half |
| run 5 (10-29 05:54, land in the scene) | og-desert | 47 % | none on surfaces |
The every-ray-misses bug is gone in all three test views (raw, inside, input). Land: run 5 had the loaded cells' land (28ea9e77); og-lake
style spots with LOD-only ground get it from ecf5defe (in this branch's base).

### Step 2, shared denoiser
Asked rt-sun-core (10-29 ~16:10) for its API. Until it lands, AO keeps AmbientLight's blurs + Accumulate and GI keeps ComputeGI's
accumulate (below): both are temporal + edge-aware spatial already.

### Step 3, one-bounce RT GI (built 92a5792d, DLL F3A8C42C; bench rt-gi-1 queued in lucky22 + og-desert)
- `[Shaders.AmbientOcclusion.RT] GI` (off), `GIDistance` 1500, `GIStrength` 1.0, DebugView 6 = the bounce light alone. AmbientLight only.
- RtAOTrace.comp: one cosine ray per half-resolution pixel and frame (its own noise, decorrelated from the AO rays), closest hit, up to
  GIDistance. At the hit: albedo from the core's per-instance record (the draw's diffuse texture averaged; grey 0.5 if unknown), the
  normal from the hit triangle (core v2 geometry data, to the scene by the instance transform, turned to the ray), lit by the sun (N.L
  and a shadow ray; outdoors while the cascades' fade is below 1), the ambient (x (1 + n.z) / 2) and the 8 lamps nearest the eye
  (ShaderManager's shadow-slot and tracked lamps, quadratic falloff to their reach, no shadow ray). Output albedo x E (the pixel's
  irradiance estimate with cosine sampling), clamped to 4; misses give 0 (the sky's light is the game's ambient already). Second
  shared image NVR RT gi (RGBA16F) -> NVR_RTGIRaw.
- AmbientLight: the RT estimate writes the bounce into its rgb (as ComputeGI's GI pass does), sets giDrawn, so ComputeGI's
  accumulate (temporal: reprojection, depth test, neighbour clamp) and its apply (x the surface colour x TESR_AmbientLightData.z =
  GIStrength) take it. ComputeGI on = its screen search wins (A/B).
- **rt-gi-1 (10-29 16:35 lucky22 slot A, 16:44 og-desert slot C; DLL F3A8C42C).** GI traced in both (Lucky 38: 8 lamps, indoors;
  desert: sun up, 0 lamps). Lucky 38 held a still camera this time (al vs al-end: 0.2 % of pixels differ = noise), so it is a valid
  same-spot A/B (the desert's light still changes between shots: not comparable). Same spot, mean picture level 0-255:
  AmbientLight 31.5 -> RT AO 33.6 (+2.2, lighter on 24 %: less occlusion than AL's GTAO overall) -> RT AO + GI 41.9 (+8.3 on 74 % of
  pixels = +25 %); GI x2 47.7; ComputeGI (screen) 31.9 (-1.7 vs RT AO). The RT bounce light alone (DebugView 6) was an even red-orange
  wash over the whole room: nearly every bounce ray hits a wall or ceiling and returned albedo x the game's AMBIENT, i.e. a second
  copy of the ambient light, which the game already uses for indirect light. Fix ac0598e2: GIAmbient (default 0) = only the sun and
  the lamps bounce. Cost (core's "Ray tracing" line): Lucky 38 AO 4 rays + GI ~16 ms (AO alone ~9.5 ms earlier) on the iGPU; desert
  ~4.8 ms (AO + GI, sun shadow rays). AmbientLight's GI accumulate 0.7-0.9 ms.
- Cull mask 0xEF on every ray (ac0598e2): leaves out 0x10, the core's alpha-tested casters (claude/rt-sun-2, off by default), which
  would otherwise block as solid cards. Testing their alpha (RtAlpha.glsl) for the AO and sky rays is the next foliage step.
- **rt-gi-2 (10-29 17:11 lucky22, DLL 315CD821, GIAmbient 0):** still +7.8/255 on 71 % (vs +8.3 with the ambient): the wash comes from
  the LAMPS: the 8 nearest lit every hit unshadowed, through walls and ceilings. RT cost 14.9 ms avg (AO 4 + GI) on the iGPU.
- **Fix a0b05c51 (DLL 59DC7BCA): lamps by resampled importance sampling** (ReSTIR's direct-light step without the reuse): at a hit one
  lamp is picked in proportion to its unshadowed light (N.L x quadratic falloff to its reach), its light / its chance, and ONE shadow
  ray to it (stopping 16 units short of the lamp so its own fixture does not block it). Unbiased; the GI accumulate averages the pick.
- **Alpha-tested casters (merged claude/rt-sun-2 94998464):** every AO / sky / GI ray now takes them (cull 0xFF) and confirms their
  candidate hits only where the texture's alpha passes (Compute/Rt/RtAlpha.glsl, the core's atlas on binding 5; a 256x256 R16F
  stand-in when [Main.Main.RayTracing] AlphaTested is off and there is no atlas). Needs fork patch 0029 when AlphaTested is on.
- Queued: rt-gi-3 (lucky22, rt-gi-1 list, fork-0029) and rt-ao-5 (og-goodsprings, trees, AlphaTested on via
  bench/deep-roadmap/rtao/run-toml-igpu-alpha.toml).
- **rt-ao-5 (10-29 17:53 og-goodsprings slot C, AlphaTested on, fork-0029, DLL 59DC7BCA):** ran clean (684 instances, 234 geometries,
  816k triangles; RT AO 4 rays: core line 6.0-6.4 ms). Foliage NOT verified: rt-sun-core's own rtsun-11 at the same spot found the core
  takes only ~5 alpha-tested meshes a frame (the cause is the core's; counters coming). My rays' alpha test waits for that.
- **rt-gi-3 (10-29 18:08 lucky22, DLL 59DC7BCA, RIS shadowed lamps):** +7.1/255 on 67 % (v1 +8.3, no-ambient +7.8): shadowing the lamps
  barely helped, so the level, not the leaks, was wrong. Measured: the RT bounce light (view 13) has mean linear 0.026 vs ComputeGI's
  0.0009 (29x) and vs the frame itself 0.015: the analytic shading at the hit (lamp colour x quadratic falloff x albedo) is not in the
  game's light units. RT cost (core line) 20.7 ms avg, min 11 ms (AO 4 + GI 1 + its shadow rays) in Lucky 38 on the iGPU.
- **GI v2 (7e8f80e7, DLL 8F014ACC):** a hit the screen saw takes the light the game drew there: the hit is projected back into the
  view, and where the input's view distance there matches (5 %, at least 8 units) the lit frame (StretchRect of the render target at
  the RT core's point: the world is drawn, NVR's effects not yet; to the trace grid, shared as NVR RT gi frame light) gives its linear
  colour, exactly the light ComputeGI's taps use. Hits off screen keep the analytic light x GIOffscreen (0.1 default; 0 = on-screen
  bounces only, i.e. ComputeGI's light with ray-traced visibility). Merged claude/rt-sun-2 ffa19075 (alpha atlas on the GPU: ~900
  alpha-tested meshes a frame at Goodsprings now really in the scene).
- **rt-gi-4 (10-29 18:33 lucky22 slot A, DLL 8F014ACC, GI v2): WORKS.** Same spot (al vs al-end 0.2 % = noise): RT AO 33.6 ->
  RT AO + GI 35.7 (+2.1/255 on 12.7 % of pixels, was +8.3 on 74 % in v1); GI x2 37.8. The bounce light (view 13) mean linear 0.0058
  (v1 0.026; ComputeGI's 0.0009): RT finds more bounce than the screen search (rays reach what the screen's slices miss), local and
  warm near the lit railing, the counter and the lamps, not a wash. Final picture natural. Flaw: sparse orange fireflies where a bounce
  ray lands on a lamp's glow on screen (the per-sample cap is 4.0: too high) -> next: a luminance clamp per sample. Cost unchanged:
  core line 21.3 ms avg, 11.0 ms min (AO 4 + GI 1 + its shadow rays) on the iGPU in Lucky 38.
- **rt-ao-6 (10-29 18:51 og-goodsprings slot C, AlphaTested on, DLL 8F014ACC):** foliage IS in the scene now: 54513 alpha-tested
  meshes taken in 60 exchanges, 46 atlas slots (rt-sun-core dc1a5dd4); the scene 1597 instances / 406 geometries / 1.36 M triangles /
  211 MB. RT AO (4 rays) core line 15-17 ms p50 (6 ms without the foliage, rt-ao-5): the alpha-tested candidates (any-hit tests per
  leaf card) roughly triple the AO cost on the iGPU. Visible occlusion from the (thin) Joshua trees in this view: not noticeable.
- **GIClamp + Interleave (commit after 7e8f80e7, DLL DE2DDFFB):** a bounce sample's luminance is capped at GIClamp (1.0 = the frame's
  white; keeps the hue) against the fireflies; Interleave 1 / 2 / 4 traces every / every 2nd (checkerboard) / every 4th texel (one of
  each 2 x 2, in turn) a frame, the rest keep their last result (the shared images persist) for AmbientLight's history to blend: the
  ray cost / 2 or / 4. Bench rt-gi-2 (lucky22) queued.
- Not yet: alpha-tested foliage and actors are not in the core's scene (no bounce from them); lamp shadows at the hit; ReSTIR GI or a
  probe cache (next, for cost) once the plain version's look and cost are measured.

## Resumed 2026-10-29 (main session, on the user's word)
- The every-ray-misses bug is rt-sun-core's to fix (its lead: the fork's StageGeometry conversion; rt-reflections' camera rays miss on
  100 % of pixels too, so the scene itself is wrong, not the passes' inputs). No bench runs here until it says rays hit.
- Done meanwhile (needs no hits): the RT sky's 4 x 4 interleave is now resolved in AmbientOcclusionSkyRT.frag (each texel averages
  the 4 x 4 traced texels round it, kept where the view distance matches within 5 % / 4 units: all 16 ring turns, no pattern, no
  bleeding across edges; NVR_RTAOInput bound as a 6th texture). Build DACEA5B7 (VS2019, clean).
- Bench: rt-ao-1 rewritten (v2: settings after their shot line; occlusion views first, inside the 8 shots where lucky22's camera
  holds), rt-ao-2 kept as the input check; run toml bench/deep-roadmap/rtao/run-toml-igpu.toml now has AmbientLight ON.

## Paused here (2026-10-28 ~15:15, on the user's word via the main session)
- State: branch claude/rt-ao-sky-9f04fd, built on the RT core (rt-sun-core d71ecf96); last DLL 669534FA (VS2019, clean). Off by
  default; with the core not running every pass is the old one.
- Open problem: run 1 traced but every AO ray reported "nothing hit" (lucky22, AO path). rt-reflections sees the same "no hit"
  with the same kind of input (640x360 RG32F padded to 768x512, depth-rebuilt positions + CameraLocation - CameraPosition), so it
  is probably shared (scene space or how small RG32F inputs arrive in the helper); rt-sun's working pass uses full-size R32F/RGBA16F.
- Next on resume: run list rt-ao-2 (lucky22; AmbientLight on; RT DebugView 4 = the view distance the helper got, 5 = the normal's
  up component) to tell an empty/garbled input from a ray-space mismatch; then rt-ao-1 again with AmbientLight on in the run toml
  and the shot lines fixed (settings FOLLOW their `shot` line). Queue with a retry on `RESULT: WAIT` (a low-memory WAIT drops the
  ticket). Bench files: work/bench/foundations/lists/rt-ao-1.txt and rt-ao-2.txt, run toml work/bench/deep-roadmap/rtao/.
t-ao-1.txt / rt-ao-2.txt, run toml workench\deep-roadmap
tao\.
- Nothing queued, no loop, no monitor running. To fill: per scene (lucky22 interior, og-desert, a porch / overhang exterior) the occlusion views side by
side, the inside view (should be nearly empty), the sky views, and the cost per ray count on the iGPU at 1280x720 (NVR's F10 lines + the
helper's own timing).

## Open / next (session-hours)
- Rebase onto the core, fill CoreReady / Trace / Origin from its RtCore API, rebuild (0.5-1 h once the API is fixed).
- iGPU bulk run of rt-ao-1 in lucky22 + og-desert + a porch spot, read the shots, cost table (1-2 h incl. queue).
- Tune Distance / the strength mapping against AmbientLight's look (RT occlusion is wider than AL's GTAO in creases) (1 h).
- Requests for the core if the shots need them: skinned actors in the TLAS (darkening under NPCs), alpha-tested foliage.
