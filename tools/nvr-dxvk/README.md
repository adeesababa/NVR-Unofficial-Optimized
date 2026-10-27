# NVR DXVK (tools/nvr-dxvk)

DXVK v3.0.1 (zlib licence, github.com/doitsujin/dxvk) plus the NVR extension, as a patch series on the v3.0.1 tag.
Only d3d9.dll is built and shipped; NVR's own DLL, menu, settings and presets stay as they are.

- `patches/`   the fork: a patch series on top of v3.0.1 (0001-0010: extension 1.0-1.6, compute, full-screen passes, pixel
               shader replacements, samplers, RESZ on every vendor, profiling, depth-stencil passes; 0011: managed textures
               uploaded before a pass samples them; new files `src/d3d9/d3d9_nvr_ext*.{h,cpp}`, plus small edits to `d3d9_device.{h,cpp}`,
               `d3d9_shader.h`, `d3d9_shader_analysis.h`, `src/d3d9/meson.build` and `dxvk_context.h`). Re-apply onto a
               newer DXVK with `git am`.
- `patches/0021-*` (2026-10-27, branch deep/fork2-displace): tessellation displacement, the separate interface `ID3D9NvrExtTess`
               ("1.7-displace"; new files `src/d3d9/d3d9_nvr_tess.{h,cpp}`); see docs/next/FORK2-DISPLACE.md. Its shaders:
               `compile-tess.sh` (src/hlsl/NewVegas/Compute/Tess -> .spv, Windows Git Bash with the Vulkan SDK).
- `build.sh`   clone + patch + build the 32-bit d3d9.dll (Linux or WSL, MinGW-w64). The GitHub workflow
               `.github/workflows/nvr-dxvk.yml` runs the same and uploads the DLL as an artifact.
- `test/`      `nvr_ext_test.cpp`: an end-to-end test of the extension on dxvk-native (Linux, any Vulkan device, also
               lavapipe software Vulkan): the AvgLuma compute pass against the D3D9 pass's arithmetic, storage images,
               buffers, the graphics passes, every ported NVR .spv, a pixel shader replacement (`nvr_ext_test_replace.frag`
               for a hand-assembled ps_3_0), device Reset. Arguments: AvgLuma.spv, the three test .spv, the Compute
               folder, nvr_ext_test_replace.spv. See docs/next/NVR-DXVK-EXTENSION.md.

The public interface NVR compiles against is `src/d3d9/d3d9_nvr_ext_api.h` in the patch; NVR's copy is `src/core/NvrDxvkExt.h`.
