#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"

void main() {
    vec4 color = textureLod(sampler2D(t_rendered, LINEAR_S), uv, 0.0);
    float depth = readDepthLod(uv);
    if (abs(DebugView - 4.0) < 0.5) {
        bool isFloor = ViewToWorldDir(ViewNormal(uv)).z > 0.75 && depth < DrawDistance && depth < farZ * 0.98;
        if (!isFloor) { o_color = vec4(color.rgb * 0.15, color.a); return; }
        float open = RoofMaskOn > 0.5 ? OpenAbove(uv, depth) : 1.0;
        o_color = open > 0.5 ? vec4(vec3(0.4 + 0.6 * saturate(Wetness)), 1.0) : vec4(0.5, 0.05, 0.05, 1.0);
        return;
    }
    if (abs(DebugView - 3.0) < 0.5) {
        vec4 env = pc.pass != 0u ? EnvAlong(normalize(toWorld(uv)), 0.0) : vec4(0.0);
        o_color = vec4(delinearize(env.rgb * env.a), 1.0);
        return;
    }
    if (MaterialOn && abs(DebugView - 7.0) < 0.5) {
        vec4 m = cb.flags.w > 0.5 ? textureLod(sampler2D(t_material, POINT_S), uv, 0.0) : vec4(0.0);
        o_color = m.b > 0.5 ? vec4(m.r, m.g, 0.6 * (1.0 - m.r), 1.0) : vec4(color.rgb * 0.15, color.a);
        return;
    }
    vec3 N = ViewNormal(uv);
    float roughness;
    float amount = Reflectivity(uv, depth, N, roughness);
    if (amount <= 0.0) { o_color = DebugView > 0.0 ? vec4(color.rgb * 0.15, color.a) : color; return; }

    vec2 position = uv / LowTexel - 0.5;
    vec2 base = floor(position);
    vec2 fraction = position - base;
    float tolerance = depth * 0.03 + 4.0;
    vec4 reflection = vec4(0.0);
    float weights = 1.0e-5;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            vec2 at = (base + vec2(x, y) + 0.5) * LowTexel;
            float tapDepth = readDepthLod(at - 0.5 * (LowTexel - cb.reciprocalResolution.xy));
            float weight = (x != 0 ? fraction.x : 1.0 - fraction.x) * (y != 0 ? fraction.y : 1.0 - fraction.y);
            weight /= 1.0 + abs(tapDepth - depth) / tolerance;
            reflection += textureLod(sampler2D(t_buffer, POINT_S), at, 0.0) * weight;
            weights += weight;
        }
    }
    reflection /= weights;

    vec3 V = normalize(ViewPosition(uv, depth));
    vec3 share;
    if (MaterialOn) {
        vec3 litColour = linearize(color.rgb);
        vec3 tint = saturate(litColour / max(max(litColour.r, max(litColour.g, litColour.b)), 1.0e-4));
        share = saturate(amount * EnvironmentFresnelF0(roughness, saturate(dot(-V, N)), mix(vec3(0.04), tint, SurfaceMetal))) * saturate(reflection.a);
    }
    else share = vec3(saturate(amount * EnvironmentFresnel(roughness, saturate(dot(-V, N)))) * saturate(reflection.a));
    vec3 reflected = Untame(reflection.rgb / max(reflection.a, 1.0e-4));
    if (SpecOccOn && cb.data4.w > 0.0) {
        float surfaceLight = luma(linearize(color.rgb));
        float reflectedLight = max(luma(reflected) * cb.layoutData.z, 1.0e-6);
        float occlusion = mix(1.0, saturate(surfaceLight / reflectedLight), cb.data4.w);
        if (abs(DebugView - 8.0) < 0.5) { o_color = vec4(saturate(surfaceLight * 8.0), saturate(reflectedLight * 8.0), occlusion, 1.0); return; }
        share *= occlusion;
    }

    if (DebugView > 4.5) { o_color = vec4(delinearize(reflected), 1.0); return; }
    if (DebugView > 1.5) { o_color = vec4(1.0 - reflection.a, reflection.a, amount * 0.5, 1.0); return; }
    if (DebugView > 0.5) { o_color = vec4(delinearize(reflected * share), color.a); return; }

    vec3 lin = linearize(color.rgb);
    if (AddOn && cb.data4.z > 0.0) lin = lin * (1.0 - share * (1.0 - cb.data4.z)) + reflected * share;
    else lin = mix(lin, reflected, share);
    o_color = vec4(delinearize(lin), color.a);
}
