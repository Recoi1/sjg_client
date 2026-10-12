#include "rarc.hpp"
#include "yaz0.hpp"
#ifdef GALAXY_EMBEDDED_ERROR_ARCHIVE
#include "Game/System/ErrorArchive.hpp"
#endif
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRFileFinder.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void put32(std::vector<u8>& bytes, size_t offset, u32 value) {
    for (size_t i = 0; i < 4; ++i) bytes[offset + i] = u8(value >> (24 - i * 8));
}

std::vector<u8> fixture() {
    std::vector<u8> bytes(0x84);
    put32(bytes, 0, RARC_MAGIC);
    put32(bytes, 4, u32(bytes.size()));
    put32(bytes, 8, 0x20);
    put32(bytes, 12, 0x60);
    put32(bytes, 16, 4);
    put32(bytes, 20, 4);
    put32(bytes, 0x20, 1);
    put32(bytes, 0x24, 0x20);
    put32(bytes, 0x28, 1);
    put32(bytes, 0x2c, 0x30);
    put32(bytes, 0x30, 14);
    put32(bytes, 0x34, 0x44);
    put32(bytes, 0x40, 0x524f4f54);
    bytes[0x4b] = 1;
    u16 hash = 0;
    for (const char ch : std::string_view("test.bin")) hash = hash * 3 + ch;
    bytes[0x52] = u8(hash >> 8);
    bytes[0x53] = u8(hash);
    put32(bytes, 0x54, 0x11000005);
    put32(bytes, 0x5c, 4);
    put32(bytes, 0x60, 0xdeadbeef);
    const char strings[] = "ROOT\0test.bin\0";
    std::copy_n(strings, 14, bytes.begin() + 0x64);
    std::copy_n("ABCD", 4, bytes.begin() + 0x80);
    return bytes;
}

template<class F> void rejected(F action) {
    bool caught = false;
    try { action(); } catch (const std::runtime_error&) { caught = true; } catch (const std::invalid_argument&) { caught = true; }
    assert(caught);
}

void selfCheck() {
    const auto bytes = fixture();
    galaxy::Rarc archive(bytes);
    const auto member = archive.resource("/Test.BIN");
    assert(member.size() == 4 && std::equal(member.begin(), member.end(), "ABCD"));
    rejected([&] { archive.resource("missing"); });
    rejected([&] { archive.resource("test.bin/child"); });
    for (size_t end = 0; end < bytes.size(); ++end)
        rejected([&] { galaxy::Rarc truncated(std::span<const u8>(bytes).first(end)); });
    const std::pair<size_t, u32> corruptions[] = {
        {0, 0}, {4, 0x100}, {8, 0xffffffff}, {12, 0xfffffff0}, {16, 0xffffffff},
        {20, 5}, {0x20, 0}, {0x20, 0xffffffff}, {0x24, 0}, {0x28, 0xffffffff},
        {0x2c, 0x20}, {0x34, 0x30}, {0x44, 14}, {0x4c, 1},
        {0x54, 0x110000ff}, {0x54, 0x13000005}, {0x58, 0xffffffff}, {0x5c, 5},
    };
    for (const auto& [offset, value] : corruptions) {
        auto bad = bytes;
        put32(bad, offset, value);
        rejected([&] { galaxy::Rarc invalid(bad); });
    }
    auto bad = bytes;
    put32(bad, 0x54, 0x02000005);
    put32(bad, 0x58, 1);
    rejected([&] { galaxy::Rarc invalid(bad); });
    bad = bytes;
    std::fill(bad.begin() + 0x64, bad.begin() + 0x72, u8('A'));
    rejected([&] { galaxy::Rarc invalid(bad); });
    auto compressed = bytes;
    put32(compressed, 0x54, 0x15000005);
    galaxy::Rarc stored(compressed);
    rejected([&] { stored.resource("test.bin"); });
    auto empty = bytes;
    put32(empty, 0x5c, 0);
    galaxy::Rarc zeroSize(empty);
    assert(zeroSize.resource("test.bin").empty());
    std::cout << "RARC boundary and lookup checks passed\n";
}

void gameApiCheck() {
    auto bytes = fixture();
    const auto volumes = JKRFileLoader::sVolumeList.mNodeCount;
    {
        JKRMemArchive archive;
        assert(archive.getResource("test.bin") == nullptr);
        rejected([&] { archive.mountFixed(bytes.data(), JKR_MEM_BREAK_FLAG_0); });
        rejected([&] { archive.mountFixed(bytes.data(), u32(bytes.size() - 1), JKR_MEM_BREAK_FLAG_0); });
        assert(JKRFileLoader::sVolumeList.mNodeCount == volumes);
        assert(archive.mountFixed(bytes.data(), u32(bytes.size()), JKR_MEM_BREAK_FLAG_0));
        std::fill(bytes.begin(), bytes.end(), 0);
        auto* resource = static_cast<u8*>(archive.getResource("/Test.BIN"));
        assert(resource && std::equal(resource, resource + 4, "ABCD"));
        assert(archive.getResSize(resource) == 4 && archive.getExpandedResSize(resource) == 4);
        assert(archive.getResource(u16(0)) == resource);
        assert(archive.getResource(u16(65000)) == nullptr);
        assert(archive.getIdxResource(0) == resource && archive.getIdxResource(1) == nullptr);
        assert(archive.getResource(u32(0), "TEST.BIN") == resource);
        assert(JKRFileLoader::getGlbResource("test.bin", &archive) == resource);
        assert(archive.getResource(static_cast<const char*>(nullptr)) == nullptr);
        assert(archive.getResSize(nullptr) == -1 && archive.getResource("missing") == nullptr);
        rejected([&] { archive.getResource(std::string(256, 'A').c_str()); });
        std::array<u8, 6> output{};
        assert(archive.readResource(output.data(), 3, "test.bin") == 3);
        assert(std::equal(output.begin(), output.begin() + 3, "ABC") && output[3] == 0);
        assert(archive.readResource(output.data(), 6, u16(0)) == 4);
        assert(archive.readResource(nullptr, 0, "test.bin") == 0);
        assert(archive.readResource(nullptr, 4, "test.bin") == 0);
        assert(archive.countResource() == 1 && archive.countFile("/") == 1);
        std::unique_ptr<JKRArcFinder> finder(archive.getFirstFile("/"));
        assert(finder && finder->mHasMoreFiles && std::string_view(finder->mName) == "test.bin");
        assert(!finder->findNextFile());
        assert(archive.removeResource(resource));
        assert(archive.getResSize(resource) == -1);
        assert(archive.getResource("test.bin") == resource);
        rejected([&] { archive.mountFixed(bytes.data(), u32(bytes.size()), JKR_MEM_BREAK_FLAG_0); });
    }
    assert(JKRFileLoader::sVolumeList.mNodeCount == volumes);
    std::cout << "Original JKR archive API and ownership checks passed\n";
}

void yaz0Check() {
    std::vector<u8> encoded{'Y','a','z','0', 0,0,0,24, 0,0,0,0, 0,0,0,0, 0xe0,'A','B','C',0,2,3};
    const auto decoded = galaxy::decodeYaz0(encoded);
    assert(decoded.size() == 24);
    for (size_t i = 0; i < decoded.size(); ++i) assert(decoded[i] == "ABC"[i % 3]);
    for (size_t end = 0; end < encoded.size(); ++end)
        rejected([&] { galaxy::decodeYaz0(std::span<const u8>(encoded).first(end)); });
    auto bad = encoded;
    bad[16] = 0;
    rejected([&] { galaxy::decodeYaz0(bad); });
    bad = encoded;
    put32(bad, 4, 64 * 1024 * 1024 + 1);
    rejected([&] { galaxy::decodeYaz0(bad); });
    auto zero = encoded;
    put32(zero, 4, 0);
    zero.resize(16);
    assert(galaxy::decodeYaz0(zero).empty());
    std::vector<u8> shortGroup{'Y','a','z','0', 0,0,0,1, 0,0,0,0, 0,0,0,0, 0xff,'A'};
    assert(galaxy::decodeYaz0(shortGroup) == std::vector<u8>{'A'});
    std::cout << "Native Yaz0 bounds and overlap checks passed\n";
}

#ifdef GALAXY_EMBEDDED_ERROR_ARCHIVE
void embeddedArchiveCheck() {
    const auto volumes = JKRFileLoader::sVolumeList.mNodeCount;
    {
        JKRMemArchive archive;
        assert(archive.mountFixed(const_cast<u8*>(cErrorArchive), sizeof(cErrorArchive), JKR_MEM_BREAK_FLAG_0));
        assert(archive.countResource() == 12);
        u32 expanded = 0;
        for (u32 i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& file = archive.mFiles[i];
            if (!(file.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            auto* stored = static_cast<u8*>(archive.getIdxResource(i));
            assert(stored && archive.getResSize(stored) == s32(file.mDataSize));
            const u32 size = archive.getExpandedResSize(stored);
            std::vector<u8> decoded(size);
            assert(archive.readResource(decoded.data(), size, file.mFileID) == size);
            JKRMemArchive nested;
            assert(nested.mountFixed(decoded.data(), size, JKR_MEM_BREAK_FLAG_0));
            assert(nested.countResource() > 0);
            std::array<u8, 5> prefix{};
            assert(archive.readResource(prefix.data(), 4, file.mFileID) == 4);
            assert(std::equal(prefix.begin(), prefix.begin() + 4, "RARC") && prefix[4] == 0);
            ++expanded;
        }
        assert(expanded == 12);
        auto* font = static_cast<u8*>(archive.getResource("/LayoutData/EmbeddedFont.arc"));
        const auto fontBytes = galaxy::decodeYaz0({font, u32(archive.getResSize(font))});
        galaxy::Rarc fontArchive(fontBytes);
        const auto messageFont = fontArchive.resource("messagefont26.brfnt");
        assert(messageFont.size() > 4 && std::equal(messageFont.begin(), messageFont.begin() + 4, "RFNT"));
    }
    assert(JKRFileLoader::sVolumeList.mNodeCount == volumes);
    std::cout << "Embedded boot archive: all 12 compressed archives and the real RFNT font loaded\n";
}
#endif

void realAssets(const std::filesystem::path& root, const std::filesystem::path& mountedRoot) {
    JKRMemArchive::setNativeResourceRoot(mountedRoot.string().c_str());
    const auto volumes = JKRFileLoader::sVolumeList.mNodeCount;
    struct Asset { const char* archive; const char* member; const char* magic; u32 records; };
    const Asset assets[] = {
        {"LayoutData/TitleLogo.arc", "blyt/titlelogo.brlyt", "RLYT", 0},
        {"LayoutData/PressStart.arc", "blyt/pressstart.brlyt", "RLYT", 0},
        {"ObjectData/Mario.arc", "mario.bdl", "J3D2", 0},
        {"StageData/HeavensDoorGalaxy.arc", "jmp/placement/common/objinfo", "", 3},
        {"StageData/HeavensDoorGalaxy/HeavensDoorGalaxyScenario.arc", "scenariodata.bcsv", "", 2},
        {"StageData/EggStarGalaxy.arc", "jmp/placement/common/objinfo", "", 21},
    };
    for (const auto& asset : assets) {
        const auto path = root / asset.archive;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Cannot open asset: " + path.string());
        const auto length = file.tellg();
        if (length < 0 || length > 64 * 1024 * 1024) throw std::runtime_error("Invalid archive size");
        std::vector<u8> bytes(static_cast<size_t>(length));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
            throw std::runtime_error("Cannot read archive");
        galaxy::Rarc archive(bytes);
        const auto member = archive.resource(asset.member);
        const auto gamePath = "/" + std::string(asset.archive);
        std::unique_ptr<JKRArchive, void(*)(JKRArchive*)> mounted(
            JKRArchive::mount(gamePath.c_str(), JKRArchive::MOUNT_MODE_MEM, nullptr, JKRArchive::MOUNT_DIRECTION_1),
            [](JKRArchive* archive) { archive->unmount(); });
        auto* gameResource = static_cast<u8*>(mounted->getResource(("/" + std::string(asset.member)).c_str()));
        assert(static_cast<JKRMemArchive*>(mounted.get())->mNativeBytes == bytes);
        assert(gameResource && mounted->getResSize(gameResource) == s32(member.size()));
        assert(std::equal(member.begin(), member.end(), gameResource));
        auto* again = JKRArchive::mount(asset.archive, JKRArchive::MOUNT_MODE_MEM, nullptr, JKRArchive::MOUNT_DIRECTION_1);
        assert(again == mounted.get() && again->_34 == 2);
        mounted.reset();
        assert(again->_34 == 1 && again->getResSize(gameResource) == s32(member.size()));
        mounted.reset(again);
        assert(mounted->getResource("/../missing") == nullptr);
        assert(!mounted->becomeCurrent("/.."));
        const auto slash = std::string_view(asset.member).find_last_of('/');
        if (slash != std::string_view::npos) {
            const auto directory = "/" + std::string(asset.member, slash);
            assert(mounted->becomeCurrent(directory.c_str()));
            assert(mounted->getResource(asset.member + slash + 1) == gameResource);
            assert(mounted->becomeCurrent("/"));
        }
        if (member.size() < 4) throw std::runtime_error("Truncated resource");
        if (*asset.magic) {
            if (!std::equal(member.begin(), member.begin() + 4, asset.magic))
                throw std::runtime_error("Unexpected resource signature");
        } else {
            const u32 records = (u32(member[0]) << 24) | (u32(member[1]) << 16)
                              | (u32(member[2]) << 8) | member[3];
            if (records != asset.records) throw std::runtime_error("Unexpected stage record count");
        }
        std::cout << "Mounted through JKRArchive: " << asset.archive << ": " << asset.member << " (" << member.size() << " bytes)\n";
    }
    assert(JKRFileLoader::sVolumeList.mNodeCount == volumes);
    for (const char* invalid : {"../README.md", "C:/README.md", "\\ObjectData\\Mario.arc", "//ObjectData/Mario.arc"})
        rejected([&] { JKRArchive::mount(invalid, JKRArchive::MOUNT_MODE_MEM, nullptr, JKRArchive::MOUNT_DIRECTION_1); });
}
}

int main(int argc, char** argv) {
    try {
        selfCheck();
        gameApiCheck();
        yaz0Check();
#ifdef GALAXY_EMBEDDED_ERROR_ARCHIVE
        embeddedArchiveCheck();
#endif
        auto* root = JKRExpHeap::createRoot(1, false);
        assert(root);
        const int rootFree = root->getTotalFreeSize();
        auto* resourceHeap = JKRExpHeap::create(16 * 1024 * 1024, root, false);
        assert(resourceHeap);
        resourceHeap->becomeCurrentHeap();
        gameApiCheck();
#ifdef GALAXY_EMBEDDED_ERROR_ARCHIVE
        embeddedArchiveCheck();
#endif
        assert(resourceHeap->check() && resourceHeap->isEmpty());
        root->becomeCurrentHeap();
        resourceHeap->destroy();
        assert(root->check() && root->getTotalFreeSize() == rootFree);
        root->destroy();
        if (argc >= 2) realAssets(std::filesystem::path(argv[1]), std::filesystem::path(argv[1]));
        if (argc == 3) realAssets(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
        else if (argc > 3) throw std::runtime_error("Usage: resource_checks [prepared-asset-folder] [extracted-files-folder]");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
