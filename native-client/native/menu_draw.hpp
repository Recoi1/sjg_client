#pragma once
#include <JSystem/JGeometry/TVec.hpp>
#include <cstdint>
namespace galaxy::menu {
void begin();void end();
void text(const wchar_t*,float x,float y,float size=18,uint32_t color=0x505050ff,bool center=true);
void button(const wchar_t*,float x,float y,float width,float height,uint32_t outline,bool selected);
bool pointer(TVec2f&);
}
