#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <chrono>
#include <span>
#include <queue>
#include <optional>
#include <atomic>
#include <cstring>
#include <future>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/shared/native_lpc.hpp"
#include "micant/km/executive/ob.hpp"
#include "micant/km/executive/se.hpp"

namespace micant::lpc {

// ============================================================================
// 1. Standard NT LPC / ALPC Message Types
// ============================================================================

// ALPC Message Attribute Flags
inline constexpr uint32_t ALPC_MESSAGE_SECURITY_ATTRIBUTE = 0x80000000;
inline constexpr uint32_t ALPC_MESSAGE_VIEW_ATTRIBUTE     = 0x40000000;
inline constexpr uint32_t ALPC_MESSAGE_CONTEXT_ATTRIBUTE  = 0x20000000;
inline constexpr uint32_t ALPC_MESSAGE_HANDLE_ATTRIBUTE   = 0x10000000;
inline constexpr uint32_t ALPC_VIEWFLG_NOT_AT_BASE        = 0x00010000;
inline constexpr size_t   PORT_MAXIMUM_MESSAGE_LENGTH     = 65536;

/**
 * @brief ALPC Data View Attribute (Section Mapping for Large Payloads).
 */
struct AlpcDataView {
    uint32_t flags{0};
    size_t   sectionOffset{0};
    size_t   viewSize{0};
    void*    viewBase{nullptr};
};

/**
 * @brief ALPC Handle Attribute (Passing object handles across process boundaries).
 */
struct AlpcHandleAttribute {
    uint32_t flags{0};
    uintptr_t handle{0};
    uint32_t desiredAccess{0};
};

/**
 * @brief ALPC Security Attribute (Client identity, impersonation, and MIC level).
 */
struct AlpcSecurityAttribute {
    uint32_t flags{0};
    se::Sid  userSid;
    uint32_t integrityLevel{se::SECURITY_MANDATORY_MEDIUM_RID};
};

/**
 * @brief ALPC Direct Message Attributes Container.
 */
struct AlpcMessageAttributes {
    uint32_t allocatedAttributes{0};
    uint32_t validAttributes{0};
    std::optional<AlpcDataView> view;
    std::optional<AlpcHandleAttribute> handle;
    std::optional<AlpcSecurityAttribute> security;
    uintptr_t context{0};
};

/**
 * @brief Internal LPC Packet containing header, payload, and optional ALPC attributes.
 */
struct PortPacket {
    PortMessage header{};
    std::vector<uint8_t> payload;
    std::optional<AlpcMessageAttributes> attributes;
};

enum class PortType : uint32_t {
    ConnectionPort,          // Named listener port (e.g. \RPC Control\MicaPort)
    ServerCommunicationPort, // Server-side dedicated endpoint
    ClientCommunicationPort  // Client-side dedicated endpoint
};

// ============================================================================
// 2. ALPC Diagnostic Telemetry
// ============================================================================
struct LPC_TELEMETRY {
    size_t totalConnectionPortsCreated{0};
    size_t totalCommunicationPortsCreated{0};
    size_t totalConnectionRequests{0};
    size_t totalConnectionsAccepted{0};
    size_t totalConnectionsRejected{0};
    size_t totalSynchronousRequests{0};
    size_t totalAsynchronousDatagrams{0};
    size_t totalRepliesSent{0};
    size_t totalLargeViewsCreated{0};
    size_t totalClientImpersonations{0};
    size_t totalPortsClosed{0};
};

inline LPC_TELEMETRY g_LpcTelemetry{};
inline std::mutex g_LpcTelemetryMutex{};

inline LPC_TELEMETRY LpcQueryTelemetry() {
    std::lock_guard<std::mutex> lk(g_LpcTelemetryMutex);
    return g_LpcTelemetry;
}

inline void LpcResetTelemetryForTesting() {
    std::lock_guard<std::mutex> lk(g_LpcTelemetryMutex);
    g_LpcTelemetry = LPC_TELEMETRY{};
}

// ============================================================================
// 3. ALPC Shared Memory Section & Data View Architecture
// ============================================================================
class AlpcPortSection {
public:
    explicit AlpcPortSection(size_t size) : buffer_(size, 0) {}

    [[nodiscard]] size_t getSize() const noexcept { return buffer_.size(); }
    [[nodiscard]] uint8_t* getBase() noexcept { return buffer_.data(); }
    [[nodiscard]] const uint8_t* getBase() const noexcept { return buffer_.data(); }

    bool writeData(size_t offset, std::span<const uint8_t> data) {
        if (offset + data.size() > buffer_.size()) return false;
        std::memcpy(buffer_.data() + offset, data.data(), data.size());
        return true;
    }

    bool readData(size_t offset, std::span<uint8_t> outBuffer) const {
        if (offset + outBuffer.size() > buffer_.size()) return false;
        std::memcpy(outBuffer.data(), buffer_.data() + offset, outBuffer.size());
        return true;
    }

private:
    std::vector<uint8_t> buffer_;
};

class PortObject;

/**
 * @brief Pending Connection Request for 3-way Handshake.
 */
struct PendingConnectRequest {
    uint32_t connectionId{0};
    ClientId clientId{};
    Handle listeningProcessId{0};
    std::shared_ptr<PortObject> clientPort;
    PortMessage connectMessage{};
    std::vector<uint8_t> connectData;
    std::optional<AlpcMessageAttributes> connectAttributes;
    std::shared_ptr<std::promise<NtStatus>> resultPromise;
    std::shared_ptr<PortObject> serverPort;
};

// ============================================================================
// 4. Advanced Local Procedure Call (ALPC) Port Object
// ============================================================================
class PortObject : public std::enable_shared_from_this<PortObject> {
public:
    PortObject(std::wstring name, PortType type,
               size_t maximumConnectionInfoLength = 0,
               size_t maximumMessageLength = 256)
        : name_(std::move(name)), type_(type),
          maximumConnectionInfoLength_(maximumConnectionInfoLength),
          maximumMessageLength_(maximumMessageLength) {}

    [[nodiscard]] const std::wstring& getName() const noexcept { return name_; }
    [[nodiscard]] PortType getType() const noexcept { return type_; }
    [[nodiscard]] bool isConnected() const noexcept { return !peerPort_.expired(); }
    void setContext(uintptr_t context) noexcept { context_ = context; }
    [[nodiscard]] uintptr_t getContext() const noexcept { return context_; }
    [[nodiscard]] size_t getMaxConnectionInfoLength() const noexcept { return maximumConnectionInfoLength_; }
    [[nodiscard]] size_t getMaxMessageLength() const noexcept { return maximumMessageLength_; }

    void setPeerPort(std::shared_ptr<PortObject> peer) {
        peerPort_ = peer;
    }

    [[nodiscard]] std::shared_ptr<PortObject> getPeerPort() const {
        return peerPort_.lock();
    }

    // Security Descriptor for Port Objects
    void setSecurityDescriptor(se::SecurityDescriptor sd) {
        securityDescriptor_ = std::move(sd);
    }

    [[nodiscard]] const std::optional<se::SecurityDescriptor>& getSecurityDescriptor() const noexcept {
        return securityDescriptor_;
    }

    // Client Token & Impersonation
    void setClientToken(std::shared_ptr<se::TokenObject> token) {
        std::unique_lock<std::mutex> lock(mutex_);
        clientToken_ = std::move(token);
    }

    [[nodiscard]] std::shared_ptr<se::TokenObject> getClientToken() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return clientToken_;
    }

    NtStatus impersonateClient(std::shared_ptr<se::TokenObject>& savedToken, const se::TokenObject* serverCurrentToken = nullptr) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!clientToken_) return NtStatus::Unsuccessful;
        if (serverCurrentToken) {
            savedToken = std::make_shared<se::TokenObject>(*serverCurrentToken);
        }
        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalClientImpersonations++;
        }
        return NtStatus::Success;
    }

    void revertToSelf(std::shared_ptr<se::TokenObject>& savedToken) {
        std::unique_lock<std::mutex> lock(mutex_);
        savedToken.reset();
    }

    // Section Management (ALPC Large Data Transfer)
    uintptr_t createSection(size_t size) {
        std::unique_lock<std::mutex> lock(mutex_);
        uintptr_t id = nextSectionId_++;
        sections_[id] = std::make_shared<AlpcPortSection>(size);
        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalLargeViewsCreated++;
        }
        return id;
    }

    bool deleteSection(uintptr_t sectionId) {
        std::unique_lock<std::mutex> lock(mutex_);
        return sections_.erase(sectionId) > 0;
    }

    [[nodiscard]] std::shared_ptr<AlpcPortSection> getSection(uintptr_t sectionId) const {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = sections_.find(sectionId);
        if (it != sections_.end()) return it->second;
        return nullptr;
    }

    bool createDataView(uintptr_t sectionId, size_t offset, size_t size, AlpcDataView& outView) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = sections_.find(sectionId);
        if (it == sections_.end()) return false;
        if (offset + size > it->second->getSize()) return false;

        outView.flags = 0;
        outView.sectionOffset = offset;
        outView.viewSize = size;
        outView.viewBase = it->second->getBase() + offset;
        return true;
    }

    /**
     * @brief Post a message packet into this port's incoming queue.
     */
    NtStatus postMessage(
        const PortMessage& header,
        std::span<const uint8_t> payload,
        const AlpcMessageAttributes* attrs = nullptr
    ) {
        std::unique_lock<std::mutex> lock(mutex_);
        PortPacket packet{
            .header = header,
            .payload = std::vector<uint8_t>(payload.begin(), payload.end())
        };
        packet.header.u1.dataLength = static_cast<uint16_t>(payload.size());
        packet.header.u1.totalLength = static_cast<uint16_t>(sizeof(PortMessage) + payload.size());

        if (attrs) {
            packet.attributes = *attrs;
        }

        inboundQueue_.push(std::move(packet));
        cv_.notify_one();
        return NtStatus::Success;
    }

    /**
     * @brief Sends a message to the connected peer port's incoming queue.
     */
    NtStatus sendMessage(
        const PortMessage& header,
        std::span<const uint8_t> payload,
        const AlpcMessageAttributes* attrs = nullptr
    ) {
        auto peer = peerPort_.lock();
        if (!peer) return NtStatus::PortConnectionRefused;
        return peer->postMessage(header, payload, attrs);
    }

    /**
     * @brief Receive an incoming message from the port queue.
     */
    NtStatus receiveMessage(
        PortMessage& outHeader,
        std::vector<uint8_t>& outPayload,
        uint32_t timeoutMs = 0xFFFFFFFF,
        AlpcMessageAttributes* outAttrs = nullptr
    ) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (inboundQueue_.empty()) {
            if (timeoutMs == 0) {
                return NtStatus::Timeout;
            }
            if (timeoutMs == 0xFFFFFFFF) {
                cv_.wait(lock, [this]() { return !inboundQueue_.empty(); });
            } else {
                bool ok = cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
                    return !inboundQueue_.empty();
                });
                if (!ok) return NtStatus::Timeout;
            }
        }

        auto packet = std::move(inboundQueue_.front());
        inboundQueue_.pop();

        outHeader = packet.header;
        outPayload = std::move(packet.payload);
        if (outAttrs && packet.attributes) {
            *outAttrs = *packet.attributes;
        }
        return NtStatus::Success;
    }

    /**
     * @brief Synchronous LPC Request-Wait-Reply Rendezvous.
     */
    NtStatus requestWaitReply(
        const PortMessage& reqHeader,
        std::span<const uint8_t> reqPayload,
        PortMessage& replyHeader,
        std::vector<uint8_t>& replyPayload,
        uint32_t timeoutMs = 5000,
        const AlpcMessageAttributes* reqAttrs = nullptr,
        AlpcMessageAttributes* replyAttrs = nullptr
    ) {
        auto peer = peerPort_.lock();
        if (!peer) return NtStatus::PortConnectionRefused;

        PortMessage msg = reqHeader;
        if (msg.messageId == 0) {
            static std::atomic<uint32_t> s_NextMsgId{1};
            msg.messageId = s_NextMsgId++;
        }
        msg.u2.type = static_cast<uint16_t>(PortMessageType::LpcRequest);

        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalSynchronousRequests++;
        }

        NtStatus sendStatus = peer->postMessage(msg, reqPayload, reqAttrs);
        if (!NT_SUCCESS(sendStatus)) return sendStatus;

        auto startTime = std::chrono::steady_clock::now();
        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timeoutMs) {
                return NtStatus::Timeout;
            }

            uint32_t remaining = static_cast<uint32_t>(timeoutMs - elapsed);
            PortMessage incoming{};
            std::vector<uint8_t> incomingData;

            NtStatus recStatus = receiveMessage(incoming, incomingData, remaining, replyAttrs);
            if (!NT_SUCCESS(recStatus)) return recStatus;

            if (incoming.messageId == msg.messageId && 
                incoming.u2.type == static_cast<uint16_t>(PortMessageType::LpcReply)) {
                replyHeader = incoming;
                replyPayload = std::move(incomingData);
                return NtStatus::Success;
            }
        }
    }

    /**
     * @brief Send a reply to a previous request across the peer port.
     */
    NtStatus reply(
        const PortMessage& reqHeader,
        std::span<const uint8_t> replyPayload,
        const AlpcMessageAttributes* replyAttrs = nullptr
    ) {
        auto peer = peerPort_.lock();
        if (!peer) return NtStatus::PortConnectionRefused;

        PortMessage replyMsg = reqHeader;
        replyMsg.u2.type = static_cast<uint16_t>(PortMessageType::LpcReply);

        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalRepliesSent++;
        }

        return peer->postMessage(replyMsg, replyPayload, replyAttrs);
    }

    /**
     * @brief Canonical NtReplyWaitReceivePort: reply to previous message (if any) and wait for next incoming message.
     */
    NtStatus replyWaitReceive(
        const PortMessage* replyHeader,
        std::span<const uint8_t> replyPayload,
        PortMessage& outReqHeader,
        std::vector<uint8_t>& outReqPayload,
        uint32_t timeoutMs = 0xFFFFFFFF,
        const AlpcMessageAttributes* replyAttrs = nullptr,
        AlpcMessageAttributes* outReqAttrs = nullptr
    ) {
        if (replyHeader) {
            NtStatus st = reply(*replyHeader, replyPayload, replyAttrs);
            if (!NT_SUCCESS(st)) return st;
        }
        return receiveMessage(outReqHeader, outReqPayload, timeoutMs, outReqAttrs);
    }

    /**
     * @brief Sends an asynchronous datagram (fire-and-forget message).
     */
    NtStatus sendDatagram(
        const PortMessage& header,
        std::span<const uint8_t> payload,
        const AlpcMessageAttributes* attrs = nullptr
    ) {
        PortMessage msg = header;
        msg.u2.type = static_cast<uint16_t>(PortMessageType::LpcDatagram);

        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalAsynchronousDatagrams++;
        }

        return sendMessage(msg, payload, attrs);
    }

    /**
     * @brief Enqueue a pending 3-way handshake connection request.
     */
    void enqueuePendingConnection(PendingConnectRequest req) {
        std::unique_lock<std::mutex> lock(mutex_);
        uint32_t id = req.connectionId;
        pendingConnections_[id] = std::move(req);
        pendingConnectionIds_.push(id);
        cv_.notify_one();
    }

    bool getNextPendingConnection(PendingConnectRequest& outReq, uint32_t timeoutMs = 0xFFFFFFFF,
                                  Handle listeningProcessId = 0) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (pendingConnectionIds_.empty()) {
            if (timeoutMs == 0) return false;
            if (timeoutMs == 0xFFFFFFFF) {
                cv_.wait(lock, [this]() { return !pendingConnectionIds_.empty(); });
            } else {
                bool ok = cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
                    return !pendingConnectionIds_.empty();
                });
                if (!ok) return false;
            }
        }

        uint32_t id = pendingConnectionIds_.front();
        pendingConnectionIds_.pop();

        auto it = pendingConnections_.find(id);
        if (it != pendingConnections_.end()) {
            it->second.listeningProcessId = listeningProcessId;
            outReq = it->second;
            return true;
        }
        return false;
    }

    bool resolvePendingConnection(uint32_t connectionId, bool accept, std::shared_ptr<PortObject> serverPort,
                                  Handle listeningProcessId = 0) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = pendingConnections_.find(connectionId);
        if (it == pendingConnections_.end()) return false;
        if (listeningProcessId && it->second.listeningProcessId != listeningProcessId) return false;

        auto req = std::move(it->second);
        pendingConnections_.erase(it);

        if (accept) {
            req.serverPort = serverPort;
            if (serverPort) {
                serverPort->maximumConnectionInfoLength_ = maximumConnectionInfoLength_;
                serverPort->maximumMessageLength_ = maximumMessageLength_;
            }
            req.clientPort->setPeerPort(serverPort);
            serverPort->setPeerPort(req.clientPort);
            if (req.connectAttributes && req.connectAttributes->security) {
                auto clientSid = req.connectAttributes->security->userSid;
                serverPort->setClientToken(se::TokenObject::createUserToken(clientSid));
            }
            req.resultPromise->set_value(NtStatus::Success);
        } else {
            req.resultPromise->set_value(NtStatus::PortConnectionRefused);
        }
        return true;
    }

    /**
     * @brief Gracefully disconnect the port and notify the peer.
     */
    void disconnect() {
        auto peer = peerPort_.lock();
        if (peer) {
            PortMessage closedMsg{};
            closedMsg.u2.type = static_cast<uint16_t>(PortMessageType::LpcPortClosed);
            peer->postMessage(closedMsg, {});
        }
        peerPort_.reset();

        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalPortsClosed++;
        }
    }

    [[nodiscard]] size_t getQueuedCount() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return inboundQueue_.size();
    }

    [[nodiscard]] size_t getPendingConnectionCount() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return pendingConnectionIds_.size();
    }

private:
    std::wstring name_;
    PortType type_;
    uintptr_t context_{0};
    size_t maximumConnectionInfoLength_{0};
    size_t maximumMessageLength_{256};
    std::weak_ptr<PortObject> peerPort_;
    std::optional<se::SecurityDescriptor> securityDescriptor_;
    std::shared_ptr<se::TokenObject> clientToken_;
    std::unordered_map<uintptr_t, std::shared_ptr<AlpcPortSection>> sections_;
    std::atomic<uintptr_t> nextSectionId_{1};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<PortPacket> inboundQueue_;

    // Pending connection requests for 3-way handshake
    std::unordered_map<uint32_t, PendingConnectRequest> pendingConnections_;
    std::queue<uint32_t> pendingConnectionIds_;
};

// ============================================================================
// 5. ALPC Subsystem Manager
// ============================================================================
class PortManager {
public:
    static PortManager& get() {
        static PortManager instance;
        return instance;
    }

    /**
     * @brief Create a named connection port under \RPC Control\<name>.
     */
    std::shared_ptr<PortObject> createPort(
        std::wstring_view name,
        std::optional<se::SecurityDescriptor> sd = std::nullopt,
        size_t maximumConnectionInfoLength = 256 - sizeof(PortMessage),
        size_t maximumMessageLength = 256
    ) {
        if (maximumMessageLength < sizeof(PortMessage) ||
            maximumMessageLength > PORT_MAXIMUM_MESSAGE_LENGTH ||
            maximumConnectionInfoLength > maximumMessageLength - sizeof(PortMessage)) return nullptr;
        std::unique_lock<std::mutex> lock(mutex_);
        std::wstring fullName = normalizePortName(name);

        auto it = namedPorts_.find(fullName);
        if (it != namedPorts_.end()) return nullptr;

        auto port = std::make_shared<PortObject>(fullName, PortType::ConnectionPort,
            maximumConnectionInfoLength, maximumMessageLength);
        if (sd) {
            port->setSecurityDescriptor(std::move(*sd));
        }
        namedPorts_[fullName] = port;

        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalConnectionPortsCreated++;
        }

        return port;
    }

    /**
     * @brief Direct fast-path connection (creates paired client and server endpoints immediately).
     */
    NtStatus connectPort(
        std::wstring_view name,
        std::shared_ptr<PortObject>& clientCommPort,
        std::shared_ptr<PortObject>& serverCommPort,
        const se::TokenObject* callerToken = nullptr
    ) {
        std::unique_lock<std::mutex> lock(mutex_);
        std::wstring fullName = normalizePortName(name);

        auto it = namedPorts_.find(fullName);
        if (it == namedPorts_.end()) {
            return NtStatus::ObjectNameNotFound;
        }

        auto targetPort = it->second;

        if (callerToken && targetPort->getSecurityDescriptor()) {
            uint32_t granted = 0;
            NtStatus secStatus = se::SecurityReferenceMonitor::accessCheck(
                *callerToken,
                *targetPort->getSecurityDescriptor(),
                se::GENERIC_READ | se::GENERIC_WRITE,
                granted,
                fullName
            );
            if (!NT_SUCCESS(secStatus)) {
                return NtStatus::AccessDenied;
            }
        }

        clientCommPort = std::make_shared<PortObject>(fullName + L":Client", PortType::ClientCommunicationPort);
        serverCommPort = std::make_shared<PortObject>(fullName + L":Server", PortType::ServerCommunicationPort);

        clientCommPort->setPeerPort(serverCommPort);
        serverCommPort->setPeerPort(clientCommPort);

        if (callerToken) {
            serverCommPort->setClientToken(std::make_shared<se::TokenObject>(*callerToken));
        }

        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalCommunicationPortsCreated += 2;
            g_LpcTelemetry.totalConnectionsAccepted++;
        }

        return NtStatus::Success;
    }

    /**
     * @brief Canonical 3-way Handshake Client Connection Initiation (NtConnectPort).
     */
    NtStatus connectPortEx(
        std::wstring_view name,
        std::span<const uint8_t> connectData,
        std::shared_ptr<PortObject>& outClientCommPort,
        const se::TokenObject* callerToken = nullptr,
        const AlpcMessageAttributes* connectAttrs = nullptr,
        uint32_t timeoutMs = 5000,
        ClientId clientId = {}
    ) {
        std::shared_ptr<PortObject> targetPort;
        std::wstring fullName = normalizePortName(name);

        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = namedPorts_.find(fullName);
            if (it == namedPorts_.end()) {
                return NtStatus::ObjectNameNotFound;
            }
            targetPort = it->second;
        }

        if (connectData.size() > targetPort->getMaxConnectionInfoLength() ||
            connectData.size() > targetPort->getMaxMessageLength() - sizeof(PortMessage))
            return NtStatus::BufferTooSmall;

        // Access check
        if (callerToken && targetPort->getSecurityDescriptor()) {
            uint32_t granted = 0;
            NtStatus secStatus = se::SecurityReferenceMonitor::accessCheck(
                *callerToken,
                *targetPort->getSecurityDescriptor(),
                se::GENERIC_READ | se::GENERIC_WRITE,
                granted,
                fullName
            );
            if (!NT_SUCCESS(secStatus)) {
                return NtStatus::AccessDenied;
            }
        }

        static std::atomic<uint32_t> s_ConnIdGen{1};
        uint32_t connId = s_ConnIdGen++;

        auto clientComm = std::make_shared<PortObject>(fullName + L":Client:" + std::to_wstring(connId),
            PortType::ClientCommunicationPort, targetPort->getMaxConnectionInfoLength(),
            targetPort->getMaxMessageLength());
        auto promise = std::make_shared<std::promise<NtStatus>>();
        auto future = promise->get_future();

        PendingConnectRequest req{
            .connectionId = connId,
            .clientId = clientId,
            .clientPort = clientComm,
            .connectMessage = PortMessage{},
            .connectData = std::vector<uint8_t>(connectData.begin(), connectData.end()),
            .resultPromise = promise
        };
        req.connectMessage.u2.type = static_cast<uint16_t>(PortMessageType::LpcConnectionRequest);
        req.connectMessage.messageId = connId;
        req.connectMessage.clientId = req.clientId;
        req.connectMessage.u1.dataLength = static_cast<uint16_t>(req.connectData.size());
        req.connectMessage.u1.totalLength = static_cast<uint16_t>(sizeof(PortMessage) + req.connectData.size());

        if (connectAttrs) {
            req.connectAttributes = *connectAttrs;
        }

        {
            std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
            g_LpcTelemetry.totalConnectionRequests++;
        }

        // Enqueue connection request into target ConnectionPort
        targetPort->enqueuePendingConnection(std::move(req));

        // Wait on server acceptance/rejection
        auto waitStatus = future.wait_for(std::chrono::milliseconds(timeoutMs));
        if (waitStatus == std::future_status::timeout) {
            return NtStatus::Timeout;
        }

        NtStatus connResult = future.get();
        if (NT_SUCCESS(connResult)) {
            outClientCommPort = clientComm;
            {
                std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
                g_LpcTelemetry.totalConnectionsAccepted++;
                g_LpcTelemetry.totalCommunicationPortsCreated += 2;
            }
        } else {
            {
                std::lock_guard<std::mutex> tlk(g_LpcTelemetryMutex);
                g_LpcTelemetry.totalConnectionsRejected++;
            }
        }
        return connResult;
    }

    [[nodiscard]] std::shared_ptr<PortObject> lookupPort(std::wstring_view name) const {
        std::unique_lock<std::mutex> lock(mutex_);
        std::wstring fullName = normalizePortName(name);
        auto it = namedPorts_.find(fullName);
        if (it != namedPorts_.end()) return it->second;
        return nullptr;
    }

    bool resolvePendingConnection(uint32_t connectionId, bool accept,
                                  std::shared_ptr<PortObject> serverPort,
                                  Handle listeningProcessId = 0) {
        std::vector<std::shared_ptr<PortObject>> listeners;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            listeners.reserve(namedPorts_.size());
            for (const auto& [_, port] : namedPorts_)
                if (port && port->getType() == PortType::ConnectionPort) listeners.push_back(port);
        }
        for (const auto& listener : listeners) {
            if (listener->resolvePendingConnection(connectionId, accept, serverPort, listeningProcessId)) return true;
        }
        return false;
    }

    [[nodiscard]] size_t getPortCount() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return namedPorts_.size();
    }

    void resetForTesting() {
        std::unique_lock<std::mutex> lock(mutex_);
        namedPorts_.clear();
    }

private:
    PortManager() = default;

    static std::wstring normalizePortName(std::wstring_view name) {
        if (name.starts_with(L"\\RPC Control\\")) {
            return std::wstring(name);
        }
        if (name.starts_with(L"\\")) {
            return std::wstring(L"\\RPC Control") + std::wstring(name);
        }
        return std::wstring(L"\\RPC Control\\") + std::wstring(name);
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::wstring, std::shared_ptr<PortObject>> namedPorts_;
};

// ============================================================================
// 6. Sovereign Canonical Executive LPC Functions (Lpc*)
// ============================================================================
inline NtStatus LpcCreatePort(
    std::wstring_view portName,
    std::shared_ptr<PortObject>& outPort,
    std::optional<se::SecurityDescriptor> sd = std::nullopt
) {
    outPort = PortManager::get().createPort(portName, std::move(sd));
    return outPort ? NtStatus::Success : NtStatus::ObjectNameCollision;
}

inline NtStatus LpcConnectPort(
    std::wstring_view portName,
    std::shared_ptr<PortObject>& clientPort,
    std::shared_ptr<PortObject>& serverPort,
    const se::TokenObject* callerToken = nullptr
) {
    return PortManager::get().connectPort(portName, clientPort, serverPort, callerToken);
}

inline NtStatus LpcRequestWaitReplyPort(
    std::shared_ptr<PortObject> port,
    const PortMessage& reqHeader,
    std::span<const uint8_t> reqPayload,
    PortMessage& replyHeader,
    std::vector<uint8_t>& replyPayload,
    uint32_t timeoutMs = 5000,
    const AlpcMessageAttributes* reqAttrs = nullptr,
    AlpcMessageAttributes* replyAttrs = nullptr
) {
    if (!port) return NtStatus::InvalidHandle;
    return port->requestWaitReply(reqHeader, reqPayload, replyHeader, replyPayload, timeoutMs, reqAttrs, replyAttrs);
}

inline NtStatus LpcReplyWaitReceivePort(
    std::shared_ptr<PortObject> port,
    const PortMessage* replyHeader,
    std::span<const uint8_t> replyPayload,
    PortMessage& outReqHeader,
    std::vector<uint8_t>& outReqPayload,
    uint32_t timeoutMs = 0xFFFFFFFF,
    const AlpcMessageAttributes* replyAttrs = nullptr,
    AlpcMessageAttributes* outReqAttrs = nullptr
) {
    if (!port) return NtStatus::InvalidHandle;
    return port->replyWaitReceive(replyHeader, replyPayload, outReqHeader, outReqPayload, timeoutMs, replyAttrs, outReqAttrs);
}

inline NtStatus LpcReplyPort(
    std::shared_ptr<PortObject> port,
    const PortMessage& reqHeader,
    std::span<const uint8_t> replyPayload,
    const AlpcMessageAttributes* replyAttrs = nullptr
) {
    if (!port) return NtStatus::InvalidHandle;
    return port->reply(reqHeader, replyPayload, replyAttrs);
}

inline NtStatus LpcRequestPort(
    std::shared_ptr<PortObject> port,
    const PortMessage& header,
    std::span<const uint8_t> payload,
    const AlpcMessageAttributes* attrs = nullptr
) {
    if (!port) return NtStatus::InvalidHandle;
    return port->sendDatagram(header, payload, attrs);
}

} // namespace micant::lpc

namespace micant::alpc {
    using namespace micant::lpc;
}
