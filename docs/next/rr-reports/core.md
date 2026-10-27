# RenderScale v2: core group report (2026-10-27)

Nothing built or run (coordinator builds once). Shaders checked with fxc (CombineDepth, Normals, and every effect that includes
Blur/BlurDepth: AmbientOcclusion, BounceLight, Flashlight, ShadowsInteriors, SnowAccumulation, SunShadows, Underwater, WetWorld: all OK).
FSRUpPrep.spv rebuilt with C:\VulkanSDK\1.3.296.0\Bin\glslangValidator.exe -V --target-env vulkan1.1 (the unchanged .comp rebuilt
byte-identical to the committed .spv first, so the command matches how it was made).

## Frame flow (v2: RenderScale < 1, LowResEffects = true, RenderPreTonemapping on)
1. BeginJitter -> UpscaleBegin: as v1 (camera port, jitter). TESR_RenderScale now = (sx, sy, 1/sx, 1/sy).
2. RenderWorldSceneGraphHook -> EndJitter (constant back to 1 as in v1) -> UpscaleWorld: UpscaleCheckDrawn (v1's viewport check,
   split out of UpscaleRun) + UpscaleEnsureResources; then NO upscale: Up.Active = false, Up.Deferred = true, targets kept.
   The hook's own depth resolves run as today (rect content).
3. RenderFirstPersonHook: RenderPassStandard/SkinnedHook now call NoteWorldDraw(true) in the FirstPerson context; while
   Up.Deferred it puts a whole-screen pass back into the render viewport (the weapon into the rect). The weapon is not jittered.
   IsWorldViewport also accepts the render viewport while Deferred.
4. ProcessImageSpaceShadersHook: TAA->UpscaleBeginEffects() (TESR_RenderScale set, RenderRect::Begin) -> RenderEffectsPreTonemapping
   (rect mode) -> FlushRenderScale(SourceTarget, GameSurface) -> TAA->UpscaleAfterEffects(GameSurface):
   - RenderRect::End; GameSurface becomes Up.Target if it is a different screen-size surface;
   - UpscaleRun(device, resolveDepth = false): rect copy -> FSRUpPrep (now with the viewmodel depth: weapon pixels motion 0, depth 0,
     reactive = [Shaders.TAA.FSR] WeaponReactive, as FSRPrep.comp at native) -> Upscaler::Choose (FSR 2 / DLSS) -> composite into
     GameSurface + world depth into the game's depth-stencil;
   - resolve DepthTexture (full size); composite again depth-only from DepthTextureViewModel at unjittered pixels; resolve
     DepthTextureViewModel (the depth-stencil ends holding the weapon's depth, as in a normal frame);
   - TESR_RenderScale = 1; ShaderManager::RenderDepthAndNormals(GameSurface) redraws TESR_DepthBuffer + TESR_NormalsBuffer (+smooth)
     at full size, so the post-tonemap chain, ReShade depth, PixelInspector, SettingShots see full-size buffers;
   - logs once "UNOFFICIAL RenderScale v2: NVR's effects ran at WxH, upscaled after them (...)". Up.Upscaled -> TAA resolve skips.
   FlushRenderScale is also called before the game's image space on the hook's early paths (lockpick, live rendered menu, world-scene
   guard), so a deferred frame is always upscaled. If none of that runs, the next UpscaleBegin drops the frame (logged).
   A second world scene while Deferred is drawn normally (constant 1 during it; BeginEffects sets the scale again).
Fallbacks to v1 (upscale right after the world): LowResEffects false, RenderPreTonemapping off, resources missing.
With RenderScale 1 or a frame not drawn small: Deferred never set, RenderRect never On, ApplyViewport returns at once -> frame as today.

## Files
- src/core/RenderRect.h: Begin(sx, sy), End() (stores LastScaleX/Y), declarations of ScreenShaped(w, h) and ApplyViewport(Device)
  (defined in ShaderManager.cpp so the header needs no new .cpp; forward-declares IDirect3DDevice9).
- src/core/ShaderManager.h/.cpp: FrameVertexRect (+ its scale), CreateFrameVertex(w, h, vb, scaleX = 1, scaleY = 1) (uv 0..Scale, same
  half-texel offset), RenderRect::ScreenShaped / ApplyViewport, new RenderDepthAndNormals(RenderTarget) = the old chain start
  (CombineDepth / merged depth+normals, Normals, RenderSmooth) moved out unchanged; includes RenderRect.h.
- src/core/EffectRecord.cpp: RenderRect::ApplyViewport(Device) right before both full-screen DrawPrimitive sites (chain and plain path).
- src/core/DxvkCompute.cpp: PassesOn() false while RenderRect::Active() (Available() unchanged).
- src/core/Sm6Pass.cpp: Sm6Pass::On() false while RenderRect::Active() (covers EffectRecord and VolumetricFog's SM6 use).
- src/effects/TAA.h / TAA.cpp: FSRSettings.LowResEffects ([Shaders.TAA.FSR] LowResEffects, used for DLSS too); UpscalePending,
  UpscaleBeginEffects, UpscaleAfterEffects (public); UpscaleCheckDrawn, UpscaleRun(Device, resolveDepth), UpscaleComposite(Device,
  fsrOutput, depthSource, colour, jittered), UpscaleFinish(ok, v2); Up.Deferred, Up.V2Logged; NoteWorldDraw(bool firstPerson).
- src/effects/TAAUpscale.cpp: the flow above; UpscaledThisFrame also true while Deferred (SkinScattering ScreenSpace stays skipped).
- src/NewVegas/Hooks/Render.cpp: NoteWorldDraw(true) in first-person passes; FlushRenderScale helper; UpscaleBeginEffects before the
  pre-tonemap chain; flush after it and on the three early image-space paths.
- src/effects/CombineDepth.cpp, Normals.cpp: ApplyViewport before their own DrawPrimitive (merged MRT pass, smoothing passes).
- Effects/CombineDepth.fx.hlsl, Normals.fx.hlsl: uv -> NDC via rectToScreen; neighbour reads clampToRect (point + CLAMP samplers:
  identical when off); smoothing's on-screen test/clamp against TESR_RenderScale.xy.
- Includes/RenderRect.hlsl: + clampToRectIfOn(uv, texel) (clamps only in rect frames: bit-identical native frames whatever the sampler).
- Includes/Blur.hlsl: includes RenderRect.hlsl; Scale/BoxBlur/Blur treat scaleFactor as a part of the DRAWN part (scaleFactor *
  TESR_RenderScale.xy). Includes/BlurDepth.hlsl: taps clampToRectIfOn. Sampling, Helpers, PBR, DitherNoise, CoverMap, Blending,
  Normals.hlsl: nothing screen-shaped to convert. SMAA.hlsl: post-tonemap only, untouched.
- Compute/FSRUpPrep.comp + .spv: binding 1 = viewmodel depth, size.w = reactive (v2) or -1 (v1: no weapon test, same output as before).
- resource/NewVegasReloaded.dll.defaults.toml: [_Shaders.TAA.FSR] LowResEffects = true.

## What other groups must know
- Every own full-screen DrawPrimitive in the pre-tonemap chain must call RenderRect::ApplyViewport(Device) right before it (after
  SetRenderTarget / BeginPass). Known own sites (not mine): AmbientLight.cpp (5), AmbientOcclusion.cpp (2), BounceLight.cpp (3),
  GodRays.cpp (4), VolumetricFog.cpp (3), VolumetricClouds.cpp, SunShadows.cpp, ShadowsInteriors.cpp, Specular.cpp, Bloom.cpp,
  CoverMap.cpp, ShadowManager.cpp. Effects that go through EffectRecord::Render need nothing.
- ApplyViewport binds FrameVertexRect for screen-shaped targets; for non-screen targets it puts FrameVertex back only if the rect quad
  was bound (an effect's own vertex buffer is left alone). Explicit viewports / scissors / StretchRect RECTs: RenderRect::Width/Height.
- Vulkan (NvrDxvkPasses) and SM6 paths are off in rect frames automatically; compute dispatches over screen textures in other files
  that do not go through PassesOn() must check RenderRect::Active() themselves.
- Includes/Blur.hlsl's Scale/BoxBlur/Blur: scaleFactor now means "of the drawn part" (no change when off).
- TESR_DepthBuffer / TESR_NormalsBuffer are full size again after the upscale; post-tonemap effects need nothing.

## Unsure / to check in game
- The game's first-person draws that do not go through RenderPassStandard/SkinnedHook (muzzle flash particles, the scope's own
  passes, anything drawn with another hook) would still use the full viewport (drawn wrong, too big). Watch the weapon: muzzle flash,
  scope, Pip-Boy arm, shell casings.
- Whether GameSurface in the image-space hook is the world's own target (assumed, either handled).
- The weapon is not jittered and gets reactive WeaponReactive: check it for ghosting when turning, and FSR's weapon edges.
- ReShadeIntegration::KeepDepthBound and the merged depth+normals pass in RenderDepthAndNormals' second run: same code as the chain start.
- Look at: DebugView 1 (the rect in the corner, now including the weapon and NVR's effects), the right/bottom screen edges (stale
  texels), AmbientLight grass flicker vs v1, the log line above, GPU timers "FSR upscale (RenderScale v2)", and LowResEffects = false
  must look exactly like v1; RenderScale 1 exactly like before.
- Minor: two blank lines in ShaderManager.cpp / Render.cpp lost their trailing tab (whitespace only).
