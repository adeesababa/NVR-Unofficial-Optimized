#pragma once

namespace MaxQuality {
	inline bool Active = false;

	inline UINT Divisor(UINT normal) { return Active ? 1u : normal; }

	inline UINT Reduced(UINT full, UINT divisor) { divisor = Divisor(divisor); return (full + divisor - 1) / divisor; }
}
