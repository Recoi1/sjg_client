#include "Game/System/FunctionAsyncExecutor.hpp"
#include "Game/Util/Functor.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "crash.hpp"
#include <cassert>
#include <stdexcept>
#include <thread>
struct Work {
    JKRHeap* expected;
    bool called = false;
    void run() {
        assert(JKRHeap::sCurrentHeap == expected); auto* data = new u8[295808]; assert(JKRHeap::findFromRoot(data) == expected); delete[] data; called = true;
    }
    void fail() { assert(JKRHeap::sCurrentHeap == expected); throw std::runtime_error("test callback"); }
};
int main() {
    galaxy::installCrashHandler(); auto* root = JKRExpHeap::createRoot(1, false); assert(root);
    auto* scene = JKRExpHeap::create(1024 * 1024, root, false); auto* main = JKRExpHeap::create(64 * 1024, root, false); assert(scene && main);
    Work work{scene}; FunctionAsyncExecInfo* info = nullptr;
    std::thread worker([&] { scene->becomeCurrentHeap(); auto functor = MR::Functor(&work, &Work::run); info = new FunctionAsyncExecInfo(functor.clone(scene), 0, "scene callback"); }); worker.join();
    main->becomeCurrentHeap(); FunctionAsyncExecutorOnMainThread executor(OSGetCurrentThread()); assert(OSSendMessage(&executor.mQueue, info, OS_MESSAGE_NOBLOCK)); executor.update();
    assert(work.called && info->mIsEnd && JKRHeap::sCurrentHeap == main); OSMessage done; assert(OSReceiveMessage(&info->mQueue, &done, OS_MESSAGE_NOBLOCK)); delete info;
    scene->becomeCurrentHeap(); auto fail = MR::Functor(&work, &Work::fail); FunctionAsyncExecInfo failing(fail.clone(scene), 0, "failure callback"); main->becomeCurrentHeap();
    const auto priority = OSGetThreadPriority(OSGetCurrentThread()); bool rejected = false;
    try { failing.execute(); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && JKRHeap::sCurrentHeap == main && OSGetThreadPriority(OSGetCurrentThread()) == priority && root->check());
    root->becomeCurrentHeap();
}

