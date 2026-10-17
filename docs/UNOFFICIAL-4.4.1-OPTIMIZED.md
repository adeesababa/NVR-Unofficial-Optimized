# NVR 4.4.1 UNOFFICIAL Optimized - technical notes

Base: the official New Vegas Reloaded 4.4.1 source (pr0bability/TESReloaded10 `86ce118a`, checked against the 4.4.1
release archive: same version, same shaders, same settings file). The optimizations come from NVR UNOFFICIAL Optimized
(adeesababa/NVR-Unofficial-Optimized); only the ones that keep the 4.4.1 picture were ported, and several were adapted
where 4.4.1 differs from the code they were written for.

Rules for this build: same picture as stock 4.4.1, full resolution, no new shader features, no frame-skipping.
"Same up to rounding" below means: the stock maths in the stock order, without the 16-bit float storage of the image
between two passes - far below what an 8-bit screen shows.

## Settings

All in `[_Main.Main.Performance]` of `NewVegasReloaded.dll.defaults.toml`, shown by 4.4.1's menu under
Main > Main > Performance, on by default, `false` = the original 4.4.1 path. All switch live except SlimDepthBuffer.

| Setting | What it does | Same picture? |
|---|---|---|
| FrameChain | Effects alternate between two buffers (the game's HDR target is one of them) instead of copying the whole frame after every pass. Passes that clip, discard, blend or use stencil get their destination pre-filled first. The last post-tonemap effect writes the back buffer directly. | Yes (same formats, passes and order) |
| ChainUsesGameTexture | The game's own HDR image is one of the two chain buffers (saves a copy at the start). | Yes |
| SlimDepthBuffer | The effects' depth buffer keeps one 32-bit value per pixel; the second value (post-projection depth) is recomputed where it is read. Restart. | Up to float rounding |
| DedicatedAO | Ambient occlusion at full resolution as in 4.4.1, but its term lives in two small G16R16F targets, the x and y SSAO kernels run in one pass (they share origin, normal and kernel basis), the blurs ping-pong, and only the combine touches the HDR frame. Pixels beyond the AO range pass through, which is what the stock in-place `clip()` left. | Up to rounding |
| CompositeFog | When no effect renders between them (snow accumulation, wet world, flashlight, specular, underwater - off by default, or only in rain / under water), the exterior sun-shadow blend and the AO blend are applied inside the fog pass: two full-resolution HDR passes fewer. The fog is 4.4.1's own; its maths in the combined pass is the stock text. If anything fails, the original three passes run. | Up to rounding |
| DedicatedGodRays | God rays run the same half-resolution passes as 4.4.1, but in their own half-resolution targets instead of a corner of full-size ones: no copies, no clears. The ray falloff is still computed per full-resolution pixel. The sky-mask pass returns black straight away for non-sky pixels (bit-identical). | Yes, except the texels at the right and bottom screen edge of the ray buffer, where the stock pass read outside its own corner |
| MergedPointShadows | The shadow term of all twelve lamps in one pass instead of two (when LightPoints > 6). | Up to rounding |
| CubeMapFaceCulling | A lamp's shadow cubemap face skips objects whose bounding sphere cannot touch it (skinned and SpeedTree objects are always drawn). | Yes (those objects were clipped) |
| SkipUnusedCubeMap | The twelfth lamp is lit without a shadow lookup in 4.4.1, so its cubemap is no longer drawn. | Yes |

Always on (no switch):
- Effects that never read `TESR_SourceBuffer` no longer copy the frame into it (and there is no copy at the start).
- Interior lamp shadows: the shadow-term blurs run on two G16R16 targets and only the last pass touches the HDR
  frame (with FrameChain); blur taps that are all equal skip the depth reads.
- Lamp shadow pass: a lamp's cubemap is not read where the pixel is beyond the lamp's radius (the stock result is
  multiplied by exactly 0 there); empty lamp slots are skipped.
- Normals: the first pass reuses its nine depth reads for the positions (stock read five of them twice). The two
  stock normal blurs are kept.
- Exterior shadow blend: the underwater water-plane test only runs when the camera is under water.
- Cinema: no per-pixel depth read when no letterbox is active. ImageAdjust: skipped at its identity settings.
- Shadow bone matrices are uploaded in runs instead of one call per bone.
- Log: graphics adapter, depth-resolve path and D3D9 runtime; a reference leak in the depth resolve fixed.

## Not ported (would change the 4.4.1 picture or resolution)

- Half-resolution ambient occlusion (4.4.1 renders AO at full resolution), half-resolution fog, half-resolution god-ray
  falloff, the removal of the normal blurs, the distant-only depth-of-field shortcut.
- Staggered sun-cascade refresh and lamp shadow refresh intervals (moving shadows would lag), partial shadow-atlas
  resolve/blur (the stock blur's edge taps cross cascades).
- Bug fixes that change the picture (contact-shadow lines, depth of field in third person), and everything that needs
  files 4.4.1 does not have.

## Measured

GTX 1070, 2560x1440, native Direct3D 9, 4.4.1 default settings; two sessions per build, alternating:

| Scene | 4.4.1 | 4.4.1 UNOFFICIAL Optimized |
|---|---|---|
| Lamp-lit casino interior | 24.7 ms (40.5 FPS) | 15.8 ms (63.1 FPS) |
| Desert at noon | 27.8 ms (36.0 FPS) | 19.7 ms (50.7 FPS) |

Same picture: inside one session the seven live switches were turned off and on, with screenshots a few frames apart
across each switch. The NVIDIA FLIP difference stayed at the frame-to-frame noise level, with no visible change (the
only differences are animated flames and one-level band edges in the sky gradient), and average brightness moved by
less than 0.01 of a colour level.
