#include <dolphin/ar.h>
#include "internal.hpp"
#include <cassert>
extern "C" void setupDspTestStorage() {
    static u32 allocations[16];
    aurora::g_config.mem2Size = 1024 * 1024;
    assert(ARInit(allocations, 16) == 0x4000);
}
extern "C" void writeDspTestStorage(const void* bytes, u32 size) {
    ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, reinterpret_cast<uintptr_t>(bytes), 0x4000, size);
}
extern "C" void resetDspTestStorage() { ARReset(); }
