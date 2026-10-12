#pragma once
#include <cstdint>
#include <array>
#include <filesystem>
#include <SDL3/SDL_gamepad.h>
namespace galaxy {
constexpr unsigned keyActionCount=16, classicActionCount=15;
using InputBindings = std::array<std::array<unsigned,16>,3>;
InputBindings inputBindings();
bool setInputBindings(const InputBindings& next);
void beginInputBindingEdit();
void discardInputBindingEdit();
bool applyInputBindingEdit();
unsigned editingBindingCode(unsigned column,unsigned action);
void initializeInputBindings(const std::filesystem::path& userPath);
unsigned bindingCode(unsigned column,unsigned action);
bool setBinding(unsigned column,unsigned action,unsigned code);
bool resetInputBindings();
void captureBinding(unsigned column,unsigned action);
void cancelBindingCapture();
bool bindingCaptureActive();
bool takeBindingEscape();
bool bindingButtonSuppressed();
bool bindingSaveFailed();
const wchar_t* bindingLabel(unsigned column,unsigned action);
const char* bindingName(unsigned column,unsigned action);
uint32_t boundClassicButtons(SDL_Gamepad* controller);
float mouseBindingValue(unsigned action);
}
