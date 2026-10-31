# RT test build changelog (claude/rt-test-merge)

One entry per merge round: what came in, the bench verdict, the package. Every RT switch stays default off and turns itself
off without an RT GPU, the NVR DXVK fork (patch 0028) or NvrRtHelper.exe. Packages live under `H:\_NVR-pkg\rt-test-<hash>\`;
nothing here is pushed, released or installed unless the user says so.

## Round 0 (2026-10-29): first merge, af12c7e7
- In: combo-1028 17c29002 + rt-sun-core 4a2d2979 (RT core, 64-bit NvrRtHelper, RT sun shadows) + rt-reflections 915657ea +
  rt-lamp-shadows d64e38fc + rt-ao-sky 679fbba1.
- Merge notes: the lamp and AO branches had turned CRLF files into LF (the "6900 deletions"); merged ignoring line endings,
  CRLF kept. ShaderManager registers all four RT users.
- Bench PASS: 1070 RT off = combo-1028 look; 1070 and plain D3D9 with every RT switch on turn RT off by themselves; iGPU 720p:
  sun core timer ~2.9 ms, lamp shadows (Vault 21, 8 lamps) 18.3 ms, AO ~+8 ms, all four +32 ms on a ~114 ms frame.
- DLL CRC32 B8433C88, NvrRtHelper 24B71C7E, fork d3d9 373E51A6. Package `H:\_NVR-pkg\rt-test-af12c7e7\`.
- Released by the user's choice as `rt-test-1` (release branch rt-test bd2089ba, release DLL 04229C0C; not Latest, not
  pre-release, for RT GPU testing only).

## Round 1 (2026-10-29 ~18:00): 37b1b043
- In: combo-1028 f45d6c86 (HDR latch); claude/rt-lamps-2 up to 3da89678 (SkipTracedCubes on with lamp RT: traced lamps keep
  their cached cube map, Lucky 38 cube maps 3.9 -> 1.86 ms GPU; mask scheme glass 0x04 / models 0x08; DebugView 3 +
  DebugLamp, OneSided diagnostic; the Lucky 38 stripe = a one-sided cube-map leak, traced is right); claude/rt-refl-2
  2de5093a (land self-hit skip, VisibleNormals VNDF glossy rays, HitLamps, RoughReach, mask 0xEF).
- Held: rt-sun-2 (rtsun-11 runs ended "NVR did not answer", the 53bd124d toml break; waits for a passing rerun on 94998464+),
  rt-lamps-2 62575ea4 (per-pixel lamp selection, untested), rt-ao-gi (GI too bright).
- Merges clean, no line-ending churn, defaults toml parses; includers of the changed headers touched and rebuilt.
- Bench PASS: 1070 RT off (20261029-174556-slotC) vs round 0 = 7/7 shots at noise level; iGPU interior (174850-slotA): lamp
  RT 8 lamps core p50 20.5 ms (Vault 21 frame 118 -> 140 ms), reflections + AO trace, all on 118 -> 152 ms, cube A/B 0.04 %.
- DLL CRC32 C7A8960B, helper 24B71C7E and fork d3d9 373E51A6 unchanged. Package `H:\_NVR-pkg\rt-test-37b1b043\`.

## Round 2 (2026-10-29 ~18:50): 6aa00dd4
- In: claude/rt-sun-2 94998464: NvrRtHelper picks the game's GPU by adapter LUID (device UUID / probe as fallbacks) and names it
  in its ready / no-RT messages; RT core alpha-tested casters ([Main.Main.RayTracing] AlphaTested, off; mask bit 0x10, shared
  Compute/Rt/RtAlpha.glsl; not effective yet, ~5 meshes a frame); fork patch 0029 (1.8-rt-uv, texcoords staged); the mask table
  in FORK-RT-CORE.md (0x01 land, 0x02 objects, 0x04/0x08 rt-lamp, 0x10 alpha-tested; non-alpha rays 0xEF); defaults toml fixed.
- Held: rt-sun-2 dc1a5dd4 (GPU alpha atlas, until rtsun-12), rt-lamps-2 fb07f685 (no READY line yet), rt-ao-gi.
- Merge clean, no line-ending churn, toml parses; includers of NvrDxvkExt.h / RtCore.h touched and rebuilt. Helper rebuilt
  (protocol header changed); fork d3d9 now rtsun\fork-0029 (0027 + 0028 + 0029).
- Bench PASS: 1070 RT off (20261029-182620-slotC) vs round 0 = 7/7 at noise level; iGPU Goodsprings (183737-slotC): helper
  "found by adapter LUID", sun / reflections / AO trace, core timer p50 3.4 ms.
- DLL CRC32 BABE6FD4, NvrRtHelper E09D0B71, fork d3d9 8FC9AE11. Package `H:\_NVR-pkg\rt-test-6aa00dd4\`.

## Round 3 (2026-10-29 ~21:20): 36b99651
- In: claude/rt-sun-2 88159b51 (skinned actors in the RT scene via fork patch 0033 GPU skinning, mask 0x20, [Main.Main.RayTracing]
  Actors off + ActorRadius; alpha atlas on the GPU, AlphaTested works (~900 alpha meshes a frame, still off by default); soft
  sun shadows [Shaders.ShadowsExteriors.RT] SunAngle 0 = hard + Denoise; shared denoiser RtDenoise.h; RtCore::Actors());
  claude/rt-lamps-2 d6d71721 (per-pixel lamp selection over every shadowed lamp, MaxLamps 63, LampsPerPixel 2; people in the
  lamp rays via RtCore::Actors; traced lamps keep their cube maps incl. partial ones: cube cost ~0 while traced; Filter off);
  claude/rt-ao-gi efe6839f (RT AO eye fix, RT sky 4x4, alpha casters, one-bounce RT GI v2 [Shaders.AmbientOcclusion.RT] GI
  off + GIDistance / GIStrength / GIOffscreen / GIClamp / GIAmbient, Interleave); claude/rt-refl-2 e7db92c5 (lamps at the hit,
  VNDF glossy rays, reach + 1 ray per 2x2 by roughness, land self-hit skip, alpha casters via ReflectionsRTAlpha, hit-distance
  blur).
- Train fixes: PointShadowForward.hlsl back to CRLF (the lamp branch made it LF again); 36b99651 RT protocol 3 with
  NVR_RT_MAX_IMAGES 32: with all four effects merged the core registered ~20 shared images and switched itself off for the
  session ("more shared images than the protocol holds"; first iGPU run 20261029-203413-slotA had no RT at all). Told rt-sun.
- Merges clean; toml parses; every new switch off; includers of the changed headers touched and rebuilt; helper rebuilt.
- Bench PASS (rerun "3b"): 1070 RT off (20261029-205024-slotC) vs round 0 = 7/7 at noise level; iGPU interior with fork-0033
  (210851-slotA, list rtm-igpu-int3): lamp RT core p50 23.4 ms (Vault 21 frame 116 -> 136 ms), Actors on +0.05 % px, GI on
  visible bounce (28 % px), reflections + AO trace, all on 157 ms; cube A/B 0.04 %.
- DLL CRC32 3B5D85BB, NvrRtHelper 3B01C4F1 (protocol 3), fork d3d9 rtsun\fork-0033 B8CF2308 (0001-0029 + 0033).
  Package `H:\_NVR-pkg\rt-test-36b99651\`.

## Round 4 (2026-10-29 ~21:55): bd4cd0b2
- In (asked by the main session through the intshadow session): claude/rootplan-round3 e38ee863 = claude/material-buffer 9eb91a64
  (MaterialBuffer, X4505 land fix, ObjectMotion, LightCommonLamps, ShadowPool; all off) + rootplan round 3:
  [Main.Main.Quality] CapturedNormalsLeanHook default ON (their A/B: same pictures, Ultra-Luxe -0.16 ms CPU-bound, run
  20261029-211101-slotB), [Shaders.Terrain.Displacement] FarHysteresis 0 (off, unproven), live FSR / DLSS switching fixes;
  also brings the WaterGun demo (BloodFluid water tool, off) and the HDR latch as 73b55860 (same change as combo's f45d6c86).
- Conflicts: PointShadowForward.h includes (kept RtLampShadows.h + ShadowPool.h), Reflections.cpp (kept the RT spread binding for
  techniques 2/4 and ObjectMotion's Temporal flag for technique 3). DxvkCompute.cpp back to CRLF. Toml parses; includers of the
  changed headers touched and rebuilt.
- Bench PASS: 1070 RT off (20261029-212551-slotC) vs round 0 = 7/7 at noise level; iGPU interior (214436-slotA): RT as round 3
  (lamp core p50 27 ms, GI bounce, all on 154 ms; cube A/B 0.04 %).
- DLL CRC32 956D1566; helper 3B01C4F1 and fork-0033 B8CF2308 unchanged. Package `H:\_NVR-pkg\rt-test-bd4cd0b2\`.

## Round 5 (2026-10-29 ~23:50): c1171da1
- In: claude/rt-sun-2 74f4b30d (actors as whole bodies with helper refit; ActorUpdateInterval 2; RtDenoise RGB mode + half res
  with the crash fix 02222b6d; sun HalfResolution off; HitTextures off + RtHit.glsl; helper per-pass GPU times; protocol 3 kept);
  claude/rt-ao-gi d42bbf80 (RT AO through RtDenoise at half res, GI RGB denoise, Interleave v2, GIInterleave; all under RT
  Enabled off); claude/rootplan-round3 635de460 (SharedDrawState default ON, -0.25 ms CPU, same picture; ShadowContrast 0).
- Not taken: rt-sun-2 4d776164 (RtDenoise Ids, untested); rt-lamps-2 / rt-refl-2 had no newer READY lines.
- Merges clean; ROOTPLAN-RESULTS.md back to CRLF; toml parses; includers of the changed headers touched; helper rebuilt.
- Bench PASS: 1070 RT off (20261029-233914-slotC): interiors at noise level vs round 0, outdoors 10 % px = a tumbleweed in
  front of the truck + bush sway (checked against round 4's picture: same scene noise as round 0's RT-on run); iGPU Goodsprings
  (233037-slotC, list rtm-igpu-ext5): helper by LUID, sun hard / soft / actors + alpha-tested, AO + GI, reflections, all on
  trace; core p50 2.9 ms (sun), 8.3 ms (all on).
- DLL CRC32 9C8D82F0, NvrRtHelper 0DEFB626 (protocol 3), fork-0033 B8CF2308. Package `H:\_NVR-pkg\rt-test-c1171da1\`.

## Round 6 (2026-10-30 ~02:00): f0fc7031
- In: claude/rt-lamps-2 6a2646f2 (people proven in the lamp rays, Interleave 1, Denoise via RtDenoise Ids off, Filter off;
  brings rt-sun-2 4d776164 RtDenoise Ids, which it was tested with); claude/rt-refl-2 1fbf3004 (glass reflections via
  core/GlassMask.h, [Shaders.Reflections.Main] GlassReflect 0 = nothing hooked; GlowBrightness reader, inert until the core sets
  the glow record flag; hit textures under RT Enabled off); claude/rt-ao-gi 79a6d04a (GI bounce albedo from hit textures, GI and
  HitTextures off); claude/rootplan-round3 525ba81c (ShadowContrast for the game's multi-pass lamp passes + F10 timer, default 0).
- Not taken: rt-sun-2 past 74f4b30d (cell-change actor release, textured actors with the amended patch 0033 = fork-0033b, glow
  flag): no READY line yet. Fork stays rtsun\fork-0033 (no branch in this round contains 4a71bb3a).
- Conflict: ReflectionsTemporal.spv (rootplan's ObjectMotion .frag + rt-refl's Reflections.glsl) recompiled from the merged
  source (glslangValidator 1.3.296, spirv-val OK); every other shader this round touched recompiles byte-identical.
  ROOTPLAN-RESULTS.md back to CRLF. MaterialBuffer alpha note (a >= 0.5, not == 1): no RT/compute code reads it.
- Bench PASS: 1070 RT off (20261030-015139-slotC) vs round 0 = 7/7 at noise level; iGPU interior (005916-slotA): lamp RT core
  p50 24.4 ms (Vault 21 117 -> 145 ms), people in the lamp rays +0.05 % px, reflections + AO + GI trace, all on 150 ms,
  cube A/B 0.04 %.
- DLL CRC32 8B889510, NvrRtHelper CFF056B2 (protocol 3, same source as round 5), fork-0033 B8CF2308.
  Package `H:\_NVR-pkg\rt-test-f0fc7031\`.

## Round 7 (2026-10-30 04:00, 4fe32674)

- Merged: claude/rt-sun-2 0b138786 (textured skinned actors in the hit atlas = fork patch 0033 amended -> ship fork-0033b,
  refit texcoord fix, glow record flag, cell-change actor release, RtDenoise Ids/Kernel, sun DenoiseKernel 5, RT-HARDWARE-TEST.md);
  claude/rt-refl-2 2eeee727 (glow at RT hits verified, MaterialBufferRoughness off); claude/rt-lamps-2 6e60a90e (Flashlight and
  Exteriors RT, both off + restart, ExteriorDebug); claude/rt-ao-gi 78d8d119 (AOAlpha, DenoiseKernel 3, ReSTIR GI reuse
  GIReuse 8 / GISpatial 0 inside GI (off), history cut on light changes, RT AO RayCount 4 -> 2); claude/rootplan-round3 7142d073
  (SettingsCache on, CasterListCache + MenuHiddenLean off, results doc).
- Not taken: rt-sun-2 76085f8c (docs only, no READY line).
- Conflict: ReflectionsTemporal.spv recompiled from the merged source (spirv-val OK); all shaders this round touched match
  their sources. FORK-RT-AO.md and ROOTPLAN-RESULTS.md back to CRLF.
- Bench PASS: 1070 RT off (20261030-040757-slotC): interiors at noise level, outdoors only clouds / bush sway / an NPC moved
  (lighting the same); iGPU Goodsprings (041043-slotC, list rtm-igpu-ext5): helper by LUID, sun hard/soft/actors+alpha, AO + GI,
  reflections, all on each change the picture, debug view right, no crash. Timings not trusted: rp8-skipluxe-4 and
  rp8-skipv21-1 ran in other slots at the same time.
- DLL CRC32 F7BAB601, NvrRtHelper 077433EA (protocol 3), fork-0033b 9E5E68CF. Package `H:\_NVR-pkgt-test-4fe32674\`.

## Round 8 (2026-10-30 07:20, d4a2439c)

- Merged: claude/rt-sun-2 a5e04304 (RTX GPU fault fix: fork patch 0034 1.8-rt-seq orders the arena copy after the vertex
  conversions + sequence texel; helper arena guard clamps indices and builds stale geometry empty; + e7f56980 scene-cost switches
  Compact / BuildTrianglesPerFrame / BuildNearRadius / CullSmallAngle, all off); claude/rt-lamps-2 a888f059 (ExteriorForward off,
  StartOffsetPerUnit 0.002 for all lamp traces, SKIN2011 outdoor record); claude/rootplan-round3 eee49d29.
- **ObjectMotion is now ON by default** ([Main.Main.Quality], rootplan eee49d29): moving people and objects keep a sharp TAA/FSR
  history.
- Ported a605e259: Balebandro's LOD water crash fix (Physically-Based-Reloaded 1ce5d62, author Alejandro Martinez):
  IsWaterNodeVisible (0x4E62E0) detour returns false for water 3D that is not a node or has no children array.
- Fork from now on: rtsunork-0033c (D58B4425, patches 0001-0029 + 0033 + 0034, RT-only).
- Conflict: PointShadowForward.h (pool slots + ExteriorForward members: both kept). PointShadowForward.hlsl back to CRLF.
- Bench PASS: 1070 RT off (20261030-071047-slotC) vs round 7 = 7/7 at noise level; iGPU interior (070945-slotA, rtm-igpu-int3):
  helper by LUID, every RT effect works, shots within 0.1-1.3 % of round 6 except Lucky 38 lamp RT 2.1 % (far railing / stairs
  no longer self-shadowed: intended), lamp trace ~22.5 ms, no stale-arena warnings, no crash.
- Side job: rt-test-7 swap package H:\_NVR-pkgt-test-7-fix-587f0ac8 (claude/rt-test-7-fix = 4fe32674 + e7f56980 + a5e04304),
  smoke PASS (1070 064310-slotC, iGPU 064526-slotC).
- DLL CRC32 7F06CF91, NvrRtHelper FC94590F (protocol 3), fork-0033c D58B4425. Package `H:\_NVR-pkgt-test-d4a2439c\`.

## Round 8b (2026-10-30 08:00, e777f57f)

- Merged claude/tester-fixes-1029 35657ae4 (dxvk-fork-2's tester fixes, all slot-checked on their branch):
  - sky horizon tint fix bb46a26d: the red band in the sky and the water (the hook overwrote SKY.pso's horizon tint, c20 -> c32);
  - SunsetGlareFix c63334d7 (on): the god rays' sun glare at sunset;
  - SunShadows stage-3 cache binds 492526d1;
  - NvrDxvkMixCheck (diagnostic, off) up to 03646a10;
  - Pip-Boy HueSafeClip tonemap fix 603d8f85 (shader only);
  - WeatherFogColor 35657ae4 (on, 1.0): NVR fog takes the weather's fog colour again (macintroll's report).
- Conflicts: include lists in Render.cpp / RenderManager.cpp / AmbientLight.cpp and Render.cpp's frame start (ObjectMotion +
  MixCheck): both sides kept. No line-ending churn.
- Bench PASS: 1070 RT off (20261030-073237-slotC) vs round 8: interiors at noise level; outdoors the pink horizon band is gone
  (intended) + bush/NPC motion. iGPU Goodsprings (074428-slotC, rtm-igpu-ext5): every RT effect changes the picture as in round 7,
  helper by LUID, no stale-arena warnings, no crash.
- DLL CRC32 A0BDCFF8, NvrRtHelper FC94590F (unchanged), fork-0033c D58B4425. Package `H:\_NVR-pkgt-test-e777f57f\`.

## Round 9 (2026-10-30 10:30, a93b6f18)
- Merged: rt-sun-2 d40b6645 (glow atlas: [Main.Main.RayTracing] GlowTextures false; RT passes take up to 12 images, helper
  extra-p3d source; RT sun DebugView 7/8 indoors), rt-refl-2 26c842ba (HitAmbientGrid true inside RT off, DebugView 6, CloudSky
  false = not passed, keep off), rt-ao-gi d631389b (full-rate GI inside the AO pass, -6..-9 ms on the iGPU; GlowGI 0, GlowDirect 0),
  rt-lamps-2 e2111852 (RankBy 0; outdoor lamp-shadow records on the land, only with ExteriorForward, off), rootplan-round3
  56600f32 (round 8/9 results docs only; KeepDarkening-off is a proposal, not merged as a default change).
- No default changes: every new switch is off.
- Conflicts: ReflectionsTemporal.spv (recompiled from the merged .frag; the compiler reproduces both sides byte for byte),
  PointShadowForward.h include list (both kept; first commit left markers, fixed in 3605495b before the build).
  ROOTPLAN-RESULTS.md came back LF: CRLF restored (a93b6f18).
- Bench PASS: 1070 RT off (20261030-100630-slotC) vs round 8b: Goodsprings at noise level; Vault 21 two shots slightly above
  noise = the NPCs moved (diff image). iGPU Lucky 38 + Vault 21 (102334-slotA, rtm-igpu-int3) vs round 8: every shot within
  0.2-1.3 %, helper by LUID (24 shared images), merged GI pass active, no errors, no crash.
- DLL CRC32 B97D6E83, NvrRtHelper 45ADE162, fork-0033c D58B4425. Package `H:\_NVR-pkgt-test-a93b6f18\`.

## Round 10 (2026-10-30 13:10, bc6a83c2) - HELD, not packaged
- Merged: rt-sun-2 9f05fd21 (helper shader limit 16 -> 32 = PROTOCOL 4: NVR DLL and helper ship together; a protocol-3 helper
  refuses, RT off with one log line), rt-refl-2 408a21cc (glow maps at RT reflection hits, with GlowTextures), rt-ao-gi b9f112ee
  (GlowGI takes the glow atlas), claude/motion-ring-fix e1568f79.
- motion-ring-fix: NEW ON DEFAULTS: [Main.Main.Quality] ObjectMotionSkipCameraFollow true (FSR blur-ring fix: the sky layers that
  follow the camera no longer count as moving); [Shaders.ShadowsInteriors.Forward] VisibleRanking 1 (lamp-shadow slots ranked by
  occlusion-queried visible share, 0.07-0.10 ms GPU), ShadowHold 2 (also holds pool lamps), lamp-shadow FadeIn 0.25 (Vault 21
  shadow popping). Also: ObjectMotion logs what moved; bench setting-shots walk command.
- Conflicts: none. ObjectMotion.h came back LF: CRLF restored (bc6a83c2). ShaderManager.cpp normalised to all CRLF by the branch.
- Bench: 1070 RT off (20261030-130207-slotC) vs round 9: Goodsprings at noise level; VAULT 21 5.5 % of pixels changed: the small
  floor lamps along the walls lost their bright floor pools (mean brightness 89.9 -> 86.6 in the lamp row). Looks like a
  regression from the new lamp-shadow defaults (VisibleRanking / ShadowHold), not only the intended pop fix: SHADOW FLIPS still
  4.2 gained / 4.2 lost per second. iGPU Lucky 38 + Vault 21 (130420-slotA): protocol-4 helper ready by LUID, RT effects run, no
  crash; Lucky 38 within 0.5 % of round 9; Vault 21 shows the same lamp change.
- DLL CRC32 CDD6912E, NvrRtHelper 2304B910 (protocol 4), fork-0033c D58B4425. Not packaged until the Vault 21 lamp change is
  confirmed intended or fixed.

## Round 10b (2026-10-30 13:25, merge of claude/motion-ring-fix aee0046c) - HELD, not packaged
- + aee0046c: VisibleRanking weights each visible pixel by the lamp's attenuation (partly hidden lamp keeps its shadow, Rivet City).
  The main session says this is installed in the user's game (DLL 626F9201).
- Bench: 1070 RT off (20261030-131953-slotC): Goodsprings differs from round 10 only by a tumbleweed, bushes and clouds;
  Vault 21 STILL has the floor-lamp pools gone (same as round 10). SHADOW FLIPS 5.1-5.5 gained / lost per second.
  iGPU (132206-slotA): Lucky 38 within 0.5 % of round 9, protocol-4 helper ready, RT effects run, no crash.
- DLL CRC32 9206EA8E, NvrRtHelper 2304B910 (protocol 4), fork-0033c. Not packaged.

## Round 10 final (2026-10-30 17:35, 927065d9) - packaged
- On rounds 10/10b (which already hold rt-sun-2 9f05fd21 protocol 4, rt-refl-2 408a21cc, rt-ao-gi b9f112ee and the installed
  claude/motion-ring-fix aee0046c), the main session asked for every READY line since round 9:
  - claude/rt-sun-grass 108cfa5b: RT grass ([Main.Main.RayTracing] Grass false, GrassRadius 1500, GrassMaxTufts 8192), RT sun cloud
    shadows ([Shaders.ShadowsExteriors.RT] CloudShadows false), player rigid parts on their own mask NVR_RT_MASK_PLAYER 0x40;
  - claude/rt-gaps ef50de0c: PlantSway true (acts only with RT and [Shaders.Plants.Wind]);
  - claude/rt-refl-weapon 2c9f7a41: [Shaders.Reflections.RT] Weapon 0 (off), WeaponMetal 1, WeaponRoughness 0.3; non-finite guards
    in the reflection resolve / temporal / composite;
  - claude/fog-clear-noon 529dd55b: [Shaders.VolumetricFog.Main] GameFogFloor 0 (off), GameFogFloorScale 1;
  - 1a8a5e48 cherry-picked alone from claude/terrain-flicker (the rest is test-only): unknown user toml keys logged once each.
  - Balebandro's water fix 1ce5d62 already in since round 8 (a605e259). claude/motion-ring-fix 51498f42 (ClusteredLamps, off, no
    gain) has no READY line: not taken.
- Conflicts: RtCore.cpp player walk (walkingPlayer flag + playerInstances count: both kept, file kept LF). Defaults toml: lines
  added with CRLF set back to LF (927065d9).
- Smoke (one bulk iGPU run, 20261030-171723-slotC, rtm-igpu-ext5): helper ready by LUID (protocol 4, 20 images, 10 shaders,
  ready after 10.8 s), each RT toggle changes the picture vs off (sun 4-8 %, AO+GI 21 %, reflections 12 %, all on 23 % of pixels),
  no errors, no crash; the unknown-key line works (DlssHelperTest). Camera spot differs from round 8b's run, so no cross-round A/B.
- KNOWN: Vault 21 floor-lamp pools gone with the motion-ring-fix lamp-shadow defaults (rounds 10/10b, 1070 + iGPU).
- DLL CRC32 0CE498AB, NvrRtHelper F8AE9D5A (protocol 4), fork-0033c D58B4425. Package `H:\_NVR-pkgt-test-927065d9\`.

## Round 11 (2026-10-30 18:40, e4e76fdd)
- Merged on round 10 (927065d9): claude/terrain-flicker 80b1dd4b (native D3D9 on NVIDIA without game MSAA: the NVAPI depth copy
  takes only the picture's area, no more white near ground; VolumetricFog.fx.hlsl height fade clamped 0..1; FailShaders test key
  in [_Main.Develop.Main], empty), claude/fog-clear-noon 01c7d9db (the same clamp in the Vulkan VolumetricFog.glsl, 44 .spv
  rebuilt), claude/motion-ring-fix 888133d1.
- motion-ring-fix 888133d1: CHANGED DEFAULT [Shaders.ShadowsInteriors.Forward] OwnGlassDistance 24 (a lamp's own glowing glass no
  longer shadows its light; fixes the Vault 21 floor-lamp pools lost in round 10); new, off: [Shaders.TAA.FSR] ReactiveMask 0,
  ReactiveMax 0.9, ReactiveThreshold 0.04, ReactiveDebug false (FSRPrep.spv / FSRUpPrep.spv); ClusteredLamps 0 (restart).
- Conflicts: none; no line-ending churn.
- Check (one iGPU run, 20261030-182657-slotA, rtm-igpu-int3 Lucky 38 + Vault 21 with every RT toggle): Lucky 38 within 0.1 % of
  round 9; Vault 21 floor-lamp pools back as in round 9 (rest 3-4 % = lamp-shadow ranking + NPCs); helper ready in 1.3 s
  (round 10's 10.8 s ran while rtlamp slot runs used the machine), 15 shaders, no errors, no crash.
- DLL CRC32 92341FB0, NvrRtHelper B1ACA97E (protocol 4, source unchanged), fork-0033c D58B4425. Package
  `H:\_NVR-pkgt-test-e4e76fdd\` (carries VolumetricFog.fx.hlsl, VolumetricFog.glsl + the 44 fog .spv, FSRPrep/FSRUpPrep.spv).

## Round 12 (2026-10-30 20:00, 92a85dab)
- Merged on round 11 (aa854b4a): claude/rt-leaves 144df98c (rt-ao-sky: [Main.Main.RayTracing] LeafCards 0 = off, 2 = two crossed
  upright cards a leaf, 3 = plus a level card; SpeedTree leaves in the traced scene, one BLAS per tree model, alpha-tested, mask
  0x10; needs AlphaTested; live; LeafCardsOpaque false debug; with LeafCards on a tree ref's root is not fade-tested) and
  claude/motion-ring-fix cc568c00 (replaces 888133d1: final ReactiveMask built after NVR's world depth resolve, ReactiveMaxDistance
  12000 in FSRPrep / FSRUpPrep, round 11b results; ReactiveMask stays 0 = off).
- Conflicts: none. ReactiveMask.h came back LF: CRLF restored (92a85dab).
- Check (one iGPU run, 20261030-194922-slotC, rtm-igpu-ext5 Goodsprings): verdict PASS, helper ready in 0.47 s (protocol 4,
  20 images, 10 shaders), each RT toggle changes the picture vs off (sun 2-6 %, AO+GI 17 %, reflections 9 %, all on 20 %), no
  errors, no crash. LeafCards stays off in this list (rt-ao-sky's own bench covered it).
- DLL CRC32 7B09B501, NvrRtHelper C8D088CE (protocol 4, source unchanged), fork-0033c D58B4425. Package
  `H:\_NVR-pkgt-test-92a85dab\`.
- LeafCards check (rt-ao-sky's list rtleaf-5, shrub view, iGPU, 20261030-200623-slotC, from the outdoor start): PASS; LeafCards 2:
  54.8 leaf meshes traced a frame, 3: 243.3; picture change vs LeafCards 0: shrub view 6.0 / 6.3 %, RT sun 4.2 % (repeat noise 11 %:
  wind / clouds), RT AO 14.5 % (repeat 3.7 %); shrubs darken under their own traced leaves, no garbage. (A first try 200234-slotA
  started inside Lucky 38: the setpos landed in the wrong cell, 0 instances, not counted.)
