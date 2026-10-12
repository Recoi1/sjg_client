#include "nw4r/ut/ResFont.h"
#include "JSystem/JKernel/JKRFileLoader.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace nw4r::ut {
struct NativeFont {
    FontInformation info{};
    FontTextureGlyph glyph{};
    struct Block { u32 offset, kind, next; void* data; };
    std::vector<Block> blocks;
    std::vector<std::unique_ptr<u8[]>> owned;
    template<class T> T* allocate(size_t size) {
        auto bytes = std::make_unique<u8[]>(size);
        T* result = reinterpret_cast<T*>(bytes.get());
        owned.push_back(std::move(bytes));
        return result;
    }
};
namespace {
constexpr u32 tag(char a, char b, char c, char d) { return u32(a) << 24 | u32(b) << 16 | u32(c) << 8 | u32(d); }
struct Reader {
    std::span<const u8> bytes;
    void range(size_t offset, size_t size) const {
        if (offset > bytes.size() || size > bytes.size() - offset) throw std::invalid_argument("truncated RFNT resource");
    }
    u16 u16be(size_t offset) const { range(offset, 2); return u16(bytes[offset]) << 8 | bytes[offset + 1]; }
    u32 u32be(size_t offset) const { return u32(u16be(offset)) << 16 | u16be(offset + 2); }
};
std::unique_ptr<NativeFont> parse(std::span<const u8> bytes) {
    Reader r{bytes}; r.range(0, 16);
    if (r.u32be(0) != tag('R','F','N','T') || r.u16be(4) != 0xfeff ||
        (r.u16be(6) != 0x102 && r.u16be(6) != 0x104) || r.u32be(8) != bytes.size())
        throw std::invalid_argument("unsupported RFNT header");
    const u16 count = r.u16be(14);
    size_t offset = r.u16be(12);
    if (offset < 16 || !count) throw std::invalid_argument("invalid RFNT block table");
    auto result = std::make_unique<NativeFont>();
    u32 glyphOffset = 0, widthOffset = 0, mapOffset = 0;
    bool haveInfo = false, haveGlyph = false;
    for (u16 i = 0; i < count; ++i) {
        r.range(offset, 8);
        const u32 kind = r.u32be(offset), size = r.u32be(offset + 4);
        if (size < 8) throw std::invalid_argument("invalid RFNT block size");
        r.range(offset, size);
        const size_t data = offset + 8, end = offset + size;
        auto within = [&](size_t length) { if (length > size - 8) throw std::invalid_argument("short RFNT block"); };
        if (kind == tag('F','I','N','F')) {
            if (haveInfo) throw std::invalid_argument("duplicate RFNT information");
            within(20); haveInfo = true;
            auto& info = result->info;
            info.fontType = bytes[data]; info.linefeed = s8(bytes[data + 1]); info.alterCharIndex = r.u16be(data + 2);
            info.defaultWidth = {s8(bytes[data + 4]), bytes[data + 5], s8(bytes[data + 6])}; info.encoding = bytes[data + 7];
            glyphOffset = r.u32be(data + 8); widthOffset = r.u32be(data + 12); mapOffset = r.u32be(data + 16);
            if (size >= 32) { info.height = bytes[data + 20]; info.width = bytes[data + 21]; info.ascent = bytes[data + 22]; }
            if (info.encoding > 3) throw std::invalid_argument("invalid RFNT encoding");
        } else if (kind == tag('T','G','L','P')) {
            if (haveGlyph) throw std::invalid_argument("duplicate RFNT glyphs");
            within(24); haveGlyph = true;
            auto& glyph = result->glyph;
            glyph.cellWidth = bytes[data]; glyph.cellHeight = bytes[data + 1]; glyph.baselinePos = s8(bytes[data + 2]); glyph.maxCharWidth = bytes[data + 3];
            glyph.sheetSize = r.u32be(data + 4); glyph.sheetNum = r.u16be(data + 8); glyph.sheetFormat = r.u16be(data + 10);
            glyph.sheetRow = r.u16be(data + 12); glyph.sheetLine = r.u16be(data + 14); glyph.sheetWidth = r.u16be(data + 16); glyph.sheetHeight = r.u16be(data + 18);
            const u32 image = r.u32be(data + 20);
            if (!glyph.sheetNum || !glyph.sheetRow || !glyph.sheetLine || !glyph.sheetSize || !glyph.sheetWidth || !glyph.sheetHeight)
                throw std::invalid_argument("empty RFNT sheet");
            if ((uint64_t(glyph.cellWidth) + 1) * glyph.sheetRow > glyph.sheetWidth ||
                (uint64_t(glyph.cellHeight) + 1) * glyph.sheetLine > glyph.sheetHeight)
                throw std::invalid_argument("RFNT cells exceed sheet dimensions");
            r.range(image, uint64_t(glyph.sheetSize) * glyph.sheetNum);
            if (image < data + 24 || image + uint64_t(glyph.sheetSize) * glyph.sheetNum > end)
                throw std::invalid_argument("RFNT sheet outside glyph block");
            // Texture pixels keep their original GX byte order; Aurora imports that format directly.
            glyph.sheetImage = const_cast<u8*>(bytes.data() + image);
            result->blocks.push_back({u32(data), kind, 0, &glyph});
        } else if (kind == tag('C','W','D','H')) {
            within(8);
            const u16 first = r.u16be(data), last = r.u16be(data + 2);
            if (last < first) throw std::invalid_argument("invalid RFNT width range");
            const size_t entries = size_t(last) - first + 1;
            within(8 + entries * 3);
            auto* width = result->allocate<FontWidth>(offsetof(FontWidth, widthTable) + entries * sizeof(CharWidths));
            width->indexBegin = first; width->indexEnd = last; width->pNext = nullptr;
            std::memcpy(width->widthTable, bytes.data() + data + 8, entries * sizeof(CharWidths));
            result->blocks.push_back({u32(data), kind, r.u32be(data + 4), width});
        } else if (kind == tag('C','M','A','P')) {
            within(14);
            const u16 first = r.u16be(data), last = r.u16be(data + 2), method = r.u16be(data + 4);
            if (last < first || method > 2) throw std::invalid_argument("invalid RFNT map range");
            const size_t entries = method == 0 ? 1 : method == 1 ? size_t(last) - first + 1 : 1 + size_t(r.u16be(data + 12)) * 2;
            within(12 + entries * 2);
            auto* map = result->allocate<FontCodeMap>(offsetof(FontCodeMap, mapInfo) + entries * sizeof(u16));
            map->ccodeBegin = first; map->ccodeEnd = last; map->mappingMethod = method; map->reserved = 0; map->pNext = nullptr;
            for (size_t j = 0; j < entries; ++j) map->mapInfo[j] = r.u16be(data + 12 + j * 2);
            if (method == 2) for (u32 j = 0; j < map->mapInfo[0]; ++j) {
                const u16 code = map->mapInfo[1 + j * 2];
                if (code < first || code > last || (j && code <= map->mapInfo[1 + (j - 1) * 2]))
                    throw std::invalid_argument("unsorted RFNT scan map");
            }
            result->blocks.push_back({u32(data), kind, r.u32be(data + 8), map});
        } else if (kind != tag('G','L','G','R')) throw std::invalid_argument("unknown RFNT block");
        offset = end;
    }
    if (!haveInfo || !haveGlyph || offset != bytes.size()) throw std::invalid_argument("incomplete RFNT file");
    auto resolve = [&](u32 address, u32 kind) -> void* {
        if (!address) return nullptr;
        auto it = std::find_if(result->blocks.begin(), result->blocks.end(), [&](const auto& b) { return b.offset == address && b.kind == kind; });
        if (it == result->blocks.end()) throw std::invalid_argument("invalid RFNT block link");
        return it->data;
    };
    result->info.pGlyph = static_cast<FontTextureGlyph*>(resolve(glyphOffset, tag('T','G','L','P')));
    result->info.pWidth = static_cast<FontWidth*>(resolve(widthOffset, tag('C','W','D','H')));
    result->info.pMap = static_cast<FontCodeMap*>(resolve(mapOffset, tag('C','M','A','P')));
    if (!result->info.pGlyph) throw std::invalid_argument("missing RFNT glyph link");
    const u32 glyphCount = u32(result->glyph.sheetNum) * result->glyph.sheetRow * result->glyph.sheetLine;
    auto validIndex = [&](u32 index) { if (index != 0xffff && index >= glyphCount) throw std::invalid_argument("RFNT glyph index out of range"); };
    if (result->info.alterCharIndex == 0xffff) throw std::invalid_argument("missing RFNT alternate glyph");
    validIndex(result->info.alterCharIndex);
    for (auto& block : result->blocks) {
        if (block.kind == tag('C','W','D','H')) {
            auto* width = static_cast<FontWidth*>(block.data); validIndex(width->indexEnd);
            width->pNext = static_cast<FontWidth*>(resolve(block.next, block.kind));
        } else if (block.kind == tag('C','M','A','P')) {
            auto* map = static_cast<FontCodeMap*>(block.data);
            map->pNext = static_cast<FontCodeMap*>(resolve(block.next, block.kind));
            if (map->mappingMethod == 0) { validIndex(map->mapInfo[0]); validIndex(u32(map->mapInfo[0]) + map->ccodeEnd - map->ccodeBegin); }
            else if (map->mappingMethod == 1) for (u32 i = 0; i <= u32(map->ccodeEnd) - map->ccodeBegin; ++i) validIndex(map->mapInfo[i]);
            else for (u32 i = 0; i < map->mapInfo[0]; ++i) validIndex(map->mapInfo[2 + i * 2]);
        }
    }
    auto checkChain = [&](auto* first) {
        size_t visited = 0;
        for (auto* node = first; node; node = node->pNext) if (++visited > result->blocks.size()) throw std::invalid_argument("cyclic RFNT chain");
    };
    // Validate every chain, including blocks not selected by FINF.
    for (auto& block : result->blocks) {
        if (block.kind == tag('C','W','D','H')) checkChain(static_cast<FontWidth*>(block.data));
        if (block.kind == tag('C','M','A','P')) checkChain(static_cast<FontCodeMap*>(block.data));
    }
    if (r.u16be(6) == 0x102) { result->info.height = result->glyph.cellHeight; result->info.width = result->glyph.cellWidth; result->info.ascent = result->glyph.baselinePos; }
    return result;
}
}
ResFont::ResFont() : mNativeFont(nullptr) {}
ResFont::~ResFont() { RemoveResource(); }
bool ResFont::SetResource(void* buffer) {
    const s32 size = JKRFileLoader::getGlbResourceSize(buffer);
    return size > 0 && SetResource(buffer, size);
}
bool ResFont::SetResource(void* buffer, size_t size) {
    if (!buffer || !IsManaging(nullptr)) return false;
    try {
        auto font = parse({static_cast<u8*>(buffer), size});
        SetResourceBuffer(buffer, &font->info); InitReaderFunc(static_cast<FontEncoding>(font->info.encoding));
        mNativeFont = font.release(); return true;
    } catch (const std::invalid_argument&) { return false; }
}
void ResFont::RemoveResource() { RemoveResourceBuffer(); delete mNativeFont; mNativeFont = nullptr; }
}
