#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
class GameSystem;
namespace galaxy {
// shortcut: scene checkpoints reset transient actors; add actor serialization if a repro needs exact NPC state.
struct TestCheckpoint {
    std::array<char, 4> magic{'G','C','P','1'};
    std::array<char, 32> stage{};
    int32_t scenario = 1;
    int32_t startId = 0, startZone = 0;
    uint32_t character = 0, progressSize = 0;
    std::array<float, 3> position{}, velocity{}, gravity{}, front{};
    std::array<uint8_t, 4096> progress{};
    uint32_t checksum = 0;
};
bool validTestCheckpoint(const TestCheckpoint& checkpoint);
bool readTestCheckpoint(const std::filesystem::path& path, TestCheckpoint* checkpoint);
bool writeTestCheckpoint(const std::filesystem::path& path, const TestCheckpoint& checkpoint);
bool captureTestCheckpoint(GameSystem* game, TestCheckpoint* checkpoint);
bool startTestCheckpoint(GameSystem* game, const TestCheckpoint& checkpoint);
bool restoreTestCheckpoint(GameSystem* game, const TestCheckpoint& checkpoint);
}
