// Native DHCP implementation linked privately into the Ring 3 service host.
#define MICANT_NATIVE_USERMODE 1
#include <cstdint>
#include <cstddef>
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/afd_socket_abi.hpp"

extern "C" __declspec(dllimport) uintptr_t socket(int,int,int) noexcept;
extern "C" __declspec(dllimport) int closesocket(uintptr_t) noexcept;
extern "C" __declspec(dllimport) int bind(uintptr_t,const void*,int) noexcept;
extern "C" __declspec(dllimport) int sendto(uintptr_t,const char*,int,int,const void*,int) noexcept;
extern "C" __declspec(dllimport) int recvfrom(uintptr_t,char*,int,int,void*,int*) noexcept;
extern "C" __declspec(dllimport) int WSAGetLastError() noexcept;

namespace {
using namespace micant;
using namespace micant::shared::afd;
constexpr uint32_t StatusSuccess=0, StatusInvalidParameter=0xc000000d, StatusAccessDenied=0xc0000022;
constexpr uint32_t StatusTimeout=0x00000102, StatusNetworkUnreachable=0xc000023c;
constexpr uint32_t StatusInvalidHandle=0xc0000008, StatusDeviceNotReady=0xc00000a3;
constexpr uint32_t DhcpIoctlQuery=IoctlQueryNetworkConfiguration, DhcpIoctlSet=IoctlSetNetworkConfiguration;
constexpr uintptr_t InvalidSocket=~uintptr_t{0};
constexpr int AfInet=2,SockDgram=2,IpProtoUdp=17;
constexpr int WsaInvalidArgument=10022,WsaNotSocket=10038,WsaNotInitialised=10093;
constexpr int WsaAddressFamilyNotSupported=10047;
constexpr int WsaFault=10014;
constexpr int WsaTimedOut=10060,SocketError=-1;
struct Wsadata {
    uint16_t version, highVersion;
    uint16_t maxSockets, maxUdpDatagram;
    char* vendorInfo;
    char description[257];
    char status[129];
};
extern "C" __declspec(dllimport) int WSAStartup(uint16_t requestedVersion, Wsadata* data) noexcept;
extern "C" __declspec(dllimport) int WSACleanup() noexcept;
struct DnsRecordA {
    DnsRecordA* next;
    const char* name;
    uint16_t type, dataLength;
    uint32_t flags;
    uint32_t ttl, reserved;
    uint32_t ipv4Address;
};
extern "C" __declspec(dllimport) uint32_t MicantDnsQueryWithTimeout(
    const char* name, uint16_t type, uint32_t timeoutMilliseconds, DnsRecordA** result) noexcept;
extern "C" __declspec(dllimport) void DnsRecordListFree(void* records, uint32_t freeType) noexcept;
struct WinsockCleanup {
    ~WinsockCleanup() { (void)WSACleanup(); }
};
struct SockaddrIn{int16_t family;uint16_t port;uint32_t address;uint8_t zero[8];};
volatile uint32_t g_xid{0x4d4e5401};
uint16_t read16(const uint8_t*p){return uint16_t((uint16_t(p[0])<<8)|p[1]);}
uint32_t read32(const uint8_t*p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
void write16(uint8_t*p,uint16_t v){p[0]=uint8_t(v>>8);p[1]=uint8_t(v);}
void write32(uint8_t*p,uint32_t v){p[0]=uint8_t(v>>24);p[1]=uint8_t(v>>16);p[2]=uint8_t(v>>8);p[3]=uint8_t(v);}
uint16_t hostToNetwork16(uint16_t v){return uint16_t((v<<8)|(v>>8));}
bool sameMac(const uint8_t*a,const uint8_t*b){uint8_t v=0;for(unsigned i=0;i<6;++i)v|=a[i]^b[i];return v==0;}

struct Options{uint8_t message{},mask[4]{},router[4]{},dns[4]{},server[4]{};uint32_t lease{};bool haveMessage{},haveMask{},haveRouter{},haveDns{},haveLease{},haveServer{};};
bool parseOptions(const uint8_t* packet,size_t length,Options& out){
    if(length<240||packet[236]!=99||packet[237]!=130||packet[238]!=83||packet[239]!=99)return false;
    size_t i=240;bool ended=false;
    while(i<length){uint8_t code=packet[i++];if(code==0)continue;if(code==255){ended=true;break;}if(i>=length)return false;uint8_t n=packet[i++];if(n>length-i)return false;
        switch(code){case 53:if(n!=1||out.haveMessage)return false;out.message=packet[i];out.haveMessage=true;break;
        case 1:if(n!=4||out.haveMask)return false;for(unsigned j=0;j<4;++j)out.mask[j]=packet[i+j];out.haveMask=true;break;
        case 3:if(n<4||out.haveRouter)return false;for(unsigned j=0;j<4;++j)out.router[j]=packet[i+j];out.haveRouter=true;break;
        case 6:if(n<4||out.haveDns)return false;for(unsigned j=0;j<4;++j)out.dns[j]=packet[i+j];out.haveDns=true;break;
        case 51:if(n!=4||out.haveLease)return false;out.lease=read32(packet+i);out.haveLease=true;break;
        case 54:if(n!=4||out.haveServer)return false;for(unsigned j=0;j<4;++j)out.server[j]=packet[i+j];out.haveServer=true;break;
        default:break;}i+=n;
    }
    return ended;
}
void setOption(uint8_t* p,size_t& at,uint8_t code,const uint8_t* value,uint8_t n){p[at++]=code;p[at++]=n;for(uint8_t i=0;i<n;++i)p[at++]=value[i];}
bool makePacket(uint8_t* packet,size_t& length,uint8_t type,uint32_t xid,const uint8_t* mac,
                const uint8_t* requested,const uint8_t* server){
    for(size_t i=0;i<576;++i)packet[i]=0;packet[0]=1;packet[1]=1;packet[2]=6;write32(packet+4,xid);write16(packet+10,0x8000);
    for(unsigned i=0;i<6;++i)packet[28+i]=mac[i];packet[236]=99;packet[237]=130;packet[238]=83;packet[239]=99;
    size_t at=240;packet[at++]=53;packet[at++]=1;packet[at++]=type;
    if(type==3){setOption(packet,at,50,requested,4);setOption(packet,at,54,server,4);}
    const uint8_t list[]={1,3,6,51};setOption(packet,at,55,list,sizeof(list));packet[at++]=255;length=at;return at<=576;
}
bool validReply(const uint8_t* p,size_t n,uint32_t xid,const uint8_t* mac,uint32_t source,uint16_t sourcePort,Options& options){
    if(n<240||sourcePort!=67||source==0||p[0]!=2||p[1]!=1||p[2]!=6||read32(p+4)!=xid||!sameMac(p+28,mac))return false;
    if(!parseOptions(p,n,options)||!options.haveMessage||!options.haveServer||
       !(options.server[0]|options.server[1]|options.server[2]|options.server[3]))return false;
    const bool relayed=(p[24]|p[25]|p[26]|p[27])!=0;
    const auto* sourceBytes=reinterpret_cast<const uint8_t*>(&source);
    if(sourceBytes[0]==options.server[0]&&sourceBytes[1]==options.server[1]&&
       sourceBytes[2]==options.server[2]&&sourceBytes[3]==options.server[3])return true;
    return relayed && sourceBytes[0]==p[24]&&sourceBytes[1]==p[25]&&sourceBytes[2]==p[26]&&sourceBytes[3]==p[27];
}
uint32_t transact(uintptr_t socketHandle,const SockaddrIn& broadcast,const uint8_t* mac,uint32_t retry,
                  NetworkConfigurationV1& config,uint32_t* leaseSeconds){
    uint32_t xid=__atomic_add_fetch(&g_xid,0x01020305,__ATOMIC_RELAXED);uint8_t request[576]{},reply[1500]{};size_t requestSize=0;
    if(!makePacket(request,requestSize,1,xid,mac,nullptr,nullptr))return StatusInvalidParameter;
    if(sendto(socketHandle,reinterpret_cast<const char*>(request),requestSize,0,&broadcast,sizeof(broadcast))==SocketError)return StatusNetworkUnreachable;
    SockaddrIn source{};int sourceLength=sizeof(source);int received=recvfrom(socketHandle,reinterpret_cast<char*>(reply),sizeof(reply),0,&source,&sourceLength);
    if(received==SocketError)return WSAGetLastError()==WsaTimedOut?StatusTimeout:StatusNetworkUnreachable;
    Options offer{};if(sourceLength!=sizeof(source)||!validReply(reply,received,xid,mac,source.address,hostToNetwork16(source.port),offer)||offer.message!=2)return StatusNetworkUnreachable;
    uint8_t offeredAddress[4]{};for(unsigned i=0;i<4;++i)offeredAddress[i]=reply[16+i];
    if(!(offeredAddress[0]|offeredAddress[1]|offeredAddress[2]|offeredAddress[3]))return StatusNetworkUnreachable;
    if(!makePacket(request,requestSize,3,xid,mac,offeredAddress,offer.server))return StatusInvalidParameter;
    if(sendto(socketHandle,reinterpret_cast<const char*>(request),requestSize,0,&broadcast,sizeof(broadcast))==SocketError)return StatusNetworkUnreachable;
    source={};sourceLength=sizeof(source);received=recvfrom(socketHandle,reinterpret_cast<char*>(reply),sizeof(reply),0,&source,&sourceLength);
    if(received==SocketError)return WSAGetLastError()==WsaTimedOut?StatusTimeout:StatusNetworkUnreachable;
    Options ack{};if(sourceLength!=sizeof(source)||!validReply(reply,received,xid,mac,source.address,hostToNetwork16(source.port),ack))return StatusNetworkUnreachable;
    if(ack.message==6)return StatusNetworkUnreachable;
    if(ack.message!=5||!ack.haveLease||!ack.lease||!ack.haveMask||
       (ack.server[0]!=offer.server[0]||ack.server[1]!=offer.server[1]||ack.server[2]!=offer.server[2]||ack.server[3]!=offer.server[3]))return StatusNetworkUnreachable;
    uint8_t* yiaddr=reply+16;if(!(yiaddr[0]|yiaddr[1]|yiaddr[2]|yiaddr[3])||
        yiaddr[0]!=offeredAddress[0]||yiaddr[1]!=offeredAddress[1]||yiaddr[2]!=offeredAddress[2]||yiaddr[3]!=offeredAddress[3])return StatusNetworkUnreachable;
    uint32_t maskBits=read32(ack.mask);uint32_t inverse=~maskBits;
    if(!maskBits||(inverse&(inverse+1))!=0)return StatusNetworkUnreachable;
    if(ack.haveRouter){const uint32_t ipBits=read32(yiaddr),routerBits=read32(ack.router);
        if((ipBits&maskBits)!=(routerBits&maskBits))return StatusNetworkUnreachable;}
    NetworkConfigurationV1 update=config;update.Size=sizeof(update);update.Version=EndpointAbiVersion;update.Flags=NetworkConfigurationSet;
    for(unsigned i=0;i<4;++i){reinterpret_cast<uint8_t*>(&update.Ipv4AddressNetworkOrder)[i]=yiaddr[i];reinterpret_cast<uint8_t*>(&update.SubnetMaskNetworkOrder)[i]=ack.mask[i];
        reinterpret_cast<uint8_t*>(&update.GatewayNetworkOrder)[i]=ack.haveRouter?ack.router[i]:0;
        reinterpret_cast<uint8_t*>(&update.DnsServerNetworkOrder)[i]=ack.haveDns?ack.dns[i]:0;}
    IoStatusBlock iosb{};const auto status=native_nt::NtDeviceIoControlFile(static_cast<Handle>(socketHandle),0,nullptr,nullptr,&iosb,
        DhcpIoctlSet,&update,sizeof(update),nullptr,0);
    if(status!=NtStatus::Success)return static_cast<uint32_t>(status);
    if(leaseSeconds)*leaseSeconds=ack.lease;(void)retry;return StatusSuccess;
}
}

namespace micant::services::dhcp {
uint32_t acquireLease(uint32_t timeoutMilliseconds,uint32_t* leaseSeconds) noexcept {
    if(timeoutMilliseconds==0||timeoutMilliseconds>60000)return StatusInvalidParameter;
    Wsadata startupData{};
    if (WSAStartup(0x0202, &startupData) != 0) return StatusNetworkUnreachable;
    WinsockCleanup cleanup{};
    uintptr_t endpoint=socket(AfInet,SockDgram,IpProtoUdp);if(endpoint==InvalidSocket)return StatusNetworkUnreachable;
    NetworkConfigurationV1 config{};IoStatusBlock iosb{};
    auto status=native_nt::NtDeviceIoControlFile(static_cast<Handle>(endpoint),0,nullptr,nullptr,&iosb,
        DhcpIoctlQuery,nullptr,0,&config,sizeof(config));
    if(status!=NtStatus::Success||iosb.information!=sizeof(config)||config.Size!=sizeof(config)||
       config.Version!=EndpointAbiVersion){closesocket(endpoint);return static_cast<uint32_t>(status==NtStatus::Success?NtStatus::Unsuccessful:status);}
    SockaddrIn any{AfInet,hostToNetwork16(68),0,{}};
    if(bind(endpoint,&any,sizeof(any))==SocketError){
        const int error=WSAGetLastError();
        closesocket(endpoint);
        if(error==WsaInvalidArgument)return StatusInvalidParameter;
        if(error==WsaFault)return 0xc0000005u;
        if(error==WsaAddressFamilyNotSupported)return 0xc00000bbu;
        if(error==WsaNotSocket)return StatusInvalidHandle;
        if(error==WsaNotInitialised)return StatusDeviceNotReady;
        return StatusNetworkUnreachable;
    }
    uint32_t timeout=timeoutMilliseconds;
    status=native_nt::NtDeviceIoControlFile(static_cast<Handle>(endpoint),0,nullptr,nullptr,&iosb,
        shared::afd::IoctlSetReceiveTimeout,&timeout,sizeof(timeout),nullptr,0);
    if(status!=NtStatus::Success){closesocket(endpoint);return static_cast<uint32_t>(status);}
    SockaddrIn broadcast{AfInet,hostToNetwork16(67),0xffffffffu,{}};
    uint32_t lease=0;uint32_t result=StatusTimeout;
    for(unsigned attempt=0;attempt<3;++attempt){result=transact(endpoint,broadcast,config.MacAddress,attempt,config,&lease);if(result==StatusSuccess||result==StatusAccessDenied)break;}
    closesocket(endpoint);if(result==StatusSuccess&&leaseSeconds)*leaseSeconds=lease;return result;
}

uint32_t resolveBootHost() noexcept {
    DnsRecordA* records{};
    const uint32_t status = MicantDnsQueryWithTimeout("example.com", 1, 5000, &records);
    bool found = false;
    for (auto* record = records; record; record = record->next) {
        if (record->type == 1 && record->dataLength == sizeof(uint32_t) && record->ipv4Address)
            found = true;
    }
    DnsRecordListFree(records, 0);
    return status == 0 && found ? StatusSuccess : StatusNetworkUnreachable;
}

}
