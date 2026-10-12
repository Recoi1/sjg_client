#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"
#include <algorithm>
#include <cstring>

bool JKRHeap::initArena2(char** memory, u32* size, int maxHeaps) {
    // shortcut: one 128 MiB native secondary bank; validate capacity with real scenes before increasing it.
    alignas(32) static u8 arena[128 * 1024 * 1024];
    if (!memory || !size || maxHeaps <= 0) return false;
    *memory = reinterpret_cast<char*>(arena);
    *size = sizeof(arena);
    return true;
}

void JKRHeap::destroy() { JKRHeap::destroy(this); }
s32 JKRHeap::resize(void* ptr, u32 size, JKRHeap* heap) {
    if (!heap) heap = findFromRoot(ptr);
    return heap ? heap->resize(ptr, size) : -1;
}
s32 JKRHeap::getSize(void* ptr, JKRHeap* heap) {
    if (!heap) heap = findFromRoot(ptr);
    return heap ? heap->do_getSize(ptr) : -1;
}
void JKRHeap::fillMemory(void* ptr, u32 size, u8 value) { std::memset(ptr, value, size); }
void JKRHeap::fillMemory(u8* ptr, u32 size, u8 value) { fillMemory(static_cast<void*>(ptr), size, value); }
bool JKRHeap::checkMemoryFilled(void* ptr, u32 size, u8 value) {
    auto* bytes = static_cast<u8*>(ptr);
    return std::all_of(bytes, bytes + size, [value](u8 byte) { return byte == value; });
}
bool JKRHeap::checkMemoryFilled(u8* ptr, u32 size, u8 value) { return checkMemoryFilled(static_cast<void*>(ptr), size, value); }
u32 JKRHeap::getMaxAllocatableSize(int alignment) {
    if (alignment <= 0 || (alignment & (alignment - 1))) return 0;
    lock();
    uintptr_t address = reinterpret_cast<uintptr_t>(getMaxFreeBlock());
    if (getHeapType() == 'EXPH') address += sizeof(JKRExpHeap::CMemBlock);
    u32 available = getFreeSize();
    uintptr_t padding = (-address) & (alignment - 1);
    u32 result = available > padding ? (available - padding) & ~(alignment - 1) : 0;
    unlock();
    return result;
}

// Solid heaps release whole regions; individual blocks are intentionally not tracked.
void JKRSolidHeap::do_free(void*) {}
s32 JKRSolidHeap::adjustSize() {
    JKRHeap* parent = getParent();
    if (!parent || mSolidTail != mEnd) return -1;
    lock();
    const u32 required = mSolidHead - reinterpret_cast<u8*>(this);
    s32 resized = parent->resize(this, required);
    if (resized >= 0) {
        mEnd = reinterpret_cast<u8*>(this) + resized;
        mSolidTail = mEnd;
        mSize = mEnd - mStart;
        mFreeSize = mEnd - mSolidHead;
    }
    unlock();
    return resized;
}
s32 JKRExpHeap::adjustSize() {
    JKRHeap* parent = getParent();
    if (!parent || mIsExternal || parent->getHeapType() != 'EXPH') return -1;
    lock();
    CMemBlock* tail = mTailFreeList;
    if (!tail || reinterpret_cast<u8*>(tail + 1) + tail->mSize != mEnd) { unlock(); return -1; }
    u8* requiredEnd = reinterpret_cast<u8*>(tail + 1);
    s32 resized = parent->resize(this, requiredEnd - reinterpret_cast<u8*>(this));
    if (resized >= 0) {
        mEnd = reinterpret_cast<u8*>(this) + resized;
        tail->mSize = mEnd - reinterpret_cast<u8*>(tail + 1);
        mSize = mEnd - mStart;
    }
    unlock();
    return resized;
}
