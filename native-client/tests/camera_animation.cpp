#include "Game/Camera/CameraAnim.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "animation_resource.hpp"
#include "yaz0.hpp"
#include "crash.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc, char** argv) {
    assert(argc == 2); galaxy::installCrashHandler();
    {
        std::vector<u8> source(132); std::memcpy(source.data(), "ANDOCANM", 8);
        auto word = [&](unsigned offset, u32 value) {
            for (unsigned n = 0; n < 4; ++n) source[offset + n] = value >> (24 - n * 8);
        };
        word(8, 1); word(24, 1); word(28, 64); word(96, 32);
        for (unsigned i = 0; i < 8; ++i) { word(32 + i * 8, 1); word(36 + i * 8, i); word(100 + i * 4, 0x3f800000); }
        auto native = galaxy::importCameraAnimation(source); CamAnmDataAccessor linear;
        linear.set(native.data() + 32, native.data() + 100);
        assert(linear.get(-1, 0, 1) == 1 && linear.get(2, 0, 1) == 1);
    }
    assert(JKRExpHeap::createRoot(1, false));
    SingletonHolder<NameObjRegister>::init(); NameObjHolder holder(8);
    SingletonHolder<NameObjRegister>::get()->setCurrentHolder(&holder);
    CameraAnim camera("Camera regression"); unsigned animations = 0, keys = 0;
    for (const auto& item : std::filesystem::recursive_directory_iterator(std::filesystem::path(argv[1]) / "StageData")) {
        if (item.path().extension() != ".arc") continue;
        std::ifstream file(item.path(), std::ios::binary); assert(file);
        std::vector<u8> bytes{std::istreambuf_iterator<char>(file), {}};
        if (bytes.size() >= 4 && !std::memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
        JKRMemArchive archive; assert(archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0));
        for (unsigned i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& entry = archive.mFiles[i]; if (!(entry.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            const std::string_view name(archive.mStringTable + entry.mNameOffset);
            if (!name.ends_with(".canm")) continue;
            auto* data = static_cast<u8*>(archive.getIdxResource(i)); const auto size = archive.getResSize(data);
            try {
                const auto original = std::vector<u8>(data, data + size);
                camera.mFileData = nullptr;
                camera.setParam(data, 1.0f);
                assert(camera.mNrFrames == CameraAnim::getAnimFrame(data));
                assert(std::memcmp(original.data(), data, size) == 0);
                auto* storage = camera.mNativeData.data(); camera.setParam(data, 0.5f);
                assert(storage == camera.mNativeData.data() && camera.mSpeed == 0.5f);
                keys += camera.mIsKey;
                for (float frame : {-1.0f, 0.0f, camera.mNrFrames / 2.0f, float(camera.mNrFrames - 1), float(camera.mNrFrames + 1)}) {
                    TVec3f pos, watch;
                    camera.mFileDataAccessor->getPos(&pos, frame); camera.mFileDataAccessor->getWatchPos(&watch, frame);
                    assert(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z));
                    assert(std::isfinite(watch.x) && std::isfinite(watch.y) && std::isfinite(watch.z));
                    assert(std::isfinite(camera.mFileDataAccessor->getTwist(frame)) && std::isfinite(camera.mFileDataAccessor->getFovy(frame)));
                }
                if (++animations == 1) {
                    for (std::size_t n = 0; n < std::min<std::size_t>(size, 140); ++n) {
                        bool rejected = false;
                        try { galaxy::importCameraAnimation({data, n}); } catch (const std::invalid_argument&) { rejected = true; }
                        assert(rejected);
                    }
                    auto invalid = original; invalid[28] = invalid[29] = invalid[30] = invalid[31] = 255;
                    bool rejected = false; try { galaxy::importCameraAnimation(invalid); } catch (const std::invalid_argument&) { rejected = true; }
                    assert(rejected);
                }
            } catch (const std::exception& error) { std::cerr << item.path().filename() << '/' << name << ": " << error.what() << '\n'; return 1; }
        }
    }
    assert(animations > 10 && keys > 0);
    std::cout << animations << " owned camera animations evaluated, including " << keys << " key animations\n";
    return 0;
}
