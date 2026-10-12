#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <revolution/mtx.h>
namespace galaxy {
// The simulation stays at 60 Hz at every rendering rate.
struct FrameTiming {
    double remainder = 0;
    unsigned advance(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0) return 0;
        remainder += std::min(seconds, 0.25);
        const auto ticks = unsigned(std::floor((remainder+1e-10)*60));
        remainder -= ticks/60.0;
        return ticks;
    }
    float alpha() const { return float(std::clamp(remainder*60,0.0,1.0)); }
};
struct FrameWork { double updateMs=0, drawMs=0; };
extern FrameWork frameWork;
extern bool measureFrameWork;
void setRenderTiming(bool enabled, uint64_t tick, float alpha, bool interpolate);
void resetRenderHistory();
void forgetModelMatrices(const void* model);
void advanceSimulationTick();
void interpolateModelMatrices(const void* model, unsigned view, Mtx* matrices, unsigned count);
bool independentRendering();
uint64_t simulationTick();
}
