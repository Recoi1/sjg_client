#include "audio_device.hpp"
#include "runtime_options.hpp"
#include <revolution/ai.h>
#include <revolution/wpad.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <array>

namespace {
void check(bool result) { if (!result) throw std::runtime_error(SDL_GetError()); }
struct Device {
    std::mutex mutex;
    std::condition_variable changed;
    SDL_AudioStream* stream = nullptr;
    std::array<SDL_AudioStream*, 4> speakers{};
    std::array<bool, 4> speakerPlaying{}, speakerMuted{};
    std::thread worker;
    std::vector<s16> buffer;
    uintptr_t source = 0;
    unsigned rate = 32000;
    AIDCallback callback = nullptr;
    std::exception_ptr failure;
    bool running = false, stopping = false;
    void stop() {
        { std::lock_guard lock(mutex); stopping = true; running = false; }
        changed.notify_all();
        if (worker.joinable()) worker.join();
        for (auto*& speaker : speakers) {
            if (speaker) SDL_DestroyAudioStream(speaker);
            speaker = nullptr;
        }
        speakerPlaying.fill(false);
        speakerMuted.fill(false);
        if (stream) {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
    }
    ~Device() { stop(); }
    void run() {
        try {
            std::vector<s16> block;
            for (;;) {
                AIDCallback notify;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [&] { return stopping || running; });
                    if (stopping) return;
                    check(SDL_SetAudioStreamGain(stream, galaxy::soundVolume(galaxy::Option::MasterVolume)));
                    const int queued = SDL_GetAudioStreamQueued(stream);
                    if (queued < 0) throw std::runtime_error(SDL_GetError());
                    if (queued >= int(rate * 4 * 32 / 1000)) {
                        changed.wait_for(lock, std::chrono::milliseconds(2));
                        continue;
                    }
                    block = buffer;
                    notify = callback;
                    check(SDL_PutAudioStreamData(stream, block.data(), int(block.size() * sizeof(s16))));
                }
                if (notify) notify();
            }
        } catch (...) {
            std::lock_guard lock(mutex);
            failure = std::current_exception();
            running = false;
        }
    }
} device;
}
#ifdef GALAXY_AUDIO_CHECKS
SDL_AudioDeviceID galaxyAudioTestDevice() {
    std::lock_guard lock(device.mutex);
    return SDL_GetAudioStreamDevice(device.stream);
}
#endif
namespace galaxy {
void checkAudioDevice() {
    std::exception_ptr failure;
    { std::lock_guard lock(device.mutex); failure = device.failure; }
    if (failure) std::rethrow_exception(failure);
}
void shutdownAudioDevice() { device.stop(); }
bool applyAudioOutput() {
    std::lock_guard lock(device.mutex);
    if(!device.stream) return true;
    std::array<SDL_AudioStream*,5> next{};
    const SDL_AudioSpec specs[]{ {SDL_AUDIO_S16,2,int(device.rate)}, {SDL_AUDIO_S16,1,6000} };
    for(unsigned i=0;i<next.size();++i) {
        if(i && !device.speakers[i-1]) continue;
        next[i]=SDL_OpenAudioDeviceStream(soundOutputDevice(), &specs[i ? 1 : 0], nullptr, nullptr);
        if(!next[i] || !SDL_SetAudioStreamGain(next[i],soundVolume(Option::MasterVolume)) ||
           ((i ? device.speakerPlaying[i-1] && !device.speakerMuted[i-1] : device.running) && !SDL_ResumeAudioStreamDevice(next[i]))) {
            for(auto* stream:next) if(stream) SDL_DestroyAudioStream(stream);
            return false;
        }
    }
    SDL_DestroyAudioStream(device.stream); device.stream=next[0];
    for(unsigned i=0;i<4;++i) { if(device.speakers[i]) SDL_DestroyAudioStream(device.speakers[i]); device.speakers[i]=next[i+1]; }
    device.changed.notify_all(); return true;
}
void sendSpeakerPcm(s32 channel, std::span<const s16> samples) {
    if (channel < 0 || channel >= 4 || samples.size() != 40) throw std::invalid_argument("Invalid speaker PCM block");
    std::lock_guard lock(device.mutex);
    if (!device.speakers[channel] || !device.speakerPlaying[channel] || device.speakerMuted[channel])
        throw std::logic_error("Speaker audio was not started");
    check(SDL_SetAudioStreamGain(device.speakers[channel], soundVolume(Option::MasterVolume)*option(Option::EffectsVolume)/100.0f));
    check(SDL_PutAudioStreamData(device.speakers[channel], samples.data(), int(samples.size_bytes())));
}
}
extern "C" {
void AIInit(u8*) {
    std::lock_guard lock(device.mutex);
    if (device.stream) return;
    check(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(device.rate)};
    device.stream = SDL_OpenAudioDeviceStream(galaxy::soundOutputDevice(), &spec, nullptr, nullptr);
    if (!device.stream) { SDL_QuitSubSystem(SDL_INIT_AUDIO); throw std::runtime_error(SDL_GetError()); }
    device.stopping = device.running = false;
    device.failure = nullptr;
    device.buffer.clear();
    try { device.worker = std::thread([] { device.run(); }); }
    catch (...) { SDL_DestroyAudioStream(device.stream); device.stream = nullptr; SDL_QuitSubSystem(SDL_INIT_AUDIO); throw; }
}
void AIInitDMA(uintptr_t address, u32 length) {
    if (!address || !length || length % 4 || length > 65536) throw std::invalid_argument("Invalid JAS DMA buffer");
    std::lock_guard lock(device.mutex);
    if (!device.stream) throw std::logic_error("Audio interface was not initialized");
    device.buffer.resize(length / sizeof(s16));
    std::memcpy(device.buffer.data(), reinterpret_cast<const void*>(address), length);
    device.source = address;
}
AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    std::lock_guard lock(device.mutex);
    const auto previous = device.callback;
    device.callback = callback;
    return previous;
}
void AISetDSPSampleRate(u32 rate) {
    if (rate > 1) throw std::invalid_argument("Invalid JAS sample rate");
    std::lock_guard lock(device.mutex);
    device.rate = rate ? 48000 : 32000;
    if (device.stream) {
        const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(device.rate)};
        check(SDL_SetAudioStreamFormat(device.stream, &spec, nullptr));
    }
}
void AIStartDMA() {
    galaxy::checkAudioDevice();
    std::lock_guard lock(device.mutex);
    if (!device.stream || device.buffer.empty()) throw std::logic_error("No JAS DMA buffer to play");
    check(SDL_ResumeAudioStreamDevice(device.stream));
    device.running = true;
    device.changed.notify_all();
}
void AIStopDMA() {
    std::lock_guard lock(device.mutex);
    device.running = false;
    if (device.stream) { check(SDL_PauseAudioStreamDevice(device.stream)); check(SDL_ClearAudioStream(device.stream)); }
    device.changed.notify_all();
}
uintptr_t AIGetDMAStartAddr() { std::lock_guard lock(device.mutex); return device.source; }
u32 AIGetDMALength() { std::lock_guard lock(device.mutex); return device.buffer.size() * sizeof(s16); }
s32 WPADControlSpeaker(s32 channel, u32 command, WPADCallback callback) {
    if (channel < 0 || channel >= 4 || command > 5) return WPAD_ERR_INVALID;
    if (command == 1 || command == 5) AIInit(nullptr);
    s32 result = WPAD_ERR_NONE;
    {
        std::lock_guard lock(device.mutex);
        auto*& stream = device.speakers[channel];
        if (command == 0) {
            if (stream) SDL_DestroyAudioStream(stream);
            stream = nullptr;
            device.speakerPlaying[channel] = false;
        } else if (command == 1 || command == 5) {
            if (!stream) {
                const SDL_AudioSpec spec{SDL_AUDIO_S16, 1, 6000};
                stream = SDL_OpenAudioDeviceStream(galaxy::soundOutputDevice(), &spec, nullptr, nullptr);
                if (!stream) throw std::runtime_error(SDL_GetError());
            }
            device.speakerMuted[channel] = false;
        } else if (!stream) result = WPAD_ERR_NO_CONTROLLER;
        else if (command == 2) {
            device.speakerMuted[channel] = true;
            check(SDL_PauseAudioStreamDevice(stream));
            check(SDL_ClearAudioStream(stream));
        } else if (command == 3 || command == 4) {
            device.speakerMuted[channel] = false;
            if (command == 4) device.speakerPlaying[channel] = true;
            if (device.speakerPlaying[channel]) check(SDL_ResumeAudioStreamDevice(stream));
        }
    }
    if (callback) callback(channel, result);
    return result;
}
BOOL WPADIsSpeakerEnabled(s32 channel) {
    std::lock_guard lock(device.mutex);
    return channel >= 0 && channel < 4 && device.speakers[channel];
}
BOOL WPADCanSendStreamData(s32 channel) {
    std::lock_guard lock(device.mutex);
    if (channel < 0 || channel >= 4 || !device.speakers[channel] || !device.speakerPlaying[channel] || device.speakerMuted[channel]) return false;
    const auto queued = SDL_GetAudioStreamQueued(device.speakers[channel]);
    if (queued < 0) throw std::runtime_error(SDL_GetError());
    return queued < 384;
}
u8 WPADGetSpeakerVolume() { return 127; } // Windows controls the playback device's master volume.
}
