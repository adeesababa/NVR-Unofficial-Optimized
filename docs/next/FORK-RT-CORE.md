# The ray tracing core (fork patch 0028 + NvrRtHelper.exe + RtCore), 2026-10-28

Branch `claude/rt-sun-core-aa6d6d` (from `claude/p84v3-tessring` 03b62a5a). Shared by the four RT items: sun shadows
(FORK-RT-SUN.md, this branch), AO / sky light, interior lamp shadows, reflections (sibling sessions). Everything is off by default:
with no RT effect on, nothing starts and nothing changes.

## The one finding that decided the design
**Windows' 32-bit Vulkan drivers have no ray tracing.** A 32-bit MinGW program enumerating device extensions on this PC
(`vkext32.exe`, 2026-10-28) sees on "AMD Radeon(TM) Graphics" (driver 25.10.36) no `VK_KHR_acceleration_structure`, no
`VK_KHR_ray_query`, no `VK_KHR_ray_tracing_pipeline`; the 64-bit `vulkaninfo` on the same GPU lists all three. The GTX 1070 has
none in either. Fallout New Vegas, NVR and the fork's d3d9.dll are 32-bit, so **no ray query can run inside the game process**.
(NVIDIA RTX in 32-bit: not testable here; assume the same. RTX Remix uses a 64-bit bridge for the same reason.)
The 32-bit AMD driver does have `VK_KHR_external_memory_win32`, `external_semaphore_win32` and timeline semaphores, so fork
patch 0026's sharing (built for the DLSS helper) works there.

So the scene and every ray live in **NvrRtHelper.exe**, a 64-bit process on the same GPU (modelled on NvrDlssHelper, FORK2-DLSS.md),
and the game only hands it triangles, inputs and instance lists, all through GPU memory and timeline semaphores. Nobody waits on the
CPU per frame.

## Changes since the first version (all on this branch; announced to the RT sessions)
- d71ecf96 **protocol v2**: per-instance records (binding 10) and the geometry data (binding 11), see the shader section.
- d71ecf96 **shared images are rounded up to multiples of 256 texels** (both sides). Measured: AMD's 32-bit (game) and 64-bit
  (helper) drivers lay out an optimal-tiling image identically only then (1280x720 R32F: 3686400 bytes in the game, 3957104 in the
  helper; the first in-game run's helper refused it and the core switched itself off, as designed). The content is the top-left
  Width x Height; `CopyOut` copies only that; give the pass the logical size in its constants, never `imageSize()`.
  `rt_helper_test.cpp` (RT_TEST_SCREEN / RT_TEST_GAME) checks a pattern texel for texel at 720p, 1080p, 1440p, 4K, 1366x768,
  1680x1050 through the padded images.
- d71ecf96 the helper allocates its first structure block and build scratch before it says ready (first frame 10 ms, was 2-4 s on the
  iGPU with the game running); NVR's watchdog allows 10 s.
- b53a3e1d `RtCore::Frame` runs before the shadow branch (indoors too); `RtCore::SetMaskHook`.
- d7fa5427 (rt-reflections) `RtCore::Exchange(Device, user)`: a second exchange of the frame for one `Point = 1` user, called by that
  user between its own passes (reflections: ray generation -> exchange -> resolve). One ring record per exchange (its own semaphore
  values); the scene is gathered once per game frame, a later exchange resends the instances + records with no geometry ops and no
  arena. A second GPU bubble only while such a user is on.

- 4ba2abfd **`RtCore::Eye()`** (= 0, 0, 0): the eye in scene space, for every ray origin from the camera. Do NOT use
  `Pointers::Generic::CameraLocation` (it reads ~0 in game: rays started ~71000 units outside the scene, every ray missed).

## Pieces
| where | what |
|---|---|
| `tools/nvr-dxvk/patches/0028-*.patch` ("1.8-rt", `ID3D9NvrExtRt`) | **Geometry staging** (every GPU with the fork): `MeasureGeometry`, `StageGeometry`, `FlushStagedGeometry` convert a D3D9 VB/IB range (float3 / float4 / half4 positions, any stride and offset; 16/32-bit lists and strips, base vertex; no index buffer) by device address (a compute shader, `d3d9_nvr_rt_convert.comp`) into float3 positions + a uint triangle list inside a shared R32F image used as raw words (the *arena*). Plus an **in-process** path (`CreateGeometry` / `CreateScene` / `BuildScene`, ray queries in NVR's own passes by the scene's address) for drivers that do offer RT to 32-bit programs (Linux RADV): opt-in with dxvk.conf `nvr.rayTracing = True`, default off (the Vulkan device is then created exactly as without the patch). DXVK edits: the three extensions + features, their entry points, `DxvkContext::nvrBeginRawCommands` (ends the pass, flushes DXVK's barriers, a full memory barrier, returns the exec command buffer), `nvrEndRawCommands`, `nvrTrack*`, a `friend` line for `FlushBuffer`. |
| `tools/nvr-rt-helper/` (MIT) | `NvrRtHelper.exe` (VS2019 x64, needs only the Vulkan SDK), `NvrRtProtocol.h` (the shared block, both sides include it), `test/rt_helper_test.cpp` (end-to-end test, below). |
| `src/core/RtCore.{h,cpp}` | NVR's side: starts the helper when an RT effect wants it, the shared images, the scene gathering, the per-frame exchange, the watchdog. |
| `src/core/RtSunShadows.{h,cpp}`, `src/hlsl/NewVegas/Compute/Rt/RtSunShadow.comp` | The first user (FORK-RT-SUN.md). Copy it for a new RT effect. |

## How a frame goes
At **one point of the frame** (`RtCore::Frame`, called in `ShaderManager` right before the exterior sun contact shadows: the depth
and normal buffers exist, the shadow buffer is next), when at least one user's `Wants()` is true:
1. **Scene** (CPU, `Core::Gather`): every loaded cell (exterior grid, or the player's interior cell), its land node and its
   references' node trees, culled to `[Main.Main.RayTracing] SceneRadius` around the camera (bound spheres). Kept: geometry with a
   lighting shader property, buffer data, no skin, no alpha test / blend, not a SpeedTree leaf, not a decal / refraction, not
   faded out, the active child of a switch node; references that may cast shadows (`NotCastShadows` honoured), not actors.
   Each geometry is keyed by its buffer data + VB + IB + counts (two placements of one model share one geometry).
   - New geometry: staged into the arena (`StageGeometry`) as long as the arena has room this frame (`ArenaMB`, default 16 MB
     = about 600k triangles a frame), with a `BUILD` op; the rest waits for the next frames. The helper builds it once.
   - Every frame: an instance (camera-relative 3x4 transform from `RenderManager::CreateD3DMatrix`, custom index 1 = land / 0 =
     object, cull mask bit 0 = land / bit 1 = object, both faces).
   - Not met for 120 frames: a `RELEASE` op (the helper frees it once no frame in flight can use it).
2. **Users record**: `RtCore::CopyIn(image, D3D9 texture/surface)` (a fork copy into a shared image, recorded now),
   `RtCore::AddPass(shader, images, writeMask, constants, groupsX, groupsY)`, `RtCore::CopyOut(image, D3D9 surface)`.
3. The core records `FlushStagedGeometry`, writes the frame record (ops, instances, passes) into the shared ring, then
   `SignalShared(inputsReady, N)` (a DXVK flush), `WaitShared(outputReady, N)` (a GPU wait in the command list being recorded)
   and the copies out. The helper (woken by an event) copies the record out, then submits: GPU wait inputsReady >= N, the
   arena's rows into its staging buffer, all new bottom-level builds in one call, the top level from the instances, the passes,
   signal outputReady = N. Users read their outputs after the core's point (e.g. in the next pass of the frame).

The arena is reused every frame: frame N+1's staging is recorded after frame N's `WaitShared`, so the helper has copied it out.

## Writing an RT effect (what the siblings need)
### NVR side (copy `RtSunShadows.cpp`)
```cpp
#include "RtCore.h"
namespace {
  int imageIn = -1, imageOut = -1, shader = -1;
  bool Wants() { return /* your setting on, the right cell type */; }
  void Setup() {   // before each helper start (and after a resolution change): your shared images
    imageIn  = RtCore::AddImage("NVR RT ao depth", w, h, NVR_EXT_SHARED_R32F);
    imageOut = RtCore::AddImage("NVR RT ao", w, h, NVR_EXT_SHARED_R16F);
  }
  void Record(IDirect3DDevice9* Device) {
    RtCore::CopyIn(imageIn, someD3D9Texture);                 // blit when the formats differ (red of G32R32F -> R32F, etc.)
    const int images[2] = { imageIn, imageOut };
    RtCore::AddPass(shader, images, 2, 0x2 /* writes images[1] */, &constants, sizeof(constants), (w + 7) / 8, (h + 7) / 8);
    RtCore::CopyOut(imageOut, yourD3D9RenderTargetSurface);   // ready for any pass recorded after RtCore::Frame
  }
}
void Register() {                                             // once, before the first RtCore::Frame (ShaderManager does it for the sun)
  shader = RtCore::AddShader("RtMyEffect");                   // Data\Shaders\NewVegasReloaded\Compute\Rt\RtMyEffect.spv
  RtCore::AddUser({ "RT my effect", Wants, Setup, Record });
}
```
Formats: `NVR_EXT_SHARED_RGBA16F`, `RG16F`, `R32F`, `RG32F`, `RGBA8`, `R16F` (patch 0026). Up to 16 shared images in all (the
arena is one), 8 passes a frame, 8 images a pass, 512 bytes of constants a pass, 16 shaders. Image index 0 is the arena: never
pass it. The interior lamp effect: `Wants()` may be true indoors; the scene gathering covers the player's interior cell then.

### Helper side: the trace shader (GLSL, compute, `glslangValidator -V --target-env vulkan1.2`)
```glsl
#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;          // the core's scene
layout(set = 0, binding = 1, std140) uniform Pass { /* your constants, <= 512 bytes */ } p;
layout(set = 0, binding = 2, r32f) uniform readonly image2D depthImage;        // images[0] of the pass
layout(set = 0, binding = 3, r16f) uniform writeonly image2D outImage;         // images[1] ...
void main() {
  rayQueryEXT q;
  rayQueryInitializeEXT(q, scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.0, dir, tMax);
  while (rayQueryProceedEXT(q)) { }
  bool hit = rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT;
  ...
}
```
- Bindings 2 + i are storage images (GENERAL layout), declare the image format; there are no samplers (imageLoad only).
- Scene space: world minus `TheRenderManager->CameraPosition` (Gamebryo units, z up). Pass what you need to rebuild positions in
  your constants; `RtSunShadows.cpp` / `RtSunShadow.comp` show the depth-buffer reconstruction that matches SunShadows.glsl's
  `toWorld()` (view z = depth x farZ; forward + right x (2u - 1) / P00 - up x (2v - 1) / P11; view normal from the normals buffer
  as right / up / forward components) and the camera-to-scene origin offset.
- `rayQueryGetIntersectionInstanceCustomIndexEXT`: 1 land, 0 objects. Cull mask: see the table below.

### Hit shading (2026-10-29, claude/rt-sun-2 824c7ecc)
- `Compute/Rt/RtHit.glsl`: `rtHitTriangle`, `rtHitObjectPosition`, `rtHitObjectNormal` (object space: turn it with
  rayQueryGetIntersectionObjectToWorldEXT), `rtHitHasTexcoords`, `rtHitTexcoord`, `rtHitAlbedo` (linear colour). With
  `[Main.Main.RayTracing] HitTextures` on, `#define RT_ALBEDO_ATLAS <rgba8 image>` and add `RtCore::AlbedoAtlasImage()` to the pass's
  images to get the texture's colour; else the record's average. Works with RtAlpha.glsl included too (either order).
- Records: flag 0x40 = textured (texcoords at floatBitsToUint(albedo.w), atlas slot = flags >> 16, shared with the alpha atlas).
- Glow (2026-10-30, claude/rt-sun-2): `[Main.Main.RayTracing] GlowTextures` (off; needs HitTextures, a game restart) puts each textured mesh's glow map (BSShaderPPLightingProperty texture 2) into a glow atlas (RGBA8, 2048 x 2048) at its diffuse slot (slots are (diffuse, glow map) pairs). `rtHitGlow(instance, primitive, bary)` in RtHit.glsl: the linear glow colour, 0 without a glow map (record flag 0x04); `#define RT_GLOW_ATLAS <rgba8 image>` and add `RtCore::GlowAtlasImage()` to the pass's images for the map's colour at the hit (only the lit parts: neon letters, lamp globes, window panes), else a glowing mesh returns rtHitAlbedo (the whole mesh, the old way). Raw map colour: the mesh's emissive colour / multiplier are not applied (the caller's brightness setting scales it). Check: [Shaders.ShadowsExteriors.RT] DebugView 8 (indoors too), log "glow maps in the atlas N". rtsun-30 PASS 20261030-095551-slotA (Lucky 38: 15 glow maps, the Bar sign's lettering, globe lamps' lower halves, ceiling slots and slot machine lights glow, the rest black).

### Instance mask bits (the one source; agreed 2026-10-29 by the main session and the RT strands)
| Bit | Who sets it | Meaning |
|---|---|---|
| 0x01 | core (default) | land (full land and LOD land) |
| 0x02 | core (default) | objects (opaque statics) |
| 0x04 | rt-lamp-shadows (`SetMaskHook`, `MaskGlass`) | lamp glass |
| 0x08 | rt-lamp-shadows (`SetMaskHook`, `MaskLampModel`) | lamp models |
| 0x10 | core, `[Main.Main.RayTracing] AlphaTested` (`NVR_RT_MASK_ALPHA`) | alpha-tested casters: NON-OPAQUE, record flag 0x20; replaces the hook's mask for those instances |
| 0x20 | core, `[Main.Main.RayTracing] Actors` (`NVR_RT_MASK_ACTOR`) | skinned actors (NPCs, creatures, the player's 3rd-person body), posed every frame (fork patch 0033) |
| 0x40-0x80 | free | ask in the RT sessions first, then add a row here |

Rays that do not test alpha use a cull mask without 0x10 (e.g. **0xEF**); otherwise alpha-tested cards act as solid quads. Rays
that test alpha keep 0x10, drop `gl_RayFlagsOpaqueEXT` and confirm candidates with `rtAlphaPass()` (`Compute/Rt/RtAlpha.glsl`, the
atlas = `RtCore::AlphaAtlasImage()` among the pass images).
- Geometry is opaque only; no texture coordinates yet.
- Protocol v2 extras (always bound):
  ```glsl
  struct Record { vec4 albedo; uint flags; uint geometryId; uint vertexOffset; uint indexOffset; };
  layout(set = 0, binding = 10, std430) readonly buffer Records { Record records[]; };     // by rayQueryGetIntersectionInstanceIdEXT
  layout(set = 0, binding = 11, std430) readonly buffer Geometry { uint words[]; };        // positions (float3) + triangle lists (uint)
  Record r = records[rayQueryGetIntersectionInstanceIdEXT(q, true)];
  uint p = rayQueryGetIntersectionPrimitiveIndexEXT(q, true);
  uvec3 i = uvec3(words[r.indexOffset + 3 * p], words[r.indexOffset + 3 * p + 1], words[r.indexOffset + 3 * p + 2]);
  vec3 v0 = vec3(uintBitsToFloat(words[r.vertexOffset + 3 * i.x]), uintBitsToFloat(words[r.vertexOffset + 3 * i.x + 1]),
                 uintBitsToFloat(words[r.vertexOffset + 3 * i.x + 2]));   // object space; rayQueryGetIntersectionObjectToWorldEXT
  ```
  `albedo` = linear rgb of the draw's diffuse texture (its smallest mip; managed textures; DXT1/3/5 and ARGB8), `flags` 0x01 land,
  0x10 albedo known (else grey 0.5); `vertexOffset` = 0xFFFFFFFF when the helper had no room (128 MB of geometry data).

### Deploy (by hand, like every NVR file)
- `NvrRtHelper.exe` -> `<game>\Data\NVSE\Plugins\NvrRt\` (its own folder: xNVSE loads every DLL right in Plugins). It writes
  `NvrRtHelper.log` next to itself (or in %TEMP%).
- The fork `d3d9.dll` with patch 0028 -> the game folder.
- `Compute\Rt\*.spv` -> `<game>\Data\Shaders\NewVegasReloaded\Compute\Rt\`.

## Settings
`[Main.Main.RayTracing]`: `SceneRadius = 8192` (units, live), `ArenaMB = 16` (needs the helper to restart). The effects have their
own sections (`[Shaders.ShadowsExteriors.RT]` ...).

## Verified (2026-10-28, this PC: 9800X3D iGPU "AMD Radeon(TM) Graphics", driver 25.10.36)
- `tools/nvr-dxvk/test/nvr_rt_test.cpp` (32-bit, the fork, in-process path): "not supported" branch PASS (GetRtInfo Supported 0,
  `CreateGeometry` -> D3DERR_NOTAVAILABLE) = what every Windows GPU gets, the GTX 1070 included. The supported branch (in-process
  BLAS/TLAS, ray queries by address) is written but cannot run on any Windows driver: untested on hardware.
- `tools/nvr-rt-helper/test/rt_helper_test.cpp` (32-bit, through the fork's d3d9.dll on the iGPU, the real `NvrRtHelper.exe` as its
  child): three geometries (float3 + uv with a 16-bit list; float4 with base vertex 2 and a 32-bit strip starting at index 1;
  half4 without an index buffer) staged at odd arena texels (mid-row, next row), built in the helper, a scene with a null and an
  unknown instance, one ray per grid point up and down: **every ray's instance id and distance exact** (1e-3); then a moved
  instance + a released geometry with a stale reference to it, 300 frames of instance churn, a geometry rebuilt in place under its
  id: all PASS. Helper GPU 0.095 ms a frame for that toy scene (builds 0.17 ms when there are any, scene 0.07, pass 0.02);
  response (NVR's write -> the helper's submit) 0.08 ms average. A first-use pipeline compile took ~2 s on the iGPU (the game's GPU
  would wait for it): the helper now compiles every registered shader before it says ready.
- `NvrRtHelper.exe --probe`: GTX 1070 "no VK_KHR_ray_query", AMD iGPU "ray tracing OK".
- Build: fork with 0001-0027 + 0028 (WSL MinGW, the same series as CI), NVR DLL (VS2019, no new warnings), helper (VS2019 x64).
- v2 (records + geometry data) and the padded images: the same end-to-end test also checks every hit's record (geometry id) and
  the hit triangle's first vertex fetched from binding 11 (object-space z exact), and the screen-size pattern (above): PASS.
- In game: see FORK-RT-SUN.md (bench runs on the iGPU): the helper starts, gets ready in ~1 s and runs every frame; the exchange
  costs ~1.4 ms of the game's GPU time at 720p on the iGPU with the sun pass.

## Rules kept / known limits
- GTX 1070 / any GPU without RT for 64-bit programs: the helper says "no ray tracing on the game's GPU", the core logs it once and
  stays off; nothing else changes (the fork's device creation is untouched by default, the extension calls are never made).
- The helper must run on the GPU DXVK uses (LUID match); a laptop's other GPU is not used.
- Geometry identity is pointer-based (buffer data + VB + IB + counts). A pointer freed and reused for different geometry with the
  same counts within 120 frames would keep the old triangles until released: not seen; watch for "ghost" shadows after cell loads.
- Geometry whose vertex buffer the game rewrites on the CPU (morphs) keeps its first shape: not traced correctly (rare in FNV
  statics; actors are excluded for that reason).
- The core runs once a frame at one point; an effect that needs results earlier in the frame (before the sun shadows) must use the
  previous frame's output or a second exchange (not built: one exchange keeps the GPU bubble to one).
- GPU bubble: the game's queue waits for the helper's work (inputs ready -> helper -> output ready) at that point. On the iGPU
  everything is one GPU anyway; the cost is the helper's GPU time plus the copies.

## Next (for whoever needs it)
- Alpha-tested geometry (foliage, fences): needs UVs + the alpha texture in the helper (an any-hit-style candidate loop in the
  ray query with a texture lookup). The arena could carry UVs; textures would need sharing (or a per-geometry alpha mask image).
- Skinned actors: per-frame refit from the skinned positions (the fork would need the skinned vertices: DXVK has them only after
  the vertex shader; a compute skinning pass in the fork from bone matrices is the clean way).
- Reflections: material data per geometry (albedo texture, normals) for shading hits; or hit-position output and shading in NVR.
- Compaction of the bottom-level structures (memory), and a refit path for moving objects instead of rebuilds.
