#include "layout_resource.hpp"
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace galaxy {
namespace {
using Bytes = std::vector<std::uint8_t>;
struct Block {
    std::span<const std::uint8_t> source;
    std::span<std::uint8_t> out;
    void range(std::size_t p, std::size_t n) const {
        if (p > source.size() || n > source.size() - p) throw std::invalid_argument("truncated layout record");
    }
    std::uint16_t u16(std::size_t p) const { range(p, 2); return source[p] << 8 | source[p + 1]; }
    std::uint32_t u32(std::size_t p) const { return std::uint32_t(u16(p)) << 16 | u16(p + 2); }
    std::uint8_t byte(std::size_t p) const { range(p, 1); return source[p]; }
    void word(std::size_t p) { const auto v = u16(p); std::memcpy(out.data() + p, &v, 2); }
    void dword(std::size_t p) { const auto v = u32(p); std::memcpy(out.data() + p, &v, 4); }
    void words(std::size_t p, std::size_t n) { range(p, n * 2); for (std::size_t i = 0; i < n; ++i) word(p + i * 2); }
    void floats(std::size_t p, std::size_t n) { range(p, n * 4); for (std::size_t i = 0; i < n; ++i) dword(p + i * 4); }
    void string(std::size_t p) const {
        range(p, 1);
        if (!std::memchr(source.data() + p, 0, source.size() - p)) throw std::invalid_argument("unterminated layout name");
    }
    Block sub(std::size_t p, std::size_t n) const { range(p, n); return {source.subspan(p, n), out.subspan(p, n)}; }
};
void material(Block b, std::uint16_t textureCount) {
    b.range(0, 64); b.words(20, 12);
    const auto flags = b.u32(60); b.dword(60);
    const auto maps = flags & 15, srts = flags >> 4 & 15, gens = flags >> 8 & 15;
    const auto indSrts = flags >> 13 & 3, stages = flags >> 15 & 7, tevs = flags >> 18 & 31;
    if (maps > 8 || srts > 10 || gens > 8 || stages > 4 || tevs > 16) throw std::invalid_argument("invalid layout material counts");
    std::size_t p = 64;
    for (unsigned i = 0; i < maps; ++i, p += 4) {
        b.range(p, 4);
        if (b.u16(p) >= textureCount) throw std::invalid_argument("invalid layout texture index");
        b.word(p);
    }
    b.floats(p, srts * 5); p += srts * 20;
    b.range(p, gens * 4); p += gens * 4;
    p += (flags >> 25 & 1) * 4 + (flags >> 27 & 1) * 4 + (flags >> 12 & 1) * 4;
    b.floats(p, indSrts * 5); p += indSrts * 20;
    p += stages * 4 + tevs * 16 + (flags >> 23 & 1) * 4 + (flags >> 24 & 1) * 4;
    b.range(0, p);
}
void coordinates(Block b, std::size_t p, unsigned n) {
    if (n > 8) throw std::invalid_argument("invalid layout texture coordinates");
    b.floats(p, n * 8);
}
void animation(Block b) {
    b.range(0, 20);
    const auto files = b.u16(12), contents = b.u16(14);
    const auto table = b.u32(16);
    b.word(8); b.words(12, 2); b.dword(16);
    b.range(20, std::size_t(files) * 4);
    for (unsigned i = 0; i < files; ++i) { b.string(20 + b.u32(20 + i * 4)); b.dword(20 + i * 4); }
    b.range(table, std::size_t(contents) * 4);
    for (unsigned i = 0; i < contents; ++i) {
        const auto content = b.u32(table + i * 4); b.dword(table + i * 4);
        b.range(content, 24);
        const auto infos = b.byte(content + 20);
        b.range(content + 24, infos * 4);
        for (unsigned j = 0; j < infos; ++j) {
            const auto ip = content + b.u32(content + 24 + j * 4); b.dword(content + 24 + j * 4);
            b.range(ip, 8); b.dword(ip);
            const auto targets = b.byte(ip + 4); b.range(ip + 8, targets * 4);
            for (unsigned k = 0; k < targets; ++k) {
                const auto tp = ip + b.u32(ip + 8 + k * 4); b.dword(ip + 8 + k * 4);
                b.range(tp, 12);
                const auto keys = b.u16(tp + 4);
                const auto curve = b.byte(tp + 2);
                const auto kp = tp + b.u32(tp + 8); b.word(tp + 4); b.dword(tp + 8);
                if (curve == 1) {
                    b.range(kp, std::size_t(keys) * 8);
                    for (unsigned n = 0; n < keys; ++n) { b.dword(kp + n * 8); b.word(kp + n * 8 + 4); }
                } else if (curve == 2) b.floats(kp, std::size_t(keys) * 3);
                else throw std::invalid_argument("unsupported layout animation curve");
            }
        }
    }
}
}
std::vector<std::uint8_t> importLayout(std::span<const std::uint8_t> bytes) {
    Bytes out(bytes.begin(), bytes.end()); Block file{bytes, out}; file.range(0, 16);
    const bool layout = std::memcmp(bytes.data(), "RLYT", 4) == 0;
    if ((!layout && std::memcmp(bytes.data(), "RLAN", 4)) || file.u16(4) != 0xfeff || file.u32(8) != bytes.size())
        throw std::invalid_argument("invalid layout file header");
    const auto count = file.u16(14), header = file.u16(12);
    if (header < 16 || !count) throw std::invalid_argument("invalid layout block table");
    file.words(4, 2); file.dword(8); file.words(12, 2);
    std::size_t p = header; unsigned materialCount = 0, fontCount = 0, textureCount = 0;
    int panes = 0, nesting = 0, groups = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto size = file.u32(p + 4);
        if (size < 8 || (size & 3)) throw std::invalid_argument("invalid layout block size");
        auto b = file.sub(p, size); b.dword(4);
        const std::string_view kind(reinterpret_cast<const char*>(b.source.data()), 4);
        if (kind == "lyt1") { b.floats(12, 2); }
        else if (kind == "txl1" || kind == "fnl1") {
            const auto n = b.u16(8); b.word(8); b.range(12, std::size_t(n) * 8);
            for (unsigned j = 0; j < n; ++j) { b.string(12 + b.u32(12 + j * 8)); b.dword(12 + j * 8); }
            if (kind == "txl1") textureCount = n; else fontCount = n;
        } else if (kind == "mat1") {
            materialCount = b.u16(8); b.word(8); b.range(12, materialCount * 4);
            for (unsigned j = 0; j < materialCount; ++j) {
                const auto start = b.u32(12 + j * 4);
                const auto end = j + 1 < materialCount ? b.u32(16 + j * 4) : size;
                if (start < 12 + materialCount * 4 || end < start) throw std::invalid_argument("invalid layout material offsets");
                material(b.sub(start, end - start), textureCount); b.dword(12 + j * 4);
            }
        } else if (kind == "pan1" || kind == "bnd1" || kind == "pic1" || kind == "txt1" || kind == "wnd1") {
            ++panes; b.range(0, 76); b.floats(36, 10);
            auto checkMaterial = [&](std::size_t offset) {
                if (b.u16(offset) >= materialCount) throw std::invalid_argument("invalid layout material index");
                b.word(offset);
            };
            if (kind == "pic1") { b.floats(76, 4); checkMaterial(92); coordinates(b, 96, b.byte(94)); }
            else if (kind == "txt1") {
                b.range(0, 116); const auto stringBytes = b.u16(78); const auto stringOffset = b.u32(88);
                if (b.u16(82) >= fontCount || (stringBytes & 1)) throw std::invalid_argument("invalid layout text resource");
                b.words(76, 2); checkMaterial(80); b.word(82); b.dword(88); b.floats(92, 6);
                if (stringBytes) {
                    b.words(stringOffset, stringBytes / 2);
                    if (b.u16(stringOffset + stringBytes - 2)) throw std::invalid_argument("unterminated layout text");
                }
            } else if (kind == "wnd1") {
                b.range(0, 104); b.floats(76, 4);
                const auto cp = b.u32(96), table = b.u32(100); b.dword(96); b.dword(100);
                b.floats(cp, 4); b.range(cp, 20); checkMaterial(cp + 16); coordinates(b, cp + 20, b.byte(cp + 18));
                const auto n = b.byte(92); b.range(table, n * 4);
                for (unsigned j = 0; j < n; ++j) { const auto fp = b.u32(table + j * 4); b.range(fp, 4); checkMaterial(fp); b.dword(table + j * 4); }
            }
        } else if (kind == "pas1") { if (!panes) throw std::invalid_argument("layout child without parent"); ++nesting; }
        else if (kind == "pae1") { if (--nesting < 0) throw std::invalid_argument("invalid layout pane nesting"); }
        else if (kind == "grp1") { const auto n = b.u16(24); b.word(24); b.range(28, std::size_t(n) * 16); }
        else if (kind == "grs1") ++groups;
        else if (kind == "gre1") { if (--groups < 0) throw std::invalid_argument("invalid layout group nesting"); }
        else if (kind == "pai1") animation(b);
        else if (kind == "pat1") { b.words(8, 2); b.dword(12); b.dword(16); b.words(20, 2); }
        else if (kind == "pah1") { b.dword(8); b.word(12); }
        else if (kind != "usd1") throw std::invalid_argument("unsupported layout block " + std::string(kind));
        p += size;
    }
    if (p != bytes.size() || nesting || groups || (layout && !panes)) throw std::invalid_argument("incomplete layout hierarchy");
    return out;
}
}
