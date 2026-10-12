#include "bmg.hpp"
#include "rarc.hpp"
#include "yaz0.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using Bytes = std::vector<u8>;
void be16(Bytes& b, size_t p, u16 v) { b[p] = v >> 8; b[p + 1] = v; }
void be32(Bytes& b, size_t p, u32 v) { be16(b, p, v >> 16); be16(b, p + 2, v); }
template<class T> T host(const Bytes& b, size_t p) { T v; assert(p + sizeof(v) <= b.size()); std::memcpy(&v, b.data() + p, sizeof(v)); return v; }
template<class F> void reject(F f) { bool caught = false; try { f(); } catch (const std::invalid_argument&) { caught = true; } assert(caught); }
Bytes fixture() {
    Bytes b(224);
    std::memcpy(b.data(), "MESGbmg1", 8); be32(b, 8, b.size()); be32(b, 12, 3); b[16] = 2;
    be32(b, 32, 0x494e4631); be32(b, 36, 64); be16(b, 40, 2); be16(b, 42, 12);
    be32(b, 48, 2); be16(b, 52, 0x1234); be32(b, 60, 20);
    be32(b, 96, 0x44415431); be32(b, 100, 64);
    be16(b, 106, 'A'); be16(b, 108, 0x1a); b[110] = 14; b[111] = 7;
    be32(b, 118, 5); be16(b, 124, '?');
    be32(b, 160, 0x464c5731); be32(b, 164, 64); be16(b, 168, 3); be16(b, 170, 2);
    b[176] = 1; be16(b, 178, 1); be16(b, 180, 1);
    b[184] = 2; be16(b, 186, 2); be16(b, 190, 1);
    b[192] = 3; be16(b, 194, 1); be32(b, 196, 0x01020304);
    be16(b, 200, 2); be16(b, 202, 0xffff);
    return b;
}
Bytes read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary); if (!f) throw std::runtime_error("missing message archive");
    Bytes b{std::istreambuf_iterator<char>(f), {}};
    return b.size() >= 4 && std::memcmp(b.data(), "Yaz0", 4) == 0 ? galaxy::decodeYaz0(b) : b;
}
int main(int argc, char** argv) {
    assert(argc == 3);
    auto b = fixture(); const auto converted = galaxy::importBmg(b);
    assert(converted.size() == 256 && host<u32>(converted, 60) == 24);
    assert(host<u16>(converted, 52) == 0x1234 && host<u16>(converted, 106) == 'A');
    assert(converted[110] == 18 && converted[111] == 7);
    assert(host<uintptr_t>(converted, 114) == 0 && host<u32>(converted, 122) == 5);
    assert(host<u16>(converted, 128) == '?');
    assert(host<u32>(converted, 228) == 0x01020304 && host<u16>(converted, 232) == 2);
    reject([&] { galaxy::importBmg(std::span(b).first(31)); });
    auto bad = b; bad[110] = 0; reject([&] { galaxy::importBmg(bad); });
    bad = b; be32(bad, 48, 6); reject([&] { galaxy::importBmg(bad); });
    bad = b; be16(bad, 200, 99); reject([&] { galaxy::importBmg(bad); });
    bad = b; be32(bad, 114, 1); reject([&] { galaxy::importBmg(bad); });
    for (const auto* language : {"UsEnglish", "UsFrench", "UsSpanish"}) {
        const auto bytes = read(std::filesystem::path(argv[1]) / language / "MessageData/Message.arc");
        galaxy::Rarc archive(bytes);
        const auto resource = archive.resource("/message.bmg");
        assert(!galaxy::importBmg(resource).empty());
    }
    const auto errorBytes = read(argv[2]); galaxy::Rarc errors(errorBytes);
    for (const auto* language : {"UsEnglish", "UsFrench", "UsSpanish"}) {
        const auto packed = errors.resource(std::string("/") + language + "/MessageData/System.arc");
        galaxy::Rarc system(packed);
        assert(!galaxy::importBmg(system.resource("/Message.bmg")).empty());
    }
    std::cout << "BMG text, tags, native pointers and dialogue flow checks passed for all owned languages\n";
    return 0;
}
