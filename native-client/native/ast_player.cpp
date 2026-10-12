#include "ast_player.hpp"
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_error.h>
#include <array>
#include <cmath>
#include <stdexcept>

namespace galaxy {
namespace {
void check(bool result) {
    if (!result) throw std::runtime_error(SDL_GetError());
}
}
AstPlayer::AstPlayer(Ast& ast, bool loop) : ast_(ast), loop_(loop) {
    if (ast.channels() != 2 && ast.channels() != 4)
        throw std::runtime_error("Playback requires stereo or four-channel Galaxy AST");
    check(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(ast.sampleRate())};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        const std::string error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error(error);
    }
}

AstPlayer::~AstPlayer() {
    SDL_DestroyAudioStream(stream_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AstPlayer::pause(bool paused) {
    check(paused ? SDL_PauseAudioStreamDevice(stream_) : SDL_ResumeAudioStreamDevice(stream_));
    paused_ = paused;
}

void AstPlayer::setVolume(float volume) {
    if (!std::isfinite(volume) || volume < 0) throw std::invalid_argument("Invalid audio volume");
    check(SDL_SetAudioStreamGain(stream_, volume));
}

void AstPlayer::pump() {
    if (ended_ || (paused_ && submitted_)) return;
    std::array<std::int16_t, 1024 * 4> source;
    std::array<std::int16_t, 1024 * 2> stereo;
    const auto channels = ast_.channels();
    for (;;) {
        const int queued = SDL_GetAudioStreamQueued(stream_);
        if (queued < 0) throw std::runtime_error(SDL_GetError());
        if (queued >= ast_.sampleRate() / 10 * 4) return;
        const auto frames = ast_.read(std::span(source).first(1024 * channels), loop_);
        if (!frames) {
            check(SDL_FlushAudioStream(stream_));
            ended_ = true;
            return;
        }
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t side = 0; side < 2; ++side) {
                int sample = source[frame * channels + side];
                // shortcut: equal stereo-stem mix for previews, use JAI channel gains when connecting adaptive BGM.
                if (channels == 4) sample = (sample + source[frame * channels + side + 2]) / 2;
                stereo[frame * 2 + side] = static_cast<std::int16_t>(sample);
            }
        }
        check(SDL_PutAudioStreamData(stream_, stereo.data(), static_cast<int>(frames * 4)));
        submitted_ += frames;
    }
}

bool AstPlayer::finished() const {
    const int queued = SDL_GetAudioStreamQueued(stream_);
    const int available = SDL_GetAudioStreamAvailable(stream_);
    if (queued < 0 || available < 0) throw std::runtime_error(SDL_GetError());
    return ended_ && queued == 0 && available == 0;
}
}
