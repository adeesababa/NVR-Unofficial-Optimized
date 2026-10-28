layout(push_constant) uniform Push {
    vec4 viewTransform[4];
    vec4 projectionTransform[4];
    vec4 reciprocalResolution;
    vec4 blur;
    vec4 data;
    vec4 cameraPosition;
    vec4 cameraData;
    uint samplerIndex;
    float useBokeh;
    uint pad[2];
} pc;

float invlerps(float a, float b, float t) { return saturate((t - a) / (b - a)); }

float smoothstepHLSL(float edge0, float edge1, float x) {
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

vec4 getQuadrants(vec2 uv) {
    float topLeftQuadrant = (uv.x < 0.5 && uv.y < 0.5) ? 1.0 : 0.0;
    float topRightQuadrant = (uv.x > 0.5 && uv.y < 0.5) ? 1.0 : 0.0;
    float bottomLeftQuadrant = (uv.x < 0.5 && uv.y > 0.5) ? 1.0 : 0.0;
    float bottomRightQuadrant = (uv.x > 0.5 && uv.y > 0.5) ? 1.0 : 0.0;
    return vec4(topLeftQuadrant, topRightQuadrant, bottomLeftQuadrant, bottomRightQuadrant);
}

vec2 remapFromQuadrant(vec2 uv, vec4 quadrants) {
    vec2 offset = vec2(0.5);
    vec2 newX = vec2(uv.x, uv.x - offset.x);
    vec2 newY = vec2(uv.y, uv.y - offset.y);
    uv.x = dot(newX.xyxy, quadrants);
    uv.y = dot(newY.xxyy, quadrants);
    return uv * 2.0;
}
