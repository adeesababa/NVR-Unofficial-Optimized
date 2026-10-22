#pragma once

#include <d3d9.h>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace LinearPreDecode {

	enum { Last = 12, CopyBase = 63, CopyFirst = 64, CopyLast = 75 };
	inline std::unordered_set<const void*> Shaders;
	inline float Gamma = 1.0f;
	inline bool Active = false;
	inline bool CopiesValid = false;
	inline float CopyGamma = 1.0f;
	inline float Source[Last + 1][4] = {};
	inline bool SourceKnown[Last + 1] = {};
	inline unsigned StatReadFailures = 0;
	inline unsigned StatChecks = 0, StatMismatches = 0;
	inline unsigned StatCopies = 0, StatRefreshes = 0;

	inline bool Decodes(UINT r) { return r == 1 || (r >= 3 && r <= Last); }

	inline void Decode(float* out, const float* in, float gamma) {
		for (int c = 0; c < 3; c++) out[c] = gamma > 1.0f ? powf(in[c] > 0.0f ? in[c] : 0.0f, gamma) : in[c];
		out[3] = in[3];
	}

	inline bool UseCache = false;
	struct PowEntry { UInt32 key; float value; };
	inline PowEntry PowTable[4096];
	inline float PowTableGamma = -1.0f;
	inline unsigned StatPowLookups = 0, StatPowMisses = 0;

	inline float CachedPow(float x, float gamma) {
		if (gamma != PowTableGamma) { memset(PowTable, 0xFF, sizeof(PowTable)); PowTableGamma = gamma; }
		UInt32 bits;
		memcpy(&bits, &x, sizeof(bits));
		UInt32 h = (bits ^ (bits >> 16)) * 0x45D9F3Bu;
		h ^= h >> 16;
		PowEntry& e = PowTable[h & 4095];
		StatPowLookups++;
		if (e.key == bits) return e.value;
		StatPowMisses++;
		const float y = powf(x, gamma);
		e.key = bits;
		e.value = y;
		return y;
	}

	inline void DecodeFast(float* out, const float* in, float gamma) {
		if (!UseCache || gamma <= 1.0f) { Decode(out, in, gamma); return; }
		for (int c = 0; c < 3; c++) out[c] = CachedPow(in[c] > 0.0f ? in[c] : 0.0f, gamma);
		out[3] = in[3];
	}

	inline void Register(const void* shader) { if (shader) Shaders.insert(shader); }
	inline void Unregister(const void* shader) { if (shader) Shaders.erase(shader); }
	inline bool Is(const void* shader) { return shader && !Shaders.empty() && Shaders.count(shader) != 0; }
}
