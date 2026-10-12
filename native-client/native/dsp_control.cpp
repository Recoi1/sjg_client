#include "dsp_control.hpp"
#include "dsp_mixer.hpp"
#include <cmath>
#include <stdexcept>

namespace {
void (*completion)(void*) = nullptr;
std::span<s16> left, right;
u32 remaining = 0;
u16 mixerLevel = 0x4000;
}
namespace galaxy {
void bootDspMixer(void (*callback)(void*)) {
    if (!callback) throw std::invalid_argument("Missing JAS DSP completion callback");
    completion = callback;
    remaining = 0;
}
void beginDspFrame(u32 count, std::span<s16> l, std::span<s16> r) {
    const auto lAddress = reinterpret_cast<uintptr_t>(l.data());
    const auto rAddress = reinterpret_cast<uintptr_t>(r.data());
    const auto distance = lAddress > rAddress ? lAddress - rAddress : rAddress - lAddress;
    if (!completion || remaining || count < 1 || count > 16 || l.size() != count * 80 || r.size() != l.size() ||
        !l.data() || !r.data() || distance < l.size_bytes())
        throw std::invalid_argument("Invalid or overlapping JAS DSP frame");
    left = l;
    right = r;
    remaining = count;
}
void releaseDspVoices(u32 group) {
    if (group > 3) throw std::invalid_argument("Invalid JAS DSP voice group");
    if (group != 3) return;
    if (!remaining) throw std::logic_error("No JAS DSP frame to complete");
    mixDspSubFrame(std::span<s16, 80>(left.data(), 80), std::span<s16, 80>(right.data(), 80), mixerLevel);
    left = left.subspan(80);
    right = right.subspan(80);
    --remaining;
    completion(nullptr);
}
void setDspLevel(f32 level) {
    if (!std::isfinite(level) || level < 0 || level >= 4) throw std::invalid_argument("Invalid JAS DSP mixer level");
    mixerLevel = u16(level * 16384.0f);
}
}
