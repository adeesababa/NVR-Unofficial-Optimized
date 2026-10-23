# NVR DXVK (tools/nvr-dxvk)

DXVK v3.0.1 (zlib licence, github.com/doitsujin/dxvk) plus the NVR extension, as a patch series on the v3.0.1 tag.
Only d3d9.dll is built and shipped; NVR's own DLL, menu, settings and presets stay as they are.

- `patches/`   the fork: one patch on top of v3.0.1 (new files `src/d3d9/d3d9_nvr_ext*.{h,cpp}`, plus small edits
               to `d3d9_device.{h,cpp}`, `src/d3d9/meson.build` and `dxvk_context.h`). Re-apply onto a newer DXVK with `git am`.
- `build.sh`   clone + patch + build the 32-bit d3d9.dll (Linux or WSL, MinGW-w64). The GitHub workflow
               `.github/workflows/nvr-dxvk.yml` runs the same and uploads the DLL as an artifact.
- `test/`      `nvr_ext_test.cpp`: an end-to-end test of the extension on dxvk-native (Linux, any Vulkan device, also
               lavapipe software Vulkan): the AvgLuma compute pass against the D3D9 pass's arithmetic. See docs/next/NVR-DXVK-EXTENSION.md.

The public interface NVR compiles against is `src/d3d9/d3d9_nvr_ext_api.h` in the patch; NVR's copy is `src/core/NvrDxvkExt.h`.
