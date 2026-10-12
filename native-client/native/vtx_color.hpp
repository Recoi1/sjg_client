#pragma once
#include <revolution/types.h>
#include <span>
class J3DAnmVtxColorFull;
class J3DAnmVtxColorKey;
namespace galaxy {
struct NativeVtxColor;
void releaseVtxColor(NativeVtxColor*);
void loadVtxColor(J3DAnmVtxColorFull&, std::span<const u8> block);
void loadVtxColor(J3DAnmVtxColorKey&, std::span<const u8> block);
std::span<const u8> knownVtxColorBlock(const void* data);
}
