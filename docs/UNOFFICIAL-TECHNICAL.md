# UNOFFICIAL Optimized build: technical notes

Plain-language overview: see the top of [README.md](../README.md). This page lists what was changed
and how, for anyone who wants the details. Measurements are from one PC (GTX 1070, 2560x1440,
native Direct3D 9) using the built-in F10 profiler; other hardware will differ.

## Effect chain and copies
- **Frame chain.** Effects used to render into the game's target and then copy the whole frame
  into `TESR_RenderedBuffer` so the next effect could read it. Each chain (pre- and post-tonemap)
  now alternates between two textures and swaps which one is "current"; samplers follow the slot
  (`TextureRecord::TextureRef`). Passes that clip, discard, blend or use stencil are detected from
  the preprocessed source and get their destination pre-filled, so output is unchanged.
  The pre-tonemap chain uses the game's own single-level target as one of the two buffers.
- Effects that never sample `TESR_SourceBuffer` no longer refresh it (the distant-only DOF mode reads
  `TESR_RenderedBuffer` instead, so it makes no full-frame copy).
- **Direct final pass.** The post-tonemap chain works in NVR-owned textures because the back buffer
  cannot be sampled. The effect that will render last is predicted (`FrameChain::SetFinalEffect`) and
  its last pass renders straight into the game target, so the chain has no copy-back to do. If another
  effect renders after all, `FrameChain::Owns` restores the image first (one copy, never a wrong image).
- **Identity LUT skip.** A colour-grading LUT whose texels are checked at load time to be an exact
  identity (the shipped `neutral_lut.png`) is not applied; a real LUT always is.
- **Composite apply.** The exterior sun-shadow composite and the AO combine are applied inside the
  volumetric-fog reconstruct pass (same maths and order) instead of as two separate full-resolution
  passes, falling back automatically when an effect that runs between them is active.

## Individual effects
- **Ambient occlusion:** dedicated half-resolution `G16R16F` ping-pong targets; both kernels
  evaluated together; depth-aware four-sample upsample. (About 2.3 ms -> 0.5 ms.)
- **Volumetric fog:** half-resolution estimate written as multiply/add coefficients to two MRTs,
  applied to the full-resolution scene (no scene detail lost); closed-form height-fog integral
  instead of a 32-step loop.
- **God rays:** dedicated half-resolution targets, early exit in the radial march, attenuation
  at reduced resolution. (About 1.6 ms -> 0.5 ms.)
- **Contact shadows:** ping-pong through scratch targets (removes a read/write feedback loop),
  N.L fade and distance-scaled bias (fixes horizontal black lines), blur skips depth reads when all
  taps are equal.
- **Depth buffer:** single `R32F` channel; the post-projection value is recomputed in the shaders
  (`projectedDepthFromLinear` in `Includes/Depth.hlsl`).
- **Interior point-light shadows:** the deferred shadow apply blurs the shadow term on the two
  `G16R16` scratch targets and touches the HDR frame only in its last pass (was: a full-frame copy,
  three HDR clears and two HDR blurs); the lighting pass skips the cubemap read for lights that cannot
  reach a pixel (their contribution is exactly zero beyond the radius). Optional
  `PointShadowInterval` redraws each light's cubemap every N frames (a cubemap stores distance from
  the light, so it is camera independent; it is redrawn at once when its light, position, radius or
  cell changes).
- **SMAA:** cheaper combined edge detection, explicit stencil state. **DitherBuster:** single pass.
  **Point shadows:** merged passes.
- **Shadow cascades:** staggered refresh (middle every 4 frames, far/LOD every 8, spread over the
  8-frame cycle), only updated cascades resolved/prefiltered, cheaper face culling, batched bone
  uploads.

## In-game switches (defaults file `NewVegasReloaded.dll.defaults.toml`)
Settings only exist if they are present in the defaults file, so always install it with the DLL.

`[_Main.Main.Performance]` - same image, on by default:
`FrameChain`, `CompositeApply`, `ChainUsesGameTexture`, `WorldSceneGuard`, `SlimDepthBuffer` (restart required).
`MergedDepthNormals` is off by default: on a GTX 1070 (native D3D9) the driver silently skipped the
two-render-target draw, leaving depth and normals stale. Only enable it to test, and check that
the F10 `Depth combine` timer then reads above 0 ms.

`[_Main.Main.ReducedQuality]` - change the image, off by default (except `PointShadowInterval`), apply immediately:
- `FXAA` - lean console-style FXAA (after FXAA 3.11), 5 texture reads per pixel, 9 on edges, after
  SMAA. Measured 0.14-0.16 ms vs SMAA 0.9-1.2 ms; meant to replace SMAA.
- `GodRaysLowRes` - god rays at quarter instead of half resolution.
- `AOLowRes` - ambient occlusion at quarter instead of half resolution.
- `PointShadowInterval` (1-4, **default 2**) - redraw point-light shadow cubemaps every N frames; 1 = every frame.

## World scene guard and trace
NVR refreshes its depth buffers only inside its hook on the game's `RenderWorldSceneGraph`. Once, after
a save was loaded straight into an interior, the game rendered frames without calling it (the F10 log
showed no `World scene (game)` / `Depth resolves` samples and `Pre-scene` equal to the whole frame), so
the effects ran on a stale depth buffer and the image came out almost black until another cell was
entered. It did not happen again in later tests, and the cause is not known. With `WorldSceneGuard` on,
NVR's effects are skipped after 10 consecutive frames without a world scene render (never on the main
menu or a loading screen) and resume as soon as one happens. `WORLD TRACE` lines in the log (at most 80
per session) record which render calls ran on the first frames after each cell change, to help find the
trigger if it recurs.

## Profiler
F10 toggles GPU/CPU timing (a small red dot and "PROF" appear in the top-right corner while it runs). Every 120 frames the averages are written to `NewVegasReloaded.log` as
`GPU PROFILE ...` and `CPU PROFILE ...` lines; `Frame interval (CPU)` is the real frame time.
Queries are read asynchronously and never flush the GPU. Only the two timestamp queries are required; the
optional disjoint and frequency queries may be missing or fail (timing continues, assuming a 1 GHz clock), so
the timers also work under DXVK (checked with DXVK 2.6.1). Failures are logged with their HRESULT. Indented names are sub-timers nested in the
line above them (contact shadow passes, fog estimate/composite, god-ray passes, exposure/bloom, chain
end copies, interior shadow blur/apply). With profiling on, `POINT SHADOWS ...` lines report how many
point-light cubemaps are redrawn per frame.

### Frame-time statistics
While profiling, every 1200 frames (and when F10 is pressed again, if at least 200 frames were collected) the log
gets a `FRAME TIMES` line: average, p50 / p95 / p99 / p99.9 / max frame time, the 1% and 0.1% low frame rate (the
average fps of the slowest 1% / 0.1% of frames) and a spike count. A second line repeats the p99 and lows for
steady play only, leaving out frames within three seconds of a cell change or loading screen. A frame much slower
than a typical one is logged as `FRAME SPIKE`, with the NVR CPU timers that were slow in it (at most 60 per
session). These use only CPU timing, so they work identically on native Direct3D 9 and under DXVK. The startup
log names the runtime in use: `D3D9 runtime: DXVK (...)` or `system Direct3D 9 ...`.

## Log markers
Lines starting with `UNOFFICIAL` report which optimized paths are active, e.g.
`UNOFFICIAL frame chain ...`, `UNOFFICIAL composite apply active ...`,
`UNOFFICIAL dedicated AO active ...`, `UNOFFICIAL depth buffer: R32F (single channel)`.

## Building
Visual Studio 2019 (v142), Release | Win32, DirectX SDK (June 2010). Effect shaders are compiled at
runtime by the game from `Data\Shaders\NewVegasReloaded\Effects\`; delete the `Cache` folder there
if a shader change does not seem to apply.
