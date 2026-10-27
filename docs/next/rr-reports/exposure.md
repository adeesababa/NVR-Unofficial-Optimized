# RenderScale v2: exposure agent (AvgLuma, Exposure, Bloom, Lens, LUT)

Native frames (RenderScale 1 / RenderRect off): every change is behind `RenderRect::Active()` (C++) or a
`TESR_RenderScale < 0.9999` test / multiply by 1 (HLSL), so the frame should be bit-identical.

## Bloom (src/effects/Bloom.cpp/.h, Bloom.fx.hlsl)
- C++: did NOT use `RenderRect::ApplyViewport` for the mip chain: it would bind `FrameVertexRect`, whose half-texel offset is the
  screen's, while each level's quad (`BloomVertexBuffer[i]`) carries that level's own half texel. New `BloomEffect::LevelQuad(i)`:
  outside rect frames returns `BloomVertexBuffer[i]` (unchanged); in rect frames builds (cached by scale + level width) a per-level
  quad with uv 0..Scale and the level's half texel, and sets the viewport to `Width(w) x Height(h)` of that level. The two loops now
  call SetRenderTarget before SetStreamSource (order swap only; needed because SetRenderTarget resets the viewport).
- The Vulkan chain (`RenderBloomExt`) is untouched; it only runs when `DxvkCompute::PassesOn()`, off in rect frames.
- HLSL: in rect frames the 13-tap downsamples and the 9-tap upsamples clamp every tap to the drawn part of their source level
  (`clampToRect`, source texel = 0.5x / 2x `TESR_BloomResolution.zw`), so stale pixels from the undrawn part do not bleed in at the
  right/bottom edges. Native frames run the original `DownsampleBox13` / `UpsampleTent9` (branch) and `FirstTap` is a pass-through
  select. The rect copies use `tex2Dlod(.., 0)` (gradients inside a branch); the bloom levels have one mip, so it is the same read.
- Unsure: rect quads are D3DPOOL_DEFAULT like `BloomVertexBuffer` and are not released on device reset / resolution change (rebuilt
  when the key changes, old one released then). If Bloom has a release path on reset, the coordinator may want to release
  `RectVertexBuffer[]` there too.
- In game: right and bottom screen edges of the glow at RenderScale 0.5-0.77 (no bright band / smear from old frames), glow size
  should match RenderScale 1 (filter radii are in fractions of the full texture, the rect is Scale of it, so the glow keeps the
  same screen size).

## AvgLuma (src/effects/AvgLuma.cpp, AvgLuma.fx.hlsl)
- C++: `RenderCompute` returns false in rect frames (the compute / histogram shaders read the whole screen and do not go through
  PassesOn), so the D3D9 pass runs. Consequence: [Shaders.Exposure.Main] Histogram falls back to the plain average while RenderScale < 1.
- HLSL: the 100 grid reads and the 12 depth taps of the autofocus go through `screenToRect`; the 12 wide taps (which reach past the
  screen edge and rely on CLAMP) are `saturate`d then scaled in rect frames only. The target is 1x1 (not screen-shaped), whole.
- In game: exposure brightness and DoF autofocus at RenderScale < 1 should match RenderScale 1 for the same view.

## Lens (Lens.fx.hlsl)
- The dirt texture is now read at `rectToScreen(uv)` so it covers the whole screen instead of being cropped to its top-left part.
  The image and bloom reads stay on uv (rect). Goes through EffectRecord::Render (core's ApplyViewport). In game: dirt pattern
  should be the same size/position at both scales.

## Exposure, LUT
- No changes: per-pixel colour transforms through EffectRecord::Render; their AvgLuma / LUT reads are not screen-shaped. Exposure's
  debug squares (`#define debug 0`) still test rect uv; harmless while debug is off. Exposure has no screen-space history.

## Not touched / for others
- Compute\ GLSL ports (Bloom*.frag, Lens/LUT/Exposure.frag, AvgLuma*.comp): off in rect frames by design.
- fxc fx_2_0: Bloom, AvgLuma, Lens, Exposure, LUT all compile.
