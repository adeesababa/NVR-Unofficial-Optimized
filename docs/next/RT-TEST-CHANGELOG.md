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
