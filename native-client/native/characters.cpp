#include "characters.hpp"
#include <revolution/nand.h>
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace galaxy {
namespace {
std::atomic<Character> active{Character::Mario};
bool available = false, portraitArchives = false;
std::array<unsigned char, 6> choices{255,255,255,255,255,255};
CharacterList list;
constexpr std::array<std::string_view, 24> archives{
    "Mario.arc", "Luigi.arc", "MarioAnime.arc", "MarioFace.arc", "LuigiFace.arc",
    "MarioShadow.arc", "LuigiShadow.arc", "MarioTornado.arc", "LuigiTornado.arc",
    "BoneMario.arc", "BoneLuigi.arc", "BeeMario.arc", "BeeLuigi.arc",
    "HopperMario.arc", "HopperLuigi.arc", "IceMario.arc", "IceLuigi.arc",
    "InvincibleMario.arc", "InvincibleLuigi.arc", "TeresaMario.arc", "TeresaLuigi.arc",
    "MarioHandL.arc", "MarioHandR.arc", "MarioHair.arc"};
}
const char* characterName(Character c) {
    constexpr const char* names[]{"Mario", "Luigi", "Peach", "Daisy"};
    return static_cast<unsigned>(c) < 4 ? names[static_cast<unsigned>(c)] : "Mario";
}
bool marioCharacter(Character c) { return c == Character::Mario || c == Character::Peach; }
Character activeCharacter() { return active.load(); }
bool customCharacterResources() { return static_cast<unsigned>(activeCharacter()) >= 2; }
void activateCharacter(Character c) {
    if (static_cast<unsigned>(c) >= 4 || (static_cast<unsigned>(c) >= 2 && !available))
        throw std::invalid_argument("Character assets are unavailable");
    active = c;
}
void initializeCharacters(const std::filesystem::path& modRoot) {
    active = Character::Mario; choices.fill(255); list = {}; portraitArchives = false;
    const auto root = modRoot / "characters/peach-daisy";
    std::error_code error;
    available = std::filesystem::is_regular_file(root / "AudioRes/player-voices.wsys", error) &&
        std::filesystem::is_regular_file(root / "AudioRes/menu-voices.wsys", error) &&
        std::filesystem::is_regular_file(root / "AudioRes/Waves/B98kawa_0.aw", error);
    for (const auto name : archives)
        available = available && std::filesystem::is_regular_file(root / "ObjectData" / name, error);
    for (const char* name : {"B70kawa_0.aw", "B70kawa_1.aw", "B70kawa_2.aw"})
        available = available && std::filesystem::is_regular_file(root / "AudioRes/Waves" / name, error);
    NANDFileInfo file{};
    if (NANDOpen("Characters.dat", &file, NAND_ACCESS_READ) == NAND_RESULT_OK) {
        std::array<unsigned char, 10> data{}; u32 size = 0;
        const bool valid = NANDGetLength(&file, &size) == NAND_RESULT_OK && size == data.size() &&
            NANDRead(&file, data.data(), data.size()) == data.size() && !std::memcmp(data.data(), "GCH\1", 4);
        NANDClose(&file);
        if (valid) for (unsigned i = 0; i < choices.size(); ++i)
            if (data[i + 4] < 4 || data[i + 4] == 255) choices[i] = data[i + 4];
    }
}
bool customCharactersAvailable() { return available; }
bool hasFileCharacter(int slot) { return slot >= 1 && slot <= 6 && choices[slot - 1] < 4; }
Character fileCharacter(int slot, bool mario) {
    const auto fallback = mario ? Character::Mario : Character::Luigi;
    if (slot < 1 || slot > 6 || choices[slot - 1] >= 4) return fallback;
    const auto c = static_cast<Character>(choices[slot - 1]);
    return !available && choices[slot - 1] >= 2 ? (marioCharacter(c) ? Character::Mario : Character::Luigi) : c;
}
bool saveFileCharacter(int slot, Character c) {
    if (slot < 1 || slot > 6 || static_cast<unsigned>(c) >= 4 || (static_cast<unsigned>(c) >= 2 && !available)) return false;
    std::array<unsigned char, 10> data{'G','C','H',1};
    std::copy(choices.begin(), choices.end(), data.begin() + 4);
    data[slot + 3] = static_cast<unsigned char>(c);
    const auto created = NANDCreate("Characters.dat", 0x30, 0);
    if (created != NAND_RESULT_OK && created != NAND_RESULT_EXISTS) return false;
    NANDFileInfo file{};
    if (NANDOpen("Characters.dat", &file, NAND_ACCESS_WRITE) != NAND_RESULT_OK) return false;
    const bool written = NANDWrite(&file, data.data(), data.size()) == data.size();
    const auto closed = NANDClose(&file);
    if (!written || closed != NAND_RESULT_OK) return false;
    choices[slot - 1] = static_cast<unsigned char>(c);
    return true;
}
std::string characterArchive(std::string_view path) {
    const auto slash = path.find_last_of('/');
    const auto base = path.substr(slash == path.npos ? 0 : slash + 1);
    if (base.starts_with("PD_") || !available) return std::string(path);
    bool replace = portraitArchives && (base == "Mario.arc" || base == "Luigi.arc" || base == "MarioAnime.arc");
    if (customCharacterResources()) for (const auto name : archives) replace |= base == name;
    if (!replace || (slash != path.npos && path.substr(0, slash) != "/ObjectData")) return std::string(path);
    return std::string(path.substr(0, slash == path.npos ? 0 : slash + 1)) + "PD_" + std::string(base);
}
void useCustomPortraitArchives(bool enabled) { portraitArchives = enabled; }
CharacterList& characterList() { return list; }
void CharacterList::open(int save, bool mario) {
    slot = save; initial = preview = fileCharacter(save, mario); changed = saveFailed = false; opened = true;
}
void CharacterList::cancel() { opened = false; preview = initial; changed = saveFailed = false; }
bool CharacterList::choose(Character c) {
    if (!opened) return false;
    if (!saveFileCharacter(slot, c)) { saveFailed = true; return false; }
    preview = c; opened = false; changed = true; saveFailed = false; return true;
}
}
