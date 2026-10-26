// Normals as Vulkan passes (needs NVR DXVK): Effects/Normals.fx.hlsl ComputeNormals and the normal half of CombineDepth.fx.hlsl's
// DepthNormals, line for line. Shared by Normals.frag and DepthNormals.frag (effects/Normals.cpp, effects/CombineDepth.cpp).
// A depth pair is (linear01, post-projection depth) as Normals.fx.hlsl's DepthPair makes it.

// HLSL mul(float4(...), TESR_InvProjectionTransform) is invProjection * v (the matrix's stored rows are its GLSL columns).
vec3 reconstructViewPosition(vec2 uv, float depth, mat4 invProjection) {
    vec4 viewSpace = invProjection * vec4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    return viewSpace.xyz / viewSpace.w;
}

// Depth.hlsl's projectedDepthFromLinear: the post-projection depth of a linear one.
vec2 depthPairFromLinear(float linear01, float nearZ, float farZ) {
    float viewZ = max(linear01 * farZ, 1.0e-6);
    return vec2(linear01, nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ)));
}

// The normal of a pixel from its depth pairs: c the centre, r1/r2 one and two texels to the right, l1/l2 to the left,
// t1/t2 up (towards -y), b1/b2 down. Returns the view-space normal (not compressed).
vec3 viewNormalFromTaps(vec2 uv, vec2 texel, mat4 invProjection, float farZ,
                        vec2 c, vec2 r1, vec2 r2, vec2 l1, vec2 l2, vec2 t1, vec2 t2, vec2 b1, vec2 b2) {
    // improved normal reconstruction algorithm from https://gist.github.com/bgolus/a07ed65602c009d5e2f753826e8078a0
    float depth = c.x * farZ;
    vec4 H = vec4(r1.x, l1.x, r2.x, l2.x) * farZ;
    vec4 V = vec4(t1.x, b1.x, t2.x, b2.x) * farZ;

    vec2 he = abs((2.0 * H.xy - H.zw) - depth);
    vec2 ve = abs((2.0 * V.xy - V.zw) - depth);

    vec3 centerPoint = reconstructViewPosition(uv, c.y, invProjection);
    vec3 rightPoint = reconstructViewPosition(uv + vec2(1.0, 0.0) * texel, r1.y, invProjection);
    vec3 leftPoint = reconstructViewPosition(uv + vec2(-1.0, 0.0) * texel, l1.y, invProjection);
    vec3 topPoint = reconstructViewPosition(uv + vec2(0.0, -1.0) * texel, t1.y, invProjection);
    vec3 bottomPoint = reconstructViewPosition(uv + vec2(0.0, 1.0) * texel, b1.y, invProjection);
    vec3 left = centerPoint - leftPoint;
    vec3 right = rightPoint - centerPoint;
    vec3 down = centerPoint - bottomPoint;
    vec3 up = topPoint - centerPoint;

    vec3 hDeriv = he.x > he.y ? left : right;
    vec3 vDeriv = ve.x > ve.y ? down : up;

    return normalize(cross(vDeriv, hDeriv));
}
