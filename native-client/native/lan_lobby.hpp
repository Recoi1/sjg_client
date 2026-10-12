#pragma once
#include "lan_session.hpp"
#include <chrono>
#include <filesystem>
#include <vector>
namespace galaxy::lan {
constexpr uint16_t discoveryPort=41680;
struct Adapter {std::string ip,broadcast;uint32_t mask=0;bool operator==(const Adapter&)const=default;};
Adapter radminAdapter();
Build lobbyBuild(const std::filesystem::path& executable,const std::filesystem::path& characterMods,uint8_t characterMask);
struct Advertisement {std::string ip;uint16_t port=0;Key key{};uint8_t players=0,characters=0;std::chrono::steady_clock::time_point seen;};
class Discovery {
public:
    Discovery();~Discovery();
    bool open(const Adapter&,const Build&,uint8_t characters,bool host=false,uint16_t port=discoveryPort);
    void close();void scan(const char* destination=nullptr);void pump();
    void advertise(uint16_t port,const Key&,uint8_t players);
    const std::vector<Advertisement>& rows()const;std::string error()const;
private:struct Impl;std::unique_ptr<Impl> state;
};
class Lobby {
public:
    bool open(const Adapter&,const Build&,uint8_t characters,uint8_t selected);
    bool host();bool join(unsigned row);void leave();void pump();void choose(uint8_t character);
    bool opened=false,hosting=false,joining=false;
    uint8_t selected=0,characters=3;
    Discovery discovery;Session session;
    std::string error;
private:Adapter adapter;Build build{};Key key{};uint32_t tick=0;
    std::chrono::steady_clock::time_point refreshed{},published{};
};
}
