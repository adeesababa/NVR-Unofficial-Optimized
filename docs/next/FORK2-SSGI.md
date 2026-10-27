# Fork round 2: screen-space GI with compute (ComputeGI), first step (2026-10-27 night)

Branch `deep/fork2-ssgi` (on `mine/nvr-dxvk-compute` 34a8df4d). No fork patch: it uses the extension as it is (1.6, any NVR DXVK
build since 0010; tested fork for the bulk run: `G:\_NVR-build-scratch\nvr-dxvk-0011`). Shot list:
`work\bench\foundations\lists\fork2-ssgi-bulk.txt` (2026-10-03 work root), scenes lucky22 and og-desert. NOT run in game yet.

## What it does (plain)
One bounce of light from what is on screen, worked out by AmbientLight's own occlusion search: a lamp-lit floor brightens and tints
the corner and the underside of the table next to it, sunlit ground lights the shaded face of a rock or a pylon. The occlusion stays
AmbientLight's (same look); the light grid (GridBounce) still brings the light from what is off screen and far away. Off by default;
does nothing on native D3D9 or stock DXVK (it needs integer bit operations, which ps_3_0 does not have, so a D3D9 version is not
possible). With it off, every pass is exactly the old one (same shaders, same cost).

## Settings ([Shaders.AmbientLight.Main], all live, defaults in resource/NewVegasReloaded.dll.defaults.toml)
| key | default | meaning |
|---|---|---|
| ComputeGI | false | on: the GI pass replaces Estimate (needs NVR DXVK + [Main.Main.Performance] NvrDxvkPasses true) |
| ComputeGIStrength | 1.0 | 0-4, bounce light added (x the surface colour) |
| ComputeGIThickness | 100 | world units a surface is taken to be thick (who hides whose light) |
| ComputeGIQuality | 1 | 0 = 2 slices x 4 steps (Estimate's 16 taps), 1 = 3 x 6 (36), 2 = 4 x 8 (64) |
| ComputeGIMips | true | far taps read coarser levels of the GI input (the cache saving); off to measure it |
| ComputeGIThinOcclusion | false | experimental: the occlusion from the bitmask too (no halo round thin things) |
| DebugView | 13 = new | ComputeGI's bounce light alone, as it is added |

## How (passes, all through the extension; F10 lines)
1. Prepare / NormalSmoothing: unchanged.
2. NEW `AmbientLightGIPrefilter.comp`, one compute dispatch, `  Ambient light GI prefilter (compute)`: per half-resolution texel
   (view depth from the prep, the frame's light: linear, capped at 1, sqrt 11:11:10) -> uvec2, in a storage buffer of 8 x 8 tiles,
   4 levels made in the same dispatch through shared memory (XeGTAO's depth MIP filter for the depth, the same weights for the light).
   Compute buys this: one dispatch instead of an inject pass + 3 downsample passes. A buffer, not storage images: the extension's
   proven write path (the Hi-Z agent found storage-image writes into mip views of a D3D9 render target reading back all zero).
3. `AmbientLightGI.frag` in Estimate's place (`  Ambient light estimate (half)`): Estimate's GTAO line for line (with Quality 0 and
   Mips off: Estimate's own value), plus a 32-sector visibility bitmask per slice (Therrien et al. 2023): each tap covers its
   front-to-back (thickness) sectors; the sectors it covers first add its light. Sectors are spaced so each is 1/32 of the
   cosine-weighted hemisphere (t = (sin(theta - n) + 1) / 2): no acos per tap, a bit count is a cosine-weighted share. Taps read 8 bytes
   (the GI input) where Estimate reads the 16-byte prep; far taps a coarser level (level floor(log2(texels) - 3.3)). Output = Estimate's
   (a visibility, rgb now the bounce light).
4. Blur X / Y: unchanged (they always blurred an rgb bounce channel, with the loose depth test).
5. `AmbientLightGIAccumulate.frag` in Accumulate's place: the occlusion's accumulation line for line + the same reprojection, depth
   test, neighbour clamp and keep share for the light, into the bounce history (historyTexture, unused since the screen bounce went).
6. `AmbientLightApply-gi.spv` (AmbientLightApply.frag with -DNVR_GI_APPLY) in Apply's place: the light upsampled with the occlusion's own
   depth-weighted taps (joint bilateral), x SurfaceColour x strength (TESR_AmbientLightData.z: the retired screen bounce's slot),
   added after the occlusion and the grid's light (the grid lights the open sectors, the GI the covered ones).
Any failure (files missing, a pipeline refused, a dispatch failing): logged once, ComputeGI off for the session, the old passes.

## Expected cost (not measured; 1080p, GTX 1070), and the honest comparison
- Prefilter: reads the full-resolution frame once (16.6 MB RGBA16F) + the prep (8.3 MB), writes 5.5 MB: about 0.12-0.15 ms
  (1440p ~0.25). This is the fixed price of the light; it cannot be lower than one read of the frame.
- GI pass vs Estimate: per tap 8 bytes instead of 16 and coarser far taps, but ~2x the arithmetic (bitmask). Quality 0 about Estimate's
  cost; Quality 1 (36 taps) maybe 1.5-2x; Quality 2 (64) 2.5-3.5x. The bulk list measures exactly this (gi-q0 / gi-a / gi-q2) and what
  the levels save (gi-nomips).
- Accumulate +4 reads, Apply +4 reads: ~0.02-0.04 ms together.
- So: ComputeGI on = roughly +0.2-0.4 ms over AmbientLight at 1080p. AmbientLight's own bounce (the light grid) costs ~0.02-0.05 ms
  plus its sun map: ComputeGI does NOT replace it at equal cost; it is a different light (near, contact, colour bleeding from what is
  on screen; the grid is room-sized cells, 3 m, and keeps what is off screen). Against BounceLight (~0.6-1 ms at 1080p, 8 samples,
  no occlusion test between): ComputeGI gives occlusion-correct bounce from 16-64 taps for less. Whether it is worth its cost is the
  bulk run's question; if the pictures show little, the verdict is "measured prototype, leave off".

## What the bulk run must show (fork2-ssgi-bulk.txt)
- gi-s0 = al (same picture): proves the GI pass's occlusion is Estimate's (a difference is a bug).
- dbg-gi not black: proves the bounce light is really there (the Hi-Z lesson: a fallback can hide a feature that does nothing).
- gi vs al pairs: the light added; F10 the cost. gi-q0/gi-q2: cost per tap; gi-nomips: what the levels save.

## Files (for a test slot or a by-hand install; nothing installed)
- `NewVegasReloaded.dll` (+ .pdb) -> `Data\nvse\Plugins\`; `resource\NewVegasReloaded.dll.defaults.toml` next to it (the new keys).
- New SPIR-V -> `Data\Shaders\NewVegasReloaded\Compute\`: AmbientLightGIPrefilter.spv, AmbientLightGI-q0/-q1/-q2.spv,
  AmbientLightGIAccumulate.spv, AmbientLightApply-gi.spv (AmbientLightGI.spv is the generic 2 x 4 build, not loaded). The other .spv
  files are unchanged (compiled on Windows by hand with the same steps as tools/nvr-dxvk/compile-compute.sh, only these; a full
  re-run of that script would rebuild every .spv with this glslang, which differs in bytes from the committed ones).
- Without the .spv files ComputeGI logs what is missing and stays off (no global compute failure).

## Missing / next (session-hours)
- In-game run of the list (coordinator). Then tune: thickness, strength, the light cap; whether Quality 0 is enough. (1 h)
- Interiors with InteriorBalance: the GI is added on top (the balance only scales the grid): check Lucky 38 is not too bright. (0.5 h)
- Multi-bounce (last frame's GI fed back into the light, as BounceLight's MultiBounce) and the grid's light for the open sectors done in
  the GI pass itself (one composition instead of two). (1-2 h)
- If the prefilter's frame read shows up as the main cost: take the light at quarter resolution (one sector per 4 x 4 pixels). (1 h)
