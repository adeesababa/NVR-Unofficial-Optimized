# Fork feature 2026-10-26: bokeh depth of field (deep/fork-bokeh)

Compute scatter bokeh (a thing D3D9 cannot do) on the NVR DXVK fork (extension 1.4). OFF by default and do nothing on
native D3D9 or stock DXVK (the old paths run unchanged).

## No DXVK patch needed (patch number 0011 left unused)
Everything the bokeh needs is already in extension 1.4: compute pipelines, storage buffers written by compute and read by a
fragment pass, ClearBuffer, push data, the sampler heap. GLSL atomics on storage buffers and shared memory need nothing from the
host side. So no `0011` patch: the fork DLL is the existing `G:\_NVR-build-scratch\nvr-dxvk-0006` (DXVK 3.0.1 + patches
0001-0006). An indirect dispatch (sized by the GPU's sprite count) would save the bokeh splat's empty groups; measured below, it
is not worth a patch yet.

## Part 1: histogram auto-exposure -> dropped in favour of the shipped one
This branch had its own histogram exposure (256-bin log2 histogram with shared + global atomics, percentile trim, AvgLuma's
smoothing). Before it was tested, the same design shipped on mine/nvr-dxvk-compute (8cf821b7: `[Shaders.Exposure.Main]
Histogram`, `HistogramLowPercent`, `HistogramHighPercent`, ~0.075 ms). Mine added nothing clearly better (same percentiles,
same smoothing), so it was removed (6fcd2338) and the shipped one merged (7a74ab00). Only the bokeh is this branch's work.

## Part 2: bokeh depth of field
Settings `[Shaders.DepthOfField.Bokeh]`: `Bokeh = false`, `BokehShape = 6` (0 disc, 5-8 polygon sides), `BokehThreshold = 0.75`,
`BokehIntensity = 1.0`, `BokehSize = 12.0` (radius in px at 1080p for a fully blurred light, scaled with the height),
`BokehRotation = 0.0` (degrees), `BokehMaxSprites = 4096` (256-16384).
Works with the autofocus DoF (technique 0: `Enabled = true` in a view); the distant-blur-only technique is unchanged (its blur is
1-2 px: no room for a shape).
- `src/hlsl/NewVegas/Compute/DepthOfFieldBokehScatter.comp`, half resolution:
  phase 0 (detect) one thread per half-res pixel: a pixel becomes a sprite when it is brighter than BokehThreshold AND stands out
  of its 5x5 neighbourhood (shared-memory tile with apron; the middle of a bright sky or wall scatters nothing), and its blur
  radius (the DoF pass's own near/far amounts x BokehSize, so the same focus and autofocus) is over one pixel. Its linear colour
  is boosted by an inverse Reinhard curve (the frame is already tonemapped: a clipped light carries up to ~20x) and appended with
  an atomic counter.
  phase 1 (splat) one 256-thread group per sprite slot (slots past the GPU's count return at once): the aperture shape's
  coverage (anti-aliased edge) over its area, added into a fixed-point RGB buffer with `atomicAdd` (16.16). Far-field sprites
  are hidden by nearer in-focus pixels (an in-focus object stands in front of the out-of-focus light); near-field sprites cover
  everything.
- `src/hlsl/NewVegas/Compute/DepthOfFieldCombineBokeh.frag`: DepthOfFieldCombine.frag line for line, then the sprite buffer
  (bilinear from half resolution) added in linear light as 1 - (1 - c) * exp(-b) (a plain add while dark, never past white).
- `src/effects/DepthOfField.{h,cpp}`: technique 0's Vulkan path (ExtRender) runs detect + splat right after the DoF (focus)
  pass, and the bokeh Combine instead of Combine. With Bokeh on, technique 0 takes that path even with `NvrDxvkPasses` off
  (`DepthOfFieldEffect::Render`). Any failure turns the bokeh off for the session (logged) and the DoF goes on as before.
  F10: "  DoF bokeh scatter (compute)" inside "Depth of field".

## Results
(see below)
