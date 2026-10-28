#version 450
#extension GL_GOOGLE_include_directive : enable
#include "TAA.glsl"

void main() {
    o_color = vec4(ResolveCore(uv).color, 1.0);
}
