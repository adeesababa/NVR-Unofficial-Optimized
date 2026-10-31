# RT many-light lamp shadows (ReSTIR-DI-style), 2026-10-31

Branch `claude/rt-restir-di` from `claude/rt-test-merge` 01a9a623 (round 13b). Setting `[Shaders.ShadowsInteriors.RT] ManyLights`,
off by default, needs a restart (its two history images are made when the RT helper starts), interiors only. Without RT nothing
changes (the RT lamp shadows are off then, and so is this).

## Where the lamp trace stands (the gap)
Indoors each lit object shades its own lamps (the game gives a draw 1-6) and multiplies each by its shadow
(PointShadowForward.hlsl). With `[Shaders.ShadowsInteriors.RT]` on, that shadow comes from the trace (RtLampTrace.comp v4):
- the trace's lamp table holds only lamps with a **cube shadow slot** (live slots 0-62; LightPoints picks them, 24 by default). A lamp
  without a slot (pool lamps with static cube maps, and every lamp past the slots: Vault 21's lobby has 447 lights) is either
  shadowed by an old cube map or **not shadowed at all**;
- each pixel traces its **2 strongest** lamps (LampsPerPixel; the result has room for 3 entries); every other lamp at that pixel
  goes back to its cube map (with its leaks, one-sided casters, stair-steps), or stays unshadowed.
Modern RT games (ReSTIR DI: Cyberpunk overdrive, Portal RTX; NVIDIA RTXDI) shadow every light: per pixel they draw a few lights
from all of them by how much light each brings (weighted reservoir sampling), trace one ray each, and reuse the samples over
neighbours and frames so one ray a pixel looks like hundreds.

## The design: exact for the strongest, a reservoir-sampled ratio for all the rest
The forward shaders shade lamps one by one, so they need a shadow per lamp, not one total. So:
1. **The two strongest lamps of a pixel: traced exactly** (as today: Rays rays each over the lamp's disc).
2. **Every other lamp that lights the pixel: one shared, traced ratio** `R = sum(w_i V_i) / sum(w_i)`, w_i = the unshadowed light
   lamp i brings (intensity x the game's falloff x N.L, the ranking's own weight), V_i its visibility. Each lamp not traced
   exactly takes R as its shadow. The total light from those lamps is then right on average (exactly right when the shader's
   lighting matches w); per lamp it is the ratio estimator of Heitz et al. 2018 ("Combining analytic direct illumination and
   stochastic shadows"), which is what ReSTIR DI's shading reduces to with an unshadowed target.
3. **R is estimated with reservoir sampling**: one streamed pass over all the pixel's other lamps keeps ManyLightsRays (1-4)
   weighted reservoirs (each keeps lamp i with probability w_i / running sum); one ray to each chosen lamp (a random point on its
   disc). With samples drawn in proportion to w, the mean visibility is an unbiased estimate of R.
4. **Reuse**: spatial (each pixel averages the samples of its 3 x 3 or 5 x 5 neighbours inside its 8 x 8 group, kept only where
   depth and normal agree: the group shares its samples in shared memory, no extra pass) and temporal (a history image pair:
   the pixel's point reprojected into the previous traced frame; where the depth there agrees, a running mean over up to
   ManyLightsHistory frames). Full ReSTIR also reuses the chosen *lamp* (the reservoir) across pixels and frames; with an
   unshadowed target that adds nothing to a ratio estimate, so the visibility samples themselves are reused (cheaper: no extra
   rays for the reuse).
5. **Every lamp of the cell that lights a drawn object goes into the table**: the cube-slot lamps keep their ids (0-62); every
   other point light a draw used this frame (collected in PointShadowForward::SetForPass) gets an id 64-254 for the next trace.
   Up to 254 lamps (ManyLightsMax). A lamp with no shadow map of its own is then traced where the traced frame saw the point and
   lit elsewhere (as before); a lamp with a cube map falls back to it only where the traced frame did not see the point.

### Result format (the same RG32F texture on s15, exact integers below 2^24)
- today: entry = cube slot (6 bits) + 64 x visibility (4 bits); r = e0 + 1024 e1, g = e2 + 1024 x depth code (12 bits).
- ManyLights: entry = lamp id (8 bits, 255 none) + 256 x visibility; r = e0 + 4096 e1, g = R (12 bits, 0-4095) + 4096 x depth code.
- The forward read takes the unit from c217.w (1/1024 or 1/4096, the CPU sets it from the newest result's format) and decodes both
  with power-of-two multiplies only (exact); in ManyLights a lamp not among the two entries reads R instead of "not traced".
  A draw's lamp id with no shadow map of its own comes as id + 256 (the shader then never touches a cube map for it).

### Not in this step (and why)
- **Emissive surfaces as lights**: the game's lit shaders only shade its point lights, so an emissive surface needs its own light
  term, not just a shadow. rt-ao-sky already has that: GlowDirect (RtAOTrace.comp) lights the bounce light from the glow-mapped
  instances (neon, signs, globes, screens) as traced sphere lights (RtCore::GlowLights). Sharing this table with that list (glow
  lights sampled by the same reservoirs, their light added in a screen pass) is the follow-up; it is not cheap here.
- Outdoors (ExteriorForward): the same scheme would fit (its table is ShadowLights); indoors first, where the lamp counts are.
- LampsPerPixel 3, Filter, Denoise: ManyLights uses the third entry's place for R, and the result is its own filter; with
  ManyLights on those settings do nothing.

## Settings ([Shaders.ShadowsInteriors.RT])
- ManyLights = false (restart): the many-light trace (RtLampTraceMany.spv) indoors.
- ManyLightsMax = 254: lamp ids in the table (63 cube-slot ids + up to 191 others).
- ManyLightsRays = 1 [1-4]: rays a pixel for all the other lamps together.
- ManyLightsSpatial = 1 [0-2]: neighbour radius of the spatial reuse (0 off, 1 = 3 x 3, 2 = 5 x 5).
- ManyLightsHistory = 16 [1-64]: frames the temporal reuse averages.
- DebugView 4 (diagnostic): R per pixel (cyan = lit share, magenta = no traced data).

## Test (one bulk run, list rtlamp-many-1)
Lucky 38 + Vault 21 (the RT lamp spots), iGPU env as FORK-RT-LAMPS.md, Actors on: cube / traced (today) / ManyLights, each twice
(repeat noise), DebugView 4, ManyLightsRays 2. Read: the log's lamp counts (cube-slot vs other lamps in the table), the picture
difference ManyLights vs today and vs its repeat, R's view, the trace's GPU time, the shader load lines (does any forward record
grow past the GPU's limit: refused counts).

## Results (run rtlamp-many-2, 20261031-034115-slotB, PASS; build b56706eb, DLL 9B6677DA)
Lucky 38 + Vault 21, iGPU (AMD Radeon Graphics) 1280x720, fork-r13 + extra-r13b. Run rtlamp-many-1 (20261031-021702-slotA) did
not test ManyLights: its first shot turned ManyLights off before the RT helper started, so no history images were made (the
"needs a restart" rule seen from the other side). rtlamp-many-2 runs the ManyLights shots first.
- **It runs**: helper 26 shared images (2 more than round 13b), "RT lamp shadows traced, ManyLights", no compile errors, no
  refused records. The rest view (DebugView 4) in Lucky 38 shows R on most surfaces, with the stair and rail shadows of the
  lamps past each pixel's two strongest: lamps the cube maps or nothing shadowed before are now traced. In Vault 21 R shows
  on fewer surfaces (where each pixel's two strongest lamps are all its lamps, there is no rest).
- **Picture** (mean |diff| of 255, share of pixels off by more than 16):
  - repeats: many vs many-b 0.20 / 0.02 % (L38), 0.23 / 0.01 % (V21); the same as rt vs rt-b (0.19, 0.22): R's history leaves no
    visible noise in still shots;
  - many vs today's trace: 0.20 / 0.03 % (L38), 0.36 / 0.14 % (V21, slightly darker: luma 31.2 vs 31.3, the extra lamps now
    shadowed); ManyLightsRays 2 vs 1: 0.22 / 0.04 %.
  - So the change is real but small on these spots: few pixels get much light from lamps past their two strongest.
- **Cost (GPU, ms a trace run on the iGPU)**: today's RtLampTrace 25.7 (L38) / 23.0 (V21); RtLampTraceMany 82-94 (L38, 3.2-3.7x)
  / 139.7 (V21, 6x). Too costly as built. Most of it is not the one extra ray: L38 has the same 24 lamps in the table. Likely
  causes, for the next step: the dynamically indexed arrays (chosen[4], entry[2], bestOf[2]) go to scratch memory on AMD; the
  8 KB of shared lamp table + group arrays cut occupancy; the two passes over the table per pixel (top 2, then reservoirs)
  scale with the lamp count (V21).
- The table's lamp count past the cube slots is unconfirmed: the log line prints every 600 traces, and trace 1 (the only one
  printed) comes before any draw noted a lamp (0 others). Vault 21's higher cost suggests the others were added.

**Verdict**: works, picture right and stable, but 3-6x the trace cost for a small visible change on these spots. Keep it off
(default off). Next: one pass over the table (top 2 and reservoirs together, no dynamic arrays), the table in a buffer instead
of shared memory, the lamp count logged on the second trace; then re-measure.

## Cost pass (run rtlamp-many-3, 20261031-040215-slotA, PASS; build 919794f3, DLL 1FDCBB72)
Changes: one pass over the table (a lamp pushed out of, or never in, the top two goes to the reservoirs), no dynamically indexed
arrays, one ray call site for all rays, ManyLightsRestStride (2: the rest ray on one pixel of each 2 x 2 a frame, in turn), and
the collected-lamps log (3rd ManyLights trace in each cell, then every 1200th; up to 16 of the lamps without a cube slot listed).
- **Cost (GPU ms a trace run, iGPU)**: Lucky 38 82-94 -> **70.7** (today's RtLampTrace 25.7: 2.8x); Vault 21 140 -> **89.5**
  (today 23.1: 3.9x). RestStride 1 vs 2: **the same** (70.8 vs 70.7, 89.6 vs 89.5).
- Why RestStride saves nothing: a wave (32/64 pixels) runs a ray if any of its pixels needs one, and every 2 x 2 has a pixel that
  traces, so every wave still traces the rest ray. The stride has to be per group (whole 8 x 8 groups in turn, the frame
  history filling the others) to cut work. The rest ray itself is the expensive one: it goes to a random, mostly far lamp
  (long, incoherent rays), while the two exact rays go to each pixel's nearest strong lamps.
- **Collected lamps (log)**: Lucky 38: 24 lamps, all with a cube slot, 0 others (every lamp the draws used there has a slot).
  Vault 21: 214 in the table = 24 cube slot + 190 others, and **166 more refused** past ManyLightsMax 254: the draws there used
  about 380 lamps. The others are the lobby's ceiling lamps (reach 320-510, intensity 0.7, fade 1).
- **Picture**: unchanged from v1 and as stable: many vs repeat 0.18 / 0.24 (rt vs repeat 0.17 / 0.23); many vs RestStride 1
  0.18 / 0.24; many vs today's trace 0.17 / 0.35 (V21 slightly darker, the extra lamps shadowed).

**Verdict**: 15-35 % cheaper than v1, still 2.8-3.9x today's trace: keep off. Next (one more run): RestStride per 8 x 8 group
(cuts the rest rays' wave work by 4), and the rest lamp drawn from nearby lamps first (a reach-weighted cap on the rest ray's
length), then re-measure.
