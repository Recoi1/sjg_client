#include "nand.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include <revolution/nand.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
namespace {
std::mutex callbackMutex;
std::condition_variable callbackReady;
bool completed;
s32 completedResult;
int cookie;
std::thread::id callbackThread;
void complete(s32 result, NANDCommandBlock* block) {
    assert(NANDGetUserData(block) == &cookie);
    std::lock_guard lock(callbackMutex);
    completedResult = result;
    callbackThread = std::this_thread::get_id();
    completed = true;
    callbackReady.notify_one();
}
template<class F> s32 async(F operation) {
    NANDCommandBlock block{};
    NANDSetUserData(&block, &cookie);
    { std::lock_guard lock(callbackMutex); completed = false; }
    assert(operation(complete, &block) == NAND_RESULT_OK);
    std::unique_lock lock(callbackMutex);
    assert(callbackReady.wait_for(lock, std::chrono::seconds(2), [] { return completed; }));
    assert(callbackThread != std::this_thread::get_id());
    return completedResult;
}
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const auto parent = fs::canonical(argv[1]);
    const auto root = parent / ("nand-check-" + std::to_string(GetCurrentProcessId()));
    assert(fs::create_directory(root));
    galaxy::setNandRoot(root);
    NANDFileInfo info;
    assert(NANDOpen("save", &info, NAND_ACCESS_READ) == NAND_RESULT_FATAL_ERROR);
    assert(NANDInit() == 0 && NANDInit() == 0 && nandIsInitialized());
    char home[NAND_MAX_PATH]{};
    assert(NANDGetHomeDir(home) == 0);
    const auto directory = root / std::string(home + 1);
    const auto target = directory / "save";
    auto* heapRoot=JKRExpHeap::createRoot(1,false);
    auto* tinyHeap=JKRExpHeap::create(1024,heapRoot,false); assert(tinyHeap);
    const auto tinyFree=tinyHeap->getTotalFreeSize(); tinyHeap->becomeCurrentHeap();
    assert(NANDCreate("save", NAND_PERM_RWALL, 0) == 0);
    assert(NANDOpen("save", &info, NAND_ACCESS_READ) == 0 && NANDClose(&info) == 0);
    assert(JKRHeap::sCurrentHeap==tinyHeap && tinyHeap->getTotalFreeSize()==tinyFree);
    JKRHeap::sCurrentHeap=nullptr; tinyHeap->destroy();
    assert(NANDCreate("save", NAND_PERM_RWALL, 0) == NAND_RESULT_EXISTS);
    assert(NANDOpen("save", &info, NAND_ACCESS_WRITE) == 0);
    assert(NANDWrite(&info, "first", 5) == 5);
    assert(read(target).empty());
    assert(NANDClose(&info) == 0 && read(target) == "first");
    assert(NANDClose(&info) == NAND_RESULT_INVALID);
    assert(NANDOpen("save", &info, NAND_ACCESS_READ) == 0);
    u32 length = 0;
    assert(NANDGetLength(&info, &length) == 0 && length == 5);
    std::array<char, 16> buffer{};
    assert(NANDRead(&info, buffer.data(), buffer.size()) == 5 && std::string(buffer.data()) == "first");
    assert(NANDRead(&info, buffer.data(), 1) == 0);
    assert(NANDWrite(&info, "bad", 3) == NAND_RESULT_ACCESS);
    assert(NANDDelete("save") == NAND_RESULT_OPENFD);
    assert(NANDClose(&info) == 0);
    assert(NANDOpen("save", &info, NAND_ACCESS_WRITE) == 0);
    assert(NANDWrite(&info, "new!!", 5) == 5);
    assert(NANDWrite(&info, nullptr, 1) == NAND_RESULT_INVALID);
    assert(NANDClose(&info) == NAND_RESULT_INVALID && read(target) == "first");
    assert(NANDOpen("save", &info, NAND_ACCESS_RW) == 0);
    assert(NANDRead(&info, buffer.data(), 2) == 2);
    assert(NANDWrite(&info, "NEW", 3) == 3);
    assert(NANDClose(&info) == 0 && read(target) == "fiNEW");
    assert(async([&](auto cb, auto block) { return NANDPrivateOpenAsync("save", &info, NAND_ACCESS_RW, cb, block); }) == 0);
    assert(async([&](auto cb, auto block) { return NANDGetLengthAsync(&info, &length, cb, block); }) == 0 && length == 5);
    assert(async([&](auto cb, auto block) { return NANDSeekAsync(&info, 2, 0, cb, block); }) == 2);
    assert(async([&](auto cb, auto block) { return NANDWriteAsync(&info, "xyz", 3, cb, block); }) == 3);
    assert(read(target) == "fiNEW");
    assert(async([&](auto cb, auto block) { return NANDSeekAsync(&info, -3, 1, cb, block); }) == 2);
    assert(async([&](auto cb, auto block) { return NANDReadAsync(&info, buffer.data(), 3, cb, block); }) == 3);
    assert(std::string(buffer.data(), 3) == "xyz");
    assert(async([&](auto cb, auto block) { return NANDSafeCloseAsync(&info, cb, block); }) == 0 && read(target) == "fixyz");
    assert(async([&](auto cb, auto block) { return NANDPrivateOpenAsync("missing", &info, NAND_ACCESS_READ, cb, block); }) == NAND_RESULT_NOEXISTS);
    assert(async([&](auto cb, auto block) { return NANDPrivateOpenAsync("../escape", &info, NAND_ACCESS_READ, cb, block); }) == NAND_RESULT_INVALID);
    galaxy::shutdownNand();
    assert(!nandIsInitialized() && NANDInit() == 0);
    assert(NANDOpen("save", &info, NAND_ACCESS_READ) == 0);
    buffer.fill(0);
    assert(NANDRead(&info, buffer.data(), buffer.size()) == 5 && std::string(buffer.data()) == "fixyz");
    assert(NANDClose(&info) == 0);
    for (const char* invalid : {"../escape", "a/../escape", "/../escape", "C:\\escape", "NUL", "con.txt", "bad.", "a//b", "bad?"})
        assert(NANDCreate(invalid, 0, 0) == NAND_RESULT_INVALID);
    std::array<char, NAND_MAX_PATH> noTerminator;
    noTerminator.fill('a');
    assert(NANDCreate(noTerminator.data(), 0, 0) == NAND_RESULT_INVALID);
    assert(NANDOpen("missing", &info, NAND_ACCESS_WRITE) == NAND_RESULT_NOEXISTS);
    assert(NANDOpen("save", &info, NAND_ACCESS_NONE) == NAND_RESULT_INVALID);
    u32 answer = ~0u;
    assert(NANDCheck(1, 1, &answer) == 0 && answer == 0);
    auto* primaryHeap = JKRExpHeap::createRoot(1, false); assert(primaryHeap);
    auto* sceneHeap = JKRExpHeap::create(64 * 1024, primaryHeap, false); assert(sceneHeap);
    sceneHeap->becomeCurrentHeap();
    const auto freeBefore = sceneHeap->getFreeSize();
    assert(async([&](auto cb, auto block) { return NANDPrivateOpenAsync("missing-after-scene-change", &info, NAND_ACCESS_READ, cb, block); }) == NAND_RESULT_NOEXISTS);
    assert(sceneHeap->getFreeSize() == freeBefore);
    JKRHeap::setCurrentHeap(nullptr); sceneHeap->destroy();
    galaxy::shutdownNandWorker();
    fs::create_directory(directory / "backup");
    const std::string backup = std::string(home) + "/backup";
    assert(NANDMove("save", backup.c_str()) == 0);
    assert(!fs::exists(target) && read(directory / "backup/save") == "fixyz");
    assert(NANDDelete("backup") == NAND_RESULT_NOTEMPTY);
    assert(NANDDelete("backup/save") == 0);
    assert(NANDDelete("backup") == 0);
    assert(NANDDelete("missing") == NAND_RESULT_NOEXISTS);
    assert(NANDDelete("/") == NAND_RESULT_ACCESS);
    NANDBanner banner{};
    const u16 title[] = {'G', 0}, empty[] = {0};
    NANDInitBanner(&banner, NAND_BANNER_FLAG_NOTCOPY, title, empty);
    assert(banner.signature == NAND_BANNER_SIGNATURE && banner.flag == NAND_BANNER_FLAG_NOTCOPY);
    assert(banner.comment[0][0] == 'G' && banner.comment[0][1] == 0 && banner.comment[1][0] == ' ');
    for (u16 character : {'G', 'L'}) {
        banner.comment[0][0] = character;
        assert(NANDCreate("/tmp/banner.bin", NAND_PERM_RWALL, 0) == 0);
        assert(NANDOpen("/tmp/banner.bin", &info, NAND_ACCESS_WRITE) == 0);
        assert(NANDWrite(&info, &banner, NAND_BANNER_SIZE(1)) == NAND_BANNER_SIZE(1));
        assert(NANDClose(&info) == 0 && NANDMove("/tmp/banner.bin", home) == 0);
        const auto saved = read(directory / "banner.bin");
        assert(saved.size() == NAND_BANNER_SIZE(1) && !memcmp(saved.data(), &banner, saved.size()));
        assert(!fs::exists(root / "tmp/banner.bin"));
    }
    for (const auto& entry : fs::directory_iterator(directory)) assert(!entry.path().filename().string().starts_with(".native-"));
    assert(NANDCreate("unfinished", 0, 0) == 0);
    assert(NANDOpen("unfinished", &info, NAND_ACCESS_WRITE) == 0);
    assert(NANDWrite(&info, "old", 3) == 3 && NANDClose(&info) == 0);
    assert(NANDOpen("unfinished", &info, NAND_ACCESS_WRITE) == 0);
    assert(NANDWrite(&info, "new", 3) == 3);
    galaxy::shutdownNand();
    assert(read(directory / "unfinished") == "old");
    assert(fs::remove(root / "tmp"));
    { std::ofstream blocked(root / "tmp"); blocked << "preserve"; }
    assert(NANDInit() == NAND_RESULT_INVALID && !nandIsInitialized());
    assert(read(root / "tmp") == "preserve" && read(directory / "unfinished") == "old");
    assert(fs::remove(root / "tmp") && NANDInit() == 0);
    assert(fs::is_directory(root / "tmp"));
    assert(NANDOpen("unfinished", &info, NAND_ACCESS_WRITE)==0);
    assert(!galaxy::setNandReadOnly(true) && !galaxy::nandReadOnly());
    assert(NANDClose(&info)==0 && galaxy::setNandReadOnly(true));
    assert(galaxy::nandReadOnly() && NANDCreate("lobby-new",0x30,0)==NAND_RESULT_ACCESS);
    assert(NANDOpen("unfinished",&info,NAND_ACCESS_WRITE)==NAND_RESULT_ACCESS);
    assert(NANDPrivateCreateDir("/tmp/lobby",0x30,0)==NAND_RESULT_ACCESS);
    assert(NANDDelete("unfinished")==NAND_RESULT_ACCESS && NANDMove("unfinished","/tmp")==NAND_RESULT_ACCESS);
    assert(NANDOpen("unfinished",&info,NAND_ACCESS_READ)==0);
    std::array<char,3> lobbyRead{};assert(NANDRead(&info,lobbyRead.data(),3)==3 && !memcmp(lobbyRead.data(),"old",3));
    assert(NANDWrite(&info,"bad",3)==NAND_RESULT_ACCESS && NANDClose(&info)==0);
    assert(read(directory / "unfinished")=="old" && !fs::exists(directory/"lobby-new"));
    assert(galaxy::setNandReadOnly(false) && !galaxy::nandReadOnly());
    galaxy::shutdownNand();
    // Remove only the fresh test directory we created under the supplied build directory.
    assert(fs::canonical(root).parent_path() == parent && root.filename().string().starts_with("nand-check-"));
    fs::remove_all(root);
}
