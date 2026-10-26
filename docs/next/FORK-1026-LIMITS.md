# NVR DXVK fork: shaders past ps_3_0's limits (2026-10-26, branch deep/fork-limits)

## The idea in one paragraph
fxc refuses a ps_3_0 shader past 32 temporary registers (X4505), so NVR's own HLSL for the land and the lit objects carries
cuts that only exist to fit (marked `NVR_NO_SM3_LIMITS` now). The same HLSL compiles WITHOUT those limits as ps_4_0 in fxc's
backwards-compatibility mode (`/Gec`: `sampler2D`, `tex2D` and `register(cN)` still work, registers land where D3D9 has them).
`tools/nvr-dxvk/replace/sm4asm2glsl.py` turns that compiled shader, instruction for instruction, into GLSL under the fork's
replacement contract (extension 1.2: the game's textures, sampler states and float constants keep arriving), and
`nolimits.py` builds one SPIR-V replacement per shader variant the game actually loaded. The fork then runs the uncut shader
wherever the game binds the cut ps_3_0 one. Native D3D9 and stock DXVK keep the ps_3_0 shaders exactly as before.

## What the no-limits build changes (NVR_NO_SM3_LIMITS)
- Sun shadows, PCF (Mode 3, the default): in each cascade's handover band both cascades are looked up and blended by the band
  fade, instead of a per-point dither between them (Shadow.hlsl; the dither was there because two inlined lookups put 30 of the
  55 outdoor object shaders over 32 temps). Every outdoor lit object and the land.
- Land (TerrainTemplate.hlsl, Parallax.hlsl): IsoHeightTaps also on 6/7-layer land with Terrain Blending (and on 6 layers with
  SkylightingMode 1); HeightEdge 2 keeps its contrast on 7-layer land with SkylightingMode 1.

## How to use it (test workflow today)
1. Fork d3d9.dll (extension 1.2 or later) + `[Main.Main.Performance] NvrDxvkReplacements = true`, and an empty folder
   `Data\Shaders\NewVegasReloaded\Compute\Replace\dump`. Start the game once: every pixel shader is written there as
   `<Name>@<hash>.pso` plus `<Name>@<hash>.defines` (its source file and defines; new in this branch).
2. `py -3 tools\nvr-dxvk\replace\nolimits.py dump <that dump folder> src\hlsl\NewVegas\Shaders <out>` (fxc from the Windows
   SDK, glslangValidator + spirv-val in WSL as compile.sh). Copy `<out>\*.spv` + `*.txt` into `Compute\Replace\`.
3. Next start: the log says `<Name> will run as ...spv` per shader. `--plain` builds the same variants WITH the cuts (a check that
   the route itself draws the same picture).
The replacements are keyed by the bytecode hash, i.e. by the settings that compiled the ps_3_0 shader: change a restart setting
that recompiles shaders and you need step 1-2 again (an unmatched replacement is simply not used).

## Results
(appended below)

### Result 2026-10-26 (slot C 1920x1080, fork d3d9 = nvr-dxvk-0006 + dxvk.conf `d3d9.customVendorId = 1002` (depth via RESZ, checked in the log), NVR DLL 3d6f188e)
Test settings: Terrain Blending on, SkylightingMode 1 (so the land shaders carry the ps_3_0 cuts), shadows at the default (PCF, Mode 3).
Lists `bench\foundations\lists\fork1026-limits-1.txt` (og-desert + og-goodsprings in one launch). Runs (bench\autotest, pictures in
H:\_NVR-autotest-shots\<run>): R1 20261026-094204-slotC = the ps_3_0 path (+ the dump); R2 20261026-103844-slotC = the cut variants
through the SM4 route (first translator version); R3 20261026-112728-slotC = the no-limits replacements (final translator).
- All 134 lit-object (ObjectTemplate) and land (TerrainTemplate) pixel shaders the game loaded were built and registered (log: 134 x
  "will run as ...spv", nothing refused), no crash, no DXVK errors, pictures normal in both scenes.
- MEASURED GAIN (World scene (game) GPU ms, F10 timers, Goodsprings main street, same launch, IsoHeightTaps toggled live):
  | | IsoHeightTaps on | IsoHeightTaps off |
  |---|---|---|
  | ps_3_0 path (R1) | 9.07 / 9.07 / 9.00 / 9.10 | 9.01 / 9.07 |
  | no-limits replacements (R3) | 5.55 / 6.39 / 5.77 / 5.85 / 6.48 | 9.04 / 9.11 |
  With Terrain Blending on, the 6/7-layer land shaders could not have IsoHeightTaps under ps_3_0 (32-temp cut), so the setting did
  nothing in Goodsprings; the no-limits shaders have it and the world scene drops by ~3 ms (9.05 -> ~6.0 ms, -33 %). With it off the
  replacements cost the same as the ps_3_0 shaders (9.04-9.11 vs 9.01-9.07): the route itself is free. Desert spot (fewer land
  layers): 2.2-2.9 ms either way, no change.
- Picture: desert R3 vs R1 19-22 % of pixels > 2/255, mean 1.2/255, signed ~0 (in-launch noise 15-17 %; the rest is grass and plants
  moving between launches); the parallax ground in Goodsprings looks the same with and without the taps. Goodsprings cross-launch
  pairs are dominated by a moving tumbleweed and grass (30-35 %), so they say nothing finer.
- Not shown: the cascade-band blend (no shadow edge crossing the Near/Middle band in these two views at noon) and HeightEdge 2's
  contrast on 7-layer land (he1 vs a shots inside noise). R2's first translator version used `nonuniformEXT` on the heap index and
  fxc /O1: 0.3-0.6 ms slower than the ps_3_0 path; the final version (uniform heap index as DXVK's own shaders, fxc /O3) is not.
  R2's desert pictures also differed more (54 %) than R3's (20 %): R2 was not re-run with the final translator (testing stopped).

### What is missing / next
- In-game test stopped by the coordinator before: a cross-launch baseline repeat, the plain route with the final translator, the
  cascade band in a view that shows it, lucky22 (interior objects). Bulk list: `bench\foundations\lists\fork1026-limits-bulk.txt`.
- The replacements are per bytecode hash (per settings): today a dump run + nolimits.py per build/settings. Next step: do it in the
  DLL (NVR compiles the no-limits HLSL with d3dcompiler_47 at load and the fork converts it: DXVK already holds a DXBC->SPIR-V
  front end), so it just works with any settings; est. 6-10 session-hours.
- No DXVK change was needed (extension 1.2's replacement path carries it): fork patch 0012 is not used.
- Settings: `[Main.Main.Performance] NvrDxvkReplacements` (existing, off by default, read at load); everything else is the existing
  IsoHeightTaps / HeightEdge / Mode settings, which now also work where ps_3_0 cut them.

### Combined build check 2026-10-26 14:24-14:41 (deep/fork-combo-1026 5f96ca01, fork d3d9 nvr-dxvk-combo, list fork1026-limits-bulk)
A = ps_3_0 path + dump (bench\autotest\20261026-142451-slotC), B = 124 no-limits replacements (20261026-143705-slotC). 134 object/land
variants dumped; 10 (SLS2037-2046) read TEXCOORD8, which DXVK places at a per-shader location: they stay ps_3_0 (tool fixed, 6edf9518).
B log: 124 x "will run as", no refusals, no crash. World scene GPU ms, Goodsprings (shots before the idle camera took over):
| | IsoHeightTaps on | IsoHeightTaps off |
|---|---|---|
| A (ps_3_0) | 9.15 / 9.18 | 8.94 / 8.98 |
| B (no-limits) | 6.12 / 6.07 (HeightEdge 1: 6.11) | 9.24 / 9.62 |
=> -3.1 ms (-34 %) with IsoHeightTaps on; with it off the route costs about the same (+0.1-0.6). Desert: 2.46-2.50 in B, A drifting
2.35-3.8 (camera moved). Pictures A vs B Goodsprings, shots 1-2: 36-38 % of pixels > 2/255, all of it clouds, tumbleweed, grass and
dust; the land, house and rocks show no difference. Later shots (and A's desert list) were taken after the game's idle third-person
camera started moving (600-frame shots make the list long): not comparable.
