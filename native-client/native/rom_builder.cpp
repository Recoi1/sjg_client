#include "rom_import.hpp"
#include <SDL3/SDL.h>
#include <thread>
#include <mutex>
#include <string>
#include <array>
#include <cmath>
#include <cstring>
namespace galaxy {
bool showRomBuilder(const std::filesystem::path& destination) {
    if(!SDL_Init(SDL_INIT_VIDEO)) return false;
    SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;
    if(!SDL_CreateWindowAndRenderer("Galaxy PC - ROM builder",960,540,0,&window,&renderer)) {SDL_Quit();return false;}
    SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
    SDL_SetRenderLogicalPresentation(renderer,960,540,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    std::atomic<unsigned> progress{0};std::atomic<bool> busy{false},finished{false};std::mutex mutex;
    std::string error;std::jthread worker;bool accepted=false,running=true;
    while(running) {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_EVENT_QUIT) running=false;
            if(event.type==SDL_EVENT_DROP_FILE && !busy) {
                if(worker.joinable()) worker.join();
                const auto rom=std::filesystem::u8path(event.drop.data);progress=0;busy=true;
                {std::lock_guard lock(mutex);error.clear();}
                worker=std::jthread([&,rom](std::stop_token stop){
                    try {importRom(rom,destination,progress,stop);finished=true;}
                    catch(const std::exception& e){std::lock_guard lock(mutex);error=e.what();}
                    busy=false;
                });
            }
        }
        if(finished){accepted=true;break;}
        SDL_SetRenderDrawColor(renderer,3,8,22,255);SDL_RenderClear(renderer);
        for(unsigned i=0;i<150;++i) {
            SDL_SetRenderDrawColor(renderer,70+i%100,140+i%100,255,255);
            SDL_RenderPoint(renderer,float((i*701+31)%960),float((i*389+17)%540));
        }
        for(int radius=138;radius<148;++radius) {
            SDL_SetRenderDrawColor(renderer,0,100,210,Uint8(30+(148-radius)*12));
            std::array<SDL_FPoint,97> points;
            for(unsigned i=0;i<points.size();++i){const float angle=float(i)*6.2831853f/96;points[i]={480+radius*std::cos(angle),240+radius*0.38f*std::sin(angle)};}
            SDL_RenderLines(renderer,points.data(),int(points.size()));
        }
        SDL_SetRenderScale(renderer,3,3);SDL_SetRenderDrawColor(renderer,235,245,255,255);
        SDL_RenderDebugText(renderer,124,76,"GALAXY PC");SDL_SetRenderScale(renderer,2,2);
        SDL_SetRenderDrawColor(renderer,175,220,255,255);
        const char* instruction=busy?"Preparing your local game data...":"Drag your own ROM into this window";
        SDL_RenderDebugText(renderer,(480-std::strlen(instruction)*8)*0.5f,177,instruction);
        SDL_SetRenderScale(renderer,1,1);SDL_SetRenderDrawColor(renderer,140,165,190,255);
        for(const auto& label:{std::pair{"USA RMGE01 revision 0 | ISO / WBFS / RVZ",405.f},std::pair{"Keep your ROM in its location after import.",426.f}})
            SDL_RenderDebugText(renderer,(960-std::strlen(label.first)*8)*0.5f,label.second,label.first);
        if(busy) {
            SDL_FRect track{240,380,480,10};SDL_SetRenderDrawColor(renderer,25,45,65,255);SDL_RenderFillRect(renderer,&track);
            track.w*=progress.load()/100.f;SDL_SetRenderDrawColor(renderer,0,190,255,255);SDL_RenderFillRect(renderer,&track);
        }
        {std::lock_guard lock(mutex);if(!error.empty()) {SDL_SetRenderDrawColor(renderer,255,160,145,255);SDL_RenderDebugText(renderer,24,472,error.c_str());}}
        if(const auto* capture=SDL_getenv("GALAXY_VERIFY_BUILDER_CAPTURE")) {
            if(auto* image=SDL_RenderReadPixels(renderer,nullptr)){SDL_SaveBMP(image,capture);SDL_DestroySurface(image);}
            running=false;
        }
        SDL_RenderPresent(renderer);SDL_Delay(16);
    }
    worker.request_stop();if(worker.joinable()) worker.join();
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return accepted;
}
}
