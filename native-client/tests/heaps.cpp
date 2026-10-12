#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"
#include <revolution/mem/expHeap.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <thread>

namespace {
struct StartupAllocation {
    StartupAllocation() { ::operator delete(::operator new(64)); }
    ~StartupAllocation() { ::operator delete(::operator new(64)); }
} startupAllocation;
struct Disposer : JKRDisposer {
    int* count;
    explicit Disposer(int* value) : count(value) {}
    ~Disposer() override { ++*count; }
};
struct Throws {
    Throws() { throw 7; }
};
}

int main() {
    auto* beforeStartup = new int(42);
    auto* root = JKRExpHeap::createRoot(1, false);
    assert(root && root->check());
    assert(JKRExpHeap::createRoot(1, false) == root);
    assert(reinterpret_cast<uintptr_t>(root) > UINT32_MAX);
    const int initialFree = root->getTotalFreeSize();
    alignas(32) static std::array<u8, 16384> sdkMemory;
    sdkMemory.fill(0xBC);
    auto sdkHeap = MEMCreateExpHeapEx(sdkMemory.data(), sdkMemory.size(), 1 | 4);
    assert(sdkHeap && reinterpret_cast<uintptr_t>(sdkHeap) > UINT32_MAX);
    const auto sdkFree = MEMGetAllocatableSizeForExpHeapEx(sdkHeap, 32);
    auto* sdkBlock = static_cast<u8*>(MEMAllocFromExpHeapEx(sdkHeap, 135, 32));
    auto* sdkTail = static_cast<u8*>(MEMAllocFromExpHeapEx(sdkHeap, 77, -64));
    assert(sdkBlock && sdkTail && !(reinterpret_cast<uintptr_t>(sdkBlock) & 31) && !(reinterpret_cast<uintptr_t>(sdkTail) & 63));
    for (unsigned i = 0; i < 135; ++i) assert(sdkBlock[i] == 0);
    assert(!MEMAllocFromExpHeapEx(sdkHeap, UINT32_MAX, 32));
    assert(!MEMAllocFromExpHeapEx(sdkHeap, 32, 3));
    MEMFreeToExpHeap(sdkHeap, sdkBlock);
    MEMFreeToExpHeap(sdkHeap, sdkTail);
    assert(MEMGetAllocatableSizeForExpHeapEx(sdkHeap, 32) == sdkFree);
    assert(MEMDestroyExpHeap(sdkHeap) == sdkHeap);
    assert(!root->alloc(UINT32_MAX, 16));
    assert(!root->alloc(8, 3));
    assert(!root->alloc(8, INT32_MIN));
    assert(!JKRExpHeap::create(64, root, false));
    assert(!JKRExpHeap::create(nullptr, 1000, root, false));
    assert(root->resize(reinterpret_cast<void*>(1), 16) == -1);
    root->free(nullptr);
    root->free(root->getStart());
    root->free(root->getEnd());

    struct Allocation { void* memory = nullptr; u32 size = 0; u8 marker = 0; };
    std::array<Allocation, 128> allocations{};
    std::mt19937 random(0x47414c);
    for (int iteration = 0; iteration < 8000; ++iteration) {
        auto& slot = allocations[random() % allocations.size()];
        if (slot.memory) {
            assert(JKRHeap::checkMemoryFilled(slot.memory, slot.size, slot.marker));
            root->free(slot.memory);
            slot.memory = nullptr;
        } else {
            slot.size = 1 + random() % 4096;
            int alignment = 1 << (3 + random() % 8);
            if (random() & 1) alignment = -alignment;
            slot.memory = root->alloc(slot.size, alignment);
            assert(slot.memory && !(reinterpret_cast<uintptr_t>(slot.memory) & (std::abs(alignment) - 1)));
            slot.marker = random();
            JKRHeap::fillMemory(slot.memory, slot.size, slot.marker);
        }
        assert(root->check());
    }
    for (auto& slot : allocations) if (slot.memory) root->free(slot.memory);
    assert(root->check() && root->getTotalFreeSize() == initialFree);

    void* growing = root->alloc(40, 16);
    std::memset(growing, 0x5a, 40);
    assert(root->resize(growing, 1024) >= 1024);
    assert(JKRHeap::checkMemoryFilled(growing, 40, 0x5a));
    assert(root->resize(growing, 24) >= 24);
    assert(root->resize(growing, UINT32_MAX) == -1);
    root->free(growing);
    assert(root->check() && root->getTotalFreeSize() == initialFree);

    auto* child = JKRExpHeap::create(128 * 1024, root, false);
    assert(child && child->check() && child->getParent() == root);
    child->becomeCurrentHeap();
    auto* ordinary = new int(17);
    assert(JKRHeap::findFromRoot(ordinary) == child && *ordinary == 17);
    delete ordinary;
    auto freeBeforeThrow = child->getTotalFreeSize();
    try { new (child, 32) Throws; assert(false); } catch (int value) { assert(value == 7); }
    assert(child->getTotalFreeSize() == freeBeforeThrow);
    int disposed = 0;
    new (child, -16) Disposer(&disposed);
    new (child, 16) Disposer(&disposed);
    child->freeTail();
    assert(disposed == 1 && child->check());
    child->freeAll();
    assert(disposed == 2 && child->check() && child->isEmpty());

    void* kept = child->alloc(4096, 16);
    assert(child->adjustSize() > 0 && child->check() && root->check());
    child->free(kept);
    root->becomeCurrentHeap();
    child->destroy();
    assert(root->check() && root->getTotalFreeSize() == initialFree);

    char* secondary = nullptr;
    u32 secondarySize = 0;
    assert(JKRHeap::initArena2(&secondary, &secondarySize, 1));
    auto* outsidePrimary = JKRExpHeap::create(secondary, secondarySize, root, false);
    assert(outsidePrimary && outsidePrimary->check());
    void* secondaryObject = outsidePrimary->alloc(100, 32);
    assert(secondaryObject && JKRHeap::findFromRoot(secondaryObject) == outsidePrimary);
    assert(reinterpret_cast<uintptr_t>(secondaryObject) > UINT32_MAX);
    outsidePrimary->free(secondaryObject);
    outsidePrimary->destroy();
    assert(root->check() && root->getTotalFreeSize() == initialFree);

    auto* solid = JKRSolidHeap::create(128 * 1024, root, false);
    assert(solid && solid->check());
    assert(!solid->alloc(UINT32_MAX, 16) && !solid->alloc(8, 3));
    auto* head = new (solid, 64) Disposer(&disposed);
    auto* tail = new (solid, -256) Disposer(&disposed);
    assert(!(reinterpret_cast<uintptr_t>(head) & 63));
    assert(!(reinterpret_cast<uintptr_t>(tail) & 255));
    solid->freeTail();
    assert(disposed == 3 && solid->check());
    assert(solid->adjustSize() > 0 && solid->check() && root->check());
    solid->destroy();
    assert(disposed == 4 && root->check() && root->getTotalFreeSize() == initialFree);

    void* all = root->alloc(root->getMaxAllocatableSize(32), 32);
    assert(all && root->check());
    root->free(all);
    assert(root->check() && root->getTotalFreeSize() == initialFree);
    root->destroy();
    assert(!JKRHeap::sRootHeap && !JKRHeap::sCurrentHeap && !JKRHeap::sSystemHeap);
    assert(*beforeStartup == 42);
    delete beforeStartup;
    root = JKRExpHeap::createRoot(1, false);
    assert(root && root->check() && root->getTotalFreeSize() == initialFree);
    auto worker = [root] {
        for (int i = 0; i < 1000; ++i) {
            void* memory = root->alloc(40 + i % 300, 32);
            assert(memory);
            std::memset(memory, 0x71, 40 + i % 300);
            assert(JKRHeap::checkMemoryFilled(memory, 40 + i % 300, 0x71));
            root->free(memory);
        }
    };
    std::thread first(worker), second(worker);
    first.join();
    second.join();
    assert(root->check() && root->getTotalFreeSize() == initialFree);
    JKRHeap::setCurrentHeap(nullptr);
    std::atomic<bool> finished = false;
    std::thread graphicsWorker([&] {
        while (!finished.load()) {
            auto* host = new char[257];
            host[0] = 1;
            assert(JKRHeap::findFromRoot(host) == nullptr);
            delete[] host;
        }
    });
    for (int i = 0; i < 10000; ++i) {
        auto* scene = JKRExpHeap::create(65536, root, false);
        auto* childScene = JKRSolidHeap::create(8192, scene, false);
        assert(scene && childScene);
        assert(JKRHeap::findFromRoot(childScene->alloc(32, 16)) == childScene);
        scene->destroy();
    }
    finished = true;
    graphicsWorker.join();
    assert(root->check() && root->getTotalFreeSize() == initialFree);
    root->becomeCurrentHeap();
    outsidePrimary = JKRExpHeap::create(secondary, secondarySize, root, false);
    new (outsidePrimary, 32) Disposer(&disposed);
    root->destroy();
    assert(disposed == 5 && !JKRHeap::sRootHeap);
}
