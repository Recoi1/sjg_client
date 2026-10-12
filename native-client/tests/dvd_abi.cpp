#include <revolution/dvd.h>
#include <cassert>
#include <cstddef>
extern "C" void checkGalaxyDvdAbi(size_t blockSize, size_t fileSize, size_t userDataOffset, size_t fileLengthOffset) {
    assert(blockSize == sizeof(DVDCommandBlock) && fileSize == sizeof(DVDFileInfo));
    assert(userDataOffset == offsetof(DVDCommandBlock, userData) && fileLengthOffset == offsetof(DVDFileInfo, length));
}
