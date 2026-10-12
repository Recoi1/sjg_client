#pragma once
#include <revolution/types.h>
#include <span>
namespace galaxy {
void bootDspMixer(void (*callback)(void*));
void beginDspFrame(u32 count, std::span<s16> left, std::span<s16> right);
void releaseDspVoices(u32 group);
void setDspLevel(f32 level);
}
