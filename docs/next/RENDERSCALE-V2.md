# RenderScale v2: NVR's effects at the internal resolution, upscaled after them (2026-10-27)

User (playtest on the NVR DXVK fork, 2560x1440, GTX 1070): "there is a minimum speedup ... i can recall getting considerable gains
from fsr vs native in other titles" / "shouldn't everything in the image be rendered lower then upscaled?" / "yes, start the rework".

## Why
RenderScale v1 (TAAUpscale.cpp) draws only the game's world scene small and upscales right after it; the first-person weapon and all
~30 NVR effects before tonemapping (AmbientLight, clouds, shadows, fog, reflections...) then run at full resolution. They cost more
than the world, so the gain was small. Grass also flickered more with AmbientLight on, because AmbientLight ran after the upscale on a
depth rebuilt from the jittered low-resolution frame. In v2 everything up to the end of the pre-tonemap chain runs small and FSR 2 /
DLSS upscale after it (as other games do), so FSR also smooths the effects' noise.

## Frame, v2 (only when RenderScale < 1 and the frame can be upscaled; else exactly as today)
1. World scene: drawn into the top-left RenderWidth x RenderHeight (already so in v1: NiCamera viewport port, BeginJitter).
2. NO upscale after the world any more (v1's UpscaleWorld is skipped when `[Shaders.TAA.FSR] LowResEffects` is true, the default).
3. First-person weapon: drawn into the same top-left rect (NoteWorldDraw's viewport fix-up kept active through RenderFirstPersonHook),
   not jittered (FSR's reactive mask covers it, as FSRPrep.comp does at native resolution).
4. Depth resolves as today (the INTZ textures hold the rect).
5. ShaderManager::RenderEffectsPreTonemapping runs in RECT MODE (RenderRect::On): every D3D9 full-screen pass into a SCREEN-SHAPED
   target draws into its top-left Width(w) x Height(h) with the full-screen quad's texture coordinates scaled to 0..Scale. Vulkan
   passes (NvrDxvkPasses), SM6 passes and compute paths are not used in rect frames (the D3D9 path runs): their GLSL is untouched.
6. At the end of RenderEffectsPreTonemapping (before ProcessImageSpaceShaders), FSR 2 / DLSS upscale GameSurface's rect (HDR) to the
   whole target, the depth NVR's later effects read (CombineDepth's TESR_DepthBuffer, and whatever else they read) is upsampled to
   full size, RenderRect::End, TESR_RenderScale back to 1. The game's image space, NVR's post-tonemap effects and the HUD run at full
   resolution as today.

## The rules (every effect)
Texture coordinates ("uv", "rect" coordinates) span 0..TESR_RenderScale.xy; screen positions ("s01") span 0..1.
Shader side: `#include "Includes/RenderRect.hlsl"` (Includes/Depth.hlsl already includes it):
- `TESR_RenderScale` (xy scale, zw 1/scale), `rectToScreen(uv)`, `screenToRect(s01)`, `clampToRect(uv, texel)`, `outsideRect(uv)`.
- Includes/Depth.hlsl is CONVERTED: reconstructPosition, reconstructWorldPosition(Lod), toWorld take rect uv; projectPosition returns
  rect uv. Code that uses them correctly needs nothing.
- Any OWN uv -> NDC / camera ray maths (`uv * 2 - 1`, `(1 - uv.y) * 2 - 1`, `TESR_ReciprocalResolution`-based ray setups...): use
  rectToScreen(uv) first.
- Any OWN projection -> uv (view/world position to texture coordinate, temporal reprojection of a history, shadow-to-screen lookups):
  screenToRect(...) after it.
- Edge tests `uv < 0 || uv > 1`, `saturate(uv)` as a screen clamp, ray marches leaving the screen: outsideRect / clampToRect.
- Reads that walk away from the pixel (blurs, ray marches, neighbourhood searches) near the right/bottom edge would read stale texels
  outside the rect: clampToRect where it matters (big kernels, marches); a 1-texel bilinear bleed is acceptable.
- Pixel-position maths (VPOS, uv * resolution for dither / blue noise / checkerboards) needs nothing.
- TESR_ReciprocalResolution keeps meaning "one texel of the full-size texture": neighbour offsets need nothing.
C++ side (`#include "../core/RenderRect.h"`):
- `RenderRect::Active()`, `ScaleX/ScaleY`, `Width(w)/Height(h)` (the drawn part of a w x h texture), `ScaleChanged()`.
- Explicit rectangles (StretchRect with a RECT, SetViewport, scissors, Clear rects, compute dispatch sizes over screen textures) use
  Width/Height of the texture's size while Active. StretchRect(src, NULL, dst, NULL) between screen-shaped textures needs nothing
  (whole to whole keeps the rect in proportion).
- Targets that are NOT screen-shaped (shadow maps, sky/LUT textures, 1x1 / small square reduction targets): the core's viewport rule
  leaves them whole. A pass that READS a screen texture into such a target (a reduction, an average) must read only the rect
  (scale its uv by TESR_RenderScale.xy).
- Temporal histories: reprojection uv via screenToRect; reset the history when RenderRect::ScaleChanged() or when a frame switches
  between rect and full (Active() differs from last frame).
- An effect that only works at full resolution can be skipped in rect frames (say so in the report) rather than drawn wrong.

## Work split (one agent per group; files outside your group: do not edit, write what you need in your report)
| group | owns |
|---|---|
| core | src/core/RenderRect.h, ShaderManager.*, EffectRecord.*, DxvkCompute.*, Sm6*, src/effects/TAA.*, TAAUpscale.cpp, TAAFSR.cpp, CombineDepth.*, Normals.*, src/NewVegas/Hooks/Render.cpp, Effects/CombineDepth.fx.hlsl, Normals.fx.hlsl, Effects/Includes/* EXCEPT the group-owned ones below, Compute/FSR*.comp, resource/*.toml (the LowResEffects key) |
| shadows | ShadowsExterior.*, ShadowsInteriors.*, PointShadows.*, PointShadows2.*, SunShadows.*, SunFar.*, TerrainShadows.* + their .fx.hlsl + Includes/Shadows.hlsl, SunCascades.hlsl, InteriorShadowApply.hlsl |
| ambient | AmbientLight.*, AmbientOcclusion.*, BounceLight.*, BounceLightMath.h + their .fx.hlsl |
| sky | SkyView.*, VolumetricClouds.*, VolumetricFog.*, VolumetricLight.*, GodRays.*, Sandstorm.* + their .fx.hlsl + Includes/Atmosphere.hlsl, Sky.hlsl, SkyShare.hlsl, SkyTake.hlsl |
| surface | SnowAccumulation.*, WetWorld.*, BloodFlow.*, BloodFluid.*, BloodFlowBoxes.h, Specular.*, Reflections.*, Flashlight.*, FlashlightBeam.*, Underwater.*, SkinScattering.* + their .fx.hlsl |
| exposure | AvgLuma.*, Exposure.*, Bloom.*, Lens.*, LUT.* (+ LUTFile.h, LUTIdentity.h) + their .fx.hlsl |

Every group: all in work\NVR-playfork (one shared tree, branch claude/play-fork): edit ONLY your files, no commits, no branch switches,
no MSBuild (the coordinator builds once), no game runs. Check shaders with
`fxc /nologo /T fx_2_0 /I src\hlsl\NewVegas\Effects /Fo NUL <file>` run from src\hlsl\NewVegas\Effects
("C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe"). Write a report docs/next/rr-reports/<group>.md: per effect,
what changed, what you were unsure of, what to look at in game, anything you need from another group's files.

Comments: match the file's style ("UNOFFICIAL 2026-10-27 RenderScale v2: ..."), short, why not what.
