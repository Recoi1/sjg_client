#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include "rfl_resource.h"

static_assert(sizeof(RFLCharModel) >= sizeof(RFLiCharModel));
static_assert(alignof(RFLCharModel) >= alignof(RFLiCharModel));
static_assert(sizeof(RFLMiddleDB) >= sizeof(RFLiMiddleDB));

namespace {
void require(bool valid) { if (!valid) throw std::runtime_error("Invalid Mii face resource"); }
void extent(std::span<const u8> bytes, size_t offset, size_t size) {
    require(offset <= bytes.size() && size <= bytes.size() - offset);
}
u16 word16(std::span<const u8> bytes, size_t offset) {
    extent(bytes, offset, 2);
    return u16(bytes[offset]) << 8 | bytes[offset + 1];
}
u32 word32(std::span<const u8> bytes, size_t offset) {
    extent(bytes, offset, 4);
    return u32(bytes[offset]) << 24 | u32(bytes[offset + 1]) << 16 | u32(bytes[offset + 2]) << 8 | bytes[offset + 3];
}
std::span<const u8> cache(const RFLiLoader* loader) {
    require(loader && loader->cached && loader->cache && loader->cacheSize);
    return {static_cast<const u8*>(loader->cache), loader->cacheSize};
}
std::span<const u8> resource(const RFLiLoader* loader, u32 id, u16 file) {
    const auto bytes = cache(loader);
    require(id < RFLiArcID_Max);
    const auto& arc = loader->archives[id];
    require(file < arc.numFiles);
    const size_t tableEnd = size_t(arc.offset) + (size_t(arc.numFiles) + 1) * 4;
    extent(bytes, arc.offset, tableEnd - arc.offset);
    const auto first = word32(bytes, size_t(arc.offset) + file * 4);
    const auto last = word32(bytes, size_t(arc.offset) + (size_t(file) + 1) * 4);
    require(first <= last);
    extent(bytes, tableEnd, last);
    return bytes.subspan(tableEnd + first, last - first);
}
void swap16(std::span<u8> bytes, size_t offset) {
    const u16 value = word16(bytes, offset);
    std::memcpy(bytes.data() + offset, &value, 2);
}
void swap32(std::span<u8> bytes, size_t offset) {
    const u32 value = word32(bytes, offset);
    std::memcpy(bytes.data() + offset, &value, 4);
}
}
extern "C" {
void RFLPCParseResource(RFLiLoader* loader) {
    const auto bytes = cache(loader);
    extent(bytes, 0, 4 + RFLiArcID_Max * 4);
    loader->version = word16(bytes, 2);
    for (unsigned id = 0; id < RFLiArcID_Max; ++id) {
        const auto offset = word32(bytes, 4 + id * 4);
        require(offset >= 4 + RFLiArcID_Max * 4);
        auto& arc = loader->archives[id];
        arc.numFiles = word16(bytes, offset);
        arc.biggestSize = word16(bytes, size_t(offset) + 2);
        arc.offset = offset + 4;
        extent(bytes, arc.offset, (size_t(arc.numFiles) + 1) * 4);
        for (unsigned file = 0; file < arc.numFiles; ++file) {
            const auto data = resource(loader, id, file);
            require(data.size() <= arc.biggestSize);
        }
    }
}
u32 RFLPCResourceSize(const RFLiLoader* loader, u32 id, u16 file) { return resource(loader, id, file).size(); }
void* RFLPCLoadResource(void* destination, const RFLiLoader* loader, u32 id, u16 file) {
    const auto bytes = resource(loader, id, file);
    require(destination || bytes.empty());
    if (!bytes.empty()) std::memcpy(destination, bytes.data(), bytes.size());
    return destination;
}
void RFLPCConvertTexture(RFLiTexture* texture, u32 size) {
    std::span<u8> bytes(reinterpret_cast<u8*>(texture), size);
    extent(bytes, 0, sizeof(RFLiTexture));
    const auto width = word16(bytes, 2), height = word16(bytes, 4);
    const auto image = word32(bytes, 28), palette = word32(bytes, 12);
    require(width && height && width <= 1024 && height <= 1024 && image >= 32);
    unsigned tileWidth = 4, tileHeight = 4, tileBytes = 32;
    switch (texture->format) {
    case GX_TF_I4: case GX_TF_C4: case GX_TF_CMPR: tileWidth = tileHeight = 8; break;
    case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: tileWidth = 8; break;
    case GX_TF_RGBA8: tileBytes = 64; break;
    case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_C14X2: break;
    default: require(false);
    }
    extent(bytes, image, size_t((width + tileWidth - 1) / tileWidth) * ((height + tileHeight - 1) / tileHeight) * tileBytes);
    if (texture->indexTexture) extent(bytes, palette, size_t(word16(bytes, 10)) * 2);
    for (size_t offset : {2, 4, 10, 26}) swap16(bytes, offset);
    for (size_t offset : {12, 28}) swap32(bytes, offset);
}
void RFLPCConvertShape(RFLiShapeRes* shape, void* data, u32 size) {
    require(shape && data && shape->part < RFLiPartsShp_Max);
    std::span<u8> bytes(static_cast<u8*>(data), size);
    extent(bytes, 0, 4);
    const std::array<u32, 9> magic{0x6E6F7365, 0x66726864, 0x66616365, 0x68616972, 0x6361705F, 0x62657264, 0x6E736C6E, 0x6D61736B, 0x676C6173};
    require(word32(bytes, 0) == magic[shape->part]);
    size_t offset = 4;
    if (shape->part == RFLiPartsShp_Faceline) {
        extent(bytes, offset, 36);
        for (unsigned i = 0; i < 9; ++i) require(std::isfinite(std::bit_cast<float>(word32(bytes, offset + i * 4))));
        offset += 36;
    }
    const bool skipTexcoords = shape->part == RFLiPartsShp_Forehead || shape->part == RFLiPartsShp_Hair ||
        shape->part == RFLiPartsShp_Beard || shape->part == RFLiPartsShp_Nose;
    std::array<u16, 3> counts{};
    const std::array<u16, 3> capacities{shape->vtxPosBufSize, shape->vtxNrmBufSize, shape->vtxTxcBufSize};
    for (unsigned field = 0; field < (skipTexcoords ? 2u : 3u); ++field) {
        const auto count = counts[field] = word16(bytes, offset);
        require(count <= capacities[field]);
        if (!field && !count) { swap16(bytes, offset); return; }
        offset += 2;
        extent(bytes, offset, size_t(count) * (field == 2 ? 4 : 6));
        offset += size_t(count) * (field == 2 ? 4 : 6);
    }
    extent(bytes, offset, 1);
    const auto primitives = bytes[offset++];
    size_t displayBytes = 0;
    for (unsigned primitive = 0; primitive < primitives; ++primitive) {
        extent(bytes, offset, 2);
        const auto count = bytes[offset++], kind = bytes[offset++];
        require(kind >= GX_QUADS && kind <= GX_POINTS && !(kind & 7));
        const unsigned indices = skipTexcoords ? 2 : 3;
        extent(bytes, offset, size_t(count) * indices);
        for (unsigned vertex = 0; vertex < count; ++vertex)
            for (unsigned field = 0; field < indices; ++field) require(bytes[offset++] < counts[field]);
        displayBytes += 3 + size_t(count) * indices;
    }
    require((displayBytes + 31) / 32 * 32 <= shape->dlBufSize);
    offset = 4;
    if (shape->part == RFLiPartsShp_Faceline) {
        for (unsigned i = 0; i < 9; ++i) swap32(bytes, offset + i * 4);
        offset += 36;
    }
    for (unsigned field = 0; field < (skipTexcoords ? 2u : 3u); ++field) {
        swap16(bytes, offset); offset += 2;
        for (unsigned i = 0; i < counts[field] * (field == 2 ? 2u : 3u); ++i) { swap16(bytes, offset); offset += 2; }
    }
}
}
