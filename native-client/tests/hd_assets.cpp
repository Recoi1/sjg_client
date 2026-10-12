#include "hd_assets.hpp"
#include "tpl.hpp"
#include "yaz0.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include <revolution/gx.h>
#include <revolution/thp.h>
#include <aurora/aurora.h>
#include <aurora/main.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <nw4r/ut/ResFont.h>
bool hdTextureMatches(const void*, unsigned, unsigned, unsigned);
bool hdMovieGpuExtent(unsigned width, unsigned height);
void hdMovieCacheStats();
void hdMovieStreamingCheck(bool reset);

int main(int argc, char** argv) {
    assert(argc == 4);
    const std::filesystem::path root(argv[1]), owned(argv[2]), scratch(argv[3]);
    std::filesystem::create_directories(scratch);
    std::filesystem::remove(scratch/"galaxy_hd.dat");
    AuroraConfig config{}; config.appName = "Galaxy HD assets check";
    config.userPath = config.cachePath = "./"; config.vsync = true;
    const auto platform = aurora_initialize(argc, argv, &config);
    SDL_SetWindowOpacity(platform.window, 0);
    SDL_SetWindowSize(platform.window, 1920, 1080);
    GXInit(nullptr, 0);
    galaxy::initializeHdAssets(root, scratch.string().c_str());
    assert(galaxy::hdTextureCount() == 130 && galaxy::hdTexturesEnabled() && galaxy::hdMoviesEnabled());
    {
        std::ifstream file(owned/"LayoutData/Font.arc", std::ios::binary);
        std::vector<u8> bytes{std::istreambuf_iterator<char>(file), {}};
        if (!memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
        JKRMemArchive archive; archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0);
        auto* data = archive.getResource("numberfont.brfnt");
        nw4r::ut::ResFont font; assert(data && font.SetResource(data, archive.getResSize(data)));
        for (wchar_t digit = L'0'; digit <= L'9'; ++digit) {
            nw4r::ut::Glyph glyph; font.GetGlyph(&glyph, digit);
            assert(hdTextureMatches(glyph.pTexture, glyph.texWidth, glyph.texHeight, glyph.texFormat));
        }
        std::cout << "Number font: every digit has an uploaded HD replacement\n";
    }
    for (const auto& name : {"StarCounter", "CoinCounter", "HitPointMeter", "StarPieceCounter"}) {
        std::ifstream file(owned/"LayoutData"/(std::string(name)+".arc"), std::ios::binary);
        std::vector<u8> bytes{std::istreambuf_iterator<char>(file), {}};
        if (!memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
        JKRMemArchive archive; archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0);
        unsigned replaced = 0;
        for (u32 i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& entry = archive.mFiles[i];
            if (!(entry.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            const std::string_view member(archive.mStringTable+entry.mNameOffset);
            if (!member.ends_with(".tpl")) continue;
            const auto* data = static_cast<const u8*>(archive.getIdxResource(i));
            const auto t = galaxy::readTpl({data, size_t(archive.getResSize(data))}, 0).texture;
            replaced += hdTextureMatches(t.data, t.width, t.height, t.format);
        }
        assert(replaced > 0);
        std::cout << name << ": " << replaced << " exact HD matches\n";
    }
    const char* names[]{"PrologueA", "PrologueB", "FinalBattle", "FinalBattleLuigi", "EpilogueA", "EpilogueALuigi", "EndingA", "EndingALuigi", "EndingB"};
    const unsigned counts[]{5591,7076,4447,4447,10115,10115,6523,6523,6300};
    for (unsigned movie = 0; movie < 9; ++movie) {
        galaxy::openHdMovie((std::string("/MovieData/")+names[movie]+".thp").c_str());
        for (unsigned frame : {0u, counts[movie]/2, counts[movie]-1}) {
            assert(galaxy::decodeHdMovieFrame(frame, counts[movie], 59.94f));
            const auto* hd = galaxy::currentHdMovieFrame(frame);
            assert(hd && hd->width == 1920 && hd->height == 880);
        }
        assert(!galaxy::decodeHdMovieFrame(counts[movie], counts[movie], 59.94f));
        assert(!galaxy::currentHdMovieFrame(0));
        std::cout << names[movie] << ": first/middle/last HD frame and bounds passed\n";
        if (std::getenv("GALAXY_VERIFY_ALL_HD_FRAMES")) {
            galaxy::openHdMovie((std::string(names[movie])+".thp").c_str());
            for (unsigned frame = 0; frame < counts[movie]; ++frame)
                assert(galaxy::decodeHdMovieFrame(frame, counts[movie], 59.94f));
            std::cout << names[movie] << ": all " << counts[movie] << " frames decoded" << std::endl;
        }
    }
    galaxy::openHdMovie("PrologueA.thp");
    const auto start = std::chrono::steady_clock::now();
    for (unsigned frame = 300; frame < 360; ++frame) assert(galaxy::decodeHdMovieFrame(frame, 5591, 59.94f));
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count()/60;
    std::cout << "HD decode average: " << ms << " ms/frame\n";
    GXRenderModeObj mode{}; mode.fbWidth = 640; mode.efbHeight = 456;
    const auto* frame = galaxy::currentHdMovieFrame(359); assert(frame);
    for (unsigned render = 0; render < 4; ++render) {
        aurora_update(); assert(aurora_begin_frame());
        GXSetCullMode(GX_CULL_NONE);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        THPGXYuv2RgbSetup(&mode);
        THPGXYuv2RgbDraw(frame->y, frame->u, frame->v, 0, 44, frame->width, frame->height, 640, 368);
        THPGXRestore(); GXDrawDone();
        u32 color; GXPeekARGB(0, 0, &color);
        aurora_end_frame(); SDL_Delay(20);
    }
    assert(hdMovieGpuExtent(1920, 880));
    hdMovieStreamingCheck(true);
    double playbackMs = 0, worstMs = 0;
    const unsigned playbackFrames = std::getenv("GALAXY_VERIFY_LONG_HD_PLAYBACK") ? 1200 : 180;
    for (unsigned number = 600; number < 600+playbackFrames; ++number) {
        const auto tick = std::chrono::steady_clock::now();
        assert(galaxy::decodeHdMovieFrame(number, 5591, 59.94f));
        frame = galaxy::currentHdMovieFrame(number); assert(frame);
        aurora_update(); assert(aurora_begin_frame());
        GXSetCullMode(GX_CULL_NONE);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        THPGXYuv2RgbSetup(&mode);
        THPGXYuv2RgbDraw(frame->y, frame->u, frame->v, 0, 44, frame->width, frame->height, 640, 368);
        THPGXRestore(); GXDrawDone();
        hdMovieStreamingCheck(false);
        aurora_end_frame();
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-tick).count();
        playbackMs += elapsed; worstMs = std::max(worstMs, elapsed);
    }
    hdMovieCacheStats();
    std::cout << "HD changing-frame decode/render/present: " << playbackMs/playbackFrames << " ms average, " << worstMs << " ms worst" << std::endl;
    aurora_update(); assert(aurora_begin_frame());
    std::ofstream capture(scratch/"hd-movie-render.ppm", std::ios::binary);
    capture << "P6\n640 456\n255\n";
    unsigned nonblack = 0;
    for (u16 y = 0; y < 456; ++y) for (u16 x = 0; x < 640; ++x) {
        u32 color; GXPeekARGB(x, y, &color);
        nonblack += (color & 0xffffff) != 0;
        const char rgb[]{char(color>>16),char(color>>8),char(color)}; capture.write(rgb, 3);
    }
    capture.close(); aurora_end_frame();
    assert(nonblack > 100000);
    for (unsigned stop = 0; stop < 10; ++stop) {
        galaxy::openHdMovie("PrologueA.thp");
        assert(galaxy::decodeHdMovieFrame(359+stop, 5591, 59.94f));
        frame = galaxy::currentHdMovieFrame(359+stop); assert(frame);
        aurora_update(); assert(aurora_begin_frame());
        THPGXYuv2RgbSetup(&mode);
        THPGXYuv2RgbDraw(frame->y, frame->u, frame->v, 0, 44, 1920, 880, 640, 368);
        THPGXRestore();
        galaxy::closeHdMovie(); // Stop during the frame, before the usual draw-done flush.
        aurora_end_frame();
    }
    galaxy::openHdMovie("PrologueA.thp");
    assert(galaxy::setHdMoviesEnabled(false));
    assert(!galaxy::currentHdMovieFrame(359) && !galaxy::decodeHdMovieFrame(0, 5591, 59.94f));
    std::vector<u8> originalY(640*368, 128), originalUv(320*184, 128);
    for (unsigned quality = 0; quality < 2; ++quality) {
        if (quality) {
            assert(galaxy::setHdMoviesEnabled(true));
            assert(galaxy::decodeHdMovieFrame(359, 5591, 59.94f));
            frame = galaxy::currentHdMovieFrame(359); assert(frame);
        }
        for (unsigned render = 0; render < 4; ++render) {
            aurora_update(); assert(aurora_begin_frame());
            THPGXYuv2RgbSetup(&mode);
            if (quality) THPGXYuv2RgbDraw(frame->y, frame->u, frame->v, 0, 44, 1920, 880, 640, 368);
            else THPGXYuv2RgbDraw(originalY.data(), originalUv.data(), originalUv.data(), 0, 44, 640, 368, 640, 368);
            THPGXRestore(); GXDrawDone(); aurora_end_frame();
        }
        assert(hdMovieGpuExtent(quality ? 1920 : 640, quality ? 880 : 368));
    }
    assert(galaxy::setHdMoviesEnabled(false));
    assert(galaxy::setHdTexturesEnabled(false));
    galaxy::initializeHdAssets(root, scratch.string().c_str());
    assert(!galaxy::hdMoviesEnabled() && !galaxy::hdTexturesEnabled() && galaxy::hdTextureCount() == 0);
    assert(galaxy::setHdTexturesEnabled(true) && galaxy::setHdMoviesEnabled(true));
    galaxy::initializeHdAssets(root, scratch.string().c_str());
    assert(galaxy::hdMoviesEnabled() && galaxy::hdTexturesEnabled() && galaxy::hdTextureCount() == 130);
    galaxy::openHdMovie("Missing.thp");
    assert(!galaxy::decodeHdMovieFrame(0, 5591, 59.94f));
    galaxy::openHdMovie("PrologueA.thp");
    assert(!galaxy::decodeHdMovieFrame(0, 5592, 59.94f));
    const auto backup = scratch/"galaxy_hd.backup";
    std::filesystem::rename(scratch/"galaxy_hd.dat", backup);
    std::filesystem::create_directory(scratch/"galaxy_hd.dat");
    assert(!galaxy::setHdMoviesEnabled(false) && galaxy::hdMoviesEnabled());
    assert(!galaxy::setHdTexturesEnabled(false) && galaxy::hdTexturesEnabled());
    std::filesystem::remove(scratch/"galaxy_hd.dat");
    std::filesystem::rename(backup, scratch/"galaxy_hd.dat");
    galaxy::shutdownHdAssets(); aurora_shutdown();
    return 0;
}
