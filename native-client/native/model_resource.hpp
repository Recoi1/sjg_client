#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace galaxy {
std::vector<std::uint8_t> importTexture(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> importModel(std::span<const std::uint8_t> bytes);
}
