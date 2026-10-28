
layout(set = 2, binding = 0, std140) uniform Consts {
    mat4 shadowToLight;
    mat4 projection;
    mat4 invProjection;
    mat4 view;
    mat4 invView;
    vec4 cameraData;
    vec4 depthConstants;
    vec4 cameraPosition;
    vec4 reciprocalResolution;
    vec4 gameTime;
    vec4 sunDirection;
    vec4 sunPosition;
    vec4 sunAmount;
    vec4 sunColor;
    vec4 sunAmbient;
    vec4 skyData;
    vec4 skyColor;
    vec4 skyLowColor;
    vec4 horizonColor;
    vec4 orthoData;
    vec4 waterSettings;
    vec4 wetWorldData;
    vec4 wetWorldLook;
    vec4 lightPosition[12];
    vec4 shadowLightPosition[12];
    vec4 lightColor[24];
} c;

#define farZ (c.cameraData.y)
#define nearZ (c.cameraData.x)
#define DrawD (c.orthoData.x * 0.9)

const float PuddleBlurRadius = 12.0;
const float PuddleBlurDepthTolerance = 0.08;
const float CurvatureWorldRadius = 100.0;
const float CurvatureDepthSign = 1.0;
const float CurvatureSurfaceTolerance = 0.0005;
const float DepressionMinCurvature = 0.25;
const float DepressionFullCurvature = 2.0;
const float DepressionSlopeFadeStart = 0.906;
const float DepressionSlopeCutoff = 0.707;
const float DepressionMaxDepthJump = 80.0;

float smoothstepH(float a, float b, float x) {
    float t = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
vec2 saturate(vec2 x) { return clamp(x, vec2(0.0), vec2(1.0)); }
vec4 saturate(vec4 x) { return clamp(x, vec4(0.0), vec4(1.0)); }

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.sLin)), coord).x * farZ;
}

float projectedDepthFromLinear(float linear01) {
    float viewZ = max(linear01 * farZ, 1.0e-6);
    return nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ));
}

vec3 reconstructPosition(vec2 uv) {
    float x = uv.x * 2.0 - 1.0;
    float y = (1.0 - uv.y) * 2.0 - 1.0;
    float z = projectedDepthFromLinear(texture(sampler2D(t_depth, NVR_SAMPLER(pc.sLin)), uv).x);
    vec4 viewSpace = vec4(x, y, z, 1.0) * c.invProjection;
    viewSpace /= viewSpace.w;
    return viewSpace.xyz;
}

vec3 projectPosition(vec3 position) {
    vec4 projection = vec4(position, 1.0) * c.projection;
    projection.xyz /= projection.w;
    projection.x = projection.x * 0.5 + 0.5;
    projection.y = 0.5 - 0.5 * projection.y;
    return projection.xyz;
}

vec3 toWorld(vec2 tex) {
    vec3 v = c.view[2].xyz;
    v += (1.0 / c.projection[0][0] * (2.0 * tex.x - 1.0)) * c.view[0].xyz;
    v += (-1.0 / c.projection[1][1] * (2.0 * tex.y - 1.0)) * c.view[1].xyz;
    return v;
}

vec3 GetWorldNormal(vec2 uv) {
    vec4 normal = vec4(texture(sampler2D(t_normals, NVR_SAMPLER(pc.sPoint)), uv).xyz * 2.0 - 1.0, 1.0);
    return (c.view * normal).xyz;
}

float SampleOrthoDepth(vec2 uv) {
    return textureLod(sampler2D(t_ortho, NVR_SAMPLER(pc.sLin)), uv, 0.0).r;
}

vec4 GetOrthoSurfacePosition(vec2 screenUV) {
    vec3 viewPosition = reconstructPosition(screenUV);
    vec3 relativeWorldPosition = (vec4(viewPosition, 0.0) * c.invView).xyz;
    return vec4(relativeWorldPosition, 1.0);
}

float GetRoofMask(vec4 orthoPos) {
    orthoPos.xyz /= orthoPos.w;

    float outofbounds = float(orthoPos.x < -1.0 || orthoPos.x > 1.0 ||
        orthoPos.y < -1.0 || orthoPos.y > 1.0 ||
        orthoPos.z < 0.0 || orthoPos.z > 1.0);

    orthoPos.x = orthoPos.x * 0.5 + 0.5;
    orthoPos.y = orthoPos.y * -0.5 + 0.5;

    float ortho = SampleOrthoDepth(orthoPos.xy);

    return float(ortho > orthoPos.z - 0.0001) * (1.0 - outofbounds);
}

float GetOrthoDepthScale() {
    vec3 depthAxis = c.shadowToLight[2].xyz;
    return max(length(depthAxis) / max(abs(c.shadowToLight[3][3]), 0.000001), 0.00000001);
}

vec2 GetCurvatureSampleRadius() {
    vec3 axisX = c.shadowToLight[0].xyz;
    vec3 axisY = c.shadowToLight[1].xyz;
    float invW = 1.0 / max(abs(c.shadowToLight[3][3]), 0.000001);
    return max(vec2(length(axisX), length(axisY)) * (0.5 * CurvatureWorldRadius * invW), vec2(0.000001));
}

float GetViewModelMask(vec2 screenUV) {
    float vmDepthX = texture(sampler2D(t_depthVM, NVR_SAMPLER(pc.sLin)), screenUV).x;
    return ((c.depthConstants.z == 0.0 && vmDepthX < 0.9) ||
        (c.depthConstants.z > 0.0 && vmDepthX > 0.01)) ? 1.0 : 0.0;
}

float GetPuddleViewModelExclusion(vec2 uv, float centerMask) {
    vec2 pixel = c.reciprocalResolution.xy;
    float mask = centerMask;
    mask = max(mask, GetViewModelMask(uv + vec2(pixel.x, 0.0)));
    mask = max(mask, GetViewModelMask(uv - vec2(pixel.x, 0.0)));
    mask = max(mask, GetViewModelMask(uv + vec2(0.0, pixel.y)));
    mask = max(mask, GetViewModelMask(uv - vec2(0.0, pixel.y)));
    return mask;
}

float ComputeDepressionMask(vec4 relativeSurface, float viewDepth, float vmMask) {
    vec4 mapPos = relativeSurface * c.shadowToLight;
    mapPos.xyz /= mapPos.w;
    mapPos.x = mapPos.x * 0.5 + 0.5;
    mapPos.y = mapPos.y * -0.5 + 0.5;
    vec2 uv = mapPos.xy;
    vec2 radius = GetCurvatureSampleRadius();
    float valid = step(radius.x, uv.x) * step(uv.x, 1.0 - radius.x)
        * step(radius.y, uv.y) * step(uv.y, 1.0 - radius.y)
        * step(0.0, mapPos.z) * step(mapPos.z, 1.0);

    float cd = SampleOrthoDepth(uv);
    float left = SampleOrthoDepth(uv + vec2(-radius.x, 0.0));
    float right = SampleOrthoDepth(uv + vec2(radius.x, 0.0));
    float top = SampleOrthoDepth(uv + vec2(0.0, -radius.y));
    float bottom = SampleOrthoDepth(uv + vec2(0.0, radius.y));
    vec2 diagonalRadius = radius * 0.70710678;
    float d1 = SampleOrthoDepth(uv - diagonalRadius);
    float d2 = SampleOrthoDepth(uv + diagonalRadius);
    float d3 = SampleOrthoDepth(uv + vec2(-diagonalRadius.x, diagonalRadius.y));
    float d4 = SampleOrthoDepth(uv + vec2(diagonalRadius.x, -diagonalRadius.y));
    float depthScale = GetOrthoDepthScale();
    float meanRing = (left + right + top + bottom + d1 + d2 + d3 + d4) * 0.125;
    float signedCurve = (cd - meanRing) * CurvatureDepthSign / depthScale;
    float concave = smoothstep(DepressionMinCurvature, DepressionFullCurvature, signedCurve);

    vec2 depthGradient = vec2(right - left, bottom - top) / (2.0 * radius);
    vec3 clipGradient = vec3(-0.5 * depthGradient.x, 0.5 * depthGradient.y, 1.0);
    vec3 heightNormal = mat3(c.shadowToLight) * clipGradient;
    heightNormal /= max(length(heightNormal), 0.000001);
    float slopeFilter = smoothstep(DepressionSlopeCutoff, DepressionSlopeFadeStart, abs(heightNormal.z));

    float depthJump = max(max(abs(left - cd), abs(right - cd)),
        max(abs(top - cd), abs(bottom - cd))) / depthScale;
    float edgeFilter = 1.0 - smoothstep(DepressionMaxDepthJump * 0.5, DepressionMaxDepthJump, depthJump);
    concave *= slopeFilter;
    concave *= edgeFilter;

    float surfaceMatch = 1.0 - smoothstep(CurvatureSurfaceTolerance,
        CurvatureSurfaceTolerance * 2.0, abs(mapPos.z - cd));
    valid *= surfaceMatch * step(0.0, viewDepth);
    concave *= valid * (1.0 - vmMask);
    return concave;
}
