#!/usr/bin/env bash
# compile-tess.sh (UNOFFICIAL 2026-10-27, fork patch 0021 / extension 1.7-displace): the tessellation displacement shaders in
# src/hlsl/NewVegas/Compute/Tess as SPIR-V, for [Main.Main.Performance] NvrDxvkDisplacement (docs/next/FORK2-DISPLACE.md).
#   TerrainDisplace.tesc                    -> TerrainDisplace.tcs.spv
#   TerrainDisplace.tese x TEX_COUNT 1-7    -> TerrainDisplace-t<N>.tes.spv (cw: the game's winding) and -t<N>-ccw.tes.spv (test)
#   TerrainDisplace.tese x TEX_COUNT 1-6, OBJECT_DEPTH -> TerrainDisplace-t<N>-d.tes.spv (+ -d-ccw): ObjectBlend's variants, which
#                                           also read the scene's depth at the pixel shader's s6 (2026-10-27; 7 layers leave no slot)
# Every result is checked with spirv-val. Windows (Git Bash): PATH="/c/VulkanSDK/1.3.296.0/Bin:$PATH" bash tools/nvr-dxvk/compile-tess.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../src/hlsl/NewVegas/Compute/Tess"
build() {   # out, source, defines...
  local out="$1" src="$2"; shift 2
  if ! log=$(glslangValidator -V --target-env vulkan1.1 "$@" -o "$out" "$src" 2>&1); then echo "$log" >&2; echo "FAILED: $out" >&2; exit 1; fi
  spirv-val --target-env vulkan1.1 "$out"
}
build TerrainDisplace.tcs.spv TerrainDisplace.tesc
n=1
for t in 1 2 3 4 5 6 7; do
  build "TerrainDisplace-t$t.tes.spv" TerrainDisplace.tese "-DTEX_COUNT=$t"
  build "TerrainDisplace-t$t-ccw.tes.spv" TerrainDisplace.tese "-DTEX_COUNT=$t" -DWINDING_CCW
  n=$((n+2))
  if [ "$t" -lt 7 ]; then
    build "TerrainDisplace-t$t-d.tes.spv" TerrainDisplace.tese "-DTEX_COUNT=$t" -DOBJECT_DEPTH
    build "TerrainDisplace-t$t-d-ccw.tes.spv" TerrainDisplace.tese "-DTEX_COUNT=$t" -DOBJECT_DEPTH -DWINDING_CCW
    n=$((n+2))
  fi
done
echo "compiled $n tessellation shaders"
