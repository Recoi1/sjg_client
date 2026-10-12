#ifdef TARGET_PC
#include "runtime_options.hpp"
#include "frame_timing.hpp"
#endif
#include "mouse_controls.hpp"
#include <aurora/input.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace galaxy {
namespace {
SDL_Window* window = nullptr;
aurora::input::LayerId layer = aurora::input::kInvalidLayerId;
bool focused = false, released = false, menuOpen = false, pointerOnly = true;
bool rightHeld = false, swallowLeft = false, menuConfirm = false, pendingConfirm = false, frameConfirm = false;
bool skipHeld = false;
SDL_FPoint pointer{}, pendingLook{}, frameLook{};
float pendingZoom = 0, frameZoom = 0, pendingScroll = 0, frameScroll = 0;
bool captured() { return window && focused && !released && !menuOpen; }
bool locked() { return captured() && !pointerOnly; }
void updateMouseGrab() {
    const bool grab = captured();
    if (window && bool(SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_GRABBED) != grab && !SDL_SetWindowMouseGrab(window, grab))
        throw std::runtime_error(SDL_GetError());
}
void clearMotion() { pendingLook = frameLook = {}; pendingZoom = frameZoom = pendingScroll = frameScroll = 0; }
aurora::input::EventResult inputEvent(const aurora::input::InputEvent& event, void*) {
    using namespace aurora::input;
    if (event.payload.is<InputEvent::Cancelled>()) { skipHeld = rightHeld = swallowLeft = menuConfirm = pendingConfirm = frameConfirm = false; clearMotion(); }
    if (const auto* key = event.payload.get_if<InputEvent::KeyChanged>();
        key && key->scancode == SDL_SCANCODE_B && !key->repeat) {
        skipHeld = key->pressed && focused && !menuOpen;
    }
    if (const auto* key = event.payload.get_if<InputEvent::KeyChanged>();
        key && key->pressed && key->scancode == SDL_SCANCODE_ESCAPE) {
        released = true; clearMotion(); updateMouseGrab(); return EventResult::Consume;
    }
    if (const auto* scroll = event.payload.get_if<InputEvent::Scroll>();
        scroll && event.source.kind == InputSource::Kind::Mouse && captured()) {
        if (std::isfinite(scroll->delta.y)) {
            if (pointerOnly) pendingScroll += scroll->delta.y;
            else if (!rightHeld) pendingZoom += scroll->delta.y;
        }
        return EventResult::Consume;
    }
    const auto* mouse = event.payload.get_if<InputEvent::PointerChanged>();
    if (!mouse || event.source.kind != InputSource::Kind::Mouse || menuOpen || !focused) return EventResult::Pass;
    if (mouse->button == SDL_BUTTON_LEFT) {
        if (mouse->phase == InputEvent::PointerChanged::Phase::Down && released) {
            released = false; swallowLeft = true; clearMotion(); updateMouseGrab(); return EventResult::Consume;
        }
        if (swallowLeft) {
            if (mouse->phase == InputEvent::PointerChanged::Phase::Up) swallowLeft = false;
            return EventResult::Consume;
        }
        if (pointerOnly) {
            menuConfirm = mouse->phase == InputEvent::PointerChanged::Phase::Down;
            pendingConfirm |= menuConfirm;
            return EventResult::Consume;
        }
    }
    if (mouse->button == SDL_BUTTON_RIGHT) {
        rightHeld = mouse->phase == InputEvent::PointerChanged::Phase::Down;
    }
    if (mouse->phase == InputEvent::PointerChanged::Phase::Move) {
        int width, height;
        if (!SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0) return EventResult::Pass;
        float contentWidth=width, contentHeight=height;
        if (appliedOption(Option::Aspect43)) {
            contentWidth=std::min(float(width), height*4.0f/3);
            contentHeight=std::min(float(height), width*3.0f/4);
        }
        if (locked()) {
            if (rightHeld) {
                pointer.x = std::clamp(pointer.x + mouse->delta.x * 2 / contentWidth, -1.0f, 1.0f);
                pointer.y = std::clamp(pointer.y + mouse->delta.y * 2 / contentHeight, -1.0f, 1.0f);
            } else {
                pendingLook.x += mouse->delta.x; pendingLook.y += mouse->delta.y;
            }
        } else if (pointerOnly) {
            pointer = {(mouse->position.x-(width-contentWidth)/2)*2/contentWidth-1, (mouse->position.y-(height-contentHeight)/2)*2/contentHeight-1};
        }
    }
    return EventResult::Pass;
}
bool normalize(TVec3f& value) {
    const float length = value.length();
    if (!std::isfinite(length) || length < 0.0001f) return false;
    value.scale(1 / length); return true;
}
}
void initializeMouseControls(SDL_Window* gameWindow) {
    shutdownMouseControls();
    window = gameWindow;
    if (!window) throw std::invalid_argument("Mouse controls need a game window");
    focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    released = menuOpen = skipHeld = rightHeld = swallowLeft = menuConfirm = pendingConfirm = frameConfirm = false; pointerOnly = true;
    pointer = {}; clearMotion();
    layer = aurora::input::register_layer({
        .label = "Galaxy keyboard / mouse", .priority = aurora::input::kGameLayerPriority + 1,
        .onEvent = inputEvent,
        .pointerMode = [](void*) {
            using aurora::input::PointerMode;
            return captured() ? (pointerOnly ? PointerMode::Hidden : PointerMode::Relative) : PointerMode::Visible;
        },
    });
    if (layer == aurora::input::kInvalidLayerId) throw std::runtime_error("Could not register native mouse controls");
    updateMouseGrab();
}
void shutdownMouseControls() {
    if (layer != aurora::input::kInvalidLayerId) aurora::input::unregister_layer(layer);
    layer = aurora::input::kInvalidLayerId;
    if (window) { SDL_SetWindowRelativeMouseMode(window, false); SDL_SetWindowMouseGrab(window, false); }
    window = nullptr; skipHeld = false; clearMotion();
}
void handleMouseWindowEvent(const SDL_Event& event) {
    if (!window) return;
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.windowID == SDL_GetWindowID(window) && event.key.scancode == SDL_SCANCODE_ESCAPE) {
            released = true; clearMotion(); updateMouseGrab();
        }
        return;
    }
    if (event.type != SDL_EVENT_WINDOW_FOCUS_LOST && event.type != SDL_EVENT_WINDOW_FOCUS_GAINED) return;
    if (event.window.windowID != SDL_GetWindowID(window)) return;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        focused = false; released = true; skipHeld = rightHeld = swallowLeft = menuConfirm = pendingConfirm = frameConfirm = false; clearMotion();
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) { focused = true; clearMotion(); }
    updateMouseGrab();
}
void setMouseContext(bool open, bool onlyPointer) {
    if (open != menuOpen || onlyPointer != pointerOnly) { skipHeld = rightHeld = menuConfirm = pendingConfirm = frameConfirm = false; clearMotion(); }
    menuOpen = open; pointerOnly = onlyPointer;
    updateMouseGrab();
}
void beginMouseFrame() {
    frameConfirm = menuConfirm || pendingConfirm; pendingConfirm = false;
    frameLook = locked() && !pointerOnly ? pendingLook : SDL_FPoint{};
    frameZoom = locked() && !pointerOnly ? pendingZoom : 0;
    frameScroll = captured() && pointerOnly ? pendingScroll : 0;
    pendingLook = {}; pendingZoom = pendingScroll = 0;
}
bool mouseControlsInitialized() { return window != nullptr; }
bool mouseMenuConfirm() { return window && focused && !menuOpen && pointerOnly && frameConfirm; }
bool mousePointer(float& x, float& y) {
    x = pointer.x; y = pointer.y;
    return window && focused && !menuOpen && (pointerOnly || (locked() && rightHeld)) &&
        std::abs(x) <= 1 && std::abs(y) <= 1;
}
bool mousePointerHeld() { return locked() && rightHeld; }
bool keyboardSkipHeld() { return window && focused && !menuOpen && skipHeld; }
SDL_FPoint mouseLook() { return frameLook; }
float mouseZoom() { return frameZoom; }
float takeMouseMenuScroll() { const auto scroll = frameScroll; frameScroll = 0; return scroll; }
void MouseOrbit::apply(TVec3f& position, TVec3f& target, TVec3f& cameraUp,
                       const TVec3f& gravityUp, const TVec3f& playerPosition, SDL_FPoint motion,
                       float zoom, float seconds) {
    if (!std::isfinite(motion.x) || !std::isfinite(motion.y) || !std::isfinite(zoom) ||
        !std::isfinite(seconds) || seconds < 0 || !std::isfinite(playerPosition.x) ||
        !std::isfinite(playerPosition.y) || !std::isfinite(playerPosition.z)) return;
    TVec3f nextUp = gravityUp, direction = position - target;
    if (!normalize(nextUp)) return;
    if (!active) {
        if ((motion.x == 0 && motion.y == 0 && zoom == 0) || !normalize(direction)) return;
        horizontal = direction - nextUp * direction.dot(nextUp);
        if (!normalize(horizontal)) {
            horizontal = nextUp.cross(TVec3f(1, 0, 0));
            if (!normalize(horizontal)) { horizontal = nextUp.cross(TVec3f(0, 0, 1)); normalize(horizontal); }
        }
        up = nextUp; active = true;
    }
    TQuat4f rotation; rotation.setRotate(up, nextUp); rotation.transform(horizontal);
    horizontal -= nextUp * horizontal.dot(nextUp);
    if (!normalize(horizontal)) { active = false; return; }
    // Coopdx sensitivity 50: 16 angle units per pixel; match the user's inverted-Y setting.
    constexpr float radiansPerPixel = 0.0015339808f, tiltLimit = 1.1780972f;
    const float sensitivity = option(Option::CameraSensitivity)/50.0f;
    rotation.setRotate(nextUp, motion.x * radiansPerPixel * sensitivity * (option(Option::CameraInvertX) ? 1 : -1)); rotation.transform(horizontal);
    pitch = std::clamp(pitch + motion.y * radiansPerPixel * sensitivity * (option(Option::CameraInvertY) ? -1 : 1), -tiltLimit, tiltLimit);
    if (zoom != 0) distanceIndex = std::clamp(distanceIndex + (zoom > 0 ? 1 : -1), 0, 2);
    constexpr float distances[] = {750, 1250, 2000};
    const float step = 7500 * std::min(seconds, 0.1f);
    distance += std::clamp(distances[distanceIndex] - distance, -step, step);
    target = playerPosition + nextUp * 125;
    position = target + (horizontal * std::cos(pitch) + nextUp * std::sin(pitch)) * distance;
    cameraUp = nextUp; up = nextUp;
}
}
