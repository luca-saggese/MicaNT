#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"

#include <cstddef>
#include <cstdint>

using namespace micant;

namespace {
constexpr uint32_t CrashHeaderFlag = 1u << 2;
constexpr uint32_t RecordCrashContext = 7;
constexpr uint32_t RecordCrashStack = 8;
constexpr uint32_t RecordEnd = 0xffffffffu;
constexpr uint32_t MaximumDumpBytes = 8192;
constexpr uint32_t MaximumStackBytes = 512;
constexpr uint32_t FileOpen = 1;
constexpr uint32_t FileOpenIf = 3;
constexpr uint32_t GenericWrite = 0x00100002u;
constexpr uint32_t GenericRead = 0x00100001u;
constexpr uint32_t ObjectCaseInsensitive = 0x40u;
constexpr uint32_t FileAttributeNormal = 0x80u;

struct DumpHeader {
    char magic[8];
    uint32_t version;
    uint32_t headerSize;
    uint16_t architecture;
    uint16_t flags;
    uint32_t pid;
    uint64_t buildId;
    uint64_t captureTime;
};
struct RecordHeader { uint32_t type, size, crc32; };
struct DataDescriptor { uint64_t base; uint32_t size, crc32; };
#pragma pack(push, 1)
struct CrashContext {
    char magic[4];
    uint16_t version;
    uint16_t size;
    uint32_t stopCode;
    uint32_t flags;
    uint64_t parameters[4];
    uint64_t buildId;
    uint32_t cpu, pid, tid, stackBytes;
    uint64_t rip, rsp, rflags, cr2;
    uint64_t gprs[16];
    uint64_t stackLimit, stackBase;
};
#pragma pack(pop)
static_assert(sizeof(DumpHeader) == 40);
static_assert(sizeof(RecordHeader) == 12);
static_assert(sizeof(DataDescriptor) == 16);
static_assert(sizeof(CrashContext) == 248);

uint8_t dumpBytes[MaximumDumpBytes]{};
uint8_t existingBytes[MaximumDumpBytes]{};

extern "C" __declspec(dllimport) int32_t NtWriteFile(
    Handle, Handle, void*, void*, IoStatusBlock*, const void*, uint32_t,
    LargeInteger*, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t NtReadFile(
    Handle, Handle, void*, void*, IoStatusBlock*, void*, uint32_t,
    LargeInteger*, uint32_t*) noexcept;
extern "C" __declspec(dllimport) int32_t NtFlushBuffersFile(
    Handle, IoStatusBlock*) noexcept;
extern "C" __declspec(dllimport) int32_t NtCreateFile(
    Handle*, uint32_t, ObjectAttributes*, IoStatusBlock*, LargeInteger*, uint32_t,
    uint32_t, uint32_t, uint32_t, void*, uint32_t) noexcept;
extern "C" __declspec(dllimport) int32_t NtClose(Handle) noexcept;
extern "C" __declspec(dllimport) int32_t NtTerminateProcess(Handle, int32_t) noexcept;
extern "C" __declspec(dllimport) int32_t MicantReadPendingCrashDump(
    void*, uint32_t, uint32_t*, uint64_t*) noexcept;
extern "C" __declspec(dllimport) int32_t MicantAcknowledgeCrashDump(uint64_t) noexcept;

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* out = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<uint8_t>(value);
    return destination;
}
extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* out = static_cast<uint8_t*>(destination);
    const auto* in = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}

uint32_t crc32(const void* data, size_t length) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool writeStd(const char* text) noexcept {
    size_t size = 0;
    while (text[size]) ++size;
    IoStatusBlock iosb{};
    const auto status = NtWriteFile(static_cast<Handle>(0x14), 0, nullptr, nullptr,
        &iosb, text, static_cast<uint32_t>(size), nullptr, nullptr);
    return status == 0 && iosb.information == size;
}

bool validateDump(const uint8_t* bytes, size_t length) noexcept {
    if (!bytes || length < sizeof(DumpHeader) + sizeof(RecordHeader) ||
        length > MaximumDumpBytes) return false;
    DumpHeader header{};
    memcpy(&header, bytes, sizeof(header));
    const char expectedMagic[8] = {'L','U','C','D','M','P','1','\0'};
    // Compare explicitly to keep this freestanding without a libc import.
    for (size_t i = 0; i < sizeof(expectedMagic); ++i)
        if (header.magic[i] != expectedMagic[i]) return false;
    if (header.version != 1 || header.headerSize != sizeof(DumpHeader) ||
        header.architecture != 0x8664 || !(header.flags & CrashHeaderFlag) || header.pid != 0)
        return false;

    size_t offset = header.headerSize;
    bool contextSeen = false, stackSeen = false, ended = false;
    CrashContext context{};
    while (offset < length) {
        if (length - offset < sizeof(RecordHeader)) return false;
        RecordHeader record{};
        memcpy(&record, bytes + offset, sizeof(record));
        offset += sizeof(record);
        if (record.size > length - offset) return false;
        const uint8_t* payload = bytes + offset;
        if ((record.size ? crc32(payload, record.size) : 0) != record.crc32) return false;
        if (record.type == RecordEnd) {
            if (record.size || record.crc32 || offset != length) return false;
            ended = true;
            break;
        }
        if (record.type == RecordCrashContext) {
            if (contextSeen || record.size != sizeof(CrashContext)) return false;
            memcpy(&context, payload, sizeof(context));
            const char crashMagic[4] = {'C','R','S','H'};
            for (size_t i = 0; i < sizeof(crashMagic); ++i)
                if (context.magic[i] != crashMagic[i]) return false;
            if (context.version != 1 || context.size != sizeof(CrashContext) ||
                context.stackBytes > MaximumStackBytes ||
                !context.buildId || !header.buildId || context.buildId != header.buildId)
                return false;
            contextSeen = true;
        } else if (record.type == RecordCrashStack) {
            if (!contextSeen || stackSeen || record.size < sizeof(DataDescriptor)) return false;
            DataDescriptor data{};
            memcpy(&data, payload, sizeof(data));
            const size_t dataBytes = record.size - sizeof(data);
            const uint8_t* stack = payload + sizeof(data);
            if (data.size != dataBytes || dataBytes > MaximumStackBytes ||
                data.size != context.stackBytes || data.base != context.stackLimit ||
                crc32(stack, dataBytes) != data.crc32) return false;
            stackSeen = true;
        } else {
            // A kernel crash image contains only the context, optional stack,
            // and the common end marker. Reject unknown records fail-closed.
            return false;
        }
        offset += record.size;
    }
    if (!ended || !contextSeen || (context.stackBytes && !stackSeen) ||
        (!context.stackBytes && stackSeen)) return false;
    return true;
}

bool makePath(uint64_t dumpId, char16_t (&path)[96]) noexcept {
    constexpr char16_t prefix[] = u"C:\\Windows\\Minidump\\LUCEOS-";
    size_t n = 0;
    for (; prefix[n]; ++n) path[n] = prefix[n];
    constexpr char16_t hex[] = u"0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        path[n++] = hex[(dumpId >> shift) & 0xf];
    path[n++] = u'.'; path[n++] = u'd'; path[n++] = u'm'; path[n++] = u'p';
    path[n] = 0;
    return n < sizeof(path) / sizeof(path[0]);
}

bool writeSuccess(const char16_t* path) noexcept {
    char line[160] = "[LuceCrashRecovery] validated, flushed, and acknowledged ";
    size_t length = 0;
    while (line[length]) ++length;
    for (size_t i = 0; path[i] && length + 3 < sizeof(line); ++i)
        line[length++] = static_cast<char>(path[i]);
    line[length++] = '\r';
    line[length++] = '\n';
    line[length] = 0;
    return writeStd(line);
}

bool openFile(const char16_t* path, uint32_t disposition, uint32_t access,
              Handle& file, IoStatusBlock& iosb, int32_t* returnedStatus = nullptr) noexcept {
    size_t chars = 0;
    while (path[chars]) ++chars;
    UnicodeString name{};
    name.length = static_cast<uint16_t>(chars * sizeof(char16_t));
    name.maximumLength = static_cast<uint16_t>(name.length + sizeof(char16_t));
    name.buffer = reinterpret_cast<wchar_t*>(const_cast<char16_t*>(path));
    ObjectAttributes attributes{};
    attributes.objectName = &name;
    attributes.attributes = ObjectCaseInsensitive;
    const int32_t status = NtCreateFile(&file, access, &attributes, &iosb, nullptr,
        0, 7, disposition, 0x20, nullptr, 0);
    if (returnedStatus) *returnedStatus = status;
    return status == 0 && file != 0;
}

bool writeStatus(const char* prefix, uint32_t value) noexcept {
    char message[96]{};
    size_t length{};
    while (prefix[length] && length + 12 < sizeof(message)) {
        message[length] = prefix[length];
        ++length;
    }
    constexpr char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) message[length++] = digits[(value >> shift) & 0xf];
    message[length++] = '\r'; message[length++] = '\n'; message[length] = 0;
    return writeStd(message);
}

bool writeAll(Handle file, const uint8_t* bytes, uint32_t length) noexcept {
    uint32_t offset = 0;
    while (offset < length) {
        const uint32_t chunk = length - offset > 4096 ? 4096 : length - offset;
        LargeInteger byteOffset{};
        byteOffset.QuadPart = offset;
        IoStatusBlock iosb{};
        const auto status = NtWriteFile(file, 0, nullptr, nullptr, &iosb,
            bytes + offset, chunk, &byteOffset, nullptr);
        if (status != 0 || !iosb.information || iosb.information > chunk) return false;
        offset += static_cast<uint32_t>(iosb.information);
    }
    return true;
}

bool existingMatches(Handle file, const uint8_t* bytes, uint32_t length) noexcept {
    uint32_t offset = 0;
    while (offset < length) {
        const uint32_t chunk = length - offset > 4096 ? 4096 : length - offset;
        LargeInteger byteOffset{};
        byteOffset.QuadPart = offset;
        IoStatusBlock iosb{};
        const auto status = NtReadFile(file, 0, nullptr, nullptr, &iosb,
            existingBytes + offset, chunk, &byteOffset, nullptr);
        if (status != 0 || iosb.information != chunk) return false;
        for (uint32_t i = 0; i < chunk; ++i)
            if (existingBytes[offset + i] != bytes[offset + i]) return false;
        offset += chunk;
    }
    uint8_t extraByte{};
    LargeInteger byteOffset{};
    byteOffset.QuadPart = length;
    IoStatusBlock iosb{};
    const auto status = NtReadFile(file, 0, nullptr, nullptr, &iosb,
        &extraByte, sizeof(extraByte), &byteOffset, nullptr);
    return iosb.information == 0 &&
        (status == 0 || static_cast<uint32_t>(status) == 0xc0000011u);
}

int recover() noexcept {
    uint32_t required{};
    uint64_t dumpId{};
    auto status = MicantReadPendingCrashDump(nullptr, 0, &required, &dumpId);
    if (static_cast<uint32_t>(status) == 0x80000006u) {
        (void)writeStd("lucecrash: no pending kernel dump\r\n");
        return 0;
    }
    if (static_cast<uint32_t>(status) != static_cast<uint32_t>(NtStatus::BufferTooSmall) ||
        required < sizeof(DumpHeader) + sizeof(RecordHeader) ||
        required > sizeof(dumpBytes) || dumpId == 0) {
        (void)writeStd("lucecrash: pending dump query failed or exceeded bounds\r\n");
        return 1;
    }
    uint32_t received = required;
    status = MicantReadPendingCrashDump(dumpBytes, required, &received, &dumpId);
    if (status != static_cast<int32_t>(NtStatus::Success) || received != required ||
        !validateDump(dumpBytes, received)) {
        (void)writeStd("lucecrash: pending dump failed length, structure, or CRC validation\r\n");
        return 1;
    }

    char16_t path[96]{};
    if (!makePath(dumpId, path)) return 1;
    Handle file{};
    IoStatusBlock iosb{};
    if (openFile(path, FileOpen, GenericRead, file, iosb)) {
        const bool same = existingMatches(file, dumpBytes, received);
        (void)NtClose(file);
        if (!same) {
            (void)writeStd("lucecrash: destination exists with different contents; retained reserved dump\r\n");
            return 1;
        }
        // Retry after a prior export whose ACK was interrupted. Re-flush the
        // exact bytes before releasing the retained source copy.
        if (!openFile(path, FileOpen, GenericWrite, file, iosb)) return 1;
    } else {
        int32_t createStatus{};
        if (!openFile(path, FileOpenIf, GenericWrite, file, iosb, &createStatus) || iosb.information != 2) {
            if (file) (void)NtClose(file);
            (void)writeStd("lucecrash: cannot create unique Minidump destination\r\n");
            (void)writeStatus("lucecrash: NtCreateFile status=0x", static_cast<uint32_t>(createStatus));
            (void)writeStatus("lucecrash: NtCreateFile information=0x", static_cast<uint32_t>(iosb.information));
            return 1;
        }
        if (!writeAll(file, dumpBytes, received)) {
            (void)NtClose(file);
            (void)writeStd("lucecrash: export write failed; retained reserved dump\r\n");
            return 1;
        }
    }
    IoStatusBlock flushStatus{};
    if (NtFlushBuffersFile(file, &flushStatus) != 0 || flushStatus.status != NtStatus::Success) {
        (void)NtClose(file);
        (void)writeStd("lucecrash: export flush failed; retained reserved dump\r\n");
        return 1;
    }
    (void)NtClose(file);
    if (!openFile(path, FileOpen, GenericRead, file, iosb)) {
        (void)writeStd("lucecrash: flushed destination cannot be reopened for persistence check\r\n");
        return 1;
    }
    const bool persisted = existingMatches(file, dumpBytes, received);
    (void)NtClose(file);
    if (!persisted) {
        (void)writeStd("lucecrash: flushed destination read-back does not match; retained reserved dump\r\n");
        return 1;
    }
    status = MicantAcknowledgeCrashDump(dumpId);
    if (status != static_cast<int32_t>(NtStatus::Success)) {
        (void)writeStd("lucecrash: durable export exists but reserved dump ACK failed\r\n");
        return 1;
    }
    (void)writeSuccess(path);
    return 0;
}
} // namespace

extern "C" void _start() {
    const int result = recover();
    (void)NtTerminateProcess(static_cast<Handle>(-1), result ?
        static_cast<int32_t>(NtStatus::Unsuccessful) : static_cast<int32_t>(NtStatus::Success));
}
