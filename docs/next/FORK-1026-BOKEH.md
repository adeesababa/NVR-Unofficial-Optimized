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
  of its surroundings (16 taps on two rings, 6 and 12 half-res px at 1080p: a lamp smaller than that scatters over its whole
  face; the middle of a bright sky or wall scatters nothing; the first version used a 5x5 tile and drew rings), and its blur
  radius (the DoF pass's own near/far amounts x BokehSize, so the same focus and autofocus) is over one pixel. Its linear colour
  is boosted by an inverse Reinhard curve (the frame is already tonemapped: a clipped light carries up to ~20x) and appended with
  an atomic counter.
  phase 1 (splat) one 256-thread group per sprite slot (slots past the GPU's count return at once): the aperture shape's
  coverage (anti-aliased edge) over its area, added into a fixed-point RGB buffer with `atomicAdd` (16.16). Far-field sprites
  are hidden by nearer in-focus pixels (an in-focus object stands in front of the out-of-focus light); near-field sprites cover
  everything.
- `src/hlsl/NewVegas/Compute/DepthOfFieldBokehResolve.frag`: the fixed-point buffer into a half-res RGBA16F texture (one texel
  per pixel), so the Combine takes one bilinear tap instead of four raw buffer reads per full-res pixel.
- `src/hlsl/NewVegas/Compute/DepthOfFieldCombineBokeh.frag`: DepthOfFieldCombine.frag line for line, then the resolved sprites
  (bilinear from half resolution) added in linear light as 1 - (1 - c) * exp(-b) (a plain add while dark, never past white).
- `src/effects/DepthOfField.{h,cpp}`: technique 0's Vulkan path (ExtRender) runs detect + splat right after the DoF (focus)
  pass, and the bokeh Combine instead of Combine. With Bokeh on, technique 0 takes that path even with `NvrDxvkPasses` off
  (`DepthOfFieldEffect::Render`). Any failure turns the bokeh off for the session (logged) and the DoF goes on as before.
  F10: "  DoF bokeh scatter (compute)" inside "Depth of field".

## Results (2026-10-26, slot runs, fork d3d9.dll = nvr-dxvk-0006 + dxvk.conf `d3d9.customVendorId = 1002`)
Fork folder: `G:\_NVR-build-scratch\nvr-dxvk-bokeh` (d3d9.dll identical to nvr-dxvk-0006, sha256 9405693f...; the dxvk.conf is
the depth fix). Every run's log shows `Depth resolve: RESZ (DXVK, any GPU brand)`, so the DoF had real depth.

What works
- Runs in game without crash or fallback, with and without `NvrDxvkPasses`: log `bokeh ready (... half resolution, up to 4096
  sprites)` / `bokeh running`, no `failed` line.
- Real bokeh shapes: og-night (slot C 1080p, run 20261026-105925-slotC, list fork1026-bokeh-1) the out-of-focus street lamps
  by the Strip become hexagons; lucky22 (slot A 960x540, runs 20261026-095727-slotA and 20261026-105718-slotA) a small white
  globe becomes a clean hexagon, the red globes get discs reaching past their edge. Stars (sky) and the in-focus foreground are
  untouched. Defaults are subtle (lucky22 mean +0.03..0.06/255 over the frame); the "big" settings (BokehSize 28, Intensity 2,
  Threshold 0.6) are clearly visible (+0.3/255 frame mean, up to 190/255 on the lights).
- Off = the old picture: Bokeh off on this branch vs the previous fork tree (NVR-dxvkfork-ci, run 20261026-112325-slotA), lucky22:
  defaults, AvgLumaCompute and the Vulkan DoF each 8.2-8.6 % of pixels > 2/255, signed -0.009..+0.009, i.e. below the
  same-build noise of two identical shots (9.9-11.2 %). With Bokeh off no code path changes (Render falls through,
  ExtRender's bokeh branch is skipped).
- Timing (F10, lucky22 slot A 960x540, solo launch, 300-frame shots): Depth of field D3D9 0.49 ms, Vulkan 0.36 ms, Vulkan +
  bokeh 0.47 ms, of which "DoF bokeh scatter (compute)" 0.028 ms (detect + splat). og-night 1080p: scatter 0.06-0.10 ms; the DoF
  totals in that launch were too noisy (the D3D9 pair alone swung 1.72-1.96 ms) to give the Combine's share. The ~0.08 ms
  between scatter and total at 960x540 was the Combine's four raw buffer reads per pixel; commit 44cb2475 resolves the buffer into
  a half-res texture first (one tap). That version is BUILT BUT NOT RUN in game (in-game testing was stopped by the coordinator).

Still missing / next
- Untested in game: the resolve-texture version (44cb2475) and the two-ring highlight test (7ab665fa was in the 105718/105925
  runs; 44cb2475 after them). Bulk list for the combined build: `bench\foundations\lists\fork1026-bokeh-bulk.txt` (off/on pairs
  with and without NvrDxvkPasses, disc, big; F10 ABAB).
- Bokeh only with the autofocus DoF (technique 0). The default gameplay DoF (distant blur only) blurs 1-2 px: nothing to shape.
- The gather passes' own fake "bokeh" (max-dilation above DiameterRange) still runs underneath; a later step could turn it off
  when Bokeh is on.
- No indirect dispatch: the splat launches BokehMaxSprites groups and the empty ones return at once (part of the 0.03-0.1 ms).
  A DispatchIndirect in the extension would trim that; not worth a fork patch yet (so no patch 0011).
- Not checked: device reset / resolution change with Bokeh on. The buffers and the half-res texture are remade when the size
  changes; the texture is a D3DPOOL_DEFAULT render target made with CreateTexture, exactly like every NVR effect texture made by
  TextureManager::InitTexture, so it behaves as those do on a Reset.

## Bulk run of the combined build (deep/fork-combo-1026 5f96ca01, list fork1026-bokeh-bulk; first in-game run of 44cb2475)
Runs: lucky22 20261026-131417-slotB (960x540), og-night 20261026-141629-slotC (1080p); fork d3d9 with RESZ for every vendor.
- Log: `Depth resolve: RESZ`, `bokeh ready` / `bokeh running`, no failure line. PASS.
- Shapes: og-night `on-big` shows clean hexagons on the out-of-focus lamps (left of the monorail, by the Strip); stars stay points. PASS.
- Off/on pairs: lucky22 off-vs-on 9.2-10.4 % of pixels > 2/255 (noise pairs 9.7-10.1 %), signed +0.01..+0.05, max 195-198 on the
  lights; og-night inside its larger noise band (23-33 %), big +0.38. Only the lights change. PASS.
- F10 (Depth of field, ms): lucky22 Vulkan 0.337 -> + bokeh 0.382 (+0.045; scatter 0.039); with NvrDxvkPasses off the bokeh path
  is 0.383 vs the D3D9 DoF 0.459. og-night 1080p Vulkan 1.25 -> + bokeh 1.36 (+0.11; scatter 0.094), passes off 1.37 vs D3D9 1.70.
  The half-res resolve worked: the Combine's share went from ~0.08 ms to ~0.01 ms. PASS (small cost).
- Not from the bokeh: in lucky22 the Vulkan side (NvrDxvkPasses on) is -0.69/255 darker than D3D9 with Bokeh OFF too (off-1 vs
  offvk-1 -0.697, 18.5 %), so it comes from another effect's Vulkan path in the combined build.
