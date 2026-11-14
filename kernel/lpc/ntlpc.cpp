#include "micant/km/core/ntos_internal.hpp"
#include "micant/km/executive/lpc.hpp"
#include "micant/km/executive/probe.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace micant::sys::internal {

namespace {

inline constexpr uint32_t PORT_QUERY_ACCESS = 0x0001;
inline constexpr uint32_t PORT_CONNECT_ACCESS = 0x0002;
inline constexpr uint32_t PORT_ALL_ACCESS = ob::STANDARD_RIGHTS_ALL |
    PORT_QUERY_ACCESS | PORT_CONNECT_ACCESS;
inline constexpr size_t PORT_MAX_USER_MESSAGE_LENGTH =
    std::min(lpc::PORT_MAXIMUM_MESSAGE_LENGTH,
             static_cast<size_t>(std::numeric_limits<uint16_t>::max()));

struct LpcPortHandleBody {
    std::shared_ptr<lpc::PortObject> port;
};

void LpcDeletePortHandleObject(ob::ObjectHeader* header) {
    if (!header) return;
    auto* body = static_cast<LpcPortHandleBody*>(header->getBody());
    if (body->port && body->port->getType() != lpc::PortType::ConnectionPort)
        body->port->disconnect();
    body->~LpcPortHandleBody();
}

ob::ObjectType& LpcPortObjectType() {
    static ob::ObjectType type{
        .typeName = L"Port",
        .typeId = ob::ObjectTypeId::Port,
        .validAccessMask = PORT_ALL_ACCESS,
        .genericMapping = {
            .genericRead = ob::STANDARD_RIGHTS_READ | PORT_QUERY_ACCESS,
            .genericWrite = ob::STANDARD_RIGHTS_WRITE | PORT_CONNECT_ACCESS,
            .genericExecute = ob::STANDARD_RIGHTS_EXECUTE | PORT_CONNECT_ACCESS,
            .genericAll = PORT_ALL_ACCESS
        },
        .deleteProcedure = LpcDeletePortHandleObject
    };
    return type;
}

ob::HandleTable& LpcCurrentHandleTable() noexcept {
#if defined(MICANT_FREESTANDING)
    return ob::detail::currentHandleTable();
#else
    return ob::detail::kernelHandleTable();
#endif
}

NtStatus LpcCopyFromCaller(void* destination, const void* source, size_t size) noexcept {
    if (!destination || (!source && size)) return NtStatus::AccessViolation;
#if defined(MICANT_FREESTANDING)
    return ex::ExSafeCopyFromUser(destination, source, size, ob::UserMode);
#else
    if (size) std::memcpy(destination, source, size);
    return NtStatus::Success;
#endif
}

NtStatus LpcCopyToCaller(void* destination, const void* source, size_t size) noexcept {
    if ((!destination && size) || !source) return NtStatus::AccessViolation;
#if defined(MICANT_FREESTANDING)
    return ex::ExSafeCopyToUser(destination, source, size, ob::UserMode);
#else
    if (size) std::memcpy(destination, source, size);
    return NtStatus::Success;
#endif
}

NtStatus LpcProbeCallerWrite(void* destination, size_t size, size_t alignment) noexcept {
    if (!destination || !size) return NtStatus::AccessViolation;
#if defined(MICANT_FREESTANDING)
    return ex::ProbeForWrite(destination, size, static_cast<uint32_t>(alignment), ob::UserMode);
#else
    const uintptr_t address = reinterpret_cast<uintptr_t>(destination);
    if ((alignment > 1 && (address & (alignment - 1))) || address > UINTPTR_MAX - size)
        return NtStatus::AccessViolation;
    return NtStatus::Success;
#endif
}

NtStatus LpcCaptureName(const UnicodeString* callerName, std::wstring& name) {
    UnicodeString captured{};
    NtStatus status = LpcCopyFromCaller(&captured, callerName, sizeof(captured));
    if (!NT_SUCCESS(status)) return status;
    if (!captured.buffer || !captured.length || (captured.length % sizeof(wchar_t)) != 0 ||
        captured.length > captured.maximumLength ||
        captured.length > 1024 * sizeof(wchar_t)) return NtStatus::InvalidParameter;

    name.resize(captured.length / sizeof(wchar_t));
    status = LpcCopyFromCaller(name.data(), captured.buffer, captured.length);
    if (!NT_SUCCESS(status)) name.clear();
    return status;
}

NtStatus LpcCreateHandle(std::shared_ptr<lpc::PortObject> port, Handle& outHandle) {
    outHandle = 0;
    if (!port) return NtStatus::InvalidParameter;

    void* bodyStorage{};
    ob::ObjectHeader* header{};
    NtStatus status = ob::ObCreateObject(ob::KernelMode, &LpcPortObjectType(), nullptr,
        sizeof(LpcPortHandleBody), &bodyStorage, &header);
    if (!NT_SUCCESS(status)) return status;

    auto* body = new (bodyStorage) LpcPortHandleBody{std::move(port)};
    (void)body;
    status = LpcCurrentHandleTable().createHandle(header, outHandle, PORT_ALL_ACCESS);
    ob::ObDereferenceObject(header); // Drop the creation reference; the handle owns the object now.
    return status;
}

NtStatus LpcReferencePort(Handle handle, uint32_t desiredAccess,
                          std::shared_ptr<lpc::PortObject>& port) {
    port.reset();
    ob::HandleTable* table{};
    Handle rawHandle{};
    NtStatus status{};
#if defined(MICANT_FREESTANDING)
    status = ob::detail::resolveHandle(handle, table, rawHandle);
    if (!NT_SUCCESS(status)) return status;
#else
    table = &ob::detail::kernelHandleTable();
    rawHandle = handle;
#endif

    ob::ObjectHeader* header{};
    status = table->referenceHandle(rawHandle, desiredAccess, &LpcPortObjectType(), &header);
    if (!NT_SUCCESS(status)) return status;
    port = static_cast<LpcPortHandleBody*>(header->getBody())->port;
    ob::ObDereferenceObject(header);
    return port ? NtStatus::Success : NtStatus::InvalidHandle;
}

void LpcCloseUnpublishedHandle(Handle handle) noexcept {
    (void)LpcCurrentHandleTable().closeHandle(handle);
}

} // namespace

// Legacy hosted NtClose probes this hook before falling back to the canonical
// Ob handle table. LPC ports now live in that table, so this hook owns nothing.
bool LpcClosePortHandle(Handle) { return false; }

NtStatus NtCreatePortSys(const SyscallFrame& f) {
    auto* callerHandle = reinterpret_cast<Handle*>(f.arg1);
    auto* callerAttributes = reinterpret_cast<const ObjectAttributes*>(f.arg2);
    if (!callerHandle || !callerAttributes) return NtStatus::InvalidParameter;
    NtStatus status = LpcProbeCallerWrite(callerHandle, sizeof(*callerHandle), alignof(Handle));
    if (!NT_SUCCESS(status)) return status;

    ObjectAttributes attributes{};
    status = LpcCopyFromCaller(&attributes, callerAttributes, sizeof(attributes));
    if (!NT_SUCCESS(status)) return status;
    if (attributes.length != sizeof(ObjectAttributes) || !attributes.objectName ||
        attributes.rootDirectory || attributes.securityDescriptor || attributes.securityQualityOfService ||
        (attributes.attributes & ~ob::OBJ_VALID_ATTRIBUTES)) return NtStatus::NotSupported;
    // Until kernel LPC objects carry captured native security descriptors,
    // only SYSTEM may publish a listener in the global port namespace.
    if (!LpcCurrentCallerIsSystem()) return NtStatus::AccessDenied;

    const size_t maxConnectionInfo = static_cast<size_t>(f.arg3);
    const size_t maxMessage = f.arg4 ? static_cast<size_t>(f.arg4) : 256;
    if (f.stackArgs && f.stackArgCount && f.stackArgs[0]) return NtStatus::NotSupported;
    if (maxMessage < sizeof(lpc::PortMessage) || maxMessage > PORT_MAX_USER_MESSAGE_LENGTH ||
        maxConnectionInfo > maxMessage - sizeof(lpc::PortMessage)) return NtStatus::InvalidParameter;

    std::wstring name;
    status = LpcCaptureName(attributes.objectName, name);
    if (!NT_SUCCESS(status)) return status;

    auto port = lpc::PortManager::get().createPort(name, std::nullopt,
                                                   maxConnectionInfo, maxMessage);
    if (!port) return NtStatus::ObjectNameCollision;
    Handle handle{};
    status = LpcCreateHandle(std::move(port), handle);
    if (!NT_SUCCESS(status)) return status;
    status = LpcCopyToCaller(callerHandle, &handle, sizeof(handle));
    if (!NT_SUCCESS(status)) LpcCloseUnpublishedHandle(handle);
    return status;
}

NtStatus NtConnectPortSys(const SyscallFrame& f) {
    auto* callerHandle = reinterpret_cast<Handle*>(f.arg1);
    auto* callerName = reinterpret_cast<const UnicodeString*>(f.arg2);
    if (!callerHandle || !callerName) return NtStatus::InvalidParameter;
    NtStatus status = LpcProbeCallerWrite(callerHandle, sizeof(*callerHandle), alignof(Handle));
    if (!NT_SUCCESS(status)) return status;

    // Security QoS and section views need their native capture/access-check
    // implementations before these optional NT parameters can be accepted.
    if (f.arg3 || f.arg4 || (f.stackArgs && f.stackArgCount > 0 && f.stackArgs[0]))
        return NtStatus::NotSupported;

    auto* callerMaxMessageLength = f.stackArgs && f.stackArgCount > 1
        ? reinterpret_cast<uint32_t*>(f.stackArgs[1]) : nullptr;
    auto* callerConnectionInformation = f.stackArgs && f.stackArgCount > 2
        ? reinterpret_cast<const uint8_t*>(f.stackArgs[2]) : nullptr;
    auto* callerConnectionInformationLength = f.stackArgs && f.stackArgCount > 3
        ? reinterpret_cast<uint32_t*>(f.stackArgs[3]) : nullptr;
    if ((callerConnectionInformation == nullptr) != (callerConnectionInformationLength == nullptr))
        return NtStatus::InvalidParameter;

    uint32_t connectionInformationLength{};
    std::vector<uint8_t> connectionInformation;
    if (callerConnectionInformationLength) {
        status = LpcCopyFromCaller(&connectionInformationLength, callerConnectionInformationLength,
                                   sizeof(connectionInformationLength));
        if (!NT_SUCCESS(status)) return status;
        if (connectionInformationLength > PORT_MAX_USER_MESSAGE_LENGTH - sizeof(lpc::PortMessage))
            return NtStatus::InvalidParameter;
        if (connectionInformationLength) {
            connectionInformation.resize(connectionInformationLength);
            status = LpcCopyFromCaller(connectionInformation.data(), callerConnectionInformation,
                                       connectionInformation.size());
            if (!NT_SUCCESS(status)) return status;
        }
    }
    if (callerMaxMessageLength) {
        status = LpcProbeCallerWrite(callerMaxMessageLength, sizeof(*callerMaxMessageLength),
                                     alignof(uint32_t));
        if (!NT_SUCCESS(status)) return status;
    }
    if (callerConnectionInformationLength) {
        status = LpcProbeCallerWrite(callerConnectionInformationLength,
                                     sizeof(*callerConnectionInformationLength), alignof(uint32_t));
        if (!NT_SUCCESS(status)) return status;
    }

    std::wstring name;
    status = LpcCaptureName(callerName, name);
    if (!NT_SUCCESS(status)) return status;

    const ClientId clientId = LpcGetCurrentClientId();
    if (!clientId.uniqueProcess || !clientId.uniqueThread) return NtStatus::AccessDenied;
    auto listener = lpc::PortManager::get().lookupPort(name);
    if (!listener) return NtStatus::ObjectNameNotFound;
    if (listener->getSecurityDescriptor()) return NtStatus::NotSupported;

    // Use the connection-port handshake. A connection is never published to
    // a client until the server has explicitly accepted it.
    std::shared_ptr<lpc::PortObject> clientPort;
    status = lpc::PortManager::get().connectPortEx(name, connectionInformation, clientPort,
                                                   nullptr, nullptr, 5000, clientId);
    if (!NT_SUCCESS(status)) return status;

    if (callerMaxMessageLength) {
        const uint32_t maximum = static_cast<uint32_t>(clientPort->getMaxMessageLength());
        status = LpcCopyToCaller(callerMaxMessageLength, &maximum, sizeof(maximum));
    }
    if (NT_SUCCESS(status) && callerConnectionInformationLength) {
        const uint32_t returnedLength = 0; // The current handshake carries no server response data.
        status = LpcCopyToCaller(callerConnectionInformationLength, &returnedLength, sizeof(returnedLength));
    }
    if (!NT_SUCCESS(status)) {
        clientPort->disconnect();
        return status;
    }

    Handle handle{};
    status = LpcCreateHandle(std::move(clientPort), handle);
    if (!NT_SUCCESS(status)) return status;
    status = LpcCopyToCaller(callerHandle, &handle, sizeof(handle));
    if (!NT_SUCCESS(status)) LpcCloseUnpublishedHandle(handle);
    return status;
}

NtStatus NtListenPortSys(const SyscallFrame& f) {
    auto* callerMessage = reinterpret_cast<lpc::PortMessage*>(f.arg2);
    if (!callerMessage) return NtStatus::InvalidParameter;

    lpc::PortMessage capacity{};
    NtStatus status = LpcCopyFromCaller(&capacity, callerMessage, sizeof(capacity));
    if (!NT_SUCCESS(status)) return status;
    const size_t outputCapacity = capacity.u1.totalLength;
    if (outputCapacity < sizeof(lpc::PortMessage) || outputCapacity > PORT_MAX_USER_MESSAGE_LENGTH)
        return NtStatus::InvalidParameter;
    status = LpcProbeCallerWrite(callerMessage, outputCapacity, alignof(lpc::PortMessage));
    if (!NT_SUCCESS(status)) return status;

    std::shared_ptr<lpc::PortObject> port;
    status = LpcReferencePort(static_cast<Handle>(f.arg1), PORT_QUERY_ACCESS, port);
    if (!NT_SUCCESS(status)) return status;
    if (port->getType() != lpc::PortType::ConnectionPort) return NtStatus::InvalidHandle;

    const ClientId listenerClientId = LpcGetCurrentClientId();
    if (!listenerClientId.uniqueProcess) return NtStatus::AccessDenied;

    lpc::PendingConnectRequest pending{};
    if (!port->getNextPendingConnection(pending, 5000, listenerClientId.uniqueProcess))
        return NtStatus::Timeout;
    if (pending.connectData.size() > outputCapacity - sizeof(lpc::PortMessage)) {
        (void)port->resolvePendingConnection(pending.connectionId, false, nullptr,
                                             listenerClientId.uniqueProcess);
        return NtStatus::BufferTooSmall;
    }

    lpc::PortMessage message = pending.connectMessage;
    message.clientId = pending.clientId;
    message.u1.dataLength = static_cast<uint16_t>(pending.connectData.size());
    message.u1.totalLength = static_cast<uint16_t>(sizeof(message) + pending.connectData.size());
    status = LpcCopyToCaller(callerMessage, &message, sizeof(message));
    if (NT_SUCCESS(status) && !pending.connectData.empty()) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(callerMessage);
        if (address > UINTPTR_MAX - sizeof(message)) return NtStatus::AccessViolation;
        status = LpcCopyToCaller(reinterpret_cast<void*>(address + sizeof(message)),
                                 pending.connectData.data(), pending.connectData.size());
    }
    if (!NT_SUCCESS(status))
        (void)port->resolvePendingConnection(pending.connectionId, false, nullptr,
                                             listenerClientId.uniqueProcess);
    return status;
}

NtStatus NtAcceptConnectPortSys(const SyscallFrame& f) {
    auto* callerHandle = reinterpret_cast<Handle*>(f.arg1);
    auto* callerMessage = reinterpret_cast<const lpc::PortMessage*>(f.arg3);
    if (!callerHandle || !callerMessage) return NtStatus::InvalidParameter;
    NtStatus status = LpcProbeCallerWrite(callerHandle, sizeof(*callerHandle), alignof(Handle));
    if (!NT_SUCCESS(status)) return status;

    // Section-based LPC views are outside this basic-message implementation.
    if ((f.stackArgs && f.stackArgCount > 0 && f.stackArgs[0]) ||
        (f.stackArgs && f.stackArgCount > 1 && f.stackArgs[1])) return NtStatus::NotSupported;

    lpc::PortMessage request{};
    status = LpcCopyFromCaller(&request, callerMessage, sizeof(request));
    if (!NT_SUCCESS(status)) return status;
    const size_t requestTotal = sizeof(request) + request.u1.dataLength;
    if (request.u2.type != static_cast<uint16_t>(lpc::PortMessageType::LpcConnectionRequest) ||
        !request.messageId || request.u2.dataInfoOffset || request.u1.totalLength != requestTotal ||
        requestTotal > PORT_MAX_USER_MESSAGE_LENGTH) return NtStatus::InvalidParameter;

    const bool accept = f.arg4 != 0;
    const ClientId serverClientId = LpcGetCurrentClientId();
    if (!serverClientId.uniqueProcess || !serverClientId.uniqueThread) return NtStatus::AccessDenied;
    if (!accept) {
        const bool rejected = lpc::PortManager::get().resolvePendingConnection(
            request.messageId, false, nullptr, serverClientId.uniqueProcess);
        if (!rejected) return NtStatus::InvalidParameter;
        const Handle noHandle{};
        return LpcCopyToCaller(callerHandle, &noHandle, sizeof(noHandle));
    }

    auto serverPort = std::make_shared<lpc::PortObject>(
        L"\\RPC Control\\Server:" + std::to_wstring(request.messageId),
        lpc::PortType::ServerCommunicationPort);
    serverPort->setContext(static_cast<uintptr_t>(f.arg2));

    Handle handle{};
    status = LpcCreateHandle(serverPort, handle);
    if (!NT_SUCCESS(status)) return status;
    status = LpcCopyToCaller(callerHandle, &handle, sizeof(handle));
    if (!NT_SUCCESS(status)) {
        LpcCloseUnpublishedHandle(handle);
        (void)lpc::PortManager::get().resolvePendingConnection(
            request.messageId, false, nullptr, serverClientId.uniqueProcess);
        return status;
    }

    if (!lpc::PortManager::get().resolvePendingConnection(
            request.messageId, true, std::move(serverPort), serverClientId.uniqueProcess)) {
        LpcCloseUnpublishedHandle(handle);
        const Handle noHandle{};
        (void)LpcCopyToCaller(callerHandle, &noHandle, sizeof(noHandle));
        return NtStatus::InvalidParameter;
    }
    return NtStatus::Success;
}

NtStatus NtReplyWaitReceivePortSys(const SyscallFrame& f) {
    auto* callerContext = reinterpret_cast<uintptr_t*>(f.arg2);
    auto* callerReply = reinterpret_cast<const lpc::PortMessage*>(f.arg3);
    auto* callerReceive = reinterpret_cast<lpc::PortMessage*>(f.arg4);
    if (!callerReceive) return NtStatus::InvalidParameter;

    lpc::PortMessage receiveCapacity{};
    NtStatus status = LpcCopyFromCaller(&receiveCapacity, callerReceive, sizeof(receiveCapacity));
    if (!NT_SUCCESS(status)) return status;
    const size_t receiveLength = receiveCapacity.u1.totalLength;
    if (receiveLength < sizeof(lpc::PortMessage) || receiveLength > PORT_MAX_USER_MESSAGE_LENGTH)
        return NtStatus::InvalidParameter;
    status = LpcProbeCallerWrite(callerReceive, receiveLength, alignof(lpc::PortMessage));
    if (!NT_SUCCESS(status)) return status;
    if (callerContext) {
        status = LpcProbeCallerWrite(callerContext, sizeof(*callerContext), alignof(uintptr_t));
        if (!NT_SUCCESS(status)) return status;
    }

    lpc::PortMessage reply{};
    const lpc::PortMessage* replyHeader = nullptr;
    std::vector<uint8_t> replyData;
    if (callerReply) {
        status = LpcCopyFromCaller(&reply, callerReply, sizeof(reply));
        if (!NT_SUCCESS(status)) return status;
        const size_t replyTotal = sizeof(reply) + reply.u1.dataLength;
        if (reply.u2.dataInfoOffset || reply.u1.totalLength != replyTotal ||
            replyTotal > PORT_MAX_USER_MESSAGE_LENGTH) return NtStatus::InvalidParameter;
        uintptr_t replyAddress = reinterpret_cast<uintptr_t>(callerReply);
        if (replyAddress > UINTPTR_MAX - sizeof(reply)) return NtStatus::AccessViolation;
        replyData.resize(reply.u1.dataLength);
        if (!replyData.empty()) {
            status = LpcCopyFromCaller(replyData.data(),
                reinterpret_cast<const void*>(replyAddress + sizeof(reply)), replyData.size());
            if (!NT_SUCCESS(status)) return status;
        }
        replyHeader = &reply;
    }

    std::shared_ptr<lpc::PortObject> port;
    status = LpcReferencePort(static_cast<Handle>(f.arg1), PORT_QUERY_ACCESS | PORT_CONNECT_ACCESS, port);
    if (!NT_SUCCESS(status)) return status;

    lpc::PortMessage request{};
    std::vector<uint8_t> requestData;
    status = port->replyWaitReceive(replyHeader, replyData, request, requestData, 5000);
    if (!NT_SUCCESS(status)) return status;
    if (requestData.size() > receiveLength - sizeof(request)) return NtStatus::BufferTooSmall;

    request.u1.dataLength = static_cast<uint16_t>(requestData.size());
    request.u1.totalLength = static_cast<uint16_t>(sizeof(request) + requestData.size());
    const uintptr_t receiveAddress = reinterpret_cast<uintptr_t>(callerReceive);
    if (receiveAddress > UINTPTR_MAX - sizeof(request)) return NtStatus::AccessViolation;
    status = LpcCopyToCaller(callerReceive, &request, sizeof(request));
    if (NT_SUCCESS(status) && !requestData.empty()) {
        status = LpcCopyToCaller(reinterpret_cast<void*>(receiveAddress + sizeof(request)),
                                 requestData.data(), requestData.size());
    }
    if (NT_SUCCESS(status) && callerContext) {
        const uintptr_t context = port->getContext();
        status = LpcCopyToCaller(callerContext, &context, sizeof(context));
    }
    return status;
}

NtStatus NtRequestWaitReplyPortSys(const SyscallFrame& f) {
    auto* callerRequest = reinterpret_cast<const lpc::PortMessage*>(f.arg2);
    auto* callerReply = reinterpret_cast<lpc::PortMessage*>(f.arg3);
    if (!callerRequest || !callerReply) return NtStatus::InvalidParameter;

    lpc::PortMessage request{};
    lpc::PortMessage replyCapacity{};
    NtStatus status = LpcCopyFromCaller(&request, callerRequest, sizeof(request));
    if (!NT_SUCCESS(status)) return status;
    status = LpcCopyFromCaller(&replyCapacity, callerReply, sizeof(replyCapacity));
    if (!NT_SUCCESS(status)) return status;

    const size_t requestLength = request.u1.dataLength;
    const size_t requestTotal = sizeof(request) + requestLength;
    if (request.u2.dataInfoOffset != 0 || request.u1.totalLength != requestTotal ||
        requestTotal > PORT_MAX_USER_MESSAGE_LENGTH) return NtStatus::InvalidParameter;
    const size_t replyBufferLength = replyCapacity.u1.totalLength;
    if (replyBufferLength < sizeof(lpc::PortMessage) ||
        replyBufferLength > PORT_MAX_USER_MESSAGE_LENGTH) return NtStatus::InvalidParameter;

    uintptr_t requestDataAddress = reinterpret_cast<uintptr_t>(callerRequest);
    if (requestDataAddress > UINTPTR_MAX - sizeof(request)) return NtStatus::AccessViolation;
    requestDataAddress += sizeof(request);
    std::vector<uint8_t> requestData(requestLength);
    if (requestLength) {
        status = LpcCopyFromCaller(requestData.data(), reinterpret_cast<const void*>(requestDataAddress), requestLength);
        if (!NT_SUCCESS(status)) return status;
    }

    status = LpcProbeCallerWrite(callerReply, replyBufferLength, alignof(lpc::PortMessage));
    if (!NT_SUCCESS(status)) return status;

    std::shared_ptr<lpc::PortObject> port;
    status = LpcReferencePort(static_cast<Handle>(f.arg1), PORT_CONNECT_ACCESS, port);
    if (!NT_SUCCESS(status)) return status;

    lpc::PortMessage reply{};
    std::vector<uint8_t> replyData;
    status = port->requestWaitReply(request, requestData, reply, replyData, 5000);
    if (!NT_SUCCESS(status)) return status;
    if (replyData.size() > replyBufferLength - sizeof(reply)) return NtStatus::BufferTooSmall;

    reply.u1.dataLength = static_cast<uint16_t>(replyData.size());
    reply.u1.totalLength = static_cast<uint16_t>(sizeof(reply) + replyData.size());
    uintptr_t replyDataAddress = reinterpret_cast<uintptr_t>(callerReply);
    if (replyDataAddress > UINTPTR_MAX - sizeof(reply)) return NtStatus::AccessViolation;
    status = LpcCopyToCaller(callerReply, &reply, sizeof(reply));
    if (!NT_SUCCESS(status)) return status;
    if (!replyData.empty()) {
        status = LpcCopyToCaller(reinterpret_cast<void*>(replyDataAddress + sizeof(reply)),
                                 replyData.data(), replyData.size());
    }
    return status;
}

} // namespace micant::sys::internal
