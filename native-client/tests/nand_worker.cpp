#include "nand.hpp"
#include "os_services.hpp"
#include "Game/System/NANDManager.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>

namespace {
JKRHeap* stationedHeap = nullptr;
std::atomic<unsigned> callbacks{0};
std::atomic<bool> callbackEntered{false}, releaseCallback{false};
void blockWorker(NANDRequestInfo*) {
    callbackEntered = true;
    while (!releaseCallback) std::this_thread::yield();
}
void completed(NANDRequestInfo* request) {
    assert(request->isDone());
    ++callbacks;
}
void submit(NANDManager& manager, NANDRequestInfo& request, unsigned target) {
    request._54 = completed;
    assert(manager.addRequest(&request));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((!request.isDone() || callbacks < target) && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    assert(request.isDone() && callbacks >= target);
}
}
// Supply the game's real worker with a test-owned heap; the shipping target uses MemoryUtil.cpp.
namespace MR { JKRHeap* getStationedHeapNapa() { return stationedHeap; } }
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    assert(argc == 2);
    const auto parent = fs::canonical(argv[1]);
    const auto directory = parent / ("nand-worker-check-" + std::to_string(GetCurrentProcessId()));
    assert(fs::create_directory(directory));
    galaxy::setNandRoot(directory);
    assert(NANDInit() == 0);
    stationedHeap = JKRExpHeap::createRoot(1, false);
    assert(stationedHeap);
    {
        NANDManager manager;
        NANDRequestInfo request;
        request.setWriteSeq("worker-save", "hello", 5, NAND_PERM_RWALL, 0);
        submit(manager, request, 1);
        assert(request.mResult == 0 && request.mFsBlock == 5);
        std::array<char, 16> data{};
        u32 length = 0;
        request.setReadSeq("worker-save", data.data(), data.size(), &length);
        submit(manager, request, 2);
        assert(request.mResult == 0 && request.mFsBlock == 5 && length == 5);
        assert(std::string(data.data()) == "hello");
        data.fill('X');
        request.setReadSeq("worker-save", data.data(), 2, &length);
        submit(manager, request, 3);
        assert(request.mResult == NAND_RESULT_AUTHENTICATION && request.mFsBlock == 0 && data[0] == 'X');
        request.setDelete("worker-save");
        submit(manager, request, 4); assert(request.mResult == 0);
        request.setReadSeq("worker-save", data.data(), data.size(), &length);
        submit(manager, request, 5); assert(request.mResult == NAND_RESULT_NOEXISTS);
        request.setCheck(0, 0, &length); request._54 = blockWorker;
        assert(manager.addRequest(&request));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!callbackEntered && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        assert(callbackEntered);
        std::array<NANDRequestInfo, 257> queued;
        std::array<u32, 257> answers{};
        for (unsigned i = 0; i < queued.size(); ++i) {
            queued[i].setCheck(0, 0, &answers[i]);
            const bool accepted = manager.addRequest(&queued[i]);
            if (i < 256) assert(accepted && !queued[i].isDone());
            else assert(!accepted && queued[i].isDone() && queued[i].mResult == NAND_RESULT_BUSY);
        }
        releaseCallback = true;
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (unsigned i = 0; i < 256; ++i) {
            while (!queued[i].isDone() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            assert(queued[i].isDone() && queued[i].mResult == 0 && answers[i] == 0);
        }
        galaxy::shutdownOsServices(); // Join the actual game save worker while its SDK storage is still alive.
    }
    galaxy::shutdownNand();
    stationedHeap->destroy(); stationedHeap = nullptr;
    assert(fs::canonical(directory).parent_path() == parent && directory.filename().string().starts_with("nand-worker-check-"));
    fs::remove_all(directory);
}
