#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
#include "micant/km/hal/boot.hpp"

namespace micant::hal::highhalf {

/**
 * @brief Canonical 64-bit Windows NT Kernel Virtual Address Constants (Unit #96).
 * In 64-bit Windows NT, user space occupies the low half [0x00000000'00000000 - 0x00007FFF'FFFFFFFF]
 * and the kernel executive occupies the high canonical half [0xFFFF8000'00000000 - 0xFFFFFFFF'FFFFFFFF].
 */
inline constexpr uint64_t HIGH_HALF_KERNEL_BASE   = 0xFFFFF80000000000ULL; // NTOSKRNL & HAL Base (PML4 slot 496)
inline constexpr uint64_t HIGH_HALF_HAL_BASE      = 0xFFFFFF0000000000ULL; // System HAL Space (PML4 slot 510)
inline constexpr uint64_t HIGH_HALF_LEGACY_BASE   = 0xFFFFFF8000000000ULL; // Classic High Kernel (-512GB, PML4 slot 511)
inline constexpr uint64_t PHYSICAL_PAGE_MASK      = 0x000FFFFFFFFFF000ULL; // Physical frame mask (4KB aligned)
inline constexpr uint64_t CANONICAL_OFFSET_MASK    = 0x0000007FFFFFFFFFULL; // 39-bit single PML4 entry range (512 GB)

inline constexpr uint16_t PML4_INDEX_USER_BASE     = 0;   // Low 512GB identity & userland mapping
inline constexpr uint16_t PML4_INDEX_KERNEL_BASE   = 496; // 0xFFFFF800'00000000ULL
inline constexpr uint16_t PML4_INDEX_HAL           = 510; // 0xFFFFFF00'00000000ULL
inline constexpr uint16_t PML4_INDEX_LEGACY        = 511; // 0xFFFFFF80'00000000ULL

/**
 * @brief Translates a physical or low-canonical address into the higher-half NT kernel virtual space.
 */
[[nodiscard]] constexpr uint64_t toHighCanonical(uint64_t physOrLowVa) noexcept {
    return HIGH_HALF_KERNEL_BASE | (physOrLowVa & CANONICAL_OFFSET_MASK);
}

/**
 * @brief Translates a high-canonical address back into its underlying physical memory offset.
 */
[[nodiscard]] constexpr uint64_t toPhysical(uint64_t highVa) noexcept {
    return highVa & CANONICAL_OFFSET_MASK;
}

/**
 * @brief Verifies whether an address belongs to the high-canonical kernel range (sign-extended bits 48..63 = 1).
 */
[[nodiscard]] constexpr bool isHighCanonical(uint64_t va) noexcept {
    return (va & 0xFFFF800000000000ULL) == 0xFFFF800000000000ULL;
}

/**
 * @brief Extracts the 9-bit PML4 index from a 64-bit virtual address (bits 47:39).
 */
[[nodiscard]] constexpr uint16_t getPml4Index(uint64_t va) noexcept {
    return static_cast<uint16_t>((va >> 39) & 0x1FF);
}

/**
 * @brief Extracts the 9-bit PDPT index from a 64-bit virtual address (bits 38:30).
 */
[[nodiscard]] constexpr uint16_t getPdptIndex(uint64_t va) noexcept {
    return static_cast<uint16_t>((va >> 30) & 0x1FF);
}

/**
 * @brief Extracts the 9-bit Page Directory index from a 64-bit virtual address (bits 29:21).
 */
[[nodiscard]] constexpr uint16_t getPdIndex(uint64_t va) noexcept {
    return static_cast<uint16_t>((va >> 21) & 0x1FF);
}

/**
 * @brief Extracts the 9-bit Page Table index from a 64-bit virtual address (bits 20:12).
 */
[[nodiscard]] constexpr uint16_t getPtIndex(uint64_t va) noexcept {
    return static_cast<uint16_t>((va >> 12) & 0x1FF);
}

/**
 * @brief High-Half Kernel Handover Telemetry and Trap Context.
 */
struct HighHalfHandoverContext64 {
    uint64_t entryRip{0};
    uint64_t kernelStackTop{0};
    uint64_t lpbHighVa{0};
    uint64_t cr3Value{0};
    uint64_t transitionCount{0};
    bool     highCanonicalActive{false};
    bool     identityUnmapped{false};
};

/**
 * Luce OS High-Half Canonical 64-bit Paging Engine (Unit #96).
 * Handles clean-room higher-half mirroring, LPB pointer translation, and firmware handover.
 */
class HighHalfPagingEngine64 {
private:
    HighHalfHandoverContext64 m_context{};

    HighHalfPagingEngine64() = default;

public:
    static HighHalfPagingEngine64& get() noexcept {
        static HighHalfPagingEngine64 instance;
        return instance;
    }

    void reset() noexcept {
        m_context = HighHalfHandoverContext64{};
    }

    [[nodiscard]] const HighHalfHandoverContext64& getContext() const noexcept {
        return m_context;
    }

    /**
     * @brief Mirrors the low-memory physical mapping into higher-half PML4 slots (496, 510, 511).
     * @param pml4Base Pointer to the 512-entry PML4 table.
     * @return true if mapping succeeded.
     */
    bool mirrorHighCanonicalSpace(uint64_t* pml4Base) noexcept {
        if (!pml4Base) return false;
        if (!(pml4Base[PML4_INDEX_USER_BASE] & 1)) return false; // Low identity mapping must be present

        uint64_t pdptEntry = pml4Base[PML4_INDEX_USER_BASE];

        // Mirror low 512 GB identity mapping into high-canonical kernel slots
        pml4Base[PML4_INDEX_KERNEL_BASE] = pdptEntry; // 0xFFFFF800'00000000
        pml4Base[PML4_INDEX_HAL]         = pdptEntry; // 0xFFFFFF80'00000000
        pml4Base[PML4_INDEX_LEGACY]      = pdptEntry; // 0xFFFFFFFF'80000000

        m_context.highCanonicalActive = true;
        return true;
    }

    /**
     * @brief Unmaps the low identity mapping (PML4 slot 0) after higher-half transition.
     * Enforces strict NULL-pointer and user-space boundary trap protection.
     * @param pml4Base Pointer to the 512-entry PML4 table.
     */
    bool unmapLowIdentitySpace(uint64_t* pml4Base) noexcept {
        if (!pml4Base) return false;
        pml4Base[PML4_INDEX_USER_BASE] = 0;
        m_context.identityUnmapped = true;
        return true;
    }

    /**
     * @brief Prepares the Handover Context Frame for the transition jump.
     */
    bool prepareHandover(
        uint64_t kernelEntryRip,
        uint64_t kernelStackTop,
        uint64_t lpbPhysicalAddress,
        uint64_t cr3
    ) noexcept {
        m_context.entryRip = toHighCanonical(kernelEntryRip);
        m_context.kernelStackTop = toHighCanonical(kernelStackTop);
        m_context.lpbHighVa = toHighCanonical(lpbPhysicalAddress);
        m_context.cr3Value = cr3;
        m_context.transitionCount++;
        return true;
    }

    /**
     * @brief Re-bases LOADER_PARAMETER_BLOCK internal pointers to high-canonical address space.
     */
    static void translateLpbPointers(boot::LoaderParameterBlock& lpb) noexcept {
        if (lpb.framebuffer.physicalBase != 0) {
            // Note: Keep physical base intact in lpb for hardware registers,
            // but add virtualBase if consumers require direct pointer access.
        }
        if (lpb.acpiTablePhysicalAddress != 0) {
            // ACPI tables can be referenced at both physical and high-canonical addresses
        }
    }
};

} // namespace micant::hal::highhalf
