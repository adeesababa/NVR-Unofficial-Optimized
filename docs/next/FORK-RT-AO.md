# Fork RT item 2: ray-traced ambient occlusion and sky visibility (2026-10-28)

Branch `claude/rt-ao-sky-9f04fd` (cut from `claude/p84v3-tessring` 03b62a5a, the installed build). Part of the hybrid ray-tracing
plan: item 1 (rt-sun-core) builds the shared acceleration structures and the ray-query entry point (`docs/next/FORK-RT-CORE.md`);
this branch only adds the AO/sky passes, their settings, the bench list and this note, written against that core.

## Status
- DONE: settings, four shaders (compiled and validated as SPIR-V 1.5), the NVR wiring into AmbientLight and AmbientOcclusion, the bench
  list, this note. The DLL builds (see "Build / files").
- WAITING: the RT core. Until its branch is merged, `RtAmbient::CoreReady()` (src/core/RtAmbient.h) answers false, so the feature is
  detected off everywhere and every pass is exactly the old one. The core-dependent part is three small functions in that header
  (CoreReady, TlasBinding/TlasResource, Origin): the rebase onto the core replaces those bodies and nothing else.
- NOT DONE: the iGPU bench run (correctness shots and cost per ray count). It needs the core.

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
| Enabled | false | on: needs the NVR DXVK with the RT core, NvrDxvkPasses on, a GPU with ray queries; else logged once and nothing changes |
| RayCount | 4 | [1-16] AO rays per half-resolution pixel and frame |
| Distance | 120 | [8-1000] AO ray length, game units (~70 = 1 m) |
| Sky | true | the sky visibility from rays too (Skylighting must be on, as for the ortho version) |
| SkyRays | 16 | 8 (one ring at 60 degrees + straight up) or 16 (+ a ring at 30 degrees: the ortho version's two rings) |
| SkyDistance | 800 | [64-8000] sky ray length (Skylighting's Distance) |
| DebugView | 0 | 1 the raw RT visibility (AmbientLight's occlusion view is switched on for it); 2 the RT sky visibility (through Skylighting's view 1); 3 black where rays began inside something, else grey by the mean hit distance |
The strength, darkest value, brightness fade, draw distance (AmbientLight) and StrengthMultiplier / ClampStrength / fog and far fade
(AmbientOcclusion), and every Skylighting setting (Strength, OpenThreshold, ShareApply, AmbientScale, LightCap, OpenSides is not used:
rays see the open sides themselves) stay theirs, mapped the same way as for the screen estimate, so A/B is one switch.

## How (passes; F10 lines)
AmbientLight (the user's AO; AmbientOcclusion is off while AmbientLight renders):
1. Prepare, NormalSmoothing: unchanged.
2. NEW `AmbientLightEstimateRT.frag` in Estimate's place (`  Ambient light RT rays (half)`): per half-resolution texel the prep's view
   depth and smoothed normal, to camera-relative world (the view's rotation), RayCount rays (`RtCommon.glsl RtOcclusion`: Malley
   cosine-weighted directions, R2-stratified, rotated by the blue noise, moved each frame by the golden ratio while Accumulate keeps a
   history), closest hit, a hit counts fully up to Distance/2 and fades to nothing at Distance. Output as Estimate's: rgb 0, a =
   pow(visibility, 1.5 x OcclusionStrength) faded by the near/far fade.
3. Blur X / Y (edge-aware, depth-weighted): unchanged = the spatial denoiser.
4. Accumulate (reprojection with the double-precision camera move, 4-texel depth test, neighbour clamp, 0.9 history share less the
   motion): unchanged = the temporal denoiser. With 4 rays and a 0.9 share the history averages ~10 frames = ~40 rays per texel.
5. Apply: unchanged.
ComputeGI keeps the screen search (its bounce light comes from it); with ComputeGI on, the RT estimate is not used.

AmbientOcclusion (when AmbientLight is off):
1. NEW `AmbientOcclusionEstimateRT.frag` (`  AO RT rays (half)`) into AO target 1: the same rays; output (ao, depth) with GTAOValue's tail
   (StrengthMultiplier, fog, far fade).
2. NEW `AmbientOcclusionTemporalRT.frag` (`  AO RT temporal (half)`): AmbientLight's Accumulate rule on AO's targets: AO 1 + last
   frame's NVR_AORTHistory -> AO 0 + this frame's history (two targets, G16R16F, made at the AO target's size on first use).
3. Blur X / Y and Combine: unchanged.

Sky (Skylighting v2, hosted by AmbientLight or AmbientOcclusion as today):
- NEW `AmbientOcclusionSkyRT.frag` in SkyVisibilityV2's place, same target and output (factor, depth, visibility). Directions fixed per
  texel (straight up for 'covered', then one or two rings of 8 weighted by their cosine to the normal, as the ortho version), turned
  by a 4x4 per-texel interleave that does not change over time: no grain that moves, so no temporal pass; the composites' depth-aware
  upsampling averages the interleave. While the RT sky runs the ortho map is not requested (ShadowManager skips its amortised redraws).
Failure rule: any RT pipeline or draw failure is logged once and turns the RT path off for the session; the other Vulkan passes are
untouched (ExtFailed is put back), NVR's own estimate/sky pass runs.

## Choices (why)
- Rays from the depth buffer, not a primary ray: the G-buffer's position and normal are what the screen AO uses, so the A/B differs only
  in the occlusion test. Ray start = position + normal x (0.75 + 0.003 x view distance) units; back-face hits within 4 x that bias are
  rays that began inside something (depth error at half resolution, or tessellated land bumps the TLAS does not have): they count as
  open (no false black) and DebugView 3 shows where.
- Closest hit (not first-found) for AO: the distance fade needs the real distance; the sky only needs any hit (TerminateOnFirstHit).
- Everything opaque (`gl_RayFlagsOpaqueEXT`): alpha-tested foliage counts as its whole cards until the core offers alpha (a request to
  it, not a local copy). Expect leaves to occlude a little too much.
- Denoise = NVR's existing passes (blur + Accumulate) instead of a new denoiser (SVGF/A-trous): the RT estimate is one more Estimate, so
  every view, fade and history rule the user knows stays the same. If 4 rays + history is too grainy on motion, the next step is a
  variance-guided blur width (cheap), not a new pipeline.
- Sky with fixed directions: the ortho version's look (its two rings) with real occluders, no noise to filter.

## Build / files
- DLL: VS2019 (v142) Release Win32 builds clean, no new warnings (2026-10-28, SHA256 5F60CB2A...). With the core missing it behaves exactly
  as 03b62a5a (the RT setting off or on).
- New SPIR-V (compile: `glslangValidator -V --target-env vulkan1.2`; tools/nvr-dxvk/compile-compute.sh now picks Vulkan 1.2 for files
  that use GL_EXT_ray_query): AmbientLightEstimateRT.spv, AmbientOcclusionEstimateRT.spv, AmbientOcclusionTemporalRT.spv,
  AmbientOcclusionSkyRT.spv -> `Data\Shaders\NewVegasReloaded\Compute\`. RtCommon.glsl is their shared include.
- Bench list: `work\bench\foundations\lists\rt-ao-1.txt` (2026-10-03 work root): AL GTAO vs RT occlusion views, raw/inside views,
  RayCount 1/2/4/8/16, Distance 250, ortho vs RT sky, AmbientOcclusion path.

## Results
Not run yet (needs the RT core). To fill: per scene (lucky22 interior, og-desert, a porch / overhang exterior) the occlusion views side by
side, the inside view (should be nearly empty), the sky views, and the F10 cost per ray count on the iGPU at 1280x720.

## Open / next (session-hours)
- Rebase onto the core, fill the three RtAmbient functions, rebuild (0.5-1 h once the core's API is fixed).
- iGPU bulk run of rt-ao-1 in lucky22 + og-desert + a porch spot, read the shots, cost table (1-2 h incl. queue).
- Tune Distance / the strength mapping against AmbientLight's look (RT occlusion is physically larger: AL's GTAO under-darkens wide
  creases) (1 h).
- Requests for the core if the shots show them: skinned actors in the TLAS (contact darkening under NPCs), alpha-tested foliage.
