#include "lifecycle.hpp"
#include "Game/NWC24/NWC24System.hpp"
#include <revolution/os.h>
#include <cassert>
#include <thread>

namespace {
unsigned powerCalls = 0;
void power() { ++powerCalls; }
template<class F> void exitRequest(F f, bool restart) {
    bool caught = false;
    try { f(); } catch (const galaxy::ApplicationExit& request) { caught = true; assert(request.restart == restart); }
    assert(caught);
}
}
int main() {
    assert(OSSetPowerCallback(power) == nullptr);
    galaxy::requestPowerOff();
    assert(powerCalls == 1 && OSSetPowerCallback(nullptr) == power);
    exitRequest(galaxy::requestPowerOff, false);
    exitRequest([] { OSRestart(0); }, true);
    exitRequest(OSRebootSystem, true);
    exitRequest(OSReturnToMenu, false);
    exitRequest(OSShutdownSystem, false);
    OSContext context{};
    context.mode = context.state = 0xFFFF;
    context.gpr[3] = 12345;
    OSSetCurrentContext(&context);
    std::thread worker([] { assert(OSGetCurrentContext() == nullptr); });
    worker.join();
    assert(OSGetCurrentContext() == &context);
    OSClearContext(&context);
    assert(!context.mode && !context.state && context.gpr[3] == 12345 && OSGetCurrentContext() == nullptr);
    PPCSync();
    NWC24System messages(nullptr, 10);
    NWC24Err error = NWC24_OK;
    s32 code = 0;
    u32 size = 1;
    assert(!messages.open(&error, &code) && error == NWC24_ERR_DISABLED && code == NWC24_ERR_DISABLED);
    assert(!messages.send(nullptr, nullptr, nullptr, 0, nullptr, 0));
    assert(messages.isSent(&error, &size) && error == NWC24_ERR_DISABLED && size == 0);
    assert(!messages.close(&error) && error == NWC24_ERR_LIB_NOT_OPENED);
    assert(!messages.open(nullptr, nullptr) && messages.isSent(nullptr, nullptr) && !messages.close(nullptr));
    return 0;
}
