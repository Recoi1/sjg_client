#include "crash.hpp"
#include "JSystem/JUtility/JUTException.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include <exception>
#include <stdexcept>

int main(int argc, char** argv) {
    galaxy::installCrashHandler();
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "fault") == 0) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    } else if (std::strcmp(argv[1], "uncaught") == 0) {
        try { throw std::runtime_error("native test error"); }
        catch (...) { std::terminate(); }
    } else if (std::strcmp(argv[1], "panic") == 0) {
        JUTException::panic_f("native check", 123, "%s: %d%%", "progress", 50);
    }
    return 2;
}
