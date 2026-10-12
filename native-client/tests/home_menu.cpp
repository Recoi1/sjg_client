#include "Game/System/HomeButtonMenuWrapper.hpp"
#include "home_menu.hpp"
#include "hd_assets.hpp"
#include "pad_snapshot.hpp"
#include "cutscene_skip.hpp"
#include <aurora/imgui.h>
#include <aurora/main.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <array>
#include <cstdio>
#include <limits>

namespace {
unsigned sounds = 0;
int lastSound = -1;
int sound(int event, int value) {
    assert(event == HBMSEV_PLAY_SOUND);
    ++sounds;
    lastSound = value;
    return HBMSEV_RET_PLAY_SOUND;
}
void frame(HBMControllerData& controller) {
    ImGui::NewFrame();
    RSO::HBMCalc(&controller);
    RSO::HBMDraw();
    ImGui::Render();
}

}
int main(int argc, char** argv) {
    assert(argc == 2);
    for (float rate : {30.0f, 60.0f, 120.0f}) {
        galaxy::CutsceneSkipHold hold;
        for (unsigned i = 0; i < unsigned(rate * 2); ++i) assert(!hold.update(true, 1 / rate));
        assert(!hold.update(false, 1 / rate));
        for (unsigned i = 0; i < unsigned(rate / 2); ++i) assert(!hold.update(true, 1 / rate));
        assert(hold.seconds > 0.49f && hold.seconds < 0.51f);
        assert(!hold.update(false, 1 / rate) && hold.seconds == 0);
        for (unsigned i = 0; i < unsigned(rate) - 1; ++i) assert(!hold.update(true, 1 / rate));
        hold.update(true, 1 / rate);
        assert(hold.update(true, 1 / rate));
        hold.reset(); assert(!hold.update(true, 1 / rate) && hold.seconds == 0);
        hold.update(false, 1 / rate);
        for (unsigned i = 0; i < unsigned(rate * 0.9f); ++i) hold.update(true, 1 / rate);
        assert(!hold.update(true, 5) && hold.seconds == 0 && !hold.armed);
        assert(!hold.update(true, 1 / rate));
    }
    assert(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO));
    const auto preferences = std::filesystem::path(argv[1]) / "galaxy_display.dat";
    const auto backup = std::filesystem::path(argv[1]) / "galaxy_display.backup";
    std::filesystem::create_directories(preferences.parent_path());
    std::filesystem::remove(preferences);
    auto* display = SDL_CreateWindow("Galaxy display check", 1280, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE); assert(display);
    assert(SDL_SetWindowOpacity(display, 0)); assert(SDL_ShowWindow(display));
    assert(galaxy::initializeDisplaySettings(display, argv[1]) && !galaxy::borderlessFullscreen());
    assert(galaxy::setBorderlessFullscreen(true)); assert(SDL_SyncWindow(display));
    assert(galaxy::borderlessFullscreen() && !SDL_GetWindowFullscreenMode(display));
    int fullscreenWidth, fullscreenHeight;
    assert(SDL_GetWindowSizeInPixels(display, &fullscreenWidth, &fullscreenHeight));
    const auto* desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(display)); assert(desktop);
    assert(fullscreenWidth == desktop->w && fullscreenHeight == desktop->h);
    std::printf("Borderless fullscreen: %d x %d, no exclusive display mode\n", fullscreenWidth, fullscreenHeight);
    std::ifstream saved(preferences, std::ios::binary); std::array<unsigned char, 8> bytes{};
    assert(saved.read(reinterpret_cast<char*>(bytes.data()), bytes.size())); saved.close();
    assert((bytes == std::array<unsigned char, 8>{'G', 'D', 'S', 1, 1, 0, 0, 0}));
    assert(SDL_SetWindowFullscreen(display, false) && SDL_SyncWindow(display));
    assert(galaxy::initializeDisplaySettings(display, argv[1]) && SDL_SyncWindow(display));
    assert(galaxy::borderlessFullscreen());
    std::filesystem::rename(preferences, backup); assert(std::filesystem::create_directory(preferences));
    assert(!galaxy::setBorderlessFullscreen(false) && SDL_SyncWindow(display));
    assert(galaxy::borderlessFullscreen());
    assert(std::filesystem::remove(preferences)); std::filesystem::rename(backup, preferences);
    { std::ofstream invalid(preferences, std::ios::binary); const char bad[] = {'G', 'D', 'S', 1, 2, 0, 0, 0}; invalid.write(bad, sizeof(bad)); }
    assert(galaxy::initializeDisplaySettings(display, argv[1]) && SDL_SyncWindow(display));
    assert(!galaxy::borderlessFullscreen());
    assert(galaxy::setBorderlessFullscreen(false));
    galaxy::initializeHdAssets({}, argv[1]);
    assert(galaxy::setHdTexturesEnabled(true) && galaxy::setHdMoviesEnabled(true));
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {640, 480};
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    assert(pixels && width && height);
    RSO::setupRsoHomeButtonMenu();
    galaxy::CutsceneSkipHold hold;
    ImGui::NewFrame(); assert(!galaxy::updateCutsceneSkip(hold, false)); ImGui::Render();
    ImGui::NewFrame(); assert(!galaxy::updateCutsceneSkip(hold, true)); ImGui::Render();
    assert(!ImGui::FindWindowByName("Cutscene skip"));
    assert(hold.seconds > 0 && hold.armed && galaxy::cutsceneSkipProgress() == hold.seconds);
    ImGui::NewFrame(); ImGui::Render();
    assert(galaxy::cutsceneSkipProgress() < 0);
    ImGui::NewFrame(); assert(!galaxy::updateCutsceneSkip(hold, true)); ImGui::Render();
    assert(!hold.armed && hold.seconds == 0);
    try { RSO::HBMInit(); assert(false); } catch (const std::logic_error&) {}
    try { RSO::HBMCreate(nullptr); assert(false); } catch (const std::invalid_argument&) {}
    HBMDataInfo info{};
    info.sound_callback = sound;
    RSO::HBMCreate(&info);
    HBMControllerData controller{};
    KPADStatus pad{};
    controller.wiiCon[0].kpad = &pad;
    RSO::HBMInit();
    assert(sounds == 1 && lastSound == HBMSE_HOME_BUTTON);
    ImGui::NewFrame(); assert(!galaxy::updateCutsceneSkip(hold, true)); ImGui::Render();
    assert(!hold.armed && hold.seconds == 0);
    pad.trig = WPAD_BUTTON_HOME;
    RSO::HBMCalc(&controller);
    assert(RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL);
    pad.trig = 0;
    frame(controller);
    frame(controller);
    assert(ImGui::GetDrawData()->TotalVtxCount > 0 && RSO::HBMGetSelectBtnNum() == HBM_SELECT_NULL);
    pad.trig = WPAD_BUTTON_HOME;
    RSO::HBMCalc(&controller);
    assert(RSO::HBMGetSelectBtnNum() == HBM_SELECT_HOMEBTN && lastSound == HBMSE_RETURN_APP);
    pad.trig = 0;
    RSO::HBMInit();
    frame(controller);
    frame(controller);
    assert(ImGui::FindWindowByName("Paused"));
    RSO::HBMStartBlackOut(); frame(controller);
    assert(galaxy::setFpsCounterEnabled(true) && galaxy::fpsCounterEnabled());
    assert(galaxy::setBorderlessFullscreen(true));
    assert(galaxy::initializeDisplaySettings(display, argv[1]) && galaxy::fpsCounterEnabled());
    assert(galaxy::setBorderlessFullscreen(false) && galaxy::fpsCounterEnabled());
    assert(galaxy::initializeDisplaySettings(display, argv[1]) && galaxy::fpsCounterEnabled());
    std::filesystem::rename(preferences, backup); assert(std::filesystem::create_directory(preferences));
    assert(!galaxy::setFpsCounterEnabled(false) && galaxy::fpsCounterEnabled());
    assert(std::filesystem::remove(preferences)); std::filesystem::rename(backup, preferences);
    assert(galaxy::setFpsCounterEnabled(false));
    assert(galaxy::initializeDisplaySettings(display, argv[1]) && !galaxy::fpsCounterEnabled());
    assert(galaxy::fpsCounterValue(59.7f) == 60 && galaxy::fpsCounterValue(-1) == 0);
    assert(galaxy::fpsCounterValue(std::numeric_limits<float>::infinity()) == 0);
    assert(galaxy::fpsCounterValue(std::numeric_limits<float>::quiet_NaN()) == 0);
    assert(galaxy::fpsCounterValue(100000) == 9999);
    hold.reset();
    for (unsigned i = 0; i <= 30; ++i) {
        ImGui::NewFrame();
        assert(!galaxy::updateCutsceneSkip(hold, i != 0)); ImGui::Render();
    }
    assert(hold.seconds > 0.49f && hold.seconds < 0.51f);
    assert(!ImGui::FindWindowByName("Cutscene skip") && galaxy::cutsceneSkipProgress() == hold.seconds);
    SDL_Event hotkey{}; hotkey.type = SDL_EVENT_KEY_DOWN; hotkey.key.scancode = SDL_SCANCODE_F1;
    galaxy::handleControllerSettingsEvent(hotkey); assert(!galaxy::controllerSettingsOpen());
    galaxy::setControllerSettingsOpen(true);
    assert(!galaxy::nativeMenuOpen());
    hold.update(false, 0); hold.update(true, 0.1f);
    ImGui::NewFrame(); assert(!galaxy::updateCutsceneSkip(hold, true)); ImGui::Render();
    assert(!hold.armed && hold.seconds == 0);
    hotkey.key.scancode = SDL_SCANCODE_ESCAPE; galaxy::handleControllerSettingsEvent(hotkey);
    assert(galaxy::takeSettingsBackRequest() && !galaxy::takeSettingsBackRequest());
    galaxy::setControllerSettingsOpen(false);
    RSO::HBMInit();
    RSO::HBMStartBlackOut();
    RSO::HBMCalc(&controller);
    assert(RSO::HBMGetSelectBtnNum() == HBM_SELECT_HOMEBTN);
    ImGui::DestroyContext();
    galaxy::shutdownHdAssets();
    SDL_DestroyWindow(display);
    SDL_Quit();
    return 0;
}
