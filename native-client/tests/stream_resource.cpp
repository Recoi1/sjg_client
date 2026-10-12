#include "stream_resource.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
    assert(argc == 2); unsigned files = 0, blocks = 0;
    for (const auto& item : std::filesystem::directory_iterator(argv[1])) {
        if (item.path().extension() != ".ast") continue;
        std::ifstream file(item.path(), std::ios::binary); assert(file);
        JASAramStream::Header header; file.read(reinterpret_cast<char*>(&header), sizeof(header)); assert(file.gcount() == 64);
        const auto original = header; const u32 length = std::filesystem::file_size(item.path());
        const auto blockSize = galaxy::streamWord(reinterpret_cast<const u8*>(&header) + 32);
        galaxy::importStreamHeader(header, length, blockSize, 6);
        assert(header.channels <= 6 && (header._10 == 32000 || header._10 == 48000) && blockSize == 10080);
        for (u32 position = 64; position < length;) {
            JASAramStream::BlockHeader block; file.read(reinterpret_cast<char*>(&block), sizeof(block)); assert(file.gcount() == 32);
            const auto size = galaxy::streamWord(reinterpret_cast<const u8*>(&block) + 4);
            const auto extent = 32 + size * header.channels, stored = std::min(extent, length - position);
            assert(galaxy::importStreamBlock(block, stored, header.channels, blockSize, position + stored == length) == extent);
            assert(block._4 == size); file.seekg(stored - 32, std::ios::cur); position += stored; ++blocks;
        }
        auto malformed = original; reinterpret_cast<u8*>(&malformed)[12] = 2; bool rejected = false;
        try { galaxy::importStreamHeader(malformed, length, blockSize, 6); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected); ++files;
    }
    assert(files == 74 && blocks > 1000); std::cout << files << " native JAS stream headers, " << blocks << " bounded blocks passed\n";
}
