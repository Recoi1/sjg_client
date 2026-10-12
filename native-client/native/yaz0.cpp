#include "yaz0.hpp"
#include <algorithm>
#include <stdexcept>

namespace galaxy {
u32 yaz0ExpandedSize(std::span<const u8> source) {
    if (source.size() < 16 || !std::equal(source.begin(), source.begin() + 4, "Yaz0"))
        throw std::runtime_error("Invalid or truncated Yaz0 header");
    const u32 size = (u32(source[4]) << 24) | (u32(source[5]) << 16) | (u32(source[6]) << 8) | source[7];
    // shortcut: 64 MiB per resource, raise only for a verified larger asset.
    if (size > 64 * 1024 * 1024) throw std::runtime_error("Yaz0 output exceeds size limit");
    return size;
}

std::vector<u8> decodeYaz0(std::span<const u8> source) {
    std::vector<u8> output(yaz0ExpandedSize(source));
    size_t input = 16;
    size_t written = 0;
    auto read = [&]() {
        if (input >= source.size()) throw std::runtime_error("Truncated Yaz0 data");
        return source[input++];
    };
    // The game's JKRDecomp::decodeSZS loop, with host indices and explicit input/output bounds.
    while (written < output.size()) {
        const u8 code = read();
        for (u32 bit = 0x80; bit && written < output.size(); bit >>= 1) {
            if (code & bit) {
                output[written++] = read();
            } else {
                const u8 first = read();
                const u8 second = read();
                const size_t distance = ((size_t(first & 0xf) << 8) | second) + 1;
                size_t count = first >> 4;
                count = count ? count + 2 : size_t(read()) + 0x12;
                if (distance > written) throw std::runtime_error("Invalid Yaz0 back-reference");
                count = std::min(count, output.size() - written);
                while (count--) {
                    output[written] = output[written - distance];
                    ++written;
                }
            }
        }
    }
    return output;
}
}
