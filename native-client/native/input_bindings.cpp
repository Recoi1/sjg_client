#include "input_bindings.hpp"
#include "internal.hpp"
#include "host_memory.hpp"
#include "io.hpp"
#include <aurora/binding.hpp>
#include <dolphin/pad.h>
#include <revolution/wpad.h>
#include <SDL3/SDL.h>
#include <array>
#include <algorithm>
#include <memory>
namespace galaxy {
namespace {
constexpr unsigned unbound=65535;
constexpr std::array<unsigned,16> defaultKeys{SDL_SCANCODE_SPACE,unbound,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_C,SDL_SCANCODE_RETURN,SDL_SCANCODE_LCTRL,SDL_SCANCODE_Q,SDL_SCANCODE_E,SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_D,SDL_SCANCODE_A,SDL_SCANCODE_W,SDL_SCANCODE_S};
constexpr std::array<unsigned,16> defaultMouse{unbound,SDL_BUTTON_LEFT,unbound,unbound,unbound,unbound,unbound,unbound,unbound,unbound,unbound,unbound,unbound,unbound,unbound,unbound};
constexpr std::array<unsigned,15> defaultPads{SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_NORTH,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_GUIDE,SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,SDL_GAMEPAD_BUTTON_COUNT,SDL_GAMEPAD_BUTTON_COUNT+1};
constexpr std::array<uint32_t,15> classicTargets{WPAD_CL_BUTTON_A,WPAD_CL_BUTTON_B,WPAD_CL_BUTTON_X,WPAD_CL_BUTTON_Y,WPAD_CL_BUTTON_PLUS,WPAD_CL_BUTTON_MINUS,WPAD_CL_BUTTON_HOME,WPAD_CL_BUTTON_UP,WPAD_CL_BUTTON_DOWN,WPAD_CL_BUTTON_LEFT,WPAD_CL_BUTTON_RIGHT,WPAD_CL_TRIGGER_L,WPAD_CL_TRIGGER_R,WPAD_CL_TRIGGER_ZL,WPAD_CL_TRIGGER_ZR};
constexpr PADButton buttons[]{PAD_BUTTON_A,PAD_BUTTON_B,PAD_BUTTON_X,PAD_BUTTON_Y,PAD_BUTTON_START,PAD_TRIGGER_Z,PAD_TRIGGER_L,PAD_TRIGGER_R,PAD_BUTTON_UP,PAD_BUTTON_DOWN,PAD_BUTTON_LEFT,PAD_BUTTON_RIGHT};
constexpr PADAxis axes[]{PAD_AXIS_LEFT_X_POS,PAD_AXIS_LEFT_X_NEG,PAD_AXIS_LEFT_Y_POS,PAD_AXIS_LEFT_Y_NEG};
std::array<std::array<unsigned,16>,3> codes{defaultKeys,{},defaultMouse};
InputBindings pending;
bool editing=false;
std::filesystem::path path;
aurora::binding::State mouseState;
std::array<aurora::binding::ControlId,16> mouseControls{};
aurora::input::LayerId layer=0;
bool capturing=false,failed=false,capturedEscape=false;
unsigned suppressedPad=unbound;
bool valid(unsigned column,unsigned action,unsigned code) {
    return column<3 && action<(column==1 ? classicActionCount : keyActionCount) &&
        (code==unbound || (column==0 ? code>0 && code<SDL_SCANCODE_COUNT && code!=SDL_SCANCODE_ESCAPE && code!=SDL_SCANCODE_B : column==1 ? code<SDL_GAMEPAD_BUTTON_COUNT+2 : code>=1 && code<=5 && code!=SDL_BUTTON_RIGHT));
}
bool save(const decltype(codes)& next) {
    if (path.empty()) return true;
    std::array<uint8_t,100> data{'G','K','B',1};
    for(unsigned c=0;c<3;++c) for(unsigned i=0;i<16;++i) { const unsigned at=4+(c*16+i)*2; data[at]=next[c][i]; data[at+1]=next[c][i]>>8; }
    return aurora::io::write_file_atomic(path,data);
}
void apply() {
    for(unsigned i=0;i<12;++i) PADSetKeyButtonBinding(0,{codes[0][i]==unbound ? PAD_KEY_INVALID : int(codes[0][i]),buttons[i]});
    for(unsigned i=0;i<4;++i) PADSetKeyAxisBinding(0,{codes[0][12+i]==unbound ? PAD_KEY_INVALID : int(codes[0][12+i]),axes[i],100});
    auto bindings=std::make_shared<aurora::binding::BindingSet>();
    for(unsigned i=0;i<16;++i) {
        if (!mouseControls[i]) mouseControls[i]=aurora::binding::register_control({"galaxy.mouse."+std::to_string(i),aurora::binding::ControlKind::Button});
        if(codes[2][i]!=unbound) bindings->bindings.push_back({.input={.source=aurora::input::mouse_source().id,.control=aurora::binding::PhysicalInput::MouseButton{uint8_t(codes[2][i])}},.target=mouseControls[i]});
    }
    (void)mouseState.set_bindings(bindings);
}
bool pressed(SDL_Gamepad* controller,unsigned code) {
    if (code==unbound) return false;
    if (code<SDL_GAMEPAD_BUTTON_COUNT) return SDL_GetGamepadButton(controller,SDL_GamepadButton(code));
    return SDL_GetGamepadAxis(controller,code==SDL_GAMEPAD_BUTTON_COUNT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)>32767*30/255;
}
}
void initializeInputBindings(const std::filesystem::path& userPath) {
    aurora::HostMemoryScope hostScope;
    discardInputBindingEdit(); failed=false; codes={defaultKeys,{},defaultMouse};
    std::copy(defaultPads.begin(),defaultPads.end(),codes[1].begin()); codes[1][15]=unbound;
    path=userPath.empty() ? std::filesystem::path{} : userPath/"galaxy_bindings.dat";
    if (!path.empty()) if(const auto data=aurora::io::read_file(path); data && data->size()==100 && std::equal(data->begin(),data->begin()+4,"GKB\1")) {
        auto next=codes; bool good=true;
        for(unsigned c=0;c<3;++c) for(unsigned i=0;i<16;++i) { const unsigned at=4+(c*16+i)*2,v=(*data)[at]|unsigned((*data)[at+1])<<8; next[c][i]=v; if(c==1 && i==15) good &= v==unbound; else good &= valid(c,i,v); }
        if(good) codes=next;
    }
    if(layer) aurora::input::unregister_layer(layer);
    layer=aurora::input::register_layer({.label="Galaxy extra mouse bindings",.priority=aurora::input::kGameLayerPriority,.onEvent=[](const aurora::input::InputEvent& event,void*) { (void)mouseState.process(event); return aurora::input::EventResult::Pass; }});
    apply();
}
unsigned bindingCode(unsigned c,unsigned a) { return c<3 && a<16 ? codes[c][a] : unbound; }
unsigned editingBindingCode(unsigned c,unsigned a) { return c<3 && a<16 ? (editing ? pending : codes)[c][a] : unbound; }
InputBindings inputBindings() { return codes; }
void beginInputBindingEdit() { cancelBindingCapture(); pending=codes; editing=true; failed=false; }
void discardInputBindingEdit() { cancelBindingCapture(); editing=false; failed=false; }
bool setInputBindings(const InputBindings& next) {
    aurora::HostMemoryScope hostScope;
    for(unsigned c=0;c<3;++c) for(unsigned a=0;a<16;++a) if(c==1 && a==15 ? next[c][a]!=unbound : !valid(c,a,next[c][a])) return false;
    if(!save(next)) return false;
    codes=next; apply(); return true;
}
bool applyInputBindingEdit() { return !editing || setInputBindings(pending); }
bool setBinding(unsigned c,unsigned a,unsigned code) {
    aurora::HostMemoryScope hostScope;
    if(!valid(c,a,code)) return false;
    if(editing) { pending[c][a]=code; return true; }
    auto next=codes; next[c][a]=code; return setInputBindings(next);
}
bool resetInputBindings() {
    aurora::HostMemoryScope hostScope;
    auto next=codes; next[0]=defaultKeys; next[2]=defaultMouse; std::copy(defaultPads.begin(),defaultPads.end(),next[1].begin());
    if(editing) { pending=next; return true; }
    return setInputBindings(next);
}
void captureBinding(unsigned c,unsigned a) {
    aurora::HostMemoryScope hostScope;
    cancelBindingCapture(); if(c>=3 || a>=(c==1 ? classicActionCount : keyActionCount)) return;
    capturing=true; failed=false;
    aurora::binding::capture_next([c,a](const aurora::binding::PhysicalInput& input) {
        capturing=false;
        if(const auto* key=input.control.get_if<aurora::binding::PhysicalInput::Key>(); key && key->scancode==SDL_SCANCODE_ESCAPE) { capturedEscape=true; return; }
        unsigned value=unbound;
        if(c==0) { if(const auto* k=input.control.get_if<aurora::binding::PhysicalInput::Key>()) value=k->scancode; else { failed=true; return; } }
        if(c==2) { if(const auto* m=input.control.get_if<aurora::binding::PhysicalInput::MouseButton>()) value=m->button; else { failed=true; return; } }
        if(c==1) {
            if(const auto* b=input.control.get_if<aurora::binding::PhysicalInput::GamepadButton>()) value=b->button;
            else if(const auto* axis=input.control.get_if<aurora::binding::PhysicalInput::GamepadAxis>(); axis && (axis->axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis->axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) value=SDL_GAMEPAD_BUTTON_COUNT+(axis->axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            else { failed=true; return; }
            suppressedPad=value;
        }
        failed=!setBinding(c,a,value);
    });
}
void cancelBindingCapture() { if(capturing) aurora::binding::cancel_capture(); capturing=false; }
bool bindingCaptureActive() { return capturing; }
bool takeBindingEscape() { return std::exchange(capturedEscape,false); }
bool bindingButtonSuppressed() { return suppressedPad!=unbound; }
bool bindingSaveFailed() { return failed; }
const wchar_t* bindingLabel(unsigned c,unsigned a) {
    constexpr const wchar_t* keyboard[]{L"Jump",L"Shoot star bits",L"Spin",L"Reset camera",L"Pause",L"Crouch",L"Tilt left",L"Tilt right",L"Camera up",L"Camera down",L"Camera left",L"Camera right",L"Move right",L"Move left",L"Move forward",L"Move back"};
    constexpr const wchar_t* classic[]{L"Classic A",L"Classic B",L"Classic X",L"Classic Y",L"Start / Plus",L"Back / Minus",L"Home",L"D-pad up",L"D-pad down",L"D-pad left",L"D-pad right",L"LB / L",L"RB / R",L"LT / ZL",L"RT / ZR"};
    return c==1 ? classic[a] : keyboard[a];
}
const char* bindingName(unsigned c,unsigned a) {
    const auto code=editingBindingCode(c,a); if(code==unbound) return "Unbound";
    if(c==0) return SDL_GetScancodeName(SDL_Scancode(code));
    if(c==1) { if(code==SDL_GAMEPAD_BUTTON_COUNT) return "LT"; if(code==SDL_GAMEPAD_BUTTON_COUNT+1) return "RT"; return SDL_GetGamepadStringForButton(SDL_GamepadButton(code)); }
    constexpr const char* names[]{"","Left mouse","Middle mouse","Right mouse","Mouse 4","Mouse 5"}; return names[code];
}
uint32_t boundClassicButtons(SDL_Gamepad* controller) {
    if(!controller || capturing) return 0;
    if(suppressedPad!=unbound) { if(pressed(controller,suppressedPad)) return 0; suppressedPad=unbound; }
    uint32_t result=0; for(unsigned i=0;i<classicActionCount;++i) if(pressed(controller,codes[1][i])) result|=classicTargets[i]; return result;
}
float mouseBindingValue(unsigned a) { return a<16 && !capturing ? mouseState.value(mouseControls[a]) : 0; }
}
