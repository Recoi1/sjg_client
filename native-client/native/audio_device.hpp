#pragma once
#include <revolution/types.h>
#include <span>
namespace galaxy {
void checkAudioDevice();
void shutdownAudioDevice();
bool applyAudioOutput();
void sendSpeakerPcm(s32 channel, std::span<const s16> samples);
}
