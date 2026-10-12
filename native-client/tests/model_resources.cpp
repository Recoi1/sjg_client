#include "model_resource.hpp"
#include "rarc.hpp"
#include "yaz0.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using Bytes = std::vector<u8>;
static Bytes read(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary); assert(f); Bytes b{std::istreambuf_iterator<char>(f), {}};
    return b.size() >= 4 && !memcmp(b.data(), "Yaz0", 4) ? galaxy::decodeYaz0(b) : b;
}
static void reject(const Bytes& b) { bool failed = false; try { galaxy::importModel(b); } catch (const std::invalid_argument&) { failed = true; } assert(failed); }
int main(int argc, char** argv) {
    assert(argc == 2); unsigned models = 0;
    for (const auto* folder : {"ObjectData", "MapPartsData"}) for (const auto& item : std::filesystem::directory_iterator(std::filesystem::path(argv[1]) / folder)) {
        if (item.path().extension() != ".arc") continue;
        const auto bytes = read(item.path()); JKRMemArchive archive;
        assert(archive.mountFixed(const_cast<u8*>(bytes.data()), bytes.size(), JKR_MEM_BREAK_FLAG_0));
        for (unsigned i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& file = archive.mFiles[i]; if (!(file.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            const std::string_view name(archive.mStringTable + file.mNameOffset);
            if (!name.ends_with(".bdl") && !name.ends_with(".bmd") && !name.ends_with(".bmt") && !name.ends_with(".bti")) continue;
            auto* data = static_cast<const u8*>(archive.getIdxResource(i)); const auto size = archive.getResSize(data); assert(size > 32);
            try {
                const auto native = name.ends_with(".bti") ? galaxy::importTexture({data, std::size_t(size)}) : galaxy::importModel({data, std::size_t(size)}); assert(native.size() >= size); ++models;
                if (models == 1 && !name.ends_with(".bti")) {
                    const Bytes source(data, data + size);
                    for (unsigned n = 0; n < 64; ++n) reject({source.begin(), source.begin() + n});
                    auto invalid = source; invalid[0] = 0; reject(invalid);
                    invalid = source; invalid[36] = invalid[37] = invalid[38] = invalid[39] = 255; reject(invalid);
                }
            } catch (const std::exception& error) { std::cerr << item.path().filename() << '/' << name << ": " << error.what() << '\n'; return 1; }
        }
    }
    assert(models > 100);
    std::cout << models << " owned model and material resources imported; truncated and malformed records rejected\n";
    return 0;
}
