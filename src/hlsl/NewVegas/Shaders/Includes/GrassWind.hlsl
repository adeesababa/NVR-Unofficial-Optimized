float GrassPushWeight(float height) {
    return saturate(saturate(height * 0.01f) * 2.0f);
}

#ifdef SHADOW_INVVIEW_REG
float3 GrassCameraRelative(float4 clip) {
    const float4 view = float4(clip.x * TESR_InvProjectionTransform._11, clip.y * TESR_InvProjectionTransform._22, clip.w, 1.0f);
    return mul(view, TESR_InvViewTransform).xyz;
}
#endif

float3 GrassWindMove(float4 inst, float4 lanes, float clock, float swing, float4 windData, float weight, float height) {
    const float2 dir = lanes.xy * rsqrt(dot(lanes.xy, lanes.xy));
    const float gustPhase = clock * 0.25f;
    const float along = dot(inst.xy, lanes.xy);
    const float front = 0.5f + 0.5f * sin(gustPhase - along);
    const float patch = 0.75f + 0.25f * sin(gustPhase - 0.5f * (along + inst.y * lanes.x - inst.x * lanes.y));
    float2 flutter;
    sincos(2.0f * clock + inst.w + (inst.x - inst.y) * 0.0078125f, flutter.x, flutter.y);
    const float alongWind = swing * length(windData.xy) + lanes.z * front * front * patch + lanes.w * flutter.x;
    const float2 xy = (dir * alongWind + float2(-dir.y, dir.x) * (lanes.w * 0.5f * flutter.y)) * (windData.z * weight);
    const float h = max(height, 1e-6f);
    return float3(xy, -min(dot(xy, xy) * 0.5f / h, h));
}
