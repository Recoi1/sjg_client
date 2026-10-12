#include "dsp_mixer.hpp"
#include "dsp_control.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>

extern "C" void setupDspTestStorage();
extern "C" void writeDspTestStorage(const void*, u32);
extern "C" void resetDspTestStorage();
namespace {
unsigned completions = 0;
void complete(void*) { ++completions; }
template<class F> void reject(F f) {
    bool failed = false;
    try { f(); } catch (const std::exception&) { failed = true; }
    assert(failed);
}
void route(JASDsp::TChannel& voice, unsigned slot, u16 bus, u16 target = 0x4000, u16 current = 0x4000) {
    const std::array<u16, 4> channel{bus, target, current, 0};
    std::memcpy(reinterpret_cast<u8*>(&voice) + 0x10 + slot * 8, channel.data(), sizeof(channel));
}
void sample(JASDsp::TChannel& voice, u16 source, u32 length, bool loop) {
    voice = {};
    voice.mIsActive = 1;
    voice.mPitch = 0x1000;
    voice._8 = 1;
    voice._100 = source;
    voice._114 = length;
    voice._118 = 0x4000;
    voice._102 = loop;
    route(voice, 0, 0x0D00);
    route(voice, 1, 0x0D60);
}
}
int main() {
    std::array<JASDsp::TChannel, 2> voices{};
    std::array<JASDsp::FxBuf, 4> fx{};
    std::array<u32, 320> table{};
    std::array<u8, 64> afc{};
    // A one-tap resampling table makes the expected PCM samples independent of the implementation.
    for (size_t i = 0; i < 64; ++i) table[i * 2 + 1] = 0x00007FFF;
    std::array<s16, 82> left{}, right{};
    left.front() = left.back() = right.front() = right.back() = 12345;
    const auto mix = [&] {
        galaxy::mixDspSubFrame(std::span<s16, 80>(left.data() + 1, 80), std::span<s16, 80>(right.data() + 1, 80), 0x1000);
        assert(left.front() == 12345 && left.back() == 12345 && right.front() == 12345 && right.back() == 12345);
    };
    reject(mix);
    galaxy::setupDspMixer(voices, fx, table, afc);
    mix();
    assert(std::all_of(left.begin() + 1, left.end() - 1, [](s16 p) { return p == 0; }));
    sample(voices[0], 0, 0, false);
    mix();
    for (unsigned i = 0; i < 80; ++i) assert(left[i + 1] == ((i / 16) % 2 ? -4096 : 4096) && left[i + 1] == right[i + 1]);
    setupDspTestStorage();
    std::array<u8, 160> pcm{};
    pcm.fill(127);
    writeDspTestStorage(pcm.data(), pcm.size());
    sample(voices[0], 8, 16, true);
    for (unsigned frame = 0; frame < 20; ++frame) {
        mix();
        for (unsigned i = frame ? 0 : 1; i < 80; ++i) assert(left[i + 1] == 8127 && right[i + 1] == 8127);
        assert(!voices[0].mIsFinished);
    }
    sample(voices[0], 8, 16, false);
    mix();
    assert(voices[0].mIsFinished && left[2] == 8127 && left[80] == 0);
    mix();
    assert(std::all_of(left.begin() + 1, left.end() - 1, [](s16 p) { return p == 0; }));
    for (size_t i = 0; i < pcm.size(); i += 2) { pcm[i] = 0xFC; pcm[i + 1] = 0; }
    writeDspTestStorage(pcm.data(), pcm.size());
    sample(voices[0], 16, 80, true);
    mix();
    assert(left[2] == -256 && right[2] == -256);
    std::array<u8, 18> encoded{};
    encoded.fill(0x77);
    encoded[0] = encoded[9] = 0x40;
    writeDspTestStorage(encoded.data(), encoded.size());
    sample(voices[0], 9, 32, true);
    for (unsigned frame = 0; frame < 20; ++frame) {
        mix();
        for (unsigned i = frame ? 0 : 1; i < 80; ++i) assert(left[i + 1] == 27 && right[i + 1] == 27);
        assert(!voices[0].mIsFinished);
    }
    sample(voices[0], 8, 0, true);
    reject(mix);
    sample(voices[0], 9, 7, true);
    voices[0]._110 = 3;
    for (unsigned frame = 0; frame < 20; ++frame) {
        mix();
        for (unsigned i = frame ? 0 : 1; i < 80; ++i) assert(left[i + 1] == 27 && right[i + 1] == 27);
    }
    sample(voices[0], 16, 80, false);
    voices[0]._118 = 0xFFFFFFF0;
    reject(mix);
    sample(voices[0], 0xFFFF, 0, false);
    reject(mix);
    voices = {};
    std::array<s16, 560> frameLeft{}, frameRight{};
    reject([&] { galaxy::beginDspFrame(7, frameLeft, frameRight); });
    reject([&] { galaxy::bootDspMixer(nullptr); });
    galaxy::bootDspMixer(complete);
    reject([&] { galaxy::beginDspFrame(7, frameLeft, frameLeft); });
    reject([&] { galaxy::beginDspFrame(6, frameLeft, frameRight); });
    reject([&] { galaxy::setDspLevel(-1); });
    reject([&] { galaxy::setDspLevel(4); });
    galaxy::setDspLevel(1);
    galaxy::beginDspFrame(7, frameLeft, frameRight);
    reject([&] { galaxy::beginDspFrame(7, frameLeft, frameRight); });
    for (unsigned frame = 0; frame < 7; ++frame) {
        for (unsigned group = 0; group < 3; ++group) galaxy::releaseDspVoices(group);
        assert(completions == frame);
        galaxy::releaseDspVoices(3);
        assert(completions == frame + 1);
    }
    reject([&] { galaxy::releaseDspVoices(3); });
    reject([&] { galaxy::releaseDspVoices(4); });
    assert(std::all_of(frameLeft.begin(), frameLeft.end(), [](s16 p) { return p == 0; }));
    assert(std::all_of(frameRight.begin(), frameRight.end(), [](s16 p) { return p == 0; }));
    resetDspTestStorage();
    return 0;
}
