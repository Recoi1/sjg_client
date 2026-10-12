#include "seq_report.hpp"
#include <cassert>
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace {
u32 reg(void*, u8 id) { assert(id == 7); return UINT32_MAX; }
}
int main() {
    const char format[] = "%d %x %s %r %%\\n";
    std::vector<u8> sequence(80);
    std::copy_n(format, sizeof(format), sequence.begin());
    sequence[sizeof(format)] = 42; sequence[sizeof(format) + 1] = 255;
    sequence[sizeof(format) + 2] = 60; sequence[sizeof(format) + 3] = 7;
    std::copy_n("hello", 6, sequence.begin() + 60);
    size_t position = 0;
    assert(galaxy::readSequenceReport(sequence, position, reg, nullptr) == "42 ff hello -1 %\r");
    assert(position == sizeof(format) + 4);
    auto rejected = [](std::span<const u8> bytes) {
        size_t offset = 0; bool caught = false;
        try { galaxy::readSequenceReport(bytes, offset, reg, nullptr); } catch (const std::invalid_argument&) { caught = true; }
        assert(caught && offset == 0);
    };
    for (size_t size = 0; size <= 64; ++size) rejected(std::span<const u8>(sequence).first(size));
    auto bad = sequence; bad[1] = 'n'; rejected(bad);
    bad = sequence; bad[sizeof(format) + 2] = 250; rejected(bad);
    bad = sequence; std::fill(bad.begin() + 60, bad.end(), 'x'); rejected(bad);
}
