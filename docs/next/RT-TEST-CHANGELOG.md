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
