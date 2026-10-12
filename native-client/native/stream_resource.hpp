#pragma once
#include "JSystem/JAudio2/JASAramStream.hpp"
#include <bit>
#include <stdexcept>

namespace galaxy {
inline u32 streamWord(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
inline u16 streamHalf(const u8* p) { return (u16(p[0]) << 8) | p[1]; }
inline void importStreamHeader(JASAramStream::Header& header, u32 fileSize, u32 blockSize, u32 maxChannels) {
    static_assert(sizeof(header) == 64);
    const auto* p = reinterpret_cast<const u8*>(&header);
    const auto channels = streamHalf(p + 12), loop = streamHalf(p + 14);
    const auto rate = streamWord(p + 16), samples = streamWord(p + 20), start = streamWord(p + 24), end = streamWord(p + 28);
    if (streamWord(p) != 0x5354524d || fileSize < 64 || streamWord(p + 4) != fileSize - 64 ||
        streamHalf(p + 8) > 1 || streamHalf(p + 10) != 16 || !channels || channels > maxChannels || channels > 6 ||
        rate < 8000 || rate > 192000 || !samples || samples > INT32_MAX || !end || end > samples ||
        (loop && start >= end) || streamWord(p + 32) != blockSize)
        throw std::invalid_argument("Invalid native audio stream header");
    header.channels = channels; header.loop = loop; header._10 = rate;
    header.loop_start = start; header.loop_end = end; header.block_size = blockSize;
}
inline u32 importStreamBlock(JASAramStream::BlockHeader& header, u32 storedSize, u32 channels, u32 blockSize, bool last) {
    static_assert(sizeof(header) == 32);
    const auto* p = reinterpret_cast<const u8*>(&header); const auto size = streamWord(p + 4);
    const u32 extent = 32 + size * channels;
    if (!channels || channels > 6 || streamWord(p) != 0x424c434b || !size || size > blockSize || size % 32 ||
        storedSize < 32 || storedSize > extent || (extent != storedSize && (!last || extent - storedSize > 64)))
        throw std::invalid_argument("Invalid native audio stream block");
    header._4 = size;
    for (unsigned i = 0; i < 6; ++i) {
        header._8[i]._0 = std::bit_cast<s16>(streamHalf(p + 8 + i * 4));
        header._8[i]._2 = std::bit_cast<s16>(streamHalf(p + 10 + i * 4));
    }
    return extent;
}
}
