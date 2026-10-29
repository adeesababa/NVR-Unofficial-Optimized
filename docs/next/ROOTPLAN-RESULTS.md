# Root-assumptions + interior-shadow plans: what was built and measured (2026-10-29)

Plans: ROOT-ASSUMPTIONS-PLAN.md, INTERIOR-SHADOWS-MODERN.md (this folder). Built autonomously on the user's word ("/loop the shadow and
root assumptions plans indefinitely"), base claude/hdr-latch 73b55860 (the installed build). Nothing installed, pushed or released.
Every feature is off by default; off = the old path exactly (or the old shaders byte for byte where it is compiled in).

Combined branch: **claude/rootplan-combo** (worktree H:\_NVR-efflamps, bench junction work\NVR-rootcombo), DLL CRC32 EB709DAC at a713ae48.

## B1 + D: ObjectMotion -- one motion buffer for every temporal reader
`[Main.Main.Quality] ObjectMotion` (live). core/ObjectMotion.h.
- While the world's main list draws, each geometry is noted once a frame (world transform; a skinned one's bone matrices the game just
  made). What moved is drawn again after the depth combine with a two-pose vertex shader into TESR_ObjectMotionBuffer (fp16: the world
  move since last frame, kept only where the fragment's depth matches the depth buffer).
- Readers subtract it from the camera-relative point before they reproject: TAA (Effects/TAA.fx.hlsl and the Vulkan TAA.glsl), FSR's
  motion vectors (Compute/FSRPrep.comp, FSRUpPrep.comp), AmbientLight's Accumulate (D3D9 and Vulkan). A world-space delta needs no
  projection, jitter or RenderScale handling: each reader projects with its own camera as before.
- D's camera-jump flag is the existing GameState.isCameraCut (in place since 10-21).
- **Verdict PASS.** Run 20261029-174247-slotC (read-back proof): 25 of 5184 sampled pixels carried a move (largest 11 units a frame,
  a walking NPC) with people in view, 0 when nothing moved in view.
- Measured: run 20261029-171255-slotC (og-goodsprings, the user's toml, FSR 3.1 at 0.67): 526 geometries noted, 21 moved ones drawn
  (9 skinned) a frame, GPU 0.03 ms; the game's bone matrices are camera relative (log line). Run 1 had the vertex shader fail to compile
  (a #define not at a line start; the feature switched itself off); fixed. Picture A/B is noise-bound (NPCs move between shots); a
  600-frame read-back line ("N of 5184 sampled pixels carry a move") is the proof check.
- Not covered: first-person view (zero motion, as TAA does), alpha-blended things, particles, grass and wind sway.

## C: LightCommonLamps -- lamps from one table, in a real loop
`[Main.Main.Misc] LightCommonLamps` (restart; with ManyLampsOnePass, which the user has on). Includes/LightCommon.hlsl, core/ManyLamps.h.
- A 1024-lamp float texture filled once a frame from the scene's light list; a draw gets its table indices (two 12-bit indices a float)
  and its world -> object rows in c37-c44. The first six lamps stay written out (their compile-time cube-map shadow slots); every lamp
  after them is one [loop]. MANY_LAMPS_ON = 2 selects it (no new define).
- GTX 1070 (tests/lamp_loop_cost.cpp, 1080p full screen): a looped table lamp costs ~1.4x an unrolled constant lamp (8 lamps 0.32 vs
  0.23 ms); clustered (per-pixel list) ~1.6x. ps_3_0 cannot loop over constant arrays at all (D3DX X4505): the table is the only way.
- SLS2034-2036 compile: 1857-1963 -> 1716-1808 instruction slots (tests/template_compile.cpp).
- Run 1 (20261029-170959-slotA vs -170741) found two bugs, both fixed: the index decode divided by 3 (a GPU divides through a reciprocal:
  lamps 3, 6, 9 read the wrong entry, Lucky 38 5 levels darker on half its pixels), and the 128-lamp table was too small for Vault 21's
  447 lights (7798 lamps a frame missing). tests/lightcommon_index_test.cpp decodes 6 x 24 indices on the GPU exactly.
- **Verdict PASS** (runs 20261029-172544-slotB off / -172917-slotB on, two launches, the user's toml): the picture equals the old path to
  the cross-launch noise floor (Lucky 38 0.03% of pixels > 16, mean 0.00, as two runs of the old path; Vault 21 0.38% vs 0.26%); the
  table held all 900 of Vault 21's frame lamps; the same lamps per draw; GPU world scene the same (~3.7 ms Lucky 38, ~7 ms Vault 21).
- NO-GO measured: the same table for the post effects' 24 lamps (SnowAccumulation) compiles SLOWER under D3DX (161 s vs 137 s): the
  snow pass's compile time is no longer the lamps. Not committed.

## Interior shadows step 1: ShadowPool -- still shadows for the lamps past LightPoints
`[Shaders.ShadowsInteriors.Forward] PoolLamps` (live, 0 = off; up to 192) and `PoolDrawsPerFrame` (4). core/ShadowPool.h.
- The next PoolLamps lamps of the same ranking get a static cube map of their own (walls and furniture), drawn a few a frame through the
  cube loop's own StaticOnly path (swapped into the spare last slot), kept per lamp, least recently used reused; published after the live
  slots with the same fill / invisible light rules; SetForPass finds a lamp through a map while pool lamps are published.
- Run 20261029-171716-slotB (the user's toml, LightPoints 24): Lucky 38 32 lamps past the live slots, Vault 21 52, all given a still
  shadow; drawing them costs a burst (6-11 ms in one frame) after arriving, then ~0. Vault 21 frame mean 0.65 levels darker with it (all
  forward lamp shadows together: 0.89) -- but that shot pair had camera drift; ABAB rerun rootplan-pool-2.
- **ABAB (run 20261029-173431-slotA, combo, lc-on toml): Vault 21 pool on -0.64 levels (1.34% of pixels > 16), def-a = def-b and
  pool-a = pool-b to 0.01%; forward lamp shadowing in that view 0.89 -> 1.53 (+70%). Lucky 38 -0.06.** Where: the walkway truss now
  shadows the back wall (diagonal stripes) and the small floor uplights' glow at the seats goes dark: with the user's LampModelShadows =
  true those lamps' housings shadow them, as the live slots would. Checked (run 20261029-181341-slotB): LightPoints 62 with no pool vs
  LightPoints 24 + pool: 0.27% of pixels > 16, mean -0.12 -- the pool gives what more live slots give (it covers 76 lamps there, the
  live slots 62). PoolDrawsPerFrame now 2 by default (4 made a 6-11 ms frame on arriving).
- `PoolPeopleLamps` (live, 0-16, 0 off): people drawn onto the nearest pool lamps' maps too (RedrawActorsOnly's MovingOnto through the
  spare slot), no shader change. Same run: 1.5-2 pool lamps a frame got people drawn on, no error, but no visible change at that view
  (people8 vs people0 0.00%): unproven, needs a spot where a pool lamp throws an NPC's shadow on screen.
- Needs LightPoints below 63. Memory: one cube map per pool lamp at ShadowCubeMapSize (6 MB at 512).
- People cast no shadow from pool lamps (they do from the live LightPoints lamps, as before).

## Interior steps 2-4
- Contact shadows per lamp: already in NVR ([Shaders.ShadowsInteriors.Main] ContactShadows, LampSize / contact hardening; off by default).
- Capsule shadows for people: not built. With the pool, the lamps without people shadows are the far ones; low value for the risk
  (every interior lit shader's record and its slot budget).
- RT: the rt-lamp-shadows branch (separate sessions).

## A1-A2
- A1 (cached static sun cascades): measured NO-GO. The whole cascade loop is 0.29-0.33 ms a frame in og-desert (run 20261029-172021-slotC;
  near 11 casters, middle 35, far 216, LOD 943 every 4th frame): a static/moving cascade cache cannot save more than that.
  (claude/sun-cache cb15b648 adds a 'still casters' count to SunCascadeStats; it reads 0 unless SunCascadeLeanDraws is on.)
- A2: the player's CharacterShadowMap exists (P82 v4); per-NPC maps not built.

## To try it (over the installed claude/hdr-latch 73b55860; not installed by these sessions)
Copy from the combo tree (H:\_NVR-efflamps):
- `build\optimized\NewVegasReloaded.dll` (+ `.pdb`) -> `<game>\Data\nvse\Plugins\`
- `resource\NewVegasReloaded.dll.defaults.toml` -> next to the DLL (the new keys' defaults: ObjectMotion, LightCommonLamps, PoolLamps,
  PoolDrawsPerFrame)
- under `src\hlsl\NewVegas\` -> the same path under `<game>\Data\Shaders\NewVegasReloaded\`:
  `Effects\TAA.fx.hlsl`, `Effects\AmbientLight.fx.hlsl`,
  `Compute\TAAResolve.spv`, `Compute\TAADebug.spv`, `Compute\TAAOutput.spv`, `Compute\FSRPrep.spv`, `Compute\FSRUpPrep.spv`,
  `Compute\AmbientLightAccumulate.spv`,
  `Shaders\ObjectTemplate.hlsl`, `Shaders\Includes\ManyLightsDirect.hlsl`, `Shaders\Includes\LightCommon.hlsl` (new)
- If a shader change seems not to take, clear `Shaders\NewVegasReloaded\Shaders\Cache\`.
Switches to try (all off by default): `[Main.Main.Quality] ObjectMotion = true` (live), `[Shaders.ShadowsInteriors.Forward] PoolLamps = 64`
(live; Vault 21 / Lucky 38), `[Main.Main.Misc] LightCommonLamps = true` (restart; only with ManyLampsOnePass on).

## Main session's step 1 (2026-10-29 evening): D for Reflections + clouds, the denoiser API
- Reflections' temporal pass (Effects/Reflections.fx.hlsl TemporalPS, Compute/ReflectionsTemporal.frag) subtracts the surface's own world
  move before reprojecting: D3D9 on stage 11 for that pass only (the ortho map's stage; the pass never reads it), Vulkan as texture 11
  with push.pass 2. Run 20261029-190324-slotB (Ultra-Luxe floor): Reflections 0.31 ms, no errors, on = off to noise (0.04%).
- AmbientLight's D3D9 Accumulate now binds the motion buffer on s8 itself (the grid passes' NVR_GridWave shares s8 and could still be
  bound: a bug of the first version, D3D9 path only).
- VolumetricClouds: nothing to do (its history reprojects a sky direction; moving things are never in the cloud buffer).
- Compute/ObjectMotion.glsl: the contract + ObjectMotionBack() for new readers (sent to rt-sun-core for its shared denoiser).
- Cost fix (561532b1): moves under 0.05 units count as still. Ultra-Luxe crowd 151 -> 13-19 moved geometries a frame, GPU 0.40 -> 0.027 ms
  (run 20261029-190742-slotA).

## Main session's steps 2-4 (2026-10-29 evening), branch claude/material-buffer f246a4c8, DLL 84A2F641
- **2. MaterialBuffer** (`[Main.Main.Quality] MaterialBuffer`, restart, with CapturedNormals): TESR_MaterialBuffer (fp16, the normals'
  span): r class (1 object, 2 skinned gear, 3 cloth, 4 land, 5 plant), g the object's bound radius (thickness, upper bound), b roughness
  (-1 none), a 1; actors' skin = the normals' a == 2. Format sent to rt-sun-core and rt-reflections. Object / Parallax / Terrain write it on
  their spare COLOR2; per-draw class + radius on c217 (ps_3_0 has c0-c223 only; c217 is free in the lit templates). **PASS**, run
  20261029-194016-slotA (Vault 21, the user's toml + MaterialBuffer): written exactly where the normals are (1849 of 4096 sampled texels),
  objects + gear, roughness 0.07-0.98, thickness up to 696; ~1000-1250 writer draws a frame, never in the skin blur's way. No reader yet.
- **3. PoolPeopleLamps: dropped** (f246a4c8 reverts it). Same run at LightPoints 4 (the NPCs' near lamps in the pool): people drawn onto
  3-6 pool lamps a frame, picture on = off to 0.01% (the repeat-shot floor).
- **4. The rest of INTERIOR-SHADOWS-MODERN:** lamp contact shadows (ContactShadows 0.5) with the pool on: no visible change (0.01%) for
  0.036 ms -- left off. Capsule shadows: not built -- people's shadows from more lamps measured invisible here (step 3). RT lamps: the
  rt-lamp-shadows sessions. Nothing left in that plan pays off on this evidence.
