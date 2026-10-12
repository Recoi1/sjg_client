#include "lan_lobby.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <cwctype>
#include <stdexcept>
namespace galaxy::lan {
namespace {
using Clock=std::chrono::steady_clock;
using Packet=std::array<uint8_t,80>;
std::string ipText(uint32_t ip) {char text[INET_ADDRSTRLEN]{};in_addr addr{};addr.s_addr=htonl(ip);inet_ntop(AF_INET,&addr,text,sizeof(text));return text;}
uint32_t ipNumber(const std::string& text) {in_addr addr{};if(inet_pton(AF_INET,text.c_str(),&addr)!=1)throw std::invalid_argument("Invalid LAN address");return ntohl(addr.s_addr);}
sockaddr_in address(const std::string& ip,uint16_t port) {sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(port);addr.sin_addr.s_addr=htonl(ipNumber(ip));return addr;}
bool maskValid(uint8_t mask){return mask==3 || mask==15;}
}
Adapter radminAdapter() {
    ULONG bytes=16384;std::vector<uint8_t> storage(bytes);
    auto result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()),&bytes);
    if(result==ERROR_BUFFER_OVERFLOW){if(bytes>1024*1024)return {};storage.resize(bytes);result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()),&bytes);}
    if(result!=NO_ERROR)return {};
    for(auto* adapter=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());adapter;adapter=adapter->Next) {
        if(adapter->OperStatus!=IfOperStatusUp || !adapter->Description)continue;
        std::wstring description=adapter->Description;std::transform(description.begin(),description.end(),description.begin(),::towlower);
        if(description.find(L"radmin")==description.npos)continue;
        for(auto* entry=adapter->FirstUnicastAddress;entry;entry=entry->Next) {
            if(entry->Address.lpSockaddr->sa_family!=AF_INET || entry->OnLinkPrefixLength<1 || entry->OnLinkPrefixLength>30)continue;
            const auto ip=ntohl(reinterpret_cast<sockaddr_in*>(entry->Address.lpSockaddr)->sin_addr.s_addr);
            const uint32_t mask=0xffffffffu<<(32-entry->OnLinkPrefixLength);
            if(!ip || (ip>>24)==127 || (ip>>16)==0xa9fe)continue;
            return {ipText(ip),ipText(ip|~mask),mask};
        }
    }
    return {};
}
Build lobbyBuild(const std::filesystem::path& executable,const std::filesystem::path& mods,uint8_t mask) {
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot initialize lobby fingerprint");
    struct Cleanup {BCRYPT_ALG_HANDLE a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("Cannot create lobby fingerprint");
    auto add=[&](const void* bytes,size_t length){if(BCryptHashData(hash,(PUCHAR)bytes,ULONG(length),0)<0)throw std::runtime_error("Cannot hash lobby build");};
    add(&mask,1);
    std::vector<std::filesystem::path> files{executable};
    if(mask==15)for(const auto& entry:std::filesystem::recursive_directory_iterator(mods))if(entry.is_regular_file())files.push_back(entry.path());
    std::sort(files.begin()+1,files.end());std::array<char,1024*1024> buffer;
    for(const auto& file:files) {
        if(file!=executable){const auto name=file.lexically_relative(mods).generic_string();add(name.c_str(),name.size()+1);}
        const auto length=std::filesystem::file_size(file);add(&length,sizeof(length));
        std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read lobby build file");
        while(input){input.read(buffer.data(),buffer.size());add(buffer.data(),size_t(input.gcount()));}
        if(!input.eof())throw std::runtime_error("Cannot finish lobby build fingerprint");
    }
    Build result{};if(BCryptFinishHash(hash,result.data(),result.size(),0)<0)throw std::runtime_error("Cannot finish lobby fingerprint");return result;
}
struct Discovery::Impl {
    SOCKET socket=INVALID_SOCKET;bool winsock=false,host=false;Adapter adapter;Build build{};uint8_t mask=3,players=1;uint16_t gamePort=0,port=discoveryPort;Key key{};
    std::array<uint8_t,8> nonce{};std::vector<Advertisement> rows;std::string error;Clock::time_point scan{};
    void close(){if(socket!=INVALID_SOCKET)closesocket(socket);socket=INVALID_SOCKET;rows.clear();}
    ~Impl(){close();if(winsock)WSACleanup();}
    Packet packet(uint8_t type){Packet p{};std::copy_n("GLAN",4,p.begin());p[4]=1;p[5]=type;p[6]=mask;std::copy(build.begin(),build.end(),p.begin()+8);return p;}
};
Discovery::Discovery():state(std::make_unique<Impl>()){WSADATA data;state->winsock=WSAStartup(MAKEWORD(2,2),&data)==0;}
Discovery::~Discovery()=default;
void Discovery::close(){state->close();}
bool Discovery::open(const Adapter& adapter,const Build& build,uint8_t mask,bool host,uint16_t port) {
    close();auto& s=*state;s.error.clear();s.host=host;s.adapter=adapter;s.build=build;s.mask=mask;s.port=port;
    try {
        if(!s.winsock || !maskValid(mask) || !port || !adapter.mask)throw std::runtime_error("LAN interface unavailable");
        ipNumber(adapter.ip);ipNumber(adapter.broadcast);
        // Broadcast packets target the subnet address, not the adapter's unicast address.
        const auto addr=address(host?"0.0.0.0":adapter.ip,host?port:0);
        s.socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);u_long nonblocking=1;int enabled=1;
        if(s.socket==INVALID_SOCKET || ioctlsocket(s.socket,FIONBIO,&nonblocking) || setsockopt(s.socket,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&enabled),sizeof(enabled)) || bind(s.socket,reinterpret_cast<const sockaddr*>(&addr),sizeof(addr)))throw std::runtime_error("Cannot open Radmin lobby discovery port");
        if(BCryptGenRandom(nullptr,s.nonce.data(),s.nonce.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot initialize lobby discovery");
        return true;
    }catch(const std::exception& e){s.error=e.what();close();return false;}
}
void Discovery::scan(const char* destination) {
    auto& s=*state;if(s.host || s.socket==INVALID_SOCKET)return;
    auto query=s.packet(1);std::copy(s.nonce.begin(),s.nonce.end(),query.begin()+40);
    const auto target=address(destination?destination:s.adapter.broadcast,s.port);
    if(sendto(s.socket,reinterpret_cast<const char*>(query.data()),query.size(),0,reinterpret_cast<const sockaddr*>(&target),sizeof(target))<0 && WSAGetLastError()!=WSAEWOULDBLOCK)s.error="Cannot scan Radmin lobbies";
    s.scan=Clock::now();
}
void Discovery::advertise(uint16_t port,const Key& key,uint8_t players){state->gamePort=port;state->key=key;state->players=players;}
void Discovery::pump() {
    auto& s=*state;if(s.socket==INVALID_SOCKET)return;
    for(unsigned i=0;i<16;++i) {
        Packet packet; sockaddr_in source{};int length=sizeof(source);
        const int count=recvfrom(s.socket,reinterpret_cast<char*>(packet.data()),packet.size(),0,reinterpret_cast<sockaddr*>(&source),&length);
        if(count<0){if(WSAGetLastError()!=WSAEWOULDBLOCK && WSAGetLastError()!=WSAEMSGSIZE)s.error="Lobby discovery receive failed";break;}
        if(count!=packet.size() || source.sin_family!=AF_INET || std::memcmp(packet.data(),"GLAN",4) || packet[4]!=1 || packet[6]!=s.mask || !std::equal(s.build.begin(),s.build.end(),packet.begin()+8))continue;
        const auto ip=ntohl(source.sin_addr.s_addr);
        if((ip&s.adapter.mask)!=(ipNumber(s.adapter.ip)&s.adapter.mask))continue;
        if(s.host && packet[5]==1 && s.gamePort && s.players<=4) {
            auto response=s.packet(2);std::copy_n(packet.begin()+40,8,response.begin()+40);std::copy(s.key.begin(),s.key.end(),response.begin()+48);
            response[64]=uint8_t(s.gamePort>>8);response[65]=uint8_t(s.gamePort);response[66]=s.players;
            sendto(s.socket,reinterpret_cast<const char*>(response.data()),response.size(),0,reinterpret_cast<const sockaddr*>(&source),sizeof(source));
        } else if(!s.host && packet[5]==2 && std::equal(s.nonce.begin(),s.nonce.end(),packet.begin()+40) && packet[66]>=1 && packet[66]<=4) {
            Advertisement row;row.ip=ipText(ip);row.port=uint16_t(packet[64]<<8|packet[65]);if(!row.port)continue;
            row.players=packet[66];row.characters=s.mask;row.seen=Clock::now();std::copy_n(packet.begin()+48,16,row.key.begin());
            const auto found=std::find_if(s.rows.begin(),s.rows.end(),[&](const auto& old){return old.ip==row.ip;});
            if(found!=s.rows.end())*found=row;else if(s.rows.size()<8)s.rows.push_back(row);
        }
    }
    std::erase_if(s.rows,[](const auto& row){return Clock::now()-row.seen>std::chrono::seconds(5);});
}
const std::vector<Advertisement>& Discovery::rows()const{return state->rows;}
std::string Discovery::error()const{return state->error;}
bool Lobby::open(const Adapter& a,const Build& b,uint8_t mask,uint8_t character) {
    leave();adapter=a;build=b;characters=mask;selected=character;error.clear();
    if(!maskValid(mask) || character>3 || !(mask&(1<<character))){error="Character unavailable";return false;}
    opened=discovery.open(a,b,mask);if(!opened)error=discovery.error();else {discovery.scan();refreshed=Clock::now();}return opened;
}
bool Lobby::host() {
    if(!opened || joining || hosting)return false;
    if(BCryptGenRandom(nullptr,key.data(),key.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){error="Cannot create lobby code";return false;}
    if(!session.host(adapter.ip.c_str(),0,build,key,characters)){error=session.error();return false;}
    if(!discovery.open(adapter,build,characters,true)){error=discovery.error();session.close();discovery.open(adapter,build,characters);return false;}
    hosting=true;published={};return true;
}
bool Lobby::join(unsigned index) {
    if(!opened || hosting || joining || index>=discovery.rows().size())return false;
    const auto row=discovery.rows()[index];if(row.players>=4){error="Lobby is full";return false;}
    joining=session.join(row.ip.c_str(),row.port,build,row.key,selected);if(!joining)error=session.error();return joining;
}
void Lobby::choose(uint8_t value){if(value<4 && characters&(1<<value))selected=value;}
void Lobby::leave(){session.close();discovery.close();opened=hosting=joining=false;tick=0;}
void Lobby::pump() {
    if(!opened)return;const auto now=Clock::now();session.pump();
    if(joining && !session.connected() && !session.error().empty()){error=session.error();joining=false;}
    if(session.connected() && now-published>=std::chrono::milliseconds(50)) {
        session.input(Input{.character=selected});
        if(hosting){World world;world.tick=++tick;world.scene=1;std::copy_n("FileSelect",11,world.stage.begin());
            for(unsigned i=0;i<4;++i){world.players[i].present=session.present(i);world.players[i].character=session.input(i).character;}
            session.publish(world);discovery.advertise(session.port(),key,session.players());}
        published=now;
    }
    discovery.pump();
    if(!hosting && !joining && now-refreshed>=std::chrono::seconds(1)){discovery.scan();refreshed=now;}
}
}
