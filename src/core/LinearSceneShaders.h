#pragma once

#include <d3d9.h>
#include <unordered_set>

namespace LinearScene {
	inline std::unordered_set<IDirect3DPixelShader9*> Capable;
	inline void NoteCapable(IDirect3DPixelShader9* handle) { if (handle) Capable.insert(handle); }
}
