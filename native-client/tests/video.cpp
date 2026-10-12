#include <aurora/aurora.h>
#include <aurora/main.h>
#include <dolphin/vi.h>
#include <dolphin/vi/vi_internal.hpp>
#include <SDL3/SDL.h>
#include <array>
#include <cassert>
#include <chrono>
#include <thread>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include "home_menu.hpp"
#include <cstdio>

extern "C" BOOL VIEnableDimming(BOOL);
extern "C" u32 VIGetDimmingCount();
namespace {
std::array<unsigned, 4> order{};
unsigned callbacks = 0;
std::array<u8, 32> oldBuffer{}, newBuffer{};
void before(u32 count) {
  assert(count == VIGetRetraceCount());
  assert(VIGetCurrentFrameBuffer() == oldBuffer.data());
  VISetNextFrameBuffer(newBuffer.data());
  order.at(callbacks++) = 1;
}
void after(u32 count) {
  assert(count == VIGetRetraceCount());
  assert(VIGetCurrentFrameBuffer() == newBuffer.data());
  order.at(callbacks++) = 2;
}
}
int main(int argc, char** argv) {
  AuroraConfig config{};
  config.appName = "Galaxy video check";
  config.userPath = config.cachePath = "./";
  const auto platform = aurora_initialize(argc, argv, &config);
  SDL_SetWindowOpacity(platform.window, 0.0f);
  GXInit(nullptr, 0);
  VIInit();
  assert(VIGetDTVStatus() == 1);
  VISetNextFrameBuffer(oldBuffer.data());
  VIWaitForRetrace();
  assert(VIGetCurrentFrameBuffer() == oldBuffer.data());
  assert(VISetPreRetraceCallback(before) == nullptr);
  assert(VISetPostRetraceCallback(after) == nullptr);
  const u32 count = VIGetRetraceCount();
  const auto start = std::chrono::steady_clock::now();
  VIWaitForRetrace();
  assert(VIGetRetraceCount() == count + 1 && callbacks == 2 && order[0] == 1 && order[1] == 2);
  assert(VISetPreRetraceCallback(nullptr) == before);
  assert(VISetPostRetraceCallback(nullptr) == after);
  VIWaitForRetrace();
  assert(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(20));
  assert(callbacks == 2 && VIGetRetraceCount() == count + 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  const auto late = std::chrono::steady_clock::now();
  VIWaitForRetrace();
  assert(std::chrono::steady_clock::now()-late < std::chrono::milliseconds(15));
  assert(VIGetRetraceCount() == count+3);
  VISetBlack(true);
  assert(aurora::vi::is_black());
  assert(aurora_begin_frame());
  aurora_end_frame();
  VISetBlack(false);
  assert(!aurora::vi::is_black());
  assert(VIEnableDimming(false));
  assert(!VIEnableDimming(true));
  assert(VIGetDimmingCount() <= 18000);
  assert(galaxy::initializeDisplaySettings(platform.window, nullptr));
  AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
  for (bool fullscreen : {true, false}) {
    aurora_update(); assert(aurora_begin_frame());
    // The menu switches modes during a frame; resizing must wait until the next update.
    assert(galaxy::setBorderlessFullscreen(fullscreen));
    aurora_end_frame(); assert(SDL_SyncWindow(platform.window)); aurora_update();
    assert(galaxy::borderlessFullscreen() == fullscreen);
    assert(aurora_begin_frame());
    const auto size = aurora_get_window_size();
    u32 renderWidth, renderHeight; AuroraGetRenderSize(&renderWidth, &renderHeight);
    assert(renderWidth == size.native_fb_width && renderHeight == size.native_fb_height);
    std::printf("Display switch during rendering: %u x %u %s\n", renderWidth, renderHeight,
        fullscreen ? "borderless fullscreen" : "windowed");
    aurora_end_frame();
  }
  aurora_shutdown();
  return 0;
}
