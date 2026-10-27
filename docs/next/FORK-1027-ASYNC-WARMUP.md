# Fork 2026-10-27: async compute and pipeline warm-up (deep/fork-async)

Two ideas for the NVR DXVK fork, checked against the DXVK v3.0.1 source and NVR's own timings. No game runs (bulk rule).

## Short answer

| idea | verdict | gain | cost to build | real risk |
|---|---|---|---|---|
| (a) async compute for the post chain | **NO-GO** | ~0 ms on the GTX 1070; at best ~0.1-0.3 ms on AMD | 25-40 session-hours | sync bugs = flicker/corruption that only shows on some GPUs |
| (b) pipeline warm-up | **measure first**: DXVK 3.0.1 already does most of it | unknown, likely small (tens to a few hundred ms per walk) | logger done (patch 0017, ~1.5 h); a real warm-up 6-10 h | logger: none (off by default). Warm-up: 32-bit address space |

Built: fork patch **0017** `nvr.pipelineLog` (measurement only, off by default) + dxvk.conf template + bulk list.

## (a) Async compute

### What is compute today and what post costs
- Fork compute shaders (`src/hlsl/NewVegas/Compute/*.comp`): AvgLuma, AvgLumaHistogram, AmbientLightBlur/Smooth (off: measured
  slower), BloodFluidSim, DoF bokeh scatter, FSR luma mip/prep, GPU particles sim, HiZBuild (off: measured slower), PixelInspect
  (debug). **Every other NVR pass is a full-screen fragment pass** (extension 1.1), not compute. At defaults the compute share of
  a frame is ~0.03-0.1 ms (average luma 0.03, bokeh 0.03-0.10 when on).
- Post cost per frame, F10 GPU timers, slot C 1080p, combined fork (run `bench\autotest\20261026-162634-slotC`, og-desert):
  frame 6.80 ms = world scene 2.46 + pre-scene 0.62 (incl. sun cascades 0.35) + water reflections 0.25 + **NVR post ~3.5-3.8 ms**
  (AL 0.78, clouds 0.48, contact shadows 0.44, SMAA 0.39, fog 0.38, game image space 0.31, luma/exposure/bloom 0.30, normals 0.12,
  sun far 0.10, cinema 0.10, depth combine 0.09, DoF 0.08, dither 0.07, resolves 0.06, rest < 0.05 each).
  Lucky 38 (sweep report): frame 8.45 = world scene 4.28 + pre-scene 0.55 + point-shadow cubemaps 0.54 + post ~3 ms.
  Both scenes are GPU-bound (fork profiling 2026-10-26), so a real GPU saving would show as fps.

### Does DXVK 3.0.1 have a compute queue?
No. `DxvkDeviceCapabilities::enableQueues` (dxvk_device_info.cpp) opens three queues: **graphics** (graphics+compute family),
**transfer** (a transfer-only family; falls back to an async-compute family only to use it FOR TRANSFERS) and **sparse**.
`DxvkCmdBuffer` has ExecBuffer/InitBuffer/InitBarriers on the graphics queue and SdmaBuffer/SdmaBarriers on the transfer queue
(uploads only). All rendering and all NVR passes run on the one graphics queue. Resources are created CONCURRENT between the
graphics and transfer families only (`DxvkSharingModeInfo`).

### What it would take
1. A compute-queue command list type + context in DXVK (the D3D9 front end records everything on one CS thread into one context).
2. Cross-queue ordering: timeline-semaphore signal/wait per submission in `DxvkSubmissionQueue` (graphics -> compute after the
   scene; compute -> graphics before the HUD).
3. Resource sharing: CONCURRENT sharing for every image the passes touch (scene colour, depth, NVR targets), or queue-family
   ownership release/acquire barriers on both queues for each one, every frame.
4. The passes themselves rewritten from fragment to compute shaders (only ~10 of ~100 passes are compute today).

### Why the overlap is not there
- The chain inside one frame is strictly serial: scene(N) -> post(N) -> the game's HUD/menus(N) drawn onto the post result ->
  Present(N). The game draws its HUD into the back buffer after NVR's post, so the graphics queue must wait for post(N) before
  HUD(N): it idles during an async post anyway.
- "Overlap with next frame's geometry" needs scene(N+1) to start while post(N) runs. The game records frame N+1 only after
  Present(N), and Present(N) comes after HUD(N), which needs post(N). Breaking that needs the HUD redirected into its own target
  and composited later (+1 frame of latency, game-side HUD rendering changed) = out of scope.
- What could overlap inside a frame: work that needs only last frame's data (cloud march 0.29 ms quarter-res temporal, AL
  accumulate 0.06) during the depth-only passes at the frame start (sun cascades 0.35, point-shadow cubes 0.54). Upper bound
  = the smaller of the two, ~0.3-0.5 ms, times how well the GPU runs them side by side.
- GTX 1070 (Pascal): no fine-grained concurrent graphics+compute; async compute on Pascal is dynamic load balancing between
  whole-GPU partitions and usually gives 0 % or a loss. RDNA/GCN do run it concurrently (typical 3-10 % on tuned engines).
- Realistic gain: **~0 ms on the user's GPU, 0.1-0.3 ms on AMD**, for 25-40 session-hours and a new class of sync bugs
  (missing semaphore/ownership = flicker on one vendor only, hard for testers to describe). NO-GO. If speed is wanted, the
  per-pass work (preshaders, stencil, half-res) has paid off far better (point shadows -0.3 ms, SMAA stencil -0.18 ms).

## (b) Pipeline warm-up

### What DXVK 3.0.1 already does (source-checked)
- **Shader translation is cached on disk across sessions** (new in 3.0: `DxvkShaderCache`, `%LOCALAPPDATA%\dxvk\<hash>.dxvk.lut/.bin`,
  off with `DXVK_SHADER_CACHE=0`). D3D9 shaders (`D3D9CommonShader`) look the IR up by name before translating. So the "D3D9 ->
  SPIR-V translation" cost is paid once per shader per install, not per session.
- **No state cache any more** (removed in DXVK 2.x; `dxvk.enableStateCache` is gone). Replaced by **graphics pipeline libraries
  (GPL)**: every shader is registered at creation (`DxvkPipelineManager::registerShader`) and its pipeline library is compiled on
  the worker threads right away. FNV and NVR create their D3D9 shaders at startup, so this is effectively a built-in preloader.
  At first draw DXVK fast-links (cheap) and compiles the optimized pipeline in the background (low priority).
- **32-bit lifetime tracking** (`dxvk.trackPipelineLifetime = Auto` = ON for a 32-bit game on NVIDIA): the library compiled at
  load is DESTROYED right after compiling, only to warm the driver's disk cache; at first use it is re-created from the cache by
  shader-module identifier (`compileShaderPipelineLocked`) on the recording thread. Small per pipeline, but once per new shader.
- The NVIDIA Vulkan driver keeps its own disk cache across sessions (the native D3D9 driver does not: "native driver keeps nothing
  between launches", warmup2).

### Where waits can still come from under DXVK
1. Graphics pipelines that **cannot be fast-linked** (`canCreateBasePipeline` false: vertex shader reads inputs the layout lacks,
   fragment inputs the vertex shader does not write - common with FNV's shader/state mixes -, flat shading, dual-source blending,
   non-identity render-target swizzles). These are compiled in full on the recording thread at first use. This is exactly what
   the old state cache covered.
2. **NVR's own extension passes**: never pipeline libraries (patch 0004: push block > 48 bytes), and NVR creates them lazily at
   their first draw (`EffectRecord::ExtTexturePass`/`ExtPass` Tried flags) -> compiled in full at first draw. Interior-only and
   exterior-only passes therefore compile at the first door / first outdoor frame of a session (driver disk cache makes later
   sessions cheaper, not free).
3. Library re-creates caused by the 32-bit lifetime tracking above ("graphics linked" in the log).
4. NVR HLSL (fxc) compiles: at startup / when an effect or a compiled-in setting changes, cached in `Shaders\...\Cache`; not an
   arrival cost in normal play.
5. The game's own loading (IOManager queue, textures on the main thread, sync file reads): ~3 s of native's 4.8 s walk hitch
   (arrivalhitch 2026-10-20); no shader warm-up touches it (IOWorker2 does).

### Earlier numbers to compare
warmup2 walk (2026-10-19/20): native 4.8 s hitch, native + preloader 2.5 s, DXVK 1.8 s (DXVK 2.6.1 then; 3.0.1 adds the IR cache,
so likely lower now). The native preloader removed ~83 % of compile waits; DXVK's GPL + disk caches are the same idea built in,
which is why DXVK already matched or beat native + preloader. DXVK's remaining 1.8 s includes the game's non-shader arrival
hitches (Primm 210-535 ms seen under DXVK too). How much of it is still pipeline compiles is **not measured** -> patch 0017.

### Built: patch 0017 `nvr.pipelineLog` (measurement only)
- dxvk.conf `nvr.pipelineLog = True` (default False), `nvr.pipelineLogMinUs = 1000`. Template:
  `tools/nvr-dxvk/debug/dxvk.conf.pipelinelog`.
- Times every first-use pipeline creation on the thread that needs it: `DxvkGraphicsPipeline::getPipelineHandle` new instance
  (kind "graphics linked" = fast link incl. library re-create, "graphics full" = complete compile) and
  `DxvkComputePipeline::getPipelineHandle` first library acquire / spec-constant variant ("compute"). Worker-thread compiles are
  not counted. Lines `NVR pipeline: <kind> <ms> ms: <vs> + <fs> (session <kind>: N waits, X ms; all kinds Y ms)` in DXVK's log.
- Files: new `src/dxvk/dxvk_nvr_pipelog.h`; `dxvk_options.{h,cpp}`, `dxvk_graphics.cpp`, `dxvk_compute.cpp` (a few lines each,
  inside `if (nvrLog)`). No extension API change; no other patch touches these files (numbered 0017 to stay clear of the
  bindless agent's 0016; in deep/fork-combo-1027's renumbered series it simply goes last).
- Built by CI (run 40530960342, PASS): `G:\_NVR-build-scratch\nvr-dxvk-async\d3d9.dll` (sha256 8acc67e9..., = the fork-combo-1026
  series 0001-0015 + 0017) with `dxvk.conf` = the pipelinelog template next to it. For deep/fork-combo-1027 the patch goes last.

### What decides the next step (bulk list `docs/next/async-bulk.txt`)
Launches W1 cold / W2 warm / W3 warm + `dxvk.trackPipelineLifetime = False` on the warmup2 walk, I1/I2 on lucky22. Rule written
now: **W2 sync-compile total under ~150 ms per walk = no warm-up for DXVK, stop.** Over that:
- mostly "graphics full" on game shaders -> a fork-side state list (record the state vectors of full compiles to a file, replay
  them on worker threads at device start, the old state cache scoped to non-linkable pipelines): 6-10 h.
- mostly NVR ext passes -> NVR creates all ext pipelines at load + the fork compiles them on a worker with the recorded target
  formats/blend: 4-6 h.
- mostly "graphics linked" -> `dxvk.trackPipelineLifetime = False` (stock option, zero code) if W3/I2 run without memory trouble.
Risk for any warm-up: 32-bit address space (keeping more pipelines alive in a 4 GB process already full of textures); a wrong
state key only wastes a background compile, it cannot change the picture.

## Results (2026-10-27 bulk, slot C, fork d3d9 8acc67e9, NVR-fork-combo DLL, NvrDxvkPasses + ArrivalSampler on)

Session totals of pipeline compiles the frame waited for ("NVR pipeline:" lines in FalloutNV_d3d9.log):

| launch | cache | graphics linked (library re-create) | graphics full | total |
|---|---|---|---|---|
| W1 noon walk | cold (DXVK cache moved aside) | 354 waits, 226 ms | 13 waits, 4.6 ms | ~231 ms |
| W2 noon walk | warm (W1's cache) | 358 waits, 393 ms | 13 waits, 11.3 ms | ~404 ms |
| W3 noon walk | warm + trackPipelineLifetime = False | NOT RUN: the user started the real game | | |
| I1 lucky22 | warm | 131 waits, 67.5 ms | 0 | ~71 ms |
| I2 lucky22 | warm + trackPipelineLifetime = False | 109 waits, 14.4 ms | 3 waits, 3.0 ms | ~18 ms |

- Rule applied: the WARM walk (W2) is ~400 ms, over the 150 ms line, and almost all of it is "graphics linked" = the 32-bit
  lifetime tracking re-creating libraries at first use (each wait ~1-5 ms, one 25 ms). Full compiles (what a state-list
  replay would fix) are only ~11 ms, and NVR's own passes are a handful of ms. So: no warm-up code. The fix is the stock
  option `dxvk.trackPipelineLifetime = False` (I2: linked waits 67 -> 14 ms indoors, no errors in the log).
- Open: W3 (the walk with the option) to confirm the walk drops too, and a long session with it to watch for 32-bit
  out-of-memory. Cold vs warm being worse warm is odd (W1 compiled libraries at load and some were still alive); not chased.
- Slot setup: NVRAutoTest8 (scene noon) added to multirun slim.json and slim-C.json saves.
