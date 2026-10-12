#include "rom_resources.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <cstring>
namespace galaxy {
namespace {
ErrorArchive error{};
GalaxyIdTable galaxies{};
StoryEventTable story{};
std::mutex mutex;
bool loaded=false;
template<std::size_t N> void local(std::array<uint8_t,N>& out,const char* name) {
    const auto path=std::filesystem::path(GALAXY_BOOT_RESOURCE_ROOT)/name;
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input || input.tellg()!=N) throw std::runtime_error("Import your supported ROM before starting the game");
    input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(out.data()),N)) throw std::runtime_error("Cannot read local boot resource");
}
void loadLocal() {
    if(loaded) return;
    // Headless fixtures use local prepared data; shipped startup always validates its own ROM first.
    std::array<uint8_t,0x9ac0> e; std::array<uint8_t,0xd20> g; std::array<uint8_t,0x1e0> s;
    local(e,"ErrorMessageArchive.arc");local(g,"GalaxyIDBCSV.bcsv");local(s,"StoryEventBCSV.bcsv");
    std::copy(e.begin(),e.end(),error);std::copy(g.begin(),g.end(),galaxies);std::copy(s.begin(),s.end(),story);loaded=true;
}
}
void initializeRomResources(std::span<const uint8_t> dol) {
    constexpr std::array<uint8_t,20> expected{0x9a,0x71,0x00,0x8a,0xe1,0xee,0x90,0x10,0xe2,0x67,0xfa,0x67,0xd1,0xf0,0xb0,0xd4,0xf0,0xe8,0x95,0xdd};
    if(dol.size()<0x543ba0 || dol.size()>32*1024*1024) throw std::runtime_error("Unsupported or truncated game executable");
    BCRYPT_ALG_HANDLE alg=nullptr;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA1_ALGORITHM,nullptr,0)<0) throw std::runtime_error("Cannot validate ROM");
    std::array<uint8_t,20> digest{};
    const auto status=BCryptHash(alg,nullptr,0,const_cast<PUCHAR>(dol.data()),ULONG(dol.size()),digest.data(),ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(alg,0);
    if(status<0 || digest!=expected) throw std::runtime_error("This build requires Super Mario Galaxy USA RMGE01 revision 0. Your ROM was left unchanged.");
    std::lock_guard lock(mutex);
    std::copy_n(dol.data()+0x53a0e0,sizeof(error),error);
    std::copy_n(dol.data()+0x538900,sizeof(galaxies),galaxies);
    std::copy_n(dol.data()+0x538720,sizeof(story),story);loaded=true;
}
const ErrorArchive& errorArchive(){std::lock_guard lock(mutex);loadLocal();return error;}
const GalaxyIdTable& galaxyIdTable(){std::lock_guard lock(mutex);loadLocal();return galaxies;}
const StoryEventTable& storyEventTable(){std::lock_guard lock(mutex);loadLocal();return story;}
}
