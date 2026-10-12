#include "character_select.hpp"
#include "characters.hpp"
#include "Game/Map/FileSelectModel.hpp"
#include "Game/Map/FileSelectIconID.hpp"
#include "Game/Screen/MiiSelectIcon.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <revolution/gx/GXPixel.h>
#include <revolution/gx/GXLighting.h>
#include <revolution/gx/GXGet.h>
#include <revolution/gx/GXTev.h>
#include <revolution/gx/GXTransform.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/material.h>
#include <cstdio>
#include <cmath>

namespace {
constexpr float portraitLowering = 30;
float portraitX(unsigned i) {
    const unsigned count = galaxy::customCharactersAvailable() ? 4 : 2;
    return (i - (count - 1) * 0.5f) * MR::getScreenWidth() * 0.255f;
}
constexpr GXColorS10 characterColors[] = {
    {220, 40, 40, 255}, {35, 170, 65, 255}, {240, 105, 165, 255}, {150, 90, 45, 255}
};
}

NativeCharacterSelect::NativeCharacterSelect() : LayoutActor("Character select", true) {
    initLayoutManager("MiiSelect", 1);
    MR::invalidateParentAnim(this);
    MR::hidePane(this, "Left"); MR::hidePane(this, "Right");
    MR::connectToSceneLayout(this);
    for (unsigned i = 0; i < 4; ++i) {
        hosts[i].identity();
        icons[i] = new MiiSelectIcon(-1, -1, -1, "Character face button", false);
        icons[i]->kill();
        const bool custom = i >= 2;
        if (custom && !galaxy::customCharactersAvailable()) continue;
        galaxy::useCustomPortraitArchives(custom);
        heads[i] = new FileSelectModel(custom ? (i % 2 ? "Luigi" : "Mario") :
            (i % 2 ? "FileSelectDataLuigi" : "FileSelectDataMario"), hosts[i], galaxy::characterName(static_cast<galaxy::Character>(i)), false,
            custom ? "MarioAnime" : nullptr);
        if (custom) {
            MR::startBck(heads[i], "Wait", nullptr);
            MR::setBckFrameAndStop(heads[i], 0);
        }
        galaxy::useCustomPortraitArchives(false);
    }
    kill();
}
void NativeCharacterSelect::open(int slot, bool mario) {
    galaxy::characterList().open(slot, mario);
    phase = age = 0;
    lastPointer.set(-1000, -1000);
    LayoutActor::appear(); MR::startAnim(this, "Appear", 0);
    MR::setTextBoxMessageRecursive(this, "TxtMiiChange", L"Characters");
    MR::setTextBoxMessageRecursive(this, "TxtPage", L"A / B");
    for (unsigned i = 0; i < 4; ++i) {
        if (!heads[i]) continue;
        FileSelectIconID id; id.setFellowID(static_cast<FileSelectIconID::EFellowID>(i % 2));
        icons[i]->appear(id); icons[i]->invalidate();
        heads[i]->makeActorDead();
    }
    MR::startSystemSE("SE_SY_FILE_SEL_MIISEL_OPEN");
}
void NativeCharacterSelect::control() {
    auto& list = galaxy::characterList();
    ++age;
    if (phase == 0 && MR::isAnimStopped(this, 0)) {
        MR::startAnim(this, "Wait", 0); phase = 1;
        for (unsigned i = 0; i < 4; ++i) if (heads[i]) {
            heads[i]->makeActorAppeared(); heads[i]->blink(); icons[i]->validate();
        }
    }
    if (phase == 1) {
        if (MR::isStarPointerInScreen(0)) {
            const auto& pointer = *MR::getStarPointerScreenPosition(0);
            if (pointer.squareDist(lastPointer) > 1) {
                for (unsigned i = 0; i < 4; ++i) if (heads[i] &&
                    std::abs(pointer.x - (MR::getScreenWidth() * 0.5f + portraitX(i))) < MR::getScreenWidth() * 0.10f &&
                    std::abs(pointer.y - (MR::getScreenHeight() * 0.5f - 60 + portraitLowering)) < 90)
                    list.preview = static_cast<galaxy::Character>(i);
            }
            lastPointer = pointer;
        }
        int current = static_cast<unsigned>(list.preview);
        const int count = galaxy::customCharactersAvailable() ? 4 : 2;
        if (MR::testCorePadTriggerLeft(0) || MR::testCorePadTriggerUp(0)) current = (current + count - 1) % count;
        if (MR::testCorePadTriggerRight(0) || MR::testCorePadTriggerDown(0)) current = (current + 1) % count;
        list.preview = static_cast<galaxy::Character>(current);
        if (MR::testSystemTriggerB()) list.cancel();
        else if (MR::testSystemTriggerA()) list.choose(list.preview);
    }
    wchar_t name[64]{};
    std::swprintf(name, 64, list.saveFailed ? L"Could not save. Try again." : L"%hs", galaxy::characterName(list.preview));
    MR::setTextBoxMessageRecursive(this, "TxtName", name);
    if (!list.opened && phase != 2) {
        MR::startAnim(this, "End", 0); phase = 2;
        for (auto* icon : icons) { icon->invalidate(); icon->kill(); }
        for (auto* head : heads) if (head) head->makeActorDead();
    }
    if (phase == 2 && MR::isAnimStopped(this, 0)) {
        kill(); return;
    }
    for (unsigned i = 0; i < 4; ++i) if (heads[i] && !MR::isDead(heads[i])) {
        const float scale = (i >= 2 ? 3.4f : 2.0f) * (i == static_cast<unsigned>(list.preview) ? 1.2f : 1.0f);
        heads[i]->mScale.set(scale);
        if (age % 180 == i * 30) heads[i]->blinkOnce();
        heads[i]->movement();
    }
}
void NativeCharacterSelect::calcAnim() {
    LayoutActor::calcAnim();
    auto* background = MR::getPane(this, "PicWindow")->GetMaterial();
    background->SetTextureNum(0);
    background->SetTexCoordGenNum(0);
    background->SetTevStageNum(0);
    background->SetTevColor(1, GXColorS10{255, 255, 255, 255});
    auto* window = MR::getPane(this, "WinWindow");
    for (const char* name : {"WinWindowC ", "WinWindowLT"}) {
        auto* material = window->FindMaterialByName(name, false);
        for (unsigned registerIndex = 0; registerIndex < 2; ++registerIndex) {
            auto color = characterColors[static_cast<unsigned>(galaxy::characterList().preview)];
            color.a = material->GetTevColor(registerIndex).a;
            material->SetTevColor(registerIndex, color);
        }
    }
    for (unsigned i = 0; i < 4; ++i) if (heads[i]) {
        const float x = portraitX(i);
        TVec2f position; position.x = x; position.y = -portraitLowering;
        icons[i]->setTrans(position);
        MR::setPaneAlpha(icons[i], "PicMario", 0);
        MR::setPaneAlpha(icons[i], "ShaMario", 0);
        MR::setPaneAlpha(icons[i], "PicMarioShine", 0);
        hosts[i][0][3] = MR::getScreenWidth() * 0.5f + x;
        hosts[i][1][3] = -MR::getScreenHeight() * 0.5f - portraitLowering;
        hosts[i][2][3] = -300;
        heads[i]->calcAnim();
        if (i >= 2) {
            const auto eye = MR::getJointMtx(heads[i], "EyePosition");
            hosts[i][0][3] += MR::getScreenWidth() * 0.5f + x - eye[0][3];
            hosts[i][1][3] += -MR::getScreenHeight() * 0.5f - portraitLowering + (i == 2 ? 32 : 45) - eye[1][3];
            heads[i]->calcAnim();
        }
    }
}
void NativeCharacterSelect::draw() const {
    if (MR::isDead(this)) return;
    LayoutActor::draw();
    if (phase != 1) return;
    TDDraw::setup(0, 1, 2);
    for (unsigned h = 0; h < 4; ++h) if (heads[h] && !MR::isDead(heads[h])) {
        const float x = MR::getScreenWidth() * 0.5f + portraitX(h);
        const float y = MR::getScreenHeight() * 0.5f + portraitLowering + 34;
        const float radius = h == static_cast<unsigned>(galaxy::characterList().preview) ? 48 : 40;
        GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 34);
        TDDraw::sendPoint(TVec3f(x, y, 0.0f), 0x00000030);
        for (unsigned i = 0; i <= 32; ++i) {
            const float angle = i * 6.2831853f / 32;
            TDDraw::sendPoint(TVec3f(x + std::cos(angle) * radius, y + std::sin(angle) * 7, 0.0f), 0);
        }
        GXEnd();
    }
    TDDraw::close();
    for (unsigned h=0;h<4;++h) if(heads[h] && !MR::isDead(heads[h])) drawHead(h);
}
void NativeCharacterSelect::drawHead(unsigned h) const {
    MR::drawInitFor2DModel(); MR::loadLight(0);
    float projection[7]; GXGetProjectionv(projection);
    constexpr float offsets[][2] = {{-3, 0}, {3, 0}, {0, -3}, {0, 3}, {-2, -2}, {2, -2}, {-2, 2}, {2, 2}, {0, 0}};
    if (auto* head = heads[h]; head && !MR::isDead(head)) {
        auto* model = head->mModelManager->getJ3DModel();
        if (h >= 2) static_cast<J3DModelX*>(model)->viewCalc2();
        else head->calcViewAndEntry();
        j3dSys.setModel(model);
        j3dSys.setTexture(model->getModelData()->getTexture());
        // The mod puts the entire face in EyeLid_v; its remaining materials are the body.
        const unsigned materials = h >= 2 ? std::min<unsigned>(6, model->getModelData()->getMaterialNum()) : model->getModelData()->getMaterialNum();
        for (unsigned pass = 0; pass < 9; ++pass) {
            const bool outline = pass < 8;
            float shifted[7]; std::copy_n(projection, 7, shifted);
            shifted[2] += offsets[pass][0] * 2 / MR::getScreenWidth();
            shifted[4] -= offsets[pass][1] * 2 / MR::getScreenHeight();
            GXSetProjectionv(shifted);
            for (unsigned i = 0; i < materials; ++i) {
                auto* packet = model->getMatPacket(i);
                j3dSys.setMatPacket(packet);
                packet->getMaterial()->load(); packet->callDL();
                auto* shape = packet->mpInitShapePacket;
                shape->getShape()->loadPreDrawSetting();
                if (shape->getDisplayListObj()) shape->getDisplayListObj()->callDL();
                if (h >= 2) {
                    GXSetChanAmbColor(GX_COLOR0A0, GXColor{64, 64, 64, 255});
                    GXSetChanMatColor(GX_COLOR0A0, GXColor{255, 255, 255, 255});
                    GXSetChanMatColor(GX_COLOR1A1, GXColor{0, 0, 0, 255});
                    GXSetChanCtrl(GX_COLOR1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
                }
                // Gameplay heads need depth to keep the back of the hair behind the face.
                GXSetZMode(!outline, outline ? GX_ALWAYS : GX_LEQUAL, !outline);
                if (outline) {
                    // Keep the material's alpha so textured hair and face cutouts retain their silhouettes.
                    const auto stage = static_cast<GXTevStageID>(packet->getMaterial()->getTevStageNum() - 1);
                    GXSetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
                    GXSetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
                }
                if (h >= 2) static_cast<J3DModelX*>(model)->shapePacketDrawFast(reinterpret_cast<J3DShapePacketX*>(shape));
                else shape->drawFast();
                J3DShape::resetVcdVatCache();
            }
        }
    }
    GXSetProjectionv(projection);
}
void NativeCharacterSelect::drawPortrait(unsigned h,float x,float y,float scale) {
    if(h>=4 || !heads[h])return;
    auto* head=heads[h];
    if(MR::isDead(head)){head->makeActorAppeared();head->blink();}
    head->mScale.set(scale*(h>=2?1.7f:1.0f));
    hosts[h].identity();hosts[h][0][3]=x;hosts[h][1][3]=-y;hosts[h][2][3]=-300;
    head->calcAnim();
    if(h>=2){const auto eye=MR::getJointMtx(head,"EyePosition");hosts[h][0][3]+=x-eye[0][3];hosts[h][1][3]+=-y+(h==2?32:45)-eye[1][3];head->calcAnim();}
    drawHead(h);
}
