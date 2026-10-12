#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
namespace galaxy {
// Call before starting game workers; these buffers stay fixed for the game lifetime.
void initializeRomResources(std::span<const uint8_t> dol);
using ErrorArchive=uint8_t[0x9ac0];
using GalaxyIdTable=uint8_t[0xd20];
using StoryEventTable=uint8_t[0x1e0];
const ErrorArchive& errorArchive();
const GalaxyIdTable& galaxyIdTable();
const StoryEventTable& storyEventTable();
}
// Preserve the original array expressions, including sizeof and address-of.
#define cErrorArchive (galaxy::errorArchive())
#define GalaxyIDBCSV (galaxy::galaxyIdTable())
#define StoryEventBCSV (galaxy::storyEventTable())
