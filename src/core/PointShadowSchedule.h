#pragma once

// Decides which point-light shadow cubemap slots are redrawn on a frame.
//
// Every slot's cubemap is rendered from scratch each frame by default. With an update interval N > 1
// a slot is redrawn only every N frames (staggered by slot so the cost is spread out) and keeps its
// previous contents in between -- but only while it still describes the same light: the slot must hold
// the same light object at the same position and radius, in the same cell, and the same texture, or it
// is redrawn at once. Moving occluders (actors, doors) lag by up to N - 1 frames; a static room does
// not change at all. Kept free of engine types so tests/point_shadow_schedule.cpp can check it.
struct PointShadowSlotState {
	const void* light = nullptr;    // the ShadowSceneLight the slot was last drawn for
	const void* texture = nullptr;  // the slot's cubemap texture (a device reset creates a new one)
	const void* cell = nullptr;     // the cell the player was in
	float x = 0, y = 0, z = 0;      // light position
	float radius = 0;               // light radius as used for the cubemap
	bool valid = false;             // the slot has been drawn at least once
};

// Why a slot is redrawn (None = it keeps its cubemap this frame). When several apply, the first listed wins.
enum class PointShadowRedraw {
	None,
	NewSlot,      // the slot was never drawn (first frame, or the light appeared)
	OtherLight,   // the slot now holds a different light than the one it was drawn for
	OtherTexture, // the cubemap texture was recreated (device reset)
	OtherCell,    // the player changed cell
	Moved,        // the light moved
	Resized,      // the light's radius changed
	Scheduled,    // nothing changed: the regular every-N-frames refresh (every frame at interval 1)
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
