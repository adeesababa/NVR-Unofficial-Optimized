
#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_samplerless_texture_functions : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "BloodFluid.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform texture2D t_depth;
layout(set = 1, binding = 1) uniform texture2D t_normals;
layout(set = 1, binding = 2) uniform texture2D t_scene;
layout(set = 1, binding = 3) uniform texture2D t_pointShadow;
layout(set = 1, binding = 4) uniform texture2D t_viewModel;
layout(set = 1, binding = 5, std430) readonly buffer Draws { DrawRec D[]; };
layout(set = 1, binding = 6, std430) readonly buffer TileCounts { uint TileCount[]; };
layout(set = 1, binding = 7, std430) readonly buffer TileLists { uint TileList[]; };
layout(set = 0, binding = 0) uniform sampler s_samplers[];
layout(push_constant) uniform Push { uint samplerIndex; uint pad0, pad1, pad2; } pc;

vec4 Texel(texture2D t, ivec2 px) { return textureLod(sampler2D(t, s_samplers[pc.samplerIndex & 0xFFFFu]), (vec2(px) + 0.5) * F.screen.zw, 0.0); }

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);
}

float Glint(vec3 N, vec3 V, vec3 L, float a2) {
    vec3 H = normalize(L + V);
    float nh = saturate(dot(N, H));
    float lh = saturate(dot(L, H));
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (3.14159 * d * d) * (0.02 + 0.98 * pow(1.0 - lh, 5.0)) * 0.25 / max(lh * lh, 0.1) * saturate(dot(N, L));
}

float LampGlint(vec3 N, vec3 V, vec3 P, vec4 lamp, vec3 color, float a2) {
    if (lamp.w <= 0.0) return 0.0;
    vec3 toLamp = lamp.xyz - P;
    float s = saturate(dot(toLamp, toLamp) / (lamp.w * lamp.w));
    if (s >= 1.0) return 0.0;
    float atten = (1.0 - s) * (1.0 - s) / (1.0 + 5.0 * s);
    return Glint(N, V, normalize(toLamp), a2) * atten * saturate(luma(color));
}

float Highlights(vec3 N, vec3 V, vec3 P, float a2, float lit) {
    float glints = 0.0;
    if (F.match.w > 0.5) glints += Glint(N, V, normalize(F.sunDir.xyz), a2) * lit * saturate(luma(linearize(F.sunColor.rgb)));
    for (int i = 0; i < 12; i++) {
        glints += LampGlint(N, V, P, F.shadowLightPos[i], F.lightColor[i].rgb, a2);
        glints += LampGlint(N, V, P, F.lightPos[i], F.lightColor[i + 12].rgb, a2);
    }
    return glints;
}

vec3 UnpackColour(float t) {
    t = floor(t + 0.5);
    float r = floor(t / 65536.0);
    float gb = t - r * 65536.0;
    float g = floor(gb / 256.0);
    return vec3(r, g, gb - g * 256.0);
}

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    uint tile = uint(px.y >> 4) * F.counts.z + uint(px.x >> 4);
    uint count = min(TileCount[tile], F.counts2.x);
    if (count == 0u) discard;
    if (IsViewModel(Texel(t_viewModel, px).x)) discard;

    float farZ = F.eyeRel.w;
    float depth = min(Texel(t_depth, px).x * farZ, farZ * 0.98);
    bool sky = depth >= farZ * 0.979;
    vec3 rel = RayAt(vec2(px) + 0.5) * depth;
    float sceneDist = length(rel);
    vec3 rd = rel / sceneDist;
    vec3 nv = Texel(t_normals, px).xyz * 2.0 - 1.0;
    vec3 Ns = vec3(dot(F.viewRot0.xyz, nv), dot(F.viewRot1.xyz, nv), dot(F.viewRot2.xyz, nv));
    Ns = dot(Ns, Ns) > 1e-6 ? normalize(Ns) : -rd;
    float aa = max(depth * F.screen.w * 2.0 / F.up.w, 0.05);
    uint debug = F.counts2.z;
    if (debug == 3u) {
        outColor = vec4(fract(depth / 64.0), Texel(t_depth, px).x > 0.0 ? 1.0 : 0.0, 0.0, 0.8);
        return;
    }

    float T = 0.0, ageSum = 0.0, poolSum = 0.0, tintW = 0.0;
    vec3 tintSum = vec3(0.0), grad = vec3(0.0);
    float A = 0.0, nearT = 1e9, dropAge = 0.0, dropTint = 0.0;
    vec3 dropN = -rd;
    vec4 dot_ = vec4(0.0);
    uint base = tile * F.counts2.x;
    for (uint k = 0u; k < count; k++) {
        DrawRec r = D[TileList[base + k]];
        float R = abs(r.a.w);
        float amount = r.c.y;
        if (debug == 2u) {
            vec3 s = ProjectRel(r.a.xyz);
            vec2 dp = s.xy - (vec2(px) + 0.5);
            if (s.z > 1.0 && dot(dp, dp) < 4.0) {
                uint fl = Flags(r.c.w);
                vec3 c = (fl & FlagSettled) != 0u ? vec3(0.35, 0.18, 0.08) : (fl & FlagFrozen) != 0u ? vec3(0.1, 0.3, 1.0) :
                    (fl & FlagGround) != 0u ? vec3(1.0, 0.85, 0.0) : (fl & FlagContact) != 0u ? vec3(1.0, 0.05, 0.05) :
                    (fl & FlagTouched) != 0u ? vec3(1.0, 0.4, 0.7) : vec3(1.0);
                dot_ = vec4(c, 1.0);
            }
            continue;
        }
        if (r.a.w > 0.0) {
            if (sky) continue;
            vec3 dd = rel - r.a.xyz;
            vec3 nn = r.b.xyz;
            float dn = dot(dd, nn);
            vec3 ip = dd - nn * dn;
            float q = dot(ip, ip) / (R * R);
            if (q >= 1.0) continue;
            float keep = saturate(1.0 - abs(dn) / (0.5 * R + 1.0 + aa)) * saturate((dot(Ns, nn) + 0.2) / 0.5) * amount;
            float w1 = 1.0 - q;
            float w = w1 * w1 * keep;
            if (w <= 0.0) continue;
            T += w;
            ageSum += w * r.b.w;
            poolSum += r.c.z > 0.5 && r.c.z < 1.5 ? w : 0.0;
            grad += ip * (-4.0 * w1 / (R * R) * keep);
            if (r.c.x > 0.5) { tintSum += w * UnpackColour(r.c.x); tintW += w; }
        }
        else {
            vec3 c = r.a.xyz;
            float t = dot(c, rd);
            float d2 = dot(c, c) - t * t;
            if (d2 >= R * R) continue;
            float h = sqrt(R * R - d2);
            float tf = t - h;
            if (tf <= 0.0 || tf > sceneDist) continue;
            A += 2.0 * h * amount;
            if (tf < nearT) { nearT = tf; dropN = normalize(rd * tf - c); dropAge = r.b.w; dropTint = r.c.x; }
        }
    }
    if (debug == 2u) {
        if (dot_.a > 0.0) { outColor = vec4(delinearize(dot_.rgb), 0.95); return; }
        discard;
    }

    float th = F.look.z;
    float slope = length(grad);
    float cover = saturate((T - th) / max(slope * aa * 1.5, 0.02) + 0.5);
    bool deep = F.look4.x > 0.5;
    float thick = saturate((T - th) / (deep ? 1.2 : 2.5));
    float coverA = saturate(A / (0.6 + aa));
    float amountAll = max(cover, coverA);
    if (debug == 1u) {
        if (coverA > 0.5) { outColor = vec4(1.0, 1.0, 1.0, 0.9); return; }
        if (cover > 0.5) { outColor = vec4(1.0, 0.15 + 0.6 * (1.0 - thick), 0.1, 0.85); return; }
        if (T > 0.05) { outColor = vec4(1.0, 0.5, 0.0, 0.5); return; }
        discard;
    }
    if (amountAll < 0.004) discard;

    vec3 sceneLin = linearize(max(Texel(t_scene, px).rgb, vec3(0.0)));
    float lit = 0.0;
    if (F.match.w > 0.5) {
        float sunAmbient = max(luma(linearize(F.sunAmbient.rgb)), 0.001);
        vec4 shadows = Texel(t_pointShadow, px);
        lit = saturate(pow(abs((shadows.r + shadows.g) / sunAmbient), 5.0));
    }
    float irradiance = luma(linearize(saturate(F.sunAmbient.rgb))) + lit * luma(linearize(saturate(F.sunColor.rgb))) * saturate(dot(Ns, normalize(F.sunDir.xyz)));
    float sceneLight = max(sceneLin.r, max(sceneLin.g, sceneLin.b)) / 0.3;
    float light = min(max(sceneLight, irradiance), 2.5);
    if (deep) light = min(light, 3.0 * sceneLight);

    float age = T > 1e-4 ? ageSum / T : dropAge;
    float pool = T > 1e-4 ? poolSum / T : 0.0;
    float dryTime = mix(F.look2.y, F.look2.z, pool);
    float dry = cover > 0.0 ? smoothstep(0.0, 1.0, age / (dryTime * mix(0.5, 1.6, thick))) : 0.0;
    vec3 freshColour = F.fresh.rgb;
    vec3 dryColour = F.dryColor.rgb;
    vec3 glintTint = vec3(0.02, 0.004, 0.004);
    if (F.match.z > 0.5 && tintW > 0.5 * T && tintW > 1e-4) {
        freshColour = linearize(saturate(tintSum / tintW / 255.0 * F.match.x)) * F.match.y;
        freshColour *= max(1.0, F.look3.w / max(max(freshColour.r, max(freshColour.g, freshColour.b)), 1e-4));
        dryColour = freshColour * vec3(0.72, 0.8, 0.8) + freshColour.r * vec3(0.0, 0.1, 0.05);
        glintTint = 0.004 + 0.016 * freshColour / max(max(freshColour.r, max(freshColour.g, freshColour.b)), 1e-4);
    }
    vec3 albedo = mix(freshColour, dryColour, dry);
    vec3 at = rel + F.eyeRel.xyz;
    albedo *= mix(1.0, 0.8 + 0.4 * valueNoise((at.xy + at.z) * 0.35), dry);
    if (deep) albedo = max(albedo, vec3(0.04 * max(albedo.r, max(albedo.g, albedo.b))));
    float rim = saturate(1.0 - (T - th) / 0.6) * cover;
    vec3 through = exp(-(1.2 + log(max(max(albedo.r, max(albedo.g, albedo.b)), 0.001) / max(albedo, vec3(0.001)))) * (deep ? 0.6 + 2.4 * thick : 0.25 + 2.0 * thick));
    vec3 blood = (sceneLin * through + light * albedo * mix(1.0, deep ? 0.45 : 0.6, saturate(thick * 1.3 - 0.3)) * (1.0 - through)) * (1.0 - 0.35 * dry * rim);
    blood += light * albedo * (pow(saturate(dot(rd, normalize(F.sunDir.xyz))), 4.0) * lit * (1.0 - dry) * (1.0 - thick) * 0.8);
    vec3 colour = mix(sceneLin, blood, cover);

    vec3 hue = saturate((glintTint - 0.004) / 0.016);
    float gloss = F.look2.w * (1.0 - dry) * cover * mix(0.5, 1.0, thick);
    vec3 V = -rd;
    if (gloss > 0.001) {
        vec3 bend = grad * F.look.w;
        if (F.rework.z > 0.5) {
            bend /= max(T / max(th, 0.05), 1.0);
            float bl = length(bend);
            if (bl > 0.5) bend *= 0.5 / bl;
        }
        vec3 Nb = normalize(Ns - bend);
        float rough = mix(0.12, 0.45, dry);
        float a2 = rough * rough * rough * rough;
        float glints = Highlights(Nb, V, rel, a2, lit);
        float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(Nb, V)), 5.0);
        vec3 surroundings = F.match.w > 0.5 ? linearize(saturate(F.horizon.rgb)) : vec3(light * 0.1);
        if (deep) surroundings *= saturate(sceneLight / max(irradiance, 0.05));
        vec3 sheenTint = mix(vec3(1.0), hue, F.look3.z);
        colour += (min(glints, 1.0) * 0.4 * min(light, 1.2) + min(fresnel, F.look3.y) * surroundings * sheenTint * (1.0 - rough) * 0.5 * F.look3.x) * gloss;
    }
    if (coverA > 0.0) {
        vec3 dropColour = freshColour, dropTintRgb = glintTint;
        if (F.rework.x > 0.5 && F.match.z > 0.5 && dropTint > 0.5 && !(tintW > 0.5 * T && tintW > 1e-4)) {
            dropColour = linearize(saturate(UnpackColour(dropTint) / 255.0 * F.match.x)) * F.match.y;
            dropColour *= max(1.0, F.look3.w / max(max(dropColour.r, max(dropColour.g, dropColour.b)), 1e-4));
            dropTintRgb = 0.004 + 0.016 * dropColour / max(max(dropColour.r, max(dropColour.g, dropColour.b)), 1e-4);
        }
        vec3 drop = light * (dropColour * 0.7 + dropTintRgb * F.look2.w);
        float a2 = 0.12 * 0.12 * 0.12 * 0.12;
        vec3 dropGlint = mix(vec3(1.0), saturate((dropTintRgb - 0.004) / 0.016), F.look3.z) * F.rework.w;
        drop += min(Highlights(dropN, V, rd * nearT, a2, lit), 1.0) * 0.4 * min(light, 1.2) * F.look2.w * dropGlint;
        colour = mix(colour, drop, coverA);
    }
    float ceiling = max(max(sceneLin.r, max(sceneLin.g, sceneLin.b)), 1.0);
    colour = min(colour, vec3(ceiling));

    float alpha = amountAll;
    vec3 full = (colour - sceneLin * (1.0 - amountAll)) / max(amountAll, 0.004);
    full = max(full, vec3(0.0));
    full *= ceiling / max(max(full.r, max(full.g, full.b)), ceiling);
    outColor = vec4(delinearize(full), alpha);
}
