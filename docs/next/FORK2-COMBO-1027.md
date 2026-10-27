# deep/fork-combo-1027: the testers' fork build + the port line, one patch series (2026-10-27)

What it is: `mine/deep/fork-combo-1026` 8062c5ae (FSR 2 native AA, GPU particles, Hi-Z, HDR output, SM6 passes, DoF bokeh,
no-limits replacements, pixel inspector / shader counters / capture tools) merged with `mine/nvr-dxvk-compute` 34a8df4d (SMAA
through the stencil, point-shadow preshaders on the CPU, the AmbientLight flicker fix = managed textures uploaded before a Vulkan
pass, DXVK profiling, NvrDxvkKeepD3D9 / NvrDxvkTestSuffix test tools). Merge base 870f3109. Nothing new is switched on: every
setting keeps the default it had on its own branch.

## Builds
- NVR DLL (tree 37c9b215, builds clean): `G:\_NVR-build-scratch\combo1027-out\NewVegasReloaded.dll`, sha256 F8EE5A65DFFF1C35...
- Fork d3d9.dll: GitHub workflow run 40441095208 (patches 0001-0016, passed), `G:\_NVR-build-scratch\nvr-dxvk-combo1027\nvr-dxvk-x32\`,
  sha256 416783C5595BA63B... No dxvk.conf next to it (not needed).

## NVR side: conflicts (both sides kept)
- `DxvkCompute.h/.cpp`: CreateBufferNoFail (debugging tools) next to PreLoad and ProfileTick (port line).
- `EffectRecord::Render`: the hand-ported Vulkan path is skipped for an effect NvrDxvkKeepD3D9 names and for a technique the SM6
  builds cover (Sm6Covers), inside the GpuDebug label.
- `EffectRecord.h`: ExtDrawTo's DepthStencil comment (the parameter itself merged cleanly).
- `SettingShots.h`: both RenderDoc shot commands: `capture [frames]` (GpuDebug) and `renderdoc [<path>]` (TriggerCapture).
- After the merge: the combo's own direct extension calls (HDR gain, Hi-Z pyramid, pixel inspector, bokeh scatter, GPU particles,
  TAA FSR) call `DxvkCompute::PreLoad` first, as the brief's 10-26 lesson asks (they bind render targets today: no change).
- `NvrDxvkExt.h` = the fork's `d3d9_nvr_ext_api.h` again, byte for byte (version 1.6; 64 slots / 96 bindings from the sm6 patch).
- The combo's Vulkan shaders that include Common.glsl were rebuilt with the port line's version (no nonuniformEXT on the sampler
  heap index; HdrGain, FSRPrep, FSRLumaMip, FSRComposite, bokeh scatter + combine, ReflectionsTraceHiZ + Count, AL HiZ).

## Fork side: one series, `tools/nvr-dxvk/patches/0001-0016`
| # | patch | was |
|---|---|---|
| 0001-0006 | extension 1.0-1.4 (compute, full-screen passes, replacements, push-data rule, samplers, DrawShifted) | same |
| 0007 | RESZ depth resolve on every GPU vendor | same (identical on both sides) |
| 0008 | 1.5 profiling | port 0008 |
| 0009 | injected passes end suspended | port 0009 |
| 0010 | 1.6 DrawDepthStencil | port 0010 |
| 0011 | managed textures uploaded before a pass samples them | port 0011 |
| 0012 | 1.5-hdr (ID3D9NvrExtHdr) | combo 0007-hdr |
| 0013 | 1.5-particles (ID3D9NvrExtGeometry) | combo 0009 |
| 0014 | 1.5-sm6 (64 slots / 96 bindings) | combo 0013 |
| 0015 | 1.5-debug nvr.debugEnv | combo 0014 |
| 0016 | 1.5-debug readback that never waits (ID3D9NvrExtReadback) | combo 0015 |

Rebased in `G:\_NVR-build-scratch\dxvk-combo1027` (branch `nvr-combo1027` of the dxvk301-win repository). One hand conflict,
0013 against 0008/0010 in `d3d9_nvr_ext.{h,cpp}`: the class keeps SetProfiling, GetProfile, DrawDepthStencil and the geometry
methods, and the shared `DrawImpl(desc, shiftX, shiftY, depthStencil, vertexCount = 3, instanceCount = 1)` serves Draw,
DrawShifted, DrawDepthStencil and DrawGeometry (no depth-stencil). Everything else applied by itself (0012, 0014 with git's 3-way
merge). The committed files re-applied in file-name order onto v3.0.1 give exactly the rebased tree.

Versions: GetInfo says 1.6. The combo's "1.5-<feature>" parts are separate interfaces found with QueryInterface (HDR, geometry,
readback) or by MaxSlots (sm6), so no NVR test reads them from the minor version. Every NVR minor-version test still means what
its feature needs: passes >= 1, replacements >= 2, mip samplers >= 3, DrawShifted >= 4, profiling >= 5, stencil >= 6. With the
older combo fork (reports 1.4) the port line's profiling and stencil stay off by themselves; with the 0011 fork (1.6, no HDR /
geometry / readback interfaces) the combo's features say once that they are missing and do nothing.

No dxvk.conf is needed (the 10-26 combo folder had none: RESZ comes from 0007).

## For the smoke test (check these first)
1. The merged DrawImpl: SMAA with NvrDxvkStencil (DrawDepthStencil) and GPU particles in rain (DrawGeometry) in the same run.
2. F10 profiling ("DXVK PROFILE" lines) with HDR output on: 0008 and 0012 both edit d3d9_swapchain.cpp.
3. Pixel inspector / shader counters (0016's storage buffers in the pass stages) together with 0009's suspended passes.
4. AmbientLight Vulkan-first (the flicker fix) with the combo's features on: managed textures still uploaded.
