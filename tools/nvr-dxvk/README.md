# NVR DXVK (tools/nvr-dxvk)

DXVK v3.0.1 (zlib licence, github.com/doitsujin/dxvk) plus the NVR extension, as a patch series on the v3.0.1 tag.
Only d3d9.dll is built and shipped; NVR's own DLL, menu, settings and presets stay as they are.

- `patches/`   the fork: one patch series on top of v3.0.1, applied in file-name order with `git am` (build.sh does
               `git am --3way patches/*.patch`; re-apply onto a newer DXVK the same way). New files
               `src/d3d9/d3d9_nvr_ext*.{h,cpp}` and `d3d9_nvr_hdr.{h,cpp}`, plus small edits to `d3d9_device.{h,cpp}`,
               `d3d9_swapchain.{h,cpp}`, `d3d9_multithread.h`, `d3d9_shader.h`, `d3d9_shader_analysis.h`, `src/d3d9/meson.build`,
               `dxvk_context.h`, `dxvk_shader.cpp`, `dxvk_instance.cpp`, the swap chain blitter and its present shaders.
               Renumbered 2026-10-27 (deep/fork-combo-1027: the port line and the 10-26 feature round in one series):
                 0001-0006  extension 1.0-1.4: compute, full-screen passes, pixel shader replacements, the push-data pipeline
                            library rule, mipmapped/anisotropic samplers, DrawShifted
                 0007       RESZ depth resolve on every GPU vendor
                 0008       1.5: profiling (SetProfiling / GetProfile; NVR's "DXVK PROFILE" lines)
                 0009       injected graphics passes end suspended (no barrier after every pass)
                 0010       1.6: DrawDepthStencil (SMAA's weight pass through its stencil)
                 0011       managed textures (and pending AUTOGENMIPMAP mips) uploaded before a pass samples them
                 0012       "1.5-hdr": ID3D9NvrExtHdr, HDR10 / scRGB output (docs/next/FORK-1026-HDR.md)
                 0013       "1.5-particles": ID3D9NvrExtGeometry, passes with their own vertex shader and instanced draws
                            (GPU particles, docs/next/FORK-1026-PARTICLES.md)
                 0014       "1.5-sm6": 64 slots per type / 96 bindings per pipeline for the SM6 builds of NVR's own passes
                            (`tools/nvr-sm6`, docs/next/FORK-1026-SM6.md; NVR reads MaxSlots from GetInfo)
                 0015       "1.5-debug": dxvk.conf `nvr.debugEnv = "NAME=value|..."` sets environment variables (DXVK_DEBUG,
                            Vulkan loader/layer settings) before DXVK and the loader read them
                 0016       "1.5-debug": ID3D9NvrExtReadback (readback that never waits), storage buffers in the pass stages
               GetInfo reports 1.6. The "1.5-<feature>" parts are separate interfaces found with QueryInterface (or, for sm6,
               by MaxSlots), not by the minor version: NVR checks each one itself and keeps its D3D9 path without it.
- `debug/`     dxvk.conf templates for debugging: `dxvk.conf.markers` (RenderDoc captures with NVR's named groups),
               `dxvk.conf.renderdoc-slot` (RenderDoc loaded as a Vulkan layer in a slot game, for the 'capture' shot) and
               `dxvk.conf.validation` (Khronos validation + synchronization validation). docs/next/FORK-DEBUG-CAPTURE.md.
- `build.sh`   clone + patch + build the 32-bit d3d9.dll (Linux or WSL, MinGW-w64). The GitHub workflow
               `.github/workflows/nvr-dxvk.yml` runs the same and uploads the DLL as an artifact.
- `test/`      `nvr_ext_test.cpp`: an end-to-end test of the extension on dxvk-native (Linux, any Vulkan device, also
               lavapipe software Vulkan): the AvgLuma compute pass against the D3D9 pass's arithmetic, storage images,
               buffers, the graphics passes, every ported NVR .spv, a pixel shader replacement (`nvr_ext_test_replace.frag`
               for a hand-assembled ps_3_0), device Reset. Arguments: AvgLuma.spv, the three test .spv, the Compute
               folder, nvr_ext_test_replace.spv. See docs/next/NVR-DXVK-EXTENSION.md.

The public interface NVR compiles against is `src/d3d9/d3d9_nvr_ext_api.h` in the patch; NVR's copy is `src/core/NvrDxvkExt.h`.
