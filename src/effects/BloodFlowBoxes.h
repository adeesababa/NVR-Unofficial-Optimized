#pragma once

#include <cmath>
#include <cstring>

#include "VolumetricSmokeQuads.h"

namespace BloodFlowBoxes {
	static const int Streams = 6;
	static const int PathColumns = 16;

	struct Item {
		D3DXVECTOR3	Center;
		D3DXVECTOR3	Normal;
		float		Radius;
		float		Age;
		float		Seed;
		float		Fade;
		bool		Pool;
		int			Slot;
		bool		Fresh;
		D3DXVECTOR3	DecalU = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
		int			DecalCode = 0;
		float		Tint = 0.0f;
	};

	inline float Length(const D3DXVECTOR3& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

	inline void ItemData(const Item& item, const D3DXVECTOR3& center, float dripLength, VolumetricSmokeQuads::Vertex& data) {
		const float code = (float)item.Slot + (item.Pool ? 1000.0f : 0.0f) + (item.Fresh ? 2000.0f : 0.0f);
		const float a[4] = { center.x, center.y, center.z, code };
		const float b[4] = { item.Normal.x, item.Normal.y, item.Normal.z, item.Radius };
		const float c[4] = { item.Age, item.Fade, floorf(item.Seed * 4095.0f + 0.5f) + 4096.0f * (float)item.DecalCode, dripLength };
		const float d[4] = { item.DecalU.x, item.DecalU.y, item.DecalU.z, item.Tint };
		memcpy(data.a, a, 16); memcpy(data.b, b, 16); memcpy(data.c, c, 16); memcpy(data.d, d, 16);
	}

	inline int Build(const Item* items, int count, float dripLength, float maxFall, const D3DXVECTOR3& eye, const D3DXVECTOR3& origin,
		const D3DXMATRIX& viewProj, VolumetricSmokeQuads::Vertex* out, float bounds[4], int* splatTriangles = nullptr) {
		float minU = 1.0f, minV = 1.0f, maxU = 0.0f, maxV = 0.0f;
		int triangles = 0;
		const D3DXVECTOR3 u(1.0f, 0.0f, 0.0f), d(0.0f, 1.0f, 0.0f), n(0.0f, 0.0f, 1.0f);
		if (splatTriangles) *splatTriangles = 0;
		for (int i = 0; i < count * 2; i++) {
			if (i == count && splatTriangles) *splatTriangles = triangles;
			const Item& item = items[i % count];
			if (item.Pool != (i >= count)) continue;
			float reach, low, high;
			const float r = item.Radius;
			if (item.Pool) {
				reach = r * 4.6f + 8.0f;
				low = 8.0f + r * 2.5f;
				high = 5.0f + 0.3f * r;
			}
			else if (fabsf(item.Normal.z) > 0.9f) {
				reach = r + dripLength * 0.5f + 6.0f;
				low = r + dripLength * 0.5f + 8.0f;
				high = r + 6.0f;
			}
			else {
				reach = r + dripLength + 6.0f;
				low = r + dripLength + maxFall + 8.0f;
				high = r + 6.0f;
			}
			const float u0 = -reach, u1 = reach, d0 = -reach, d1 = reach, n0 = -low, n1 = high;
			const D3DXVECTOR3 center = item.Center - eye;
			D3DXVECTOR3 corner[8];
			D3DXVECTOR4 clip[8];
			int outside[6] = {};
			bool behind = false;
			for (int k = 0; k < 8; k++) {
				corner[k] = center + u * ((k & 1) ? u1 : u0) + d * ((k & 2) ? d1 : d0) + n * ((k & 4) ? n1 : n0);
				const D3DXVECTOR3 p = corner[k] + origin;
				const D3DXVECTOR4 p4(p.x, p.y, p.z, 1.0f);
				D3DXVec4Transform(&clip[k], &p4, &viewProj);
				if (clip[k].x < -clip[k].w) outside[0]++;
				if (clip[k].x > clip[k].w) outside[1]++;
				if (clip[k].y < -clip[k].w) outside[2]++;
				if (clip[k].y > clip[k].w) outside[3]++;
				if (clip[k].w <= 0.0f) { outside[4]++; behind = true; }
				if (clip[k].z > clip[k].w) outside[5]++;
			}
			bool culled = false;
			for (int f = 0; f < 6; f++) if (outside[f] == 8) culled = true;
			if (culled) continue;
			if (behind) { minU = minV = 0.0f; maxU = maxV = 1.0f; }
			else {
				for (int k = 0; k < 8; k++) {
					const float su = 0.5f + 0.5f * clip[k].x / clip[k].w, sv = 0.5f - 0.5f * clip[k].y / clip[k].w;
					if (su < minU) minU = su;
					if (su > maxU) maxU = su;
					if (sv < minV) minV = sv;
					if (sv > maxV) maxV = sv;
				}
			}
			VolumetricSmokeQuads::Vertex data = {};
			ItemData(item, center, dripLength, data);
			static const int faces[6][4] = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
			for (int f = 0; f < 6; f++) {
				static const int order[6] = { 0, 1, 2, 0, 2, 3 };
				for (int t = 0; t < 6; t++) {
					VolumetricSmokeQuads::Vertex& v = out[triangles * 3 + t];
					v = data;
					const int k = faces[f][order[t]];
					v.x = (k & 1) ? u1 : u0; v.y = (k & 2) ? d1 : d0; v.z = (k & 4) ? n1 : n0; v.w = (float)f;
				}
				triangles += 2;
			}
		}
		bounds[0] = minU; bounds[1] = minV; bounds[2] = maxU; bounds[3] = maxV;
		return triangles;
	}

	inline int BuildProbe(const Item* items, int count, float dripLength, int rows, int rowsPerItem, const D3DXVECTOR3& eye, VolumetricSmokeQuads::Vertex* out) {
		int triangles = 0;
		for (int i = 0; i < count; i++) {
			const Item& item = items[i];
			if (item.Slot < 0 || (item.Slot + 1) * rowsPerItem > rows) continue;
			VolumetricSmokeQuads::Vertex data = {};
			ItemData(item, item.Center - eye, dripLength, data);
			const float y0 = 1.0f - 2.0f * (float)(item.Slot * rowsPerItem) / (float)rows;
			const float y1 = 1.0f - 2.0f * (float)((item.Slot + 1) * rowsPerItem) / (float)rows;
			const float corner[4][2] = { { -1.0f, y0 }, { 1.0f, y0 }, { 1.0f, y1 }, { -1.0f, y1 } };
			static const int order[6] = { 0, 1, 2, 0, 2, 3 };
			for (int t = 0; t < 6; t++) {
				VolumetricSmokeQuads::Vertex& v = out[triangles * 3 + t];
				v = data;
				v.x = corner[order[t]][0]; v.y = corner[order[t]][1]; v.z = 0.5f; v.w = 1.0f;
			}
			triangles += 2;
		}
		return triangles;
	}
}
