#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace galaxy {
std::vector<std::uint8_t> importAudioArchive(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> importWaveBank(std::span<const std::uint8_t> bytes);
}
