
float4 TESR_EnvCube : register(c190);
samplerCUBE TESR_ReflectionsEnv : register(s10) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

float4 LiveCube(float3 worldDir, float roughness) {
    float lod = roughness * TESR_EnvCube.z;
    float4 sharp = texCUBElod(TESR_ReflectionsEnv, float4(worldDir, lod));
    float4 coarse = texCUBElod(TESR_ReflectionsEnv, float4(worldDir, min(lod + 2.0f, TESR_EnvCube.z)));
    float4 m = sharp + coarse * (1.0f - saturate(sharp.a));
    return float4(sqrt(max(m.rgb / max(m.a, 1.0e-4f), 0.0f)), saturate(m.a));
}
