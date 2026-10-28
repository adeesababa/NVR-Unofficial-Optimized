# Combo 2026-10-28 (`claude/combo-1028`)

User: "merge this with the fsr 3.1 session and the connor session. merge anything else i might be missing". Base: the installed
P84 v3 build `claude/p84v3-tessring` 5c547109. Nothing pushed, not installed.

## What is in it

| branch | what | doc |
|---|---|---|
| `claude/captured-normals` (4686dfd5 + docs) | CapturedNormals: surfaces' smooth normals for every effect (`[Main.Main.Quality] CapturedNormals`, **on by default since this combo**: user, "it's a fix"), snow `CapturedNormals` / `Look 1` (macintroll's v5, off), land / parallax smooth shadow normals, native D3D9 draw-hook fix (also revives LinearScene on native) | CAPTURED-NORMALS.md |
| `claude/fsr31-upscaler-0ab1f2` (525cd2b3) | FSR 3.1 upscaler next to FSR 2.2.1 (`[Shaders.TAA.FSR] Version 1`; default 0 = FSR 2 as before), NVR DXVK only | FSR31.md |
| `claude/manylamps-port` (3e64691b) | Conor (mrfury97)'s ManyLampsOnePass (off, restart, DXVK only), ManyLampsGlow and FoldFog (on) | MANYLAMPS-PORT.md |

One conflict (Render.cpp frame start: CapturedNormals::BeginFrame next to FoldFog::BeginFrame, both kept).

Checked and **not** merged:
- the four RT branches (rt-sun-core, rt-ao-sky, rt-lamp-shadows, rt-reflections): paused, rays still miss, older base;
- `deep/fork2-drawskip` (DrawSkipCensus) and `claude/benchq-warm` / `claude/serene-darwin-89488f`: diagnostics / bench tools;
- `claude/skytint-moon` e86e837c "Physical sky off by default" (10-25): the physical sky is on by default now; a default flip is the
  user's call;
- `claude/elated-gates-89d793` (10-26): the NvrDxvkKeepD3D9 test switch from the Conor fork-flicker hunt (the fix is in the DXVK fork);
- `p84` / `release/p82` / `p83-v1-local-backup`: release packaging branches.
Everything else touched on 10-28 (lampruns, grass-reach, BendNormals, tess-ring, zresolve, dxvk-fork-2 defaults) was already in.

## Checks

- Offline (D3DX43, 222 template shaders): everything off = byte-identical to 5c547109; everything on together (LINEAR_SCENE,
  CAPTURED_NORMALS + FORWARD, MANY_LAMPS + SPECULAR_LAMPS6) compiles for exterior, interior and the displacement layout.
- Slot runs on this one build (RUN_TOML `bench\deep-roadmap\tools\run-toml-combo1028.toml` = the user's toml + ManyLampsOnePass +
  CapturedNormals):

| run | list | result |
|---|---|---|
| 20261028-165602-slotC | fsr31-still (og-desert, NVR DXVK fork) | PASS. FSR 3.1 passes ran (155 profile lines) beside FSR 2.2.1; ManyLamps, merge, LinearScene on; self-check ok. |
| 20261028-170042-slotA | manylamps-1-dxvk (Lucky 38, Vault 21, Hidden Valley, fork) | PASS, 29 shots. On vs off ~1/255 mean (same picture, as the port expects); capture self-check ok. |
| 20261028-170506-slotC | capnormals-3-native (og-desert, native D3D9) | PASS. Capture (986 draws / frame) + merge + LinearScene (1,385 linear draws) on native; ManyLamps off there (needs DXVK), as designed. |

## Files to copy (over the installed 5c547109 / 767d9608)

- `Data\NVSE\Plugins\`: `NewVegasReloaded.dll`, `NewVegasReloaded.pdb`, `NewVegasReloaded.dll.defaults.toml`
- `Data\Shaders\NewVegasReloaded\`:
  - `Shaders\ObjectTemplate.hlsl`, `Shaders\ParallaxTemplate.hlsl`, `Shaders\SkinTemplate.hlsl`, `Shaders\TerrainTemplate.hlsl`,
    `Shaders\Includes\CapturedNormals.hlsl`, `Shaders\Includes\ManyLightsDirect.hlsl`
  - `Effects\SnowAccumulation.fx.hlsl`, `Effects\Includes\CapturedNormals.hlsl`
  - `Compute\SnowAccumulation.frag`, `Compute\SnowAccumulation.spv`, `Compute\FSR31*.spv` (11 files)
- To try: ManyLampsOnePass (`[Main.Main.Misc]`, restart), FSR 3.1 (`[Shaders.TAA.FSR] Version = 1`). CapturedNormals is on by
  default (your toml already has it on). Clear `Shaders\NewVegasReloaded\Shaders\Cache\` if a shader change does not show.
