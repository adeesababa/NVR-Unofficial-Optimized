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
- Not run: the ticket (1028-150751-bd86) was cancelled while waiting, when RT work was paused (below).
- Ran 2026-10-29 (run 20261029-035120-slotC, og-goodsprings; a first try on lucky22 was an interior: the sun user never ran).
  **Cause found:** "RT sun constants" logged origin 71228.7 -2581.8 -8547.4 with camera position -71229 2582 8547: the ray
  origin `Pointers::Generic::CameraLocation - CameraPosition` is minus the eye's world position, because CameraLocation reads ~0
  here. Every ray started ~71000 units outside the camera-relative scene. The scene itself is right: the helper's readback of the
  first three bottom levels matches NVR's CPU vertices exactly (door: first vertex -2.8 -40.3 -71.6 on both sides, bounds inside
  the model bound, no NaN), transforms are camera-relative (a few thousand units). DebugView 5: depth, normals and the sun are fine
  (ground = traced free, back faces = facing away, sky = no depth).
- **Fix 4ba2abfd:** `RtCore::Eye()` = (0, 0, 0), the eye in scene space (the scene is relative to CameraPosition, the NiCamera's
  world translation); the sun pass uses it. Sent to rt-reflections, rt-ao-sky and rt-lamp-shadows (their origins used the same
  formula). Verification: rtsun-5 (rtsun-3's four headings + DebugView 6).

### rtsun-5 (run 20261029-044643-slotC, after the Eye() fix; startup fix f8662fff/f6480bfd in)
- **Rays hit.** DebugView 6 (camera rays) draws the scene where the screen has it: car, house, fence, windmill, rocks, at the right
  depth. DebugView 4: the house and fence cast traced shadows on the ground; the faces seen here face away from the sun (the
  view looks toward it) and stay white, as designed (the light term already darkens them).
- **No land in the scene:** the ground and the hills are white in DebugView 6, so terrain casts no traced shadow yet. rtsun-6 logs
  why AddGeometry leaves land out (counts by reason).
- `player.setangle z` did not turn the third-person view (all four headings show one picture): rtsun-6 moves the sun instead.
- Cost at 1280x720 on the iGPU: "Ray tracing (copies + helper + waits)" p50 2.49-2.55 ms (rays now hit and the helper does
  real traversal; it was ~1.35 ms while every ray missed at once), helper GPU 3.8 ms avg incl. the first frame's builds,
  passes 1.50 ms, scene 0.13 ms; 644 instances, 200 geometries, 782k triangles.
- rt-reflections confirms on their side (Ultra-Luxe, 20261029-043724-slotA): camera rays hit every reflecting surface except
  actors and leaves.

### rtsun-6 (run 20261029-050116-slotC; the sun moves: GameHour 9 / 12 / 16)
- **Matches the cascades for what is in the scene:** at 9:00 the house porch, the car and the crate cast traced shadows where the
  cascades put them, with no acne on the ground (NormalBias 2 + 0.0005/unit, StartBias 1 hold). The car's side shows thin
  self-shadow bands from its trim (real geometry, also hard-edged: one ray, no softening).
- **Missing casters:** land (hills cast nothing; see below) and everything alpha-tested (foliage, fences, power-line wires:
  56100 of 99960 object geometries met in 60 exchanges were left out for alpha; 2400 not a lighting shader, 2040 decal /
  refraction, 780 no data). Mode 0 keeps those through the cascades.
- **Land:** "land geometry met 0". The walk's reach test rejected every land node (their world bounds are not usable; NVR's
  cascade walk does not test land either). Fix 28ea9e77: land skips it; the loaded grid limits it. Verification: rtsun-7.
- Cost: "Ray tracing" p50 2.66-2.85 ms at 720p (one 6.0 ms window while GameHour jumped and the cascades rebuilt); helper GPU
  1.76 ms avg after the builds (passes 1.63 ms, scene 0.13 ms), response 0.42 ms.

## Paused here (2026-10-28 15:20, on the user's word: the normals session finishes first)
- **Works:** the RT core end to end on the iGPU (9800X3D, RDNA2) through the 64-bit NvrRtHelper.exe: feature detection, the
  helper starts and is ready in ~1 s, BLAS from the game's D3D9 buffers (fork patch 0028 StageGeometry -> arena -> helper),
  a per-frame camera-relative TLAS (649 instances / 197 geometries / 779k triangles at Goodsprings), the trace pass, the
  visibility back into NVR, DebugViews 1-6, Mode 0 / Mode 1. Cost: ~1.35 ms a frame at 720p on the iGPU (exchange + waits).
  Off by default; off = the old path exactly.
- **Bug:** every ray misses (DebugView 4 all white at four headings), and the RT reflections / AO sessions see the same: a
  shared fault in the core, not the sun pass. Ruled out by reading: masks, the instance transform (= CreateD3DMatrix),
  ShadowFade, buffer device addresses (DXVK enables BDA on every buffer), the order (depth and normals are rendered before
  RtCore::Frame), the flush-before-signal order.
- **Best lead:** the fork's StageGeometry conversion (game VB/IB -> arena) is the one untested link; second, the pass inputs
  (the trace pass takes only 0.33 ms for 921k pixels, so most pixels may not trace at all).
- **rt-reflections' alignment check (run 20261028-150909-slotA, Ultra-Luxe):** camera rays from p.origin toward each pixel's
  depth-rebuilt surface (tMin 1, tMax 2x the distance + 500, mask 0xFF) miss on 100% of pixels, with no reflection inputs
  involved. So the scene itself is wrong (positions from the arena, the transforms / space, or an empty TLAS in practice),
  not the sun pass inputs. This points to the first lead; DebugView 6 + the probes in rtsun-4 should say which.
- **Next step:** run bench list foundations\liststsun-4.txt as is (the diagnostics are committed, 435889eb; DLL and helper
  built; the helper copy is in bench\deep-roadmaptsun\extra). DebugView 6 white -> the scene: compare "RT CORE probe" (NVR log)
  with "probe geometry" (NvrRtHelper.log); DebugView 6 depth-like -> the inputs: read DebugView 5's codes and "RT sun constants".
  The proper normals buffer (real-surface-normals session) can replace TESR_NormalsBuffer afterwards; it does not cause the bug.

## Open problems / next
- Alpha-tested casters (foliage, fences, chain link) and actors are not traced: Mode 0 keeps them through the cascades.
- Soft shadows: one hard ray per pixel; the contact shadows' blur and the cascades' PCF soften the combined result in Mode 0;
  Mode 1 is hard-edged. A cone of rays (sun's angular size) + a denoise is the next step if wanted.
- RenderScale / FSR: the trace runs at the full screen size and assumes the world fills the screen (RenderScale's rect frames
  draw the world into the top-left part: not handled yet; the bench runs have FSR off).
- Cost on a real RT GPU unknown (the iGPU only shows relative cost).
