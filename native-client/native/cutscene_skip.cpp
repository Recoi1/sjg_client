#include "cutscene_skip.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <nw4r/ut/TextWriterBase.h>

namespace galaxy {
void drawCutsceneSkipPrompt() {
    const float progress = cutsceneSkipProgress();
    if (progress < 0) return;
    const float width = MR::getScreenWidth(), height = MR::getScreenHeight();
    const float x = width - 44, y = height - 48;
    TDDraw::setup(0, 1, 2);
    Mtx44 projection; C_MTXOrtho(projection, 0, height, 0, width, -1, 1);
    Mtx identity{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}};
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    GXLoadPosMtxImm(identity, GX_PNMTX0); GXSetCurrentMtx(GX_PNMTX0);
    TDDraw::drawFillBox(TVec2f(x-17, y+19), TVec2f(x+17, y+24), 0x202020ff);
    TDDraw::drawFillBox(TVec2f(x-15, y+20), TVec2f(x+15, y+23), 0xffffff80);
    if (progress > 0)
        TDDraw::drawFillBox(TVec2f(x-15, y+20), TVec2f(x-15+30*progress, y+23), 0xffcf40ff);
    nw4r::ut::TextWriterBase<wchar_t> writer;
    writer.SetFont(*MR::getPictureFontNW4R()); writer.SetFontSize(36, 36);
    writer.SetCursor(x-writer.CalcStringWidth(L"1", 1)*0.5f, y-18);
    writer.SetupGX(); writer.Print(L"1", 1);
    writer.SetFont(*MR::getMenuFontNW4R()); writer.SetFontSize(12, 12);
    const float labelX = x-writer.CalcStringWidth(L"Hold to skip", 12)*0.5f;
    writer.SetTextColor(0x202020ff); writer.SetCursor(labelX+1, y+28);
    writer.SetupGX(); writer.Print(L"Hold to skip", 12);
    writer.SetTextColor(0xffffffff); writer.SetCursor(labelX, y+27);
    writer.SetupGX(); writer.Print(L"Hold to skip", 12);
    TDDraw::close();
}
}
