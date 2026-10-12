#include "tpl.hpp"
#include <cassert>
#include <stdexcept>
#include <vector>
namespace {
void put32(std::vector<u8>& bytes, size_t offset, u32 value) { for (int i = 0; i < 4; ++i) bytes[offset + i] = value >> (24 - 8 * i); }
}
int main() {
    std::vector<u8> bytes(128);
    put32(bytes, 0, 0x20af30); put32(bytes, 4, 1); put32(bytes, 8, 12); put32(bytes, 12, 20);
    put32(bytes, 20, 0x00040004); put32(bytes, 24, GX_TF_RGBA8); put32(bytes, 28, 64);
    put32(bytes, 40, GX_LINEAR); put32(bytes, 44, GX_LINEAR);
    auto image = galaxy::readTpl(bytes, 0);
    assert(image.texture.width == 4 && image.texture.height == 4 && image.texture.data == reinterpret_cast<char*>(bytes.data() + 64));
    assert(!image.hasPalette);
    auto rejected = [](std::span<const u8> bytes, u32 index = 0) {
        bool caught = false; try { galaxy::readTpl(bytes, index); } catch (const std::invalid_argument&) { caught = true; } assert(caught);
    };
    for (size_t length = 0; length < bytes.size(); ++length) rejected(std::span<const u8>(bytes).first(length));
    rejected(bytes, 1);
    auto bad = bytes; put32(bad, 28, 0xfffffff0); rejected(bad);
    bad = bytes; put32(bad, 24, GX_TF_C8); rejected(bad);
    put32(bytes, 16, 56); bytes[57] = 16; put32(bytes, 60, GX_TL_RGB565); put32(bytes, 64, 96);
    put32(bytes, 24, GX_TF_C4); put32(bytes, 28, 64);
    image = galaxy::readTpl(bytes, 0); assert(image.hasPalette && image.palette.numEntries == 16);
}
