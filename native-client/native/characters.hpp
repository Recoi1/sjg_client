#pragma once
#include <filesystem>
#include <string>
#include <string_view>
class LayoutActor;
namespace nw4r::ut { struct Glyph; }
namespace galaxy {
enum class Character : unsigned char { Mario, Luigi, Peach, Daisy };
const char* characterName(Character character);
bool marioCharacter(Character character);
Character activeCharacter();
void activateCharacter(Character character);
bool customCharacterResources();
void initializeCharacters(const std::filesystem::path& modRoot);
bool customCharactersAvailable();
bool hasFileCharacter(int slot);
Character fileCharacter(int slot, bool mario);
bool saveFileCharacter(int slot, Character character);
std::string characterArchive(std::string_view path);
void useCustomPortraitArchives(bool enabled);
void initializeCharacterOverlays(const std::filesystem::path& modRoot);
bool characterLifeGlyph(nw4r::ut::Glyph* glyph);
void updateCharacterLifeIcon(LayoutActor* layout);
class CharacterList {
public:
    void open(int slot, bool mario);
    void cancel();
    bool choose(Character character);
    bool opened = false, changed = false, saveFailed = false;
    int slot = 0;
    Character preview = Character::Mario;
    Character initial = Character::Mario;
};
CharacterList& characterList();
}
class JAUSectionHeap;
namespace galaxy {
void prepareCharacterVoices(JAUSectionHeap* heap);
bool updateCharacterVoices(JAUSectionHeap* heap);
void selectCharacterMenuVoices(Character character);
}
