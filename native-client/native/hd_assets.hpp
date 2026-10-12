#pragma once
#include <cstdint>
#include <filesystem>

namespace galaxy {
void initializeHdAssets(const std::filesystem::path& root, const char* userPath);
void shutdownHdAssets();
bool hdTexturesEnabled();
bool hdMoviesEnabled();
bool setHdTexturesEnabled(bool enabled);
bool setHdMoviesEnabled(bool enabled);
unsigned hdTextureCount();
void openHdMovie(const char* name);
void closeHdMovie();
struct HdMovieFrame {
    std::uint8_t* y;
    std::uint8_t* u;
    std::uint8_t* v;
    unsigned width, height;
};
bool decodeHdMovieFrame(unsigned frame, unsigned expectedFrames, float expectedRate);
const HdMovieFrame* currentHdMovieFrame(unsigned frame);
}
