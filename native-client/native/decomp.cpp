#include "decomp.hpp"
#include "yaz0.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace galaxy {
void decompress(std::span<const u8> source, std::span<u8> destination, u32 skip) {
    if (source.size() < 16) throw std::invalid_argument("short compression header");
    std::vector<u8> decoded;
    if (std::memcmp(source.data(), "Yaz0", 4) == 0) decoded = decodeYaz0(source);
    else if (std::memcmp(source.data(), "Yay0", 4) == 0) {
        auto u32be = [&](size_t offset) -> u32 {
            if (offset > source.size() || source.size() - offset < 4) throw std::invalid_argument("short Yay0 bit mask");
            return u32(source[offset]) << 24 | u32(source[offset + 1]) << 16 | u32(source[offset + 2]) << 8 | source[offset + 3];
        };
        const u32 size = u32be(4);
        size_t maskOffset = 16, linkOffset = u32be(8), dataOffset = u32be(12);
        if (size > 64 * 1024 * 1024 || linkOffset < 16 || dataOffset < linkOffset || dataOffset > source.size())
            throw std::invalid_argument("invalid Yay0 section offsets or expanded size");
        const size_t maskEnd = linkOffset, linkEnd = dataOffset;
        decoded.reserve(size);
        u32 mask = 0; unsigned remaining = 0;
        auto dataByte = [&]() -> u8 { if (dataOffset == source.size()) throw std::invalid_argument("short Yay0 data"); return source[dataOffset++]; };
        while (decoded.size() < size) {
            if (!remaining) { if (maskOffset > maskEnd || maskEnd - maskOffset < 4) throw std::invalid_argument("short Yay0 mask section"); mask = u32be(maskOffset); maskOffset += 4; remaining = 32; }
            if (mask & 0x80000000) decoded.push_back(dataByte());
            else {
                if (linkOffset > linkEnd || linkEnd - linkOffset < 2) throw std::invalid_argument("short Yay0 link section");
                const u16 link = u16(source[linkOffset]) << 8 | source[linkOffset + 1]; linkOffset += 2;
                const size_t distance = (link & 0xfff) + 1;
                const size_t length = link >> 12 ? (link >> 12) + 2 : dataByte() + 18;
                if (distance > decoded.size() || length > size - decoded.size()) throw std::invalid_argument("invalid Yay0 back reference");
                for (size_t i = 0; i < length; ++i) decoded.push_back(decoded[decoded.size() - distance]);
            }
            mask <<= 1; --remaining;
        }
    } else throw std::invalid_argument("unsupported compression format");
    if (skip > decoded.size()) throw std::invalid_argument("decompression skip exceeds output");
    std::copy_n(decoded.begin() + skip, std::min(destination.size(), decoded.size() - skip), destination.begin());
}
}
