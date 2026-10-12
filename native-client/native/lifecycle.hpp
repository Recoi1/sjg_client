#pragma once
#include <exception>

namespace galaxy {
struct ApplicationExit : std::exception {
    bool restart;
    explicit ApplicationExit(bool restart) : restart(restart) {}
    const char* what() const noexcept override { return restart ? "Restart requested" : "Quit requested"; }
};
void requestPowerOff();
void restartApplication();
}
