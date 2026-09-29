// Checks the point-light cubemap refresh schedule (src/core/PointShadowSchedule.h).
#include <cstdio>
#include <vector>
#include "../src/core/PointShadowSchedule.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); failures++; } else { std::printf("PASS: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static PointShadowSlotState State(const void* light, float x = 1, float y = 2, float z = 3, float r = 500)
{
	PointShadowSlotState s;
	static int cell, texture;
	s.light = light; s.texture = &texture; s.cell = &cell; s.x = x; s.y = y; s.z = z; s.radius = r; s.valid = true;
	return s;
}

int main()
{
	int lightA, lightB;
	const PointShadowSlotState a = State(&lightA);

	// Interval 1 (the default) redraws everything every frame, whatever the state.
	bool always = true;
	for (unsigned frame = 0; frame < 100; frame++) for (unsigned slot = 0; slot < 12; slot++) always &= PointShadowNeedsRedraw(a, a, frame, slot, 1);
	CHECK(always, "interval 1 redraws every slot on every frame");
	CHECK(PointShadowNeedsRedraw(PointShadowSlotState(), a, 5, 0, 1), "interval 1 redraws a never-drawn slot");

	// Stable slot: exactly one redraw per `interval` frames, for every slot, over long runs (including 3, which does not divide the old 8-frame counter).
	for (unsigned interval = 2; interval <= 4; interval++) {
		bool exactlyOnce = true;
		for (unsigned slot = 0; slot < 12; slot++)
			for (unsigned start = 0; start < 50; start++) {
				unsigned redraws = 0;
				for (unsigned f = start; f < start + interval; f++) redraws += PointShadowNeedsRedraw(a, a, f, slot, interval);
				exactlyOnce &= redraws == 1;
			}
		CHECK(exactlyOnce, "interval %u: a stable slot is redrawn exactly once in any %u consecutive frames", interval, interval);
	}

	// Staggering: with 12 slots at interval 2 or 3 the per-frame redraw count is spread evenly.
	for (unsigned interval = 2; interval <= 4; interval++) {
		unsigned minPer = 99, maxPer = 0;
		for (unsigned f = 0; f < 60; f++) {
			unsigned n = 0;
			for (unsigned slot = 0; slot < 12; slot++) n += PointShadowNeedsRedraw(a, a, f, slot, interval);
			minPer = n < minPer ? n : minPer; maxPer = n > maxPer ? n : maxPer;
		}
		CHECK(maxPer - minPer <= 0, "interval %u: 12 slots redraw %u per frame every frame (staggered evenly)", interval, minPer);
	}

	// Anything that changes what the slot describes forces an immediate redraw, on a frame it would otherwise skip.
	const unsigned skipFrame = 1, slot = 0, interval = 4; // (1 + 0) % 4 != 0
	CHECK(!PointShadowNeedsRedraw(a, a, skipFrame, slot, interval), "the test frame is one where a stable slot is skipped");
	CHECK(PointShadowNeedsRedraw(a, State(&lightB), skipFrame, slot, interval), "a different light in the slot is redrawn at once");
	CHECK(PointShadowNeedsRedraw(a, State(&lightA, 1.001f), skipFrame, slot, interval), "a moved light (x) is redrawn at once");
	CHECK(PointShadowNeedsRedraw(a, State(&lightA, 1, 2.5f), skipFrame, slot, interval), "a moved light (y) is redrawn at once");
	CHECK(PointShadowNeedsRedraw(a, State(&lightA, 1, 2, 3.5f), skipFrame, slot, interval), "a moved light (z) is redrawn at once");
	CHECK(PointShadowNeedsRedraw(a, State(&lightA, 1, 2, 3, 501), skipFrame, slot, interval), "a changed radius is redrawn at once");
	PointShadowSlotState otherCell = a; int cell2; otherCell.cell = &cell2;
	CHECK(PointShadowNeedsRedraw(a, otherCell, skipFrame, slot, interval), "a different cell is redrawn at once");
	PointShadowSlotState newTexture = a; int texture2; newTexture.texture = &texture2;
	CHECK(PointShadowNeedsRedraw(a, newTexture, skipFrame, slot, interval), "a recreated cubemap texture (device reset) is redrawn at once");
	CHECK(PointShadowNeedsRedraw(PointShadowSlotState(), a, skipFrame, slot, interval), "a slot that was never drawn is redrawn at once");

	// A light that moves every frame (carried torch) is redrawn every frame at any interval.
	bool moving = true;
	PointShadowSlotState last = State(&lightA, 0);
	for (unsigned f = 0; f < 40; f++) {
		PointShadowSlotState now = State(&lightA, (float)f + 1);
		moving &= PointShadowNeedsRedraw(last, now, f, 5, 4);
		last = now;
	}
	CHECK(moving, "a light that moves every frame is redrawn every frame at interval 4");

	std::printf(failures ? "\n%d check(s) FAILED\n" : "\nAll point shadow schedule checks passed\n", failures);
	return failures ? 1 : 0;
}
