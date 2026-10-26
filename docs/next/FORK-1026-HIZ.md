# NVR DXVK fork: Hi-Z pyramid for reflections and ambient occlusion (2026-10-26, branch deep/fork-hiz)

Overnight fork agent "Hi-Z SSR + AO" (brief: bench\deep-roadmap\OVERNIGHT-1026-FORK-AGENTS.md, patch number 0010).

## What was built
- `core/HiZ.{h,cpp}` + `Compute/HiZBuild.comp`: a hierarchical depth pyramid (`NVR_HiZ`, G32R32F, 7 levels: nearest and farthest
  linear depth per cell, the weapon left out), built in ONE compute dispatch per frame (16x16 workgroups, each reads a 128x128 block
  of TESR_DepthBuffer and writes all seven levels through shared memory; no second dispatch, no atomics). ShaderManager builds it right
  after depth + normals, and only when a reader wants it this frame. F10 timer `  Hi-Z pyramid`.
- `[Shaders.Reflections.Main] HiZ = false`: the Reflections trace (Vulkan pass) as a hierarchical ray march
  (`Compute/ReflectionsTraceHiZ.frag`): the ray is walked cell by cell in screen space; a cell that cannot hold a hit (the ray is in
  front of its nearest depth, or behind its farthest by more than the thickness, or it is all sky) is skipped whole and the walk climbs
  a level; where a hit is possible it goes down; on the finest level (the full-resolution depth) the trace's own hit rule and thickness
  apply at EVERY pixel the ray crosses, instead of at `Steps` samples + 5 bisection steps. Everything before and after the march (the
  roughness spread, cube / light grid / sky fallbacks, firefly cap) is the old trace's, line for line. F10 timer
  `  Reflections trace (half)` (new, both paths).
- `[Shaders.AmbientLight.Main] HiZ = 0`: AmbientLight's Estimate (Vulkan pass, `Compute/AmbientLightEstimateHiZ.frag`) reads its far
  horizon taps from a coarse pyramid level (Scalable Ambient Obscurance style: a tap 2^(HiZ+1)+ prep texels away reads level m) instead
  of the 16-byte half-resolution prep.
- Both need the NVR DXVK d3d9.dll and `[Main.Main.Performance] NvrDxvkPasses = true`; off (default), native D3D9 or stock DXVK: no
  pyramid is built and nothing changes. A missing .spv or a refused pipeline/dispatch turns only the Hi-Z paths off (logged once), never
  the other Vulkan passes.
- No DXVK change was needed: extension 1.4 already binds single mips as storage images and whole chains as sampled images, and
  `texelFetch` ignores the sampler's LOD clamp. Patch number 0010 is unused; the tests ran on the stock-plus-0006 d3d9.dll
  (`G:\_NVR-build-scratch\nvr-dxvk-hiz` = nvr-dxvk-0006's d3d9.dll + `dxvk.conf` with `d3d9.customVendorId = 1002` for the depth fix).

## Checked offline
A CPU model of the pyramid + walk (scratch hiz_model.py: floor, walls, boxes, a thin pole, 1920x1080) against a brute-force walk with
1/6-pixel steps and the same per-pixel rule: 247 of 250 rays find the same hit pixel (the three others: two thin crossings the brute
force's steps missed, one ray over the 96-cell limit); 16.5 cells visited per ray on average (the old trace: 24 + 5 depth reads).

## In-game results (slot C, 1920x1080, GTX 1070, fork d3d9.dll 0006 + customVendorId 1002: "Depth resolve: RESZ", real depth)
Runs (pictures H:\_NVR-autotest-shots\<run>, logs bench\autotest\<run>; diffs and sheets G:\_NVR-build-scratch\fork-hiz-diffs):
- fork1026-hiz-1 (first trace version): og-desert 20261026-092045-slotC (q1) and again 20261026-093120-slotC (1-hiz1), og-lake
  20261026-093120-slotC (2-hiz1), lucky22 20261026-093750-slotC. No crash, no fallback, every Hi-Z log line as expected.
- fork1026-hiz-2 (after fix 1 below): og-desert 20261026-101550-slotC (1-hiz2), lucky22 20261026-110454-slotC.
- Fix 2 (below, commit 2758406d) is built and compiled, NOT run in game yet (in-game testing was stopped by the coordinator).

F10 GPU timers, ms, mean of 3 ABAB shots per launch (600 frames each), Vulkan passes on, Reflections on with GroundSheen /
InteriorFloors 1:
| scene (run) | trace off -> HiZ | Hi-Z pyramid | Reflections (SSR) off -> HiZ | net (SSR + pyramid) | old trace in D3D9 |
|---|---|---|---|---|---|
| og-desert (hiz-1) | 0.608 -> 0.170 | 0.080 | 1.495 -> 1.068 | -0.35 | 0.491 |
| og-desert (hiz-1 again) | 0.589 -> 0.171 | 0.083 | 1.469 -> 1.048 | -0.34 | 0.500 |
| og-lake (hiz-1) | 0.295 -> 0.134 | 0.079 | 1.106 -> 0.942 | -0.08 | 0.286 |
| lucky22 (hiz-1) | 0.691 -> 0.287 | 0.085 | 1.703 -> 1.259 | -0.36 | 0.662 |
| og-desert (hiz-2, fix 1) | 0.588 -> 0.207 | 0.081 | 1.448 -> 1.060 | -0.31 | - |
| lucky22 (hiz-2, fix 1) | 0.683 -> 0.297 | 0.077 | 1.596 -> 1.176 | -0.34 | - |

Long rays (MaxDistance 12000 instead of 4000, hiz-1): the old trace costs +0.07 ms more, the Hi-Z walk the same as at 4000: range is
free. The pyramid itself: 0.073-0.085 ms (p50 0.073) at 1080p. Against the earlier finding "the plain SSR port is slower than D3D9":
with HiZ the trace is 0.29-0.35 ms faster than D3D9's own trace in the same launch.

Pictures:
- Off = the old picture: HiZ false runs exactly the old pipelines (no pyramid is built). lucky22 (static camera): D3D9 vs Vulkan-off
  11.2 % > 2/255 vs off/off noise 10.9 %; og-desert adjacent D3D9/off 17 % vs noise 26-30 % (the outdoor spots drift between shots).
- Fix 1 (commit 1e8979ea): the first walk tested every pixel from t = 2 and stopped on its own floor (the depth buffer's pixel steps
  put the ray just "behind" it), the hit then thrown away: lucky22's DebugView 2 showed almost no hits. Now a pixel whose surface
  faces along the ray is no hit and the walk goes on.
- After fix 1: og-desert: hits view off vs HiZ 7 % (noise 8 %), reflection-alone view 10.1 % (noise 10.1 %): same picture.
  lucky22: whole picture off vs HiZ 15.6 % (noise 9.3 %, signed +0.10), reflection-alone view 21 % vs 10 % noise: the far floor
  matches, but rays from the NEAR floor (bottom half of the screen, 3rd-person camera looking down) found no hit where the old trace
  finds the bar and the rail.
- Fix 2 (commit 2758406d, untested): cause = rays grazing a floor run out of cells (every coarse cell holds floor nearer than the ray,
  so the walk goes pixel by pixel and hit the 128-cell limit). Now after 64 cells the rest of the ray is searched exactly as the old
  trace does it (its samples beyond the point reached, its hit rule, its 5-step bisection), so the walk cannot find less than the old
  trace. Expected cost: a little more than fix 1 on near-floor rays only; to be measured.

AmbientLight HiZ (far taps on coarse levels): NO WIN. Estimate pass 0.157 -> 0.170 (og-desert), 0.310 -> 0.300 (lucky22, noise
+-0.02), HiZ 2 / 4 likewise; and the picture changes (signed +0.3..+0.8, the occlusion lighter: the cell's nearest depth puts far
samples out of reach). The 16-byte prep reads are not this pass's bottleneck. Left in, off by default; the better AO use of the
pyramid (a GTAO horizon search walking coarse levels) is not built.

## Combined-build bulk run (deep/fork-combo-1026 5f96ca01, list fork1026-hiz-bulk, fix 2 in game)
Runs: lucky22 20261026-131314-slotA (slot A = 960x540), og-desert 20261026-133053-slotC, og-lake 20261026-140324-slotC (1080p).
No crash, no fallback or error line, all Hi-Z log lines present, "Depth resolve: RESZ".
| scene | trace off -> HiZ | pyramid | net | reflection-only pair off/HiZ vs noise |
|---|---|---|---|---|
| lucky22 (960x540) | 0.108 -> 0.074 | 0.025 | -0.009 | 19.6 / 18.6 % vs 9.7 % (bottom half 24 % vs 9 %): FAIL |
| og-desert | 0.535 -> 0.223 | 0.071 | -0.24 | 10.1 / 10.2 % vs 10.5 %: PASS |
| og-lake | 0.081 -> 0.081 (nothing reflected) | 0.071 | +0.07 | 18-20 % vs 23 % (scene moves): PASS |
Fix 2 did not change lucky22. Cause found in the reflection-only view: under HiZ the near floor shows the floor's OWN pattern (the
circle decals) where the old trace shows the dark mirrored rail: rough-floor rays spread almost along the floor pass the facing test
(dot < 0.2) and hit the floor's own nearby pixels (self-hits), averaged over frames into a faint copy of the floor. Also seen:
AmbientLight's prepare +0.015 ms in every scene whenever the pyramid was built right before it.
Fix 3 (code only, after the bulk run; built, not run in game): a level-0 pixel whose point lies on the reflecting surface's own plane
is no hit; and Reflections builds the pyramid right before its own passes (inside "Reflections (SSR)"), AmbientLight's HiZ alone
still builds it early.

## Settings (all off by default; need NVR DXVK + [Main.Main.Performance] NvrDxvkPasses)
- `[Shaders.Reflections.Main] HiZ = false`: recommended ON with the fork once fix 2 is confirmed in the bulk run.
- `[Shaders.AmbientLight.Main] HiZ = 0` (0-6): keep 0 (measured no win).

## Still missing / next step
1. Bulk run of bench\foundations\lists\fork1026-hiz-bulk.txt (copy in tools\nvr-dxvk\test\) with fix 2: lucky22's reflection-alone
   pair must come down to the noise band (~10 %), the trace timer should stay well under the old 0.6-0.7 ms.
2. If near-floor rays still cost much: a cheaper floor skip (a per-cell "all facing up" flag or the depth slope in the pyramid's
   spare channels) instead of the fixed-step fallback.
3. No DXVK patch (0010 unused). NVR_HiZ is a D3DPOOL_DEFAULT texture made on first use, like the effects' own buffers.
