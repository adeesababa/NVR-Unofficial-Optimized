# NVR DXVK phase 2: replacing the game's own shaders through the extension (research notes, 2026-10-23)

DXVK-FORK-PLAN.md, "Phase 2": the hard walls left in D3D9 are in the game's lit-object shaders (ps_3_0's 32 temps, the
native driver's ~3,600-instruction refusal, no room for a character shadow map). The idea: the NVR extension registers a
SPIR-V replacement for a given D3D9 shader and DXVK binds ours wherever the game binds that shader, with the game's
constants and textures still flowing. These notes say exactly where that plugs into DXVK 3.0.1 and what a replacement
has to look like. Nothing of this is implemented; it is the map for the next session.

## Where a replacement plugs in
- `D3D9DeviceEx::CreateVertexShader/CreatePixelShader` -> `CreateShaderModule` (d3d9_device.cpp ~7884): the bytecode is
  analysed (`D3D9ShaderAnalysis`: version, samplers used, render targets written, constant usage, input signature), then
  `D3D9ShaderModuleSet::GetShaderModule` (d3d9_shader.cpp ~1203) looks the shader up by `DxvkShaderHash` (a hash of the
  bytecode) and otherwise builds a `D3D9CommonShader`, whose `DxvkShader` comes from `createCachedShader` + a
  `D3D9ShaderConverter` (dxbc-spirv's SM3 path).
- The replacement hook is one `if` in `GetShaderModule`: if the extension holds a replacement for this hash (or for NVR's
  name of the shader, which NVR knows from its own shader loader), build the `DxvkShader` from the registered SPIR-V
  (`DxvkSpirvShader`, as the extension's passes do) instead of converting the bytecode. Everything after that (binding,
  constants, state) is untouched: D3D9 sees a normal shader object with the original analysis.
- The analysis must stay the original's: DXVK uses its sampler mask and render-target mask (`D3D9ShaderMasks`) for
  hazard tracking and framebuffer decisions, and its input signature for vertex layout linking. A replacement may read
  MORE textures than the original only through the extension's own slots (resource indices 64+), never by claiming D3D9
  samplers the original did not use, or the masks would lie.

## What a replacement must declare (the contract with DXVK's D3D9 side)
From d3d9_shader.h / d3d9_fixed_function.h / d3d9_state.h and how `BindConstantBuffer`/`UpdatePushConstant` bind things:
- Constants: one uniform buffer per stage in the CBV set (set 2 for the SPIR-V-shader kind): `VSStaticConstants` (3) /
  `PSStaticConstants` (6) (or `VSDynamicConstants` (4) for SWVP). Layout `D3D9ConstantLayout`: the int registers first
  (`intCount` x int4), then the float registers (`floatCount` x float4), then (SWVP only) the bool bitmask; non-SWVP bools
  come as a spec constant. The counts come from the device (`GetVertexConstantLayout`/`GetPixelConstantLayout`), so the
  replacement's block must be written for the same counts (224 float4 for PS). The PS also gets `PSShared` (5) with the
  fog and alpha data (`D3D9SharedPS`), the VS `VSClipPlanes` (0).
- Textures: sampled images in the SRV set at binding = `computeTextureBinding(stage, index)`: PS samplers 0-15 map 1:1,
  the displacement sampler 16, VS samplers 17-20. The view type per sampler (2D/cube/volume) is what the game set; the
  original shader handles mismatches via spec constants (`forceSamplerTypeSpecConstants`), a replacement has to pick one.
- Samplers: DXVK's global sampler heap (set 0, binding 0 in the FF shaders) with the heap index in push data; for the FF
  pixel shader the indices sit at `GetPushSamplerOffset(i)` = 32 + sizeof(D3D9FfpsPushData) + 2 i. For dxbc-spirv shaders
  the offsets are assigned by `DxvkIrShader` (dxvk_shader_ir.cpp ~120-145, `mapPushData`); a replacement built as a
  `DxvkSpirvShader` can declare its own offsets and the push-data gather does the rest, as the extension's passes do.
- Shared push data: fog colour / alpha reference / fog parameters in the shared block (offset 0, `D3D9SharedPushData`),
  per-stage blocks after it (`D3D9VsPushData` / `D3D9FsPushData`), all declared with `DxvkPushDataBlock` as in the FF
  shaders. Whatever the replacement does not declare, it simply does not get.
- Spec constants: D3D9 passes fog mode, alpha test function, sampler types, point size state etc. as a spec-data buffer
  (set 7 for IR shaders, `SpecDataSet`) that DXVK lowers to constants per pipeline; a replacement wanting alpha test or fog
  behaviour identical to the original must read the same block (`DxvkShaderBinding specDataBuffer` in the create info).
- IO: vertex inputs and VS->PS interpolants go by D3D9 semantics through dxbc-spirv's `IoMap` (fixed locations per
  semantic); a replacement must use the same location numbers so DXVK's linking (`DxvkShaderLinkage`) matches. The FF
  shaders show the numbers (`findFixedFunctionLocation`).

## Proposed extension method (1.2)
`RegisterShaderReplacement(const NVR_EXT_SHADER_REPLACEMENT_DESC*)`: { shader hash (or bytecode to hash), stage, SPIR-V,
extra bindings (the extension's own slots: NVR's shadow map, light grid, ...), push block size }. The device keeps a map
hash -> replacement; `GetShaderModule` consults it; the shader cache key gets a suffix so a replaced and an original
shader never share a cache entry. NVR binds the extra resources once per frame through a new `SetStageResources(stage,
bindings)` call (they stay bound across draws, like D3D9 samplers; the per-draw constants keep flowing from the game).

## Risks and the order to tackle them
1. The constant layout per variant: NVR's lit-object family (SLS20xx) has many variants; start with the few the 6-lamp
   path uses, and verify with a replacement that is a straight port of one original (same picture) before changing any.
2. The spec-constant block: alpha test and fog must match the game's state or objects look wrong at a distance.
3. Sampler types per variant (2D vs cube for environment maps).
4. First measurable step is still DXVK-FORK-PLAN.md phase 2 step 1: with stock DXVK, does the ~3,600-instruction
   refusal (HandBlood, P82) disappear? If yes, that wall is gone for every DXVK user already; the replacement route is then
   about the 32-temp limit, the lamp count per pass and the character shadow map.
Estimate unchanged: 10-20 session-hours, mostly in matching the layout of the first variant; after that each variant is
a port of its HLSL to GLSL with the extra inputs.
