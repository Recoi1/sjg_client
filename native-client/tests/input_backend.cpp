#include <dolphin/pad.h>
#include <revolution/kpad.h>
#include <aurora/aurora.h>
#include <aurora/input.hpp>
#include <SDL3/SDL.h>
#include "input/sdl_input.hpp"
#include "mouse_controls.hpp"
#include "pad_snapshot.hpp"
#include "cutscene_skip.hpp"
#include <array>
#include <cassert>
namespace { SDL_Window* gameWindow = nullptr; }
extern "C" void startInputTest(int argc, char** argv) {
    AuroraConfig config{};
    config.appName = "Galaxy input check";
    config.userPath = config.cachePath = "./";
    const auto platform = aurora_initialize(argc, argv, &config);
    gameWindow = platform.window;
    SDL_SetWindowOpacity(platform.window, 0.0f);
}
extern "C" void checkMouseTest() {
    using namespace galaxy;
    using namespace aurora::input;
    initializeMouseControls(gameWindow);
    SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED; focus.window.windowID = SDL_GetWindowID(gameWindow);
    handleMouseWindowEvent(focus);
    auto cursor = [&]() { aurora::input::sdl::update_cursor(); return SDL_GetWindowRelativeMouseMode(gameWindow); };
    // Synthetic focus events do not take desktop focus; check SDL's requested grab flag.
    auto grabbed = [&]() { return bool(SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_MOUSE_GRABBED); };
    assert(pointer_mode() == PointerMode::Hidden && !cursor() && !SDL_CursorVisible());
    assert(grabbed());
    auto motion = [](float dx, float dy, float x = 0, float y = 0) {
        SDL_Event event{}; event.type = SDL_EVENT_MOUSE_MOTION; event.motion.xrel = dx; event.motion.yrel = dy;
        event.motion.x = x; event.motion.y = y;
        aurora::input::sdl::dispatch(event);
    };
    auto button = [](uint8_t value, bool down) {
        SDL_Event event{}; event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = value; event.button.down = down; aurora::input::sdl::dispatch(event);
    };
    auto wheel = [](float delta) {
        SDL_Event event{}; event.type = SDL_EVENT_MOUSE_WHEEL; event.wheel.y = delta;
        aurora::input::sdl::dispatch(event);
    };
    float x, y; int width, height; assert(SDL_GetWindowSize(gameWindow, &width, &height));
    motion(0, 0, width * 0.5f + 100, height * 0.5f - 50); beginMouseFrame();
    assert(mousePointer(x, y) && std::abs(x - 200.0f / width) < 0.00001f && std::abs(y + 100.0f / height) < 0.00001f);
    assert(mouseLook().x == 0 && mouseLook().y == 0);
    button(SDL_BUTTON_LEFT, true); beginMouseFrame();
    KPADStatus menuPad{}; assert(KPADRead(0, &menuPad, 1) == 1);
    assert(menuPad.trig & WPAD_BUTTON_A);
    assert(!(menuPad.hold & WPAD_BUTTON_B) && !cursor() && !SDL_CursorVisible());
    assert(!cutsceneSkipButtonHeld());
    assert(KPADRead(0, &menuPad, 1) == 1 && !(menuPad.trig & WPAD_BUTTON_A));
    button(SDL_BUTTON_LEFT, false); beginMouseFrame();
    assert(KPADRead(0, &menuPad, 1) == 1 && (menuPad.release & WPAD_BUTTON_A));
    button(SDL_BUTTON_LEFT, true); button(SDL_BUTTON_LEFT, false); beginMouseFrame();
    assert(KPADRead(0, &menuPad, 1) == 1 && (menuPad.trig & WPAD_BUTTON_A));
    beginMouseFrame(); assert(KPADRead(0, &menuPad, 1) == 1 && !menuPad.hold);
    SDL_Event skipKey{}; skipKey.type = SDL_EVENT_KEY_DOWN; skipKey.key.scancode = SDL_SCANCODE_B; skipKey.key.down = true;
    aurora::input::sdl::dispatch(skipKey);
    assert(cutsceneSkipButtonHeld());
    skipKey.type = SDL_EVENT_KEY_UP; skipKey.key.down = false; aurora::input::sdl::dispatch(skipKey);
    assert(!cutsceneSkipButtonHeld());
    skipKey.type = SDL_EVENT_KEY_DOWN; skipKey.key.down = true; aurora::input::sdl::dispatch(skipKey);
    setMouseContext(true, true); assert(!cutsceneSkipButtonHeld());
    setMouseContext(false, true); assert(!cutsceneSkipButtonHeld());
    skipKey.key.repeat = true; aurora::input::sdl::dispatch(skipKey); assert(!cutsceneSkipButtonHeld());
    skipKey.key.repeat = false; skipKey.type = SDL_EVENT_KEY_UP; skipKey.key.down = false;
    aurora::input::sdl::dispatch(skipKey);
    SDL_Event menuEscape{}; menuEscape.type = SDL_EVENT_KEY_DOWN;
    menuEscape.key.scancode = SDL_SCANCODE_ESCAPE; menuEscape.key.down = true;
    aurora::input::sdl::dispatch(menuEscape);
    assert(!grabbed() && !cursor() && SDL_CursorVisible());
    button(SDL_BUTTON_LEFT, true); beginMouseFrame();
    assert(grabbed() && !cursor() && !SDL_CursorVisible());
    assert(KPADRead(0, &menuPad, 1) == 1 && !(menuPad.trig & WPAD_BUTTON_A));
    button(SDL_BUTTON_LEFT, false); beginMouseFrame();
    button(SDL_BUTTON_LEFT, true); beginMouseFrame();
    assert(KPADRead(0, &menuPad, 1) == 1 && (menuPad.trig & WPAD_BUTTON_A));
    button(SDL_BUTTON_LEFT, false); beginMouseFrame(); KPADRead(0, &menuPad, 1);
    menuEscape.type = SDL_EVENT_KEY_UP; menuEscape.key.down = false;
    aurora::input::sdl::dispatch(menuEscape);
    wheel(-0.25f); wheel(-0.75f); beginMouseFrame();
    assert(mouseZoom() == 0 && takeMouseMenuScroll() == 1 && takeMouseMenuScroll() == 0);
    wheel(2); beginMouseFrame(); assert(takeMouseMenuScroll() == -2);
    wheel(-1); setMouseContext(false, false); beginMouseFrame(); assert(takeMouseMenuScroll() == 0);
    setMouseContext(false, false);
    assert(pointer_mode() == PointerMode::Relative && cursor());
    assert(grabbed());
    motion(40, 10); beginMouseFrame();
    assert(mouseLook().x == 40 && mouseLook().y == 10 && !mousePointer(x, y));
    beginMouseFrame(); assert(mouseLook().x == 0);
    wheel(-1); beginMouseFrame(); assert(mouseZoom() == 1);
    beginMouseFrame(); assert(mouseZoom() == 0);
    wheel(1); beginMouseFrame(); assert(mouseZoom() == -1);
    button(SDL_BUTTON_RIGHT, true); motion(30, 20); beginMouseFrame();
    assert(mousePointer(x, y) && std::abs(x - 260.0f / width) < 0.00001f && mouseLook().x == 0);
    wheel(-1); beginMouseFrame(); assert(mouseZoom() == 0);
    motion(100000, -100000); assert(mousePointer(x, y) && x == 1 && y == -1);
    button(SDL_BUTTON_RIGHT, false); motion(10, 5); beginMouseFrame(); assert(mouseLook().x == 10 && !mousePointer(x, y));
    SDL_Event escape{}; escape.type = SDL_EVENT_KEY_DOWN; escape.key.scancode = SDL_SCANCODE_ESCAPE; escape.key.down = true;
    aurora::input::sdl::dispatch(escape);
    assert(pointer_mode() == PointerMode::Visible && !cursor());
    assert(!grabbed());
    assert(SDL_CursorVisible());
    motion(100, 100); wheel(-1); beginMouseFrame(); assert(mouseLook().x == 0 && mouseZoom() == 0);
    button(SDL_BUTTON_LEFT, true);
    std::array<PADStatus, 4> pads{}; PADRead(pads.data());
    assert(pointer_mode() == PointerMode::Relative && cursor() && !(readNativePad(0).buttons & WPAD_BUTTON_B));
    button(SDL_BUTTON_LEFT, false); button(SDL_BUTTON_LEFT, true); PADRead(pads.data());
    assert(readNativePad(0).buttons & WPAD_BUTTON_B); button(SDL_BUTTON_LEFT, false);
    escape.type = SDL_EVENT_KEY_UP; escape.key.down = false; aurora::input::sdl::dispatch(escape);
    setMouseContext(true, false); assert(pointer_mode() == PointerMode::Visible && !cursor());
    assert(!grabbed());
    assert(SDL_CursorVisible());
    motion(100, 100); wheel(-1); beginMouseFrame(); assert(mouseLook().x == 0 && mouseZoom() == 0);
    setMouseContext(false, false); assert(cursor());
    setMouseContext(true, false);
    escape.type = SDL_EVENT_KEY_DOWN; escape.key.windowID = SDL_GetWindowID(gameWindow); handleMouseWindowEvent(escape);
    setMouseContext(false, false); assert(pointer_mode() == PointerMode::Visible && !cursor());
    button(SDL_BUTTON_LEFT, true); button(SDL_BUTTON_LEFT, false); assert(cursor());
    skipKey.type = SDL_EVENT_KEY_DOWN; skipKey.key.down = true; aurora::input::sdl::dispatch(skipKey);
    assert(cutsceneSkipButtonHeld());
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST; handleMouseWindowEvent(focus);
    assert(!cutsceneSkipButtonHeld());
    assert(pointer_mode() == PointerMode::Visible && !cursor() && !mousePointer(x, y));
    assert(!grabbed());
    assert(SDL_CursorVisible());
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED; handleMouseWindowEvent(focus);
    assert(!cutsceneSkipButtonHeld());
    skipKey.type = SDL_EVENT_KEY_UP; skipKey.key.down = false; aurora::input::sdl::dispatch(skipKey);
    assert(pointer_mode() == PointerMode::Visible);
    button(SDL_BUTTON_LEFT, true); button(SDL_BUTTON_LEFT, false); assert(cursor());
    shutdownMouseControls(); aurora::input::sdl::update_cursor();
    assert(!mouseControlsInitialized() && !SDL_GetWindowRelativeMouseMode(gameWindow));
    assert(!grabbed());
    assert(SDL_CursorVisible());
}
extern "C" void setInputTest(u32 channel, u16 buttons, s8 stickX, s8 stickY, bool connected) {
    if (!connected) { PADClearVirtualStatus(channel); return; }
    PADStatus status{};
    status.button = buttons;
    status.stickX = stickX; status.stickY = stickY;
    status.err = connected ? PAD_ERR_NONE : PAD_ERR_NO_CONTROLLER;
    PADSetVirtualStatus(channel, &status);
}
extern "C" void stopInputTest() { PADClearAllVirtualStatus(); aurora_shutdown(); }
extern "C" void checkKeyboardTest() {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_SPACE; event.key.down = true;
    aurora::input::sdl::dispatch(event);
    event = {}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button = SDL_BUTTON_LEFT; event.button.down = true;
    aurora::input::sdl::dispatch(event);
    std::array<PADStatus, 4> pads{}; PADRead(pads.data());
    assert(pads[0].button & PAD_BUTTON_A);
    const auto native=galaxy::readNativePad(0);
    assert((native.buttons & (WPAD_BUTTON_A | WPAD_BUTTON_B)) == (WPAD_BUTTON_A | WPAD_BUTTON_B));
    event = {}; event.type = SDL_EVENT_KEY_UP; event.key.scancode = SDL_SCANCODE_SPACE;
    aurora::input::sdl::dispatch(event);
    event = {}; event.type = SDL_EVENT_MOUSE_BUTTON_UP; event.button.button = SDL_BUTTON_LEFT;
    aurora::input::sdl::dispatch(event);
    event = {}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_W; event.key.down = true;
    aurora::input::sdl::dispatch(event);
    PADRead(pads.data()); assert(pads[0].stickY == 127 && pads[0].button == 0);
    event.type = SDL_EVENT_KEY_UP; event.key.down = false;
    aurora::input::sdl::dispatch(event);
}
