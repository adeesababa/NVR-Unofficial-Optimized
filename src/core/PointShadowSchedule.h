#pragma once

struct PointShadowSlotState {
	const void* light = nullptr;
	const void* texture = nullptr;
	const void* cell = nullptr;
	float x = 0, y = 0, z = 0;
	float radius = 0;
	unsigned long long casterHash = 0;
	bool staticCasters = false;
	bool valid = false;
};

inline unsigned long long PointShadowAddCasterHash(unsigned long long setHash, unsigned long long casterHash)
{
	return setHash + casterHash * 0x9E3779B185EBCA87ULL;
}

enum class PointShadowRedraw {
	None,
	NewSlot,
	OtherLight,
	OtherTexture,
	OtherCell,
	Moved,
	Resized,
	CastersChanged,
	Scheduled,
	Count
};

inline PointShadowRedraw PointShadowRedrawReason(const PointShadowSlotState& last, const PointShadowSlotState& now,
	unsigned frame, unsigned slot, unsigned interval)
{
	if (!last.valid) return PointShadowRedraw::NewSlot;
	if (last.light != now.light) return PointShadowRedraw::OtherLight;
	if (last.texture != now.texture) return PointShadowRedraw::OtherTexture;
	if (last.cell != now.cell) return PointShadowRedraw::OtherCell;
	if (last.x != now.x || last.y != now.y || last.z != now.z) return PointShadowRedraw::Moved;
	if (last.radius != now.radius) return PointShadowRedraw::Resized;
	if (last.staticCasters != now.staticCasters) return PointShadowRedraw::CastersChanged;
	if (now.staticCasters && last.casterHash != now.casterHash) return PointShadowRedraw::CastersChanged;
	if (now.staticCasters) return PointShadowRedraw::None;
	if (interval <= 1 || (frame + slot) % interval == 0) return PointShadowRedraw::Scheduled;
	return PointShadowRedraw::None;
}

inline bool PointShadowSlotChanged(const PointShadowSlotState& last, const PointShadowSlotState& now)
{
	const PointShadowRedraw why = PointShadowRedrawReason(last, now, 0, 0, 0);
	return why != PointShadowRedraw::Scheduled && why != PointShadowRedraw::None;
}

inline bool PointShadowNeedsRedraw(const PointShadowSlotState& last, const PointShadowSlotState& now,
	unsigned frame, unsigned slot, unsigned interval)
{
	return PointShadowRedrawReason(last, now, frame, slot, interval) != PointShadowRedraw::None;
}

enum class PointShadowDraw {
	All,
	StaticThenMoving,
	MovingOnly,
};

inline PointShadowDraw PointShadowDrawPlan(bool split, const PointShadowSlotState& staticLast, const PointShadowSlotState& staticNow,
	bool moving)
{
	if (!split || !staticNow.texture) return PointShadowDraw::All;
	const bool staticCurrent = PointShadowRedrawReason(staticLast, staticNow, 0, 0, 1) == PointShadowRedraw::None;
	if (!moving) return staticCurrent ? PointShadowDraw::MovingOnly : PointShadowDraw::All;
	return staticCurrent ? PointShadowDraw::MovingOnly : PointShadowDraw::StaticThenMoving;
}
