#include "pause_settings.hpp"
#include "menu_draw.hpp"
#include "home_menu.hpp"
#include "runtime_options.hpp"
#include "input_bindings.hpp"
#include "audio_device.hpp"
#include "frame_timing.hpp"
#include "io.hpp"
#include <aurora/aurora.h>
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/MessageHolder.hpp"
#include <dolphin/pad.h>
#include "pad_snapshot.hpp"
#include "hd_assets.hpp"
#include "characters.hpp"
#include "cutscene_skip.hpp"
#include "mouse_controls.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/material.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/ut/TextWriterBase.h>
#include <aurora/gfx.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <utility>
#include <stdexcept>

namespace {
NativePauseSettings* pendingApply=nullptr;
constexpr GXColorS10 colors[] = {{220,40,40,255}, {35,170,65,255}, {240,105,165,255}, {150,90,45,255}};
GXColorS10 menuColor(bool titleScreen) { return titleScreen ? GXColorS10{20,135,200,255} : colors[static_cast<unsigned>(galaxy::activeCharacter())]; }
void tint(LayoutActor* actor, const char* name, bool solid, bool titleScreen) {
    auto* pane = MR::getPane(actor, name);
    auto* material = pane->GetMaterial();
    for (unsigned i = 0; i < (solid ? 2 : 1); ++i) {
        auto color = menuColor(titleScreen); color.a = material->GetTevColor(i).a;
        material->SetTevColor(i, color);
    }
}
void setup2D() {
    TDDraw::setup(0, 1, 2);
    Mtx44 projection; C_MTXOrtho(projection, 0, MR::getScreenHeight(), 0, MR::getScreenWidth(), -1, 1);
    Mtx identity{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}};
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    GXLoadPosMtxImm(identity, GX_PNMTX0); GXSetCurrentMtx(GX_PNMTX0);
}
void capsule(float x, float y, float width, float height, u32 color) {
    // Match Galaxy's pill-shaped buttons with native GX geometry.
    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 34);
    TDDraw::sendPoint(TVec3f(x, y, 0.0f), color);
    for (unsigned i = 0; i <= 32; ++i) {
        const float angle = i * 6.2831853f / 32;
        const float cosine = std::cos(angle);
        TDDraw::sendPoint(TVec3f(x + (cosine >= 0 ? 1 : -1) * (width-height)*0.5f + cosine*height*0.5f,
                               y + std::sin(angle)*height*0.5f, 0.0f), color);
    }
    GXEnd();
}
void roundedBox(float x, float y, float size, float radius, u32 top, u32 bottom) {
    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 38);
    TDDraw::sendPoint(TVec3f(x, y, 0.0f), top);
    for (unsigned i = 0; i <= 36; ++i) {
        const unsigned corner = (i/9)%4;
        const float angle = (corner*90 + (i%9)*90.0f/8)*0.0174532925f;
        const float dx = (corner == 0 || corner == 3 ? 1 : -1)*(size*0.5f-radius);
        const float dy = (corner < 2 ? 1 : -1)*(size*0.5f-radius);
        const float py = y+dy+std::sin(angle)*radius;
        TDDraw::sendPoint(TVec3f(x+dx+std::cos(angle)*radius, py, 0.0f), py > y ? bottom : top);
    }
    GXEnd();
}
void text(const wchar_t* label, float x, float y, float size, u32 color, bool center = false) {
    nw4r::ut::TextWriterBase<wchar_t> writer;
    writer.SetFont(*MR::getFontOnCurrentLanguage()); writer.SetFontSize(size, size);
    const auto length = std::wcslen(label);
    if (center) x -= writer.CalcStringWidth(label, length)*0.5f;
    writer.SetColorMapping(color & 0xffffff00, color);
    writer.SetCursor(x, y); writer.SetupGX(); writer.Print(label, length);
}
bool menuPointer(TVec2f& position) {
    if (galaxy::mouseMenuConfirm()) {
        float x, y;
        if (!galaxy::mousePointer(x, y)) return false;
        // Native pointer history trails a click by one frame; use the click's current mouse position.
        position.set((x+1)*MR::getScreenWidth()*0.5f, (y+1)*MR::getScreenHeight()*0.5f);
        return true;
    }
    if (!MR::isStarPointerInScreen(0)) return false;
    position = *MR::getStarPointerScreenPosition(0); return true;
}
float rowY(unsigned row) { return 108 + row*30; }
const wchar_t* onOff(bool enabled) { return enabled ? L"On" : L"Off"; }
using galaxy::Option;
struct Row { const wchar_t* label; Option option; };
constexpr Row displayOptions[]{{L"Force 4:3",Option::Aspect43},{L"VSync",Option::Vsync},{L"Graphics backend",Option::Backend},{L"Framerate mode",Option::FrameMode},{L"Frame limit",Option::FrameLimit},{L"Interpolation",Option::Interpolation},{L"Filtering",Option::Filtering},{L"Anti-aliasing",Option::AntiAliasing},{L"Draw distance",Option::DrawDistance}};
constexpr Row controllerOptions[]{{L"Background gamepad",Option::BackgroundGamepad},{L"Disable gamepads",Option::DisableGamepads},{L"Gamepad",Option::Gamepad},{L"Deadzone",Option::Deadzone},{L"Rumble strength",Option::RumbleStrength}};
constexpr Row cameraOptions[]{{L"Camera mode",Option::CameraMode},{L"Invert X",Option::CameraInvertX},{L"Invert Y",Option::CameraInvertY},{L"Sensitivity",Option::CameraSensitivity},{L"Loose collision",Option::LooseCameraCollision}};
constexpr Row soundOptions[]{{L"Sound output",Option::SoundOutput},{L"Master volume",Option::MasterVolume},{L"Music volume",Option::MusicVolume},{L"Sound effects",Option::EffectsVolume},{L"Environment volume",Option::EnvironmentVolume},{L"Fade distant sounds",Option::DistantSoundFade},{L"Mute when unfocused",Option::MuteUnfocused}};
constexpr Row analogOptions[]{{L"Rotate left stick 90",Option::RotateLeftStick},{L"Invert left X",Option::InvertLeftX},{L"Invert left Y",Option::InvertLeftY},{L"Rotate right stick 90",Option::RotateRightStick},{L"Invert right X",Option::InvertRightX},{L"Invert right Y",Option::InvertRightY}};
const Row* optionRow(unsigned page,unsigned row) {
    if(page==1 && row>=4 && row<9) return &controllerOptions[row-4];
    if(page==2 && row>=2 && row<11) return &displayOptions[row-2];
    if(page==5 && row<5) return &cameraOptions[row];
    if(page==6 && row<7) return &soundOptions[row];
    if(page==7 && row==0) { static constexpr Row language{L"Language",Option::Language}; return &language; }
    if(page==11 && row<6) return &analogOptions[row];
    return nullptr;
}
unsigned nextValue(Option key,unsigned value,int direction) {
    if(key==Option::FrameLimit) {
        constexpr unsigned limits[]{60,90,120,144,165,180,200,240,300,360,0};
        auto it=std::find(std::begin(limits),std::end(limits),value);
        const unsigned at=it==std::end(limits) ? 0 : it-std::begin(limits);
        return limits[(at+std::size(limits)+direction)%std::size(limits)];
    }
    unsigned maximum=galaxy::optionMaximum(key),step=1,minimum=key==Option::CameraSensitivity ? 1 : 0;
    if(key==Option::Gamepad) maximum=std::min(15u,PADCount());
    if(key==Option::SoundOutput) { int count=0; auto* devices=SDL_GetAudioPlaybackDevices(&count); SDL_free(devices); maximum=std::min(15,count); }
    if(key==Option::CameraSensitivity || (key>=Option::MasterVolume && key<=Option::EnvironmentVolume) || key==Option::RumbleStrength) step=5;
    const int next=int(value)+int(step)*direction;
    return next<int(minimum) ? maximum : next>int(maximum) ? minimum : unsigned(next);
}
const wchar_t* optionValue(Option key,unsigned value,wchar_t* buffer,unsigned size) {
    if(key==Option::Backend) { constexpr const wchar_t* names[]{L"Auto",L"Direct3D 12",L"Vulkan"}; return names[value]; }
    if(key==Option::FrameMode) return value ? L"High refresh" : L"Original 60";
    if(key==Option::Interpolation) return value ? L"Smooth" : L"Low latency";
    if(key==Option::Filtering) { constexpr const wchar_t* names[]{L"Original",L"Nearest",L"Bilinear",L"Trilinear"}; return names[value]; }
    if(key==Option::AntiAliasing) return value ? L"4x MSAA" : L"Off";
    if(key==Option::DrawDistance) { constexpr const wchar_t* names[]{L"1x",L"1.5x",L"2x"}; return names[value]; }
    if(key==Option::CameraMode) return value ? L"Free look" : L"Galaxy";
    if(key==Option::Language) { constexpr const wchar_t* names[]{L"English",L"French",L"Spanish"}; return names[value]; }
    if(key==Option::FrameLimit && !value) return L"Unlimited";
    if(key==Option::Gamepad) { const auto* name=value && value<=PADCount() ? PADGetNameForControllerIndex(value-1) : "Auto"; std::swprintf(buffer,size,L"%.20hs",name ? name : "Unavailable"); return buffer; }
    if(key==Option::SoundOutput) {
        int count=0; auto* devices=SDL_GetAudioPlaybackDevices(&count);
        const char* name=value && value<=unsigned(count) ? SDL_GetAudioDeviceName(devices[value-1]) : "Default";
        std::swprintf(buffer,size,L"%.20hs",name ? name : "Unavailable"); SDL_free(devices); return buffer;
    }
    if(galaxy::optionMaximum(key)==1) return onOff(value);
    std::swprintf(buffer,size,L"%u%ls",value,key==Option::FrameLimit ? L" FPS" : L"%"); return buffer;
}

}

NativePauseSettings::NativePauseSettings(bool titleScreen) : LayoutActor("Pause settings", true), titleScreen(titleScreen) {
    initLayoutManager("MiiSelect", 1);
    MR::invalidateParentAnim(this);
    static_cast<nw4r::lyt::TextBox*>(MR::getPane(this, "TxtMiiChange"))->AllocStringBuffer(32);
    for (const char* name : {"Left", "Right", "PageNumber", "TxtName"}) MR::hidePane(this, name);
    buttonLayout = new LayoutActor("Settings button", true);
    buttonLayout->initLayoutManager("PauseMenu", 1);
    auto* root = MR::getRootPane(buttonLayout);
    for (auto it = root->mChildList.GetBeginIter(); it != root->mChildList.GetEndIter(); ++it)
        if (std::strcmp(it->mName, "BackButton")) it->SetVisible(false);
    MR::hidePane(buttonLayout, "NBackDown");
    button = new ButtonPaneController(buttonLayout, "NBackUp", "BoxButton1", 0, true);
    button->_22 = false;
    button->_23 = true;
    MR::setFollowPos(&buttonPosition, buttonLayout, "NBackUp");
    MR::setFollowTypeReplace(buttonLayout, "NBackUp");
    MR::hidePane(buttonLayout, "Sha1"); MR::hidePane(buttonLayout, "Txt1");
    buttonLayout->kill(); LayoutActor::kill();
}
void NativePauseSettings::appearButton(float x) {
    buttonPosition.set(x < 0 ? MR::getScreenWidth()*0.5f : x, MR::getScreenHeight()-50.0f);
    buttonLayout->appear(); button->appear();
}
void NativePauseSettings::hideButton() { kill(); buttonLayout->kill(); }
void NativePauseSettings::updateButton(bool enabled) {
    button->update(); buttonLayout->movement();
    if (!enabled || !MR::isDead(this)) return;
    TVec2f pointer;
    const bool pointing = menuPointer(pointer) && buttonLayout->mLayoutManager->isPointing("BoxButton1", pointer);
    if (!pointing) return;
    button->onPointing();
    if (button->isPointingTrigger()) MR::startSystemSE("SE_SY_SELECT_PAUSE_ITEM");
    if (button->trySelect()) open();
}
void NativePauseSettings::calcButton() {
    MR::setPaneScale(buttonLayout, 0.8f, 0.8f, nullptr);
    buttonLayout->calcAnim();
    auto* bounds = MR::getPane(buttonLayout, "BoxButton1");
    bounds->mSize.width = bounds->mSize.height*std::abs(bounds->mGlbMtx._11/bounds->mGlbMtx._00);
    for (const char* name : {"PicLButton1", "PicCButton1", "PicRButton1", "PicLFrame1", "PicCFrame1", "PicRFrame1"}) tint(buttonLayout, name, false, titleScreen);
    for (const char* name : {"PicLShine1", "PicCShine1", "PicRShine1"}) tint(buttonLayout, name, true, titleScreen);
}
void NativePauseSettings::drawButton() const {
    if (MR::isDead(buttonLayout) || button->isHidden()) return;
    const auto* pane = MR::getPane(buttonLayout, "BoxButton1");
    TVec2f center; MR::copyPaneTrans(&center, buttonLayout, "BoxButton1");
    const float size = pane->mSize.height*std::abs(pane->mGlbMtx._11);
    const float radius = size*0.30f;
    const u32 gray = 0x50505000 | MR::getPane(buttonLayout, "PicCButton1")->mGlbAlpha;
    const auto color = titleScreen ? GXColorS10{0,0,0,255} : menuColor(titleScreen);
    const u32 packed = u32(color.r)<<24 | u32(color.g)<<16 | u32(color.b)<<8 | (gray & 255);
    setup2D();
    roundedBox(center.x, center.y+3, size, size*0.22f, 0x40404000 | (gray & 255), 0x40404000 | (gray & 255));
    roundedBox(center.x, center.y, size, size*0.22f, packed, packed);
    roundedBox(center.x, center.y, size-3, size*0.20f, 0xffffff00 | (gray & 255), 0xffffff00 | (gray & 255));
    roundedBox(center.x, center.y, size-5, size*0.18f, packed, packed);
    roundedBox(center.x, center.y, size-8, size*0.17f, 0xffffff00 | (gray & 255), 0xd0d0d000 | (gray & 255));
    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 130);
    for (unsigned i = 0; i <= 64; ++i) {
        const float angle = i*6.2831853f/64;
        const float outer = radius*((i%8 >= 2 && i%8 <= 5) ? 1.0f : 0.75f);
        TDDraw::sendPoint(TVec3f(center.x+std::cos(angle)*outer, center.y+std::sin(angle)*outer, 0.0f), gray);
        TDDraw::sendPoint(TVec3f(center.x+std::cos(angle)*radius*0.4f, center.y+std::sin(angle)*radius*0.4f, 0.0f), gray);
    }
    GXEnd(); TDDraw::close();
}
void NativePauseSettings::open() {
    pendingOptions=galaxy::runtimeOptions(); pendingController=galaxy::getControllerSettings();
    pendingFullscreen=galaxy::borderlessFullscreen(); pendingFps=galaxy::fpsCounterEnabled();
    pendingTextures=galaxy::hdTexturesEnabled(); pendingMovies=galaxy::hdMoviesEnabled();
    galaxy::beginInputBindingEdit();
    phase = page = selected = guidePage = scrollRepeat = 0; scrollStick = 0; scrollWheel = 0; saveFailed = false;
    lastPointer.set(-1000, -1000); backHeld = galaxy::cutsceneSkipButtonHeld();
    galaxy::setControllerSettingsOpen(true);
    LayoutActor::appear(); MR::startAnim(this, "Appear", 0);
    MR::startSystemSE("SE_SY_FILE_SEL_MIISEL_OPEN");
}
void NativePauseSettings::kill() {
    if(pendingApply==this) pendingApply=nullptr;
    galaxy::discardInputBindingEdit(); LayoutActor::kill(); galaxy::setControllerSettingsOpen(false); button->forceToWait();
}
unsigned NativePauseSettings::rowCount() const {
    constexpr unsigned counts[]{8,10,12,3,11,6,8,5,18,17,18,7,1,3}; return counts[page];
}
unsigned NativePauseSettings::firstVisible() const { return selected<7 ? 0 : std::min(selected-6,rowCount()-7); }
void NativePauseSettings::activateRow(int direction) {
    if(page==13) {
        if(selected==0) { pendingApply=this; phase=3; }
        else if(selected==1) { galaxy::discardInputBindingEdit(); MR::startAnim(this,"End",0); phase=2; }
        else { page=selected=0; }
        return;
    }
    if(page && selected==rowCount()-1) { page=selected=0; return; }
    if(const auto* row=optionRow(page,selected)) {
        pendingOptions[unsigned(row->option)]=nextValue(row->option,pendingOptions[unsigned(row->option)],direction);
        MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM"); return;
    }
    auto& settings=pendingController;
    if(!page) {
        if(selected==7) { page=13; selected=0; }
        else { page=selected+1; selected=0; }
    } else if(page==1) {
        if(selected==0) settings.allStars=!settings.allStars;
        if(selected==1) settings.swapLeft=!settings.swapLeft;
        if(selected==2) settings.swapRight=!settings.swapRight;
        if(selected==3) settings={};
    } else if(page==2) {
        if(selected==0) pendingFullscreen=!pendingFullscreen; else pendingFps=!pendingFps;
    } else if(page==3) {
        if(selected==0) pendingTextures=!pendingTextures; else pendingMovies=!pendingMovies;
    } else if(page==4) {
        if(selected==0) { keyboardGuide=!keyboardGuide; guidePage=0; }
        if(selected==5) guidePage=(guidePage+1)%3;
        if(selected>=6 && selected<=9) { page=selected==9 ? 11 : selected+2; selected=0; }
    } else if(page>=8 && page<=10) {
        const unsigned count=page==9 ? galaxy::classicActionCount : galaxy::keyActionCount;
        if(selected==count) saveFailed=!galaxy::resetInputBindings();
        else if(direction<0) saveFailed=!galaxy::setBinding(page-8,selected,65535);
        else galaxy::captureBinding(page-8,selected);
    } else if(page==7) {
        if(selected==1) { page=4; selected=0; }
        if(selected==2) { page=12; selected=0; }
        if(selected==3) saveFailed=!galaxy::openUserDataDirectory();
    }
    MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");
}
void NativePauseSettings::control() {
    const bool held = galaxy::cutsceneSkipButtonHeld();
    const bool back = (held && !backHeld) || galaxy::takeSettingsBackRequest() || MR::testCorePadTriggerPlus(0);
    backHeld = held;
    if (phase == 0 && MR::isAnimStopped(this, 0)) { MR::startAnim(this, "Wait", 0); phase = 1; }
    if (galaxy::bindingCaptureActive()) return;
    if (phase == 1) {
        bool pointingRow = false;
        TVec2f pointer;
        if (menuPointer(pointer)) {
            if (std::abs(pointer.x-MR::getScreenWidth()*0.5f) < MR::getScreenWidth()*0.32f)
                for (unsigned first=firstVisible(), i=first; i < std::min(rowCount(),first+7); ++i) if (std::abs(pointer.y-rowY(i-first)) < 14) {
                    pointingRow = true;
                    if (pointer.squareDist(lastPointer) > 1 || galaxy::mouseMenuConfirm()) selected = i;
                }
            lastPointer = pointer;
        }
        const float stick = MR::getSubPadStickY(0);
        const int direction = stick > 0.55f ? -1 : stick < -0.55f ? 1 : 0;
        int scroll = 0;
        if (direction != scrollStick) { scrollStick = direction; scrollRepeat = 18; scroll = direction; }
        else if (direction && --scrollRepeat == 0) { scroll = direction; scrollRepeat = 6; }
        scrollWheel = std::clamp(scrollWheel + galaxy::takeMouseMenuScroll(), -float(rowCount()), float(rowCount()));
        const int wheel = int(scrollWheel);
        scrollWheel -= wheel; scroll += wheel;
        selected = std::clamp(int(selected)+scroll, 0, int(rowCount())-1);
        if (MR::testCorePadTriggerUp(0)) selected = (selected+rowCount()-1)%rowCount();
        if (MR::testCorePadTriggerDown(0)) selected = (selected+1)%rowCount();
        if (back) {
            if(page==13) page=selected=0;
            else if(page) page=selected=0;
            else { page=13; selected=0; }
        } else if ((MR::testSystemTriggerA() && (!galaxy::mouseMenuConfirm() || pointingRow)) || MR::testCorePadTriggerLeft(0) || MR::testCorePadTriggerRight(0)) activateRow(MR::testCorePadTriggerLeft(0) ? -1 : 1);
    }
    if (phase == 2 && MR::isAnimStopped(this, 0)) kill();
}
void NativePauseSettings::calcAnim() {
    constexpr const wchar_t* titles[]{L"Settings", L"Controller", L"Display", L"HD assets", L"Controls", L"Camera", L"Sound", L"Misc", L"Keyboard binds", L"Controller binds", L"Mouse binds", L"Analog sticks", L"Information",L"Apply settings?"};
    MR::setTextBoxMessageRecursive(this, "TxtMiiChange", titles[page]);
    LayoutActor::calcAnim();
    for (const char* name : {"PicWindow", "WinWindowFrame", "WinWindow"})
        MR::getPane(this, name)->mSize.width = MR::getRootPane(this)->mSize.width-48.0f;
    auto* background = MR::getPane(this, "PicWindow")->GetMaterial();
    background->SetTextureNum(0); background->SetTexCoordGenNum(0); background->SetTevStageNum(0);
    background->SetTevColor(1, GXColorS10{255,255,255,255});
    for (const char* name : {"WinWindowC ", "WinWindowLT"}) {
        auto* material = MR::getPane(this, "WinWindow")->FindMaterialByName(name, false);
        for (unsigned i = 0; i < 2; ++i) { auto color = menuColor(titleScreen); color.a = material->GetTevColor(i).a; material->SetTevColor(i, color); }
    }
}
void NativePauseSettings::draw() const {
    if (MR::isDead(this)) return;
    LayoutActor::draw(); if(phase==3) { text(L"Applying settings...",MR::getScreenWidth()*0.5f,180,20,0x505050ff,true); return; } if (phase != 1) return;
    const float center = MR::getScreenWidth()*0.5f, width = MR::getScreenWidth()*0.64f;
    const auto color = menuColor(titleScreen);
    const u32 packed = u32(color.r)<<24 | u32(color.g)<<16 | u32(color.b)<<8 | 255;
    setup2D(); capsule(center, rowY(selected-firstVisible())+2, width, 29, 0x00000030);
    capsule(center, rowY(selected-firstVisible()), width, 29, packed);
    capsule(center, rowY(selected-firstVisible()), width-4, 25, 0xfff9ffff);
    const auto settings = pendingController;
    constexpr const wchar_t* mainRows[]{L"Controller", L"Display", L"HD assets", L"Controls", L"Camera", L"Sound", L"Misc", L"Back"};
    constexpr const wchar_t* controllerRows[]{L"Controller layout", L"Swap LB / LT", L"Swap RB / RT", L"Reset defaults", L"Back"};
    constexpr const wchar_t* displayRows[]{L"Borderless fullscreen", L"FPS counter", L"Back"};
    constexpr const wchar_t* hdRows[]{L"HD interface textures", L"HD cutscenes", L"Back"};
    for (unsigned i = firstVisible(); i < std::min(rowCount(),firstVisible()+7); ++i) {
        const wchar_t* label = L""; const wchar_t* value = L"";
        if (page == 0) label = mainRows[i];
        if (page == 1 && i<4) {
            label = controllerRows[i];
            if (i == 0) value = settings.allStars ? L"All-Stars" : L"Nvidia";
            if (i == 1) value = onOff(settings.swapLeft);
            if (i == 2) value = onOff(settings.swapRight);
        }
        if (page == 2 && i<2) { label = displayRows[i]; value = onOff(i == 0 ? pendingFullscreen : pendingFps); }
        if (page == 3) { label = hdRows[i]; if (i < 2) value = onOff(i == 0 ? pendingTextures : pendingMovies); }
        if(page==13) { constexpr const wchar_t* choices[]{L"Apply settings",L"Discard changes",L"Cancel"}; label=choices[i]; }
        if (page == 4) {
            constexpr const wchar_t* actions[]{L"Select menus", L"Jump", L"Skip cutscene", L"Shoot star bits", L"Spin", L"Crouch", L"Reset camera", L"Camera / distance", L"Move", L"Star pointer", L"Release mouse", L"Pause"};
            const wchar_t* keyboard[]{L"Left click", L"Space", L"Hold B 1 second", L"Left click", L"Left Shift", L"Ctrl", L"C", L"Mouse / wheel", L"W A S D", L"Right click + mouse", L"Esc", L"Enter"};
            const wchar_t* xbox[]{L"A", settings.allStars ? L"A / B / RB" : L"A / RT", L"Hold B 1 second", settings.allStars ? L"RT" : L"B / RB", settings.allStars ? L"X / Y" : L"X", settings.swapLeft ? L"LB" : L"LT", settings.allStars ? (settings.swapLeft ? L"LT" : L"LB") : (settings.swapLeft ? L"LT / Y" : L"LB / Y"), L"D-pad", L"Left stick", L"Right stick", L"", L"Menu / Start"};
            if (settings.swapRight) { xbox[1] = settings.allStars ? L"A / B / RT" : L"A / RB"; xbox[3] = settings.allStars ? L"RB" : L"B / RT"; }
            if (i == 0) { label = L"Default control guide"; value = keyboardGuide ? L"Keyboard / mouse" : L"Xbox"; }
            else if (i < 5) { const auto index = guidePage*4+i-1; label = actions[index]; value = keyboardGuide ? keyboard[index] : xbox[index]; }
            else { constexpr const wchar_t* extra[]{L"Next page",L"Keyboard bindings",L"Controller bindings",L"Mouse bindings",L"Analog stick options",L"Back"}; label=extra[i-5]; }
        }
        wchar_t valueBuffer[96]{};
        if(const auto* row=optionRow(page,i)) { label=row->label; value=optionValue(row->option,pendingOptions[unsigned(row->option)],valueBuffer,96); }
        if(page>=8 && page<=10) {
            const unsigned count=page==9 ? galaxy::classicActionCount : galaxy::keyActionCount;
            if(i<count) { label=galaxy::bindingLabel(page-8,i); std::swprintf(valueBuffer,96,L"%.24hs",galaxy::bindingName(page-8,i)); value=valueBuffer; }
            else label=L"Reset all bindings";
        }
        if(page==7 && i>0) { constexpr const wchar_t* misc[]{L"Menu options / controls",L"Information",L"Open user data",L"Back"}; label=misc[i-1]; }
        if(page && page!=13 && i==rowCount()-1) { label=L"Back"; value=L""; }
        const float y = rowY(i-firstVisible())-9;
        text(label, center-width*0.5f+14, y, MR::getScreenWidth()<700 ? 16 : 18, 0x505050ff);
        text(value, center+width*0.12f, y, 15, 0x505050ff);
    }
    const wchar_t* hint = (saveFailed || galaxy::bindingSaveFailed()) ? L"Could not apply/save. Previous settings kept." : galaxy::bindingCaptureActive() ? L"Press a key / button. Esc cancels." : L"A / click: select    Left/right: change    B / Esc: back";
    text(hint,center,336,14,(saveFailed || galaxy::bindingSaveFailed()) ? 0xcc2020ff : 0xffffffff,true);
    if(rowCount()>7) text(L"Wheel / left stick: scroll",center,355,14,0xffffffff,true);
    if(rowCount()>7) { wchar_t scroll[32]{}; std::swprintf(scroll,32,L"%u / %u",selected+1,rowCount()); text(scroll,center+width*0.5f-20,303,12,0x505050ff,true); }
    if(page==2 || page==6 || page==7) text(L"Changes take effect when you choose Apply.",center,303,12,0x505050ff,true);
    if(page>=8 && page<=10) text(L"Left: clear binding. Esc, B and right mouse are reserved.",center,303,12,0x505050ff,true);
    if(page==12) {
        text(L"Super Mario Galaxy PC",center,154,22,0x505050ff,true);
        text(L"60 Hz gameplay; independent high-refresh rendering.",center,197,14,0x505050ff,true);
        text(L"Smooth interpolation adds one simulation frame of delay.",center,230,14,0x505050ff,true);
        text(L"Low latency shows the newest simulation pose.",center,258,14,0x505050ff,true);
    }
    TDDraw::close();
}
bool NativePauseSettings::applyPending() {
    const auto previousOptions=galaxy::runtimeOptions();
    const auto previousController=galaxy::getControllerSettings();
    const auto previousBindings=galaxy::inputBindings();
    const bool fullscreen=galaxy::borderlessFullscreen(), fps=galaxy::fpsCounterEnabled();
    const bool textures=galaxy::hdTexturesEnabled(), movies=galaxy::hdMoviesEnabled();
    const auto previousBackend=aurora_get_backend();
    constexpr AuroraBackend backends[]{BACKEND_AUTO,BACKEND_D3D12,BACKEND_VULKAN};
    const auto graphics=[&](const galaxy::RuntimeOptions& options) {
        return aurora_reconfigure_graphics(backends[options[unsigned(Option::Backend)]],options[unsigned(Option::AntiAliasing)] ? 4 : 1);
    };
    const bool changeGraphics=pendingOptions[unsigned(Option::Backend)]!=previousOptions[unsigned(Option::Backend)] || pendingOptions[unsigned(Option::AntiAliasing)]!=previousOptions[unsigned(Option::AntiAliasing)];
    const bool changeAudio=pendingOptions[unsigned(Option::SoundOutput)]!=previousOptions[unsigned(Option::SoundOutput)];
    const bool changeLanguage=pendingOptions[unsigned(Option::Language)]!=previousOptions[unsigned(Option::Language)];
    auto* messages=MR::getGameSystemObjHolder()->mMessageHolder;
    bool success=false;
    {
        aurora::io::AtomicFileBatch files;
        success=galaxy::setRuntimeOptions(pendingOptions) && galaxy::setControllerSettings(pendingController) &&
            galaxy::applyInputBindingEdit() &&
            (pendingFullscreen==fullscreen || galaxy::setBorderlessFullscreen(pendingFullscreen)) &&
            galaxy::setFpsCounterEnabled(pendingFps) &&
            (pendingTextures==textures || galaxy::setHdTexturesEnabled(pendingTextures)) &&
            (pendingMovies==movies || galaxy::setHdMoviesEnabled(pendingMovies));
        if(success) {
            galaxy::applyRuntimeOptions();
            success=(!changeAudio || galaxy::applyAudioOutput()) && (!changeLanguage || messages->applyLanguage()) &&
                (!changeGraphics || graphics(pendingOptions));
        }
        if(success) success=files.commit();
    }
    if(!success) {
        aurora::io::AtomicFileBatch discardWrites;
        const bool restored=galaxy::setRuntimeOptions(previousOptions) && galaxy::setControllerSettings(previousController) &&
            galaxy::setInputBindings(previousBindings) && galaxy::setBorderlessFullscreen(fullscreen) &&
            galaxy::setFpsCounterEnabled(fps) && galaxy::setHdTexturesEnabled(textures) && galaxy::setHdMoviesEnabled(movies);
        galaxy::applyRuntimeOptions();
        if(!restored || (changeAudio && !galaxy::applyAudioOutput()) || (changeLanguage && !messages->applyLanguage()) ||
           (changeGraphics && !aurora_reconfigure_graphics(previousBackend,previousOptions[unsigned(Option::AntiAliasing)] ? 4 : 1))) throw std::runtime_error("Cannot restore previous running settings");
    }
    galaxy::resetRenderHistory();
    if(!titleScreen) buttonPosition.x=MR::getScreenWidth()*0.5f;
    saveFailed=!success;
    if(success) { galaxy::discardInputBindingEdit(); MR::startAnim(this,"End",0); phase=2; }
    else { page=13; selected=0; phase=1; }
    return success;
}
namespace galaxy::menu {
void begin(){setup2D();}
void end(){TDDraw::close();}
void text(const wchar_t* label,float x,float y,float size,uint32_t color,bool center){::text(label,x,y,size,color,center);}
bool pointer(TVec2f& position){return menuPointer(position);}
void button(const wchar_t* label,float x,float y,float width,float height,uint32_t outline,bool selected){
    // The font writer changes vertex formats; restore geometry before each subsequent button.
    setup2D();
    capsule(x,y+2,width,height,0x00000030);capsule(x,y,width,height,outline);
    capsule(x,y,width-4,height-4,selected?0xd9f0ffff:0xfff9ffff);
    ::text(label,x,y-10,18,0x505050ff,true);
}
}
namespace galaxy {
bool applyPendingSettings() {
    if(!pendingApply) return false;
    auto* settings=std::exchange(pendingApply,nullptr);
    settings->applyPending(); return true;
}
void drawFpsCounter() {
    // frameLoop can destroy the scene before this overlay restores its camera.
    auto* game = SingletonHolder<GameSystem>::get();
    if (!fpsCounterEnabled() || !game || !game->mSceneController || game->mSceneController->mSceneInitializeState != SceneInitializeState_End ||
        !game->mSceneController->mScene || std::strcmp(game->mSceneController->mCurrSceneControlInfo.mScene,"Game") || !game->mSceneController->isExistSceneObjHolder() || !MR::getNumberFontNW4R()) return;
    setup2D();
    wchar_t number[8]{}; std::swprintf(number, 8, L"%d", fpsCounterValue(aurora_get_fps()));
    nw4r::ut::TextWriterBase<wchar_t> writer;
    writer.SetFont(*MR::getNumberFontNW4R()); writer.SetFontSize(24, 24);
    const auto length = std::wcslen(number);
    const float x = MR::getScreenWidth()-18-writer.CalcStringWidth(number, length);
    writer.SetCursor(x, 14); writer.SetupGX(); writer.Print(number, length);
    text(L"FPS", x-42, 19, 14, 0x000000ff); text(L"FPS", x-43, 18, 14, 0xffffffff);
    TDDraw::close();
}
}
