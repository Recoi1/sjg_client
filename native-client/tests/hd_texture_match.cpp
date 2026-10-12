#include <dolphin/gx.h>
#include <aurora/texture.hpp>
#include <aurora/aurora.h>
#include <SDL3/SDL.h>
#include "gx/texture.hpp"
#include "gfx/texture.hpp"
#include <cassert>

bool hdTextureMatches(const void* data, unsigned width, unsigned height, unsigned format) {
    GXTexObj obj;
    GXInitTexObj(&obj, data, width, height, GXTexFmt(format), GX_CLAMP, GX_CLAMP, GX_FALSE);
    if (!aurora::texture::has_replacement(&obj)) return false;
    for (unsigned retry = 0; retry < 100; ++retry) {
        aurora_update(); assert(aurora_begin_frame());
        const auto texture = aurora::gx::texture::resolve_static_texture(reinterpret_cast<const GXTexObj_&>(obj));
        const bool ready = texture->isReplacement && texture->size.width > width && texture->size.height > height;
        aurora_end_frame();
        if (ready) return true;
        SDL_Delay(10);
    }
    assert(false && "HD texture did not upload to the GPU");
    return false;
}
bool hdMovieGpuExtent(unsigned width, unsigned height) {
    const auto& obj = aurora::gx::g_gxState.loadedTextures[0];
    return obj.width() == width && obj.height() == height;
}

void hdMovieStreamingCheck(bool reset) {
    static std::array<const void*, 3> handles{};
    static uint64_t cacheBytes = 0, cacheEntries = 0;
    const auto& stats = aurora::gx::texture_stats();
    if (reset) { cacheBytes = stats.contentCacheBytes; cacheEntries = stats.contentCacheEntries; }
    assert(stats.contentCacheBytes == cacheBytes && stats.contentCacheEntries == cacheEntries);
    for (unsigned plane = 0; plane < 3; ++plane) {
        const auto& bound = aurora::gx::g_gxState.textures[plane];
        assert(bound.texObj.is_streaming() && bound.ref);
        if (reset) handles[plane] = bound.ref.get();
        assert(handles[plane] == bound.ref.get());
    }
}
void hdMovieCacheStats() {
    const auto& s = aurora::gx::texture_stats();
    std::printf("HD frame cache: %llu bytes, %llu entries\n", (unsigned long long)s.contentCacheBytes, (unsigned long long)s.contentCacheEntries);
}
