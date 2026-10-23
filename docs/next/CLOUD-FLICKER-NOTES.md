# SunFar render-target hazard check (DeepSeek lead #2, and #1), 2026-10-22 15:30

**Verdict: the hazard does not happen.** TESR_SunFarBuffer was never bound on any texture stage when the SunFar pass drew into it:
0 of ~1,450 frames, before the pass and at the draw itself. The draw states were already full-screen every frame. Lead #2 is
ruled out and lead #1 looks clean. No fix is needed for this, so none was added as a default.

## What was run
- Tree `work\NVR-sunfarhazard`, branch `deep/sunfar-hazard` (git worktree off dev c90b907b). It holds the lightgrid2 uncommitted state
  copied from NVR-lightgrid2, minus the pbrdark session's test-only MarkShaders edits. Uncommitted, DLL 1B859E64, not installed.
- Test log `[Shaders.CloudShadows.Main] HazardLog = 1` (off by default). It walks stages 0-15 with GetTexture twice: before
  RenderEffectToRT, and inside the pass right before DrawPrimitive (new `EffectRecord::BeforeDraw` hook, after BeginPass + pass
  textures, so after SetCT). It also compares the game's texture cache (renderState unk10A0) and logs Z / ZWrite / Scissor /
  Blend / AlphaTest / Stencil / Cull / ColorWrite / sRGBWrite / Fill, the viewport, the scissor rect and the depth surface.
- Native D3D9, real install via run_one2 (lock, restore checked). Scene: dusk cliff lake, 14 h, gWastelandCloudy, time frozen. Cloud
  shadows on (4 layers, 2 used). Shot list: `bench\foundations\lists\sunfarhazard-1.txt`. Run folder: `bench\autotest\20261022-152919`. Pictures:
  `H:\_NVR-autotest-shots\20261022-152919`.

## Log (NewVegasReloaded_2026-10-22_15-29-21.log)
- `before the pass: target on none (game cache mask 0000; mountain shadows' ground missing: s4 not rebound)`
- `at draw: target on none; states Z 0 ZWrite 0 Scissor 0 Blend 0 AlphaTest 0 Stencil 0 Cull 1 ColorWrite 15 sRGBWrite 0 Fill 3
  viewport 0 0 2560x1440 scissor 0 0 2560 1440`. This was the only line in the run, so the states never changed.
- Summary at 1200 frames: bound before the pass 0, at the draw 0, device/cache disagree 0. The same was true with mountain shadows on.
- This was the worst case for the lead: mountain shadows off, so SunFar's SetCT does not rebind s4. Something between the composite
  and the next frame's SunFar pass already replaces s4 (the composite's sampler, or the game's own scene draws).

## Why this also holds under DXVK
Which texture sits on which stage depends only on the order of the game's and NVR's D3D9 calls. That order does not depend on the
D3D9 implementation, so DXVK gets the same bindings. One part was not observed directly: D3DX Begin/End state save and restore,
which DXVK implements itself. The logged states are taken after BeginPass, so they are what the draw actually used.

## Side result: TargetFix option (off)
The branch also has `TargetFix = false`. When on, it unbinds the target from every stage (with NoteDeviceTexture) and sets
full-screen states around the pass, then puts them back. On native it has nothing to unbind and the states it sets are the ones
already in use.

The picture A/B is inconclusive because of shot order:
- Same-setting pairs differ in 16.7 % of pixels.
- log vs fix pairs differ in about 35 %.
- Pixels off by more than 8 levels are 0.02-0.03 % in both cases.

log/fix also lines up with odd/even shot position, so this can't separate a real difference from position-to-position noise.
Leave it off; it is not a proposed fix.

## Not covered / next leads
- Only one scene and steady play. Loads, menus and weather changes were not logged (the log stays bounded and can run while
  playing if wanted).
- DXVK was not run. With the hazard absent there was no reason to try to reproduce it in a slot copy.
- Remaining DeepSeek leads: #3 (D3DX state-block vs NVR cache desync; no device/cache mismatch was seen for the target here) and
  #5 (pass textures after BeginPass). The most useful next step is still the tester's toml + dxvk.conf and a frame capture of a dark frame.

## 2026-10-23 follow-up (user: "this only happened after P82 v3")
- SunFar / cloud-shadow code is identical between p82-v2 (1c81b7a6) and p82 v3 (bf1357ec). v3's big default change: AmbientLight ON,
  which moves the sun-shadow apply into AmbientLight's Apply pass (path "intoAL"; four paths are picked per frame).
- AmbientLight history-reset frames (Temporal off = the frame after a >0.5 s hitch) are NOT darker: lower third 62.7 vs 62.7
  (run 20261023-100753, dusk, cloudy, cloud shadows on); AmbientLight off only +4 %.
- New test tool FlickerWatch (core\FlickerWatch.h): per-frame lower/upper-half brightness, logs jumps with the frame's path flags.
  Native smoke (20261023-102941): 1,500 frames, 0 flagged; normal path "intoAL ALapplied".
- Installed for the user with default settings: deep/combo-1023 (DLL D16144E9), FlickerWatch 1. Our DXVK = v2.6.1 x86 + dxvk.conf
  useD32forD24 (changes the depth format the effects read): ask the testers for their DXVK version and dxvk.conf.
