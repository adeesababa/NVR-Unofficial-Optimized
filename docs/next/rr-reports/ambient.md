# RenderScale v2: ambient group report (2026-10-27)

Nothing built or run. All three shaders checked with fxc fx_2_0: OK. Line endings kept (AmbientLight.cpp / BounceLight.cpp /
BounceLight.fx.hlsl LF; AmbientOcclusion.cpp / AmbientLight.fx.hlsl / AmbientOcclusion.fx.hlsl CRLF).
Native frames: every change is a multiply by TESR_RenderScale (1), a clamp to TESR_RenderScale.xy where it was 1 (saturate), or
clampToRectIfOn, so RenderScale 1 should give the same values as before.

## AmbientLight
C++ (src/effects/AmbientLight.cpp):
- RenderRect::ApplyViewport before every D3D9 full-screen draw: RenderPrepare (Prepare, SmoothX/Y), the main 8-pass loop, and both
  grid draws (ScreenInject, GridInject/Propagate). The grid atlas, the stats target and the 1x1 balance stay whole; for the grid passes
  the call only puts the whole-target quad back if a rect quad was bound.
- Stats pass (p 5): its 32 x 18 target could pass ScreenShaped at 16:9, so after ApplyViewport it gets statsViewport and FrameVertex
  back in rect frames; StatsSample reads the rect itself (below).
- SmoothCompute and RenderBlurCompute (they check Available(), not PassesOn()) return false in rect frames: the D3D9 passes run.
  ComputeGI / NvrDxvkPasses are all behind ExtOn() = PassesOn(): off in rect frames already.
- History: `continuous` also needs RenderRect::Active() equal to last frame's and !ScaleChanged() (function-static lastRect).
HLSL (AmbientLight.fx.hlsl):
- PositionFromLinear / PositionFromViewZ: rectToScreen first (Prepare, Smooth, Estimate, Accumulate all pass texture coordinates).
- SmoothNormal and Estimate: the on-screen test and the clamp use TESR_RenderScale.xy; Estimate's march offset (screenRadius is a
  share of the screen) x TESR_RenderScale.xy.
- Blur taps: clampToRectIfOn. Apply's 4 upsample taps: clampToRectIfOn. SkyFactorAt's clamp: upper bound TESR_RenderScale.xy.
- Accumulate: the reprojected position (screen) -> screenToRect for the history taps and the "moved" distance; the inside test stays
  on the screen position.
- ScreenInject (grid screen memory): projectPosition already gives rect uv; edge tests -> outsideRect, extent x TESR_RenderScale.xy.
- StatsSample: StatsAt(screenToRect(...)) so the 32 x 18 samples cover the drawn picture.

## AmbientOcclusion
C++: ApplyViewport before the sky pass draw (SkyVisibilityV2) and the 4-pass Dedicated loop. Vulkan path behind PassesOn().
HLSL: DedicatedUV upper clamp = TESR_RenderScale.xy (blur and combine edges); Dedicated/Packed blur offsets x TESR_RenderScale.xy;
GTAO radiusUV x TESR_RenderScale.xy (before the pixel cap, which is in texels already); GTAOPosition / GTAOPositionLod clampToRectIfOn,
GTAOPositionLod's own NDC via rectToScreen; the SSAO kernel's projected depth reads clampToRectIfOn.
Not converted: the legacy unnamed technique (EffectRecord path, only for old files / after a failure; its halfres #if would be wrong
in rect frames) and the PackedAO quadrant scheme (PackedUV), which the C++ never picks.

## BounceLight
C++: ApplyViewport before the 5-pass D3D9 loop's draw; history reset as AmbientLight (lastRect, updated only when the low passes run).
HLSL: PositionFromLinear rectToScreen; Gather / GatherPlus on-screen test and clamp to TESR_RenderScale.xy; Blur taps clampToRectIfOn;
HistoryAt reads at screenToRect(last.xy) (Temporal and MultiBounce); GatherPlus "moved" via screenToRect.
Not converted: the retired NeonSpill (pyramid / gather; unreachable, neonSpill forced 0).

## Unsure / for other groups
- AmbientOcclusion::Render falls back to the legacy path if the incoming viewport is not the whole target. Without Skylighting it does not
  call SetRenderTarget first, so if the chain hands it a rect viewport left by an earlier ApplyViewport, the AO silently goes legacy
  (wrong in rect frames). Core: please make sure the viewport is full when an effect's Render starts (or tell me and I compare against
  Width/Height in rect frames).
- Deferred combines read by other groups: AmbientOcclusion's ResultTexture (VolumetricFog CompositeAO, sky group) and BounceLight's
  deferredTexture/deferredLayout (Specular RenderWithBounce, surface group) are drawn in their rect; their upsamples should clamp to
  TESR_RenderScale.xy as DedicatedUV now does.
- Accumulate / HistoryAt bilinear taps can read one half-res texel past the rect edge (depth test rejects most): accepted bleed.
- Estimate keeps MaxScreenRadius as a share of the screen (so the reach in pixels shrinks with the scale, same look per object).

## Look at in game (RenderScale < 1, LowResEffects on)
- AmbientLight grass flicker vs v1 (the reason for the rework), DebugView 1 (occlusion) and 11 (busy depth) for a clean rect, no band
  at the right/bottom edges; turning the camera: no ghost trail from history (it resets on scale changes / native <-> rect).
- Indoors with GridBounce + InteriorBalance: the room's brightness same as at RenderScale 1 (stats now read the rect).
- AO (GTAO and SSAO) and Skylighting: no dark/light seam at the right/bottom edges; radius looks the same as at native.
- BounceLight Temporal: no smear when the scale changes.
- RenderScale 1: must match before exactly.
