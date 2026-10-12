#include "Game/System/DrawSyncManager.hpp"
#include <aurora/aurora.h>
#include <aurora/main.h>
#include <SDL3/SDL.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <thread>

extern "C" u16 GXReadDrawSync();
namespace {
std::array<u16, 16> received{};
size_t count = 0;
std::thread::id producer;
void callback(u16 token) {
    assert(std::this_thread::get_id() == producer && count < received.size());
    received[count++] = token;
}
struct GameCallback : DrawSyncCallback {
    u16 token = 0;
    unsigned calls = 0;
    void drawSyncCallback(u16 value) override { token = value; ++calls; }
};
}
int main(int argc, char** argv) {
    AuroraConfig config{};
    config.appName = "Galaxy synchronization check";
    config.userPath = config.cachePath = "./";
    const auto info = aurora_initialize(argc, argv, &config);
    SDL_SetWindowOpacity(info.window, 0.0f);
    producer = std::this_thread::get_id();
    GXInit(nullptr, 0);
    assert(aurora_begin_frame());
    assert(GXSetDrawSyncCallback(callback) == nullptr);
    GXSetDrawSync(0);
    GXSetDrawSync(0xffff);
    GXSetDrawSync(0xffff);
    assert(count == 3 && received[0] == 0 && received[1] == 0xffff && received[2] == 0xffff);
    assert(GXReadDrawSync() == 0xffff);

    alignas(32) std::array<u8, 1024> list{};
    GXBeginDisplayList(list.data(), list.size());
    GXSetDrawSync(7);
    GXSetDrawSync(8);
    const u32 size = GXEndDisplayList();
    assert(size && size <= list.size() && count == 3);
    GXCallDisplayList(list.data(), size);
    GXDrawDone();
    assert(count == 5 && received[3] == 7 && received[4] == 8 && GXReadDrawSync() == 8);
    assert(GXSetDrawSyncCallback(nullptr) == callback);
    GXSetDrawSync(9);
    assert(count == 5 && GXReadDrawSync() == 9);

    GameCallback low, high;
    auto* manager = DrawSyncManager::start(8, 15);
    const u16 lowStart = manager->setCallback(0, 2, &low);
    const u16 highStart = manager->setCallback(4, 1, &high);
    assert(lowStart == 1 && highStart == 0xa000);
    manager->pushBreakPoint();
    GXSetDrawSync(lowStart + 1);
    GXSetDrawSync(highStart);
    assert(low.calls == 1 && low.token == 2 && high.calls == 1 && high.token == 0xa000);
    GXSetDrawSync(0);
    GXSetDrawSync(3);
    assert(low.calls == 1 && high.calls == 1);
    manager->clearSceneCallback();
    GXSetDrawSync(highStart);
    assert(high.calls == 1);
    DrawSyncManager::end();
    assert(!DrawSyncManager::sInstance);
    GXSetDrawSync(lowStart);
    assert(low.calls == 1);
    aurora_end_frame();
    aurora_shutdown();
    std::puts("Native GX tokens, display-list ordering and game callback routing passed");
    return 0;
}
