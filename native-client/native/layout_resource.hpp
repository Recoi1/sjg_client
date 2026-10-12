#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace galaxy {
std::vector<std::uint8_t> importLayout(std::span<const std::uint8_t> bytes);
}
