#include <revolution/nand.h>
#include "JSystem/JKernel/JKRHeap.hpp"
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <set>

namespace {
struct Worker {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::function<void()>> jobs;
    std::set<NANDCommandBlock*> pending;
    std::thread thread;
    bool stopping = false;
    void stop() {
        { std::lock_guard lock(mutex); stopping = true; }
        changed.notify_all();
        if (thread.joinable()) thread.join();
    }
    ~Worker() { stop(); }
    s32 submit(std::function<s32()> operation, NANDCallback callback, NANDCommandBlock* block) {
        struct RestoreHeap { JKRHeap* previous = JKRHeap::sCurrentHeap; ~RestoreHeap() { JKRHeap::sCurrentHeap = previous; } } restore;
        // The save worker and queued operations must survive scene heap destruction.
        JKRHeap::setCurrentHeap(nullptr);
        if (!block || !callback) return NAND_RESULT_INVALID;
        if (!nandIsInitialized()) return NAND_RESULT_FATAL_ERROR;
        std::lock_guard lock(mutex);
        if ((stopping && thread.get_id() != std::this_thread::get_id()) || jobs.size() >= 128 || pending.contains(block)) return NAND_RESULT_BUSY;
        if (!thread.joinable()) {
            stopping = false;
            thread = std::thread([this] {
                std::unique_lock lock(mutex);
                for (;;) {
                    changed.wait(lock, [&] { return stopping || !jobs.empty(); });
                    if (jobs.empty() && stopping) return;
                    auto job = std::move(jobs.front()); jobs.pop_front();
                    lock.unlock(); job(); lock.lock();
                }
            });
        }
        pending.insert(block);
        jobs.emplace_back([this, operation, callback, block] {
            s32 result;
            try { result = operation(); } catch (...) { result = NAND_RESULT_FATAL_ERROR; }
            { std::lock_guard lock(mutex); pending.erase(block); }
            // Wii completion callbacks run atomically with respect to interrupt-protected game state.
            const BOOL interrupts = OSDisableInterrupts();
            try { callback(result, block); }
            catch (...) { OSRestoreInterrupts(interrupts); throw; }
            OSRestoreInterrupts(interrupts);
        });
        changed.notify_one();
        return NAND_RESULT_OK;
    }
} worker;
template<class F> s32 named(const char* name, F operation, NANDCallback callback, NANDCommandBlock* block) {
    if (!name) return NAND_RESULT_INVALID;
    size_t length = 0;
    while (length < NAND_MAX_PATH && name[length]) ++length;
    if (!length || length == NAND_MAX_PATH) return NAND_RESULT_INVALID;
    return worker.submit([name = std::string(name, length), operation] { return operation(name.c_str()); }, callback, block);
}
}
namespace galaxy {
void shutdownNandWorker() { worker.stop(); }
void resetNandWorker() { std::lock_guard lock(worker.mutex); worker.stopping = false; }
}
extern "C" {
s32 NANDPrivateOpen(const char* name, NANDFileInfo* info, u8 access) { return NANDOpen(name, info, access); }
s32 NANDPrivateCreate(const char* name, u8 permission, u8 attr) { return NANDCreate(name, permission, attr); }
s32 NANDPrivateDelete(const char* name) { return NANDDelete(name); }
s32 NANDOpenAsync(const char* name, NANDFileInfo* info, u8 access, NANDCallback callback, NANDCommandBlock* block) {
    return named(name, [=](const char* path) { return NANDOpen(path, info, access); }, callback, block);
}
s32 NANDPrivateOpenAsync(const char* name, NANDFileInfo* info, u8 access, NANDCallback callback, NANDCommandBlock* block) {
    return NANDOpenAsync(name, info, access, callback, block);
}
s32 NANDPrivateSafeOpenAsync(const char* name, NANDFileInfo* info, u8 access, void*, u32, NANDCallback callback, NANDCommandBlock* block) {
    // Native writable handles already use an atomic temporary file, including ordinary NANDOpen.
    return NANDOpenAsync(name, info, access, callback, block);
}
s32 NANDCloseAsync(NANDFileInfo* info, NANDCallback callback, NANDCommandBlock* block) {
    return worker.submit([=] { return NANDClose(info); }, callback, block);
}
s32 NANDSafeCloseAsync(NANDFileInfo* info, NANDCallback callback, NANDCommandBlock* block) { return NANDCloseAsync(info, callback, block); }
s32 NANDReadAsync(NANDFileInfo* info, void* output, u32 size, NANDCallback callback, NANDCommandBlock* block) {
    return worker.submit([=] { return NANDRead(info, output, size); }, callback, block);
}
s32 NANDWriteAsync(NANDFileInfo* info, const void* input, u32 size, NANDCallback callback, NANDCommandBlock* block) {
    return worker.submit([=] { return NANDWrite(info, input, size); }, callback, block);
}
s32 NANDSeekAsync(NANDFileInfo* info, s32 offset, s32 mode, NANDCallback callback, NANDCommandBlock* block) {
    return worker.submit([=] { return NANDSeek(info, offset, mode); }, callback, block);
}
s32 NANDGetLengthAsync(NANDFileInfo* info, u32* output, NANDCallback callback, NANDCommandBlock* block) {
    return worker.submit([=] { return NANDGetLength(info, output); }, callback, block);
}
s32 NANDPrivateCreateAsync(const char* name, u8 permission, u8 attr, NANDCallback callback, NANDCommandBlock* block) {
    return named(name, [=](const char* path) { return NANDCreate(path, permission, attr); }, callback, block);
}
s32 NANDPrivateDeleteAsync(const char* name, NANDCallback callback, NANDCommandBlock* block) {
    return named(name, [](const char* path) { return NANDDelete(path); }, callback, block);
}
s32 NANDPrivateCreateDirAsync(const char* name, u8 permission, u8 attr, NANDCallback callback, NANDCommandBlock* block) {
    return named(name, [=](const char* path) { return NANDPrivateCreateDir(path, permission, attr); }, callback, block);
}
}
