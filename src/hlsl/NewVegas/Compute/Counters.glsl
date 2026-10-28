#ifndef NVR_COUNTERS_GLSL
#define NVR_COUNTERS_GLSL

#ifdef NVR_COUNTERS
#ifndef NVR_COUNTERS_SET
#define NVR_COUNTERS_SET 3
#endif
#ifndef NVR_COUNTERS_BINDING
#define NVR_COUNTERS_BINDING 0
#endif
layout(set = NVR_COUNTERS_SET, binding = NVR_COUNTERS_BINDING, std430) buffer NvrCounterBlock {
    uvec4 header;
    uint  count[32];
    vec4  probe[8];
} nvrDbg;
#define NVR_COUNT(i) atomicAdd(nvrDbg.count[(i)], 1u)
#define NVR_COUNT_N(i, n) atomicAdd(nvrDbg.count[(i)], uint(n))
#define NVR_PROBE_AT(p, slot, v) if (all(equal(uvec2(p), nvrDbg.header.xy))) nvrDbg.probe[(slot)] = vec4(v)
#define NVR_PROBE(slot, v) NVR_PROBE_AT(uvec2(gl_FragCoord.xy), slot, v)
#else
#define NVR_COUNT(i)
#define NVR_COUNT_N(i, n)
#define NVR_PROBE_AT(p, slot, v)
#define NVR_PROBE(slot, v)
#endif

#endif
