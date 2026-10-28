#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_grid;

#define VL_DEPTH
#define VL_COMPOSITE
#define VL_GRID
#include "VolumetricLight.glsl"

void main() {
    vec4 color = POINT_LOD(t_rendered, uv);
    float depth = readDepthLod(uv);
    float planar = depth >= farZ * 0.996 ? MarchDistance : min(depth, MarchDistance);

    float s = sqrt(planar / MarchDistance) * 64.0;
    float z = min(floor(s), 63.0);
    vec2 local = uv * GridSize;
    float farSide = LINEAR_LOD(t_grid, GridUV(z, local)).r;
    float nearSide = z > 0.5 ? LINEAR_LOD(t_grid, GridUV(z - 1.0, local)).r : 0.0;
    float sigma = Density * length(toWorld(uv));
    vec2 air = vec2(mix(nearSide, farSide, saturate(s - z)) / cb.gridData.y, 1.0 - exp(-sigma * planar));
    o_color = CompositeAir(uv, color, air);
}
