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
