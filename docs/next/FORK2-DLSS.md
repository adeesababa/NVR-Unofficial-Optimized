# DLSS through a 64-bit helper (fork round 2, 2026-10-27, branch deep/fork2-dlss)

**Settings moved 2026-10-27 (user: "give dlss its own tab like fsr does ... toggling one disables the other"):** DLSS is now its own
menu tab, `[Shaders.TAA.DLSS]` Enabled / RenderScale / HelperTest / JitterFlip (were `[Shaders.TAA.FSR]` Upscaler 1, RenderScale,
DlssHelperTest, DlssJitterFlip). FSR and DLSS are either/or: switching one on in the menu switches the other off; a settings file
with both on gets DLSS. The menu entry reads "TAA / FSR / DLSS". The text below keeps the old names.

First step of docs/next/FORK2-DLSS-PLAN.md, built. Everything is OFF by default (`[Shaders.TAA.FSR] Upscaler = 0`): off is the
exact old behaviour, nothing starts, nothing is made.

## What it does (plain English)
Fallout New Vegas is a 32-bit program, NVIDIA's DLSS exists only as 64-bit code, so DLSS cannot run inside the game. With
`Upscaler = 1`, NVR now starts a small separate 64-bit program, **NvrDlssHelper.exe**, which uses the same graphics card. Every
frame the game hands it three pictures (the low-resolution colour, the depth and the motion), the helper runs DLSS on them on the
GPU and hands the finished full-resolution picture back. The pictures never leave the graphics card (shared GPU memory), and the
two sides only tell each other "your turn" through GPU semaphores, so neither side waits on the CPU.
Whenever anything is missing (no NVIDIA card, no RTX card, no helper, an old driver, a stock d3d9.dll, a helper that crashes or
stops answering) NVR quietly uses FSR 2 instead and writes the reason in its log once.

The user's GTX 1070 cannot run DLSS (no tensor cores), so this step also has a **pass-through test** (`DlssHelperTest = 1`): the
helper does everything except DLSS: it takes the same shared pictures through the same semaphores, stretches the colour picture to
full size and draws a marker (a magenta box that moves one place per frame, a green box that stays). That proves the whole
cross-process path on the 1070. DLSS itself is compiled in and checked at start: on the 1070 the helper logs
"NGX: DLSS not available on this GPU (the GPU has no DLSS support (RTX 20 series or newer needed) ...)" and NVR uses FSR 2.

## Bulk test 1 (2026-10-27, run 20261027-112147-slotC, Lucky 38): the helper never ran -> fixed
NVR logged "a semaphore's handle could not be passed to the helper" and ran FSR 2 in every helper shot. Cause (reproduced with
`NvrDlssHelper.exe --probe`, which now checks it): Vulkan allows ONE export of an NT handle per semaphore / memory object, and
NVIDIA's driver hands the SAME handle value back on a second export. DXVK's DxvkFence (and DxvkResourceAllocation) export once at
creation, try D3DKMT with it and close it; so every later export returned that closed handle, and NVR's DuplicateHandle into the
helper failed with Win32 error 6 (invalid handle). (The images "worked" only by luck: a closed handle value can be reused by an
unrelated new handle, which would then have been sent to the helper.) Fix in fork patch 0026: DXVK keeps that one exported handle
(`DxvkFence::ntHandle()`, `DxvkResourceAllocation::sharedNtHandle()`, closed with the object) and every CreateHandle returns a
duplicate of it. The log now names the step and the code: CreateShared* / CreateHandle failures carry the VkResult or Win32 error
(`NVR_EXT_HRESULT_FROM_VK`, `HRESULT_FROM_WIN32`), DuplicateHandle into the helper its Win32 error, a helper that exited at once
its exit code; on success "DLSS helper: 4 images and 2 semaphores given to the helper (process N)".
Also in this round: the AmbientLight flicker fix (NVR c4ee6341, DxvkCompute::PreLoad, cherry-picked: the full merge of
nvr-dxvk-compute brings 32 more commits that need its other fork patches 0008-0010, which the combo-1027 merge handles) and fork
patch 0011 (managed-texture upload) in this branch's fork build.

## Proven so far (no game run; the coordinator's bulk test does that)
- The fork patch builds in CI (round 2 with 0011 and the NT-handle fix: run 40533105192, d3d9.dll sha256 8ECF6D60845A96C4...;
  round 1: run 40520838088, DE9A058BF672C02C..., has the semaphore bug).
- `NvrDlssHelper.exe --selftest` on this PC (GTX 1070, driver with Vulkan 1.4): a test process exports four images and two
  timeline semaphores exactly like the fork does, starts a second copy as the helper, runs 300 frames and checks the pixels:
  PASSED (picture right, marker at frame % 8, the previous marker gone = no stale or torn frames). Helper GPU work 0.04 ms per
  frame, helper response (NVR's write -> helper's submit) 0.07 ms average, 2.6 ms worst.
- `NvrDlssHelper.exe --selftest dlss` (the child in real DLSS mode): PASSED on the 1070 = it gives up cleanly in 0.3 s with
  "NGX: DLSS not available on this GPU (NVIDIA GeForce GTX 1070: NGX says DLSS is not supported here; it needs a GeForce RTX 20
  series or newer)", state FAILED: exactly what makes NVR fall back to FSR 2.
- `NvrDlssHelper.exe --probe`: "DLSS not available" on the 1070, as expected (NGX answers FeatureNotSupported).
- NVR's DLL builds clean (no new warnings); the DLSS path is only reachable with Upscaler 1.
- Builds (round 2, code of a7036e35): NVR `G:\_NVR-build-scratch\fork2-dlss-out\NewVegasReloaded.dll` (sha256 4F844E978BCDEB45...),
  helper `G:\_NVR-build-scratch\fork2-dlss-helper\bin\NvrDlssHelper.exe` (B34E5FB4B3A70317...) with NVIDIA's release
  `nvngx_dlss.dll` copied next to it, fork `G:\_NVR-build-scratch\nvr-dxvk-fork2-dlss-r2\nvr-dxvk-x32\d3d9.dll` (8ECF6D60845A96C4...).
  `--probe` also shows the cause of round 1: "semaphore NT handle exported twice (first closed): handles 073C / 073C (the SAME
  value), second DEAD (Win32 error 6)", the same for memory.

## Pieces
| where | what |
|---|---|
| `tools/nvr-dxvk/patches/0026-*.patch` (fork "1.7-dlss") | `ID3D9NvrExtShare`: `CreateSharedImage` (a DXVK image with a dedicated, exportable allocation, OPAQUE_WIN32 NT handle, kept in GENERAL), `CreateSharedFence` (exportable timeline semaphore), `CopyToShared` / `CopyFromShared` (image copy, or a 1:1 blit when formats differ), `SignalShared` (signal + flush), `WaitShared` (GPU wait in the command list being recorded), `SignalFromHost` / `GetCompletedValue` (the watchdog). Plus `DxvkResourceAllocation::getMemoryTypeIndex()` (the importer must use the exporter's memory type). DXVK already enables VK_KHR_external_memory_win32 / external_semaphore_win32 when the driver has them. No NVIDIA code. Additive: a new interface queried from ID3D9NvrExt. |
| `src/core/UpscalerDlss.cpp` | `DlssUpscaler : IUpscaler`: the detection chain, helper start, shared block, per-frame record, watchdog thread, fallback to FSR 2, log lines. `Upscaler::Choose(1)` returns it; it runs FSR 2 itself until the helper is ready and from the first problem on. |
| `tools/nvr-dlss-helper/` (MIT) | `NvrDlssHelper.cpp` (Vulkan 1.2 device on the game's GPU by LUID, imports, NGX init + capability check, DLSS create/evaluate, pass-through, timestamps, `--probe`, `--selftest`), `NvrDlssProtocol.h` (the shared block, shared with NVR), `NvrDlssHelper.vcxproj` (VS2019 x64), README, LICENSE, NVIDIA-SDK-NOTICE.txt. |
| `resource/NewVegasReloaded.dll.defaults.toml` | `Upscaler` text updated, `DlssHelperTest = 0`, `DlssJitterFlip = 0`. |

## Settings ([Shaders.TAA.FSR], all need TAA + FSR Enabled like FSR itself)
- `Upscaler = 0` (0 FSR 2, 1 DLSS through the helper; FSR 2 whenever DLSS cannot run).
- `DlssHelperTest = 0` (1 with Upscaler 1: the pass-through test instead of DLSS; any NVIDIA GPU; not for playing).
- `DlssJitterFlip = 0` (0-3: flips the jitter sign given to DLSS, x = 1, y = 2; for an RTX tester with NVIDIA's dev DLL).

## How a frame goes
NVR (render thread, in DXVK's command stream, no waits):
1. `CopyToShared` colour (render rectangle; RGBA16F, any input format by blit), depth (G32R32F -> R32F by blit), motion (RG16F).
2. Writes frame N's values into the shared block (render size, jitter in render pixels, reset, motion scale = render size because
   NVR's motion is in uv, HDR flag, frame time), sets latestFrame = N, wakes the helper (an event).
3. `SignalShared(inputsReady, N)`: DXVK submits everything so far with that signal.
4. `WaitShared(outputReady, N)`: the next DXVK submission waits on the GPU.
5. `CopyFromShared` output -> `TESR_DlssOutput`, which the RenderScale composite / TAA's FSR composite read as before.
Helper (own thread of its own process): woken by the event, records DLSS (or the pass-through copy) into one of 4 command buffers
and submits it with a GPU wait for inputsReady >= N and a signal of outputReady = N. Because NVR's CPU runs ahead of the GPU, the
helper's submission is normally queued before the GPU reaches step 3: the GPU goes from the game's work to the helper's and back
without a CPU in between.

DLSS inputs: colour (HDR flag + AutoExposure for the RenderScale world before tonemapping; LDR for TAA's native AA), depth 0 near
.. 1 far (not inverted), motion low-res unjittered (`MVLowRes`), MV scale = render size (DLSS: current + mv = previous, as FSR 2),
jitter in render pixels (FSR 2's convention = DLSS's "same coordinate system as the motion vectors"; `DlssJitterFlip` if not),
InRenderSubrectDimensions = the render size inside input-size images, quality mode from render / display (>= 0.99 DLAA, >= 0.66
Quality, >= 0.57 Balanced, >= 0.49 Performance, else Ultra Performance), feature re-created when sizes/HDR change. Layouts:
inputs SHADER_READ_ONLY_OPTIMAL and output GENERAL during evaluate, back to GENERAL (the fork's layout) after.

Safety: if the helper dies or no frame comes back for 3 s while frames are pending, a watchdog thread in NVR ends the helper and
signals outputReady from the CPU (far ahead), so the game's GPU queue cannot hang; DLSS is then off for the session (FSR 2).
Stop (size change, DlssHelperTest toggled, release) lets the helper finish its submitted frames first, then signals what is left.
The helper exits by itself when the game exits.

## Detection chain (each "no" = FSR 2, logged once with the reason)
Upscaler 1 -> NVR DXVK present -> the fork has `ID3D9NvrExtShare` (patch 1.7-dlss) -> the driver can share images/semaphores ->
the Vulkan device is NVIDIA (vendor 0x10DE; the real one, not the D3D9 one dxvk.conf may spoof) -> `NvrDlssHelper.exe` in the game
folder or `Data\NVSE\Plugins\NvrDlss\` -> `nvngx_dlss.dll` next to it (DLSS mode only) -> the helper imports everything, NGX init
and `SuperSampling.Available` (covers GTX cards and old drivers) -> ready within 15 s.

## Cost (expected)
Pass-through test on the 1070: the helper's own GPU work ~0.04 ms at 720p in the self-test; plus three small copies and one copy
back (~0.2-0.4 ms at 1080p) and one extra DXVK submission. DLSS itself on an RTX card: NVIDIA's numbers, roughly 0.5-1.5 ms at 1080p
output depending on the card (instead of FSR 2's ~2.5 ms on the 1070 at native resolution). Off: nothing.

## Install for a test (by hand, nothing installed by the agent)
- d3d9.dll: `G:\_NVR-build-scratch\nvr-dxvk-fork2-dlss-r2\nvr-dxvk-x32\d3d9.dll` (fork 0001-0015 + 0011 + 0026 round 2; or any
  later combo fork with both; the round-1 folder nvr-dxvk-fork2-dlss has the semaphore bug and no 0011).
- NVR DLL: `G:\_NVR-build-scratch\fork2-dlss-out\NewVegasReloaded.dll` -> `<game>\Data\NVSE\Plugins\`.
- Helper: `G:\_NVR-build-scratch\fork2-dlss-helper\bin\NvrDlssHelper.exe` -> `<game>\Data\NVSE\Plugins\NvrDlss\` (make the folder).
  For DLSS on an RTX card also `nvngx_dlss.dll` from the same bin folder (NVIDIA's release DLL) and NVIDIA's `LICENSE.txt`.
  Never put them straight into `Data\NVSE\Plugins` (xNVSE tries every DLL there).
- No shader files change.

## Licence (NVIDIA RTX SDKs licence, read in full 2026-10-27)
- Nothing from NVIDIA's SDK is in the repository; the helper's build references the SDK folder (`DlssSdkDir`), its README says
  where to get the SDK. NewVegasReloaded.dll (GPL) contains no NVIDIA code and does not link NGX; only the separate helper links
  `nvsdk_ngx_s.lib` and loads `nvngx_dlss.dll`. The helper's own source is MIT (licence 4e: the SDK must not become subject to an
  open-source licence; MIT covers only our files, NVIDIA's parts stay under NVIDIA's terms, NVIDIA-SDK-NOTICE.txt).
- DLSS only for systems with NVIDIA GPUs (supplement 1): NVR does not start the helper on other GPUs and the helper refuses them.
- **Before any public release** (not done, not for this step): NVIDIA's attribution in the credits / about (supplement 7.1b),
  NVIDIA's prior written approval of any use of their marks (7.2c, samples two weeks ahead; no NVIDIA logo is used anywhere),
  notify NVIDIA before a commercial release (supplement 4; NVR is not commercial, but say so if it ever is), ship NVIDIA's
  LICENSE.txt with the helper and nvngx_dlss.dll, never ship the dev DLL, include the third-party notices of the DLSS
  Programming Guide section 9.5/9.6 in the public documentation, keep nvngx_dlss.dll's signature (no re-signing that strips it).

## What is still missing / next steps
1. The bulk test on the 1070 (`fork2-dlss-bulk.txt`): the pass-through pictures, the log lines and the F10 cost (coordinator).
2. An RTX tester: `NvrDlssHelper.exe --probe` (should say AVAILABLE), then Upscaler 1 at RenderScale 0.67/0.58/0.5 against FSR 2,
   with NVIDIA's dev `nvngx_dlss.dll` (`/p:DlssDevDll=true`) once for its jitter / motion-vector overlays (DlssJitterFlip if the
   jitter check says so). Expected work: 2-4 session-hours of fixes from that feedback (MV/jitter signs, the HDR exposure choice,
   the depth format) — risk: nobody here can see DLSS's picture.
3. Menu: Upscaler is a number in the FSR section; a proper choice list with "DLSS (needs RTX + helper)" when the menu is next touched.
4. Packaging: `tools/build.ps1` / `check-package.ps1` do not include the helper yet (a release decision: see Licence).
5. Optional later: Intel XeSS (also 64-bit only) could use the same helper and protocol; NVIDIA's Frame Generation is out of scope.
