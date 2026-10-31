# NvrDlssHelper (tools/nvr-dlss-helper)

The 64-bit helper program behind New Vegas Reloaded's DLSS upscaler (`[Shaders.TAA.DLSS] Enabled = true`; before 2026-10-27 `[Shaders.TAA.FSR] Upscaler = 1`). Fallout New Vegas,
NVR and the NVR DXVK `d3d9.dll` are 32-bit; NVIDIA ships DLSS (NGX, `nvngx_dlss.dll`) for 64-bit Windows only, so DLSS runs
in this separate program, on the same GPU, with its own Vulkan device. NVR starts it, shares four GPU images (eight with Ray
Reconstruction) and two GPU timeline semaphores with it through the NVR DXVK fork (patch 1.7-dlss, `ID3D9NvrExtShare`) and a
small shared-memory block (`NvrDlssProtocol.h`, protocol 2), and it exits with the game. Design: `docs/next/FORK2-DLSS.md`;
Ray Reconstruction: `docs/next/FORK2-DLSS-RR.md`.

Licence: this folder is MIT (`LICENSE`). NVIDIA's SDK parts are NVIDIA's (`NVIDIA-SDK-NOTICE.txt`). Nothing from NVIDIA's SDK
is in this repository, and none of it is in NewVegasReloaded.dll.

## Getting NVIDIA's DLSS SDK

From https://github.com/NVIDIA/DLSS (the build was made with tag v310.9.16), read its `LICENSE.txt` and keep, in a folder
outside this repository:

    include\nvsdk_ngx*.h            (the API headers)
    lib\x64\nvsdk_ngx_s.lib         (Windows_x86_64\x64 in NVIDIA's tree: the static-CRT loader library)
    lib\rel\nvngx_dlss.dll          (the DLSS runtime that is shipped)
    lib\dev\nvngx_dlss.dll          (NVIDIA's development runtime with the on-screen overlay: testing only, never shipped)
    lib\rel\nvngx_dlssd.dll         (optional: the Ray Reconstruction runtime, "DLSS-D"; lib\Windows_x86_64\rel in NVIDIA's tree)
    lib\dev\nvngx_dlssd.dll         (optional: its development build, testing only)
    LICENSE.txt

`nvngx_dlssd.dll` comes from the same repository (https://github.com/NVIDIA/DLSS, `lib/Windows_x86_64/rel/nvngx_dlssd.dll`)
under the same licence terms as `nvngx_dlss.dll`. It is not in this repository either. The local SDK copy the builds use
(`G:\_NVR-build-scratch\dlss-sdk`, 2026-10-30) does not have it, nor NVIDIA's DLSS-D headers; the helper does not need those
headers (see "Ray Reconstruction" below).

## Building

Visual Studio 2019 (v142) x64 and the Vulkan SDK headers + `vulkan-1.lib` (`%VULKAN_SDK%`, else `C:\VulkanSDK\1.3.296.0`):

    msbuild tools\nvr-dlss-helper\NvrDlssHelper.vcxproj /p:Configuration=Release /p:Platform=x64 ^
      /p:DlssSdkDir=<SDK folder> /p:OutDir=<out>\ /p:IntDir=<obj>\

`DlssSdkDir` defaults to `%NVR_DLSS_SDK%`, then `G:\_NVR-build-scratch\dlss-sdk`. Without the SDK it still builds, for the
pass-through test only (`NVR_DLSS_NO_SDK`). `/p:DlssDevDll=true` copies NVIDIA's development `nvngx_dlss.dll` (its jitter and
motion-vector overlays help to check NVR's inputs). The release `nvngx_dlss.dll` is copied next to the exe, and
`nvngx_dlssd.dll` too when the SDK folder has it (the build says which).

## Installing (by hand)

Either next to the game's `FalloutNV.exe` / `d3d9.dll`, or (tidier) in its own folder:

    <game>\Data\NVSE\Plugins\NvrDlss\NvrDlssHelper.exe
    <game>\Data\NVSE\Plugins\NvrDlss\nvngx_dlss.dll        (DLSS only; the pass-through test does not need it)
    <game>\Data\NVSE\Plugins\NvrDlss\nvngx_dlssd.dll       (Ray Reconstruction only; optional)
    <game>\Data\NVSE\Plugins\NvrDlss\LICENSE.txt           (NVIDIA's, from the SDK, when shipping nvngx_dlss.dll / nvngx_dlssd.dll)

Not straight into `Data\NVSE\Plugins` (xNVSE tries to load every DLL there as a plugin). It needs the NVR DXVK `d3d9.dll`
built with patch 0026 (1.7-dlss) and an NVIDIA GPU. The helper writes `NvrDlssHelper.log` next to itself (or into %TEMP%).

## DLSS presets (`[Shaders.TAA.DLSS] Preset`)

NVR sends its own number every frame. The helper turns it into one NGX preset hint per quality mode, set before the DLSS
feature is made, so a change re-creates the feature (as a size or quality change does). An NVIDIA App or driver override wins
over the hint (NGX logs "Using DRS Overridden Preset").

| Preset | hints |
|---|---|
| 0 | none: the DLL's own choice (310.9.16: transformer K, M at Performance, L at Ultra Performance). Nothing is written to NGX until another preset was used once; after that, 0 writes "default" (0) to every mode. |
| 1 | CNN: E at Quality, Balanced, Performance and Ultra Quality; F at DLAA and Ultra Performance (deprecated in 310.x) |
| 2 | transformer, pinned: K at DLAA, Quality, Balanced and Ultra Quality; M at Performance; L at Ultra Performance |
| 3 | transformer J at every mode |

Any other number counts as 0. The log names the preset in its "NGX: DLSS feature made" line.

## Ray Reconstruction (protocol 2, `[Shaders.TAA.DLSS] RayReconstruction`)

With NVR's RR switch on, NVR makes four more shared images, the RR guides: diffuse albedo, specular albedo, normals and
roughness. It flags some frames as RR frames and sends the camera's world-to-view and view-to-clip matrices with them. The
helper evaluates those frames with NVIDIA's DLSS Ray Reconstruction (NGX feature `RayReconstruction`, "DLSS-D") instead of
DLSS SR. It needs:
- an RTX GPU and everything DLSS SR needs;
- `nvngx_dlssd.dll` next to the helper (NVIDIA's DLSS SDK, see above);
- NGX's capability `SuperSamplingDenoising.Available`, checked at start.

RR has its own NGX feature and its own parameter map, and both features stay alive while frames alternate. RR may not run: no
nvngx_dlssd.dll, an old driver, or a failed feature creation or evaluate. The helper then:
- says why in its log and its status line ("NGX: Ray Reconstruction not available (...); DLSS SR goes on");
- reports `rrAvailable = 0`;
- runs DLSS SR on that frame's picture.

It never gives up on DLSS SR because of RR. NVR stops sending RR frames once it sees `rrAvailable = 0`.

Without NVIDIA's DLSS-D headers, the helper sets RR through NGX's generic parameter API with string keys. The keys were
checked against the strings of `nvngx_dlss.dll` 310.9.16 and of the driver's NGX core. A few values are from memory and not
verified: denoise mode 1, `DLSS.Use.HW.Depth` 1 = hardware depth, the normals' space and the matrix layout. The list is in
`NvrDlssHelper.cpp`, above `RayReconstructionCapability`. RR presets (`RRPreset`): 0 none, 1 D, 2 E, the same at every
quality mode.

In the pass-through test (`HelperTest = 1`), RR frames need no NGX. The helper stretches the colour as always and draws the
four guides as thumbnails along the bottom edge of the picture: each a sixth of its width, from the left diffuse albedo,
specular albedo, normals, roughness. Roughness shows as red, and the normals' negative parts as black. This shows on any NVIDIA
GPU, a GTX 1070 included, that NVR's RR side reaches the helper.

## By hand

    NvrDlssHelper.exe --probe       the GPU, Vulkan interop support, and whether DLSS is available on this PC; then
                                    "probe: Ray Reconstruction (SuperSamplingDenoising.Available) AVAILABLE" or
                                    "... not available: <reason>", and "nvngx_dlssd.dll next to the helper: yes / no"
    NvrDlssHelper.exe --selftest    the whole exchange without the game: this process exports images and semaphores like the
                                    fork, starts a second copy of itself as the helper (pass-through), runs 300 frames and checks
                                    the pixels, the marker (frame order) and the round trip; PASSED / FAILED at the end
    NvrDlssHelper.exe --selftest dlss   the same with the child in real DLSS mode: on an RTX card it must run the frames
                                    (each counted as a DLSS frame; DLSS's picture is not compared with the pattern); on
                                    a card without DLSS it must give up cleanly with the NGX reason (that is NVR's fallback)
    NvrDlssHelper.exe --selftest rr     the RR exchange (protocol 2): eight images, every frame an RR frame, the four guides
                                    cleared to known colours; the pass-through child must report rrAvailable, count all 300
                                    frames as RR frames and show each guide's colour in its thumbnail
    NvrDlssHelper.exe --selftest dlss rr   both: on an RTX card every frame must run as RR, or as DLSS SR where RR is not
                                    available (logged); without DLSS it must give up cleanly as above

The logs are NvrDlssHelper-selftest.log, -selftest-dlss.log, -selftest-rr.log and -selftest-dlss-rr.log.

On the GTX 1070 (2026-10-27): probe = "DLSS not available" (no tensor cores), self-test PASSED (helper GPU work 0.04 ms,
response 0.05-0.07 ms average), `--selftest dlss` PASSED (gives up in 0.3 s with "NGX: DLSS not available on this GPU").
Protocol 2 (2026-10-30): `--selftest rr` and the presets have not been run yet. RR itself can only be tried on an RTX card.
