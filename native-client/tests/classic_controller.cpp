#include "Game/System/WPad.hpp"
#include "Game/System/WPadHolder.hpp"
#include "Game/System/WPadButton.hpp"
#include "Game/System/WPadStick.hpp"
#include "Game/System/WPadPointer.hpp"
#include "Game/System/WPadAcceleration.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "crash.hpp"
#include "pad_snapshot.hpp"
#include "runtime_options.hpp"
#include "input_bindings.hpp"
#include "home_menu.hpp"
#include "mouse_controls.hpp"
#include "cutscene_skip.hpp"
#include "../aurora/lib/input/sdl_input.hpp"
#include <aurora/aurora.h>
#include <aurora/input.hpp>
#include <dolphin/pad.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <cmath>
#include <fstream>
#include <array>
#include <filesystem>
#include <cstring>

namespace {
unsigned extensions = 0;
void extension(s32 channel, s32 type) {
    assert(channel == 0 && (type == WPAD_DEV_CLASSIC || type == WPAD_DEV_FREESTYLE));
    if (type == WPAD_DEV_CLASSIC) ++extensions;
}
const std::pair<SDL_GamepadButton, u32> buttons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, WPAD_CL_BUTTON_A}, {SDL_GAMEPAD_BUTTON_EAST, WPAD_CL_BUTTON_B},
    {SDL_GAMEPAD_BUTTON_WEST, WPAD_CL_BUTTON_X}, {SDL_GAMEPAD_BUTTON_NORTH, WPAD_CL_BUTTON_Y},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, WPAD_CL_TRIGGER_L}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, WPAD_CL_TRIGGER_R},
    {SDL_GAMEPAD_BUTTON_START, WPAD_CL_BUTTON_PLUS}, {SDL_GAMEPAD_BUTTON_BACK, WPAD_CL_BUTTON_MINUS},
    {SDL_GAMEPAD_BUTTON_GUIDE, WPAD_CL_BUTTON_HOME}, {SDL_GAMEPAD_BUTTON_DPAD_UP, WPAD_CL_BUTTON_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, WPAD_CL_BUTTON_DOWN}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, WPAD_CL_BUTTON_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, WPAD_CL_BUTTON_RIGHT},
};
}

int main(int argc, char** argv) {
    assert(argc == 3 || argc == 4);
    galaxy::installCrashHandler();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    AuroraConfig config{}; config.appName = "Galaxy Classic Controller check";
    config.userPath = config.cachePath = argv[2];
    const auto platform = aurora_initialize(argc, argv, &config);
    SDL_SetWindowOpacity(platform.window, 0);
    auto* root = JKRExpHeap::createRoot(1, false); assert(root);
    KPADInit();
    if (argc == 4) {
        const galaxy::ControllerSettings expected = !std::strcmp(argv[3], "saved") ?
            galaxy::ControllerSettings{true, true, false} : galaxy::ControllerSettings{};
        assert(galaxy::getControllerSettings() == expected);
        aurora_shutdown();
        return 0;
    }
    SDL_VirtualJoystickDesc desc; SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD; desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << desc.naxes) - 1; desc.button_mask = (1u << desc.nbuttons) - 1;
    desc.name = "Galaxy Classic Controller test";
    const auto id = SDL_AttachVirtualJoystick(&desc); assert(id);
    auto* joystick = SDL_OpenJoystick(id); assert(joystick);
    aurora_update();
    for (u32 i = 0; i < PADCount(); ++i) {
        if (SDL_GetGamepadID(PADGetSDLGamepadForIndex(i)) == id) { PADSetPortForIndex(i, 0); break; }
    }
    assert(SDL_GetGamepadID(PADGetSDLGamepadForIndex(PADGetIndexForPort(0))) == id);
    WPADSetExtensionCallback(0, extension);
    WPad pad(0); WPadReadDataInfo info; pad.setReadInfo(&info);
    auto read = [&]() -> const KPADStatus& {
        SDL_UpdateJoysticks(); aurora_update(); info.mValidStatusCount = KPADRead(0, info.mStatusArray, 1);
        assert(info.mValidStatusCount == 1);
        pad.mButton->update(); pad.mStick->update(); pad.mSubPadAccel->update(); pad.mPointer->update();
        return info.mStatusArray[0];
    };
    auto setButtons = [&](u32 bits) {
        for (const auto [button, mask] : buttons) assert(SDL_SetJoystickVirtualButton(joystick, button, (bits & mask) != 0));
        assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, bits & WPAD_CL_TRIGGER_ZL ? 32767 : -32768));
        assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, bits & WPAD_CL_TRIGGER_ZR ? 32767 : -32768));
    };
    setButtons(0); read();
    assert(extensions == 1);
    galaxy::captureBinding(1,0); setButtons(WPAD_CL_BUTTON_B); read();
    assert(!galaxy::bindingCaptureActive() && galaxy::bindingCode(1,0)==SDL_GAMEPAD_BUTTON_EAST);
    assert(read().hold==0 && !galaxy::cutsceneSkipButtonHeld());
    setButtons(0); read(); setButtons(WPAD_CL_BUTTON_B);
    assert(read().hold & WPAD_BUTTON_A); assert(galaxy::cutsceneSkipButtonHeld());
    setButtons(0); read(); assert(galaxy::resetInputBindings());
    u32 type; assert(WPADProbe(0, &type) == WPAD_ERR_NONE && type == WPAD_DEV_CLASSIC);
    std::ifstream expected(argv[1], std::ios::binary); assert(expected);
    unsigned entries = 0;
    for (std::array<u32, 2> entry; expected.read(reinterpret_cast<char*>(entry.data()), sizeof(entry));) {
        setButtons(entry[0]); const auto& status = read();
        if (status.ex_status.cl.hold != entry[0] || status.ex_status.cl.trig != entry[0])
            std::fprintf(stderr, "Classic sample %u: expected %04x, hold=%04x trig=%04x Wii=%04x\n", entries, entry[0], status.ex_status.cl.hold, status.ex_status.cl.trig, status.hold);
        assert(status.dev_type == WPAD_DEV_CLASSIC && status.data_format == WPAD_FMT_CLASSIC_ACC_DPD);
        assert(status.ex_status.cl.hold == entry[0] && status.ex_status.cl.trig == entry[0]);
        assert((status.hold & KPAD_BUTTON_MASK) == entry[1] && status.trig == entry[1]);
        assert(galaxy::cutsceneSkipButtonHeld() == bool(entry[0] & WPAD_CL_BUTTON_B));
        if (entry[1] & WPAD_BUTTON_A) assert(pad.mButton->testButtonA());
        if (entry[1] & WPAD_BUTTON_B) assert(pad.mButton->testButtonB());
        if (entry[1] & WPAD_BUTTON_C) assert(pad.mButton->testButtonC());
        if (entry[1] & WPAD_BUTTON_Z) assert(pad.mButton->testButtonZ());
        assert(read().trig == 0);
        setButtons(0);
        for (unsigned i = 0; (read().hold & entry[1]) && i < 100; ++i) SDL_Delay(5);
        const auto& released = info.mStatusArray[0];
        if (released.release != entry[1]) std::fprintf(stderr, "Classic release %u: expected %04x, release=%04x hold=%04x\n", entries, entry[1], released.release, released.hold);
        assert(released.release == entry[1]); ++entries;
    }
    assert(entries == 15 && expected.eof());
    setButtons(WPAD_CL_BUTTON_A | WPAD_CL_TRIGGER_R | WPAD_CL_TRIGGER_ZR);
    assert((read().hold & (WPAD_BUTTON_A | WPAD_BUTTON_B)) == (WPAD_BUTTON_A | WPAD_BUTTON_B));
    setButtons(0); read();
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 16000));
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -24000));
    const auto& moving = read();
    assert(pad.mStick->mStick.x > 0.25f && pad.mStick->mStick.y > 0.5f);
    TVec3f tilt; assert(pad.mSubPadAccel->getAcceleration(&tilt));
    assert(tilt.x == moving.ex_status.cl.lstick.x && tilt.y == tilt.x && tilt.z == moving.ex_status.cl.lstick.y);
    KPADReset(); read(); assert(!galaxy::pointerVisible(0));
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 32767));
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, -32768));
    const auto before = read().pos;
    assert(galaxy::pointerVisible(0));
    const auto after = read().pos;
    assert(after.x > before.x && after.y < before.y);
    for (unsigned i = 0; i < 45; ++i) read();
    assert(read().pos.x == 1 && read().pos.y == -1 && pad.mPointer->mIsPointInScreen);
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 0));
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, 0));
    const auto held = read().pos;
    for (unsigned i = 0; i < 8; ++i) assert(read().pos.x == held.x && read().pos.y == held.y);
    assert(galaxy::pointerVisible(0));
    SDL_Delay(2050); read(); assert(!galaxy::pointerVisible(0));
    pad.mPointer->reset(); assert(pad.mPointer->mIsPointInScreen);
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 1000));
    assert(read().pos.x == held.x && !galaxy::pointerVisible(0));
    bool captured = false;
    const auto layer = aurora::input::register_layer({
        .label = "controller test menu", .priority = aurora::input::kGameLayerPriority + 1,
        .onEvent = [](const aurora::input::InputEvent&, void*) { return aurora::input::EventResult::Pass; },
        .capturesSource = [](const aurora::input::InputSource& source, void* data) {
            return source.kind == aurora::input::InputSource::Kind::Controller && *static_cast<bool*>(data);
        }, .userdata = &captured,
    });
    assert(layer != aurora::input::kInvalidLayerId);
    setButtons(WPAD_CL_BUTTON_A); assert(read().hold & WPAD_BUTTON_A);
    captured = true; assert(!read().hold);
    captured = false; assert(!read().hold);
    setButtons(0); read(); setButtons(WPAD_CL_BUTTON_A); assert(read().trig & WPAD_BUTTON_A);
    setButtons(0); read(); aurora::input::unregister_layer(layer);
    SDL_Event chord{}; chord.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    chord.gbutton.which = id; chord.gbutton.button = SDL_GAMEPAD_BUTTON_START;
    galaxy::handleControllerSettingsEvent(chord); assert(!galaxy::controllerSettingsOpen());
    setButtons(WPAD_CL_BUTTON_MINUS | WPAD_CL_BUTTON_PLUS); read();
    galaxy::handleControllerSettingsEvent(chord); assert(!galaxy::controllerSettingsOpen());
    setButtons(0); read();
    // Layout changes are immediate, but presses held across the change stay suppressed.
    setButtons(WPAD_CL_BUTTON_B); read();
    const galaxy::ControllerSettings saved{true, true, false};
    assert(galaxy::setControllerSettings(saved) && galaxy::getControllerSettings() == saved);
    assert(!read().hold);
    setButtons(0); read(); setButtons(WPAD_CL_BUTTON_B); assert(read().trig == WPAD_BUTTON_A);
    setButtons(0); read(); setButtons(WPAD_CL_TRIGGER_L); assert(read().trig == WPAD_BUTTON_Z);
    setButtons(0); read();
    const auto preferences = std::filesystem::path(argv[2]) / "galaxy_controller.dat";
    const auto backup = std::filesystem::path(argv[2]) / "galaxy_controller.backup";
    std::filesystem::rename(preferences, backup);
    assert(std::filesystem::create_directory(preferences));
    assert(!galaxy::setControllerSettings({}) && galaxy::getControllerSettings() == saved);
    assert(std::filesystem::remove(preferences)); std::filesystem::rename(backup, preferences);
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 32767)); read();
    galaxy::initializeMouseControls(platform.window);
    SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED; focus.window.windowID = SDL_GetWindowID(platform.window);
    galaxy::handleMouseWindowEvent(focus); galaxy::setMouseContext(false, false);
    SDL_Event mouse{}; mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN; mouse.button.button = SDL_BUTTON_RIGHT; mouse.button.down = true;
    aurora::input::sdl::dispatch(mouse);
    mouse = {}; mouse.type = SDL_EVENT_MOUSE_MOTION; mouse.motion.xrel = 100; mouse.motion.yrel = -50;
    aurora::input::sdl::dispatch(mouse);
    int width, height; assert(SDL_GetWindowSize(platform.window, &width, &height));
    KPADStatus mouseStatus{}; assert(KPADRead(0, &mouseStatus, 1) == 1);
    assert(galaxy::pointerVisible(0));
    assert(std::abs(mouseStatus.pos.x - 200.0f / width) < 0.00001f && std::abs(mouseStatus.pos.y + 100.0f / height) < 0.00001f);
    assert(KPADRead(0, &mouseStatus, 1) == 1 && std::abs(mouseStatus.pos.x - 200.0f / width) < 0.00001f);
    mouse = {}; mouse.type = SDL_EVENT_MOUSE_BUTTON_UP; mouse.button.button = SDL_BUTTON_RIGHT;
    aurora::input::sdl::dispatch(mouse);
    assert(KPADRead(0, &mouseStatus, 1) == 1 && mouseStatus.pos.x > 200.0f / width);
    galaxy::setMouseContext(false, true);
    assert(aurora::input::pointer_mode() == aurora::input::PointerMode::Hidden);
    // A stationary menu mouse must not reset the controller pointer each sample.
    mouse = {}; mouse.type = SDL_EVENT_MOUSE_MOTION;
    mouse.motion.x = width * 0.5f; mouse.motion.y = height * 0.5f;
    aurora::input::sdl::dispatch(mouse);
    assert(KPADRead(0, &mouseStatus, 1) == 1 && mouseStatus.pos.x == 0 && mouseStatus.pos.y == 0);
    const auto menuBefore = read().pos;
    const auto menuAfter = read().pos;
    assert(menuAfter.x > menuBefore.x);
    assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 0));
    const auto menuHeld = read().pos;
    for (unsigned i = 0; i < 8; ++i) assert(read().pos.x == menuHeld.x);
    mouse.motion.x = width * 0.25f; mouse.motion.y = height * 0.75f;
    aurora::input::sdl::dispatch(mouse);
    assert(KPADRead(0, &mouseStatus, 1) == 1 && mouseStatus.pos.x == -0.5f && mouseStatus.pos.y == 0.5f);
    assert(galaxy::pointerVisible(0));
    mouse = {}; mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN; mouse.button.button = SDL_BUTTON_LEFT; mouse.button.down = true;
    aurora::input::sdl::dispatch(mouse); galaxy::beginMouseFrame();
    assert(KPADRead(0, &mouseStatus, 1) == 1 && (mouseStatus.trig & WPAD_BUTTON_A) && !(mouseStatus.hold & WPAD_BUTTON_B));
    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP; mouse.button.down = false;
    aurora::input::sdl::dispatch(mouse); galaxy::beginMouseFrame();
    assert(KPADRead(0, &mouseStatus, 1) == 1 && (mouseStatus.release & WPAD_BUTTON_A));
    galaxy::shutdownMouseControls();
    SDL_CloseJoystick(joystick); assert(SDL_DetachVirtualJoystick(id)); aurora_update();
    // Auto can fall back to another connected device; disabling gamepads preserves keyboard input.
    if(PADCount()) assert(read().dev_type==WPAD_DEV_CLASSIC);
    assert(galaxy::setOption(galaxy::Option::DisableGamepads,1));
    const auto& disconnected = read();
    assert(disconnected.dev_type == WPAD_DEV_FREESTYLE && disconnected.ex_status.cl.hold == 0);
    // Keyboard port zero remains usable after unplugging the controller.
    assert(WPADProbe(0, &type) == WPAD_ERR_NONE && type == WPAD_DEV_FREESTYLE);
    galaxy::initializeMouseControls(platform.window);
    SDL_Event key{}; key.type = SDL_EVENT_KEY_DOWN; key.key.scancode = SDL_SCANCODE_W; key.key.down = true;
    aurora::input::sdl::dispatch(key);
    key.key.scancode = SDL_SCANCODE_D; aurora::input::sdl::dispatch(key);
    assert(KPADRead(0, &mouseStatus, 1) == 1);
    assert(mouseStatus.ex_status.fs.acc.x == -1 && mouseStatus.ex_status.fs.acc.y == -1 && mouseStatus.ex_status.fs.acc.z == 1);
    info.mStatusArray[0] = mouseStatus; info.mValidStatusCount = 1; pad.mSubPadAccel->update();
    assert(pad.mSubPadAccel->getAcceleration(&tilt) && tilt.x == 1 && tilt.y == 1 && tilt.z == 1);
    key.type = SDL_EVENT_KEY_UP; key.key.down = false; aurora::input::sdl::dispatch(key);
    key.key.scancode = SDL_SCANCODE_W; aurora::input::sdl::dispatch(key);
    galaxy::shutdownMouseControls();
    aurora_shutdown();
}
