# Root assumptions: a plan across NVR's weak points (2026-10-29, plan only, nothing built)

Method (as in INTERIOR-SHADOWS-MODERN.md): group the open bugs and costs by the **assumption** that causes them, and replace
that assumption the way current engines do, instead of tuning each symptom. Each piece below is off by default, keeps the old path
as the A/B and the fallback, and lands with one bulk bench batch (no per-fix runs).

## The four assumptions

| # | assumption NVR inherited | symptoms it causes today (from the notes) | modern replacement |
|---|---|---|---|
| A | The static world is **redrawn every frame**, one engine draw per object per view | sun cascades ~9,000 draws ("the draw call is the floor", 0.4-2.8 ms on testers' CPUs); lamp cubes; Vault 21 CPU-bound; midday empty maps (QuantizeSun); grass shadows only ~2 m (near cascade only); face shadow pops at cascade borders; batch renderer / instancing NO-GOs (they worked *inside* the engine's draw loop) | **cache by change rate**: static layers drawn once and kept (sun: world-anchored cached pages / clipmap; lamps: the shadow atlas), moving things in a small per-frame overlay; the geometry for NVR's own views comes from a **GPU-resident scene** (the RT core's staged arena: 279k tris, 43 MB in Lucky 38) instead of engine draw calls |
| B | Screen-space effects **guess the scene from depth** | AmbientLight edge outline + halo, rock black faces (flat depth normals), SSR cut at screen edges / floor holes, crusty contact-shadow ring on tess bumps, "contact composite darkens the whole pixel" | **truth buffers** written while the game draws: normals (CapturedNormals, done), **object motion**, material (MaterialRules id, roughness/metal), plus a **world-space fallback** for what's off screen (AmbientLight grid today, scene arena / RT where available) |
| C | Lighting is re-implemented **in each of ~200 game shader permutations**, each with its own light list and ps_3_0 limits | lamp falloff differs per family (MatchEverywhere), PBR dark rugs (per-vertex lamp distance), skin faint shadows (its own ambient share), shiny shaders at the ~3,600-slot cap (POINT_SHADOW_FIT steps), snow effect 8-minute compile (24 lamps inlined), compile hitches (~62 % of arrival hitch time), 6 lamps with shadows per draw | **one lighting library**: lamp data in a float texture + a clustered (froxel) light list, read in a real loop (`tex2Dlod`, not unrolled constants); every family includes the same `LightCommon.hlsl` (falloff, shadow lookup, PBR), so fixes land once and permutation counts fall |
| D | Each temporal effect keeps **its own history and reject rules**, with **camera-only motion** | AmbientLight trails on a grabbed teapot, history wiped at every exterior cell border, cloud flicker only with TAA + vol clouds, FSR 2/3.1 NPC/weapon ghosting unproven, RT soft shadows noisy (no denoise) | **one temporal backbone** per frame: object motion vectors + one disocclusion/reactive mask + a camera-jump flag, computed once and read by TAA, FSR, AmbientLight, SSR, clouds and the RT denoiser |

Interior shadows (INTERIOR-SHADOWS-MODERN.md) is A + C + D applied to one problem: atlas = A, the 1-sampler lookup = C, the shared
visibility buffer = D.

## A. Cache by change rate + GPU-resident scene
1. **Sun: cached world-anchored cascades.** Far and middle cascades snap to world texel grids (they already QuantizeSun); keep their
   static layer and redraw only when the sun step or the snapped origin changes, scrolling the map by the integer offset and drawing only the
   new strip (UE's cached VSM pages / Far Cry clipmap scrolling). Moving things (actors, grass, plant wind, ragdolls) go into a thin
   per-frame overlay min'd on top. The existing `SunCascadesDrawnMask` and `SunCascadeSkipFlat` are the start of this.
   This also fixes grass reach: grass becomes part of the overlay at any distance you want to pay for, not only the near cascade.
2. **Player / NPC shadows from their own maps**, not the cascades: CHARACTER-SHADOW-MAP-DESIGN.md (already prototyped) generalised to
   the 2-4 nearest actors. That ends the face-shadow pops at cascade borders and the CascadeLambda/FOV sensitivity.
3. **GPU-resident scene for NVR's own views** (fork only): the arena the RT core already fills (`StageGeometry`) is also drawn by NVR for
   shadow views with a few big draws (static mesh per cell + instance table) instead of one engine draw per object. On native D3D9 the
   fallback is the cached layers of 1-2 (cheap there too, because they rarely redraw).
- Risk: the scrolling cache needs exact texel snapping or shadows swim; animated statics (doors, flags) must be classed as moving.

## B. Truth buffers + world-space fallback
1. **Object motion vectors**: in the draw hooks NVR already has (SetShadersHook / RenderPass hooks), keep each geometry's previous
   world matrix (and the previous bone palette for skinned draws) and write screen velocity into an RG16F target with MRT, in the game's
   own draws (the CapturedNormals path is the model: same hooks, same MRT budget). Static geometry keeps camera-only motion at zero extra cost.
2. **Material id + roughness/metal** from MaterialRules into the same MRT (one 8-bit channel each), so SSR, AmbientLight and specular stop
   inferring "is this metal/floor/grass" from depth bends (the grass rule that drew the AL outline).
3. **Thickness**: depth-buffer thickness guess replaced by a back-face depth pass only for the nearest objects (or the arena's per-object
   bounds): fixes the GTAO halo and contact-shadow rings by construction.
4. **World-space fallback** for off-screen rays: AmbientLight grid (GridFallback, works indoors), then the arena via RT where present.
- Risk: MRT count in game shaders (ps_3_0 has 4 targets; normals already take one) and the ~3,600-slot cap. C frees the slots.

## C. One lighting library (clustered forward)
1. Lamp table (position, radius, colour, falloff kind, shadow atlas rect, fixture size) in a 256 x N float texture, uploaded once a frame.
2. A froxel grid (e.g. 16 x 9 x 24) listing lamp indices per cell, built on the CPU (447 lights is small) or in a pixel pass.
3. `LightCommon.hlsl`: one loop `for (i < cellCount)` with `tex2Dlod` fetches, one falloff (Karis windowed, MatchEverywhere by
   construction), one shadow lookup (atlas), one BRDF. ObjectTemplate, Terrain, Skin, SkinVPS, hair, snow include it, so the per-family
   copies are deleted and every lamp the game gives (and more: ManyLamps) is lit.
4. Side effects: snow's 24-lamp unroll goes away (compile seconds, not minutes), fewer permutations to precompile (shorter warm-up, smaller
   baked cache), shaders well under the slot cap (no POINT_SHADOW_FIT steps).
- Risk: dynamic loops cost more than unrolled code on old GPUs (measure on the GTX 1070 first); look changes where per-family quirks were
  "the look" (skin ambient share): keep them as named per-family parameters, not code copies.

## D. One temporal backbone
1. Per frame, once: velocity (B1), disocclusion mask (depth + velocity mismatch), reactive mask (weapon, particles, alpha), and a
   `cameraJumped` flag (teleport / load / > 1000 units), replacing each effect's isCellChanged reset.
2. TAA, FSR 2/3.1, AmbientLight Accumulate, SSR temporal, volumetric clouds, RT lamp denoise read it instead of their own rules.
3. A shared reprojection helper in HLSL (`Temporal.hlsl`: reproject, neighbourhood clamp, confidence) so they also agree on the edges.
- Risk: the vol-cloud + TAA flicker may be a different bug (not reproduced yet with the user's save): run its save in the same batch.

## Order and size (session-hours; the real risk named above)
1. **B1 + D** first (~6-8 h): motion vectors are the input everything temporal is missing, and FSR 3.1 / TAA / AL get better at once.
2. **C** (~8-12 h): the biggest code change, but it unblocks the interior atlas, removes the compile-time and slot-cap problems, and makes
   every lighting fix land once. Do the GTX 1070 dynamic-loop cost check before converting families (1 h).
3. **Interior shadows** (INTERIOR-SHADOWS-MODERN.md, ~12-16 h), now on C's lookup and D's buffer.
4. **A1-A2** sun caching + character maps (~8-10 h); **A3** arena-drawn shadow views later, with the fork RT work.
5. **B2-B4** as the effects that need them come up.

Measure each step in one bulk batch: the intshadow lookback metrics, AL edge/ring numbers (aloutline), teapot grab trail, Vault 21 /
Ultra-Luxe / Strip CPU ms (CpuSplit), arrival hitch time, FSR NPC ghosting shots, compile time from a cold cache.

## Process weak point (same lens)
Assumption: a fix is done when its branch is built. Fact: most notes end in "built, NOT installed". Replacement: a fixed weekly
integration train (one combo branch, one bulk batch, one install), with each plan step above landing only through it.
