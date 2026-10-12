#pragma once
#include <revolution/tpl.h>
#include <span>
namespace galaxy {
struct TplImage { TPLHeader texture{}; TPLClutHeader palette{}; bool hasPalette = false; };
TplImage readTpl(std::span<const u8> bytes, u32 index);
}
