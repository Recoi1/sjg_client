#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace galaxy {
std::vector<std::uint8_t> importBmg(std::span<const std::uint8_t> source);
}
