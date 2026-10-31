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

## Round 6 (2026-10-29 23:58), claude/rootplan-round3
- **16. The "lit specks" are not in the cube shadow lookup.** The holes % metric (cpucheck intshadow_holes, reused in rounds 4-5) sets
  the darkening to 0 where the shadows-off picture is darker than 6/255, so near-black pixels inside a shadow counted as holes: that
  was the saloon's whole 6.3 % and most of the bar's. With a brightness floor of 16 (scratch specks.py; also isolated dots = hole pixels
  with at most 4 others in their 5x5): saloon 0.00 %, bar 0.04 %, Lucky 38 0.53 %, Vault 21 1.54 %. Mapped, the remaining isolated dots
  are light through the Vault 21 catwalk's grating (correct) and lit silhouettes (chair / table edges, railings, lamp globes, pillars);
  none on flat shadowed surfaces. No lookup change (filter / bias would only risk acne). Corrected numbers for ShadowContrast 0 / 0.5 /
  1.0 (one pass): Lucky 38 0.53 / 0.54 / 0.74, Vault 21 1.54 / 1.51 / 1.64, saloon 0.00 / 0.08 / 0.08, bar 0.04 / 0.05 / 0.04 %.
- **17. ShadowContrast for the game's lamp passes** (ManyLampsOnePass off, the release default; c50a4c1c, timer 54bee6ac, DLL 423B7B96;
  needs MaterialBuffer). Base passes that have the ambient (the game's light pass with lamps 2-3, metal or not) darken in the shader
  as the one-pass shader does; the added DIFFUSE lamp passes (plain and FoldHighlights fold) add the light their shadows took to
  MaterialBuffer's alpha (alpha-only mask, only for ONE/ONE blends: measured ~400 such passes a frame in Lucky 38, 0 with another
  blend), and one step after the main list multiplies the scene by 1 - strength x lost / (lit + lost) (a copy of the frame + a
  full-screen multiply blend, MSAA-safe). Off: every shader as before (same instruction counts). Runs (toml look-scmp / -expo = the
  user toml with ManyLampsOnePass false + MaterialBuffer true): 20261029-232933-slotB (specks), -232827-slotA (brightness),
  -235502-slotA (cost).

  | view | solid % today / 0.5 / 1.0 / P80 | specks@16 today / 1.0 | room after exposure 0.5 / 1.0 / P80 |
  |---|---|---|---|
  | Lucky 38 | 6.44 / 6.99 / 7.78 / 8.62 | 0.52 / 0.63 | -0.8 / -2.2 / +29.5 % |
  | Vault 21 | 8.42 / 8.74 / 9.27 / 11.60 | 1.36 / 1.50 | -0.8 / -2.3 / +50.9 % |
  | saloon | 2.57 / 3.50 / 4.92 / 4.13 | 0.03 / 0.09 | -0.8 / -2.0 / +21.6 % |
  | bar | 26.1 / 32.7 / 36.6 / 30.1 | 0.00 / 0.02 | -5.0 / -14.1 / +19.8 % |

  Cost: the step is 0.036-0.050 ms on the GTX 1070 (F10 "ShadowContrast step"); the lamp passes' alpha write is lost in the frame's
  noise. **PASS** (under 0.1 ms, same look as the one-pass version). The bar view darkens more (-14 % at 1.0) because much of it lies
  in lamp shadow. **Proposed defaults:** ShadowContrast stays 0 (a look change; the user / Conor decide); 0.5 the suggested value
  (rooms -1..-5 %); with ManyLampsOnePass off it also needs MaterialBuffer on.

## Round 7 (2026-10-30 03:05), claude/rootplan-round3 -- step 18, NVR's main-thread costs
Profiles: CPU sampler 1 kHz (core/CpuSampler.h), user toml on the DXVK fork, AI on, 4 shots of 600 frames per place; samples in
bench\deep-roadmap\rootplan\samples (luxe / gs / v21 / fs.txt), analysed with cpulimits\samples.py. The sampler stops the thread
~0.22 ms per sample, so the shares rank, they are not times. Top 5 NVR costs per scene (share of the main thread):

| scene | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Ultra-Luxe | ConstantFilter ~15 % (Redundant 4.4 %, its driver calls ~8 %) | lamp caster check ~3 % (PointShadowCasterState + HashCasterBytes + motion map) | MaterialRulesDraw::RuleFor 1.0 % | LinearPreDecode::DecodeFast 0.85 % | toml string map ~0.7 % + ImGui mouse 0.66 % |
| Goodsprings (GPU-bound, Present 21 %) | PhysAtmosphere Chapman / Radiance / EvalSky ~5.5 % | SettingManager FillNode / SplitString / toml ~2 % | ImGui 1 % | ConstantFilter | sun cell walk |
| Vault 21 | ConstantFilter ~12 % (Redundant 4.3, SendPixel 2.9, HookVertex 2.0, HookPixel 2.8 %) | LinearPreDecode 1.24 % | PointShadowForward::SetForPass 1.22 % | ManyLamps::SetForPass 1.13 % (+0.78 % its driver calls) | MaterialRulesDraw 0.70 % |
| Freeside night | ConstantFilter ~3.6 % (+ NVR's driver calls 23 % incl. Present) | sun walk (WalkSunChildren + WalkSunCell) 1.7 % | LinearPreDecode + powf 1.55 % | ImGui_ImplWin32_UpdateMouseData 0.91 % | RmaosDraw::ForObject 0.64 % |

The sky (Goodsprings) already skips unchanged inputs; its inputs change every frame, so a cut would need a tolerance (not the same
picture): left. Freeside at night: `set gamehour` crashed the game twice (0x00877C6D, game code only); profiled in lucky22 + moveto.

Cuts (each its own switch, [Main.Main.Performance], live; same picture; A/B = 4 rotated rounds, AI off, CPU-bound):
- **SettingsCache** (7ffeb721, DLL 473F53BC; default on): GetSettingI/F/S keep each (section, key) result until any setting changes
  (SettingsRevision). Ultra-Luxe: off 6.72 / on 6.45 ms frame interval, **-0.27 ms**, every round (+0.35 / +0.25 / +0.11 / +0.33 off).
  PASS, stays on.
- **SkipRedundantConstants off** (the ConstantFilter, ~15 % of the thread in the sampler): 6.37 vs 6.45 ms, **-0.08 ms** with the
  filter off, all 4 rounds the same way. Under DXVK the filter costs slightly more than the driver calls it saves; under 0.1 ms.
- **CasterListCache** (4e3cd2f4, DLL 7E744708; default off): each lamp keeps its casters' motion entries and only looks up a caster that
  is new at its place in the list; a caster's hash reuses its transform's hash. 6.37 vs 6.45 ms, **-0.08 ms** (rounds -0.24 / +0.12 /
  -0.10 / -0.11): under 0.1 ms.
- **MenuHiddenLean** (1f828641, DLL 5A8F5401; default off): with the menu closed, no Win32 backend frame (only the display size and
  the time step are set).

Second A/B, the same cuts in Vault 21 and Freeside at night (cut-3, AI off, 4 rotated rounds; frame interval ms, base / with the cut):

| cut | Ultra-Luxe | Vault 21 | Freeside night | verdict |
|---|---|---|---|---|
| SkipRedundantConstants off | 6.45 / 6.37 (-0.08) | 6.86 / 6.71 (-0.15) | 4.74 / 4.64 (-0.10) | -0.08..-0.15: candidate, see below |
| CasterListCache | 6.45 / 6.37 (-0.08) | 6.86 / 6.83 (-0.03) | 4.74 / 4.60 (-0.14) | under 0.1 ms on average: stays off |
| MenuHiddenLean | - | 6.86 / 6.82 (-0.04) | 4.74 / 4.81 (+0.07) | nothing: stays off |
| all three | - | 6.86 / 6.73 (-0.13) | 4.74 / 4.58 (-0.16) | = SkipRedundantConstants off alone |

Runs 20261030-025737-slotB (Vault 21) and -030515-slotB (Freeside). Both overlapped another session's game run (rtrefl20 and rtgi17)
on the same machine; Freeside's rounds drift by up to 0.9 ms, so its column is weak. Pictures: every variant within base vs base.

**Round 7 verdict.** SettingsCache is the one cut over 0.1 ms (-0.27 ms, on). Turning the ConstantFilter off
(SkipRedundantConstants false) is the only other candidate: faster in all three scenes, the same picture, but -0.08..-0.15 ms
measured beside other games. Proposed: flip its default to false after one clean confirmation run (no other game on the machine);
not flipped here. The rest of NVR's top 5 is under 0.1 ms each (caster check, menu, pre-decode: PreDecodeCache already keeps powf's
results) or not a same-picture cut (the sky), so step 18's rule (cut while each saves >= 0.1 ms) ends here.

Pictures: every variant vs base is within base vs base (>16/255: 0.01-0.04 % of pixels). Run 20261030-025208-slotB (cut-2);
cut-1 (-023913-slotB) is void: another session's game overlapped it until 02:40 and every variant slowed by 2x for that window.

## Round 8

**Step 19: SkipRedundantConstants confirmation. Default stays true (unchanged).** Lists rootplan-skip-luxe / -v21 (6 ABBA rounds,
600 frames, AI off) on claude/rootplan-round3 1f828641 (DLL 5A8F5401). No attempt ran alone: all 7 overlapped an RT session's game
(the RT sessions keep the machine busy), so a clean run is not possible right now. Frame interval ms, base / noskip:

| run | scene | base | noskip | diff | overlap |
|---|---|---|---|---|---|
| 20261030-034548-slotA | Ultra-Luxe | 8.86 | 9.25 | +0.39 | rtao6 (heavy) |
| 20261030-035202-slotB | Ultra-Luxe | 6.22 | 6.29 | +0.07 | rtrefl21 |
| 20261030-035856-slotB | Ultra-Luxe | 6.44 | 6.39 | -0.05 | rtlamp |
| 20261030-040618-slotB | Ultra-Luxe | 10.27 | 11.57 | +1.30 | rtgi20 + rtm-r7 (heavy) |
| 20261030-041148-slotA | Vault 21 | 6.89 | 6.83 | -0.06 | rtm-r7-igpu-ext |
| 20261030-042853-slotA | Vault 21 | 9.52 | 8.71 | -0.81 | rtgi20 (heavy) |
| 20261030-045807-slotB | Vault 21 | 6.92 | 6.86 | -0.06 | rtsun-kill |

The quiet runs give -0.05..+0.07 ms in Ultra-Luxe and -0.06 ms in Vault 21: under the 0.1 ms bar and not the same sign in every
scene, so round 7's -0.08..-0.15 was the other games. Freeside was not run (the rule already fails in Ultra-Luxe).

**ObjectMotion on by default** (eee49d29, toml only, DLL unchanged 5A8F5401): it is a fix (moving people and objects keep a sharp TAA /
FSR history), its read-back check passed (run 20261029-174247-slotC) and it costs 0.03 ms GPU.

**Step 20: road pop, second try. No pop spot found; FarHysteresis stays 0.** Lists rootplan-road-2 / -3 (eee49d29): at each place the
player steps 384 units back and forth every 160 frames, 10 steps with FarHysteresis 0 and 10 with 256. The land log's mean
"pieces flipped since the frame before" per block:

| place | far passes | switches | flips/frame h0 | flips/frame h256 |
|---|---|---|---|---|
| Highway 95 north of Goodsprings (setpos -67850 / -67466, 2450) | 16 | 8 | 0.000 | 0.000 |
| The Strip approach (setpos 2493 / 2877, 90239) | 0 | 0 | 0.000 | 0.000 |
| Primm (moveto 000CDF46 +0 / +384), run 2 | 9-13 | 9-10 | 0.000 | 0.000 |
| Primm, run 3 (h256 first) | 29-30 | 13-14 | 0.010 | 0.000 |
| Novac (moveto 0009289B +2048 / +2432) | 26 | 14 | 0.002 | 0.000 |

Runs 20261030-072313-slotC (road-2, clean) and -075309-slotC (road-3; overlap does not change a count). At most one piece flips
every 100-500 frames while walking, and FarHysteresis 256 takes that to 0. On the Strip approach no far land is drawn. A pop players
would see is not there at these spots, so the hysteresis default is left alone (no picture A/B was needed). If a tester names a
road where land pops, run the same list there.

## Round 9

**Step 21: the dedicated full-screen interior shadow pass.** effects/ShadowsInteriors.cpp (D3D9 technique DedicatedInteriorShadows,
Vulkan ShadowsInteriorsBlur/Apply.frag), called from ShaderManager.cpp (interiorApply, about line 2024).

When it runs:
- **Forward lamp shadows on** (`[Shaders.ShadowsInteriors.Forward] Enabled`, default, and PBR drawing this frame): the pass is only
  `KeepDarkening` (default true): the lamp mask (PointShadows.fx) is drawn with its cube-map lookups switched off
  (TESR_ShadowFade.z = 0) and applied unblurred (no DarkeningBlur) in one pass, darkening (Darkness 0.65) what no lamp in the mask's
  list reaches. It is not a shadow. The mask's list = 12 shadow lamps + 12 tracked + the extra list (the 40 nearest lamps).
  With KeepDarkening off, neither the mask nor the pass is drawn (the buffer is cleared once).
- **Forward off, or PBR off**: the old full path: PointShadows.fx with the live cube slots 0-11, both blurs on the two G16R16 targets,
  then the apply. That path reads the same live cube maps, so TwoSidedCasters reaches it (it changes how the cubes are drawn); the
  ShadowPool does not (pool cubes are published to the forward shaders only), and ShadowContrast does not (forward shaders only).

So with forward on there is nothing of the rework's (pool lamps, two-sided, contrast) to bring to it: those are about lamp shadows,
and this pass draws none. What is left is its cost and its look.

Run 20261030-084344-slotB (list rootplan-int-1, user toml, 600 frames, GPU profile, 2 rounds forward + reversed; overlapped rtlamp,
so the GPU timer per pass is the number to read). GPU ms per pass (GTX 1070, FSR 3.1 at 0.67), mean of the 2 rounds:

| room | base mask + apply | KeepDarkening off | InteriorShadowFold | MaskHalfRes |
|---|---|---|---|---|
| Lucky 38 | 0.071 + 0.022 = 0.093 | 0 | 0.071 + 0 (fog +0.019) = 0.090 | 0.044 + 0.017 = 0.061 |
| Vault 21 | 0.068 + 0.023 = 0.091 | 0 | 0.070 + 0 (fog +0.020) = 0.090 | 0.048 + 0.018 = 0.066 |
| saloon | 0.075 + 0.021 = 0.096 | 0 | 0.075 + 0 (fog +0.012) = 0.087 | 0.047 + 0.019 = 0.066 |
| bar | 0.084 + 0.028 = 0.112 | 0 | 0.065 + 0 (fog +0.015) = 0.080 | 0.053 + 0.024 = 0.077 |

Pictures (960x540; base vs base is 9-10 % of pixels > 2, 0.00-0.01 % > 16):

| room | KeepDarkening off: > 16 / mean | Fold: > 16 / mean | MaskHalfRes: > 16 / mean |
|---|---|---|---|
| Lucky 38 | 0.01 % / +0.08 | 0.01 % / +0.08 | 0.00 % / +0.01 |
| Vault 21 | 0.33 % / +0.42 | 0.01 % / +0.22 | 0.00 % / +0.04 |
| saloon | 0.00 % / +0.16 | 0.00 % / +0.17 | 0.00 % / +0.03 |
| bar | 0.00 % / +0.10 | 0.00 % / +0.09 | 0.00 % / +0.01 |

What the darkening still adds with forward on: almost nothing. Its only visible part (Vault 21, 1781 pixels, 101 -> 119 of 255) is
not unlit corners but distant rooms lit by their OWN lamps (the window room upper left, the far doorways: rootplan\round9\
v21-dark-diff.png): those lamps are not among the mask's 40 nearest, so the pass darkens light that is really there. Everywhere else
it is within base vs base. Cost 0.09-0.11 ms GPU per room (the old "about 0.8 ms" in the toml text predates LampMaskScissor and
MaskSkipFar). MaskHalfRes saves ~0.03 ms with the same picture; Fold saves 0.01-0.03 ms but still differs next to the void.

**Proposal: retire the darkening with forward on: KeepDarkening default false** (the switch already exists; no new code). It
removes 0.09-0.11 ms GPU and one full-screen pass, and its only visible effect is a wrong one (far lamp-lit rooms darkened). Not
flipped here: it changes the image (Vault 21), so it is the user's call. The full old path stays for forward off / PBR off (it is the
only lamp shadow there). If the darkening is kept, MaskHalfRes on is the free part (-0.03 ms, same picture).

## Round 10 (bugs on the installed round 9, a93b6f18): FSR blur ring, Vault 21 shadow pops

Branch claude/motion-ring-fix (on a93b6f18). Bench aid 7be29d14 / 74c4db9c: setting-shot lists take `walk <units/s> [seconds]` (the
player glides forward and back; it pauses after the list's own moveto / setpos).

**FSR blur circle round the player (FSR + LowResEffects): ObjectMotion's sky layers. Fixed, on by default (6abf22e1).** With low-res
effects the upscale runs after ObjectMotion's buffer is made, so FSR read it. While walking, the sky layers (AtmosphereDome, CloudDome,
HorizonLayerClear/Overcast, LowerLayer: radius 450-1060 units, centred on the camera) moved by exactly the camera's move (log, run
20261030-121733-slotA: `move 0.00 -3.20 0.00, camera 0.00 -3.20 0.00`), were drawn as "moved", and their depth matched the ground's along
a ring 7-15 m round the player (the 2 % + 4 unit depth test): that ground got the camera's move as its own and FSR reprojected it as if it
walked along. ObjectMotionSkipCameraFollow (3fc982fc): a geometry whose move equals the camera's counts as still. With it on, nothing
was drawn as moved for 1300 frames of walking in Primm (no 600-frame line at all); off, 6-8 a frame. Workaround on a93b6f18: ObjectMotion off.

**Vault 21 shadow pops: the live slots went to lamps that light nothing visible.** New SHADOW FLIPS log line (F10 profile): lamps
gaining / losing a shadow per second, live vs pool, and (VisibleRanking 2 = measure only) those lighting something visible.
- ShadowHold (8dbf90d2, off): pool members held N s, a pool map kept until redrawn, flicker under 2 % not redrawn, FadeIn continuous
  across live / pool. In the pool runs (20261030-121229-slotA) it held nothing: the pool flips (3-5/s at PoolLamps 64) were not rank-edge.
- VisibleRanking (f5b2bdae, 40ac4c92, off; 0 / 1 / 2 = off / on / measure only): after the depth combine, one occlusion-queried 160x90
  pass per candidate lamp keeps the pixels whose visible surface lies within its reach (walls hide what is behind); answers read a few
  frames later without a flush, smoothed; ranking = brightness x that share, unmeasured next, lighting nothing visible last. ShadowHold
  above 0 then also holds a slot holder (still lighting something visible) for N s. Cost 0.07-0.10 ms GPU (128 lamps measured a frame).

Run 20261030-123241-slotA (lamp-pop-6, Vault 21 casino floor, walking back and forth 100 u/s, PoolLamps 0, 2 rounds):

| variant | lamps with a shadow that light something visible | flips/s (visible) |
|---|---|---|
| LightPoints 24, today's ranking | 7.2 of 24 | 0.00 |
| 24, VisibleRanking | 24.0 | 2.07 |
| 24, VisibleRanking + ShadowHold 2 | 23.9 | 0.00 |
| 24, today's + LampSwitchMargin 0.5 | 7.8 | 0.32 |
| LightPoints 64, today's ranking | 19.2 of 64 | 0.00 |
| 64, VisibleRanking | 64.0 | 6.86 |
| 64, VisibleRanking + ShadowHold 2 | 62.3 | 0.15 |

Lucky 38 (20261030-122534-slotA): no flips in any variant; visible shadowed lamps 11.8 -> 19.6 at LightPoints 24 with VisibleRanking.
The pops were not reproduced at these spots with today's ranking (0 flips); what the numbers show is that 2/3 of the slots were spent on
lamps behind walls. **Proposal: VisibleRanking 1 + ShadowHold 2 (+ FadeIn 0.25)**: three times the visible lamp shadows at LightPoints
24, no churn. Not defaulted: it changes the picture (more lamp shadows where the player looks). Not done: fade-out of a lost live slot.

## Round 11: clustered forward lamps (ClusteredLamps, off) + the G-buffer question

Branch claude/motion-ring-fix 8e72e4bd (DLL DE0BEDFF), base installed aee0046c. `[Main.Main.Misc] ClusteredLamps` (needs a restart with
LightCommonLamps + ManyLampsOnePass on, then switches live): the CPU bins the frame's lamp table into 64 px tiles x 8 log-depth slices
(31 lamps a froxel, most important first) at the frame's first world draw; the many-lamp shader keeps the draw's 6 shadowed lamps and then
lights every lamp of the pixel's froxel instead of the draw's own list (no per-object cap). Off for reflections, cube maps, first person.

Bench cluster-1, night, 600 frames still, ABBA x2, 1070 slot (640x360 render, FSR 0.67): run 20261030-142219-slotA (toml with the table,
cl0/cl1 live) and 20261030-150954-slotA (the user's toml = today). Medians of the second half of each shot, ms:

| place | world GPU cl0 / cl1 | frame GPU cl0 / cl1 | CPU render cl0 / cl1 | binning CPU | entries a froxel, dropped |
|---|---|---|---|---|---|
| Lucky 38 | 3.48 / 3.53 | 5.92 / 5.91 | 5.24 / 5.15 | 0.009 | 3.1, 0 |
| Vault 21 | 4.06 / 4.22 | 7.04 / 7.00 | 6.19 / 6.09 | 0.115 | 8.4, 265 (499 lamps binned) |
| Freeside | 1.92 / 1.93 | 4.90 / 4.83 | 3.13 / 3.05 | 0.006 | 1.4, 0 |
| today (L38 / V21 / FS) | 2.94 / 4.08 / 1.76 world | 5.23 / 6.82 / 4.65 | 4.60 / 5.94 / 3.01 | - | - |

Picture: cl0 vs cl1 is within TAA/FSR noise everywhere (>16 of 255: 0.01-0.05 % of pixels, mean +0.00..+0.05; cl0 vs cl0 gives the same).
The draws' own lists already reached every lamp that lights them (lamps past 24 dropped 0.00, 16 capped passes a frame), so the froxel
loop re-lights the same lamps: no visible gain, +0.05..+0.16 ms world GPU, binning 0.01-0.12 ms CPU. Vault 21's coarse froxels overflow
(265 of 4044 entries dropped, the faintest ones, still no picture change). **Verdict: keep ClusteredLamps off**; worth it only with a
content case where objects are lit by more than 24 lamps or the game's per-object lamp pick misses lamps (none in these three places).
The table toml vs today differs at Lucky 38 (+0.5 ms world and CPU) but not in Vault 21: two launches, not trusted; re-check in one launch
before the table (LightCommonLamps) is offered as a default.

G-buffer albedo pass: no. Census (run 20261030-133028-slotA, Lucky 38): 1772 world draws a frame, multi-pass objects 0, extra lamp /
highlight / texture passes 0 (one-pass lighting already); world GPU 3.1 ms, of it NVR 4+-lamp shaders 70 %, 1-3-lamp 21 %. The lighting
a deferred pass would replace is already one pass and clustered shows no missing lamps, while the frame is CPU-bound (render 3-6 ms CPU =
the frame interval, GPU 4.6-7 ms on the 1070 at 640x360); a G-buffer pass re-submits every world draw (~1772 a frame, the costly part on
the CPU) to save at most the 21 % (~0.65 ms GPU) the game's own short lamp lists already light cheaply. Nothing in these numbers it would add.

## Round 11b: Vault 21 floor pools (OwnGlassDistance), LightCommonLamps cost, D1 ReactiveMask

**Vault 21 floor lamps lost their floor pools with the round 10 lamp-shadow defaults (merge round 10 note). Fixed: OwnGlassDistance 24
(fa516ed1, on).** pool-1 (run 20261030-180052-slotA, one switch a shot from today at the merge view): only VisibleRanking changes the
picture (ShadowHold 0, FadeIn 0, LampModelShadows 0: 0.00-0.01 %); VisibleRanking 0 is brighter by +0.51 mean (1.77 % of pixels > 16).
LogLamps: the floor lamps stand 15 units above the floor, 0 units from their own glowing glass; VisibleRanking gives the visible ones a live
slot, and the glass then took their whole floor and wall pool. pool-2 (180948): LampGlassShadows 0 or LampModelOwnDistance -1 bring back
+0.45 of it; the rest is real pillar / wall shadows. OwnGlassDistance (new, [Shaders.ShadowsInteriors.Forward], live, below 0 = as before):
a lamp part within that many units of a light casts nothing into that light's map (other lamps' maps keep it). Run 20261030-182137-slotA
ABAB: Vault 21 +0.45 on both pairs; VisibleRanking 1 vs 0 at that view now 0.01 % (the same picture); Lucky 38 0.10 %, Freeside noise.

**LightCommonLamps cost (the open +0.5 ms), ABBA launches** (runs 180052 A / 180303 B / 180507 B / 180713 A, still timing shots): world GPU
Lucky 38 3.01 / 3.07 / 3.07 / 3.00, Vault 21 0.99 / 1.10 / 1.53 / 1.00, Freeside 1.63 / 1.68 / 1.74 / 1.63 ms; CPU the same. The table
costs +0.06 ms (Lucky 38), +0.05-0.1 (Freeside), +0.1-0.5 (Vault 21); the +0.5 ms was cross-launch noise.

**D1 ReactiveMask (off): FSR's reactive mask from what the world draws over its main opaque list** ([Shaders.TAA.FSR] ReactiveMask scale,
ReactiveMax 0.9, ReactiveThreshold 0.04, ReactiveMaxDistance 12000, ReactiveDebug; core/ReactiveMask.h; final de042902, DLL 00BC37EF).
Colour copied when the main list ends and after the world's depth resolve; mask = largest change of c/(1+c) x scale; FSRPrep / FSRUpPrep
take max(weapon, mask) where their depth is nearer than ReactiveMaxDistance. How it got there:
- the blended stage alone (RenderBlended 0xB650C0) holds almost none of it: 0.00 % (run 183310); window glow cards and neon are elsewhere;
- main list end to world end also marks the sky and the distant land (drawn after the main list; run 182413, x4 debug view);
- a D3D9 pass reads the INTZ world depth as 0 under NVR DXVK, even after the resolve (runs 190111, 190349): the distance cut is in the
  FSR prep compute shaders, on their own depth.
Run 20261030-190713-slotA (still Freeside at night, FSR 3.1 RenderScale 0.67, ABAB): sky pixels changed > 8 levels vs mask off: cut
0.49 %, uncut 2.33 %, mask-off repeat 0.11 %; lower half (the near alpha draws) 0.85 %. Coverage walking: 5.2 % of the screen > 0.1 at
scale 1, 8.4 % at 4. Not judged: whether smoke / sparks trail less in motion (needs a scene with them and a look in motion); cost not
measured cleanly (walk timings swing; two screen copies + one pass at 960x540).

## Round 12: plan step C (the other shader families onto one lamp list) + the reactive trailing question

**Which family loses lamps (LAMP CAPS census, d618e2fc, run 20261030-195707-slotA).** Per pass type and object, a frame's passes against
the object's own active lamps (its lighting property's list). Lucky 38, Vault 21 (people) and Freeside at night:
- the shiny many-lamp shaders (SLS2034/2036) show 16 / 550 / 15 objects a frame "short" by 21 / 2850 / 72 lamps, and ManyLamps' own line
  shows it adds exactly those (16 / 564 / 16 objects, 21 / 2902 / 77 lamps): they are lit;
- every other object family (SLS2011-2031 objects, people, NOLIGHT) is short of nothing; SLS2024 (2-lamp glowing objects) 3 objects;
- the land (TerrainTemplate SLS2092-2146, lamps from Vanilla Plus Terrain): in Freeside half the pieces near the lamps (7.25 own lamps)
  are drawn by the 6-lamp land shader with lamps left out (2 objects, 15 lamps a frame).
So step C's per-family table ports have nothing to light that is not lit already, except the land. The rest of C's value (fewer
permutations, one falloff / shadow lookup) carries no measured picture or time gain here; the one shared lookup the atlas needs is built
with the atlas step.

**LandAllLamps (off, restart; e042b74f, DLL E35F344C).** The land cannot read the table (no free sampler), so NVR adds the piece's
other lamps after VPT's in VPT's own registers (c39 colours / c63 positions / c88 count; land shaders compiled for 24), after VPT's count
write. Self-check against VPT's entries: v1 (acting on every write) 46 % matched and refused itself; v2 100 % matched by position, colour
difference 0.0000 (run 20261030-202304-slotA): 16 lamps a frame added to 6 land draws. Picture vs off (run 202124, two launches, Freeside
at night and with a placed street light): 0.09-0.11 % of pixels > 16, mean -0.02, the cross-launch noise; no change on the road in the
diff maps. The added lamps are in each piece's list but reach nothing visible at these spots. Stays off.

**Reactive mask and fire (the trailing question).** The game's fire FXFireMed01 placed ahead in Freeside at night (it drifts as puffs).
Walking (run 201820) and still (run 202441), ReactiveMask 0 / 4 alternating: the fire puff is on screen in 4 of 6 ReactiveMask 4 shots
and 0 of 6 mask-off shots (p ~0.03 if it were chance). Suggests FSR without the mask drops short-lived fire; with it the fire shows.
Not a proof (the puffs are random); worth a look in motion before defaulting ReactiveMask on.
Notes: the SettingShots `walk` applies to the whole list; `sgtm 0.001` freezes particle effects (none emit); placeatme <base> 1 300 0 puts
it ahead of the player.

## Round 13: INTERIOR-SHADOWS-MODERN step 1, the shadow atlas (ShadowAtlas, off)

`[Shaders.ShadowsInteriors.Atlas] Enabled` (restart; Size 8192 = 128 MB R16F). core/ShadowAtlas.h, PointShadowForward.hlsl
POINT_SHADOW_ATLAS. Every lamp shadow NVR publishes indoors (the live LightPoints slots, people included, and the ShadowPool's still
maps) is also an octahedral tile of one atlas (live 1024, pool 512 at 8192), and the object / skin shaders read the draw's lamps from it
on s8 instead of six cube maps: the same filters, only the tap reads the tile (each light's tile in its tint register c182-c187; the
debug views 1-2 then tint every lamp red). A live tile is converted from its cube map when that was redrawn (or the slot changed lamp),
a pool tile when its cube map was drawn. Off: no define, the shaders as before.

Bench (atlas-still / atlas-walk, cube = user toml, atlas = user-toml-atlas; final 7b953f41, DLL 37C23EF7, runs 20261030-220400 /
221308 stills, 220758 / 221509 Vault 21 walk):
- look: atlas vs cube maps Lucky 38 0.04 % of pixels > 16 (repeat noise 0.01 %, cross-launch), Vault 21 0.36 %, Freeside 0.03 %, means
  +-0.02; PoolLamps 160 changes Vault 21 by 0.13 % in both;
- popping: SHADOW FLIPS 0 / s walking with the pool off in both; with PoolLamps 160 (76 lamps shadowed) 12-20 / s in both (the pool's
  own churn, not the atlas's);
- cost (GTX 1070, Vault 21 walk): world scene 3.8-3.9 ms vs 4.5-4.6 (one 2D sampler instead of six cubes), conversion 0.20 ms (about 5
  live tiles a frame; every frame was 1.3 ms), frame GPU 6.96-7.10 vs 7.25-7.45 ms, CPU 7.2 vs 7.6 ms.
Bugs on the way, fixed: the tap script rewrote the cube path's own PS_TAP into itself (skin lamp-shadow shaders failed to compile, so
forward lamp shadows switched off with the atlas OFF: commits 0af1b129-d5..., fixed 2eeeac12; never sent in a READY line); a dump of the
atlas (Dump) and a per-slot trace (TraceSlot, DebugView 3 reads the atlas) were added to find it. The atlas's first "6 % darker" was the
atlas against the screen-space fallback, not against the cube maps.
Next (steps 2-3): capsule shadows for people (they then leave the live cube maps), contact shadows.
