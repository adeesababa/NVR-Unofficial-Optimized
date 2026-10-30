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

## Round 2 (2026-10-29 20:20), claude/material-buffer 9eb91a64, DLL 3EC44507
- **5. MaterialBuffer readers: blocked.** Reflections already uses all 16 sampler registers on its D3D9 path, and it already gets per-pixel
  roughness + metal from MaterialAware where an _rmaos map is bound. Skin/SSS cannot read thickness from it: skin pixels are not in it
  (the skin templates' COLOR2 is the skin blur's). The buffer stays for the RT passes (format sent to rt-sun-core / rt-reflections).
- **6. SLS2132-2138 (6-layer land) X4505: FIXED.** Fails at flags 0 exactly with terrain blending + captured normals (32 of 256 option
  combinations; 5 and 7 layers compile: a D3DX allocation quirk). Shaders that hit X4505 are compiled again with
  D3DXSHADER_PREFER_FLOW_CONTROL (ShaderRecord::LoadShader and the parallel precompiler): all 256 combinations compile; every shader that
  compiled before keeps its bytecode. In game, run 20261029-201613-slotC (the user's toml + terrain blending): the four retried and
  loaded, no temp-register error left, PASS.
- **7.** Asked the RT merge session (local_0076c7d1) to merge claude/material-buffer into the RT train.

## Round 3 (2026-10-29 21:15), claude/rootplan-round3 e8da87a7, DLL 9748E7AF (on claude/material-buffer)
- **8. CapturedNormals lean hook** (`[Main.Main.Quality] CapturedNormalsLeanHook`, live, default off; NVR DXVK only): render targets,
  pixel shader and blend states are tracked from the device's Set calls (slots 37/57/107, re-synced after NVR's state-block applies)
  instead of read back on every draw, and the write mask is only set when it changes. **PASS**, run 20261029-211101-slotB (Ultra-Luxe,
  the user's toml, 960x540, CPU-bound: 0.00 ms waiting for the GPU), ABAB x4: frame 6.84 -> 6.68 ms (-0.16 ms, lean faster in 4 of 4
  rounds), all of it in world draws (2.79 -> 2.64); pictures lean vs off = off vs off (<=0.03% of pixels >16 levels). On DXVK the hook
  cost far less than the 1.5 ms measured on native D3D9 (where the lean path is not used). The first run (-204846-slotB) had the AI on,
  the camera turned during it: no timing from it, its rounds 1-2 also showed equal pictures. Can be default on.
- **9. FarHysteresis** (`[Shaders.Terrain.Displacement]`, game units, live, default 0) + the land log counts pieces whose near/far choice
  flipped since the frame before. Run 20261029-210747-slotC (Goodsprings road, player stepped 64 units back and forth every 160 frames,
  0 vs 256 ABAB): **0.00-0.01 flips per frame with or without it** -- no piece sat at FadeEnd+256 there (14-18 land pieces known), so
  the pop was not reproduced; pictures 256 vs 0 = 0 vs 0 over time (outdoor noise, mean +-0.5). Harmless, unproven: stays 0 until a spot
  with the pop (the flip count in the log now finds one).
- **10. Live upscaler switching.** The menu hands the RenderScale over when FSR <-> DLSS is switched; TAA logs every change of upscaler /
  RenderScale / on-off (and says why a picture went native); Choose() restarts history and frees the stopped FSR's textures on every
  switch and logs it. Same run, FSR 3.1 -> FSR 2 -> DLSS(HelperTest) -> FSR 3.1 -> FSR 2 -> DLSS at its own RenderScale 1.0 -> FSR 3.1:
  **PASS** -- one TAA line + one "switched live" line per change, FSR 2 / FSR 3.1 textures remade each time, no crash, the RenderScale 1.0
  case logged as native "-- was upscaled before: each ... has its own RenderScale". The slot has no NvrDlssHelper.exe, so DLSS fell back
  to FSR 2 (logged; one redundant "FSR 2.2.1 -> FSR 2.2.1 (DLSS not running)" line): the helper itself is untested here.

## Round 4 (2026-10-29 21:50), claude/rootplan-round3
- **11. SharedDrawState** (`[Main.Main.Quality]`, live, NVR DXVK, with the lean hook; f2c92ecd, DLL 80473250): LinearScene's per-draw
  hook reads the render target / pixel shader / blend states CapturedNormals tracks (core/DrawState.h) instead of up to six device Get
  calls. ObjectMotion has no per-draw device hook (it notes geometry from the game's pass hooks, 0.03 ms), so there was nothing of its
  to merge. **PASS**, run 20261029-213536-slotA (Ultra-Luxe, the user's toml, CPU-bound, ABAB x4): 7.13 -> 6.88 ms (-0.25 ms, 4 of 4
  rounds) on top of the lean hook; the same decisions (970 draws in linear light, 299 in the stored curve, on and off; ~1070 of ~1270
  draws read the shared state); pictures equal (<=0.03% >16 levels). Default on (toml only).
- **13.** Asked the RT merge session to take claude/rootplan-round3 (lean hook default on: its A/B was clean).
- **12. Interior lamp-shadow look (Conor's regression), with the pool on.** Runs (all Lucky 38 floor / Vault 21 lobby / Prospector
  saloon / bar, cpucheck intshadow-lookback views, toml rootplan\look-pool = the user's toml + PoolLamps 32 + quiet pictures; solid % /
  holes % from each variant's shadows-off pair as cpucheck intshadow_holes.py): look-1 20261029-214201-slotA, look-2 -220446-slotA,
  look-3 -221441-slotB, look-4 (exposure ON, brightness only) -222253-slotB. Lists rootplan-look-1..4, stats scratch look_stats.py.
  - **The user's toml is not the defaults' case:** it has `[Shaders.PBR.LampFalloff] Interior = true` (the physical lamp shape replaces
    the falloff FalloffMatch decodes) and AmbientLight off. So FalloffMatch, AmbientLight's InteriorBounceShare / GridLamps and
    ForwardReachMask 0/1 change nothing there (look-1: every variant within 0.2 of today).
  - **The P80 look comes only with brighter lamps along their reach** (Interior off + FalloffMatch 0 + ForwardReachMask 0), and it is
    the P80 numbers: holes V21 3.09 -> 1.07 %, saloon 6.28 -> 1.61 %, bar 2.81 -> 1.92 %; solid +6..+94 %. Price: rooms +30..+67 %
    brighter with exposure off and **still +23..+51 % after the exposure effect settles** (look-4) -- the brightness FalloffMatch fixed.
  - Turning the ambient down does not buy it back: `[Shaders.PBR.Interiors] AmbientScale` 0.5 -> 0.2 moves the room by ~1 % (indoors
    nearly all the light is lamps). Lowering the lamps again (LightingScale 0.8) brings the holes back (V21 2.18, bar 3.36): the holes are
    light the forward shadows cannot take away (lamps without a shadow, the game's own passes); they shrink only as NVR's shadowed lamps
    carry more of the pixel.
  - The pool helps on its own: PoolLamps 32 vs 0, V21 solid 4.73 -> 6.07 % (more lamps shadowed), others +2..+4 %, no brightness cost.
  - **Proposal.** (a) Keep the defaults; ship the pool on (PoolLamps 32) once its own default is decided. (b) Closest settings-only step
    for this toml: `[Shaders.PBR.LampFalloff] Brightness = 1.5` -- saloon holes 6.3 -> 3.0 %, V21 3.1 -> 2.2 %, solid +5..+36 %, rooms
    +9..+14 % after exposure (the user's choice, not a default). (c) The real "ShadowContrast" needs code: the lamp passes already know
    how much of their light each pixel lost to its shadow; write that share into MaterialBuffer's free alpha (the lamp passes, additive),
    and let a full-screen step after the main list darken the rest of that pixel's light (the unshadowed lamps and the game's passes) by
    ShadowContrast x that share. P80's solid shadows at today's room brightness; not built (needs MaterialBuffer on and the lamp-pass
    MRT write: a session of its own).

## Round 5 (2026-10-29 23:15), claude/rootplan-round3 6928662e, DLL 7740ECB0
- **14. ShadowContrast** (`[Shaders.ShadowsInteriors.Forward] ShadowContrast`, [0-1], default 0; above 0 at startup compiles it in
  (SHADOW_CONTRAST_ON), then live, indoors only). Built **in the one-pass lamp shader** instead of the planned MaterialBuffer alpha +
  full-screen step: with ManyLampsOnePass (the user's toml) the six shadowed lamps, the other lamps and the ambient are in one shader,
  so each shadowed lamp's term is split there into kept and lost light (Includes/ManyLightsDirect.hlsl SC_SPLIT) and the pixel's light
  is multiplied by 1 - strength x lost / (lit + lost) before the glow. No buffer, no extra pass; off = every shader as before (same
  instruction counts, checked with template_compile); +80 instruction slots on. Not in the game's multi-pass path (ManyLampsOnePass
  off, the NVR default): there each lamp pass is added on its own and cannot dim the rest -- that would need the buffer design.
  Runs (same 4 views, toml look-sc / look-sc-expo = user toml + pool 32 + ShadowContrast compiled in): specks 20261029-225123-slotA,
  brightness + cost -225549-slotA, bar cost ABAB -230746-slotC; picture rootplan\shadowcontrast-sal-v21.png.

  | view | today solid / holes | 0.5 | 1.0 | P80 look | room after exposure 0.5 / 1.0 / P80 |
  |---|---|---|---|---|---|
  | Lucky 38 | 6.40 / 0.98 | 7.08 / 1.02 | 7.94 / 1.30 | 8.71 / 0.90 | -0.9 / -2.4 / +28.6 % |
  | Vault 21 | 6.18 / 3.11 | 6.49 / 3.32 | 6.94 / 3.69 | 8.17 / 1.06 | -0.5 / -1.3 / +51.1 % |
  | saloon | 1.17 / 6.32 | 1.59 / 5.66 | 2.49 / 4.15 | 2.16 / 1.42 | -0.6 / -1.3 / +22.7 % |
  | bar | 9.16 / 2.92 | 9.75 / 2.73 | 10.58 / 2.72 | 9.93 / 1.92 | -0.7 / -1.7 / +29.1 % |

  Cost: GPU frame equal to today within noise (Lucky 38 / Vault 21 / saloon +-0.05 ms; bar ABAB x4 -0.10 ms mean, pairs +0.08..-0.29):
  under 0.1 ms. **Verdict:** the solid-shadow part of the P80 look at today's room brightness (solid +8..+113 %, rooms -1..-2 %), at no
  measurable cost; the lit specks only partly (saloon -34 %, bar -7 %, but Lucky 38 / Vault 21 +33 % / +19 %: there the specks are lit
  texels in the shadow lookup itself, which a deeper shadow shows more). **Proposed:** default stays 0 (a look change, and the specks
  are their own fix: the lookup's filter, not the light balance); 0.5 as the suggested value for players who want deeper lamp shadows
  (specks no worse than today except Vault 21 +0.2 points). Handed to the RT merge train.
