#include "RVLFaceLibInternal.h"
#include "nand.hpp"
#include "os_services.hpp"
#include "rarc.hpp"
#include "yaz0.hpp"
#include "crash.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>
std::atomic<unsigned> callbacks{0};
void formatted() { ++callbacks; }
int main(int argc, char** argv) {
    assert(argc == 3); galaxy::installCrashHandler();
    const auto parent = std::filesystem::canonical(argv[2]);
    const auto root = parent / ("rfl-check-" + std::to_string(GetCurrentProcessId()));
    assert(std::filesystem::create_directory(root)); galaxy::setNandRoot(root); assert(NANDInit() == 0);
    RFLiCharInfo lhs{}, rhs{};
    assert(RFLiIsSameFaceCore(&lhs, &rhs) && !RFLiIsSameFaceCore(nullptr, &rhs));
    rhs.eye.x = 1; assert(!RFLiIsSameFaceCore(&lhs, &rhs)); rhs.eye.x = 0;
    rhs.personal.name[0] = L'X'; assert(RFLiIsSameFaceCore(&lhs, &rhs));
    std::ifstream input(argv[1], std::ios::binary); assert(input);
    std::vector<u8> packed{std::istreambuf_iterator<char>(input), {}};
    const auto bytes = packed[0] == 'Y' ? galaxy::decodeYaz0(packed) : packed;
    galaxy::Rarc archive(bytes); const auto resource = archive.resource("/RFL_Res.dat");
    const auto workSize = RFLGetWorkSize(FALSE);
    std::vector<u8> work(workSize + 64, 0xcd);
    auto* buffer = reinterpret_cast<u8*>((reinterpret_cast<uintptr_t>(work.data()) + 31) & ~uintptr_t(31));
    const auto status = RFLInitRes(buffer, const_cast<u8*>(resource.data()), resource.size(), FALSE);
    assert(status == RFLErrcode_Success && RFLAvailable());
    assert(RFLiFormatAsync(formatted) == RFLErrcode_Busy);
    assert(RFLWaitAsync() == RFLErrcode_Success);
    assert(callbacks == 1 && !RFLiNotFoundError() && !RFLiNeedRepairError());
    const auto path = root / "shared2/menu/FaceLib/RFL_DB.dat";
    assert(std::filesystem::file_size(path) == sizeof(RFLiDatabase) + RFLi_HDB_DATA_MAX * sizeof(RFLiHiddenCharData));
    assert(RFLiCalculateCRC(RFLiGetDBManager()->database, sizeof(RFLiDatabase)) == 0);
    for (auto* p = buffer + workSize; p < work.data() + work.size(); ++p) assert(*p == 0xcd);
    RFLExit(); assert(!RFLAvailable());
    assert(NANDInit() == 0);
    const auto reloaded = RFLInitRes(buffer, const_cast<u8*>(resource.data()), resource.size(), FALSE);
    assert(reloaded == RFLErrcode_Success);
    assert(RFLAvailable() && !RFLiNotFoundError()); RFLExit();
    galaxy::shutdownNandWorker(); galaxy::shutdownOsServices(); galaxy::shutdownNand();
    std::filesystem::remove_all(root);
    std::cout << "Original Mii runtime: initialization, face comparison, asynchronous database format, CRC, reload and work-buffer bounds passed\n";
    return 0;
}
