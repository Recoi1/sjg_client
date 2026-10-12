#include "ast.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using Bytes = std::vector<std::uint8_t>;
static void put16(Bytes& b, std::size_t p, std::uint16_t n) {
    b[p] = n >> 8; b[p + 1] = n;
}
static void put32(Bytes& b, std::size_t p, std::uint32_t n) {
    put16(b, p, n >> 16); put16(b, p + 2, n);
}
static constexpr std::array<std::int16_t, 12> samples{
    -32768, 32767, -1, 1, -300, 200, 123, -456, 32000, -32000, 17, -19};
static Bytes fixture() {
    Bytes b(64);
    std::copy_n("STRM", 4, b.begin());
    put16(b, 8, 1); put16(b, 10, 16); put16(b, 12, 2); put16(b, 14, 0xffff);
    put32(b, 16, 32000); put32(b, 20, 6); put32(b, 24, 2); put32(b, 28, 5);
    for (int start : {0, 3}) {
        const auto p = b.size();
        const auto channelBytes = start == 0 ? 6 : 8;
        b.resize(p + 32 + 2 * channelBytes);
        std::copy_n("BLCK", 4, b.begin() + p);
        put32(b, p + 4, channelBytes);
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < 3; ++frame)
                put16(b, p + 32 + channel * channelBytes + frame * 2, samples[(start + frame) * 2 + channel]);
    }
    put32(b, 4, b.size() - 64);
    return b;
}
static void rejects(const Bytes& b) {
    bool failed = false;
    try { galaxy::Ast ast(b); } catch (const std::runtime_error&) { failed = true; }
    assert(failed);
}
static void synthetic() {
    const auto bytes = fixture();
    galaxy::Ast ast(bytes);
    assert(ast.channels() == 2 && ast.sampleRate() == 32000 && ast.frames() == 6);
    std::array<std::int16_t, 20> out{};
    assert(ast.read(out) == 6);
    assert(std::equal(samples.begin(), samples.end(), out.begin()));
    assert(ast.read(out) == 0 && ast.position() == 6);
    ast.seek(0);
    assert(ast.read(out, true) == 10);
    const std::array<unsigned, 10> expected{0, 1, 2, 3, 4, 2, 3, 4, 2, 3};
    for (unsigned i = 0; i < expected.size(); ++i)
        for (unsigned channel = 0; channel < 2; ++channel)
            assert(out[i * 2 + channel] == samples[expected[i] * 2 + channel]);
    ast.seek(4);
    assert(ast.read(out) == 2 && out[0] == 32000 && out[2] == 17);
    ast.seek(6);
    assert(ast.read(std::span(out).first(2), true) == 1 && out[0] == -300);
    const auto position = ast.position();
    assert(ast.read({}) == 0 && ast.position() == position);
    try { ast.seek(7); assert(false); } catch (const std::out_of_range&) {}
    try { ast.read(std::span(out).first(1)); assert(false); } catch (const std::invalid_argument&) {}
    auto disabled = bytes;
    put16(disabled, 14, 0);
    galaxy::Ast once(disabled);
    assert(once.read(out, true) == 6 && once.read(out, true) == 0);
    auto oneFrameLoop = bytes;
    put32(oneFrameLoop, 24, 4);
    galaxy::Ast repeatOne(oneFrameLoop);
    repeatOne.seek(4);
    assert(repeatOne.read(out, true) == 10);
    for (unsigned frame = 0; frame < 10; ++frame)
        assert(out[frame * 2] == 32000 && out[frame * 2 + 1] == -32000);
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        Bytes truncated(bytes.begin(), bytes.begin() + size);
        rejects(truncated);
    }
    for (const auto [p, value] : std::array<std::pair<std::size_t, std::uint32_t>, 9>{
        {{4, 0}, {16, 0}, {20, 99}, {24, 5}, {28, 7}, {64, 0}, {68, 0}, {68, 7}, {68, 0xffffffff}}}) {
        auto bad = bytes; put32(bad, p, value); rejects(bad);
    }
    for (const auto [p, value] : std::array<std::pair<std::size_t, std::uint16_t>, 4>{
        {{8, 0}, {10, 8}, {12, 0}, {12, 7}}}) {
        auto bad = bytes; put16(bad, p, value); rejects(bad);
    }
    auto incomplete = bytes;
    incomplete.pop_back(); put32(incomplete, 4, incomplete.size() - 64); rejects(incomplete);
    auto shortTail = bytes;
    shortTail.resize(64 + 32 + 96, 0);
    std::fill(shortTail.begin() + 64, shortTail.end(), 0);
    std::copy_n("BLCK", 4, shortTail.begin() + 64);
    put32(shortTail, 4, shortTail.size() - 64);
    put32(shortTail, 20, 32); put16(shortTail, 14, 0); put32(shortTail, 68, 64);
    for (std::size_t p = 96; p < shortTail.size(); p += 2) put16(shortTail, p, 123);
    galaxy::Ast tail(shortTail);
    assert(tail.silentTailFrames() == 16);
    std::array<std::int16_t, 64> tailPcm{};
    assert(tail.read(tailPcm) == 32);
    for (unsigned frame = 0; frame < 32; ++frame) {
        assert(tailPcm[frame * 2] == 123);
        assert(tailPcm[frame * 2 + 1] == (frame < 16 ? 123 : 0));
    }
    shortTail.resize(shortTail.size() - 2); put32(shortTail, 4, shortTail.size() - 64); rejects(shortTail);
}

static void checkFile(const std::filesystem::path& path, const Bytes* reference = nullptr) {
    const auto bytes = galaxy::readAstFile(path);
    galaxy::Ast ast(bytes);
    if (reference) assert(reference->size() == std::size_t(ast.frames()) * ast.channels() * 2);
    std::array<std::int16_t, 2046> out;
    const auto buffer = std::span(out).first(out.size() / ast.channels() * ast.channels());
    std::size_t total = 0;
    for (;;) {
        const auto frames = ast.read(buffer);
        if (!frames) break;
        if (reference) {
            for (std::size_t i = 0; i < frames * ast.channels(); ++i) {
                const auto p = (total * ast.channels() + i) * 2;
                std::uint16_t expected = (*reference)[p] | (std::uint16_t((*reference)[p + 1]) << 8);
                if (total + i / ast.channels() >= ast.frames() - ast.silentTailFrames() &&
                    i % ast.channels() == ast.channels() - 1) {
                    // vgmstream r2117 emits -1 at EOF; the native decoder explicitly emits silence.
                    assert(expected == 0xffff);
                    expected = 0;
                }
                if (static_cast<std::uint16_t>(out[i]) != expected)
                    throw std::runtime_error("PCM mismatch at frame " + std::to_string(total + i / ast.channels()) +
                        ", channel " + std::to_string(i % ast.channels()) + ": native " + std::to_string(out[i]) +
                        ", reference bits " + std::to_string(expected));
            }
        }
        total += frames;
    }
    assert(total == ast.frames());
    if (reference && ast.loops()) {
        ast.seek(ast.loopEnd() - 1);
        assert(ast.read(buffer.first(ast.channels() * 3), true) == 3);
        const std::array<std::uint32_t, 3> positions{ast.loopEnd() - 1, ast.loopStart(), ast.loopStart() + 1};
        for (std::size_t frame = 0; frame < 3; ++frame)
            for (std::size_t channel = 0; channel < ast.channels(); ++channel) {
                const auto p = (std::size_t(positions[frame]) * ast.channels() + channel) * 2;
                std::uint16_t expected = (*reference)[p] | (std::uint16_t((*reference)[p + 1]) << 8);
                if (positions[frame] >= ast.frames() - ast.silentTailFrames() && channel == ast.channels() - 1) {
                    assert(expected == 0xffff); expected = 0;
                }
                assert(static_cast<std::uint16_t>(out[frame * ast.channels() + channel]) == expected);
            }
    }
    std::cout << "PASS " << path.filename().string() << ": " << total << " frames, " << ast.channels()
              << " channels, " << ast.silentTailFrames() << " final-channel silent tail frames\n";
}

int main(int argc, char** argv) {
    try {
        synthetic();
        if (argc == 2 && std::filesystem::is_directory(argv[1])) {
            unsigned count = 0;
            for (const auto& file : std::filesystem::directory_iterator(argv[1])) {
                if (file.path().extension() == ".ast") { checkFile(file.path()); ++count; }
            }
            assert(count > 0);
            std::cout << count << " native AST streams passed\n";
        } else if (argc == 3) {
            std::ifstream file(argv[2], std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open reference PCM");
            const Bytes reference{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            checkFile(argv[1], &reference);
        } else if (argc != 1) throw std::runtime_error("Usage: audio_checks [AST-directory | FILE.ast REFERENCE.pcm]");
        std::cout << "Native audio checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
