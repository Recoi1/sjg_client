#include "tpl.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace galaxy {
TplImage readTpl(std::span<const u8> bytes, u32 index) {
    auto range = [&](size_t offset, size_t size) {
        if (offset > bytes.size() || size > bytes.size() - offset) throw std::invalid_argument("truncated TPL resource");
    };
    auto u16be = [&](size_t offset) -> u16 { range(offset, 2); return u16(bytes[offset]) << 8 | bytes[offset + 1]; };
    auto u32be = [&](size_t offset) -> u32 { return u32(u16be(offset)) << 16 | u16be(offset + 2); };
    range(0, 12);
    const u32 count = u32be(4), table = u32be(8);
    if (u32be(0) != 0x0020af30 || index >= count || table < 12) throw std::invalid_argument("invalid TPL header or image index");
    range(table, uint64_t(count) * 8);
    const u32 texture = u32be(table + uint64_t(index) * 8), palette = u32be(table + uint64_t(index) * 8 + 4);
    range(texture, 36);
    TplImage result;
    auto& t = result.texture;
    t.height = u16be(texture); t.width = u16be(texture + 2); t.format = u32be(texture + 4);
    const u32 image = u32be(texture + 8);
    t.wrapS = GXTexWrapMode(u32be(texture + 12)); t.wrapT = GXTexWrapMode(u32be(texture + 16));
    t.minFilter = GXTexFilter(u32be(texture + 20)); t.magFilter = GXTexFilter(u32be(texture + 24));
    t.LODBias = std::bit_cast<float>(u32be(texture + 28));
    t.edgeLODEnable = bytes[texture + 32]; t.minLOD = bytes[texture + 33]; t.maxLOD = bytes[texture + 34];
    if (!t.width || !t.height || t.width > 1024 || t.height > 1024 || t.wrapS > GX_MIRROR || t.wrapT > GX_MIRROR ||
        t.minFilter > GX_LIN_MIP_LIN || t.magFilter > GX_LINEAR || t.minLOD > t.maxLOD || t.maxLOD > 10 || !std::isfinite(t.LODBias))
        throw std::invalid_argument("invalid TPL texture attributes");
    u32 blockWidth = 4, blockHeight = 4, blockSize = 32;
    switch (t.format) {
    case GX_TF_I4: case GX_TF_C4: case GX_TF_CMPR: blockWidth = blockHeight = 8; break;
    case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: blockWidth = 8; break;
    case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_C14X2: break;
    case GX_TF_RGBA8: blockSize = 64; break;
    default: throw std::invalid_argument("unsupported TPL texture format");
    }
    size_t imageSize = 0;
    u32 width = t.width, height = t.height;
    for (u32 level = 0; level <= t.maxLOD; ++level) {
        imageSize += size_t((width + blockWidth - 1) / blockWidth) * ((height + blockHeight - 1) / blockHeight) * blockSize;
        width = std::max(1u, width / 2); height = std::max(1u, height / 2);
    }
    range(image, imageSize);
    t.data = reinterpret_cast<char*>(const_cast<u8*>(bytes.data() + image));
    const bool indexed = t.format == GX_TF_C4 || t.format == GX_TF_C8 || t.format == GX_TF_C14X2;
    if (indexed && !palette) throw std::invalid_argument("indexed TPL has no palette");
    if (palette) {
        range(palette, 12);
        auto& p = result.palette;
        p.numEntries = u16be(palette); p.format = GXTlutFmt(u32be(palette + 4));
        const u32 pixels = u32be(palette + 8);
        if (!p.numEntries || p.numEntries > 16384 || p.format > GX_TL_RGB5A3) throw std::invalid_argument("invalid TPL palette");
        range(pixels, size_t(p.numEntries) * 2);
        p.data = reinterpret_cast<char*>(const_cast<u8*>(bytes.data() + pixels));
        result.hasPalette = true;
    }
    return result;
}
}
