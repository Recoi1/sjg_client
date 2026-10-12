#include "collision_resource.hpp"
#include "yaz0.hpp"
#include "crash.hpp"
#include "Game/Map/KCollision.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc, char** argv) {
    assert(argc == 2); galaxy::installCrashHandler(); auto* root = JKRExpHeap::createRoot(1, false); assert(root); unsigned files = 0, triangles = 0, hits = 0;
    for (const auto* folder : {"ObjectData", "MapPartsData"}) for (const auto& item : std::filesystem::directory_iterator(std::filesystem::path(argv[1]) / folder)) {
        if (item.path().extension() != ".arc") continue;
        std::ifstream f(item.path(), std::ios::binary); assert(f); std::vector<u8> bytes{std::istreambuf_iterator<char>(f), {}};
        if (!memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
        JKRMemArchive archive; assert(archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0));
        for (unsigned i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& file = archive.mFiles[i]; if (!(file.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            const std::string_view name(archive.mStringTable + file.mNameOffset); if (!name.ends_with(".kcl")) continue;
            const auto* data = static_cast<const u8*>(archive.getIdxResource(i)); const auto size = archive.getResSize(data);
            try {
                auto native = galaxy::importCollision({data, std::size_t(size)}); KCollisionServer server; server.setData(native.data());
                assert(server.getTriangleNum() >= 0); triangles += server.getTriangleNum();
                for (unsigned t = 0; t < unsigned(server.getTriangleNum()); ++t) {
                    auto* prism = server.getPrismData(t); if (server.isNearParallelNormal(prism)) continue;
                    TVec3f center(0, 0, 0);
                    for (unsigned v = 0; v < 3; ++v) { const auto p = server.getPos(prism, v); assert(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)); center += p; }
                    center *= 1.f / 3;
                    if (t % 257 == 0) {
                        const auto* normal = server.getFaceNormal(prism); TVec3f start(center), direction(*normal); start += *normal; direction *= -2;
                        float distances[16]{}; u8 feature; u32 count = 0; KC_PrismData* found[16]{};
                        if (server.checkArrow(start, direction, distances, &feature, &count, found, 16)) { assert(std::isfinite(distances[0]) && count <= 16); ++hits; }
                    }
                }
                if (!files) {
                    for (unsigned n = 0; n < 56; ++n) { bool rejected = false; try { galaxy::importCollision({data, n}); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected); }
                    auto malformed = std::vector<u8>(data, data + size); memset(malformed.data() + 12, 255, 4); bool rejected = false;
                    try { galaxy::importCollision(malformed); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected);
                }
                delete server.mapInfo; ++files; assert(root->check());
            } catch (const std::exception& e) { std::cerr << item.path().filename() << '/' << name << ": " << e.what() << '\n'; return 1; }
        }
    }
    assert(files > 100 && triangles > 1000 && hits > 100); std::cout << files << " collision files, " << triangles << " triangles, " << hits << " native ray intersections passed\n";
}
