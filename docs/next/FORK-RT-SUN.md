# RT sun shadows (item 1 of the hybrid ray tracing plan), 2026-10-28

Branch `claude/rt-sun-core-aa6d6d` (from `claude/p84v3-tessring` 03b62a5a). Built on the ray tracing core: FORK-RT-CORE.md (read
that first: rays run in the 64-bit NvrRtHelper.exe because Windows' 32-bit drivers have no ray tracing). Off by default.

## What it does
`[Shaders.ShadowsExteriors.RT] Enabled`: outdoors, one ray per pixel toward the sun against the core's scene (the opaque static
geometry of the loaded cells: land, buildings, rocks, props), from NVR's depth and normal buffers. The visibility (0/1) goes into
the sun shadow buffer's red channel right after the contact shadows:
- **Mode 0 (combine, default):** multiplied in (fork pass `RtSunCombine`, blend multiply, red only). The cascades and the contact
  shadows stay exactly as before, the rays add what the maps miss (thin casters, contact, long shadows at grazing sun, the
  precision of a map texel). Alpha-tested casters (leaves, fences), trees' leaves and actors still come from the cascades.
- **Mode 1 (replace):** inside `Range` the cascades give way to the rays: the object shaders' forward cascade lookups are switched
  off (TESR_ShadowForwardData.x = 1 while Mode 1 is in force), SunShadows runs its deferred technique, and its apply fades the
  cascades in only over the last 15% of `Range` (TESR_RtSunReplace / the Vulkan apply's flags.w), where the rays fade out.
  Then the rays are multiplied in as in Mode 0. Casters the core does not trace (alpha-tested, actors) cast no sun shadow inside
  `Range` in this mode.
- `DebugView` 1 = the traced shadow alone in the buffer (cascades and contact left out), 2 = traced but not applied (the cascades'
  picture with the rays' cost), 3 = distance to the caster (white = free or 4096+ units).

Files: `src/core/RtSunShadows.{h,cpp}` (the core user: settings, the trace pass's constants, the combine pass),
`src/hlsl/NewVegas/Compute/Rt/RtSunShadow.comp` (+ .spv; runs in the helper), `src/hlsl/NewVegas/Compute/RtSunCombine.frag`
(+ .spv), `Effects/SunShadows.fx.hlsl` + `Compute/SunShadowsApply.frag` (+ both .spv) for Mode 1, hooks in ShaderManager.cpp (the
RT point, the technique choice, the combine after the contact shadows), ShadowsExterior.cpp (the forward switch, the constant),
SunShadows.cpp (flags.w).

## The ray
- Position: as SunShadows.glsl's `toWorld()`: view z = depth x farZ; scene position = origin + (forward + right x (2u - 1) / P00 -
  up x (2v - 1) / P11) x view z (scene space = world minus the camera position; origin = CameraLocation - CameraPosition).
- Normal: the normals buffer's view-space normal turned into scene space (its components along right / up / forward).
- Pixels facing away from the sun, the sky, and pixels beyond `Range`: visibility 1 (the lighting's N.L does the rest).
- Bias: start = position + normal x (`NormalBias` + `NormalBiasPerUnit` x view z) + sun x `StartBias`. Ray length `RayLength`.
- Sun direction: the cascades' own (TESR_SmoothedSunDir), so both agree while the sun steps; no rays while the sun's shadows are
  faded out (dawn / dusk).

## Settings (`[Shaders.ShadowsExteriors.RT]`, all live)
Enabled false, Mode 0, Strength 1.0, Range 6000, NormalBias 2.0, NormalBiasPerUnit 0.0005, StartBias 1.0, RayLength 30000,
DebugView 0. The scene: `[Main.Main.RayTracing]` SceneRadius 8192, ArenaMB 16.

## In-game runs (9800X3D iGPU, 1280x720, slot C, fork 0001-0028, NVR DXVK passes on, FSR off, clouds / AL / fog off)
### rtsun-1 (run 20261028-130508-slotC, pictures H:\_NVR-autotest-shots\20261028-130508-slotC)
- **iGPU smoke check: PASS.** DXVK on "AMD Radeon(TM) Graphics" (device filter through SLOT_DXVK_DEVICE), NVR's extension 1.6,
  the picture drawn right (shot `cascades`). It was dusk-dark: the list used an overcast weather by mistake (fixed in rtsun-2).
- The helper started at the first RT shot, found ray tracing on the iGPU, imported the arena, then **refused the 1280x720 depth
  image** (the 32-bit and 64-bit drivers lay out such images differently). The core switched itself off with the reason in the
  log and the game went on with the cascades: the off path works as designed. Fixed: shared images are rounded up to 256 texels
  (FORK-RT-CORE.md), verified texel for texel offline for 720p .. 4K.
- The run hit the autotest plugin's 600 s limit at shot 13 of 15 (the iGPU is slow): the list was shortened for rtsun-2.

### rtsun-2 (run 20261028-132822-slotC)
- The helper is ready ~1 s after RT is switched on; the first frame with all builds took 1.19 s (later: preallocation, ~10 ms).
- GPU PROFILE "Ray tracing (copies + helper + waits)": p50 ~1.4 ms at 720p on the iGPU (the exchange: depth + normals in,
  the trace, visibility out, the game waiting for the helper).
- No visible difference between Mode 0 and the cascades: ForwardShadows (the user's setting) samples the cascades in the object
  shaders, so DebugView 1 cannot show the rays alone. DebugView 4 was added for that.

### rtsun-3 (run 20261028-145222-slotC; the first try 20261028-144215 hit the 600 s limit at shot 1)
- RT CORE at exchange 60: 649 instances, 197 geometries, 778909 triangles, 120 MB; helper GPU 2.67 ms avg incl. the first
  frame's builds (118 ms), scene 0.13 ms, passes 0.38 ms. Later frames: helper GPU 0.47 ms (passes 0.33 ms).
- "Ray tracing" p50 1.34-1.37 ms per frame (p90 1.40-1.44), the same at every heading.
- **DebugView 4 is white everywhere at all four headings: no ray hits anything**, while the cascades show long shadows (16:00).
  The other RT users see the same in their runs (rt-reflections: 1130 instances, every ray misses; rt-ao-sky: the same).
  The offline end-to-end test (synthetic geometry through the helper) passes, so the fault is in what the game gives the
  scene or the pass: the fork's StageGeometry conversion (game vertex buffers -> arena) is the one link no test covers on this
  machine (the in-process test only takes the unsupported branch on the iGPU), or the pass inputs (depth, normals, sun).
  0.33 ms for 921k pixels on the iGPU also suggests most pixels never trace.

### rtsun-4 (the no-hit hunt)
- DebugView 5: per-pixel state codes (0.1 no depth / sky, 0.2 sun down, 0.3 beyond the range, 0.45 facing away, 0.6 traced hit,
  0.8 traced free). DebugView 6: a primary ray from the camera through each pixel, hit distance / 8192 (white = no hit): the
  scene alone, independent of depth, normals and the sun.
- One-time probes: NVR logs "RT CORE probe" for the first three geometries it stages (vertex counts, BaseVertexIndex, the VB
  chip offset, stride, position offset, model bound, the CPU copy of vertex 0 when kept, the camera-relative translation);
  the helper logs "probe geometry" with the same ids read back from its staging buffer (first vertices, bounds, indices, the
  instance transform). Matching the two says whether the arena holds the game's positions.
- (results below when the run is in)

## Open problems / next
- Alpha-tested casters (foliage, fences, chain link) and actors are not traced: Mode 0 keeps them through the cascades.
- Soft shadows: one hard ray per pixel; the contact shadows' blur and the cascades' PCF soften the combined result in Mode 0;
  Mode 1 is hard-edged. A cone of rays (sun's angular size) + a denoise is the next step if wanted.
- RenderScale / FSR: the trace runs at the full screen size and assumes the world fills the screen (RenderScale's rect frames
  draw the world into the top-left part: not handled yet; the bench runs have FSR off).
- Cost on a real RT GPU unknown (the iGPU only shows relative cost).
