// Bounded native DNS client. Configuration and datagrams flow through the
// public NTDLL/AFD boundary; no kernel Executive headers are included.
#define MICANT_NATIVE_USERMODE 1
#include <cstdint>
#include <cstddef>
#include <string_view>
#include "micant/shared/native_nt_api.hpp"
#include "micant/shared/afd_socket_abi.hpp"

extern "C" __declspec(dllimport) uintptr_t socket(int, int, int) noexcept;
extern "C" __declspec(dllimport) int closesocket(uintptr_t) noexcept;
extern "C" __declspec(dllimport) int setsockopt(uintptr_t, int, int, const char*, int) noexcept;
extern "C" __declspec(dllimport) int sendto(uintptr_t, const char*, int, int, const void*, int) noexcept;
extern "C" __declspec(dllimport) int recvfrom(uintptr_t, char*, int, int, void*, int*) noexcept;
extern "C" __declspec(dllimport) int send(uintptr_t, const char*, int, int) noexcept;
extern "C" __declspec(dllimport) int recv(uintptr_t, char*, int, int) noexcept;
extern "C" __declspec(dllimport) int connect(uintptr_t, const void*, int) noexcept;
extern "C" __declspec(dllimport) int WSAGetLastError() noexcept;
struct DnsWsadata {
    uint16_t version, highVersion, maxSockets, maxUdpDatagram;
    char* vendorInfo;
    char description[257], status[129];
};
extern "C" __declspec(dllimport) int WSAStartup(uint16_t, DnsWsadata*) noexcept;
extern "C" __declspec(dllimport) int WSACleanup() noexcept;

namespace {
using namespace micant;
using namespace micant::shared::afd;
constexpr uint16_t DnsA = 1, DnsAaaa = 28, DnsIn = 1;
constexpr uint32_t DnsSuccess = 0, DnsNameError = 9003, DnsServerFailure = 9002;
constexpr uint32_t DnsNoRecords = 9501;
constexpr uint32_t DnsInvalidParameter = 87, DnsInvalidName = 9005, DnsNotSupported = 50;
constexpr uint32_t DnsNoMemory = 14, DnsTimeout = 1460;
constexpr int AfInet = 2, SockStream = 1, SockDgram = 2, IpProtoTcp = 6, IpProtoUdp = 17;
constexpr int SolSocket = 0xffff, SoReceiveTimeout = 0x1006, SocketError = -1;
constexpr int SoSendTimeout = 0x1005;
constexpr uintptr_t InvalidSocket = ~uintptr_t{0};
constexpr int WsaTimedOut = 10060;
constexpr int WsaMessageTooLong = 10040;
struct SockaddrIn { int16_t family; uint16_t port; uint32_t address; uint8_t zero[8]; };
struct RecordFlags { uint32_t section:2; uint32_t deleted:1; uint32_t charset:2; uint32_t unused:3; uint32_t reserved:24; };
struct AData { uint32_t address; };
struct AaaaData { uint8_t address[16]; };
struct RecordA { RecordA* next; const char* name; uint16_t type, dataLength; RecordFlags flags; uint32_t ttl, reserved; union { AData a; AaaaData aaaa; } data; };
struct RecordW { RecordW* next; const wchar_t* name; uint16_t type, dataLength; RecordFlags flags; uint32_t ttl, reserved; union { AData a; AaaaData aaaa; } data; };
struct Slot { bool used, wide; char ansiName[256]; wchar_t wideName[256]; alignas(8) uint8_t publicRecord[sizeof(RecordW)]; };
Slot g_slots[64]{};
volatile uint8_t g_slotLock{};
uint16_t g_transactionId{};

struct Deadline {
    uint64_t start{}, frequency{};
    uint32_t milliseconds{};
    bool enabled{};
    bool initialize(uint32_t timeout) noexcept {
        if (!timeout) return true;
        LargeInteger counter{}, rate{};
        if (native_nt::NtQueryPerformanceCounter(&counter, &rate) != NtStatus::Success ||
            counter.QuadPart < 0 || rate.QuadPart <= 0 ||
            static_cast<uint64_t>(rate.QuadPart) > UINT64_MAX / 1000) return false;
        start = static_cast<uint64_t>(counter.QuadPart);
        frequency = static_cast<uint64_t>(rate.QuadPart);
        milliseconds = timeout; enabled = true; return true;
    }
    bool remaining(uint32_t& value) const noexcept {
        if (!enabled) { value = 0; return true; }
        LargeInteger counter{};
        if (native_nt::NtQueryPerformanceCounter(&counter, nullptr) != NtStatus::Success ||
            counter.QuadPart < 0 || static_cast<uint64_t>(counter.QuadPart) < start) return false;
        const uint64_t delta = static_cast<uint64_t>(counter.QuadPart) - start;
        const uint64_t seconds = delta / frequency;
        if (seconds > milliseconds / 1000) return false;
        const uint64_t elapsed = seconds * 1000 + (delta % frequency) * 1000 / frequency;
        if (elapsed >= milliseconds) return false;
        value = static_cast<uint32_t>(milliseconds - elapsed); return true;
    }
};

bool setDeadline(uintptr_t socket, Deadline* deadline, int option) noexcept {
    if (!deadline) return true;
    uint32_t remaining{};
    return deadline->remaining(remaining) &&
        setsockopt(socket, SolSocket, option, reinterpret_cast<const char*>(&remaining), sizeof(remaining)) == 0;
}
bool setConnectDeadline(uintptr_t socket, Deadline* deadline) noexcept {
    if (!deadline) return true;
    uint32_t remaining{}; IoStatusBlock iosb{};
    return deadline->remaining(remaining) && native_nt::NtDeviceIoControlFile(
        static_cast<Handle>(socket), 0, nullptr, nullptr, &iosb, IoctlSetConnectTimeout,
        &remaining, sizeof(remaining), nullptr, 0) == NtStatus::Success;
}

void* copy(void* out, const void* in, size_t size) noexcept {
    auto* d = static_cast<uint8_t*>(out); auto* s = static_cast<const uint8_t*>(in);
    for (size_t i = 0; i < size; ++i) d[i] = s[i]; return out;
}
void clear(void* out, size_t size) noexcept { auto* p = static_cast<uint8_t*>(out); for (size_t i=0;i<size;++i)p[i]=0; }
size_t length(const char* s) noexcept { size_t n=0; if(s)while(s[n])++n; return n; }
bool equalNoCase(const char* a, const char* b) noexcept {
    size_t i=0; while(a[i]&&b[i]) { char x=a[i], y=b[i]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return false; ++i; } return a[i]==b[i];
}
uint16_t read16(const uint8_t* p) noexcept { return static_cast<uint16_t>((p[0]<<8)|p[1]); }
uint32_t read32(const uint8_t* p) noexcept { return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3]; }
void write16(uint8_t* p,uint16_t v) noexcept { p[0]=uint8_t(v>>8);p[1]=uint8_t(v); }

bool encodeName(const char* name, uint8_t* out, size_t capacity, size_t& used) noexcept {
    used=0; size_t n=length(name); if(!n||n>254)return false; if(name[n-1]=='.')--n; if(!n)return false;
    size_t start=0;
    while(start<n) {
        size_t end=start; while(end<n&&name[end]!='.')++end; size_t label=end-start;
        if(!label||label>63||used+1+label+1>capacity)return false;
        out[used++]=static_cast<uint8_t>(label);
        for(size_t i=start;i<end;++i) { unsigned char c=static_cast<unsigned char>(name[i]); if(c<=32||c>=127)return false; out[used++]=c; }
        start=end+1;
    }
    out[used++]=0; return true;
}
bool decodeName(const uint8_t* packet,size_t packetSize,size_t& offset,char* out,size_t capacity) noexcept {
    size_t cursor=offset,next=0,used=0,steps=0;bool jumped=false;
    while(cursor<packetSize&&steps++<128) {
        uint8_t n=packet[cursor++];
        if(!n) { offset=jumped?next:cursor; if(used>=capacity)return false;out[used]=0;return true; }
        if((n&0xc0)==0xc0) { if(cursor>=packetSize)return false;uint16_t ptr=((n&0x3f)<<8)|packet[cursor++];if(ptr>=cursor-2)return false;if(!jumped){next=cursor;jumped=true;}cursor=ptr;continue; }
        if((n&0xc0)||n>63||n>packetSize-cursor)return false;
        if(used&&used+1>=capacity)return false;if(used)out[used++]='.';
        if(n>capacity-used-1)return false;
        for(uint8_t i=0;i<n;++i){uint8_t c=packet[cursor++];if(c<=32||c>=127)return false;out[used++]=static_cast<char>(c);}
    }
    return false;
}
bool validateQuestion(const uint8_t* packet, size_t packetSize, const char* name,
                      uint16_t qtype) noexcept {
    if (!packet || packetSize < 12 || read16(packet + 4) != 1) return false;
    size_t offset = 12;
    char question[256]{};
    return decodeName(packet, packetSize, offset, question, sizeof(question)) &&
        offset <= packetSize && packetSize - offset >= 4 &&
        read16(packet + offset) == qtype && read16(packet + offset + 2) == DnsIn &&
        equalNoCase(question, name);
}
bool acquire(volatile uint8_t* lock) noexcept { uint8_t expected=0;return __atomic_compare_exchange_n(lock,&expected,1,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED); }
void release(volatile uint8_t* lock) noexcept { __atomic_store_n(lock,0,__ATOMIC_RELEASE); }

bool queryConfiguration(uintptr_t endpoint, NetworkConfigurationV1& config) noexcept {
    IoStatusBlock iosb{}; clear(&config,sizeof(config));
    return native_nt::NtDeviceIoControlFile(static_cast<Handle>(endpoint),0,nullptr,nullptr,&iosb,
        IoctlQueryNetworkConfiguration,nullptr,0,&config,sizeof(config))==NtStatus::Success &&
        iosb.information==sizeof(config)&&config.Size==sizeof(config)&&config.Version==EndpointAbiVersion&&
        (config.Flags&NetworkConfigurationDnsValid)&&config.DnsServerNetworkOrder!=0;
}

uint32_t performQuery(const char* name,uint16_t qtype,uint8_t* packet,size_t& packetSize,Deadline* deadline) noexcept {
    const size_t packetCapacity=packetSize;
    DnsWsadata socketData{};
    if (WSAStartup(0x0202, &socketData) != 0) return DnsServerFailure;
    struct WinsockLease { ~WinsockLease() { (void)WSACleanup(); } } winsockLease;
    uint32_t result=DnsServerFailure;
    uintptr_t udp=socket(AfInet,SockDgram,IpProtoUdp);
    if(udp==InvalidSocket){return DnsServerFailure;}
    NetworkConfigurationV1 config{};
    if(!queryConfiguration(udp,config)){closesocket(udp);return DnsServerFailure;}
    uint32_t timeout=2500;
    if(setsockopt(udp,SolSocket,SoReceiveTimeout,reinterpret_cast<const char*>(&timeout),sizeof(timeout))!=0){closesocket(udp);return DnsServerFailure;}
    uint8_t query[512]{};size_t encoded=0;
    if(!encodeName(name,query+12,sizeof(query)-16,encoded)){closesocket(udp);return DnsInvalidName;}
    uint16_t id=static_cast<uint16_t>(__atomic_add_fetch(&g_transactionId,0x9e37,__ATOMIC_RELAXED));
    write16(query,id);write16(query+2,0x0100);write16(query+4,1);
    write16(query+12+encoded,qtype);write16(query+14+encoded,DnsIn);
    const size_t querySize=12+encoded+4;
    SockaddrIn server{AfInet,static_cast<uint16_t>((53u<<8)|(53u>>8)),config.DnsServerNetworkOrder,{}};
    if(!setDeadline(udp,deadline,SoSendTimeout)){closesocket(udp);return DnsTimeout;}
    if(sendto(udp,reinterpret_cast<const char*>(query),querySize,0,&server,sizeof(server))!=static_cast<int>(querySize)){
        const int error=WSAGetLastError();closesocket(udp);return error==WsaTimedOut?DnsTimeout:DnsServerFailure;}
    if(!setDeadline(udp,deadline,SoReceiveTimeout)){closesocket(udp);return DnsTimeout;}
    SockaddrIn source{};int sourceLength=sizeof(source);
    int got=recvfrom(udp,reinterpret_cast<char*>(packet),static_cast<int>(packetSize),0,&source,&sourceLength);
    const int receiveError=got==SocketError?WSAGetLastError():0;
    closesocket(udp);
    const bool datagramTruncated = got==SocketError && receiveError==WsaMessageTooLong;
    if(got==SocketError&&!datagramTruncated){result=receiveError==WsaTimedOut?DnsTimeout:DnsServerFailure;return result;}
    if(sourceLength!=sizeof(source)||source.family!=AfInet||source.address!=server.address||source.port!=server.port||
       (datagramTruncated ? packetCapacity<12 : got<12)){return DnsServerFailure;}
    packetSize=datagramTruncated?packetCapacity:static_cast<size_t>(got);
    if(read16(packet)!=id||(read16(packet+2)&0x8000)==0||(read16(packet+2)&0x7800)!=0||read16(packet+4)!=1)
        return DnsServerFailure;
    if(datagramTruncated || (read16(packet+2)&0x0200)) {
        uintptr_t tcp=socket(AfInet,SockStream,IpProtoTcp);
        if(tcp==InvalidSocket){return DnsServerFailure;}
        timeout=5000;
        if(setsockopt(tcp,SolSocket,SoReceiveTimeout,reinterpret_cast<const char*>(&timeout),sizeof(timeout))!=0){closesocket(tcp);return DnsServerFailure;}
        if(!setConnectDeadline(tcp,deadline)){closesocket(tcp);return DnsTimeout;}
        if(connect(tcp,&server,sizeof(server))==SocketError){
            const int error=WSAGetLastError();closesocket(tcp);return error==WsaTimedOut?DnsTimeout:DnsServerFailure;}
        uint8_t framed[514]{};write16(framed,static_cast<uint16_t>(querySize));copy(framed+2,query,querySize);
        size_t sent=0;while(sent<querySize+2){if(!setDeadline(tcp,deadline,SoSendTimeout)){closesocket(tcp);return DnsTimeout;}int n=send(tcp,reinterpret_cast<const char*>(framed+sent),static_cast<int>(querySize+2-sent),0);if(n<=0){closesocket(tcp);return DnsServerFailure;}sent+=n;}
        uint8_t prefix[2]{};size_t prefixRead=0;while(prefixRead<2){if(!setDeadline(tcp,deadline,SoReceiveTimeout)){closesocket(tcp);return DnsTimeout;}int n=recv(tcp,reinterpret_cast<char*>(prefix+prefixRead),2-prefixRead,0);if(n<=0){closesocket(tcp);return DnsServerFailure;}prefixRead+=n;}
        size_t message=read16(prefix);if(message<12||message>packetCapacity){closesocket(tcp);return DnsServerFailure;}
        size_t read=0;while(read<message){if(!setDeadline(tcp,deadline,SoReceiveTimeout)){closesocket(tcp);return DnsTimeout;}int n=recv(tcp,reinterpret_cast<char*>(packet+read),static_cast<int>(message-read),0);if(n<=0){closesocket(tcp);return DnsServerFailure;}read+=n;}
        closesocket(tcp);
        packetSize=message;
    }
    const uint16_t responseFlags = read16(packet + 2);
    if (read16(packet) != id || (responseFlags & 0x8000) == 0 ||
        (responseFlags & 0x7a00) != 0 || (responseFlags & 0x0040) != 0 ||
        !validateQuestion(packet, packetSize, name, qtype)) return DnsServerFailure;
    uint8_t rcode=read16(packet+2)&0xf;
    if(rcode==3){return DnsNameError;}
    if(rcode){return DnsServerFailure;}
    return DnsSuccess;
}

Slot* allocateSlot(bool wide) noexcept {
    if(!acquire(&g_slotLock))return nullptr;
    for(auto& slot:g_slots)if(!slot.used){slot.used=true;slot.wide=wide;slot.ansiName[0]=0;slot.wideName[0]=0;clear(slot.publicRecord,sizeof(slot.publicRecord));release(&g_slotLock);return &slot;}
    release(&g_slotLock);return nullptr;
}
void releaseSlot(Slot* slot) noexcept { if(!slot)return;while(!acquire(&g_slotLock)){}slot->used=false;release(&g_slotLock); }

bool hostsIpv4(std::string_view requested, uint32_t& address) noexcept {
    const auto* shared = reinterpret_cast<const volatile KUserSharedData*>(UserSharedDataAddress);
    char16_t drive = __atomic_load_n(&shared->ntSystemRoot[0], __ATOMIC_ACQUIRE);
    if (drive >= u'a' && drive <= u'z') drive = static_cast<char16_t>(drive - (u'a' - u'A'));
    if (drive < u'A' || drive > u'Z') return false;
    wchar_t path[] = L"X:\\MicaNT\\System32\\drivers\\etc\\hosts";
    path[0] = static_cast<wchar_t>(drive);
    UnicodeString name{path};
    ObjectAttributes attributes{}; attributes.objectName = &name; attributes.attributes = OBJ_CASE_INSENSITIVE;
    Handle file{}; IoStatusBlock status{};
    constexpr uint32_t ReadAccess = 0x00100001u;
    const auto openStatus = native_nt::NtCreateFile(&file, ReadAccess, &attributes, &status, nullptr,
        0, 0x7u, 1u, 0x20u, nullptr, 0);
    if (openStatus != NtStatus::Success) return false;
    char contents[8192]{};
    LargeInteger offset{};
    const auto readStatus = native_nt::NtReadFile(file, 0, nullptr, nullptr, &status,
        contents, sizeof(contents), &offset, nullptr);
    (void)native_nt::NtClose(file);
    if (readStatus != NtStatus::Success && readStatus != NtStatus::EndOfFile) return false;
    size_t cursor{};
    const size_t bytes = status.information <= sizeof(contents) ? status.information : sizeof(contents);
    while (cursor < bytes) {
        while (cursor < bytes && (contents[cursor] == ' ' || contents[cursor] == '\t' ||
               contents[cursor] == '\r' || contents[cursor] == '\n')) ++cursor;
        if (cursor == bytes) break;
        if (contents[cursor] == '#') {
            while (cursor < bytes && contents[cursor] != '\n') ++cursor;
            continue;
        }
        uint32_t octets[4]{};
        bool valid = true;
        for (size_t part = 0; part < 4; ++part) {
            if (cursor == bytes || contents[cursor] < '0' || contents[cursor] > '9') { valid = false; break; }
            size_t digits{};
            while (cursor < bytes && contents[cursor] >= '0' && contents[cursor] <= '9') {
                const uint32_t digit = static_cast<uint32_t>(contents[cursor++] - '0');
                if (octets[part] > (255u - digit) / 10u) { valid = false; break; }
                octets[part] = octets[part] * 10u + digit;
                if (++digits > 3) { valid = false; break; }
            }
            if (!valid || (part != 3 && (cursor == bytes || contents[cursor++] != '.'))) { valid = false; break; }
        }
        if (!valid) { while (cursor < bytes && contents[cursor] != '\n') ++cursor; continue; }
        address = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);
        bool matched = false;
        while (cursor < bytes && contents[cursor] != '\n' && contents[cursor] != '#') {
            while (cursor < bytes && (contents[cursor] == ' ' || contents[cursor] == '\t')) ++cursor;
            const size_t begin = cursor;
            while (cursor < bytes && contents[cursor] != ' ' && contents[cursor] != '\t' &&
                   contents[cursor] != '\r' && contents[cursor] != '\n' && contents[cursor] != '#') ++cursor;
            if (cursor - begin == requested.size() && cursor > begin) {
                bool same = true;
                for (size_t i = 0; i < requested.size(); ++i) {
                    char left = contents[begin + i], right = requested[i];
                    if (left >= 'A' && left <= 'Z') left = static_cast<char>(left + ('a' - 'A'));
                    if (right >= 'A' && right <= 'Z') right = static_cast<char>(right + ('a' - 'A'));
                    if (left != right) { same = false; break; }
                }
                if (same) matched = true;
            }
        }
        if (matched) return true;
        while (cursor < bytes && contents[cursor] != '\n') ++cursor;
    }
    return false;
}

bool resolveHosts(const char* name, uint16_t qtype, bool wide, void** output) noexcept {
    if (qtype != DnsA || !name || !output) return false;
    uint32_t address{};
    if (!hostsIpv4(name, address)) return false;
    Slot* slot = allocateSlot(wide);
    if (!slot) return false;
    const size_t n = length(name);
    if (n >= 256) { releaseSlot(slot); return false; }
    if (wide) {
        auto* record = reinterpret_cast<RecordW*>(slot->publicRecord);
        record->name = slot->wideName; record->type = DnsA; record->ttl = 60;
        record->dataLength = sizeof(address); record->data.a.address = address;
        for (size_t i = 0; i <= n; ++i) slot->wideName[i] = static_cast<unsigned char>(name[i]);
        *output = record;
    } else {
        auto* record = reinterpret_cast<RecordA*>(slot->publicRecord);
        record->name = slot->ansiName; record->type = DnsA; record->ttl = 60;
        record->dataLength = sizeof(address); record->data.a.address = address;
        copy(slot->ansiName, name, n + 1);
        *output = record;
    }
    return true;
}

uint32_t query(const char* name,uint16_t qtype,bool wide,void** output,Deadline* deadline = nullptr) noexcept {
    if (!name || !output) return DnsInvalidParameter;
    *output = nullptr;
    if (qtype != DnsA && qtype != DnsAaaa) return DnsNotSupported;
    size_t initialLength = length(name);
    if (initialLength && name[initialLength - 1] == '.') --initialLength;
    if (!initialLength || initialLength >= 256) return DnsInvalidName;
    if (resolveHosts(name, qtype, wide, output)) return DnsSuccess;
    char current[256]{};
    copy(current, name, initialLength);
    char visited[9][256]{};
    copy(visited[0], current, initialLength + 1);
    size_t visitedCount = 1;
    uint32_t chainTtl = UINT32_MAX;
    for (size_t queryRound = 0; queryRound < 9; ++queryRound) {
        uint8_t packet[4096]{};
        size_t packetSize = sizeof(packet);
        uint32_t status = performQuery(current, qtype, packet, packetSize, deadline);
        if (status) return status;
        size_t offset = 12;
        char question[256]{};
        if (!decodeName(packet, packetSize, offset, question, sizeof(question)) ||
            offset + 4 > packetSize || read16(packet + offset) != qtype ||
            read16(packet + offset + 2) != DnsIn || !equalNoCase(question, current))
            return DnsServerFailure;
        const size_t answerStart = offset + 4;
        const uint16_t answers = read16(packet + 6);
        bool followedAlias = false;
        // Resolve aliases from the answer section regardless of RR ordering.
        // Bound the chain and reject cycles before issuing another query.
        for (;;) {
            size_t cursor = answerStart;
            char target[256]{};
            bool found = false;
            for (uint16_t i = 0; i < answers; ++i) {
                char owner[256]{};
                if (!decodeName(packet, packetSize, cursor, owner, sizeof(owner)) ||
                    cursor + 10 > packetSize) return DnsServerFailure;
                const uint16_t type = read16(packet + cursor), klass = read16(packet + cursor + 2);
                const uint32_t ttl = read32(packet + cursor + 4);
                const uint16_t rdlength = read16(packet + cursor + 8);
                cursor += 10;
                if (rdlength > packetSize - cursor) return DnsServerFailure;
                if (type == 5 && klass == DnsIn && equalNoCase(owner, current)) {
                    size_t nameEnd = cursor;
                    char candidate[256]{};
                    if (!decodeName(packet, packetSize, nameEnd, candidate, sizeof(candidate)) ||
                        nameEnd != cursor + rdlength || !candidate[0] ||
                        (found && !equalNoCase(target, candidate))) return DnsServerFailure;
                    copy(target, candidate, length(candidate) + 1);
                    if (ttl < chainTtl) chainTtl = ttl;
                    found = true;
                }
                cursor += rdlength;
            }
            if (!found) break;
            for (size_t i = 0; i < visitedCount; ++i)
                if (equalNoCase(visited[i], target)) return DnsServerFailure;
            if (visitedCount == 9) return DnsServerFailure;
            copy(visited[visitedCount++], target, length(target) + 1);
            copy(current, target, length(target) + 1);
            followedAlias = true;
        }
        Slot* head = nullptr;
        Slot* tail = nullptr;
        offset = answerStart;
        for (uint16_t i = 0; i < answers; ++i) {
            char owner[256]{};
            if (!decodeName(packet, packetSize, offset, owner, sizeof(owner)) || offset + 10 > packetSize) {
                status = DnsServerFailure; break;
            }
            const uint16_t type = read16(packet + offset), klass = read16(packet + offset + 2);
            const uint16_t rdlength = read16(packet + offset + 8);
            const uint32_t recordTtl = read32(packet + offset + 4);
            const uint32_t ttl = recordTtl < chainTtl ? recordTtl : chainTtl;
            offset += 10;
            if (rdlength > packetSize - offset) { status = DnsServerFailure; break; }
            if (type == qtype && klass == DnsIn && equalNoCase(owner, current) &&
                ((qtype == DnsA && rdlength == 4) || (qtype == DnsAaaa && rdlength == 16))) {
                Slot* slot = allocateSlot(wide);
                if (!slot) { status = DnsNoMemory; break; }
                const size_t n = length(owner);
                if (n >= 256) { releaseSlot(slot); status = DnsServerFailure; break; }
                if (wide) {
                    auto* r = reinterpret_cast<RecordW*>(slot->publicRecord);
                    r->name = slot->wideName; r->type = type; r->ttl = ttl; r->dataLength = rdlength;
                    if (qtype == DnsA) copy(&r->data.a.address, packet + offset, 4);
                    else copy(r->data.aaaa.address, packet + offset, 16);
                    for (size_t j = 0; j <= n; ++j) slot->wideName[j] = static_cast<unsigned char>(owner[j]);
                } else {
                    auto* r = reinterpret_cast<RecordA*>(slot->publicRecord);
                    r->name = slot->ansiName; r->type = type; r->ttl = ttl; r->dataLength = rdlength;
                    if (qtype == DnsA) copy(&r->data.a.address, packet + offset, 4);
                    else copy(r->data.aaaa.address, packet + offset, 16);
                    copy(slot->ansiName, owner, n + 1);
                }
                if (tail) {
                    if (wide) reinterpret_cast<RecordW*>(tail->publicRecord)->next = reinterpret_cast<RecordW*>(slot->publicRecord);
                    else reinterpret_cast<RecordA*>(tail->publicRecord)->next = reinterpret_cast<RecordA*>(slot->publicRecord);
                } else head = slot;
                tail = slot;
            }
            offset += rdlength;
        }
        if (status || !head) {
            for (Slot* slot = head; slot;) {
                void* next = wide
                    ? static_cast<void*>(reinterpret_cast<RecordW*>(slot->publicRecord)->next)
                    : static_cast<void*>(reinterpret_cast<RecordA*>(slot->publicRecord)->next);
                releaseSlot(slot);
                slot = nullptr;
                if (next) for (auto& candidate : g_slots)
                    if (candidate.publicRecord == reinterpret_cast<uint8_t*>(next)) { slot = &candidate; break; }
            }
            if (status) return status;
            if (!followedAlias) return DnsNoRecords;
            continue;
        }
        *output = head->publicRecord;
        return DnsSuccess;
    }
    return DnsServerFailure;
}
}

extern "C" {
__declspec(dllexport) uint32_t MicantDnsQueryWithTimeout(const char* name,uint16_t type,uint32_t timeout,RecordA** result) noexcept {
    if(result)*result=nullptr;
    Deadline deadline{};
    if(!deadline.initialize(timeout))return DnsServerFailure;
    return query(name,type,false,reinterpret_cast<void**>(result),&deadline);
}
__declspec(dllexport) uint32_t DnsQuery_A(const char* name,uint16_t type,uint32_t options,void* extra,RecordA** result,void** reserved) noexcept {
    if(result)*result=nullptr;if(options||extra||reserved)return DnsNotSupported;return query(name,type,false,reinterpret_cast<void**>(result));
}
__declspec(dllexport) uint32_t DnsQuery_W(const wchar_t* name,uint16_t type,uint32_t options,void* extra,RecordW** result,void** reserved) noexcept {
    if(result)*result=nullptr;if(!name||!result||extra||reserved)return DnsInvalidParameter;if(options)return DnsNotSupported;char narrow[256]{};size_t i=0;
    for(;name[i]&&i<255;++i){if(static_cast<uint32_t>(name[i])>0x7f)return DnsNotSupported;narrow[i]=static_cast<char>(name[i]);}
    if(name[i])return DnsInvalidName;narrow[i]=0;return query(narrow,type,true,reinterpret_cast<void**>(result));
}
__declspec(dllexport) void DnsRecordListFree(void* list,uint32_t) noexcept {
    while(list){void* next=*reinterpret_cast<void**>(list);for(auto& slot:g_slots)if(slot.used&&slot.publicRecord==list){releaseSlot(&slot);break;}list=next;}
}
bool DllMain(void*,unsigned long,void*) noexcept{return true;}
void* memcpy(void* d,const void* s,size_t n){return copy(d,s,n);}
void* memset(void* d,int c,size_t n){auto* p=static_cast<uint8_t*>(d);for(size_t i=0;i<n;++i)p[i]=static_cast<uint8_t>(c);return d;}
}
