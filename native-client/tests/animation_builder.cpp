#include "yaz0.hpp"
#include "crash.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/J3DGraphLoader/J3DAnmLoader.hpp"
#include "JSystem/J3DGraphAnimator/J3DAnimation.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
static void finite(float f) { assert(std::isfinite(f)); }
static void evaluate(J3DAnmBase& animation) {
    const auto* bytes = animation.mNativeResource.data();
    auto count = [&](unsigned p) { u16 value; memcpy(&value, bytes + 32 + p, 2); return value; };
    for (float frame : {-1.0f, 0.0f, float(animation.getFrameMax()) / 2 + 0.25f, float(animation.getFrameMax()) + 1}) {
        animation.setFrame(frame);
        switch (animation.getKind()) {
        case 8: case 9: case 16: {
            auto& a = static_cast<J3DAnmTransform&>(animation);
            for (u16 i = 0; i < a.field_0x1e; ++i) {
                J3DTransformInfo t{}; a.getTransform(i, &t);
                finite(t.mScale.x); finite(t.mScale.y); finite(t.mScale.z); finite(t.mTranslate.x); finite(t.mTranslate.y); finite(t.mTranslate.z);
            } break;
        }
        case 4: {
            auto& a = static_cast<J3DAnmTextureSRTKey&>(animation);
            for (u16 i = 0; i < a.getUpdateMaterialNum(); ++i) {
                J3DTextureSRTInfo t{}; a.getTransform(i, &t); finite(t.mScaleX); finite(t.mScaleY); finite(t.mTranslationX); finite(t.mTranslationY);
                assert(a.mUpdateMaterialName.getName(i));
            } break;
        }
        case 10: case 11: {
            auto& a = static_cast<J3DAnmColor&>(animation);
            for (u16 i = 0; i < a.getUpdateMaterialNum(); ++i) { GXColor c{}; a.getColor(i, &c); assert(a.mUpdateMaterialName.getName(i)); } break;
        }
        case 5: {
            auto& a = static_cast<J3DAnmTevRegKey&>(animation);
            for (u16 i = 0; i < a.getCRegUpdateMaterialNum(); ++i) { GXColorS10 c{}; a.getTevColorReg(i, &c); }
            for (u16 i = 0; i < a.getKRegUpdateMaterialNum(); ++i) { GXColor c{}; a.getTevKonstReg(i, &c); } break;
        }
        case 2: {
            auto& a = static_cast<J3DAnmTexPattern&>(animation);
            for (u16 i = 0; i < a.getUpdateMaterialNum(); ++i) { u16 texture; a.getTexNo(i, &texture); assert(a.mUpdateMaterialName.getName(i)); } break;
        }
        case 6: {
            auto& a = static_cast<J3DAnmVisibilityFull&>(animation);
            for (u16 i = 0; i < a.mUpdateMaterialNum; ++i) { u8 visibility; a.getVisibility(i, &visibility); assert(visibility <= 1); } break;
        }
        case 12: case 13: {
            auto& a = static_cast<J3DAnmCluster&>(animation); for (u16 i = 0; i < count(12); ++i) finite(a.getWeight(i)); break;
        }
        default: assert(false);
        }
    }
}
int main(int argc, char** argv) {
    assert(argc == 2); galaxy::installCrashHandler();
    static_assert(sizeof(J3DAnmTransformKeyData) == 36 && sizeof(J3DAnmTextureSRTKeyData) == 96 && sizeof(J3DAnmTevRegKeyData) == 88);
    J3DTextureSRTInfo original{2, 3, 123, 4, 5}, copy{}; copy = original;
    assert(copy.mScaleX == 2 && copy.mScaleY == 3 && copy.mRotation == 123 && copy.mTranslationX == 4 && copy.mTranslationY == 5);
    Mtx matrix{{1,2,3,4},{5,6,7,8},{9,10,11,12}}; J3DTexMtxInfo tex{}; tex.setEffectMtx(matrix);
    assert(tex.mEffectMtx[2][3] == 12 && tex.mEffectMtx[3][0] == 0 && tex.mEffectMtx[3][3] == 1);
    auto* root = JKRExpHeap::createRoot(1, false); assert(root); unsigned animations = 0;
    for (const auto* folder : {"ObjectData", "MapPartsData"}) for (const auto& item : std::filesystem::directory_iterator(std::filesystem::path(argv[1]) / folder)) {
        if (item.path().extension() != ".arc") continue; root->becomeCurrentHeap();
        std::ifstream f(item.path(), std::ios::binary); assert(f); std::vector<u8> bytes{std::istreambuf_iterator<char>(f), {}};
        if (!memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
        JKRMemArchive archive; assert(archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0));
        for (unsigned i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& file = archive.mFiles[i]; if (!(file.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            const std::string_view name(archive.mStringTable + file.mNameOffset);
            if (!name.ends_with(".bck") && !name.ends_with(".bca") && !name.ends_with(".btk") && !name.ends_with(".bpk") && !name.ends_with(".bpa") && !name.ends_with(".brk") && !name.ends_with(".btp") && !name.ends_with(".bva") && !name.ends_with(".blk") && !name.ends_with(".bla")) continue;
            try {
                const auto disposers = root->mDisposerList.getNumLinks();
                std::unique_ptr<J3DAnmBase> animation(J3DAnmLoaderDataBase::load(archive.getIdxResource(i))); assert(animation && !animation->mNativeResource.empty()); assert(root->mDisposerList.getNumLinks() == disposers + 1); evaluate(*animation); animation.reset(); assert(root->mDisposerList.getNumLinks() == disposers); ++animations;
            } catch (const std::exception& e) { std::cerr << item.path().filename() << '/' << name << ": " << e.what() << '\n'; return 1; }
            assert(root->check());
        }
    }
    assert(animations > 100); std::cout << animations << " original animation constructions and all-track frame evaluations passed\n";
}
