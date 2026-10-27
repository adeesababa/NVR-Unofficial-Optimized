# Conor's first fork bug report (2026-10-26): AO / DoF / bounce flicker, terrain blend white line

Report (tester Conor, on the DXVK fork): "AO type shaders are still flickering", "DoF and bounce lighting flicker", "terrain
blending has the white line near where two terrain surfaces meet again".

## His setup (his log + toml; copies in work\bench\deep-roadmap\evidence\conorflicker\)
- NVR DXVK extension 1.4 on DXVK v3.0.1-12, `NvrDxvkPasses = true`; NVIDIA RTX 4070 Ti SUPER (DXVK reports an AMD card), RESZ depth.
- The game's MSAA at 4x, SMAA on, TAA off, film grain 0.3. Every fork slot test before this one ran with MSAA off (slim-C.json).
- AmbientLight on: it switches AmbientOcclusion and BounceLight off (ShaderManager's EnabledGuard), so "AO type" and "bounce" are
  AmbientLight's occlusion and bounce in his game.

## Result: the flicker is AmbientLight's Vulkan path (fork only), indoors
ROOT CAUSE + FIX (21:00, the DXVK-fork session, mine/nvr-dxvk-compute c4ee6341): DXVK uploads a D3DPOOL_MANAGED texture to the GPU
only when a D3D9 draw binds it, and the NVR extension reads the GPU image as it is. AmbientLight's blue noise (read only by the Vulkan
Estimate) stayed all zeros, so every pixel used the same slice angle and the per-frame rotation brightened/darkened the whole
occlusion at once; a few D3D9 frames uploaded it (the 'cure'). Fix: DxvkCompute::PreLoad on every sampled image before each
extension Draw/Dispatch (NVR side only). Confirmed here with MSAA 4x + Conor's toml (run cf-msaa4-vk-preload): floor range 0.03
(was ~12) over 48 pictures. Left over: the fixed Vulkan floor is ~1.1/255 darker than AmbientLight on D3D9 (29.2 vs 30.3).
Any Vulkan pass reading a file texture had the same exposure; A/B lists that start on D3D9 hid it.

CORRECTION (20:05, after more runs): MSAA is NOT needed (the same toggle with iMultiSample=0, run 20261026-200204-slotC), and it is
NOT the light grid (Conor has GridBounce = 0.0, so RenderGrid never runs): the every-4th-frame cadence comes from something else.
A ring of uniform buffers per effect (mine/nvr-dxvk-compute 26cbc681) did not change it. Reproduce in a normal slot: lucky22,
list conorflicker-burst3, RUN_TOML evidence\conorflicker\conor-fx-vk-g0.toml (the fork session's own Lucky 38 tests ran defaults).

Method: still-camera bursts (48 pictures 2 or 3 frames apart, no settings reload between them), Conor's toml with AO / AmbientLight /
BounceLight / DoF on, slot C at 1920x1080 with MSAA 4x, combo DLL (8794c1df), fork d3d9 nvr-dxvk-combo2. Tools: flickermap.py,
brightness.py, groups.py, stages.py in the evidence folder; runs listed in its RUNS.txt.

| scene | native D3D9 | fork, NvrDxvkPasses off | fork, NvrDxvkPasses on |
|---|---|---|---|
| og-desert noon, still | steady | steady | steady |
| og-desert noon, panning 8 deg/s | leftover 3.41 | 3.33 | 3.39 (same) |
| Lucky 38, still | floor 30.6 every frame | 30.5 every frame | **33 / 21 / 33 / 21** |

- Bisect in one launch (`NvrDxvkKeepD3D9`, below): keeping Exposure + Bloom or PointShadows + ShadowsInteriors on D3D9 leaves the
  toggle; keeping **AmbientLight** on D3D9 stops it, and a few D3D9 frames cure it for the rest of the launch (a stuck state that
  forms after the load).
- Stage captures (`world` + `stage`): the world scene is identical every frame; the picture right after the AmbientLight step drops to
  ~0.65x on every 4th frame. Floor 40-55 % darker, walls and objects less. The other
  Vulkan frames are ~7 % brighter than D3D9's AmbientLight.
- AmbientLight DebugView 1 (occlusion only) toggles the same way: the occlusion is wrong on those frames. InteriorBalance off
  still toggles (not the indoor balance).
- AmbientLight off: AO, BounceLight and DoF all run as Vulkan passes and are steady (floor 19.4 in all 48 frames, no pixel flicker).
- The fix is on the DXVK-fork line (mine/nvr-dxvk-compute; AmbientLight's GLSL / ExtDraw belong to that session).
- Until then, for Conor: `[Main.Main.Performance] NvrDxvkPasses = false` (measured steady), or keep it on with this branch's
  `NvrDxvkKeepD3D9 = "AmbientLight"`.

## Terrain blending white line: not reproduced
og-desert with Conor's terrain settings (Enabled, HeightEdge 2, Smooth) and MSAA 4x: native, fork-D3D9 and fork-Vulkan pictures
match along the asphalt slabs, the rocks and the land's own triangle edges (the only differences are the anti-aliased silhouettes,
resolved slightly differently by DXVK). Needs Conor's screenshot and the place.

## NvrDxvkKeepD3D9 (test switch, this branch)
`[Main.Main.Performance] NvrDxvkKeepD3D9 = "AmbientLight,Exposure"`: these effects keep their D3D9 passes while the rest stay Vulkan.
Same setting as on mine/nvr-dxvk-compute (82612f69), but also checked in EffectRecord::ExtDrawTo and ExtFullScreen, so effects with
their own Render (AmbientLight, AmbientOcclusion, BounceLight, DoF ...) honour it too. Settable per shot ('set' in a shot list).

## Bench lesson
Changing a slot's INIs (MSAA) must happen only while the run holds the slot: the first version of run_bursts.sh set them when it
queued, and other sessions' slot C runs between 17:56 and 19:11 got MSAA 4x (they were told). Fixed in run_bursts.sh (waits until the
queue board shows its own ticket running, restores as soon as the run returns).
