# NVR DXVK (tools/nvr-dxvk)

DXVK v3.0.1 (zlib licence, github.com/doitsujin/dxvk) plus the NVR extension, as a patch series on the v3.0.1 tag.
Only d3d9.dll is built and shipped; NVR's own DLL, menu, settings and presets stay as they are.

- `patches/`   the fork: three patches on top of v3.0.1 (1.0 compute, 1.1 full-screen passes, 1.2 pixel shader
               replacements; new files `src/d3d9/d3d9_nvr_ext*.{h,cpp}`, plus small edits to `d3d9_device.{h,cpp}`,
               `d3d9_shader.h`, `d3d9_shader_analysis.h`, `src/d3d9/meson.build` and `dxvk_context.h`). Re-apply onto a
               newer DXVK with `git am`. 0009 (1.5-particles, branch deep/fork-particles): `ID3D9NvrExtGeometry`, a
               separate interface queried from ID3D9NvrExt (own vertex shader reading buffers, instanced draws; used by
               GPU particles, docs/next/FORK-1026-PARTICLES.md); only the three d3d9_nvr_ext* files change. 0013 (deep/fork-sm6, "1.5-sm6"): 64 slots per type / 96 bindings per pipeline for the
               SM6 builds of NVR's own passes (`tools/nvr-sm6`, docs/next/FORK-1026-SM6.md). 0014 (deep/fork-debug-capture,
               "1.5-debug"): dxvk.conf `nvr.debugEnv = "NAME=value|..."` sets environment variables (DXVK_DEBUG, Vulkan
               loader/layer settings) before DXVK and the loader read them; only `src/dxvk/dxvk_instance.cpp` changes.
               0026 (deep/fork2-dlss, "1.7-dlss"): `ID3D9NvrExtShare`, images and timeline semaphores exported to another
               process (NVR's 64-bit DLSS helper, `tools/nvr-dlss-helper`, docs/next/FORK2-DLSS.md): plain Vulkan external
               memory/semaphores, no NVIDIA code; the d3d9_nvr_ext* files, plus `src/dxvk/dxvk_memory.*`, `dxvk_fence.*` and
               `dxvk_image.cpp`: an NT handle is exported ONCE per object and kept (NVIDIA returns the same handle value on a
               second export, so DXVK's close-after-first-export left every later export dead), callers get duplicates.
               0011 (from mine/nvr-dxvk-compute, the AmbientLight flicker fix): managed textures (and pending mips) are
               uploaded before an extension pass samples them; on this branch it sits between 0009 and 0013 (applies there).
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
