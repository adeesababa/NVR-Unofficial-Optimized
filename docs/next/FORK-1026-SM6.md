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
