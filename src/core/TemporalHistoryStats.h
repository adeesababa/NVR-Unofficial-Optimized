#pragma once
#include <cstdio>

struct TemporalHistoryStats {
	unsigned int drawn = 0, used = 0, off = 0, noHistory = 0, notLastFrame = 0, jumped = 0;

	void Note(bool temporalOn, bool haveHistory, bool drewLastFrame, bool usable) {
		drawn++;
		if (usable) used++;
		else if (!temporalOn) off++;
		else if (!haveHistory) noHistory++;
		else if (!drewLastFrame) notLastFrame++;
		else jumped++;
	}
	void Reset() { drawn = used = off = noHistory = notLastFrame = jumped = 0; }
	void Describe(char* text, size_t size) const {
		sprintf_s(text, size, "%u frames drawn, history used on %u (%.0f%%); not used: %u temporal off, %u no history yet, %u not drawn last frame, %u camera jump",
			drawn, used, drawn ? 100.0 * used / drawn : 0.0, off, noHistory, notLastFrame, jumped);
	}
};
