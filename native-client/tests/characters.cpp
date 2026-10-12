#include "characters.hpp"
#include "nand.hpp"
#include <revolution/nand.h>
#include <cassert>
#include <fstream>
#include <cstring>
int main(int argc, char** argv) {
    assert(argc == 3);
    galaxy::setNandRoot(argv[2]); assert(NANDInit() == NAND_RESULT_OK);
    NANDDelete("Characters.dat");
    galaxy::initializeCharacters(argv[1]);
    assert(galaxy::customCharactersAvailable());
    assert(!galaxy::hasFileCharacter(1) && !galaxy::hasFileCharacter(0) && !galaxy::hasFileCharacter(7));
    assert(galaxy::fileCharacter(1, true) == galaxy::Character::Mario);
    assert(galaxy::fileCharacter(1, false) == galaxy::Character::Luigi);
    auto& list = galaxy::characterList();
    list.open(1, true); assert(list.opened && !list.changed);
    list.cancel(); assert(!list.opened && !list.changed);
    assert(!list.choose(galaxy::Character::Peach));
    list.open(1, true); assert(list.choose(galaxy::Character::Peach) && list.changed && !list.opened);
    assert(galaxy::fileCharacter(1, true) == galaxy::Character::Peach);
    assert(galaxy::saveFileCharacter(2, galaxy::Character::Daisy));
    assert(!galaxy::saveFileCharacter(0, galaxy::Character::Mario));
    assert(!galaxy::saveFileCharacter(7, galaxy::Character::Mario));
    assert(!galaxy::saveFileCharacter(1, static_cast<galaxy::Character>(4)));
    galaxy::initializeCharacters(argv[1]);
    assert(galaxy::fileCharacter(1, true) == galaxy::Character::Peach);
    assert(galaxy::fileCharacter(2, false) == galaxy::Character::Daisy);
    assert(galaxy::fileCharacter(2, true) == galaxy::Character::Daisy);
    assert(galaxy::hasFileCharacter(1) && galaxy::hasFileCharacter(2) && !galaxy::hasFileCharacter(3));
    for (auto c : {galaxy::Character::Mario, galaxy::Character::Peach, galaxy::Character::Daisy, galaxy::Character::Luigi}) {
        galaxy::activateCharacter(c);
        const bool custom = static_cast<unsigned>(c) >= 2;
        assert(galaxy::characterArchive("/ObjectData/Mario.arc") == (custom ? "/ObjectData/PD_Mario.arc" : "/ObjectData/Mario.arc"));
        assert(galaxy::characterArchive("MarioAnime.arc") == (custom ? "PD_MarioAnime.arc" : "MarioAnime.arc"));
        assert(galaxy::characterArchive("/ObjectData/PD_Mario.arc") == "/ObjectData/PD_Mario.arc");
        assert(galaxy::characterArchive("/MapPartsData/Mario.arc") == "/MapPartsData/Mario.arc");
        assert(galaxy::characterArchive("/ObjectData/FileSelectDataMario.arc") == "/ObjectData/FileSelectDataMario.arc");
    }
    galaxy::useCustomPortraitArchives(true);
    assert(galaxy::characterArchive("FileSelectDataMario.arc") == "FileSelectDataMario.arc");
    galaxy::activateCharacter(galaxy::Character::Mario);
    assert(galaxy::characterArchive("Mario.arc") == "PD_Mario.arc");
    assert(galaxy::characterArchive("Luigi.arc") == "PD_Luigi.arc");
    galaxy::useCustomPortraitArchives(false);
    assert(galaxy::characterArchive("Mario.arc") == "Mario.arc");
    galaxy::initializeCharacters(std::filesystem::path(argv[1]) / "missing-mod");
    assert(!galaxy::customCharactersAvailable() && galaxy::fileCharacter(1, true) == galaxy::Character::Mario);
    NANDFileInfo file{}; assert(NANDOpen("Characters.dat", &file, NAND_ACCESS_WRITE) == NAND_RESULT_OK);
    const unsigned char invalid[]{'G','C','H',1,99,99,99,99,99,99};
    assert(NANDWrite(&file, invalid, sizeof invalid) == sizeof invalid && NANDClose(&file) == NAND_RESULT_OK);
    galaxy::initializeCharacters(argv[1]); assert(galaxy::fileCharacter(1, true) == galaxy::Character::Mario);
    NANDDelete("Characters.dat"); assert(NANDCreate("Characters.dat", 0x30, 0) == NAND_RESULT_OK);
    assert(NANDOpen("Characters.dat", &file, NAND_ACCESS_READ) == NAND_RESULT_OK);
    list.open(1, true); assert(!list.choose(galaxy::Character::Peach) && list.opened && list.saveFailed);
    assert(galaxy::fileCharacter(1, true) == galaxy::Character::Mario);
    assert(NANDClose(&file) == NAND_RESULT_OK); list.cancel();
    galaxy::shutdownNand();
}
