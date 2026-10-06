#pragma once

constexpr int PointShadowSlotsMax = 16;

inline void AssignStablePointShadowSlots(const void* const* previous, const void* const* ranked, int count, int slots,
	const void** out)
{
	bool placed[PointShadowSlotsMax] = {};
	for (int s = 0; s < slots; s++) out[s] = nullptr;
	for (int s = 0; s < slots; s++) {
		if (!previous[s]) continue;
		for (int r = 0; r < count; r++) {
			if (placed[r] || ranked[r] != previous[s]) continue;
			out[s] = ranked[r];
			placed[r] = true;
			break;
		}
	}
	int freeSlot = 0;
	for (int r = 0; r < count; r++) {
		if (placed[r]) continue;
		while (freeSlot < slots && out[freeSlot]) freeSlot++;
		if (freeSlot >= slots) return;
		out[freeSlot++] = ranked[r];
	}
}
