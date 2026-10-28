#version 450
#extension GL_GOOGLE_include_directive : enable
#include "SunShadows.glsl"

void main() {
    vec2 OffsetMask = pc.pass == 0u ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 shadow = DepthBlurKeep(uv, OffsetMask, cb.screenSpaceData.y, 3500.0, SSS_MAXDEPTH);
    if (pc.pass != 2u) { o_color = shadow; return; }

    if (readDepthLod(uv) > SSS_MAXDEPTH) { o_color = shadow; return; }
    o_color = pow(shadow, vec4(cb.screenSpaceData.w));
}
