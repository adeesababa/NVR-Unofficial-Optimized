# Terrain flicker: deep dive (2026-10-30, branch claude/terrain-flicker)

Started from the installed build claude/motion-ring-fix aee0046c (on rt-test-merge a93b6f18). The question is why terrain
flicker keeps coming back, inconsistently, across users and NVR versions.

## 1. What can make the land flicker (abstract)

Flicker means a land pixel's colour changes from frame to frame while the camera and the world stand still. So we need the
inputs of land shading that can change between frames on their own.

| Class | Inputs that change per frame with a still camera | Where in NVR |
|---|---|---|
| Sub-pixel jitter + history | TAA/FSR jitter offsets, history weight, history rejection/clamp, LowResEffects (effects run at the jittered low resolution) | TAA.Main Jitter, TAA.FSR, TAAEffect::LowResJitterUV |
| Temporal noise | the noise phase of AO / AmbientLight / cloud shadows / terrain-blend dither / film grain / sky dither, blue-noise index | AmbientLight Temporal, TerrainBlendExtra.z, Cinema FilmGrainAmount, SkyDither |
| Shadow sampling | cascade choice per pixel, cascade texel snapping (stabilisation), cascades redrawn on alternate frames (LimitFrequency, Stagger), sun steps (QuantizeSun / SmoothSun / glide), screen-space shadow march noise | ShadowsExteriors.ShadowMaps, SunSmoothing, ScreenSpace |
| Geometry choice | tessellation factor / far switch (FarUntessellated, FarHysteresis), LOD land vs full land, terrain blend dither, displacement in the shadow pass (CastShadows) | Terrain.Displacement, Terrain.Blending |
| Depth fights | land layers and decals at a distance, inverted depth on/off, D24 vs D32 (`useD32forD24`), the near plane | depth format, InvertedDepth |
| Texture choice | parallax step count and mip choice (IsoHeightTaps), per-layer blend weights, mip bias under FSR (MipBias -0.5 + RenderScale) | Terrain.Parallax, POM.Displacement, TAA MipBias |
| Path / backend | DXVK vs native D3D9, the fork's Vulkan passes (NvrDxvkPasses, KeepD3D9), managed-texture uploads under DXVK (the AL blue-noise case) | DxvkCompute |
| Races | multithreading mods (AI threads, IOWorker2) changing scene state while it is drawn, shadow-pass object lists | outside NVR |
| GPU vendor | precision differences (half vs float), derivative and helper-lane behaviour, NaN handling | NVIDIA behind DXVK's AMD spoof |

## 2. Evidence (every past report)

Gathered from the memory notes, LOGS-INDEX and the notes folders. Nine reports:

1. **Cloud ground flicker (P82 v3 testers).** On DXVK with NVIDIA cards, single frames come out about 17% darker in the lower
   half. Not explained; many causes were ruled out (CLOUD-FLICKER-NOTES.md).
2. **Clouds shaking.** TAA jitter plus FSR LowResEffects; the clouds composite used the unjittered matrices. Fixed on linear-scene.
3. **Conor's fork flicker.** AmbientLight's blue noise was never uploaded under DXVK. Fixed in c4ee6341 (PreLoad). Before that
   fix, 42% of desert pixels differed (empty noise textures).
4. **Conor's terrain white line.** Not reproduced.
5. **RTX 5090 tester.** Indoor temporal flicker; worked around with NvrDxvkPasses off.
6. **Iro.** Flicker with a KeepD3D9 list. Not reproduced.
7. **Owen.** A night road tile flickers under lamps (POM). Candidate fix: lamp-run cap 2 to 8 (lampruns), not run.
8. **Smaller ground cases:**
   - AO quarter-resolution shimmer.
   - Tessellation ObjectBlend shimmer (ObjectBlendShadows added, off).
   - IsoHeightTaps measured about 1.3x more flicker.
   - Grass flickers with FSR + AmbientLight.
   - Cell-border history resets.
9. **Older cases:**
   - Staggered sun cascades (made opt-in).
   - Exposure flicker.
   - Floor-trash flicker, which happens with every NVR effect off.

What they have in common:
- Something temporal (jitter, history or noise) is in almost every case.
- The DXVK / fork Vulkan path is in 4 of the 9.
- No two users had the same toml, and we never collected the DXVK version or dxvk.conf.

## 3. The detector: `landflicker`

Shot-list command `landflicker <frames>` (core/SettingShots.h, `SettingShots::Land`). Over the shot's last frames the camera
stands still, and each frame's finished picture is halved and read back as luma. One frame then draws every land shader flat
magenta, which marks the land pixels. The mask is eroded by one pixel to leave out object edges.

The log lines start `LAND FLICKER`. For the land and for everything else they give:
- the per-pixel temporal standard deviation in 8-bit levels: mean, p95 and p99;
- the share of pixels over 2 and over 5 levels;
- one-frame spikes per 1000 pixel-frames (a jump of 10+ levels that comes back the next frame);
- the same by far, middle and near screen rows;
- the land's mean brightness frame by frame (a whole-ground pulse such as report 1).

Each shot also gets a heatmap, `<slug>-landstd.png`. Lists: bench\foundations\lists\tflick-*.txt.

## 4. Runs and findings

**Bench setup:** slot A on the iGPU (AMD Radeon, NVR DXVK fork-0033) at 1280x720. The toml is the user's own of 2026-10-30
(bench\deep-roadmap\terrainflicker\user-20261030.toml). It has:
- FSR at 0.5 with LowResEffects, TAA with jitter;
- tessellation with Contrast 10, terrain parallax with IsoHeightTaps;
- film grain 0.3, SkyDither and DitherBuster;
- Terrain Blending off.

Each shot runs with `sgtm 0.001`, so sun and clouds stand still, and measures 64 frames.

**tflick-3, noon desert (run 20261030-144358-slotA): no terrain flicker on this setup.**
- **Detector.** The mask is right: 27% of the picture is land (road and rocks are objects). It repeats exactly: base and
  base-2 give the same numbers.
- **Base** (grain and SkyDither off): land std 0.39 levels, p99 1.4, 0.2% of land pixels over 2 levels, no one-frame
  spikes, no brightness pulse (range 0.14%).
- **The user's toml as is:** std 0.55, the film grain.
- **TAA off:** land and rest exactly still (std 0.00). Every bit of frame-to-frame change goes through TAA: jitter, plus the
  noise patterns, whose frame counter only moves with TAA.
- **DitherBuster off:** p99 down from 1.37 to 0.97; pixels over 2 levels down from 0.23% to 0.08%. It is the largest single
  source left, and it is meant to be one (a temporal dither).
- **Jitter off / FSR off:** more pixels over 2 levels (0.64% / 0.79%), still small.
- **No effect:** tessellation, parallax, screen-space shadows, sun shadows, ContactHardening, LowResEffects.
- **Side find:** in the shot taken with TAA switched off mid-run, the tessellated land near the player stands up in big
  spikes. That is the user's Contrast 10, the "field of spikes" value; the default is now 4. The mip-bias compensation
  (DxvkCompute DisplaceMipCompensation) probably only helps while TAA runs. Not yet checked.

**Conclusion so far:** a still camera in daylight on the iGPU, with the user's toml, shows no terrain flicker. The reports
need something this setup lacks: NVIDIA behind DXVK, clouds or cloud shadows, night, a changed shader set (the 6-layer
X4505 fallback below), or movement.

**6-layer land fallback (testers 2026-10-30).** macintroll (native D3D9, MSAA 4x, The Frontier) sees straight-edged
light/dark patches along the cell grid with the terrain shader on. His log shows SLS2132/2134/2136/2138 failing with X4505
in Parallax.hlsl. Those are the 6-layer land shaders built with terrain blending and captured normals.
- The failure happens once at load and is never retried. No cached .pso is written, so the cost is a recompile at every
  start, not hitches in play.
- With PREFER_FLOW_CONTROL (the fix, 9eb91a64) the shaders are small, measured offline with the game's D3DX
  (template_compile): 6 layers take 2308 slots, against 1986 for 5 layers and 2380 for 7 at the default flags. The flag
  costs about 5%.
- To check the mechanism: the test-only switch `[Main.Develop.Main] FailShaders` makes named shaders fail as the old
  build did. A/B list tflick-5, tomls blend-on and blend-on-fail6.

## 5. Diagnosis guide for users

(written once the causes are known)
