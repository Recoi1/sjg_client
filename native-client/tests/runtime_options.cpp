#include "runtime_options.hpp"
#include "host_memory.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
extern "C" bool AuroraUsesHostMemory();
#include "frame_timing.hpp"
#include "input_bindings.hpp"
#include "pad_snapshot.hpp"
#include <revolution/wpad.h>
#include "io.hpp"
#include <aurora/aurora.h>
#include <aurora/main.h>
#include <aurora/binding.hpp>
#include <dolphin/pad.h>
extern "C" void VIAdvanceRetrace();
extern "C" unsigned VIGetRetraceCount();
#include "input/sdl_input.hpp"
#include <SDL3/SDL.h>
#include <cassert>
#include <fstream>
#include <limits>
#include <cstring>
using namespace galaxy;
int main(int argc,char** argv) {
    assert(argc==2); const auto root=std::filesystem::path(argv[1]); std::filesystem::create_directories(root);
    std::filesystem::remove(root/"galaxy_options.dat"); std::filesystem::remove(root/"galaxy_bindings.dat");
    initializeRuntimeOptions(root); assert(option(Option::FrameLimit)==60 && option(Option::MasterVolume)==100);
    for(unsigned i=0;i<optionCount;++i) assert(!setOption(Option(i),optionMaximum(Option(i))+1));
    assert(!setOption(Option::Count,0)); assert(!setOption(Option::FrameLimit,59)); assert(!setOption(Option::CameraSensitivity,0));
    assert(setOption(Option::FrameLimit,240)); assert(setOption(Option::Vsync,0)); assert(setOption(Option::FrameMode,1));
    assert(appliedOption(Option::FrameLimit)==60); initializeRuntimeOptions(root); assert(option(Option::FrameLimit)==240 && !option(Option::Vsync));
    const auto good=aurora::io::read_file(root/"galaxy_options.dat"); assert(good);
    {
        aurora::io::AtomicFileBatch changes;
        assert(setOption(Option::FrameLimit,120));
        assert(aurora::io::read_file(root/"galaxy_options.dat")==good);
        assert(changes.commit());
    }
    assert(aurora::io::read_file(root/"galaxy_options.dat")!=good);
    assert(aurora::io::write_file_atomic(root/"galaxy_options.dat",*good)); initializeRuntimeOptions(root);
    std::filesystem::create_directories(root/"cannot-save.dat");
    {
        aurora::io::AtomicFileBatch changes;
        const std::array<uint8_t,1> data{1};
        assert(aurora::io::write_file_atomic(root/"galaxy_options.dat",data));
        assert(aurora::io::write_file_atomic(root/"cannot-save.dat",data));
        assert(!changes.commit());
    }
    assert(aurora::io::read_file(root/"galaxy_options.dat")==good);
#ifdef _WIN32
    const auto readonlyFile=root/"rollback-readonly.dat";
    if(std::filesystem::exists(readonlyFile)) std::filesystem::permissions(readonlyFile,std::filesystem::perms::owner_all);
    const std::array<uint8_t,1> original{7},changed{9};
    assert(aurora::io::write_file_atomic(readonlyFile,original));
    std::filesystem::permissions(readonlyFile,std::filesystem::perms::owner_read);
    {
        aurora::io::AtomicFileBatch changes;
        assert(aurora::io::write_file_atomic(root/"galaxy_options.dat",changed));
        assert(aurora::io::write_file_atomic(readonlyFile,changed));
        assert(!changes.commit());
    }
    std::filesystem::permissions(readonlyFile,std::filesystem::perms::owner_all);
    assert(aurora::io::read_file(root/"galaxy_options.dat")==good);
    assert(aurora::io::read_file(readonlyFile)==std::optional<std::vector<uint8_t>>({7}));
#endif
    auto bad=*good; bad[3]=2; assert(aurora::io::write_file_atomic(root/"galaxy_options.dat",bad)); initializeRuntimeOptions(root); assert(option(Option::FrameLimit)==60);
    bad=*good; bad[4+unsigned(Option::FrameLimit)*2]=59; bad[5+unsigned(Option::FrameLimit)*2]=0; assert(aurora::io::write_file_atomic(root/"galaxy_options.dat",bad)); initializeRuntimeOptions(root); assert(option(Option::FrameLimit)==60);
    assert(setOption(Option::MasterVolume,30)); assert(setOption(Option::MusicVolume,20)); assert(soundVolume(Option::MasterVolume)==.3f && soundVolume(Option::MusicVolume)==.2f);
    assert(setOption(Option::MuteUnfocused,1)); setAudioFocus(false); assert(soundVolume(Option::MasterVolume)==0); setAudioFocus(true);
    for(bool right:{false,true}) {
        const auto rotate=right?Option::RotateRightStick:Option::RotateLeftStick,xkey=right?Option::InvertRightX:Option::InvertLeftX,ykey=right?Option::InvertRightY:Option::InvertLeftY;
        for(unsigned r=0;r<2;++r) for(unsigned x=0;x<2;++x) for(unsigned y=0;y<2;++y) {
            assert(setOption(rotate,r)&&setOption(xkey,x)&&setOption(ykey,y)); float sx=.25f,sy=.75f; transformStick(sx,sy,right);
            assert(sx==(r?-.75f:.25f)*(x?-1:1) && sy==(r?.25f:.75f)*(y?-1:1));
        }
    }
    initializeRuntimeOptions({});
    for(unsigned fps:{60,90,120,144,165,180,200,240,300,360,1000}) {
        FrameTiming clock; unsigned ticks=0;
        for(unsigned frame=0;frame<fps*30;++frame) { ticks+=clock.advance(1.0/fps); assert(clock.alpha()>=0 && clock.alpha()<=1); }
        assert(ticks==1800); assert(clock.advance(-1)==0 && clock.advance(std::numeric_limits<double>::quiet_NaN())==0); assert(clock.advance(10)==15);
    }
    Mtx pose{{1,0,0,0},{0,1,0,0},{0,0,1,0}}; const int model=0;
    setRenderTiming(true,1,1,true); interpolateModelMatrices(&model,0,&pose,1);
    for(unsigned step=1;step<=4;++step) { pose[0][3]=40; setRenderTiming(true,2,step*.25f,true); interpolateModelMatrices(&model,0,&pose,1); assert(pose[0][3]==step*10); }
    pose[0][3]=10000; setRenderTiming(true,3,.25f,true); interpolateModelMatrices(&model,0,&pose,1); assert(pose[0][3]==10000);
    pose[0][3]=10040; setRenderTiming(true,4,.25f,false); interpolateModelMatrices(&model,0,&pose,1); assert(pose[0][3]==10040);
    forgetModelMatrices(&model); pose[0][3]=10060; setRenderTiming(true,4,0,true); interpolateModelMatrices(&model,0,&pose,1); assert(pose[0][3]==10060);
    resetRenderHistory();
    auto* heapRoot=JKRExpHeap::createRoot(1,false); auto* tiny=JKRExpHeap::create(1024,heapRoot,false); assert(tiny);
    const auto freeBefore=tiny->getTotalFreeSize(); tiny->becomeCurrentHeap();
    {
        aurora::HostMemoryScope host;
        assert(AuroraUsesHostMemory()); auto* large=new char[8192]; assert(!JKRHeap::findFromRoot(large)); large[8191]=1; delete[] large;
        { aurora::HostMemoryScope nested; assert(AuroraUsesHostMemory()); } assert(AuroraUsesHostMemory());
    }
    assert(!AuroraUsesHostMemory() && JKRHeap::sCurrentHeap==tiny && tiny->getTotalFreeSize()==freeBefore);
    setRenderTiming(true,5,.5f,true); interpolateModelMatrices(&model,0,&pose,1);
    assert(JKRHeap::sCurrentHeap==tiny && tiny->getTotalFreeSize()==freeBefore);
    JKRHeap::sCurrentHeap=nullptr; resetRenderHistory(); tiny->destroy(); setRenderTiming(false,0,1,false);
    // A blocked path must leave the applied settings untouched.
    const auto blocked=root/"blocked"; std::filesystem::create_directories(blocked/"galaxy_options.dat"); initializeRuntimeOptions(blocked); assert(!setOption(Option::FrameLimit,240) && option(Option::FrameLimit)==60); initializeRuntimeOptions({});
    AuroraConfig config{}; config.appName="Galaxy runtime options check"; config.userPath=config.cachePath=argv[1]; const auto platform=aurora_initialize(argc,argv,&config); SDL_SetWindowOpacity(platform.window,0);
    initializeNativePads(); initializeInputBindings(root);
    assert(bindingCode(0,0)==SDL_SCANCODE_SPACE && bindingCode(2,1)==SDL_BUTTON_LEFT);
    assert(!setBinding(0,0,SDL_SCANCODE_ESCAPE) && !setBinding(2,0,SDL_BUTTON_RIGHT) && !setBinding(1,0,500));
    tiny=JKRExpHeap::create(1024,heapRoot,false); assert(tiny); const auto bindingFree=tiny->getTotalFreeSize(); tiny->becomeCurrentHeap();
    assert(setBinding(0,0,SDL_SCANCODE_J)); assert(setBinding(2,2,SDL_BUTTON_X1)); assert(setBinding(1,0,SDL_GAMEPAD_BUTTON_NORTH));
    assert(JKRHeap::sCurrentHeap==tiny && tiny->getTotalFreeSize()==bindingFree); JKRHeap::sCurrentHeap=nullptr; tiny->destroy();
    initializeInputBindings(root); assert(bindingCode(0,0)==SDL_SCANCODE_J && bindingCode(2,2)==SDL_BUTTON_X1 && bindingCode(1,0)==SDL_GAMEPAD_BUTTON_NORTH);
    SDL_Event key{}; key.type=SDL_EVENT_KEY_DOWN; key.key.scancode=SDL_SCANCODE_J; key.key.down=true; aurora::input::sdl::dispatch(key); assert(readNativePad(0).buttons&WPAD_BUTTON_A);
    key.type=SDL_EVENT_KEY_UP; key.key.down=false; aurora::input::sdl::dispatch(key); assert(!(readNativePad(0).buttons&WPAD_BUTTON_A));
    captureBinding(0,0); assert(bindingCaptureActive()); key.type=SDL_EVENT_KEY_DOWN; key.key.scancode=SDL_SCANCODE_K; key.key.down=true; aurora::input::sdl::dispatch(key); assert(!bindingCaptureActive() && bindingCode(0,0)==SDL_SCANCODE_K && !(readNativePad(0).buttons&WPAD_BUTTON_A));
    key.type=SDL_EVENT_KEY_UP; key.key.down=false; aurora::input::sdl::dispatch(key); key.type=SDL_EVENT_KEY_DOWN; key.key.down=true; aurora::input::sdl::dispatch(key); assert(readNativePad(0).buttons&WPAD_BUTTON_A);
    key.type=SDL_EVENT_KEY_UP; key.key.down=false; aurora::input::sdl::dispatch(key);
    SDL_Event mouse{}; mouse.type=SDL_EVENT_MOUSE_BUTTON_DOWN; mouse.button.button=SDL_BUTTON_X1; mouse.button.down=true; aurora::input::sdl::dispatch(mouse); assert(mouseBindingValue(2)==1 && readNativePad(0).accZ>2);
    mouse.type=SDL_EVENT_MOUSE_BUTTON_UP; mouse.button.down=false; aurora::input::sdl::dispatch(mouse); assert(mouseBindingValue(2)==0);
    captureBinding(0,0); key.type=SDL_EVENT_KEY_DOWN; key.key.scancode=SDL_SCANCODE_ESCAPE; key.key.down=true; aurora::input::sdl::dispatch(key);
    assert(!bindingCaptureActive() && takeBindingEscape() && !takeBindingEscape() && bindingCode(0,0)==SDL_SCANCODE_K);
    key.type=SDL_EVENT_KEY_UP; key.key.down=false; aurora::input::sdl::dispatch(key);
    assert(resetInputBindings() && bindingCode(0,0)==SDL_SCANCODE_SPACE);
    const auto oldBindings=aurora::io::read_file(root/"galaxy_bindings.dat");
    beginInputBindingEdit(); assert(setBinding(0,0,SDL_SCANCODE_J));
    assert(editingBindingCode(0,0)==SDL_SCANCODE_J && bindingCode(0,0)==SDL_SCANCODE_SPACE);
    assert(aurora::io::read_file(root/"galaxy_bindings.dat")==oldBindings);
    discardInputBindingEdit(); assert(editingBindingCode(0,0)==SDL_SCANCODE_SPACE);
    beginInputBindingEdit(); assert(setBinding(0,0,SDL_SCANCODE_J)); assert(applyInputBindingEdit()); discardInputBindingEdit();
    assert(bindingCode(0,0)==SDL_SCANCODE_J);
    initializeInputBindings(root); assert(bindingCode(0,0)==SDL_SCANCODE_J);
    std::filesystem::create_directories(blocked/"galaxy_bindings.dat"); initializeInputBindings(blocked); assert(!setBinding(0,0,SDL_SCANCODE_J) && bindingCode(0,0)==SDL_SCANCODE_SPACE);
    const auto count=VIGetRetraceCount(); VIAdvanceRetrace(); assert(VIGetRetraceCount()==count+1);
    aurora_shutdown(); return 0;
}
