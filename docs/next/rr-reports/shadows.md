# RenderScale v2: shadows group report (2026-10-27)

Nothing built or run. fxc /T fx_2_0 OK: SunShadows, ShadowsInteriors, PointShadows, PointShadows2, ShadowsExteriors, SunFar, SunDepthFit.
Native frames: every change is ApplyViewport (no-op when off), screenToRect (x1) or clampToRectIfOn (returns uv when off): bit-identical.

## Per effect
- **SunShadows** (cpp): ApplyViewport before the ping-pong DrawPrimitive. (fx): the three contact-shadow marches (ScreenSpaceShadow,
  ScreenSpaceShadowFast, SharpContactShadow) read depth through clampToRectIfOn (marches toward the sun leave the drawn part).
  The blurs go through Includes/BlurDepth (core already clamps). Vulkan path off in rect frames (PassesOn).
- **ShadowsInteriors** (cpp): ApplyViewport before the dedicated path's 3 draws. (fx): InteriorShadowBlur's taps and depth reads clampToRectIfOn.
  InteriorShadowApply.hlsl: toWorld(uv) only, nothing to do.
- **PointShadows** (fx): LampContactPS's ContactOpen: ContactProject returns a SCREEN position (own projection), so the depth read is
  readDepthLod(screenToRect(s.xy)); its 0..1 edge test stays on the screen position (= the rect edge). Half mask / upsample need nothing
  (half target is screen-shaped, uv proportional). Draws go through EffectRecord.
- **PointShadows2, ShadowsExteriors, SunFar**: only toWorld/readDepth on rect uv; draws via EffectRecord: no change.
  SunFar terrain-shadow / cloud uvs are world/light space: untouched.
- **SunDepthFit.h** (SDSM): the block grid (screen/8) counts as screen-shaped, so ApplyViewport before the Reduce and Count draws
  restricts both to the grid's drawn part; set->Blocks = Width(GridW) * Height(GridH) (the 1-in-8192 threshold); ApplyViewport again
  after restoring the caller's target. SunDepthFit.fx.hlsl (not on my list, but only SunDepthFit.h uses it): ReducePS's depth reads
  clampToRectIfOn (the last block column/row would reach up to 7 pixels past the rect). Debug view is post-tonemap: unchanged.
- **ShadowManager / ShadowsExterior / TerrainShadows / PointShadowForward**: every viewport, scissor, clear and StretchRect is on shadow
  atlases, cubes, the character map or whole-target clears: no change. No screen-space histories (the old atlas is light space).

## Needed from other groups (not edited)
- core, ShaderManager.cpp ~1669 (LampMaskScissor): maskRect is the lamps' full-resolution screen rectangle; in rect frames it must be
  scaled by ScaleX/ScaleY (left/top floored, right/bottom ceiled, clipped to Width/Height of the mask surface). Unscaled it sits at
  the full-resolution position while the lamps are drawn shrunk toward the top-left, so lamp light gets cut off (black mask). Please scale it.
- LampScissor.h (game lamp passes, world draw) has the same full-resolution rectangle question during the small world scene (v1 too).

## Unsure / to look at in game (RenderScale < 1)
- Sun contact shadows at the right/bottom screen edge (clamped march: a smear of edge depth instead of garbage).
- Interior shadows: blurred mask at the right/bottom edge.
- Lamp contact shadows (LampContact > 0) line up with the props.
- FitToDepth on with FitToDepthLog: same splits at RenderScale 1 vs 0.67 looking at the same view; DebugView 2 blocks cover the picture.
- SunFar WaterShadows reads NVR_WaterDepth (game's water depth map) at rect uv: correct only if that map is drawn in the small world
  viewport like the scene; check the shore line at RenderScale < 1.
