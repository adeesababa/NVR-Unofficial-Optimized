# Captured surface normals (2026-10-28)

Tester macintroll's proposal: write the real interpolated vertex normals into a screen buffer while the world draws, instead of
rebuilding them from depth. Branch `claude/captured-normals` (from `claude/p84v3-tessring` 5c547109). Snow is the first user.

## Why

Every screen-space effect reads `TESR_NormalsBuffer`, which `Normals.fx` rebuilds from the depth buffer: one flat normal per triangle,
noisy at edges and under MSAA / FSR. That is behind triangle-shaped snow on rocks (macintroll), the cloud-shadow crust on rocks,
striped facing terms, and probably the FSR circle's cascade acne. The surfaces' own shaders know the smooth normal; they now write it.

## How it works

| piece | file |
|---|---|
| the target, the draw hook, the main-list span | `src/core/CapturedNormals.h` |
| template side (`CapturedNormalOut`, COLOR3) | `src/hlsl/NewVegas/Shaders/Includes/CapturedNormals.hlsl` |
| reader side (`CapturedNormal(uv, distance, n)`) | `src/hlsl/NewVegas/Effects/Includes/CapturedNormals.hlsl` |
| writers | `ObjectTemplate.hlsl` (both PS families), `ParallaxTemplate.hlsl`, `SkinTemplate.hlsl`, `TerrainTemplate.hlsl` |
| hook points | `Render.cpp` GrassFirst::MainListHook (`CaptureSpan`), `Render.cpp` frame start, `ShaderRecord.cpp` (define + writer list), `TextureManager.cpp` (`TESR_CapturedNormalsBuffer`), `ShaderManager.cpp` (`TESR_CapturedNormalsData`) |

- **Target**: A16B16G16R16F, the scene target's size and multisampling, bound as **render target 3** only around the game's own
  `RenderBatches(1, 0x25B)` call (the early grass part is outside). Cleared to 0 (= nothing) with ColorFill at the start of the list.
  - rg: the normal, octahedral (`ShadowEncodeNormal`), world space, turned to face the camera;
  - b: camera distance (length of the camera-relative world position);
  - a: 1 = written.
- **What is written**: the smooth interpolated vertex normal, after skinning / cloth / tessellation (land with BendNormals: the bent
  normal), **before** the normal map:
  - objects: the SmoothObjectNormals normal already sent by the vertex shader (`IN.smoothNormal`, or `lightDistSq.w / lightDir.w /
    vertexColor.a`, or `uv.zw` in the 4-6 lamp family);
  - parallax: `uv.zw` now carries the vertex normal (octahedral, only with the define; the displacement layout has no channel for it and
    writes nothing);
  - skin: `IN.normal`;
  - land: `IN.normal` (the land is not rotated).
  The added lamp / highlight passes, the slot-tight six-lamp shader (SLS2035), the displacement layout and SmoothObjectNormals off
  write a = 0. ps_3_0 needs COLOR0..3 contiguous, so COLOR1 / COLOR2 are written as 0 where the template did not write them already
  (discarded: nothing is bound there, except the skin blur's own targets during skin draws, which the skin template fills as before).
- **Who writes (the draw hook, DP / DIP / DPUP / DIPUP / Clear)**: only the template pixel shaders compiled with the define (noted by
  ShaderRecord), drawn into the span's scene target, with colour writes on and either no blending or the land's SRCALPHA / INVSRCALPHA
  fade (terrain blending's Smooth; the template writes alpha 1 = a replace). Everything else gets `COLORWRITEENABLE3 = 0`: the game's
  own shaders, additive lamp / highlight passes, other blends, depth-only draws. A draw into any other target (copies, masks, shadow
  maps) and every Clear has the target unbound (MRT sizes must match; Clear clears every bound target); the next writing draw binds it
  again. LinearScene's full-screen conversions unbind targets 1-3 themselves.
- **MSAA**: a multisampled copy, resolved (StretchRect) after the list. On native D3D9 multisampled extra targets draw nothing (measured
  by SkinScattering on a GTX 1070), so with MSAA and no NVR DXVK the capture is skipped (one log line). Edges resolve to a < 1 and fall
  back.
- **RenderScale v2**: nothing special: the world draws into the target's top-left part like into the scene's, and effects read it with
  the same rect coordinates as the depth buffer. With `LowResEffects` off the frame is upscaled before the effects:
  `TAAEffect::UpscaledBeforeEffects()` then clears `TESR_CapturedNormalsData.x` and readers fall back.
- **Validity (readers)**: `CapturedNormal(uv, distance, n)` is true only when a = 1 and the stored distance matches the pixel's
  (`|b - d| <= 0.4 % d + 2 units`, `TESR_CapturedNormalsData.yz`). Anything drawn over the list's surfaces later (water, alpha,
  grass, decals, particles), the game's own shaders (about 9 % of the list's draws in the LinearScene census) and the first person fall
  back to the depth-rebuilt normal.
- **Compile switch**: `CAPTURED_NORMALS_ON` is defined only when a reader's switch is on at startup (`CapturedNormals::CompiledIn`).
  Off: no define, the 222 template shaders are byte-identical to before (offline D3DX43 check, exterior), nothing hooked, no target.
  On at startup, it is live afterwards: the capture runs only on frames where a reader wants it (snow on, snowing / snow lying,
  outdoors), the hook passes everything through otherwise.

## Snow (the first reader)

`[Shaders.SnowAccumulation.Main]`:
- `CapturedNormals = false`: the slope test (where snow may lie, world z) and the lighting (with `snow_NRM.dds` on top) take the
  captured normal where valid; elsewhere the depth normal with `NormalSmoothing` as before. Restart to compile it in; live after.
- `CapturedNormalsDebugView` (0-2): 1 = the normal the snow takes (bright: captured, dim: fallback), 2 = how much captured and
  depth-rebuilt differ (white = 90 degrees).
- `Look` (0-1): 1 = macintroll's version 5 of the snow shader (shared 2026-10-28; techniques 2 and 3, his names prefixed `SA5_`,
  his blue noise as `Textures\Effects\Snownoise256.dds`), also reading the captured normals in place of his depth smoothing. Always
  a D3D9 pass (only Look 0 has an NVR DXVK pass). His CoverMap.fx change (drifts and sun melt compiled out) was a test on his side and
  is not taken: `PersistentDrifts = 0` / `PersistentShade = 1` do the same.

Both the D3D9 technique and the NVR DXVK pass (`Compute/SnowAccumulation.frag/.spv`, binding 8 + two uniforms) read it.

## Opting other effects in (later; each with its own A/B setting, off by default)

1. Declare the sampler in the effect, in declaration order (samplers bind by order, not by register):
   `sampler2D TESR_CapturedNormalsBuffer : register(sN) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };`
   then `#include "Includes/CapturedNormals.hlsl"`.
2. Where the effect reads `GetWorldNormal(uv)`: `float3 n; if (!(Switch && CapturedNormal(uv, length(toWorld(uv) * readDepth(uv)), n))) n = GetWorldNormal(uv);`
   (view-space readers: `mul(n, (float3x3)TESR_ViewTransform)`-style back to view, or compare in world space).
3. C++: add the effect's switch to `CapturedNormals::CompiledIn()` (startup) and to the `wanted` expression at the
   `CapturedNormals::BeginFrame` call in Render.cpp (per frame), so the capture runs when any reader wants it.
4. NVR DXVK passes: bind `CapturedNormals::Texture` as one more sampled image and read it with `texelFetch` (see
   SnowAccumulation.frag `capturedNormalRaw`).

Candidates:
| effect | what it gains | where |
|---|---|---|
| SunFar `CascadeLight` (far sun shadows) | normal offset / slope bias per smooth surface, not per triangle: less cascade acne in the FSR circle | `ShadowsExteriors.fx` / SunFar |
| cloud shadows | the facing term on rocks without the crust | `CloudShadows` in ShadowsExteriors |
| AmbientOcclusion / AmbientLight | GTAO and bounce from the real normal (no facets on low-poly rocks, characters); replaces NormalSmoothing taps where valid | `AmbientOcclusion.fx`, `AmbientLight.fx` |
| PointShadows darkening, Specular, Reflections | smooth facing terms | their `.fx` |

## Self-check and the agreement guard

- **Self-check** (main session's warning: P31 MergedDepthNormals' two-target draw was silently dropped by native D3D9 on a GTX 1070):
  on the 60th, 300th and 1200th captured frame (then every 18,000th) the target is read back at 64 x 64: empty right after the clear,
  some texels written after the list. Any failure turns the capture off for the session with a log line; the readers fall back.
- **Agreement guard** (`CapturedNormalAgrees`): a captured normal is taken only within ~60 degrees of the depth-rebuilt one
  (`TESR_CapturedNormalsData.w` = 0.5). The first run showed a few thin pieces at a slab's edge (og-desert) whose captured normal was
  90+ degrees off, which turned to black diamonds in the snow. Which shader draws them is not known yet (follow-up); DebugView 2 shows
  them red.

## Cost

Per frame with it on: one fp16 screen-target clear, one 8-byte write per template pixel, a few render-state sets per draw, with MSAA a
resolve. F10 profile per shot (og-desert, GTX 1070, NVR DXVK; 2-3 samples each, noisy):

| | world scene (game), capture off -> on | Snow accumulation, depth normals -> captured |
|---|---|---|
| FSR 0.667 | 1.430 -> 1.429 ms | 1.497 -> 1.205 ms |
| native res | 2.400 -> 2.200 ms | 1.543 -> 1.518 ms |
| looking down | 2.583 -> 2.347 ms | 2.140 -> 1.702 ms |

The capture is below the noise (well under 0.1 ms); snow gets cheaper (the depth smoothing taps are skipped where the normal is captured).

## Run

Lists `bench\foundations\lists\capnormals-1-desert.txt` (FSR on and off, Look 0 and 1, debug views) and `capnormals-2-msaa.txt`
(SLOT_MSAA 4), og-desert, NVR DXVK fork (zresolve), RUN_TOML `bench\deep-roadmap\tools\run-toml-capnormals-20261028.toml`
(the user's toml + SnowAccumulation on, `[Shaders.Snow.Main] ForceSnow`, Increase 0.02, CapturedNormals on at startup).
Note: older snow lists set `ForceSnow` under `[Shaders.Precipitations.Main]`; the code reads `[Shaders.Snow.Main]`, and
SnowAccumulation is off by default, which is why those runs showed no snow cover.

Results (2026-10-28, branch c1684e95):

| run | where | result |
|---|---|---|
| 20261028-152942-slotC | NVR DXVK fork, FSR 0.667 on / off, Look 0 / 1 | PASS. Self-check ok (0 / ~1,015 of 4,096 texels). ~160-370 draws per frame write normals, 7-94 masked. Snow on the jagged rock slab: depth normals put translucent triangle patches of snow over the steep sides; captured: sides bare, snow on the tops only, clean edges; no triangles. FSR: same. LowResEffects off: the guard (falls back). |
| 20261028-153748-slotC | NVR DXVK fork, game MSAA 4x | PASS. Scene draws normally with the multisampled target bound; self-check ok; view as without MSAA. |
| 20261028-153413-slotC | native D3D9 | PASS, but the capture never runs: 0.5 draws per frame reach the hook. **LinearScene's draw hook also sees ~0 draws on native** (2.4 vs ~1,400 on the fork): the device vtable hooks do not see the game's draws there. Snow = as before (fallback). Pre-existing for LinearScene; needs its own look. |
| 20261028-152222-slotC | (first try, before the guard) | all my shots taken; the game crashed in the queue's hand-over moveto to a second, batched list (game code 0x008F644F), not in NVR; reruns were solo. |

Pictures: `H:\_NVR-autotest-shots61028-152942-slotC-capnormals1` (and the other run folders).

## Files to copy (manual install)

- `NewVegasReloaded.dll` (+ `.pdb`), `NewVegasReloaded.dll.defaults.toml` -> `<game>\Data\NVSE\Plugins\`
- under `<game>\Data\Shaders\NewVegasReloaded\`:
  - `Shaders\Includes\CapturedNormals.hlsl` (new), `Shaders\ObjectTemplate.hlsl`, `Shaders\ParallaxTemplate.hlsl`,
    `Shaders\SkinTemplate.hlsl`, `Shaders\TerrainTemplate.hlsl`
  - `Effects\Includes\CapturedNormals.hlsl` (new), `Effects\SnowAccumulation.fx.hlsl`
  - `Compute\SnowAccumulation.frag`, `Compute\SnowAccumulation.spv`
- `Textures\Effects\Snownoise256.dds` -> `<game>\Data\Textures\Effects\` (Look 1 only; already in the user's install, same file)
- To try it: `[Shaders.SnowAccumulation.Main] CapturedNormals = true` and restart. If a shader change does not seem to take effect,
  clear `Shaders\NewVegasReloaded\Shaders\Cache\`.
