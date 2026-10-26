# Fork feature (overnight 2026-10-26): true HDR output (HDR10 / scRGB)

Branch `deep/fork-hdr`, fork patch `tools/nvr-dxvk/patches/0007-NVR-extension-1.5-hdr-...patch` (extension "1.5-hdr"; the
merger renumbers). Off by default; nothing changes without the fork's d3d9.dll, and nothing changes with it while
`[Shaders.HDR.Main] Enabled = false`.

## The user's display (checked 2026-10-26 03:10)
- Monitor AOC AG276QZD2 on the GTX 1070. `IDXGIOutput6::GetDesc1`: colour space `DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709`
  = **Windows HDR is OFF**; 10 bits per colour; the display reports max 417.7 nits, 253.8 nits full frame (an HDR-capable panel).
- Vulkan (vulkaninfo, NVIDIA): the surface offers `A2B10G10R10 + HDR10_ST2084` and `R16G16B16A16_SFLOAT + EXTENDED_SRGB_LINEAR`
  **even with Windows HDR off**, so "the surface supports HDR10" says nothing about the display. NVR therefore checks Windows'
  own HDR switch (DXGI colour space of the monitor showing the game) and stays SDR when it is off.
- To see real HDR the user turns on "Use HDR" in Windows Settings > Display, then `[Shaders.HDR.Main] Enabled = true`.

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
| `[Shaders.HDR.Main] Enabled` | false | HDR output on (needs NVR DXVK 1.5-hdr and Windows HDR on) |
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

## Result (2026-10-26, slot runs, Windows HDR off so `IgnoreDisplayCheck = true`)
Builds: fork d3d9.dll `G:\_NVR-build-scratch\nvr-dxvk-hdr\d3d9.dll` (DXVK v3.0.1 + patches 0001-0007, built locally in WSL with
the same meson/MinGW setup as build.sh; `dxvk.conf` there has `d3d9.customVendorId = 1002` for depth on the GTX 1070; NVR log:
"Depth resolve: RESZ"). NVR DLL: MSBuild (VS2019 v142), branch `deep/fork-hdr`.

Runs (pictures in `H:\_NVR-autotest-shots\<run>`, NVR logs in `bench\autotest\<run>`, DXVK logs copied to
`G:\_NVR-build-scratch\nvr-dxvk-hdr\runlogs`):
- `20261026-100032-slotC` og-desert 1080p, list `fork1026-hdr-1`; `20261026-100138-slotA` lucky22 960x540, same list;
  `20261026-110208-slotC` og-desert 1080p, list `fork1026-hdr-2-timing` (off/on x3 + towards the sun).

What works (all measured):
- No crash, no fallback. DXVK log: `NVR ext: HDR output HDR10 (ST.2084, 10-bit): paper white 200 nits, peak 417.7 nits` and
  `Presenter: Actual swapchain properties: Color space: VK_COLOR_SPACE_HDR10_ST2084_EXT` (format A2B10G10R10); back to
  SRGB_NONLINEAR when switched off; scRGB mode switches too. Switching on/off between shots live: fine.
- Off = the old picture, and on = the same SDR picture (NVR's picture is untouched by design): off-1 vs off-2 17.3 % / 1.75
  (desert, scene noise), off vs hdr 15.1 % / 1.61 and 16.2 % / 1.67; lucky22 10.8 % / 1.27 vs 10.8 % / 1.25 and 11.1 % / 1.29.
- SDR content lands at paper white: HDR output / (sRGB-decoded SDR picture x 200 nits) over mid-tones = 0.9995-0.9998 (median)
  in every capture of both scenes; with HighlightStrength 0 the whole frame stays at or below paper white (max 185 nits in
  lucky22). Control: with the display check active (SDR desktop) NVR stays SDR and DXVK's stock blit runs (ratio 1.0145 =
  DXVK's 203 nits / 200, as expected).
- Highlights go above it: lucky22 neon 0.12 % of pixels above paper white, max 387 nits (peak 418: ratio 2.09 = limit);
  looking up at the bright ceiling 0.45 % of pixels, max 417.5 = the peak. Desert towards the sun: max 260 nits (peak 418),
  385 nits with PeakNits 1000 (0.8 % of pixels above paper white). The plain desert view at noon has no SDR pixel near white
  (brightest 0.83 encoded), so it gets no gain at all, correctly.
- HUD stays at paper white: ShowHudMask marks 1.0-1.1 % of pixels as HUD (the top row and the bottom HUD band: HP/AP bars,
  compass), and those pixels never exceed 110 nits.
- Cost (F10, 1080p, GTX 1070): HDR gain pass 0.081 ms + scene/gain copy for the HUD test 0.105 ms = ~0.19 ms per frame,
  only while HDR is on. The present blit already existed (two more texel fetches per pixel there). Off: zero (a few setting reads).

What is still missing / next steps:
- Not seen on a real HDR display: Windows HDR is off on this PC, so the captures check the numbers the fork sends, not the
  picture on the panel. The user's 5-minute check: turn on Windows HDR, set Windows' SDR content brightness, `[Shaders.HDR.Main]
  Enabled = true`, PaperWhiteNits to taste; look at the sun/neon/HUD; alt-tab and the main menu once.
- The gain follows the scene's luminance with one curve; highlight colour is the SDR tonemapper's (desaturated toward white),
  only brighter. A true HDR tonemap (the tonemapper run a second time with an HDR shoulder, per channel) would give more
  saturated highlights: next step if the look asks for it.
- Post effects that move pixels after the gain (motion blur, DoF) use the gain of the unblurred position (small halos possible
  at bright edges in motion). Semi-transparent menus fall back to paper white for the whole covered area (intended).
- The EDID path in DXVK fails on this monitor ("Failed to parse edid"); NVR takes the display's peak from DXGI instead (418 nits).
- Menu: the settings live in `[Shaders.HDR.Main]` (a settings-only section like Cloth); not checked whether the menu lists it.

## Combined build check (deep/fork-combo-1026 5f96ca01, NVR DLL 42b2dfba, fork v3.0.1-10-g02b00593, no dxvk.conf): PASS
Analysis only (runs 20261026-131417-slotB lucky22 960x540, 20261026-133747-slotC desert view 1080p).
- DXVK log (slot B, copied to `G:\_NVR-build-scratch\nvr-dxvk-hdr\runlogs\20261026-131417-slotB-combo_d3d9.log`): the same
  sequence as on deep/fork-hdr: SRGB -> HDR10_ST2084 (A2B10G10R10) -> SRGB -> HDR10 -> EXTENDED_SRGB_LINEAR (R16G16B16A16_SFLOAT)
  -> SRGB -> HDR10 -> SRGB; one EDID line only (the read-once fix holds). Depth: RESZ, even without the vendor spoof.
- Paper white, desert: capture/picture mid-tone ratio 0.9998 (5-95 % 0.963-1.040), as before. Highlights: towards the sun max
  259.8 nits (before 259.9), 0.068 % of pixels above paper white (0.089); PeakNits 1000: max 369.7 (384.9), 0.76 % (0.81).
- Cost: HDR gain pass 0.081 ms, scene copy 0.104 ms (before 0.081 / 0.105).
- lucky22: highlights to 395.5 nits (limit 418), HUD mask 1.1 % of pixels, at most 130 nits; HighlightStrength 0 max 163 nits.
  The per-pixel ratio is not usable in this run because the camera keeps moving through every shot (off-1 vs off-2: 82.7 % of
  pixels > 2/255, mean 14.0; before 10.8 % / 1.27), so capture (frame 60) and picture (frame 150) show different frames. An
  alignment-free check (luminance quantiles, HUD bands excluded) gives capture/picture 1.02 / 1.02 / 1.01 at p25 / p50 / p75
  (0.98-1.02 across captures), i.e. paper white holds. The moving camera affects every feature's pictures in that run (an
  issue with the batched lucky22 run or the combo, not with HDR): re-run lucky22 solo before trusting any lucky22 A/B from it.
