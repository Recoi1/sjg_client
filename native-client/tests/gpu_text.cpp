#include "nw4r/ut/ResFont.h"
#include "nw4r/ut/TextWriterBase.h"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "Game/System/ErrorArchive.hpp"
#include "yaz0.hpp"
#include <revolution/mtx.h>
#include <cassert>
#include <stdexcept>

extern "C" void verifyGalaxyText() {
    JKRMemArchive archive;
    assert(archive.mountFixed(const_cast<u8*>(cErrorArchive), sizeof(cErrorArchive), JKR_MEM_BREAK_FLAG_0));
    auto* packed = static_cast<u8*>(archive.getResource("/LayoutData/EmbeddedFont.arc"));
    assert(packed);
    auto bytes = galaxy::decodeYaz0({packed, u32(archive.getResSize(packed))});
    JKRMemArchive fonts;
    assert(fonts.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0));
    nw4r::ut::ResFont font;
    assert(font.SetResource(fonts.getResource("MessageFont26.brfnt")));
    font.InitReaderFunc(nw4r::ut::FONT_ENCODING_CP1252);
    Mtx44 projection;
    C_MTXOrtho(projection, 0, 480, 0, 640, 0, 1);
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    Mtx identity = {{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    GXLoadPosMtxImm(identity, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    nw4r::ut::TextWriterBase<char> writer;
    writer.SetFont(font);
    writer.SetDrawFlag(writer.VERTICAL_ORIGIN_BASELINE);
    writer.mIsWidthFixed = true;
    writer.mFixedWidth = 10;
    writer.SetCharSpace(1);
    writer.SetCursor(0, 0);
    writer.SetupGX();
    const char noTerminator[] = {'A', 'B'};
    assert(writer.Print(noTerminator, 2) == 21 && writer.GetCursorX() == 21 && writer.GetCursorY() == 0);
    writer.SetCursor(0, 0);
    assert(writer.Print("ABC\nD", 5) == 32);
    assert(writer.GetCursorX() == 10 && writer.GetCursorY() == writer.GetLineHeight());
    writer.SetCursor(0, 0);
    writer.SetWidthLimit(15);
    assert(writer.Print("ABC", 3) == 10);
    assert(writer.GetCursorX() == 10 && writer.GetCursorY() == 2 * writer.GetLineHeight());
    writer.SetCursor(7, 9);
    assert(writer.Print("", 0) == 0 && writer.GetCursorX() == 7 && writer.GetCursorY() == 9);
    bool rejected = false;
    try { writer.Print("A", -1); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    GXDrawDone();
}
