# Fork round 2: screen-space GI (ComputeGI), 2026-10-27 night

Branch `deep/fork2-ssgi` (on `mine/nvr-dxvk-compute` 34a8df4d). No fork patch: the extension as it is (1.6; any NVR DXVK with 0004+;
bulk runs with `G:\_NVR-build-scratch\nvr-dxvk-0011`). Shot list: `work\bench\foundations\lists\fork2-ssgi-bulk.txt` (2026-10-03 work
root), scenes lucky22 and og-desert. v2 build: `G:\_NVR-build-scratch\fork2-ssgi-out\NewVegasReloaded.dll`, sha256 87f8edfc7bdbd463...
(VS2019 Release Win32, full rebuild, no new warnings). v1's DLL (77fc77e0) is superseded.

## What it does (plain)
One bounce of light from what is on screen, gathered by AmbientLight's own occlusion search: a lamp-lit floor brightens and tints
the corner and the underside of the table next to it, sunlit ground lights the shaded face of a rock or a pylon. The occlusion stays
AmbientLight's (same picture at strength 0); the light grid (GridBounce) still brings the light from what is off screen. Off by
default; does nothing on native D3D9 or stock DXVK (it needs integer bit operations, which ps_3_0 does not have). With it off every
pass and shader is exactly the old one.

## Settings ([Shaders.AmbientLight.Main], all live, defaults in resource/NewVegasReloaded.dll.defaults.toml)
| key | default | meaning |
|---|---|---|
| ComputeGI | false | on: needs NVR DXVK + [Main.Main.Performance] NvrDxvkPasses true |
| ComputeGIStrength | 1.0 | 0-4, bounce light added (x the surface colour); 0 = the picture of ComputeGI off |
| ComputeGIThickness | 100 | world units a surface is taken to be thick (who hides whose light) |
| ComputeGIQuality | 0 | 0 = 2 slices x 4 steps (Estimate's own 16 taps), 1 = 3 x 6 (36), 2 = 4 x 8 (64) |
| ComputeGIThinOcclusion | false | experimental: the occlusion from the bitmask too (no halo round thin things) |
| ComputeGITest | 0 | test only: 1 = GI estimate alone, 2 = + its accumulate (the strength-0 check per pass) |
| DebugView | 13 = new | the GI light alone as it arrives (x strength, before the surface colour) |

## How (v2, all Vulkan passes; F10 lines)
1. Prepare / NormalSmoothing: unchanged.
2. NEW `AmbientLightGILight.frag` (`  Ambient light GI light (half)`): the frame's light per prep texel (linear, capped at 1, sqrt) into
   `NVR_AmbientLightGILight` (A8R8G8B8, made the first time ComputeGI runs; 2 MB at 1080p, so the GI pass's light reads stay in cache).
3. `AmbientLightEstimate-gi0/1/2.spv` in Estimate's place (`  Ambient light estimate (half)`): AmbientLightEstimate.frag itself compiled
   with NVR_GI (variants.txt). The GTAO lines, taps and inputs are Estimate's (checked: the file without NVR_GI compiles to exactly the old
   SPIR-V). Added per slice: a 32-sector visibility bitmask (Therrien et al. 2023), sectors spaced so each is 1/32 of the cosine-weighted
   hemisphere (t = (sin(theta - n) + 1) / 2, no acos per tap); each tap covers its front-to-back (thickness) sectors and the sectors it
   covers first add its light if its surface faces the pixel (the prep's normal, read anyway). Output rgb = the light, a = the occlusion.
4. Blur X / Y: unchanged (they always blurred an rgb bounce channel).
5. `AmbientLightGIAccumulate.frag` in Accumulate's place: the occlusion's lines unchanged + the same reprojection, depth test, neighbour
   clamp and keep share for the light, into the bounce history (historyTexture, unused since the screen bounce went).
6. `AmbientLightApply-gi.spv` (AmbientLightApply.frag with -DNVR_GI_APPLY) in Apply's place: the light upsampled with the occlusion's own
   depth-weighted taps (joint bilateral), x SurfaceColour x strength (TESR_AmbientLightData.z, the retired screen bounce's slot), added
   after the occlusion and the grid's light (the grid lights the open sectors, the GI the covered ones).
Any failure: logged once, ComputeGI off for the session, the old passes. Missing .spv files: logged, ComputeGI stays off.

## v1 -> v2 (bulk run 2026-10-26 23:06, lucky22, coordinator)
v1 (510684a0) packed depth + light with a compute dispatch into a storage buffer (8 bytes a texel, 8 x 8 tiles, 4 levels) that the GI
pass read. Results: the log clean, but (1) gi-s0 (strength 0, 16 taps, no levels) was NOT AmbientLight's picture (52 % of pixels > 2/255
vs the noise pair's 11 %; dark pixels +26 %, i.e. less occlusion), although its GTAO code was Estimate's line for line: the depths it
read from the buffer were not the prep's. The exact fault was not found from the pictures (writer and reader index the same, the push
layouts match; the compute-written storage buffer read in a fragment pass is the suspect). (2) Cost: estimate 0.37 (al) -> 0.43 (q0),
0.97 (q1), 2.05 (q2) ms: the cost follows the tap count; the 8-byte buffer taps saved nothing over the 16-byte prep texture reads
(texture cache vs buffer loads), and the levels made it slower (gi-nomips 0.1 ms faster). Prefilter 0.15-0.17 ms. (3) DebugView 13
showed light x SurfaceColour, whose hue boost made it look green / purple.
v2 drops the buffer and the levels: the GI pass reads the prep exactly as Estimate does, the light comes from a 4-byte texture written
by a plain pass, DebugView 13 shows the light alone, Quality defaults to 0. Why q1 cost +0.6 ms: 36 taps instead of 16 (x2.25) on a
pass whose cost is its taps; the bitmask itself adds ~16 % at equal taps.

## v2 run (2026-10-26 23:57, lucky22, coordinator) and the v2.1 shader fix
- PASS: gi-s0 vs al-a 10.6 % of pixels > 2/255 (noise pair 11.4 %), gi-s0-t1 11.8 %, -t2 10.2 %: at strength 0 ComputeGI is
  AmbientLight's picture. gi-a vs al-a 38 %, +1.55 signed.
- Cost (whole AmbientLight, ms): al 1.30-1.46; GI q0 1.68-1.72 (estimate 0.33 -> 0.52, light pass 0.08-0.09); q1 3.22; q2 4.89.
- WRONG 1, DebugView 13 showed a pink "normal" picture: Estimate sends its raw diagnostic values (visibility, first tap's cosine, its
  weight) out in rgb for every DebugView from 6 up (harmless before: no later view read Estimate's rgb), and the GI's rgb IS the light
  view 13 shows. Fix: in the NVR_GI build only DebugView 6 takes that exit.
- WRONG 2, the even lift over the carpet: SliceSin clamped a direction outside the hemisphere by the sign of sin(theta - n), which
  flips past theta - n = -pi. A floor tap close to the pixel on the camera's side, pushed back by the thickness, lands below the floor
  there, and its "back" came out at the far edge: that one tap covered all 32 sectors with the floor's own light (and blocked the walls'
  light behind). Fix: outside the hemisphere a direction can only lie beyond its own side's edge (theta stays on its side's half of the
  slice), so the clamp is the side's sign. A 2D model of one slice (scratch slice_model.py: floor at camera elevations 15/35/60 deg,
  with and without a wall 120 units away) gives: old 32 of 32 sectors from the floor itself and none from the wall; fixed 0 from a flat
  floor and 6 from the wall (the wall's exact cosine share is 5.9 of 32). So the light is now local by construction.
- Both fixes are in the shaders only (AmbientLightEstimate-gi0/1/2.spv); the DLL is unchanged (87f8edfc). The q0 estimate should get
  cheaper too (the floor's taps no longer all read their light and normal). q1/q2 cost far more than their tap count (the 36- and
  64-tap shaders are 145-250 KB of unrolled code): keep q0.

## Expected cost of v2 (1080p, GTX 1070; before the run)
Light pass ~0.05-0.08 ms (one read of the frame), GI estimate at q0 ~ Estimate + 15-25 % (0.43-0.47 vs 0.37), accumulate and apply
+0.02-0.04 together: about +0.15-0.2 ms over AmbientLight. AmbientLight's grid bounce costs ~0.02-0.05 ms + its sun map: ComputeGI
does not replace it at equal cost; it is a different light (near, contact, colour bleeding from what is on screen). Against
BounceLight (~0.6-1 ms) it is cheaper and occlusion-correct. "More taps for the same cost" did NOT happen on the GTX 1070.

## Files (test slot / by-hand install; nothing installed)
- `NewVegasReloaded.dll` (+ .pdb) -> `Data\nvse\Plugins\`; `resource\NewVegasReloaded.dll.defaults.toml` next to it.
- New SPIR-V -> `Data\Shaders\NewVegasReloaded\Compute\`: AmbientLightGILight.spv, AmbientLightEstimate-gi0/-gi1/-gi2.spv,
  AmbientLightGIAccumulate.spv, AmbientLightApply-gi.spv. The other .spv files are unchanged (only these were compiled, on Windows,
  with the steps of tools/nvr-dxvk/compile-compute.sh; a full run of that script rebuilds every .spv with this glslang, other bytes).

## Missing / next (session-hours)
- The bulk run of v2 (coordinator). If gi-s0 still differs, gi-s0-t1 / -t2 name the pass. (0.5 h after the run)
- Tune thickness / strength; InteriorBalance does not scale the GI light (Lucky 38 may come out brighter). (1 h)
- Fold the light pass into NormalSmoothing's second pass (it writes the prep anyway) to save its pass. (1 h)
- Verdict after the run: keep as an option at q0 if the light is visible and +0.15-0.2 ms; otherwise leave off as a measured prototype.
