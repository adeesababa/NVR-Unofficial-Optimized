# FSR 2 on the NVR DXVK fork (overnight 2026-10-26, branch deep/fork-fsr)

Credit and licence: FidelityFX Super Resolution 2 is AMD's (FSR 2.2.1, GPUOpen,
https://github.com/GPUOpen-Effects/FidelityFX-FSR2), MIT licence. The licence text ships next to the shaders:
`src/hlsl/NewVegas/Compute/FSR2/LICENSE-FidelityFX-FSR2.txt`. AMD's GLSL headers in that folder are unchanged; only
`ffx_fsr2_callbacks_nvr.h` (derived from AMD's `ffx_fsr2_callbacks_glsl.h`, with the changes listed at its top) and the
small pass wrappers are NVR's. GTX 1070: no DLSS, so FSR (vendor-neutral, MIT) is the upscaler of choice.

## What was built
- Step 1, "native AA": FSR 2 replaces TAA's resolve at the screen's own resolution, on TAA's existing jitter (8-step
  Halton, applied to the world camera's frustum) and TAA's camera reprojection.
  - Settings `[Shaders.TAA.FSR]`: `Enabled` (false), `Sharpness` (0.0, FSR's RCAS), `WeaponReactive` (1.0). TAA must be on
    (`[Shaders.TAA.Status] Enabled`); TAA's `Jitter` and `MipBias` are used; `DebugView` 1 (motion) and 5 (FSR depth) show
    NVR's FSR inputs.
  - Needs NVR DXVK (extension 1.4, the existing d3d9.dll; no DXVK change was needed, so there is no patch 0008). Native
    D3D9 / stock DXVK: the setting does nothing and TAA's own resolve runs (logged once). Any failure (missing .spv, refused
    pipeline, failed dispatch) is logged once and TAA's own resolve takes over for the session.
  - Per frame (effects/TAAFSR.cpp): `FSRPrep` (NVR: camera motion vectors from depth with TAA's reprojection maths, a
    device-style depth FSR's standard formula turns back into NVR's linear depth, reactive = 1 on the first-person weapon),
    `FSRLumaMip` (NVR: the average log luma per 32x32 block, what AMD's luminance pyramid stores in the mip FSR reads),
    AMD's `Reconstruct`, `DepthClip`, `Lock`, `Accumulate` (or `AccumulateSharpen` + `RCAS`), then `FSRComposite` (FSR's
    output into the frame; the un-jittered first-person weapon is put back exactly as rendered, as TAA's WeaponTAA 0).
  - What AMD's code needed for D3D9 resources: no r32ui render target exists, so the reconstructed-depth atomics run on a
    storage buffer; r8/rg8/rgba8/rgba16-unorm resources became float formats of at least the same precision; samplers come
    from DXVK's sampler heap; exposure is 1 (the input is the tonemapped frame).
- Test aid: the shot-list `turn <deg/s>` command (copied from claude/musing-haslett 8082535c) for pan shots.

## Step 2 (real upscaling): not done tonight, and why
Rendering the 3D scene at a lower internal resolution is an engine/NVR integration job, not a DXVK one:
1. The world (and the first-person weapon, which has its own camera and depth) would have to be drawn into a sub-rectangle
   of the full-size targets: the Gamebryo camera's viewport port (NiCamera port, normalised) set to the render scale for the
   world and weapon renders, the TAA jitter scaled to render pixels, phase count 8 / scale^2, mip bias log2(scale) - 1.
2. FSR then has to run straight after the world render, on the HDR target (FSR accepts an input larger than the render
   size), BEFORE NVR's pre-tonemap effects, because all of them (AO, AmbientLight, fog, clouds, shadows, reflections...)
   read colour and depth by full-screen uv. Their depth (CombineDepth's linear R32F from the INTZ world + viewmodel depth)
   would have to be upscaled too (nearest-of-4, written into CombineDepth's texture instead of running CombineDepth), and
   the game's own later draws that use the hardware depth buffer at full size checked one by one.
3. Every NVR replacement of the game's world shaders that reads a screen-space buffer by VPOS / ReciprocalResolution
   (terrain blend depth, light grid, screen shadow buffers) would read the wrong place inside a sub-rectangle and needs a
   scale.
4. Reactive mask for particles and transparencies drawn in the world pass (they would be upscaled with the opaque scene).
DXVK cannot do this by itself safely: rescaling all screen-sized render targets in the driver would also shrink the UI,
the game's own image space and every NVR buffer. Estimate: 8-15 session-hours; main risk: the viewport port handling of the
game and the audit of every screen-space read in NVR's world shaders. On the GTX 1070, FSR 2 itself costs about 1-1.5 ms
at 1440p (AMD's figures for that class), so upscaling only pays where the world render plus NVR's pre-tonemap effects
cost clearly more than that.

## Results
(filled in below after the slot runs)
