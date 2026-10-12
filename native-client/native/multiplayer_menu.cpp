#include "multiplayer_menu.hpp"
#include "character_select.hpp"
#include "menu_draw.hpp"
#include "nand.hpp"
#include "home_menu.hpp"
#include "mouse_controls.hpp"
#include "cutscene_skip.hpp"
#include "Game/Map/FileSelectModel.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/WPad.hpp"
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/material.h>
#include <nw4r/lyt/textBox.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include "host_memory.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdexcept>
namespace {
galaxy::lan::Build fingerprint{};
constexpr uint32_t colors[]{0xdc2828ff,0x23aa41ff,0xf069a5ff,0x965a2dff};
float buttonX(){return MR::getScreenWidth()*0.155f;}
float buttonY(){return MR::getScreenHeight()-110;}
bool inside(TVec2f p,float x,float y,float width,float height){return std::abs(p.x-x)<width*.5f && std::abs(p.y-y)<height*.5f;}
}
namespace galaxy {
void initializeMultiplayer(const std::filesystem::path& mods) {
    std::array<wchar_t,32768> name{};const auto count=GetModuleFileNameW(nullptr,name.data(),name.size());
    if(!count || count==name.size())throw std::runtime_error("Cannot identify multiplayer build");
    fingerprint=lan::lobbyBuild(std::filesystem::path(name.data()),mods,customCharactersAvailable()?15:3);
}
}
NativeMultiplayerMenu::NativeMultiplayerMenu(NativeCharacterSelect* picker):LayoutActor("Multiplayer",true),portraits(picker) {
    initLayoutManager("MiiSelect",1);MR::invalidateParentAnim(this);
    static_cast<nw4r::lyt::TextBox*>(MR::getPane(this,"TxtMiiChange"))->AllocStringBuffer(32);
    for(const char* name:{"Left","Right","PageNumber","TxtName"})MR::hidePane(this,name);
    MR::connectToSceneLayout(this);LayoutActor::kill();
}
void NativeMultiplayerMenu::updateButton(bool enabled,galaxy::Character character) {
    aurora::HostMemoryScope host;
    const auto now=std::chrono::steady_clock::now();
    if(now-adapterChecked>=std::chrono::seconds(2)) {
        auto next=galaxy::lan::radminAdapter();
        if(next!=adapter && !MR::isDead(this)){lobby.error="Radmin connection changed. Leave and reconnect.";lobby.leave();}
        adapter=next;adapterChecked=now;
    }
    buttonVisible=enabled && !adapter.ip.empty() && MR::isDead(this);buttonPointed=false;
    if(!buttonVisible)return;
    TVec2f pointer;
    buttonPointed=galaxy::menu::pointer(pointer) && inside(pointer,buttonX(),buttonY(),180,34);
    if(buttonPointed && MR::testSystemTriggerA())open(character);
}
void NativeMultiplayerMenu::open(galaxy::Character character) {
    aurora::HostMemoryScope host;
    if(adapter.ip.empty() || !galaxy::setNandReadOnly(true))return;
    lobby.open(adapter,fingerprint,galaxy::customCharactersAvailable()?15:3,static_cast<uint8_t>(character));
    selected=phase=scrollRepeat=0;scrollStick=0;scrollWheel=0;lastPointer.set(-1000,-1000);backHeld=galaxy::cutsceneSkipButtonHeld();buttonVisible=false;
    galaxy::setControllerSettingsOpen(true);LayoutActor::appear();MR::startAnim(this,"Appear",0);
    MR::startSystemSE("SE_SY_FILE_SEL_MIISEL_OPEN");
}
void NativeMultiplayerMenu::kill() {
    aurora::HostMemoryScope host;
    lobby.leave();galaxy::setNandReadOnly(false);LayoutActor::kill();galaxy::setControllerSettingsOpen(false);
    for(auto* head:portraits->heads)if(head)head->makeActorDead();
}
unsigned NativeMultiplayerMenu::rows()const{return lobby.hosting || lobby.joining ? 2:unsigned(lobby.discovery.rows().size())+2;}
void NativeMultiplayerMenu::activate() {
    if(lobby.hosting || lobby.joining) {
        if(selected==0){unsigned next=lobby.selected;do{next=(next+1)%4;}while(!(lobby.characters&(1<<next)));lobby.choose(next);}
        else {lobby.leave();MR::startAnim(this,"End",0);phase=2;}
    }else if(selected==0){lobby.host();selected=0;}
    else if(selected==rows()-1){MR::startAnim(this,"End",0);phase=2;}
    else if(lobby.join(selected-1))selected=0;
}
void NativeMultiplayerMenu::control() {
    aurora::HostMemoryScope host;lobby.pump();
    for(auto* head:portraits->heads)if(head && !MR::isDead(head))head->movement();
    if(phase==0 && MR::isAnimStopped(this,0)){MR::startAnim(this,"Wait",0);phase=1;}
    if(phase==2){if(MR::isAnimStopped(this,0))kill();return;}
    if(phase!=1)return;
    selected=std::min(selected,rows()-1);
    const bool held=galaxy::cutsceneSkipButtonHeld();const bool back=galaxy::takeSettingsBackRequest() || (MR::testSystemTriggerB()&&!backHeld);backHeld=held;
    if(back){lobby.leave();MR::startAnim(this,"End",0);phase=2;return;}
    if(MR::testCorePadTriggerUp(0))selected=(selected+rows()-1)%rows();
    if(MR::testCorePadTriggerDown(0))selected=(selected+1)%rows();
    const float stick=MR::getSubPadStickY(0);const int direction=stick>.55f?-1:stick<-.55f?1:0;
    int scroll=0;
    if(direction!=scrollStick){scrollStick=direction;scrollRepeat=18;scroll=direction;}
    else if(direction && --scrollRepeat==0){scroll=direction;scrollRepeat=6;}
    scrollWheel=std::clamp(scrollWheel+galaxy::takeMouseMenuScroll(),-float(rows()),float(rows()));
    scroll+=int(scrollWheel);scrollWheel-=int(scrollWheel);
    if(scroll)selected=std::clamp(int(selected)+scroll,0,int(rows()-1));
    TVec2f pointer;bool pointed=false;
    if(galaxy::menu::pointer(pointer)) {
        const unsigned first=selected<6?0:std::min(selected-5,rows()-6);
        for(unsigned i=first;i<std::min(rows(),first+6);++i) {
            const float y=(lobby.hosting||lobby.joining)?300+i*36:125+(i-first)*32;
            if(inside(pointer,MR::getScreenWidth()*.5f,y,MR::getScreenWidth()*.64f,30)){if(pointer.squareDist(lastPointer)>1 || galaxy::mouseMenuConfirm())selected=i;pointed=true;break;}
        }
        lastPointer=pointer;
    }
    if(MR::testSystemTriggerA() && (!galaxy::mouseMenuConfirm() || pointed))activate();
}
void NativeMultiplayerMenu::calcAnim() {
    MR::setTextBoxMessageRecursive(this,"TxtMiiChange",lobby.hosting||lobby.joining?L"Multiplayer lobby":L"Radmin LAN lobbies");LayoutActor::calcAnim();
    for(const char* name:{"PicWindow","WinWindowFrame","WinWindow"})MR::getPane(this,name)->mSize.width=MR::getRootPane(this)->mSize.width-48;
    auto* background=MR::getPane(this,"PicWindow")->GetMaterial();background->SetTextureNum(0);background->SetTexCoordGenNum(0);background->SetTevStageNum(0);background->SetTevColor(1,{255,255,255,255});
    const auto packed=colors[lobby.selected];
    for(const char* name:{"WinWindowC ","WinWindowLT"}){auto* material=MR::getPane(this,"WinWindow")->FindMaterialByName(name,false);for(unsigned i=0;i<2;++i)material->SetTevColor(i,{s16(packed>>24),s16((packed>>16)&255),s16((packed>>8)&255),material->GetTevColor(i).a});}
}
void NativeMultiplayerMenu::drawButton()const {
    namespace menu=galaxy::menu;
    if(buttonVisible){menu::begin();menu::button(L"Multiplayer",buttonX(),buttonY(),180,34,colors[static_cast<unsigned>(galaxy::characterList().preview)],buttonPointed);menu::end();}
}
void NativeMultiplayerMenu::draw()const {
    namespace menu=galaxy::menu;
    if(MR::isDead(this))return;
    LayoutActor::draw();if(phase!=1)return;
    const float center=MR::getScreenWidth()*.5f,width=MR::getScreenWidth()*.64f;
    menu::begin();
    if(lobby.hosting || lobby.joining) {
        const auto world=lobby.session.world();
        for(unsigned i=0;i<4;++i){const float x=MR::getScreenWidth()*(.17f+i*.22f);wchar_t label[64];const auto& player=world.players[i];
            if(player.present)std::swprintf(label,64,L"P%u: %hs%ls",i+1,galaxy::characterName(static_cast<galaxy::Character>(player.character)),i==lobby.session.slot()?L" (you)":L"");
            else std::swprintf(label,64,L"P%u: Waiting",i+1);
            menu::text(label,x,244,14);}
        menu::text(lobby.session.connected()?L"Connected - local saves disabled":L"Connecting...",center,80,16);
        wchar_t choice[64];std::swprintf(choice,64,L"Your character: %hs",galaxy::characterName(static_cast<galaxy::Character>(lobby.selected)));
        menu::button(choice,center,300,width,30,colors[lobby.selected],selected==0);
        menu::button(L"Leave lobby",center,336,width,30,colors[lobby.selected],selected==1);
        menu::text(L"Co-op gameplay is still being implemented.",center+1,374,14,0x000000ff);
        menu::text(L"Co-op gameplay is still being implemented.",center,373,14,0xffffffff);
        menu::end();
        for(unsigned i=0;i<4;++i)if(world.players[i].present)portraits->drawPortrait(world.players[i].character,MR::getScreenWidth()*(.17f+i*.22f),215,1.55f);
        menu::begin();
    }else {
        const unsigned first=selected<6?0:std::min(selected-5,rows()-6);
        for(unsigned i=first;i<std::min(rows(),first+6);++i){wchar_t label[96];
            if(i==0)std::swprintf(label,96,L"Host lobby");
            else if(i==rows()-1)std::swprintf(label,96,L"Back");
            else {const auto& row=lobby.discovery.rows()[i-1];std::swprintf(label,96,L"%hs - %u / 4 players",row.ip.c_str(),row.players);}
            menu::button(label,center,125+(i-first)*32,width,30,colors[lobby.selected],selected==i);}
        if(lobby.discovery.rows().empty())menu::text(L"Scanning Radmin VPN for compatible lobbies...",center,245,16);
        menu::text(L"Same Radmin network, game build and character mods.",center,345,14);
    }
    if(!lobby.error.empty()){wchar_t error[128];std::swprintf(error,128,L"%.95hs",lobby.error.c_str());menu::text(error,center,405,14,0xb02020ff);}
    menu::end();
}
