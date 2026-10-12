#include "ast.hpp"
#include <algorithm>
#include <bit>
#include <fstream>
#include <stdexcept>

namespace galaxy {
namespace {
// shortcut: 128 MiB per resident AST, switch to file-backed blocks for larger verified streams.
constexpr std::size_t maxFileSize = 128 * 1024 * 1024;
std::uint16_t be16(std::span<const std::uint8_t> b, std::size_t p) {
    return (std::uint16_t(b[p]) << 8) | b[p + 1];
}
std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t p) {
    return (std::uint32_t(be16(b, p)) << 16) | be16(b, p + 2);
}
bool tag(std::span<const std::uint8_t> b, std::size_t p, const char* text) {
    return std::equal(b.begin() + p, b.begin() + p + 4, text);
}
}

std::vector<std::uint8_t> readAstFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open AST: " + path.string());
    const auto size = file.tellg();
    if (size < 64 || size > maxFileSize) throw std::runtime_error("Invalid AST file size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot read complete AST");
    return bytes;
}

Ast::Ast(std::span<const std::uint8_t> bytes) : bytes_(bytes) {
    if (bytes.size() < 64 || bytes.size() > maxFileSize || !tag(bytes, 0, "STRM") ||
        be32(bytes, 4) != bytes.size() - 64)
        throw std::runtime_error("Invalid AST header or length");
    if (be16(bytes, 8) != 1 || be16(bytes, 10) != 16)
        throw std::runtime_error("AST requires big-endian PCM16");
    channels_ = be16(bytes, 12);
    loops_ = be16(bytes, 14) != 0;
    rate_ = be32(bytes, 16);
    frames_ = be32(bytes, 20);
    loopStart_ = be32(bytes, 24);
    loopEnd_ = be32(bytes, 28);
    if (!channels_ || channels_ > 6 || rate_ < 8000 || rate_ > 192000 || !frames_ ||
        (loops_ && (loopStart_ >= loopEnd_ || loopEnd_ > frames_)))
        throw std::runtime_error("Invalid AST channels, rate, samples, or loop range");
    std::uint32_t capacity = 0;
    for (std::size_t p = 64; p < bytes.size();) {
        if (bytes.size() - p < 32 || !tag(bytes, p, "BLCK"))
            throw std::runtime_error("Invalid or truncated AST block header");
        const auto size = be32(bytes, p + 4);
        if (!size || size % 2 || size > maxFileSize / channels_)
            throw std::runtime_error("Invalid or truncated AST channel block");
        const auto remaining = bytes.size() - p - 32;
        const auto fullSize = std::size_t(size) * channels_;
        if (fullSize > remaining) {
            const auto missing = fullSize - remaining;
            // Verified Galaxy tails can omit 32/64 bytes; fill absent final-channel samples with silence.
            if (missing > 64 || missing >= size || missing % 32 || size % 32 || remaining % 32 ||
                capacity >= frames_ || frames_ - capacity > size / 2)
                throw std::runtime_error("Invalid or truncated AST final block");
            const auto storedFrames = (remaining - std::size_t(size) * (channels_ - 1)) / 2;
            if (frames_ - capacity > storedFrames)
                silentTailFrames_ = frames_ - capacity - storedFrames;
        }
        blocks_.push_back({p + 32, size, capacity});
        capacity += size / 2;
        p += 32 + std::size_t(size) * channels_;
    }
    if (capacity < frames_) throw std::runtime_error("AST has fewer samples than its header declares");
}

void Ast::seek(std::uint32_t frame) {
    if (frame > frames_) throw std::out_of_range("AST seek exceeds sample count");
    position_ = frame;
}

std::size_t Ast::read(std::span<std::int16_t> output, bool loop) {
    if (output.size() % channels_) throw std::invalid_argument("AST output must contain whole sample frames");
    const bool repeat = loop && loops_;
    std::size_t written = 0;
    const auto requested = output.size() / channels_;
    while (written < requested) {
        if (repeat && position_ >= loopEnd_) position_ = loopStart_;
        if (position_ == frames_) break;
        const auto next = std::upper_bound(blocks_.begin(), blocks_.end(), position_,
            [](std::uint32_t frame, const Block& b) { return frame < b.firstFrame; });
        const auto& block = *std::prev(next);
        const auto inBlock = position_ - block.firstFrame;
        const auto end = repeat ? loopEnd_ : frames_;
        const auto count = std::min({requested - written, std::size_t(block.bytes / 2 - inBlock),
                                    std::size_t(end - position_)});
        for (std::size_t frame = 0; frame < count; ++frame) {
            for (std::size_t channel = 0; channel < channels_; ++channel) {
                const auto p = block.offset + channel * block.bytes + (inBlock + frame) * 2;
                output[(written + frame) * channels_ + channel] = p + 1 < bytes_.size()
                    ? std::bit_cast<std::int16_t>(be16(bytes_, p)) : 0;
            }
        }
        position_ += count;
        written += count;
    }
    return written;
}
}
