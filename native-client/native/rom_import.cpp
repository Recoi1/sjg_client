#include "rom_import.hpp"
#include "rom_resources.hpp"
#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <SDL3/SDL.h>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cstring>
namespace galaxy {
namespace {
void validateDisc() {
    const auto* id=DVDGetCurrentDiskID();
    if(!id || std::memcmp(id->gameName,"RMGE",4) || std::memcmp(id->company,"01",2) || id->gameVersion)
        throw std::runtime_error("Supported ROM: Super Mario Galaxy USA RMGE01 revision 0 (ISO, WBFS or RVZ).");
    s32 size=0;const auto* dol=DVDGetDOLLocation(&size);
    if(!dol || size<=0) throw std::runtime_error("Cannot read the game executable from this ROM");
    initializeRomResources({dol,std::size_t(size)});
}
struct Entry {std::filesystem::path relative;u32 number,size;};
void entries(s32 number,const std::filesystem::path& parent,std::vector<Entry>& out,unsigned depth) {
    if(depth>16) throw std::runtime_error("ROM directory nesting exceeds supported limits");
    DVDDir dir{};if(!DVDFastOpenDir(number,&dir)) throw std::runtime_error("Cannot read ROM directory");
    DVDDirEntry entry{};
    while(DVDReadDir(&dir,&entry)) {
        if(!safeDiscName(entry.name)) throw std::runtime_error("ROM contains an unsafe file name");
        const auto relative=parent/entry.name;
        if(entry.isDir) entries(entry.entryNum,relative,out,depth+1);
        else {
            if(out.size()>=20000) throw std::runtime_error("ROM file count exceeds supported limits");
            DVDFileInfo file{};if(!DVDFastOpen(entry.entryNum,&file)) throw std::runtime_error("Cannot read ROM file metadata");
            const auto size=file.length;DVDClose(&file);
            if(size>1024u*1024*1024) throw std::runtime_error("ROM file exceeds supported limits");
            out.push_back({relative,entry.entryNum,size});
        }
    }
    DVDCloseDir(&dir);
}
}
bool safeDiscName(const char* name) {
    if(!name || !*name) return false;
    std::string n(name);if(n.size()>255 || n=="." || n==".." || n.back()=='.' || n.back()==' ') return false;
    for(unsigned char c:n) if(c<32 || c>=127 || std::strchr("\\/:*?\"<>|",c)) return false;
    auto stem=n.substr(0,n.find('.'));for(char& c:stem) if(c>='a' && c<='z') c-=32;
    return stem!="CON" && stem!="PRN" && stem!="AUX" && stem!="NUL" &&
        !(stem.size()==4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3]>='1' && stem[3]<='9');
}
ImportedGame readImportedGame(const std::filesystem::path& destination) {
    std::ifstream file(destination/"rom.txt",std::ios::binary|std::ios::ate);
    if(!file || file.tellg()<=0 || file.tellg()>32769) return {};
    file.seekg(0);
    std::string path;std::getline(file,path);if(!file || path.empty() || path.size()>32768) return {};
    ImportedGame game{std::filesystem::u8path(path),destination/"files"};
    if(!std::filesystem::is_regular_file(game.rom) || !std::filesystem::is_directory(game.files) ||
        !std::filesystem::is_regular_file(destination/"complete")) return {};
    return game;
}
void importRom(const std::filesystem::path& rom,const std::filesystem::path& destination,std::atomic<unsigned>& progress,std::stop_token cancel) {
    if(std::filesystem::exists(destination)) throw std::runtime_error("Game data already exists. Move it aside before importing again; it has not been overwritten.");
    const auto source=std::filesystem::canonical(rom);
    const auto utf8=source.u8string();const std::string sourceName(utf8.begin(),utf8.end());
    if(sourceName.find_first_of("\r\n")!=std::string::npos) throw std::runtime_error("ROM path contains unsupported characters");
    if(!aurora_dvd_open(sourceName.c_str())) throw std::runtime_error("Cannot open this ROM. Drop a supported disc image, not a ZIP archive.");
    struct Disc {~Disc(){aurora_dvd_close();}} disc;
    validateDisc();
    std::vector<Entry> list;entries(0,{},list,0);
    uint64_t total=0;for(const auto& e:list) total+=e.size;
    if(list.empty() || !total || total>8ull*1024*1024*1024) throw std::runtime_error("Invalid ROM resource inventory");
    std::filesystem::create_directories(destination.parent_path());
    if(std::filesystem::space(destination.parent_path()).available<total+64*1024*1024) throw std::runtime_error("Not enough free space to prepare your ROM");
    const auto temporary=std::filesystem::path(destination.wstring()+L".import-"+std::to_wstring(SDL_GetTicksNS()));
    if(!std::filesystem::create_directory(temporary)) throw std::runtime_error("Cannot create local game data");
    try {
        std::vector<uint8_t> buffer(1024*1024);uint64_t written=0;
        for(const auto& e:list) {
            if(cancel.stop_requested()) throw std::runtime_error("ROM import cancelled");
            const auto path=temporary/"files"/e.relative;std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path,std::ios::binary);if(!output) throw std::runtime_error("Cannot write game resource");
            DVDFileInfo file{};if(!DVDFastOpen(e.number,&file)) throw std::runtime_error("Cannot open ROM resource");
            struct Close {DVDFileInfo* file;~Close(){DVDClose(file);}} close{&file};
            for(u32 offset=0;offset<e.size;) {
                if(cancel.stop_requested()) throw std::runtime_error("ROM import cancelled");
                const auto amount=std::min<u32>(u32(buffer.size()),e.size-offset);
                if(DVDReadPrio(&file,buffer.data(),amount,offset,2)!=s32(amount)) throw std::runtime_error("ROM resource read failed");
                if(!output.write(reinterpret_cast<char*>(buffer.data()),amount)) throw std::runtime_error("Game resource write failed");
                offset+=amount;written+=amount;progress=unsigned(written*99/total);
            }
            output.close();if(!output) throw std::runtime_error("Could not finish a game resource");
        }
        for(const auto& pair: {std::pair{"rom.txt",sourceName+"\n"},std::pair{"complete",std::string("RMGE01 revision 0\n")}}) {
            std::ofstream output(temporary/pair.first,std::ios::binary);
            if(!output.write(pair.second.data(),pair.second.size())) throw std::runtime_error("Could not finish game data");
            output.close();if(!output) throw std::runtime_error("Could not commit game data");
        }
        if(cancel.stop_requested()) throw std::runtime_error("ROM import cancelled");
        std::filesystem::rename(temporary,destination);progress=100;
    } catch(...) {std::error_code ignored;std::filesystem::remove_all(temporary,ignored);throw;}
}
}
