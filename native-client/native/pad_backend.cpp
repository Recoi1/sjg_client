#include "pad_snapshot.hpp"
#include "runtime_options.hpp"
#include "input_bindings.hpp"
#include "mouse_controls.hpp"
#include "cutscene_skip.hpp"
#include "internal.hpp"
#include "io.hpp"
#include <dolphin/pad.h>
#include <aurora/input.hpp>
#include <revolution/wpad.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <cstdio>

namespace galaxy {
namespace {
bool allStars = false, swapLeft = false, swapRight = false;
std::array<SDL_JoystickID, 4> configured{};
std::array<uint32_t, 4> suppressedButtons{};
const PADAxis axisTargets[] = {PAD_AXIS_LEFT_X_POS, PAD_AXIS_LEFT_X_NEG, PAD_AXIS_LEFT_Y_POS,
    PAD_AXIS_LEFT_Y_NEG, PAD_AXIS_RIGHT_X_POS, PAD_AXIS_RIGHT_X_NEG, PAD_AXIS_RIGHT_Y_POS,
    PAD_AXIS_RIGHT_Y_NEG, PAD_AXIS_TRIGGER_L, PAD_AXIS_TRIGGER_R};
const PADAxisMapping controllerAxes[] = {
    {{SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_POSITIVE}, -1, PAD_AXIS_LEFT_X_POS},
    {{SDL_GAMEPAD_AXIS_LEFTX, AXIS_SIGN_NEGATIVE}, -1, PAD_AXIS_LEFT_X_NEG},
    {{SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_NEGATIVE}, -1, PAD_AXIS_LEFT_Y_POS},
    {{SDL_GAMEPAD_AXIS_LEFTY, AXIS_SIGN_POSITIVE}, -1, PAD_AXIS_LEFT_Y_NEG},
    {{SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_POSITIVE}, -1, PAD_AXIS_RIGHT_X_POS},
    {{SDL_GAMEPAD_AXIS_RIGHTX, AXIS_SIGN_NEGATIVE}, -1, PAD_AXIS_RIGHT_X_NEG},
    {{SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_NEGATIVE}, -1, PAD_AXIS_RIGHT_Y_POS},
    {{SDL_GAMEPAD_AXIS_RIGHTY, AXIS_SIGN_POSITIVE}, -1, PAD_AXIS_RIGHT_Y_NEG},
    {{SDL_GAMEPAD_AXIS_LEFT_TRIGGER, AXIS_SIGN_POSITIVE}, -1, PAD_AXIS_TRIGGER_L},
    {{SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, AXIS_SIGN_POSITIVE}, -1, PAD_AXIS_TRIGGER_R},
};
uint32_t classicToWii(uint32_t buttons) {
    uint32_t result = 0;
    const std::pair<uint32_t, uint32_t> mapping[] = {
        {WPAD_CL_BUTTON_X, 0x0020}, {WPAD_CL_BUTTON_HOME, WPAD_BUTTON_HOME},
        {WPAD_CL_BUTTON_A, WPAD_BUTTON_A}, {WPAD_CL_BUTTON_B, allStars ? WPAD_BUTTON_A : WPAD_BUTTON_B},
        {WPAD_CL_BUTTON_UP, WPAD_BUTTON_UP}, {WPAD_CL_BUTTON_LEFT, WPAD_BUTTON_LEFT},
        {WPAD_CL_BUTTON_RIGHT, WPAD_BUTTON_RIGHT}, {WPAD_CL_BUTTON_DOWN, WPAD_BUTTON_DOWN},
        {WPAD_CL_BUTTON_PLUS, WPAD_BUTTON_PLUS}, {WPAD_CL_BUTTON_MINUS, WPAD_BUTTON_MINUS},
        {swapRight ? WPAD_CL_TRIGGER_R : WPAD_CL_TRIGGER_ZR, allStars ? WPAD_BUTTON_B : WPAD_BUTTON_A},
        {WPAD_CL_BUTTON_Y, allStars ? 0x0020 : WPAD_BUTTON_C},
        {swapRight ? WPAD_CL_TRIGGER_ZR : WPAD_CL_TRIGGER_R, allStars ? WPAD_BUTTON_A : WPAD_BUTTON_B},
        {swapLeft ? WPAD_CL_TRIGGER_L : WPAD_CL_TRIGGER_ZL, WPAD_BUTTON_Z},
        {swapLeft ? WPAD_CL_TRIGGER_ZL : WPAD_CL_TRIGGER_L, WPAD_BUTTON_C},
    };
    for (const auto [classic, wii] : mapping) if (buttons & classic) result |= wii;
    return result;
}
}
ControllerSettings getControllerSettings() { return {allStars, swapLeft, swapRight}; }
bool setControllerSettings(ControllerSettings settings) {
    if (aurora::g_config.userPath) {
        const std::array<uint8_t, 8> data{'G', 'C', 'C', 1, settings.allStars, settings.swapLeft, settings.swapRight, 0};
        if (!aurora::io::write_file_atomic(aurora::io::fs_path_from_string(aurora::g_config.userPath) / "galaxy_controller.dat", data))
            return false;
    }
    allStars = settings.allStars; swapLeft = settings.swapLeft; swapRight = settings.swapRight;
    // Changing layouts must not turn a held menu button into a new gameplay press.
    suppressedButtons.fill(0xffff);
    return true;
}
void initializeNativePads() {
    allStars = swapLeft = swapRight = false;
    if (aurora::g_config.userPath) {
        const auto data = aurora::io::read_file(aurora::io::fs_path_from_string(aurora::g_config.userPath) / "galaxy_controller.dat");
        if (data && data->size() == 8 && (*data)[0] == 'G' && (*data)[1] == 'C' && (*data)[2] == 'C' &&
            (*data)[3] == 1 && (*data)[4] <= 1 && (*data)[5] <= 1 && (*data)[6] <= 1 && (*data)[7] == 0) {
            allStars = (*data)[4]; swapLeft = (*data)[5]; swapRight = (*data)[6];
        }
    }
    const char* layout = std::getenv("GALAXY_CONTROLLER_LAYOUT");
    if (layout && std::strcmp(layout, "nvidia") && std::strcmp(layout, "allstars"))
        throw std::invalid_argument("GALAXY_CONTROLLER_LAYOUT must be nvidia or allstars");
    if (layout) allStars = !std::strcmp(layout, "allstars");
    if (const char* swap = std::getenv("GALAXY_SWAP_L_ZL")) swapLeft = !std::strcmp(swap, "1");
    if (const char* swap = std::getenv("GALAXY_SWAP_R_ZR")) swapRight = !std::strcmp(swap, "1");
    PADDefaultKeyBindings bindings{};
    for (auto& button : bindings.buttons) button.scancode = PAD_KEY_INVALID;
    for (unsigned i = 0; i < PAD_AXIS_COUNT; ++i) bindings.axes[i] = {PAD_KEY_INVALID, axisTargets[i], 0};
    bindings.buttons[0] = {SDL_SCANCODE_SPACE, PAD_BUTTON_A};
    bindings.buttons[1] = {PAD_KEY_MOUSE_LEFT, PAD_BUTTON_B};
    bindings.buttons[2] = {SDL_SCANCODE_LSHIFT, PAD_BUTTON_X};
    bindings.buttons[3] = {SDL_SCANCODE_C, PAD_BUTTON_Y};
    bindings.buttons[4] = {SDL_SCANCODE_RETURN, PAD_BUTTON_START};
    bindings.buttons[5] = {SDL_SCANCODE_LCTRL, PAD_TRIGGER_Z};
    bindings.buttons[6] = {SDL_SCANCODE_Q, PAD_TRIGGER_L};
    bindings.buttons[7] = {SDL_SCANCODE_E, PAD_TRIGGER_R};
    bindings.buttons[8] = {SDL_SCANCODE_UP, PAD_BUTTON_UP};
    bindings.buttons[9] = {SDL_SCANCODE_DOWN, PAD_BUTTON_DOWN};
    bindings.buttons[10] = {SDL_SCANCODE_LEFT, PAD_BUTTON_LEFT};
    bindings.buttons[11] = {SDL_SCANCODE_RIGHT, PAD_BUTTON_RIGHT};
    bindings.axes[0] = {SDL_SCANCODE_D, PAD_AXIS_LEFT_X_POS, 100};
    bindings.axes[1] = {SDL_SCANCODE_A, PAD_AXIS_LEFT_X_NEG, 100};
    bindings.axes[2] = {SDL_SCANCODE_W, PAD_AXIS_LEFT_Y_POS, 100};
    bindings.axes[3] = {SDL_SCANCODE_S, PAD_AXIS_LEFT_Y_NEG, 100};
    if (!PADSetDefaultKeyBindings(0, &bindings)) throw std::runtime_error("Could not register Galaxy keyboard controls");
    PADInit();
    PADRestoreDefaultKeyBindings(0);
    PADSetKeyboardActive(0, true);
    initializeInputBindings(aurora::g_config.userPath ? aurora::io::fs_path_from_string(aurora::g_config.userPath) : std::filesystem::path{});
}
PadSnapshot readNativePad(int channel) {
    PadSnapshot result;
    if (channel < 0 || channel >= 4) return result;
    if (option(Option::DisableGamepads)) { PADClearPort(channel); configured[channel]=0; }
    else if (channel == 0 && PADCount()) {
        const unsigned wanted = option(Option::Gamepad);
        if (wanted && wanted <= PADCount()) { if (PADGetIndexForPort(0) != int(wanted-1)) PADSetPortForIndex(wanted-1, 0); }
        else if (PADGetIndexForPort(0) < 0) PADSetPortForIndex(0, 0);
    }
    const auto index = PADGetIndexForPort(channel);
    const bool verification = std::getenv("GALAXY_VERIFY_CAPTURE") && std::getenv("GALAXY_VERIFY_INPUT");
    auto* controller = index >= 0 && !verification ? PADGetSDLGamepadForIndex(index) : nullptr;
    if (controller && configured[channel] != SDL_GetGamepadID(controller)) {
        u32 count;
        // Classic buttons are translated below; keep PAD's keyboard routing and calibrated axes.
        auto* buttons = PADGetButtonMappings(channel, &count);
        for (u32 i = 0; i < count; ++i) buttons[i].nativeButton = PAD_NATIVE_BUTTON_INVALID;
        auto* axes = PADGetAxisMappings(channel, &count);
        std::copy(std::begin(controllerAxes), std::end(controllerAxes), axes);
        PADGetDeadZones(channel)->emulateTriggers = false;
        configured[channel] = SDL_GetGamepadID(controller);
        suppressedButtons[channel] = 0;
        std::fprintf(stdout, "[native input] %s: Classic Controller %s, LB/LT swap=%d, RB/RT swap=%d\n",
                     SDL_GetGamepadName(controller), allStars ? "All-Stars" : "Nvidia", swapLeft, swapRight);
        std::fflush(stdout);
    }
    if (controller) {
        auto* zones = PADGetDeadZones(channel);
        zones->useDeadzones = true;
        zones->stickDeadZone = zones->substickDeadZone = option(Option::Deadzone)*32767/100;
        PADSetRumbleIntensity(channel, option(Option::RumbleStrength)*65535/100, option(Option::RumbleStrength)*65535/100);
    }
    std::array<PADStatus, 4> statuses{};
    PADRead(statuses.data());
    const auto& status = statuses[channel];
    result.connected = status.err == PAD_ERR_NONE;
    if (!result.connected) return result;
    constexpr std::array<std::pair<uint32_t, uint32_t>, 11> mapping{{
        {PAD_BUTTON_A, 0x0800}, {PAD_BUTTON_B, 0x0400}, {PAD_BUTTON_Y, 0x4000},
        {PAD_TRIGGER_Z, 0x2000}, {PAD_BUTTON_START, 0x0010},
        {PAD_BUTTON_LEFT, 1}, {PAD_BUTTON_RIGHT, 2}, {PAD_BUTTON_DOWN, 4}, {PAD_BUTTON_UP, 8},
        {PAD_TRIGGER_L, 0x0200}, {PAD_TRIGGER_R, 0x0100},
    }};
    for (const auto [native, wii] : mapping) if (status.button & native) result.buttons |= wii;
    if (status.button & PAD_BUTTON_X) result.buttons |= 0x0020;
    if (status.extButton & PAD_BUTTON_BACK) result.buttons |= 0x1000;
    if (status.extButton & PAD_BUTTON_GUIDE) result.buttons |= 0x8000;
    result.stickX = std::clamp(status.stickX / 127.0f, -1.0f, 1.0f);
    result.stickY = std::clamp(status.stickY / 127.0f, -1.0f, 1.0f);
    result.classic = controller != nullptr;
    result.rightStickX = status.substickX / 127.0f;
    result.rightStickY = status.substickY / 127.0f;
    if (controller) { transformStick(result.stickX, result.stickY, false); transformStick(result.rightStickX, result.rightStickY, true); }
    result.leftTrigger = status.triggerLeft / 255.0f;
    result.rightTrigger = status.triggerRight / 255.0f;
    const bool controllerCaptured = controller && aurora::input::captured_above(
        aurora::input::source_for_gamepad(SDL_GetGamepadID(controller)), aurora::input::kGameLayerPriority);
    if (controller) {
        result.classicButtons = boundClassicButtons(controller);
        if (controllerCaptured) suppressedButtons[channel] |= result.classicButtons;
        suppressedButtons[channel] &= result.classicButtons;
        result.classicButtons = controllerCaptured ? 0 : result.classicButtons & ~suppressedButtons[channel];
        result.buttons |= classicToWii(result.classicButtons);
    }
    if (PADHasSensor(channel, PAD_SENSOR_ACCEL)) {
        PADSetSensorEnabled(channel, PAD_SENSOR_ACCEL, true);
        float acceleration[3];
        if (PADGetSensorData(channel, PAD_SENSOR_ACCEL, acceleration, 3)) {
            result.accX = acceleration[0] / 9.80665f;
            result.accY = -acceleration[2] / 9.80665f;
            result.accZ = acceleration[1] / 9.80665f;
        }
    }
    // Spin and tilt are also available on controllers without motion sensors.
    if (channel==0) {
        constexpr uint32_t targets[]{WPAD_BUTTON_A,WPAD_BUTTON_B,0x0020,WPAD_BUTTON_C,WPAD_BUTTON_PLUS,WPAD_BUTTON_Z,0,0,WPAD_BUTTON_UP,WPAD_BUTTON_DOWN,WPAD_BUTTON_LEFT,WPAD_BUTTON_RIGHT};
        for(unsigned i=0;i<12;++i) if(mouseBindingValue(i)) result.buttons |= targets[i];
        result.stickX = std::clamp(result.stickX+mouseBindingValue(12)-mouseBindingValue(13),-1.0f,1.0f);
        result.stickY = std::clamp(result.stickY+mouseBindingValue(14)-mouseBindingValue(15),-1.0f,1.0f);
    }
    if (status.button & PAD_BUTTON_X || (channel==0 && mouseBindingValue(2))) result.accZ += 3;
    result.accX += ((status.button & PAD_TRIGGER_R) ? 0.6f : 0) - ((status.button & PAD_TRIGGER_L) ? 0.6f : 0);
    if(channel==0) result.accX += 0.6f*(mouseBindingValue(7)-mouseBindingValue(6));
    result.accZ = std::copysign(std::sqrt(std::max(0.0f, result.accZ * result.accZ - result.accX * result.accX)), result.accZ);
    if (channel == 0 && !aurora::input::captured_above(aurora::input::mouse_source().id, aurora::input::kGameLayerPriority)) {
        if (mouseMenuConfirm()) result.buttons |= WPAD_BUTTON_A;
        if (mouseControlsInitialized()) {
            result.pointerValid = mousePointer(result.pointerX, result.pointerY);
            result.mousePointerActive = result.pointerValid && mousePointerHeld();
        } else if (auto* window = SDL_GetMouseFocus()) {
            float x, y;
            SDL_GetMouseState(&x, &y);
            int width, height;
            if (SDL_GetWindowSize(window, &width, &height) && width > 0 && height > 0) {
                result.pointerX = x / width * 2 - 1;
                result.pointerY = y / height * 2 - 1;
                result.pointerValid = std::abs(result.pointerX) <= 1 && std::abs(result.pointerY) <= 1;
            }
        }
    }
    if (!result.classic && !PADIsInputCaptured(channel) && (channel != 0 || status.substickX || status.substickY ||
        (!result.pointerValid && PADGetIndexForPort(channel) >= 0) || std::getenv("GALAXY_VERIFY_INPUT"))) {
        result.pointerX = std::clamp(status.substickX / 127.0f, -1.0f, 1.0f);
        result.pointerY = std::clamp(-status.substickY / 127.0f, -1.0f, 1.0f);
        result.pointerValid = true;
    }
    int percentage = 100;
    if (index >= 0) if (auto* controller = PADGetSDLGamepadForIndex(index)) {
        if (SDL_GetGamepadPowerInfo(controller, &percentage) == SDL_POWERSTATE_ON_BATTERY)
            result.battery = static_cast<uint8_t>(std::clamp((percentage + 24) / 25, 0, 4));
    }
    return result;
}
bool cutsceneSkipButtonHeld() {
    if (keyboardSkipHeld()) return true;
    const auto pad = readNativePad(0);
    if(bindingCaptureActive() || bindingButtonSuppressed()) return false;
    if(pad.classic) {
        auto* controller = PADGetSDLGamepadForIndex(PADGetIndexForPort(0));
        return controller && !aurora::input::captured_above(aurora::input::source_for_gamepad(SDL_GetGamepadID(controller)),aurora::input::kGameLayerPriority) && SDL_GetGamepadButton(controller,SDL_GAMEPAD_BUTTON_EAST);
    }
    return pad.buttons & WPAD_BUTTON_B;
}
void rumbleNativePad(int channel, bool enabled) {
    if (channel >= 0 && channel < 4) PADControlMotor(channel, enabled ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP);
}
}
