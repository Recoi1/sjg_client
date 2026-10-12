#pragma once
#include <cstdint>
namespace galaxy {
struct ControllerSettings {
    bool allStars = false, swapLeft = false, swapRight = false;
    bool operator==(const ControllerSettings&) const = default;
};
ControllerSettings getControllerSettings();
bool setControllerSettings(ControllerSettings settings);
struct PadSnapshot {
    uint32_t buttons = 0;
    float stickX = 0, stickY = 0;
    float pointerX = 0, pointerY = 0;
    float rightStickX = 0, rightStickY = 0;
    float leftTrigger = 0, rightTrigger = 0;
    uint32_t classicButtons = 0;
    float accX = 0, accY = 0, accZ = 1;
    bool connected = false, pointerValid = false, mousePointerActive = false;
    bool classic = false;
    uint8_t battery = 4;
};
bool pointerVisible(int channel);
void initializeNativePads();
PadSnapshot readNativePad(int channel);
void rumbleNativePad(int channel, bool enabled);
}
