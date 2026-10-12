#include <revolution/kpad.h>
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include "mouse_controls.hpp"
#include <limits>
extern "C" void startInputTest(int, char**);
extern "C" void setInputTest(u32, u16, s8, s8, bool);
extern "C" void stopInputTest();
extern "C" void checkKeyboardTest();
extern "C" void checkMouseTest();
namespace {
unsigned connects = 0, disconnects = 0, extensions = 0;
void connect(s32 channel, s32 reason) {
    assert(channel == 1);
    if (reason == WPAD_ERR_NONE) ++connects; else { assert(reason == WPAD_ERR_NO_CONTROLLER); ++disconnects; }
}
void extension(s32 channel, s32 type) { assert(channel == 1 && type == WPAD_DEV_FREESTYLE); ++extensions; }
}
int main(int argc, char** argv) {
    startInputTest(argc, argv);
    KPADInit();
    checkKeyboardTest();
    checkMouseTest();
    {
        galaxy::MouseOrbit orbit;
        TVec3f position(0, 0, 1000), target(0, 0, 0), up(0, 1, 0), player(0, 0, 0);
        constexpr float sensitivity = 0.0015339808f, initialPitch = 0.1438107f;
        orbit.apply(position, target, up, up, player, {}); assert(!orbit.active && position.z == 1000);
        orbit.apply(position, target, up, up, player, {1024, 0});
        assert(orbit.active && std::abs(position.x + 750 * std::cos(initialPitch)) < 0.01f && std::abs(position.z) < 0.1f);
        assert(target.distance(TVec3f(0, 125, 0)) < 0.01f);
        const TVec3f offset = position - target;
        position.set(0, 0, 1000); target.set(100, 50, 30);
        player.set(100, 50, 30);
        orbit.apply(position, target, up, up, player, {});
        assert(target.distance(player + up * 125) < 0.01f && (position - target).distance(offset) < 0.01f);
        orbit.apply(position, target, up, TVec3f(1, 0, 0), player, {0, -10000});
        assert(std::abs(up.x - 1) < 0.0001f && std::abs(up.y) < 0.0001f);
        TVec3f direction = position - target; direction.normalize();
        assert(std::abs(direction.dot(up) - std::sin(1.1780972f)) < 0.0001f);
        orbit.apply(position, target, up, TVec3f(-1, 0, 0), player, {0, 10000});
        direction = position - target; direction.normalize();
        assert(std::isfinite(position.x) && std::abs((position - target).length() - 750) < 0.01f);
        assert(std::abs(direction.dot(up) + std::sin(1.1780972f)) < 0.0001f);
        const TVec3f previous = position;
        orbit.apply(position, target, up, up, player, {std::numeric_limits<float>::infinity(), 0});
        assert(position.distance(previous) == 0);
        orbit.apply(position, target, up, TVec3f(0, 0, 0), player, {100, 100});
        assert(position.distance(previous) == 0);
        orbit.apply(position, target, up, up, player, {}, 1, -1);
        assert(position.distance(previous) == 0);
        for (unsigned frames : {30u, 60u, 120u}) {
            galaxy::MouseOrbit continuous;
            TVec3f eye(0, 0, 1000), origin(0, 0, 0), worldUp(0, 1, 0);
            for (unsigned i = 0; i < frames; ++i) {
                eye.set(0, 0, 1000);
                continuous.apply(eye, origin, worldUp, worldUp, TVec3f(0, 0, 0), {300.0f / frames, 100.0f / frames});
            }
            const float pitch = initialPitch - 100 * sensitivity;
            assert(std::abs(eye.x + std::sin(300 * sensitivity) * std::cos(pitch) * 750) < 0.1f);
            assert(std::abs(eye.y - 125 - std::sin(pitch) * 750) < 0.1f);
            for (int zoom : {1, 1, 1, -1, -1, -1}) {
                const int index = std::clamp(continuous.distanceIndex + zoom, 0, 2);
                constexpr float distances[] = {750, 1250, 2000};
                for (unsigned i = 0; i < frames; ++i)
                    continuous.apply(eye, origin, worldUp, worldUp, TVec3f(0, 0, 0), {}, i == 0 ? float(zoom) : 0, 1.0f / frames);
                assert(continuous.distanceIndex == index && continuous.distance == distances[index]);
                assert(std::abs((eye - origin).length() - distances[index]) < 0.1f);
            }
        }
    }
    KPADStatus status[2]{};
    status[1].hold = 0xabcdef;
    assert(WPADSetConnectCallback(1, connect) == nullptr);
    assert(WPADSetExtensionCallback(1, extension) == nullptr);
    setInputTest(1, 0x0110, -127, 127, true);
    assert(KPADRead(1, status, 2) == 1 && status[1].hold == 0xabcdef);
    assert(connects == 1 && extensions == 1 && !disconnects);
    assert(status[0].hold == (WPAD_BUTTON_A | WPAD_BUTTON_Z) && status[0].trig == status[0].hold);
    assert(status[0].wpad_err == WPAD_ERR_NONE && status[0].dev_type == WPAD_DEV_FREESTYLE);
    assert(status[0].ex_status.fs.stick.x == -1 && status[0].ex_status.fs.stick.y == 1);
    assert(status[0].acc.z == 1 && status[0].dpd_valid_fg == 2);
    KPADSetBtnRepeat(1, 0.001f, 0.01f);
    setInputTest(1, 0, 0, 0, true); KPADRead(1, status, 1);
    assert(status[0].release == (WPAD_BUTTON_A | WPAD_BUTTON_Z));
    setInputTest(1, 0x0400, 0, 0, true); KPADRead(1, status, 1);
    assert(status[0].acc.z == 4);
    setInputTest(1, 0x0100, 0, 0, true); KPADRead(1, status, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(3)); KPADRead(1, status, 1);
    assert(status[0].hold & KPAD_BUTTON_RPT);
    assert(!status[0].trig);
    KPADRead(1, status, 1); assert(!status[0].trig);
    setInputTest(1, 0, 0, 0, false); KPADRead(1, status, 1);
    assert(disconnects == 1 && status[0].wpad_err == WPAD_ERR_NO_CONTROLLER && !status[0].hold);
    setInputTest(1, 0, 0, 0, true); KPADRead(1, status, 1);
    assert(connects == 2 && extensions == 2);
    u32 type;
    assert(WPADProbe(1, &type) == WPAD_ERR_NONE && type == WPAD_DEV_FREESTYLE);
    assert(WPADProbe(4, &type) == WPAD_ERR_INVALID);
    assert(!KPADRead(-1, status, 1) && !KPADRead(0, nullptr, 1) && !KPADRead(0, status, 0));
    bool rejected = false;
    try { KPADSetBtnRepeat(0, -1, 1); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    WPADDisconnect(1); KPADRead(1, status, 1);
    assert(disconnects == 2 && status[0].wpad_err == WPAD_ERR_NO_CONTROLLER);
    assert(WPADSetConnectCallback(1, nullptr) == connect);
    assert(WPADSetExtensionCallback(1, nullptr) == extension);
    stopInputTest();
    return 0;
}
