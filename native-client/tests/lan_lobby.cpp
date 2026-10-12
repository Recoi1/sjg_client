#include "lan_lobby.hpp"
#include <cassert>
#include <thread>
#include <fstream>
#include <iostream>
using namespace galaxy::lan;
int main(int argc,char** argv) {
    Build build{};build[0]=42;
    Lobby host;std::array<Lobby,3> guests;
    const Adapter local{"127.0.0.1","127.0.0.1",0xff000000};
    assert(!host.open(local,build,1,0));assert(host.open(local,build,15,0) && host.host());
    for(unsigned i=0;i<3;++i){auto adapter=local;adapter.ip="127.0.0."+std::to_string(i+2);assert(guests[i].open(adapter,build,15,i+1));guests[i].discovery.scan(local.ip.c_str());}
    auto pump=[&](auto done){for(unsigned i=0;i<2000;++i){host.pump();for(auto& g:guests)g.pump();if(done())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(false);};
    pump([&]{for(auto& g:guests)if(g.discovery.rows().size()!=1)return false;return true;});
    for(auto& g:guests){assert(g.discovery.rows()[0].characters==15);assert(!g.join(999));assert(g.join(0));}
    pump([&]{return host.session.players()==4 && guests[2].session.players()==4;});
    for(unsigned i=0;i<3;++i)assert(host.session.world().players[guests[i].session.slot()].character==i+1);
    guests[2].choose(2);pump([&]{return guests[0].session.world().players[guests[2].session.slot()].character==2;});
    guests[0].choose(255);assert(guests[0].selected==1);
    auto wrong=build;wrong[0]++;Discovery incompatible;
    assert(incompatible.open({"127.0.0.5","127.0.0.1",0xff000000},wrong,15));incompatible.scan("127.0.0.1");
    for(unsigned i=0;i<100;++i){host.pump();incompatible.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(incompatible.rows().empty());
    Discovery missingMods;assert(missingMods.open({"127.0.0.6","127.0.0.1",0xff000000},build,3));missingMods.scan("127.0.0.1");
    for(unsigned i=0;i<100;++i){host.pump();missingMods.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(missingMods.rows().empty());
    const auto departed=guests[1].session.slot();guests[1].leave();pump([&]{return host.session.players()==3 && !guests[0].session.world().players[departed].present;});
    host.leave();pump([&]{return !guests[0].joining && !guests[2].joining;});assert(!guests[0].error.empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(5100));guests[0].discovery.pump();assert(guests[0].discovery.rows().empty());
    assert(guests[1].open(local,build,3,1));guests[1].choose(3);assert(guests[1].selected==1);guests[1].leave();
    if(argc>1){const auto fingerprint=lobbyBuild(argv[1],{},3);assert(fingerprint!=Build{} && fingerprint==lobbyBuild(argv[1],{},3));bool rejected=false;try{lobbyBuild(argv[1],"missing-character-mod-fixture",15);}catch(const std::filesystem::filesystem_error&){rejected=true;}assert(rejected);}
    const auto adapter=radminAdapter();
    if(!adapter.ip.empty()) {
        Discovery broadcaster,scanner;Key key{};key[0]=17;
        assert(broadcaster.open(adapter,build,15,true));broadcaster.advertise(41681,key,1);
        assert(scanner.open(adapter,build,15));scanner.scan();
        for(unsigned i=0;i<2000 && scanner.rows().empty();++i){broadcaster.pump();scanner.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        assert(scanner.rows().size()==1 && scanner.rows()[0].ip==adapter.ip && scanner.rows()[0].key==key);
    }
    Session restricted,unsupported;Key code{};assert(restricted.host("127.0.0.1",0,build,code,3));
    assert(unsupported.join("127.0.0.1",restricted.port(),build,code,3));
    for(unsigned i=0;i<100;++i){restricted.pump();unsupported.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!unsupported.connected() && restricted.players()==1);
    std::cout<<"Radmin adapter: "<<adapter.ip<<" broadcast "<<adapter.broadcast<<"\nFour-player discovery, roster, mod/build mismatch, leave and expiry checks passed\n";
}
