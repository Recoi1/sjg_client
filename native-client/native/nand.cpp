#include "nand.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <utility>
#include <revolution/nand.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
constexpr char home[] = "/title/00010000/524d4745/data";
fs::path root = fs::path(__FILE__).parent_path().parent_path() / "userdata/nand";
bool initialized = false;
bool readOnly = false;
std::mutex mutex;
unsigned serial = 0;
struct File {
    HANDLE handle;
    fs::path target, temporary;
    u8 access;
    s32 failure = NAND_RESULT_OK;
};
std::map<NANDFileInfo*, File> files;
s32 error(DWORD value = GetLastError()) {
    switch (value) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return NAND_RESULT_NOEXISTS;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return NAND_RESULT_EXISTS;
    case ERROR_ACCESS_DENIED: return NAND_RESULT_ACCESS;
    case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: return NAND_RESULT_BUSY;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return NAND_RESULT_MAXBLOCKS;
    case ERROR_DIR_NOT_EMPTY: return NAND_RESULT_NOTEMPTY;
    default: return NAND_RESULT_UNKNOWN;
    }
}
std::wstring windowsPath(const fs::path& path) { return L"\\\\?\\" + path.wstring(); }
fs::path path(const char* value) {
    if (!value) throw std::invalid_argument("null NAND path");
    size_t length = 0;
    while (length < NAND_MAX_PATH && value[length]) ++length;
    if (!length || length == NAND_MAX_PATH) throw std::invalid_argument("invalid NAND path length");
    std::string virtualPath(value, length);
    if (virtualPath.front() != '/') virtualPath = std::string(home) + '/' + virtualPath;
    fs::path result = root;
    for (size_t begin = 1; begin < virtualPath.size();) {
        const size_t end = virtualPath.find('/', begin);
        std::string component = virtualPath.substr(begin, end == std::string::npos ? end : end - begin);
        if (component.empty() || component == "." || component == ".." || component.back() == '.' || component.back() == ' ')
            throw std::invalid_argument("invalid NAND path component");
        for (char byte : component) if (byte < 32 || byte > 126 || std::strchr("\\:*?\"<>|", byte))
            throw std::invalid_argument("invalid NAND path character");
        std::string stem = component.substr(0, component.find('.'));
        for (char& byte : stem) if (byte >= 'a' && byte <= 'z') byte -= 'a' - 'A';
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9'))
            throw std::invalid_argument("reserved NAND path component");
        result /= component;
        const DWORD attributes = GetFileAttributesW(windowsPath(result).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::invalid_argument("NAND reparse points are not supported");
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}
template<class F> s32 api(F function) {
    // File handles and paths must survive scene heap resets and adjusted worker heaps.
    struct HostHeap { JKRHeap* saved=std::exchange(JKRHeap::sCurrentHeap,nullptr); ~HostHeap() { JKRHeap::sCurrentHeap=saved; } } hostHeap;
    try {
        std::lock_guard lock(mutex);
        if (!initialized) return NAND_RESULT_FATAL_ERROR;
        return function();
    } catch (const std::invalid_argument&) { return NAND_RESULT_INVALID; }
    catch (const std::bad_alloc&) { return NAND_RESULT_ALLOC_FAILED; }
    catch (...) { return NAND_RESULT_UNKNOWN; }
}
bool busy(const fs::path& target) {
    for (const auto& [info, file] : files) if (file.target == target) return true;
    return false;
}
File* file(NANDFileInfo* info) {
    const auto found = files.find(info);
    return found == files.end() ? nullptr : &found->second;
}
}
namespace galaxy {
bool setNandReadOnly(bool enabled) {
    std::lock_guard lock(mutex);
    if(enabled) for(const auto& [info,current]:files) if(current.access & NAND_ACCESS_WRITE) return false;
    readOnly=enabled;return true;
}
bool nandReadOnly() {std::lock_guard lock(mutex);return readOnly;}
void shutdownNandWorker();
void resetNandWorker();
void shutdownNand() {
    shutdownNandWorker();
    std::lock_guard lock(mutex);
    bool failed = false;
    for (auto& [info, current] : files) {
        if (!CloseHandle(current.handle)) failed = true;
        // An unfinished save never replaces the previous committed file.
        if (!current.temporary.empty() && !DeleteFileW(windowsPath(current.temporary).c_str())) failed = true;
    }
    files.clear(); initialized = false; readOnly = false;
    if (failed) throw std::runtime_error("could not clean up native save handles or temporary files");
}
void setNandRoot(const fs::path& value) {
    std::lock_guard lock(mutex);
    if (initialized || !files.empty()) throw std::logic_error("NAND root is already active");
    root = fs::absolute(value);
}
}
extern "C" {
s32 NANDInit() {
    try {
        std::lock_guard lock(mutex);
        if (initialized) return NAND_RESULT_OK;
        fs::create_directories(root);
        root = fs::canonical(root);
        fs::path directory = root;
        for (const char* component : {"title", "00010000", "524d4745", "data"}) {
            directory /= component;
            const auto attributes = GetFileAttributesW(windowsPath(directory).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return NAND_RESULT_ACCESS;
            if (!CreateDirectoryW(windowsPath(directory).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return error();
        }
        for (const char* name : {"/shared2", "/tmp"}) {
            const auto systemDirectory = path(name);
            const auto attributes = GetFileAttributesW(windowsPath(systemDirectory).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return NAND_RESULT_INVALID;
            if (!CreateDirectoryW(windowsPath(systemDirectory).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return error();
        }
        galaxy::resetNandWorker();
        initialized = true;
        return NAND_RESULT_OK;
    } catch (...) { return NAND_RESULT_FATAL_ERROR; }
}
BOOL nandIsInitialized() { std::lock_guard lock(mutex); return initialized; }
s32 NANDGetHomeDir(char* output) {
    return api([&] { if (!output) return NAND_RESULT_INVALID; std::memcpy(output, home, sizeof(home)); return NAND_RESULT_OK; });
}
// shortcut: ownership/attributes follow Windows permissions; add Wii metadata when console-save import is supported.
s32 NANDCreate(const char* name, u8, u8) {
    return api([&] {
        if(readOnly) return NAND_RESULT_ACCESS;
        const auto target = path(name);
        HANDLE handle = CreateFileW(windowsPath(target).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return error();
        return CloseHandle(handle) ? NAND_RESULT_OK : error();
    });
}
s32 NANDPrivateCreateDir(const char* name, u8, u8) {
    return api([&] {
        if(readOnly) return NAND_RESULT_ACCESS;
        return CreateDirectoryW(windowsPath(path(name)).c_str(), nullptr) ? NAND_RESULT_OK : error();
    });
}
s32 NANDOpen(const char* name, NANDFileInfo* info, u8 access) {
    return api([&] {
        if (!info || access < NAND_ACCESS_READ || access > NAND_ACCESS_RW) return NAND_RESULT_INVALID;
        if(readOnly && (access & NAND_ACCESS_WRITE)) return NAND_RESULT_ACCESS;
        if (files.contains(info)) return NAND_RESULT_OPENFD;
        const auto target = path(name);
        if (busy(target)) return NAND_RESULT_BUSY;
        fs::path temporary;
        if (access & NAND_ACCESS_WRITE) {
            // Preserve the old file until all writes and the final flush succeed.
            temporary = target.parent_path() / (L".native-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial) + L".tmp");
            if (!CopyFileW(windowsPath(target).c_str(), windowsPath(temporary).c_str(), TRUE)) return error();
        }
        const auto& opened = temporary.empty() ? target : temporary;
        HANDLE handle = CreateFileW(windowsPath(opened).c_str(), GENERIC_READ | ((access & NAND_ACCESS_WRITE) ? GENERIC_WRITE : 0),
                                   FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const s32 result = error(); if (!temporary.empty()) DeleteFileW(windowsPath(temporary).c_str()); return result;
        }
        try { files.emplace(info, File{handle, target, temporary, access}); }
        catch (...) { CloseHandle(handle); if (!temporary.empty()) DeleteFileW(windowsPath(temporary).c_str()); throw; }
        *info = {}; info->accType = access;
        return NAND_RESULT_OK;
    });
}
s32 NANDRead(NANDFileInfo* info, void* output, u32 length) {
    return api([&] {
        auto* current = file(info);
        if (!current || (length && !output) || length > INT32_MAX) return NAND_RESULT_INVALID;
        if (!(current->access & NAND_ACCESS_READ)) return NAND_RESULT_ACCESS;
        DWORD count = 0;
        if (!ReadFile(current->handle, output, length, &count, nullptr)) return error();
        return static_cast<s32>(count);
    });
}
s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 mode) {
    return api([&] {
        auto* current = file(info);
        if (!current || mode < 0 || mode > 2) return NAND_RESULT_INVALID;
        LARGE_INTEGER position, result;
        position.QuadPart = offset;
        if (!SetFilePointerEx(current->handle, position, &result, mode)) return error();
        if (result.QuadPart > INT32_MAX) return NAND_RESULT_INVALID;
        return static_cast<s32>(result.QuadPart);
    });
}
s32 NANDWrite(NANDFileInfo* info, const void* input, u32 length) {
    return api([&] {
        if(readOnly) return NAND_RESULT_ACCESS;
        auto* current = file(info);
        if (!current) return NAND_RESULT_INVALID;
        if (!(current->access & NAND_ACCESS_WRITE)) return NAND_RESULT_ACCESS;
        if ((length && !input) || length > INT32_MAX) return current->failure = NAND_RESULT_INVALID;
        if (current->failure) return current->failure;
        DWORD count = 0;
        if (!WriteFile(current->handle, input, length, &count, nullptr)) return current->failure = error();
        if (count != length) return current->failure = NAND_RESULT_MAXBLOCKS;
        return static_cast<s32>(count);
    });
}
s32 NANDGetLength(NANDFileInfo* info, u32* output) {
    return api([&] {
        auto* current = file(info); if (!current || !output) return NAND_RESULT_INVALID;
        LARGE_INTEGER size;
        if (!GetFileSizeEx(current->handle, &size)) return error();
        if (size.QuadPart < 0 || size.QuadPart > UINT32_MAX) return NAND_RESULT_INVALID;
        *output = static_cast<u32>(size.QuadPart); return NAND_RESULT_OK;
    });
}
s32 NANDClose(NANDFileInfo* info) {
    return api([&] {
        auto* current = file(info); if (!current) return NAND_RESULT_INVALID;
        s32 result = current->failure;
        if (!current->temporary.empty() && !result && !FlushFileBuffers(current->handle)) result = error();
        if (!CloseHandle(current->handle) && !result) result = error();
        if (!current->temporary.empty()) {
            if (!result && !MoveFileExW(windowsPath(current->temporary).c_str(), windowsPath(current->target).c_str(),
                                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) result = error();
            if (result) DeleteFileW(windowsPath(current->temporary).c_str());
        }
        files.erase(info); *info = {}; return result;
    });
}
s32 NANDDelete(const char* name) {
    return api([&] {
        if(readOnly) return NAND_RESULT_ACCESS;
        const auto target = path(name); if (target == root) return NAND_RESULT_ACCESS;
        if (busy(target)) return NAND_RESULT_OPENFD;
        const DWORD attributes = GetFileAttributesW(windowsPath(target).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return error();
        const BOOL removed = attributes & FILE_ATTRIBUTE_DIRECTORY ? RemoveDirectoryW(windowsPath(target).c_str()) : DeleteFileW(windowsPath(target).c_str());
        return removed ? NAND_RESULT_OK : error();
    });
}
s32 NANDMove(const char* name, const char* directory) {
    return api([&] {
        if(readOnly) return NAND_RESULT_ACCESS;
        const auto source = path(name), destination = path(directory) / source.filename();
        // Validate the complete destination, including an existing final reparse point.
        const std::string virtualDestination = (destination.lexically_relative(root)).generic_string();
        path(("/" + virtualDestination).c_str());
        if (busy(source) || busy(destination)) return NAND_RESULT_OPENFD;
        const auto attributes = GetFileAttributesW(windowsPath(path(directory)).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return error();
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return NAND_RESULT_INVALID;
        return MoveFileExW(windowsPath(source).c_str(), windowsPath(destination).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? NAND_RESULT_OK : error();
    });
}
s32 NANDCheck(u32 blocks, u32, u32* answer) {
    return api([&] {
        if (!answer) return NAND_RESULT_INVALID;
        ULARGE_INTEGER available;
        if (!GetDiskFreeSpaceExW(windowsPath(root).c_str(), &available, nullptr, nullptr)) return error();
        // Windows has no separate Wii inode quota; available disk space is the real native limit.
        *answer = u64(blocks) * 16384 > available.QuadPart ? NAND_CHECK_HOME_INSSPACE | NAND_CHECK_SYS_INSSPACE : 0;
        return NAND_RESULT_OK;
    });
}
void NANDInitBanner(NANDBanner* banner, u32 flags, const u16* title, const u16* comment) {
    if (!banner || !title || !comment) throw std::invalid_argument("invalid NAND banner");
    *banner = {}; banner->signature = NAND_BANNER_SIGNATURE; banner->flag = flags;
    const u16* text[] = {title, comment};
    for (unsigned line = 0; line < 2; ++line) {
        if (!text[line][0]) banner->comment[line][0] = ' ';
        else for (unsigned i = 0; i < NAND_BANNER_COMMENT_SIZE && text[line][i]; ++i) banner->comment[line][i] = text[line][i];
    }
}
}
