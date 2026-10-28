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

## Black outlines round rocks, and ObjectBlend (2026-10-27, branch claude/tess-blend-4078c1, on claude/play-fork ac5f5de6)
User: "some tessellation strength settings ... break terrain blending leaving black outlines between rocks and terrain"; terrain
blending was off the whole time and switching it changes nothing.
- Cause (from the code; not yet seen in a picture): the land pixel shader's FORWARD sun shadows (ForwardShadows, the user's setting)
  look up `projectionPosition` (TEXCOORD6), which the TES left at the FLAT land. With Center 0 the land only rises. Where it rises
  against a rock whose side slopes outward towards its foot (most rocks and mounds), the flat point under the raised land lies
  INSIDE the rock, so the shadow map (which holds the rock) shadows it: a dark band round every rock, about rise / tan(slope) wide,
  so wider with more Strength. Not AO / AmbientLight / fog: those read the scene depth, which is displaced and consistent; no gap
  opens (Center 0 never sinks the land). The deferred sun shadows (ForwardShadows off) read the depth too, so they never had it.
- Fix: the TES writes `projectionPosition = v7 + max(disp, 0) x clipN`: it rises with the land, never below the flat land (the
  shadow maps still hold the flat land; a point under it would shadow itself, round 2's dark land). Fog reads it too (a few units).
- ObjectBlend (`[Shaders.Terrain.Displacement] ObjectBlend = true`, `ObjectBlendWidth = 32`, `ObjectBlendKeep = 0`, all live;
  `DebugView 7` shows it): the displacement (relief and the Center offset) is scaled by mix(Keep, 1, smoothstep(0, 1, dist / Width)),
  dist = the distance from the land point to the nearest surface drawn before the land (rocks, buildings, feet, the near grass that
  GrassFirst draws first), so those meet the ground as on flat land instead of sinking in. The TES projects 9 flat-land points
  (the point, 4 at Width/2, 4 at Width) with ModelViewProj and reads terrain blending's depth copy (made at the first land bind of
  the world scene; now also with blending off when ObjectBlend wants it) at each: gap = land view depth - scene view depth
  (scene view depth = B / (depth - A), A and B from MVP rows 2 and 3: row 2 = A x row 3 + (0, 0, 0, B) for any perspective camera),
  dist = min sqrt(offset^2 + gap^2). Linear filtering + clamping the uv keep it continuous (shared patch edges agree: no cracks).
- How the TES gets the depth without a d3d9 change: the fork lets a TES read only textures its pixel shader uses, s0-s7. With
  NvrDxvkDisplacement on at startup (`NVR_DISPLACE_DEPTH`), TerrainTemplate's pixel shaders below 7 layers declare
  `TESR_DisplaceDepth` at s6, BaseMap[6]'s slot (fxc and d3dx9_43 give BaseMap only the layers read; checked), used in a branch never
  taken; RegisterDisplace checks the bytecode's constant table and registers those with `TerrainDisplace-t<N>-d[-ccw].tes`
  (OBJECT_DEPTH) and TextureMask bit 6. 7-layer land has no free slot: plain TES, keeps its bumps. Moving the slot to s15 for all
  layer counts would need patch 0021 to accept TextureMask bits 8-15 (one more push dword of sampler indices).
- Per draw: the copy is this view's only in the world scene (not the water reflection), so Terrain.cpp PrepareDisplaceDepth
  (ShaderRecord::SetCT of those land shaders) calls DxvkCompute::DisplaceSceneDepth, which sends SetTessState with ObjectBlend on
  (ControlParams[6], forwarded by the TCS in o_nvr.w) and the viewport's share of the copy (EvalParams[3]: RenderScale draws the
  world into the top-left part) for world-scene land, the base state (ObjectBlend off) elsewhere; SetTessState only on change.
  Push room: MaxFactor and FixedFactor now share ControlParams[0] (MaxFactor + 128 x FixedFactor), DebugView and LodBias share
  EvalParams[2] (16 x DebugView + LodBias + 4). The DLL and the .spv files must be updated together.
- Cost (not measured): one depth copy a frame (~0.1 ms at 1440p, F10 "Terrain blend depth copy"), 9 depth taps per generated
  vertex within FadeEnd. GrassFirst stays on (its grass is in the copy, so the bumps also fade round grass tufts: they sit on the
  ground). The land pixel shaders recompile once (new define).
- Not done here: the bumps casting shadows of their own (done next: "The bumps cast sun shadows" below).
- First in-game test (user videos H:\2026-10-27 21-41-55.mp4, 21-44-17.mp4, Goodsprings scrub at night): ObjectBlend distorted the
  ground round every grass tuft. GrassFirst draws the near grass (and GPU grass) before the land, so it is in the copy, and the fade
  follows the blades, the wind and the view (gap measured along view rays). ObjectBlend is now OFF by default (experimental). A
  grass-free copy needs a second copy right after GrassFirst's early grass and a combine pass (object = depth at the land where it is
  nearer than the grass-only depth), or GrassFirst drawing the grass after the objects; still view-dependent near rocks.
- "Vertices shaking" in the wireframe while moving (still camera: nothing moves, frame differences ~0): distance-based levels with
  fractional_odd_spacing make the generated points slide as the camera moves, and the height lookup's detail level follows the
  spacing. The user's EdgeGrowth was 1.0 (clamped to 0.1, ten times the default): the levels change ten times faster. 0 = no sliding
  nearer than FadeStart.
- Live on/off in the Tessellation panel: `[Shaders.Terrain.Displacement] Enabled` (missing key = on); NvrDxvkDisplacement still
  installs it at startup (now listed as a restart-to-turn-on setting in the menu).
- User: "seems like all tessellation is doing visually is moving the ground up and down". Measured (DXT5 alpha of the loose
  landscape textures): scrubdirtstripnv01 / scrubdirtcanyon01 alpha 74-132 (mean 105/255, std 6.2), dirtwastes01 std 5.6,
  crackeddirtwastes01 8.2, pebblesdirtwasteland01 21.9, dirtwasteland01 18.7; 50-80 % of that variance is in shapes of 32-64
  texels and more. At Strength 10 the scrub bumps were (h - 0.5) x 10 = about +-0.25 units, while Center 0 lifted the whole ground
  by 10 x (0.5 - 0.41) + 10 x 0.5 = ~4-5 units near the camera, fading out between FadeStart and FadeEnd (the ground rising ahead).
  Now each layer's height counts from that texture's own average (texelFetch of its smallest mip), the blended difference is
  stretched by `Contrast` (default 10) and eased into the Strength range (0.5 x Strength x x / sqrt(1 + x^2), x = 2 x Contrast x
  difference), and `Center` defaults to 0.5 (no lift; the forward shadow lookup still never goes below the flat land). Contrast
  shares EvalParams[1] with Center (16 x Contrast + 0.99 x Center).

## The bumps cast sun shadows (CastShadows, plan step 2; 2026-10-27, branch claude/optimistic-kepler-4b182d on cca252c8)
`[Shaders.Terrain.Displacement] CastShadows = true` (live; missing key = on). No d3d9 change: the fork's 1.7-displace (patch 0021)
already allows a second family, a TES that writes a single output, and pixel shader registers the shader never declares (psBits read
the device's c34/c35). Code: `src/core/DisplaceShadows.h`, `DxvkCompute::RegisterDisplace` (family 2), `Shadows/ShadowMapLand.vso`,
`Shadows/ShadowMapLand.pso.hlsl` (loaded as -t1..-t7), the `SHADOW_CASTER` variants of TerrainDisplace.tese (-s / -sd [-ccw]).
- How the land reached the cascades before: cell land went through the opaque caster pass (ShadowManager geometryPass:
  ShadowMap.vso + ShadowMapOpaque.pso), the same shaders as every opaque object, with only the position stream used and no land
  texture bound. Now cell land whose bound reaches FadeEnd of the camera is sorted (AccumObject / WalkSunObject, sun cascades only:
  DisplaceShadows::BeginCascades .. EndCascades) into its own pass, drawn first in RenderAccums; anything it cannot draw goes back
  to the opaque pass.
- The land's own vertex layout and the land TCS, unchanged: ShadowMapLand.vso writes TerrainTemplate's outputs at their places, but
  TEXCOORD6 / the position = the cascade's clip position (pancaked as ShadowMap.vso), TEXCOORD7 = the MAIN camera's eye in object
  space (so the TCS's split levels, distance fade and BorderGrid are the camera's, point for point, not the light's), the free w of
  TEXCOORD3/4/5/7 = the cascade's ModelViewProj x normal (the TES moves the clip position by it; interpolated like the normal, so
  exact after the same division by its length), c0-c3 = last frame's main-camera ModelViewProj (ObjectBlend). The TES (-s) runs the
  same height blend and displacement and writes only TEXCOORD6; the pixel shader writes its depth (PCF maps), and declares the base
  maps (and the depth copy at s6) in a branch never taken, so the TES may read them.
- Which layers have a height map (LandHeight c34/c35) is set per land draw by Vanilla Plus Terrain through the game's constant map
  (VanillaPlusTerrain.dll registers LandSpec/LandHeight/...; closed source), nowhere NVR could read it. So the land's own draws are
  taken: for the world scene's span only (TerrainShaders::BeginWorldScene .. EndWorldScene) the device's DrawIndexedPrimitive is
  hooked (vtable 82, GrassFirst's own hooks nest inside it and put it back), and a draw with a land pixel shader bound (the ones
  ShaderRecord::LoadShader made from TerrainTemplate, noted with their TEX_COUNT) records, keyed by stream 0's vertex buffer and the
  base vertex, the base map addresses, c34-c35 and s0's sampler states. The shadow pass looks a piece up by its buffer data
  (VBChip[0]->VB, BaseVertexIndex) and uses it only if the piece's own landscape textures (ppTextures[0][0..N-1]) are those
  addresses. A piece the camera has never drawn (or whose buffer was reused) keeps the flat caster. Log: "NVR TESS shadows" every
  600 cascade frames (pieces drawn with bumps / flat and why) and the first few keys on both sides.
- ObjectBlend in the shadow draws: the cascades are drawn before the world scene, so this frame's depth copy does not exist yet;
  they read last frame's copy with last frame's camera (NoteDepthCopy keeps ViewProj and the camera position at the copy), valid
  only if the copy was made after the previous cascades. A frame late: the same for anything standing still, a frame behind for a
  moving one. (ObjectBlend is off by default since cca252c8.)
- The main view: while land was drawn displaced into the cascades in the last 16 frames, the TES gets ShadowsHold (viewParams +
  256) and writes projectionPosition = the displaced clip position, below the flat land too (Center > 0); else as before (never below
  the flat land).
- Left as they were: the ortho map (rain, skylighting), the light grid's sun map, the character map and the lamps' cube maps keep
  the flat land (with Center 0 the displaced land is above it: no self-shadow; with Center > 0 the lamps may darken sunk dips). The
  far / cached cascades keep the bumps as they were when drawn (CachedDistantShadows: up to a few frames).
- Cost (not measured): the land within FadeEnd tessellated once more per cascade it reaches (~23k quads within 2048 units at the
  default EdgeLength 12 / EdgeGrowth 0.01, strips as three patch lists) plus a few device reads per land draw in the world scene;
  F10 "Sun cascade: displaced land". Land farther than FadeEnd is drawn as before.

## The bumps are lit as slopes (BendNormals, plan step 3; 2026-10-28, on claude/play-fork 04714ffb)
`[Shaders.Terrain.Displacement] BendNormals = true` (live; a missing key = on). Before, the TES passed the vertex shader's flat tangent,
binormal and normal on, so a bump's sun side and shaded side were lit alike (Conor, after a spiky Goodsprings shot at Strength 33 /
Contrast 10: can the tessellation "use normals"?).
- The camera's TES variants (not the `-s` / `-sd` shadow casters) read each layer's height twice more at the same detail level, one
  texel of it along u and along v (forward differences; the spacing of the generated points wherever that level is above 0). The blended
  slope goes through the same Contrast easing (its derivative), Strength, weight and ObjectBlend share as the displacement.
- Into game units through the triangle's own uv-over-x/y map (from its three corners: land uv is a plane over the land's x / y, so the
  map is the same for every triangle of a quadrant whatever its slope, so no seams along the game's edges, and no assumption about the
  tangent's sign). Normal = n minus the slope along the surface (exact for a height moved along a constant normal); tangent and
  binormal re-orthogonalised to it, each keeping its length and its w (the main vertex shader writes only their xyz).
- The fades' own slope (weight: distance and quadrant border; ObjectBlend) is left out: small, and per game triangle, so it would show
  their edges in the light. Inside the 128-unit strip along a quadrant border the bumps fade out a little faster than the light says.
- Push data: `viewParams + 128` = BendNormals OFF (the free bit between DebugView x 16 and ShadowsHold x 256). Set = off, so an older DLL
  with these shaders bends, and this DLL with older shaders is unchanged unless it is switched off. DebugView 6 shows the bent normal.
- Cost (not measured): 2 more filtered lookups per blended layer (weight > 0.0005, usually 1-3) per generated point of the camera's land,
  and a 2 x 2 inverse; nothing in the shadow passes. Expected well under 0.1 ms on the 1070.
- Not done: rocks (ObjectDisplace.tese). Their pixel shaders light in tangent space with light and view vectors the vertex shader
  already turned (TEXCOORD1/3/5/7, which slots hold them differs per variant) and the TES has no object-space position for the uv map: a
  per-variant rotation of those vectors, about 1-2 h.

## Smoother bumps by default: BumpSize 48, Contrast 4 (2026-10-28, on BendNormals dbf6cf1c)
Conor (RTX 4070 Ti SUPER, settings never touched: Strength 10, Contrast 10, EdgeLength 12, LodBias 0) saw his Goodsprings ground as a
dense field of sharp spikes and asked how it would work with smooth terrain. The height's detail level followed the point spacing
(12 units near the camera) and Contrast 10 stretched every texture's grain, so neighbouring points went fully up and down.
- `[Shaders.Terrain.Displacement] BumpSize = 48` (live, whole game units 0-255, 0 = the old behaviour): the height lookup is never
  finer than that, whatever the point spacing. The uv per game unit comes from the triangle's own uv map J (the one BendNormals uses),
  sqrt|det J|, and the level is rounded to 1/256 so two triangles agree to the bit along their shared edge. In landHeight, so the
  shadow casters (CastShadows) and the BendNormals differences follow the same bumps. LodBias now adds on top of it. Push:
  `viewParams + 512 x BumpSize` (ShadowsHold is now read as bit 256 only). With BumpSize above the spacing (everywhere within FadeEnd at
  the defaults: EdgeGrowth 0.01 reaches 48 only at 3600 units) the detail level no longer changes with distance, so it cannot crawl as
  you move either.
- `Contrast` default 4 (was 10; also the DLL's value for a missing key).
- Offline check (scratch script on the loose landscape textures, DXT5 alpha, box mips, trilinear like the TES, 400 random lines with points
  every 12 units, one layer). The land's uv repeat in game units is not known offline, so both 683 (6 per cell) and 2048 (1 per quadrant)
  were run; the averages of the two. Relief = std of the displacement, slope = 99th percentile of the step between neighbouring points,
  spike = 99th percentile of a point's height against the mean of its two neighbours (single-point spikes), game units:

  | Strength, BumpSize, Contrast | smooth (pavement, dirt01, muddirt) | scrub / dirt (6 textures) | rough (pebbles, loose rocks, dirtwasteland, riverbed, dirtpebbles) |
  |---|---|---|---|
  | 10, 0 (old), 10 (old)  | 0.93 / 8 deg / 0.96 | 1.54 / 15 deg / 2.01 | 3.09 / 27 deg / 3.67 |
  | 10, 48, 10             | 0.59 / 3 deg / 0.19 | 0.84 / 4 deg / 0.34  | 2.29 / 10 deg / 0.84 |
  | 10, 48, 4 (new)        | 0.25 / 1 deg / 0.08 | 0.36 / 2 deg / 0.15  | 1.23 / 5 deg / 0.39  |
  | 33, 0, 10 (user's)     | 3.08 / 24 deg / 3.18 | 5.07 / 41 deg / 6.63 | 10.19 / 59 deg / 12.11 |
  | 33, 48, 4              | 0.81 / 4 deg / 0.27 | 1.18 / 6 deg / 0.49  | 4.07 / 16 deg / 1.30 |

  At the old defaults a single point stands out from its neighbours by about as much as the whole relief (noise: spikes); with
  BumpSize 48 by a quarter or less (rounded mounds). Smooth ground ends up with a quarter of a unit, rough ground keeps clear mounds.
- Not changed: the rocks (`[Shaders.POM.Displacement]`, Contrast 2, LodBias 0 per patch), on their own branch.

## Objects: rocks, cliffs, walls (plan step 5; 2026-10-28, branch claude/intelligent-lalande-6e0cf0 on claude/tess-blend-4078c1 0a51be43)
`[Shaders.POM.Displacement]` (the POM page of the menu, "Tessellation (displacement)"): `Enabled = false` (live), `Strength = 8`,
`Center = 0.5`, `Contrast = 2`, `EdgePixels = 12`, `EdgeFade = 1.5`, `MaxFactor = 16`, `FadeStart = 1024`, `FadeEnd = 2048`, `LodBias = 0`,
`FixedFactor = 0`, `DebugView = 0` (1 wireframe), `Winding = 0`. Needs NvrDxvkDisplacement at startup like the land. **No d3d9 change**:
the fork's 1.7-displace takes a third family as it is.
- Which shaders: every object with a parallax height map is drawn by ParallaxTemplate.hlsl (PAR2000-2019.vso, PAR2000-2032.pso: the
  main lit pass, the AD / DIFFUSE lamp passes, the ONLY_SPECULAR highlight passes, the NO_LIGHT texture pass). HeightMap (height in red,
  as Parallax.hlsl reads it) is at s3 in the main pass and s2 in all the others (read from each pixel shader's constant table).
  ModelViewProj is c0-c3 as on the land. The vertex shaders have the object-space position and normal (inputs), but sent neither on.
- The IO problem: the fork's TCS / TES pass the vertex shader's outputs on by location, and DXVK (dxbc-spirv's sm3 IoMap) gives fixed
  locations only to NORMAL0 (0), TEXCOORD0-7 (1-8), COLOR0-1 (9, 10) and FOG (11); anything else (this template's TEXCOORD8 shadowUVs and
  TEXCOORD9 shadowWorldPos) gets 12, 13 ... in the order each shader declares it, and VS and PS are matched by semantic name, which a
  glslang TES does not carry. So with `NVR_DISPLACE_OBJECTS` (ShaderRecord::BuildDefines: NvrDxvkDisplacement on and the NVR DXVK
  extension present) the template uses a fixed layout: shadowUVs / shadowWorldPos go to TEXCOORD5 / 6 in the variants with a vertex
  colour or fog (at most 2 lights there), else to COLOR0 / COLOR1 (the ONLY_LIGHT passes have neither; dxbc-spirv does not clamp shader
  model 3 colours; under native D3D9 the layout is never compiled); NORMAL0 carries the clip position again and TEXCOORD0's zw the object
  normal (octahedral). vs_3_0 allows 10 outputs besides the position (fxc X5622), so every vertex shader writes exactly 10 of the 11
  fixed slots, unused ones as 0: all but TEXCOORD2, or in the DIFFUSE lamp passes (TEXCOORD2 = their light's attenuation) all but COLOR0.
  Checked offline with fxc for all 49 variants, both ways (the scratch script: every VS writes exactly its set, every PS reads only fixed
  slots). Off, the shaders are exactly as before.
- Shaders (`Compute/Tess`): `ObjectDisplace.tcs` / `-c.tcs` (the two output sets), `ObjectDisplace-h3`, `-h2`, `-h2-c` `.tes` (+ `-ccw`).
  Registered as family 3 (`DxvkCompute::RegisterDisplace` -> RegisterDisplaceObject), VsFloatRegisters c0-c3, TextureMask = the height
  map's sampler.
- One tessellation state for every family (the fork's SetTessState): the land and the objects need different push data, so each
  registered shader's ShaderRecord keeps its family (`DisplaceFamily`) and `ShaderRecord::SetCT` (runs whenever the pixel shader changes)
  calls `DxvkCompute::DisplaceBind`, which sends the objects' state for an object shader and the land's base back for a land shader
  after the objects'. DisplaceTick re-sends whichever family was last bound (an object shader can stay bound into the next frame). The
  land's own sends (ObjectBlend, shadow pass) are unchanged.
- The split: in screen space (`EdgePixels`, from the clip positions alone; the push data has half the screen size), so every pass of an
  object gets the same split (the lamp and highlight passes redraw the same triangles and must land on the same depth) and an edge's
  level depends only on its two ends. Patches wholly outside one side of the view (margin 16 x Strength + 1 in clip units: enough for a 10 degree scope) or behind the
  camera get level 0. Distance fade by view depth (clip w), FadeStart .. FadeEnd.
- The height: as the land's (each texture's own mean = its smallest mip, stretched by Contrast, eased into 0..1, Center the level that
  stays put), but read with texelFetch and a manual bilinear / trilinear filter (wrapping) so the passes' different sampler states (s3
  vs s2) cannot change the depth. Detail level per patch: the largest uv distance between generated points. Strength is in game units on
  scaled objects too: ModelViewProj row 3's length is the object's scale. Position `precise`, gl_Position `invariant`.
- **Seams: pinned edges.** A mesh's vertices are split where its uv or normals jump (uv seams, hard edges). The two triangles on either
  side then read different heights or move along different normals, and a crack opens. A TCS / TES sees only its own triangle, so it
  cannot tell such an edge from a smooth one: "fade where the endpoints' normals differ" cannot work (on a smooth rock every vertex has
  its own normal), and "displace along an averaged normal" needs the other side's normal, which no stage has without an adjacency
  buffer (a CPU pass over every mesh). Chosen: the displacement is 0 on every edge of the game's own triangles and fades in over
  `EdgeFade` generated steps (outer level k x gl_TessCoord[k]). No original edge ever moves, so no crack can open anywhere, whatever the
  mesh. Cost: the bumps sit inside the game's triangles, with a flat seam along each original edge (rock formations have big triangles,
  so most of the surface gets them). `EdgeFade 0` = no pinning (test: shows where the seams are).
- Not moved, on purpose: the lighting vectors (light directions, view vector: a few units against lights hundreds away), the forward
  sun shadow lookup (shadowWorldPos stays at the flat surface, which is what the sun's shadow maps hold: a point below it would shadow
  itself), the projected shadow uvs, the fog.
- Shadows (task item 4): NOT done. The rocks reach the sun's cascades through NVR's opaque caster pass (ShadowMap.vso /
  ShadowMapOpaque.pso) with no height map bound. Doing it like the land's CastShadows = a family 4 (a shadow vertex shader writing this
  layout with the cascade's clip position and the MAIN camera's clip position for the split, the object's height map bound from its
  BSShaderPPLightingProperty, PAR objects sorted into their own pass): about 2-3 h. Easier than the land (no Vanilla Plus Terrain
  capture), but not "cheap". Until then: with ForwardShadows on (the user's setting) no acne (the lookup stays flat); with the deferred
  sun shadows (ForwardShadows off) the dips below the flat surface (Center 0.5) can self-shadow; the bumps cast no shadow.
- Cost (not measured): every parallax object within FadeEnd is tessellated in each of its passes (lit + lamp + highlight + water
  reflection). With the switch on and Enabled off: the 49 parallax shaders recompile once (new define) and the vertex shaders write a
  few more outputs; nothing is tessellated.
- Limits: parallax still runs on top (as on the land). Objects without a height map (most architecture) are not parallax objects and stay
  as they are. A DIFFUSE vertex shader paired with a non-DIFFUSE pixel shader would leave the pixel shader's COLOR0 undefined (not seen in
  the template's pairings).

## Far land drawn untessellated (2026-10-28, FarUntessellated)
`[Shaders.Terrain.Displacement] FarUntessellated = true` (live; missing key = on). **No d3d9 change.**
- Why: every land draw went through the fork's tessellation (VS -> TCS -> TES, a strip as three patch lists) at any distance, while
  past FadeEnd TerrainDisplace.tesc only sets level 1 and nothing moves. A tester (Conor, uGridsToLoad 5) logged "per frame 350-980
  land draws tessellated"; the user's Goodsprings bench view 7.
- Where his count comes from (his log NewVegasReloaded_2026-10-28_17-15-12.log, the code and the decompile; not yet confirmed by the
  new log line):
  - "670 land pieces known" is the shadow capture's map, which only grows: 100 at the start (5 x 5 cells x 4 quadrants), 116, 120 ...
    673 as he travelled. Not the loaded land.
  - DisplaceTick runs TWICE a rendered frame (ShaderManager::UpdateConstants is called from RenderHook and again from
    ProcessImageSpaceShadersHook), so the old line's "per frame" was per half frame: his real count is about 700-1,960 a frame. (Side
    effects of the double tick, left as they are: settings re-read every 4 frames, ShadowsHold lasts 8 frames, not 16.)
  - The game draws a strips geometry with ONE DrawIndexedPrimitive PER STRIP (NiDX9Renderer::Do_OnlyRenderTristrips 0xE74840:
    NumArrays draws, each ArrayLengths[i] - 2 triangles), and the fork counts each. The TERRAIN PROBE (2026-10-17 bench) saw ~22-31
    land pieces in view, each with 2 passes: BSSM_LANDLO_A (SLS2080.vso, not tessellated) and one BSSM_LAND<n>O (SLS2100.vso,
    tessellated). So about 30 tessellated land passes in the world scene, as many again in the water reflection, plus the shadow
    pass's near pieces: ~1,200 draws a frame means about 16 strips a land quadrant (one per row of a 17 x 17 grid), the user's ~14 a
    frame about 1. Why his land is in many strips and the bench's is not is open (a land or engine-fix mod?); the new log line says.
  - The log line now gives, per rendered frame: land passes and their strip draws for the world scene, the water reflection and
    anything else, and the far ones.
- Where: `RenderPassStandardHook` (Render.cpp, BSBatchRenderer::RenderPassImmediately_Standard), before each game pass and before its
  shader bind, calls `DxvkCompute::DisplaceLandPass(geometry, pass ID, context)`. A land pass drawn with the tessellated shaders (IDs
  0x1EF-0x230, BSSM_LAND<n>O...) of the world scene or the water's reflection whose world bound lies wholly past FadeEnd + 256 units of the camera,
  measured horizontally (the reflection camera is the camera mirrored below the water: same x and y; the horizontal distance never
  exceeds the 3D distance the TCS measures), sets `DisplaceFar`. `DisplaceSend` then gives the fork one fixed "off" state for the land
  family (Enable 0) instead of the land's state; the state asked for is kept (`DisplaceWanted`) and sent as it is again by the next near
  land pass or ANY other pass (so a land draw is never left switched off by the piece before it). The binds' own sends (DisplaceBind,
  DisplaceSceneDepth / ObjectBlend) go through DisplaceSend and so follow the flag; DisplaceShadowBegin (land within FadeEnd only) and
  DisplaceTick (once a frame) clear it. The objects' state (family 3) is never touched.
- Same picture: past FadeEnd every point and edge midpoint has fade 0, so the TCS gives level 1 (fractional_odd: no split), weight 0
  and fade 0 (no relief, no Center offset), and the TES evaluates exactly the corners (1 x a + 0 x b + 0 x c, displacement 0,
  projectionPosition = the VS's): the same triangles the VS gives. Only the debug views change out there (far land now plain, not
  wireframe / coloured), which shows where the cut lies.
- Cost of the switching: SetTessState is a lock + compare + copy in the fork; the work is one unbind / rebind in ApplyDraw at the next
  draw after a near <-> far change (NVR TESS "rebinds" goes up by about that). Land passes come grouped by pass type, so changes are
  few; the log counts them ("near/far switches").
- Log (per rendered frame): `NVR TESS on: ... | land passes (strip draws): world W (w), water reflection R (r), other O (o) | far
  land on: F passes (f strip draws) untessellated (wholly past FadeEnd + 256), M near/far switches | ...`. One flag before a pass
  covers all its strip draws.
- Expected saving (not measured): per far strip draw, two Vulkan draws fewer (one strip draw instead of three patch lists) and no
  TCS / TES invocations (about 3 + 3 per strip triangle at level 1). With uGridsToLoad 5 most land in view is past FadeEnd (pieces
  within ~3,750 units of the camera stay tessellated): for a setup like Conor's (~1,200 strip draws a frame) roughly 80-90 % go plain,
  ~2,000 Vulkan draws a frame fewer on DXVK's CS thread (a few ms of that thread) and some GPU; little where the land is one strip a
  quadrant (the user's bench view: ~14 draws a frame).
- Limits: the bound is a sphere (radius ~1,450 for a 2,048 quadrant), so pieces whose centre is within about FadeEnd + 256 + 1,450
  stay tessellated (conservative). Other cameras (local map, anything outside the world scene and the water reflection) keep the
  tessellation as before. A land pass with an ID outside the list stays tessellated as before.
## The ring round the player, and bumps that change size with jitter (2026-10-28, branch claude/tess-ring on claude/linear-scene 7663bed0)
User (installed 7663bed0, Strength 33 / Contrast 10 / Center 0.5 / LodBias 1, CastShadows on, FSR RenderScale 0.667 + LowResEffects,
TAA Jitter on, MipBias -0.5, screen-space shadows on with NearFade 400): "a crusty weird shadow circle around us with brokenish looking
shadows on tessellated geometry", "a hard edge where things get shadowed / darkened just outside this circle and it follows us around",
"turning off jitter seems to shrink tessellation". Found from the code (not seen in a picture, no game run).
- The ring: the screen-space CONTACT shadows ([Shaders.ShadowsExteriors.ScreenSpace], SunShadows.fx ScreenSpaceShadow / Fast / Sharp and
  the two Vulkan marches) march the depth buffer from each pixel toward the sun and count where the scene is in front of the ray. On flat
  ground nothing is; on tessellated ground every bump is, so the bumps cast crusty little contact shadows on each other (on top of their
  real ones from the sun's maps, CastShadows), choppy from the 4 random steps, the blur and LowResEffects' small buffer. NearFade
  (3c5ebc8a, the user's 400) switches the contact shadows off nearer than 200-400 units of view depth: inside that circle the bumps look
  clean (bright), outside it crusty and darker, a hard-ish ring that moves with the camera. The flat land past FadeEnd gets no such
  darkening (nothing blocks the march there).
- Jitter: TAA / FSR / DLSS set a texture mip bias on every sampler while the world draws (TAAEffect::SetMipBias: MipBias when jittering,
  plus log2(RenderScale) with FSR / DLSS; the user: -0.585 - 0.5 = -1.09 with jitter, -0.585 without). The tessellation evaluation
  shaders read the heights with textureLod and the PS's own samplers, and Vulkan adds the sampler's mipLodBias to an explicit level too:
  with jitter on the heights came from a mip about one level sharper, so the bumps were sharper and taller (spikier at Contrast 10);
  jitter off -> smoother, smaller bumps. The shadow casters copy the land's sampler states, so the maps followed the same change.
- `[Shaders.Terrain.Displacement] ContactShadowsOutside = true` (live; missing key = on; off = as before): while the bumps are on
  (displacement registered, Enabled, Strength > 0), TESR_ShadowScreenSpaceFade (D3D9 and the Vulkan passes' flags.y/z) is the farther of
  NearFade's and FadeEnd -/+ a quarter of the fade band (the user: 1792 .. 2304), set every frame in ShadowsExteriorEffect::UpdateConstants
  (DxvkCompute::DisplaceContactFade). So no bump is ever in a contact march, and the contact shadows come in where the bumps have gone.
  Nearer than that, everything's sun shadows are the shadow maps' (with the user's cascades the near ones are 0.1-0.9 units a texel).
  View depth, not distance: at the screen's edges the contact shadows come in a little farther still (never over bumps).
- `IgnoreMipBias = true` (live; missing key = on; off = as before): TAAEffect::WorldMipBias (the bias the frame's first world scene got)
  is added back to the TES's LodBias part (TerrainDisplace viewParams; ObjectDisplace's lodBias for the rocks too), at most 3 (stays
  under DebugView's 16s). The heights are read at the detail level the point spacing asks for (+ LodBias), with or without jitter or
  RenderScale. Last frame's bias (the same unless the TAA settings change). The water reflection's land is drawn without TAA's bias, so
  there the bumps come out a level smoother (not visible).
- No shader changed: only the DLL and the defaults toml.
- Left as it was: the land pixel shader's shadow lookup and sky light take the normal rebuilt from screen derivatives of the displaced
  position, one per tessellated triangle (GetShadowGeometricNormal). With the user's tiny near-cascade texels the normal offset it
  drives is a fraction of a unit, so it is not the crust; the sky light per facet is a mild shading of the bumps. Strength 33 at
  Contrast 10 makes real spikes (+-7-16 units over 12-unit spacing) that cast real, hard sun shadows (CastShadows): Strength 10-15, or
  the smoother bumps of claude/funny-mclean-fde02c (BumpSize, not installed), would look calmer.
- Also noticed (not changed): ContactNearFade is called with the march's END position (pos after the loop), not the pixel's: a few
  units of view depth off, harmless here.

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
- (These are the first step's values. Shipped since 2026-10-28: Strength 10, Center 0.5, Contrast 4, BumpSize 48, EdgeLength 12, LodBias 0
  on top of BumpSize, CastShadows and BendNormals on, ObjectBlend off; see the sections above.)

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
- (Done 2026-10-27: CastShadows, above.) The sun's cascades hold the bumps; the ortho map, light grid map and lamp cube maps do not.
- Grass, decals (blood) and feet keep to the flat land (with 16 units = 23 cm, a few cm either way).
- (Done 2026-10-28: BendNormals, above.) The lighting normal was the flat land's: large displaced shapes were not shaded as slopes.
- Parallax still runs on top (the depth is a little doubled close up).
- First sight of each land variant compiles a tessellation pipeline (a hitch, once per session; DXVK's state cache does not keep
  SPIR-V shaders).
- Strips cost three draws instead of one (the land: about 7 strip draws a frame in Goodsprings, so ~21).
- Winding: cw is expected to keep the game's facing (Vulkan's upper-left tessellation domain); `ccw` is there to switch if wrong.

## Plan for the rest (session-hours, after the first in-game look)
1. Fixes from the bulk test (winding, a missed interface, cost) (1-2 h).
2. Shadows: DONE for the sun cascades (CastShadows, above; no d3d9 change). Left: the lamps' cube maps outdoors (1-2 h, the same
   pass with the cube shaders), only needed for Center > 0.
3. Lighting: DONE for the land (BendNormals, above; not yet seen in game). Left: the rocks (1-2 h).
4. Parallax hand-off: lower the land parallax's height by the displacement weight (1 h, NVR shader + a constant).
5. Objects (rocks, walls): DONE 2026-10-28 ("Objects" above; not yet seen in game). Left: their shadows (2-3 h).
6. Speed: screen-space edge factors, patch culling off screen / back-facing (tess level 0), measure (2 h).
7. (Done in v2: strips, without any index conversion.)
Total about 10-16 session-hours left; the main risk is the shadow pass (step 2: NVR has to supply textures the shadow draws do not bind).
