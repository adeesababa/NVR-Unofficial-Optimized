# AMD FSR 3.1 upscaler next to FSR 2.2.1 (2026-10-28, branch claude/fsr31-upscaler-0ab1f2)

Credit and licence: FidelityFX Super Resolution 3.1 is AMD's (FSR 3.1.x upscaler from the AMD FidelityFX SDK 1.1.4, GPUOpen,
https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK), MIT licence. The licence text ships next to the shaders:
`src/hlsl/NewVegas/Compute/FSR31/LICENSE-FidelityFX-SDK.txt`. AMD's GPU headers in that folder are unchanged; only
`fsr3upscaler/ffx_fsr3upscaler_callbacks_nvr.h` (made from AMD's `ffx_fsr3upscaler_callbacks_glsl.h`, kept beside it; the changes
are listed at its top), the small pass wrappers `FSR31*.comp` and `core/UpscalerFsr31.cpp` are NVR's. **Upscaling only: no frame
generation** (not built, not prototyped).

## The setting
`[Shaders.TAA.FSR] Version` (menu: TAA / FSR / DLSS, a combo): **0 = FSR 2.2.1 (default, exactly as before)**, 1 = FSR 3.1.
Everything else is shared: `Enabled`, `RenderScale` (the quality-mode combo), `LowResEffects`, `Sharpness`, `WeaponReactive`,
`DebugView`. DLSS keeps its own tab and wins over both, as before. Live (no restart).

Why not the key `Upscaler`: until 2026-10-27 `[Shaders.TAA.FSR] Upscaler = 1` meant DLSS; old settings files still carry it (the
user's has `Upscaler = 0` and the old `DlssHelperTest`). Reusing it would have switched such a file to FSR 3.1 by surprise.

## Which way: 32-bit SPIR-V in the fork, no helper (the 32-bit question)
FSR 3.1 is open source and its GPU half is plain GLSL (AMD ships a GLSL Vulkan backend: `sdk/src/backends/vk/shaders/fsr3upscaler`).
Only AMD's prebuilt runtime DLLs are 64-bit, and NVR does not need them: exactly as for FSR 2, AMD's passes are compiled to SPIR-V
(glslang 15, Vulkan SDK 1.3.296.0) and recorded by NVR's 32-bit code into DXVK's command stream through the fork's compute
extension; `core/UpscalerFsr31.cpp` does on the CPU what AMD's `ffx_fsr3upscaler.cpp` does (constants, ping-pong resources, clears,
dispatch order). So no 64-bit helper, no shared images, no semaphores, no new DXVK patch: it runs with the d3d9.dll the user already
has (extension 1.3 or newer for the mip sampler). The DLSS-style helper would only be needed for closed 64-bit code.

What AMD's code needed for D3D9 resources (all in the callbacks header): no r32ui render target in D3D9, so the reconstructed
previous depth and SPD's atomic counter are storage buffers (atomicMin / atomicAdd), and the 1x1 frame info is a buffer (reset with
UpdateBuffer); r8 / rgba8 resources became r16f / rgba16f; samplers come from DXVK's sampler heap (plus a mipmapped linear one for
the shading-change pyramid: NVR's plain linear sampler is clamped to mip 0); exposure 1; the reactive mask is the green channel of
NVR's depth input (weapon); no transparency-and-composition mask (0, AMD's default); SPD runs its LDS path (no subgroup quad ops).
Bindings were checked against the compiled SPIR-V with spirv-dis.

Inputs are FSR 2's, unchanged: the same depth, camera-only motion vectors, reactive mask for the weapon, RenderScale v2
(LowResEffects) and its HDR input, native AA after tonemapping. Jitter: FSR 3.1's own sequence is the same function as FSR 2's
(Halton 2/3, `8 * (display / render)^2` phases: `ffxFsr3UpscalerGetJitterOffset` / `GetJitterPhaseCount` are identical), so NVR's
jitter is used as is; the phase count steps by one per frame after a change, as AMD does. Sharpening: 3.1's RCAS (same as FSR 2's).
Failure handling: a missing `FSR31*.spv` or a failed pass is logged once and FSR 2 takes over the same frame and the rest of the
session (it does NOT turn RenderScale off). Switching between FSR 2 and 3.1 restarts the history and frees the other's textures.

Per frame, in AMD's order: prepare inputs, luma pyramid (SPD), shading-change pyramid (SPD), shading change, prepare reactivity,
luma instability, accumulate (or accumulate + RCAS when Sharpness > 0). F10 timers `  FSR3.1 ...` inside "FSR upscale (RenderScale)"
or "TAA" (native AA).

## Results
(filled in below after the slot runs)

## Files to copy (install)
(below)
