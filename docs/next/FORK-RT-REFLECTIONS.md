# RT reflections (NVR DXVK fork, hybrid ray tracing item 4 of 4)

Session rt-reflections, branch `claude/rt-reflections-499161` (from `claude/p84v3-tessring` 03b62a5a). No push, no install.
The shared scene (BLAS/TLAS) and the place rays run come from session rt-sun-core (`docs/next/FORK-RT-CORE.md`, `src/core/RtCore.*`).
This pass only uses them.

## Status

| Item | State |
|---|---|
| In-game passes `Compute/ReflectionsRayGen.frag`, `ReflectionsRTResolve.frag`, `ReflectionsRT.glsl` | written, compile (Vulkan 1.1, spirv-val OK) |
| Helper pass `Compute/Rt/ReflectionsRT.comp` (ray queries) | written on the core's layout (FORK-RT-CORE.md), compiles (Vulkan 1.2, spirv-val OK) |
| RT core user (`RtRegister`: images, shader, Record) | written, builds, on the core's API (rebased onto 3093064f) |
| Late exchange `RtCore::Exchange` (between RayGen and Resolve) | **requested from rt-sun-core**; `RtCoreClient::Exchange` answers false until `NVR_RTCORE_EXCHANGE` |
| C++ (`effects/Reflections.cpp`, `core/RtCoreClient.h`), settings `[Shaders.Reflections.RT]` | builds; RT answers "no" until the core is merged |
| SplitTest (RayGen + Resolve without rays, any GPU) | **PASS** on the 1070 (luxe): same picture, +0.045 ms (Results) |
| RT core | **waiting on rt-sun-core** (helper process, first end-to-end test pending) |
| iGPU bench (`rtrefl-1`, scenes water / rain / luxe / lucky22) | not run (needs the core) |

## PAUSED HERE (2026-10-28 ~15:15, on the user's word via the main session)

- **State:** branch `claude/rt-reflections-499161`, on rt-sun-core's `d7fa5427` (its RtCore with `RtCore::Exchange`, written here, merged
  there). Everything is off by default; nothing installed, nothing pushed. The test tree is `work\NVR-rtrefl`, a junction to this worktree.
  The last DLL built is b2cf1518, in `build\optimized`.
- **Works:** SplitTest (RayGen + Resolve without rays) gives the same picture as the old trace on the 1070, at +0.045 ms. On the iGPU
  the whole chain runs: the helper comes up, my pass runs every frame, and the helper's GPU time is 0.42 ms
  (Ultra-Luxe: 263 geometries, 1130 instances).
- **Blocker:** every ray reports no hit (Ultra-Luxe run 20261028-142402-slotA, DebugView 2 all red). rt-ao-sky and rt-sun see the same,
  so it is the in-game scene the core builds, not this pass. RT DebugView 5 (camera -> surface alignment check, commit 38b96b26) was
  made to tell scene errors from input errors. Its Ultra-Luxe run (rtrefl-2, bench/autotest/20261028-150909-slotA, shot 02 align)
  is **100 % red**: camera rays toward visible surfaces hit nothing either, even with 2 x their distance + 500 units of reach. So the
  scene in the helper does not contain the room where the screen sees it (transforms / positions / space). It is a core bug, not
  this pass's inputs, and has been sent to rt-sun-core.
- **To resume:** once rt-sun-core fixes the no-hit bug, rebase onto its branch and rebuild. Then run `rtrefl-2` in luxe, lucky22, og-lake
  and og-goodsprings (rain toml), as the slots lack the water/rain saves. Compare ssr-N vs rt-N and the DebugViews, fill the Results with
  cost, then use the v2 records (albedo, binding 10) in `Rt/ReflectionsRT.comp` for the hits the screen cannot show.

## Where the rays run: a 64-bit helper, not the game

rt-sun-core measured on this PC (note `RT-CORE-HELPER-2026-10-28.md` in the session notes): AMD's **32-bit** Windows Vulkan driver
offers no `VK_KHR_acceleration_structure` / `VK_KHR_ray_query`. The 64-bit driver on the same iGPU does. FNV and the fork's
d3d9.dll are 32-bit, so **no ray query can run inside the game's own Vulkan passes on Windows**. The GTX 1070 has no RT at all.
RTX Remix also uses a 64-bit bridge. The RT core is therefore a helper process (NvrRtHelper.exe, modelled on the DLSS helper) that
builds the scene and runs NVR-named compute shaders. Images are shared both ways through fork patch 0026 (shared images and
timeline semaphores).

This changed this pass's first design (a ray query inside the trace shader, commit cf4a0d9b) into three steps per frame:

1. **RayGen** (in game, `ReflectionsRayGen.frag`, half resolution, technique 8). It is `ReflectionsTrace.frag` line for line up
   to the end of the screen-space march. It then writes three targets instead of shading the fallbacks:
   - `March` (RGBA16F): what the march found (linear colour, confidence);
   - `Dir` (RG32F, shared): the reflected direction (world axes), octahedral-encoded;
   - `Ray` (RG32F, shared): x the surface's view z (0 = nothing reflects here), y the roughness, + 2 where a ray is wanted. That is where the march
     left a share missing (confidence below 1) on a surface no rougher than `MaxRoughness` (RT fades in from 75 % of it).
     SSR comes first, and a ray is cast only where SSR misses.
2. **The helper** (`Rt/ReflectionsRT.comp`, one thread per half-resolution pixel) reads `Dir` and `Ray`. It rebuilds the surface from
   the view z the way `RtSunShadows` does (scene space = world minus `CameraPosition`). Where a ray is wanted it
   runs one closest-hit query from the surface, starting 1 + 0.15 % of the distance along it (past the depth buffer's own error).
   From the hit it sends one shadow ray toward the sun (outdoors, by day). It writes `Hit` (RGBA16F): the distance (-1 no ray
   asked, 0 nothing within `Distance`), the instance index, sun visibility, the primitive index.
3. **Resolve** (in game, `ReflectionsRTResolve.frag`, technique 9) writes what the trace writes, so temporal, blur and composite
   are untouched. A hit fills the whole missing share, from the first source that can show it:
   - **visible on screen** (the projected hit matches the depth buffer within `ScreenTolerance` of its distance, and is not
     the weapon): the lit picture at that pixel. This is exact. It is what the march misses through its step size or its
     thickness guess (thin poles, stepped-over edges, the far end of the ray);
   - **out of view** (behind the camera, off the sides): the surroundings cube toward the hit. It was captured from about here;
     its `a` channel says how much of it was ever seen, and the rest goes to the estimate;
   - **in view but hidden behind something**, or never seen: the **estimate**, a grey surface facing the ray. It is lit by the
     cube's blurriest mip and by the sun as the helper's shadow ray saw it. The cube toward a hidden hit would show whatever hides
     it, i.e. a wrong colour.

   A ray that met nothing within Distance keeps the old fallbacks (cube along the ray, light grid, sky). It takes the sky without
   the sky-visibility guess, because RT has shown nothing blocks it. From the fallbacks on, Resolve is `ReflectionsTrace.frag`
   line for line.

**Leaks fixed by construction.** A ray the march loses today takes the cube or the sky even under a roof or into a wall: the sky
in the floor of a covered porch, the cube's bright window in a floor facing a wall. With RT, a ray that hits anything never reaches
those fallbacks.

**No helper this frame** (the setting off, no RT GPU, the helper late or dead, a pipeline refused): the plain trace runs. The picture
and cost are then exactly today's. The one exception is a frame where the helper fails after RayGen: that frame pays the
RayGen pass on top. The 1070 never gets that far: `RtCoreClient::Ready()` is false, so not even RayGen runs.

**Denoising** for rough surfaces is the existing chain: one GGX-jittered ray per half-resolution pixel per frame (the same
direction the march used), then temporal history (Temporal 0.85) and the edge-aware blur (wider when rough), then the firefly cap.
Rougher than MaxRoughness gets no rays: blurred reflections gain little from exact hits and cost the same.

**Roughness** comes from what the trace already uses: MaterialAware's `_rmaos` buffer where a surface has a map, else the
Dry/WetRoughness of the floor guess.

### Settings `[Shaders.Reflections.RT]` (off by default)

| Key | Default | |
|---|---|---|
| Enabled | false | needs the NVR DXVK fork, its Vulkan passes and the RT core's helper on a GPU with ray tracing |
| MaxRoughness | 0.5 | rougher: old fallbacks only |
| Distance | 6000 | game units |
| ScreenTolerance | 0.02 | share of the distance for "the hit is the pixel the screen shows" |
| EstimateBrightness | 1.0 | |
| SunShadows | true | the helper's shadow ray from each hit |
| SplitTest | false | test only: RayGen + Resolve without rays and without the helper; must be the normal trace's picture exactly |
| DebugView | 0 | 1 sources (green march, cyan RT on screen, blue RT via the cube, magenta estimate, grey old fallbacks); 2 hit distance (red: open); 3 RT without the march; 4 a colour per hit instance |

SSR and the cube stay as the fallback and the A/B path: Enabled false runs the old trace pipeline untouched.

## Hit shading: how far it can go here

What a ray query gives at the hit is geometry only: distance, instance custom index, primitive, barycentrics, front face. NVR is
D3D9 through Vulkan, and now the rays even run in another process. The material at the hit point is the hard part:
the texture, its UVs, the shader constants and the lamps were bound by the game for **that draw**, in a pass that has long finished.
DXVK sees them only as that draw's descriptors. So the tiers are:

| Tier | What it needs | State |
|---|---|---|
| 0 Screen at the hit | the depth buffer + the lit picture (in game) | **done**: exact where the hit is visible; the common case of SSR misses on screen |
| 1 Cube toward the hit | the existing surroundings cube | **done** for out-of-view hits; parallax-free (centred near the camera), 64 px, a little behind in time |
| 2 Estimate | sun direction/colour, cube's blurriest mip, the helper's shadow ray | **done**: lights tiers 3-4 (grey 0.4 / facing the ray where the records lack data) |
| 3 Per-draw albedo | protocol v2 record per instance (binding 10): the draw's diffuse texture average (smallest mip, read once per texture by RtCore) | **done 2026-10-29** in `Rt/ReflectionsRT.comp` (second output `shade`); not yet seen in game (every ray misses) |
| 4 Hit normal | protocol v2 geometry data (binding 11): the triangle's positions + indices, times the instance's object-to-scene 3x3 | **done 2026-10-29**; the sun shadow ray now leaves the hit along that normal; not yet seen in game |
| 5 Textured albedo | per instance the diffuse texture and the UV stream, shared into the helper (or a per-draw texture atlas) | later; most of the "full shading" gap, and costly in shared memory |
| 6 Lamps at the hit | NVR's lamp lists (tiled lamps / ShadowsInteriors' lamp array) passed to the helper + a shadow ray per lamp | later; indoors the estimate's ambient (cube) carries the lamps' light roughly until then |
| 7 The game's own shading at the hit | the draw's pixel shader re-run with its constants at a ray hit | **no**: not from a ray query in D3D9-through-Vulkan, and not across a process boundary, without replacing every game material (what RTX Remix does) |

So: on-screen hits are exact; off-screen hits are as good as the cube; hidden hits are their draw's average colour on their own triangle normal (tiers 3-4, built but not yet seen in game),
and approximate (no texture detail, no lamps beyond the ambient) until tiers 5-6.

## Water

The game's water already reflects through its own planar reflection (WATER*.pso, `ReflectionMap`). That is exact for what the game
draws into it, and NVR's `ReflectionClipFix` fixed its clipping. The trace skips water pixels on purpose ("water reflects on its
own"). RT can add here only in two ways. One is to replace the water shader's reflection sample, which is in the game's D3D9 pixel
shader (SPIR-V replacement territory). The other is a second layer over water pixels, which would double the planar image.
Neither is done in this pass. The water scene stays in the bench list to check that nothing changes on water and to measure the
shore and docks next to it.

## Glass (`[Shaders.Reflections.Main] GlassReflect`, 0 = off, the default)

FNV draws see-through glass (windows, bottles, shop cases) in the world scene's blended stage, alpha blended WITH depth writes, so
the depth buffer and the normals rebuilt from it already hold the glass surface; only "this pixel is glass" was missing, and the
floor guess only takes surfaces facing up. `core/GlassMask.h` adds it: inside that stage each see-through draw (SandstormGlass's
rules; characters never count) is drawn once more into a screen-sized mask with a one-instruction "return 1" pixel shader (z test
LESSEQUAL, no blend, no depth write; MSAA like the scene, resolved after the stage). The redraw keeps the game's own z test (NVR
may run inverted depth). Reflections reads the
mask: masked pixels reflect as smooth glass (roughness 0.03) at strength GlassReflect x Reflectivity, any orientation, through
SSR, the cube and (with `[Shaders.Reflections.RT]`) the ray trace; the composite's Fresnel does the rest. Not during a sandstorm
that hides glass depth. Off: nothing hooked, no target, no draw.

## Requests to rt-sun-core

Sent 2026-10-28 ~12:00. Its reply (12:45) moved RT into the helper; the open points are:

1. The helper pass binding layout: **settled** by `NvrRtProtocol.h` (253a5c32). Set 0: binding 0 the scene, 1 the pass constants
   (up to 512 B), 2..9 the pass's shared images as storage images. `ReflectionsRT.comp` uses `Dir` and `Ray` at 2-3 (readonly)
   and `Hit` at 4 (written), plus a 48-byte constant block. The scene is in camera-relative world space, so the offset is 0.
   The NVR side is `RtCore` (AddUser / AddImage / AddShader / CopyIn / AddPass / CopyOut); this effect is a user (`RtRegister`).
2. Shared image formats: only RGBA16F, RG16F, R32F, RG32F, RGBA8 and R16F, so the rays are two RG32F images (an octahedral direction;
   view z + roughness) and the hits are RGBA16F.
2b. **The late exchange (blocker).** The core exchanges once a frame, at `RtCore::Frame` before the sun shadows. Reflections makes
   its rays much later: the material buffer is captured after that point, and the rays depend on the screen march. It also needs the
   hits before Resolve in the same frame. Requested: `RtCore::Exchange(Device, user)`, a second exchange for one user at the moment
   it calls it, reusing the frame's scene. The alternatives are worse: rays made at the core's point lose MaterialAware and the
   SSR-first rule, and last frame's rays give one frame of lag (ghosting). Also requested: run `RtCore::Frame` indoors outside the
   shadow branch.
3. Scene space vs NVR's camera-relative world (`RtCoreClient::Offset`).
4. Per-instance records by `instanceCustomIndex`: average albedo per draw, the hit normal (tiers 3-4); flag bits for alpha-tested /
   foliage / skinned / water / sky. The sky dome and water must be left out of the scene or masked, or a reflected ray stops at the
   sky sphere.
5. Same-frame sync: Resolve reads `Hit` right after RayGen in the same frame. The core waits GPU-side on its timeline semaphore,
   so the cost lands as GPU time ("Reflections RT (helper wait)" F10 line), not a CPU stall.

## Build / files

- `src/hlsl/NewVegas/Compute/ReflectionsRayGen.frag`, `ReflectionsRTResolve.frag`, `ReflectionsRT.glsl` (in game),
  `Rt/ReflectionsRT.comp` (helper). `tools/nvr-dxvk/compile-compute.sh` now builds `Rt/*.comp` for Vulkan 1.2, the rest as before.
- `src/core/RtCoreClient.h`: the only code written against the core's API (`Ready`, `Trace`, `Offset`, `HasMaterials`).
- `src/effects/Reflections.{h,cpp}`: settings, `RtWanted`, `RtImages` (made on first use), `RtTrace`, techniques 8/9 in
  `ExtRender` (RT uniform on buffer slot 2, the four RT images on image slots 16-19).
- `resource/NewVegasReloaded.dll.defaults.toml`: `[_Shaders.Reflections.RT]`.
- Bench: `bench/foundations/lists/rtrefl-split.txt` (any GPU), `rtrefl-1.txt` (iGPU, with the helper);
  run toml `bench/deep-roadmap/evidence/rtrefl/rtrefl.toml`.

## Results

### 2026-10-30 01:55 glow at hits (rtrefl-9; f7b1b776 = rt-sun-2 35f88f09 merged, DLL 1C6828D4; fork-0033b, helper extra-p3)
The core now sets NVR_RT_RECORD_GLOW on draws with a glow map (neon, lit signs and windows, lamp globes); a glowing hit the screen
cannot show adds its colour x `[Shaders.Reflections.RT] GlowBrightness` (3) to its shading. First real check of the core's flag.
- luxe 20261030-005110-slotA: reflection alone GlowBrightness 3 vs 0: 3.01 % px > 2 (noise 0.02 %), 17403 px brighter / 567
  darker; full picture 0.53 % vs 0.19 %, at most +27 levels: no blowout. On the out-of-view wall panels in the floor.
- lucky22 20261030-014310-slotA: small: 0.32 % alone (noise 0.01 %, 652 brighter / 126 darker); the full
  picture is in noise (0.16 % vs 0.20 %): the casino's neon is on screen, where the screen tier already reflects it exactly.
- Cost: none measurable (one multiply-add in Resolve); "Reflections RT" 10.1-13.2 ms across the shots, glow or not.

### 2026-10-30 00:20 hit textures (rtrefl-8; 8de4960b, DLL 485F9B82; core rt-sun-2 4d776164, fork-0033, helper protocol 3)
`[Shaders.Reflections.RT] HitTextures` (true, inside RT Enabled false) with the core's `[Main.Main.RayTracing] HitTextures` (off by
default; on in the run toml with AlphaTested): a hit the screen cannot show takes its texture's colour at the hit (RtHit.glsl
`rtHitAlbedo`, the core's 64 x 64 albedo atlas) instead of the draw's average. Skinned actors stay average. Variants
ReflectionsRTTex / ReflectionsRTAlphaTex (albedo atlas on binding 6 / 7).
- luxe 20261029-233808-slotA: reflection alone texture vs average 3.78 % of pixels > 2 against 0.02 % noise; full picture 0.87 %
  vs 0.25 %. The change is in the floor where the out-of-view wall panels and fountain base reflect. GPU "Reflections RT" 8.7 ms
  either way (no in-launch cost); helper passes 7.8 ms a frame with AlphaTested + HitTextures on (4.7 in earlier runs without them:
  the core's alpha/albedo atlas work and the alpha pass, not this switch).
- lucky22 20261030-001029-slotA: 8.49 % vs 0.00 % noise alone; full 0.64 % vs 0.20 %; "Reflections RT" 10.8 / 10.6 ms (tex / avg,
  pads 10.7-10.8: in noise); helper passes 9.5 ms (3082 instances, 325k triangles).

### 2026-10-29 21:00-23:20 glass reflections (rtrefl-7; 1414c513, DLL ca236309; fixed in c85cdb10, DLL 57543F00)
- iGPU saloon16: window panes and the mirror reflect. Reflection alone (DebugView 1), glass vs off: 16.2 % of pixels > 2, against
  0.67 % noise; full picture 7.9 % vs a 6.5 % noise floor (saloon's noise is high). 7500 redraws / 600 stages = ~12.5 a frame.
  Flaw: a band over the NPC's face in the mask: glass BEHIND her was marked. Cause: the redraw forced ZFUNC LESSEQUAL, and NVR
  may run inverted depth (larger = nearer), so glass behind nearer surfaces passed. 06f60e05 tried "later opaque draws unmark"
  (0 such draws: wrong guess); c85cdb10 keeps the game's own z test instead (characters stay never-glass).
- iGPU saloon16 again with c85cdb10: face clean; the reflection-alone diff is the window panes (cut by the NPC in front), the
  mirror and the jukebox glass only: 11.3 % > 2 vs 0.27 % noise; full picture 1.08 % vs 0.68 % noise; glass + RT 1.93 %.
- iGPU luxe: 2.11 % of pixels > 2 against 0.16 % noise, 40 glass redraws a frame, no artefacts seen.
- GTX 1070 saloon16, cost (CPU PROFILE per shot, 2 samples of 120 frames each): "Rnd world draws (2nd)" 0.32 ms with glass vs
  0.17-0.23 ms without (alone shots 0.25/0.29 vs 0.24): about +0.05-0.1 ms CPU for ~12.5 redraws. GPU "World scene (game)" 2.03
  with glass vs 1.83-1.96 without, inside its own spread (warm 2.02 with no glass): no measurable GPU cost.

### 2026-10-29 19:26 hit-distance-aware blur (rtrefl-6; DLL adbea5a0, f79a0a7b): ContactSharpness 0.3 vs 1 (the old even blur)

The runs are `bench/autotest/20261029-192624-slotA` (luxe) and `20261029-195738-slotA` (lucky22), all 9 shots each.
- luxe: 0.32 % of pixels vs a 0.14 % floor; reflection alone 0.37 %. The differences sit on the edges where things meet the floor
  (the fountain's base, counter and table edges): the contact sharpening acts where it should.
- lucky22: 0.08 % vs 0.04 %; reflection alone 0.17 %.
- **Verdict: works, small at these defaults.** In both rooms most hits are far from the surface compared with the eye's distance,
  so the share is near 1 and the blur stays full. This becomes the main lever once the shared denoiser has a specular mode
  (rt-sun-core's RtDenoise, 2026-10-29: one channel, no hit distance yet). Until then ContactSpread can go lower (sharper sooner)
  at the price of more visible noise on rough floors. No measurable cost (a texture read in the blur; one more R16F target).

### 2026-10-29 18:58 foliage in reflections: noon, `[Main.Main.RayTracing] AlphaTested` on (fork-0029, DLL b618044a, 77079aa2)

The run is `bench/autotest/20261029-185845-slotA`, list rtrefl-3, all 9 shots. The core took the alpha-tested meshes (41877 met over 60
exchanges, atlas 40 slots, 1334 instances), and my pass ran as ReflectionsRTAlpha (the cut-outs tested through RtAlpha.glsl).
- **Only weakly verified.** Shots don't compare across launches: the two noon runs' SSR-only shots already differ on 41 % of pixels
  (light). In this run, RT vs SSR is 23.9 % of pixels, +0.37/255 (rtrefl-4 noon without foliage: 24 %, +0.52). Less brightening is
  what bushes blocking some of the sky in the wet ground's reflection would give. The alignment view cannot show it (a ground pixel
  the screen shows is never behind a bush). A direct check needs a view of a reflecting surface behind or under foliage: a puddle
  under a tree or a fence (a bench spot to find).
- **Cost:** helper passes 2.7-3.2 ms (noon without foliage: exchange 1.9-2.3 ms); scene rebuild 0.28-0.34 ms with 1334 instances.

### 2026-10-29 18:27 the ray budget (rtrefl-5; DLL 3142cb6b + shaders eed3580d): RoughShare + the key-lamp shadow ray only below roughness 0.25

The runs are `bench/autotest/20261029-182734-slotA` (lucky22) and `20261029-184453-slotA` (luxe), all 9 shots each. Budget = RoughShare 0.3 (above
roughness 0.3, one ray per 2x2 pixels a frame; the caster moves round the block, the others shade its hit point). Full = RoughShare 0.
- **Cost (helper passes, the RT CORE lines):** luxe **4.7 ms** (6.0-9.8 in rtrefl-4), lucky22 **5.4-5.7 ms** (8.9-11.2). The
  budget and the smooth-only lamp shadow take back what the lamp shadow ray cost, and the lamps stay. Exchange wait: luxe 5.6-6.0 ms,
  lucky22 6.5-7.8 ms. (The first try, 20261029-181234: exchange 6.8-8.2 ms budget vs 9.8-10.9 full at lucky22.)
- **Picture, budget vs full:** luxe 0.32 % of pixels vs a 0.14 % noise floor: the same. lucky22 3.3 % vs 0.07 %, mean -0.12/255
  (the budget a little darker; reflection alone 13.7 %, -0.33). The first try's flaw was fixed in eed3580d: a block whose caster the
  screen had answered gave its neighbours nothing to borrow. The fix barely moved lucky22 (3.5 -> 3.3 %), so what is left is not
  missing rays. Most likely it is the averaging: four pixels sharing one sample, then averaged in the compressed (Tame) space, give a
  slightly lower mean than four independent samples. Small; RoughShare can go up (rougher surfaces only) or to 0.

### 2026-10-29 17:41 lucky22, rtrefl-4 (DLL 42db45eb), all 11 shots

The run is `bench/autotest/20261029-174137-slotA`. Noise floor: 0.08 %.
- **HitLamps** (rt vs rt-nolamps): 10.4 % of pixels, +0.33 brighter. The casino floor's reflection of what the camera cannot see
  (the ceiling, the walls behind) now takes the red lanterns' light: a warmer red tint over the lower half, no blotches or odd
  colours. **RT vs SSR**: 9.7 % (0.24 % before lamps, run 20261029-050819): in a lamp-lit room the lamps are most of what RT adds.
- **VisibleNormals + RoughReach**: 4.65 %, -0.14.
- **MaxRoughness 0.8**: 1.4 %, +0.16.
- **Cost:** helper passes 8.9-11.2 ms (6.4 before), scene 0.6 ms, response 3.2-3.9 ms: the key-lamp shadow ray again (budget next).

### 2026-10-29 17:23 luxe, rtrefl-4 (DLL 42db45eb), all 11 shots

The run is `bench/autotest/20261029-172327-slotA`. Noise floor (pad-a vs pad-b): 0.20 %.
- **HitLamps** (rt vs rt-nolamps): 1.0 % of pixels, lamps add light (+0.04). They brighten a wall patch behind the fountain and
  floor areas reflecting what the camera cannot see.
- **VisibleNormals + RoughReach** (rt vs rt-old): 0.77 %, -0.02.
- **RT vs SSR**: 2.14 % (1.37 % with the earlier rays, run 20261029-043724).
- **MaxRoughness 0.8** (reflection alone): 3.2 %, +0.27 (brighter: more of the rough floor reflects the room).
- **Cost:** helper passes 6.0-9.8 ms (4.6 ms before this round), `Reflections trace (half)` 15-20 ms with RT (12.3 before), exchange
  7.8-11 ms. **Most of the increase is the key-lamp shadow ray**, one more ray per indoor hit. Next: trace it only on smooth
  surfaces (a sharp reflection shows the shadow; a rough one blurs it away), with the ray budget.
 (DLL 42db45eb: lamps, VNDF, RoughReach, the land self-hit skip, mask 0xEF)

The run is `bench/autotest/20261029-165859-slotA`, all 11 shots. Noise floor (pad-a vs pad-b): 10.5 % (wet ground), top half 0.4 %.
- **The self-hit skip works**: the amber blotches are gone (reflection alone, shot 08).
- **This corrects the first noon reading below.** Much of the "ground reflects the bushes and hills" darkening there was this
  artefact, not real occlusion. The desert bushes are alpha-tested, so they are not in the scene with `[Main.Main.RayTracing]
  AlphaTested` off (the core's default), and the open wet ground really sees the sky. With the fix, RT vs SSR is 24 % of pixels,
  **brighter** (+0.52/255): where a ray meets nothing, RT takes the sky without SSR's sky-visibility guess. What RT adds at a
  desert spot like this is small until foliage is in the scene (AlphaTested on + an alpha test in this pass: next).
- **VisibleNormals + RoughReach vs the earlier rays** (rt vs rt-old): 24 % of pixels, -0.52/255. The wet ground is near-mirror
  (WetRoughness 0.08), so this is the reach and sampling change on the dry, rougher patches.
- **HitLamps** (rt vs rt-nolamps): 10.8 % = noise. No lamps at noon outdoors, as expected; indoors next.
- **MaxRoughness 0.8** (reflection alone, shots 08 vs 10): 9 % of pixels, +0.007: rougher ground now gets rays.

### 2026-10-29 16:27 OUTDOOR A/B AT A STILL SPOT: scene noon (NVRAutoTest8, desert by pylons, sun high), forced rain wetness, rain particles off

Branch `claude/rt-refl-2` (from `claude/rt-test-merge` af12c7e7, DLL 429a8c6b, unchanged RT code). Run
`bench/autotest/20261029-162722-slotA`, list `rtrefl-3`, all 9 shots. The `noon` save loads in place, in first person and clear
weather, so the frame is still. The og-* spots teleport the player, and the player then moves.
- **Noise floor** (two RT-off shots): top half 0.19 % of pixels > 2/255 (sky, hills: still), bottom half 10.5 % (the wet ground's own
  temporal noise).
- **RT vs off**: 21.5 % of pixels, mean -0.97/255: **the wet ground gets darker where it should**. In the reflection-alone views
  (shots 06/07), SSR's wet ground mirrors bright sky almost everywhere, in front of the bushes and the hills too: the sky fallback
  leaks wherever the march misses. With RT, the ground reflects the bushes and the hills where they block the sky, and keeps the sky
  only where it is open.
- **Artefact found**: amber blotches low in the frame. Rays leaving the tessellated (displaced) ground hit the flat land mesh of the
  scene right away and reflect "ground". Fixed after the run: a land hit within 40 units + 1 % of the view distance of the start
  counts as the ground itself, and one more query starts past it (`Rt/ReflectionsRT.comp`, `LandSelfHit`). To verify in rtrefl-4.
- **Alignment** (shot 02): the ground and bushes are green. Red: the far roofs, rocks and hills, which are distant LOD objects / terrain
  beyond the core's SceneRadius (8192): expected.
- **Cost**: `Reflections trace (half)` 2.9-3.0 ms SSR only -> 5.4-6.0 ms RT (about +2.5-3 ms); exchange 1.9-2.3 ms.

### 2026-10-29 04:34 FIRST REAL RT REFLECTIONS, iGPU (AMD Radeon(TM) Graphics, 1280x720, FSR off), scene luxe, slot A

The run is `bench/autotest/20261029-043724-slotA` (list `rtrefl-2`, toml `evidence/rtrefl/rtrefl-igpu.toml`), with the shots in
`H:\_NVR-autotest-shots\20261029-043724-slotA`. It used DLL e2c3d06c: this branch on rt-sun-core `4ba2abfd` (the every-ray-misses
fix: the eye is `RtCore::Eye()`), fork-0028 and the helper v2. The plugin's time ran out after shot 09 of 13, so the iGPU is too slow
for the whole list.

- **The scene lines up with the screen.** On the alignment check (DebugView 5, shot 02), camera rays toward every reflecting
  surface hit that surface: it is green nearly everywhere. Yellow / red shows only on the actors and parts of the near plant.
  Actors are not in the scene (the core keeps statics only), and alpha-tested leaves are opaque or missing. So this is the expected
  limit, not a bug.
- **Rays hit.** Hit distance (DebugView 2, shot 07): near objects white, far ones grey, no "nothing within Distance" (red) inside
  the dome. Sources (DebugView 1, shot 06): the screen march (green) plus RT where it missed, mostly the estimate (magenta) on the
  walls and floor edges. That is hits hidden from the camera or never seen, now shaded with the draw's albedo and the hit
  normal (v2 records).
- **The picture.** RT on vs off (shots 04 pad vs 05 rt): 1.37 % of pixels differ by > 2/255, mean +0.009 (slightly brighter).
  The noise floor (off vs off 03/04, on vs on 01/05) is 0.25-0.33 %. The amplified difference image sits where it should: the
  polished checkerboard floor round the fountain now picks up more of the fountain's blue neon (rays the march stepped over or ran
  off screen with), plus a few wall patches. No wrong colours or leaks are visible.
- **Cost on the iGPU** (F10 p50, settled shots; correctness and relative cost only, this GPU is a tenth of a 1070):

  | | SSR only | RT on |
  |---|---|---|
  | `Reflections trace (half)` (RayGen + exchange + Resolve with RT) | 5.2-6.4 ms | 12.3 ms |
  | `Reflections RT (exchange)` (the game's GPU wait for the helper) | - | 5.57 ms |
  | helper GPU per frame (RT CORE line) | - | 4.9 ms: passes 4.63, scene rebuild 0.26 (structure builds 70 ms once at load) |
  | helper response (NVR's signal -> its submit) | - | 1.0 ms avg, 1.7 max |

  So RT costs about +6-7 ms at 720p on 2 RDNA2 compute units: one ray per missing half-res pixel plus one sun shadow ray per hit
  (indoors the sun ray is skipped). The rough rule (rays scale with compute units) gives about 0.5-1 ms on a mid-range RT card at
  1080p. That is an estimate, not measured.


### 2026-10-29 05:08 iGPU, scene lucky22 (Lucky 38 casino floor, MaterialAware tiles + metal), slot A, all 12 shots

The run is `bench/autotest/20261029-050819-slotA`, with the shots in `H:\_NVR-autotest-shots\20261029-050819-slotA`. Same build and settings.
- **Alignment** (shot 02): green over the floor, walls, rails, tables and ceiling. Red ("nothing hit") on a few columns at the
  left and the potted palm at the right: objects the core's scene leaves out. The palm has alpha-tested leaves. The columns are
  probably references the gather skips (it honours `NotCastShadows`, right for sun shadows and wrong for reflections). Requested
  from rt-sun-core: a gather flag so reflections get every opaque static, cast-shadows or not.
- **The reflection alone** (DebugView 1, shots 09 vs 10): RT adds reflection over the floor, with 15.5 % of pixels > 2/255,
  mean +0.63 (brighter: rays the march lost now find the room). **Final picture** (shots 04 vs 05): 0.24 % of pixels > 2/255 against a
  noise floor of 0.08 %, mean -0.02 (slightly darker). Where a ray now meets the room, the bright surroundings cube or sky
  fallback no longer leaks in. That is the "no leaks" effect, and the floor reads a little less washed out.
- **Cost:** `Reflections trace (half)` 4.5 ms SSR only -> 13.65 ms RT (+9 ms); exchange wait 7.5 ms; helper passes 6.41 ms, scene
  rebuild 0.40 ms (2677 instances, 279k triangles), response 1.65 ms.

### 2026-10-29 05:39 iGPU, og-lake (cliff lake shore), slot C, DLL 44d4bd03 (core 8a43b7d1, incl. the land fix 28ea9e77)

The run is `bench/autotest/20261029-053821-slotC`. **Not a usable A/B.** The player is in third person and moving, and there is a dust
storm: two RT-off shots differ on 26 % of pixels (> 2/255). The alignment check (shot 04) is still readable: rocks and mesquite
hit (green / white), and the flat ground in front is red. **The land is still not in the scene:** the core logs "land geometry
met 0 ... cells walked 540, without a land node 0, land node culled 0", so 28ea9e77 doesn't reach the land geometry yet
(reported to rt-sun-core). Objects: 209 instances, 82 geometries, 525k triangles; helper GPU 1.0 ms, my pass 0.9-1.0 ms (few
reflecting pixels outdoors in dry weather).

### 2026-10-29 06:17 iGPU, og-goodsprings with forced rain (wet road), slot C, DLL 44d4bd03

The run is `bench/autotest/20261029-061648-slotC`; the plugin stopped after shot 08 of 13. **Not a usable picture A/B**: the rain
particles and moving clouds put two RT-off shots 62 % of pixels apart. **Alignment (shot 04)**: the road, the ground, the houses
and the rocks are green, so the land is in the scene here (Goodsprings' cells have full land; og-lake's ground is LOD terrain,
which rt-sun-core's 18d27859 adds). Two kinds of patches:
- **blue** patches on the ground: the traced land is nearer than the screen's surface. NVR's land tessellation displaces the
  ground on the GPU, while the scene holds the undisplaced mesh. That costs reflections nothing visible: the hit is the same
  ground, within the screen-match tolerance a little farther on.
- **red** on a pole top and a far roof edge: alpha or thin geometry left out.

Cost: `Reflections trace (half)` 2.0-3.4 ms SSR only -> 6.0 ms RT (about +3 ms); exchange 3.05 ms; helper passes 2.35 ms, scene 0.15 ms
(686 instances, 811k triangles with land, 125 MB); response 0.46 ms.

### 2026-10-29 07:02 iGPU, rtrefl-3 (rain particles off) at og-lake and og-goodsprings, DLL 7835bac1 (core incl. ecf5defe)

The runs are `bench/autotest/20261029-070247-slotC` (og-lake, all 9 shots) and `20261029-072126-slotC` (og-goodsprings, 6 of 9).
- **og-lake alignment (shot 02)**: the ground slabs that were red now hit, pale blue / white. That is a blend of "hit" and
  "slightly nearer": the LOD terrain the core falls back to here sits a little off the full-detail ground. The rocks hit as before.
- **Outdoor picture A/B still impossible in the slot spots**: two RT-off shots differ on 31 % (og-lake) and 51 % (og-goodsprings) of
  pixels even with the rain particles off. In these spots the light and the player move between shots (the player is mid-jump at
  og-lake). The reflection-alone view at og-lake differs by only 1.5 % between SSR and RT (few reflecting pixels in dry weather). A clean
  outdoor A/B needs a still outdoor test spot: a save with the player standing, weather and time frozen. That is a harness item,
  noted for the bench owners.

### Where the bench stands against "done" (2026-10-29 06:30; og-lake row updated 07:30)

| Scene | Alignment (scene = screen) | Picture A/B | Cost on the iGPU (720p, 2 CUs) |
|---|---|---|---|
| Ultra-Luxe (polished floor) | yes | **yes**: the floor picks up the fountain's neon; 1.37 % px vs floor 0.3 %; no wrong colours / leaks | +6-7 ms |
| Lucky 38 (metal + tiles) | yes (left columns, palm missing: gather filters) | **yes**: 15.5 % more reflection on the floor, cube/sky leaks gone | +9 ms |
| og-lake (water) | yes since ecf5defe (LOD ground hits, a little off the real ground) | no (light and player move: 31 % noise) | ~+1 ms |
| og-goodsprings rain (wet road) | yes (land in) | no (51-62 % noise even without rain streaks) | +3 ms |

Water: the game's planar water reflection is untouched by design (see "Water"), so og-lake checks only the shore and rocks.
Still to do for a full "done": a clean outdoor A/B needs a still outdoor test spot (a save with the player standing, weather and time
frozen); the existing og-* spots cannot give one. The gather gaps (actors, alpha-tested leaves, no-shadow statics) are rt-sun-core's.

**Summary of the two interiors:** RT fills SSR's gaps and blocks the fallback's leaks, with no wrong colours seen. It costs +6-9 ms at
720p on the 2-CU iGPU; the helper's ray passes are most of it.

### 2026-10-28 12:47 SplitTest, GTX 1070, scene luxe (Ultra-Luxe floor), fork d3d9 nvr-dxvk-zresolve, slot A

Run `bench/autotest/20261028-124747-slotA`, shots `H:\_NVR-autotest-shots\20261028-124747-slotA`; list `rtrefl-split`, toml
`evidence/rtrefl/rtrefl.toml` (InteriorFloors 0.6, MaterialAware 1, FSR off), DLL a91b819b. The log shows
`pass ReflectionsRayGen ready (25 bindings, 3 targets)` and `pass ReflectionsRTResolve ready`, with no fallback line.

| Pair (2 shots apart, same noise frame) | pixels > 2/255 | mean |
|---|---|---|
| old-1 vs old-2 (noise floor) | 1.57 % | 0.587 |
| old-1 vs split-1 | 1.33 % | 0.556 |
| old-2 vs split-2 | 1.43 % | 0.568 |
| reflection alone (DebugView 1) old vs split | 0.07 % | 0.146 |
| sources (DebugView 6) old vs split | 0.59 % | 0.202 |

**Same picture**: the split differs from the old trace no more than the old trace differs from itself.
**Cost of the split** (F10 `Reflections trace (half)`, p50 over 120 frames per shot): old 0.140-0.142 ms, split 0.185-0.186 ms, i.e.
**+0.045 ms** for RayGen's two extra targets (then RGBA32F; now G32R32F) and the Resolve pass. Rays come on top of that. The whole effect is
0.64 -> 0.69 ms p50.
The rain scene (wet road) was refused: slim slot A has no save NVRAutoTest7. It goes into the iGPU run on a full slot.
