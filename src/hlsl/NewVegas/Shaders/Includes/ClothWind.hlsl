#define CLOTH_WIND_DECLARED
float4 ClothA : register(c248);
float4 ClothB : register(c249);
float4 ClothC : register(c250);
float4 ClothD : register(c251);
float4 ClothE : register(c252);
float4 ClothF : register(c253);
float4 ClothLimb[12] : register(c226);
float4 ClothLimbParams : register(c238);
float4 ClothLimbGround : register(c239);
float4 ClothPelvis[3] : register(c200);
float4 ClothRing[16] : register(c203);
float4 ClothWalk : register(c219);
float4 ClothLegNow[6] : register(c220);
static float3 ClothWalkMove = 0.0f;

float ClothTri(float c) {
    const float t = abs(frac(c + 0.5f) * 2.0f - 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static const float ClothBillowIn = 1.5f;

bool ClothIsMask5() { return ClothA.y > 4.5f && ClothA.y < 5.5f; }
float3 ClothBillowDir(float3 n, float3 out5) {
    return ClothA.y > 5.5f ? n * (out5.x < 0.0f ? -1.0f : 1.0f) : (ClothIsMask5() ? out5 : n);
}

float3 ClothDisplacement(float3 p, float3 n) {
    const float along = dot(p, ClothB.xyz) * ClothB.w;
    const float wave = ClothTri(along - ClothC.x) * 2.0f - 1.0f;
    const float gust = 0.7f + 0.3f * ClothTri(ClothC.y + dot(p, float3(0.0021f, 0.0027f, 0.0013f)));
    const float r = ClothD.y;
    const float ripple = 0.6f * (ClothTri(dot(p, float3(0.71f, 0.53f, 0.37f)) * r + ClothC.z) * 2.0f - 1.0f)
                       + 0.4f * (ClothTri(dot(p, float3(-0.43f, 0.81f, 0.59f)) * r * 1.7f - ClothC.w) * 2.0f - 1.0f);
    float billow = ClothA.z * wave + ClothA.w * ripple;
    const float in5 = min(billow, 0.0f);
    const float k5 = ClothD.x * gust * (1.0f / ClothBillowIn);
    [flatten] if (ClothIsMask5()) billow += in5 * (rsqrt(1.0f + in5 * in5 * k5 * k5) - 1.0f);
    return ClothD.x * gust * (ClothB.xyz * (0.6f + 0.4f * wave) + n * billow);
}

float3 ClothMotion(float3 bindP) {
    return ClothE.xyz + float3(bindP.x * ClothF.xy + bindP.y * ClothF.zw, 0.0f) + ClothWalkMove;
}

float3 ClothPelvisPoint(float3 bindP) {
    const float4 q = float4(bindP, 1.0f);
    return float3(dot(ClothPelvis[0], q), dot(ClothPelvis[1], q), dot(ClothPelvis[2], q));
}
float3 ClothPelvisVector(float3 v) {
    return float3(dot(ClothPelvis[0].xyz, v), dot(ClothPelvis[1].xyz, v), dot(ClothPelvis[2].xyz, v));
}
float3 ClothRingAt(float u) {
    const float i0 = floor(u);
    const float i1 = i0 >= 15.0f ? 0.0f : i0 + 1.0f;
    return lerp(ClothRing[(int)i0].xyz, ClothRing[(int)i1].xyz, u - i0);
}
static const float ClothPushMargin = 1.0f;
static const float ClothPushBand = 2.0f;
static const float ClothPushSoft = 2.0f;
float3 ClothLegPushSegment(float3 p, float3 o, float4 a, float4 b) {
    const float3 ab = b.xyz - a.xyz;
    const float L = length(ab);
    const float3 e = ab / max(L, 1e-4f);
    const float3 rel = p - a.xyz;
    const float along = dot(rel, e);
    const float R = lerp(a.w, b.w, saturate(along / max(L, 1e-4f))) + ClothPushMargin;
    const float3 pp = rel - along * e;
    const float3 op = o - dot(o, e) * e;
    const float ol = length(op);
    const float3 oh = op / max(ol, 1e-4f);
    const float B = dot(pp, oh);
    const float disc = B * B - (dot(pp, pp) - R * R);
    const float sexit = disc >= 0.0f ? sqrt(max(disc, 0.0f)) - B : -1000.0f;
    const float depth = R - length(pp);
    const float gin = depth > 0.0f ? smoothstep(0.0f, 1.0f, depth * (1.0f / ClothPushBand)) : 0.0f;
    const float g = gin + (1.0f - gin) * smoothstep(0.0f, 1.0f, (B + 1.0f) * 0.5f);
    const float ends = saturate(1.0f - max(-along, along - L) / R);
    const float ko = smoothstep(0.0f, 1.0f, (ol - 0.1f) * (1.0f / 0.3f));
    const float lp = length(pp);
    const float mr = ClothPushSoft * log(1.0f + exp(min((R - lp) * (1.0f / ClothPushSoft), 40.0f))) * ends * (1.0f - ko);
    return oh * (ClothPushSoft * log(1.0f + exp(min(sexit * (1.0f / ClothPushSoft), 40.0f))) * g * ends * ko) + pp / max(lp, 1e-4f) * mr;
}
float3 ClothLegPush(float3 p, float3 o) {
    float3 best = ClothLegPushSegment(p, o, ClothLegNow[0], ClothLegNow[1]);
    float3 q = ClothLegPushSegment(p, o, ClothLegNow[1], ClothLegNow[2]);
    best = dot(q, q) > dot(best, best) ? q : best;
    q = ClothLegPushSegment(p, o, ClothLegNow[3], ClothLegNow[4]);
    best = dot(q, q) > dot(best, best) ? q : best;
    q = ClothLegPushSegment(p, o, ClothLegNow[4], ClothLegNow[5]);
    return dot(q, q) > dot(best, best) ? q : best;
}

float3 ClothInDir(float3 out5) {
    return ClothIsMask5() ? out5 * rsqrt(max(dot(out5, out5), 1e-8f)) : 0.0f;
}

float3 ClothInward5(float3 d, float3 in5) {
    const float s = min(dot(d, in5), 0.0f);
    return d + in5 * (s * (rsqrt(1.0f + s * s * (1.0f / (ClothBillowIn * ClothBillowIn))) - 1.0f));
}

static const float ClothLegTau = 2.0f;
static const float ClothLegMargin = 1.0f;

float4 ClothLegSegment(float3 p, float4 a, float4 b) {
    const float3 ab = b.xyz - a.xyz;
    const float t = saturate(dot(p - a.xyz, ab) / max(dot(ab, ab), 1e-4f));
    const float3 v = p - (a.xyz + ab * t);
    return float4(v, length(v) - lerp(a.w, b.w, t));
}

float4 ClothLegBind(float3 bindP, float4 hip, float4 knee, float4 ankle) {
    const float4 t = ClothLegSegment(bindP, hip, knee);
    const float4 c = ClothLegSegment(bindP, knee, ankle);
    const float2 r = float2(length(t.xyz), length(c.xyz));
    const float2 e = exp2((min(t.w, c.w) - float2(t.w, c.w)) * (1.0f / ClothLegTau));
    const float3 g = e.x * t.xyz / max(r.x, 1e-4f) + e.y * c.xyz / max(r.y, 1e-4f);
    const float rb = dot(e, r) / (e.x + e.y);
    const float sb = dot(e, float2(t.w, c.w)) / (e.x + e.y);
    return float4(g * (rsqrt(max(dot(g, g), 1e-8f)) * rb), (rb - sb) / max(rb, 1e-3f));
}

float3 ClothLegLimit(float3 d, float4 leg, float3 up, float mask) {
    const float r = length(leg.xyz);
    const float3 n = leg.xyz / max(r, 1e-4f);
    const float3 a = up - dot(up, n) * n;
    const float dn = dot(d, n);
    const float da = dot(d, a) * rsqrt(max(dot(a, a), 1e-8f));
    const float T2 = max(dot(d, d) - dn * dn - da * da, 0.0f) * (mask * mask);
    const float R = r * leg.w + ClothLegMargin;
    const float allow = max(ClothBillowIn, (r - sqrt(max(R * R - T2, 0.0f))) / max(mask, 1e-3f));
    const float s = min(dn, 0.0f);
    const float q = s / allow;
    return d + n * (s * rsqrt(sqrt(1.0f + q * q * q * q)) - s);
}

float3 ClothLimit5(float3 d, float3 in5, float4 legL, float4 legR, float3 up, float mask) {
    d = ClothInward5(d, in5);
    [branch] if (legL.w + legR.w > 0.0f) {
        [unroll] for (int i = 0; i < 2; i++) {
            d = ClothLegLimit(d, legL, up, mask);
            d = ClothLegLimit(d, legR, up, mask);
        }
    }
    return d;
}

float ClothLimbDistance(float3 p, float4 a, float4 b) {
    const float3 ab = b.xyz - a.xyz;
    const float t = saturate(dot(p - a.xyz, ab) / max(dot(ab, ab), 1e-4f));
    return length(p - (a.xyz + ab * t)) - lerp(a.w, b.w, t);
}

float3 ClothOutwardBind(float3 bindP) {
    const float2 o = bindP.xy - ClothLimbGround.zw;
    const float l = length(o);
    return float3(o / max(l, 1e-3f) * saturate(l * 0.25f), 0.0f);
}

float ClothMask(float4 vertexColor, float3 p, float3 bindP, float weight5) {
    float m;
    [branch] if (ClothA.y > 4.5f) m = saturate(weight5);
    else [branch] if (ClothA.y > 3.5f) {
        float d = ClothLimbDistance(bindP, ClothLimb[0], ClothLimb[1]);
        d = min(d, ClothLimbDistance(bindP, ClothLimb[1], ClothLimb[2]));
        d = min(d, ClothLimbDistance(bindP, ClothLimb[2], ClothLimb[3]));
        d = min(d, ClothLimbDistance(bindP, ClothLimb[4], ClothLimb[5]));
        d = min(d, ClothLimbDistance(bindP, ClothLimb[5], ClothLimb[6]));
        d = min(d, ClothLimbDistance(bindP, ClothLimb[6], ClothLimb[7]));
        d = min(d, ClothLimbDistance(bindP, ClothLimb[8], ClothLimb[9]));
        d = min(d, ClothLimbDistance(bindP, ClothLimb[10], ClothLimb[11]));
        const float off = saturate((d - ClothLimbParams.z) * ClothLimbParams.w);
        const float h = saturate((ClothLimbParams.x - bindP.z) * ClothLimbParams.y);
        const float g = saturate((bindP.z - ClothLimbGround.x) * ClothLimbGround.y);
        m = off * off * (3.0f - 2.0f * off) * h * h * (3.0f - 2.0f * h) * g * g * (3.0f - 2.0f * g);
    }
    else [flatten] if (ClothA.y > 2.5f) {
        m = saturate((ClothD.z - bindP.z) * ClothD.w);
        m = m * m * (3.0f - 2.0f * m);
        const float cut = (ClothA.y - 3.0f) * 1000.0f;
        [flatten] if (cut > 0.5f) m *= saturate((bindP.z - cut) * 0.1f);
    }
    else m = ClothA.y > 1.5f ? saturate((ClothD.z - p.z) * ClothD.w) : (ClothA.y > 0.5f ? 1.0f : saturate(vertexColor.r));
    return ClothA.x * m;
}

void ClothWind(inout float3 p, inout float3 T, inout float3 B, inout float3 N, float4 vertexColor, float3 bindP, float weight5, float3 out5,
               float4 legL5, float4 legR5, float3 up5) {
    [branch] if (ClothA.x > 0.0f) {
        const float mask = ClothMask(vertexColor, p, bindP, weight5);
        const float e = 2.0f;
        const float3 n = normalize(N);
        const float3 bill = ClothBillowDir(n, out5);
        const float3 motion = ClothMotion(bindP);
        const float3 in5 = ClothInDir(out5);
        const float3 d0 = mask * ClothLimit5(ClothDisplacement(p, bill) + motion, in5, legL5, legR5, up5, mask);
        const float3 dt = mask * ClothLimit5(ClothDisplacement(p + T * e, bill) + motion, in5, legL5, legR5, up5, mask);
        const float3 db = mask * ClothLimit5(ClothDisplacement(p + B * e, bill) + motion, in5, legL5, legR5, up5, mask);
        const float3 t2 = T * e + (dt - d0);
        const float3 b2 = B * e + (db - d0);
        float3 n0 = normalize(cross(T, B));
        float3 n2 = normalize(cross(t2, b2));
        n0 = dot(n0, N) < 0.0f ? -n0 : n0;
        n2 = dot(n2, N) < 0.0f ? -n2 : n2;
        p += d0;
        T = normalize(t2) * length(T);
        B = normalize(b2) * length(B);
        N = normalize(n + (n2 - n0)) * length(N);
    }
}

float3 ClothWindPosition(float3 p, float3 n, float4 vertexColor, float3 bindP, float weight5, float3 out5, float4 legL5, float4 legR5, float3 up5) {
    [branch] if (ClothA.x > 0.0f) {
        const float l = length(n);
        const float3 bill = ClothBillowDir(l > 1e-4f ? n / l : 0.0f, out5);
        const float mask = ClothMask(vertexColor, p, bindP, weight5);
        p += mask * ClothLimit5(ClothDisplacement(p, bill) + ClothMotion(bindP), ClothInDir(out5), legL5, legR5, up5, mask);
    }
    return p;
}

void ClothWindTBN(inout float3 p, inout float3x3 tbn, float4 vertexColor, float3 bindP, float weight5, float3 out5, float4 legL5, float4 legR5, float3 up5) {
    float3 T = tbn[0], B = tbn[1], N = tbn[2];
    ClothWind(p, T, B, N, vertexColor, bindP, weight5, out5, legL5, legR5, up5);
    tbn = float3x3(T, B, N);
}
