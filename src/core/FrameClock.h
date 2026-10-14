#pragma once

inline double ElapsedOncePerFrame(double& lastFrameTime) {
	const double now = TheFrameRateManager->Time;
	if (now == lastFrameTime) return 0.0;
	const double elapsed = lastFrameTime < 0.0 ? 0.0 : now - lastFrameTime;
	lastFrameTime = now;
	return elapsed < 0.0 ? 0.0 : (elapsed > 0.1 ? 0.1 : elapsed);
}

static const double TunedClockRate = 2.0;
