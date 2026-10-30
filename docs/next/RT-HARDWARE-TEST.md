# RT on real hardware: checklist and test list (2026-10-30)

Everything in the RT features so far (sun shadows, reflections, lamp shadows, AO / sky / GI, the shared denoiser) was developed on
the 9800X3D's iGPU ("AMD Radeon(TM) Graphics", RDNA2, 2 CUs) at 1280x720. The iGPU proves the features work; it does not say what they
cost on a real RT card. This is what the first tester with an RTX 20xx or newer / RX 6000 or newer should run and send back, so the
numbers can be compared run to run and card to card.

## 0. What you need
- The NVR DXVK fork's d3d9.dll with the RT patches (0001-0029 + 0033; the RT test package ships it) in the game folder, and
  `Data\NVSE\Plugins\NvrRt\NvrRtHelper.exe` (protocol 3) next to NVR. Windows 64-bit, a current driver.
- The game must run ON the RT card (NVR's log: "Graphics adapter as reported to the game: ..."). The helper always takes the game's own
  GPU (adapter LUID) and stays off on a GPU without ray tracing; it never borrows another GPU.
- F10 once in game turns on NVR's GPU profiler (the "GPU PROFILE" lines, every 120 frames). The helper writes its own per-pass times
  ("RT helper: pass GPU times") into NVR's log every 120 frames by itself.

## 1. The helper started on the right GPU (no measuring yet)
Turn one RT effect on (`[Shaders.ShadowsExteriors.RT] Enabled = true`), load an exterior, look for these lines in NVR's log:
- `RT helper: ready: ray tracing on <your card>, the game's GPU (found by adapter LUID; N shared images, arena 4096x1024)`
- `RT helper ready after N ms` (expect well under 1 s).
- No `ray tracing core off for this session: ...` line. If there is one, send it: it names the reason.
Also send `Data\NVSE\Plugins\NvrRt\NvrRtHelper.log` (the Vulkan devices it saw, which one it took).

## 2. Settings for comparable numbers
Same as the iGPU benches unless a step says otherwise: 1920x1080 (also 2560x1440 if you can), FSR off, the run toml's heavy effects
off (volumetric clouds, AmbientLight, volumetric fog), sunny weather (`fw 000FFC88`), GameHour 16, time frozen (`sgtm 0.001`), stand at
Goodsprings (og-goodsprings spot) facing the town. Let each setting settle 300 frames before reading a window.

| Step | Settings (on top of the base) | What to read |
|---|---|---|
| A | RT sun `Enabled` true, everything else default | "Ray tracing (copies + helper + waits)", helper "RtSunShadow.spv" pass time, RT CORE line at exchange 60 / 600 |
| B | A + `[Main.Main.RayTracing] AlphaTested` true | same; the "alpha-tested meshes met" line |
| C | B + `Actors` true | same; the "actors: N skinned partitions" line; the helper's "refits in place / full builds" line |
| D | C + `HitTextures` true | same; atlas slots in the alpha line |
| E | D + `SunAngle` 0.53, `Denoise` true | + "RT denoise: RT sun shadows" |
| F | E + `HalfResolution` true, `DenoiseKernel` 3 | same |
| G | F + each other RT effect on, one at a time (reflections, lamp shadows indoors, AO / sky / GI) | each effect's own GPU PROFILE line + the helper's pass time for its shader |
| H | everything on (the "max RT" case) | the frame time (FrameTimeStats line), all of the above |

The bench list `bench/foundations/lists/rt-hw-1.txt` does steps A-F with shots (`profile` on); G and H are by hand.

## 3. What to send back (one block per step)
```
card / driver / resolution:
step:
Ray tracing (copies + helper + waits) p50 / p90:
helper pass times (shader: ms a run):
RT denoise p50 (if on):
RT CORE at 600: instances, geometries, triangles, MB, helper GPU avg (builds / scene / passes):
frame time p50 / p99 (FrameTimeStats):
anything wrong in the picture (DebugView 4 shot of the sun shadows, DebugView 6 of the scene):
```
Plus the helper's log and NVR's log of the session.

## 4. Things that only a real RT card can answer
- The pure trace cost per ray at 1080p (the iGPU's numbers are mostly waiting: two processes share one small GPU).
- Whether the helper's exchange (the game waits for the helper each frame) costs more than the rays: compare "Ray tracing (copies +
  helper + waits)" against the sum of the helper's pass times. A large gap means the cross-process wait dominates and is the next thing
  to optimize (async exchange: trace frame N while the game renders N+1).
- Memory: the RT CORE line's MB (bottom levels) + the helper's process memory (Task Manager, dedicated GPU memory) with everything on.
- Actors: the full-build vs refit counts and the "builds" time with people around (Lucky 38, Vault 21, Freeside).
- Whether `HalfResolution` + `DenoiseKernel 3` is still needed, or full resolution 5x5 is affordable.

## 5. Known limits to keep in mind while testing
- Only one exchange a frame per effect point; RT reflections adds a second one (its own wait).
- The RT scene holds the loaded cells within `SceneRadius` (8192); LOD land only where the loaded cells have none.
- Skinned actors are re-posed every `ActorUpdateInterval` (2) frames.
- The 1070 / any GPU without ray query: the helper says "no ray tracing on the game's GPU" and everything keeps its non-RT path.
