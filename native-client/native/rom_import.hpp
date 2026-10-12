#pragma once
#include <atomic>
#include <filesystem>
#include <stop_token>
namespace galaxy {
struct ImportedGame {std::filesystem::path rom,files;};
bool safeDiscName(const char*);
ImportedGame readImportedGame(const std::filesystem::path&);
void importRom(const std::filesystem::path& rom,const std::filesystem::path& destination,std::atomic<unsigned>& progress,std::stop_token cancel={});
// Returns false when the user closes the drop window.
bool showRomBuilder(const std::filesystem::path& destination);
}
