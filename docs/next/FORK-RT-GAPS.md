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
