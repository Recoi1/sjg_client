#include "crash.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include "JSystem/JKernel/JKRHeap.hpp"

namespace {
void printStack(CONTEXT context) {
    static SRWLOCK lock = SRWLOCK_INIT;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    auto* heap = JKRHeap::sCurrentHeap;
    JKRHeap::sCurrentHeap = nullptr;
    const auto process = GetCurrentProcess();
    if (SymInitialize(process, nullptr, TRUE)) {
        alignas(SYMBOL_INFO) char bytes[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(bytes);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
        for (unsigned i = 0; i < 24 && frame.AddrPC.Offset; ++i) {
            DWORD64 displacement = 0;
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
                std::fprintf(stderr, "  %s + 0x%llx\n", symbol->Name, static_cast<unsigned long long>(displacement));
            else std::fprintf(stderr, "  frame %p\n", reinterpret_cast<void*>(frame.AddrPC.Offset));
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context,
                             nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        }
        SymCleanup(process);
    }
    JKRHeap::sCurrentHeap = heap;
    ReleaseSRWLockExclusive(&lock);
}
LONG WINAPI reportFault(EXCEPTION_POINTERS* fault) {
    const auto& record = *fault->ExceptionRecord;
    std::fprintf(stderr, "Native crash 0x%08lx at %p (image base %p)\n",
                 record.ExceptionCode, record.ExceptionAddress, GetModuleHandleW(nullptr));
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        std::fprintf(stderr, "Memory operation %llu at address 0x%llx\n",
                     static_cast<unsigned long long>(record.ExceptionInformation[0]),
                     static_cast<unsigned long long>(record.ExceptionInformation[1]));
    std::fprintf(stderr, "Registers: rax=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx rsp=%llx\n",
                 fault->ContextRecord->Rax, fault->ContextRecord->Rcx, fault->ContextRecord->Rdx,
                 fault->ContextRecord->Rsi, fault->ContextRecord->Rdi, fault->ContextRecord->Rsp);
    printStack(*fault->ContextRecord);
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
void reportUncaught() noexcept {
    if (auto error = std::current_exception()) {
        try { std::rethrow_exception(error); }
        catch (const std::exception& failure) { std::fprintf(stderr, "Uncaught native error: %s\n", failure.what()); }
        catch (...) { std::fputs("Uncaught native error of unknown type\n", stderr); }
    } else {
        std::fputs("Native process terminated without an active exception\n", stderr);
    }
    galaxy::printStackTrace(); std::fflush(stderr); std::abort();
}
LONG WINAPI reportBreakpoint(EXCEPTION_POINTERS* fault) {
    if (fault->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) reportFault(fault);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
namespace galaxy {
void printStackTrace() { CONTEXT context{}; RtlCaptureContext(&context); printStack(context); }
void installCrashHandler() {
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(reportFault);
    AddVectoredExceptionHandler(1, reportBreakpoint);
    std::set_terminate(reportUncaught);
}
}
