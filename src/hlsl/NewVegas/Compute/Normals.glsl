
vec3 reconstructViewPosition(vec2 uv, float depth, mat4 invProjection) {
    vec4 viewSpace = invProjection * vec4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    return viewSpace.xyz / viewSpace.w;
}

vec2 depthPairFromLinear(float linear01, float nearZ, float farZ) {
    float viewZ = max(linear01 * farZ, 1.0e-6);
    return vec2(linear01, nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ)));
}

vec3 viewNormalFromTaps(vec2 uv, vec2 texel, mat4 invProjection, float farZ,
                        vec2 c, vec2 r1, vec2 r2, vec2 l1, vec2 l2, vec2 t1, vec2 t2, vec2 b1, vec2 b2) {
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
