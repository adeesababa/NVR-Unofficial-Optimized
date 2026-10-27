# DLSS for NVR (plan, 2026-10-27, branch deep/fork2-upscale)

> **Update 2026-10-27 (deep/fork2-dlss):** the first step is built: fork patch 0026 (`ID3D9NvrExtShare`), `core/UpscalerDlss.cpp`
> and the helper `tools/nvr-dlss-helper` (named NvrDlssHelper.exe, not NvrDlssHost.exe). The user approved the SDK download
> (tag v310.9.16, kept outside the repository). What was built and how: **docs/next/FORK2-DLSS.md**. The text below is the plan
> as written before.

For players with an NVIDIA RTX card (GeForce RTX 20 series or newer: DLSS needs tensor cores). The user's GTX 1070 cannot run
it, so this is for other users, and it cannot be tested on this PC. Nothing from NVIDIA was downloaded: the DLSS SDK needs the
user's explicit approval first (see "What has to be downloaded").

## The short answer
- DLSS slots into the same place as FSR 2: NVR now has an upscaler interface (`src/core/Upscaler.h`, `Upscaler::IUpscaler`) that
  FSR 2 implements (`src/core/Upscaler.cpp`). Both of NVR's uses call it with the same inputs: TAA's native AA
  (`effects/TAAFSR.cpp`) and RenderScale real upscaling (`effects/TAAUpscale.cpp`). A DLSS upscaler would be a second class
  behind that interface; `[Shaders.TAA.FSR] Upscaler = 1` already selects it and falls back to FSR 2 (logged once) while it
  does not exist.
- **The real obstacle is not DXVK, it is 32 bits.** Fallout New Vegas is a 32-bit program, so NVR and the fork's d3d9.dll are
  32-bit. NVIDIA ships DLSS (the NGX runtime `nvngx_dlss.dll` and the `nvsdk_ngx` libraries) for 64-bit Windows only. A 32-bit
  process cannot load it, inside DXVK or anywhere else. This is also why NVIDIA's RTX Remix runs 32-bit games through a 64-bit
  "bridge" process (the New Vegas RTX mod does exactly that).
- So DLSS needs a small **64-bit helper program** (working name `NvrDlssHost.exe`) that owns NGX, and the fork shares four GPU
  images and two GPU semaphores with it (Vulkan external memory + external semaphores; same GPU, no copies through the CPU).
  The game's own DXVK device stays where it is.
- Estimate once approved: 10-16 session-hours (helper 4-6, fork patch 3-5, NVR side 1-2, testing by an RTX user). Main risks:
  cross-process GPU synchronisation inside DXVK's submission thread, and that nobody here can test it (needs an RTX tester).

## Where DLSS replaces FSR 2 (the same hook, the same inputs)
`Upscaler::Inputs` (core/Upscaler.h) is already what DLSS's evaluate call takes:

| NVR input | FSR 2 | DLSS (NVSDK_NGX_VK_DLSS_Eval_Params) |
|---|---|---|
| Color: jittered picture, render size in the top-left of a full-size texture | r_input_color_jittered | pInColor + InColorSubrectBase (0,0), InRenderSubrectDimensions |
| Depth: FSR-style device depth (0 near .. 1 far) | r_input_depth | pInDepth (flags: not inverted) -- or the game's own reversed INTZ depth with the DepthInverted flag |
| Motion: previous uv - current uv, unjittered, render size | r_input_motion_vectors, scale 1 | pInMotionVectors, InMVScaleX/Y = render width/height (DLSS wants pixels; check the sign in the programming guide) |
| JitterX/Y in render pixels (FSR's sign convention) | fJitter | InJitterOffsetX/Y (same units; sign to confirm with the DLSS dev DLL's jitter overlay) |
| Reset | frame index 0 + clears | InReset = 1 |
| Hdr (world before tonemapping) / LDR (TAA's native AA) | the HDR builds of Reconstruct/Accumulate | feature flag NVSDK_NGX_DLSS_Feature_Flags_IsHDR at creation |
| Exposure 1 | Exposure() = 1 | pInExposureTexture null + InPreExposure 1, or the AutoExposure flag |
| Sharpness | RCAS | none (DLSS 2.5.1+ removed its sharpening; NVR's own Sharpening effect can follow) |
| Render / display size | renderSize / displaySize | creation: InWidth/InHeight (max render size), InTargetWidth/InTargetHeight; per frame: InRenderSubrectDimensions |
| Output: rgba16f, display size, read by NVR's composite | rw_upscaled_output | pInOutput (storage-capable image) |

Quality modes map to RenderScale: DLSS Quality 0.667, Balanced 0.58, Performance 0.5, DLAA 1.0 (= TAA's native-AA position, i.e.
Enabled with RenderScale 1). NVSDK_NGX_DLSS_GetOptimalSettings gives the exact render size per mode; NVR would round RenderScale
to it.

## How NGX would be hosted (the 64-bit helper)
NGX for Vulkan must be given a VkInstance/VkPhysicalDevice/VkDevice that were created with the extensions it needs, and its
evaluate call records into a VkCommandBuffer of that device. That device cannot be DXVK's (32-bit), so the helper makes its own:

1. **Helper start** (NVR starts it once, when Upscaler 1 is chosen and an NVIDIA adapter is present; it exits with the game).
   - Creates a VkInstance and VkDevice on the same physical GPU as DXVK (match `VkPhysicalDeviceIDProperties::deviceLUID`, which
     the fork reports to NVR).
   - Extensions: asks NGX first. `NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements` and
     `NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements` (feature NVSDK_NGX_Feature_SuperSampling) return the exact lists for
     the installed driver/DLL; the lists commonly reported are VK_NVX_binary_import, VK_NVX_image_view_handle, VK_KHR_push_descriptor
     and buffer device address (to confirm with the SDK at implementation time; the older `NVSDK_NGX_VULKAN_RequiredExtensions`
     returns the same). Plus, for sharing with DXVK: VK_KHR_external_memory_win32, VK_KHR_external_semaphore_win32 and timeline
     semaphores (core in Vulkan 1.2).
   - `NVSDK_NGX_VULKAN_Init_with_ProjectID(...)`, then `NVSDK_NGX_VULKAN_GetCapabilityParameters` and
     `NVSDK_NGX_Parameter_SuperSampling_Available` (+ `..._NeedsUpdatedDriver`, min driver version). Not available: the helper
     answers "no" and exits; NVR logs the reason once and FSR 2 runs.
2. **Shared resources** (fork patch, numbered 0023 or whatever the merger gives it; additive, a separate interface like
   ID3D9NvrExtGeometry/Hdr: `ID3D9NvrExtShare`):
   - The fork creates four images with `VkExternalMemoryImageCreateInfo` / `VkExportMemoryAllocateInfo` (handle type
     OPAQUE_WIN32): colour in (render max size, rgba16f), depth in (r32f), motion in (rg16f), colour out (display size, rgba16f),
     and two exportable timeline semaphores ("inputs ready", "output ready"). Their NT handles are duplicated into the helper
     (DuplicateHandle; the helper's process handle comes from NVR), which imports them (`VkImportMemoryWin32HandleInfoKHR`,
     `VkImportSemaphoreWin32HandleInfoKHR`).
   - Per frame, in DXVK's command stream (the dxvk-cs thread, in order with everything else): copy NVR's three input textures into
     the shared images, end the command list and submit it with a signal of "inputs ready" = N; tell the helper "frame N" plus the
     per-frame values (jitter, sizes, reset, MV scale) through a small shared-memory block and an event; the next submission waits
     on "output ready" = N before the copy of the shared output into NVR's output texture. Timeline semaphores allow DXVK to submit
     that wait before the helper has submitted its signal: no CPU wait anywhere.
   - DXVK's own instance/device need only the external memory/semaphore extensions (DXVK 2.x/3.x already knows
     VK_KHR_external_memory_win32 and VK_KHR_external_semaphore_win32 for shared D3D resources); none of NGX's own extensions are
     needed in the 32-bit process.
3. **Helper per frame**: wait "inputs ready" = N, record `NGX_VULKAN_EVALUATE_DLSS_EXT` (helpers in nvsdk_ngx_helpers_vk.h) on
   resources made with `NVSDK_NGX_Create_ImageView_Resource_VK` from the imported images, signal "output ready" = N, submit.
   The DLSS feature is (re)created with `NGX_VULKAN_CREATE_DLSS_EXT` when the sizes, HDR flag or quality change.
4. **NVR side** (no SDK code at all): `Upscaler::Dlss()` returns a `DlssUpscaler : IUpscaler` whose Evaluate calls the new fork
   interface with the same `Upscaler::Inputs`, and whose Output() is NVR's texture the fork copies the result into. `Choose()`
   already falls back to FSR 2. Nothing else in TAAFSR.cpp / TAAUpscale.cpp changes.

Why not run all of DXVK in a 64-bit bridge (RTX Remix's route, gutbash/b-bridge): every one of NVR's D3D9 calls and its whole
extension interface would have to cross the process boundary each frame (NVR makes thousands of state/constant calls), and NVR's
readbacks and queries would stall. The helper only exchanges four images and two counters per frame.

Runtime detection, in order (each "no" = FSR 2, logged once): Upscaler setting 1; NVIDIA adapter (D3D9 adapter identifier
vendor 0x10DE); `NvrDlssHost.exe` and `nvngx_dlss.dll` present next to it (the game folder or `Data\NVSE\Plugins\NVR`); the fork
exposes ID3D9NvrExtShare; the helper starts and answers "SuperSampling available" (this covers "not an RTX card" and "driver too
old"). A GTX card therefore never gets further than the helper's answer.

## What has to be downloaded (needs the user's approval; nothing done)
From NVIDIA's public DLSS SDK repository, https://github.com/NVIDIA/DLSS (pick the newest release tag at the time; the
repository is large because it carries the DLLs, ~100-300 MB):

| file | what for | goes where |
|---|---|---|
| `include/nvsdk_ngx.h`, `nvsdk_ngx_defs.h`, `nvsdk_ngx_params.h`, `nvsdk_ngx_helpers.h`, `nvsdk_ngx_vk.h`, `nvsdk_ngx_helpers_vk.h` | the API | helper source only (never NVR's DLL) |
| `lib/Windows_x86_64/x86_64/nvsdk_ngx_s.lib` (static CRT) or `nvsdk_ngx_d.lib` (dynamic CRT) | NGX loader, linked into the 64-bit helper | helper build |
| `lib/Windows_x86_64/rel/nvngx_dlss.dll` | the DLSS model/runtime (redistributable) | shipped next to the helper |
| `lib/Windows_x86_64/dev/nvngx_dlss.dll` | development build with the on-screen debug overlay (jitter / motion vector checks) | testing only, never shipped |
| `doc/DLSS_Programming_Guide_Release.pdf` | integration rules (MV/jitter conventions, quality modes) | reading |
| `LICENSE.txt` | the licence | shipped with the helper |

Licence notes (to be read in full at download time; summary from memory, not verified here): the SDK is under NVIDIA's own
"RTX SDKs" licence, not an open-source licence. It allows using the SDK in an application and redistributing the runtime DLL
with it, with NVIDIA's attribution/notice; the DLL must not be modified; "NVIDIA" / "DLSS" names follow NVIDIA's trademark
guidelines. NVR's DLL is GPL-3.0 (with exceptions, License.md): **do not link nvsdk_ngx into NewVegasReloaded.dll**; the separate
64-bit helper (its own licence, talks to NVR only through shared GPU memory and a shared-memory block) keeps the proprietary code
out of the GPL program. The fork (DXVK, zlib licence) needs no NVIDIA code at all in this design.

## Steps once approved
1. Download the SDK into its own folder (G:\_NVR-build-scratch\dlss-sdk, never into the repo); read LICENSE.txt and the
   programming guide's MV/jitter sections; confirm the 64-bit-only state of the current release.
2. Fork patch: `ID3D9NvrExtShare` (adapter LUID, exportable images + timeline semaphores, the per-frame copy-in / signal /
   wait / copy-out recorded in DXVK's stream). Test it first without NVIDIA code: a dummy 64-bit helper that just copies colour
   in -> out proves the sharing and the synchronisation on any GPU (the GTX 1070 included).
3. `tools/nvr-dlss-host/`: the 64-bit helper (Vulkan instance/device with NGX's extensions, NGX init, capability check, feature
   create/evaluate, the frame loop). Built with MSVC x64 separately from NVR.
4. NVR: `DlssUpscaler` behind `Upscaler::Dlss()`, the start/stop of the helper, the detection chain above, the log lines.
5. An RTX tester: DLSS dev DLL overlay to check jitter and motion vectors, then Quality/Balanced/Performance A/B against FSR 2
   at the same RenderScale (the bulk list fork2-upscale-bulk.txt with `set Shaders.TAA.FSR Upscaler 1`).

## Also worth knowing
- Intel XeSS has the same 64-bit-only problem (libxess.dll is x64); it would go through the same helper.
- AMD FSR 3.1's upscaler is open source (MIT) and could replace FSR 2.2.1 inside the 32-bit fork exactly like FSR 2 now (no helper);
  it is somewhat better at thin details and disocclusion. A port is an FSR-2-sized job (the passes changed), 6-10 session-hours.
