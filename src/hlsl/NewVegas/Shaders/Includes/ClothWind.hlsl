float4 ClothA : register(c248);
float4 ClothB : register(c249);
float4 ClothC : register(c250);
float4 ClothD : register(c251);

float ClothTri(float c) {
    const float t = abs(frac(c + 0.5f) * 2.0f - 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float3 ClothDisplacement(float3 p, float3 n) {
    const float along = dot(p, ClothB.xyz) * ClothB.w;
    const float wave = ClothTri(along - ClothC.x) * 2.0f - 1.0f;
    const float gust = 0.7f + 0.3f * ClothTri(ClothC.y + dot(p, float3(0.0021f, 0.0027f, 0.0013f)));
    const float r = ClothD.y;
    const float ripple = 0.6f * (ClothTri(dot(p, float3(0.71f, 0.53f, 0.37f)) * r + ClothC.z) * 2.0f - 1.0f)
                       + 0.4f * (ClothTri(dot(p, float3(-0.43f, 0.81f, 0.59f)) * r * 1.7f - ClothC.w) * 2.0f - 1.0f);
    return ClothD.x * gust * (ClothB.xyz * (0.6f + 0.4f * wave) + n * (ClothA.z * wave + ClothA.w * ripple));
}

float ClothMask(float4 vertexColor, float3 p) {
    const float m = ClothA.y > 1.5f ? saturate((ClothD.z - p.z) * ClothD.w) : (ClothA.y > 0.5f ? 1.0f : saturate(vertexColor.r));
    return ClothA.x * m;
}

void ClothWind(inout float3 p, inout float3 T, inout float3 B, inout float3 N, float4 vertexColor) {
    [branch] if (ClothA.x > 0.0f) {
        const float mask = ClothMask(vertexColor, p);
        const float e = 2.0f;
        const float3 n = normalize(N);
        const float3 d0 = mask * ClothDisplacement(p, n);
        const float3 dt = mask * ClothDisplacement(p + T * e, n);
        const float3 db = mask * ClothDisplacement(p + B * e, n);
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

float3 ClothWindPosition(float3 p, float3 n, float4 vertexColor) {
    [branch] if (ClothA.x > 0.0f) {
        const float l = length(n);
        p += ClothMask(vertexColor, p) * ClothDisplacement(p, l > 1e-4f ? n / l : 0.0f);
    }
    return p;
}

void ClothWindTBN(inout float3 p, inout float3x3 tbn, float4 vertexColor) {
    float3 T = tbn[0], B = tbn[1], N = tbn[2];
    ClothWind(p, T, B, N, vertexColor);
    tbn = float3x3(T, B, N);
}
