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

## Round 2 + interim (2026-10-26 afternoon)
Round 2 (combo 45516f6e, fix 3 merged, lucky22 20261026-144905-slotA): still FAIL indoors (reflection-only 19.4 / 18.7 % vs 9.7 % noise; the near floor gets no screen hits under HiZ where the old trace finds the rail and counter); trace 0.108 -> 0.082 ms at 960x540, pyramid 0.025. Outdoors passed in round 1.
Interim (coordinator's call): `[Shaders.Reflections.Main] HiZ` now applies OUTDOORS ONLY (ReflectionsEffect::HiZWanted: exterior, or the test-only `HiZInteriorsTest`); indoors the old trace runs and no pyramid is built. Diagnostic: DebugView 9 (how each ray's walk ended) and 10 (what it skipped), Hi-Z trace only; shot list bench\foundations\lists\fork1026-hiz-diag.txt (copy in tools\nvr-dxvk\test\), lucky22. Not run in game yet.

## Diag run (combo ffacc671, lucky22 20261026-151619-slotA)
- Outdoors-only rule: PASS. Indoors HiZ true alone vs off 11.0 / 11.3 % vs 10.1-10.3 % noise, signed ~0 (the old trace runs).
- DebugView 9 (HiZInteriorsTest): NO walk hit anywhere on screen (bottom third 98 % red = walked to the end with nothing, 0 % green); the far-floor green of DebugView 2 is the cube / light-grid fallback. DebugView 10: ~4-5 'behind everything by more than the thickness' skips on every ray (blue), few facing / own-plane skips. Model of the walk does not do this: the coarse cells' farthest depth (the pyramid's second channel) reads too small (or the channels arrive swapped) in game, so every coarse cell looks 'behind' and the walk climbs and leaves without descending.
- Fix behind HiZInteriorsTest (commit below): the coarse levels' two channels are read as min/max of the pair, and the 'behind' skip runs only on the finest level (the depth itself). Outdoors unchanged. Expected: DebugView 9 turns green where the old trace finds hits. IMPORTANT: outdoors has the same pyramid, so the outdoor 'same picture' may have been the sky fallback hiding missing walk hits, and part of the outdoor speed-up may be the walk skipping too much: a DebugView 9 shot outdoors (og-desert) is needed before trusting the outdoor numbers.

Switchable everywhere since 0c0dc647's successor: the fix is the test setting `[Shaders.Reflections.Main] HiZSafeCells = false` (indoors HiZ still needs `HiZInteriorsTest`). Lists: fork1026-hiz-diag-out.txt (og-desert, 14 shots: DebugView 9 shipped/fix, DebugView 2 and 1 old/shipped/fix, timing old vs fix ABAB x3) and fork1026-hiz-diag.txt (lucky22, 15 shots, now with the fix).

## Diag runs 2 (combo 7a4deb30: og-desert 20261026-154204-slotC, lucky22 20261026-154901-slotA) and verdict
1. The shipped outdoor HiZ found NO walk hits (og-desert DebugView 9: bottom 99 % red, middle 31 % red + 19 % yellow, 0 % green). The 'same picture' outdoors was the sky fallback (the old trace finds few screen hits on open desert either: DebugView 2 found-share identical for all three), and the speed-up was the walk skipping everything: FAKE.
2. HiZSafeCells restores the old picture (lucky22 reflection-only 11.2 % vs 9.9 % noise; DebugView 2 found-share equal to the old trace's) but NOT through the walk: DebugView 9 shows ~0 % green; rays run out of the 64 cells (magenta 46-74 % at the bottom) or are found by the fixed-step rest (cyan). So with the fix the walk is extra work in front of the old trace.
3. True cost not measured: the diag-out list had no 'profile' line (my mistake). Expected: HiZ+fix costs the pyramid + up to 64 cells + the old trace, i.e. more than the old trace.
4. Verdict: HiZ is NOT shippable as is; the setting is marked UNDER REPAIR, LEAVE OFF (off by default as before). The walk logic is right in the CPU model, so the pyramid as the walk reads it in game is the suspect. Added a test-only readback check, `HiZCheck = N` (log 'Hi-Z check': pyramid levels 0 and 3 vs min/max from the depth buffer: wrong nearest / farthest / zero / swapped counts) and list fork1026-hiz-check.txt (check shot + F10 timing old / shipped / fix x3, with profile). AmbientLight HiZ: measured no win, stays off.

## Root cause found (check run combo e781aae8, og-desert 20261026-162241-slotC)
- HiZCheck: the pyramid was ALL ZERO in game (level 0: 11454 of 11454 cells nearest and farthest wrong, all 0; level 3 the same; depth buffer itself fine: R32F 1920x1080, centre 0.0275). So the nearest 0 never let a cell be skipped as 'in front' and the farthest 0 made every coarse cell look 'behind': the walk climbed and left without a hit (DebugView 9 all red); with HiZSafeCells it walked pixel by pixel into the cell limit.
- F10 (og-desert, 600-frame shots, 3 rounds): old trace 0.628 ms; HiZ as shipped 0.230 + pyramid 0.084 (fake: no hits); HiZ + HiZSafeCells 1.169 + 0.083 (walk + old-trace fallback). Reflections (SSR) 1.466 / 1.152 / 2.092.
- Fix (pyramid v2, code only): HiZBuild.comp writes a storage BUFFER (the extension's proven write path, as AvgLuma's) instead of storage-image views of a mipmapped D3D9 render target, and reads the depth with texelFetch (no gather / sampler); the trace reads the buffer. Either v1 suspect (storage writes into mip views of a D3D9 render target, or textureGather through the heap sampler) is gone. HiZ stays off and 'under repair' until proven. AmbientLight HiZ retired (measured no win). Proof list: fork1026-hiz-check2.txt.

## check2 (combo a3f41491, og-desert 20261026-170133-slotC): pyramid right, trace broken by a slot clash
- Hi-Z check: nearest wrong 0, farthest wrong 0 (the 'swapped' counts 725 / 16 are cells whose nearest and farthest differ by less than the check's tolerance, so both comparisons match either way: harmless, the check's wording). The v2 storage buffer is right.
- But the trace read garbage: the pyramid buffer was bound on buffer slot 0, which read-only storage buffers share with uniform buffers, so it replaced the trace's Constants block (found by the pixel-inspector counters: every pixel 'not started', depth 0.0037 units). The picture (a diagonal wedge without reflections, DebugView 2 hits 145 vs 207, reflection-only 48 % vs 10 % noise) and timing of that run are invalid. Fix: buffer slot 1 (Reflections.cpp ReflHiZBinding + its resource).
- Pyramid cost in v2: 0.28-0.37 ms (v1 image path 0.08): the per-thread 64 texelFetch x 2 and row-major buffer writes. If the walk now works, the next step is cheaper build code (gather again, or shared-memory staging for coalesced writes).
