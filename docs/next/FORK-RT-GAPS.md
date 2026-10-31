# RT scene gaps: swaying foliage, the player, far LODs, fog / smoke / glass (2026-10-30)

Branch `claude/rt-gaps` (from the installed build aee0046c, on rt-test-merge a93b6f18). Session rt-ao-sky. Grass is
rt-sun-core's (`claude/rt-sun-grass`); this branch does not touch it.

## 1. Swaying foliage

### What moves in the game, and what the traced scene holds

| Plant | How the game moves it | In the traced scene (RtCore) |
|---|---|---|
| Grass | the game's grass vertex shader (wind) | not in it (rt-sun-core's grass work) |
| SpeedTree bushes and trees, leaves (WastelandShrub01, ~18,000 placed) | camera-facing cards built in the leaf vertex shader (STLEAF001/003: rustle + rock rotations, wind matrices); NVR's plant wind adds a gust boost | **not in it at all** (`kLeaf`: the vertex buffer holds one point a leaf, the card is made in the shader) |
| SpeedTree branches | the game's branch shader | left out as not a lighting shader |
| Static plants (STAT: Joshua trees, yuccas, creosote, sagebrush, ...) | **still in the vanilla game**; NVR's `[Shaders.Plants.Wind]` (off by default, needs a restart) bends them in the object vertex shaders and the sun's shadow maps | in it (alpha-tested cards with `AlphaTested`), **standing still** |
| Anything moved by its node (animated meshes, doors, mobiles) | its node transform | moves already: instance transforms are rebuilt from `m_worldTransform` every frame |

So with the default settings no traced plant is out of step with the drawn one: the static plants do not move,
the SpeedTree leaves are not traced (Mode 0 sun shadows keep them through the cascades, which sway). The mismatch
the brief describes appears with plant wind on: the Joshua trees bend and their cascade shadows follow, but their
traced shadows (multiplied in by Mode 0) stand still, so two shadows separate in a gust.

### PlantSway (`[Main.Main.RayTracing] PlantSway`, default on, live; commit 86d10e0f)
The plant wind's bend is a smooth function of height: lean x (h / H)^2 along the wind, the gust taken from waves
hundreds of units long. Across one plant that is almost an affine map, and an affine map is exactly what an
instance transform holds. So each plant's traced instance gets a shear rooted at its base,
P' = P + lean (P.z - base) / H, where lean is the drawn top's gust move at the plant's root (`PlantWind::RtLean`,
the same numbers as the per-draw constants). The BLAS is never rebuilt or refitted: **no extra GPU cost**, a few
float operations per plant instance on the CPU.
- Error against the drawn plant: base and top agree; at half height the shear is lean / 4 ahead (the shader's
  (h / H)^2). The flutter (a quarter of the lean, short waves) and the top's drop (a fraction of a unit) are left
  out. At Bend 6 and a middling wind a 300-unit Joshua tree's top moves ~10 units; mid-crown error ~2 units.
- Same tests as the drawn path (plant wind on and outdoors, a plant texture by keyword, a root node), without the
  drawn path's per-pass SAFETY bookkeeping (a plant drawn by a vanilla vertex shader in some pass stays still
  while its traced copy leans; the plant wind log counts those as SAFETY skips, 0 in its tests).
- The RT CORE scene log prints `plant sway on: N plant instances leaned a frame` while plant wind's statics are on.

The alternatives, and why not: refitting the BLAS per frame with the bend (a compute pass in the helper before
each refit, like the actors' skinning) is exact but costs a refit of every plant near the camera each frame;
jittering the traced visibility (blurring traced plant shadows over the sway range) hides the static shadow but
also softens shadows that do not move.

### Not done: SpeedTree leaves in the traced scene
The commonest plant in the Mojave casts no traced shadow, no AO and shows bare branches in RT reflections. Its
vertex buffer holds one point a leaf; the card corners come from the leaf shader's constant tables, set per draw.
Tracing them needs those tables captured at draw time and cards expanded into a mesh per tree model (built once,
instanced), probably crossed fixed cards rather than camera-facing ones (a camera-facing card is wrong for a
shadow ray anyway). The rustle is small rotations of each card about its own centre: it changes the dappling,
not where the bush's shadow lies, so a still card set would be acceptable. A session or two of work; a separate
step if wanted. Mode 0 sun shadows keep the leaves' (swaying) cascade shadows meanwhile.

### LeafCards: SpeedTree leaves in the traced scene (`[Main.Main.RayTracing] LeafCards`, default 0 = off; branch claude/rt-leaves)
Built 2026-10-30 on the round 11 merge e4e76fdd.
- **How:** a leaf mesh's vertices are the cards' centres (4 per leaf, same position) with BLENDINDICES z = corner-table index,
  w = corner scale; the leaf shader moves each corner out by `leafBase[int(z)] * w` (y along the camera's right, z along its up).
  The corner table is on the CPU (`SpeedTreeLeafShaderProperty::leafData->leafBase`, 48 float4), so nothing is captured at draw
  time. Each tree model's leaf mesh is read back once (read-only locks of its managed vertex / index buffers, pool 1), every corner
  moved out along fixed axes (2 = two crossed upright cards, x-z and y-z; 3 = and a level x-y card), written to our own managed
  buffers (float3 position + float2 texcoord) and staged by the fork like any mesh. One BLAS per model (the CPU buffers are freed a
  few frames after the build), one instance per tree, alpha-tested with the tree's leaves texture (`TreeModel->LeavesTexture`) at
  its atlas slot and the draw's alpha ref, mask NVR_RT_MASK_ALPHA (0x10): rays with mask 0xEF still skip them. No wind (the rustle
  turns cards about their own centres: the dappling moves, the bush's shadow does not). The texcoords run v 0..-1: the atlas
  lookup's `fract` wraps them as the game's sampler does.
- **The walk fix:** a TREE ref's root fade node reads FadeAlpha 0 on (nearly) every tree the game draws (rtleaf-4 tree probe: 630,
  later 668 of 668 leaf meshes under a faded root), and the RT walk dropped faded nodes, so only ~22 leaf meshes a frame reached
  it. The cascades' walk never fade-tests a ref's root; with LeafCards on, the RT walk no longer does for tree refs either (one LOD
  of each tree's two leaf meshes stays out by its APP_CULLED flag).
- **Debug:** `LeafCardsOpaque` (traces the cards as opaque objects) and an RT CORE `tree probe` line (one frame at each scene log
  with LeafCards on: tree refs, their leaf meshes by what would stop the walk) next to the `leaf cards` line.
- **Sun:** the default sun (Mode 0) multiplies traced visibility with the cascades, which already hold the (swaying) leaf shadows;
  the traced cards add the same silhouettes, so the sun picture moves within noise (below). LeafCards pays off in RT AO / GI /
  reflections, where the leaves were missing.

## 2. The player and far LODs

- **Third-person body**: walked every frame with `Actors` on (`Walk(Player->GetNode())`), **in first person too**:
  the game leaves the body node drawn (not APP_CULLED) in first-person view (rtfol-2: 34 body instances a frame), so
  the player casts traced sun / lamp shadows and occludes RT AO in both views. The RT CORE `player:` log line
  (60 and every 600 exchanges, with Actors) shows it.
- **First-person arms and weapon** (`firstPersonNiNode`): not traced, by design. The view model sits at the camera at
  its own field of view and scale; as a caster it would shadow the screen itself, and it double-counts the body.
  NVR's shadow maps skip it for the same reason (`isFirstPerson: continue`, ShadowManager.cpp). Modern games shadow
  the full body, not the view model. If the body is culled in first person, the fix is to walk it anyway
  (pose permitting), not to add the arms.
- **Far object LODs**: not in the scene. LOD land is (only as a fallback where the loaded cells hold no land).
  `SceneRadius` 8192 is about the loaded grid's reach (uGridsToLoad 5), where the full objects stand: object LOD
  there would double them. Object LOD only adds anything beyond 8192, where it matters for RT reflections of
  the skyline, not for sun shadows, AO or GI. Left to rt-reflections if they want a longer reach.

## 3. Fog, smoke, particles, glass

- **Glass and other blended-only meshes** are out of the scene, so sun and lamp rays pass them at full strength. That
  is close to right (clear glass transmits ~90 %), and it matters for lamps: a lamp's own glass globe or shade
  surrounds the light, and putting glass in as a blocker (even partly) would darken everything that lamp lights.
  A tinted transmission (any-hit accumulating colour through a glass mask) costs a second ray query per shadow
  ray, for coloured shadows behind the few exterior windows. Not worth it.
- **Weather fog / sandstorm**: the weather's sunlight colour already carries the fog's dimming (foggy weathers are
  authored with dimmer sun). Attenuating the traced sun by the fog layer as well would count it twice. The
  in-scattering (VolumetricFog) already reads the sun's shadows.
- **Local smoke (GunFX volumetric smoke, off by default) and particles**: they would need either a march of the
  froxel volume toward the light (only inside the view frustum; smoke off screen casts nothing) or per-frame
  particle geometry with an alpha any-hit (rebuilt every frame, like actors). The Mojave's smoke is sparse and
  short-lived. Not worth it.

Verdict for 3: no prototype. The current behaviour (only opaque and alpha-tested casters block rays) is the right
one for this game.

## 4. Multi-bounce GI: a world-space radiance cache (2026-10-31, plan; branch claude/rt-radiance-cache)

### The gap
Modern RT games (Cyberpunk / Alan Wake 2 path tracing with SHaRC or NRC, Metro Exodus EE's probes, RTXGI DDGI) carry light over
several bounces through a world-space cache: a path stops at its second vertex and takes everything after it from the cache. NVR's RT
GI (FORK-RT-AO.md) is one bounce: a ray from each half-resolution pixel, and at its hit either the game's lit frame (on screen) or
analytic light (sun + one picked lamp, each with a shadow ray; GlowGI). Hits on screen already hold more than one bounce (the lit
frame has last frame's GI in it), so the gap is **off screen**: a room lit by a lamp behind the camera, a sunlit courtyard seen
through a door, the far side of a corridor. There the bounce sees only direct light, and corners that light reaches in two bounces
stay black.

### Design (no new core features: shared images and passes only)
The core has no storage buffers or integer images to share, so a hash grid with atomic inserts (SHaRC proper) is out. A
**camera-centred, world-anchored grid of irradiance cells** updated by gathering (DDGI-style) needs neither, and its update has the
same feedback loop that gives SHaRC its infinite bounces:
- **Grid:** 32 x 32 x 16 cells round the camera, `CacheCellSize` units each (default 128, ~1.8 m: 58 x 58 x 29 m). The cells are
  fixed in the world (cell = floor(world / size)) and stored toroidally (slot = cell mod 32/32/16), so moving the camera re-uses
  every cell still inside the box. Each slot keeps its cell's world coordinate (its stamp) and is reset when a different cell maps there.
- **Storage:** one RGBA16F shared image, 256 x 256 = 16384 cells x 4 texels: three L1 coefficient texels (red, green, blue: the mean
  of the incoming light and the mean of light x direction, so irradiance(n) = c0 + 2 c1 . n) and one header texel (the stamp, the
  frames of samples behind it, how much of what it sees is the back of something).
- **Update pass** (`RtRadianceCache.spv`, RtAOTrace.comp built with RT_CACHE_PASS): a quarter of the cells a frame (in turn), each
  traces `CacheRays` rays (default 8, a rotated Fibonacci sphere) from its centre. At a hit: the hit's colour (HitTextures) x (the
  same direct light as the GI bounce: sun + one picked lamp with shadow rays) + **the cache's own irradiance at the hit** (last
  frame's: the recursion: bounce n+1 from bounce n, one more bounce a frame) + glow. Misses give 0 (the sky's light is the game's
  ambient already, as for GI). Blended into the cell's history (1 / M, M up to `CacheFrames`, default 16).
- **Leaks:** a cell whose centre sits inside a wall sees mostly back faces; its header keeps that share and lookups weigh it out.
  Lookups also weigh each of the 8 cells round the point by the cosine toward it (DDGI's normal weight) and look from half a cell off
  the surface.
- **Lookup:** in the GI bounce (both the separate GI pass and the merged AO + GI pass), at an off-screen hit, the light it sends back
  becomes albedo x (direct + `CacheStrength` x cache irradiance) instead of albedo x direct. On-screen hits stay the lit frame.
  The merged pass has no free image binding, so the cache goes at binding 4 (the plain AO path's GI output, unused when the
  reuse runs in that pass); the separate GI pass binds it as its 12th image (binding 15).
- **Settings** (`[Shaders.AmbientOcclusion.RT]`, needs GI and GIReuse > 0): `RadianceCache` (default false), `CacheStrength` (1.0),
  `CacheCellSize` (128), `CacheRays` (8), `CacheFrames` (16). Off = exactly the current passes (no image is registered differently,
  the update pass is not added, the lookup's flag is 0).
- **Debug:** `DebugView 6` (the GI bounce light alone, as before) with RadianceCache 0 / 1 shows what the cache adds.
- **Expected cost on the iGPU:** 4096 cells x 8 rays x (1 + up to 2 shadow rays) ~ 100k rays a frame, ~1/5 of the GI pass's;
  the lookup 32 image reads per off-screen hit. Target < 3 ms.

### Test
One bulk run, iGPU, Lucky 38 (an interior lit by lamps) and Goodsprings noon (sun bounce), RadianceCache 0 / 1 (+ DebugView 6):
the bounce light's mean and its change off-screen-lit corners, the pass costs (update pass + the GI pass with lookups), no crash.

## Runs (iGPU, fork-0033c, helper extra-p3d, run toml rtao/run-toml-igpu-hit-plants.toml = AlphaTested + HitTextures + Actors + plant wind on)
- **rtfol-1** 20261030-135727-slotC (og-goodsprings 16:00, Bend 16): no crash, no helper errors. Log: `plant sway on: 581.5 plant
  instances leaned a frame`; `player: third person; body node drawn; 29.4 body instances a frame`. The slot's camera faces the sun
  at this spot, so the plants' shadows fall toward the camera, and the player's idle motion swamps the whole-frame differences: no
  verdict on the look from it.
- **rtfol-2** 20261030-143328-slotC (9:00, Bend 40): same camera (east, into the morning sun): the tree's ground shadow lies in shade,
  no reading there. The first-person shot (JIP `ToggleFirstPerson 1`) answers the player question: `player: first person; body node
  drawn; 34.0 body instances a frame; first-person node culled (not traced)`. **The third-person body stays in the traced scene in
  first person** (it casts traced shadows and occludes AO there); the view model is not traced. Nothing to add.
- **rtfol-3** 20261030-152043-slotC (9:00, Bend 40, RT sun DebugView 6 = the traced scene from the camera): **PASS**. The traced
  Joshua tree's crown with PlantSway 1 differs from PlantSway 0 at the same moment in 7.3 % of its pixels (noise between repeat shots
  1.5-4.6 %); the crops show the crown shifted ~3 px downwind at the top, the trunk base in place (rooted shear), the static pole
  unchanged. Between the two gust phases (TimeOffset 0 / 2 = 0.5 rad of the gust wave) the change is within noise, in both settings:
  the phases were too close to tell apart. Cost: RT sun pass 21.4 ms (on) vs 21.8 ms (off) in DebugView 6 = the same (the shear is a
  transform; nothing is rebuilt).
- Verdict: PlantSway works and costs nothing; ship it on (it does nothing unless plant wind is on).

### LeafCards runs (iGPU, fork-0033c, helper rtmerge/extra-r11 protocol 4, run toml rtao/run-toml-igpu-hit-actors.toml)
- **rtleaf-1** 20261030-184153-slotC (og-goodsprings 15:00): no crash; leaf buffers readable (pool 1), 6 models built, 5.5 leaf meshes
  a frame. No SpeedTree bush in this camera (the bushes in view are static alpha plants, traced already): no verdict on the look.
- **rtleaf-2** 20261030-185141-slotC (FFEU57 bush view, plantwind-B's setpos, 13:00): traced view LeafCards 0 vs 2 / 3 within noise
  (0.20 / 0.37 % vs 0.40 %): the bushes in view absent from the traced scene.
- **rtleaf-3** 20261030-191200-slotC: LeafCardsOpaque 1 shows no cards either; the logged cards are right (corners +-30..60 units on the
  tree's bound) but the ~5 trees met a frame are 2600-5600 units away: the walk never reaches the near bushes.
- **rtleaf-4** 20261030-192150-slotC: tree probe: 334 tree refs, 668 leaf meshes, 630 under a faded root, 304 under APP_CULLED (the other
  LOD), 22 reachable. Cause found (above).
- **rtleaf-5** 20261030-193840-slotC (after the walk fix, DLL 38C2A430): **PASS**. 243 leaf meshes traced a frame (was 22), 10 models.
  Traced view (RT sun DebugView 6) LeafCards 0 vs 2: 6.0 % px, vs 3: 6.3 % (noise 0.64 %): the near bush and the bushes to the right
  appear with their leaf shapes (alpha-tested). RT AO (DebugView 1) 0 vs 2 / 3: 13.1 / 13.0 % px, ~12 % darker (noise 2.8 %): the bushes
  occlude their insides and the ground under them. Final picture with RT sun: 2.3 / 6.0 % vs 8.6 % noise (the cascades hold the
  leaf shadows already). Cost: RT sun pass 4.5-5.1 ms through the run (as without); RT AO pass 5.3 -> 5.7-5.8 ms (~+0.5 ms on the
  iGPU at this bush-heavy view). No crash, no helper errors.
- Verdict: LeafCards works; ship it off by default (as asked), worth turning on with RT AO / GI / reflections. 2 planes look the same
  as 3 here.
