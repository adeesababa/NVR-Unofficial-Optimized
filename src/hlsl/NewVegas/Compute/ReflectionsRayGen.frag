#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Reflections.glsl"
#include "ReflectionsRT.glsl"

layout(location = 1) out vec4 o_dir;
layout(location = 2) out vec4 o_ray;

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 uv = (vpos + 0.5) * LowTexel;
    vec2 pixelUV = uv - 0.5 * (LowTexel - cb.reciprocalResolution.xy);
    float depth = readDepthLod(pixelUV);
    vec3 N = ViewNormal(pixelUV);
    float roughness;
    float amount = Reflectivity(pixelUV, depth, N, roughness);
    if (amount <= 0.0) { o_color = vec4(0.0); o_dir = vec4(0.0); o_ray = vec4(0.0); return; }

    vec3 position = ViewPosition(pixelUV, depth);
    vec3 V = normalize(position);

    float a = roughness * roughness;
    float u1 = GradientNoise(vpos), u2 = GradientNoise(vpos + vec2(37.0, 17.0));
    float cosTheta = sqrt((1.0 - u1) / (1.0 + (a * a - 1.0) * u1));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 6.2831853 * u2;
    vec3 tangent = normalize(abs(N.z) < 0.999 ? cross(N, vec3(0, 0, 1)) : cross(N, vec3(1, 0, 0)));
    vec3 bitangent = cross(N, tangent);
    vec3 H = normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + N * cosTheta);
    vec3 R = reflect(V, H);
    if (dot(R, N) <= 0.0) R = reflect(V, N);

    float jitter = GradientNoise(vpos + vec2(11.0, 71.0));
    float lastT = 0.0;
    float lastZ = position.z;
    float hitT = -1.0;
    bool searching = true;
    for (float i = 1.0; i <= Steps && searching; i += 1.0) {
        float s = (i - 1.0 + jitter) / Steps;
        float t = RayLength * s * s + 2.0;
        vec3 p = position + R * t;
        vec2 at = ToScreen(p);
        if (p.z <= nearZ || any(lessThanEqual(at, vec2(0.0))) || any(greaterThanEqual(at, vec2(1.0)))) searching = false;
        else {
            float sceneZ = readDepthLod(at);
            float behind = p.z - sceneZ;
            float thickness = (max(abs(p.z - lastZ), 20.0) * 1.5 + t * 0.03) * ThicknessScale;
            if (behind > 0.0 && behind < thickness && sceneZ < farZ * 0.98) { hitT = t; searching = false; }
            else { lastT = t; lastZ = p.z; }
        }
    }

    vec3 colour = vec3(0.0);
    float confidence = 0.0;
    if (hitT > 0.0) {
        float lo = lastT, hi = hitT;
        for (int r = 0; r < RefineSteps; r++) {
            float m = 0.5 * (lo + hi);
            vec3 q = position + R * m;
            if (q.z > readDepthLod(ToScreen(q))) hi = m; else lo = m;
        }
        vec2 hit = ToScreen(position + R * hi);
        vec3 hitNormal = ViewNormal(hit);
        if (!IsWeapon(hit) && dot(hitNormal, R) < 0.2) {
            colour = linearize(textureLod(sampler2D(t_rendered, LINEAR_S), hit, 0.0).rgb);
            vec2 edge = min(hit, 1.0 - hit);
            confidence = saturate(min(edge.x, edge.y) * 12.0) * (1.0 - smoothstep(0.75, 1.0, hi / RayLength));
        }
    }
    bool wantRay = RtOn && ((RtFade(roughness) > 0.0 && (confidence < 0.999 || abs(RtDebugView - 3.0) < 0.5)) || abs(RtDebugView - 5.0) < 0.5);
    o_color = vec4(colour, confidence);
    o_dir = vec4(OctEncode(ViewToWorldDir(R)), 0.0, 0.0);
    o_ray = vec4(depth, roughness + (wantRay ? 2.0 : 0.0), 0.0, 0.0);
}
