#include "thp_file.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void word(std::vector<u8>& b, size_t p, u32 v) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = u8(v >> (24 - i * 8));
}
template<class F> void reject(F f) {
    bool rejected = false;
    try { f(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}
std::vector<u8> read(std::ifstream& file, size_t offset, size_t size) {
    std::vector<u8> b(size);
    file.seekg(offset);
    file.read(reinterpret_cast<char*>(b.data()), size);
    assert(size_t(file.gcount()) == size);
    return b;
}
void canaries(const std::vector<u8>& b) { assert(b.front() == 0xA5 && b.back() == 0xA5); }
void videoFixture() {
    std::vector<u8> b{0xFF, 0xD8, 0xFF, 0xDB, 0, 67, 0};
    b.insert(b.end(), 64, 1);
    const u8 frame[]{0xFF, 0xC0, 0, 17, 8, 0, 16, 0, 16, 3, 1, 0x22, 0, 2, 0x11, 0, 3, 0x11, 0};
    b.insert(b.end(), std::begin(frame), std::end(frame));
    for (u8 descriptor : {u8(0), u8(0x10)}) {
        b.insert(b.end(), {0xFF, 0xC4, 0, 20, descriptor, 1});
        b.insert(b.end(), 15, 0);
        b.push_back(0);
    }
    const u8 scan[]{0xFF, 0xDA, 0, 12, 3, 1, 0, 2, 0, 3, 0, 0, 63, 0};
    b.insert(b.end(), std::begin(scan), std::end(scan));
    const size_t entropy = b.size();
    b.insert(b.end(), {0, 0, 0xFF, 0xD9});
    std::vector<u8> y(258, 0xA5), u(66, 0xA5), v(66, 0xA5);
    const auto decode = [&](size_t size, size_t yCapacity = 256, u32 width = 16) {
        return THPVideoDecodeSized(b.data(), size, y.data() + 1, yCapacity, u.data() + 1, 64, v.data() + 1, 64, width, 16);
    };
    assert(decode(b.size()) == 0);
    for (const auto* plane : {&y, &u, &v}) {
        canaries(*plane);
        assert(std::all_of(plane->begin() + 1, plane->end() - 1, [](u8 p) { return p == 128; }));
    }
    for (size_t size = 0; size < entropy + 2; ++size) assert(decode(size) != 0);
    std::fill(y.begin(), y.end(), 0xA5);
    assert(decode(b.size(), 255) != 0 && decode(b.size(), 256, 32) != 0);
    assert(std::all_of(y.begin(), y.end(), [](u8 p) { return p == 0xA5; }));
    canaries(u); canaries(v);
}
void audioFixture() {
    std::vector<u8> b(96, 0);
    word(b, 0, 8); word(b, 4, 14);
    for (size_t i = 1; i < 8; ++i) { b[80 + i] = u8((i * 2 - 2) << 4 | (i * 2 - 1)); b[88 + i] = 0x11; }
    std::array<s16, 30> out{};
    out.front() = out.back() = 12345;
    assert(THPAudioDecodeSized(out.data() + 1, 28, b.data(), b.size(), 0) == 14);
    for (int i = 0; i < 14; ++i) { assert(out[1 + 2 * i] == 1); assert(out[2 + 2 * i] == (i < 8 ? i : i - 16)); }
    assert(out.front() == 12345 && out.back() == 12345);
    assert(THPAudioDecodeSized(out.data() + 1, 28, b.data(), b.size(), 1) == 14);
    for (int i = 0; i < 14; ++i) { assert(out[1 + i] == 1); assert(out[15 + i] == (i < 8 ? i : i - 16)); }
    word(b, 0, 0);
    assert(THPAudioDecodeSized(out.data() + 1, 28, b.data(), 88, 0) == 14);
    for (int i = 0; i < 14; ++i) assert(out[1 + 2 * i] == out[2 + 2 * i]);
    const auto before = out;
    for (size_t size = 0; size < 88; ++size) assert(THPAudioDecodeSized(out.data() + 1, 28, b.data(), size, 0) == 0);
    assert(THPAudioDecodeSized(out.data() + 1, 27, b.data(), 88, 0) == 0);
    b[80] = 0x80;
    assert(THPAudioDecodeSized(out.data() + 1, 28, b.data(), 88, 0) == 0 && out == before);
    word(b, 4, 0xFFFFFFFF);
    assert(THPAudioDecodeSized(out.data() + 1, 28, b.data(), 88, 0) == 0 && out == before);
}
void movie(const std::filesystem::path& p) {
    std::ifstream file(p, std::ios::binary);
    const auto headerBytes = read(file, 0, 48);
    const auto h = galaxy::readThpHeader(headerBytes, std::filesystem::file_size(p));
    const auto c = galaxy::readThpComponents(read(file, h.compInfoDataOffsets, 20));
    const auto video = galaxy::readThpVideo(read(file, h.compInfoDataOffsets + 20, 12));
    const auto audio = galaxy::readThpAudio(read(file, h.compInfoDataOffsets + 32, 16));
    auto invalidHeader = headerBytes;
    word(invalidHeader, 24, h.bufSize + 1);
    reject([&] { galaxy::readThpHeader(invalidHeader, std::filesystem::file_size(p)); });
    reject([&] { galaxy::readThpHeader(std::span(headerBytes).first(47), std::filesystem::file_size(p)); });
    const size_t ySize = size_t(video.xSize) * video.ySize;
    std::vector<u8> y(ySize + 2, 0xA5), u(ySize / 4 + 2, 0xA5), v(ySize / 4 + 2, 0xA5);
    std::vector<s16> samples(size_t(h.audioMaxSamples) * 2 + 2, 12345);
    size_t offset = h.movieDataOffsets, size = h.firstFrameSize;
    for (u32 index = 0; index < h.numFrames; ++index) {
        assert(size <= h.bufSize && size >= 16);
        const auto frameBytes = read(file, offset, size);
        const auto f = galaxy::readThpFrame(frameBytes, c, audio.sndNumTracks, 0);
        if (index == 0 || index == h.numFrames / 2 || index == h.numFrames - 1) {
            assert(THPVideoDecodeSized(f.video.data(), f.video.size(), y.data() + 1, ySize, u.data() + 1, ySize / 4,
                                      v.data() + 1, ySize / 4, video.xSize, video.ySize) == 0);
            const auto count = THPAudioDecodeSized(samples.data() + 1, samples.size() - 2, f.audio.data(), f.audio.size(), 0);
            assert(count != 0 && count <= h.audioMaxSamples);
            assert(THPVideoDecodeSized(f.video.data(), f.video.size() / 2, y.data() + 1, ySize, u.data() + 1, ySize / 4,
                                      v.data() + 1, ySize / 4, video.xSize, video.ySize) != 0);
            assert(THPAudioDecodeSized(samples.data() + 1, samples.size() - 2, f.audio.data(), 79, 0) == 0);
            canaries(y); canaries(u); canaries(v);
            assert(samples.front() == 12345 && samples.back() == 12345);
        }
        if (index == h.numFrames - 1) assert(offset == h.finalFrameDataOffsets);
        offset += size;
        size = f.nextSize;
    }
    assert(offset == size_t(h.movieDataOffsets) + h.movieDataSize);
    std::cout << p.filename().string() << ": frame chain, video, and audio passed\n";
}
}
int main(int argc, char** argv) {
    assert(argc == 2 && THPInit());
    videoFixture(); audioFixture();
    unsigned count = 0;
    for (const auto& p : std::filesystem::directory_iterator(argv[1])) if (p.path().extension() == ".thp") { movie(p.path()); ++count; }
    assert(count == 9);
    return 0;
}
