// UNOFFICIAL 2026-10-28 (NVR DXVK fork, hybrid RT item 2; docs/next/FORK-RT-AO.md) what the RT AO / sky passes share: the scene's
// acceleration structure (the shared RT core's TLAS, docs/next/FORK-RT-CORE.md) and the ray helpers. core/RtAmbient.h binds the TLAS
// (set 3, binding 0) and fills the RT push fields (the including file's push block declares them under NVR_RT: rt, rtOrigin).
// Space: rays are traced in camera-relative world units (NVR's: the view's rotation only, z up) + rtOrigin.xyz = the TLAS's space.
// Each including file starts with #version 460 and #extension GL_EXT_ray_query : require (glslang takes neither from here) and is
// compiled for Vulkan 1.2 (SPIR-V 1.5: ray queries; tools/nvr-dxvk/compile-compute.sh does it for files that use them).

layout(set = 3, binding = 0) uniform accelerationStructureEXT u_tlas;

#define RT_DISTANCE     (pc.rt.x)
#define RT_RAYS         (int(pc.rt.y + 0.5))
#define RT_SKY_DISTANCE (pc.rt.z)
#define RT_SKY_RAYS     (int(pc.rt.w + 0.5))
#define RT_DEBUG        (pc.rtOrigin.w)

// A direction round n, cosine-weighted (Malley: a point on the unit disc lifted to the hemisphere), from u in [0, 1)^2.
vec3 RtCosineDirection(vec3 n, vec2 u) {
    vec3 t = normalize(abs(n.z) < 0.999 ? cross(n, vec3(0.0, 0.0, 1.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(n, t);
    float r = sqrt(u.x);
    float phi = 6.2831853 * u.y;
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0 - u.x, 0.0)));
}

// The i-th point of the R2 sequence (Roberts 2018): even cover of the square for any count, so 1-16 rays stay stratified.
vec2 RtR2(float i) { return fract(i * vec2(0.7548777, 0.5698403)); }

// The bias that lifts a ray's start off its surface: the depth buffer (24 bits, reconstructed at half resolution) and the TLAS's
// triangles disagree by more the further away the pixel is. viewZ in world units.
float RtBias(float viewZ) { return 0.75 + viewZ * 0.003; }

// One ray against the TLAS from p (camera-relative world) along d. Returns x the hit distance (-1: nothing within tMax), y 1 if the
// hit is a triangle's back face. All geometry is taken as opaque (gl_RayFlagsOpaqueEXT: the core's alpha-tested foliage counts whole).
// anyHit: stop at the first hit found (visibility only), else the closest.
vec2 RtTrace(vec3 p, vec3 d, float tMin, float tMax, bool anyHit) {
    rayQueryEXT q;
    uint flags = gl_RayFlagsOpaqueEXT | (anyHit ? gl_RayFlagsTerminateOnFirstHitEXT : 0u);
    rayQueryInitializeEXT(q, u_tlas, flags, 0xFFu, p + pc.rtOrigin.xyz, tMin, d, tMax);
    while (rayQueryProceedEXT(q)) {}
    if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionTriangleEXT) return vec2(-1.0, 0.0);
    return vec2(rayQueryGetIntersectionTEXT(q, true), rayQueryGetIntersectionFrontFaceEXT(q, true) ? 0.0 : 1.0);
}

// Ambient occlusion from RT_RAYS cosine-weighted rays, RT_DISTANCE long, at p (camera-relative world) with world normal n and view
// distance viewZ; noise in [0, 1)^2 (per pixel, new each frame when there is a history to average it). A hit counts in full up to
// half the distance and fades to nothing at the end (no hard edge where a wall comes into reach). A back face hit right at the start
// is a ray that began inside something (the depth's error, or bumps the TLAS does not have): it does not count (open), and
// inside counts it for DebugView 3. Returns the visibility (cosine-weighted, 1 open).
float RtOcclusion(vec3 p, vec3 n, float viewZ, vec2 noise, out float inside, out float meanHit) {
    float bias = RtBias(viewZ);
    vec3 start = p + n * bias;
    float reach = RT_DISTANCE;
    float occlusion = 0.0;
    inside = 0.0;
    meanHit = 0.0;
    int rays = RT_RAYS;
    for (int i = 0; i < rays; i++) {
        vec3 d = RtCosineDirection(n, fract(noise + RtR2(float(i))));
        vec2 hit = RtTrace(start, d, 0.05 * bias, reach, false);
        if (hit.x < 0.0) { meanHit += 1.0; continue; }
        if (hit.y > 0.5 && hit.x < 4.0 * bias) { inside += 1.0; meanHit += 1.0; continue; }
        occlusion += 1.0 - smoothstep(0.5 * reach, reach, hit.x);
        meanHit += hit.x / reach;
    }
    float inv = 1.0 / float(max(rays, 1));
    inside *= inv;
    meanHit *= inv;
    return 1.0 - occlusion * inv;
}

// Sky visibility with fixed directions (no noise): straight up + one ring at 60 degrees up (8) and, with 16 rays, a ring at 30 (8 more;
// the ortho version's two rings), each weighed by its cosine to n; a ray sees the sky if nothing is within RT_SKY_DISTANCE. Only the
// upper hemisphere is sky; the rings turn with turn (a fixed angle per pixel, the same every frame: no flicker). covered: the
// straight-up ray is blocked (SkyFactor's 'covered', DebugView 4 of Skylighting).
float RtSkyVisibility(vec3 p, vec3 n, float viewZ, float turn, out float covered) {
    float bias = RtBias(viewZ) + 8.0;
    vec3 start = p + n * bias;
    float reach = RT_SKY_DISTANCE;
    covered = RtTrace(start, vec3(0.0, 0.0, 1.0), 0.0, reach, true).x >= 0.0 ? 1.0 : 0.0;
    float total = 0.034 * saturate(n.z);
    float open = total * (1.0 - covered);
    int rings = RT_SKY_RAYS > 8 ? 2 : 1;
    for (int k = 0; k < rings; k++) {
        float c = k == 0 ? 0.5 : 0.8660254;      // cos(elevation): 60, then 30 degrees up
        float s = k == 0 ? 0.8660254 : 0.5;
        float ringWeight = k == 0 ? 0.0324 : 0.0884;
        for (int i = 0; i < 8; i++) {
            float a = turn + (float(i) + 0.5 * float(k)) * (3.14159265 / 4.0);
            vec3 d = vec3(cos(a) * c, sin(a) * c, s);
            float w = ringWeight * saturate(dot(d, n));
            if (w <= 0.0) continue;
            total += w;
            if (RtTrace(start, d, 0.0, reach, true).x < 0.0) open += w;
        }
    }
    return total > 1.0e-5 ? open / total : 1.0;
}
