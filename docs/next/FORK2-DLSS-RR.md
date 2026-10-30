# DLSS Ray Reconstruction (2026-10-30, branch claude/dlss-rr)

Off by default: `[Shaders.TAA.DLSS] RayReconstruction = false`. With the switch off, nothing below runs and the frame is
unchanged. It needs an NVIDIA RTX card, NVR DXVK, `NvrDlssHelper.exe`, `nvngx_dlss.dll`, and NVIDIA's Ray Reconstruction
runtime `nvngx_dlssd.dll` next to the helper. Without any of these it switches itself off, logs the reason once, and the frame is
the same as with the switch off. Base: claude/motion-ring-fix aee0046c on claude/rt-test-merge a93b6f18.

## The problem and the contract

NVR's ray traced effects each trace about one ray a pixel and clean up their own noise:
- **RT sun shadows:** a 0/1 visibility; RtDenoise runs only with SunAngle > 0.
- **RT lamp shadows:** per-lamp visibility, used in the next frame's object shaders. Its Denoise switch is off by default.
- **RT AO and GI:** half-resolution signals, sent through RtDenoise, then AmbientLight's blurs and accumulation.
- **RT reflections:** the rays fill the pixels screen-space reflections miss. A reflection temporal pass and two blurs follow.

Every one of these denoisers blurs real detail and costs GPU time. NVIDIA's DLSS Ray Reconstruction (NGX feature
`NVSDK_NGX_Feature_RayReconstruction`, "DLSS-D") replaces all of them at once. It is a trained network that denoises *and*
anti-aliases or upscales the final lit picture, guided by the surfaces' albedo, normals, roughness, depth and motion.

What RR needs each frame, at render resolution:

| input | meaning | NVR source |
|---|---|---|
| colour | the noisy lit picture, linear HDR, before tonemapping, jittered | the frame chain's image after the god rays, **decoded** from the stored curve with `pow(c, Linearization)` (TESR_ToneMapping.w, 2.2). The FP16 scene target is gamma-encoded, not linear, at this point. |
| depth | device depth | FSRPrep's depth (0 near .. 1 far), the same as DLSS SR gets |
| motion | previous minus current, render pixels | FSRPrep's motion (camera + ObjectMotion; weapon 0), scale = render size |
| diffuse albedo | surface colour of diffuse light | **none exists in NVR.** v1 uses an approximation (setting RRAlbedo, below). A real one needs the templates' spare COLOR1 output (follow-up). |
| specular albedo | integrated specular reflectance | EnvBRDFApprox(F0 0.04, roughness, N·V), worked out from the normals, depth and roughness |
| normals | per-pixel surface normal | TESR_NormalsBuffer (view space x 0.5 + 0.5), turned into camera-relative world space (setting RRNormals = 1 keeps view space) |
| roughness | linear roughness | TESR_MaterialBuffer.b when `[Main.Main.Quality] MaterialBuffer` is on, otherwise the constant RRRoughness |
| matrices | world to view, view to clip | camera-relative view (rotation only, no translation) and the unjittered projection, D3DX row-vector layout. The game's projection is reversed-Z, but the depth RR gets (FSRPrep's) is standard, 0 near .. 1 far, so ViewToClip has `_33 = f/(f-n)`, `_43 = -nf/(f-n)` to match it. |
| jitter, reset, frame time, sizes | as for DLSS SR | as for DLSS SR |

Output: the display-size picture, denoised and anti-aliased. Linear HDR, RGBA16F.

## Where it runs (v1: native resolution only)

The RR point is inside `ShaderManager::RenderEffectsPreTonemapping`, after the god rays and before the average luma, exposure
and bloom. At that point:
- every RT composite has already been applied (sun, AO/GI through AmbientLight, reflections, and lamps inside the object shaders);
- so have the fog, the volumetric light, the sandstorm and the god rays. They are drawn with this frame's jittered depth, so RR
  has to resolve them with the rest, the way DLSS SR resolves them after the tonemap;
- exposure, bloom, lens and LUT all run after this point;
- the camera is unjittered and the Vulkan passes are allowed, because it is not a rect frame.

**Changed after the review (2026-10-30).** The first build put the point right after the Reflections block, before the fog. That
was wrong in two ways:
- every effect after it used jittered depth on RR's unjittered picture, and nothing resolved their edges, which shimmered along
  silhouettes;
- the fog's shadow/AO composite fold had to be turned off.

With the point after the god rays, the fold stays as it always was.

**v1 runs RR only at RenderScale 1, so the output is DLAA-quality RR.** With RenderScale below 1, NVR's v2 path draws the
effects in a rect, and in rect frames the Vulkan passes are off (DxvkCompute::PassesOn). That turns off the RT AO, RT GI and RT
reflections, while the RT sun and lamp traces ignore the rect. RR would get no RT signal worth denoising. Such frames keep DLSS
SR as before, and the log says why once. Making the RT effects work in rect frames is follow-up 1.

Each frame at the RR point (NVR DXVK passes, all at native size):
1. **FSRPrep** (existing pipeline) gives motion and depth. RR keeps its own previous camera, as RenderScale's UpscaleRun does.
   TAA's own bookkeeping comes later in the frame, after the tonemap.
2. **DlssRRGuides** (new compute pass) gives the linear colour, diffuse albedo, specular albedo, normals and roughness.
3. **DLSS helper exchange** (the existing semaphores): seven inputs are copied into shared images, the frame values are sent,
   the helper evaluates DLSS-D, and the output is copied back into TESR_DlssOutput.
4. **DlssRRComposite** (new graphics pass) writes the output into the chain:
   - encoded back to the stored curve (`pow(c, 1 / Linearization)`);
   - the first-person weapon put back exactly as it was rendered, as FSRComposite does (it is not jittered).
5. TAA's post-tonemap resolve is skipped for that frame, as for an upscaled frame. TAA still advances its jitter index.

## Per-effect denoisers while RR runs

They are decided at the start of each frame, before the RT point. That decision combines two things:
- **the previous frame's RR result,** which is only known at the RR point, after the effects. So the first RR frame is still
  denoised as before.
- **this frame's plan.** `TAAEffect::RayReconstructionPlanned` runs every frame check that is known at that moment. The frame
  chain is judged by its setting, because the chain begins later in the frame.

`Upscaler::RayReconstructionWantsRaw()` is true only if all of these hold:
- RR delivered last frame, meaning its composite reached the frame;
- this frame is planned for RR;
- RR is on;
- the helper is alive (polled at the frame's start) and RR is available;
- RRKeepDenoisers is off (the A/B switch).

A frame that will not go to RR therefore keeps every denoiser, even right after RR frames: the Pip-Boy, a menu, a terminal,
the Vulkan passes switched off, a RenderScale frame. Only an RR failure that nothing could know at the frame's start (DLSS-D
failing in the helper, a composite that fails) still gives one frame of raw signals.

While it is true:
- RtDenoise::Run returns false, so every caller uses its noisy signal: RT sun with SunAngle > 0, RT lamps with Denoise on, RT AO
  and RT GI.
- AmbientLight skips its two blur passes, and its accumulation keeps no history (share 0), but only when its estimate is the RT
  one.
- The AO effect's RT temporal pass is skipped (AO effect path, AmbientLight off).
- Reflections, when their rays are on: the temporal history share is 0 and the two blurs are skipped.

When it is false, every path is exactly the old one.

## States and auto-off

The RR switch only adds to DLSS (`[Shaders.TAA.DLSS] Enabled`). Every check below fails quietly, logs once, and leaves SR, or
FSR 2, as it is today:

| check | where | when it fails |
|---|---|---|
| DLSS Enabled and running (RTX, helper, nvngx_dlss.dll) | today's checks | FSR 2, no RR |
| `nvngx_dlssd.dll` next to the helper (skipped in HelperTest) | NVR, at helper start | RR images are not made; SR as today |
| `SuperSamplingDenoising.Available` (NGX capability) | helper, after NGX init | helper reports rrAvailable = 0; SR keeps running |
| DLSS-D feature creation / evaluate | helper | rrAvailable = 0 with the NGX reason; that frame and the rest go to SR |
| RenderScale 1, NVR DXVK passes on, a world frame (as TAA's) | NVR, at the RR point | this frame SR (post-tonemap) |

An RR failure never uses the helper's FAILED state, because that would also end SR. Changing the RR switch restarts the helper,
as HelperTest does, because the shared images are made at start.

On a GTX card the helper already gives up on DLSS altogether ("NGX: DLSS not available"), so RR is off with it. With
`HelperTest = 1` the pass-through test also exercises the RR exchange on any NVIDIA card, a GTX 1070 included. The helper
stretches the colour (an identity copy at native size) and draws the four guide images as thumbnails along the bottom edge. This
proves that NVR's whole RR side reaches the helper: the guides, the decode and encode, the weapon put-back, TAA's skip and the
denoiser bypass.

## Protocol v2 (tools/nvr-dlss-helper/NvrDlssProtocol.h)

- **Version 2.** The helper rejects version 1, and NVR ships with a matching helper.
- **Image table of 8:** colour, depth, motion, output, then diffuse albedo, specular albedo, normals, roughness. `imageCount`
  (4 or 8, set up by NVR) says how many were made. The input images are every index except OUTPUT.
- **`features`** (set up by NVR): bit 0 means the RR images are there and RR is wanted.
- **Per frame:**
  - `flags` bit 0 means this frame is RR;
  - `preset` and `rrPreset` carry NVR's own numbers, which the helper maps to NGX letters;
  - `rrOptions` bit 0 means the normals are in view space;
  - `worldToView[16]` and `viewToClip[16]` are the matrices.
- **Written by the helper:** `rrAvailable`, `rrEvaluated`, `rrResult`.

## Helper: what is set in NGX (string keys, no NVIDIA header needed)

The DLSS-D C helpers (`nvsdk_ngx_*_dlssd*.h`) and `nvngx_dlssd.dll` are not in the local SDK copy. The helper therefore goes
through the generic parameter API, which is declared in the headers we have:
- `NVSDK_NGX_VULKAN_CreateFeature1(device, cmd, NVSDK_NGX_Feature_RayReconstruction, params, &handle)`;
- `NVSDK_NGX_VULKAN_EvaluateFeature_C`.

How sure each piece is:
- **Verified in nvngx_dlss.dll 310.9.16's own strings:** `DLSS.Denoise.Mode`, `DLSS.Roughness.Mode`, `DLSS.Use.HW.Depth`,
  `DLSS.Input.DiffuseAlbedo`, `DLSS.Input.SpecularAlbedo`, `GBuffer.Normals`, `GBuffer.Roughness`, `WorldToViewMatrix`,
  `ViewToClipMatrix`, `RayReconstruction.Hint.Render.Preset.<mode>`.
- **Verified in the driver's NGX core:** `SuperSamplingDenoising.Available`.
- **Verified from the static library's debug types:** the RR preset numbers (D = 4, E = 5).
- **From memory, NOT verified:**
  - Denoise_Mode_DLUnified = 1;
  - Use.HW.Depth = 1 means hardware depth;
  - Roughness_Mode 0 = unpacked (the default, so it is not set);
  - the expected normal space and matrix layout.

  The settings RRNormals and RRDepthLinear let a tester flip the guesses that matter without a rebuild.

## DLSS presets (CNN or transformer): `[Shaders.TAA.DLSS] Preset`

Today NVR sets no preset hint, so the DLL decides. The 310.9.16 header says the default is transformer: K at DLAA, Quality and
Balanced, M at Performance, L at Ultra Performance. CNN is still offered: E, and F at DLAA and Ultra Performance (the old CNN
defaults), marked deprecated. A-D have been removed.

The setting is cheap: one hint per quality mode, set before the feature is made, so a change re-creates the feature.

| Preset | Model |
|---|---|
| 0 | NVIDIA's default for the DLL in use, which with the newest DLL is the newest model. There is no "latest" value in the API. |
| 1 | CNN |
| 2 | transformer K/M/L, pinned |
| 3 | transformer J |

An NVIDIA App or driver override of the preset (DRS) wins over the hint. NGX logs "Using DRS Overridden Preset".

RR has its own `RRPreset` (0 = default, 1 = D, 2 = E).

## Settings ([Shaders.TAA.DLSS], all off or neutral by default)

| setting | default | meaning |
|---|---|---|
| RayReconstruction | false | RR as above (needs Enabled, RenderScale 1). Changing it restarts the helper. |
| Preset | 0 | DLSS SR model (table above) |
| RRPreset | 0 | 0 default, 1 D, 2 E |
| RRAlbedo | 0 | 0 flat grey 0.5 (RR denoises texture and light alike: softer textures, strongest denoise); 1 the picture's own colour at a fixed brightness (keeps colour detail as albedo) |
| RRRoughness | 0.6 | roughness where no MaterialBuffer exists |
| RRNormals | 0 | 0 camera-relative world space, 1 view space (unverified convention: try 1 if the lighting smears on turns) |
| RRDepthLinear | false | false: hardware depth (`DLSS.Use.HW.Depth` 1); true: send 0 (if RR's depth reads wrong) |
| RRKeepDenoisers | false | true: NVR's own RT denoisers keep running under RR (A/B) |

**Also from the review:**
- **With RenderScale below 1, RR counts as not wanted.** No guide images are made and RR is not set up. The log says once
  that RR runs at RenderScale 1 only. Crossing RenderScale 1 restarts the helper, with or without the guide images.
- **RR's guide textures (about 34 bytes a pixel) are freed** if RR stays wanted but does not run for 600 frames, or if it fails.
- **The helper copies NGX's Vulkan extension names before listing them.** With RR on, a second NGX query runs before the
  instance and device are made.
- **The decoded colour is capped at 65000 and NaN becomes 0.** The guide is FP16, and a value over about 154 in the stored curve
  would decode to +Inf.
- **In a frame whose RT effects skipped their denoisers, the first-person weapon takes RR's output.** It is slightly soft,
  because the weapon is not jittered, but it no longer carries the raw 1-ray noise.
- **The RT lamp shadows' denoiser keeps running under RR** (RtDenoise Settings.IgnoreRr). The next frame's object shaders read
  its result, so a raw one would reach RR late and stay raw for a frame after RR stops.
- **The DLSS figures count a run of frames without an exchange as a gap,** not as one long frame. That covers FSR 2 frames and
  frames where the effects did not run.

## Not in v1 (follow-ups)

1. **RR with upscaling (RenderScale < 1).** The RT passes and their composites need to work in rect frames: rect-aware UVs and
   Vulkan passes inside them, plus an RR point before the fog in a rect frame.
2. **A real diffuse albedo.** The Object, Parallax and Terrain templates already have a COLOR1 output that writes 0 today. Writing
   linear base colour × (1 − metallic) there would give the albedo, and the specular albedo would come from the real F0.
3. **The RT primary rays do not take the jitter**, while depth and normals are jittered: a sub-pixel mismatch.
4. **Hit distances** (`DLSSD.SpecularHitDistance`) from the RT reflections, plus reflection motion vectors.
5. **Transparency and particle masks**, and `DLSSD.ColorBeforeFog`.
6. **Half-resolution AO and GI:** RR would prefer full-resolution signals (MaxQuality).

## DLSS diagnostics in the log (2026-10-30, tester report: DLAA on an RTX 5060, "fps dropped to ~20 at random while turning")

The tester's logs showed a healthy helper, with averages over 25 seconds: helper GPU 0.95 ms, response 0.15 ms, nothing
overwritten. Nothing per frame was logged, so the drops could not be seen. While the helper runs (no F10 needed), every
`DLSS HELPER` line now carries:

- **the mode first:** DLAA / Quality / Balanced / Performance / Ultra Performance, HDR / LDR / Ray Reconstruction, and the preset;
- **the game's frame interval over the window**, from one exchange to the next: average, p99 and maximum, and how many frames
  took over 33 ms and over 50 ms;
- **the exchange on the game's queue**, from three timestamp queries read back without waiting:
  - **the copies in:** start to "inputs ready". This is one command list, so it is the copies' own GPU time.
  - **the span to the helper's output** being copied back: average, maximum, and how many frames were over 4 ms. This one
    crosses the flush that "inputs ready" makes (review 2026-10-30). In a CPU-bound frame it also holds the CPU time until the
    frame's next submit, so on its own it does not blame the helper. The helper's own share is its GPU time and response, which
    the helper measures itself;
- **the game process's video memory against its budget** (DXGI 1.4 on the GPU DXVK uses), with "OVER BUDGET" when Windows has
  to page.

A frame over 50 ms that is also 3x the typical one gets its own `DLSS SLOW FRAME` line (at most 30 a session, one a second). It
gives:
- the latest copies-in time and span;
- the frames in flight;
- the helper's own response and GPU time;
- the video memory.

If a drop shows a normal span and normal helper figures, the drop is not the helper's.

**FSR 2's own textures are freed once DLSS has run 600 frames.** Since the review, this happens again after every stretch of
FSR 2 frames, such as a live switch to FSR and back. They take about 190 MB at 1920x1080 and are only FSR 2's
stand-in for the second the helper needs to start. On an 8 GB card that memory matters. FSR 2 makes them again on its first
frame if DLSS stops.

## DLAA stall with interior shadows (2026-10-30 evening, tester report: RTX 5060, P84 v4)

The tester saw runs of 45-53 ms frames, but only with DLAA on and the dedicated interior shadow pass on. The F10 log showed:
- Present ~46 ms;
- "DLSS helper (copies + helper's work + waits)" at p50 1.4 ms and max 45-59 ms;
- the helper's own GPU time 0.94 ms and its response 0.15 ms throughout.

Raising the helper's CPU priority changed nothing; HAGS was on. So the game's GPU queue sat in its wait for the helper's output
while the helper's submission, made 0.15 ms after NVR's write, was not being run. That points to the scheduling of two
processes on one GPU (wait-before-signal across processes), not to the helper's work.

Two `[Shaders.TAA.DLSS]` settings change how the helper hands its work over. Both are on by default, and both are start options of
the helper (`--wait cpu|gpu`, `--gpu-priority high|normal`), so changing one restarts it:
- **HelperWait = 1.** The helper waits on the CPU (vkWaitSemaphores) until the game's "inputs ready" has been reached, then
  submits a frame with no semaphore wait in it. No cross-process GPU wait is left for the driver to resolve, and the GPU runs the
  helper's work as soon as it is submitted. The helper's response is measured before that wait, so it means the same in both
  modes. NvrDlssHelper.log gives the CPU wait's average and maximum every 600 frames. 0 = the old GPU wait.
- **HelperGpuPriority = 1.** Two requests:
  - the helper's queue at VK_KHR/EXT_global_priority HIGH, where the driver offers and permits it;
  - the helper's process at the WDDM GPU scheduling priority class HIGH (D3DKMTSetProcessSchedulingPriorityClass; the CPU
    priority class does not reach the GPU).

  On the GTX 1070 the WDDM class is set and the Vulkan HIGH is "not permitted"; both outcomes are logged.

Every exchange whose span is over 20 ms gets a `DLSS EXCHANGE STALL frame N` line: the span, the copies, the frames in flight then,
and the helper's own GPU time and response. The DLSS HELPER line counts these stalls per window.
