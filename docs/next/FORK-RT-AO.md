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
