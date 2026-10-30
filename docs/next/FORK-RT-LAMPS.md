# Fork RT: interior lamp shadows traced (2026-10-28)

## Resumed 2026-10-29 (main session: "tell the rt sessions to resume work")
- Every ray misses in the shared core (rt-sun-core's bug, open). Until rays hit, the lamp side tests what needs no hits: whether a
  point's lookup finds its traced pixel at all (DebugView 2 coverage, run `rtlamp-2-saloon16`). Lead sent to rt-sun-core: the depth
  CopyIn may arrive as 0 (their trace pass is 0.33 ms for 921k pixels; my lamp lookups fell back on 100 % of pixels; a depth of 0
  writes depth code 0 = "sky", which never matches). Coverage all bright red = that.
- **How many lamps, and which (asked by the main session; Vault 21's lobby: 447 lights, 222 invisible fills, 64 cube slots):**
  the trace takes up to **9 lamps a frame** (MaxLamps, default 8): the lamps holding cube shadow slots 0-8. The slots are NVR's
  existing pick (LightPoints, LampRanking 1 = brightness x reach / distance, LampSwitchMargin hysteresis, ShadowCluster, camera
  independent), so invisible fill lights take traced slots like any lamp when they rank high (they get InvisibleLightRadius, a
  broad soft disc). Slots 9+ keep their cube maps. The 9 is the result format's limit (RG32F, 4 bits a lamp, exact floats); more
  needs a second result image (and a second s-register, which the Interior shaders do not have free) or per-pixel lamp
  selection (store the top-N lamp indices per pixel instead of fixed slots: the way past 9, to do once rays hit).
- **Why RT fixes the main session's two interior-shadow problems by construction:** the rays use every triangle both ways (the
  core's instances are two-sided), so one-sided casters (the rings under cups, cut-off shade backs) shadow correctly with no
  TwoSided pass; and a traced lamp costs no cube map, so the 64-slot pressure only matters for the cube fallback. What RT does
  not fix yet: people (not in the scene: ActorsFromCubeMaps) and alpha-tested casters.

## Phase 2 (2026-10-29 from 16:00, branch claude/rt-lamps-2 from claude/rt-test-merge af12c7e7)
### Step 1: the Lucky 38 stair-base stripe = a cube-map LEAK, traced is right (runs rtlamp-5 16:38, rtlamp-6 16:52, PASS)
- DebugView 3 (one traced lamp, DebugLamp) + the traced-lamps log: the stripe is lamp 3 of the trace = cube slot 3, an invisible fill
  light at (1789, -3505, 7805), reach 834, no fixture. Traced, a large surface blocks it over the floor with a straight edge.
- OneSided (diagnostic: only the closest caster counts, and only if it faces the lamp, as the cube maps draw casters): that lamp's
  blocked region becomes lit and the stripe comes back (rt1-onesided vs rt1: 5.6 % of pixels, +0.50). So the blocker faces AWAY
  from the lamp: the one-sided cube map does not draw it and leaks the light; the two-sided rays block it. No RT fix needed; it is
  the cube maps' single-sided-caster problem (the main session's "rings under cups"), which RT removes.
- Also: mask scheme agreed with rt-sun-core (ba320c39): lamp glass 0x04, lamp models 0x08, alpha-tested casters 0x10 (the core's,
  non-opaque); the lamp rays use 0xEF (0xE7 near the lamp) until they test alpha.
### Step 4: SkipTracedCubes (f100ad80, DLL D6ECAD36, on): DONE (run rtlamp-7 17:16, 20261029-171614-slotA, PASS)
- A traced lamp keeps its cube map (the fallback) instead of redrawing it; drawn when it has none of its own, and while people move
  near it with ActorsFromCubeMaps on. ~2600-3000 redraws skipped per 600 traces.
- Picture identical: rt-skip vs rt-redraw 0.0 % of pixels > 2/255 in Lucky 38 and Vault 21 (pairs repeat to 0.0-0.1 %).
- Lucky 38 (iGPU): "Point shadow cubemaps" GPU 3.9 -> 1.86 ms a frame, "Shadow maps (CPU)" 1.5 -> 1.07 ms. Vault 21: little (8.5 ->
  7.2-8.5 GPU, CPU noisy 1.0-3.1): most of its 64 shadowed lamps are not among the 9 traced, and its people force redraws. The
  win grows with step 3 (every shadowed lamp traced where it matters) and step 2 (people traced: their redraws go too).

### Runs rtlamp-10 (actors) + rtlamp-11 (soft 32 + filter v2), 2026-10-29 19:44-19:57, both PASS (DLL FB49B761, rt-sun-2 c9e185d8)
- rtlamp-10, fork-0033 + Actors: the core posed 5040 skinned partitions in exchanges 1-60 (~84 a frame); A/B repeats 0.00 %; rt vs
  cube L38 2.2 %, V21 1.1 %; nothing wrong on or round the people (no speckle, no self-shadow); people moved between shots, so the
  run does not prove their traced shadows (needs a still NPC + DebugView 3 on the lamp over them). Cost: RT 36-42 ms (L38) /
  39 ms (V21) vs 28 / 24 without actors: the skinned rebuilds ~+8 (L38) to +15 ms (V21) on the iGPU.
- But V21's cube maps stayed 8.1 ms (7 people refreshes a frame): KeepCube refused any lamp whose last redraw was PARTIAL, and a
  people refresh with CubeFaceViewCull is always partial -> a loop that redrew them every frame (also in rtlamp-7/8 with
  ActorsFromCubeMaps 0). Fixed (dbb?): a partial cube map is kept too while people do not come from the cube maps. DLL 98511EF1.
- rtlamp-11, LampRadius 32 / invisible 48, 1 ray, Jitter off: hard / soft1 / soft4 still differ on < 0.1 % of pixels: traced lamp
  shadows are a small part of these views and their penumbrae barely show. Filter v2: +14 ms (iGPU) and further from soft4 than
  soft1 (0.13 vs 0.05 %): stays off. Soft lamp shadows: LampRadius 4 + Jitter (TAA) is enough here; the filter is parked.

### Step 3, part 2: soft shadows + a lamp-aware filter (Filter, off; f65dbcce, DLL 1879B367; run rtlamp-9 19:12, PASS)
- The shared RtDenoise (rt-sun-core) filters one value a pixel; this result is three lamps a pixel whose order differs between
  neighbours, so it cannot go through it. RtLampFilter.comp, a second helper pass: each of the pixel's lamps averages the visibility
  of the SAME cube slot among 5 x 5 taps (Step 2), depth and normal weighted.
- rtlamp-9 (LampRadius 8, 1 ray, Jitter off): hard, soft 1 ray and soft 4 rays are the same picture (mean 0.04 / 255): at 8 units
  the penumbrae are too small to see. The filter only changed thin silhouettes (rails, pipes) to brighter (0.2 % of pixels): the
  depth slope came from a one-sided difference, huge at a silhouette, so background taps leaked in. Cost (iGPU, ms, trace + copies):
  L38 hard 28.0, soft1 27.0, soft1 + filter 40.4, soft 4 rays 58.4; V21 24.2 / 24.1 / 37.7 / 52.3: the filter cost 13 ms.
- v2 (f4cb4ec1, shader only): the smaller one-sided difference each way + an early out where the pixel and its 4 neighbours a step
  away agree. Next run rtlamp-11: LampRadius 32 / invisible 48, where the soft edges show.

### Step 2: actors (merged claude/rt-sun-2 7fc7ffc2: [Main.Main.RayTracing] Actors, mask 0x20, fork patch 0030) -- built, NOT run
- The lamp rays' masks (far 0xEF, near 0xE7) already keep 0x20, so posed people block lamp rays once the core has them.
- New RtCore::Actors() (running + Actors on + the fork's 1.8-rt-skin): while true, ActorsFromCubeMaps is ignored, so people's
  shadows come from the trace only and SkipTracedCubes also skips the people-only refreshes (Vault 21: 7 a frame, ~8 ms iGPU).
- To run once rt-sun-core ships a fork d3d9 with patch 0030: rtlamp-8 list with Actors on/off; watch actors' self-shadowing
  (StartOffset on skinned normals) and the Vault 21 cube time.

### Step 3, part 1: per-pixel lamp selection over every shadowed lamp (v4, 62575ea4, DLL FBF611F4; run rtlamp-8 18:00, PASS)
- The trace's lamp table holds every published shadow lamp (cube slots 0-62; 24 in this run's toml, LightPoints); each pixel ranks
  them by the unshadowed light they bring it (intensity x the game's falloff x N.L) and traces its LampsPerPixel strongest. The
  result: three (cube slot + visibility) entries + the depth code per pixel; a draw's lamp not among them uses its cube map there.
- Coverage (exact): Lucky 38 green 39.6 %, yellow (the pixel traced other, stronger lamps) 2.9 %, red 1.1 %, blue 0; Vault 21
  green 38.7 %, yellow 4.2 %, red 0.7 %. A/B pairs repeat to 0.0-0.1 %. rt vs cube: Lucky 38 8.7 % / -0.34, Vault 21 5.5 % / -0.02.
- Lamps per pixel: 2 = 3 to 0.1-0.2 % of pixels; 1 differs 1.8 %. Default 2.
- Cost, helper GPU incl. copies and waits (iGPU 2 CUs, 720p, 1 ray): Lucky 38 1 / 2 / 3 lamps a pixel = 19.4 / 25.8 / 31.3 ms; Vault 21
  17.6 / 23.8 / 24.4 ms. (v3: 9 fixed lamps for every pixel: 23-26 / 16.5 ms, with Vault 21's visible lamps mostly untraced.)
- Cube maps (SkipTracedCubes): Lucky 38 1.75 ms (3.7 without RT); Vault 21 stays 8.1 ms although ~14 redraws a frame are skipped:
  answered from the run's log: Vault 21's 8.1 ms is 7.0 lamps a frame refreshing only their people ("Cube people onto copy",
  ~1.05 ms a lamp; 89 moving pieces there vs 32 in Lucky 38). SkipTracedCubes must keep those while actors come from the cube maps
  (ActorsFromCubeMaps: not in the traced scene yet), so that cost goes only with step 2 (actors in the TLAS).
- Still to do in step 3: soft shadows without noise = the shared denoiser (rt-sun-core); true ReSTIR (reservoir reuse across pixels
  and frames) would only pay off with many more lamps per pixel than the forward lamp shaders can use (they shade 1-6 lamps each).

## SUMMARY (2026-10-29 07:40): where RT lamp shadows stand
Branch `claude/rt-lamp-shadows-d97675` (merged with claude/rt-sun-core-aa6d6d up to ecf5defe), DLL B1BD417C. Not installed, not pushed.
- **Works end to end on the iGPU** (9800X3D RDNA2, 2 CUs, 720p): the RT core's 64-bit helper traces the 9 lamps the visible draws
  used most last frame (per pixel, 1-8 rays over the lamp's disc, two segments so lamp models / glass never shadow their own light);
  the forward lamp shaders read the result next frame, reprojected, and use their cube map where the traced frame did not see the
  point. Off by default; off = byte-identical shaders; no ray tracing on the GTX 1070 (the core stays off, the shaders keep the cube
  maps).
- **Correctness:** clean A/B in Lucky 38 and Vault 21 (cube-a = cube-b, rt1-a = rt1-b to 0.0-0.1 %); the exact coverage view reads
  the traced value on 44 % (Lucky 38, Vault 21) and 52 % (saloon) of pixels, depth mismatch 0.1-0.4 %, nothing off screen; skin
  included. Traced shadows fall where they should (under tables and chairs in Vault 21). No lamp shadows itself: noskip,
  LampStop 24 / 48 and the per-lamp stop change nothing in Lucky 38; the mask hook classes ~5 % of pieces as lamp glass / models.
  No ceiling garbage seen (the ceilings are lit or shadowed as the cube maps, coverage clean there).
- **Cost per frame, helper GPU incl. copies and waits (iGPU, 720p):**

  | scene | lamps traced | rays | ms |
  |---|---|---|---|
  | any | 0 | - | 0 (core idles) |
  | Lucky 38 | 4 | 1 | 15.7 |
  | Lucky 38 | 9 | 1 | 23-26 |
  | Lucky 38 | 9 | 4 (soft) | 90-94 |
  | Vault 21 lobby | 9 | 1 | 16.5 (4 lamps: 16.2: four lamps reach most pixels) |
  | saloon | 9 | 1 | 11-20 |

  About 2.5-3 ms per traced lamp per ray on this 2-CU iGPU. A 40-CU RDNA2 or an RTX card: estimate 15-30x faster (unmeasured).
- **Open:** (1) Lucky 38's stair-base stripe is shadowed traced but lit by the cube map; not self-shadowing (tested); likely a blocker
  the cube map does not draw (one-sided surface or an excluded object kind) -- unresolved, the lamp is not identified yet;
  (2) people are not in the RT scene (ActorsFromCubeMaps keeps their shadows via the cube maps); (3) soft shadows are noise at
  1 ray (no denoise; TAA or more rays); (4) save26 / save27 (GS bar / Vault 21) cannot run on the slim bench slots: Vault 21 was
  reached by moveto; (5) the saloon's third-person camera settles differently at the trace's frame rate, so its cube-vs-traced
  numbers are confounded; (6) cube maps are still drawn for traced lamps (the saving to take next); (7) the debug compare view
  still sends encoded values through the lighting (coverage is exact, compare is indicative only).

### Vault 21 rerun with the screen-driven lamp choice (2026-10-29 07:30, run 20261029-073053-slotA, rtlamp-4b, PASS; DLL B1BD417C)
- Coverage 43.8 % green (was 0 %), red 0.1 %, blue 0 %: the traced lamps are now the ones lighting the view; traced shadows under
  the table and chairs. rt1 vs cube 6.2 % > 2/255 (0.42 % > 16), mean -0.02; A/B clean. Cost 16.5 ms (9 lamps), 16.2 (4 lamps),
  noskip 13.2. Lucky 38 tint pair 8.2 % / -0.27 (the stripe as before).

### Vault 21 lobby + Lucky 38 lamp tint (2026-10-29 06:56, run 20261029-065640-slotA, rtlamp-4, 15 shots, PASS; DLL 7AE67874)
- **Vault 21 (447 lights): traced = cube to 0.1 %, coverage 0 % green.** No draw on screen used a traced lamp: the lamps lighting
  this view held cube slots above 8, and the trace took cube slots 0-8 whatever the screen showed (its cost fell to ~3 ms, the
  trace running for lamps nothing visible used). A/B clean (cube-a = cube-b, rt1-a = rt1-b: 0.0 %).
- **Fix (1a5e4025, DLL B1BD417C):** trace the lamps the screen uses most. Every draw counts its lamps by cube slot (NoteUse), the
  next frame's trace takes the MaxLamps most used (ties: NVR's own slot order), and the result carries its own slot -> traced-index
  map (TracedMap), so draws always decode with the map of the frame they read. This replaces "how lamps are picked" above: it is
  now the 9 lamps the visible draws used most last frame, out of NVR's published shadow lamps.
- Lucky 38 tint shots (forward DebugView 2 + LogLamps): rt vs cube 7.4 %, the stair-base stripe still shadowed traced; LampSlot
  log has the 12 lamps (red globes and two orange ones); the stripe's lamp is not identified yet (the room is red-lit, the tint does
  not separate it). Still open.
- Re-run of the same list on B1BD417C queued (rtlamp-4b).

### Lucky 38 self-shadow check (2026-10-29 06:38, run 20261029-063825-slotA, rtlamp-3, 13 shots, PASS; DLL 7AE67874)
- The mask hook works: "mask hook 200000 calls: objects 190034, lamp glass 5169, lamp models 4796 (cell glow list 132)".
- **The stair-base stripe is NOT a lamp shadowing itself:** noskip (glass + models block), LampStop 24 and LampStop 48 all equal
  rt1 to 0.1-0.2 %; the per-lamp stop past each lamp's glow part changes nothing either. So what blocks that light lies more
  than 48 units from its lamp: a real blocker the cube map misses (a one-sided surface it does not draw, which the two-sided rays
  hit) or geometry the cube map leaves out (MinRadius, UseCastShadowFlag, the object kinds). Which one is open: next list logs
  the lamp (forward DebugView 2 tint + LogLamps) to find it.
- A/B still clean (cube-a = cube-b 0.1 %, rt1-a = rt1-b 0.0 %); rt1 vs cube 7.9 % / -0.22 as before; coverage green 43.5 %.
- save26 / save27 could not run anywhere: every bench slot is slim (vanilla plugins, fixed save list without those saves). Vault 21
  is reached instead from the lucky22 save the way the main session's intshadow lists do (player.moveto 0017A642 + a fixed spot):
  list rtlamp-4-lucky22 (queued).

### Saloon, full list (2026-10-29 05:15, run 20261029-051554-slotA, 17 shots, PASS) + next batch
- Coverage (exact view): green 51.5 %, red 0.3 %, blue 0 %; the CHARACTER is green now: the skin shaders' lookup works (the
  earlier "blue" was the old encoded view's distortion).
- The third-person camera settles differently at the trace's ~5 fps than without it (its smoothing is frame-time based): cube-a =
  cube-b and rt1-a = rt1-b (2.7 % / 2.6 %, TAA noise), but cube vs traced shows the room shifted. So cube-vs-traced numbers in this
  scene are confounded (8.1 %); use Lucky 38 (first person) for the A/B, the saloon for coverage only.
- save26 / save27 failed: slot A is slim (no such saves); now queued on slot C (full).
- Built 67ee1a0d (DLL 7AE67874): per-lamp stop past the lamp's own glowing part + mask-hook log. Queued rtlamp-3-lucky22 (rt1 vs
  noskip vs LampStop 24 / 48, A/B, coverage, cost) and the save26 / save27 lists.

### Lucky 38, full list (2026-10-29 04:54, run 20261029-045431-slotA, 16/16 shots, PASS; DLL 7CA65BE9)
- **Clean A/B:** cube-a = cube-b and rt1-a = rt1-b to the pixel (0.0 %), so every difference below is the feature.
- **Cost (iGPU 2 CUs, 720p; GPU PROFILE "Ray tracing (copies + helper + waits)" p50 per frame, matched to the shots):**

  | shot | lamps traced | rays | ms / frame |
  |---|---|---|---|
  | cost-m0 | 0 | - | 0 (no trace: the core idles) |
  | cost-m4 | 4 | 1 | 15.7 |
  | rt1 / cost-m9 | 9 | 1 | 26.0 |
  | soft4 / cost-r4 | 9 | 4 (radius 4) | 90-94 |
  | noskip | 9 | 1, glass + models block | 21.3 |

  Scene: 2677 instances, 265 geometries, 279k triangles, 43 MB; scene build 0.5 ms a frame, first BLAS builds 47 ms once.
  About 2.5-3 ms per traced lamp per ray here; a 40-CU RDNA2 or an RTX card should be ~15-30x faster (estimate, unmeasured).
- **Picture:** rt1 vs cube 7.9 % of pixels > 2/255, 2.2 % > 16, darker on average (-0.22). The clearest difference: the lamp-lit
  stripe by the stair base (cube) is in shadow traced: likely a lamp shadowing ITSELF (the red globe lamps: the light sits inside
  its glass globe). noskip (glass and lamp models allowed to block) = rt1 to 0.2 %: the mask skip has no effect, so either the
  core's mask hook is not applied or LampFixtures does not class these globes. Coverage (exact view): green 43.5 % (traced value
  used), red 0.4 %, blue 0 %, grey 14.9 % (no traced lamp on that draw). soft4 = rt1 to 0.3 % (radius 4 barely softens);
  actorsmin vs rt1 2.7 %.
- **Next (built after the batch, not during it):** per-lamp stop past the lamp's own glowing part (LampFixtures::NearestLampPart,
  independent of masks) and a mask-hook log (class counts, glow-list size), then the same lists + LampStop 24/48 shots.

### First run with hitting rays (2026-10-29 04:23, saloon16, run 20261029-042346-slotA, 7 of 16 shots)
- Merged rt-sun-core's ray fix (4ba2abfd: the ray origin is RtCore::Eye = 0; CameraLocation reads ~0 in game, so every ray had
  started ~71000 units outside the camera-relative scene); my pass used the same CameraLocation for its origin and for the
  traced frame's eye in the reprojection: both now RtCore::Eye / CameraPosition (05f34409). Then the startup-hang fix
  (f6480bfd: NewEffectCompiler, d3dcompiler_47); the first batch after the ray fix died in the snow effect's 8-minute D3DX compile.
- **Cost on the iGPU (9800X3D, RDNA2 2 CUs, 720p), helper GPU per frame, rays now hitting:** 1 ray x up to 9 lamps, two
  segments each: passes 11-20 ms (RT CORE lines at frames 600 / 1200; the scene build 0.15 ms, first BLAS builds 100 ms once);
  the window with the 4-ray soft shots: 57 ms. With every ray missing it had been 1.1 ms: the cost is the hits (BVH traversal
  to the first hit, no early out on these short interior rays). Relative only: an RX 6700 XT has 40 CUs, an RTX 3060 dedicated RT
  cores; expect roughly 1/15-1/30 of this there, i.e. ~0.5-1.5 ms for 9 lamps x 1 ray at 1080p. Still to measure per MaxLamps step.
- rt1 vs cube 11.0 % of pixels > 2/255 vs the cube-vs-cube noise 7.9 % (a real difference now; +0.16 mean: a little brighter),
  but the third-person camera drifted between shots in this run, so the pair is not a clean A/B; the full batch repeats it.
- NVR's self-test stopped the run after 7 shots: ~1 min a shot at 300 frames and ~5 fps with the trace on. Lists now 120 frames
  a shot (one GPU PROFILE window each: "Ray tracing (copies + helper + waits)" per shot = the cost per MaxLamps / Rays step).

### Coverage run 2026-10-29 03:35 (rtlamp-2-saloon16, run 20261029-033543-slotA, 7 shots, PASS)
- **The lookup works for the world:** the coverage view showed the traced value used (green) on walls, floor, tables and stools,
  no red (depth matches at both tolerances 0.02 and 0.2: identical), a little blue (0.3 %, off the traced frame's screen). So the
  reprojection, the depth code and the result texture on s15 are right. The depth-input lead was wrong for my pass.
- Yesterday's "no difference" explained: this view has few visible lamp shadows, and every ray misses (core bug), so traced =
  lit everywhere = the cube maps' picture wherever they have no shadow (rt1 vs cube: 1.0 % of pixels > 2/255, below the cube-vs-cube
  noise of 6.5 %). The compare view (DebugView 1) is green-tinted wherever the cube maps shade = expected with all-miss rays.
- **Caveat found:** the debug views sent their classes through the lighting as encoded shadow values, and other lighting maths
  distorted them (the compare view brightened red 1.38x, green 2.3x: impossible for a visibility). The character showed BLUE (off
  screen) in that version, which is not trustworthy. Fixed (shaders only): DebugView 2 now writes its colour as the pixel's
  output in ObjectTemplate and SkinTemplate (first traced light of the draw; grey = none traced). Next run gives exact percentages,
  the character included.
- Next run waits for rt-sun-core's ray fix (batched: coverage + rtlamp-1 lists in the same launches).

## PAUSED HERE (2026-10-28 ~15:15, on the user's word via the main session: "pause rt research and dev for now")
Branch `claude/rt-lamp-shadows-d97675` @ this commit (merged with `claude/rt-sun-core-aa6d6d` d71ecf96). Nothing installed, nothing
pushed, no bench tickets left. Test tree link: `work\NVR-rtlamp` (2026-10-03 work root) -> this worktree; DLL in `build\optimized`.
- **Current design (supersedes the 18-lamp / RGBA32F text below):** an RtCore user (`src/core/RtLampShadows.h`, registered next to
  RtSunShadows in ShaderManager). The helper runs `Compute/Rt/RtLampTrace.spv` at RtCore's frame point: R32F depth + RGBA16F normals
  in, RG32F out = 4-bit visibility for 9 lamps (cube slots 0-8; r: 0-5, g: 6-8 + a 12-bit log depth code). The lamp shaders
  (`POINT_SHADOW_RT`) read it NEXT frame on s15, reprojected (c206-c209), with the cube map where it does not match. Mask hook:
  lamp glass 0x04 / lamp models 0x08 only (RtCore::SetMaskHook). `ActorsFromCubeMaps` (the scene has no people).
- **What the runs showed (iGPU, 1280x720):**
  - Lucky 38 (two runs): RT never ran. Run 1 had the old RtCore (screen-size images refused) and timed out at 600 s with 24 shots.
    Run 2: SLS2051/2052 (the POINT variants) failed to compile because `sampler_state {... = POINT}` meets the POINT macro, so the
    forward lamp shadows were refused and no lamps got slots. Fixed in cf5d1b89 (no sampler_state); lists trimmed to 16 shots.
  - Saloon (saloon16, run 20261028-143339-slotA, 15 of 16 shots): RT RAN. Helper up after 1.5 s, 435 instances / 234 geometries /
    262k triangles, helper GPU 1.25 ms a frame (passes 1.09 ms for 9 lamps x 1 ray; scene 0.15 ms; first builds 42.7 ms). 24 lamps
    published. BUT the picture does not use the traced result: rt1 vs cube differs less than cube vs cube (3.5 % vs 6.1 % of pixels
    > 2/255); the compare view (DebugView 1) shows no red or green; the cost shots differ only by noise. So every lamp lookup falls
    back to its cube map: the reprojection or depth match fails (or the result is not reaching s15).
- **Next (when resumed):**
  1. Run `rtlamp-2-saloon16` (list ready: cube, rt1, DebugView 2 coverage at tolerance 0.02 and 0.2, compare). The coverage view
     (988963c8) colours each traced lamp's light: green = traced value used, blue = off the traced frame's screen, red = depth
     mismatch (brighter = further off). Blue everywhere -> the reprojection matrix or the camera (CameraLocation vs CameraPosition)
     is wrong; red -> the depth code / far plane or the depth-buffer convention; nothing at all -> the result texture or s15 binding.
  2. Then `rtlamp-1-{lucky22,saloon16,save26,save27}` (16 shots each; save26/save27 = the GS bar / Vault 21 saves) for the pictures
     and the cost per MaxLamps (0/4/9) and Rays 4. Bench env: LOCKTAG rtlamp, FORK_D3D9 = bench\deep-roadmap\rtsun\fork-0028,
     SLOT_EXTRA = bench\deep-roadmap\rtsun\extra, RUN_TOML = bench\deep-roadmap\rtlamp\run-toml-igpu-rtlamp.toml, SLOT_RES 1280x720,
     SLOT_D3DDEVICE / SLOT_DXVK_DEVICE "AMD Radeon(TM) Graphics", BENCHQ_SOLO=1. ~23 s a shot on the iGPU: keep lists under ~18 shots.
  3. Not yet looked at: the save26 run (20261028-144344-slotA folder is the saloon re-run that NVR's self-test stopped at 8 shots).

Item 3 of NVR's hybrid ray-tracing plan (with the RT core + sun shadows, RT AO/sky and RT reflections). Branch
`claude/rt-lamp-shadows-d97675` (off `claude/p84v3-tessring` 03b62a5a). Nothing pushed, nothing installed.

## Status
- **Written and checked offline; not run.** The tracing needs the RT core's 64-bit helper (session rt-sun-core,
  `docs/next/FORK-RT-CORE.md`, not landed yet). `src/core/RtCoreStandIn.h` stands in for it and answers "no ray tracing", so RT lamp
  shadows stay off (log: `UNOFFICIAL RT lamp shadows: off: no ray tracing (the RT core is not in this build)`).
- Next: rebase onto the core's branch, map RtCoreStandIn's four calls onto its API (submit a pass by .spv name with depth +
  normals in and an RGBA32F image out, get the newest result back), build, run the bulk lists below on the iGPU, fill in "Results".

## Design change, 2026-10-28 afternoon (why the first version was parked)
The first version traced inside the object shaders: a fork patch rewrote marked `texCUBEgrad` samples of the converted lamp shaders
into ray queries against a TLAS whose address it baked in (offline-tested: valid SPIR-V; dxbc-spirv's lowering checked). rt-sun-core
then measured that **AMD's 32-bit Windows Vulkan driver exposes no ray tracing at all** (the 64-bit one does), and FNV + the fork's
d3d9.dll are 32-bit: no ray query can run inside the game on Windows. The RT core became a 64-bit helper process sharing images with
the game. The in-shader version only works where a 32-bit driver has RT (Linux RADV under Proton): parked, not deleted, in
`tools/nvr-dxvk/parked/` (`rtlamp-in-shader-ray-queries.patch` = the fork side, `rtlamp-test/` = its SPIR-V test; the NVR side is in
commit 85b45a1a).

## What it does (plain)
Indoors, each lamp's shadow comes from rays traced against the room's real geometry instead of the lamp's shadow cube map. Same
lamps, same per-object lamp shadows (a lamp's shadow takes away only that lamp's light), without the cube map's limits: no texel
stair-steps, no seams on floor pieces, no bias speckles or leaks at contact, soft with distance (LampRadius). The tracing runs in the
helper beside the game and arrives a frame or so late; where it does not match what is on screen the cube map is used.

## How
1. **Trace (in the helper):** `src/hlsl/NewVegas/Compute/Rt/RtLampTrace.comp` (GLSL, GL_EXT_ray_query; compiles and passes
   spirv-val for Vulkan 1.2). Per pixel of a frame's depth (TESR_DepthBuffer, normals TESR_NormalsBuffer), for up to 18 shadowed
   lamps (cube slots 0-17): Rays rays over a disc of LampRadius facing the point, each in two segments (the whole ray without lamp
   glass; the last LampModelDistance without lamp models either, so a lamp never shadows its own light while a chandelier still
   shadows the room's other lamps), stopping LampStop short of the lamp, starting StartOffset off the surface. Result RGBA32F:
   4-bit visibility, 6 lamps per float channel (exact integers below 2^24), view depth in alpha.
2. **Request (NVR, once a frame indoors):** `RtLampShadows::Submit` (from PointShadowForward::Publish once the lamp slots are known)
   fills the pass constants (std140, `TracePass`: view/projection as TESR_, camera data, size, ray settings, masks, the 18 lamps
   camera-relative with reach and radius), remembers the frame's camera, and hands both to the core.
3. **Use (object shaders, `POINT_SHADOW_RT`):** the newest result is bound on s15, point sampled. Each lamp of a draw that has a
   traced slot reprojects its point into the traced frame (`NVR_RtLampReproject`, c206-c209: camera-relative now -> that frame's
   clip, TAA's own reprojection as a matrix), and takes the traced value if the stored view depth matches within DepthTolerance;
   otherwise (hidden there, off screen, slot past MaxLamps/18) its cube map as before.
4. **Registers / samplers:** c206-c209 (the character shadow map's, outdoors only), c215-c217; s15 = the sixth lamp's cube stage,
   because every other sampler is taken in some Interior variant (s9 the sun atlas, s10 the live cube). So in RT builds the sixth
   light of a draw (the rare six-lamp OPT passes) has no cube fallback (traced or lit) and DebugView 3 (cube trace) shows grey.

Checked offline:
- ObjectTemplate pixel shaders as the game compiles Interior records (FORWARD_SHADOWS 0): 4 variants x 5 option sets (plain;
  MAG+EVEN+SMOOTH; TEXEL; FIT 3; LIVE_CUBE): **RT off = byte-identical to HEAD**; RT on compiles everywhere, 1146-2178 slots
  (plain ps_3_0: under the native driver's ~3600 limit too). SM3002/3003/3005/3007 compile with RT.
- The packing (pack as the .comp, unpack as the HLSL, float32, 2000 random sets) and the reprojection matrix (against TAA's formula,
  200 random cameras/moves) agree exactly (numpy).
- NVR DLL: VS2019 Release Win32 builds clean (`H:\_NVR-rtlamp\nvr-out`).

## Settings `[Shaders.ShadowsInteriors.RT]` (defaults toml; off by default)
| key | default | meaning |
|---|---|---|
| Enabled | false | restart; needs `[Shaders.ShadowsInteriors.Forward] Enabled`, the RT helper, a GPU with ray tracing |
| Live | true | live A/B: false = the cube maps again (same shaders) |
| MaxLamps | 8 | 0-18 shadowed lamps traced (by cube slot, best-ranked first); the rest keep their cube map |
| Rays | 1 | rays per lamp per pixel (1-8) |
| LampRadius | 4 | world units: the disc the rays aim over; 0 = hard, no noise |
| InvisibleLightRadius | 24 | the same for the game's invisible lights (-1 = LampRadius) |
| Jitter | true | rotate the ray pattern each frame (for TAA) |
| StartOffset | 0.5 | world units off the surface: no self-shadow |
| LampStop | 12 | a ray stops this short of its lamp (bulb, glass) |
| SkipLampModels | true | lamp models don't block the last LampModelDistance of their own light's rays |
| SkipLampGlass | true | lamp glass / bulbs / glow meshes never block |
| Actors | true | people and creatures block (if the scene holds them) |
| DepthTolerance | 0.02 | share of the distance within which the traced frame's surface counts as this point's |
| DebugView | 0 | 1 = compare: white both lit, black both blocked, RED only traced blocked, GREEN only cube blocked |

## What the RT core must provide (told to rt-sun-core)
1. Available() before NVR loads its shaders; a pass run by .spv name with TESR_DepthBuffer + TESR_NormalsBuffer shared in, an RGBA32F
   output shared back as a D3D9 texture, the 1.3 KB constants block, and the frame number the result belongs to.
2. The scene for that frame's camera (camera-relative), interiors with statics + actors, each instance's Mask from
   `RtLampShadows::InstanceMask` (0x01 rest, 0x02 lamp glass, 0x04 lamp models, 0x08 actors). Without masks (0xFF) lamp glass and
   models block; LampStop alone keeps the bulb itself out.

## Bench (not run yet)
Lists `work\bench\foundations\lists\rtlamp-1-{lucky22,saloon16,save26,save27}.txt` (2026-10-03 work root; save26/save27 = the GS bar
lantern / Vault 21 saves, whichever is Vault 21): 24 shots each, `frames 300` + `profile`: cube vs traced pairs, compare views hard and
soft, soft 4 rays / 1 ray, the self-shadow check (SkipLampGlass/SkipLampModels off), actors off, DebugView 2 tints with LogLamps, and
the cost sweep MaxLamps 0/1/2/4/8/16/64 (64 = 18) and Rays 2/4/8. Run, one launch per scene: `LOCKTAG=rtlamp BENCHQ_SOLO=1
SNAPSHOT=snap-dxvkfork-merge-20261026 FORK_D3D9=<the core's fork x32> RUN_TOML=<copy of run-toml-flashlight-20261028 with
[Shaders.ShadowsInteriors.RT] Enabled = true, FSR off> SLOT_RES=1280x720 SLOT_D3DDEVICE="AMD Radeon(TM) Graphics"
SLOT_DXVK_DEVICE="AMD Radeon(TM) Graphics" sh bench/deep-roadmap/multirun/run_slot_when_free.sh <scene> rtlamp-1-<scene> rtlamp-1 <tree>`.
Pass criteria: cmp-hard mostly white/black with red/green only along soft edges, no green areas (light the traced result lets
through where the cube map blocks: a leak or missing geometry), no dark cones above sconces / fan-blade ovals in rt1 (they appear
in noskip), no ceiling garbage; cost = the helper's trace time per MaxLamps step (relative only: the iGPU is ~1/10 of a 1070).

## Results
(none yet)

## Known limits / next
- A frame or more of lag: a moving lamp or person casts its traced shadow a frame late; a surface just uncovered uses its cube map
  for that frame. Fast camera turns: the newly visible screen edge uses cube maps until the next result.
- Soft shadows are per-pixel noise at Rays 1-2: TAA averages it (Jitter); without TAA use Rays 4+, or LampRadius 0 (noise-free).
  A spatial denoise in the helper (depth/normal-aware blur per lamp before packing) is the next step if it shows.
- Alpha-tested casters are opaque in the core's v1.
- The cube maps are still drawn while RT is on (fallback, A/B); skipping redraws for lamps whose pixels are all traced is the saving
  to take once the pictures pass.
