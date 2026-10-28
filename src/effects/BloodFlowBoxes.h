#pragma once

#include <algorithm>
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
		bool		Visible = true;
		float		SlopeX = 0.0f, SlopeY = 0.0f;
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

	inline float& SplatUp() { static float v = 1.0f; return v; }
	inline float& PoolUp() { static float v = 0.3f; return v; }

	inline void Extent(const Item& item, float dripLength, float maxFall, float& reach, float& low, float& high) {
		const float r = item.Radius;
		if (item.Pool) {
			reach = r * 4.6f + 8.0f;
			low = 8.0f + r * 2.5f;
			high = 5.0f + PoolUp() * r;
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
		if (!item.Pool) {
			reach = (std::max)(reach, r * 8.0f + 4.0f);
			low = (std::max)(low, r * 8.0f + 4.0f);
			high = (std::max)(high, r * SplatUp() + 6.0f);
		}
	}

	inline bool OnScreen(const Item& item, float dripLength, float maxFall, const D3DXVECTOR3& eye, const D3DXVECTOR3& origin, const D3DXMATRIX& viewProj) {
		float reach, low, high;
		Extent(item, dripLength, maxFall, reach, low, high);
		const D3DXVECTOR3 center = item.Center - eye;
		int outside[6] = {};
		for (int k = 0; k < 8; k++) {
			const D3DXVECTOR3 p = center + D3DXVECTOR3((k & 1) ? reach : -reach, (k & 2) ? reach : -reach, (k & 4) ? high : -low) + origin;
			const D3DXVECTOR4 p4(p.x, p.y, p.z, 1.0f);
			D3DXVECTOR4 c;
			D3DXVec4Transform(&c, &p4, &viewProj);
			if (c.x < -c.w) outside[0]++;
			if (c.x > c.w) outside[1]++;
			if (c.y < -c.w) outside[2]++;
			if (c.y > c.w) outside[3]++;
			if (c.w <= 0.0f) outside[4]++;
			if (c.z > c.w) outside[5]++;
		}
		for (int f = 0; f < 6; f++) if (outside[f] == 8) return false;
		return true;
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
			Extent(item, dripLength, maxFall, reach, low, high);
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
			if (item.Slot < 0 || (item.Slot + 1) * rowsPerItem > rows || !item.Visible) continue;
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

	struct SplatMask {
		static const int CellSize = 512;
		static const int CellsPerRow = 8;
		static const int Cells = 48;
		static const int AtlasWidth = CellSize * CellsPerRow, AtlasHeight = CellSize * (Cells / CellsPerRow);
		static const int FreshFrames = 6;
		static const int MaxBakes = 4;
		static constexpr float MaxTexelSpeed = 16.0f;

		struct Cell { int Slot = -1; float Seed = -1.0f; int N = 0; float Run = -1.0f; bool Valid = false; int Fresh = 0; unsigned LastUsed = 0; };
		Cell		cells[Cells];
		float		codeOfSlot[1024] = {};
		unsigned	frame = 0;
		int			baked = 0;
		int			masked = 0;
		bool		always = false;

		static float E(float reach) { return 1.15f + 3.12f * reach; }
		static float D(float reach, float drips) { return drips * (std::max)(1.3f, 2.4f * reach) + 0.2f; }

		static void GrowRun(const Item& item, float drips, float& grow, float& run) {
			const float g = (std::min)((std::max)(item.Age / 0.12f, 0.0f), 1.0f);
			grow = g * g * (3.0f - 2.0f * g);
			const float l = Length(item.Normal);
			const D3DXVECTOR3 n = l > 1e-6f ? item.Normal / l : D3DXVECTOR3(0.0f, 0.0f, 1.0f);
			const float steep = Length(D3DXVECTOR3(n.x * n.z, n.y * n.z, n.z * n.z - 1.0f));
			run = drips * (std::min)((std::max)(steep * 3.0f - 0.15f, 0.0f), 1.0f) * (1.0f - expf(-(std::max)(item.Age - 0.3f, 0.0f) / 4.0f));
		}

		int Update(const Item* items, int count, float reach, float drips, const D3DXVECTOR3& eye, float screenHeight, float proj22,
			VolumetricSmokeQuads::Vertex* out) {
			frame++;
			baked = masked = 0;
			for (int s = 0; s < 1024; s++) codeOfSlot[s] = 0.0f;
			const float e = E(reach), d = D(reach, drips), span = 2.0f * e + d;
			const float kMax = (std::max)(2.4f * reach, 1.26f);
			int triangles = 0;
			for (int i = 0; i < count; i++) {
				const Item& item = items[i];
				if (item.Pool || !item.Visible || item.Slot < 0 || item.Slot >= 1024) continue;
				int c = -1;
				for (int k = 0; k < Cells; k++) if (cells[k].Slot == item.Slot && cells[k].LastUsed + 1 >= frame) { c = k; break; }
				if (c < 0) {
					for (int k = 0; k < Cells && c < 0; k++) if (cells[k].Slot < 0 || cells[k].LastUsed + 1 < frame) c = k;
					if (c < 0) continue;
					cells[c] = Cell();
					cells[c].Slot = item.Slot;
				}
				Cell& cell = cells[c];
				cell.LastUsed = frame;
				const float r = (std::max)((std::min)(item.Radius, 24.0f), 1.0f);
				const float distance = (std::max)(Length(item.Center - eye) - (e + d) * r, 5.0f);
				const float pixels = span * r / distance * screenHeight * proj22 * 0.5f;
				int n = 32;
				while (n < CellSize && n < pixels) n *= 2;
				float grow, run;
				GrowRun(item, drips, grow, run);
				if (item.Fresh) cell.Fresh = FreshFrames;
				if (cell.Seed != item.Seed) cell.Valid = false;
				const float l = Length(item.Normal);
				const D3DXVECTOR3 nn = l > 1e-6f ? item.Normal / l : D3DXVECTOR3(0.0f, 0.0f, 1.0f);
				const float slope = (std::min)((std::max)(Length(D3DXVECTOR3(nn.x * nn.z, nn.y * nn.z, nn.z * nn.z - 1.0f)) * 3.0f - 0.15f, 0.0f), 1.0f);
				const float runSpeed = drips * slope * 0.25f * expf(-(std::max)(item.Age - 0.3f, 0.0f) / 4.0f);
				const int texels = (std::max)(n, cell.N);
				const bool settled = cell.Fresh == 0 && grow >= 1.0f && runSpeed * kMax * texels / span < MaxTexelSpeed;
				if (cell.Fresh > 0) cell.Fresh--;
				if (!settled && !always) { cell.Valid = false; continue; }
				const bool stale = !cell.Valid || n > cell.N || fabsf(run - cell.Run) > span / ((float)texels * kMax);
				const bool bake = always || (stale && baked < MaxBakes);
				if (!bake && !cell.Valid) continue;
				if (bake) {
					cell.Seed = item.Seed; cell.N = texels; cell.Run = run; cell.Valid = true;
					const float x0 = (float)((c % CellsPerRow) * CellSize), y0 = (float)((c / CellsPerRow) * CellSize);
					VolumetricSmokeQuads::Vertex data = {};
					ItemData(item, D3DXVECTOR3(x0, y0, (float)cell.N), 0.0f, data);
					const float u0 = x0 / AtlasWidth * 2.0f - 1.0f, u1 = (x0 + cell.N) / AtlasWidth * 2.0f - 1.0f;
					const float v0 = 1.0f - y0 / AtlasHeight * 2.0f, v1 = 1.0f - (y0 + cell.N) / AtlasHeight * 2.0f;
					const float corner[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
					static const int order[6] = { 0, 1, 2, 0, 2, 3 };
					for (int t = 0; t < 6; t++) {
						VolumetricSmokeQuads::Vertex& v = out[triangles * 3 + t];
						v = data;
						v.x = corner[order[t]][0]; v.y = corner[order[t]][1]; v.z = 0.5f; v.w = 1.0f;
					}
					triangles += 2;
					baked++;
				}
				codeOfSlot[item.Slot] = 1.0f + (float)c * 1024.0f + (float)cell.N;
				masked++;
			}
			return triangles;
		}

		void Mark(VolumetricSmokeQuads::Vertex* v, int vertices) const {
			for (int i = 0; i < vertices; i++) {
				const float code = floorf(v[i].a[3] + 0.5f);
				const int slot = (int)fmodf(code, 1000.0f);
				v[i].c[3] = slot >= 0 && slot < 1024 ? codeOfSlot[slot] : 0.0f;
			}
		}

		void Reset() { for (int k = 0; k < Cells; k++) cells[k] = Cell(); }
	};
}
