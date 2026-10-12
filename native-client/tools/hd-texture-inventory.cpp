#include "tpl.hpp"
#include "yaz0.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include <xxhash.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    for (const auto& path : std::filesystem::recursive_directory_iterator(argv[1])) {
        if (path.path().extension() != ".arc") continue;
        std::ifstream file(path.path(), std::ios::binary);
        std::vector<u8> bytes{std::istreambuf_iterator<char>(file), {}};
        if (bytes.size() >= 4 && !memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
        if (bytes.size() < 4 || memcmp(bytes.data(), "RARC", 4)) continue;
        JKRMemArchive archive;
        archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0);
        for (u32 i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& entry = archive.mFiles[i];
            if (!(entry.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            const std::string_view name(archive.mStringTable + entry.mNameOffset);
            const auto* data = static_cast<const u8*>(archive.getIdxResource(i));
            const std::span<const u8> tpl(data, archive.getResSize(data));
            if (name.ends_with(".brfnt")) {
                auto be16 = [&](size_t p) -> u16 {
                    if (p > tpl.size() || tpl.size() - p < 2) throw std::runtime_error("Truncated font");
                    return u16(data[p]) << 8 | data[p+1];
                };
                auto be32 = [&](size_t p) -> u32 { return u32(be16(p)) << 16 | be16(p+2); };
                if (tpl.size() < 16 || memcmp(data, "RFNT", 4)) throw std::runtime_error("Invalid font");
                size_t block = be16(12);
                for (unsigned section = 0; section < be16(14); ++section) {
                    const auto size = be32(block+4);
                    if (size < 8 || block > tpl.size() || size > tpl.size()-block) throw std::runtime_error("Invalid font block");
                    if (!memcmp(data+block, "TGLP", 4)) {
                        if (size < 32) throw std::runtime_error("Truncated font glyph header");
                        const auto sheetSize = be32(block+12), offset = be32(block+28);
                        const auto count = be16(block+16), format = be16(block+18);
                        const auto width = be16(block+24), height = be16(block+26);
                        if (!sheetSize || offset > tpl.size() || uint64_t(count)*sheetSize > tpl.size()-offset)
                            throw std::runtime_error("Truncated font sheets");
                        for (unsigned sheet = 0; sheet < count; ++sheet)
                            std::printf("%s\t%.*s[%u]\ttex1_%ux%u_%016llx_%u\n", path.path().filename().string().c_str(),
                                int(name.size()), name.data(), sheet, width, height,
                                static_cast<unsigned long long>(XXH64(data+offset+sheet*sheetSize, sheetSize, 0)), format);
                    }
                    block += size;
                }
                continue;
            }
            if (!name.ends_with(".tpl")) continue;
            if (tpl.size() < 12) throw std::runtime_error("Truncated TPL");
            const auto count = u32(data[4]) << 24 | u32(data[5]) << 16 | u32(data[6]) << 8 | data[7];
            for (u32 image = 0; image < count; ++image) {
                const auto info = galaxy::readTpl(tpl, image);
                const auto& t = info.texture;
                u32 bw = 4, bh = 4, size = 32;
                if (t.format == 0 || t.format == 8 || t.format == 14) bw = bh = 8;
                else if (t.format == 1 || t.format == 2 || t.format == 9) bw = 8;
                else if (t.format == 6) size = 64;
                const auto n = size_t((t.width+bw-1)/bw) * ((t.height+bh-1)/bh) * size;
                const auto* pixels = reinterpret_cast<const u8*>(t.data);
                std::printf("%s\t%.*s\ttex1_%ux%u_%016llx", path.path().filename().string().c_str(),
                    int(name.size()), name.data(), t.width, t.height, static_cast<unsigned long long>(XXH64(pixels, n, 0)));
                if (info.hasPalette) {
                    u32 lo = UINT32_MAX, hi = 0;
                    for (size_t k = 0; k < n; ++k) {
                        if (t.format == 8) { lo = std::min(lo, u32(pixels[k] >> 4)); hi = std::max(hi, u32(pixels[k] >> 4)); lo = std::min(lo, u32(pixels[k]&15)); hi = std::max(hi, u32(pixels[k]&15)); }
                        else if (t.format == 9) { lo = std::min(lo, u32(pixels[k])); hi = std::max(hi, u32(pixels[k])); }
                        else { const auto index = (u32(pixels[k]) << 8 | pixels[k+1]) & 16383; ++k; lo = std::min(lo, index); hi = std::max(hi, index); }
                    }
                    if (hi >= info.palette.numEntries) throw std::runtime_error("Palette index exceeds TPL palette");
                    std::printf("_%016llx", static_cast<unsigned long long>(XXH64(info.palette.data + lo*2, (hi+1-lo)*2, 0)));
                }
                std::printf("_%u\n", t.format);
            }
        }
    }
}
