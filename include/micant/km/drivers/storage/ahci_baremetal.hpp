// ============================================================================
// MicaNT: Bare-Metal Intel C230/C236 SATA AHCI Storage Controller Driver
// (ahci_baremetal.hpp)
//
// Description:
//   Clean-room bare-metal AHCI 1.3.1 bus-master DMA driver for the Intel
//   C230/C236 Chipset SATA Controller (0x8086:0xA102 at PCI 00:17.0).
//   Enables direct physical sector reads and writes on an initialized SATA port,
//   HGST Ultrastar 2.0 TB SATA Enterprise HDD) in pure Ring 0 Long Mode.
//
//   Features:
//   - PCI Configuration Space discovery (BAR5 ABAR MMIO at 0xF7240000)
//   - Bus Master & Memory Space enablement
//   - Global Host Control (GHC.AE = 1, AHCI mode active)
//   - ATA SATA disk detection across implemented AHCI ports
//   - 1KB-aligned Command List & 256-byte aligned Received FIS DMA buffers
//   - ATA-8 48-bit LBA DMA Read/Write (READ_DMA_EXT 0x25, WRITE_DMA_EXT 0x35)
//   - Physical Region Descriptor Table (PRDT) multi-sector transfers (up to 64KB)
//   - GPT Partition Table resolution: locates Partition 2 (MICA_SYS at LBA 2,099,200)
//   - FAT32 BPB parsing & root directory file ingestion (C:\MicaNT\system.ini)
//
// Clean-Room ISO C++23. Zero proprietary Microsoft code.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <cstring>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"

namespace micant::km::ahci_baremetal {

// --- AHCI Register Offsets ---
inline constexpr uint32_t AHCI_REG_CAP  = 0x00; // Host Capabilities
inline constexpr uint32_t AHCI_REG_GHC  = 0x04; // Global Host Control
inline constexpr uint32_t AHCI_REG_IS   = 0x08; // Interrupt Status
inline constexpr uint32_t AHCI_REG_PI   = 0x0C; // Ports Implemented
inline constexpr uint32_t AHCI_REG_VS   = 0x10; // Version

inline constexpr uint32_t AHCI_GHC_AE   = (1u << 31); // AHCI Enable
inline constexpr uint32_t AHCI_GHC_IE   = (1u << 1);  // Interrupt Enable
inline constexpr uint32_t AHCI_GHC_HR   = (1u << 0);  // HBA Reset

// --- AHCI Port Register Offsets (Base: ABAR + 0x100 + port * 0x80) ---
inline constexpr uint32_t PORT_REG_CLB  = 0x00; // Command List Base Low
inline constexpr uint32_t PORT_REG_CLBU = 0x04; // Command List Base High
inline constexpr uint32_t PORT_REG_FB   = 0x08; // FIS Base Low
inline constexpr uint32_t PORT_REG_FBU  = 0x0C; // FIS Base High
inline constexpr uint32_t PORT_REG_IS   = 0x10; // Interrupt Status
inline constexpr uint32_t PORT_REG_IE   = 0x14; // Interrupt Enable
inline constexpr uint32_t PORT_REG_CMD  = 0x18; // Command and Status
inline constexpr uint32_t PORT_REG_TFD  = 0x20; // Task File Data
inline constexpr uint32_t PORT_REG_SIG  = 0x24; // Signature
inline constexpr uint32_t PORT_REG_SSTS = 0x28; // SATA Status (SSTS)
inline constexpr uint32_t PORT_REG_SCTL = 0x2C; // SATA Control (SCTL)
inline constexpr uint32_t PORT_REG_SERR = 0x30; // SATA Error (SERR)
inline constexpr uint32_t PORT_REG_SACT = 0x34; // SATA Active (NCQ)
inline constexpr uint32_t PORT_REG_CI   = 0x38; // Command Issue

inline constexpr uint32_t PORT_CMD_ST   = (1u << 0);  // Start
inline constexpr uint32_t PORT_CMD_FRE  = (1u << 4);  // FIS Receive Enable
inline constexpr uint32_t PORT_CMD_FR   = (1u << 14); // FIS Receive Running
inline constexpr uint32_t PORT_CMD_CR   = (1u << 15); // Command List Running

// --- ATA Commands ---
inline constexpr uint8_t ATA_CMD_READ_DMA_EXT   = 0x25;
inline constexpr uint8_t ATA_CMD_WRITE_DMA_EXT  = 0x35;
inline constexpr uint8_t ATA_CMD_IDENTIFY       = 0xEC;
inline constexpr uint8_t ATA_CMD_FLUSH_CACHE    = 0xE7;
inline constexpr uint8_t ATA_CMD_FLUSH_CACHE_EXT = 0xEA;

#pragma pack(push, 1)

// --- AHCI Command Header (32 bytes per slot) ---
struct AhciCommandHeader {
    uint8_t  cfl : 5;      // Command FIS Length in DWORDS (usually 5)
    uint8_t  a : 1;        // ATAPI
    uint8_t  w : 1;        // Write (1 = Device <- Host, 0 = Device -> Host)
    uint8_t  p : 1;        // Prefetchable
    uint8_t  r : 1;        // Reset
    uint8_t  b : 1;        // BIST
    uint8_t  c : 1;        // Clear Busy upon R_OK
    uint8_t  reserved0 : 1;
    uint8_t  pmp : 4;      // Port Multiplier Port
    uint16_t prdtl;        // Physical Region Descriptor Table Length (# of entries)
    volatile uint32_t prdbc;// Physical Region Descriptor Byte Count transferred
    uint32_t ctba;         // Command Table Base Address Low
    uint32_t ctbau;        // Command Table Base Address High
    uint32_t reserved1[4];
};

// --- AHCI PRDT Entry (16 bytes per memory chunk) ---
struct AhciPrdtEntry {
    uint32_t dba;          // Data Base Address Low
    uint32_t dbau;         // Data Base Address High
    uint32_t reserved0;
    uint32_t dbc : 22;     // Data Byte Count (0-indexed: size - 1)
    uint32_t reserved1 : 9;
    uint32_t i : 1;        // Interrupt on completion
};

// --- FIS Type 0x27: Register Host to Device ---
struct FisRegH2D {
    uint8_t  fis_type;     // 0x27
    uint8_t  pmport_c;     // Port Multiplier (bits 3:0), Command/Control (bit 7: 1 = Command)
    uint8_t  command;      // ATA Command (0x25 READ_DMA_EXT)
    uint8_t  featurel;     // Feature Low
    uint8_t  lba0;         // LBA 7:0
    uint8_t  lba1;         // LBA 15:8
    uint8_t  lba2;         // LBA 23:16
    uint8_t  device;       // 1 << 6 (LBA mode)
    uint8_t  lba3;         // LBA 31:24
    uint8_t  lba4;         // LBA 39:32
    uint8_t  lba5;         // LBA 47:40
    uint8_t  featureh;     // Feature High
    uint8_t  countl;       // Sector Count 7:0
    uint8_t  counth;       // Sector Count 15:8
    uint8_t  icc;          // Isochronous Command Completion
    uint8_t  control;      // Control register
    uint8_t  reserved[4];
};

// --- Command Table (Command FIS + ATAPI command + PRDT entries) ---
struct AhciCommandTable {
    uint8_t       cfis[64];       // Command FIS (typically FisRegH2D)
    uint8_t       acmd[16];       // ATAPI Command
    uint8_t       reserved[48];
    AhciPrdtEntry prdt[8];        // Up to 8 PRD entries
};

#pragma pack(pop)

// --- DMA Static Work Buffers ---
alignas(1024) static uint8_t s_ahciCmdListMem[1024];       // 32 slots * 32 bytes
alignas(256)  static uint8_t s_ahciRecvFisMem[256];        // 256 bytes
alignas(128)  static AhciCommandTable s_ahciCmdTable;      // Slot 0 Command Table
alignas(4096) static uint8_t s_ahciDmaBuffer[65536];       // 64 KB DMA Transfer Buffer

/**
 * @brief Sovereign Bare-Metal AHCI SATA Controller & disk storage engine.
 */
class BareMetalAhciController {
public:
    using MmioMapper = void* (*)(uint64_t physical, size_t size) noexcept;
    using DmaTranslator = uint64_t (*)(const void* address, size_t length) noexcept;
    using PciRead32 = bool (*)(uint8_t bus, uint8_t device, uint8_t function,
                              uint8_t offset, uint32_t& value) noexcept;
    using PciWrite32 = bool (*)(uint8_t bus, uint8_t device, uint8_t function,
                               uint8_t offset, uint32_t value) noexcept;

private:
    uint8_t m_pciBus{0};
    uint8_t m_pciDevice{0};
    uint8_t m_pciFunction{0};
    uint64_t m_abarPhys{0};
    volatile uint32_t* m_abar{nullptr};
    bool     m_initialized{false};
    bool     m_port1Online{false};
    bool     m_supports64BitDma{false};
    uint8_t  m_dataPort{0xFF};
    uint64_t m_port1TotalSectors{0};
    bool m_supportsFlushCache{false};
    bool m_supportsFlushCacheExt{false};
    MmioMapper m_mmioMapper{};
    DmaTranslator m_dmaTranslator{};
    PciRead32 m_pciRead32{};
    PciWrite32 m_pciWrite32{};
    char     m_diskModel[41]{};

public:
    static BareMetalAhciController& get() noexcept {
        static BareMetalAhciController instance;
        return instance;
    }

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool isPort1Online() const noexcept { return m_port1Online; }
    [[nodiscard]] uint64_t getAbarPhysical() const noexcept { return m_abarPhys; }
    [[nodiscard]] uint64_t getPort1TotalSectors() const noexcept { return m_port1TotalSectors; }
    [[nodiscard]] const char* getDiskModel() const noexcept { return m_diskModel; }

    /** Discover and initialize an AHCI controller and its first SATA disk port. */
    bool initialize(MmioMapper mmioMapper, DmaTranslator dmaTranslator,
                    PciRead32 pciRead32, PciWrite32 pciWrite32) noexcept {
        if (m_initialized) return true;
        if (!mmioMapper || !dmaTranslator || !pciRead32 || !pciWrite32) return false;
        m_mmioMapper = mmioMapper;
        m_dmaTranslator = dmaTranslator;
        m_pciRead32 = pciRead32;
        m_pciWrite32 = pciWrite32;
        // Controller locations differ between the target C236 platform
        // (00:17.0) and Q35/ICH9 (00:1f.2), so select by PCI class instead
        // of assuming one chipset's BDF.
        bool foundController = false;
        for (uint16_t bus = 0; bus <= UINT8_MAX && !foundController; ++bus) {
          for (uint16_t device = 0; device < 32 && !foundController; ++device) {
            for (uint8_t function = 0; function < 8; ++function) {
                uint32_t vendorDev{};
                if (!m_pciRead32(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                                 function, 0x00, vendorDev)) return false;
                if (static_cast<uint16_t>(vendorDev) == 0xFFFF) continue;
                uint32_t classReg{};
                if (!m_pciRead32(static_cast<uint8_t>(bus), static_cast<uint8_t>(device),
                                 function, 0x08, classReg)) return false;
                const uint8_t programmingInterface = static_cast<uint8_t>((classReg >> 8) & 0xFF);
                const uint8_t subclass = static_cast<uint8_t>((classReg >> 16) & 0xFF);
                const uint8_t baseClass = static_cast<uint8_t>((classReg >> 24) & 0xFF);
                if (baseClass == 0x01 && subclass == 0x06 && programmingInterface == 0x01) {
                    m_pciBus = static_cast<uint8_t>(bus);
                    m_pciDevice = static_cast<uint8_t>(device);
                    m_pciFunction = function;
                    foundController = true;
                    break;
                }
            }
          }
        }
        if (!foundController) return false;

        // Read BAR5 (ABAR MMIO) from the discovered controller.
        uint32_t bar5{};
        if (!m_pciRead32(m_pciBus, m_pciDevice, m_pciFunction, 0x24, bar5)) return false;
        const uint32_t barType = (bar5 >> 1) & 3u;
        if ((bar5 & 1u) != 0 || barType == 1u || barType == 3u || (bar5 & 8u) != 0)
            return false;
        uint64_t abarPhysical = bar5 & ~0xFULL;
        if (barType == 2u) {
            uint32_t bar6{};
            if (!m_pciRead32(m_pciBus, m_pciDevice, m_pciFunction, 0x28, bar6)) return false;
            abarPhysical |= static_cast<uint64_t>(bar6) << 32;
        }
        if (!abarPhysical || (abarPhysical & 0xF) != 0 || abarPhysical > UINT64_MAX - 0x1100)
            return false;
        m_abarPhys = abarPhysical;

        // Enable Bus Master, Memory Space, and I/O Space.
        uint32_t pciCmd{};
        if (!m_pciRead32(m_pciBus, m_pciDevice, m_pciFunction, 0x04, pciCmd) ||
            !m_pciWrite32(m_pciBus, m_pciDevice, m_pciFunction, 0x04,
                          (pciCmd & 0xFFFFu) | 0x0006u))
            return false;

        // Map the register window through the kernel's uncached MMIO mapper.
        m_abar = static_cast<volatile uint32_t*>(
            m_mmioMapper(m_abarPhys, 0x1100));
        if (!m_abar) return false;

        // 5. Ensure AHCI Enable (GHC.AE = 1)
        uint32_t ghc = m_abar[AHCI_REG_GHC / 4];
        if ((ghc & AHCI_GHC_AE) == 0) {
            m_abar[AHCI_REG_GHC / 4] = ghc | AHCI_GHC_AE;
        }
        m_supports64BitDma = (m_abar[AHCI_REG_CAP / 4] & (1u << 31)) != 0;

        // Find the first implemented SATA disk port. Firmware and QEMU
        // commonly attach the boot disk to port zero, while some systems use
        // port one; do not assume a physical connector number.
        uint32_t pi = m_abar[AHCI_REG_PI / 4];
        for (uint8_t port = 0; port < 32; ++port) {
            if ((pi & (uint32_t{1} << port)) == 0) continue;
            if (!initPort(port)) continue;
            m_dataPort = port;
            m_port1Online = true;
            if (readDiskIdentify()) {
                m_initialized = true;
                break;
            }
            m_port1Online = false;
            m_dataPort = 0xFF;
        }

        return m_initialized;
    }

private:
    uint64_t dmaAddress(const void* address, size_t length) const noexcept {
        const uint64_t physical = m_dmaTranslator ? m_dmaTranslator(address, length) : 0;
        if (!physical || (!m_supports64BitDma && physical > UINT32_MAX)) return 0;
        if (!m_supports64BitDma && length && length - 1 > UINT32_MAX - physical) return 0;
        return physical;
    }

    volatile uint32_t* getPortRegs(uint8_t port) const noexcept {
        uintptr_t portBase = reinterpret_cast<uintptr_t>(m_abar) + 0x100 + port * 0x80;
        return reinterpret_cast<volatile uint32_t*>(portBase);
    }

    bool stopPort(volatile uint32_t* pRegs) noexcept {
        pRegs[PORT_REG_CMD / 4] &= ~PORT_CMD_ST;
        pRegs[PORT_REG_CMD / 4] &= ~PORT_CMD_FRE;

        // Wait for CR and FR to clear (up to 500ms)
        for (int i = 0; i < 50000; ++i) {
            uint32_t cmd = pRegs[PORT_REG_CMD / 4];
            if ((cmd & (PORT_CMD_CR | PORT_CMD_FR)) == 0) {
                return true;
            }
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
        return false;
    }

    bool startPort(volatile uint32_t* pRegs) noexcept {
        // Wait until CR is clear before starting
        bool stopped = false;
        for (uint32_t attempt = 0; attempt < 50000; ++attempt) {
            if ((pRegs[PORT_REG_CMD / 4] & PORT_CMD_CR) == 0) {
                stopped = true;
                break;
            }
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
        if (!stopped) return false;
        pRegs[PORT_REG_CMD / 4] |= PORT_CMD_FRE;
        pRegs[PORT_REG_CMD / 4] |= PORT_CMD_ST;
        return true;
    }

    bool initPort(uint8_t port) noexcept {
        volatile uint32_t* pRegs = getPortRegs(port);

        // Check SSTS
        uint32_t ssts = pRegs[PORT_REG_SSTS / 4];
        uint8_t det = ssts & 0x0F;
        if (det != 3) {
            return false; // No device present
        }

        uint32_t sig = pRegs[PORT_REG_SIG / 4];
        if (sig != 0x00000101) {
            // Not a SATA disk (e.g. ATAPI)
            return false;
        }

        // Stop port command engine
        if (!stopPort(pRegs)) return false;

        // Zero DMA work buffers
        std::memset(s_ahciCmdListMem, 0, sizeof(s_ahciCmdListMem));
        std::memset(s_ahciRecvFisMem, 0, sizeof(s_ahciRecvFisMem));
        std::memset(&s_ahciCmdTable, 0, sizeof(s_ahciCmdTable));

        // Program Command List and Received FIS Base
        uint64_t clbPhys = dmaAddress(s_ahciCmdListMem, sizeof(s_ahciCmdListMem));
        uint64_t fbPhys  = dmaAddress(s_ahciRecvFisMem, sizeof(s_ahciRecvFisMem));
        if (!clbPhys || !fbPhys) return false;

        pRegs[PORT_REG_CLB / 4]  = static_cast<uint32_t>(clbPhys & 0xFFFFFFFF);
        pRegs[PORT_REG_CLBU / 4] = static_cast<uint32_t>((clbPhys >> 32) & 0xFFFFFFFF);
        pRegs[PORT_REG_FB / 4]   = static_cast<uint32_t>(fbPhys & 0xFFFFFFFF);
        pRegs[PORT_REG_FBU / 4]  = static_cast<uint32_t>((fbPhys >> 32) & 0xFFFFFFFF);

        // Setup Command Header 0 pointing to s_ahciCmdTable
        auto* cmdHeaders = reinterpret_cast<AhciCommandHeader*>(s_ahciCmdListMem);
        uint64_t ctbaPhys = dmaAddress(&s_ahciCmdTable, sizeof(s_ahciCmdTable));
        if (!ctbaPhys) return false;
        cmdHeaders[0].ctba  = static_cast<uint32_t>(ctbaPhys & 0xFFFFFFFF);
        cmdHeaders[0].ctbau = static_cast<uint32_t>((ctbaPhys >> 32) & 0xFFFFFFFF);
        cmdHeaders[0].prdtl = 1;

        // Clear error and interrupt registers
        pRegs[PORT_REG_SERR / 4] = 0xFFFFFFFF;
        pRegs[PORT_REG_IS / 4]   = 0xFFFFFFFF;

        // Start port
        if (!startPort(pRegs)) return false;

        return true;
    }

    bool readDiskIdentify() noexcept {
        alignas(512) static uint8_t idBuf[512];
        std::memset(idBuf, 0, sizeof(idBuf));

        volatile uint32_t* pRegs = getPortRegs(m_dataPort);
        auto* cmdHeaders = reinterpret_cast<AhciCommandHeader*>(s_ahciCmdListMem);

        cmdHeaders[0].cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
        cmdHeaders[0].w = 0; // Read
        cmdHeaders[0].prdtl = 1;
        cmdHeaders[0].prdbc = 0;

        // Setup PRDT 0
        uint64_t bufPhys = dmaAddress(idBuf, sizeof(idBuf));
        if (!bufPhys) return false;
        s_ahciCmdTable.prdt[0].dba  = static_cast<uint32_t>(bufPhys & 0xFFFFFFFF);
        s_ahciCmdTable.prdt[0].dbau = static_cast<uint32_t>((bufPhys >> 32) & 0xFFFFFFFF);
        s_ahciCmdTable.prdt[0].dbc  = 512 - 1;
        s_ahciCmdTable.prdt[0].i    = 1;

        // Setup Command FIS
        std::memset(&s_ahciCmdTable.cfis, 0, sizeof(s_ahciCmdTable.cfis));
        auto* fis = reinterpret_cast<FisRegH2D*>(s_ahciCmdTable.cfis);
        fis->fis_type = 0x27;
        fis->pmport_c = 0x80; // Command bit = 1
        fis->command  = ATA_CMD_IDENTIFY;

        // Clear interrupt status & issue command
        pRegs[PORT_REG_IS / 4] = 0xFFFFFFFF;
        pRegs[PORT_REG_CI / 4] = 1; // Issue slot 0

        // Wait for completion (timeout ~100ms)
        bool completed = false;
        for (int i = 0; i < 100000; ++i) {
            if ((pRegs[PORT_REG_CI / 4] & 1) == 0) {
                completed = true;
                break;
            }
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
        if (!completed || (pRegs[PORT_REG_TFD / 4] & 1u) != 0) return false;

        // Parse Model String (Words 27..46, bytes 54..93, byte-swapped)
        size_t idx = 0;
        for (int w = 27; w <= 46 && idx < 40; ++w) {
            m_diskModel[idx++] = static_cast<char>(idBuf[w * 2 + 1]);
            m_diskModel[idx++] = static_cast<char>(idBuf[w * 2]);
        }
        m_diskModel[idx] = '\0';
        // Trim trailing spaces
        while (idx > 0 && (m_diskModel[idx - 1] == ' ' || m_diskModel[idx - 1] == '\0')) {
            m_diskModel[--idx] = '\0';
        }

        // Total 48-bit LBA sectors (Words 100..103, bytes 200..207)
        const uint16_t commandSet83 = *reinterpret_cast<const uint16_t*>(&idBuf[166]);
        const uint16_t commandSet82 = *reinterpret_cast<const uint16_t*>(&idBuf[164]);
        const bool commandSet83Valid = (commandSet83 & 0xC000u) == 0x4000u;
        m_supportsFlushCache = commandSet83Valid && (commandSet82 & 0x1000u) != 0;
        m_supportsFlushCacheExt = commandSet83Valid && (commandSet83 & 0x1000u) != 0;
        uint64_t sectorCount{};
        if ((commandSet83 & 0xC400u) == 0x4400u)
            std::memcpy(&sectorCount, &idBuf[200], sizeof(sectorCount));
        if (!sectorCount) {
            sectorCount = *reinterpret_cast<const uint16_t*>(&idBuf[120]) |
                (static_cast<uint64_t>(*reinterpret_cast<const uint16_t*>(&idBuf[122])) << 16);
            sectorCount &= 0x0FFFFFFFu;
        }
        if (!sectorCount) return false;
        m_port1TotalSectors = sectorCount;
        return true;
    }

public:
    /**
     * @brief Direct 48-bit LBA Sector Read via AHCI Bus-Master DMA.
     * @param lba Starting physical LBA on /dev/sdb.
     * @param sectorCount Number of 512-byte sectors to read (up to 128 = 64KB per call).
     * @param dest Destination memory buffer.
     * @return true on DMA transfer success, false on fault/timeout.
     */
    bool readSectors(uint64_t lba, uint16_t sectorCount, void* dest) noexcept {
        if (!m_port1Online || !dest || sectorCount == 0 || sectorCount > 128 ||
            !m_port1TotalSectors || lba >= m_port1TotalSectors ||
            sectorCount > m_port1TotalSectors - lba) {
            return false;
        }

        volatile uint32_t* pRegs = getPortRegs(m_dataPort);
        auto* cmdHeaders = reinterpret_cast<AhciCommandHeader*>(s_ahciCmdListMem);

        cmdHeaders[0].cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
        cmdHeaders[0].w = 0; // Read DMA
        cmdHeaders[0].prdtl = 1;
        cmdHeaders[0].prdbc = 0;

        const size_t transferBytes = static_cast<size_t>(sectorCount) * 512;
        uint64_t bufPhys = dmaAddress(dest, transferBytes);
        if (!bufPhys) return false;
        s_ahciCmdTable.prdt[0].dba  = static_cast<uint32_t>(bufPhys & 0xFFFFFFFF);
        s_ahciCmdTable.prdt[0].dbau = static_cast<uint32_t>((bufPhys >> 32) & 0xFFFFFFFF);
        s_ahciCmdTable.prdt[0].dbc  = static_cast<uint32_t>(transferBytes - 1);
        s_ahciCmdTable.prdt[0].i    = 1;

        std::memset(&s_ahciCmdTable.cfis, 0, sizeof(s_ahciCmdTable.cfis));
        auto* fis = reinterpret_cast<FisRegH2D*>(s_ahciCmdTable.cfis);
        fis->fis_type = 0x27;
        fis->pmport_c = 0x80;
        fis->command  = ATA_CMD_READ_DMA_EXT;

        fis->lba0 = static_cast<uint8_t>(lba & 0xFF);
        fis->lba1 = static_cast<uint8_t>((lba >> 8) & 0xFF);
        fis->lba2 = static_cast<uint8_t>((lba >> 16) & 0xFF);
        fis->device = 1 << 6; // LBA mode

        fis->lba3 = static_cast<uint8_t>((lba >> 24) & 0xFF);
        fis->lba4 = static_cast<uint8_t>((lba >> 32) & 0xFF);
        fis->lba5 = static_cast<uint8_t>((lba >> 40) & 0xFF);

        fis->countl = static_cast<uint8_t>(sectorCount & 0xFF);
        fis->counth = static_cast<uint8_t>((sectorCount >> 8) & 0xFF);

        pRegs[PORT_REG_IS / 4] = 0xFFFFFFFF;
        pRegs[PORT_REG_CI / 4] = 1; // Issue slot 0

        // Poll for completion (timeout ~200ms)
        for (int i = 0; i < 200000; ++i) {
            if ((pRegs[PORT_REG_CI / 4] & 1) == 0) {
                break;
            }
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }

        if (pRegs[PORT_REG_CI / 4] & 1) {
            return false; // Timeout
        }

        uint32_t tfd = pRegs[PORT_REG_TFD / 4];
        if (tfd & (1 | 0x20)) { // Error (bit 0) or Drive Fault (bit 5)
            return false;
        }

        return true;
    }

    /**
     * @brief Direct 48-bit LBA Sector Write via AHCI Bus-Master DMA.
     */
    bool writeSectors(uint64_t lba, uint16_t sectorCount, const void* src) noexcept {
        if (!m_port1Online || !src || sectorCount == 0 || sectorCount > 128 ||
            !m_port1TotalSectors || lba >= m_port1TotalSectors ||
            sectorCount > m_port1TotalSectors - lba) {
            return false;
        }

        volatile uint32_t* pRegs = getPortRegs(m_dataPort);
        auto* cmdHeaders = reinterpret_cast<AhciCommandHeader*>(s_ahciCmdListMem);

        cmdHeaders[0].cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
        cmdHeaders[0].w = 1; // Write DMA
        cmdHeaders[0].prdtl = 1;
        cmdHeaders[0].prdbc = 0;

        const size_t transferBytes = static_cast<size_t>(sectorCount) * 512;
        uint64_t bufPhys = dmaAddress(src, transferBytes);
        if (!bufPhys) return false;
        s_ahciCmdTable.prdt[0].dba  = static_cast<uint32_t>(bufPhys & 0xFFFFFFFF);
        s_ahciCmdTable.prdt[0].dbau = static_cast<uint32_t>((bufPhys >> 32) & 0xFFFFFFFF);
        s_ahciCmdTable.prdt[0].dbc  = static_cast<uint32_t>(transferBytes - 1);
        s_ahciCmdTable.prdt[0].i    = 1;

        std::memset(&s_ahciCmdTable.cfis, 0, sizeof(s_ahciCmdTable.cfis));
        auto* fis = reinterpret_cast<FisRegH2D*>(s_ahciCmdTable.cfis);
        fis->fis_type = 0x27;
        fis->pmport_c = 0x80;
        fis->command  = ATA_CMD_WRITE_DMA_EXT;

        fis->lba0 = static_cast<uint8_t>(lba & 0xFF);
        fis->lba1 = static_cast<uint8_t>((lba >> 8) & 0xFF);
        fis->lba2 = static_cast<uint8_t>((lba >> 16) & 0xFF);
        fis->device = 1 << 6;

        fis->lba3 = static_cast<uint8_t>((lba >> 24) & 0xFF);
        fis->lba4 = static_cast<uint8_t>((lba >> 32) & 0xFF);
        fis->lba5 = static_cast<uint8_t>((lba >> 40) & 0xFF);

        fis->countl = static_cast<uint8_t>(sectorCount & 0xFF);
        fis->counth = static_cast<uint8_t>((sectorCount >> 8) & 0xFF);

        pRegs[PORT_REG_IS / 4] = 0xFFFFFFFF;
        pRegs[PORT_REG_CI / 4] = 1;

        for (int i = 0; i < 200000; ++i) {
            if ((pRegs[PORT_REG_CI / 4] & 1) == 0) {
                break;
            }
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }

        if (pRegs[PORT_REG_CI / 4] & 1) {
            return false;
        }

        uint32_t tfd = pRegs[PORT_REG_TFD / 4];
        return (tfd & (1 | 0x20)) == 0;
    }

    // Drain the ATA device's volatile write cache. The drive must advertise
    // one of the IDENTIFY FLUSH CACHE command bits; otherwise durability is
    // explicitly unavailable. This command has no data phase.
    NtStatus flushCache() noexcept {
        if (!m_port1Online) return NtStatus::DeviceNotReady;
        if (!m_supportsFlushCacheExt && !m_supportsFlushCache)
            return NtStatus::NotSupported;

        volatile uint32_t* pRegs = getPortRegs(m_dataPort);
        auto* cmdHeaders = reinterpret_cast<AhciCommandHeader*>(s_ahciCmdListMem);
        cmdHeaders[0].cfl = sizeof(FisRegH2D) / sizeof(uint32_t);
        cmdHeaders[0].w = 0;
        cmdHeaders[0].prdtl = 0;
        cmdHeaders[0].prdbc = 0;

        std::memset(&s_ahciCmdTable.cfis, 0, sizeof(s_ahciCmdTable.cfis));
        auto* fis = reinterpret_cast<FisRegH2D*>(s_ahciCmdTable.cfis);
        fis->fis_type = 0x27;
        fis->pmport_c = 0x80;
        fis->command = m_supportsFlushCacheExt
            ? ATA_CMD_FLUSH_CACHE_EXT : ATA_CMD_FLUSH_CACHE;

        pRegs[PORT_REG_IS / 4] = 0xFFFFFFFF;
        pRegs[PORT_REG_CI / 4] = 1;
        for (int i = 0; i < 200000; ++i) {
            if ((pRegs[PORT_REG_CI / 4] & 1) == 0) break;
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
        if (pRegs[PORT_REG_CI / 4] & 1) return NtStatus::IoTimeout;
        return (pRegs[PORT_REG_TFD / 4] & (1u | 0x20u)) == 0
            ? NtStatus::Success : NtStatus::DeviceDataError;
    }

};

// --- Helper String Primitives for Freestanding FAT32 ---
static inline bool strEqualsIgnoreCase(const char* a, const char* b) noexcept {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static inline void copyStrSafe(char* dst, const char* src, size_t maxLen) noexcept {
    if (!dst || maxLen == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < maxLen) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static inline void format83Name(const uint8_t* raw11, char* outName, size_t maxLen) noexcept {
    if (!raw11 || !outName || maxLen == 0) return;
    size_t o = 0;
    size_t nameEnd = 8;
    while (nameEnd > 0 && raw11[nameEnd - 1] == ' ') --nameEnd;
    for (size_t i = 0; i < nameEnd && o + 1 < maxLen; ++i) {
        outName[o++] = static_cast<char>(raw11[i]);
    }
    size_t extEnd = 3;
    while (extEnd > 0 && raw11[8 + extEnd - 1] == ' ') --extEnd;
    if (extEnd > 0 && o + 1 < maxLen) {
        outName[o++] = '.';
        for (size_t i = 0; i < extEnd && o + 1 < maxLen; ++i) {
            outName[o++] = static_cast<char>(raw11[8 + i]);
        }
    }
    outName[o] = '\0';
}

// --- Sovereign Bare-Metal FAT32 Filesystem Subsystem ---

struct Fat32DirEntry {
    char     name[128]{};
    char     shortName[13]{};
    bool     isDirectory{false};
    uint32_t fileSize{0};
    uint32_t firstCluster{0};
};

struct Fat32BlockAccess {
    using Read = bool (*)(void* context, uint64_t lba, uint16_t sectorCount,
                          void* buffer) noexcept;
    using Write = bool (*)(void* context, uint64_t lba, uint16_t sectorCount,
                           const void* buffer) noexcept;
    void* context{};
    uint64_t totalSectors{};
    Read read{};
    Write write{};
};

class Fat32Volume {
private:
    Fat32BlockAccess m_device{};
    bool     m_mounted{false};
    uint64_t m_volumeLbaOffset{};
    char     m_driveLetter{'C'};
    uint16_t m_bytesPerSec{512};
    uint8_t  m_secPerClus{8};
    uint16_t m_reservedSec{32};
    uint8_t  m_numFats{2};
    uint32_t m_secPerFat{0};
    uint32_t m_rootClus{2};
    uint32_t m_fatStartLba{32};
    uint32_t m_dataStartLba{0};
    uint64_t m_totalSectors{0};
    bool m_readOnly{};
    char     m_volumeLabel[12]{};

    // 1-sector cache for FAT lookups
    alignas(512) uint8_t m_fatCache[512]{};
    uint32_t m_cachedFatSector{0xFFFFFFFF};

    // Cluster DMA work buffer (up to 64 KB = 128 sectors)
    alignas(4096) uint8_t m_clusterBuf[65536]{};

public:
    Fat32Volume() noexcept = default;
    Fat32Volume(uint64_t lbaOffset, char driveLetter) noexcept
        : m_volumeLbaOffset(lbaOffset), m_driveLetter(driveLetter) {}

    [[nodiscard]] bool isMounted() const noexcept { return m_mounted; }
    [[nodiscard]] char getDriveLetter() const noexcept { return m_driveLetter; }
    [[nodiscard]] const char* getVolumeLabel() const noexcept { return m_volumeLabel; }
    [[nodiscard]] uint64_t getTotalCapacityBytes() const noexcept {
        return m_totalSectors * static_cast<uint64_t>(m_bytesPerSec);
    }
    void unmount() noexcept {
        m_mounted = false;
        m_device = {};
        m_totalSectors = 0;
        m_cachedFatSector = 0xFFFFFFFF;
    }
    [[nodiscard]] uint32_t getBytesPerCluster() const noexcept {
        return static_cast<uint32_t>(m_bytesPerSec) * m_secPerClus;
    }
    [[nodiscard]] uint32_t getRootCluster() const noexcept { return m_rootClus; }

    bool readVolumeSectors(uint64_t relativeLba, uint16_t sectorCount, void* dest) noexcept {
        if (!m_device.read || !m_mounted || !dest || !sectorCount ||
            relativeLba >= m_totalSectors || sectorCount > m_totalSectors - relativeLba ||
            m_volumeLbaOffset > UINT64_MAX - relativeLba) return false;
        return m_device.read(m_device.context, m_volumeLbaOffset + relativeLba,
                             sectorCount, dest);
    }

    bool writeVolumeSectors(uint64_t relativeLba, uint16_t sectorCount, const void* src) noexcept {
        if (m_readOnly || !m_device.write || !src || !sectorCount ||
            relativeLba >= m_totalSectors || sectorCount > m_totalSectors - relativeLba ||
            m_volumeLbaOffset > UINT64_MAX - relativeLba) return false;
        return m_device.write(m_device.context, m_volumeLbaOffset + relativeLba,
                              sectorCount, src);
    }

    bool mount(Fat32BlockAccess device, uint64_t lbaOffset, char driveLetter,
               uint64_t partitionSectors = 0, bool readOnly = false) noexcept {
        m_mounted = false;
        m_device = device;
        m_volumeLbaOffset = lbaOffset;
        m_driveLetter = driveLetter;
        m_readOnly = readOnly;
        alignas(512) uint8_t bpb[512];
        if (!m_device.read || !m_device.read(m_device.context, lbaOffset, 1, bpb)) {
            return false;
        }
        if (bpb[510] != 0x55 || bpb[511] != 0xAA) {
            return false;
        }

        m_bytesPerSec = *reinterpret_cast<const uint16_t*>(&bpb[11]);
        m_secPerClus  = bpb[13];
        m_reservedSec = *reinterpret_cast<const uint16_t*>(&bpb[14]);
        m_numFats     = bpb[16];
        m_secPerFat   = *reinterpret_cast<const uint32_t*>(&bpb[36]);
        m_rootClus    = *reinterpret_cast<const uint32_t*>(&bpb[44]);

        uint32_t totSec32 = *reinterpret_cast<const uint32_t*>(&bpb[32]);
        uint16_t totSec16 = *reinterpret_cast<const uint16_t*>(&bpb[19]);
        m_totalSectors = (totSec32 != 0) ? totSec32 : totSec16;

        if (m_bytesPerSec != 512 || m_secPerClus == 0 || m_secPerClus > 128 ||
            (m_secPerClus & (m_secPerClus - 1)) != 0 || m_secPerFat == 0 ||
            m_numFats == 0 || m_totalSectors == 0 ||
            m_reservedSec >= m_totalSectors ||
            static_cast<uint64_t>(m_reservedSec) +
                static_cast<uint64_t>(m_numFats) * m_secPerFat >= m_totalSectors ||
            m_rootClus < 2) {
            return false;
        }
        if (lbaOffset >= m_device.totalSectors ||
            m_totalSectors > m_device.totalSectors - lbaOffset) return false;
        if (partitionSectors != 0 && m_totalSectors > partitionSectors) return false;

        std::memcpy(m_volumeLabel, &bpb[71], 11);
        m_volumeLabel[11] = '\0';
        for (int i = 10; i >= 0; --i) {
            if (m_volumeLabel[i] == ' ') m_volumeLabel[i] = '\0';
            else break;
        }

        m_fatStartLba  = m_reservedSec;
        m_dataStartLba = m_reservedSec + (m_numFats * m_secPerFat);
        m_cachedFatSector = 0xFFFFFFFF;
        m_mounted = true;
        const uint64_t dataClusters = (m_totalSectors - m_dataStartLba) / m_secPerClus;
        if (static_cast<uint64_t>(m_rootClus - 2) >= dataClusters) {
            m_mounted = false;
            return false;
        }
        return true;
    }

    bool mount(BareMetalAhciController& ahci, uint64_t lbaOffset, char driveLetter,
               uint64_t partitionSectors = 0, bool readOnly = false) noexcept {
        Fat32BlockAccess device{
            &ahci, ahci.getPort1TotalSectors(),
            [](void* context, uint64_t lba, uint16_t count, void* buffer) noexcept {
                return static_cast<BareMetalAhciController*>(context)->readSectors(
                    lba, count, buffer);
            },
            [](void* context, uint64_t lba, uint16_t count, const void* buffer) noexcept {
                return static_cast<BareMetalAhciController*>(context)->writeSectors(
                    lba, count, buffer);
            }
        };
        return mount(device, lbaOffset, driveLetter, partitionSectors, readOnly);
    }

    bool mount(BareMetalAhciController& ahci) noexcept {
        return mount(ahci, m_volumeLbaOffset, m_driveLetter);
    }

    uint32_t clusterToLba(uint32_t clus) const noexcept {
        return m_dataStartLba + (clus - 2) * m_secPerClus;
    }

    uint32_t getNextCluster(uint32_t clus) noexcept {
        if (!m_mounted || !m_device.read || clus < 2 || clus >= 0x0FFFFFF8) return 0x0FFFFFFF;
        uint32_t fatByteOffset = clus * 4;
        uint32_t fatSector = m_fatStartLba + (fatByteOffset / m_bytesPerSec);
        uint32_t entryOffset = fatByteOffset % m_bytesPerSec;

        if (m_cachedFatSector != fatSector) {
            if (!readVolumeSectors(fatSector, 1, m_fatCache)) {
                return 0x0FFFFFFF;
            }
            m_cachedFatSector = fatSector;
        }

        uint32_t nextClus = *reinterpret_cast<const uint32_t*>(&m_fatCache[entryOffset]) & 0x0FFFFFFF;
        return nextClus;
    }

    template<typename Callback>
    bool iterateDirectory(uint32_t dirClus, Callback&& cb) noexcept {
        if (!m_mounted || !m_device.read || dirClus < 2) return false;

        uint32_t currClus = dirClus;
        char lfnName[256]{};
        bool lfnActive = false;

        while (currClus >= 2 && currClus < 0x0FFFFFF8) {
            uint32_t lba = clusterToLba(currClus);
            uint16_t secToRead = m_secPerClus;
            if (secToRead > 128) secToRead = 128;

            if (!readVolumeSectors(lba, secToRead, m_clusterBuf)) {
                return false;
            }

            size_t totalEntries = (secToRead * m_bytesPerSec) / 32;
            for (size_t i = 0; i < totalEntries; ++i) {
                const uint8_t* ent = &m_clusterBuf[i * 32];
                if (ent[0] == 0x00) {
                    return true;
                }
                if (ent[0] == 0xE5) {
                    lfnActive = false;
                    lfnName[0] = '\0';
                    continue;
                }

                uint8_t attr = ent[11];
                if (attr == 0x0F) {
                    uint8_t seq = ent[0] & 0x1F;
                    if (seq >= 1 && seq <= 16) {
                        lfnActive = true;
                        size_t baseIdx = (seq - 1) * 13;
                        const size_t offsets[] = {
                            1, 3, 5, 7, 9,
                            14, 16, 18, 20, 22, 24,
                            28, 30
                        };
                        for (size_t c = 0; c < 13; ++c) {
                            uint16_t u = static_cast<uint16_t>(ent[offsets[c]]) |
                                         (static_cast<uint16_t>(ent[offsets[c] + 1]) << 8);
                            if (u == 0x0000 || u == 0xFFFF) {
                                lfnName[baseIdx + c] = '\0';
                                break;
                            }
                            lfnName[baseIdx + c] = (u < 128) ? static_cast<char>(u) : '_';
                        }
                    }
                    continue;
                }

                if (attr & 0x08) {
                    lfnActive = false;
                    lfnName[0] = '\0';
                    continue;
                }

                Fat32DirEntry dEntry{};
                dEntry.isDirectory = (attr & 0x10) != 0;
                uint16_t hi = *reinterpret_cast<const uint16_t*>(&ent[20]);
                uint16_t lo = *reinterpret_cast<const uint16_t*>(&ent[26]);
                dEntry.firstCluster = (static_cast<uint32_t>(hi) << 16) | lo;
                dEntry.fileSize = *reinterpret_cast<const uint32_t*>(&ent[28]);

                format83Name(ent, dEntry.shortName, sizeof(dEntry.shortName));

                if (lfnActive && lfnName[0] != '\0') {
                    copyStrSafe(dEntry.name, lfnName, sizeof(dEntry.name));
                } else {
                    copyStrSafe(dEntry.name, dEntry.shortName, sizeof(dEntry.name));
                }

                lfnActive = false;
                lfnName[0] = '\0';

                if (!cb(dEntry)) {
                    return true;
                }
            }

            currClus = getNextCluster(currClus);
        }

        return true;
    }

    bool findPath(const char* path, Fat32DirEntry& outEntry) noexcept {
        if (!m_mounted || !path) return false;

        const char* p = path;
        if ((p[0] == 'C' || p[0] == 'c' || p[0] == 'D' || p[0] == 'd') && p[1] == ':') {
            p += 2;
        }

        while (*p == '\\' || *p == '/') ++p;

        if (*p == '\0') {
            outEntry.name[0] = '\\';
            outEntry.name[1] = '\0';
            outEntry.shortName[0] = '\\';
            outEntry.shortName[1] = '\0';
            outEntry.isDirectory = true;
            outEntry.firstCluster = m_rootClus;
            outEntry.fileSize = 0;
            return true;
        }

        uint32_t curDirClus = m_rootClus;

        while (*p) {
            char segment[128]{};
            size_t segLen = 0;
            while (*p && *p != '\\' && *p != '/' && segLen + 1 < sizeof(segment)) {
                segment[segLen++] = *p++;
            }
            segment[segLen] = '\0';
            while (*p == '\\' || *p == '/') ++p;

            bool isLastSegment = (*p == '\0');
            bool found = false;
            Fat32DirEntry matched{};

            iterateDirectory(curDirClus, [&](const Fat32DirEntry& e) noexcept {
                if (strEqualsIgnoreCase(segment, e.name) ||
                    strEqualsIgnoreCase(segment, e.shortName)) {
                    matched = e;
                    found = true;
                    return false;
                }
                return true;
            });

            if (!found) {
                return false;
            }

            if (isLastSegment) {
                outEntry = matched;
                return true;
            }

            if (!matched.isDirectory) {
                return false;
            }

            curDirClus = matched.firstCluster;
        }

        return false;
    }

    bool readFile(const char* path, void* outBuf, size_t maxBytes, size_t& bytesRead) noexcept {
        bytesRead = 0;
        if (!m_mounted || !outBuf || maxBytes == 0) return false;

        Fat32DirEntry entry{};
        if (!findPath(path, entry) || entry.isDirectory) {
            return false;
        }

        uint32_t clus = entry.firstCluster;
        uint8_t* out = static_cast<uint8_t*>(outBuf);
        uint32_t clusterBytes = getBytesPerCluster();

        while (clus >= 2 && clus < 0x0FFFFFF8 && bytesRead < entry.fileSize && bytesRead < maxBytes) {
            uint32_t lba = clusterToLba(clus);
            uint16_t secToRead = m_secPerClus;
            if (secToRead > 128) secToRead = 128;

            if (!readVolumeSectors(lba, secToRead, m_clusterBuf)) {
                break;
            }

            uint32_t remaining = entry.fileSize - bytesRead;
            uint32_t toCopy = (remaining < clusterBytes) ? remaining : clusterBytes;
            if (bytesRead + toCopy > maxBytes) {
                toCopy = static_cast<uint32_t>(maxBytes - bytesRead);
            }

            std::memcpy(out + bytesRead, m_clusterBuf, toCopy);
            bytesRead += toCopy;

            if (bytesRead >= entry.fileSize) break;
            clus = getNextCluster(clus);
        }

        return (bytesRead > 0);
    }

    bool readFileAt(const char* path, uint64_t offset, void* outBuf,
                    size_t length, size_t& bytesRead) noexcept {
        bytesRead = 0;
        Fat32DirEntry entry{};
        if (!findPath(path, entry) || entry.isDirectory) return false;
        return readFileEntryAt(entry, offset, outBuf, length, bytesRead);
    }

    bool readFileEntryAt(const Fat32DirEntry& entry, uint64_t offset, void* outBuf,
                         size_t length, size_t& bytesRead) noexcept {
        bytesRead = 0;
        if (!m_mounted || entry.isDirectory || (!outBuf && length)) return false;
        if (length == 0) return true;
        if (offset >= entry.fileSize) return true;

        const uint32_t clusterBytes = getBytesPerCluster();
        const uint64_t available = static_cast<uint64_t>(entry.fileSize) - offset;
        const size_t target = available < length ? static_cast<size_t>(available) : length;
        const uint64_t skipClusters = offset / clusterBytes;
        uint32_t cluster = entry.firstCluster;
        const uint64_t maxClusters = m_totalSectors / m_secPerClus + 1;
        if (skipClusters >= maxClusters) return false;
        for (uint64_t skipped = 0; skipped < skipClusters; ++skipped) {
            if (cluster < 2 || cluster >= 0x0FFFFFF8) return false;
            cluster = getNextCluster(cluster);
        }

        size_t withinCluster = static_cast<size_t>(offset % clusterBytes);
        auto* output = static_cast<uint8_t*>(outBuf);
        uint64_t clustersRead{};
        while (bytesRead < target) {
            if (cluster < 2 || cluster >= 0x0FFFFFF8 || ++clustersRead > maxClusters)
                return false;
            const uint64_t clusterIndex = static_cast<uint64_t>(cluster - 2);
            const uint64_t clusterCount = m_totalSectors > m_dataStartLba
                ? (m_totalSectors - m_dataStartLba) / m_secPerClus : 0;
            if (clusterIndex >= clusterCount) return false;

            const uint32_t lba = clusterToLba(cluster);
            if (!readVolumeSectors(lba, m_secPerClus, m_clusterBuf)) return false;
            const size_t count = (target - bytesRead < clusterBytes - withinCluster)
                ? target - bytesRead : clusterBytes - withinCluster;
            std::memcpy(output + bytesRead, m_clusterBuf + withinCluster, count);
            bytesRead += count;
            withinCluster = 0;
            if (bytesRead < target) cluster = getNextCluster(cluster);
        }
        return true;
    }

    bool writeFile(const char* path, const void* data, size_t sizeBytes, size_t& bytesWritten) noexcept {
        bytesWritten = 0;
        if (!m_mounted || !data || sizeBytes == 0) return false;

        Fat32DirEntry entry{};
        if (!findPath(path, entry) || entry.isDirectory) {
            return false;
        }

        uint32_t clus = entry.firstCluster;
        const uint8_t* in = static_cast<const uint8_t*>(data);
        uint32_t clusterBytes = getBytesPerCluster();

        while (clus >= 2 && clus < 0x0FFFFFF8 && bytesWritten < sizeBytes) {
            uint32_t lba = clusterToLba(clus);
            uint16_t secToWrite = m_secPerClus;
            if (secToWrite > 128) secToWrite = 128;

            uint32_t remaining = sizeBytes - bytesWritten;
            uint32_t toCopy = (remaining < clusterBytes) ? remaining : clusterBytes;

            std::memset(m_clusterBuf, 0, clusterBytes);
            std::memcpy(m_clusterBuf, in + bytesWritten, toCopy);

            if (!writeVolumeSectors(lba, secToWrite, m_clusterBuf)) {
                break;
            }

            bytesWritten += toCopy;
            if (bytesWritten >= sizeBytes) break;
            clus = getNextCluster(clus);
        }

        return (bytesWritten > 0);
    }

    bool createFile(const char* path, const void* data, size_t sizeBytes) noexcept {
        if (!m_mounted || !m_device.read || !path) return false;
        Fat32DirEntry existing{};
        if (findPath(path, existing)) {
            size_t written = 0;
            return writeFile(path, data, sizeBytes, written);
        }

        const char* p = path;
        if ((p[0] == 'C' || p[0] == 'c' || p[0] == 'D' || p[0] == 'd') && p[1] == ':') {
            p += 2;
        }
        while (*p == '\\' || *p == '/') ++p;

        const char* lastSlash = nullptr;
        for (const char* s = p; *s; ++s) {
            if (*s == '\\' || *s == '/') lastSlash = s;
        }
        const char* fn = lastSlash ? (lastSlash + 1) : p;

        uint8_t raw83[11];
        std::memset(raw83, ' ', 11);
        size_t ni = 0;
        const char* curFn = fn;
        while (*curFn && *curFn != '.' && ni < 8) {
            char c = *curFn++;
            if (c >= 'a' && c <= 'z') c -= 32;
            raw83[ni++] = static_cast<uint8_t>(c);
        }
        while (*curFn && *curFn != '.') ++curFn;
        if (*curFn == '.') {
            ++curFn;
            size_t ei = 0;
            while (*curFn && ei < 3) {
                char c = *curFn++;
                if (c >= 'a' && c <= 'z') c -= 32;
                raw83[8 + (ei++)] = static_cast<uint8_t>(c);
            }
        }

        uint32_t allocatedClus = 0;
        for (uint32_t c = 3; c < 100000; ++c) {
            uint32_t fatByteOffset = c * 4;
            uint32_t fatSec = m_fatStartLba + (fatByteOffset / m_bytesPerSec);
            uint32_t entOff = fatByteOffset % m_bytesPerSec;
            if (m_cachedFatSector != fatSec) {
                if (!readVolumeSectors(fatSec, 1, m_fatCache)) break;
                m_cachedFatSector = fatSec;
            }
            uint32_t val = *reinterpret_cast<const uint32_t*>(&m_fatCache[entOff]) & 0x0FFFFFFF;
            if (val == 0) {
                allocatedClus = c;
                *reinterpret_cast<uint32_t*>(&m_fatCache[entOff]) = 0x0FFFFFFF;
                writeVolumeSectors(fatSec, 1, m_fatCache);
                if (m_numFats > 1) {
                    writeVolumeSectors(fatSec + m_secPerFat, 1, m_fatCache);
                }
                break;
            }
        }

        if (allocatedClus == 0) return false;

        uint32_t clusterBytes = getBytesPerCluster();
        std::memset(m_clusterBuf, 0, clusterBytes);
        size_t toCopy = (sizeBytes < clusterBytes) ? sizeBytes : clusterBytes;
        if (data && toCopy > 0) {
            std::memcpy(m_clusterBuf, data, toCopy);
        }
        uint32_t lba = clusterToLba(allocatedClus);
        if (!writeVolumeSectors(lba, m_secPerClus, m_clusterBuf)) {
            return false;
        }

        uint32_t targetDirClus = m_rootClus;
        if (lastSlash) {
            char parentDir[128]{};
            size_t plen = static_cast<size_t>(lastSlash - p);
            if (plen >= sizeof(parentDir)) plen = sizeof(parentDir) - 1;
            std::memcpy(parentDir, p, plen);
            parentDir[plen] = '\0';
            Fat32DirEntry pEntry{};
            if (findPath(parentDir, pEntry) && pEntry.isDirectory) {
                targetDirClus = pEntry.firstCluster;
            } else {
                return false;
            }
        }

        uint32_t curDirClus = targetDirClus;
        bool inserted = false;
        while (curDirClus >= 2 && curDirClus < 0x0FFFFFF8 && !inserted) {
            uint32_t dirLba = clusterToLba(curDirClus);
            if (!readVolumeSectors(dirLba, m_secPerClus, m_clusterBuf)) break;
            size_t totalEntries = (m_secPerClus * m_bytesPerSec) / 32;
            for (size_t i = 0; i < totalEntries; ++i) {
                uint8_t* ent = &m_clusterBuf[i * 32];
                if (ent[0] == 0x00 || ent[0] == 0xE5) {
                    std::memcpy(ent, raw83, 11);
                    ent[11] = 0x20;
                    std::memset(ent + 12, 0, 8);
                    *reinterpret_cast<uint16_t*>(&ent[20]) = static_cast<uint16_t>((allocatedClus >> 16) & 0xFFFF);
                    std::memset(ent + 22, 0, 4);
                    *reinterpret_cast<uint16_t*>(&ent[26]) = static_cast<uint16_t>(allocatedClus & 0xFFFF);
                    *reinterpret_cast<uint32_t*>(&ent[28]) = static_cast<uint32_t>(sizeBytes);

                    writeVolumeSectors(dirLba, m_secPerClus, m_clusterBuf);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) {
                curDirClus = getNextCluster(curDirClus);
            }
        }

        return inserted;
    }

    bool createDirectory(const char* path) noexcept {
        if (!m_mounted || !m_device.read || !path) return false;
        Fat32DirEntry existing{};
        if (findPath(path, existing)) return false; // Already exists

        const char* p = path;
        if ((p[0] == 'C' || p[0] == 'c' || p[0] == 'D' || p[0] == 'd') && p[1] == ':') p += 2;
        while (*p == '\\' || *p == '/') ++p;
        if (*p == '\0') return false;

        const char* lastSlash = nullptr;
        for (const char* s = p; *s; ++s) {
            if (*s == '\\' || *s == '/') lastSlash = s;
        }
        const char* dirName = lastSlash ? (lastSlash + 1) : p;

        uint32_t parentDirClus = m_rootClus;
        if (lastSlash) {
            char parentDir[128]{};
            size_t plen = static_cast<size_t>(lastSlash - p);
            if (plen >= sizeof(parentDir)) plen = sizeof(parentDir) - 1;
            std::memcpy(parentDir, p, plen);
            parentDir[plen] = '\0';
            Fat32DirEntry pEntry{};
            if (findPath(parentDir, pEntry) && pEntry.isDirectory) {
                parentDirClus = pEntry.firstCluster;
            } else {
                return false;
            }
        }

        uint8_t raw83[11];
        std::memset(raw83, ' ', 11);
        size_t ni = 0;
        const char* cur = dirName;
        while (*cur && *cur != '.' && ni < 8) {
            char c = *cur++;
            if (c >= 'a' && c <= 'z') c -= 32;
            raw83[ni++] = static_cast<uint8_t>(c);
        }

        uint32_t allocatedClus = 0;
        for (uint32_t c = 3; c < 100000; ++c) {
            uint32_t fatByteOffset = c * 4;
            uint32_t fatSec = m_fatStartLba + (fatByteOffset / m_bytesPerSec);
            uint32_t entOff = fatByteOffset % m_bytesPerSec;
            if (m_cachedFatSector != fatSec) {
                if (!readVolumeSectors(fatSec, 1, m_fatCache)) break;
                m_cachedFatSector = fatSec;
            }
            uint32_t val = *reinterpret_cast<const uint32_t*>(&m_fatCache[entOff]) & 0x0FFFFFFF;
            if (val == 0) {
                allocatedClus = c;
                *reinterpret_cast<uint32_t*>(&m_fatCache[entOff]) = 0x0FFFFFFF;
                writeVolumeSectors(fatSec, 1, m_fatCache);
                if (m_numFats > 1) {
                    writeVolumeSectors(fatSec + m_secPerFat, 1, m_fatCache);
                }
                break;
            }
        }
        if (allocatedClus == 0) return false;

        uint32_t clusterBytes = getBytesPerCluster();
        std::memset(m_clusterBuf, 0, clusterBytes);

        // "." entry
        uint8_t* dot = &m_clusterBuf[0];
        std::memset(dot, ' ', 11);
        dot[0] = '.';
        dot[11] = 0x10; // Directory
        *reinterpret_cast<uint16_t*>(&dot[20]) = static_cast<uint16_t>((allocatedClus >> 16) & 0xFFFF);
        *reinterpret_cast<uint16_t*>(&dot[26]) = static_cast<uint16_t>(allocatedClus & 0xFFFF);

        // ".." entry
        uint8_t* dotdot = &m_clusterBuf[32];
        std::memset(dotdot, ' ', 11);
        dotdot[0] = '.'; dotdot[1] = '.';
        dotdot[11] = 0x10; // Directory
        uint32_t pVal = (parentDirClus == m_rootClus) ? 0 : parentDirClus;
        *reinterpret_cast<uint16_t*>(&dotdot[20]) = static_cast<uint16_t>((pVal >> 16) & 0xFFFF);
        *reinterpret_cast<uint16_t*>(&dotdot[26]) = static_cast<uint16_t>(pVal & 0xFFFF);

        uint32_t lba = clusterToLba(allocatedClus);
        if (!writeVolumeSectors(lba, m_secPerClus, m_clusterBuf)) {
            return false;
        }

        uint32_t curDirClus = parentDirClus;
        bool inserted = false;
        while (curDirClus >= 2 && curDirClus < 0x0FFFFFF8 && !inserted) {
            uint32_t dirLba = clusterToLba(curDirClus);
            if (!readVolumeSectors(dirLba, m_secPerClus, m_clusterBuf)) break;
            size_t totalEntries = (m_secPerClus * m_bytesPerSec) / 32;
            for (size_t i = 0; i < totalEntries; ++i) {
                uint8_t* ent = &m_clusterBuf[i * 32];
                if (ent[0] == 0x00 || ent[0] == 0xE5) {
                    std::memcpy(ent, raw83, 11);
                    ent[11] = 0x10; // Directory
                    std::memset(ent + 12, 0, 8);
                    *reinterpret_cast<uint16_t*>(&ent[20]) = static_cast<uint16_t>((allocatedClus >> 16) & 0xFFFF);
                    std::memset(ent + 22, 0, 4);
                    *reinterpret_cast<uint16_t*>(&ent[26]) = static_cast<uint16_t>(allocatedClus & 0xFFFF);
                    *reinterpret_cast<uint32_t*>(&ent[28]) = 0;

                    writeVolumeSectors(dirLba, m_secPerClus, m_clusterBuf);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) {
                curDirClus = getNextCluster(curDirClus);
            }
        }
        return inserted;
    }

    bool deleteFile(const char* path) noexcept {
        if (!m_mounted || !m_device.read || !path) return false;
        Fat32DirEntry entry{};
        if (!findPath(path, entry) || entry.isDirectory) return false;

        const char* p = path;
        if ((p[0] == 'C' || p[0] == 'c' || p[0] == 'D' || p[0] == 'd') && p[1] == ':') p += 2;
        while (*p == '\\' || *p == '/') ++p;
        const char* lastSlash = nullptr;
        for (const char* s = p; *s; ++s) { if (*s == '\\' || *s == '/') lastSlash = s; }
        const char* fn = lastSlash ? (lastSlash + 1) : p;

        uint32_t targetDirClus = m_rootClus;
        if (lastSlash) {
            char parentDir[128]{};
            size_t plen = static_cast<size_t>(lastSlash - p);
            if (plen >= sizeof(parentDir)) plen = sizeof(parentDir) - 1;
            std::memcpy(parentDir, p, plen);
            parentDir[plen] = '\0';
            Fat32DirEntry pEntry{};
            if (findPath(parentDir, pEntry) && pEntry.isDirectory) {
                targetDirClus = pEntry.firstCluster;
            } else {
                return false;
            }
        }

        uint32_t curDirClus = targetDirClus;
        bool deleted = false;
        while (curDirClus >= 2 && curDirClus < 0x0FFFFFF8 && !deleted) {
            uint32_t dirLba = clusterToLba(curDirClus);
            if (!readVolumeSectors(dirLba, m_secPerClus, m_clusterBuf)) break;
            size_t totalEntries = (m_secPerClus * m_bytesPerSec) / 32;
            for (size_t i = 0; i < totalEntries; ++i) {
                uint8_t* ent = &m_clusterBuf[i * 32];
                if (ent[0] == 0x00) break;
                if (ent[0] == 0xE5) continue;
                char sName[13];
                format83Name(ent, sName, sizeof(sName));
                if (strEqualsIgnoreCase(fn, sName)) {
                    ent[0] = 0xE5;
                    writeVolumeSectors(dirLba, m_secPerClus, m_clusterBuf);
                    deleted = true;
                    break;
                }
            }
            if (!deleted) curDirClus = getNextCluster(curDirClus);
        }

        uint32_t clus = entry.firstCluster;
        while (clus >= 2 && clus < 0x0FFFFFF8) {
            uint32_t next = getNextCluster(clus);
            uint32_t fatByteOffset = clus * 4;
            uint32_t fatSec = m_fatStartLba + (fatByteOffset / m_bytesPerSec);
            uint32_t entOff = fatByteOffset % m_bytesPerSec;
            if (m_cachedFatSector != fatSec) {
                readVolumeSectors(fatSec, 1, m_fatCache);
                m_cachedFatSector = fatSec;
            }
            *reinterpret_cast<uint32_t*>(&m_fatCache[entOff]) = 0;
            writeVolumeSectors(fatSec, 1, m_fatCache);
            if (m_numFats > 1) {
                writeVolumeSectors(fatSec + m_secPerFat, 1, m_fatCache);
            }
            clus = next;
        }

        return deleted;
    }
};

class Fat32FileSystem {
    struct PartitionExtent {
        uint64_t firstLba{};
        uint64_t sectorCount{};
        bool present{};
    };

    static uint32_t crc32(const uint8_t* bytes, size_t length,
                          uint32_t crc = 0xFFFFFFFFu) noexcept {
        for (size_t i = 0; i < length; ++i) {
            crc ^= bytes[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return crc;
    }

    static uint16_t readLe16(const uint8_t* p) noexcept {
        return static_cast<uint16_t>(p[0]) |
               (static_cast<uint16_t>(p[1]) << 8);
    }
    static uint32_t readLe32(const uint8_t* p) noexcept {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }
    static uint64_t readLe64(const uint8_t* p) noexcept {
        return static_cast<uint64_t>(readLe32(p)) |
               (static_cast<uint64_t>(readLe32(p + 4)) << 32);
    }

    static bool validGpt(const Fat32BlockAccess& device, uint8_t* sector,
                         PartitionExtent (&partitions)[3]) noexcept {
        if (!device.read || !device.read(device.context, 1, 1, sector) ||
            std::memcmp(sector, "EFI PART", 8) != 0) return false;
        const uint32_t headerSize = readLe32(sector + 12);
        if (headerSize < 92 || headerSize > 512 || readLe64(sector + 24) != 1)
            return false;
        const uint64_t diskSectors = device.totalSectors;
        const uint64_t firstUsable = readLe64(sector + 40);
        const uint64_t lastUsable = readLe64(sector + 48);
        const uint64_t entriesLba = readLe64(sector + 72);
        const uint32_t entryCount = readLe32(sector + 80);
        const uint32_t entrySize = readLe32(sector + 84);
        const uint32_t expectedEntriesCrc = readLe32(sector + 88);
        const uint32_t expectedHeaderCrc = readLe32(sector + 16);
        uint8_t headerCopy[512];
        std::memcpy(headerCopy, sector, sizeof(headerCopy));
        std::memset(headerCopy + 16, 0, 4);
        if ((crc32(headerCopy, headerSize) ^ 0xFFFFFFFFu) != expectedHeaderCrc ||
            firstUsable > lastUsable || lastUsable >= diskSectors ||
            entryCount == 0 || entrySize < 128 || (entrySize & 7u) != 0)
            return false;

        const uint64_t arrayBytes = static_cast<uint64_t>(entryCount) * entrySize;
        if (arrayBytes == 0 || arrayBytes > 65536 || entriesLba >= diskSectors)
            return false;
        const uint64_t arraySectors = (arrayBytes + 511) / 512;
        if (arraySectors > diskSectors - entriesLba) return false;

        alignas(4096) static uint8_t entryBuffer[65536];
        const uint16_t sectors = static_cast<uint16_t>(arraySectors);
        if (!device.read(device.context, entriesLba, sectors, entryBuffer)) return false;
        if ((crc32(entryBuffer, static_cast<size_t>(arrayBytes)) ^ 0xFFFFFFFFu) !=
            expectedEntriesCrc) return false;

        for (uint32_t i = 0; i < entryCount && i < 3; ++i) {
            const uint8_t* entry = entryBuffer + static_cast<size_t>(i) * entrySize;
            bool emptyType = true;
            for (size_t b = 0; b < 16; ++b) emptyType &= entry[b] == 0;
            if (emptyType) continue;
            const uint64_t first = readLe64(entry + 32);
            const uint64_t last = readLe64(entry + 40);
            if (first < firstUsable || last > lastUsable || first > last) return false;
            partitions[i] = {first, last - first + 1, true};
        }
        return true;
    }

    static bool discoverPartitions(const Fat32BlockAccess& device,
                                   PartitionExtent (&partitions)[3],
                                   bool& hasPartitionTable) noexcept {
        alignas(4096) static uint8_t sector[512];
        hasPartitionTable = false;
        if (!device.read || !device.read(device.context, 0, 1, sector)) return false;
        if (sector[510] != 0x55 || sector[511] != 0xAA) return true;
        for (size_t i = 0; i < 4; ++i) {
            const uint8_t* entry = sector + 446 + i * 16;
            const uint8_t type = entry[4];
            const uint64_t first = readLe32(entry + 8);
            const uint64_t count = readLe32(entry + 12);
            if (type == 0 || count == 0) continue;
            hasPartitionTable = true;
            if (type == 0xEE) return validGpt(device, sector, partitions);
            if (i >= 3) continue;
            if (type != 0x0B && type != 0x0C && type != 0xEF) continue;
            if (first >= device.totalSectors || count > device.totalSectors - first) continue;
            partitions[i] = {first, count, true};
        }
        return true;
    }

public:
    static Fat32Volume& getVolumeEsp() noexcept {
        static Fat32Volume s_volEsp(0, 'S');
        return s_volEsp;
    }
    static Fat32Volume& getVolumeC() noexcept {
        static Fat32Volume s_volC(0, 'C');
        return s_volC;
    }
    static Fat32Volume& getVolumeD() noexcept {
        static Fat32Volume s_volD(0, 'D');
        return s_volD;
    }
    static Fat32Volume& get(char drive = 'C') noexcept {
        if (drive == 'S' || drive == 's') return getVolumeEsp();
        if (drive == 'D' || drive == 'd') return getVolumeD();
        return getVolumeC();
    }
    static bool mountAll(const Fat32BlockAccess& device) noexcept {
        PartitionExtent partitions[3]{};
        bool hasPartitionTable{};
        if (!device.read || !device.totalSectors ||
            !discoverPartitions(device, partitions, hasPartitionTable)) return false;
        if (!hasPartitionTable) {
            // Accept a FAT32 superfloppy only when sector zero is not an MBR.
            return getVolumeC().mount(device, 0, 'C', device.totalSectors, true);
        }
        const bool okEsp = partitions[0].present &&
            getVolumeEsp().mount(device, partitions[0].firstLba, 'S', partitions[0].sectorCount, true);
        const bool okC = partitions[1].present &&
            getVolumeC().mount(device, partitions[1].firstLba, 'C', partitions[1].sectorCount, true);
        const bool okD = partitions[2].present &&
            getVolumeD().mount(device, partitions[2].firstLba, 'D', partitions[2].sectorCount, true);
        return okEsp || okC || okD;
    }
    static bool mountAll(BareMetalAhciController& ahci) noexcept {
        const Fat32BlockAccess device{
            &ahci, ahci.getPort1TotalSectors(),
            [](void* context, uint64_t lba, uint16_t count, void* buffer) noexcept {
                return static_cast<BareMetalAhciController*>(context)->readSectors(
                    lba, count, buffer);
            },
            [](void* context, uint64_t lba, uint16_t count, const void* buffer) noexcept {
                return static_cast<BareMetalAhciController*>(context)->writeSectors(
                    lba, count, buffer);
            }
        };
        return mountAll(device);
    }
    static void unmountAll() noexcept {
        getVolumeEsp().unmount();
        getVolumeC().unmount();
        getVolumeD().unmount();
    }
    static bool copyFile(Fat32Volume& srcVol, const char* srcPath, Fat32Volume& dstVol, const char* dstPath) noexcept {
        alignas(4096) static uint8_t s_copyBuffer[262144];
        size_t readBytes = 0;
        if (!srcVol.readFile(srcPath, s_copyBuffer, sizeof(s_copyBuffer), readBytes)) {
            return false;
        }
        return dstVol.createFile(dstPath, s_copyBuffer, readBytes);
    }
};

} // namespace micant::km::ahci_baremetal
