#include "lan_session.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <chrono>
namespace galaxy::lan {
namespace {
using Clock=std::chrono::steady_clock;
using Packet=std::array<uint8_t,320>;
enum Type:uint8_t {Hello=1,Welcome,Controls,Snapshot,Keepalive};
void put(Packet& p,unsigned& at,uint32_t v){for(int shift=24;shift>=0;shift-=8)p[at++]=uint8_t(v>>shift);}
uint32_t get(const Packet& p,unsigned& at){uint32_t v=0;for(int i=0;i<4;++i)v=(v<<8)|p[at++];return v;}
void encode(Packet& p,const World& w) {
    unsigned at=60;put(p,at,w.tick);put(p,at,w.scene);p[at++]=w.scenario;
    std::copy(w.stage.begin(),w.stage.end(),p.begin()+at);at+=w.stage.size();
    for(const auto& player:w.players) {
        p[at++]=player.present;p[at++]=player.character;p[at++]=player.health;
        for(const auto* v:{&player.position,&player.front,&player.gravity}) for(float f:*v) put(p,at,std::bit_cast<uint32_t>(f));
    }
}
World decode(const Packet& p) {
    World w;unsigned at=60;w.tick=get(p,at);w.scene=get(p,at);w.scenario=p[at++];
    std::copy_n(p.begin()+at,w.stage.size(),w.stage.begin());at+=w.stage.size();
    for(auto& player:w.players){player.present=p[at++]!=0;player.character=p[at++];player.health=p[at++];
        for(auto* v:{&player.position,&player.front,&player.gravity})for(float& f:*v)f=std::bit_cast<float>(get(p,at));}
    return w;
}
bool nonblock(SOCKET s){u_long enabled=1;int yes=1;return !ioctlsocket(s,FIONBIO,&enabled) && !setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof(yes));}
bool address(const char* ip,uint16_t port,sockaddr_in& out){out={};out.sin_family=AF_INET;out.sin_port=htons(port);return ip && inet_pton(AF_INET,ip,&out.sin_addr)==1;}
}
bool validWorld(const World& w) {
    if(!w.scenario || w.scenario>99 || !w.stage[0] || w.stage.back()) return false;
    bool ended=false;for(char c:w.stage){if(!c){ended=true;continue;}if(ended || !(c>='A'&&c<='Z') && !(c>='a'&&c<='z') && !(c>='0'&&c<='9') && c!='_') return false;}
    for(const auto& p:w.players) {
        if(p.character>3 || p.health>6) return false;
        for(const auto* v:{&p.position,&p.front,&p.gravity})for(float f:*v)if(!std::isfinite(f) || std::abs(f)>10000000) return false;
        for(const auto* v:{&p.front,&p.gravity}) {float n=0;for(float f:*v)n+=f*f;if(n<.8f || n>1.2f)return false;}
    }
    return true;
}
struct Session::Impl {
    struct Peer {SOCKET socket=INVALID_SOCKET;bool accepted=false;Packet incoming{},outgoing{};unsigned received=0,sent=320;Clock::time_point last=Clock::now(),created=last;};
    SOCKET listener=INVALID_SOCKET;std::array<Peer,3> peers{};Build build{};Key key{};
    bool hosting=false,live=false,starting=false,winsock=false;unsigned local=4;uint16_t boundPort=0;uint8_t preferredCharacter=0,characterMask=15;
    std::array<Input,4> inputs{};World world{};std::string error;Clock::time_point ping=Clock::now();
    ~Impl(){shutdown();if(winsock)WSACleanup();}
    void drop(Peer& p){if(p.socket!=INVALID_SOCKET)closesocket(p.socket);p=Peer{};}
    void shutdown(){for(auto& p:peers)drop(p);if(listener!=INVALID_SOCKET)closesocket(listener);listener=INVALID_SOCKET;live=starting=hosting=false;local=4;inputs={};world={};}
    Packet packet(Type type,unsigned slot=0){Packet p{};std::copy_n("GNET",4,p.begin());p[4]=2;p[5]=type;p[6]=uint8_t(slot);std::copy(build.begin(),build.end(),p.begin()+12);std::copy(key.begin(),key.end(),p.begin()+44);return p;}
    bool queue(Peer& p,Packet packet) {
        if(p.sent!=p.outgoing.size()) return false;
        p.outgoing=packet;p.sent=0;return true;
    }
    bool check(const Packet& p){return !std::memcmp(p.data(),"GNET",4) && p[4]==2 && p[5]>=Hello && p[5]<=Keepalive &&
        std::equal(build.begin(),build.end(),p.begin()+12) && std::equal(key.begin(),key.end(),p.begin()+44);}
    bool message(Peer& peer,unsigned index) {
        const auto& p=peer.incoming;if(!check(p)) {error="Incompatible build or incorrect session code";return false;}
        const auto type=Type(p[5]);const unsigned remote=index+1;
        if(hosting) {
            if(!peer.accepted) {if(type!=Hello || p[66]>3 || !(characterMask&(1<<p[66])))return false;inputs[remote].character=p[66];peer.accepted=true;return queue(peer,packet(Welcome,remote));}
            if(type==Controls && p[6]==remote) {if(p[66]>3 || !(characterMask&(1<<p[66])))return false;for(unsigned i=62;i<66;++i)if(p[i]==128)return false;inputs[remote]={uint16_t(p[60]<<8|p[61]),int8_t(p[62]),int8_t(p[63]),int8_t(p[64]),int8_t(p[65]),p[66]};return true;}
            return type==Keepalive && p[6]==remote;
        }
        if(!live) {if(type!=Welcome || p[6]<1 || p[6]>3)return false;local=p[6];live=peer.accepted=true;return true;}
        if(type==Snapshot) {for(unsigned i=0;i<4;++i)if(p[133+i*39]>1)return false;const auto next=decode(p);if(!validWorld(next)){error="Invalid host snapshot";return false;}
            if(next.scene!=world.scene || int32_t(next.tick-world.tick)>0)world=next;return true;}
        return type==Keepalive && p[6]==0;
    }
    void io(Peer& p,unsigned index) {
        if(p.socket==INVALID_SOCKET)return;
        // Bound reads to eight packets per peer per call, so a sender cannot monopolize a game frame.
        for(unsigned count=0;count<8;++count) {
            const int n=recv(p.socket,reinterpret_cast<char*>(p.incoming.data()+p.received),int(p.incoming.size()-p.received),0);
            if(n<0){if(WSAGetLastError()!=WSAEWOULDBLOCK)drop(p);break;}
            if(!n){if(!hosting)error="Host disconnected";drop(p);break;}
            p.received+=n;p.last=Clock::now();
            if(p.received==p.incoming.size()){p.received=0;if(!message(p,index)){drop(p);break;}}
        }
        if(p.socket!=INVALID_SOCKET && p.sent<p.outgoing.size()) {
            const int n=send(p.socket,reinterpret_cast<const char*>(p.outgoing.data()+p.sent),int(p.outgoing.size()-p.sent),0);
            if(n>0)p.sent+=n;else if(n==0 || WSAGetLastError()!=WSAEWOULDBLOCK)drop(p);
        }
        if(p.socket!=INVALID_SOCKET && (Clock::now()-p.last>std::chrono::seconds(10) || (!p.accepted && Clock::now()-p.created>std::chrono::seconds(5)))){error="Peer timed out";drop(p);}
        if(p.socket==INVALID_SOCKET){inputs[index+1]={};if(!hosting){live=starting=false;local=4;}}
    }
};
Session::Session():state(std::make_unique<Impl>()){WSADATA data;state->winsock=WSAStartup(MAKEWORD(2,2),&data)==0;}
Session::~Session()=default;
bool Session::host(const char* ip,uint16_t port,const Build& build,const Key& key,uint8_t characterMask) {
    close();auto& s=*state;s.error.clear();sockaddr_in addr;
    if(!s.winsock || (characterMask!=3 && characterMask!=15) || !address(ip,port,addr)){s.error="Enter a numeric IPv4 address";return false;}
    s.characterMask=characterMask;
    s.listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(s.listener==INVALID_SOCKET || bind(s.listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)) || listen(s.listener,3) || !nonblock(s.listener)){s.error="Cannot listen on this address and port";s.shutdown();return false;}
    int length=sizeof(addr);getsockname(s.listener,reinterpret_cast<sockaddr*>(&addr),&length);s.boundPort=ntohs(addr.sin_port);
    s.build=build;s.key=key;s.hosting=s.live=true;s.local=0;return true;
}
bool Session::join(const char* ip,uint16_t port,const Build& build,const Key& key,uint8_t character) {
    close();auto& s=*state;s.error.clear();s.preferredCharacter=character;sockaddr_in addr;
    if(!s.winsock || !port || character>3 || !address(ip,port,addr)){s.error="Enter the host's IPv4 address and port";return false;}
    auto& p=s.peers[0];p.socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(p.socket==INVALID_SOCKET || !nonblock(p.socket)){s.shutdown();return false;}
    const int result=connect(p.socket,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
    if(result && WSAGetLastError()!=WSAEWOULDBLOCK){s.error="Cannot connect to host";s.shutdown();return false;}
    s.build=build;s.key=key;s.starting=true;s.boundPort=port;return true;
}
void Session::close(){state->shutdown();}
void Session::pump() {
    auto& s=*state;
    if(s.hosting) for(unsigned count=0;count<4;++count) {
        const auto incoming=accept(s.listener,nullptr,nullptr);if(incoming==INVALID_SOCKET)break;
        auto free=std::find_if(s.peers.begin(),s.peers.end(),[](const auto& p){return p.socket==INVALID_SOCKET;});
        if(free==s.peers.end() || !nonblock(incoming)){closesocket(incoming);continue;}
        *free=Impl::Peer{};free->socket=incoming;
    }
    if(s.starting && !s.live) {
        fd_set write,fail;FD_ZERO(&write);FD_ZERO(&fail);FD_SET(s.peers[0].socket,&write);FD_SET(s.peers[0].socket,&fail);timeval zero{};
        if(select(0,nullptr,&write,&fail,&zero)>0) {
            if(FD_ISSET(s.peers[0].socket,&fail)){s.error="Connection refused";s.shutdown();return;}
            auto hello=s.packet(Hello);hello[66]=s.preferredCharacter;s.queue(s.peers[0],hello);s.starting=false;
        } else if(Clock::now()-s.peers[0].last>std::chrono::seconds(10)){s.error="Connection timed out";s.shutdown();return;}
        if(s.starting)return;
    }
    for(unsigned i=0;i<s.peers.size();++i)s.io(s.peers[i],i);
    if(s.live && Clock::now()-s.ping>std::chrono::seconds(1)){
        for(auto& p:s.peers)if(p.accepted)s.queue(p,s.packet(Keepalive,s.local));s.ping=Clock::now();
    }
}
bool Session::publish(const World& w){auto& s=*state;if(!s.hosting || !validWorld(w))return false;s.world=w;auto packet=s.packet(Snapshot);encode(packet,w);for(auto& p:s.peers)if(p.accepted)s.queue(p,packet);return true;}
void Session::input(Input value){auto& s=*state;if(!s.live || value.character>3)return;s.inputs[s.local]=value;if(s.hosting)return;auto p=s.packet(Controls,s.local);p[60]=uint8_t(value.buttons>>8);p[61]=uint8_t(value.buttons);p[62]=uint8_t(value.x);p[63]=uint8_t(value.y);p[64]=uint8_t(value.px);p[65]=uint8_t(value.py);p[66]=value.character;s.queue(s.peers[0],p);}
bool Session::present(unsigned slot)const{return slot<4 && state->live && (state->hosting ? slot==0 || state->peers[slot-1].accepted : state->world.players[slot].present);}
bool Session::connected()const{return state->live;}
unsigned Session::slot()const{return state->local;}
unsigned Session::players()const{if(!state->live)return 0;if(!state->hosting){unsigned count=0;for(const auto& p:state->world.players)count+=p.present;return count;}return 1+unsigned(std::count_if(state->peers.begin(),state->peers.end(),[](const auto& p){return p.accepted;}));}
uint16_t Session::port()const{return state->boundPort;}
Input Session::input(unsigned slot)const{return slot<4?state->inputs[slot]:Input{};}
World Session::world()const{return state->world;}
std::string Session::error()const{return state->error;}
}
