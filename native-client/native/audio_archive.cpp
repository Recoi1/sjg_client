#include "audio_archive.hpp"
#include <cstring>
#include <stdexcept>
#include <unordered_set>
#include <string>

namespace galaxy {
namespace {
struct Record {
    std::span<const std::uint8_t> source;
    std::span<std::uint8_t> out;
    void range(std::size_t p, std::size_t n) const {
        if (p > source.size() || n > source.size() - p)
            throw std::invalid_argument("truncated audio record at " + std::to_string(p) + " length " + std::to_string(n) + " extent " + std::to_string(source.size()));
    }
    std::uint16_t u16(std::size_t p) const { range(p, 2); return source[p] << 8 | source[p + 1]; }
    std::uint32_t u32(std::size_t p) const { return std::uint32_t(u16(p)) << 16 | u16(p + 2); }
    void word(std::size_t p) { const auto v = u16(p); std::memcpy(out.data() + p, &v, 2); }
    void dword(std::size_t p) { const auto v = u32(p); std::memcpy(out.data() + p, &v, 4); }
    void dwords(std::size_t p, std::size_t n) { range(p, n * 4); for (std::size_t i = 0; i < n; ++i) dword(p + i * 4); }
    std::uint32_t count(std::size_t p, std::size_t start, std::size_t stride) {
        const auto n = u32(p); range(start, std::size_t(n) * stride); dword(p); return n;
    }
    void string(std::size_t p, std::size_t limit = 0) const {
        range(p, 1); const auto size = limit ? limit : source.size() - p; range(p, size);
        if (!std::memchr(source.data() + p, 0, size)) throw std::invalid_argument("unterminated audio name");
    }
    Record sub(std::size_t p, std::size_t n) const { range(p, n); return {source.subspan(p, n), out.subspan(p, n)}; }
};
void require(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }

void soundTable(Record b, bool names) {
    require(b.u32(0) == (names ? 0x4253544e : 0x42535420), "invalid sound table signature");
    b.dwords(0, 4); const std::size_t root = b.u32(12);
    const auto sections = b.count(root, root + 4, 4); require(sections <= 256, "too many sound sections");
    for (unsigned s = 0; s < sections; ++s) {
        const auto sp = root + 4 + s * 4; const std::size_t section = b.u32(sp); b.dword(sp); if (!section) continue;
        const auto start = section + (names ? 8 : 4);
        const auto groups = b.count(section, start, 4); require(groups <= 256, "too many sound groups");
        if (names) { const auto name = b.u32(section + 4); b.dword(section + 4); if (name) b.string(name); }
        for (unsigned g = 0; g < groups; ++g) {
            const auto gp = start + g * 4; const std::size_t group = b.u32(gp); b.dword(gp); if (!group) continue;
            const auto items = b.count(group, group + 8, 4); require(items <= 65536, "too many sound items");
            const auto category = b.u32(group + 4); b.dword(group + 4); if (names && category) b.string(category);
            for (unsigned i = 0; i < items; ++i) {
                const auto ip = group + 8 + i * 4; const auto value = b.u32(ip); b.dword(ip);
                const std::size_t item = names ? value : value & 0xffffff; if (!item) continue;
                if (names) { b.string(item); continue; }
                const auto type = value >> 24 & 0xf0;
                require(type == 0x50 || type == 0x60 || type == 0x70, "unsupported sound table item");
                b.range(item, type == 0x50 ? 6 : 8); b.word(item + 2);
                if (type != 0x70) { b.word(item + 4); if (type == 0x60) b.word(item + 6); }
                else { const auto name = b.u32(item + 4); b.dword(item + 4); b.string(name); }
            }
        }
    }
}
void sequences(Record b) {
    b.range(0, 8); require(b.source[0] == 'S' && b.source[1] == 'C', "invalid sequence collection");
    const auto size = b.u32(4); require(size >= 8 && size <= b.source.size(), "invalid sequence collection extent");
    b = b.sub(0, size); const auto groups = b.u16(2); b.word(2); b.dword(4); b.range(8, groups * 4);
    for (unsigned i = 0; i < groups; ++i) {
        const auto p = 8 + i * 4; const std::size_t table = b.u32(p); b.dword(p);
        const auto count = b.count(table, table + 4, 4);
        for (unsigned j = 0; j < count; ++j) {
            const auto q = table + 4 + j * 4; b.range(b.u32(q), 1); b.dword(q);
        }
    }
    // Sequence instructions are byte streams; their multi-byte operands remain big-endian.
}
void waves(Record b) {
    require(b.u32(0) == 0x57535953, "invalid wave bank signature"); b.range(0, 24);
    const std::size_t archiveBank = b.u32(16), ctrlGroup = b.u32(20); b.dwords(4, 5);
    const auto groups = b.count(ctrlGroup + 8, ctrlGroup + 12, 4);
    require(groups > 0 && groups <= 32, "invalid wave bank group count");
    require(b.count(archiveBank + 4, archiveBank + 8, 4) == groups, "wave bank group counts differ");
    for (unsigned i = 0; i < groups; ++i) {
        const auto ap = archiveBank + 8 + i * 4, cp = ctrlGroup + 12 + i * 4;
        const std::size_t archive = b.u32(ap), scene = b.u32(cp); b.dword(ap); b.dword(cp); b.range(scene, 16);
        const std::size_t ctrl = b.u32(scene + 12); b.dword(scene + 12); b.string(archive, 112);
        const auto count = b.count(ctrl + 4, ctrl + 8, 4);
        require(count <= 65536 && b.count(archive + 112, archive + 116, 4) == count, "wave bank wave counts differ");
        for (unsigned j = 0; j < count; ++j) {
            const auto wp = archive + 116 + j * 4, idp = ctrl + 8 + j * 4;
            const std::size_t wave = b.u32(wp), id = b.u32(idp); b.dword(wp); b.dword(idp); b.dword(id);
            b.range(wave, 36); b.dwords(wave + 4, 7); b.word(wave + 32); b.word(wave + 34);
            const auto end = b.u32(wave + 24), samples = b.u32(wave + 28);
            require(!b.u32(wave + 16) || (b.u32(wave + 20) <= end && end <= samples), "invalid wave loop");
        }
    }
}
void bank(Record b) {
    require(b.u32(0) == 0x49424e4b && b.u32(12) == 1, "unsupported instrument bank version");
    b.dwords(0, 4); std::size_t env = 0, envSize = 0, osc = 0, list = 0;
    const auto declaredSize = b.u32(4); b.range(0, declaredSize);
    for (std::size_t p = 32; p < declaredSize;) {
        if (b.source.size() - p < 8) { for (; p < b.source.size(); ++p) require(!b.source[p], "invalid instrument padding"); break; }
        const auto tag = b.u32(p), size = b.u32(p + 4); b.range(p + 8, size); b.dwords(p, 2);
        if (tag == 0x454e5654) { require(!env, "duplicate envelope chunk"); env = p + 8; envSize = size; }
        if (tag == 0x4f534354) { require(!osc, "duplicate oscillator chunk"); osc = p + 8; }
        if (tag == 0x4c495354) { require(!list, "duplicate instrument list"); list = p + 8; }
        p += 8 + ((std::size_t(size) + 3) & ~std::size_t(3));
    }
    require(env && osc && list && !(envSize % 6), "missing instrument bank chunks");
    for (std::size_t p = env; p < env + envSize; p += 2) b.word(p);
    const auto oscCount = b.count(osc, osc + 4, 28);
    auto envelope = [&](std::uint32_t offset) {
        require(offset < envSize && !(offset % 6), "invalid envelope offset");
        for (std::size_t p = offset; p < envSize; p += 6) {
            require(envSize - p >= 6, "truncated envelope point"); const auto mode = b.u16(env + p);
            if (mode == 13) { const auto target = b.u16(env + p + 4); require(std::size_t(target) * 6 < envSize - offset, "invalid envelope loop target"); return; }
            if (mode == 14 || mode == 15) return;
            require(mode <= 3, "invalid envelope curve");
        }
        throw std::invalid_argument("unterminated envelope");
    };
    for (unsigned i = 0; i < oscCount; ++i) {
        const auto p = osc + 4 + i * 28; b.dword(p); b.dwords(p + 8, 5);
        envelope(b.u32(p + 12)); envelope(b.u32(p + 16));
    }
    std::unordered_set<std::uint32_t> effects;
    auto effect = [&](std::size_t p) {
        if (!effects.insert(p).second) return;
        require(b.u32(p) == 0x52616e64 || b.u32(p) == 0x53656e73, "unsupported instrument effect");
        b.range(p, 16); b.dword(p); b.dwords(p + 8, 2);
    };
    auto effectList = [&](std::size_t& p) {
        const auto n = b.count(p, p + 4, 4); p += 4;
        for (unsigned i = 0; i < n; ++i, p += 4) { effect(b.u32(p)); b.dword(p); }
    };
    auto velocities = [&](std::size_t& p) {
        const auto n = b.count(p, p + 4, 16); p += 4;
        for (unsigned i = 0; i < n; ++i, p += 16) b.dwords(p + 4, 3);
    };
    const auto count = b.count(list, list + 4, 4); require(count <= 240, "too many instruments");
    std::unordered_set<std::uint32_t> seen;
    for (unsigned i = 0; i < count; ++i) {
        const auto ip = list + 4 + i * 4; const std::size_t inst = b.u32(ip); b.dword(ip);
        if (!inst || !seen.insert(inst).second) continue;
        const auto tag = b.u32(inst); b.dword(inst); std::size_t p = inst + 4;
        if (tag == 0x496e7374) {
            const auto n = b.count(p, p + 4, 4); require(n <= 2, "too many instrument oscillators"); p += 4;
            for (unsigned j = 0; j < n; ++j, p += 4) { require(b.u32(p) < oscCount, "invalid oscillator index"); b.dword(p); }
            effectList(p); const auto keys = b.count(p, p + 4, 8); p += 4;
            for (unsigned j = 0; j < keys; ++j) { b.dword(p); p += 4; velocities(p); }
            b.dwords(p, 2);
        } else if (tag == 0x50657263) {
            const auto n = b.count(p, p + 4, 4); require(n <= 128, "too many percussion keys"); p += 4;
            for (unsigned j = 0; j < n; ++j, p += 4) {
                const std::size_t drum = b.u32(p); b.dword(p); if (!drum) continue;
                require(b.u32(drum) == 0x506d6170, "invalid percussion map"); b.dwords(drum, 3); b.word(drum + 14);
                std::size_t q = drum + 16; effectList(q); velocities(q);
            }
        } else throw std::invalid_argument("unsupported instrument record");
    }
}
}
std::vector<std::uint8_t> importWaveBank(std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> result(bytes.begin(), bytes.end()); Record b{bytes, result};
    require(b.u32(4) == bytes.size(), "invalid wave bank extent");
    waves(b);
    return result;
}
std::vector<std::uint8_t> importAudioArchive(std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> result(bytes.begin(), bytes.end()); Record b{bytes, result};
    require(b.u32(0) == 0x41415f3c, "invalid audio archive signature"); b.dword(0);
    struct Payload { std::uint32_t tag, offset, size; }; std::vector<Payload> payloads;
    std::size_t p = 4;
    for (;;) {
        const auto tag = b.u32(p); b.dword(p); p += 4;
        if (tag == 0x3e5f4141) break;
        if (tag == 0x77732020 || tag == 0x626e6b20) {
            const auto n = tag == 0x77732020 ? 3 : 2; b.range(p, n * 4);
            require(b.u32(p) < 256, "invalid audio bank index"); const auto offset = b.u32(p + 4), size = b.u32(std::size_t(offset) + 4);
            require(size >= 24, "invalid audio bank size"); b.range(offset, size); payloads.push_back({tag, offset, size}); b.dwords(p, n); p += n * 4;
        } else if (tag == 0x62737420 || tag == 0x6273746e || tag == 0x62736320) {
            const auto start = b.u32(p), end = b.u32(p + 4); require(end > start, "invalid audio table extent");
            b.range(start, end - start); payloads.push_back({tag, start, end - start}); b.dwords(p, 2); p += 8;
        } else throw std::invalid_argument("unsupported audio archive command");
    }
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (auto item : payloads) {
        if (item.tag == 0x626e6b20) {
            // Some retail IBNK sizes end inside LIST; the next BAA payload bounds the final record.
            auto end = bytes.size();
            for (const auto& other : payloads) if (other.offset > item.offset && other.offset < end) end = other.offset;
            require(item.size <= end - item.offset, "instrument bank overlaps next payload");
            item.size = std::uint32_t(end - item.offset);
        }
        require(item.offset >= p, "audio payload overlaps commands");
        for (const auto& r : ranges) require(item.offset >= r.second || item.offset + std::size_t(item.size) <= r.first, "overlapping audio payloads");
        ranges.emplace_back(item.offset, item.offset + std::size_t(item.size)); const auto record = b.sub(item.offset, item.size);
        try { switch (item.tag) {
        case 0x77732020: waves(record); break;
        case 0x626e6b20: bank(record); break;
        case 0x62736320: sequences(record); break;
        default: soundTable(record, item.tag == 0x6273746e); break;
        } } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("Audio payload at " + std::to_string(item.offset) + ": " + error.what());
        }
    }
    return result;
}
}
