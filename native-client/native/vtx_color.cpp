#include "vtx_color.hpp"
#include "JSystem/J3DGraphAnimator/J3DAnimation.hpp"
#include "JSystem/JKernel/JKRFileLoader.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace galaxy {
struct NativeVtxColor {
    std::vector<J3DAnmColorFullTable> full[2];
    std::vector<J3DAnmColorKeyTable> key[2];
    std::vector<J3DAnmVtxColorIndexData> indices[2];
    std::vector<std::vector<u16>> lists[2];
    std::vector<u8> colors[4];
    std::vector<s16> keyColors[4];
};
void releaseVtxColor(NativeVtxColor* data) { delete data; }
namespace {
struct Reader {
    std::span<const u8> bytes;
    void range(size_t offset, size_t size) const { if (offset > bytes.size() || size > bytes.size() - offset) throw std::invalid_argument("truncated vertex-color animation"); }
    u16 read16(size_t offset) const { range(offset, 2); return u16(bytes[offset]) << 8 | bytes[offset + 1]; }
    u32 read32(size_t offset) const { return u32(read16(offset)) << 16 | read16(offset + 2); }
};
template<class Animation> void load(Animation& animation, std::span<const u8> bytes, bool key) {
    Reader r{bytes}; r.range(0, 64);
    if (r.read32(0) != (key ? 0x56434b31 : 0x56434631) || r.read32(4) != bytes.size() || s16(r.read16(10)) < 0)
        throw std::invalid_argument("invalid vertex-color animation block");
    auto result = std::make_unique<NativeVtxColor>();
    std::array<u16, 4> colorCounts;
    for (unsigned channel = 0; channel < 4; ++channel) {
        colorCounts[channel] = r.read16(16 + channel * 2);
        const u32 offset = r.read32(48 + channel * 4);
        r.range(offset, size_t(colorCounts[channel]) * (key ? 2 : 1));
        if (key) {
            auto& values = result->keyColors[channel]; values.resize(colorCounts[channel]);
            for (size_t i = 0; i < values.size(); ++i) values[i] = s16(r.read16(offset + i * 2));
        } else result->colors[channel].assign(bytes.begin() + offset, bytes.begin() + offset + colorCounts[channel]);
    }
    for (unsigned side = 0; side < 2; ++side) {
        const u16 count = r.read16(12 + side * 2);
        const u32 tableOffset = r.read32(24 + side * 4), indicesOffset = r.read32(32 + side * 4), listOffset = r.read32(40 + side * 4);
        r.range(tableOffset, size_t(count) * (key ? 24 : 16)); r.range(indicesOffset, size_t(count) * 8);
        result->indices[side].resize(count); result->lists[side].resize(count);
        result->full[side].resize(key ? 0 : count); result->key[side].resize(key ? count : 0);
        for (u32 i = 0; i < count; ++i) {
            const u16 listCount = r.read16(indicesOffset + i * 8);
            const uint64_t offset = uint64_t(listOffset) + uint64_t(r.read32(indicesOffset + i * 8 + 4)) * 2;
            r.range(offset, size_t(listCount) * 2);
            auto& list = result->lists[side][i]; list.resize(listCount);
            for (u32 j = 0; j < listCount; ++j) list[j] = r.read16(offset + j * 2);
            result->indices[side][i] = {listCount, list.data()};
            for (unsigned channel = 0; channel < 4; ++channel) {
                const size_t entry = tableOffset + size_t(i) * (key ? 24 : 16) + channel * (key ? 6 : 4);
                const u16 frames = r.read16(entry), valueOffset = r.read16(entry + 2), type = key ? r.read16(entry + 4) : 0;
                const u32 values = key ? frames <= 1 ? frames : u32(frames) * (type ? 4 : 3) : frames;
                if ((!key && !frames) || type > 1 || valueOffset > colorCounts[channel] || values > colorCounts[channel] - valueOffset)
                    throw std::invalid_argument("vertex-color animation values out of bounds");
                if (key) {
                    auto* channels = reinterpret_cast<J3DAnmKeyTableBase*>(&result->key[side][i]);
                    channels[channel] = {frames, valueOffset, type};
                    if (frames > 1) {
                        const auto& data = result->keyColors[channel]; const unsigned stride = type ? 4 : 3;
                        for (u32 j = 1; j < frames; ++j) if (data[valueOffset + j * stride] <= data[valueOffset + (j - 1) * stride])
                            throw std::invalid_argument("vertex-color key times not increasing");
                    }
                } else {
                    auto* channels = reinterpret_cast<u16*>(&result->full[side][i]);
                    channels[channel * 2] = frames; channels[channel * 2 + 1] = valueOffset;
                }
            }
        }
    }
    // Publish only after the entire payload validates; replacing a resource cannot leave partial tables.
    releaseVtxColor(animation.mNativeVtxColor);
    animation.mNativeVtxColor = result.release();
    auto& native = *animation.mNativeVtxColor;
    animation.mFrameMax = r.read16(10); animation.mAttribute = bytes[8]; animation.mFrame = 0;
    for (unsigned side = 0; side < 2; ++side) {
        animation.mAnmTableNum[side] = native.indices[side].size(); animation.mAnmVtxColorIndexData[side] = native.indices[side].data();
        if constexpr (std::is_same_v<Animation, J3DAnmVtxColorFull>) animation.mpTable[side] = native.full[side].data();
        else animation.mpTable[side] = native.key[side].data();
    }
    if constexpr (std::is_same_v<Animation, J3DAnmVtxColorFull>) {
        animation.mColorR = native.colors[0].data(); animation.mColorG = native.colors[1].data();
        animation.mColorB = native.colors[2].data(); animation.mColorA = native.colors[3].data();
    } else {
        animation.mColorR = native.keyColors[0].data(); animation.mColorG = native.keyColors[1].data();
        animation.mColorB = native.keyColors[2].data(); animation.mColorA = native.keyColors[3].data();
    }
}
}
void loadVtxColor(J3DAnmVtxColorFull& animation, std::span<const u8> block) { load(animation, block, false); }
void loadVtxColor(J3DAnmVtxColorKey& animation, std::span<const u8> block) { load(animation, block, true); }
std::span<const u8> knownVtxColorBlock(const void* data) {
    const s32 remaining = JKRFileLoader::getGlbResourceRemainingSize(data);
    if (remaining < 8) throw std::invalid_argument("vertex-color resource extent unavailable");
    Reader r{{static_cast<const u8*>(data), size_t(remaining)}};
    const u32 size = r.read32(4); r.range(0, size);
    return r.bytes.first(size);
}
}
