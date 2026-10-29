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
