#include "frame_timing.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <utility>
#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>
namespace galaxy {
FrameWork frameWork;
bool measureFrameWork=false;
namespace {
bool independent = false, interpolation = false;
uint64_t currentTick = 0;
float fraction = 0;
using Matrix = std::array<float,12>;
struct History { uint64_t tick=0; std::vector<Matrix> previous,current; };
std::unordered_map<const void*,std::array<History,4>> history;
}
void setRenderTiming(bool enabled, uint64_t tick, float alpha, bool interpolate) {
    independent=enabled; currentTick=tick; fraction=std::isfinite(alpha) ? std::clamp(alpha,0.0f,1.0f) : 1.0f; interpolation=interpolate;
}
bool independentRendering() { return independent; }
uint64_t simulationTick() { return currentTick; }
void advanceSimulationTick() { ++currentTick; }
void resetRenderHistory() { history.clear(); }
void forgetModelMatrices(const void* model) { history.erase(model); }
void interpolateModelMatrices(const void* model, unsigned view, Mtx* matrices, unsigned count) {
    if (!independent || !interpolation || !matrices || !count || view>=4) return;
    // Render history persists across frames, unlike the game draw scratch heap.
    struct HostHeap { JKRHeap* saved=std::exchange(JKRHeap::sCurrentHeap,nullptr); ~HostHeap() { JKRHeap::sCurrentHeap=saved; } } hostHeap;
    auto& h=history[model][view];
    if (h.current.size()!=count || currentTick<h.tick || currentTick>h.tick+1) {
        h.current.resize(count); std::memcpy(h.current.data(),matrices,count*sizeof(Mtx));
        h.previous=h.current; h.tick=currentTick;
    } else if (currentTick!=h.tick) {
        h.previous=h.current; std::memcpy(h.current.data(),matrices,count*sizeof(Mtx)); h.tick=currentTick;
        // Teleports and camera cuts must snap, rather than smear across the level.
        for (unsigned i=0;i<count;++i) {
            float jump=0; for (unsigned axis : {3u,7u,11u}) { const float d=h.current[i][axis]-h.previous[i][axis]; jump+=d*d; }
            if (jump>500*500) { h.previous=h.current; break; }
        }
    }
    for (unsigned i=0;i<count;++i) for (unsigned j=0;j<12;++j)
        reinterpret_cast<float*>(matrices[i])[j]=h.previous[i][j]+(h.current[i][j]-h.previous[i][j])*fraction;
}
}
