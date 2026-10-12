#include "model_resource.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/FurShader.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/JUtility/JUTNameTab.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Animation/XanimeResource.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "JSystem/J3DGraphAnimator/J3DAnimation.hpp"
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "nw4r/ut/Color.h"
#include "Game/Util/Color.hpp"
#include "Game/RhythmLib/AudMeTrack.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"
#include "JSystem/J3DGraphBase/J3DShapeDraw.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTevs.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp"
#include "JSystem/J3DGraphAnimator/J3DModel.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"
#include <stdexcept>
#include "yaz0.hpp"
#include "crash.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/J3DGraphLoader/J3DModelLoader.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <array>
extern XanimeGroupInfo marioAnimeTable[];
extern XanimeAuxInfo marioAnimeAuxTable[];
extern XanimeOfsInfo marioAnimeOfsTable[];
extern XanimeBckTable1 singleAnimeTable[];
extern XanimeBckTable2 doubleAnimeTable[];
extern XanimeBckTable3 tripleAnimeTable[];
extern XanimeBckTable4 quadAnimeTable[];
template<class Track, class List> void checkTrackList(List& list) {
    alignas(Track) unsigned char storage[3][sizeof(Track)]; memset(storage, 0xa5, sizeof(storage));
    Track* tracks[3];
    for (unsigned i = 0; i < 3; ++i) {
        tracks[i] = reinterpret_cast<Track*>(storage[i]);
        new (&tracks[i]->mNode) JGadget::TLinkListNode(); list.Push_back(tracks[i]);
        assert(List::Element_toNode(tracks[i]) == &tracks[i]->mNode);
    }
    assert(list.size() == 3 && *list.begin() == tracks[0]); list.Remove(tracks[1]);
    auto it = list.begin(); assert(*it == tracks[0]); ++it; assert(*it == tracks[2]); ++it; assert(it == list.end());
    list.Remove(tracks[0]); list.Remove(tracks[2]); assert(list.size() == 0 && list.begin() == list.end());
    for (const auto& bytes : storage) for (unsigned i = 0; i < sizeof(Track); ++i)
        if (i < offsetof(Track, mNode) || i >= offsetof(Track, mNode) + sizeof(JGadget::TLinkListNode)) assert(bytes[i] == 0xa5);
}
int main(int argc, char** argv) {
    assert(argc == 2); galaxy::installCrashHandler();
    {
        alignas(J3DMatPacket) unsigned char storage[sizeof(J3DMatPacket)];
        std::memset(storage, 0xa5, sizeof storage);
        auto* packet = new (storage) J3DMatPacket();
        assert(packet->getShapePacket() == nullptr);
        J3DShapePacket shape;
        packet->addShapePacket(&shape);
        assert(packet->getShapePacket() == &shape && shape.getNextPacket() == nullptr);
        packet->~J3DMatPacket();
    }
    {
        alignas(32) std::array<u8, 256> bytes{};
        GDLObj commands;
        GDInitGDLObj(&commands, bytes.data(), bytes.size()); GDSetCurrent(&commands);
        J3DTevStage stage;
        stage.setStageNo(0);
        stage.setTevColorAB(GX_CC_ZERO, GX_CC_C0); stage.setTevColorCD(GX_CC_RASC, GX_CC_C0);
        stage.setTevColorOp(GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        stage.setAlphaABCD(GX_CA_TEXA, GX_CA_KONST, GX_CA_RASA, GX_CA_A0);
        stage.setTevAlphaOp(GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
        stage.load(0);
        const std::array<u8, 10> expected{0x61, 0xc0, 0x08, 0xf2, 0xa2, 0x61, 0xc1, 0x18, 0x9a, 0x90};
        assert(GDGetCurrOffset() == expected.size() && !memcmp(bytes.data(), expected.data(), expected.size()));
        GDSetCurrOffset(0);
        J3DColorBlockAmbientOn colors;
        colors.setMatColor(0, J3DGXColor(GXColor{0x12,0x34,0x56,0x78}));
        colors.setMatColor(1, J3DGXColor(GXColor{0x9a,0xbc,0xde,0xf0}));
        colors.setAmbColor(0, J3DGXColor(GXColor{0x21,0x43,0x65,0x87}));
        colors.setAmbColor(1, J3DGXColor(GXColor{0x09,0x8b,0xad,0xcf}));
        colors.load();
        const std::array<u8, 8> material{0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
        const std::array<u8, 8> ambient{0x21,0x43,0x65,0x87,0x09,0x8b,0xad,0xcf};
        assert(!memcmp(bytes.data()+5, material.data(), material.size()));
        assert(!memcmp(bytes.data()+18, ambient.data(), ambient.size()));
        colors.setMatColor(0, J3DGXColor(GXColor{0x78,0x56,0x34,0x12})); colors.patchMatColor();
        assert(bytes[5] == 0x78 && bytes[8] == 0x12);
        GDSetCurrent(nullptr);
    }
    {
        J3DModelX::Flags flags; flags.clear(); flags._1E = true; flags._11 = true;
        for (unsigned i = 0; i < 16; ++i) assert(flags.usesDisplayList(i) == (i == 1 || i == 14));
        flags.clear(); for (unsigned i = 0; i < 16; ++i) assert(!flags.usesDisplayList(i));
    }
    {
        nw4r::ut::Color color(0x12345678);
        assert(color.r == 0x12 && color.g == 0x34 && color.b == 0x56 && color.a == 0x78);
        assert(u32(color) == 0x12345678);
        color = GXColor{0xab, 0xcd, 0xef, 0x81};
        assert(u32(color) == 0xabcdef81);
        color = 0x11223344; assert(color.r == 0x11 && color.a == 0x44);
        Color8 effect(0x21324354); assert(effect.r == 0x21 && effect.a == 0x54 && u32(effect) == 0x21324354);
        Color8 shadow(128); const GXColor channels = shadow;
        assert(channels.r == 0 && channels.g == 0 && channels.b == 0 && channels.a == 128);
    }
    {
        wchar_t stringFormat[28]{};
        for (unsigned i = 0; i < 3; ++i) {
            auto* tag = stringFormat + i * 9; tag[0] = 0x1a;
            auto* bytes = reinterpret_cast<u8*>(tag + 1); bytes[0] = 18; bytes[1] = 7;
            const u32 index = i == 1 ? 0 : 1; memcpy(bytes + 12, &index, sizeof(index));
        }
        wchar_t output[64]{};
        ReplaceTagFunction::ReplaceArgs(output, 64, stringFormat, L"Mario", L"Galaxy");
        assert(!wcscmp(output, L"GalaxyMarioGalaxy"));
        wchar_t numberFormat[15]{};
        for (unsigned i = 0; i < 2; ++i) {
            auto* tag = numberFormat + i * 7; tag[0] = 0x1a;
            auto* bytes = reinterpret_cast<u8*>(tag + 1); bytes[0] = 14; bytes[1] = 6; tag[2] = 5;
            const u32 index = 1 - i; memcpy(bytes + 8, &index, sizeof(index));
        }
        ReplaceTagFunction::ReplaceArgs(output, 64, numberFormat, 1, 7);
        assert(!wcscmp(output, L"0701"));
        wchar_t small[5]{0,0,0,0,0x7777};
        ReplaceTagFunction::ReplaceArgs(small, 4, stringFormat, L"Mario", L"Galaxy");
        assert(!wcscmp(small, L"Gal") && small[4] == 0x7777);
    }
    static_assert(sizeof(J3DModelInfoBlock) == 24 && sizeof(J3DVertexBlock) == 64 && sizeof(J3DMaterialBlock) == 132);
    auto* root = JKRExpHeap::createRoot(1, false); assert(root); unsigned models = 0, controlsChecked = 0;
    {
        static_assert(sizeof(ResTIMG) == 32);
        ResTIMG original[2]{};
        struct Capture { ResTIMG header; u8 pixels[32]; } capture{};
        capture.header.mImageDataOffset = offsetof(Capture, pixels);
        J3DTexture textures(2, original);
        assert(textures.getResTIMG(0) == original && textures.getResTIMG(1) == original + 1);
        textures.setResTIMG(1, capture.header);
        auto* replacement = textures.getResTIMG(1);
        assert(replacement == &capture.header && textures.getResTIMG(0) == original);
        assert(reinterpret_cast<u8*>(replacement) + replacement->mImageDataOffset == capture.pixels);
        capture.pixels[0] = 0x5a;
        assert(*(reinterpret_cast<u8*>(replacement) + replacement->mImageDataOffset) == 0x5a);
    }
    JASTrack::TList tracks; checkTrackList<JASTrack>(tracks.mTracks);
    AudMeTrack::TList remixTracks; checkTrackList<AudMeTrack>(remixTracks);
    u8 texCommand[]{0x61, 0x94, 0, 3, 0xff};
    assert(isTexNoReg(texCommand) && getTexNoReg(texCommand) == 1023);
    j3dSys.setModelDrawMtx(&J3DMtxBuffer::sNoUseDrawMtx);
    j3dSys.setModelNrmMtx(&J3DMtxBuffer::sNoUseNrmMtx);
    j3dSys.setModelDrawMtx(nullptr); j3dSys.setModelNrmMtx(nullptr);
    const u8 geometry[32]{GX_TRIANGLESTRIP,0,2,3,4,5,6,7,8};
    J3DShapeDraw draw(geometry, sizeof(geometry)); assert(draw.countVertex(3) == 2); draw.addTexMtxIndexInDL(3, 1, 30);
    const u8 expected[]{GX_TRIANGLESTRIP,0,2,3,33,4,5,6,36,7,8}; assert(!memcmp(draw.getDisplayList(), expected, sizeof(expected)));
    root->free(draw.getDisplayList());
    const u8 truncated[]{GX_TRIANGLESTRIP,255,255}; J3DShapeDraw invalid(truncated, sizeof(truncated)); bool rejected = false;
    try { invalid.addTexMtxIndexInDL(3, 1, 30); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected && root->check());
    auto checkControls = [&](ResourceHolder& holder) {
            for (u32 i = 0; i < holder.mBanmtResTable->mCount; ++i) {
                BckCtrl controls(&holder, holder.mBanmtResTable->getResName(i));
                JMapInfo expected; expected.attach(holder.mBanmtResTable->getRes(i));
                std::vector<std::vector<u8>> churn(16, std::vector<u8>(expected.mNativeData.size(), 0xa5));
                for (int row = 0; row < expected.getNumEntries(); ++row) {
                    const char* animation = nullptr; s32 playFrame = -1;
                    assert(expected.getValue(row, "name", &animation));
                    expected.getValue(row, "play_frame", &playFrame);
                    const auto* setting = !std::strcmp(animation, "_default") ? &controls.mDefaultCtrlData : controls.find(animation);
                    assert(setting && !std::strcmp(setting->mName, animation) && setting->mPlayFrame == playFrame);
                    ++controlsChecked;
                }
            }
    };
    for (const auto* name : {"CursorB.arc", "Coin.arc", "MarioShadow.arc", "Mario.arc", "MarioAnime.arc", "BeeMario.arc", "BeeLuigi.arc"}) {
        root->becomeCurrentHeap();
        std::ifstream f(std::filesystem::path(argv[1]) / "ObjectData" / name, std::ios::binary); assert(f);
        std::vector<u8> bytes{std::istreambuf_iterator<char>(f), {}};
        if (!memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
        JKRMemArchive archive; assert(archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0));
        auto* heap = JKRExpHeap::create(16 * 1024 * 1024, root, false); assert(heap); root->becomeCurrentHeap();
        for (unsigned i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& file = archive.mFiles[i]; if (!(file.mFlag & JKRArchive::FILE_FLAG_FILE)) continue;
            const std::string_view resource(archive.mStringTable + file.mNameOffset); if (!resource.ends_with(".bmd") && !resource.ends_with(".bdl")) continue;
            const auto* raw = static_cast<const u8*>(archive.getIdxResource(i));
            const auto native = galaxy::importModel({raw, std::size_t(archive.getResSize(raw))}); heap->becomeCurrentHeap();
            auto* model = resource.ends_with(".bdl") ? J3DModelLoaderDataBase::loadBinaryDisplayList(native.data(), 0x1200000)
                                                    : J3DModelLoaderDataBase::load(native.data(), 0x11300000);
            assert(model && model->getJointNum() && model->getMaterialNum() && model->getShapeNum());
            if (!std::strncmp(name, "Bee", 3)) {
                unsigned furShapes = 0;
                for (u32 shape = 0; shape < model->getShapeNum(); ++shape) {
                    auto* geometry = model->getShapeNodePointer(shape);
                    if (!std::strstr(model->getMaterialName()->getName(geometry->mMaterial->mIndex), "Fur")) continue;
                    CShader fur(model, nullptr);
                    auto* display = geometry->getShapeDraw(0);
                    const auto displaySize = display->getDisplayListSize();
                    for (u32 truncatedSize : {1u, 2u, 3u}) {
                        display->setDisplayListSize(truncatedSize);
                        bool rejectedFur = false;
                        try { fur.makeIndexData(geometry); } catch (const std::invalid_argument&) { rejectedFur = true; }
                        assert(rejectedFur); display->setDisplayListSize(displaySize);
                    }
                    u32 attributeOffset = 3;
                    for (auto* descriptor = geometry->getVtxDesc(); descriptor->attr != GX_VA_NULL; ++descriptor) {
                        if (descriptor->attr == GX_VA_POS || descriptor->attr == GX_VA_NRM || descriptor->attr == GX_VA_TEX0) {
                            auto* index = display->getDisplayList() + attributeOffset;
                            const std::array<u8, 2> originalIndex{index[0], index[1]};
                            index[0] = 0xff; if (descriptor->type == GX_INDEX16) index[1] = 0xff;
                            bool rejectedFur = false;
                            try { fur.makeIndexData(geometry); } catch (const std::invalid_argument&) { rejectedFur = true; }
                            assert(rejectedFur); std::memcpy(index, originalIndex.data(), 2);
                        }
                        attributeOffset += descriptor->type == GX_INDEX16 ? 2 : descriptor->type != GX_NONE;
                    }
                    fur.makeIndexData(geometry);
                    fur.checkBorderVtx(model, shape);
                    unsigned mapped = 0;
                    for (u32 vertex = 0; vertex < model->getVertexData().mVtxNum; ++vertex) {
                        const auto index = fur.mIndexArray[vertex];
                        assert((index._0 == 0xffff) == (index._2 == 0xffff));
                        mapped += index._0 != 0xffff;
                    }
                    assert(mapped > 0 && heap->check()); ++furShapes;
                }
                assert(furShapes > 0);
            }
            MR::initEnvelopeAndEnvMapOrProjMapModelData(model);
            auto* solid = JKRSolidHeap::create(1024 * 1024, heap, false); assert(solid); solid->becomeCurrentHeap();
            auto* instance = new J3DModel(model, J3DMdlFlag_UseSharedDL, 1); j3dSys.setModel(instance);
            auto* buffers = instance->getMtxBuffer();
            j3dSys.setModelDrawMtx(buffers->mpDrawMtxArr[1][0]);
            j3dSys.setModelNrmMtx(buffers->mpNrmMtxArr[1][0]);
            j3dSys.setModelDrawMtx(buffers->mpUserAnmMtx);
            j3dSys.setModelDrawMtx(buffers->mpWeightEvlpMtx);
            j3dSys.setModel(nullptr); j3dSys.setModelDrawMtx(nullptr); j3dSys.setModelNrmMtx(nullptr);
            heap->becomeCurrentHeap(); solid->destroy();
            assert(model->getHierarchy()->mType == 16);
            assert(model->getVertexData().mNativeArraySizes[0] >= model->getVertexData().getVtxNum() * 6);
            assert(heap->check() && root->check()); ++models; root->becomeCurrentHeap();
        }
        heap->becomeCurrentHeap();
        {
            ResourceHolder holder(archive); assert((holder.mModelResTable->mCount || holder.mMotionResTable->mCount) && heap->check());
            checkControls(holder);
            if (!std::strcmp(name, "MarioAnime.arc")) {
                XanimeResourceTable animations(&holder, marioAnimeTable, marioAnimeAuxTable, marioAnimeOfsTable,
                    reinterpret_cast<XanimeBckTable*>(singleAnimeTable), doubleAnimeTable, tripleAnimeTable, quadAnimeTable, nullptr);
                const auto* walk = animations.getGroupInfo("基本");
                assert(walk && walk->mEnd > 0 && walk->mAttribute == J3DFrameCtrl::EMode_LOOP);
                J3DFrameCtrl control(s16(walk->mEnd));
                control.setAttribute(walk->mAttribute); control.setLoop(s16(walk->mLoop)); control.setRate(walk->mRate);
                unsigned advanced = 0, looped = 0;
                for (unsigned frame = 0; frame < 300; ++frame) {
                    const auto previous = control.getFrame(); control.update();
                    advanced += control.getFrame() != previous; looped += control.checkState(2);
                    assert(control.getFrame() >= walk->mStart && control.getFrame() < walk->mEnd);
                }
                assert(advanced == 300 && looped > 0);
            }

        }
        root->becomeCurrentHeap(); heap->destroy(); assert(root->check());
    }
    unsigned directoryChecks = 0;
    for (const auto* folder : {"ObjectData", "MapPartsData"}) for (const auto& item : std::filesystem::directory_iterator(std::filesystem::path(argv[1]) / folder)) {
        if (item.path().extension() != ".arc") continue;
        std::ifstream f(item.path(), std::ios::binary); assert(f); std::vector<u8> bytes{std::istreambuf_iterator<char>(f), {}};
        if (!memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
        JKRMemArchive archive; assert(archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0));
        unsigned expected = 0;
        for (unsigned i = 0; i < archive.mInfoBlock->mNrFiles; ++i) {
            const auto& file = archive.mFiles[i];
            if ((file.mFlag & JKRArchive::FILE_FLAG_FILE) && strstr(archive.mStringTable + file.mNameOffset, ".bdl")) ++expected;
        }
        if (!controlsChecked && ResourceHolder::count(&archive, ".banmt", nullptr)) {
            auto* controlHeap = JKRExpHeap::create(32 * 1024 * 1024, root, false); assert(controlHeap);
            controlHeap->becomeCurrentHeap();
            { ResourceHolder holder(archive); checkControls(holder); }
            root->becomeCurrentHeap(); controlHeap->destroy(); assert(root->check());
        }
        assert(ResourceHolder::count(&archive, ".bdl", nullptr) == expected && root->check()); ++directoryChecks;
    }
    assert(directoryChecks > 100);
    assert(controlsChecked > 0);
    std::cout << controlsChecked << " animation settings retained across temporary allocations\n";
    assert(models >= 4); std::cout << models << " original BMD/BDL model constructions, sized GPU commands and heap canaries passed\n";
    return 0;
}

