# Fork round 2: real displacement instead of parallax (tessellation), first step (2026-10-27)

Branch `deep/fork2-displace` (from `mine/nvr-dxvk-compute` 34a8df4d). Fork patch **0021** (`tools/nvr-dxvk/patches/0021-...`),
extension "1.7-displace" as a SEPARATE interface (`ID3D9NvrExtTess`, GUID ...d321), so `ID3D9NvrExt` and its version number are
untouched and the merger can renumber freely. Everything is off by default; nothing has run in the game yet.

Builds v3 (2026-10-27, after bulk round 2, push data): NVR DLL `G:\_NVR-build-scratch\fork2-displace-out\NewVegasReloaded.dll`
(sha256 498BBA3A6AA56E0F...; the shaders in src/hlsl/NewVegas/Compute/Tess must be deployed with it); fork d3d9.dll = CI run
40533723125 (0001-0011 + 0021 v3, green), `G:\_NVR-build-scratch\nvr-dxvk-fork2-displace-v3\nvr-dxvk-x32\d3d9.dll` (sha256
014842C828B69ED7...). Use that folder as FORK_D3D9 with list fork2-displace3-bulk.txt.

Builds v2 (2026-10-27, after bulk round 1, strips): NVR DLL `G:\_NVR-build-scratch\fork2-displace-out\NewVegasReloaded.dll`
(sha256 01DBB22C0632B22C..., MSVC 2019, no new warnings); fork d3d9.dll = CI run 40524970205 (0001-0011 + 0021 v2, green, no warnings in
the new files), `G:\_NVR-build-scratch\nvr-dxvk-fork2-displace-v2\nvr-dxvk-x32\d3d9.dll` (sha256 30EBFA8D3ACD49DD...). Use that folder as
FORK_D3D9 for the bulk test (round 1's nvr-dxvk-fork2-displace, CI 40441667857, skips strips; nvr-dxvk-0011 has no tessellation).

## Bulk test round 1 (2026-10-27 10:14, d15eb79d) and the fix: the land comes as triangle STRIPS
Runs 20261027-101406-slotC (og-goodsprings) / 101624-slotC (og-desert): interface found, SLS2100.vso + 28 pixel shaders registered
and attached ("shaders 1 VS, 28 PS"; the "(00000000)" in the register line is the HRESULT S_OK, not a hash), but "per frame 0.0
land draws tessellated, 7.0 not (strips/fans)": every land draw matched the family and was skipped by the topology test, because
the game draws its land quadrants as triangle strips (TERRAIN PROBE lines confirm SLS2100.vso with SLS2124/2132/2140/2142.pso).
Fix (patch 0021 v2, no index buffer read or written): a strip of n indices from DrawPrimitive / DrawIndexedPrimitive is drawn as
three patch lists over its index windows from index 0, 1 and 2 (3 x floor((n - 3 - k) / 3 + 1) indices each): window k's patch p
is strip triangle k + 3p, so the three windows hold every strip triangle exactly once (checked offline for n = 3..399). The TCS
gets the window in its push data (ControlParams[7], DXVK's own), swaps the first two control points of odd strip triangles (their
winding is reversed in a strip) and drops degenerate ones (strip joins: two equal corners; tessellation level 0). Strips of the UP
draws and fans stay untessellated, and the first such draw is named in DXVK's log with its primitive type. While a pair's
interface check is still pending, every draw rebinds (so a refused pair always gets its own topology back).

## Bulk test round 2 (2026-10-27 11:23, v2) and the fix: push data overlapped D3D9's shared block
Run 20261027-112347-slotC (Goodsprings): 7 land draws a frame tessellated, nothing refused, but with it on the near land went flat
dark grey (on / strong / ccw / debug shots 31-42 % of pixels differ from off; noise 5.9 %), while s0 (Strength 0, FixedFactor 1)
matched off. dbg-factor showed no splitting at all (all blue) and dbg-wire only the plain 128-unit grid.
Cause: the TCS and TES declared their push-constant blocks from offset 0. In DXVK 3.0 offsets 0-31 are the SHARED push block of a
graphics pipeline (D3D9's fog colour / alpha ref / fog parameters, declared for every graphics stage by DXVK's D3D9 shaders), and
DXVK's push-offset remap (DxvkShaderBindingMap::mapPushData) gives the shared block offsets 0-15 first. So the TCS read MaxFactor,
EdgeLength, EdgeGrowth and FadeStart from D3D9's fog data (MaxFactor ~0: never split) and the TES read its sampler heap indices from
it (random samplers, garbage heights: the land sank by up to 8 units into the flat land's sun shadow: dark). s0 never samples and
never splits, so it stayed right. DXVK's own D3D9 shaders put their stage blocks at MaxSharedPushDataSize (32): that is the rule.
Fix (patch 0021 v3 + shaders): both blocks start at shader offset 32 (`layout(offset = 32)`, DxvkPushDataBlock at
MaxSharedPushDataSize, sampler block offsets 32 + 2i; the host still pushes at the stage's offset 0). Also: the pixel shader's
projectionPosition (its forward sun shadows and fog) stays the flat land's, since the shadow maps are not displaced; Center now
defaults to 0 (bumps only rise, nothing sinks below the flat land's shadow). New debug views 4-6 (uv, layer weights, normal as the
vertex colour) prove the pixel shader's inputs arrive interpolated. Displacement is now Strength x (relief x weight + offset x fade):
the relief (height - 0.5) goes to 0 on quadrant borders, the offset (0.5 - Center) only fades with distance, so with Center 0 a
border keeps the average raised level of the land on both sides (no groove along it). Test list: fork2-displace3-bulk.txt.

## What it does (plain English)
Parallax only makes the ground LOOK bumpy: the surface is still flat, so its outline, its depth (fog, AO, reflections, shadows on it)
and its edges against the sky stay flat. With `[Main.Main.Performance] NvrDxvkDisplacement` on, the NVR-built DXVK splits every land
triangle near the camera into up to 16 x 16 small ones (Vulkan tessellation; your GTX 1070 has it, D3D9 has nothing like it) and moves
each new point up or down by the land texture's own height map: the same height map NVR's land parallax already reads (the diffuse
alpha of land textures that carry one, e.g. Vanilla Plus Terrain). Stones, ruts and tracks get real shape. The land's lighting shader
is unchanged and simply runs on the bumpy surface. Far land (past FadeEnd, 2048 units = 29 m) is drawn exactly as before.

First step = one shader family: the land (TerrainTemplate.hlsl: vertex shader SLS2100.vso, all 28 land pixel shaders SLS2092-2146).
Rocks and walls (objects) are the next family (see the plan).

## Settings (defaults in resource/NewVegasReloaded.dll.defaults.toml)
- `[Main.Main.Performance] NvrDxvkDisplacement = false`: the switch. Needs a restart to turn ON (the land's shaders are registered
  with DXVK as they load); off works at once. Native D3D9, stock DXVK, or a fork without patch 0021: nothing is registered, nothing
  changes (log line says why).
- `[Shaders.Terrain.Displacement]` (all live): `Strength = 16.0` (game units between lowest and highest point; 0 = the game's own
  triangles exactly), `Center = 0.0` (since v3: the height that stays at the flat land; 0 = everything rises, nothing sinks below the
  flat land's sun shadow; 0.5 keeps feet right on average), `EdgeLength = 8.0` (spacing of the new points next to the camera; the land grid is 128), `EdgeGrowth = 0.01`
  (spacing added per unit of distance), `MaxFactor = 16`, `FadeStart = 1024`, `FadeEnd = 2048`, `BorderGrid = 2048` (bumps fade to 0
  on land quadrant borders: each quadrant is its own draw with its own textures, so a crack could open there), `LodBias = 0`,
  `FixedFactor = 0` (test), `DebugView = 0` (1 wireframe, 2 height as grey, 3 split count), `Winding = 0` (test).

## How it works
Fork (patch 0021, new files `src/d3d9/d3d9_nvr_tess.{h,cpp}`, small hooks in `d3d9_device.{h,cpp}`, `d3d9_shader.h`, `meson.build`,
the API header):
- NVR registers (before the D3D9 shader is created, like 1.2's replacements) a vertex shader + its TCS under a family number, and
  pixel shaders of the family + their TES (one per winding). `D3D9DeviceEx::CreateShaderModule` attaches the registration to the
  D3D9 shader object (`D3D9CommonShader`), computing where the listed vertex shader constants (ModelViewProj c0-c3) sit in DXVK's
  compacted constant buffer.
- `D3D9DeviceEx::ApplyPrimitiveType` (the end of every PrepareDraw) asks `D3D9NvrTess::ApplyDraw`: when the bound VS and PS share a
  family, the draw is a triangle list (or a DrawPrimitive / DrawIndexedPrimitive strip) and SetTessState enabled it, it binds the TCS + TES and a patch-list topology (3 control
  points) and pushes the parameters; otherwise it unbinds them (only when they were bound) and the normal path runs. Strips
  get three patch lists (see "Bulk test round 1"); fans and UP-draw strips are counted and left alone. One branch per draw while
  nothing is registered.
- The VS stays DXVK's own conversion of the game's bytecode. The TCS reads its outputs at the D3D9 semantic locations (vec4 each),
  the TES writes what the PS reads. At a pair's first draw the CS thread checks both interfaces against DXVK's shader metadata; a
  mismatch is logged and that pair always draws untessellated (the safety net).
- The TES reads the PS's own textures and samplers (only the ones the PS uses: the hazard tracking stays right), the VS's float
  constants and the clip planes. With pipeline libraries the TES's descriptors live in the pre-raster set, which DXVK refreshes only
  when a pre-raster stage's bindings change: once anything is registered, PS texture binds also mark the TES stage dirty.
- `DebugView 1`: the tessellated draws get polygon mode LINE (BindRasterizerState honours it while they are bound).

NVR (`src/core/DxvkCompute.{h,cpp}` RegisterDisplace / DisplaceTick, `ShaderRecord::LoadShader`, `effects/Terrain.cpp`):
registration of SLS2100.vso (TCS `Compute/Tess/TerrainDisplace.tcs.spv`) and each TerrainTemplate PS (TES
`TerrainDisplace-t<layers>[-ccw].tes.spv`, textures s0..s<layers-1>, PS registers c34/c35 = LandHeight: which layers have a height
map); once a frame the settings go to SetTessState, every 600 frames a `NVR TESS` log line with per-frame counters.

Shaders (`src/hlsl/NewVegas/Compute/Tess`, build: `PATH="/c/VulkanSDK/1.3.296.0/Bin:$PATH" bash tools/nvr-dxvk/compile-tess.sh`):
- TCS: edge level = edge length / target spacing (EdgeLength + EdgeGrowth x distance of the edge's midpoint), faded to 1 between
  FadeStart and FadeEnd, clamped to MaxFactor; inner = max of the edges. Per control point: the weight (distance fade, 0 on a
  quadrant border) and the height lookup's detail level. Crack-free by construction: an edge's level depends only on its two end
  points and the eye, a point's weight only on itself.
- TES: interpolates every VS output, height = the layer heights blended by the layer weights (0.5 where a layer has no height map, as
  the parallax does), detail level matched to the point spacing (no texel noise), displacement = weight x Strength x (height -
  Center) along the vertex normal; clip position = the VS's clip position interpolated + displacement x ModelViewProj (so Strength 0
  is the game's geometry bit for bit with FixedFactor 1); the object position and clip position the PS reads move with it (its fog,
  forward shadows and parallax follow the bumps); clip distances written from D3D9's clip planes (water reflections).

## Test: `work\bench\foundations\lists\fork2-displace-bulk.txt` (og-goodsprings, og-desert)
Needs a fork d3d9.dll with patch 0021 (this branch's CI build) and NvrDxvkDisplacement = true in the startup settings. off/on pairs,
`s0` (must equal off), `strong`, `ccw` (winding check), debug views 1-3 (wireframe = the proof shot), F10 profile. What to look for is
in the list's header.

## Known limits of this first step
- The bumps cast no shadows of their own and receive the flat land's: the sun's shadow maps are drawn with NVR's shadow shaders,
  which are not displaced. With Center 0.5 the lower half of the bumps lies below the flat land's shadow depth (may darken dips under
  a low sun); Center 0 avoids that but raises the ground by Strength/2 on average.
- Grass, decals (blood) and feet keep to the flat land (with 16 units = 23 cm, a few cm either way).
- The lighting normal is the flat land's (the normal maps carry the fine detail): large displaced shapes are not shaded as slopes.
- Parallax still runs on top (the depth is a little doubled close up).
- First sight of each land variant compiles a tessellation pipeline (a hitch, once per session; DXVK's state cache does not keep
  SPIR-V shaders).
- Strips cost three draws instead of one (the land: about 7 strip draws a frame in Goodsprings, so ~21).
- Winding: cw is expected to keep the game's facing (Vulkan's upper-left tessellation domain); `ccw` is there to switch if wrong.

## Plan for the rest (session-hours, after the first in-game look)
1. Fixes from the bulk test (winding, a missed interface, cost) (1-2 h).
2. Shadows: register NVR's land shadow-map shaders as members of the family with a depth-only TES, NVR binding the land's base maps
   for those draws, so the sun's cascades see the same bumps; same for the point-light cubes outdoors if any (3-5 h). Until then
   Center 0 is the safe choice for shadows.
3. Lighting: bend the normal / tangent frame by the height's slope in the TES (finite differences, 2 extra lookups per layer) (1-2 h).
4. Parallax hand-off: lower the land parallax's height by the displacement weight (1 h, NVR shader + a constant).
5. Objects (rocks, walls): the PAR family (ParallaxTemplate.hlsl, height map in s2) needs the object-space position and normal as VS
   outputs (a compile-time define, only with the switch on) plus its own TCS/TES; walls with straight edges need the same border rule
   as the land quadrants at mesh seams (3-5 h).
6. Speed: screen-space edge factors, patch culling off screen / back-facing (tess level 0), measure (2 h).
7. (Done in v2: strips, without any index conversion.)
Total about 10-16 session-hours left; the main risk is the shadow pass (step 2: NVR has to supply textures the shadow draws do not bind).
