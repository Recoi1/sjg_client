#include <revolution/sc.h>
#include <revolution/mem/allocator.h>
#include <cassert>
#include <cstdlib>
#include <stdexcept>

namespace {
unsigned allocations = 0, frees = 0;
void* allocate(MEMAllocator* allocator, u32 size) {
    assert(allocator->heapParam1 == 16 && size == 64);
    ++allocations; return std::malloc(size);
}
void release(MEMAllocator*, void* memory) { ++frees; std::free(memory); }
}
int main() {
    assert(SCGetLanguage() == SC_LANG_ENGLISH && SCGetSoundMode() == SC_SOUND_MODE_STEREO);
    assert(SCGetAspectRatio() == SC_ASPECT_RATIO_16x9 && SCGetProgressiveMode() == SC_PROGRESSIVE_MODE_ON);
    assert(SCGetEuRgb60Mode() == SC_EURGB60_MODE_OFF);
    const MEMAllocatorFunc functions{allocate, release};
    MEMAllocator allocator{&functions, nullptr, 16, 0};
    void* memory = MEMAllocFromAllocator(&allocator, 64);
    assert(memory && allocations == 1);
    MEMFreeToAllocator(&allocator, memory); assert(frees == 1);
    MEMFreeToAllocator(nullptr, nullptr);
    assert(!MEMAllocFromAllocator(nullptr, 64));
    allocator.pFunc = nullptr;
    assert(!MEMAllocFromAllocator(&allocator, 64));
    bool failed = false;
    try { MEMFreeToAllocator(&allocator, reinterpret_cast<void*>(uintptr_t(1))); } catch (const std::invalid_argument&) { failed = true; }
    assert(failed && frees == 1);
}
