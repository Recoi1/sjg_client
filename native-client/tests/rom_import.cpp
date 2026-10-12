#include "rom_import.hpp"
#include "rom_resources.hpp"
#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <cassert>
#include <fstream>
#include <cstring>
#include <vector>
#include <iostream>
#include <SDL3/SDL.h>
#include <chrono>
template<class F> void rejected(F function) {bool caught=false;try{function();}catch(const std::exception&){caught=true;}assert(caught);}
int main(int argc,char** argv) {
    assert(argc==3);
    for(const auto* n:{"Mario.arc","StageData","SMR.szs"}) assert(galaxy::safeDiscName(n));
    for(const auto* n:{"",".","..","a/b","a\\b","C:rom","CON.arc","com1","LPT9.arc","a.","a ","a\n"}) assert(!galaxy::safeDiscName(n));
    std::vector<uint8_t> corrupt(0x543ba0);
    rejected([&]{galaxy::initializeRomResources(corrupt);});
    const auto destination=std::filesystem::path(std::string(argv[2])+"-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    assert(!std::filesystem::exists(destination));
    std::atomic<unsigned> progress{0};
    rejected([&]{galaxy::importRom("missing.iso",destination,progress);});assert(!std::filesystem::exists(destination));
    std::stop_source cancelled;cancelled.request_stop();
    rejected([&]{galaxy::importRom(argv[1],destination,progress,cancelled.get_token());});assert(!std::filesystem::exists(destination));
    galaxy::importRom(argv[1],destination,progress);assert(progress==100);
    const auto game=galaxy::readImportedGame(destination);
    assert(game.rom==std::filesystem::canonical(argv[1]) && game.files==destination/"files");
    rejected([&]{galaxy::importRom(argv[1],destination,progress);});
    assert(aurora_dvd_open(argv[1]));
    s32 size=0;const auto* dol=DVDGetDOLLocation(&size);assert(dol && size>0);
    galaxy::initializeRomResources({dol,std::size_t(size)});
    assert(std::memcmp(cErrorArchive,"RARC",4)==0 && galaxy::galaxyIdTable()[3]==55);
    // Compare every extracted file against a fresh disc read, including full movie and audio payloads.
    std::vector<uint8_t> actual(1024*1024),expected(actual.size());unsigned count=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(game.files)) {
        if(!entry.is_regular_file()) continue;
        const auto relative=entry.path().lexically_relative(game.files).generic_string();
        DVDFileInfo file{};assert(DVDOpen(("/"+relative).c_str(),&file));
        assert(file.length==entry.file_size());std::ifstream extracted(entry.path(),std::ios::binary);
        for(u32 offset=0;offset<file.length;) {
            const auto n=std::min<u32>(actual.size(),file.length-offset);
            assert(extracted.read(reinterpret_cast<char*>(actual.data()),n));
            assert(DVDReadPrio(&file,expected.data(),n,offset,2)==s32(n));
            assert(std::equal(actual.begin(),actual.begin()+n,expected.begin()));offset+=n;
        }
        assert(DVDClose(&file));++count;
    }
    assert(count>1000);aurora_dvd_close();
    std::cout<<"ROM importer verified "<<count<<" full files, rejection, cancellation and overwrite protection\n";
    assert(std::filesystem::weakly_canonical(destination.parent_path())==std::filesystem::canonical(std::filesystem::path(argv[2]).parent_path()));
    assert(destination.filename().string().starts_with(std::filesystem::path(argv[2]).filename().string()+"-"));
    std::filesystem::remove_all(destination);
}
