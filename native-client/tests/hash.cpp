#include <Game/Util/HashUtil.hpp>
#include <cassert>
#include <stdexcept>

int main() {
    static_assert(sizeof(uintptr_t) == sizeof(void*));
    assert(MR::getHashCode("Aa") == 2112);
    assert(MR::getHashCodeLower("Aa") == 3104);
    assert(MR::getHashCodeLower("MiXeD") == MR::getHashCode("mixed"));
    const char highBytes[] = {static_cast<char>(0xff), 'A', 0};
    assert(MR::getHashCodeLower(highBytes) == 66);
    HashSortTable empty(0);
    empty.sort();
    uintptr_t value = 123;
    assert(!empty.search("missing", &value) && value == 0);
    assert(!empty.add("overflow", 0, false));

    int object;
    const auto address = reinterpret_cast<uintptr_t>(&object);
    HashSortTable table(3);
    assert(table.add("pointer", address, false));
    assert(!table.add("pointer", 12, true));
    assert(table.add(0xfffffffe, uintptr_t(1) << 48));
    assert(table.add(1, 42));
    assert(!table.add("overflow", 0, false));
    table.sort();
    assert(table.search("pointer", &value) && reinterpret_cast<int*>(value) == &object);
    assert(table.search(0xfffffffe, &value) && value == uintptr_t(1) << 48);
    assert(table.search(1, &value) && value == 42);
    assert(table.search("pointer", nullptr));
    value = address;
    assert(!table.search("absent", &value) && value == 0);

    HashSortTable full(1024);
    for (unsigned i = 0; i < 1024; ++i) assert(full.add(i, (uintptr_t(1) << 40) + i));
    assert(!full.add(1024, 0));
    full.sort();
    for (unsigned i = 0; i < 1024; ++i)
        assert(full.search(i, &value) && value == (uintptr_t(1) << 40) + i);
    try { HashSortTable oversized(1025); assert(false); } catch (const std::invalid_argument&) {}
}
