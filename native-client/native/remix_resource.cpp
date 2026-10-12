#include "remix_resource.hpp"
#include <stdexcept>
namespace galaxy {
std::vector<std::uint32_t> importRemix(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 4 || bytes.size() % 4) throw std::invalid_argument("invalid remix resource size");
    std::vector<std::uint32_t> words;
    words.reserve(bytes.size() / 4);
    for (std::size_t i = 0; i < bytes.size(); i += 4)
        words.push_back(std::uint32_t(bytes[i]) << 24 | std::uint32_t(bytes[i + 1]) << 16 | std::uint32_t(bytes[i + 2]) << 8 | bytes[i + 3]);
    auto range = [&](std::size_t p, std::size_t n) {
        if (p > words.size() || n > words.size() - p) throw std::invalid_argument("truncated remix record");
    };
    const auto count = words[0]; range(1, count);
    std::size_t p = 1 + count;
    for (unsigned i = 0; i < count; ++i) {
        range(p, 2); const auto tracks = words[p++], notes = words[p++];
        range(p, notes); p += notes;
        for (unsigned j = 0; j < tracks; ++j) { range(p, 1 + std::size_t(notes) * 4); p += 1 + std::size_t(notes) * 4; }
    }
    // Resource alignment can append zero words after the final group.
    for (; p < words.size(); ++p) if (words[p]) throw std::invalid_argument("unexpected remix trailing data");
    return words;
}
}
