#pragma once
#include "JSystem/JAudio2/JASDSPInterface.hpp"
#include <span>
namespace galaxy {
void setupDspMixer(std::span<JASDsp::TChannel>, std::span<JASDsp::FxBuf, 4>, std::span<const u32, 320>, std::span<const u8, 64>);
void mixDspSubFrame(std::span<s16, 80> left, std::span<s16, 80> right, u16 level);
}
