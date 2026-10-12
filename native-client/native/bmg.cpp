#include "bmg.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace galaxy {
namespace {
using Bytes = std::vector<std::uint8_t>;
struct Reader {
    std::span<const std::uint8_t> bytes;
    void range(std::size_t offset, std::size_t count) const {
        if (offset > bytes.size() || count > bytes.size() - offset) throw std::invalid_argument("truncated BMG resource");
    }
    std::uint16_t word(std::size_t offset) const { range(offset, 2); return bytes[offset] << 8 | bytes[offset + 1]; }
    std::uint32_t dword(std::size_t offset) const { return std::uint32_t(word(offset)) << 16 | word(offset + 2); }
};
template<class T> void put(Bytes& bytes, std::size_t offset, T value) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) throw std::invalid_argument("invalid BMG destination");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
void word(Bytes& out, Reader r, std::size_t offset) { put(out, offset, r.word(offset)); }
void dword(Bytes& out, Reader r, std::size_t offset) { put(out, offset, r.dword(offset)); }
struct Block { std::uint32_t magic; Bytes bytes; };
constexpr std::uint32_t Inf = 0x494e4631, Dat = 0x44415431, Flow = 0x464c5731;
}

std::vector<std::uint8_t> importBmg(std::span<const std::uint8_t> source) {
    Reader r{source}; r.range(0, 32);
    if (std::memcmp(source.data(), "MESGbmg1", 8) || (r.dword(8) < 32 || r.dword(8) > source.size()) || source[16] != 2)
        throw std::invalid_argument("unsupported BMG header or text encoding");
    const auto count = r.dword(12);
    if (!count || count > 32) throw std::invalid_argument("invalid BMG block count");
    std::vector<Block> blocks;
    std::size_t cursor = 32, info = SIZE_MAX, data = SIZE_MAX;
    bool declaredBoundary = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto magic = r.dword(cursor), size = r.dword(cursor + 4);
        if (size < 8 || (size & 31)) throw std::invalid_argument("invalid BMG block size");
        auto storedSize = std::size_t(size);
        if (storedSize > source.size() - cursor && magic == 0x464c4931 && i + 1 == count) {
            r.range(cursor, 16);
            const auto labels = r.word(cursor + 8);
            const auto stride = source[cursor + 10];
            const auto required = 16 + std::size_t(labels) * stride;
            if (stride != 8 || size - (source.size() - cursor) >= 32 || required > source.size() - cursor)
                throw std::invalid_argument("truncated BMG labels");
            storedSize = source.size() - cursor;
        }
        r.range(cursor, storedSize);
        if (std::any_of(blocks.begin(), blocks.end(), [magic](const Block& b) { return b.magic == magic; }))
            throw std::invalid_argument("duplicate BMG block");
        if (magic == Inf) info = i;
        if (magic == Dat) data = i;
        blocks.push_back({magic, Bytes(source.begin() + cursor, source.begin() + cursor + storedSize)});
        cursor += storedSize;
        if (cursor == r.dword(8)) declaredBoundary = true;
    }
    if (cursor != source.size() || !declaredBoundary || info == SIZE_MAX || data == SIZE_MAX)
        throw std::invalid_argument("incomplete BMG message blocks");

    auto& dat = blocks[data].bytes;
    const Bytes originalText(dat.begin() + 8, dat.end());
    Reader text{originalText};
    Bytes converted;
    std::vector<std::uint32_t> offsets(text.bytes.size(), UINT32_MAX);
    for (std::size_t pos = 0; pos < text.bytes.size();) {
        offsets[pos] = converted.size();
        const auto code = text.word(pos);
        const auto start = converted.size();
        converted.resize(start + 2);
        put(converted, start, code);
        if (code != 0x1a) { pos += 2; continue; }
        text.range(pos, 6);
        const auto length = text.bytes[pos + 2], group = text.bytes[pos + 3];
        const auto tag = text.word(pos + 4);
        if (length < 6 || (length & 1)) throw std::invalid_argument("invalid BMG control length");
        text.range(pos, length);
        const bool pointer = group == 7;
        if (pointer && (length < 14 || length > 250)) throw std::invalid_argument("invalid BMG string argument");
        const auto extra = pointer ? sizeof(void*) - 4 : 0;
        converted.insert(converted.end(), text.bytes.begin() + pos + 2, text.bytes.begin() + pos + length);
        converted[start + 2] = length + extra;
        put(converted, start + 4, tag);
        const auto params = start + 6;
        if (group == 6 || group == 7 || group == 8) {
            if ((length - 6) % 4) throw std::invalid_argument("invalid BMG numeric argument");
            for (std::size_t p = 6; p < length; p += 4) put(converted, start + p, text.dword(pos + p));
            if (pointer) {
                if (text.dword(pos + 6) != 0) throw std::invalid_argument("serialized BMG pointer is not empty");
                converted.insert(converted.begin() + params + 4, extra, 0);
            }
        } else if (group == 1 || group == 2 || group == 10) {
            for (std::size_t p = 6; p < length; p += 2) put(converted, start + p, text.word(pos + p));
        } else if (group == 255 && tag == 2) {
            for (std::size_t p = 7; p + 1 < length; p += 2) put(converted, start + p, text.word(pos + p));
        } else if (group != 3 && group != 4 && group != 5 && group != 9 && !(group == 255 && tag == 0)) {
            throw std::invalid_argument("unsupported BMG control group " + std::to_string(group) + " tag " + std::to_string(tag));
        }
        pos += length;
    }
    dat.resize(8); dat.insert(dat.end(), converted.begin(), converted.end());
    dat.resize((dat.size() + 31) & ~std::size_t(31));

    auto& inf = blocks[info].bytes; Reader ir{inf};
    ir.range(0, 16);
    const auto messages = ir.word(8), stride = ir.word(10);
    if (stride < 12) throw std::invalid_argument("invalid BMG message record size");
    ir.range(16, std::size_t(messages) * stride);
    word(inf, ir, 8); word(inf, ir, 10); dword(inf, ir, 12);
    for (std::size_t i = 0; i < messages; ++i) {
        const auto entry = 16 + i * stride;
        const auto offset = ir.dword(entry);
        if (offset >= offsets.size() || offsets[offset] == UINT32_MAX || (offset & 1))
            throw std::invalid_argument("invalid BMG message text offset");
        std::size_t end = offset;
        while (text.word(end) != 0) {
            if (text.word(end) == 0x1a) { text.range(end, 4); end += text.bytes[end + 2]; }
            else end += 2;
        }
        put(inf, entry, offsets[offset]); word(inf, ir, entry + 4);
    }
    for (auto& block : blocks) {
        auto& bytes = block.bytes;
        if (block.magic == Flow) {
            Reader fr{bytes}; fr.range(0, 16);
            const auto nodes = fr.word(8), branches = fr.word(10);
            fr.range(16, std::size_t(nodes) * 8 + std::size_t(branches) * 2);
            word(bytes, fr, 8); word(bytes, fr, 10); dword(bytes, fr, 12);
            for (std::size_t i = 0; i < nodes; ++i) {
                const auto entry = 16 + i * 8;
                if (bytes[entry] < 1 || bytes[entry] > 3) throw std::invalid_argument("invalid BMG flow type");
                word(bytes, fr, entry + 2);
                if (bytes[entry] == 3) dword(bytes, fr, entry + 4);
                else { word(bytes, fr, entry + 4); word(bytes, fr, entry + 6); }
            }
            for (std::size_t i = 0; i < branches; ++i) {
                const auto entry = 16 + nodes * 8 + i * 2;
                const auto target = fr.word(entry);
                if (target != 0xffff && target >= nodes) throw std::invalid_argument("invalid BMG branch target");
                put(bytes, entry, target);
            }
        }
        put(bytes, 0, block.magic); put(bytes, 4, std::uint32_t(bytes.size()));
    }
    Bytes output(source.begin(), source.begin() + 32);
    for (const auto& block : blocks) output.insert(output.end(), block.bytes.begin(), block.bytes.end());
    put(output, 8, std::uint32_t(output.size())); put(output, 12, count);
    return output;
}
}
