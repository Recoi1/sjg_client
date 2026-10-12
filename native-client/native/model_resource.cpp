#include "model_resource.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace galaxy {
namespace {
void require(bool valid, const char* message) { if (!valid) throw std::invalid_argument(message); }
struct Block {
    std::span<const std::uint8_t> source;
    std::span<std::uint8_t> out;
    void range(std::size_t p, std::size_t n) const {
        if (p > source.size() || n > source.size() - p)
            throw std::invalid_argument("model record at " + std::to_string(p) + " length " + std::to_string(n) + " exceeds " + std::to_string(source.size()));
    }
    std::uint16_t u16(std::size_t p) const { range(p, 2); return source[p] << 8 | source[p + 1]; }
    std::uint32_t u32(std::size_t p) const { return std::uint32_t(u16(p)) << 16 | u16(p + 2); }
    std::uint8_t byte(std::size_t p) const { range(p, 1); return source[p]; }
    void word(std::size_t p) { const auto v = u16(p); std::memcpy(out.data() + p, &v, 2); }
    void dword(std::size_t p) { const auto v = u32(p); std::memcpy(out.data() + p, &v, 4); }
    void words(std::size_t p, std::size_t n) { range(p, n * 2); for (std::size_t i = 0; i < n; ++i) word(p + i * 2); }
    void dwords(std::size_t p, std::size_t n) { range(p, n * 4); for (std::size_t i = 0; i < n; ++i) dword(p + i * 4); }
    Block sub(std::size_t p, std::size_t n) const { range(p, n); return {source.subspan(p, n), out.subspan(p, n)}; }
    void string(std::size_t p) const { range(p, 1); require(std::memchr(source.data() + p, 0, source.size() - p), "unterminated model name"); }
};
void names(Block b, std::size_t p) {
    if (!p) return;
    const auto count = b.u16(p); b.words(p, 2); b.range(p + 4, count * 4);
    for (unsigned i = 0; i < count; ++i) { const auto entry = p + 4 + i * 4; b.string(p + b.u16(entry + 2)); b.words(entry, 2); }
}
std::vector<std::size_t> offsets(Block b, std::size_t p, unsigned count) {
    b.range(p, count * 4); std::vector<std::size_t> result;
    for (unsigned i = 0; i < count; ++i) { const auto value = b.u32(p + i * 4); if (value) b.range(value, 1); result.push_back(value); b.dword(p + i * 4); }
    return result;
}
std::size_t endOf(Block b, const std::vector<std::size_t>& offsets, std::size_t p) {
    auto end = b.source.size(); for (auto offset : offsets) if (offset > p && offset < end) end = offset; return end;
}
void texture(Block b, std::size_t p) {
        b.range(p, 32); const auto width = b.u16(p + 2), height = b.u16(p + 4);
        require(width && height && width <= 1024 && height <= 1024, "invalid model texture dimensions");
        const auto format = b.byte(p); const auto levels = std::max(1u, unsigned(b.byte(p + 24)));
        require(levels <= 11, "invalid model texture mip count");
        unsigned bw = 4, bh = 4, bs = 32;
        switch (format) {
        case 0: case 8: case 14: bw = bh = 8; break;
        case 1: case 2: case 9: bw = 8; break;
        case 3: case 4: case 5: case 10: break;
        case 6: bs = 64; break;
        default: throw std::invalid_argument("unsupported model texture format");
        }
        std::size_t size = 0; unsigned w = width, h = height;
        for (unsigned l = 0; l < levels; ++l) { size += std::size_t((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bs; w = std::max(1u, w / 2); h = std::max(1u, h / 2); }
        b.range(p + b.u32(p + 28), size);
        if (b.u16(p + 10)) b.range(p + b.u32(p + 12), b.u16(p + 10) * 2);
        b.words(p + 2, 2); b.word(p + 10); b.dword(p + 12); b.word(p + 26); b.dword(p + 28);
}
void textures(Block b) {
    const auto count = b.u16(8); b.word(8); const auto table = offsets(b, 12, 2); names(b, table[1]);
    b.range(table[0], count * 32);
    for (unsigned i = 0; i < count; ++i) texture(b, table[0] + i * 32);
}
std::vector<std::uint8_t> material(Block b) {
    const auto count = b.u16(8); b.word(8); const auto table = offsets(b, 12, 30); names(b, table[2]);
    constexpr std::array<unsigned, 30> strides{332,2,0,312,4,4,1,8,4,52,1,4,4,100,100,2,4,8,4,1,20,4,4,44,8,4,4,1,1,16};
    std::array<std::size_t, 30> used{};
    auto entry = [&](unsigned t, unsigned index) {
        require(table[t] && strides[t], "missing referenced material table"); const auto p = table[t] + std::size_t(index) * strides[t];
        b.range(p, strides[t]); used[t] = std::max(used[t], std::size_t(index) + 1); return p;
    };
    auto index16 = [&](std::size_t p, unsigned t, unsigned limit) {
        const auto value = b.u16(p); if (value != 0xffff) { entry(t, value); require(value < limit, "material index exceeds hardware limit"); }
    };
    for (unsigned i = 0; i < count; ++i) {
        const auto idp = entry(1, i); const auto id = b.u16(idp); const auto p = entry(0, id);
        const unsigned byteTables[]{4,6,10,19,27,26,28};
        for (unsigned j = 0; j < 7; ++j) {
            const auto value = b.byte(p + j + 1); if (value != 255) {
                const auto q = entry(byteTables[j], value);
                if (j == 1) require(b.byte(q) <= 2, "too many color channels");
                if (j == 2) require(b.byte(q) <= 8, "too many texture generators");
                if (j == 3) require(b.byte(q) <= 16, "too many TEV stages");
            }
        }
        for (unsigned j = 0; j < 2; ++j) { index16(p + 8 + j * 2, 5, 65536); index16(p + 20 + j * 2, 8, 65536); }
        for (unsigned j = 0; j < 4; ++j) { index16(p + 12 + j * 2, 7, 65536); index16(p + 148 + j * 2, 18, 65536); index16(p + 220 + j * 2, 17, 65536); index16(p + 292 + j * 2, 22, 65536); }
        for (unsigned j = 0; j < 8; ++j) { index16(p + 40 + j * 2, 11, 65536); index16(p + 72 + j * 2, 13, 65536); index16(p + 132 + j * 2, 15, 65536); }
        for (unsigned j = 0; j < 16; ++j) { index16(p + 188 + j * 2, 16, 65536); index16(p + 228 + j * 2, 20, 65536); index16(p + 260 + j * 2, 21, 65536); }
        index16(p + 324, 23, 65536); index16(p + 326, 24, 65536); index16(p + 328, 25, 65536); index16(p + 330, 29, 65536);
    }
    std::vector<std::uint8_t> result(b.out.begin(), b.out.end());
    for (unsigned t = 0; t < table.size(); ++t) {
        if (t == 2) continue;
        if (t == 3 && table[t] && table[t] != table[2]) used[t] = count;
        std::vector<std::uint8_t> source;
        if (used[t]) {
            source.resize(used[t] * strides[t]);
            if (t == 3) {
                for (unsigned i = 0; i < count; ++i) {
                    const auto p = table[t] + i * strides[t]; if (!b.byte(p)) continue;
                    b.range(p, strides[t]); memcpy(source.data() + i * strides[t], b.source.data() + p, strides[t]);
                }
            } else { b.range(table[t], source.size()); memcpy(source.data(), b.source.data() + table[t], source.size()); }
        }
        std::vector<std::uint8_t> converted = source; Block c{source, converted};
        for (std::size_t p = 0; p < source.size(); p += strides[t]) {
            if (t == 0) {
                c.words(p + 8, 8); c.words(p + 40, 8); c.words(p + 72, 8); c.words(p + 132, 12);
                c.words(p + 188, 60); c.words(p + 324, 4);
            } else if (t == 1 || t == 15) c.word(p);
            else if (t == 3) { if (!c.byte(p)) continue; require(c.byte(p + 1) <= 3, "too many indirect texture stages"); for (unsigned j = 0; j < 3; ++j) c.dwords(p + 20 + j * 28, 6); }
            else if (t == 4) c.dword(p);
            else if (t == 9) { c.dwords(p, 6); c.dwords(p + 28, 6); }
            else if (t == 13 || t == 14) { c.dwords(p + 4, 5); c.word(p + 24); c.dwords(p + 28, 18); }
            else if (t == 17) c.words(p, 4);
            else if (t == 23) { c.word(p + 2); c.dwords(p + 4, 4); c.words(p + 24, 10); }
            else if (t == 29) c.dwords(p + 4, 3);
        }
        // Retail material tables can overlap; separate their byte and numeric views before conversion.
        result.resize((result.size() + 3) & ~std::size_t(3));
        const std::uint32_t newOffset = converted.empty() ? 0 : std::uint32_t(result.size());
        memcpy(result.data() + 12 + t * 4, &newOffset, 4); result.insert(result.end(), converted.begin(), converted.end());
    }
    result.resize((result.size() + 31) & ~std::size_t(31)); const auto length = std::uint32_t(result.size()); memcpy(result.data() + 4, &length, 4);
    return result;
}
void vertex(Block b) {
    const auto table = offsets(b, 8, 14); require(table[0] != 0, "missing vertex formats");
    std::array<unsigned, 26> types{}; types.fill(255); auto p = table[0]; bool terminated = false;
    for (unsigned i = 0; i < 27; ++i, p += 16) {
        const auto attr = b.u32(p); b.dwords(p, 3);
        if (attr == 255) { terminated = true; break; }
        require(attr < types.size() && b.u32(p + 8) <= 5, "invalid vertex format"); types[attr] = b.u32(p + 8);
    }
    require(terminated, "unterminated vertex formats");
    const unsigned attributes[]{9,10,25,11,12,13,14,15,16,17,18,19,20};
    for (unsigned i = 1; i < table.size(); ++i) {
        if (!table[i] || (i == 4 || i == 5)) continue;
        auto type = types[attributes[i - 1]]; if (i == 3 && type == 255) type = types[10];
        require(type <= 4, "missing numeric vertex format");
        const auto start = table[i], end = endOf(b, table, start);
        if (type == 2 || type == 3) b.words(start, (end - start) / 2);
        else if (type == 4) b.dwords(start, (end - start) / 4);
    }
}
void shape(Block b, unsigned jointCount, unsigned envelopeCount) {
    const auto count = b.u16(8); b.word(8); const auto table = offsets(b, 12, 8); names(b, table[2]); b.words(table[1], count);
    auto checkTable = [&](unsigned t, std::size_t index, std::size_t stride) {
        require(table[t] && index < (endOf(b, table, table[t]) - table[t]) / stride, "shape index outside table"); return table[t] + index * stride;
    };
    for (unsigned i = 0; i < count; ++i) {
        const auto p = checkTable(0, b.u16(table[1] + i * 2), 40); b.words(p + 2, 4); b.dwords(p + 12, 7);
        require(b.byte(p) <= 3, "invalid shape matrix type");
        std::size_t d = table[3] + b.u16(p + 4); require(d >= table[3], "invalid vertex descriptor offset");
        bool terminated = false;
        for (unsigned j = 0; j < 27; ++j, d += 8) { const auto attr = b.u32(d); b.dwords(d, 2); if (attr == 255) { terminated = true; break; } require(attr <= 25 && b.u32(d + 4) <= 3, "invalid vertex descriptor"); }
        require(terminated, "unterminated vertex descriptors");
        for (unsigned j = 0; j < b.u16(p + 2); ++j) {
            const auto m = checkTable(6, b.u16(p + 6) + j, 8), draw = checkTable(7, b.u16(p + 8) + j, 8);
            b.words(m, 2); b.dword(m + 4); b.dwords(draw, 2);
            const auto first = b.u32(m + 4); const auto n = b.u16(m + 2); const auto mp = table[4] + std::size_t(first) * 2;
            require(mp <= endOf(b, table, table[4]) && n * 2 <= endOf(b, table, table[4]) - mp, "invalid shape matrix range"); b.words(mp, n);
            for (unsigned k = 0; k < n; ++k) require(b.u16(mp + k * 2) < jointCount + envelopeCount || b.u16(mp + k * 2) == 65535, "invalid shape matrix index");
            b.range(table[5] + b.u32(draw + 4), b.u32(draw));
        }
    }
}
}
std::vector<std::uint8_t> importTexture(std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> result(bytes.begin(), bytes.end()); texture({bytes, result}, 0); return result;
}
std::vector<std::uint8_t> importModel(std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> result(bytes.begin(), bytes.end()); Block file{bytes, result}; file.range(0, 32);
    require(file.u32(0) == 0x4a334432, "unsupported model signature");
    const auto format = file.u32(4); require(format == 0x62646c34 || format == 0x62646c33 || format == 0x626d6433 || format == 0x626d7433, "unsupported model version");
    const auto size = file.u32(8), count = file.u32(12); require(size == bytes.size() && count <= size / 8, "invalid model extent"); file.dwords(0, 4);
    struct Entry { std::uint32_t tag; Block b; }; std::vector<Entry> blocks; unsigned joints = 0, envelopes = 0, materials = 0, shapes = 0;
    std::size_t p = 32;
    for (unsigned i = 0; i < count; ++i) {
        const auto tag = file.u32(p), length = file.u32(p + 4); require(length >= 8 && !(length & 3), "invalid model block extent");
        auto b = file.sub(p, length); b.dwords(0, 2); blocks.push_back({tag, b}); p += length;
        if (tag == 0x4a4e5431) joints = b.u16(8);
        if (tag == 0x45565031) envelopes = b.u16(8);
        if (tag == 0x4d415433) materials = b.u16(8);
        if (tag == 0x53485031) shapes = b.u16(8);
    }
    require(p == size, "incomplete model block table");
    std::vector<std::uint8_t> rebuilt(result.begin(), result.begin() + 32);
    for (auto [tag, b] : blocks) {
        try {
            switch (tag) {
            case 0x494e4631: {
                b.word(8); b.dwords(12, 3); std::size_t h = b.u32(20); int nesting = 0; bool end = false;
                for (; h + 4 <= b.source.size(); h += 4) {
                    const auto type = b.u16(h), value = b.u16(h + 2); b.words(h, 2);
                    if (type == 0) { end = true; break; }
                    if (type == 1) ++nesting; else if (type == 2) require(--nesting >= 0, "invalid model hierarchy nesting");
                    else if (type == 16) require(value < joints, "invalid hierarchy joint");
                    else if (type == 17) require(value < materials, "invalid hierarchy material");
                    else if (type == 18) require(value < shapes, "invalid hierarchy shape");
                    else throw std::invalid_argument("invalid hierarchy command");
                }
                require(end && nesting == 0, "incomplete model hierarchy"); break;
            }
            case 0x56545831: vertex(b); break;
            case 0x45565031: {
                b.word(8); const auto table = offsets(b, 12, 4); b.range(table[0], envelopes);
                std::size_t n = 0; for (unsigned i = 0; i < envelopes; ++i) n += b.byte(table[0] + i);
                b.words(table[1], n); b.dwords(table[2], n); if (table[3]) b.dwords(table[3], joints * 12);
                for (unsigned i = 0; i < n; ++i) require(b.u16(table[1] + i * 2) < joints, "invalid envelope joint"); break;
            }
            case 0x44525731: {
                const auto n = b.u16(8); require(n >= envelopes, "invalid draw matrix count"); b.word(8); const auto table = offsets(b, 12, 2);
                b.range(table[0], n); b.words(table[1], n);
                for (unsigned i = 0; i < n; ++i) { const auto flag = b.byte(table[0] + i); require(flag <= 1 && b.u16(table[1] + i * 2) < (flag ? envelopes : joints), "invalid draw matrix index"); } break;
            }
            case 0x4a4e5431: {
                b.word(8); const auto table = offsets(b, 12, 3); b.words(table[1], joints); names(b, table[2]);
                for (unsigned i = 0; i < joints; ++i) {
                    const auto index = b.u16(table[1] + i * 2); require(index < joints, "invalid joint index"); const auto q = table[0] + index * 64; b.range(q, 64);
                    b.word(q); b.dwords(q + 4, 3); b.words(q + 16, 3); b.dwords(q + 24, 10);
                } break;
            }
            case 0x4d415433: { const auto expanded = material(b); rebuilt.insert(rebuilt.end(), expanded.begin(), expanded.end()); continue; }
            case 0x53485031: shape(b, joints, envelopes); break;
            case 0x54455831: textures(b); break;
            case 0x4d444c33: {
                const auto n = b.u16(8); b.word(8); const auto table = offsets(b, 12, 6); names(b, table[5]);
                b.range(table[3], n);
                for (unsigned i = 0; i < n; ++i) {
                    const auto d = table[0] + i * 8; b.range(d + b.u32(d), b.u32(d + 4)); b.dwords(d, 2);
                    b.words(table[1] + i * 16, 6); b.dwords(table[2] + i * 8, 2);
                } break;
            }
            default: throw std::invalid_argument("unsupported model block");
            }
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument(std::string(reinterpret_cast<const char*>(b.source.data()), 4) + ": " + error.what());
        }
        rebuilt.insert(rebuilt.end(), b.out.begin(), b.out.end());
    }
    const auto length = std::uint32_t(rebuilt.size()); memcpy(rebuilt.data() + 8, &length, 4);
    return rebuilt;
}
}
