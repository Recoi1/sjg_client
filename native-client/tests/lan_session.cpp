#include "lan_session.hpp"
#include <cassert>
#include <thread>
#include <chrono>
#include <limits>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using namespace galaxy::lan;
int main() {
    Build build{};build[0]=99;Key key{};key[0]=55;
    Session host;std::array<Session,3> guests;
    assert(!host.host("invalid",0,build,key));assert(host.host("127.0.0.1",0,build,key));
    for(auto& g:guests) assert(g.join("127.0.0.1",host.port(),build,key));
    auto pump=[&](auto done){for(unsigned i=0;i<1000;++i){host.pump();for(auto& g:guests)g.pump();if(done())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(false);};
    pump([&]{return host.players()==4 && guests[0].connected() && guests[1].connected() && guests[2].connected();});
    std::array<bool,4> used{true,false,false,false};
    for(auto& g:guests){assert(g.slot()>0 && g.slot()<4 && !used[g.slot()]);used[g.slot()]=true;}
    Input buttons{0x100,99,-33,45,-22,3};guests[1].input(buttons);
    pump([&]{return host.input(guests[1].slot())==buttons;});
    World w;w.tick=100;w.scene=1;std::strcpy(w.stage.data(),"EggStarGalaxy");
    for(unsigned i=0;i<4;++i){w.players[i].present=true;w.players[i].character=i;w.players[i].position={float(i*100),20,-10};}
    assert(host.publish(w));
    pump([&]{return guests[0].world().tick==100 && guests[1].world().tick==100 && guests[2].world().tick==100;});
    for(auto& g:guests){assert(g.players()==4 && g.world().players[3].character==3 && g.world().players[2].position[0]==200);}
    auto bad=w;bad.players[0].position[0]=std::numeric_limits<float>::quiet_NaN();assert(!host.publish(bad));
    bad=w;bad.players[0].gravity={0,0,0};assert(!validWorld(bad));bad=w;bad.stage.fill('A');assert(!validWorld(bad));
    Session fifth;assert(fifth.join("127.0.0.1",host.port(),build,key));
    for(unsigned i=0;i<100;++i){host.pump();fifth.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!fifth.connected());
    const auto departed=guests[1].slot();guests[1].close();pump([&]{return host.players()==3;});assert(host.input(departed)==Input{});
    Session incompatible;auto wrong=build;wrong[0]++;assert(incompatible.join("127.0.0.1",host.port(),wrong,key));
    for(unsigned i=0;i<100;++i){host.pump();incompatible.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!incompatible.connected() && host.players()==3);
    Session unauthorized;auto wrongKey=key;wrongKey[0]++;assert(unauthorized.join("127.0.0.1",host.port(),build,wrongKey));
    for(unsigned i=0;i<100;++i){host.pump();unauthorized.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!unauthorized.connected() && host.players()==3);
    assert(guests[1].join("127.0.0.1",host.port(),build,key));pump([&]{return host.players()==4 && guests[1].connected();});
    for(unsigned tick=101;tick<401;++tick){w.tick=tick;host.publish(w);for(auto& g:guests)g.input({uint16_t(tick),int8_t(tick%100),0,0,0});host.pump();for(auto& g:guests)g.pump();}
    pump([&]{host.publish(w);return guests[0].world().tick==400 && guests[1].world().tick==400 && guests[2].world().tick==400;});
    host.close();for(unsigned i=0;i<100;++i)for(auto& g:guests)g.pump();for(auto& g:guests)assert(!g.connected());
    // Fragment a real host packet, then send a malformed snapshot through the socket boundary.
    const SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(server!=INVALID_SOCKET);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address)) && !listen(server,1));
    int length=sizeof(address);assert(!getsockname(server,reinterpret_cast<sockaddr*>(&address),&length));
    Session client;assert(client.join("127.0.0.1",ntohs(address.sin_port),build,key));
    SOCKET wire=accept(server,nullptr,nullptr);assert(wire!=INVALID_SOCKET);
    std::array<uint8_t,320> packet{};std::copy_n("GNET",4,packet.begin());packet[4]=2;packet[5]=2;packet[6]=1;
    std::copy(build.begin(),build.end(),packet.begin()+12);std::copy(key.begin(),key.end(),packet.begin()+44);
    for(unsigned i=0;i<20;++i){client.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(send(wire,reinterpret_cast<const char*>(packet.data()),160,0)==160);
    for(unsigned i=0;i<20;++i){client.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!client.connected());
    assert(send(wire,reinterpret_cast<const char*>(packet.data()+160),160,0)==160);
    for(unsigned i=0;i<20;++i){client.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(client.connected());
    packet[5]=4;packet[6]=0;packet[68]=1;std::copy_n("EggStarGalaxy",13,packet.begin()+69);
    assert(send(wire,reinterpret_cast<const char*>(packet.data()),320,0)==320);
    for(unsigned i=0;i<50;++i){client.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(!client.connected() && client.error()=="Invalid host snapshot");closesocket(wire);closesocket(server);
}
