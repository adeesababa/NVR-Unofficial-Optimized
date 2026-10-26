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
