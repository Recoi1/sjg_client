#include "audio_device.hpp"
#include "runtime_options.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <revolution/ai.h>
#include <revolution/wpad.h>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <thread>

SDL_AudioDeviceID galaxyAudioTestDevice();
namespace {
std::atomic<unsigned> callbacks{};
std::atomic<float> mixedPeak{};
void SDLCALL postmix(void*,const SDL_AudioSpec*,float* buffer,int bytes) {
    float peak=0; for(int i=0;i<bytes/int(sizeof(float));++i) peak=std::max(peak,std::abs(buffer[i]));
    mixedPeak.store(peak);
}
std::array<s16, 1280> samples{};
void complete() {
    AIInitDMA(reinterpret_cast<uintptr_t>(samples.data()), sizeof(samples));
    ++callbacks;
}
template<class F> void reject(F f) {
    bool failed = false;
    try { f(); } catch (const std::exception&) { failed = true; }
    assert(failed);
}
void waitFor(unsigned target) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (callbacks.load() < target && std::chrono::steady_clock::now() < deadline) {
        galaxy::checkAudioDevice();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(callbacks.load() >= target);
}
}
int main() {
    samples.fill(1234);
    reject([] { AIStartDMA(); });
    reject([] { AIInitDMA(0, 4); });
    reject([] { AISetDSPSampleRate(2); });
    for (unsigned rate = 0; rate < 2; ++rate) {
        AISetDSPSampleRate(rate);
        AIInit(nullptr);
        reject([] { AIStartDMA(); });
        reject([] { AIInitDMA(reinterpret_cast<uintptr_t>(samples.data()), 3); });
        AIInitDMA(reinterpret_cast<uintptr_t>(samples.data()), sizeof(samples));
        assert(AIGetDMAStartAddr() == reinterpret_cast<uintptr_t>(samples.data()));
        assert(AIGetDMALength() == sizeof(samples));
        assert(AIRegisterDMACallback(complete) == nullptr);
        const auto start = callbacks.load();
        AIStartDMA();
        waitFor(start + 5);
        const auto outputDevice=galaxyAudioTestDevice(); assert(outputDevice);
        assert(SDL_SetAudioPostmixCallback(outputDevice,postmix,nullptr));
        waitFor(callbacks.load()+5); assert(mixedPeak.load()>.03f);
        assert(galaxy::setOption(galaxy::Option::MasterVolume,50)); waitFor(callbacks.load()+5);
        assert(mixedPeak.load()>.005f && mixedPeak.load()<.025f);
        assert(galaxy::setOption(galaxy::Option::MasterVolume,0)); waitFor(callbacks.load()+5); assert(mixedPeak.load()==0);
        assert(galaxy::setOption(galaxy::Option::MasterVolume,100) && galaxy::setOption(galaxy::Option::MuteUnfocused,1));
        galaxy::setAudioFocus(false); waitFor(callbacks.load()+5); assert(mixedPeak.load()==0);
        galaxy::setAudioFocus(true); waitFor(callbacks.load()+5); assert(mixedPeak.load()>.03f);
        assert(SDL_SetAudioPostmixCallback(outputDevice,nullptr,nullptr));
        std::array<s16, 40> speakerSamples{};
        speakerSamples.fill(-5000);
        reject([&] { galaxy::sendSpeakerPcm(0, speakerSamples); });
        assert(WPADControlSpeaker(-1, 1, nullptr) == WPAD_ERR_INVALID);
        assert(WPADControlSpeaker(0, 4, nullptr) == WPAD_ERR_NO_CONTROLLER);
        assert(WPADControlSpeaker(0, 1, nullptr) == WPAD_ERR_NONE);
        assert(WPADIsSpeakerEnabled(0) && !WPADCanSendStreamData(0));
        assert(WPADControlSpeaker(0, 4, nullptr) == WPAD_ERR_NONE);
        assert(WPADCanSendStreamData(0) && WPADGetSpeakerVolume() == 127);
        galaxy::sendSpeakerPcm(0, speakerSamples);
        const auto dmaAddress=AIGetDMAStartAddr(); const auto dmaLength=AIGetDMALength();
        assert(galaxy::applyAudioOutput());
        assert(AIGetDMAStartAddr()==dmaAddress && AIGetDMALength()==dmaLength);
        waitFor(callbacks.load()+5);
        const auto changedOutput=galaxyAudioTestDevice(); assert(changedOutput);
        assert(SDL_SetAudioPostmixCallback(changedOutput,postmix,nullptr));
        waitFor(callbacks.load()+5); assert(mixedPeak.load()>.03f);
        assert(SDL_SetAudioPostmixCallback(changedOutput,nullptr,nullptr));
        assert(WPADIsSpeakerEnabled(0)); galaxy::sendSpeakerPcm(0,speakerSamples);
        assert(WPADControlSpeaker(0, 2, nullptr) == WPAD_ERR_NONE);
        assert(!WPADCanSendStreamData(0));
        assert(WPADControlSpeaker(0, 3, nullptr) == WPAD_ERR_NONE);
        galaxy::sendSpeakerPcm(0, speakerSamples);
        assert(WPADControlSpeaker(0, 0, nullptr) == WPAD_ERR_NONE);
        assert(!WPADIsSpeakerEnabled(0));
        AIStopDMA();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        const auto paused = callbacks.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        assert(callbacks.load() == paused);
        AIStartDMA();
        waitFor(paused + 3);
        galaxy::shutdownAudioDevice();
        const auto stopped = callbacks.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        assert(callbacks.load() == stopped);
        assert(AIRegisterDMACallback(nullptr) == complete);
        galaxy::checkAudioDevice();
        galaxy::shutdownAudioDevice();
    }
    return 0;
}
