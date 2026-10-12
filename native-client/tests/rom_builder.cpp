#include "rom_import.hpp"
#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
int main(int argc,char** argv) {
    assert(argc==3);
    assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"GALAXY_VERIFY_BUILDER_CAPTURE",argv[1],true));
    assert(!galaxy::showRomBuilder(std::filesystem::path(argv[1]).parent_path()/"builder-preview-no-install"));
    std::ifstream image(argv[1],std::ios::binary);char magic[2]{};assert(image.read(magic,2));
    assert(magic[0]=='B' && magic[1]=='M' && std::filesystem::file_size(argv[1])>100000);
    assert(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(),"GALAXY_VERIFY_BUILDER_CAPTURE"));
    const auto parent=std::filesystem::path(argv[1]).parent_path();
    const auto destination=parent/("builder-drop-test-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    assert(SDL_Init(SDL_INIT_VIDEO));
    std::atomic<bool> finished=false;
    std::jthread drop([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        SDL_Event event{};event.type=SDL_EVENT_DROP_FILE;event.drop.data=argv[2];assert(SDL_PushEvent(&event));
        for(unsigned i=0;i<9000 && !finished;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(!finished){SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);}
    });
    const bool imported=galaxy::showRomBuilder(destination);finished=true;drop.join();assert(imported);
    assert(!galaxy::readImportedGame(destination).rom.empty());
    assert(std::filesystem::canonical(destination.parent_path())==std::filesystem::canonical(parent));
    std::filesystem::remove_all(destination);
}
