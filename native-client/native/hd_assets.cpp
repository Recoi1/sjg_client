#include "hd_assets.hpp"
#include "io.hpp"
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <aurora/texture.hpp>
#include <dolphin/thp.h>
#include <array>
#include <bit>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

namespace galaxy {
namespace {
std::filesystem::path assetRoot, preferences;
aurora::texture::ReplacementGroup textures;
bool useTextures = true, useMovies = true;
FILE* movie = nullptr;
std::vector<std::array<std::uint8_t, 12>> movieIndex;
std::uint64_t movieSize = 0;
unsigned movieFrames = 0, decodedFrame = UINT_MAX;
float movieRate = 0;
HdMovieFrame decoded{};
std::uint8_t* pixels = nullptr;
std::uint8_t* compressed = nullptr;
constexpr size_t maxFrameBytes = 4*1024*1024;

std::uint32_t word(const std::uint8_t* bytes) {
    return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 | std::uint32_t(bytes[3]) << 24;
}
std::uint64_t wide(const std::uint8_t* bytes) { return word(bytes) | std::uint64_t(word(bytes+4)) << 32; }
bool save(bool texture, bool video) {
    const std::array<std::uint8_t, 8> data{'G','H','D',1, texture, video, 0, 0};
    return preferences.empty() || aurora::io::write_file_atomic(preferences, data);
}
}
void initializeHdAssets(const std::filesystem::path& root, const char* userPath) {
    shutdownHdAssets();
    assetRoot = root;
    preferences = userPath ? aurora::io::fs_path_from_string(userPath) / "galaxy_hd.dat" : std::filesystem::path{};
    useTextures = useMovies = true;
    if (!preferences.empty()) {
        const auto data = aurora::io::read_file(preferences);
        if (data && data->size() == 8 && !memcmp(data->data(), "GHD\1", 4) && (*data)[4] <= 1 && (*data)[5] <= 1 && !(*data)[6] && !(*data)[7]) {
            useTextures = (*data)[4]; useMovies = (*data)[5];
        }
    }
    std::error_code error;
    if (useTextures && !assetRoot.empty() && std::filesystem::is_directory(assetRoot / "textures", error))
        textures = aurora::texture::load_replacement_directory(assetRoot / "textures");
    std::printf("[HD assets] %zu textures registered; HD cutscenes %s\n", textures.registrations.size(), useMovies ? "enabled" : "disabled");
}
void shutdownHdAssets() {
    closeHdMovie();
    aurora::texture::unregister_replacements(textures);
    textures.registrations.clear();
}
bool hdTexturesEnabled() { return useTextures; }
bool hdMoviesEnabled() { return useMovies; }
unsigned hdTextureCount() { return textures.registrations.size(); }
bool setHdTexturesEnabled(bool enabled) {
    if (!save(enabled, useMovies)) return false;
    if(enabled!=useTextures) {
        aurora::texture::unregister_replacements(textures); textures.registrations.clear();
        std::error_code error;
        if(enabled && !assetRoot.empty() && std::filesystem::is_directory(assetRoot/"textures",error))
            textures=aurora::texture::load_replacement_directory(assetRoot/"textures");
    }
    useTextures = enabled;
    return true;
}
bool setHdMoviesEnabled(bool enabled) {
    if (!save(useTextures, enabled)) return false;
    useMovies = enabled;
    return true;
}
void closeHdMovie() {
    // The FIFO worker may still be reading the last drawn frame when the movie ends.
    if (pixels) AuroraGXSync();
    if (movie) std::fclose(movie);
    movie = nullptr;
    movieIndex.clear();
    std::free(pixels); pixels = nullptr;
    std::free(compressed); compressed = nullptr;
    decoded = {}; decodedFrame = UINT_MAX;
}
void openHdMovie(const char* name) {
    closeHdMovie();
    if (!name || assetRoot.empty()) return;
    auto basename = std::filesystem::path(name).filename();
    if (basename.extension() != ".thp") return;
    basename.replace_extension(".ghdp");
    const auto path = assetRoot / "movies" / basename;
    movie = _wfopen(path.c_str(), L"rb");
    if (!movie) return;
    if (_fseeki64(movie, 0, SEEK_END)) { closeHdMovie(); return; }
    const auto size = _ftelli64(movie);
    std::array<std::uint8_t, 32> header{};
    if (size < 32 || _fseeki64(movie, 0, SEEK_SET) || std::fread(header.data(), 1, 32, movie) != 32 || memcmp(header.data(), "GHDP0001", 8)) {
        closeHdMovie(); return;
    }
    decoded.width = word(header.data()+8); decoded.height = word(header.data()+12);
    movieFrames = word(header.data()+16); movieRate = std::bit_cast<float>(word(header.data()+20)); movieSize = size;
    if (!decoded.width || !decoded.height || decoded.width > 1920 || decoded.height > 1080 || decoded.width%16 || decoded.height%8 ||
        !movieFrames || movieFrames > 100000 || !std::isfinite(movieRate) || movieRate < 1 || movieRate > 120 ||
        word(header.data()+24) || word(header.data()+28) || 32+std::uint64_t(movieFrames)*12 >= movieSize) {
        closeHdMovie(); return;
    }
    movieIndex.resize(movieFrames);
    if (std::fread(movieIndex.data(), 12, movieFrames, movie) != movieFrames) { closeHdMovie(); return; }
    const auto ySize = size_t(decoded.width) * decoded.height;
    pixels = static_cast<std::uint8_t*>(std::malloc(ySize*3/2));
    compressed = static_cast<std::uint8_t*>(std::malloc(maxFrameBytes));
    if (!pixels || !compressed) { closeHdMovie(); return; }
    decoded.y = pixels; decoded.u = pixels+ySize; decoded.v = pixels+ySize+ySize/4;
    std::printf("[HD movie] %s: %ux%u, %u frames\n", basename.string().c_str(), decoded.width, decoded.height, movieFrames);
}
bool decodeHdMovieFrame(unsigned frame, unsigned expectedFrames, float expectedRate) {
    decodedFrame = UINT_MAX;
    if (!useMovies || !movie) return false;
    if (expectedFrames != movieFrames || std::abs(expectedRate-movieRate) > 0.001f || frame >= movieFrames) {
        closeHdMovie(); return false;
    }
    const auto& entry = movieIndex[frame];
    const auto offset = wide(entry.data()); const auto count = word(entry.data()+8);
    const auto ySize = size_t(decoded.width) * decoded.height;
    if (offset < 32+std::uint64_t(movieFrames)*12 || offset > movieSize || !count || count > maxFrameBytes || count > movieSize-offset ||
        _fseeki64(movie, offset, SEEK_SET) || std::fread(compressed, 1, count, movie) != count ||
        THPVideoDecodeSized(compressed, count, decoded.y, ySize, decoded.u, ySize/4, decoded.v, ySize/4, decoded.width, decoded.height)) {
        std::fprintf(stderr, "[HD movie] Invalid frame %u; using original video\n", frame);
        closeHdMovie(); return false;
    }
    decodedFrame = frame;
    return true;
}
const HdMovieFrame* currentHdMovieFrame(unsigned frame) {
    return useMovies && movie && decodedFrame == frame ? &decoded : nullptr;
}
}
