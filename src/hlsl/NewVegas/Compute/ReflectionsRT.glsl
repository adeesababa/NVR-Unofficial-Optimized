// UNOFFICIAL 2026-10-28 (NVR DXVK, RT reflections, docs/next/FORK-RT-REFLECTIONS.md): what ReflectionsTraceRT.frag adds to the trace:
// its own constants, the ray queries into the shared scene (the RT core's TLAS, docs/next/FORK-RT-CORE.md) and the cheap shading
// of a hit. Everything here is reached only through the RT block of ReflectionsTraceRT.frag; the other Reflections passes never
// include it. Spaces: NVR's "camera-relative world" (world axes, origin at the camera) everywhere in the effect; the TLAS may sit
// elsewhere (rtc.rt2.xyz moves a camera-relative point into it).
//
// The RT core adapter (rtTrace / rtVisible / rtHitAlbedo / rtHitNormal) is the only part written against the RT core's API.
// Until that API is fixed it uses a plain TLAS binding and GL_EXT_ray_query, which is what any version of it must give.
#extension GL_EXT_ray_query : require

layout(set = 3, binding = 0) uniform accelerationStructureEXT rtScene;   // the RT core's TLAS this frame

layout(set = 2, binding = 1, std140) uniform RtConstants {
    vec4 rt;    // x 1 = on, y MaxRoughness, z Distance (game units), w DebugView
    vec4 rt2;   // xyz camera-relative -> TLAS space offset, w screen match tolerance (share of the depth)
    vec4 rt3;   // x 1 = per-instance materials are bound, y the estimate's brightness, z 1 = sun shadow rays, w sun amount (0 night / indoors)
    vec4 sunDir;    // world, toward the sun (TESR_SunDirection)
    vec4 sunColor;  // TESR_SunColor (gamma)
} rtc;

#define RtOn (rtc.rt.x > 0.5)
#define RtMaxRoughness rtc.rt.y
#define RtDistance rtc.rt.z
#define RtDebugView rtc.rt.w
#define RtTolerance rtc.rt2.w

struct RtHit {
    bool hit;
    float t;            // along the ray, game units
    int instance;       // gl_InstanceCustomIndexEXT of the hit instance (the RT core's per-draw record)
    int primitive;
    vec2 bary;
    bool front;
};

// One closest-hit ray query. Opaque only: alpha-tested geometry is whatever the RT core marks it (a request: docs).
RtHit rtTrace(vec3 originCamRel, vec3 dir, float tMin, float tMax) {
    RtHit h;
    h.hit = false; h.t = tMax; h.instance = -1; h.primitive = -1; h.bary = vec2(0.0); h.front = true;
    rayQueryEXT q;
    rayQueryInitializeEXT(q, rtScene, gl_RayFlagsOpaqueEXT, 0xFFu, originCamRel + rtc.rt2.xyz, tMin, dir, tMax);
    while (rayQueryProceedEXT(q)) {}
    if (rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        h.hit = true;
        h.t = rayQueryGetIntersectionTEXT(q, true);
        h.instance = rayQueryGetIntersectionInstanceCustomIndexEXT(q, true);
        h.primitive = rayQueryGetIntersectionPrimitiveIndexEXT(q, true);
        h.bary = rayQueryGetIntersectionBarycentricsEXT(q, true);
        h.front = rayQueryGetIntersectionFrontFaceEXT(q, true);
    }
    return h;
}

// Any hit at all between tMin and tMax (a shadow ray: stops at the first).
bool rtVisible(vec3 originCamRel, vec3 dir, float tMin, float tMax) {
    rayQueryEXT q;
    rayQueryInitializeEXT(q, rtScene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
        0xFFu, originCamRel + rtc.rt2.xyz, tMin, dir, tMax);
    while (rayQueryProceedEXT(q)) {}
    return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT;
}

// The hit's surface colour and normal from the RT core's per-instance records. Not there yet (requested from rt-sun-core: an
// average albedo per draw, later a bindless diffuse index + UV stream; a normal needs the triangle's vertices): false = unknown.
bool rtHitAlbedo(RtHit h, out vec3 albedo) { albedo = vec3(0.4); return false; }
bool rtHitNormal(RtHit h, vec3 dir, out vec3 normal) { normal = -dir; return false; }

// ---- Shading a hit the screen cannot show --------------------------------------------------------------------------------
// The cheap version (FORK-RT-REFLECTIONS.md "Hit shading"): a grey (or, with the records, the draw's own) surface, lit by the
// surroundings cube's blurriest mip round its normal (what light the place has) and, outdoors by day, the sun through a shadow ray.
const float RtGuessAlbedo = 0.4;   // as GridAlbedo: desert ground, plaster
vec3 RtShadeHit(RtHit h, vec3 hitCamRel, vec3 dir, out float known) {
    vec3 albedo, normal;
    known = rtHitAlbedo(h, albedo) ? 1.0 : 0.0;
    if (known < 0.5) albedo = vec3(RtGuessAlbedo);
    rtHitNormal(h, dir, normal);
    vec3 ambient = CubeOn > 0.0 ? EnvAlong(normal, 1.0).rgb : SkyAlong(vec3(0.0, 0.0, 1.0)) * 0.5;
    vec3 light = ambient;
    float sunAmount = rtc.rt3.w;
    if (sunAmount > 0.0) {
        vec3 L = normalize(rtc.sunDir.xyz);
        float ndl = saturate(dot(normal, L));
        if (ndl > 0.0 && (rtc.rt3.z < 0.5 || rtVisible(hitCamRel + normal * 2.0, L, 0.5, 30000.0)))
            light += linearize(rtc.sunColor.rgb) * ndl * sunAmount;
    }
    return albedo * light * rtc.rt3.y;
}
