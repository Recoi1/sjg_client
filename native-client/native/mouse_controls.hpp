#pragma once
#include <SDL3/SDL_events.h>
#include <JSystem/JGeometry/TVec.hpp>
struct SDL_Window;
namespace galaxy {
void initializeMouseControls(SDL_Window* window);
void shutdownMouseControls();
void handleMouseWindowEvent(const SDL_Event& event);
void setMouseContext(bool menuOpen, bool pointerOnly);
void beginMouseFrame();
bool mouseControlsInitialized();
bool mouseMenuConfirm();
bool mousePointer(float& x, float& y);
bool mousePointerHeld();
bool keyboardSkipHeld();
SDL_FPoint mouseLook();
float mouseZoom();
float takeMouseMenuScroll();
struct MouseOrbit {
    TVec3f horizontal{0, 0, 1}, up{0, 1, 0};
    float pitch = 0.1438107f, distance = 750;
    int distanceIndex = 0;
    bool active = false;
    void apply(TVec3f& position, TVec3f& target, TVec3f& cameraUp,
               const TVec3f& gravityUp, const TVec3f& playerPosition, SDL_FPoint motion,
               float zoom = 0, float seconds = 1.0f / 60);
};
}
