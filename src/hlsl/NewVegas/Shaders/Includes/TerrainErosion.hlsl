
float4 TESR_CameraPosition : register(c180);
float4 TESR_TerrainErosionData : register(c181);
float4 TESR_TerrainErosionRange : register(c182);

float2 ErosionHash(float2 p) {
    float3 p3 = frac(p.xyx * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float3 ErosionLayer(float2 position, float2 slope, float spacing) {
    float2 down = slope / max(length(slope), 1e-5f);
    float2 across = float2(-down.y, down.x);
    float2 cellPosition = position / spacing;
    float2 cell = floor(cellPosition);
    float2 inCell = cellPosition - cell;
    float height = 0.0f;
    float2 gradient = 0.0f;
    float weights = 0.0f;
    [unroll] for (int y = -1; y <= 1; y++) {
        [unroll] for (int x = -1; x <= 1; x++) {
            float2 offset = float2(x, y);
            float2 toPoint = inCell - offset - ErosionHash(cell + offset);
            float weight = exp(-2.0f * dot(toPoint, toPoint));
            float phase = 6.2831853f * dot(toPoint, across);
            height += cos(phase) * weight;
            gradient -= sin(phase) * 6.2831853f * across * weight;
            weights += weight;
        }
    }
    return float3(gradient / (weights * spacing), height / weights);
}

float3 GetErodedNormal(float3 normal, float3 position, float footprint, out float cavity) {
    float2 slope = -normal.xy / max(normal.z, 0.1f);
    float2 anchored = position.xy + TESR_CameraPosition.xy;
    float steepness = saturate(length(slope) * 2.5f);

    float far = smoothstep(TESR_TerrainErosionRange.x, TESR_TerrainErosionRange.y, length(position));
    float layers = lerp(1.0f, lerp(4.0f, 6.0f, TESR_TerrainErosionRange.w), far);
    float minPixels = lerp(4.0f, lerp(4.0f, 2.0f, TESR_TerrainErosionRange.w), far);

    float2 gradient = slope;
    float depth = 0.0f;
    float total = 0.0f;
    float layerSpacing = TESR_TerrainErosionData.y;
    float amplitude = TESR_TerrainErosionData.x * 0.12f * layerSpacing * lerp(TESR_TerrainErosionRange.z, 1.0f, far);
    [loop] for (int i = 0; i < 6; i++) {
        float fade = saturate(layerSpacing / (minPixels * footprint) - 1.0f) * saturate(layers - i);
        [branch] if (fade <= 0.0f) break;
        float3 layer = ErosionLayer(anchored, gradient, layerSpacing);
        float weight = amplitude * fade * steepness;
        gradient += layer.xy * weight;
        depth += layer.z * weight;
        total += weight;
        layerSpacing *= 0.5f;
        amplitude *= 0.5f;
    }
    cavity = total > 0.0f ? saturate(-depth / total) * lerp(TESR_TerrainErosionRange.z, 1.0f, far) : 0.0f;
    return normalize(float3(-gradient.x, -gradient.y, 1.0f));
}
