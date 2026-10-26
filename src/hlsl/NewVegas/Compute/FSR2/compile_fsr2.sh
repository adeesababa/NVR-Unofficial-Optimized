#!/usr/bin/env bash
# Builds NVR's FSR 2 compute passes (AMD FidelityFX Super Resolution 2.2.1, MIT) into ../<Name>.spv for NVR DXVK.
# Runs in Linux/WSL with glslangValidator + spirv-val on PATH or unpacked into ~/vktools/root (as tools/nvr-dxvk/replace/compile.sh).
# Options are the ones AMD's own Vulkan build uses (src/ffx-fsr2-api/CMakeLists.txt), with NVR's fixed permutation:
# LDR input, render-resolution motion vectors, unjittered motion vectors, standard (not inverted) depth, reference Lanczos.
set -euo pipefail
export PATH="$HOME/vktools/root/usr/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/vktools/root/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/.."
COMMON=(-V --target-env vulkan1.1 -S comp -Os -DFFX_GPU=1 -DFFX_GLSL=1 -DFFX_HALF=0
  -DFFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0 -DFFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1 -DFFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2 -DFFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE=0
  -DFFX_FSR2_OPTION_HDR_COLOR_INPUT=0 -DFFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1
  -DFFX_FSR2_OPTION_JITTERED_MOTION_VECTORS=0 -DFFX_FSR2_OPTION_INVERTED_DEPTH=0)

build() {   # build <source> <name> [extra defines...]
  local src="$1" name="$2"; shift 2
  glslangValidator "${COMMON[@]}" "$@" -I"$HERE" -o "$OUT/$name.spv" "$HERE/$src" > "/tmp/fsr2.$$.log" || { cat "/tmp/fsr2.$$.log"; exit 1; }
  spirv-val --target-env vulkan1.1 "$OUT/$name.spv"
  echo "ok: $name.spv ($(stat -c %s "$OUT/$name.spv") bytes)"
}

build FSR2Reconstruct.comp FSR2Reconstruct
build FSR2DepthClip.comp   FSR2DepthClip
build FSR2Lock.comp        FSR2Lock
build FSR2Accumulate.comp  FSR2Accumulate        -DFFX_FSR2_OPTION_APPLY_SHARPENING=0
build FSR2Accumulate.comp  FSR2AccumulateSharpen -DFFX_FSR2_OPTION_APPLY_SHARPENING=1
build FSR2RCAS.comp        FSR2RCAS

# NVR's own passes around FSR (plain GLSL, Common.glsl conventions).
for f in FSRPrep.comp FSRLumaMip.comp; do
  [ -f "$OUT/$f" ] || continue
  glslangValidator -V --target-env vulkan1.1 -o "$OUT/${f%.comp}.spv" "$OUT/$f" > "/tmp/fsr2.$$.log" || { cat "/tmp/fsr2.$$.log"; exit 1; }
  spirv-val --target-env vulkan1.1 "$OUT/${f%.comp}.spv"; echo "ok: ${f%.comp}.spv"
done
for f in FSRComposite.frag; do
  [ -f "$OUT/$f" ] || continue
  glslangValidator -V --target-env vulkan1.1 -o "$OUT/${f%.frag}.spv" "$OUT/$f" > "/tmp/fsr2.$$.log" || { cat "/tmp/fsr2.$$.log"; exit 1; }
  spirv-val --target-env vulkan1.1 "$OUT/${f%.frag}.spv"; echo "ok: ${f%.frag}.spv"
done
