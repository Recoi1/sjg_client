#include "nw4r/ut/ResFont.h"
#include "yaz0.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "Game/System/ErrorArchive.hpp"
#include <cassert>
#include <cstring>
#include <vector>
#include <array>
#include <stdexcept>

int main() {
    const auto checkReader = [](nw4r::ut::CharStrmReader::ReadNextCharFunc read,
                                const void* data, size_t size, u16 expected) {
        nw4r::ut::CharStrmReader reader(read);
        reader.mCharStrm = data;
        u16 code = 0xffff;
        const auto* end = static_cast<const u8*>(data) + size;
        assert(reader.NextBounded(end, &code) && code == expected);
        assert(reader.GetCurrentPos() == end && !reader.NextBounded(end, &code));
        for (size_t prefix = 1; prefix < size; ++prefix) {
            reader.mCharStrm = data; code = 0xffff;
            bool rejected = false;
            try { reader.NextBounded(static_cast<const u8*>(data) + prefix, &code); }
            catch (const std::invalid_argument&) { rejected = true; }
            assert(rejected && reader.GetCurrentPos() == data && code == 0xffff);
        }
    };
    const u8 utf8[] = {0xe2, 0x82, 0xac}, sjis[] = {0x82, 0xa0}, ascii[] = {'A'};
    const u16 utf16[] = {0x1234};
    checkReader(&nw4r::ut::CharStrmReader::ReadNextCharUTF8, utf8, sizeof(utf8), 0x20ac);
    checkReader(&nw4r::ut::CharStrmReader::ReadNextCharSJIS, sjis, sizeof(sjis), 0x82a0);
    checkReader(&nw4r::ut::CharStrmReader::ReadNextCharCP1252, ascii, sizeof(ascii), 'A');
    checkReader(&nw4r::ut::CharStrmReader::ReadNextCharUTF16, utf16, sizeof(utf16), 0x1234);
    for (const auto bytes : {std::array<u8, 3>{0xe2, 0x41, 0xac}, {0xe0, 0x80, 0x80}, {0xed, 0xa0, 0x80}}) {
        nw4r::ut::CharStrmReader reader(&nw4r::ut::CharStrmReader::ReadNextCharUTF8);
        reader.mCharStrm = bytes.data();
        u16 code = 0xffff;
        bool rejected = false;
        try { reader.NextBounded(bytes.data() + bytes.size(), &code); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected && reader.GetCurrentPos() == bytes.data() && code == 0xffff);
    }
    alignas(nw4r::ut::FontCodeMap) std::array<u8, offsetof(nw4r::ut::FontCodeMap, mapInfo) + 10> scanStorage{};
    auto* scan = reinterpret_cast<nw4r::ut::FontCodeMap*>(scanStorage.data());
    scan->mappingMethod = 2;
    scan->mapInfo[0] = 2; scan->mapInfo[1] = 20; scan->mapInfo[2] = 4; scan->mapInfo[3] = 40; scan->mapInfo[4] = 9;
    nw4r::ut::detail::ResFontBase query;
    assert(query.FindGlyphIndex(scan, 20) == 4 && query.FindGlyphIndex(scan, 40) == 9);
    assert(query.FindGlyphIndex(scan, 0) == 0xffff && query.FindGlyphIndex(scan, 30) == 0xffff && query.FindGlyphIndex(scan, 50) == 0xffff);
    scan->mapInfo[0] = 0;
    assert(query.FindGlyphIndex(scan, 20) == 0xffff);
    JKRMemArchive archive;
    assert(archive.mountFixed(const_cast<u8*>(cErrorArchive), sizeof(cErrorArchive), JKR_MEM_BREAK_FLAG_0));
    auto* packed = static_cast<u8*>(archive.getResource("/LayoutData/EmbeddedFont.arc"));
    assert(packed);
    auto decoded = galaxy::decodeYaz0({packed, u32(archive.getResSize(packed))});
    JKRMemArchive fontArchive;
    assert(fontArchive.mountFixed(decoded.data(), decoded.size(), JKR_MEM_BREAK_FLAG_0));
    auto* bytes = static_cast<u8*>(fontArchive.getResource("MessageFont26.brfnt"));
    const s32 size = fontArchive.getResSize(bytes);
    assert(bytes && size > 16);
    std::vector<u8> original(bytes, bytes + size);
    {
        nw4r::ut::ResFont font;
        assert(font.SetResource(bytes));
        assert(font.GetWidth() > 0 && font.GetHeight() > 0 && font.GetCellHeight() > 0);
        unsigned first = 0;
        while (first < 65536 && !font.HasGlyph(first)) ++first;
        assert(first < 65536);
        nw4r::ut::Glyph glyph;
        font.GetGlyph(&glyph, first);
        assert(glyph.pTexture >= bytes && glyph.pTexture < bytes + size);
        assert(glyph.texWidth > 0 && glyph.texHeight > 0);
        assert(glyph.cellX < glyph.texWidth && glyph.cellY < glyph.texHeight);
        assert(!font.SetResource(bytes));
        assert(std::memcmp(bytes, original.data(), size) == 0);
        font.RemoveResource(); assert(font.IsManaging(nullptr));
        assert(font.SetResource(bytes));
        font.GetGlyph(&glyph, 0xffff); assert(glyph.pTexture);
    }
    for (size_t length = 0; length < original.size(); ++length) {
        nw4r::ut::ResFont font;
        assert(!font.SetResource(original.data(), length));
        assert(font.IsManaging(nullptr));
    }
    auto corrupt = original; corrupt[4] = 0;
    nw4r::ut::ResFont font; assert(!font.SetResource(corrupt.data(), corrupt.size()));
    corrupt = original;
    // FINF's glyph link must target a TGLP payload, never arbitrary bytes.
    corrupt[32] = 0xff; corrupt[33] = 0xff; corrupt[34] = 0xff; corrupt[35] = 0xff;
    assert(!font.SetResource(corrupt.data(), corrupt.size()));
}
