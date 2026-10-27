# RenderScale v2: surface group report (2026-10-27)

Nothing built or run. Every touched shader checked with fxc fx_2_0 (Reflections also with REFL_MATERIAL_ON / SPECOCC / ADD = 1): OK.
RenderScale 1: every change is a multiply by 1, a clampToRectIfOn (off when the scale is 1), or a test against TESR_RenderScale.xy
(= 1), so native frames should be bit-identical.

## Per effect
- **Specular** (Specular.cpp, .fx.hlsl): ApplyViewport before RenderWithBounce's DrawPrimitive. Quarter-res ring reads (16 taps up to
  3 blocks out) and the upsample's right/bottom taps clampToRectIfOn(.., lowPx). Prep/Low passes go through EffectRecord.
- **CoverMap** (CoverMap.cpp): ApplyViewport before the update pass (a no-op: the map is not screen-shaped; stays whole).
- **Reflections**: ViewPosition takes rect uv (rectToScreen); ToScreen now returns rect uv, so the trace, refinement and cube capture
  read the right texel; screen-edge tests and edge fades use rectToScreen / TESR_RenderScale.xy (same inequality kinds as before, so
  native is identical). Temporal: reprojected `last` via screenToRect, inside test against the rect. Blur taps (up to 8 half-res
  texels) and the composite's upsample taps clampToRectIfOn. Reflections.cpp: history unusable when RenderRect::ScaleChanged() or
  Active() differs from last frame (a function-local static).
- **WetWorld**: GetSSRViewPosition NDC from rectToScreen(uv); both puddle traces (original + FixedReflectionTrace) test the screen
  position and read screenToRect of it (hit, mid, refined); edge fade on the screen position. Refraction offset direction from
  rectToScreen(projectPosition(..)) (the offset native frames get). BlurWetMap taps clampToRectIfOn.
  NOT done: clampToRectIfOn on the two first-person-mask refraction reads: it pushed technique 2 (Wet(true,true,true)) past ps_3_0's
  224 constants. The refraction offset is clamped to a few texels, so at most a sliver at the right/bottom edge.
- **SnowAccumulation**: quarter-scale coverage pass's edge test against TESR_RenderScale.xy (the rest is Blur/BlurDepth + Depth.hlsl).
- **Underwater**: god-ray quarter pass clip against the rect; WaterDistortion wobble phase from the screen position and amplitude
  scaled by RenderScale (same look across the picture).
- **Flashlight**: its own BoxBlurMin/Avg treat scaleFactor as a part of the drawn part (as Includes/Blur.hlsl). Spot shadow /
  cookie maths is light-space. StretchRects are whole-to-whole.
- **FlashlightBeam**: nothing needed (toWorld/readDepth on rect uv, ign on pixel position, light-space cookie).
- **BloodFlow** (BloodFlow.cpp, .fx.hlsl): ApplyViewport after SetRenderTarget(0, image) for the box/splat/bead draws (projection
  unchanged, the viewport scales it); the StretchRect `area` uses RenderRect::Width/Height (the boxes' bounds are 0..1 of the drawn
  part); splat mask LOD uses the drawn height. SeeSurface / Relief (probe pass) test the screen position, then read screenToRect.
  Box pixel shaders use VPOS * TESR_ReciprocalResolution: already rect uv.
- **BloodFluid**: skipped in rect frames (called from BloodFlow.cpp: `On() && !RenderRect::Active()`): its tile binning and draw
  cover the whole screen and it goes through the DXVK extension without PassesOn(). The simulation pauses those frames.
- **SkinScattering**: unchanged, stays skipped while TAAEffect::UpscaledThisFrame(). To support it: its own SetViewport/scissor in
  ClearTarget and the skin-pass viewport (SkinScattering.cpp ~234-253, 317-326, 422-442) would need Width/Height in rect frames,
  the skin geometry drawn by the game into the rect viewport (NoteWorldDraw-like handling for its MRT pass), and its blur passes'
  taps clampToRectIfOn.

## Unsure / to check in game
- BloodFlow/Specular/Reflections use TESR_ReciprocalResolution as "one pixel" for AA widths and tolerances; in rect frames a drawn
  pixel is 1/Scale of that, so blood edges are slightly sharper (left: the rule says the constant keeps its meaning).
- Reflections' cube capture runs in rect frames reading the rect (correct); if it runs outside the chain it reads whole frames.
- Look at: puddle SSR and floor reflections near the right/bottom edge and when the scale changes (one frame without history);
  blood pools at the screen edge (paint area copy); Specular highlights at the right/bottom edge; underwater god rays.
- Nothing needed from other groups' files.
