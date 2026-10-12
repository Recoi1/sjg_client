#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <SDL3/SDL_audio.h>
namespace galaxy {
enum class Option : unsigned {
    Aspect43, Vsync, Backend, FrameMode, FrameLimit, Interpolation, Filtering, AntiAliasing, DrawDistance,
    CameraMode, CameraInvertX, CameraInvertY, CameraSensitivity, LooseCameraCollision,
    BackgroundGamepad, DisableGamepads, Gamepad, Deadzone, RumbleStrength,
    RotateLeftStick, InvertLeftX, InvertLeftY, RotateRightStick, InvertRightX, InvertRightY,
    SoundOutput, MasterVolume, MusicVolume, EffectsVolume, EnvironmentVolume, DistantSoundFade, MuteUnfocused,
    Language, Count
};
constexpr unsigned optionCount = static_cast<unsigned>(Option::Count);
using RuntimeOptions = std::array<unsigned, optionCount>;
RuntimeOptions runtimeOptions();
bool setRuntimeOptions(const RuntimeOptions& next);
void initializeRuntimeOptions(const std::filesystem::path& userPath);
unsigned option(Option key);
unsigned appliedOption(Option key);
void transformStick(float& x,float& y,bool right);
bool setOption(Option key, unsigned value);
unsigned optionMaximum(Option key);
SDL_AudioDeviceID soundOutputDevice();
float soundVolume(Option bus);
void setAudioFocus(bool focused);
void applyRuntimeOptions();
bool openUserDataDirectory();
}
