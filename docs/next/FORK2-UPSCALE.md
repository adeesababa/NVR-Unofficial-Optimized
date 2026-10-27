# Real upscaling with FSR 2 ("RenderScale") + a DLSS scaffold (fork round 2, 2026-10-27, branch deep/fork2-upscale)

Base: mine/deep/fork-combo-1026 (FSR 2 at native resolution from the first fork round, docs/next/FORK-1026-FSR.md). No DXVK patch
was needed (the fork's extension 1.4+ and RESZ depth resolve are enough), so there is no patch 0023. Built clean; NOT run in game
(the coordinator's bulk test: `bench\foundations\lists\fork2-upscale-bulk.txt`). DLSS: plan only, `docs/next/FORK2-DLSS-PLAN.md`.

## What it does (plain words)
With `[Shaders.TAA.FSR] RenderScale` below 1 (and `Enabled` + TAA on, NVR DXVK), the game draws its 3D world at a lower internal
resolution, e.g. 0.67 = two thirds of the width and height (45 % of the pixels), and FSR 2 immediately turns that into a full-
resolution, anti-aliased picture. The first-person weapon, the HUD and menus, the game's own image space and every NVR effect
(AO, AmbientLight, fog, clouds, reflections...) then run at full resolution, unchanged. RenderScale 1 (the default) = off: FSR at
native resolution after tonemapping, exactly the 2026-10-26 path (or TAA's own resolve with `Enabled` off).

What it saves: only the game's own world drawing ("World scene (game)" in F10). What it costs: FSR's passes (~2.5 ms at native
1080p on the GTX 1070; at RenderScale the render-size passes shrink, the output pass does not) + a copy and a composite. So it is
**a win only where the world itself is expensive** (1440p+, big cities, many lamps; and it replaces the game's MSAA, which must be
off, and which costs a lot at 1440p). In the light 1080p desert scene (world ~2.3 ms) it will likely be slower than no AA, and
faster than FSR at native resolution. The bulk test measures exactly that.

## How it works (the cheapest route found)
The route: **draw the world into a sub-rectangle of the existing full-size targets** (top-left RenderWidth x RenderHeight),
upscale that sub-rectangle into the same targets right after the world, before anything else reads it. Resizing the game's
scene targets instead was rejected: the game's image space, NVR's ~40 effects and their buffers all assume the screen size.

1. **Smaller world viewport** (TAAEffect::BeginJitter -> UpscaleBegin, effects/TAAUpscale.cpp): the world camera's
   `NiCamera::ViewPort` (normalised l, r, t, b) is set to the render size for the length of the world scene only.
   `NiDX9Renderer::Do_SetCameraData` (0xE6C780, decompiled) turns the port into the D3D9 viewport on every SetCameraData of the
   scene (X = W*l, Y = H*(1-t), size = W*(r-l) x H*(t-b), rounded), so every world draw lands in the corner. The frustum (and so
   the projection) is unchanged: every clip-space lookup (shadow maps, water reflections and its depth map, the light grid)
   reads exactly what it read before. Jitter: FSR's own (Halton 2/3, `8 * (display/render)^2` steps, in render pixels).
   Texture mip bias: TAA's MipBias + log2(scale) (AMD: textures as sharp as the screen, not the render size).
2. **Upscale right after the world** (TAAEffect::UpscaleWorld, called in RenderWorldSceneGraphHook after EndJitter, i.e. before
   the game's refraction pass, the first-person weapon and the hook's own depth resolve):
   - the world depth resolved into TESR_DepthBufferWorld (INTZ, RESZ), the low-resolution picture copied out (StretchRect);
   - `FSRUpPrep.comp` (NVR): camera motion vectors (TAA's reprojection, as FSRPrep) and FSR's depth, from the INTZ depth;
   - FSR 2 through the new upscaler interface (core/Upscaler.cpp): render size < display size, the render-size resources used
     in their top-left (AMD's "max render size"); the **HDR builds** of Reconstruct/Accumulate (FFX_FSR2_OPTION_HDR_COLOR_INPUT,
     `FSR2ReconstructHdr.spv`, `FSR2Accumulate[Sharpen]Hdr.spv`) because the world target is the HDR scene before tonemapping;
   - a D3D9 composite (ps_3_0, pre-transformed quad, all states in a state block): FSR's output into the world's colour target
     (alpha from the nearest low-resolution pixel: some NVR shaders keep a material mark there), and the low-resolution depth,
     nearest-upsampled, into the world's depth buffer. The hook's own depth resolve that follows then gives CombineDepth, and so
     every NVR effect, a full-resolution depth. The game's refraction pass and the weapon depth-test against it too.
   - TAA's resolve (post-tonemap) skips that frame.
3. **What runs before the upscale and had to cope with the sub-rectangle** (audit, 2026-10-27):
   - NVR's world shaders reading a screen-space texture by VPOS (terrain blending's depth copy, LampSums' / HighlightSums' run
     copy) read copies made **inside** the same world scene: same pixels, nothing to change. VPOS dither patterns: fine.
   - Viewport checks of world draws: LampScissor (lamp-only passes drawn in their lamps' screen rectangle), forward lamp shadows'
     "another camera" test and the B42 scope fix compared the viewport with the screen size; they now accept the render size
     (`TAAEffect::IsWorldViewport`). Without that the forward lamp shadows would have switched off in upscaled frames.
   - ExactPassSums' fold-in switches render targets mid-scene (SetRenderTarget resets the viewport to the whole target): it now
     keeps the scene's viewport (core/PassSums.h; with RenderScale off the viewport is the whole target, as before).
   - Safety net for anything else that resets the viewport mid-scene: every world pass (RenderPassStandard/SkinnedHook,
     `TAAEffect::NoteWorldDraw`) that finds the whole-screen viewport while the world is drawn small is put back into the render
     viewport, and the passes are counted: the log says once how many used which (`RenderScale: of this world scene's passes ...`).
   - The game's **water refraction copy** is made mid-scene (BSShaderAccumulator 0xB65C60 copies the world target into a texture
     before the water passes), so its picture fills only the corner: NVR's 7 water shaders scale that one lookup by the new
     constant `TESR_RenderScale` (1 when RenderScale is off: same picture). The water's depth map is drawn before the world
     scene (DrawWorld_UpdateWater) at full size and is NOT scaled -- **to confirm at og-lake**.
   - Skin scattering's extra targets ([Shaders.Skin.NewSkin], off by default) are drawn with the world: skipped in upscaled
     frames.
   - Effects after the upscale (all of NVR's): full resolution, nothing to change.
4. **Not upscaled** (that frame at native resolution, each reason logged once): TAA or FSR off, no NVR DXVK / FSR failed, no RESZ
   depth resolve, the game's MSAA on, a scissor set (gun scopes), the Pip-Boy, menus/overlays/loading, an unusual camera port, a
   second world scene in the frame. Any failure while running (missing .spv, refused pipeline): logged, that frame is stretched
   bilinearly so it is whole, and RenderScale is off for the session. If the game ever ignored the port (the viewport after the
   world is the whole screen), RenderScale switches itself off (logged) and nothing is lost.

## Settings ([Shaders.TAA.FSR], all live)
| key | default | meaning |
|---|---|---|
| RenderScale | 1.0 | [0.5-1.0] world resolution as a fraction of the screen's; 1 = off (native FSR as before) |
| Upscaler | 0 | 0 FSR 2; 1 DLSS (not built: FSR 2 runs, logged once; see FORK2-DLSS-PLAN.md) |
| DebugView | 0 | RenderScale only: 1 the world as drawn at the internal resolution in its corner (rest black: the proof), 2 the same stretched without FSR (blocky), 3 FSR's motion input |
Enabled / Sharpness (RCAS) apply as before. TAA's Jitter / MipBias are used. Needs the game's MSAA off.

Log lines (grep "RenderScale"): `RenderScale 0.67: the world scene is drawn at 1286x724 (45% of the pixels) and upscaled by FSR
2.2.1 to 1920x1080 (HDR input, jitter 17 steps).`; with F10 profiling every 240 frames `RENDERSCALE: n frames upscaled ...`.
F10 GPU timers: `FSR upscale (RenderScale)` (whole cost) with `RenderScale copy`, `FSR prep (RenderScale)`, AMD's pass timers,
`RenderScale composite + depth`; compare `World scene (game)`.

## Files
- `src/core/Upscaler.h/.cpp` (new): `Upscaler::IUpscaler` (Evaluate/Output/Failed), `Inputs`; FSR 2's AMD passes moved here from
  TAAFSR.cpp unchanged in what they record (+ render size < display size, + HDR pass set); `Dlss()` null; `Choose()` fallback.
- `src/effects/TAAFSR.cpp`: native FSR = NVR prep + `Upscaler::Choose()->Evaluate` + composite (same passes, same order).
- `src/effects/TAAUpscale.cpp` (new): RenderScale. `TAA.h/.cpp`: settings, BeginJitter branch, EndJitter port restore, Render skip.
- `src/NewVegas/Hooks/Render.cpp`: BeginJitter(first world), UpscaleWorld, scope-fix viewport test.
- `src/core/LampScissor.h`, `PointShadowForward.h`, `PassSums.h`, `effects/SkinScattering.cpp`, `core/ShaderManager.cpp`
  (TESR_RenderScale), `src/hlsl/NewVegas/Shaders/WATER000/001/008/016/017/018/025.pso.hlsl`.
- `src/hlsl/NewVegas/Compute/FSRUpPrep.comp/.spv`, `FSR2ReconstructHdr.spv`, `FSR2AccumulateHdr.spv`,
  `FSR2AccumulateSharpenHdr.spv` (built on Windows, Vulkan SDK 1.3.296.0 / glslang 15; `FSR2/compile_fsr2.sh hdr`).
- Deploy (fork users): the DLL, the 7 WATER*.pso.hlsl, and the 4 new .spv under `Shaders\NewVegasReloaded\Compute\`.

## Still missing / next steps (session-hours)
1. In-game proof and numbers (the bulk list; coordinator). Things to look at: the DebugView 1 corner picture; edges and thin
   detail at 0.67 vs native FSR; water (og-lake) refraction lining up; lamp shadows indoors (lucky22) present in upscaled frames.
2. Depth-based NVR effects after the upscale use the nearest-upsampled, jittered depth: edges of fog/AO against the sky may
   shimmer by a fraction of a pixel. If visible: upsample FSR's dilated depth instead, or jitter-compensate the nearest pick
   (1-2 h).
3. Per-object motion (characters, doors) and a reactive mask for particles/transparencies drawn in the world: as in step 1,
   camera motion only; at low resolution thin particles (rain, sparks) shimmer more (2-4 h for a particle reactive mask from
   the blended stage).
4. Cost: the copy-out could go (FSR reading the world target directly), and the composite could be a Vulkan pass once the
   extension can write depth (ext 1.6 DrawDepthStencil, patch 0010 of the nvr-dxvk-compute line, after the merge) (1-2 h).
5. A native-D3D9 version for the user (no fork): the same viewport trick with AMD FSR 1 (EASU + RCAS, spatial, MIT) in a
   ps_3_0 pass instead of FSR 2 would work on native D3D9 (no temporal AA, softer, but cheap: ~0.3 ms). 3-5 h.
6. The sun glare: the game's sun occlusion counts pixels (1 - n/1500, BSShaderAccumulator 0xB63540); if the glare looks weaker
   at low RenderScale, scale that count (1 h).
7. DLSS: FORK2-DLSS-PLAN.md (needs the SDK download approval and a 64-bit helper; 10-16 h, RTX tester needed).
