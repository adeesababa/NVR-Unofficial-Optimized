#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientLight.glsl"

layout(set = 1, binding = 0) uniform texture2D t_scene;

layout(location = 0) out vec4 o_color;

void main() {
    vec2 uv = (floor(gl_FragCoord.xy) + 0.5) * pc.frame.xy;
    vec3 light = min(linearize(linearLod(t_scene, uv).rgb), vec3(1.0));
    o_color = vec4(sqrt(max(light, vec3(0.0))), 1.0);
}
