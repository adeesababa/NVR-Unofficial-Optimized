# NVR DXVK fork: GPU particles (2026-10-26, branch deep/fork-particles, fork patch 0009)

Rain and snow drops that are real particles: they live in a storage buffer from frame to frame, a compute shader moves
them and lands them on what they hit, and they are drawn as streaks, splashes and flakes. D3D9 cannot do this (no compute
shaders, no buffers a shader can write, no way to keep per-drop state on the GPU), so it needs the NVR DXVK fork.

## What was built
Fork (patch `tools/nvr-dxvk/patches/0009-NVR-extension-1.5-particles-geometry-passes.patch`, 3 files, all in the
extension's own files):
- A new interface `ID3D9NvrExtGeometry` (GUID ...d309), queried from `ID3D9NvrExt` with QueryInterface. Own GUID instead
  of new methods on `ID3D9NvrExt`, so the method table the other 1.5 patches extend is untouched and a d3d9.dll without
  it just answers E_NOINTERFACE (NVR then leaves the feature off). The extension version number is NOT changed (stays 1.4).
- `CreateGeometryPipeline`: a graphics pipeline whose vertex shader reads its own resources (read-only storage buffers and
  uniform buffers, by slot like every other binding; no samplers or push block in the vertex stage).
- `DrawGeometry`: the existing pass (`DrawImpl`: DXVK state saved and restored, blend, targets) with a vertex and an
  instance count instead of the fixed full-screen triangle; triangle list, no vertex input, no depth buffer.
NVR (`src/effects/GpuParticles.{h,cpp}`, shaders `src/hlsl/NewVegas/Compute/GpuParticles*.{glsl,comp,vert,frag}` + .spv):
- Particles (32 bytes each: position, state, velocity, seed) in a box round the camera, in eye-relative coordinates
  (each frame the camera's movement is subtracted, so drops stay put in the world and the numbers stay small).
- Simulation (compute, 64 per group): fall (rain ~650 units/s, snow ~70 with drift), wrap sideways round the box, land
  when crossing into cover in the rain's top-down occlusion map (roofs, trees, ground, also off screen; the map is
  requested while the effect runs) or when going behind the surface the camera sees in the depth buffer (by less than one
  step + 30 units; snapped onto that surface along the camera ray; nothing nearer than 60 units, so the first-person gun
  never collects drops). A landed rain drop is a splash for 0.4 s, a landed flake settles for 4 s; then the particle
  starts again at the top of the box (never under cover).
- Drawing (geometry pass, 6 vertices per particle, instanced): falling rain = a streak along its last 0.045 s of motion,
  at least ~0.6 px wide (thinner drops fade instead of flickering); splash = a camera-facing ring with droplet rays (since run 3),
  growing and fading; snow = a round flake. Fragment shader: soft fade where it meets the scene's depth (and hidden
  behind anything nearer), near-lens and box-edge fades, colour = the frame behind it mixed toward the sky's light (fog
  colour brightened). Alpha-blended over a copy of the frame after the weather effects (after Precipitations and Snow,
  before the heat haze), then that copy becomes the frame (FrameChain Commit).
- F10 timer: "GPU particles (sim + draw)" (includes the frame copy).

## Settings: [Shaders.GPUParticles.Main] (all live)
| key | default | meaning |
|---|---|---|
| Enabled | false | the feature; needs rain or snow outdoors (ForceRain / ForceSnow count) and the fork's d3d9.dll |
| Count | 32768 | particles (64-262144) |
| Size | 1.0 | drop width / splash / flake size in game units |
| Speed | 1.0 | fall speed multiplier |
| Opacity | 0.8 | strength |
| Radius | 1200 | half width of the box round the camera (units) |
| Splashes | true | landed rain splashes / snow settles; off = straight back to the top |

Off (or no fork, native D3D9, stock DXVK): `Wanted()` is false, nothing is created or drawn, the ortho map is not
requested: the old picture.

## Results
(see the section appended below)

## Result (2026-10-26, slot C 1920x1080, GTX 1070, og-desert / og-night, ForceRain / ForceSnow)
Builds: fork d3d9.dll from CI run 40284996130 (patches 0001-0006 + 0009) in `G:\_NVR-build-scratch\nvr-dxvk-particles\nvr-dxvk-x32`
(+ `dxvk.conf` with `d3d9.customVendorId = 1002`, the slot depth fix); NVR DLL F6C2C908 (tip b9c10109 + base 7a74ab00 merged).
- Works: no crash, no fallback, in all three runs (66 + 22 + 17 shots); log `UNOFFICIAL GPU particles: ready` / `running (rain,
  N particles, ortho map)`. With the depth fix the log says `Depth resolve: RESZ` and the drops are hidden behind the hills and
  stop at the ground; seen looking down, streaks end on the road and leave expanding splash rings; snow flakes fall and lie on the
  road for their 4 s (pictures `H:\_NVR-autotest-shots\20261026-095528-slotC\1-particles2` and `...\20261026-105058-slotC\1-particles3`,
  crops in `G:\_NVR-build-scratch\particles-diffs\r2d, r3d`). At night (og-night) the drops take the dark sky light and show only
  faintly near lit areas (no glowing rain).
- Off = old picture: off has no code path that touches the device (no extension call, no ortho map request); off/off pairs differ
  only by the scene's own noise (og-desert 19-29 % of pixels > 2/255, signed ~0.1-0.4: exposure still settling). On vs off:
  +1/255 on average, 2-2.6 % of pixels > 16/255 (the drops).
- Cost (F10 "GPU particles (sim + draw)", includes the frame copy; og-desert, 600-frame windows, run 20261026-105058-slotC):
  | Count | ms |
  |---|---|
  | 8192 | 0.09 |
  | 32768 (default) | 0.15 (three ABAB rounds 0.149 / 0.153 / 0.149) |
  | 131072 | 0.42 (looking down at splashes 0.38; same with Splashes off 0.28) |
  | 262144 | 0.63 (run 2) |
  Game frame total off 7.70-7.86 vs on 7.83-7.85 ms (inside the scene's swing).
- Missing / next: (1) the drops are lit only by the fog colour + the scene behind them (no lamps, no sun glint, no wind from the
  weather); (2) splashes are camera-facing rings, fine on flat ground, not shaped to walls; (3) only rain and snow: GunFX smoke
  puffs as GPU particles (second target) not started; the geometry interface (any instanced quads with a buffer-reading vertex
  shader) is what it needs; (4) the hit test against the depth buffer uses a 30-unit margin, so a drop passing just behind a thin
  pole lands on it; (5) runs in the post-tonemap chain like the screen-space rain, so no bloom or fog on the drops.
- Settings: [Shaders.GPUParticles.Main] Enabled (false), Count (32768), Size (1.0), Speed (1.0), Opacity (0.8), Radius (1200),
  Splashes (true). Needs ForceRain/ForceSnow or a rain/snow weather, outdoors, and the fork's d3d9.dll.
