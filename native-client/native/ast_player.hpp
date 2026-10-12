#pragma once
#include "ast.hpp"
#include <SDL3/SDL_audio.h>

namespace galaxy {
// Call pump regularly on the owning thread. The caller retains the Ast and its bytes.
class AstPlayer {
public:
    explicit AstPlayer(Ast& ast, bool loop = false);
    ~AstPlayer();
    AstPlayer(const AstPlayer&) = delete;
    AstPlayer& operator=(const AstPlayer&) = delete;
    void pause(bool paused);
    void setVolume(float volume);
    void pump();
    bool finished() const;
    std::uint64_t submittedFrames() const { return submitted_; }

private:
    Ast& ast_;
    SDL_AudioStream* stream_ = nullptr;
    bool loop_, ended_ = false, paused_ = true;
    std::uint64_t submitted_ = 0;
};
}
