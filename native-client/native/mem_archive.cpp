#include "rarc.hpp"
#include "yaz0.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace {
std::filesystem::path sResourceRoot;
std::mutex sMountMutex;
constexpr u32 maxArchiveSize = 64 * 1024 * 1024;
}

u32 JKRArchive::getExpandedResSize(const void* resource) const {
    return getResSize(resource);
}

JKRMemArchive::JKRMemArchive() {
    mHeader = nullptr;
    mFileDataStart = nullptr;
    mInfoBlock = nullptr;
    mDirs = nullptr;
    mFiles = nullptr;
    mStringTable = nullptr;
    mExpandSizes = nullptr;
    mHeap = nullptr;
    mMountMode = MOUNT_MODE_0;
    mEntryNum = -1;
    _34 = 0;
    _58 = 0;
    _5C = 0;
    _6C = false;
}

JKRMemArchive::JKRMemArchive(long entryNum, EMountDirection direction) : JKRMemArchive() {
    open(entryNum, direction);
}

JKRMemArchive::~JKRMemArchive() {
    if (mIsMounted) removeVolumeList(&mLoaderLink);
}

void JKRMemArchive::fixedInit(long entryNum) {
    mMountMode = MOUNT_MODE_MEM;
    mEntryNum = entryNum;
    _34 = 1;
    _58 = 2;
    mHeap = JKRHeap::sCurrentHeap;
}

bool JKRMemArchive::mountFixed(void*, JKRMemBreakFlag) {
    throw std::runtime_error("Native mountFixed requires the actual buffer length");
}

bool JKRMemArchive::open(long, EMountDirection) {
    throw std::runtime_error("Native numeric DVD entry loading is not implemented");
}

bool JKRMemArchive::open(void* input, unsigned long size, JKRMemBreakFlag flag) {
    if (mIsMounted || !input || size == 0 || size > maxArchiveSize)
        throw std::runtime_error("Invalid native archive buffer or mount state");
    if (flag != JKR_MEM_BREAK_FLAG_0)
        throw std::runtime_error("Native archive input ownership transfer is not implemented");
    struct RestoreHeap { JKRHeap* previous = JKRHeap::sCurrentHeap; ~RestoreHeap() { JKRHeap::sCurrentHeap = previous; } } restore;
    // Native archive copies and metadata have independent ownership outside fixed Wii heap budgets.
    JKRHeap::sCurrentHeap = nullptr;
    auto* first = static_cast<u8*>(input);
    std::vector<u8> bytes(first, first + size);
    auto parsed = std::make_unique<galaxy::Rarc>(bytes);
    for (const auto& file : parsed->files_)
        if ((file.mFlag & FILE_FLAG_COMPRESSED) && !(file.mFlag & FILE_FLAG_IS_YAZ0))
            throw std::runtime_error("Native Yay0 archive members are not supported yet");
    mNativeBytes = std::move(bytes);
    mNativeArchive = std::move(parsed);
    mHeader = &mNativeArchive->header_;
    mInfoBlock = &mNativeArchive->info_;
    mDirs = mNativeArchive->dirs_.data();
    mFiles = mNativeArchive->files_.data();
    mStringTable = reinterpret_cast<char*>(const_cast<u8*>(mNativeArchive->strings_.data()));
    mFileDataStart = const_cast<u8*>(mNativeArchive->data_.data());
    mMountMode = MOUNT_MODE_MEM;
    _5C = 0;
    _6C = false;
    return true;
}

bool JKRMemArchive::mountFixed(void* input, u32 size, JKRMemBreakFlag flag) {
    if (!open(input, size, flag)) return false;
    fixedInit(-1);
    mLoaderType = RARC_MAGIC;
    mLoaderName = mStringTable + mDirs->mNameOffset;
    prependVolumeList(&mLoaderLink);
    mIsMounted = true;
    if (!sCurrentVolume) {
        sCurrentVolume = this;
        sCurrentDirID = 0;
    }
    return true;
}

void JKRMemArchive::setNativeResourceRoot(const char* root) {
    if (!root) throw std::runtime_error("Resource root is required");
    std::lock_guard lock(sMountMutex);
    const auto path = std::filesystem::canonical(root);
    if (!std::filesystem::is_directory(path)) throw std::runtime_error("Resource root must be a directory");
    sResourceRoot = path;
}

JKRMemArchive* JKRMemArchive::mountNativeFile(const char* name, JKRHeap* heap, EMountDirection direction) {
    if (!name || heap || direction != MOUNT_DIRECTION_1)
        throw std::runtime_error("Native file mounting requires a path and the host allocator");
    std::lock_guard lock(sMountMutex);
    if (sResourceRoot.empty()) throw std::runtime_error("Set the native resource root before mounting");
    std::string_view relative(name);
    if (relative.starts_with('/')) relative.remove_prefix(1);
    if (relative.empty() || relative.find_first_of("\\:") != std::string_view::npos)
        throw std::runtime_error("Invalid game resource path");
    const auto requested = std::filesystem::path(relative);
    if (requested.is_absolute() || requested.has_root_name())
        throw std::runtime_error("Invalid game resource path");
    for (const auto& part : requested)
        if (part == ".." || part == ".") throw std::runtime_error("Invalid game resource path");
    const auto path = std::filesystem::canonical(sResourceRoot / requested);
    const auto inside = path.lexically_relative(sResourceRoot);
    if (inside.empty() || inside.is_absolute() || *inside.begin() == "..")
        throw std::runtime_error("Game resource path leaves the resource root");
    const auto key = path.generic_string();
    // shortcut: keep mount/unmount on the loader thread until native asynchronous loading is ported.
    for (auto* link = sVolumeList.mHead; link; link = link->mNext) {
        auto* mounted = dynamic_cast<JKRMemArchive*>(static_cast<JKRFileLoader*>(link->mData));
        if (mounted && mounted->mNativePath == key) { ++mounted->_34; return mounted; }
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open game archive: " + key);
    const auto size = file.tellg();
    if (size <= 0 || size > maxArchiveSize) throw std::runtime_error("Invalid game archive size");
    std::vector<u8> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot read game archive: " + key);
    if (bytes.size() >= 4 && std::equal(bytes.begin(), bytes.begin() + 4, "Yaz0"))
        bytes = galaxy::decodeYaz0(bytes);
    auto archive = std::make_unique<JKRMemArchive>();
    archive->mountFixed(bytes.data(), u32(bytes.size()), JKR_MEM_BREAK_FLAG_0);
    archive->mNativePath = key;
    return archive.release();
}
