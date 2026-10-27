# RenderScale v2: sky group report (2026-10-27)

Nothing built or run. Shaders checked with fxc /T fx_2_0: GodRays, VolumetricFog, VolumetricLight, VolumetricClouds (all OK; the
fog's X3206 at line 428 was there before). Line endings kept per file (CRLF: GodRays.cpp/.fx, VolumetricFog.cpp, Sandstorm.cpp).
Native frames: every change is an exact no-op off (x * 1, the old `> 1` tests become `> TESR_RenderScale.xy` = 1, clampToRectIfOn,
`RenderRect::Active()` false, history-reset tests false == false).

## Per effect
### GodRays (GodRays.cpp, GodRays.fx.hlsl)
- Dedicated path: ApplyViewport before every D3D9 pass draw (the half / quarter ray targets are screen-shaped, so the masks and blurs
  draw their rect part; the combine its rect). The viewport-size check accepts a rect viewport left by an earlier pass in rect frames,
  and `original` is reset to the whole target.
- DedicatedRadialBlur: the march runs in screen positions (uv and the sun's projectPosition through rectToScreen, ray lengths and the
  0..1 edge test as at native), each tap read at screenToRect(samplePos); RayAttenuation gets the rect uv. reconstructPositionFromDepth
  (own uv -> NDC): rectToScreen first.
- Packed and legacy paths (only used after the dedicated path failed) are skipped in rect frames (no rays rather than wrong ones).
- SkipEmpty / SkipIdle reductions read their whole source (not cut): stale texels past the rect can only prevent a skip, never cause one.
- Unsure: nothing big. Look at: rays toward a sun near the right/bottom edge, SkipIdle still skipping (GPU timer "GR"), rays length
  the same as at native.

### VolumetricFog (VolumetricFog.cpp, VolumetricFog.fx.hlsl)
- RenderShadowAO (ApplyShadowAO) and both dedicated passes (half MRT + full composite): ApplyViewport before the D3D9 draw. Same
  viewport-check relaxation as GodRays. Packed path: rect frames take the plain EffectRecord pass instead (its layout assumes the
  whole screen). Vulkan / SM6 are off in rect frames (core).
- FogApply's upsample clamp and CompositeAO's tap clamp: the drawn part's edge (TESR_RenderScale.xy - half texel; same numbers off).
- The froxel grid is not used in rect frames (see VolumetricLight), so the fog gets gridTexture = null there.
- NOTE for the coordinator: the SM6 fog builds (Compute\Sm6\VolumetricFog.*.txt) hash VolumetricFog.fx.hlsl (and Depth.hlsl, which
  core already changed): they are stale now and fall back to D3D9 with a "stale" log line until tools\nvr-sm6\build.py is rerun.
- Look at: fog at the right/bottom edge, the AO/shadow composite, interiors (CompositeFogInterior).

### VolumetricLight (VolumetricLight.h/.cpp, VolumetricLight.fx.hlsl)
- Its passes go through EffectRecord (ApplyViewport there): half-size buffers draw their rect part.
- Froxels: CompositeTechnique() is 0 (the march) in rect frames: the grid atlas (gw*8 x gh*8) has the screen's aspect, so the rect
  viewport would cut its 8 x 8 slice tiles. ShaderManager's lightGrid follows CompositeTechnique, so nothing else changes.
- Temporal: reprojected position through screenToRect, edge test against the rect; history dropped on ScaleChanged / rect<->native
  (lastRect member). Blur taps clampToRectIfOn.
- Look at: users with Froxels = 1 get the march look in rect frames (slightly different light shafts, a bit more cost).

### VolumetricClouds (VolumetricClouds.h/.cpp, VolumetricClouds.fx.hlsl)
- March / temporal / composite go through RenderEffectToRT / EffectRecord (ApplyViewport there); the quarter / half / full targets and
  the 1-in-4 block target are screen-shaped. Shadow map (square) and probe (1x1) stay whole.
- Temporal: previous = screenToRect(projection), outsideRect test; history restarts on ScaleChanged or rect<->native.
  CatmullRom's outer taps clampToRectIfOn.
- Water reflection (drawn during the world scene, outside the rect frame): reads the history at saturate(at) * TESR_CloudsHistoryScale.xy,
  a new constant (Constants.HistoryScale, set after each RenderClouds to that frame's RenderRect scale, 1 natively).
- Resolution 3-5 (targets larger than the screen: not screen-shaped, so drawn whole): the clouds are NOT drawn in rect frames (logged
  once). With HideGameClouds on that means no clouds at all in that combination. Rare (supersampled clouds + FSR), said in the log.
- Unsure: the block target's rect width round(blockW * s) can be one block short of ceil(round(lowW * s) / 2): the last quarter-res
  column at the rect's right edge may keep a stale march value (one quarter-res pixel). Look at the right/bottom edge of clouds.
- Look at: cloud ghosting when turning, the clouds in the water reflection (should match the sky), switching RenderScale in game
  (history restarts cleanly, no smear).

### Sandstorm (Sandstorm.cpp)
- UseFastPath() false in rect frames: the reference pass (technique 0, EffectRecord) draws instead; its maths use toWorld / readDepth
  with rect uv and pixel-position jitter: nothing to change. The fast path's dust grid has the same atlas problem as the VL grid.
- Look at: the storm in rect frames costs the reference path's time (slower than FastPath).

### SkyView, Atmosphere / Sky / SkyShare / SkyTake includes
- Nothing to change: the tables are 128x64, 256x64, 32x32 (not screen-shaped, drawn whole); the includes have no screen maths.

## Needs from other groups
- None in code. Coordinator: rerun the SM6 build (above) if NvrSm6Shaders is used; deploy the four changed .fx.hlsl files.
