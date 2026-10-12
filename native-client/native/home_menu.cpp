#include "Game/System/HomeButtonMenuWrapper.hpp"
#include "home_menu.hpp"
#include "cutscene_skip.hpp"
#include "frame_timing.hpp"
#include "io.hpp"
#include <array>
#include <filesystem>
#include <aurora/imgui.h>
#include <SDL3/SDL.h>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
struct Menu {
    bool created = false;
    bool active = false;
    bool firstDraw = false;
    HBMSoundCallback sound = nullptr;
    HBMSelectBtnNum selected = HBM_SELECT_NULL;
} menu;
bool settingsOpen = false, backRequested = false, showFps = false;
float skipProgress = -1;
int skipPromptFrame = -1;
uint64_t skipPromptTick = 0;
SDL_Window* displayWindow = nullptr;
std::filesystem::path displaySettingsPath;
bool applyDisplayMode(bool enabled) {
    return displayWindow && (!enabled || SDL_SetWindowFullscreenMode(displayWindow, nullptr)) &&
        SDL_SetWindowFullscreen(displayWindow, enabled);
}
bool beginHomeWindow(const char* title) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos, {viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}, IM_COL32(0, 0, 0, 180));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 16});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 6});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {10, 10});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.035f, 0.055f, 0.10f, 0.98f});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.10f, 0.22f, 0.38f, 1});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.17f, 0.37f, 0.58f, 1});
    ImGui::PushStyleColor(ImGuiCol_Header, {0.10f, 0.22f, 0.38f, 1});
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(440.0f, viewport->Size.x - 24.0f), 100.0f}, ImGuiCond_Always);
    const bool visible = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(viewport->Size.y < 600 ? 1.0f : 1.25f);
    return visible;
}
void endHomeWindow() {
    ImGui::End(); ImGui::PopStyleColor(4); ImGui::PopStyleVar(5);
}
}

namespace galaxy {
bool updateCutsceneSkip(CutsceneSkipHold& hold, bool held) {
    if (settingsOpen || menu.active) { hold.reset(); return false; }
    if (independentRendering()) {
        if (hold.lastTick + 1 != simulationTick()) hold.reset();
        hold.lastTick = simulationTick();
    } else if (hold.lastUiFrame + 1 != ImGui::GetFrameCount()) hold.reset();
    hold.lastUiFrame = ImGui::GetFrameCount();
    const bool skip = hold.update(held, independentRendering() ? 1.0f/60 : ImGui::GetIO().DeltaTime);
    skipProgress = skip ? -1 : hold.seconds;
    skipPromptFrame = ImGui::GetFrameCount(); skipPromptTick = simulationTick();
    return skip;
}
float cutsceneSkipProgress() {
    return !settingsOpen && !menu.active && (independentRendering() ? skipPromptTick == simulationTick() : skipPromptFrame == ImGui::GetFrameCount()) ? skipProgress : -1;
}
bool initializeDisplaySettings(SDL_Window* window, const char* userPath) {
    displayWindow = window;
    displaySettingsPath = userPath ? aurora::io::fs_path_from_string(userPath) / "galaxy_display.dat" : std::filesystem::path{};
    showFps = false;
    if (!displaySettingsPath.empty()) {
        const auto data = aurora::io::read_file(displaySettingsPath);
        if (data && data->size() == 8 && (*data)[0] == 'G' && (*data)[1] == 'D' && (*data)[2] == 'S' &&
            (*data)[3] == 1 && (*data)[4] <= 1 && (*data)[5] <= 1 && (*data)[6] == 0 && (*data)[7] == 0) {
            showFps = (*data)[5];
            return applyDisplayMode((*data)[4]);
        }
    }
    return applyDisplayMode(false);
}
bool borderlessFullscreen() {
    return displayWindow && (SDL_GetWindowFlags(displayWindow) & SDL_WINDOW_FULLSCREEN);
}
bool setBorderlessFullscreen(bool enabled) {
    const bool previous = borderlessFullscreen();
    if (!applyDisplayMode(enabled)) return false;
    const std::array<uint8_t, 8> data{'G', 'D', 'S', 1, enabled, showFps, 0, 0};
    if (!displaySettingsPath.empty() && !aurora::io::write_file_atomic(displaySettingsPath, data)) {
        applyDisplayMode(previous);
        return false;
    }
    return true;
}
void handleControllerSettingsEvent(const SDL_Event& event) {
    if (settingsOpen && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_ESCAPE)
        backRequested = true;
}
bool controllerSettingsOpen() { return settingsOpen; }
void setControllerSettingsOpen(bool open) { settingsOpen = open; backRequested = false; }
bool takeSettingsBackRequest() { return std::exchange(backRequested, false); }
bool nativeMenuOpen() { return menu.active; }
bool fpsCounterEnabled() { return showFps; }
bool setFpsCounterEnabled(bool enabled) {
    const std::array<uint8_t, 8> data{'G', 'D', 'S', 1, borderlessFullscreen(), enabled, 0, 0};
    if (!displaySettingsPath.empty() && !aurora::io::write_file_atomic(displaySettingsPath, data)) return false;
    showFps = enabled;
    return true;
}
int fpsCounterValue(float fps) {
    return std::isfinite(fps) ? int(std::clamp(fps, 0.0f, 9999.0f) + 0.5f) : 0;
}
}

void RSO::setupRsoHomeButtonMenu() { menu = {}; }
void RSO::HBMCreate(const HBMDataInfo* info) {
    if (!info) throw std::invalid_argument("Missing home menu data");
    menu = {.created = true, .sound = info->sound_callback};
}
void RSO::HBMInit() {
    if (!menu.created) throw std::logic_error("Home menu was not created");
    menu.active = menu.firstDraw = true;
    menu.selected = HBM_SELECT_NULL;
    if (menu.sound) menu.sound(HBMSEV_PLAY_SOUND, HBMSE_HOME_BUTTON);
}
void RSO::HBMCalc(const HBMControllerData* controller) {
    if (!menu.active) return;
    if (!controller) throw std::invalid_argument("Missing home menu controller data");
    if (!menu.firstDraw) {
        for (const auto& pad : controller->wiiCon) {
            if (pad.kpad && (pad.kpad->trig & WPAD_BUTTON_HOME)) menu.selected = HBM_SELECT_HOMEBTN;
        }
    }
    if (menu.selected == HBM_SELECT_NULL) return;
    if (menu.selected == HBM_SELECT_BTN1) {
        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;
        if (!SDL_PushEvent(&event)) throw std::runtime_error(SDL_GetError());
    }
    menu.active = false;
    ImGui::SetWindowFocus(nullptr);
    if (menu.sound) menu.sound(HBMSEV_PLAY_SOUND, menu.selected == HBM_SELECT_BTN1 ? HBMSE_GOTO_MENU : HBMSE_RETURN_APP);
}
void RSO::HBMDraw() {
    if (!menu.active) return;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    // shortcut: the PC menu uses Resume/Quit; add restart after native reset/save behavior is validated.
    if (beginHomeWindow("Paused")) {
        const bool back = !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false));
        const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Resume", {width, 40.0f})) menu.selected = HBM_SELECT_HOMEBTN;
        ImGui::SameLine();
        if (ImGui::Button("Quit", {width, 40.0f})) menu.selected = HBM_SELECT_BTN1;
        if (!menu.firstDraw && back) menu.selected = HBM_SELECT_HOMEBTN;
    }
    endHomeWindow();
    menu.firstDraw = false;
}
HBMSelectBtnNum RSO::HBMGetSelectBtnNum() { return menu.selected; }
void RSO::HBMSetAdjustFlag(int) {
    // ImGui uses the host viewport dimensions rather than the Wii's manual aspect adjustment.
}
void RSO::HBMStartBlackOut() {
    if (menu.active) menu.selected = HBM_SELECT_HOMEBTN;
}
