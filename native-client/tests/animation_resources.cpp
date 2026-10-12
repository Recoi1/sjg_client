#include "animation_resource.hpp"
#include "JSystem/JAudio2/JAUSoundAnimator.hpp"
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
static void reject(const Bytes& b) { bool failed = false; try { galaxy::importAnimation(b); } catch (const std::invalid_argument&) { failed = true; } assert(failed); }
int main(int argc, char** argv) {
    assert(argc == 2); unsigned models = 0, sounds = 0;
    for (const auto* folder : {"ObjectData", "MapPartsData"}) for (const auto& item : std::filesystem::directory_iterator(std::filesystem::path(argv[1]) / folder)) {
        if (item.path().extension() != ".arc") continue;
        const auto bytes = read(item.path()); JKRMemArchive archive;
        assert(archive.mountFixed(const_cast<u8*>(bytes.data()), bytes.size(), JKR_MEM_BREAK_FLAG_0));
        for (unsigned i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& file = archive.mFiles[i]; if (!(file.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            const std::string_view name(archive.mStringTable + file.mNameOffset);
            if (name.ends_with(".bas")) {
                auto* data = static_cast<const u8*>(archive.getIdxResource(i)); const auto size = archive.getResSize(data);
                try {
                    const auto native = galaxy::importSoundAnimation({data, std::size_t(size)});
                    auto* animation = reinterpret_cast<const JAUSoundAnimation*>(native.data());
                    assert(animation->mControl == nullptr && animation->getNumSounds() == (data[0] << 8 | data[1]));
                    for (unsigned n = 0; n < animation->getNumSounds(); ++n) assert(std::isfinite(animation->getSound(n)->mNoteOnTime));
                    if (++sounds == 1) {
                        for (unsigned n = 0; n < size; ++n) {
                            if (n >= 8 + animation->getNumSounds() * 32) break;
                            bool failed = false; try { galaxy::importSoundAnimation({data, n}); } catch (const std::invalid_argument&) { failed = true; } assert(failed);
                        }
                    }
                } catch (const std::exception& error) { std::cerr << item.path().filename() << '/' << name << ": " << error.what() << '\n'; return 1; }
                continue;
            }
            if (!name.ends_with(".bck") && !name.ends_with(".bca") && !name.ends_with(".btk") && !name.ends_with(".bpk") && !name.ends_with(".bpa") && !name.ends_with(".brk") && !name.ends_with(".btp") && !name.ends_with(".bva") && !name.ends_with(".blk") && !name.ends_with(".bla")) continue;
            auto* data = static_cast<const u8*>(archive.getIdxResource(i)); const auto size = archive.getResSize(data); assert(size > 32);
            try {
                const auto native = galaxy::importAnimation({data, std::size_t(size)}); assert(native.size() >= size); ++models;
                if (models == 1) {
                    const Bytes source(data, data + size);
                    for (unsigned n = 0; n < 64; ++n) reject({source.begin(), source.begin() + n});
                    auto invalid = source; invalid[0] = 0; reject(invalid);
                    invalid = source; invalid[36] = invalid[37] = invalid[38] = invalid[39] = 255; reject(invalid);
                }
            } catch (const std::exception& error) { std::cerr << item.path().filename() << '/' << name << ": " << error.what() << '\n'; return 1; }
        }
    }
    assert(models > 100 && sounds > 100);
    std::cout << sounds << " owned sound animation resources imported\n";
    std::cout << models << " owned animation resources imported; truncated and malformed records rejected\n";
    return 0;
}
