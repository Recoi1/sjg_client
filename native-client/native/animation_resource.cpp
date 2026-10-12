#include "animation_resource.hpp"
#include "JSystem/JAudio2/JAUSoundAnimator.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
namespace galaxy {
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }
struct Block {
    std::span<const std::uint8_t> source;
    std::span<std::uint8_t> output;
    void range(std::size_t p, std::size_t n) const {
        if (p > source.size() || n > source.size() - p) throw std::invalid_argument("animation range " + std::to_string(p) + ":" + std::to_string(n) + " exceeds " + std::to_string(source.size()));
    }
    std::uint16_t u16(std::size_t p) const { range(p, 2); return source[p] << 8 | source[p + 1]; }
    std::uint32_t u32(std::size_t p) const { return std::uint32_t(u16(p)) << 16 | u16(p + 2); }
    void words(std::size_t p, std::size_t n) { range(p, n * 2); for (std::size_t i = 0; i < n; ++i) { auto value = u16(p + i * 2); std::memcpy(output.data() + p + i * 2, &value, 2); } }
    void dwords(std::size_t p, std::size_t n) { range(p, n * 4); for (std::size_t i = 0; i < n; ++i) { auto value = u32(p + i * 4); std::memcpy(output.data() + p + i * 4, &value, 4); } }
    std::size_t offset(std::size_t p) { const auto value = u32(p); if (value) range(value, 1); dwords(p, 1); return value; }
    void names(std::size_t p, unsigned expected) {
        if (!p) { require(!expected, "missing animation material names"); return; }
        const auto count = u16(p); require(count == expected, "animation material name count mismatch");
        words(p, 2); range(p + 4, count * 4);
        for (unsigned i = 0; i < count; ++i) {
            auto q = p + 4 + i * 4; const auto name = p + u16(q + 2); range(name, 1);
            require(std::memchr(source.data() + name, 0, source.size() - name), "unterminated animation material name"); words(q, 2);
        }
    }
    void table(std::size_t p, unsigned count, unsigned stride, unsigned channels, bool key, std::span<const unsigned> lengths) {
        range(p, std::size_t(count) * stride); require(!count || p, "missing animation track table");
        for (unsigned i = 0; i < count; ++i) for (unsigned c = 0; c < channels; ++c) {
            auto q = p + std::size_t(i) * stride + c * (key ? 6 : 4);
            const unsigned frames = u16(q), start = u16(q + 2), type = key ? u16(q + 4) : 0;
            const auto values = key && frames > 1 ? unsigned(frames) * (type ? 4 : 3) : unsigned(frames);
            const auto size = lengths[c % lengths.size()];
            require(type <= 1 && (key || frames) && start <= size && values <= size - start, "animation track values exceed table"); words(q, key ? 3 : 2);
        }
    }
    void values(std::size_t p, unsigned count, unsigned width) {
        require(!count || p, "missing animation value array"); range(p, std::size_t(count) * width);
        if (width == 2) words(p, count); else if (width == 4) {
            for (unsigned i = 0; i < count; ++i) require(std::isfinite(std::bit_cast<float>(u32(p + i * 4))), "nonfinite animation value");
            dwords(p, count);
        }
    }
};
void transform(Block b, bool key, bool texture) {
    b.range(0, texture ? 96 : 36); const unsigned count = b.u16(12);
    require(!texture || count % 3 == 0, "texture animation track count is not divisible by three");
    std::array<unsigned, 3> lengths{b.u16(14), b.u16(16), b.u16(18)};
    const auto table = b.offset(20);
    const auto scale = b.offset(texture ? 40 : 24), rotation = b.offset(texture ? 44 : 28), translation = b.offset(texture ? 48 : 32);
    require(!key || b.source[9] <= 15, "invalid animation rotation shift");
    b.table(table, texture ? count : count * 3, key ? 18 : 12, 3, key, lengths);
    b.values(scale, lengths[0], 4); b.values(rotation, lengths[1], 2); b.values(translation, lengths[2], 4); b.words(10, 5);
    if (!texture) return;
    const auto materials = count / 3;
    b.values(b.offset(24), materials, 2); b.names(b.offset(28), materials); b.values(b.offset(32), materials, 1); b.values(b.offset(36), materials * 3, 4);
    const unsigned postCount = b.u16(52); require(postCount % 3 == 0, "post texture track count is not divisible by three");
    std::array<unsigned, 3> postLengths{b.u16(54), b.u16(56), b.u16(58)};
    b.table(b.offset(60), postCount, 18, 3, true, postLengths);
    b.values(b.offset(64), postCount / 3, 2); b.names(b.offset(68), postCount / 3);
    b.values(b.offset(72), postCount / 3, 1); b.values(b.offset(76), postCount, 4);
    b.values(b.offset(80), postLengths[0], 4); b.values(b.offset(84), postLengths[1], 2); b.values(b.offset(88), postLengths[2], 4);
    b.words(52, 4); b.dwords(92, 1);
}
void colors(Block b, bool key, bool registers) {
    b.range(0, registers ? 88 : 52);
    const unsigned groups = registers ? 2 : 1;
    for (unsigned group = 0; group < groups; ++group) {
        const unsigned count = b.u16(registers ? 12 + group * 2 : 14);
        std::array<unsigned, 4> lengths; for (unsigned c = 0; c < 4; ++c) lengths[c] = b.u16(16 + group * 8 + c * 2);
        const auto table = b.offset(registers ? 32 + group * 4 : 24);
        b.table(table, count, registers ? 28 : key ? 24 : 16, 4, key, lengths);
        if (registers) for (unsigned i = 0; i < count; ++i) require(b.source[table + i * 28 + 24] < 4, "invalid animated TEV register");
        b.values(b.offset(registers ? 40 + group * 4 : 28), count, 2);
        b.names(b.offset(registers ? 48 + group * 4 : 32), count);
        for (unsigned c = 0; c < 4; ++c) b.values(b.offset((registers ? 56 + group * 16 : 36) + c * 4), lengths[c], key ? 2 : 1);
    }
    b.words(registers ? 10 : 12, registers ? 11 : 6);
}
}
std::vector<std::uint8_t> importCameraAnimation(std::span<const std::uint8_t> source) {
    Block input{source, {}}; input.range(0, 32);
    require(!std::memcmp(source.data(), "ANDO", 4), "invalid camera animation magic");
    const bool key = !std::memcmp(source.data() + 4, "CKAN", 4);
    require(key || !std::memcmp(source.data() + 4, "CANM", 4), "invalid camera animation type");
    require(input.u32(8) && input.u32(24), "empty camera animation");
    const auto infoSize = key ? 96u : 64u;
    input.range(32, infoSize);
    const std::size_t values = 32ull + input.u32(28);
    require(values >= 32 + infoSize && values % 4 == 0, "invalid camera value offset");
    const auto bytes = input.u32(values);
    require(bytes % 4 == 0, "invalid camera value size");
    input.range(values + 4, bytes);
    for (unsigned i = 0; i < 8; ++i) {
        const auto entry = 32 + i * (key ? 12 : 8);
        const auto count = input.u32(entry), offset = input.u32(entry + 4);
        const auto type = key ? input.u32(entry + 8) : 0;
        const unsigned stride = key && count > 1 ? (type ? 4 : 3) : 1;
        require(count && type <= 1 && offset <= bytes / 4 && count <= (bytes / 4 - offset) / stride,
                "camera track exceeds value array");
        if (key && count > 1) for (unsigned n = 1; n < count; ++n)
            require(std::bit_cast<float>(input.u32(values + 4 + (offset + n * stride) * 4)) >
                    std::bit_cast<float>(input.u32(values + 4 + (offset + (n - 1) * stride) * 4)),
                    "camera key times are not increasing");
    }
    std::vector<std::uint8_t> output(source.begin(), source.end());
    Block block{source, output};
    block.dwords(8, 6); block.dwords(32, infoSize / 4); block.dwords(values, 1);
    block.values(values + 4, bytes / 4, 4);
    return output;
}
std::vector<std::uint8_t> importSoundAnimation(std::span<const std::uint8_t> source) {
    Block input{source, {}}; input.range(0, 8);
    const auto count = input.u16(0);
    require(input.u32(4) == 0, "serialized sound animation has a runtime controller");
    input.range(8, std::size_t(count) * 32);
    constexpr auto header = offsetof(JAUSoundAnimation, mSounds);
    static_assert(sizeof(JAUSoundAnimationSound) == 32);
    std::vector<std::uint8_t> result(header + std::size_t(count) * 32);
    memcpy(result.data(), &count, sizeof(count));
    for (unsigned i = 0; i < count; ++i) {
        const auto record = source.subspan(8 + std::size_t(i) * 32, 32);
        auto output = std::span(result).subspan(header + std::size_t(i) * 32, 32);
        std::copy(record.begin(), record.end(), output.begin()); Block sound{record, output};
        for (unsigned p : {4u, 8u, 12u}) require(std::isfinite(std::bit_cast<float>(sound.u32(p))), "nonfinite sound animation parameter");
        sound.dwords(0, 5); sound.dwords(28, 1);
    }
    return result;
}
std::vector<std::uint8_t> importAnimation(std::span<const std::uint8_t> source) {
    std::vector<std::uint8_t> result(source.begin(), source.end()); Block file{source, result}; file.range(0, 32);
    require(file.u32(0) == 0x4a334431 && file.u32(8) == source.size(), "invalid animation file header");
    const auto type = file.u32(4); std::uint32_t expected = 0;
    switch (type) {
    case 0x62636b31: expected = 0x414e4b31; break; case 0x62636131: expected = 0x414e4631; break;
    case 0x62746b31: expected = 0x54544b31; break; case 0x62706b31: expected = 0x50414b31; break;
    case 0x62706131: expected = 0x50414631; break; case 0x62726b31: expected = 0x54524b31; break;
    case 0x62747031: expected = 0x54505431; break; case 0x62766131: expected = 0x56414631; break;
    case 0x626c6b31: expected = 0x434c4b31; break; case 0x626c6131: expected = 0x434c4631; break;
    default: throw std::invalid_argument("unsupported animation file type");
    }
    require(file.u32(12) == 1, "animation requires one matching data block");
    file.range(32, 8); const std::size_t size = file.u32(36), extent = source.size() - 32;
    // Retail exporters disagree by alignment padding; referenced data is bounded by the archive entry.
    require(size >= 8 && (size > extent ? size - extent : extent - size) < 32 && file.u32(32) == expected, "invalid animation block extent or kind");
    Block b{source.subspan(32), std::span(result).subspan(32)};
    switch (expected) {
    case 0x414e4b31: case 0x414e4631: case 0x54544b31: transform(b, expected != 0x414e4631, expected == 0x54544b31); break;
    case 0x50414b31: case 0x50414631: case 0x54524b31: colors(b, expected != 0x50414631, expected == 0x54524b31); break;
    default: {
        b.range(0, 24); const unsigned count = b.u16(12), length = b.u16(14); const auto table = b.offset(16), values = b.offset(20);
        const bool key = expected == 0x434c4b31, pattern = expected == 0x54505431, cluster = key || expected == 0x434c4631;
        const std::array<unsigned, 1> lengths{length}; b.table(table, count, key ? 6 : pattern ? 8 : 4, 1, key, lengths);
        b.values(values, length, cluster ? 4 : pattern ? 2 : 1);
        if (pattern) { b.range(0, 32); b.values(b.offset(24), count, 2); b.names(b.offset(28), count); for (unsigned i = 0; i < count; ++i) { require(b.source[table + i * 8 + 4] < 8, "invalid animated texture slot"); b.words(table + i * 8 + 6, 1); } }
        b.words(10, 3); break;
    }
    }
    require(std::int16_t(b.u16(expected == 0x50414b31 || expected == 0x50414631 ? 12 : 10)) >= 0 && b.source[8] <= 4, "invalid animation duration or loop mode");
    b.dwords(0, 2); file.dwords(0, 4); file.dwords(28, 1); return result;
}
}
