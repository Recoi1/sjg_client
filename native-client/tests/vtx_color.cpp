#include "vtx_color.hpp"
#include "JSystem/J3DGraphAnimator/J3DAnimation.hpp"
#include <array>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <vector>
namespace {
void put16(std::vector<u8>& bytes, size_t offset, u16 value) { bytes[offset] = value >> 8; bytes[offset + 1] = value; }
void put32(std::vector<u8>& bytes, size_t offset, u32 value) { put16(bytes, offset, value >> 16); put16(bytes, offset + 2, value); }
std::vector<u8> fixture(bool key) {
    std::vector<u8> bytes(key ? 152 : 112);
    put32(bytes, 0, key ? 0x56434b31 : 0x56434631); put32(bytes, 4, bytes.size());
    bytes[8] = 2; put16(bytes, 10, 2); put16(bytes, 12, 1);
    const u32 index = key ? 88 : 80, list = key ? 96 : 88, colors = key ? 104 : 96;
    put32(bytes, 24, 64); put32(bytes, 32, index); put32(bytes, 40, list);
    put16(bytes, index, 2); put16(bytes, list, 100); put16(bytes, list + 2, 200);
    for (unsigned channel = 0; channel < 4; ++channel) {
        put16(bytes, 16 + channel * 2, key ? 6 : 2);
        put32(bytes, 48 + channel * 4, colors + channel * (key ? 12 : 4));
        const u32 entry = 64 + channel * (key ? 6 : 4);
        put16(bytes, entry, 2);
        if (key) {
            put16(bytes, colors + channel * 12, 0); put16(bytes, colors + channel * 12 + 2, 10 + channel);
            put16(bytes, colors + channel * 12 + 6, 2); put16(bytes, colors + channel * 12 + 8, 30 + channel);
        } else {
            bytes[colors + channel * 4] = 10 + channel; bytes[colors + channel * 4 + 1] = 30 + channel;
        }
    }
    return bytes;
}
}
int main() {
    auto fullBytes = fixture(false);
    J3DAnmVtxColorFull full;
    galaxy::loadVtxColor(full, fullBytes);
    GXColor color{};
    full.mFrame = 0; full.getColor(0, 0, &color); assert(color.r == 10 && color.g == 11 && color.b == 12 && color.a == 13);
    full.mFrame = 1; full.getColor(0, 0, &color); assert(color.r == 30 && color.a == 33);
    full.mFrame = 100; full.getColor(0, 0, &color); assert(color.r == 30);
    const auto* indices = static_cast<const u16*>(full.mAnmVtxColorIndexData[0][0].mpData);
    assert(full.mAnmVtxColorIndexData[0][0].mNum == 2 && indices[0] == 100 && indices[1] == 200);
    const auto* previous = full.mNativeVtxColor;
    for (size_t size = 0; size < fullBytes.size(); ++size) {
        bool caught = false;
        try { galaxy::loadVtxColor(full, std::span<const u8>(fullBytes).first(size)); } catch (const std::invalid_argument&) { caught = true; }
        assert(caught && full.mNativeVtxColor == previous);
    }
    auto bad = fullBytes; put32(bad, 84, UINT32_MAX);
    bool caught = false; try { galaxy::loadVtxColor(full, bad); } catch (const std::invalid_argument&) { caught = true; } assert(caught);
    fullBytes.assign(fullBytes.size(), 0); full.getColor(0, 0, &color); assert(color.r == 30);
    auto keyBytes = fixture(true);
    J3DAnmVtxColorKey key;
    galaxy::loadVtxColor(key, keyBytes);
    key.mFrame = 1; key.getColor(0, 0, &color); assert(color.r == 20 && color.a == 23);
    key.mFrame = -1; key.getColor(0, 0, &color); assert(color.r == 10);
    key.mFrame = 20; key.getColor(0, 0, &color); assert(color.r == 30);
    keyBytes.assign(keyBytes.size(), 0); key.getColor(0, 0, &color); assert(color.r == 30);
}
