# NVR DXVK: the NVR extension, proof of concept (2026-10-23)

Plan: DXVK-FORK-PLAN.md. Why not the public interop: DXVK-INTEROP-REVIEW.md. This note: what was built, what was verified
here, what the owner has to test in game, and how NVR talks to it.

## In one paragraph
An NVR-built DXVK d3d9.dll ("NVR DXVK": DXVK v3.0.1 + one patch) offers a small COM interface on the D3D9 device,
`ID3D9NvrExt`. Through it NVR creates compute pipelines from SPIR-V, binds D3D9 textures and its own buffers, pushes
constants and dispatches. Every call is recorded into DXVK's own command stream (the CS thread), so the compute work runs
in order with the D3D9 draws before and after it, and DXVK does the image layouts, barriers and resource lifetimes. No CPU
wait, no second Vulkan queue, no semaphore juggling. The first user is the average-luminance pass (AvgLuma): a two-phase
shared-memory reduction that reads every pixel once (exact) instead of the D3D9 pass's 112 bilinear taps, with the rest of
the pass's logic (8-frame average, adaptation speeds, depth-of-field focus) kept line for line. Native D3D9 and stock DXVK
are unchanged: the option does nothing there and the D3D9 pass runs as before.

## What is where
NVR repository, branch `nvr-dxvk-compute` (off combo-1023):
- `tools/nvr-dxvk/patches/0001-*.patch`: the fork, as one patch on DXVK's v3.0.1 tag. `tools/nvr-dxvk/build.sh` clones,
  patches and builds the 32-bit d3d9.dll (Linux/WSL, MinGW-w64). `.github/workflows/nvr-dxvk.yml` does the same on GitHub
  (Actions tab, "NVR DXVK d3d9.dll", Run workflow; the artifact `nvr-dxvk-x32` holds the DLL). `tools/nvr-dxvk/test/`:
  the end-to-end test program (see Verified).
- `src/core/NvrDxvkExt.h`: the interface (identical to the patch's `src/d3d9/d3d9_nvr_ext_api.h`, except line endings).
- `src/core/DxvkCompute.{h,cpp}`: NVR's wrapper: finds the extension at start (logged), loads `.spv` files, creates
  pipelines/buffers/samplers, fails once and stays off for the session.
- `src/effects/AvgLuma.{h,cpp}`: `RenderCompute` (the compute path) and `CheckCompute` (the test tool).
  `src/hlsl/NewVegas/Compute/AvgLuma.comp` (GLSL) and `AvgLuma.spv` (built from it; what NVR loads).
- `src/core/ShaderManager.cpp` (RenderEffectsPreTonemapping): compute first, the D3D9 pass when it is off or fails.
- Settings (`resource/NewVegasReloaded.dll.defaults.toml`, read in SettingManager.cpp), both live:
  `[Main.Main.Performance] AvgLumaCompute = false` (needs NVR DXVK) and `AvgLumaComputeCheck = 0` (test tool).

The fork: 7 files, +1397 / -1 lines. New files: `d3d9_nvr_ext_api.h` (266 lines, the interface), `d3d9_nvr_ext.h` (192),
`d3d9_nvr_ext.cpp` (911). Edits to DXVK's own files: 26 lines in all: `d3d9_device.h` (member, friend, include),
`d3d9_device.cpp` (constructor, QueryInterface), `src/d3d9/meson.build` (the new source, the version header),
`dxvk_context.h` (two small methods: a snapshot/restore of the push-data state). Rebasing onto a newer DXVK should be
a `git am` plus, at worst, a look at those four spots. DXVK's version string becomes `v3.0.1-nvr1` (its log and HUD).

## The interface (NvrDxvkExt.h)
`IDirect3DDevice9::QueryInterface(__uuidof(ID3D9NvrExt))`: E_NOINTERFACE on native D3D9 and stock DXVK.
- `GetInfo`: versions, limits, the GPU's subgroup size.
- `CreateComputePipeline(SPIR-V, bindings, push size)`: the bindings list what the shader declares (set/binding as in the
  GLSL; DXVK remaps them) and which NVR slot each uses. Types: sampled image, storage image, storage buffer, uniform buffer,
  sampler (via DXVK's sampler heap: the shader declares `uniform sampler s_samplers[]` and reads the index from its push block).
- `CreateBuffer`, `CreateSampler`, `UpdateBuffer`, `ClearBuffer`, `CopyBuffer`, `CopyBufferToTexture`, `CopyTextureToBuffer`:
  all recorded, no waits.
- `Dispatch(pipeline, resources per slot, push data, groups)`: the D3D9 textures bound may be render targets of earlier
  passes (DXVK orders and transitions them); a texture must not be the device's current render target at the time.
- `ReadBuffer`: TEST ONLY, waits for the GPU (a whole frame's work).
Slots are the extension's own (DXVK resource indices 64+), so a dispatch never touches what the game or NVR has bound in
D3D9. Push block: up to 240 bytes, the shader's block starts at offset 0.

Two things inside DXVK that mattered:
- Push data is one shared state for graphics and compute (D3D9's fog colour, alpha reference and sampler indices live
  in it). The dispatch saves it before and restores it after (the `dxvk_context.h` edit), otherwise the next D3D9 draw
  would push stale values.
- D3D9 images are created without storage usage; a storage-image binding asks DXVK to add it (`ensureImageCompatibility`:
  the image is relocated once). AvgLuma does not need it: it writes a buffer and copies 8 bytes into the 1x1 texture.

## How NVR uses it (AvgLuma)
Per frame, when `avglumaRequired` (Exposure, DepthOfField or Debug on):
1. phase 0: one 16x16 workgroup per tile of the chain's current image (`TheTextureManager->RenderedTexture`, what the
   D3D9 pass samples as TESR_RenderedBuffer), each writing one partial sum of `luma(linearize(rgb))` (Helpers.hlsl's).
2. phase 1: one workgroup sums the partials, then runs the pass's own code: `newLuma = (avg + 7 * old.r) / 8`, the
   `stepTo` adaptation with TESR_ExposureData.zw and TESR_GameTime.w, the focal distance from 13 depth taps
   (TESR_DepthBuffer = CombineDepth's texture, TESR_MotionBlurData, TESR_DepthOfFieldData, TESR_CameraData), reading last
   frame's result from a ping-pong state buffer (as the pass reads its own texture).
3. 8 bytes (four half floats) are copied from the state buffer into TESR_AvgLumaBuffer. Exposure, DepthOfField, GodRays
   and Debug read the texture as before; nothing else in NVR changes.
The push block carries the five float4 constants (112 bytes); the samplers are LINEAR/CLAMP like the pass's.
Cost: one read of the frame plus a tiny second dispatch (phase 0 at 2560x1440 is 14,400 groups); the D3D9 pass costs
112 taps and a 1x1 draw, so this is a measurement of the route, not a saving. F10's "Average luma" timer covers either path.

## Verified here (Linux container, no game)
- Stock DXVK v3.0.1 builds from source as the release does (32-bit, MinGW-w64 gcc 13, Meson 1.9, glslang 15):
  `d3d9.dll`, PE32 i386. The patched tree builds the same way (`tools/nvr-dxvk/build.sh` was run from a clean clone).
- End to end on dxvk-native (the Linux build of the same source) with lavapipe (software Vulkan), `tools/nvr-dxvk/test/
  nvr_ext_test.cpp`: a 67x41 RGBA16F "frame" and an R32F "depth" filled by D3D9 clears, then the real `AvgLuma.spv`:
  three frames of phase 0 + phase 1 + copy to the 1x1 texture + a D3D9 clear + Present, then readback. The compute
  results match the D3D9 pass's arithmetic to 1e-6 (luma 0.010450 / 0.019594 / 0.027595 over the three frames, focus
  0.271895), and the 1x1 texture read back through GetRenderTargetData holds the same values as half floats. Bad input
  (an undeclared slot, truncated SPIR-V) fails cleanly with a log line.
- Vulkan validation layers: no errors on DXVK's legacy descriptor path (`dxvk.enableDescriptorHeap = False`,
  `dxvk.enableDescriptorBuffer = False`). On the default path (descriptor heap) the layer available here (1.3.275) is older
  than the extension DXVK 3.0 uses and reports "set not bound" for DXVK's own draws as well as ours: the layer's
  blindness, not a finding, but the heap path is only verified by the results, not by a layer.
- The GitHub workflow built the DLL from the branch: Actions run "NVR DXVK d3d9.dll" of 2026-10-23, artifact
  `nvr-dxvk-x32` (d3d9.dll 9.9 MB; the same size as the local build).
- NOT compiled with MSVC: `DxvkCompute.cpp` passes a MinGW syntax check, but the repository's "Build New Vegas Reloaded"
  workflow never starts (its `windows-2019` runner no longer exists on GitHub; the runs of every branch sit queued), so
  the NVR-side C++ (AvgLuma, Normals, DxvkCompute, the ShaderManager/SettingManager edits) and the new HLSL technique
  were reviewed, not built. The owner's VS2019 build is the first compile; expect at most small fixes.

## NOT verified: what the owner must test in game
1. Install the fork's d3d9.dll (from the Actions artifact, or build.sh) next to FalloutNV.exe in a DXVK slot, with the
   usual dxvk.conf. Start the game with the branch's NVR DLL. The log must say
   `UNOFFICIAL NVR DXVK: NVR DXVK extension 1.0 on DXVK v3.0.1-nvr1 ...: compute paths available`, and DXVK's own log
   `DXVK: v3.0.1-nvr1`. With stock DXVK or native D3D9 the line says the extension is not present; everything else must
   look exactly as before (the branch adds no other behaviour).
2. The fork without the option (AvgLumaCompute off): play as usual for a while; it must behave like stock v3.0.1 (the
   patch adds nothing to the D3D9 paths, but a different build of DXVK is a different build).
3. `AvgLumaCompute = true` (menu: Main > Performance; its description says "needs NVR DXVK"): the log gets
   `AvgLuma: compute path ready` and `compute path running`. Exposure and depth-of-field focus must behave as before
   (toggle the option live and compare: the Debug effect shows the texel at the right edge, 0.55-0.6 down). Expect a small
   difference in level: the exact average vs 112 samples. If anything fails, the log says so and the D3D9 pass takes over.
4. `AvgLumaComputeCheck = 60` (test tool; hidden unless the test switches are shown): every 60 frames a line
   `AvgLuma check (frame N): compute exact average A -> luma ... | D3D9 112 taps: luma ... | diff luma ...`. The diff is the
   sampling error of the old pass; the go/no-go number. Those frames stall (ReadBuffer waits): set it back to 0 afterwards.
5. F10 timings: "  Average luma" with the option on and off.
6. Alt-tab, a device reset (fullscreen toggle), resolution change (the buffers are re-made when width/height change),
   ReShade chained, and the game's loading screens: nothing crashes, no DXVK log errors (`NVR ext:` lines).
Unknowns to watch: the `ensureImageCompatibility` path is unused by AvgLuma (no storage images yet); the push-data
restore has been exercised only by the test's clears, not by the game's fog/alpha-test draws (a wrong fog colour or alpha
test right after the AvgLuma pass would point at it); the win32 DLL was built, not run.

## Next candidates (after the go/no-go)
Full-screen graphics passes with own SPIR-V (the next extension method), then AmbientLight's blurs/accumulate, the light
grid, froxel fog, the cloud march, BloodFlow, reflections (DXVK-FORK-PLAN.md step 5). Each keeps its setting names; NVR
picks the compute path when the extension exists.

## Also on the branch: shared smoothed normals (goal 2)
`[Main.Main.Quality] SmoothNormals = 0` (reach in pixels, 0 off, live; try 72). Every effect rebuilds its normal from the
depth buffer (Normals.fx.hlsl), one flat normal per triangle on low-poly ground and rocks: faceted cloud shadows (SunFar),
face shadow offsets (SunShadows), snow (SnowAccumulation). With a value above 0, technique `NormalsSmooth` in
Normals.fx.hlsl smooths TESR_NormalsBuffer in place right after the normals pass (pass X into a spare full-resolution
texture, pass Y back), the same edge-aware averaging AmbientLight's NormalSmoothing uses (3 taps each side at r/3, 2r/3, r;
a tap counts as far as it lies on the pixel's tangent plane and its normal is within ~40-60 degrees), so SunFar (which
runs before AmbientLight's prepare) and every other reader get it. AmbientLight then skips its own half-resolution
smoothing (its NormalSmoothing setting is untouched; it simply has nothing left to do). Native D3D9 and DXVK alike.
To test: a low-poly rock or terrain under cloud shadow at 0 vs 72; F10 "  Normal smoothing (shared)" (expected 0.2-0.3 ms
at 1440p); AmbientLight on with both: the log says `SmoothNormals: on (... AmbientLight's own NormalSmoothing skipped)`.
Shader file to copy: `Shaders\NewVegasReloaded\Effects\Normals.fx.hlsl` (clear `Effects\Cache\` if it does not take).
