#include "pad_snapshot.hpp"
#include "mouse_controls.hpp"
#include <revolution/kpad.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace {
using Clock = std::chrono::steady_clock;
struct Pad {
    KPADStatus last{};
    WPADConnectCallback connect = nullptr;
    WPADExtensionCallback extension = nullptr;
    bool connected = false, disconnected = false;
    u32 device = WPAD_DEV_NOT_FOUND;
    Vec2 pointer{};
    Vec2 lastMouse{};
    bool mouseInitialized = false;
    float delay = 0.5f, pulse = 1.0f / 6;
    Clock::time_point repeat = Clock::time_point::max();
    Clock::time_point pointerUntil = Clock::time_point::min();
};
std::array<Pad, 4> pads;
std::recursive_mutex mutex;
bool initialized = false;
Pad& get(s32 channel) {
    if (channel < 0 || channel >= 4) throw std::invalid_argument("Invalid controller channel");
    return pads[channel];
}
void parameter(s32 channel, float radius, float sensitivity) {
    std::lock_guard lock(mutex);
    get(channel);
    if (!std::isfinite(radius) || !std::isfinite(sensitivity) || radius < 0 || sensitivity < 0)
        throw std::invalid_argument("Invalid controller calibration");
    // Native digital coordinates need no infrared camera noise filtering.
}
}
namespace galaxy {
bool pointerVisible(int channel) {
    std::lock_guard lock(mutex);
    if (channel < 0 || channel >= 4) return false;
    const auto& pad = pads[channel];
    return pad.connected && pad.last.dpd_valid_fg && Clock::now() < pad.pointerUntil;
}
}
extern "C" {
s32 WPADReadFaceData(s32 channel, void* output, u16 length, u32, WPADCallback callback) {
    if (channel < 0 || channel >= 4 || !output || !length) return WPAD_ERR_INVALID;
    // Native controllers do not expose Wii Remote EEPROM or stored Mii records.
    if (callback) callback(channel, WPAD_ERR_NOPERM);
    return WPAD_ERR_NOPERM;
}
void KPADInit() {
    std::lock_guard lock(mutex);
    if (!initialized) { galaxy::initializeNativePads(); initialized = true; }
}
void KPADReset() {
    std::lock_guard lock(mutex);
    for (auto& pad : pads) { pad.last = {}; pad.pointer = {}; pad.repeat = Clock::time_point::max(); pad.pointerUntil = Clock::time_point::min(); }
}
s32 KPADRead(s32 channel, KPADStatus* output, u32 count) {
    std::lock_guard lock(mutex);
    if (channel < 0 || channel >= 4 || !output || count == 0) return 0;
    if (!initialized) throw std::logic_error("Controller read before initialization");
    auto& pad = get(channel);
    auto input = galaxy::readNativePad(channel);
    if (pad.disconnected) input.connected = false;
    const auto device = input.connected ? (input.classic ? WPAD_DEV_CLASSIC : WPAD_DEV_FREESTYLE) : WPAD_DEV_NOT_FOUND;
    if (input.connected != pad.connected) {
        pad.connected = input.connected;
        if (pad.connect) pad.connect(channel, input.connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER);
    }
    if (device != pad.device) {
        pad.device = device;
        if (input.connected && pad.extension) pad.extension(channel, device);
    }
    KPADStatus result{};
    result.dev_type = device;
    result.wpad_err = input.connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER;
    result.data_format = input.classic ? WPAD_FMT_CLASSIC_ACC_DPD : WPAD_FMT_FREESTYLE_ACC_DPD;
    result.hold = input.connected ? input.buttons : 0;
    result.trig = result.hold & ~pad.last.hold;
    result.release = pad.last.hold & ~result.hold & KPAD_BUTTON_MASK;
    const auto now = Clock::now();
    if (result.hold != (pad.last.hold & KPAD_BUTTON_MASK)) {
        pad.repeat = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(pad.delay));
    } else if (result.hold && now >= pad.repeat) {
        result.hold |= KPAD_BUTTON_RPT;
        pad.repeat = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(pad.pulse));
    }
    result.acc = {input.accX, input.accY, input.accZ};
    result.acc_value = std::sqrt(input.accX * input.accX + input.accY * input.accY + input.accZ * input.accZ);
    result.acc_speed = result.acc_value - pad.last.acc_value;
    result.acc_vertical = {input.accX, input.accZ};
    result.ex_status.fs.stick = {input.stickX, input.stickY};
    result.ex_status.fs.acc = result.acc;
    result.ex_status.fs.acc_value = result.acc_value;
    result.ex_status.fs.acc_speed = result.acc_speed;
    if (channel == 0 && !input.classic && galaxy::mouseControlsInitialized()) {
        // Match the Classic patch's stick steering for Nunchuk tilt activities.
        result.ex_status.fs.acc = {-input.stickX, -input.stickY, input.stickX};
        result.ex_status.fs.acc_value = std::sqrt(2 * input.stickX * input.stickX + input.stickY * input.stickY);
        result.ex_status.fs.acc_speed = result.ex_status.fs.acc_value - pad.last.ex_status.fs.acc_value;
    }
    if (input.classic) {
        auto& classic = result.ex_status.cl;
        const auto previous = pad.last.dev_type == WPAD_DEV_CLASSIC ? pad.last.ex_status.cl.hold : 0;
        classic.hold = input.connected ? input.classicButtons : 0;
        classic.trig = classic.hold & ~previous;
        classic.release = previous & ~classic.hold;
        classic.lstick = {input.stickX, input.stickY};
        classic.rstick = {input.rightStickX, input.rightStickY};
        classic.ltrigger = input.leftTrigger;
        classic.rtrigger = input.rightTrigger;
        bool mouseMoved = false;
        if (input.pointerValid) {
            mouseMoved = pad.mouseInitialized && (input.pointerX != pad.lastMouse.x || input.pointerY != pad.lastMouse.y);
            pad.lastMouse = {input.pointerX, input.pointerY}; pad.mouseInitialized = true;
        }
        if (mouseMoved || input.mousePointerActive) pad.pointer = pad.lastMouse;
        else {
            // The supplied patch accumulates the right stick by 1/20 each sample.
            pad.pointer.x = std::clamp(pad.pointer.x + input.rightStickX / 20, -1.0f, 1.0f);
            pad.pointer.y = std::clamp(pad.pointer.y - input.rightStickY / 20, -1.0f, 1.0f);
        }
        input.pointerX = pad.pointer.x; input.pointerY = pad.pointer.y;
        input.pointerValid = true;
    }
    result.pos = {input.pointerX, input.pointerY};
    result.vec = {result.pos.x - pad.last.pos.x, result.pos.y - pad.last.pos.y};
    result.speed = std::hypot(result.vec.x, result.vec.y);
    result.horizon = {1, 0};
    result.dist = 1;
    result.dpd_valid_fg = input.connected && input.pointerValid ? 2 : 0;
    const bool moved = pad.last.dev_type == device && pad.last.dpd_valid_fg && result.speed > 0.0001f;
    const bool aiming = input.classic && std::hypot(input.rightStickX, input.rightStickY) > 0.12f;
    if (!result.dpd_valid_fg || pad.last.dev_type != device) pad.pointerUntil = Clock::time_point::min();
    // Keep a stationary pointer visible briefly so its target can still be confirmed.
    if (result.dpd_valid_fg && (moved || aiming || input.mousePointerActive || (channel == 0 && galaxy::mouseMenuConfirm())))
        pad.pointerUntil = now + std::chrono::seconds(2);
    output[0] = result;
    pad.last = result;
    return 1;
}
void KPADSetBtnRepeat(s32 channel, f32 delay, f32 pulse) {
    parameter(channel, delay, pulse);
    std::lock_guard lock(mutex);
    get(channel).delay = delay; get(channel).pulse = pulse;
}
void KPADSetSensorHeight(s32 channel, f32 height) {
    std::lock_guard lock(mutex); get(channel);
    if (!std::isfinite(height)) throw std::invalid_argument("Invalid sensor bar height");
}
void KPADSetPosParam(s32 channel, f32 radius, f32 sensitivity) { parameter(channel, radius, sensitivity); }
void KPADSetAccParam(s32 channel, f32 radius, f32 sensitivity) { parameter(channel, radius, sensitivity); }
void KPADSetHoriParam(s32 channel, f32 radius, f32 sensitivity) { parameter(channel, radius, sensitivity); }
void KPADSetDistParam(s32 channel, f32 radius, f32 sensitivity) { parameter(channel, radius, sensitivity); }
u32 WPADGetWorkMemorySize() { return 0; } // SDL owns the native device buffers.
void WPADRegisterAllocator(WPADAlloc, WPADFree) {} // No SDK device buffers are allocated.
WPADConnectCallback WPADSetConnectCallback(s32 channel, WPADConnectCallback callback) {
    std::lock_guard lock(mutex); return std::exchange(get(channel).connect, callback);
}
WPADExtensionCallback WPADSetExtensionCallback(s32 channel, WPADExtensionCallback callback) {
    std::lock_guard lock(mutex); return std::exchange(get(channel).extension, callback);
}
s32 WPADProbe(s32 channel, u32* type) {
    std::lock_guard lock(mutex);
    if (channel < 0 || channel >= 4 || !type || !initialized) return WPAD_ERR_INVALID;
    const auto input = galaxy::readNativePad(channel);
    const bool connected = !get(channel).disconnected && input.connected;
    *type = connected ? (input.classic ? WPAD_DEV_CLASSIC : WPAD_DEV_FREESTYLE) : WPAD_DEV_NOT_FOUND;
    return connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER;
}
void WPADDisconnect(s32 channel) { std::lock_guard lock(mutex); get(channel).disconnected = true; galaxy::rumbleNativePad(channel, false); }
void WPADControlMotor(s32 channel, u32 command) {
    std::lock_guard lock(mutex); get(channel);
    if (command > WPAD_MOTOR_RUMBLE) throw std::invalid_argument("Invalid rumble command");
    galaxy::rumbleNativePad(channel, command == WPAD_MOTOR_RUMBLE);
}
s32 WPADGetInfoAsync(s32 channel, WPADInfo* info, WPADCallback callback) {
    std::lock_guard lock(mutex);
    if (channel < 0 || channel >= 4 || !info || !initialized) return WPAD_ERR_INVALID;
    const auto input = galaxy::readNativePad(channel);
    if (get(channel).disconnected || !input.connected) return WPAD_ERR_NO_CONTROLLER;
    *info = {};
    info->battery = input.battery;
    info->attach = true;
    if (callback) callback(channel, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}
u8 WPADGetSensorBarPosition() { return WPAD_SENSOR_BAR_POS_TOP; }
// Host device power management owns controller sleep.
void WPADSetAutoSleepTime(u8) {}
}
