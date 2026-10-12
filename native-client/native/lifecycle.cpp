#include "lifecycle.hpp"
#include <revolution/os.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <vector>

namespace {
std::atomic<OSPowerCallback> powerCallback{nullptr};
thread_local OSContext* context = nullptr;
}
namespace galaxy {
void requestPowerOff() {
    if (const auto callback = powerCallback.load()) callback();
    else throw ApplicationExit(false);
}
void restartApplication() {
    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
    if (!length || length >= executable.size()) throw std::runtime_error("Could not locate the game executable for restart");
    const wchar_t* command = GetCommandLineW();
    std::vector<wchar_t> commandLine(command, command + std::wcslen(command) + 1);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.data(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        throw std::runtime_error("Could not restart the game");
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
}
extern "C" {
OSPowerCallback OSSetPowerCallback(OSPowerCallback callback) { return powerCallback.exchange(callback); }
BOOL OSGetResetButtonState() { return (GetAsyncKeyState(VK_F10) & 0x8000) != 0; }
void OSRestart(u32) { throw galaxy::ApplicationExit(true); }
void OSRebootSystem() { throw galaxy::ApplicationExit(true); }
void OSReturnToMenu() { throw galaxy::ApplicationExit(false); }
void OSShutdownSystem() { throw galaxy::ApplicationExit(false); }
void PPCSync() { std::atomic_thread_fence(std::memory_order_seq_cst); }
void OSClearContext(OSContext* value) {
    if (!value) throw std::invalid_argument("Missing OS context");
    value->mode = value->state = 0;
    if (context == value) context = nullptr;
}
void OSSetCurrentContext(OSContext* value) { context = value; }
OSContext* OSGetCurrentContext() { return context; }
}
