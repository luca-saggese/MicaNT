// ============================================================================
// MicaNT: Bare-Metal Intel I210 Ethernet Frame Transport
// (i210_controller.hpp)
//
// Description:
//   OEM .sys PCIe bus-master driver for the Intel I210 Ethernet controller.
//   It transfers Ethernet frames to its registered network-stack owner and
//   implements no L3/L4 protocol, routing, shell, or application service.
//   It is not linked into ntoskrnl_core; all hardware ownership and mappings
//   arrive through shared boot-driver services after an Io/PnP claim.
//
// Clean-Room ISO C++23. Zero proprietary Microsoft code.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif

namespace micant::oem::i210 {

// Hardware resources are supplied by the canonical PCI/HAL owner. The DMA
// translator must return a device-visible contiguous range for the entire span,
// or zero; a CPU virtual address is never accepted as a DMA address.
struct HardwareServices {
    using PciRead32 = uint32_t (*)(uint32_t bdf, uint16_t offset) noexcept;
    using PciWrite32 = bool (*)(uint32_t bdf, uint16_t offset, uint32_t value) noexcept;
    using PciClaim = bool (*)(uint32_t bdf, uint8_t barIndex,
                              uint64_t minimumLength, uint64_t* token,
                              uint64_t* physicalAddress, uint64_t* length) noexcept;
    using PciRelease = bool (*)(uint64_t token) noexcept;
    using MmioMapper = volatile uint32_t* (*)(uint64_t physical, size_t length) noexcept;
    using MmioReleaser = bool (*)(volatile uint32_t* mapping, size_t length) noexcept;
    using DmaAllocator = bool (*)(uint64_t claim, size_t bytes, size_t alignment,
                                   void** cpu, uint64_t* device, uint64_t* token) noexcept;
    using DmaReleaser = bool (*)(uint64_t token) noexcept;
    using MonotonicClock = bool (*)(uint64_t* counter, uint64_t* frequency) noexcept;
    using DelayMicroseconds = bool (*)(uint32_t microseconds) noexcept;

    PciRead32 readPci32{};
    PciWrite32 writePci32{};
    PciClaim claimPci{};
    PciRelease releasePci{};
    MmioMapper mapMmio{};
    MmioReleaser releaseMmio{};
    DmaAllocator allocateDma{};
    DmaReleaser releaseDma{};
    MonotonicClock queryClock{};
    DelayMicroseconds delayUs{};
};

// --- Intel I210 Register Offsets ---
inline constexpr uint32_t I210_REG_CTRL      = 0x00000; // Device Control
inline constexpr uint32_t I210_REG_STATUS    = 0x00008; // Device Status
inline constexpr uint32_t I210_REG_CTRL_EXT  = 0x00018; // Extended Device Control
inline constexpr uint32_t I210_REG_ICR       = 0x000C0; // Interrupt Cause Read
inline constexpr uint32_t I210_REG_IMC       = 0x000D8; // Interrupt Mask Clear
inline constexpr uint32_t I210_REG_RCTL      = 0x00100; // Receive Control
inline constexpr uint32_t I210_REG_TCTL      = 0x00400; // Transmit Control
inline constexpr uint32_t I210_REG_TIPG      = 0x00410; // Transmit Inter-Packet Gap
inline constexpr uint32_t I210_REG_RAL0      = 0x05400; // Receive Address 0 Low (MAC 0..3)
inline constexpr uint32_t I210_REG_RAH0      = 0x05404; // Receive Address 0 High (MAC 4..5 + AV)

// Queue 0 Descriptors (I210 offsets)
inline constexpr uint32_t I210_REG_RDBAL     = 0x0C000; // RX Desc Base Low (Queue 0)
inline constexpr uint32_t I210_REG_RDBAH     = 0x0C004; // RX Desc Base High
inline constexpr uint32_t I210_REG_RDLEN     = 0x0C008; // RX Desc Length
inline constexpr uint32_t I210_REG_SRRCTL0   = 0x0C00C; // Split & Replication Receive Control
inline constexpr uint32_t I210_REG_RDH       = 0x0C010; // RX Desc Head
inline constexpr uint32_t I210_REG_RDT       = 0x0C018; // RX Desc Tail
inline constexpr uint32_t I210_REG_RXDCTL    = 0x0C028; // RX Desc Control

inline constexpr uint32_t I210_REG_TDBAL     = 0x0E000; // TX Desc Base Low (Queue 0)
inline constexpr uint32_t I210_REG_TDBAH     = 0x0E004; // TX Desc Base High
inline constexpr uint32_t I210_REG_TDLEN     = 0x0E008; // TX Desc Length
inline constexpr uint32_t I210_REG_TDH       = 0x0E010; // TX Desc Head
inline constexpr uint32_t I210_REG_TDT       = 0x0E018; // TX Desc Tail
inline constexpr uint32_t I210_REG_TXDCTL    = 0x0E028; // TX Desc Control

// Bitmasks
inline constexpr uint32_t CTRL_SLU            = (1u << 6);  // Set Link Up
inline constexpr uint32_t CTRL_EXT_DRV_LOAD   = (1u << 28); // Driver Loaded (Management NC-SI Pass-Through)
inline constexpr uint32_t STATUS_LU          = (1u << 1);  // Link Up status
inline constexpr uint32_t RCTL_EN            = (1u << 1);  // RX Enable
inline constexpr uint32_t RCTL_BAM           = (1u << 15); // Broadcast Accept
inline constexpr uint32_t RCTL_BSIZE_2048    = (0u << 16); // 2048-byte RX buffer
inline constexpr uint32_t RCTL_SECRC         = (1u << 26); // Strip Ethernet CRC

inline constexpr uint32_t TCTL_EN            = (1u << 1);  // TX Enable
inline constexpr uint32_t TCTL_PSP           = (1u << 3);  // Pad Short Packets
inline constexpr uint32_t TCTL_CT            = (0x0Fu << 4);  // Collision Threshold
inline constexpr uint32_t TCTL_COLD          = (0x40u << 12); // Collision Distance

#pragma pack(push, 1)

// --- Legacy 16-byte Receive Descriptor ---
struct I210RxDesc {
    uint64_t buffer_addr; // Physical buffer address
    uint16_t length;      // Length of received packet
    uint16_t checksum;    // Packet checksum
    volatile uint8_t status; // Bit 0: DD (Descriptor Done), Bit 1: EOP
    uint8_t  errors;      // Errors
    uint16_t special;     // Special
};

// --- Legacy 16-byte Transmit Descriptor ---
struct I210TxDesc {
    uint64_t buffer_addr; // Physical buffer address
    uint16_t length;      // Length of packet to transmit
    uint8_t  cso;         // Checksum offset
    uint8_t  cmd;         // Bit 0: EOP, Bit 1: IFCS, Bit 3: RS
    volatile uint8_t status; // Bit 0: DD (Descriptor Done)
    uint8_t  css;         // Checksum start
    uint16_t special;     // Special
};

// --- Ethernet Frame Structures ---
struct EthernetHeader {
    uint8_t  dstMac[6];
    uint8_t  srcMac[6];
    uint16_t etherType; // Big-endian
};
static_assert(sizeof(EthernetHeader) == 14);

#pragma pack(pop)

// --- Static DMA Ring Buffers ---
inline constexpr size_t NUM_RX_DESC = 64;
inline constexpr size_t NUM_TX_DESC = 64;
inline constexpr size_t RX_BUFFER_SIZE = 2048;

struct alignas(4096) DmaStorage {
    alignas(128) I210RxDesc rxRing[NUM_RX_DESC];
    alignas(128) I210TxDesc txRing[NUM_TX_DESC];
    alignas(4096) uint8_t rxBuffers[NUM_RX_DESC][RX_BUFFER_SIZE];
    alignas(4096) uint8_t txBuffers[NUM_TX_DESC][RX_BUFFER_SIZE];
};


using ReceiveFrameCallback = void(*)(void* context, const uint8_t* frame,
                                     size_t length) noexcept;

/**
 @brief Luce OS Bare-Metal Intel I210 Gigabit Ethernet Controller Driver.
 */
class BareMetalI210Driver {
private:
    volatile uint32_t* m_mmio{nullptr};
    bool     m_initialized{false};
    bool     m_linkUp{false};
    uint8_t  m_mac[6]{};
    uint64_t m_rxRingDma{};
    uint64_t m_txRingDma{};
    uint64_t m_rxBufferDma[NUM_RX_DESC]{};
    uint64_t m_txBufferDma[NUM_TX_DESC]{};
    uint32_t m_rxTail{0};
    uint32_t m_txTail{0};
    uint32_t m_pciBdf{UINT32_MAX};
    uint32_t m_originalPciCommand{};
    uint64_t m_claimToken{};
    uint64_t m_mmioPhysical{};
    size_t m_mmioLength{};
    HardwareServices m_services{};
    DmaStorage* m_dma{};
    uint64_t m_dmaToken{};
    uint64_t m_dmaBase{};
    bool m_claimed{false};
    bool m_busMasterEnabled{false};
    bool m_commandChanged{false};
    bool m_mmioActive{false};
    bool m_cleanupPending{false};
    void* m_receiveContext{};
    ReceiveFrameCallback m_receiveCallback{};
    uint64_t m_rxPackets{0};
    uint64_t m_txPackets{0};


    inline void writeReg(uint32_t offset, uint32_t val) noexcept {
        m_mmio[offset / 4] = val;
    }

    inline uint32_t readReg(uint32_t offset) const noexcept {
        return m_mmio[offset / 4];
    }

    bool waitRegister(uint32_t offset, uint32_t mask, uint32_t expected,
                      uint32_t timeoutMs) noexcept {
        if (!m_services.queryClock || !m_services.delayUs) return false;
        uint64_t start{}, frequency{};
        if (!m_services.queryClock(&start, &frequency) || !frequency) return false;
        const uint64_t whole = frequency / 1000u;
        const uint64_t fraction = ((frequency % 1000u) * timeoutMs) / 1000u;
        if (timeoutMs && whole > (UINT64_MAX - fraction) / timeoutMs) return false;
        const uint64_t duration = whole * timeoutMs + fraction;
        if (start > UINT64_MAX - duration) return false;
        const uint64_t deadline = start + duration;
        for (uint32_t polls = 0; polls < 1'000'000u; ++polls) {
            if ((readReg(offset) & mask) == expected) return true;
            uint64_t now{}, currentFrequency{};
            if (!m_services.queryClock(&now, &currentFrequency) ||
                currentFrequency != frequency || now >= deadline) return false;
            if (!m_services.delayUs(50)) return false;
        }
        return false;
    }

    bool waitTxCompletion(uint32_t descriptor, uint32_t timeoutMs) noexcept {
        if (!m_services.queryClock) return false;
        uint64_t start{}, frequency{};
        if (!m_services.queryClock(&start, &frequency) || !frequency) return false;
        const uint64_t whole = frequency / 1000u;
        const uint64_t fraction = ((frequency % 1000u) * timeoutMs) / 1000u;
        if (timeoutMs && whole > (UINT64_MAX - fraction) / timeoutMs) return false;
        const uint64_t duration = whole * timeoutMs + fraction;
        if (start > UINT64_MAX - duration) return false;
        const uint64_t deadline = start + duration;
        for (uint32_t polls = 0; polls < 1'000'000u; ++polls) {
            if ((m_dma->txRing[descriptor].status & 1u) != 0) {
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                return true;
            }
#if defined(__x86_64__) || defined(__i386__)
            __asm__ __volatile__("pause");
#endif
            uint64_t now{}, currentFrequency{};
            if (!m_services.queryClock(&now, &currentFrequency) ||
                currentFrequency != frequency || now >= deadline) return false;
        }
        return false;
    }

    bool waitTransactionsDrained() noexcept {
        const uint32_t commandStatus = m_services.readPci32(m_pciBdf, 4);
        if (commandStatus == UINT32_MAX || !(commandStatus & (1u << 20))) return false;
        uint32_t pointer = m_services.readPci32(m_pciBdf, 0x34) & 0xffu;
        uint16_t deviceStatusOffset{};
        for (unsigned visited = 0; pointer && visited < 48; ++visited) {
            if (pointer < 0x40 || pointer > 0xfc || (pointer & 3u)) return false;
            const uint32_t capability = m_services.readPci32(m_pciBdf, static_cast<uint16_t>(pointer));
            if (capability == UINT32_MAX) return false;
            if ((capability & 0xffu) == 0x10u) {
                if (pointer > 0xf4) return false;
                deviceStatusOffset = static_cast<uint16_t>(pointer + 8);
                break;
            }
            pointer = (capability >> 8) & 0xffu;
        }
        if (!deviceStatusOffset) return false;
        uint64_t start{}, frequency{};
        if (!m_services.queryClock(&start, &frequency) || !frequency) return false;
        const uint64_t duration = frequency / 10;
        if (!duration || start > UINT64_MAX - duration) return false;
        for (unsigned poll = 0; poll < 10000; ++poll) {
            const uint32_t status = m_services.readPci32(m_pciBdf, deviceStatusOffset);
            if (status == UINT32_MAX) return false;
            if (!(status & (1u << 21))) { __atomic_thread_fence(__ATOMIC_ACQUIRE); return true; }
            uint64_t now{}, currentFrequency{};
            if (!m_services.queryClock(&now, &currentFrequency) || currentFrequency != frequency ||
                now - start >= duration || !m_services.delayUs(50)) return false;
        }
        return false;
    }

    bool rollbackAndRelease() noexcept {
        if (m_mmio && m_mmioActive) {
            writeReg(I210_REG_IMC, 0xFFFFFFFF);
            writeReg(I210_REG_RCTL, 0);
            writeReg(I210_REG_TCTL, 0);
            writeReg(I210_REG_RXDCTL, 0);
            writeReg(I210_REG_TXDCTL, 0);
            if (!waitRegister(I210_REG_RXDCTL, 1u << 25, 0, 100) ||
                !waitRegister(I210_REG_TXDCTL, 1u << 25, 0, 100) ||
                !waitRegister(I210_REG_RCTL, RCTL_EN, 0, 100) ||
                !waitRegister(I210_REG_TCTL, TCTL_EN, 0, 100)) {
                m_cleanupPending = true;
                return false;
            }
        }
        if (m_claimed && m_busMasterEnabled) {
            const uint32_t command = m_services.readPci32(m_pciBdf, 0x04);
            if (command == UINT32_MAX ||
                !m_services.writePci32(m_pciBdf, 0x04,
                                       (command & 0x0000FFFFu) & ~0x00000004u)) {
                m_cleanupPending = true;
                return false;
            }
            const uint32_t verify = m_services.readPci32(m_pciBdf, 0x04);
            if (verify == UINT32_MAX || (verify & 0x00000004u)) {
                m_cleanupPending = true;
                return false;
            }
            if (!waitTransactionsDrained()) {
                m_cleanupPending = true;
                return false;
            }
            m_busMasterEnabled = false;
        }
        if (m_claimed && m_commandChanged) {
            const uint32_t restore = m_originalPciCommand & 0x0000FFFFu & ~0x00000004u;
            if (!m_services.writePci32(m_pciBdf, 0x04, restore)) {
                m_cleanupPending = true;
                return false;
            }
            const uint32_t verify = m_services.readPci32(m_pciBdf, 0x04);
            if (verify == UINT32_MAX || (verify & 0x00000004u) ||
                ((verify ^ restore) & 0x00000002u)) {
                m_cleanupPending = true;
                return false;
            }
            m_commandChanged = false;
            m_mmioActive = false;
        }
        if (m_dmaToken) {
            if (!m_services.releaseDma || !m_services.releaseDma(m_dmaToken)) {
                m_cleanupPending = true;
                return false;
            }
            m_dmaToken = 0;
            m_dma = nullptr;
            m_dmaBase = 0;
        }
        if (m_mmio) {
            if (!m_services.releaseMmio ||
                !m_services.releaseMmio(m_mmio, m_mmioLength)) {
                m_cleanupPending = true;
                return false;
            }
            m_mmio = nullptr;
        }
        if (m_claimed) {
            if (!m_services.releasePci || !m_services.releasePci(m_claimToken)) {
                m_cleanupPending = true;
                return false;
            }
            m_claimed = false;
            m_claimToken = 0;
        }
        m_initialized = false;
        m_linkUp = false;
        m_cleanupPending = false;
        return true;
    }

public:
    static BareMetalI210Driver& get() noexcept {
        static BareMetalI210Driver instance;
        return instance;
    }

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isLinkUp() const noexcept { return m_linkUp; }
    [[nodiscard]] uint64_t getLinkSpeedBps() const noexcept {
        if (!m_initialized || !m_linkUp) return 0;
        switch ((readReg(I210_REG_STATUS) >> 6) & 3u) {
            case 0: return 10'000'000;
            case 1: return 100'000'000;
            case 2: return 1'000'000'000;
            default: return 0;
        }
    }
    [[nodiscard]] const uint8_t* getMac() const noexcept { return m_mac; }
    [[nodiscard]] uint64_t getRxPackets() const noexcept { return m_rxPackets; }
    [[nodiscard]] uint64_t getTxPackets() const noexcept { return m_txPackets; }
    [[nodiscard]] uint32_t getRDH() const noexcept { return m_initialized ? readReg(I210_REG_RDH) : 0; }
    [[nodiscard]] uint32_t getRDT() const noexcept { return m_initialized ? readReg(I210_REG_RDT) : 0; }
    [[nodiscard]] uint32_t getRXDCTL() const noexcept { return m_initialized ? readReg(I210_REG_RXDCTL) : 0; }
    [[nodiscard]] uint32_t getSRRCTL() const noexcept { return m_initialized ? readReg(I210_REG_SRRCTL0) : 0; }
    [[nodiscard]] uint8_t  getDesc0Status() const noexcept { return m_dma ? m_dma->rxRing[0].status : 0; }
    [[nodiscard]] uint32_t getRxTail() const noexcept { return m_rxTail; }

    bool shutdown() noexcept {
        if (!m_claimed && !m_mmio && !m_initialized) return !m_cleanupPending;
        return rollbackAndRelease();
    }

    void registerReceiveCallback(void* context, ReceiveFrameCallback callback) noexcept {
        m_receiveContext = context;
        m_receiveCallback = callback;
    }

    bool initialize(const HardwareServices& services) noexcept {
        if (m_initialized) return true;
        if (m_cleanupPending && !rollbackAndRelease()) return false;
        if (!services.readPci32 || !services.writePci32 || !services.claimPci ||
            !services.releasePci || !services.mapMmio || !services.releaseMmio ||
            !services.allocateDma || !services.releaseDma || !services.queryClock || !services.delayUs) return false;
        m_services = services;

        uint32_t targetBdf = UINT32_MAX;
        for (uint32_t bus = 0; bus <= UINT8_MAX && targetBdf == UINT32_MAX; ++bus) {
            for (uint32_t device = 0; device < 32 && targetBdf == UINT32_MAX; ++device) {
                for (uint32_t function = 0; function < 8; ++function) {
                    const uint32_t bdf = (bus << 8) | (device << 3) | function;
                    const uint32_t vendorDevice = services.readPci32(bdf, 0x00);
                    if (static_cast<uint16_t>(vendorDevice) == 0x8086 &&
                        static_cast<uint16_t>(vendorDevice >> 16) == 0x1533) {
                        targetBdf = bdf;
                        break;
                    }
                }
            }
        }
        if (targetBdf == UINT32_MAX) return false;

        // Io/PnP owns BAR decoding and serialization. No config write, BAR
        // access, or MMIO mapping is allowed until this claim succeeds.
        m_pciBdf = targetBdf;
        uint64_t barLength{};
        const bool claimed = services.claimPci(targetBdf, 0, 0x20000, &m_claimToken,
                                               &m_mmioPhysical, &barLength);
        if (m_claimToken) m_claimed = true;
        if (!claimed || !m_claimed || !m_mmioPhysical || barLength < 0x20000 ||
            m_mmioPhysical > UINT64_MAX - 0x20000u) {
            if (m_claimed) (void)rollbackAndRelease();
            return false;
        }
        m_mmioLength = 0x20000;
        const uint32_t commandAndStatus = services.readPci32(targetBdf, 0x04);
        if (commandAndStatus == UINT32_MAX) {
            (void)rollbackAndRelease();
            return false;
        }
        // PCI Status bits are write-one-to-clear. Never echo the upper half of
        // the Command/Status dword into a command update.
        m_originalPciCommand = commandAndStatus & 0xFFFFu;
        m_mmio = services.mapMmio(m_mmioPhysical, m_mmioLength);
        if (!m_mmio) {
            (void)rollbackAndRelease();
            return false;
        }

        // One pinned common buffer holds both rings and all frame buffers.
        void* cpu{};
        if (!services.allocateDma(m_claimToken, sizeof(DmaStorage), alignof(DmaStorage),
                                  &cpu, &m_dmaBase, &m_dmaToken) || !cpu || !m_dmaToken ||
            !m_dmaBase || m_dmaBase > UINT64_MAX - (sizeof(DmaStorage) - 1)) {
            (void)rollbackAndRelease();
            return false;
        }
        m_dma = static_cast<DmaStorage*>(cpu);
        m_txRingDma = m_dmaBase + offsetof(DmaStorage, txRing);
        m_rxRingDma = m_dmaBase + offsetof(DmaStorage, rxRing);
        for (size_t i = 0; i < NUM_TX_DESC; ++i) {
            m_txBufferDma[i] = m_dmaBase + offsetof(DmaStorage, txBuffers) + i * RX_BUFFER_SIZE;
            m_rxBufferDma[i] = m_dmaBase + offsetof(DmaStorage, rxBuffers) + i * RX_BUFFER_SIZE;
        }

        m_commandChanged = true;
        if (!services.writePci32(targetBdf, 0x04,
                                 m_originalPciCommand | 0x00000002u)) {
            (void)rollbackAndRelease();
            return false;
        }
        const uint32_t memoryCommand = services.readPci32(targetBdf, 0x04);
        if (memoryCommand == UINT32_MAX || (memoryCommand & 0x00000002u) == 0) {
            (void)rollbackAndRelease();
            return false;
        }
        m_mmioActive = true;

        // 5. Disable interrupts
        writeReg(I210_REG_IMC, 0xFFFFFFFF);
        (void)readReg(I210_REG_ICR);

        // 6. Read Hardware MAC Address from RAL0/RAH0
        uint32_t ral0 = readReg(I210_REG_RAL0);
        uint32_t rah0 = readReg(I210_REG_RAH0);
        if (rah0 & (1u << 31)) { // Address Valid bit
            m_mac[0] = static_cast<uint8_t>(ral0 & 0xFF);
            m_mac[1] = static_cast<uint8_t>((ral0 >> 8) & 0xFF);
            m_mac[2] = static_cast<uint8_t>((ral0 >> 16) & 0xFF);
            m_mac[3] = static_cast<uint8_t>((ral0 >> 24) & 0xFF);
            m_mac[4] = static_cast<uint8_t>(rah0 & 0xFF);
            m_mac[5] = static_cast<uint8_t>((rah0 >> 8) & 0xFF);
        }
        bool validMac = (m_mac[0] & 1u) == 0;
        bool nonzeroMac = false;
        for (const uint8_t octet : m_mac) nonzeroMac |= octet != 0;
        if (!validMac || !nonzeroMac) {
            (void)rollbackAndRelease();
            return false;
        }

        // Bus mastering is enabled only after every descriptor and packet
        // buffer has a validated contiguous DMA address.
        m_busMasterEnabled = true;
        if (!services.writePci32(targetBdf, 0x04,
                                 m_originalPciCommand | 0x00000006u)) {
            (void)rollbackAndRelease();
            return false;
        }
        const uint32_t busMasterCommand = services.readPci32(targetBdf, 0x04);
        if (busMasterCommand == UINT32_MAX || (busMasterCommand & 0x00000006u) != 0x6u) {
            (void)rollbackAndRelease();
            return false;
        }
        const auto failAfterBusMaster = [&]() noexcept {
            (void)rollbackAndRelease();
            return false;
        };

        // 6b. Inform Management Controller / NC-SI that Host Driver is Loaded
        uint32_t ctrlExt = readReg(I210_REG_CTRL_EXT);
        writeReg(I210_REG_CTRL_EXT, ctrlExt | CTRL_EXT_DRV_LOAD);

        // 6c. Clear Multicast Table Array (MTA)
        for (uint32_t m = 0; m < 128; ++m) {
            writeReg(0x05200 + m * 4, 0);
        }

        // 7. Assert Set Link Up (SLU) in Device Control
        uint32_t ctrl = readReg(I210_REG_CTRL);
        writeReg(I210_REG_CTRL, ctrl | CTRL_SLU);

        // 8. Initialize Transmit Descriptors & Ring
        std::memset(m_dma->txRing, 0, sizeof(m_dma->txRing));
        for (auto& descriptor : m_dma->txRing) descriptor.status = 1; // DD: ring entries are initially available.
        __atomic_thread_fence(__ATOMIC_RELEASE);
        uint64_t txRingPhys = m_txRingDma;
        writeReg(I210_REG_TDBAL, static_cast<uint32_t>(txRingPhys & 0xFFFFFFFF));
        writeReg(I210_REG_TDBAH, static_cast<uint32_t>((txRingPhys >> 32) & 0xFFFFFFFF));
        writeReg(I210_REG_TDLEN, sizeof(m_dma->txRing));

        // Enable TX Queue and poll until hardware confirms
        writeReg(I210_REG_TXDCTL, (1u << 25));
        if (!waitRegister(I210_REG_TXDCTL, 1u << 25, 1u << 25, 100))
            return failAfterBusMaster();
        writeReg(I210_REG_TDH, 0);
        writeReg(I210_REG_TDT, 0);

        writeReg(I210_REG_TIPG, 10 | (10 << 10) | (10 << 20));
        writeReg(I210_REG_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD);
        if (!waitRegister(I210_REG_TCTL, TCTL_EN, TCTL_EN, 100))
            return failAfterBusMaster();

        // 9. Initialize Receive Descriptors & Ring
        std::memset(m_dma->rxRing, 0, sizeof(m_dma->rxRing));
        for (size_t i = 0; i < NUM_RX_DESC; ++i) {
            uint64_t bufPhys = m_rxBufferDma[i];
            m_dma->rxRing[i].buffer_addr = bufPhys;
            m_dma->rxRing[i].status = 0;
        }
        __atomic_thread_fence(__ATOMIC_RELEASE);

        uint64_t rxRingPhys = m_rxRingDma;
        writeReg(I210_REG_RDBAL, static_cast<uint32_t>(rxRingPhys & 0xFFFFFFFF));
        writeReg(I210_REG_RDBAH, static_cast<uint32_t>((rxRingPhys >> 32) & 0xFFFFFFFF));
        writeReg(I210_REG_RDLEN, sizeof(m_dma->rxRing));

        // Configure SRRCTL (2 KB buffer, drop enable on queue full, DESCTYPE=000b Legacy)
        writeReg(I210_REG_SRRCTL0, 2 | (1u << 31));

        // Enable RX Queue and poll until hardware confirms (Section 4.6.4)
        writeReg(I210_REG_RXDCTL, (1u << 25));
        if (!waitRegister(I210_REG_RXDCTL, 1u << 25, 1u << 25, 100))
            return failAfterBusMaster();

        // Set Head & Tail AFTER Queue is Enabled!
        writeReg(I210_REG_RDH, 0);
        writeReg(I210_REG_RDT, static_cast<uint32_t>(NUM_RX_DESC - 1));

        writeReg(I210_REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);
        if (!waitRegister(I210_REG_RCTL, RCTL_EN, RCTL_EN, 100))
            return failAfterBusMaster();

        // 10. Check Link Status
        uint32_t status = readReg(I210_REG_STATUS);
        m_linkUp = (status & STATUS_LU) != 0;
        m_initialized = true;

        return true;
    }

    /**
     * @brief Transmit an Ethernet frame via bus-master DMA.
     */
    bool sendPacket(const void* data, uint16_t length) noexcept {
        if (!m_initialized || !data || length == 0 || length > RX_BUFFER_SIZE) {
            return false;
        }

        uint16_t txLen = (length < 60) ? 60 : length; // Guarantee minimum IEEE 802.3 frame size (prevents datacenter switch drops)

        uint32_t cur = m_txTail;
        uint32_t next = (cur + 1) % NUM_TX_DESC;
        if ((m_dma->txRing[cur].status & 1u) == 0) return false;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);

        if (txLen > length) {
            std::memset(m_dma->txBuffers[cur], 0, txLen);
        }
        std::memcpy(m_dma->txBuffers[cur], data, length);

        m_dma->txRing[cur].buffer_addr = m_txBufferDma[cur];
        m_dma->txRing[cur].length = txLen;
        m_dma->txRing[cur].cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP | IFCS | RS
        m_dma->txRing[cur].status = 0;
        m_dma->txRing[cur].cso = 0;
        m_dma->txRing[cur].css = 0;
        m_dma->txRing[cur].special = 0;

        __atomic_thread_fence(__ATOMIC_RELEASE);
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("sfence" ::: "memory");
#endif
        m_txTail = next;
        writeReg(I210_REG_TDT, next);

        if (!waitTxCompletion(cur, 1000)) return false;
        m_txPackets++;

        return true;
    }

    /**
     * @brief Poll completed hardware descriptors and pass real Ethernet frames
     *        to the registered network-stack owner.
     */
    void pollRx() noexcept {
        if (!m_initialized) return;

        while ((m_dma->rxRing[m_rxTail].status & 1) != 0) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
#if defined(_MSC_VER)
            _mm_lfence();
#elif defined(__x86_64__)
            __asm__ __volatile__("lfence" ::: "memory");
#endif
            volatile I210RxDesc* desc = &m_dma->rxRing[m_rxTail];
            uint16_t pktLen = desc->length;
            const uint8_t descriptorStatus = desc->status;
            const uint8_t descriptorErrors = desc->errors;
            if ((descriptorStatus & 0x02u) != 0 && descriptorErrors == 0 &&
                pktLen >= sizeof(EthernetHeader) && pktLen <= RX_BUFFER_SIZE) {
                m_rxPackets++;
                const uint8_t* pkt = m_dma->rxBuffers[m_rxTail];
                if (m_receiveCallback) m_receiveCallback(m_receiveContext, pkt, pktLen);
            }

            // Re-arm descriptor
            desc->status = 0;
            __atomic_thread_fence(__ATOMIC_RELEASE);
#if defined(_MSC_VER)
            _mm_mfence();
#elif defined(__x86_64__)
            __asm__ __volatile__("mfence" ::: "memory");
#endif
            uint32_t prevTail = m_rxTail;
            m_rxTail = (m_rxTail + 1) % NUM_RX_DESC;
            writeReg(I210_REG_RDT, prevTail);
        }
    }

};

} // namespace micant::oem::i210
