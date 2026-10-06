#pragma once

#include <cstdint>
#include <cstring>

namespace VolumetricSmokeQuads {
    struct Vertex { float x, y, z, w; float a[4], b[4], c[4], d[4]; };

    inline int Build(const float* records, int stride, int total, const D3DXMATRIX& viewProj, float sx, float sy, float nearFade,
        Vertex* out, int maxSegments, float bounds[4], Vertex* outBig = nullptr, int* keptBig = nullptr, float bigArea = 2.0f,
        float* area = nullptr, float thinShare = 0.0f) {
        if (keptBig) *keptBig = 0;
        if (area) *area = 0.0f;
        auto lo = [](float p, float q) { return p < q ? p : q; };
        auto hi = [](float p, float q) { return p > q ? p : q; };
        bounds[0] = bounds[1] = 2.0f;
        bounds[2] = bounds[3] = -1.0f;
        int kept = 0;
        for (int i = 0; i < total; i++) {
            const float* r = records + i * stride;
            D3DXVECTOR4 clip[2];
            float radius[2];
            for (int e = 0; e < 2; e++) {
                const D3DXVECTOR4 p(r[e * 4], r[e * 4 + 1], r[e * 4 + 2], 1.0f);
                D3DXVec4Transform(&clip[e], &p, &viewProj);
                radius[e] = r[e * 4 + 3];
            }
            const float reachMax = 3.0f * hi(radius[0], radius[1]);
            if (clip[0].w < nearFade - reachMax && clip[1].w < nearFade - reachMax) continue;
            for (int e = 0; e < 2; e++) {
                const int o = 1 - e;
                if (clip[e].w >= nearFade) continue;
                if (clip[o].w <= nearFade) { clip[e] = clip[o]; radius[e] = radius[o]; continue; }
                const float k = (nearFade - clip[e].w) / (clip[o].w - clip[e].w);
                clip[e] = clip[e] + (clip[o] - clip[e]) * k;
                radius[e] = radius[e] + (radius[o] - radius[e]) * k;
            }
            float u0 = 2.0f, v0 = 2.0f, u1 = -1.0f, v1 = -1.0f;
            for (int e = 0; e < 2; e++) {
                const float depth = hi(clip[e].w, 1.0f);
                const float reach = 3.0f * radius[e];
                const float u = 0.5f + 0.5f * clip[e].x / depth, v = 0.5f - 0.5f * clip[e].y / depth;
                const float du = 0.5f * sx * reach / depth, dv = 0.5f * sy * reach / depth;
                u0 = lo(u0, u - du); u1 = hi(u1, u + du);
                v0 = lo(v0, v - dv); v1 = hi(v1, v + dv);
            }
            if (u1 < 0.0f || u0 > 1.0f || v1 < 0.0f || v0 > 1.0f) continue;
            u0 = hi(u0, 0.0f); v0 = hi(v0, 0.0f); u1 = lo(u1, 1.0f); v1 = lo(v1, 1.0f);
            const float share = (u1 - u0) * (v1 - v0);
            float thin = 1.0f;
            if (thinShare > 0.0f && share > thinShare) {
                float t = (share - thinShare) / thinShare;
                t = t > 1.0f ? 1.0f : t;
                t = t * t * (3.0f - 2.0f * t);
                const float key = stride >= 16 ? r[12] : (float)i;
                uint32_t h;
                std::memcpy(&h, &key, sizeof(h));
                h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
                thin = h % 3u == 0u ? 1.0f + 2.0f * t : 1.0f - t;
                if (thin <= 0.0f) continue;
            }
            bounds[0] = lo(bounds[0], u0); bounds[1] = lo(bounds[1], v0);
            bounds[2] = hi(bounds[2], u1); bounds[3] = hi(bounds[3], v1);
            if (area) *area += share;
            float toBig = 0.0f;
            if (outBig && keptBig) {
                const float lowEdge = 0.75f * bigArea, highEdge = 1.5f * bigArea;
                toBig = share <= lowEdge ? 0.0f : share >= highEdge ? 1.0f : (share - lowEdge) / (highEdge - lowEdge);
                toBig = toBig * toBig * (3.0f - 2.0f * toBig);
            }
            const float x0 = 2.0f * u0 - 1.0f, x1 = 2.0f * u1 - 1.0f, y0 = 1.0f - 2.0f * v0, y1 = 1.0f - 2.0f * v1;
            const float corners[6][2] = { { x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
            auto write = [&](Vertex* dest, float share) {
                for (int k = 0; k < 6; k++) {
                    Vertex& v = dest[k];
                    v.x = corners[k][0]; v.y = corners[k][1]; v.z = 0.5f; v.w = 1.0f;
                    for (int j = 0; j < 4; j++) { v.a[j] = r[j]; v.b[j] = r[4 + j]; v.c[j] = r[8 + j]; }
                    v.c[0] *= share; v.c[1] *= share;
                    if (stride >= 16) for (int j = 0; j < 4; j++) v.d[j] = r[12 + j];
                    else { v.d[0] = v.d[1] = 0.0f; v.d[2] = v.d[3] = r[10]; }
                }
            };
            if (toBig < 1.0f && kept < maxSegments) { write(out + kept * 6, (1.0f - toBig) * thin); kept++; }
            if (toBig > 0.0f && *keptBig < maxSegments) { write(outBig + *keptBig * 6, toBig * thin); (*keptBig)++; }
        }
        return kept;
    }
}
