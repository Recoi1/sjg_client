#include <revolution/OS.h>
#include "os_services.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace {
OSMessageQueue queue;
std::atomic<bool> entered{false};
void* consume(void*) {
    entered = true;
    OSMessage message;
    OSReceiveMessage(&queue, &message, OS_MESSAGE_BLOCK);
    return message;
}
void* waitForever(void*) {
    entered = true;
    while (true) { OSMessage message; OSReceiveMessage(&queue, &message, OS_MESSAGE_BLOCK); }
}
void* exitWithValue(void* value) { OSExitThread(value); return nullptr; }
void* heapWorker(void* expected) {
    auto* heap = static_cast<JKRHeap*>(expected);
    assert(JKRHeap::getCurrentHeap() == heap);
    for (unsigned i = 0; i < 100; ++i) {
        auto* memory = JKRHeap::alloc(64, 16, nullptr);
        assert(memory && JKRHeap::findFromRoot(memory) == heap);
        OSYieldThread();
        assert(JKRHeap::getCurrentHeap() == heap);
        JKRHeap::free(memory, heap);
    }
    return heap;
}
void awaitEntered() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!entered && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    assert(entered);
}
}
int main() {
    OSMutex mutex{};
    OSInitMutex(&mutex);
    OSLockMutex(&mutex); OSLockMutex(&mutex);
    std::atomic<bool> attempted{false};
    std::thread contender([&] { assert(!OSTryLockMutex(&mutex)); attempted = true; OSLockMutex(&mutex); OSUnlockMutex(&mutex); });
    while (!attempted) std::this_thread::yield();
    OSUnlockMutex(&mutex); OSUnlockMutex(&mutex); contender.join();
    assert(mutex.count == 0);
    bool invalid = false;
    try { OSUnlockMutex(&mutex); } catch (const std::logic_error&) { invalid = true; }
    assert(invalid);
    assert(OSDisableInterrupts()); assert(!OSDisableInterrupts()); assert(!OSRestoreInterrupts(false)); assert(!OSRestoreInterrupts(true));

    std::array<OSMessage, 2> messages{};
    OSInitMessageQueue(&queue, messages.data(), messages.size());
    auto* highPointer = reinterpret_cast<void*>(uintptr_t(0x1234567812345678));
    assert(OSSendMessage(&queue, highPointer, 0)); assert(OSJamMessage(&queue, nullptr, 0)); assert(!OSSendMessage(&queue, highPointer, 0));
    OSMessage result = highPointer;
    assert(OSReceiveMessage(&queue, &result, 0) && result == nullptr);
    assert(OSReceiveMessage(&queue, &result, 0) && result == highPointer);
    assert(!OSReceiveMessage(&queue, &result, 0));
    OSThread thread{};
    assert(OSCreateThread(&thread, consume, nullptr, nullptr, 0, 10, 0));
    assert(OSIsThreadSuspended(&thread)); assert(!entered);
    assert(OSResumeThread(&thread) == 1); awaitEntered();
    assert(OSSendMessage(&queue, highPointer, 1));
    assert(OSJoinThread(&thread, &result) && result == highPointer && OSIsThreadTerminated(&thread));
    entered = false;
    assert(OSCreateThread(&thread, waitForever, nullptr, nullptr, 0, 10, 1));
    OSResumeThread(&thread); awaitEntered(); OSCancelThread(&thread);
    assert(OSIsThreadTerminated(&thread)); assert(!OSJoinThread(&thread, nullptr));
    assert(OSCreateThread(&thread, exitWithValue, highPointer, nullptr, 0, 10, 0));
    OSResumeThread(&thread); assert(OSJoinThread(&thread, &result) && result == highPointer);
    assert(OSCreateThread(&thread, consume, nullptr, nullptr, 0, 10, 0));
    OSCancelThread(&thread); assert(OSIsThreadTerminated(&thread));
    assert(!OSCreateThread(&thread, consume, nullptr, nullptr, 0, 32, 0));
    OSMessageQueue uninitialized;
    invalid = false;
    try { OSReceiveMessage(&uninitialized, nullptr, 0); } catch (const std::logic_error&) { invalid = true; }
    assert(invalid);
    auto* root = JKRExpHeap::createRoot(1, false);
    auto* first = JKRExpHeap::create(65536, root, false);
    auto* second = JKRExpHeap::create(65536, root, false);
    assert(first && second);
    OSThread workers[2]{};
    first->becomeCurrentHeap();
    assert(OSCreateThread(&workers[0], heapWorker, first, nullptr, 0, 10, 0));
    second->becomeCurrentHeap();
    assert(OSCreateThread(&workers[1], heapWorker, second, nullptr, 0, 10, 0));
    root->becomeCurrentHeap();
    OSResumeThread(&workers[0]); OSResumeThread(&workers[1]);
    assert(OSJoinThread(&workers[0], &result) && result == first);
    assert(OSJoinThread(&workers[1], &result) && result == second);
    assert(JKRHeap::getCurrentHeap() == root && first->check() && second->check());
    entered = false;
    assert(OSCreateThread(&thread, waitForever, nullptr, nullptr, 0, 10, 0));
    OSResumeThread(&thread); awaitEntered();
    galaxy::shutdownOsServices();
    assert(thread.state == OS_THREAD_STATE_MORIBUND);
    first->destroy(); second->destroy(); root->destroy();
    return 0;
}
