#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
namespace galaxy::lan {
constexpr unsigned maxPlayers=4;
struct Input {uint16_t buttons=0;int8_t x=0,y=0,px=0,py=0;uint8_t character=0;bool operator==(const Input&)const=default;};
struct Player {bool present=false;uint8_t character=0,health=3;std::array<float,3> position{},front{0,0,1},gravity{0,-1,0};};
struct World {uint32_t tick=0,scene=0;uint8_t scenario=1;std::array<char,64> stage{};std::array<Player,maxPlayers> players{};};
using Build=std::array<uint8_t,32>;
using Key=std::array<uint8_t,16>;
bool validWorld(const World&);
// One game thread owns a session. pump() never waits for a remote computer.
class Session {
public:
    Session();~Session();
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    bool host(const char* localIp,uint16_t port,const Build&,const Key&,uint8_t characterMask=15);
    bool join(const char* hostIp,uint16_t port,const Build&,const Key&,uint8_t character=0);
    void close();void pump();
    bool publish(const World&);void input(Input);
    bool connected()const;unsigned slot()const;unsigned players()const;uint16_t port()const;
    bool present(unsigned slot)const;
    Input input(unsigned)const;World world()const;std::string error()const;
private:struct Impl;std::unique_ptr<Impl> state;
};
}
