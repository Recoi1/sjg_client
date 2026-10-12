#pragma once
#include <filesystem>
namespace galaxy {
// Set before NANDInit; each port instance owns its private save directory.
void setNandRoot(const std::filesystem::path& root);
void shutdownNand();
void shutdownNandWorker();
// Refuse entry while a write handle is active; lobbies must never commit local saves.
bool setNandReadOnly(bool enabled);
bool nandReadOnly();
}
