#include <revolution/os.h>
#include <revolution/sc.h>
#include <revolution/mem/allocator.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "crash.hpp"
#ifdef GALAXY_RUNTIME_OPTIONS
#include "runtime_options.hpp"
#endif

extern "C" {
// These are the current PC port defaults: English assets, stereo output, and a 1280x720 window.
u8 SCGetLanguage() {
#ifdef GALAXY_RUNTIME_OPTIONS
    constexpr u8 languages[]{SC_LANG_ENGLISH, SC_LANG_FRENCH, SC_LANG_SPANISH};
    return languages[galaxy::appliedOption(galaxy::Option::Language)];
#else
    return SC_LANG_ENGLISH;
#endif
}
u8 SCGetSoundMode() { return SC_SOUND_MODE_STEREO; }
u8 SCGetAspectRatio() {
#ifdef GALAXY_RUNTIME_OPTIONS
    return galaxy::appliedOption(galaxy::Option::Aspect43) ? SC_ASPECT_RATIO_4x3 : SC_ASPECT_RATIO_16x9;
#else
    return SC_ASPECT_RATIO_16x9;
#endif
}
u8 SCGetProgressiveMode() { return SC_PROGRESSIVE_MODE_ON; }
u8 SCGetEuRgb60Mode() { return SC_EURGB60_MODE_OFF; }

void* MEMAllocFromAllocator(MEMAllocator* allocator, u32 size) {
    if (!allocator || !allocator->pFunc || !allocator->pFunc->pfAlloc) return nullptr;
    return allocator->pFunc->pfAlloc(allocator, size);
}
void MEMFreeToAllocator(MEMAllocator* allocator, void* memory) {
    if (!memory) return;
    if (!allocator || !allocator->pFunc || !allocator->pFunc->pfFree) throw std::invalid_argument("invalid SDK allocator");
    allocator->pFunc->pfFree(allocator, memory);
}
void OSVReport(const char* message, va_list arguments) {
    if (message) { std::vfprintf(stdout, message, arguments); std::fflush(stdout); }
}
void OSReport(const char* message, ...) {
    va_list arguments; va_start(arguments, message); OSVReport(message, arguments); va_end(arguments);
}
void OSRegisterVersion(const char* version) { OSReport("%s\n", version ? version : ""); }
void OSPanic(const char* file, int line, const char* message, ...) {
    std::fprintf(stderr, "PANIC %s:%d: ", file ? file : "unknown", line);
    va_list arguments; va_start(arguments, message);
    if (message) std::vfprintf(stderr, message, arguments);
    va_end(arguments); std::fputc('\n', stderr); galaxy::printStackTrace(); std::fflush(stderr); std::abort();
}
void OSFatal(GXColor, GXColor, const char* message) { OSPanic("native platform", 0, "%s", message ? message : "fatal error"); }
}
