#include <revolution/OS.h>
#include "os_services.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <stdexcept>

namespace {
struct WorkerExit { void* value; };
enum Kind { Mutex, Queue, Thread, SleepQueue };
struct State {
    const void* key;
    Kind kind;
    State* next;
    std::recursive_mutex recursive;
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> cancel{false};
    std::atomic<int> suspended{0};
    std::atomic<bool> finished{true};
    HANDLE handle = nullptr;
    OSThread* thread = nullptr;
    void* (*entry)(void*) = nullptr;
    void* argument = nullptr;
    unsigned generation = 0;
    bool initialized = false;
    JKRHeap* heap = nullptr;
};
// SDK objects have no destroy API; reuse process-owned state by address until native shutdown.
State* states = nullptr;
std::atomic<bool> stopping{false};
SRWLOCK registryLock = SRWLOCK_INIT;
State& state(const void* key, Kind kind) {
    if (!key) throw std::invalid_argument("null OS service object");
    AcquireSRWLockExclusive(&registryLock);
    for (State* item = states; item; item = item->next) {
        if (item->key == key && item->kind == kind) {
            ReleaseSRWLockExclusive(&registryLock);
            return *item;
        }
    }
    void* memory = std::malloc(sizeof(State));
    if (!memory) { ReleaseSRWLockExclusive(&registryLock); throw std::bad_alloc(); }
    auto* item = new (memory) State;
    item->key = key; item->kind = kind; item->next = states; states = item;
    ReleaseSRWLockExclusive(&registryLock);
    return *item;
}
thread_local State* current = nullptr;
thread_local OSThread externalThread{};
void checkpoint() {
    if (!current) return;
    if (current->cancel) throw WorkerExit{nullptr};
    if (current->suspended > 0) {
        std::unique_lock lock(current->mutex);
        current->changed.wait(lock, [] { return current->cancel || current->suspended <= 0; });
        if (current->cancel) throw WorkerExit{nullptr};
    }
}
DWORD WINAPI runThread(void* argument) {
    auto& item = *static_cast<State*>(argument);
    current = &item;
    JKRHeap::setCurrentHeap(item.heap);
    void* value = nullptr;
    try {
        checkpoint();
        item.thread->state = OS_THREAD_STATE_RUNNING;
        value = item.entry(item.argument);
    } catch (const WorkerExit& exit) { value = exit.value; }
    catch (...) { std::terminate(); }
    OSEnableInterrupts();
    item.thread->value = value;
    item.thread->state = OS_THREAD_STATE_MORIBUND;
    item.finished = true;
    item.changed.notify_all();
    current = nullptr;
    return 0;
}
std::recursive_mutex interruptMutex;
thread_local bool interruptsEnabled = true;
thread_local int schedulerDisabled = 0;
BOOL send(OSMessageQueue* queue, OSMessage message, s32 flags, bool jam) {
    auto& item = state(queue, Queue);
    checkpoint();
    std::unique_lock lock(item.mutex);
    if (!item.initialized) throw std::logic_error("OS queue is not initialized");
    while (queue->usedCount == queue->msgCount) {
        if (!(flags & OS_MESSAGE_BLOCK)) return false;
        item.changed.wait_for(lock, std::chrono::milliseconds(20));
        lock.unlock(); checkpoint(); lock.lock();
    }
    if (jam) queue->firstIndex = (queue->firstIndex + queue->msgCount - 1) % queue->msgCount;
    const int index = jam ? queue->firstIndex : (queue->firstIndex + queue->usedCount) % queue->msgCount;
    queue->msgArray[index] = message;
    ++queue->usedCount;
    item.changed.notify_all();
    return true;
}
}

extern "C" {
void OSInitMutex(OSMutex* mutex) {
    state(mutex, Mutex);
    std::memset(mutex, 0, sizeof(*mutex));
}
void OSLockMutex(OSMutex* mutex) {
    checkpoint();
    auto& item = state(mutex, Mutex);
    while (!item.recursive.try_lock()) { Sleep(1); checkpoint(); }
    mutex->thread = OSGetCurrentThread(); ++mutex->count;
}
BOOL OSTryLockMutex(OSMutex* mutex) {
    checkpoint();
    if (!state(mutex, Mutex).recursive.try_lock()) return false;
    mutex->thread = OSGetCurrentThread(); ++mutex->count;
    return true;
}
void OSUnlockMutex(OSMutex* mutex) {
    if (mutex->thread != OSGetCurrentThread() || mutex->count <= 0) throw std::logic_error("OS mutex unlocked by non-owner");
    if (--mutex->count == 0) mutex->thread = nullptr;
    state(mutex, Mutex).recursive.unlock();
}
void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* messages, s32 count) {
    if (!messages || count <= 0) throw std::invalid_argument("invalid OS message queue storage");
    auto& item = state(queue, Queue);
    std::lock_guard lock(item.mutex);
    std::memset(queue, 0, sizeof(*queue));
    queue->msgArray = messages; queue->msgCount = count; item.initialized = true;
}
BOOL OSSendMessage(OSMessageQueue* queue, OSMessage message, s32 flags) { return send(queue, message, flags, false); }
BOOL OSJamMessage(OSMessageQueue* queue, OSMessage message, s32 flags) { return send(queue, message, flags, true); }
BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* message, s32 flags) {
    auto& item = state(queue, Queue);
    checkpoint();
    std::unique_lock lock(item.mutex);
    if (!item.initialized) throw std::logic_error("OS queue is not initialized");
    while (queue->usedCount == 0) {
        if (!(flags & OS_MESSAGE_BLOCK)) return false;
        item.changed.wait_for(lock, std::chrono::milliseconds(20));
        lock.unlock(); checkpoint(); lock.lock();
    }
    if (message) *message = queue->msgArray[queue->firstIndex];
    queue->firstIndex = (queue->firstIndex + 1) % queue->msgCount;
    --queue->usedCount;
    item.changed.notify_all();
    return true;
}
OSThread* OSGetCurrentThread() {
    if (current) return current->thread;
    externalThread.state = OS_THREAD_STATE_RUNNING;
    return &externalThread;
}
BOOL OSCreateThread(OSThread* thread, void* (*entry)(void*), void* argument, void* stack, u32 stackSize, OSPriority priority, u16 attr) {
    if (stopping || !thread || !entry || priority < 0 || priority > 31) return false;
    auto& item = state(thread, Thread);
    std::lock_guard lock(item.mutex);
    if (!item.finished) return false;
    if (item.handle) { WaitForSingleObject(item.handle, INFINITE); CloseHandle(item.handle); }
    std::memset(thread, 0, sizeof(*thread));
    thread->state = OS_THREAD_STATE_READY; thread->attr = attr; thread->suspend = 1;
    thread->priority = thread->base = priority;
    thread->stackBase = static_cast<u8*>(stack);
    thread->stackEnd = reinterpret_cast<u32*>(stack ? static_cast<u8*>(stack) - stackSize : nullptr);
    item.thread = thread; item.entry = entry; item.argument = argument;
    item.heap = JKRHeap::getCurrentHeap();
    item.cancel = false; item.suspended = 1; item.finished = false;
    // Windows owns the actual stack; the SDK stack allocation remains available for diagnostics.
    item.handle = CreateThread(nullptr, 0, runThread, &item, 0, nullptr);
    if (!item.handle) { item.finished = true; thread->state = 0; return false; }
    return true;
}
s32 OSResumeThread(OSThread* thread) {
    auto& item = state(thread, Thread);
    std::lock_guard lock(item.mutex);
    const int previous = item.suspended;
    if (previous > 0) item.suspended = previous - 1;
    thread->suspend = item.suspended;
    item.changed.notify_all();
    return previous;
}
s32 OSSuspendThread(OSThread* thread) {
    auto& item = state(thread, Thread);
    const int previous = item.suspended.fetch_add(1);
    thread->suspend = previous + 1;
    // shortcut: suspension and cancellation take effect at OS calls; do not suspend a CPU-only worker.
    if (&item == current) checkpoint();
    return previous;
}
void OSCancelThread(OSThread* thread) {
    auto& item = state(thread, Thread);
    item.cancel = true; item.changed.notify_all();
    if (&item == current) throw WorkerExit{nullptr};
    if (item.handle) {
        if (WaitForSingleObject(item.handle, 5000) != WAIT_OBJECT_0)
            throw std::runtime_error("OS worker did not reach a cancellation point; its memory cannot be freed");
    }
}
void OSExitThread(void* value) {
    if (!current) throw std::logic_error("OSExitThread called outside OS worker");
    throw WorkerExit{value};
}
BOOL OSJoinThread(OSThread* thread, void** value) {
    auto& item = state(thread, Thread);
    if (&item == current || (thread->attr & 1) || !item.handle) return false;
    WaitForSingleObject(item.handle, INFINITE);
    if (value) *value = thread->value;
    return true;
}
void OSDetachThread(OSThread* thread) { thread->attr |= 1; }
BOOL OSIsThreadSuspended(OSThread* thread) { return state(thread, Thread).suspended > 0; }
BOOL OSIsThreadTerminated(OSThread* thread) { return state(thread, Thread).finished; }
void OSYieldThread() { checkpoint(); SwitchToThread(); }
BOOL OSSetThreadPriority(OSThread* thread, OSPriority priority) {
    if (!thread || priority < 0 || priority > 31) return false;
    thread->priority = thread->base = priority;
    // Keep SDK priorities as metadata; Windows schedules these background workers without real-time priority.
    return true;
}
OSPriority OSGetThreadPriority(OSThread* thread) { return thread->priority; }
void OSInitThreadQueue(OSThreadQueue* queue) { state(queue, SleepQueue); queue->head = queue->tail = nullptr; }
void OSSleepThread(OSThreadQueue* queue) {
    auto& item = state(queue, SleepQueue);
    std::unique_lock lock(item.mutex);
    const unsigned generation = item.generation;
    const bool restoreInterrupts = !interruptsEnabled;
    if (restoreInterrupts) OSEnableInterrupts();
    while (generation == item.generation) {
        item.changed.wait_for(lock, std::chrono::milliseconds(20));
        lock.unlock(); checkpoint(); lock.lock();
    }
    lock.unlock();
    if (restoreInterrupts) OSDisableInterrupts();
}
void OSWakeupThread(OSThreadQueue* queue) {
    auto& item = state(queue, SleepQueue);
    std::lock_guard lock(item.mutex);
    ++item.generation; item.changed.notify_all();
}
BOOL OSDisableInterrupts() {
    const bool previous = interruptsEnabled;
    if (previous) { interruptMutex.lock(); interruptsEnabled = false; }
    return previous;
}
BOOL OSEnableInterrupts() {
    const bool previous = interruptsEnabled;
    if (!previous) { interruptsEnabled = true; interruptMutex.unlock(); }
    return previous;
}
BOOL OSRestoreInterrupts(BOOL enabled) { return enabled ? OSEnableInterrupts() : OSDisableInterrupts(); }
// shortcut: Windows scheduling continues; callers must use native mutexes for shared data until cooperative scheduling is extended.
s32 OSDisableScheduler() { return schedulerDisabled++; }
s32 OSEnableScheduler() { const int previous = schedulerDisabled; if (previous) --schedulerDisabled; return previous; }
void __OSThreadInit() { OSGetCurrentThread(); }
void OSSleepTicks(OSTime ticks) {
    extern vu32 OS_BUS_CLOCK_SPEED;
    const auto clock = OS_BUS_CLOCK_SPEED / 4;
    if (!clock) throw std::logic_error("OS clock not initialized");
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(double(ticks) / clock);
    do { checkpoint(); Sleep(1); } while (std::chrono::steady_clock::now() < end);
}
u32 __OSBusClock = 162000000;
vu32 OS_BUS_CLOCK_SPEED = 162000000;
}

namespace galaxy {
void shutdownOsServices() {
    if (current) throw std::logic_error("OS services must be shut down by the game thread");
    stopping = true;
    const BOOL interrupts = OSEnableInterrupts();
    AcquireSRWLockExclusive(&registryLock);
    for (State* item = states; item; item = item->next) if (item->kind == Thread) {
        item->cancel = true; item->changed.notify_all();
    }
    State* snapshot = states;
    ReleaseSRWLockExclusive(&registryLock);
    for (State* item = snapshot; item; item = item->next) if (item->kind == Thread && item->handle) {
        if (WaitForSingleObject(item->handle, 5000) != WAIT_OBJECT_0) {
            OSRestoreInterrupts(interrupts);
            throw std::runtime_error("native worker is still running; OS services cannot be released");
        }
        CloseHandle(item->handle); item->handle = nullptr;
    }
    AcquireSRWLockExclusive(&registryLock);
    while (states) {
        State* item = states; states = item->next;
        item->~State(); std::free(item);
    }
    ReleaseSRWLockExclusive(&registryLock);
    stopping = false;
    OSRestoreInterrupts(interrupts);
}
}
