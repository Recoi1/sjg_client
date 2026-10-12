#pragma once
#include <revolution/types.h>
#include <span>
namespace galaxy {
void decompress(std::span<const u8> source, std::span<u8> destination, u32 skip = 0);
}
