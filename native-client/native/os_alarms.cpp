#include <revolution/os.h>
#include "os_services.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <new>
#include <stdexcept>

namespace {
struct Alarm {
    OSAlarm* key;
    Alarm* next;
    std::mutex mutex;
    std::condition_variable changed;
    HANDLE worker = nullptr;
    bool armed = false, executing = false, stopping = false;
    OSTime fire = 0, period = 0;
    OSAlarmHandler handler = nullptr;
    unsigned generation = 0;
};
SRWLOCK registry = SRWLOCK_INIT;
Alarm* alarms = nullptr;
thread_local Alarm* executingAlarm = nullptr;
DWORD WINAPI run(void* argument) {
    auto& alarm = *static_cast<Alarm*>(argument);
    std::unique_lock lock(alarm.mutex);
    while (true) {
        alarm.changed.wait(lock, [&] { return alarm.armed || alarm.stopping; });
        if (alarm.stopping) return 0;
        const OSTime remaining = alarm.fire - OSGetTime();
        if (remaining > 0) {
            // Recheck the game clock so pause and time-scale changes cannot fire an alarm early.
            alarm.changed.wait_for(lock, std::chrono::milliseconds(1));
            continue;
        }
        const unsigned generation = alarm.generation;
        const auto handler = alarm.handler;
        if (alarm.period) {
            const OSTime elapsed = OSGetTime() - alarm.fire;
            const OSTime intervals = elapsed / alarm.period;
            if (intervals >= (INT64_MAX - alarm.fire) / alarm.period) alarm.armed = false;
            else alarm.fire += (intervals + 1) * alarm.period;
            alarm.key->fire = alarm.fire;
        } else alarm.armed = false;
        alarm.executing = true;
        OSContext context{};
        lock.unlock();
        executingAlarm = &alarm;
        const BOOL interrupts = OSDisableInterrupts();
        try { handler(alarm.key, &context); }
        catch (...) { std::terminate(); }
        OSRestoreInterrupts(interrupts);
        executingAlarm = nullptr;
        lock.lock();
        alarm.executing = false;
        if (generation == alarm.generation && !alarm.armed) alarm.key->handler = nullptr;
        alarm.changed.notify_all();
    }
}

Alarm& state(OSAlarm* key) {
    if (!key) throw std::invalid_argument("null OS alarm");
    AcquireSRWLockExclusive(&registry);
    for (auto* alarm = alarms; alarm; alarm = alarm->next) if (alarm->key == key) { ReleaseSRWLockExclusive(&registry); return *alarm; }
    void* memory = std::malloc(sizeof(Alarm));
    if (!memory) { ReleaseSRWLockExclusive(&registry); throw std::bad_alloc(); }
    auto* alarm = new (memory) Alarm;
    alarm->key = key; alarm->next = alarms;
    alarm->worker = CreateThread(nullptr, 0, run, alarm, 0, nullptr);
    if (!alarm->worker) { alarm->~Alarm(); std::free(alarm); ReleaseSRWLockExclusive(&registry); throw std::runtime_error("could not create OS alarm worker"); }
    // shortcut: reuse process-owned alarm workers by address; add shared scheduling when alarm usage warrants it.
    alarms = alarm;
    ReleaseSRWLockExclusive(&registry);
    return *alarm;
}
void schedule(OSAlarm* key, OSTime fire, OSTime period, OSAlarmHandler handler) {
    if (!handler || fire < 0 || period < 0) throw std::invalid_argument("invalid OS alarm schedule");
    auto& alarm = state(key);
    std::lock_guard lock(alarm.mutex);
    alarm.fire = fire; alarm.period = period; alarm.handler = handler; alarm.armed = true; ++alarm.generation;
    key->handler = handler; key->fire = fire; key->period = period; key->start = fire;
    alarm.changed.notify_all();
}
}
extern "C" {
void OSCancelAlarm(OSAlarm* key) {
    auto& alarm = state(key);
    std::unique_lock lock(alarm.mutex);
    alarm.armed = false; ++alarm.generation; alarm.changed.notify_all();
    if (executingAlarm != &alarm && alarm.executing) {
        // Alarm handlers acquire the same native interrupt lock; release it while joining a callback.
        const BOOL previous = OSEnableInterrupts();
        alarm.changed.wait(lock, [&] { return !alarm.executing; });
        lock.unlock(); OSRestoreInterrupts(previous); lock.lock();
    }
    key->handler = nullptr;
}
void OSCreateAlarm(OSAlarm* key) { OSCancelAlarm(key); *key = {}; }
void OSSetAlarm(OSAlarm* key, OSTime delay, OSAlarmHandler handler) {
    const OSTime now = OSGetTime();
    if (delay < 0 || delay > INT64_MAX - now) throw std::invalid_argument("invalid OS alarm delay");
    schedule(key, now + delay, 0, handler);
}
void OSSetPeriodicAlarm(OSAlarm* key, OSTime start, OSTime period, OSAlarmHandler handler) {
    if (period <= 0) throw std::invalid_argument("invalid OS periodic alarm interval");
    schedule(key, start, period, handler);
}
void OSSetAlarmTag(OSAlarm* alarm, u32 tag) { alarm->tag = tag; }
void OSSetAlarmUserData(OSAlarm* alarm, void* data) { alarm->userData = data; }
void* OSGetAlarmUserData(const OSAlarm* alarm) { return alarm->userData; }
}

namespace galaxy {
void shutdownAlarms() {
    const BOOL interrupts = OSEnableInterrupts();
    AcquireSRWLockExclusive(&registry);
    for (auto* alarm = alarms; alarm; alarm = alarm->next) {
        std::lock_guard lock(alarm->mutex);
        alarm->stopping = true; alarm->armed = false; ++alarm->generation;
        alarm->changed.notify_all();
    }
    auto* snapshot = alarms;
    ReleaseSRWLockExclusive(&registry);
    for (auto* alarm = snapshot; alarm; alarm = alarm->next)
        if (WaitForSingleObject(alarm->worker, 5000) != WAIT_OBJECT_0) {
            OSRestoreInterrupts(interrupts);
            throw std::runtime_error("native alarm callback is still running");
        }
    AcquireSRWLockExclusive(&registry);
    while (alarms) {
        auto* alarm = alarms; alarms = alarm->next;
        CloseHandle(alarm->worker); alarm->~Alarm(); std::free(alarm);
    }
    ReleaseSRWLockExclusive(&registry);
    OSRestoreInterrupts(interrupts);
}
}
