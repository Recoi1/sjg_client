#include <dolphin/arq.h>
#include "internal.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>

namespace {
unsigned completed = 0;
ARQRequest request{};
void callback(uintptr_t value) { assert(value == reinterpret_cast<uintptr_t>(&request)); ++completed; }
template<class F> void reject(F operation) {
    const auto before = completed;
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    assert(failed && completed == before);
}
}
int main() {
    aurora::g_config.mem2Size = 1024 * 1024;
    std::array<u32, 2> blocks{};
    assert(!ARCheckInit() && ARInit(blocks.data(), 2) == 0x4000 && ARCheckInit());
    assert(ARGetSize() == 1024 * 1024 && ARGetBaseAddress() == 0x4000);
    assert(ARAlloc(32) == 0x4000 && ARAlloc(64) == 0x4020 && ARAlloc(32) == 0);
    u32 length;
    assert(ARFree(&length) == 0x4020 && length == 64);
    assert(ARFree(&length) == 0x4000 && length == 32);
    assert(ARFree(&length) == 0 && length == 0);
    assert(!ARAlloc(1) && !ARAlloc(UINT32_MAX));
    std::array<u8, 64> source{}, output{};
    for (unsigned i = 0; i < source.size(); ++i) source[i] = i + 1;
    ARQInit(); assert(ARQCheckInit());
    ARQPostRequest(&request, 0, 0, 1, reinterpret_cast<uintptr_t>(source.data()), ARGetSize() - 64, 64, callback);
    ARQPostRequest(&request, 0, 1, 0, ARGetSize() - 64, reinterpret_cast<uintptr_t>(output.data()), 64, callback);
    assert(output == source && completed == 2);
    reject([&] { ARQPostRequest(&request, 0, 0, 0, reinterpret_cast<uintptr_t>(source.data()), ARGetSize() - 32, 64, callback); });
    reject([&] { ARQPostRequest(&request, 0, 1, 0, uintptr_t(0x100000000) + 0x4000, reinterpret_cast<uintptr_t>(output.data()), 64, callback); });
    reject([&] { ARQPostRequest(&request, 0, 0, 0, 0, 0x4000, 64, callback); });
    reject([&] { ARQPostRequest(&request, 0, 2, 0, 0, 0x4000, 64, callback); });
    ARStartDMA(0, reinterpret_cast<uintptr_t>(source.data()), 0x4000, 64);
    output.fill(0);
    ARStartDMA(1, reinterpret_cast<uintptr_t>(output.data()), 0x4000, 64);
    assert(output == source && ARGetDMAStatus() == 0);
    ARClear(AR_CLEAR_INTERNAL_USER);
    ARStartDMA(1, reinterpret_cast<uintptr_t>(output.data()), 0x4000, 64);
    assert((output == std::array<u8, 64>{}));
    ARQReset(); assert(!ARQCheckInit());
    ARReset(); assert(!ARCheckInit() && !ARGetStorageAddress() && ARGetSize() == 0);
}
