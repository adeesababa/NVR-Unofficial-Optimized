# NVR's own shaders without Shader Model 3's limits on the fork (deep/fork-sm6, 2026-10-26)

Overnight agent brief: `bench\deep-roadmap\OVERNIGHT-1026-FORK-AGENTS.md` (patch number 0013).

## What it is
The D3D9 path compiles every NVR effect (`src\hlsl\NewVegas\Effects\*.fx.hlsl`) with fxc as ps_3_0: 224 float constants
(literals and preshader results count), 16 samplers, 32 temporaries, instruction slots. The Vulkan ports so far were GLSL
written by hand per pass. This branch adds a general route: a pass of an effect is compiled from the SAME .fx.hlsl to a
SPIR-V fragment shader (no SM3 limits), and NVR draws it through the NVR extension's full-screen passes in place of the
D3D9 draw, fed exactly what the D3D9 draw would have read. `#if NVR_SM6` in the .hlsl adds what only the fork can afford;
the ps_3_0 build never sees it, so native D3D9 and stock DXVK are unchanged.

Setting: `[Main.Main.Performance] NvrSm6Shaders = false` (live; needs NVR DXVK; does nothing on native D3D9 or stock DXVK).

## How the compile path works
- Compiler: glslang's HLSL front end (`tools/nvr-sm6/hlsl2spv.py`), at build time in WSL. DXC was the first idea, but the
  Windows SDK's dxc.exe has no SPIR-V back end ("SPIR-V CodeGen not available") and a DXC download was not needed: glslang is
  already the fork's build tool and reads the effects' HLSL (with a rewrite of the D3D9-only parts, below). Loading a compiler
  in the 32-bit game was not needed either: the .spv files are precompiled next to the shaders, like the existing ports.
- The rewrite (on the preprocessed source, so `#if` blocks are already resolved):
  - each global `sampler2D/3D/CUBE` -> a texture (set 1, binding i, extension slot i) and a sampler from DXVK's sampler heap
    (set 0 binding 0, a runtime array; the heap index for each distinct sampler_state sits in the push block, extension
    sampler slot k at push offset 4k); `tex2D/tex2Dlod/tex2Dgrad/tex2Dbias/tex2Dproj/tex3D*/texCUBE*` become small functions
    over (texture, slot); functions that take a sampler take (texture, slot) instead (the call sites are rewritten too);
  - every global uniform (TESR_..., NVR_...) moves into one uniform buffer (set 2, binding 0, HLSL cbuffer packing,
    `row_major` so a D3DXMATRIX is copied as stored = the logical matrix SetMatrix gives the D3D9 pass);
  - techniques are dropped; a wrapper entry point calls the pass's pixel shader with the technique's arguments (uniform
    bools etc.), uv from location 0 (the extension's full-screen triangle), POSITION/VPOS from gl_FragCoord, COLORn -> target n;
  - the heap array is patched to a runtime array in the SPIR-V (glslang emits size 1 for `SamplerState x[]`), spirv-val runs.
- Variants: one .spv per combination of the compile-time defines EffectRecord passes (`EffectRecord::BuildMacros`, shared by
  LoadEffect and the SM6 loader) that the effect's sources actually test (`#if`): VolumetricFog has 2 (PHYSICAL_SKY_ON 0/1).
- Output: `src\hlsl\NewVegas\Compute\Sm6\<Effect>.<Technique>.<Pass>.txt` (which defines matter) and per variant
  `<...>.<key>.spv` + `<...>.<key>.txt` (layout: FNV-1a hashes of the .fx.hlsl and every include, the uniform buffer's members
  and offsets with literal defaults, textures with binding and sampler slot, sampler states, vertex shader, render states,
  push size, target count). Deployed as `Data\Shaders\NewVegasReloaded\Compute\Sm6\`.
- Rebuild: `python3 tools/nvr-sm6/build.py` (WSL) after changing an effect that has an SM6 build; the list is
  `tools/nvr-sm6/passes.txt`. A forgotten rebuild is refused in game ("stale", logged once; the D3D9 pass runs), never drawn wrong.

## How NVR draws it (core\Sm6Pass.{h,cpp})
- `Sm6Pass::Prepare(effect, technique, pass)`: picks the variant for this effect's defines, checks the source hashes, reads
  the layout, makes the samplers and the pipeline (its own CreateGraphicsPipeline call: a refused SM6 pass turns off only itself).
- `Sm6Pass::Draw(device, targets, count, uv)`: constants = the effect's own parameter values (ID3DXEffect GetVectorArray /
  GetMatrixArray / GetFloatArray: after the caller's SetCT and SetVector, exactly what the D3D9 draw reads); a constant only
  the SM6 build declares comes from the registered TESR_ constant (ShaderManager::ConstantsTable), else its literal default,
  else 0 (logged once). Textures = the device's texture on the stage the effect's compiled D3D9 pass samples that sampler from
  (D3DXGetShaderConstantTable of the pass), i.e. what SetCT and the effect's own SetTexture calls bound; else the effect's
  TESR_ texture; a stage with nothing bound gets another input of the same kind (D3D9 reads an empty stage; the shaders multiply
  such reads by a 0 flag). So switching a pass over is replacing its DrawPrimitive with Sm6Pass::Draw, same setup code.
- Users: VolumetricFog::RenderDedicated (both passes: half-resolution estimate into the two fog targets, TexelCentre uv as
  HalfVS; full-resolution reconstruct/composite, ScreenQuad), and a generic hook in EffectRecord::Render for any pass whose
  vertex shader is FrameVS and that sets no render states (both the frame-chain and the legacy path). A technique whose passes
  all have SM6 builds skips the hand-ported ExtRender. A failed SM6 draw falls back to the D3D9 draw in the same frame.

## The fork side (patch 0013, "1.5-sm6")
`tools/nvr-dxvk/patches/0013-NVR-extension-1.5-sm6-64-resource-slots-per-type-96-bindings.patch`: the extension's caps were
32 resource slots per type and 32 bindings per pipeline (about 15 textures with their samplers): now 64 slots per type and 96
bindings (image slots 64..127, written-buffer views moved from 96 to 128; DXVK's own limits are 128 UBO/sampler slots and
1024 resource slots; D3D9 itself uses the slots below 64). `GetInfo().MaxSlots` says 64; NVR allows 96 bindings only then.
The NVR header (`src\core\NvrDxvkExt.h`) is unchanged (its defines size nothing NVR passes). Version number left at 1.4 as the
brief asks ("1.5-sm6" in comments; the merger renumbers). Fork DLL: `G:\_NVR-build-scratch\nvr-dxvk-sm6\` (0001-0006 + 0013,
with `dxvk.conf` `d3d9.customVendorId = 1002` for the depth path on NVIDIA).

## First proof: VolumetricFog's sky look (the X4507 from tonight)
cfe15f2b gave the fog's sky colour the sky's horizon tint and saturation (TESR_FogSkyLook); the composite pass with the
physical sky was already at c218 of 224, so even one more register failed (X4507, fog did not load) and 595a7c22 reverted it.
Here: `VolumetricFog.fx.hlsl` declares `TESR_FogSkyLook` (rgb SkyTint x SkyTintHorizon) and `TESR_SkyLook` (x saturation)
under `#if NVR_SM6` and applies `SkyLook()` to the fog's sky colour in LegacyFogTerms and ModernFogTerms (both dedicated passes
call them: the estimate, and the composite where the four estimates disagree). The ps_3_0 build gets an empty statement there:
the native fog is the reverted one.

## Results (2026-10-26, slot runs, GTX 1070 1080p, fork 0001-0006 + 0013, dxvk.conf customVendorId = 1002: RESZ depth confirmed)
- Compile proof: the same `VolumetricFog.fx.hlsl` with `NVR_SM6=1` fails fxc's ps_3_0 at the CompositeFog pass (X4507, 224
  constants exceeded, line 1271); without it fxc compiles (the native fog). glslang builds the SM6 version: the composite needs 72
  float4 (1792 bytes, no literals counted), 13 textures, 4 sampler states.
- Generality (`tools/nvr-sm6/survey.py`, every pass of every effect, nothing shipped): 323 of 346 passes compile to SPIR-V; 214
  of those are "plain" (FrameVS, no render states) and could go through the generic EffectRecord hook just by listing them in
  passes.txt. The 23 that fail: SMAA (several TEXCOORD inputs from its own vertex shader), BloodFlow (its own geometry), a few
  HLSL loop variables used after the loop.
- Off = old picture: native D3D9 og-desert (run 20261026-111729-slotC), NvrSm6Shaders on vs off: 13-16 % px >2, mean 1.6-1.9,
  the same as two identical off shots (17 %, mean 2.3); no SM6 line in the log; fog 0.38 ms either way.
- On, same picture where nothing new is asked for: fork og-desert (20261026-114027-slotC): SM6 vs D3D9 16.6 % px >2 mean 1.70 vs
  settle-vs-D3D9 16.3 % mean 1.69 (noise); fork lucky22 (20261026-111155-slotA, lamp mask + indoor fog): 10-11 % mean 1.2-1.3
  vs 10.7 % mean 1.25 between two D3D9 shots. No crash, no fallback, both pipelines created (DXVK log: 12 / 14 bindings).
- Cost (F10, the SM6 build is the plain glslang output, no spirv-opt -O): fog estimate 0.108 -> 0.172 ms, composite 0.265 ->
  0.284 ms (fog total 0.377 -> 0.462, +0.085 ms) at og-desert; lucky22 lamp mask 0.211 -> 0.241 ms. Slower than fxc's code, as
  the hand ports were (0.408 vs 0.393 with variants): the point is the limits, not speed. Next: run spirv-opt -O on the output
  (the hand ports' compile-compute.sh does) and re-time.
- NOT tested in game (testing stopped by the coordinator): the horizon tint visible through the fog on the fork (the tint/sat
  shots of fork1026-sm6-desert/night ran only before the entry-point fix, when the SM6 draws were skipped), og-night, the
  generic hook on any effect beyond PointShadows, patch 0013's raised caps beyond 18 bindings. Shot list for the combined bulk
  run: bench\foundations\lists\fork1026-sm6-bulk.txt (og-desert, og-night, lucky22).
- Bugs found and fixed on the way: D3DX's Get*Array refuse non-array parameters (NVR_ constants came out 0); DXVK creates every
  stage with entry point "main" (glslang named it nvr_main: the draws were skipped silently, timers ~0.002 ms).
