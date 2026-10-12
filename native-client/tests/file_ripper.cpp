#include "Game/System/FileRipper.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "crash.hpp"
#include <aurora/dvd.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
int main(int argc, char** argv) {
    assert(argc == 2); galaxy::installCrashHandler(); assert(aurora_dvd_open(argv[1]));
    auto* root = JKRExpHeap::createRoot(1, false); assert(root);
    auto* heap = JKRExpHeap::create(8 * 1024 * 1024, root, false); assert(heap);
    auto* adjacent = JKRExpHeap::create(1024 * 1024, root, false); assert(adjacent);
    root->becomeCurrentHeap();
    {
    const char* paths[]{"/ObjectData/Mario.arc", "/AudioRes/Seqs/JaiSeq.arc", "/AudioRes/Info/JaiRemixSeq.arc"};
    for (const auto* path : paths) {
        u32 size = 0;
        auto* bytes = static_cast<u8*>(FileRipper::loadToMainRAM(path, nullptr, true, heap, FileRipper::UNK_1, &size));
        assert(bytes && size >= 32 && !memcmp(bytes, "RARC", 4));
        assert(heap->check() && adjacent->check() && root->check());
        { JKRMemArchive archive; assert(archive.mountFixed(bytes, size, JKR_MEM_BREAK_FLAG_0)); assert(archive.countResource()); }
        heap->free(bytes); assert(heap->check());
    }
    DVDFileInfo file{}; assert(DVDOpen(paths[0], &file));
    std::vector<u8> output(48, 0xcd);
    assert(FileRipper::decompressFromDVD(&file, output.data(), (file.length + 31) & ~31u, 16, nullptr, 0));
    assert(!memcmp(output.data(), "RARC", 4));
    for (unsigned i = 16; i < 48; ++i) assert(output[i] == 0xcd);
    bool rejected = false;
    try { FileRipper::decompressFromDVD(&file, output.data(), 16, 16, nullptr, 0); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected && DVDClose(&file));
    assert(DVDOpen(paths[0], &file)); u8 header[32]; assert(DVDReadPrio(&file, header, 32, 0, 2) == 32); assert(DVDClose(&file));
    const u32 expanded = u32(header[4]) << 24 | u32(header[5]) << 16 | u32(header[6]) << 8 | header[7];
    auto* retainedOnly = JKRExpHeap::create(expanded + 2048, root, false); assert(retainedOnly); retainedOnly->becomeCurrentHeap();
    u32 size = 0; auto* resource = FileRipper::loadToMainRAM(paths[0], nullptr, true, retainedOnly, FileRipper::UNK_1, &size);
    assert(resource && size == expanded && retainedOnly->check()); retainedOnly->free(resource);
    root->becomeCurrentHeap(); retainedOnly->destroy(); assert(root->check());
    }
    assert(root->check()); aurora_dvd_close(); assert(root->check());
    adjacent->destroy(); assert(root->check()); heap->destroy();
    // Aurora retains process-wide cache capacity; its backing root heap lives until process exit.
    assert(root->check());
    std::cout << "Original file loader: owned disc, backward allocation, bounded decode and adjacent heap checks passed\n";
    return 0;
}
