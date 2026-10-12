#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace galaxy {
std::vector<std::uint8_t> readAstFile(const std::filesystem::path& path);

// The caller keeps the original AST bytes alive; PCM is decoded a block at a time.
class Ast {
public:
    explicit Ast(std::span<const std::uint8_t> bytes);
    std::uint32_t sampleRate() const { return rate_; }
    std::uint16_t channels() const { return channels_; }
    std::uint32_t frames() const { return frames_; }
    std::uint32_t position() const { return position_; }
    std::uint32_t loopStart() const { return loopStart_; }
    std::uint32_t loopEnd() const { return loopEnd_; }
    bool loops() const { return loops_; }
    std::uint32_t silentTailFrames() const { return silentTailFrames_; }
    void seek(std::uint32_t frame);
    std::size_t read(std::span<std::int16_t> interleaved, bool loop = false);

private:
    struct Block { std::size_t offset; std::uint32_t bytes, firstFrame; };
    std::span<const std::uint8_t> bytes_;
    std::vector<Block> blocks_;
    std::uint32_t rate_, frames_, loopStart_, loopEnd_, position_ = 0;
    std::uint32_t silentTailFrames_ = 0;
    std::uint16_t channels_;
    bool loops_;
};
}
