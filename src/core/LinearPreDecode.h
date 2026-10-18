#pragma once

// UNOFFICIAL 2026-10-17 LinearPreDecode ([Shaders.PBR.Linear] PreDecode, needs a restart).
//
// With linear lighting on, every lit-object pixel decoded the same per-draw colour constants again: pow(colour, gamma) for the
// ambient (c1) and each light colour (PSLightColor, c3-c12), up to 8 colours = 48 quarter-rate log/exp per pixel in the 6-lamp
// shaders. Measured in the frame replay (evidence\objects-LOG.md L-O5): ~80% of linear lighting's cost indoors (0.47 of 0.59 ms)
// and ~40% at noon. Here they are decoded ONCE, on the CPU: the game's own registers stay exactly as the game sets them, and a
// decoded copy of c1 and c3-c12 goes to c64 and c66-c75 (copy register = 63 + game register; free in every ObjectTemplate pixel
// shader), which the shaders compiled with LIN_PREDECODED (ObjectTemplate.hlsl) read instead. Whenever such a shader is bound
// (or a state block / reset leaves one bound) the game's c1-c12 are read back from the device and every copy that does not
// match is remade; while it stays bound, the game's new values get their copies as they are set. (A first version tracked the
// game's values from its uploads only and lost them after state blocks: stale copies, a darker picture in game, L-O7.) Shaders register here when created (ShaderRecord::LoadShader)
// and leave when released; the hooks are core/ConstantFilter.h's (frame thread only). The CPU's powf and the GPU's pow
// (exp2(log2)) differ in the last bits only.
#include <d3d9.h>
#include <cmath>
#include <unordered_set>

namespace LinearPreDecode {

	enum { Last = 12, CopyBase = 63, CopyFirst = 64, CopyLast = 75 };
	inline std::unordered_set<const void*> Shaders;	// pixel shaders compiled with LIN_PREDECODED
	inline float Gamma = 1.0f;					// [Shaders.PBR.Linear] Gamma as the shaders use it (TESR_LinearLighting.x), live
	inline bool Active = false;					// a LIN_PREDECODED shader is bound
	inline bool CopiesValid = false;			// c64-c75 hold the decoded copies of every known game value, made with CopyGamma
	inline float CopyGamma = 1.0f;
	inline float Source[Last + 1][4] = {};		// the game value each copy in c64-c75 was made from
	inline bool SourceKnown[Last + 1] = {};
	inline unsigned StatReadFailures = 0;
	inline unsigned StatChecks = 0, StatMismatches = 0;	// F10 self-check (ConstantFilter.h PreDecodeCheck)
	inline unsigned StatCopies = 0, StatRefreshes = 0;

	inline bool Decodes(UINT r) { return r == 1 || (r >= 3 && r <= Last); }

	inline void Decode(float* out, const float* in, float gamma) {
		for (int c = 0; c < 3; c++) out[c] = gamma > 1.0f ? powf(in[c] > 0.0f ? in[c] : 0.0f, gamma) : in[c];	// LinColor
		out[3] = in[3];
	}

	inline void Register(const void* shader) { if (shader) Shaders.insert(shader); }
	inline void Unregister(const void* shader) { if (shader) Shaders.erase(shader); }
	inline bool Is(const void* shader) { return shader && !Shaders.empty() && Shaders.count(shader) != 0; }
}
