#pragma once

#include "JSystem/JKernel/JKRArchive.hpp"
#include <span>
#include <string_view>
#include <vector>
#include <map>

class JKRMemArchive;

namespace galaxy {
// The caller owns the archive bytes; decoded member views live with this archive.
class Rarc {
public:
    explicit Rarc(std::span<const u8> bytes);
    std::span<const u8> resource(std::string_view path) const;

private:
    friend class ::JKRMemArchive;
    std::string_view name(u32 offset) const;
    std::span<const u8> bytes_;
    std::span<const u8> strings_;
    std::span<const u8> data_;
    std::vector<JKRArchive::SDIDirEntry> dirs_;
    std::vector<JKRArchive::SDIFileEntry> files_;
    mutable std::map<u32, std::vector<u8>> decoded_;
    JKRArchive::RarcHeader header_{};
    JKRArchive::RarcInfoBlock info_{};
};
}
