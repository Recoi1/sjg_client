#include "particle_resource.hpp"
#include "model_resource.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
namespace galaxy {
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }
struct Block {
    std::span<const std::uint8_t> source;
    std::span<std::uint8_t> output;
    void range(std::size_t p, std::size_t n) const {
        if (p > source.size() || n > source.size() - p) throw std::invalid_argument("particle range " + std::to_string(p) + ":" + std::to_string(n) + " exceeds " + std::to_string(source.size()));
    }
    unsigned byte(std::size_t p) const { range(p, 1); return source[p]; }
    std::uint16_t u16(std::size_t p) const { range(p, 2); return source[p] << 8 | source[p + 1]; }
    std::uint32_t u32(std::size_t p) const { return std::uint32_t(u16(p)) << 16 | u16(p + 2); }
    void words(std::size_t p, unsigned n) { range(p, n * 2); for (unsigned i = 0; i < n; ++i) { const auto v = u16(p + i * 2); std::memcpy(output.data() + p + i * 2, &v, 2); } }
    void dwords(std::size_t p, unsigned n) { range(p, n * 4); for (unsigned i = 0; i < n; ++i) { const auto v = u32(p + i * 4); std::memcpy(output.data() + p + i * 4, &v, 4); } }
    float number(std::size_t p) const { const auto v = std::bit_cast<float>(u32(p)); require(std::isfinite(v), "nonfinite particle value"); return v; }
    void floats(std::size_t p, unsigned n) { range(p, n * 4); for (unsigned i = 0; i < n; ++i) number(p + i * 4); dwords(p, n); }
    Block sub(std::size_t p, std::size_t n) const { range(p, n); return {source.subspan(p, n), output.subspan(p, n)}; }
};
void shapeFlags(unsigned flags) {
    const auto type = flags & 15;
    require(type <= 10 && ((flags >> 7) & 7) < 5 && ((type < 3 || type > 6) || ((flags >> 4) & 7) < 5), "invalid particle shape selector");
}
void colorKeys(Block b, unsigned offset, unsigned count, unsigned maximum) {
    require(count && offset >= 52 && maximum < 32767, "invalid particle color animation"); b.range(offset, count * 6);
    unsigned previous = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto frame = b.u16(offset + i * 6); require(frame <= 32767 && (!i || frame > previous), "invalid particle color key order");
        previous = frame; b.words(offset + i * 6, 1);
    }
}
void baseShape(Block b, unsigned textureCount) {
    b.range(0, 52); const auto flags = b.u32(8); shapeFlags(flags);
    const auto blend = b.u16(24); require((blend & 3) < 3 && ((blend >> 2) & 15) < 10 && ((blend >> 6) & 15) < 10 && ((flags >> 15) & 7) < 6, "invalid particle blend configuration");
    require(((b.byte(30) >> 2) & 7) <= 4 && ((b.byte(33) >> 4) & 7) <= 4, "invalid particle animation selector");
    if (textureCount) require(b.byte(32) < textureCount, "particle texture index exceeds references");
    if (flags & 0x1000000) b.floats(52, 10);
    if (b.byte(30) & 1) {
        const auto p = (flags & 0x1000000) ? 92 : 52; const auto count = b.byte(31); require(count, "empty particle texture animation"); b.range(p, count);
        for (unsigned i = 0; i < count; ++i) require(b.byte(p + i) < textureCount, "animated particle texture exceeds references");
    }
    if (b.byte(33) & 2) colorKeys(b, b.u16(12), b.byte(34), b.u16(36));
    if (b.byte(33) & 8) colorKeys(b, b.u16(14), b.byte(35), b.u16(36));
    b.dwords(8, 1); b.words(12, 2); b.floats(16, 2); b.words(24, 1); b.words(36, 1);
}
}
std::vector<std::uint8_t> importParticles(std::span<const std::uint8_t> source) {
    std::vector<std::uint8_t> result(source.begin(), source.end()); Block file{source, result}; file.range(0, 16);
    require(!std::memcmp(source.data(), "JPAC2-10", 8), "unsupported particle archive header");
    const unsigned resources = file.u16(8), textures = file.u16(10); const std::size_t textureOffset = file.u32(12);
    require(resources && textureOffset >= 16 && textureOffset <= source.size(), "invalid particle archive counts");
    file.dwords(4, 1); file.words(8, 2); file.dwords(12, 1); std::size_t p = 16; std::set<unsigned> ids;
    for (unsigned i = 0; i < resources; ++i) {
        file.range(p, 8); require(ids.insert(file.u16(p)).second, "duplicate particle resource ID");
        const unsigned blocks = file.u16(p + 2), fields = file.byte(p + 4), keys = file.byte(p + 5), references = file.byte(p + 6);
        file.words(p, 2); p += 8; unsigned fieldCount = 0, keyCount = 0; bool dynamics = false, base = false, textureTable = false; std::set<unsigned> singleton;
        for (unsigned j = 0; j < blocks; ++j) {
            file.range(p, 8); const auto tag = file.u32(p), size = file.u32(p + 4);
            require(p <= textureOffset && size >= 8 && size <= textureOffset - p, "particle block exceeds resource section"); Block b = file.sub(p, size);
            if (tag != 0x464c4431 && tag != 0x4b464131) require(singleton.insert(tag).second, "duplicate particle data block");
            switch (tag) {
            case 0x42454d31: b.range(0, 124); b.dwords(8, 2); b.floats(16, 22); b.words(104, 8); dynamics = true; break;
            case 0x42535031: baseShape(b, references); base = true; break;
            case 0x45535031: b.range(0, 96); b.dwords(8, 1); b.floats(12, 7); b.words(40, 2); b.floats(44, 13); break;
            case 0x53535031:
                b.range(0, 72); shapeFlags(b.u32(8)); if (references) require(b.byte(69) < references, "child particle texture exceeds references");
                b.dwords(8, 1); b.floats(12, 10); b.floats(60, 1); b.words(64, 2); b.words(70, 1); break;
            case 0x45545831:
                b.range(0, 40); if (b.u32(8) & 1) require(b.byte(37) < references, "indirect particle texture exceeds references");
                if (b.u32(8) & 256) require(b.byte(38) < references, "secondary particle texture exceeds references");
                b.dwords(8, 1); b.floats(12, 6); break;
            case 0x464c4431:
                b.range(0, 68); require((b.u32(8) & 15) <= 8, "invalid particle field selector"); b.dwords(8, 1); b.floats(12, 13); ++fieldCount; break;
            case 0x4b464131: {
                b.range(0, 12); const auto count = b.byte(9); require(count && b.byte(8) <= 10, "invalid particle key curve"); b.range(12, count * 16);
                float previous = -1;
                for (unsigned k = 0; k < count; ++k) { const auto time = b.number(12 + k * 16); require(time > previous, "invalid particle key order"); previous = time; }
                b.floats(12, count * 4); ++keyCount; break;
            }
            case 0x54444231:
                b.range(8, references * 2); for (unsigned k = 0; k < references; ++k) require(b.u16(8 + k * 2) < textures, "particle texture reference exceeds archive");
                b.words(8, references); textureTable = true; break;
            default: throw std::invalid_argument("unsupported particle block " + std::to_string(tag));
            }
            b.dwords(0, 2); p += size;
        }
        require(dynamics && base && fieldCount == fields && keyCount == keys && (!references || textureTable), "particle resource has missing data blocks");
    }
    require(p <= textureOffset, "particle resource section overlaps textures"); p = textureOffset;
    for (unsigned i = 0; i < textures; ++i) {
        file.range(p, 64); const auto size = file.u32(p + 4); require(file.u32(p) == 0x54455831 && size >= 64, "invalid particle texture block");
        Block b = file.sub(p, size); require(std::memchr(b.source.data() + 12, 0, 20), "unterminated particle texture name");
        const auto texture = importTexture(b.source.subspan(32)); std::memcpy(b.output.data() + 32, texture.data(), texture.size()); b.dwords(0, 2); p += size;
    }
    require(p == source.size(), "particle archive has unaccounted trailing bytes"); return result;
}
}
