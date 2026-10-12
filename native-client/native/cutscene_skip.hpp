#pragma once
#include <algorithm>
#include <cstdint>

namespace galaxy {
struct CutsceneSkipHold {
    float seconds = 0;
    bool armed = false;
    int lastUiFrame = -1;
    uint64_t lastTick = UINT64_MAX;

    void reset() { seconds = 0; armed = false; lastUiFrame = -1; lastTick = UINT64_MAX; }
    bool update(bool held, float deltaSeconds) {
        // A load or pause must not complete the hold on its first returning frame.
        if (deltaSeconds > 0.25f) { reset(); return false; }
        if (!held) { seconds = 0; armed = true; }
        else if (armed) seconds = std::min(1.0f, seconds + std::clamp(deltaSeconds, 0.0f, 0.1f));
        return seconds >= 1.0f;
    }
};
bool updateCutsceneSkip(CutsceneSkipHold&, bool held);
float cutsceneSkipProgress();
bool cutsceneSkipButtonHeld();
void drawCutsceneSkipPrompt();
}
