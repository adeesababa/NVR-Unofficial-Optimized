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
- **Confirmed (tflick-6, native D3D9 + MSAA 4x on the GTX 1070, Terrain Blending on, 2026-10-30).** Run 20261030-162248-slotA
  (shaders as built) against 20261030-165356-slotA (FailShaders SLS2132-2138, log "UNOFFICIAL TEST FailShaders ... not
  loaded"). Looking steeply down at the noon spot, the 6-layer quadrant the player stands on draws with the game's own
  shader: a bright patch with straight edges along the quadrant lines (33% of the picture's pixels differ by more than 16
  levels). This is macintroll's look. It is a fixed look, not flicker (frame-to-frame std 0.31 in both runs). Over cell
  (23,22) the two runs are the same (mean difference 0.6 levels), so those quads did not pick SLS2132-2138 there.
- The fix (9eb91a64, PREFER_FLOW_CONTROL retry) is in every build after P84 v4. A log with `X4505` on SLS2132/2134/2136/2138 means an older build.
- Not the cause: the game's MSAA. The large dark patches seen over cell (23,22) from 6000+ units up are the same on native
  MSAA 4x and on the fork without MSAA. They are shadows.

**Native D3D9 on NVIDIA with the game's MSAA off: the near ground white (found 2026-10-30, fixed).** Runs 20261030-171859
(one effect off per shot: only VolumetricFog off cures it), 20261030-172205 (fog switches: DistantFogBlend 0 cures it),
20261030-173015 / -173135 (both fixes: no band, distant fog no longer reaches the near ground).
- Cause 1, depth: on native D3D9 an NVIDIA card has no RESZ, so the depth comes through NVAPI StretchRectEx, which copied the
  game's whole depth surface into the depth texture. Here that surface is 1280x1024 for a 1280x720 picture, so the depth was
  squashed: below 720/1024 of the screen (row 506 of 720) every pixel read far. Every depth effect was wrong there. Fixed:
  only the picture's own top-left area is copied (log: "UNOFFICIAL depth (NVAPI): the game's depth surface is ...").
- Cause 2, fog: the distant fog's height fade `exp(-z / (80000 * DistantFogHeight))` passes 1 below z 0, so the lerp toward
  the sky ran past the sky colour to white and then inf, which TAA kept (a frozen picture). Clamped to 0..1 in
  VolumetricFog.fx.hlsl. The Vulkan copy (Compute/VolumetricFog.glsl, lines 701 and 842) has the same formula and still
  needs the clamp plus an .spv rebuild (WSL).
- Who it hits: NVIDIA cards on native D3D9 (no DXVK) with the game's MSAA off, when the game's depth surface is bigger
  than the picture. With MSAA the game's depth is a separate surface the size of the picture.

**Bench notes.**
- Native D3D9 cannot use the iGPU here: it has no monitor, so it is not in D3D9's adapter list and the game quits at start
  when sD3DDevice names it. Native runs use the GTX 1070.
- The settings reader only reads keys listed in the defaults toml. FailShaders had to go into the defaults file, and the
  same gap made user toml lines silently do nothing. Since 1a8a5e48 the log lists every user toml key this build does not
  know ("setting X in your toml isn't known to this build: ignored").
- landflicker pictures on the fork carry pink specks: FSR keeps parts of the one magenta mask frame in its history for
  several frames (0.4% of the pixels; none with FSR, TAA or jitter off). Use the shots without landflicker for looks.

## 5. Diagnosis guide for users

**First, the log** (`NewVegasReloaded.log` next to the game, or in the Logs folder):
- `X4505` on `SLS2132`, `SLS2134`, `SLS2136` or `SLS2138`: an old build. The land quadrants with 6 texture layers draw with
  the game's own shader, which shows as fixed light or dark patches with straight edges along the land grid. Update to
  a build newer than P84 v4.
- `WARNING: the game's own anti-aliasing is on (iMultiSample 4)`: turn Antialiasing off in the game's launcher. NVR's TAA /
  FSR / DLSS smooth the edges instead.
- `setting ... in your toml isn't known to this build: ignored`: that line in your toml does nothing in this build (a
  setting from another build, or a typo).
- `[ERROR]` lines on any `SLS` shader (the land shaders): that land draws with the game's shader. Send the log.

**Then, is it a fixed patch or a flicker?** Stand still and watch for a few seconds.
- A fixed patch with straight edges: the shader case above.
- Flicker on a still picture: in our measurements every bit of frame-to-frame change on the land goes through TAA (the
  picture is exactly still with TAA off). To narrow it down, try one at a time:
  1. `[Shaders.TAA.Status] Enabled = false` (TAA in the menu) (if the flicker stops, it is in the temporal path);
  2. DitherBuster off, then film grain 0 (each is a small, wanted dither);
  3. FSR off, then Jitter off;
  4. on the DXVK fork: NvrDxvkPasses off (the RTX 5090 case);
  5. Terrain Displacement off, then Terrain Parallax off.
  Tell us which step stops it, your graphics card, and whether you use DXVK.
