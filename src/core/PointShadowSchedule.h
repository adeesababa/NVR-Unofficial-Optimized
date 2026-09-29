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

inline bool PointShadowSlotChanged(const PointShadowSlotState& last, const PointShadowSlotState& now)
{
	return !last.valid || last.light != now.light || last.texture != now.texture || last.cell != now.cell ||
		last.x != now.x || last.y != now.y || last.z != now.z || last.radius != now.radius;
}

inline bool PointShadowNeedsRedraw(const PointShadowSlotState& last, const PointShadowSlotState& now,
	unsigned frame, unsigned slot, unsigned interval)
{
	if (interval <= 1) return true;
	if (PointShadowSlotChanged(last, now)) return true;
	return (frame + slot) % interval == 0;
}
