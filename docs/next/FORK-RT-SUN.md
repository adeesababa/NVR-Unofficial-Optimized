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

### rtsun-2
(pending)

## Open problems / next
- Alpha-tested casters (foliage, fences, chain link) and actors are not traced: Mode 0 keeps them through the cascades.
- Soft shadows: one hard ray per pixel; the contact shadows' blur and the cascades' PCF soften the combined result in Mode 0;
  Mode 1 is hard-edged. A cone of rays (sun's angular size) + a denoise is the next step if wanted.
- RenderScale / FSR: the trace runs at the full screen size and assumes the world fills the screen (RenderScale's rect frames
  draw the world into the top-left part: not handled yet; the bench runs have FSR off).
- Cost on a real RT GPU unknown (the iGPU only shows relative cost).
