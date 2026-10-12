#pragma once
#include <revolution/types.h>
#include <span>
#include <string>
namespace galaxy {
std::string readSequenceReport(std::span<const u8> sequence, size_t& position, u32 (*readRegister)(void*, u8), void* context);
}
