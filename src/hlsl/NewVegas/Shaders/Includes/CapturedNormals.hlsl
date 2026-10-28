#ifndef NVR_CAPTURED_NORMALS_HLSL
#define NVR_CAPTURED_NORMALS_HLSL

#ifndef CAPTURED_NORMALS_ON
    #define CAPTURED_NORMALS_ON 0
#endif

#if CAPTURED_NORMALS_ON
    #define CAPTURED_NORMAL_INVALID float4(0.0f, 0.0f, 0.0f, 0.0f)
    float4 CapturedNormalOut(float3 n, float3 worldPos) {
        const float len2 = dot(n, n);
        [flatten] if (len2 < 1.0e-6f) return CAPTURED_NORMAL_INVALID;
        n *= rsqrt(len2);
        n *= dot(n, worldPos) > 0.0f ? -1.0f : 1.0f;
        #if defined(SKIN) || defined(CAPTURED_ACTOR)
            return float4(ShadowEncodeNormal(n), length(worldPos), 2.0f);
        #else
            return float4(ShadowEncodeNormal(n), length(worldPos), 1.0f);
        #endif
    }
#endif

#endif
