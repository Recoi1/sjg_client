#include "rarc.hpp"
#include "decomp.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace {
void require(bool valid) {
    if (!valid) throw std::runtime_error("Invalid or truncated RARC archive");
}

void range(u64 offset, u64 size, u64 limit) {
    require(offset <= limit && size <= limit - offset);
}

u16 be16(std::span<const u8> bytes, size_t offset) {
    range(offset, 2, bytes.size());
    return (u16(bytes[offset]) << 8) | bytes[offset + 1];
}

u32 be32(std::span<const u8> bytes, size_t offset) {
    range(offset, 4, bytes.size());
    return (u32(bytes[offset]) << 24) | (u32(bytes[offset + 1]) << 16)
         | (u32(bytes[offset + 2]) << 8) | bytes[offset + 3];
}
}

namespace galaxy {
Rarc::Rarc(std::span<const u8> bytes) : bytes_(bytes) {
    range(0, 32, bytes.size());
    header_ = {be32(bytes, 0), be32(bytes, 4), be32(bytes, 8), be32(bytes, 12),
               be32(bytes, 16), be32(bytes, 20), be32(bytes, 24), be32(bytes, 28)};
    require(be32(bytes, 0) == RARC_MAGIC);
    const u64 fileSize = be32(bytes, 4);
    const u64 base = be32(bytes, 8);
    const u64 dataOffset = base + be32(bytes, 12);
    const u64 dataSize = be32(bytes, 16);
    require(base >= 32);
    range(0, fileSize, bytes.size());
    range(base, 32, dataOffset);
    range(dataOffset, dataSize, fileSize);
    require(u64(be32(bytes, 20)) + be32(bytes, 24) <= dataSize);
    bytes_ = bytes.first(fileSize);
    info_ = {be32(bytes_, base), be32(bytes_, base + 4), be32(bytes_, base + 8),
             be32(bytes_, base + 12), be32(bytes_, base + 16), be32(bytes_, base + 20),
             be16(bytes_, base + 24), be16(bytes_, base + 26), be32(bytes_, base + 28)};
    const u32 dirCount = be32(bytes_, base);
    const u32 fileCount = be32(bytes_, base + 8);
    require(dirCount != 0);
    auto table = [&](u32 offset, u64 size) {
        require(offset >= 32);
        const u64 start = base + offset;
        range(start, size, dataOffset);
        return bytes_.subspan(start, size);
    };
    const auto dirs = table(be32(bytes_, base + 4), u64(dirCount) * 16);
    // Disk entries are 20 bytes; their final word is not a 64-bit host pointer.
    const auto files = table(be32(bytes_, base + 12), u64(fileCount) * 20);
    strings_ = table(be32(bytes_, base + 20), be32(bytes_, base + 16));
    auto disjoint = [](std::span<const u8> a, std::span<const u8> b) {
        return a.empty() || b.empty() || a.data() + a.size() <= b.data() || b.data() + b.size() <= a.data();
    };
    require(disjoint(dirs, files) && disjoint(dirs, strings_) && disjoint(files, strings_));
    data_ = bytes_.subspan(dataOffset, dataSize);
    dirs_.reserve(dirCount);
    for (u32 i = 0; i < dirCount; ++i) {
        const size_t off = size_t(i) * 16;
        JKRArchive::SDIDirEntry dir{be32(dirs, off), be32(dirs, off + 4),
            be16(dirs, off + 8), be16(dirs, off + 10), be32(dirs, off + 12)};
        name(dir.mNameOffset);
        range(dir.mFirstFileIndex, dir.mNrFiles, fileCount);
        dirs_.push_back(dir);
    }
    files_.reserve(fileCount);
    for (u32 i = 0; i < fileCount; ++i) {
        const size_t off = size_t(i) * 20;
        const u32 packed = be32(files, off + 4);
        JKRArchive::SDIFileEntry file{};
        file.mFileID = be16(files, off);
        file.mHash = be16(files, off + 2);
        file.mFlag = packed >> 24;
        file.mNameOffset = packed & 0xffffff;
        file.mDataOffset = be32(files, off + 8);
        file.mDataSize = be32(files, off + 12);
        const auto fileName = name(file.mNameOffset);
        const u32 kind = file.mFlag & (JKRArchive::FILE_FLAG_FILE | JKRArchive::FILE_FLAG_FOLDER);
        require(kind == JKRArchive::FILE_FLAG_FILE || kind == JKRArchive::FILE_FLAG_FOLDER);
        if (kind == JKRArchive::FILE_FLAG_FILE) {
            range(file.mDataOffset, file.mDataSize, data_.size());
        } else {
            require(file.mDirIndex < dirCount || (file.mDirIndex == 0xffffffff && fileName == ".."));
        }
        files_.push_back(file);
    }
}

std::string_view Rarc::name(u32 offset) const {
    require(offset < strings_.size());
    const auto tail = strings_.subspan(offset);
    const auto end = std::find(tail.begin(), tail.end(), u8(0));
    require(end != tail.end());
    return {reinterpret_cast<const char*>(tail.data()), size_t(end - tail.begin())};
}

std::span<const u8> Rarc::resource(std::string_view path) const {
    std::string folded(path);
    for (char& ch : folded) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    path = folded;
    if (path.starts_with('/')) path.remove_prefix(1);
    u32 dirIndex = 0;
    while (!path.empty()) {
        const size_t slash = path.find('/');
        const auto part = path.substr(0, slash);
        const auto& dir = dirs_[dirIndex];
        const JKRArchive::SDIFileEntry* match = nullptr;
        for (u32 i = 0; i < dir.mNrFiles; ++i) {
            const auto& file = files_[dir.mFirstFileIndex + i];
            if (name(file.mNameOffset) == part) { match = &file; break; }
        }
        if (!match) break;
        if (slash == std::string_view::npos) {
            if (!(match->mFlag & JKRArchive::FILE_FLAG_FILE)) break;
            if (match->mFlag & JKRArchive::FILE_FLAG_COMPRESSED) {
                const auto cached = decoded_.find(match->mDataOffset);
                if (cached != decoded_.end()) return cached->second;
                const auto packed = data_.subspan(match->mDataOffset, match->mDataSize);
                if (packed.size() < 16) throw std::invalid_argument("short compressed RARC member");
                const u32 length = u32(packed[4]) << 24 | u32(packed[5]) << 16 | u32(packed[6]) << 8 | packed[7];
                if (length > 64 * 1024 * 1024) throw std::invalid_argument("oversized compressed RARC member");
                std::vector<u8> decoded(length);
                decompress(packed, decoded);
                return decoded_.emplace(match->mDataOffset, std::move(decoded)).first->second;
            }
            return data_.subspan(match->mDataOffset, match->mDataSize);
        }
        if (!(match->mFlag & JKRArchive::FILE_FLAG_FOLDER) || match->mDirIndex >= dirs_.size()) break;
        dirIndex = match->mDirIndex;
        path.remove_prefix(slash + 1);
    }
    throw std::runtime_error("RARC resource not found: " + folded);
}
}
