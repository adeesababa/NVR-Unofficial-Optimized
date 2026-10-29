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
