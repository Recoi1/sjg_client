#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace galaxy {
std::vector<std::uint8_t> importAnimation(std::span<const std::uint8_t> source);
std::vector<std::uint8_t> importSoundAnimation(std::span<const std::uint8_t> source);
std::vector<std::uint8_t> importCameraAnimation(std::span<const std::uint8_t> source);
}
