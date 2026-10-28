# RT reflections (NVR DXVK fork, hybrid ray tracing item 4 of 4)

Session rt-reflections, branch `claude/rt-reflections-499161` (from `claude/p84v3-tessring` 03b62a5a). No push, no install.
The shared acceleration structures and ray-query entry point come from session rt-sun-core (`docs/next/FORK-RT-CORE.md`).
This pass only reads them.

## Status

| Item | State |
|---|---|
| Shader `Compute/ReflectionsTraceRT.frag` + `Compute/ReflectionsRT.glsl` | written, compiles (glslang, Vulkan 1.2, spirv-val OK) |
| C++ (`effects/Reflections.cpp`, `core/RtCoreClient.h`), settings `[Shaders.Reflections.RT]` | written; see the build line below |
| RT core API (TLAS binding, offset, per-instance records) | **waiting on rt-sun-core**; `RtCoreClient.h` answers "no RT" until then |
| iGPU bench (list `bench/foundations/lists/rtrefl-1.txt`, scenes water / rain / luxe / lucky22) | not run (needs the core) |

## What it does

The existing Reflections trace (half resolution, a fixed-step screen-space march plus 5 bisection steps) runs first.
`ReflectionsTraceRT.frag` is `ReflectionsTrace.frag` line for line, plus one RT block.

1. **When a ray is cast.** Only where the screen march left part of the answer missing (confidence below 1: no hit, a hit near the
   screen edge, or past the far end of the ray). The surface must be no rougher than `MaxRoughness`; RT fades in from 75 % of it.
   Where the march answered fully, nothing is cast: SSR first, a ray only where SSR misses.
2. **The ray.** It starts at the surface, lifted along the normal by 1 + 0.15 % of the depth (the depth buffer's own error far
   away), in the same GGX-jittered direction as the march (a new jitter every frame). One closest-hit `rayQueryEXT`, opaque, up
   to `Distance`.
3. **A hit** fills the whole missing share, and the first source that can show it is used:
   - **visible on screen** (the hit, projected, matches the depth buffer there within `ScreenTolerance` of its distance, and is not
     the weapon): the lit picture at that pixel. This is exact. It is what the march would have found if its steps or its
     thickness guess had not hidden it (thin poles, stepped-over edges, the far end of the ray);
   - **out of view** (behind the camera, off the sides): the surroundings cube, looked up toward the hit from the camera. The cube
     is captured from about there, so it shows what was seen in that direction. Its `a` channel is how much of it was ever seen,
     and the rest goes to the estimate;
   - **in view but hidden behind something**, or never seen: the **estimate** (below). The cube toward a hidden hit would show
     whatever hides it, i.e. a wrong colour.
4. **No hit within Distance**: the old fallbacks (cube along the ray, light grid, sky). A ray that went the whole Distance open
   also takes the sky without the sky-visibility guess (`Skylighting` reflection term): RT has shown the sky is not hidden there.
5. **Leaks fixed by construction.** Today a ray the march loses takes the cube or the sky, even under a roof or through a wall
   (the sky in the floor of a covered porch, the cube's bright window in a floor facing a wall). With RT, a ray that hits
   anything never reaches those fallbacks.

Everything after the trace is unchanged: temporal history (Temporal 0.85), the edge-aware blur (wider on rough surfaces), the
firefly cap, the composite (Fresnel, MaterialAware metal colour, SpecularOcclusion). That chain is the denoiser for rough
surfaces: one ray per half-resolution pixel per frame, jittered by GGX, then accumulated and blurred. Rougher than MaxRoughness
gets no rays (blurred reflections gain little from exact hits and cost the same per ray).

Roughness comes from what the trace already uses: MaterialAware's `_rmaos` material buffer where a surface has one, else the
Dry/WetRoughness of the floor guess.

### Settings `[Shaders.Reflections.RT]` (all off / safe by default)

| Key | Default | |
|---|---|---|
| Enabled | false | needs the NVR DXVK fork, the Vulkan passes and the RT core up on a GPU with `VK_KHR_ray_query` |
| MaxRoughness | 0.5 | rougher: old fallbacks only |
| Distance | 6000 | game units |
| ScreenTolerance | 0.02 | share of the distance for "the hit is the pixel the screen shows" |
| EstimateBrightness | 1.0 | |
| SunShadows | true | the estimate's sun through a shadow ray |
| DebugView | 0 | 1 sources (green march, cyan RT on screen, blue RT via cube, magenta estimate, orange known material, grey old fallbacks); 2 hit distance (red: open); 3 RT without the march; 4 colour per hit instance |

SSR and the cube remain the fallback and the A/B path (Enabled false = the exact old trace, the old `.spv`).

### Detected off

`ReflectionsEffect::RtWanted()` = Enabled, the Vulkan passes not failed, the RT pipeline not refused, and `RtCoreClient::Ready()`
(the core's TLAS built this frame). On the GTX 1070 (no `VK_KHR_ray_query`) the core never reports ready. The trace then picks
the same pipeline as before (the plain trace or the Hi-Z walk) with the same bindings and constants, so the picture and the cost are
unchanged. RT DebugViews only change the effect's DebugView while RT runs.

## Hit shading: how far it can go here

What a ray query gives at the hit is geometry only: distance, instance custom index, primitive index, barycentrics, front face.
NVR is D3D9 through Vulkan. The material at the hit point (which texture, which UVs, which shader constants, which lamps) was
bound by the game for **that draw**, possibly in a pass that has long finished, and DXVK only sees it as a draw's descriptor
state. That is the hard part, and it decides the tiers:

| Tier | What it needs | State |
|---|---|---|
| 0 Screen at the hit | nothing more (depth + the lit picture) | **done**: exact where the hit is visible, the common case for SSR's misses on screen |
| 1 Cube toward the hit | the existing surroundings cube | **done** for out-of-view hits; parallax-free (cube centred near the camera), low-resolution, a little behind in time |
| 2 Estimate | sun direction/colour, the cube's blurriest mip as ambient, one shadow ray | **done** as a grey 0.4-albedo surface; its normal is guessed as facing the ray. Correct brightness order, wrong colour |
| 3 Per-draw albedo | the core keeps, per BLAS instance, the average colour of the draw's stage-0 texture (its 1x1 mip, read once when the BLAS is made) | **requested** from rt-sun-core (`rtHitAlbedo`); the shader already reads it when `rt3.x` = 1 |
| 4 Hit normal | the triangle's vertices: `VK_KHR_ray_tracing_position_fetch`, or vertex/index buffer addresses per instance | **requested** (`rtHitNormal`); RDNA2 drivers may lack position fetch, buffer addresses work everywhere |
| 5 Textured albedo | bindless index of the diffuse texture + the UV stream (format, offset, stride) per instance; DXVK knows both at draw time | later; most of the "full shading" gap |
| 6 Lamps at the hit | NVR's light lists (tiled lamps / ShadowsInteriors lamp array) readable from the pass + shadow rays per lamp | later; indoors the estimate's ambient (cube) carries the lamps' light roughly until then |
| 7 The game's own shading | the draw's pixel shader and constants re-run at the hit: not possible from a ray query in D3D9-through-Vulkan without recompiling every game shader as a callable | **no** |

So: on-screen hits are exact; off-screen hits are as good as the cube; hidden hits are a plausibly lit grey until tiers 3-4 land,
and approximate (no texture detail, no lamps beyond the ambient) until tiers 5-6. A reflection that gets full-quality shading
everywhere would need the game's materials re-expressed per draw (what RTX Remix does by replacing the game's materials). That is out of scope.

## Water

The game's water already reflects through its own planar reflection render (WATER*.pso, `ReflectionMap`). That map is exact
for what the game draws into it, and NVR's `ReflectionClipFix` fixed its clipping. The Reflections trace skips water pixels
on purpose ("water reflects on its own"). RT can only add here by replacing the water shader's reflection sample, which is in the
game's D3D9 pixel shader (phase-2 SPIR-V replacement territory), or by a second layer over water pixels that would double the
planar image. Not done in this pass. The water scene stays in the bench list to check that nothing changes on water (no leaks
onto it) and to measure shore and dock surfaces next to it.

## Requests sent to rt-sun-core (2026-10-28 ~12:00)

1. TLAS binding for an NVR fragment pass (bind type, set/binding; my pass is the existing `ExtDrawTo` graphics trace).
2. TLAS space vs NVR's camera-relative world (`RtCoreClient::Offset`).
3. What a hit returns; position fetch or vertex buffer addresses for the hit normal.
4. Per-instance record buffer indexed by `instanceCustomIndex`: average albedo per draw first, later a bindless diffuse index + UV
   stream; flag bits for alpha-tested / foliage / skinned / water / sky. The sky dome and water must not be in the TLAS, or
   must be maskable: a reflected ray must not stop at the sky sphere.
5. Its shadow-ray include (this pass does its own one-line shadow query meanwhile).
6. The harness: `SLOT_DXVK_DEVICE` (found in `run_slot.sh`, added by rt-sun-core) and `SLOT_RES=1280x720`, plus its fork build folder.

## Build / files

- `src/hlsl/NewVegas/Compute/ReflectionsTraceRT.frag`, `ReflectionsRT.glsl` -> `ReflectionsTraceRT.spv`
  (`tools/nvr-dxvk/compile-compute.sh` now builds ray-query shaders for Vulkan 1.2, the rest as before for 1.1).
- `src/core/RtCoreClient.h`: the only place written against the core's API.
- `src/effects/Reflections.{h,cpp}`: settings, `RtWanted`, the RT pipeline and bindings (uniform slot 2, TLAS slot 0).
- `resource/NewVegasReloaded.dll.defaults.toml`: `[_Shaders.Reflections.RT]`.

## Results

(not yet: needs the RT core; see Status)
