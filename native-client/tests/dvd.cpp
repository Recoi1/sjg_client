#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

extern "C" void checkGalaxyDvdAbi(size_t, size_t, size_t, size_t);
namespace {
std::atomic<unsigned> callbacks{0};
int callbackData;
void readDone(s32 result, DVDFileInfo* file) {
    assert(result == 64 && file->cb.userData == &callbackData && file->cb.transferredSize == 64);
    ++callbacks;
}
bool expectedDisc;
void checked(s32 result, DVDCommandBlock* block) { assert(bool(result) == expectedDisc && block->userData == &callbackData); ++callbacks; }
void waitFor(unsigned count) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (callbacks.load() < count && std::chrono::steady_clock::now() < limit) std::this_thread::yield();
    assert(callbacks.load() == count);
}
}
int main(int argc, char** argv) {
    assert(argc == 3);
    checkGalaxyDvdAbi(sizeof(DVDCommandBlock), sizeof(DVDFileInfo), offsetof(DVDCommandBlock, userData), offsetof(DVDFileInfo, length));
    DVDCommandBlock block{};
    block.userData = &callbackData;
    assert(!DVDCheckDisk() && DVDCheckDiskAsync(&block, checked));
    assert(!aurora_dvd_open("missing-galaxy.wbfs"));
    assert(aurora_dvd_open(argv[1]));
    expectedDisc = true;
    assert(DVDCheckDisk() && DVDGetDriveStatus() == DVD_STATE_END && DVDCheckDiskAsync(&block, checked));
    assert(aurora_dvd_base_entry_count() > 1000);
    assert(std::memcmp(DVDGetCurrentDiskID()->gameName, "RMGE", 4) == 0);
    assert(DVDConvertPathToEntrynum("/ObjectData/Mario.arc") == DVDConvertPathToEntrynum("/objectdata/MARIO.ARC"));
    assert(DVDConvertPathToEntrynum("/missing-file") == -1);
    std::array<DVDFileInfo, 2> files{};
    std::array<std::array<u8, 64>, 2> bytes{}, expected{};
    const char* names[]{"/ObjectData/Mario.arc", "/MovieData/PrologueA.thp"};
    for (size_t i = 0; i < files.size(); ++i) {
        const auto path = std::filesystem::path(argv[2]) / (names[i] + 1);
        std::ifstream extracted(path, std::ios::binary);
        extracted.read(reinterpret_cast<char*>(expected[i].data()), 64);
        assert(extracted.gcount() == 64);
        assert(DVDOpen(names[i], &files[i]) && files[i].length == std::filesystem::file_size(path));
        files[i].cb.userData = &callbackData;
        assert(DVDReadAsyncPrio(&files[i], bytes[i].data(), 64, 0, readDone, 2));
    }
    waitFor(4);
    assert(bytes == expected);
    for (auto& file : files) {
        std::array<u8, 64> tail{};
        assert(!DVDReadAsyncPrio(&file, tail.data(), -1, 0, nullptr, 2));
        assert(!DVDReadAsyncPrio(&file, tail.data(), 64, 0x7FFFFFFF, nullptr, 2));
        assert(DVDSeekPrio(&file, 0, 2) == 0);
        assert(DVDReadPrio(&file, tail.data(), 64, file.length - 64, 2) == 64);
        assert(DVDClose(&file) && file.cb.userData == &callbackData);
        assert(!DVDClose(&file) && !DVDReadAsyncPrio(&file, tail.data(), 64, 0, nullptr, 2));
    }
    DVDFileInfo again{};
    assert(DVDFastOpen(DVDConvertPathToEntrynum(names[0]), &again));
    assert(DVDClose(&again));
    aurora_dvd_close();
    expectedDisc = false;
    assert(!DVDCheckDisk() && DVDCheckDiskAsync(&block, checked));
    std::cout << "Owned WBFS: disc identity, entry IDs, asynchronous reads, callback data, and close passed\n";
    return 0;
}
