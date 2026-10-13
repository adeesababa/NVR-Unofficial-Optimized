#pragma once

#include <cmath>
#include <cstring>

#include "VolumetricSmokeQuads.h"

// The geometry BloodFlow draws (BloodFlow.fx.hlsl). Kept free of NVR's globals so the preview harness
// (tests\blood_preview.cpp) runs exactly this code.
//  - Build: one box round each splat or pool, drawn into the image (technique BloodFlow).
//  - BuildProbe: one quad per item into the path texture (technique BloodFlowProbe): Streams rows of PathColumns texels
//    per item slot, where the probe pass works out (from the depth buffer) where each of the item's streams of blood
//    runs: down the surface the way gravity pulls, stopping on flat ground or falling off an edge. Once seen clearly for
//    a few frames a path is kept as it is (a slot keeps its rows for as long as the item is drawn every frame). The same
//    quads, one row per item, for the bounds texture (technique BloodFlowBounds: the box round each item's real paths).
namespace BloodFlowBoxes {
	static const int Streams = 6;		// rows per item in the path texture
	static const int PathColumns = 16;	// texels per row: header, middle, 14 points along the path

	// One splat or pool, in absolute world space.
	struct Item {
		D3DXVECTOR3	Center;		// splat: where it hit the wall; pool: the ground point under the body
		D3DXVECTOR3	Normal;		// a guess at the surface's normal (splats: against the way it was thrown; pools: up)
		float		Radius;		// splat: its radius; pool: the largest it grows to
		float		Age;		// seconds
		float		Seed;		// 0-1, stable per splat or body (sent to the shader as a whole number, 0-4095)
		float		Fade;		// 0-1
		bool		Pool;
		int			Slot;		// its rows in the path texture (Streams per slot)
		bool		Fresh;		// the slot's rows hold nothing of this item yet (new, or not drawn last frame)
	};

	inline float Length(const D3DXVECTOR3& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

	// The data every vertex of an item carries (see BloodFlow.fx.hlsl, VSIN). Positions are eye-relative.
	inline void ItemData(const Item& item, const D3DXVECTOR3& center, float dripLength, VolumetricSmokeQuads::Vertex& data) {
		// The seed goes as a whole number, and so does the slot code: the shader rounds them, so the tiny differences
		// interpolation makes between pixels cannot change what it draws.
		const float code = (float)item.Slot + (item.Pool ? 1000.0f : 0.0f) + (item.Fresh ? 2000.0f : 0.0f);
		const float a[4] = { center.x, center.y, center.z, code };
		const float b[4] = { item.Normal.x, item.Normal.y, item.Normal.z, item.Radius };
		const float c[4] = { item.Age, item.Fade, floorf(item.Seed * 4095.0f + 0.5f), dripLength };
		const float d[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		memcpy(data.a, a, 16); memcpy(data.b, b, 16); memcpy(data.c, c, 16); memcpy(data.d, d, 16);
	}

	// eye: the camera's absolute position; origin: where NVR's camera-centred space puts the eye (the inverse view's
	// translation; positions there are absolute - eye + origin); viewProj: that space's view * projection. Writes three
	// vertices per triangle into out (room for 36 per item) and returns the triangle count; bounds gets the union of the
	// boxes' screen areas in UV (min u, min v, max u, max v; the whole screen if a box reaches behind the eye).
	// The boxes are level (world axes): a splat's reaches down far enough for its streams and for drops falling off an
	// edge under it (maxFall); a pool's covers the trickles that run off it downhill.
	inline int Build(const Item* items, int count, float dripLength, float maxFall, const D3DXVECTOR3& eye, const D3DXVECTOR3& origin,
		const D3DXMATRIX& viewProj, VolumetricSmokeQuads::Vertex* out, float bounds[4]) {
		float minU = 1.0f, minV = 1.0f, maxU = 0.0f, maxV = 0.0f;
		int triangles = 0;
		const D3DXVECTOR3 u(1.0f, 0.0f, 0.0f), d(0.0f, 1.0f, 0.0f), n(0.0f, 0.0f, 1.0f);
		for (int i = 0; i < count; i++) {
			const Item& item = items[i];
			float reach, low, high;
			const float r = item.Radius;
			if (item.Pool) {
				reach = r * 4.6f + 8.0f;					// the pool (up to 1.45 r) and trickles running off it (about 3 r more)
				low = 8.0f + r * 2.5f;
				high = 5.0f + 0.3f * r;
			}
			else if (fabsf(item.Normal.z) > 0.9f) {
				// Thrown straight down (or up): most likely on a floor, where it stays; a slope there runs a little.
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
			const D3DXVECTOR3 center = item.Center - eye;   // eye-relative
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
			// The box's faces seen from inside (their outward normal points away from the eye): every pixel the box covers is
			// shaded once, whether the eye is outside the box or in it.
			static const int faces[6][4] = { { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 6, 7, 5 } };
			const D3DXVECTOR3 outward[6] = { -u, u, -d, d, -n, n };
			for (int f = 0; f < 6; f++) {
				const D3DXVECTOR3 faceCenter = (corner[faces[f][0]] + corner[faces[f][1]] + corner[faces[f][2]] + corner[faces[f][3]]) * 0.25f;
				if (D3DXVec3Dot(&outward[f], &faceCenter) <= 0.0f) continue;
				static const int order[6] = { 0, 1, 2, 0, 2, 3 };
				for (int t = 0; t < 6; t++) {
					VolumetricSmokeQuads::Vertex& v = out[triangles * 3 + t];
					v = data;
					const D3DXVECTOR4& p = clip[faces[f][order[t]]];
					v.x = p.x; v.y = p.y; v.z = p.z; v.w = p.w;
				}
				triangles += 2;
			}
		}
		bounds[0] = minU; bounds[1] = minV; bounds[2] = maxU; bounds[3] = maxV;
		return triangles;
	}

	// One quad per item over its rows of a target `rows` high (the path texture: Streams rows per item; the bounds
	// texture: one), the target's whole width, whether the item is on screen or not (its paths must be carried over
	// every frame). Writes 6 vertices per item; returns the triangle count.
	inline int BuildProbe(const Item* items, int count, float dripLength, int rows, int rowsPerItem, const D3DXVECTOR3& eye, VolumetricSmokeQuads::Vertex* out) {
		int triangles = 0;
		for (int i = 0; i < count; i++) {
			const Item& item = items[i];
			if (item.Slot < 0 || (item.Slot + 1) * rowsPerItem > rows) continue;
			VolumetricSmokeQuads::Vertex data = {};
			ItemData(item, item.Center - eye, dripLength, data);
			// D3D9 puts pixel centres on whole screen coordinates: the quad from row r0 to r1 covers rows r0 .. r1 - 1.
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
