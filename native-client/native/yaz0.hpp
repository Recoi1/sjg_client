#pragma once
#include <revolution/types.h>
#include <span>
#include <vector>

namespace galaxy {
u32 yaz0ExpandedSize(std::span<const u8> source);
std::vector<u8> decodeYaz0(std::span<const u8> source);
}
