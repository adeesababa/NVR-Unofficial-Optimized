# Fork feature (overnight 2026-10-26): true HDR output (HDR10 / scRGB)

Branch `deep/fork-hdr`, fork patch `tools/nvr-dxvk/patches/0007-NVR-extension-1.5-hdr-...patch` (extension "1.5-hdr"; the
merger renumbers). Off by default; nothing changes without the fork's d3d9.dll, and nothing changes with it while
`[Shaders.HDR.Status] Enabled = false`.

## The user's display (checked 2026-10-26 03:10)
- Monitor AOC AG276QZD2 on the GTX 1070. `IDXGIOutput6::GetDesc1`: colour space `DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709`
  = **Windows HDR is OFF**; 10 bits per colour; the display reports max 417.7 nits, 253.8 nits full frame (an HDR-capable panel).
- Vulkan (vulkaninfo, NVIDIA): the surface offers `A2B10G10R10 + HDR10_ST2084` and `R16G16B16A16_SFLOAT + EXTENDED_SRGB_LINEAR`
  **even with Windows HDR off**, so "the surface supports HDR10" says nothing about the display. NVR therefore checks Windows'
  own HDR switch (DXGI colour space of the monitor showing the game) and stays SDR when it is off.
- To see real HDR the user turns on "Use HDR" in Windows Settings > Display, then `[Shaders.HDR.Status] Enabled = true`.

## How it works
1. The game's D3D9 back buffer stays 8-bit SDR. Everything drawn after NVR (HUD, menus, Pip-Boy overlay, ImGui) keeps
   drawing into it exactly as before; NVR's own picture (and the SettingShots pictures) are unchanged.
2. NVR, each frame, right after the game's tonemapper and before its post chain (`Render.cpp`, `core/HdrOutput.h`):
   a Vulkan pass (`Compute/HdrGain.frag`) computes a per-pixel **highlight gain** (R16F) from the HDR scene before
   tonemapping (exposure and linearisation as the tonemapper uses them) and the tonemapper's SDR result: gain 1 below
   `HighlightKnee` (the SDR look unchanged), rising for pixels the SDR tonemapper squeezed towards white, following the scene's
   brightness with a smooth roll-off to `PeakNits / PaperWhiteNits`.
3. After NVR's last effect (and ReShade's), before the HUD: `ID3D9NvrExtHdr::MarkHdrScene(picture, gain)` - the fork copies both.
4. At present, the fork (DXVK's own swap chain blitter, `DxvkNvrHdrBlit`, the blit shader `nvr_hdr_input`): back buffer pixel
   -> sRGB decode (or 2.2) -> times the gain where the back buffer still equals the marked picture (within `HudThreshold`;
   elsewhere the HUD/menu drew: gain 1) -> times paper white -> HDR10 PQ (Rec.2020) or scRGB. The game's gamma ramp is applied
   to the SDR value first, as without HDR. HDR metadata (max luminance / MaxCLL = PeakNits, MaxFALL = paper white) is sent.
   Frames NVR does not mark (menus over a frozen scene, loading screens, main menu) are shown at paper white.
5. The swap chain colour space switch itself is DXVK's existing machinery (`ID3D9VkExtSwapchain`'s colour space + presenter
   surface format); the patch adds the gain path, the HUD test and a test capture.

## Settings ([Shaders.HDR.Main], all apply immediately)
| setting | default | meaning |
|---|---|---|
| `[Shaders.HDR.Status] Enabled` | false | HDR output on (needs NVR DXVK 1.5-hdr and Windows HDR on) |
| Mode | 1 | 1 HDR10 (PQ, 10-bit), 2 scRGB (FP16 linear) |
| PaperWhiteNits | 200 | where SDR white, HUD and menus land (match Windows' SDR content brightness) |
| PeakNits | 0 | highlight ceiling; 0 = the display's peak from Windows (1000 if unknown) |
| HighlightStrength | 1.0 | 0 = SDR picture at paper white, 1 = full highlight headroom |
| HighlightKnee | 0.5 | SDR level (linear) where highlights start to rise |
| HudThreshold | 1.5 | 8-bit steps: a pixel further from NVR's picture counts as HUD (gain 1) |
| Gamma22 | false | decode SDR with 2.2 instead of the sRGB curve |
| IgnoreDisplayCheck | false | test only: send HDR with Windows HDR off |
| ShowHudMask | false | test only: tint the HUD-judged pixels green |
| CaptureFrame | 0 | test only: each new value saves one HDR output frame (PFM, nits) + a log line |

## Fork API (ID3D9NvrExtHdr, own GUID ...d310, so ID3D9NvrExt's vtable is untouched)
`GetHdrInfo`, `SetHdrOutput(mode, paper white, peak, MaxFALL, HUD threshold, flags)`, `MarkHdrScene(picture, gain)`,
`RequestHdrCapture` / `ReadHdrCapture` (test: the same blit into an scRGB image, read back). Files: new
`src/d3d9/d3d9_nvr_hdr.{h,cpp}`; edits in `d3d9_nvr_ext_api.h`, `d3d9_device.{h,cpp}` (member, friend, QueryInterface),
`d3d9_swapchain.{h,cpp}` (state, present), `dxvk_swapchain_blitter.{h,cpp}` + `dxvk_present_common.glsl` / `_frag` /
`_frag_blit` (two bindings, four push values, two spec constants), `meson.build`.
