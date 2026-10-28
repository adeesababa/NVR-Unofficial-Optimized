#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 resolution;
    vec4 data;
    vec4 depthData;
    vec4 pred;
    vec4 toneMappingClip;
    uint samplerIndex;
    uint pointSamplerIndex;
    uint mode;
    uint flags;
} pc;

#ifdef SMAA_EDGE_MODE
#define EDGE_MODE uint(SMAA_EDGE_MODE)
#else
#define EDGE_MODE pc.mode
#endif

layout(location = 0) in vec2 uv_in;
layout(location = 0) out vec4 o_color;

#define SMAA_THRESHOLD (pc.data.x > 0.0 ? pc.data.x : 0.05)
#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR (pc.depthData.y > 0.0 ? pc.depthData.y : 2.0)

vec4 SamplePoint(vec2 coord) { return texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), coord); }
vec4 SampleDepth(vec2 coord) { return textureLod(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord, 0.0); }

float DitherGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}
float TonemapDitherAt(vec2 pixel) {
    vec2 p = pixel + pc.toneMappingClip.z * vec2(5.588238, 3.235);
    return (DitherGradientNoise(p) + DitherGradientNoise(p + vec2(47.0, 17.0)) - 1.0) * pc.toneMappingClip.y;
}
float LumaWithoutDither(float luma, vec2 pixel) {
    return luma - TonemapDitherAt(pixel) * saturate(luma * 128.0);
}

#define DISCARD { if ((pc.flags & 1u) != 0u) discard; o_color = vec4(0.0); return; }
#define MARKED(edges) o_color = vec4(edges, 0.0, 1.0)

void LumaEdgeDetection(vec2 texcoord, vec4 offset[3]) {
    vec2 threshold = vec2(SMAA_THRESHOLD, SMAA_THRESHOLD);
    vec3 weights = vec3(0.2126, 0.7152, 0.0722);
    float L = dot(SamplePoint(texcoord).rgb, weights);
    float Lleft = dot(SamplePoint(offset[0].xy).rgb, weights);
    float Ltop = dot(SamplePoint(offset[0].zw).rgb, weights);
    vec4 delta;
    delta.xy = abs(L - vec2(Lleft, Ltop));
    vec2 edges = step(threshold, delta.xy);
    if (dot(edges, vec2(1.0, 1.0)) == 0.0)
        DISCARD
    float Lright = dot(SamplePoint(offset[1].xy).rgb, weights);
    float Lbottom = dot(SamplePoint(offset[1].zw).rgb, weights);
    delta.zw = abs(L - vec2(Lright, Lbottom));
    vec2 maxDelta = max(delta.xy, delta.zw);
    float Lleftleft = dot(SamplePoint(offset[2].xy).rgb, weights);
    float Ltoptop = dot(SamplePoint(offset[2].zw).rgb, weights);
    delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);
    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);
    MARKED(edges);
}

void ColorEdgeDetection(vec2 texcoord, vec4 offset[3]) {
    vec2 threshold = vec2(SMAA_THRESHOLD, SMAA_THRESHOLD);
    vec4 delta;
    vec3 C = SamplePoint(texcoord).rgb;
    vec3 Cleft = SamplePoint(offset[0].xy).rgb;
    vec3 t = abs(C - Cleft);
    delta.x = max(max(t.r, t.g), t.b);
    vec3 Ctop = SamplePoint(offset[0].zw).rgb;
    t = abs(C - Ctop);
    delta.y = max(max(t.r, t.g), t.b);
    vec2 edges = step(threshold, delta.xy);
    if (dot(edges, vec2(1.0, 1.0)) == 0.0)
        DISCARD
    vec3 Cright = SamplePoint(offset[1].xy).rgb;
    t = abs(C - Cright);
    delta.z = max(max(t.r, t.g), t.b);
    vec3 Cbottom = SamplePoint(offset[1].zw).rgb;
    t = abs(C - Cbottom);
    delta.w = max(max(t.r, t.g), t.b);
    vec2 maxDelta = max(delta.xy, delta.zw);
    vec3 Cleftleft = SamplePoint(offset[2].xy).rgb;
    t = abs(C - Cleftleft);
    delta.z = max(max(t.r, t.g), t.b);
    vec3 Ctoptop = SamplePoint(offset[2].zw).rgb;
    t = abs(C - Ctoptop);
    delta.w = max(max(t.r, t.g), t.b);
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);
    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);
    MARKED(edges);
}

vec2 SMAADepthEdges(vec2 texcoord, vec4 offset[3]) {
    vec2 o = pc.depthData.x * pc.resolution.xy;
    float P     = SampleDepth(texcoord + o).r;
    float Pleft = SampleDepth(offset[0].xy + o).r;
    float Ptop  = SampleDepth(offset[0].zw + o).r;

    vec2 delta = abs(vec2(P) - vec2(Pleft, Ptop));
    float depthThreshold = (pc.data.y > 0.0) ? pc.data.y : 0.005;
    return step(vec2(depthThreshold * max(P, 0.000001)), delta);
}

void DepthEdgeDetection(vec2 texcoord, vec4 offset[3]) {
    vec2 edges = SMAADepthEdges(texcoord, offset);

    if (dot(edges, vec2(1.0, 1.0)) == 0.0)
        DISCARD

    MARKED(edges);
}

void LumaDepthEdgeDetection(vec2 texcoord, vec4 offset[3]) {
    vec3 weights = vec3(0.2126, 0.7152, 0.0722);
    float L = dot(SamplePoint(texcoord).rgb, weights);
    float Lleft = dot(SamplePoint(offset[0].xy).rgb, weights);
    float Ltop = dot(SamplePoint(offset[0].zw).rgb, weights);
    vec2 delta = abs(L - vec2(Lleft, Ltop));
    vec2 lumaEdges = step(vec2(SMAA_THRESHOLD, SMAA_THRESHOLD), delta);
    vec2 depthEdges = SMAADepthEdges(texcoord, offset);

    if (dot(lumaEdges, vec2(1.0, 1.0)) == 0.0) {
        if (dot(depthEdges, vec2(1.0, 1.0)) == 0.0)
            DISCARD
        MARKED(depthEdges);
        return;
    }

    float Lright = dot(SamplePoint(offset[1].xy).rgb, weights);
    float Lbottom = dot(SamplePoint(offset[1].zw).rgb, weights);
    vec2 maxDelta = max(delta, abs(L - vec2(Lright, Lbottom)));
    float Lleftleft = dot(SamplePoint(offset[2].xy).rgb, weights);
    float Ltoptop = dot(SamplePoint(offset[2].zw).rgb, weights);
    maxDelta = max(maxDelta, abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop)));
    lumaEdges *= step(vec2(max(maxDelta.x, maxDelta.y)), SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta);

    MARKED(max(lumaEdges, depthEdges));
}

float SMAALumaTap(vec2 coord, vec2 pixel) {
    float l = dot(SamplePoint(coord).rgb, vec3(0.2126, 0.7152, 0.0722));
    if (pc.pred.w > 0.5)
        l = LumaWithoutDither(l, pixel);
    return l;
}

void LumaDepthEdgeDetectionStable(vec2 vpos, vec2 texcoord, vec4 offset[3]) {
    float L = SMAALumaTap(texcoord, vpos);
    float Lleft = SMAALumaTap(offset[0].xy, vpos + vec2(-1.0, 0.0));
    float Ltop = SMAALumaTap(offset[0].zw, vpos + vec2(0.0, -1.0));
    vec2 delta = abs(L - vec2(Lleft, Ltop));
    vec2 depthEdges = SMAADepthEdges(texcoord, offset);
    vec2 threshold = vec2(SMAA_THRESHOLD, SMAA_THRESHOLD);
    if (pc.pred.x > 0.5)
        threshold *= pc.pred.y * (1.0 - pc.pred.z * depthEdges);
    vec2 lumaEdges = step(threshold, delta);

    if (dot(lumaEdges, vec2(1.0, 1.0)) == 0.0) {
        if (dot(depthEdges, vec2(1.0, 1.0)) == 0.0)
            DISCARD
        MARKED(depthEdges);
        return;
    }

    float Lright = SMAALumaTap(offset[1].xy, vpos + vec2(1.0, 0.0));
    float Lbottom = SMAALumaTap(offset[1].zw, vpos + vec2(0.0, 1.0));
    vec2 maxDelta = max(delta, abs(L - vec2(Lright, Lbottom)));
    float Lleftleft = SMAALumaTap(offset[2].xy, vpos + vec2(-2.0, 0.0));
    float Ltoptop = SMAALumaTap(offset[2].zw, vpos + vec2(0.0, -2.0));
    maxDelta = max(maxDelta, abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop)));
    lumaEdges *= step(vec2(max(maxDelta.x, maxDelta.y)), SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta);

    MARKED(max(lumaEdges, depthEdges));
}

void main() {
    vec2 texcoord = uv_in + pc.data.w * pc.resolution.xy;
    vec4 offset[3];
    offset[0] = pc.resolution.xyxy * vec4(-1.0, 0.0, 0.0, -1.0) + texcoord.xyxy;
    offset[1] = pc.resolution.xyxy * vec4( 1.0, 0.0, 0.0,  1.0) + texcoord.xyxy;
    offset[2] = pc.resolution.xyxy * vec4(-2.0, 0.0, 0.0, -2.0) + texcoord.xyxy;

    if (EDGE_MODE == 0u) LumaEdgeDetection(texcoord, offset);
    else if (EDGE_MODE == 1u) ColorEdgeDetection(texcoord, offset);
    else if (EDGE_MODE == 2u) DepthEdgeDetection(texcoord, offset);
    else if (EDGE_MODE == 3u) LumaDepthEdgeDetection(texcoord, offset);
    else LumaDepthEdgeDetectionStable(floor(gl_FragCoord.xy), texcoord, offset);
}
