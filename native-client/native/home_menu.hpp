#pragma once
#include <SDL3/SDL_events.h>
struct SDL_Window;
namespace galaxy {
bool initializeDisplaySettings(SDL_Window* window, const char* userPath);
bool borderlessFullscreen();
bool setBorderlessFullscreen(bool enabled);
void handleControllerSettingsEvent(const SDL_Event& event);
bool controllerSettingsOpen();
bool nativeMenuOpen();
void setControllerSettingsOpen(bool open);
bool takeSettingsBackRequest();
bool fpsCounterEnabled();
bool setFpsCounterEnabled(bool enabled);
int fpsCounterValue(float fps);
}
