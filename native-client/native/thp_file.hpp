#pragma once
#include <revolution/thp.h>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>

namespace galaxy {
inline u32 thpWord(std::span<const u8> bytes, size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) throw std::runtime_error("Truncated THP metadata");
    return u32(bytes[offset]) << 24 | u32(bytes[offset + 1]) << 16 | u32(bytes[offset + 2]) << 8 | bytes[offset + 3];
}
inline THPHeader readThpHeader(std::span<const u8> bytes, size_t fileSize) {
    if (bytes.size() < 48 || std::memcmp(bytes.data(), "THP\0", 4)) throw std::runtime_error("Invalid THP header");
    THPHeader h{};
    std::memcpy(h.magic, bytes.data(), 4);
    h.version = thpWord(bytes, 4); h.bufSize = thpWord(bytes, 8); h.audioMaxSamples = thpWord(bytes, 12);
    h.frameRate = std::bit_cast<f32>(thpWord(bytes, 16)); h.numFrames = thpWord(bytes, 20);
    h.firstFrameSize = thpWord(bytes, 24); h.movieDataSize = thpWord(bytes, 28);
    h.compInfoDataOffsets = thpWord(bytes, 32); h.offsetDataOffsets = thpWord(bytes, 36);
    h.movieDataOffsets = thpWord(bytes, 40); h.finalFrameDataOffsets = thpWord(bytes, 44);
    if (h.version != 0x11000 || !h.numFrames || !std::isfinite(h.frameRate) || h.frameRate <= 0 || h.frameRate > 120 ||
        h.bufSize < 12 || h.bufSize > 16 * 1024 * 1024 || h.audioMaxSamples > 65536 ||
        h.firstFrameSize < 12 || h.firstFrameSize > h.bufSize || h.compInfoDataOffsets < 48 ||
        h.compInfoDataOffsets > fileSize || fileSize - h.compInfoDataOffsets < 20 ||
        h.movieDataOffsets < h.compInfoDataOffsets + 20 || h.movieDataOffsets > fileSize ||
        h.movieDataSize > fileSize - h.movieDataOffsets || h.firstFrameSize > h.movieDataSize ||
        h.finalFrameDataOffsets < h.movieDataOffsets || h.finalFrameDataOffsets >= h.movieDataOffsets + h.movieDataSize)
        throw std::runtime_error("Invalid THP header extents");
    return h;
}
inline THPFrameCompInfo readThpComponents(std::span<const u8> bytes) {
    THPFrameCompInfo c{};
    c.numComponents = thpWord(bytes, 0);
    if (bytes.size() < 20 || (c.numComponents != 1 && c.numComponents != 2) || bytes[4] != 0 ||
        (c.numComponents == 2 && bytes[5] != 1)) throw std::runtime_error("Unsupported THP components");
    std::memcpy(c.frameComp, bytes.data() + 4, 16);
    return c;
}
inline THPVideoInfo readThpVideo(std::span<const u8> bytes) {
    THPVideoInfo v{thpWord(bytes, 0), thpWord(bytes, 4), thpWord(bytes, 8)};
    // Galaxy allocates packed 8x4 GX tiles for both luma and half-sized chroma.
    if (!v.xSize || !v.ySize || v.xSize > 1024 || v.ySize > 1024 || v.xSize % 16 || v.ySize % 8 || v.videoType != 0)
        throw std::runtime_error("Unsupported THP video dimensions");
    return v;
}
inline THPAudioInfo readThpAudio(std::span<const u8> bytes) {
    THPAudioInfo a{thpWord(bytes, 0), thpWord(bytes, 4), thpWord(bytes, 8), thpWord(bytes, 12)};
    if ((a.sndChannels != 1 && a.sndChannels != 2) || a.sndFrequency < 8000 || a.sndFrequency > 48000 ||
        !a.sndNumTracks || a.sndNumTracks > 16) throw std::runtime_error("Unsupported THP audio metadata");
    return a;
}
struct ThpFrame {
    std::span<const u8> video;
    std::span<const u8> audio;
    u32 nextSize;
};
inline ThpFrame readThpFrame(std::span<const u8> bytes, const THPFrameCompInfo& components, u32 tracks, u32 track) {
    size_t offset = 8 + components.numComponents * 4;
    if (offset > bytes.size() || components.numComponents < 1 || components.numComponents > 2)
        throw std::runtime_error("Truncated THP frame header");
    const u32 videoSize = thpWord(bytes, 8);
    if (!videoSize || videoSize > bytes.size() - offset) throw std::runtime_error("Invalid THP video extent");
    ThpFrame f{bytes.subspan(offset, videoSize), {}, thpWord(bytes, 0)};
    offset += videoSize;
    if (components.numComponents == 2) {
        const u32 audioSize = thpWord(bytes, 12);
        if (audioSize < 80 || !tracks || track >= tracks || size_t(audioSize) * tracks > bytes.size() - offset)
            throw std::runtime_error("Invalid THP audio extent");
        f.audio = bytes.subspan(offset + size_t(audioSize) * track, audioSize);
    }
    return f;
}
}
