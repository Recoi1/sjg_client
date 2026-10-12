#include "JSystem/JKernel/JKRUnitHeap.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/JAudio2/JASCalc.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <random>

int main() {
    auto* root = JKRExpHeap::createRoot(1, false);
    const int initial = root->getTotalFreeSize();
    assert(!JKRUnitHeap::create(0, 4096, 16, root, false));
    assert(!JKRUnitHeap::create(1, 4096, 24, root, false));
    assert(!JKRUnitHeap::create(UINT32_MAX, 4096, 16, root, false));
    auto* heap = JKRUnitHeap::create(52, 16000, 32, root, false);
    assert(heap && heap->check() && heap->getMaxFreeBlock() == heap->mUnits);
    const int available = heap->getTotalFreeSize();
    assert(!heap->alloc(UINT32_MAX, 16)); assert(!heap->alloc(4, 128));
    void* head = heap->alloc(3 * heap->mUnitSize, 32);
    void* tail = heap->alloc(2 * heap->mUnitSize, -32);
    assert(head == heap->mUnits && tail && tail != head);
    assert(heap->do_getSize(head) == 3 * heap->mUnitSize);
    heap->free(static_cast<u8*>(head) + heap->mUnitSize);
    assert(heap->do_getSize(head) == 3 * heap->mUnitSize);
    heap->free(head); heap->free(head); heap->free(tail);
    assert(heap->check() && heap->getTotalFreeSize() == available);
    struct Item { void* pointer = nullptr; u32 size = 0; u8 marker = 0; };
    std::array<Item, 80> items{};
    std::mt19937 random(0x554e4954);
    for (int iteration = 0; iteration < 5000; ++iteration) {
        auto& item = items[random() % items.size()];
        if (item.pointer) {
            auto* bytes = static_cast<u8*>(item.pointer);
            for (u32 i = 0; i < item.size; ++i) assert(bytes[i] == item.marker);
            heap->free(item.pointer); item.pointer = nullptr;
        } else {
            item.size = 1 + random() % 200; item.marker = random();
            item.pointer = heap->alloc(item.size, random() & 1 ? 16 : -16);
            if (item.pointer) { assert(heap->do_getSize(item.pointer) >= item.size); std::memset(item.pointer, item.marker, item.size); }
        }
        assert(heap->check());
    }
    heap->freeAll(); assert(heap->check() && heap->getTotalFreeSize() == available);
    heap->destroy(); assert(root->getTotalFreeSize() == initial);
    std::array<u8, 160> data, expected;
    for (unsigned offset = 0; offset < 8; ++offset) {
        for (unsigned length = 0; length < 65; ++length) {
            for (unsigned i = 0; i < data.size(); ++i) data[i] = i;
            expected = data;
            std::memmove(expected.data() + 40 + offset, expected.data() + offset, length);
            JASCalc::bcopyfast(data.data() + offset, data.data() + 40 + offset, length);
            assert(data == expected);
            std::memset(expected.data() + offset, 0, length);
            JASCalc::bzero(data.data() + offset, length); assert(data == expected);
        }
    }
    root->destroy();
}
