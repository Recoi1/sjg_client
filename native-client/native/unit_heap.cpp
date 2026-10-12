#include "JSystem/JKernel/JKRUnitHeap.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

namespace {
u8 mask(u32 index) { return 0x80 >> (index & 7); }
struct Run { s32 start = -1; u32 length = 0; };
Run largest(JKRUnitHeap* heap) {
    Run best, run;
    for (u32 i = 0; i <= heap->mUnitCount; ++i) {
        if (i < heap->mUnitCount && !heap->isUnitUsed(i)) {
            if (!run.length) run.start = i;
            ++run.length;
        } else { if (run.length > best.length) best = run; run = {}; }
    }
    return best;
}
}
JKRUnitHeap* JKRUnitHeap::create(u32 unitSize, u32 size, u32 alignment, JKRHeap* parent, bool errorFlag) {
    if (!parent) parent = sRootHeap;
    if (!alignment) alignment = 4;
    alignment = std::max<u32>(alignment, alignof(std::max_align_t));
    if (!parent || !unitSize || (alignment & (alignment - 1))) return nullptr;
    if (unitSize > UINT32_MAX - alignment + 1) return nullptr;
    unitSize = (unitSize + alignment - 1) & ~(alignment - 1);
    if (size == UINT32_MAX) size = parent->getMaxAllocatableSize(alignment);
    if (size <= sizeof(JKRUnitHeap) + alignment) return nullptr;
    auto* memory = static_cast<u8*>(parent->alloc(size, alignment));
    if (!memory) return nullptr;
    u32 count = (size - sizeof(JKRUnitHeap)) / (uint64_t(unitSize) + sizeof(u32) + 1);
    u8* bat = memory + sizeof(JKRUnitHeap);
    u32* lengths = nullptr;
    u8* units = nullptr;
    while (count) {
        lengths = reinterpret_cast<u32*>((reinterpret_cast<uintptr_t>(bat + (count + 7) / 8) + 3) & ~uintptr_t(3));
        units = reinterpret_cast<u8*>((reinterpret_cast<uintptr_t>(lengths + count) + alignment - 1) & ~uintptr_t(alignment - 1));
        if (units <= memory + size && uint64_t(count) * unitSize <= u32(memory + size - units)) break;
        --count;
    }
    if (!count) { parent->free(memory); return nullptr; }
    auto* heap = new (memory) JKRUnitHeap(bat, units, unitSize, count, count * unitSize, alignment, parent, errorFlag);
    heap->mAllocCounts = lengths;
    std::memset(lengths, 0, count * sizeof(u32));
    return heap;
}
JKRUnitHeap::JKRUnitHeap(u8* bat, u8* units, u32 unitSize, u32 count, u32, u32 alignment, JKRHeap* parent, bool errorFlag)
    : JKRHeap(units, unitSize * count, parent, errorFlag), mUnitSize(unitSize), mUnitCount(count), mAlignment(alignment),
      mBat(bat), mUnits(units), mTotalFreeSize(unitSize * count), mAllocMode(1), mAllocCounts(nullptr) {
    clearBatArea();
}
JKRUnitHeap::~JKRUnitHeap() { dispose(); }
void JKRUnitHeap::do_destroy() { auto* parent = getParent(); this->~JKRUnitHeap(); if (parent) parent->free(this); }
void JKRUnitHeap::clearBatArea() {
    std::memset(mBat, 0xff, mUnits - mBat);
    for (u32 i = 0; i < mUnitCount; ++i) mBat[i / 8] &= ~mask(i);
    if (mAllocCounts) std::memset(mAllocCounts, 0, mUnitCount * sizeof(u32));
}
BOOL JKRUnitHeap::isUnitUsed(int i) const { return i >= 0 && u32(i) < mUnitCount && (mBat[i / 8] & mask(i)); }
void JKRUnitHeap::setUnitUsed(int i) { if (i >= 0 && u32(i) < mUnitCount) mBat[i / 8] |= mask(i); }
s32 JKRUnitHeap::find1FreeBlock(int direction) { return findFreeBlock(direction, 1); }
s32 JKRUnitHeap::findFreeBlock(int direction, u32 count) {
    if (!count || count > mUnitCount) return -1;
    Run best, run;
    for (u32 step = 0; step <= mUnitCount; ++step) {
        const s32 i = direction >= 0 ? s32(step) : s32(mUnitCount) - 1 - s32(step);
        if (step < mUnitCount && !isUnitUsed(i)) {
            if (!run.length) run.start = i;
            ++run.length;
        } else {
            if (run.length >= count && (mAllocMode == 1 || run.length == count))
                return direction >= 0 ? run.start : run.start - count + 1;
            if (run.length >= count && (best.start < 0 || run.length < best.length)) best = run;
            run = {};
        }
    }
    return best.start < 0 ? -1 : direction >= 0 ? best.start : best.start - count + 1;
}
s32 JKRUnitHeap::findFreeBlock_fromHead(u32 count) { return count ? findFreeBlock(1, count) : largest(this).length; }
s32 JKRUnitHeap::findFreeBlock_fromTail(u32 count) { return count ? findFreeBlock(-1, count) : largest(this).length; }
void* JKRUnitHeap::do_alloc(u32 size, int alignment) {
    if (!size || !mAllocCounts || alignment == INT32_MIN) return nullptr;
    const u32 requestedAlignment = alignment < 0 ? -alignment : alignment;
    if (requestedAlignment && ((requestedAlignment & (requestedAlignment - 1)) || requestedAlignment > mAlignment)) return nullptr;
    std::lock_guard guard(mMutex);
    const uint64_t count = (uint64_t(size) + mUnitSize - 1) / mUnitSize;
    if (count > mUnitCount) return nullptr;
    const s32 index = findFreeBlock(alignment, count);
    if (index < 0) return nullptr;
    for (u32 i = 0; i < count; ++i) { setUnitUsed(index + i); mAllocCounts[index + i] = i ? UINT32_MAX : u32(count); }
    mTotalFreeSize -= count * mUnitSize;
    return indexToAddress(index);
}
void JKRUnitHeap::do_free(void* memory) {
    std::lock_guard guard(mMutex);
    const s32 index = addressToIndex(memory);
    if (index < 0 || !mAllocCounts || !mAllocCounts[index] || mAllocCounts[index] == UINT32_MAX) return;
    const u32 count = mAllocCounts[index];
    dispose(memory, count * mUnitSize);
    for (u32 i = 0; i < count; ++i) { mBat[(index + i) / 8] &= ~mask(index + i); mAllocCounts[index + i] = 0; }
    mTotalFreeSize += count * mUnitSize;
}
void JKRUnitHeap::do_freeAll() { std::lock_guard guard(mMutex); dispose(); clearBatArea(); mTotalFreeSize = mSize; }
void JKRUnitHeap::do_freeTail() {
    // Unit allocations have no separate tail region; the SDK operation does not release individual units.
}
void JKRUnitHeap::do_fillFreeArea() {
    std::lock_guard guard(mMutex);
    for (u32 i = 0; i < mUnitCount; ++i) if (!isUnitUsed(i)) std::memset(indexToAddress(i), 0xdd, mUnitSize);
}
s32 JKRUnitHeap::do_resize(void*, u32) { return -1; }
s32 JKRUnitHeap::do_getSize(void* memory) {
    std::lock_guard guard(mMutex);
    const s32 i = addressToIndex(memory);
    return i < 0 || !mAllocCounts || mAllocCounts[i] == UINT32_MAX ? -1 : mAllocCounts[i] ? mAllocCounts[i] * mUnitSize : -1;
}
s32 JKRUnitHeap::do_getFreeSize() { std::lock_guard guard(mMutex); return largest(this).length * mUnitSize; }
void* JKRUnitHeap::do_getMaxFreeBlock() { std::lock_guard guard(mMutex); return indexToAddress(largest(this).start); }
s32 JKRUnitHeap::do_getTotalFreeSize() { std::lock_guard guard(mMutex); return mTotalFreeSize; }
void* JKRUnitHeap::indexToAddress(int i) { return i >= 0 && u32(i) < mUnitCount ? mUnits + uint64_t(i) * mUnitSize : nullptr; }
s32 JKRUnitHeap::addressToIndex(void* memory) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(memory), base = reinterpret_cast<uintptr_t>(mUnits);
    if (address < base || address - base >= uint64_t(mUnitCount) * mUnitSize || (address - base) % mUnitSize) return -1;
    return (address - base) / mUnitSize;
}
bool JKRUnitHeap::check() {
    std::lock_guard guard(mMutex);
    u32 free = 0;
    for (u32 i = 0; i < mUnitCount; ++i) {
        if (bool(isUnitUsed(i)) != bool(mAllocCounts[i])) return false;
        if (!isUnitUsed(i)) free += mUnitSize;
        else if (mAllocCounts[i] != UINT32_MAX) {
            const u32 count = mAllocCounts[i];
            if (count > mUnitCount - i) return false;
            for (u32 j = 1; j < count; ++j) if (mAllocCounts[i + j] != UINT32_MAX || !isUnitUsed(i + j)) return false;
        } else if (!i || !mAllocCounts[i - 1]) return false;
    }
    return free == mTotalFreeSize;
}
bool JKRUnitHeap::dump() { return check(); }
void JKRUnitHeap::state_register(TState* state, u32 id) const {
    setState_u32ID_(state, id); setState_uUsedSize_(state, mSize - mTotalFreeSize);
    u32 hash = 0;
    for (u32 i = 0; i < mUnitCount; ++i) hash = hash * 31 + mAllocCounts[i];
    setState_u32CheckCode_(state, hash);
}
bool JKRUnitHeap::state_compare(const TState& a, const TState& b) const { return a.getCheckCode() == b.getCheckCode() && a.getUsedSize() == b.getUsedSize(); }
u32 JKRUnitHeap::getHeapType() { return 'UNIT'; }
