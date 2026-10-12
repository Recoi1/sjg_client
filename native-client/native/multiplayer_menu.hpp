#pragma once
#include "Game/Screen/LayoutActor.hpp"
#include "lan_lobby.hpp"
#include "characters.hpp"
class NativeCharacterSelect;
class NativeMultiplayerMenu : public LayoutActor {
public:
    explicit NativeMultiplayerMenu(NativeCharacterSelect*);
    void updateButton(bool enabled,galaxy::Character);
    void open(galaxy::Character);
    void drawButton() const;
    void kill() override;void control() override;void calcAnim() override;void draw() const override;
    NativeCharacterSelect* portraits;
    galaxy::lan::Lobby lobby;
    galaxy::lan::Adapter adapter;
    unsigned phase=0,selected=0;
    bool buttonVisible=false,buttonPointed=false,backHeld=false;
    int scrollStick=0;unsigned scrollRepeat=0;float scrollWheel=0;
    TVec2f lastPointer{-1000,-1000};
private:
    unsigned rows() const;void activate();
    std::chrono::steady_clock::time_point adapterChecked{};
};
namespace galaxy {void initializeMultiplayer(const std::filesystem::path& characterMods);}
