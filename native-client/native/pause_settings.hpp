#pragma once
#include "Game/Screen/LayoutActor.hpp"
#include "runtime_options.hpp"
#include "pad_snapshot.hpp"
class ButtonPaneController;
class NativePauseSettings : public LayoutActor {
public:
    explicit NativePauseSettings(bool titleScreen = false);
    void appearButton(float x = -1.0f);
    void updateButton(bool enabled);
    void calcButton();
    void drawButton() const;
    void hideButton();
    void open();
    void kill() override;
    void control() override;
    void calcAnim() override;
    void draw() const override;
    unsigned rowCount() const;
    void activateRow(int direction = 1);
    bool applyPending();
    unsigned firstVisible() const;
    bool titleScreen;
    LayoutActor* buttonLayout;
    ButtonPaneController* button;
    TVec2f buttonPosition, lastPointer{-1000, -1000};
    unsigned phase = 0, page = 0, selected = 0, guidePage = 0;
    int scrollStick = 0;
    unsigned scrollRepeat = 0;
    float scrollWheel = 0;
    bool keyboardGuide = false, saveFailed = false, backHeld = false;
    galaxy::RuntimeOptions pendingOptions{};
    galaxy::ControllerSettings pendingController{};
    bool pendingFullscreen=false, pendingFps=false, pendingTextures=false, pendingMovies=false;
};
namespace galaxy { void drawFpsCounter(); bool applyPendingSettings(); }
