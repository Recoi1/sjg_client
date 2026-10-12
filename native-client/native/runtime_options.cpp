#include "runtime_options.hpp"
#include "io.hpp"
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <SDL3/SDL.h>
#include <algorithm>
namespace galaxy {
namespace {
constexpr std::array<unsigned, optionCount> defaults{
    0,1,0,0,60,1,0,0,0, 1,0,1,50,1, 0,0,0,16,50, 0,0,0,0,0,0, 0,100,100,100,100,1,0,0
};
constexpr std::array<unsigned, optionCount> maxima{
    1,1,2,1,360,1,3,1,2, 1,1,1,100,1, 1,1,15,50,100, 1,1,1,1,1,1, 15,100,100,100,100,1,1,2
};
std::array<std::atomic<unsigned>, optionCount> values;
std::filesystem::path settingsPath;
std::array<unsigned, optionCount> applied = defaults;
std::atomic_bool focused{true};
struct Defaults { Defaults() { for (unsigned i=0; i<optionCount; ++i) values[i]=defaults[i]; } } init;
}
unsigned option(Option key) { const auto i=unsigned(key); return i<optionCount ? values[i].load() : 0; }
unsigned appliedOption(Option key) { return unsigned(key)<optionCount ? applied[unsigned(key)] : 0; }
void transformStick(float& x,float& y,bool right) {
    if (option(right ? Option::RotateRightStick : Option::RotateLeftStick)) { const float old=x; x=-y; y=old; }
    if (option(right ? Option::InvertRightX : Option::InvertLeftX)) x=-x;
    if (option(right ? Option::InvertRightY : Option::InvertLeftY)) y=-y;
}
unsigned optionMaximum(Option key) { const auto i=unsigned(key); return i<optionCount ? maxima[i] : 0; }
void initializeRuntimeOptions(const std::filesystem::path& userPath) {
    settingsPath = userPath.empty() ? std::filesystem::path{} : userPath/"galaxy_options.dat";
    for (unsigned i=0; i<optionCount; ++i) values[i]=defaults[i];
    applied = defaults;
    if (settingsPath.empty()) return;
    const auto data=aurora::io::read_file(settingsPath);
    if (!data || data->size()!=4+optionCount*2 || !std::equal(data->begin(),data->begin()+4,"GOP\1")) return;
    for (unsigned i=0; i<optionCount; ++i) {
        const auto v=(*data)[4+i*2] | unsigned((*data)[5+i*2])<<8;
        if (v>maxima[i] || (i==unsigned(Option::FrameLimit) && v && v<60) ||
            (i==unsigned(Option::CameraSensitivity) && !v)) return;
    }
    for (unsigned i=0; i<optionCount; ++i) applied[i]=values[i]=(*data)[4+i*2] | unsigned((*data)[5+i*2])<<8;
}
bool setOption(Option key, unsigned value) {
    const auto index=unsigned(key);
    if (index>=optionCount || value>maxima[index] || (key==Option::FrameLimit && value && value<60) ||
        (key==Option::CameraSensitivity && !value)) return false;
    auto next=runtimeOptions(); next[index]=value; return setRuntimeOptions(next);
}
RuntimeOptions runtimeOptions() {
    RuntimeOptions result; for(unsigned i=0;i<optionCount;++i) result[i]=values[i].load(); return result;
}
bool setRuntimeOptions(const RuntimeOptions& next) {
    for(unsigned i=0;i<optionCount;++i) if(next[i]>maxima[i]) return false;
    if((next[unsigned(Option::FrameLimit)] && next[unsigned(Option::FrameLimit)]<60) || !next[unsigned(Option::CameraSensitivity)]) return false;
    std::array<uint8_t,4+optionCount*2> data{'G','O','P',1};
    for (unsigned i=0; i<optionCount; ++i) {
        const auto v=next[i]; data[4+i*2]=v; data[5+i*2]=v>>8;
    }
    if (!settingsPath.empty() && !aurora::io::write_file_atomic(settingsPath,data)) return false;
    for(unsigned i=0;i<optionCount;++i) values[i]=next[i]; return true;
}
SDL_AudioDeviceID soundOutputDevice() {
    const auto selected=option(Option::SoundOutput);
    int count=0; auto* devices=SDL_GetAudioPlaybackDevices(&count);
    const auto result=selected && selected<=unsigned(count) ? devices[selected-1] : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    SDL_free(devices); return result;
}
float soundVolume(Option bus) {
    if (option(Option::MuteUnfocused) && !focused.load()) return 0;
    return option(bus)/100.0f;
}
void setAudioFocus(bool value) { focused=value; }
bool openUserDataDirectory() {
    if(settingsPath.empty()) return false;
    const auto url="file:///"+settingsPath.parent_path().generic_string();
    return SDL_OpenURL(url.c_str());
}
void applyRuntimeOptions() {
    applied=runtimeOptions();
    aurora_enable_vsync(option(Option::Vsync));
    aurora_set_background_input(option(Option::BackgroundGamepad));
    AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
    AuroraSetContentAspect(appliedOption(Option::Aspect43) ? 4.0f/3 : 0);
    AuroraSetTextureFiltering(option(Option::Filtering));
}
}
