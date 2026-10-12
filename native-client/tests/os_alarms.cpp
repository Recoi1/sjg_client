#include <revolution/os.h>
#include "os_services.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace {
std::atomic<unsigned> count{0};
void callback(OSAlarm* alarm, OSContext*) {
    assert(OSGetAlarmUserData(alarm) == &count);
    ++count;
}
void cancelSelf(OSAlarm* alarm, OSContext* context) { callback(alarm, context); OSCancelAlarm(alarm); }
void awaitCount(unsigned target) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (count < target && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    assert(count >= target);
}
}
int main() {
    OSAlarm alarm{};
    OSCreateAlarm(&alarm); OSSetAlarmUserData(&alarm, &count);
    OSSetAlarm(&alarm, OS_MSEC_TO_TICKS(20), callback);
    std::this_thread::sleep_for(std::chrono::milliseconds(3)); assert(count == 0);
    awaitCount(1); OSCancelAlarm(&alarm); assert(count == 1);
    OSSetPeriodicAlarm(&alarm, OSGetTime() + OS_MSEC_TO_TICKS(5), OS_MSEC_TO_TICKS(5), callback);
    awaitCount(4); OSCancelAlarm(&alarm);
    const unsigned stopped = count;
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); assert(count == stopped);
    OSSetAlarm(&alarm, OS_MSEC_TO_TICKS(20), callback); OSCancelAlarm(&alarm);
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); assert(count == stopped);
    OSSetPeriodicAlarm(&alarm, OSGetTime(), OS_MSEC_TO_TICKS(5), cancelSelf);
    awaitCount(stopped + 1); OSCancelAlarm(&alarm);
    assert(count == stopped + 1);
    const BOOL interrupts = OSDisableInterrupts();
    OSSetAlarm(&alarm, 0, callback);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    OSCancelAlarm(&alarm); // Must release the interrupt lock while joining a pending callback.
    OSRestoreInterrupts(interrupts);
    bool invalid = false;
    try { OSSetPeriodicAlarm(&alarm, -1, 1, callback); } catch (const std::invalid_argument&) { invalid = true; }
    assert(invalid);
    OSCreateAlarm(&alarm); assert(!alarm.handler && !alarm.userData);
    OSSetAlarmUserData(&alarm, &count);
    OSSetPeriodicAlarm(&alarm, OSGetTime() + OS_MSEC_TO_TICKS(30), OS_MSEC_TO_TICKS(5), callback);
    const unsigned beforeShutdown = count;
    galaxy::shutdownAlarms();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    assert(count == beforeShutdown);
    OSCreateAlarm(&alarm); // Registry and workers can restart after a complete shutdown.
    galaxy::shutdownAlarms(); galaxy::shutdownOsServices();
}
