#include "layout_resource.hpp"
#include "remix_resource.hpp"
#include "rarc.hpp"
#include "yaz0.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Bytes = std::vector<u8>;
void be16(Bytes& b, size_t p, u16 v) { b[p] = v >> 8; b[p + 1] = v; }
void be32(Bytes& b, size_t p, u32 v) { be16(b, p, v >> 16); be16(b, p + 2, v); }
template<class T> T host(const Bytes& b, size_t p) { T value; assert(p + sizeof(T) <= b.size()); memcpy(&value, b.data() + p, sizeof(T)); return value; }
template<class F> void reject(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } assert(caught); }
Bytes read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("missing resource archive");
    Bytes b{std::istreambuf_iterator<char>(file), {}};
    return b.size() >= 4 && !memcmp(b.data(), "Yaz0", 4) ? galaxy::decodeYaz0(b) : b;
}
unsigned layouts = 0, animations = 0;
void checkArchive(const Bytes& bytes) {
    // HOME uses the native menu; its separate U8 archive does not enter the BRLYT mount path.
    if (bytes.size() >= 4 && bytes[0] == 0x55 && bytes[1] == 0xaa && bytes[2] == 0x38 && bytes[3] == 0x2d) return;
    JKRMemArchive archive;
    assert(archive.mountFixed(const_cast<u8*>(bytes.data()), bytes.size(), JKR_MEM_BREAK_FLAG_0));
    for (u32 i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
        const auto& entry = archive.mFiles[i];
        if (!(entry.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
        const std::string_view name(archive.mStringTable + entry.mNameOffset);
        if (!name.ends_with(".brlyt") && !name.ends_with(".brlan")) continue;
        const auto* data = static_cast<const u8*>(archive.getIdxResource(i));
        const auto native = galaxy::importLayout({data, static_cast<size_t>(archive.getResSize(data))});
        assert(host<u32>(native, 8) == native.size() && host<u16>(native, 4) == 0xfeff);
        if (name.ends_with(".brlyt")) {
            auto originalWord = [&](size_t p) { return u32(data[p]) << 24 | u32(data[p + 1]) << 16 | u32(data[p + 2]) << 8 | data[p + 3]; };
            for (size_t p = host<u16>(native, 12); p < native.size(); p += host<u32>(native, p + 4)) {
                if (!memcmp(data + p, "pic1", 4)) for (unsigned j = 0; j < 4; ++j)
                    assert(host<u32>(native, p + 76 + j * 4) == originalWord(p + 76 + j * 4));
                if (!memcmp(data + p, "txt1", 4)) for (unsigned j = 0; j < 2; ++j)
                    assert(host<u32>(native, p + 92 + j * 4) == originalWord(p + 92 + j * 4));
                if (!memcmp(data + p, "wnd1", 4)) for (unsigned j = 0; j < 4; ++j) {
                    const auto c = p + host<u32>(native, p + 96) + j * 4;
                    assert(host<u32>(native, c) == originalWord(c));
                }
            }
        }
        if (name.ends_with(".brlyt")) ++layouts; else ++animations;
    }
}
int main(int argc, char** argv) {
    assert(argc == 3);
    Bytes b(112); memcpy(b.data(), "RLYT", 4); be16(b, 4, 0xfeff); be16(b, 6, 8);
    be32(b, 8, b.size()); be16(b, 12, 16); be16(b, 14, 2);
    memcpy(b.data() + 16, "lyt1", 4); be32(b, 20, 20); be32(b, 28, 0x43a00000); be32(b, 32, 0x43700000);
    memcpy(b.data() + 36, "pan1", 4); be32(b, 40, 76); memcpy(b.data() + 48, "Root", 5); be32(b, 72, 0x3fc00000);
    const auto native = galaxy::importLayout(b); assert(host<float>(native, 28) == 320 && host<float>(native, 72) == 1.5f);
    for (size_t n = 0; n < b.size(); ++n) reject([&] { galaxy::importLayout(std::span(b).first(n)); });
    auto bad = b; be32(bad, 40, 1000); reject([&] { galaxy::importLayout(bad); });
    bad = b; memcpy(bad.data() + 36, "pae1", 4); reject([&] { galaxy::importLayout(bad); });
    Bytes a(104); memcpy(a.data(), "RLAN", 4); be16(a, 4, 0xfeff); be16(a, 6, 8); be32(a, 8, a.size()); be16(a, 12, 16); be16(a, 14, 1);
    memcpy(a.data() + 16, "pai1", 4); be32(a, 20, 88); be16(a, 24, 120); be16(a, 30, 1); be32(a, 32, 20); be32(a, 36, 24);
    memcpy(a.data() + 40, "Root", 5); a[60] = 1; be32(a, 64, 28); memcpy(a.data() + 68, "RLPA", 4); a[72] = 1; be32(a, 76, 12);
    a[82] = 2; be16(a, 84, 1); be32(a, 88, 12); be32(a, 96, 0x3fc00000);
    const auto anim = galaxy::importLayout(a); assert(host<float>(anim, 96) == 1.5f && host<u32>(anim, 68) == 0x524c5041);
    bad = a; a[82] = 3; reject([&] { galaxy::importLayout(a); });
    Bytes remix(40); const u32 values[]{1, 0, 1, 1, 4, 5, 6, 7, 8, 0x100ff33};
    for (unsigned i = 0; i < 10; ++i) be32(remix, i * 4, values[i]);
    const auto notes = galaxy::importRemix(remix); assert(notes[9] == 0x100ff33);
    for (size_t n = 0; n < remix.size(); ++n) reject([&] { galaxy::importRemix(std::span(remix).first(n)); });
    bad = remix; be32(bad, 8, UINT32_MAX); reject([&] { galaxy::importRemix(bad); });
    const std::filesystem::path root(argv[1]);
    for (const auto* folder : {"LayoutData", "UsEnglish/LayoutData", "UsFrench/LayoutData", "UsSpanish/LayoutData"}) {
        if (!std::filesystem::exists(root / folder)) continue;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root / folder)) {
            if (entry.path().extension() != ".arc") continue;
            try { checkArchive(read(entry.path())); }
            catch (const std::exception& error) { std::cerr << entry.path() << ": " << error.what() << '\n'; return 1; }
        }
    }
    const auto errors = read(argv[2]); galaxy::Rarc archive(errors);
    const auto errorWindow = archive.resource("/LayoutData/ErrorMessageWindow.arc");
    checkArchive(Bytes(errorWindow.begin(), errorWindow.end()));
    const auto remixArchive = read(root / "AudioRes/Info/JaiRemixSeq.arc");
    JKRMemArchive music; assert(music.mountFixed(const_cast<u8*>(remixArchive.data()), remixArchive.size(), JKR_MEM_BREAK_FLAG_0));
    const auto* data = static_cast<const u8*>(music.getResource(static_cast<u16>(0)));
    assert(!galaxy::importRemix({data, static_cast<size_t>(music.getResSize(data))}).empty());
    std::cerr << "Checked " << layouts << " layouts and " << animations << " animations\n";
    assert(layouts > 20 && animations > 100);
    std::cout << layouts << " owned layouts and " << animations << " animations imported; remix and malformed resource checks passed\n";
    return 0;
}
