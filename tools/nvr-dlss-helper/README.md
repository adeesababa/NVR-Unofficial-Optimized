# NvrDlssHelper (tools/nvr-dlss-helper)

The 64-bit helper program behind New Vegas Reloaded's DLSS upscaler (`[Shaders.TAA.DLSS] Enabled = true`; before 2026-10-27 `[Shaders.TAA.FSR] Upscaler = 1`). Fallout New Vegas,
NVR and the NVR DXVK `d3d9.dll` are 32-bit; NVIDIA ships DLSS (NGX, `nvngx_dlss.dll`) for 64-bit Windows only, so DLSS runs
in this separate program, on the same GPU, with its own Vulkan device. NVR starts it, shares four GPU images and two GPU
timeline semaphores with it through the NVR DXVK fork (patch 1.7-dlss, `ID3D9NvrExtShare`) and a small shared-memory block
(`NvrDlssProtocol.h`), and it exits with the game. Design: `docs/next/FORK2-DLSS.md`.

Licence: this folder is MIT (`LICENSE`). NVIDIA's SDK parts are NVIDIA's (`NVIDIA-SDK-NOTICE.txt`). Nothing from NVIDIA's SDK
is in this repository, and none of it is in NewVegasReloaded.dll.

## Getting NVIDIA's DLSS SDK

From https://github.com/NVIDIA/DLSS (the build was made with tag v310.9.16), read its `LICENSE.txt` and keep, in a folder
outside this repository:

    include\nvsdk_ngx*.h            (the API headers)
    lib\x64\nvsdk_ngx_s.lib         (Windows_x86_64\x64 in NVIDIA's tree: the static-CRT loader library)
    lib\rel\nvngx_dlss.dll          (the DLSS runtime that is shipped)
    lib\dev\nvngx_dlss.dll          (NVIDIA's development runtime with the on-screen overlay: testing only, never shipped)
    LICENSE.txt

## Building

Visual Studio 2019 (v142) x64 and the Vulkan SDK headers + `vulkan-1.lib` (`%VULKAN_SDK%`, else `C:\VulkanSDK\1.3.296.0`):

    msbuild tools\nvr-dlss-helper\NvrDlssHelper.vcxproj /p:Configuration=Release /p:Platform=x64 ^
      /p:DlssSdkDir=<SDK folder> /p:OutDir=<out>\ /p:IntDir=<obj>\

`DlssSdkDir` defaults to `%NVR_DLSS_SDK%`, then `G:\_NVR-build-scratch\dlss-sdk`. Without the SDK it still builds, for the
pass-through test only (`NVR_DLSS_NO_SDK`). `/p:DlssDevDll=true` copies NVIDIA's development `nvngx_dlss.dll` (its jitter and
motion-vector overlays help to check NVR's inputs). The release `nvngx_dlss.dll` is copied next to the exe.

## Installing (by hand)

Either next to the game's `FalloutNV.exe` / `d3d9.dll`, or (tidier) in its own folder:

    <game>\Data\NVSE\Plugins\NvrDlss\NvrDlssHelper.exe
    <game>\Data\NVSE\Plugins\NvrDlss\nvngx_dlss.dll        (DLSS only; the pass-through test does not need it)
    <game>\Data\NVSE\Plugins\NvrDlss\LICENSE.txt           (NVIDIA's, from the SDK, when shipping nvngx_dlss.dll)

Not straight into `Data\NVSE\Plugins` (xNVSE tries to load every DLL there as a plugin). It needs the NVR DXVK `d3d9.dll`
built with patch 0026 (1.7-dlss) and an NVIDIA GPU. The helper writes `NvrDlssHelper.log` next to itself (or into %TEMP%).

## By hand

    NvrDlssHelper.exe --probe       the GPU, Vulkan interop support, and whether DLSS is available on this PC
    NvrDlssHelper.exe --selftest    the whole exchange without the game: this process exports images and semaphores like the
                                    fork, starts a second copy of itself as the helper (pass-through), runs 300 frames and checks
                                    the pixels, the marker (frame order) and the round trip; PASSED / FAILED at the end
    NvrDlssHelper.exe --selftest dlss   the same with the child in real DLSS mode: on an RTX card it must run the frames; on
                                    a card without DLSS it must give up cleanly with the NGX reason (that is NVR's fallback)

On the GTX 1070 (2026-10-27): probe = "DLSS not available" (no tensor cores), self-test PASSED (helper GPU work 0.04 ms,
response 0.05-0.07 ms average), `--selftest dlss` PASSED (gives up in 0.3 s with "NGX: DLSS not available on this GPU").
