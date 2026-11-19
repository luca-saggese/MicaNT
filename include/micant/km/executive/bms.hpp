/**
 * @file bms.hpp
 * Luce OS Clean-Room Windows NT Battery Management System (BMS) & Smart Battery Class Driver
 * 
 * Implements the standard Windows NT Battery Architecture (battc.sys / cmbatt.sys)
 * providing battery device objects (\Device\Battery0), dynamic battery tags, standard
 * Win32/NT battery IOCTLs, multi-cell balancing telemetry, Coulomb counter SOC tracking,
 * State of Health (SOH) wear analytics, and autonomous thermal runaway safety guards.
 *
 * Clean-Room Design: Conforms to Microsoft Win32/WDK Battery Class Specifications
 * and ACPI 6.5 Smart Battery Subsystem Specifications without proprietary code.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <mutex>
#include <span>
#include <algorithm>
#include <format>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/io.hpp"
#include "micant/km/executive/po.hpp"

namespace micant::po::bms {

// ============================================================================
// 1. Standard NT Battery IOCTL Definitions & Constants
// ============================================================================
inline constexpr uint32_t IOCTL_BATTERY_QUERY_TAG         = 0x00294040;
inline constexpr uint32_t IOCTL_BATTERY_QUERY_INFORMATION = 0x00294044;
inline constexpr uint32_t IOCTL_BATTERY_SET_INFORMATION   = 0x00294048;
inline constexpr uint32_t IOCTL_BATTERY_QUERY_STATUS      = 0x0029404C;

// Battery Power State Flags (BATTERY_STATUS.PowerState)
inline constexpr uint32_t BATTERY_POWER_ON_LINE = 0x00000001; // AC / USB-PD online
inline constexpr uint32_t BATTERY_DISCHARGING   = 0x00000002; // Battery is discharging
inline constexpr uint32_t BATTERY_CHARGING      = 0x00000004; // Battery is actively charging
inline constexpr uint32_t BATTERY_CRITICAL      = 0x00000008; // Battery reached critical trip point

// Battery Capabilities (BATTERY_INFORMATION.Capabilities)
inline constexpr uint32_t BATTERY_SYSTEM_BATTERY     = 0x00000001;
inline constexpr uint32_t BATTERY_CAPACITY_RELATIVE  = 0x00000002;
inline constexpr uint32_t BATTERY_IS_SHORT_TERM      = 0x00000004;
inline constexpr uint32_t BATTERY_SET_CHARGE_SUPPORT = 0x00000008;

// Battery Query Information Levels
enum class BatteryQueryInfoLevel : uint32_t {
    BatteryInformation           = 0,
    BatteryGranularityInformation = 1,
    BatteryTemperature           = 2,
    BatteryEstimatedTime         = 3,
    BatteryDeviceName            = 4,
    BatteryManufactureDate       = 5,
    BatteryManufactureName       = 6,
    BatteryUniqueID              = 7,
    BatterySerialNumber          = 8
};

// Battery Set Information Levels
enum class BatterySetInfoLevel : uint32_t {
    BatteryCriticalBias = 0,
    BatteryCharge       = 1,
    BatteryDischarge    = 2
};

// ============================================================================
// 2. Standard NT WDK Wire-Format Layouts (Packing = 1)
// ============================================================================
#pragma pack(push, 1)

struct BatteryQueryInformation {
    uint32_t              batteryTag{0};
    BatteryQueryInfoLevel infoLevel{BatteryQueryInfoLevel::BatteryInformation};
    uint32_t              atRate{0};
};

struct BatteryInformation {
    uint32_t capabilities{0};
    uint8_t  technology{0};
    uint8_t  reserved[3]{0, 0, 0};
    uint8_t  chemistry[4]{};
    uint32_t designedCapacity{0};
    uint32_t fullChargedCapacity{0};
    uint32_t defaultAlert1{0};
    uint32_t defaultAlert2{0};
    uint32_t criticalBias{0};
    uint32_t cycleCount{42};
};

struct BatteryStatus {
    uint32_t powerState{0};
    uint32_t capacity{0};
    uint32_t voltage{0};
    int32_t  rate{0};
};
inline constexpr int32_t BATTERY_UNKNOWN_RATE = -1;
inline constexpr uint32_t BATTERY_UNKNOWN_TIME = UINT32_MAX;

struct BatteryReportingScale {
    uint32_t Granularity;
    uint32_t Capacity;
};
static_assert(sizeof(BatteryReportingScale) == 8);

struct BatteryWaitStatus {
    uint32_t batteryTag{0};
    uint32_t timeout{0};
    uint32_t powerState{0};
    uint32_t lowCapacity{0};
    uint32_t highCapacity{0};
};

struct BatteryManufactureDate {
    uint8_t  day{14};
    uint8_t  month{10};
    uint16_t year{2026};
};

#pragma pack(pop)

// ============================================================================
// 3. Hardware Cell Telemetry & Safety Protections
// ============================================================================
struct CellTelemetry {
    static constexpr size_t CELL_COUNT = 4; // Standard 4S Lithium Pack (14.8V nominal, 16.8V max)

    std::array<uint16_t, CELL_COUNT> cellVoltageMv{ 4185, 4192, 4180, 4190 };
    std::array<uint16_t, CELL_COUNT> internalResistanceMilliOhms{ 18, 19, 18, 19 };
    std::array<bool, CELL_COUNT>     isBalancing{ false, false, false, false };

    uint16_t temperatureKelvinTenths{ 2982 }; // 298.2 K = 25.0 °C
    uint32_t instantaneousCurrentMa{ 0 };     // Pack current in mA

    // Safety Fault Bits
    bool overVoltageFault{ false };       // Cell > 4250 mV
    bool underVoltageFault{ false };      // Cell < 3000 mV
    bool overCurrentFault{ false };       // Current > 15000 mA
    bool overTemperatureFault{ false };   // Temp > 60.0 °C (333.2 K)
    bool underTemperatureFault{ false };  // Temp < 0.0 °C (273.2 K) - Inhibit charge

    [[nodiscard]] uint16_t getMaxCellDeltaMv() const noexcept {
        uint16_t minV = *std::min_element(cellVoltageMv.begin(), cellVoltageMv.end());
        uint16_t maxV = *std::max_element(cellVoltageMv.begin(), cellVoltageMv.end());
        return (maxV > minV) ? (maxV - minV) : 0;
    }

    [[nodiscard]] double getTemperatureCelsius() const noexcept {
        return (static_cast<double>(temperatureKelvinTenths) / 10.0) - 273.15;
    }
};

// ============================================================================
// 4. Sovereign Battery Management Subsystem Controller
// ============================================================================
class SmartBatterySubsystem {
private:
    mutable std::mutex       m_lock;
    uint32_t                 m_batteryTag{1};
    bool                     m_batteryPresent{true};
    BatteryInformation       m_info{};
    BatteryStatus            m_status{};
    std::array<BatteryReportingScale, 4> m_reportingScales{};
    uint32_t m_reportingScaleCount{0};
    CellTelemetry            m_cells{};
    std::string              m_deviceName{"MicaNT Sovereign Smart Battery Pack"};
    std::string              m_mfgName{"Barrer Silicon Systems"};
    std::string              m_serialNumber{"BSS-2026-BMS-0042"};
    uint32_t                 m_acWattage{65}; // 65W AC Adapter / USB-PD EPR

    SmartBatterySubsystem() noexcept {
        resetToDefaultState();
    }

public:
    static SmartBatterySubsystem& get() noexcept {
        static SmartBatterySubsystem instance;
        return instance;
    }

    void resetToDefaultState() noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        m_batteryTag = 1;
        m_batteryPresent = true;

        m_info.capabilities = BATTERY_SYSTEM_BATTERY | BATTERY_SET_CHARGE_SUPPORT;
        m_info.technology = 1;
        m_info.chemistry[0] = 'L'; m_info.chemistry[1] = 'I';
        m_info.chemistry[2] = 'O'; m_info.chemistry[3] = 'N';
        m_info.designedCapacity = 70000;    // 70.0 Wh
        m_info.fullChargedCapacity = 66500; // 66.5 Wh (95% Health)
        m_info.defaultAlert1 = 7000;        // 10%
        m_info.defaultAlert2 = 3500;        // 5%
        m_info.criticalBias = 0;
        m_info.cycleCount = 42;

        m_status.powerState = BATTERY_POWER_ON_LINE;
        m_status.capacity = 66500;
        m_status.voltage = 16800; // 4S x 4.2V = 16.8V
        m_status.rate = 0;        // Float idle on AC

        m_reportingScales = {};
        m_reportingScaleCount = 0;
        m_deviceName = "MicaNT Sovereign Smart Battery Pack";
        m_mfgName = "Barrer Silicon Systems";
        m_serialNumber = "BSS-2026-BMS-0042";
        m_cells = CellTelemetry{};
        m_acWattage = 65;
    }

    [[nodiscard]] uint32_t getBatteryTag() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_batteryPresent ? m_batteryTag : 0;
    }

    [[nodiscard]] bool isBatteryPresent() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_batteryPresent;
    }

    void setBatteryPresent(bool present) noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_batteryPresent != present) {
            advanceBatteryTag();
        }
        m_batteryPresent = present;
    }

    // The ACPI control-method miniport supplies firmware-reported data here.
    // The class driver continues to own the public battery device and IOCTLs.
    void updateFromAcpi(bool present, const BatteryInformation& information,
                        const BatteryStatus& status, std::string_view deviceName,
                        std::string_view serialNumber, std::string_view manufacturer,
                        const std::array<BatteryReportingScale, 4>& reportingScales = {},
                        uint32_t reportingScaleCount = 0) noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        bool characteristicsChanged = false;
        if (present && m_batteryPresent) {
            characteristicsChanged = m_info.capabilities != information.capabilities ||
                m_info.technology != information.technology ||
                std::memcmp(m_info.chemistry, information.chemistry, sizeof(m_info.chemistry)) != 0 ||
                m_info.designedCapacity != information.designedCapacity ||
                m_info.fullChargedCapacity != information.fullChargedCapacity ||
                m_info.defaultAlert1 != information.defaultAlert1 ||
                m_info.defaultAlert2 != information.defaultAlert2 ||
                m_info.criticalBias != information.criticalBias ||
                m_info.cycleCount != information.cycleCount ||
                m_deviceName != deviceName || m_serialNumber != serialNumber ||
                m_mfgName != manufacturer || m_reportingScaleCount != reportingScaleCount;
            if (!characteristicsChanged) {
                const uint32_t compareCount = std::min<uint32_t>(reportingScaleCount, 4);
                for (uint32_t i = 0; i < compareCount; ++i) {
                    if (m_reportingScales[i].Granularity != reportingScales[i].Granularity ||
                        m_reportingScales[i].Capacity != reportingScales[i].Capacity) {
                        characteristicsChanged = true;
                        break;
                    }
                }
            }
        }
        if (m_batteryPresent != present || characteristicsChanged) {
            advanceBatteryTag();
        }
        m_batteryPresent = present;
        if (!present) {
            m_info = {};
            m_status = {};
            m_reportingScales = {};
            m_reportingScaleCount = 0;
            m_deviceName.clear();
            m_serialNumber.clear();
            m_mfgName.clear();
            return;
        }
        m_info = information;
        m_status = status;
        m_reportingScales = reportingScales;
        m_reportingScaleCount = std::min<uint32_t>(reportingScaleCount, 4);
        m_deviceName.assign(deviceName);
        m_serialNumber.assign(serialNumber);
        m_mfgName.assign(manufacturer);
    }

    // Connect or disconnect AC mains / USB-PD charger
    void setAcPower(bool connected, uint32_t wattage = 65) noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        m_acWattage = wattage;
        if (connected) {
            m_status.powerState |= BATTERY_POWER_ON_LINE;
            m_status.powerState &= ~BATTERY_DISCHARGING;
            if (m_status.capacity < m_info.fullChargedCapacity) {
                m_status.powerState |= BATTERY_CHARGING;
                m_status.rate = static_cast<int32_t>(wattage * 850); // ~85% charging efficiency
            } else {
                m_status.powerState &= ~BATTERY_CHARGING;
                m_status.rate = 0; // Float / Top-off
            }
        } else {
            m_status.powerState &= ~(BATTERY_POWER_ON_LINE | BATTERY_CHARGING);
            m_status.powerState |= BATTERY_DISCHARGING;
            m_status.rate = -14500; // Typical 14.5W laptop/desktop idle load
        }
        recalculateCellVoltages();
    }

    // Simulate system current draw in mA (positive = charge, negative = discharge)
    void applyCurrentFlow(int32_t currentMa, uint32_t elapsedSeconds = 1) noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        m_cells.instantaneousCurrentMa = static_cast<uint32_t>(std::abs(currentMa));

        // Power (mW) = Voltage (V) * Current (mA)
        int32_t powerMw = static_cast<int32_t>((static_cast<int64_t>(m_status.voltage) * currentMa) / 1000);
        m_status.rate = powerMw;

        // Energy delta (mWh) = Power (mW) * Seconds / 3600
        int64_t deltaMwh = (static_cast<int64_t>(powerMw) * elapsedSeconds) / 3600;
        int64_t newCap = static_cast<int64_t>(m_status.capacity) + deltaMwh;

        if (newCap > static_cast<int64_t>(m_info.fullChargedCapacity)) {
            newCap = m_info.fullChargedCapacity;
        } else if (newCap < 0) {
            newCap = 0;
        }
        m_status.capacity = static_cast<uint32_t>(newCap);

        // Update charging / discharging states
        if (currentMa > 0) {
            m_status.powerState |= BATTERY_CHARGING;
            m_status.powerState &= ~BATTERY_DISCHARGING;
        } else if (currentMa < 0) {
            m_status.powerState |= BATTERY_DISCHARGING;
            m_status.powerState &= ~BATTERY_CHARGING;
        } else {
            m_status.powerState &= ~(BATTERY_CHARGING | BATTERY_DISCHARGING);
        }

        // Check Critical Trip Point
        if (m_status.capacity <= m_info.defaultAlert2) {
            m_status.powerState |= BATTERY_CRITICAL;
        } else {
            m_status.powerState &= ~BATTERY_CRITICAL;
        }

        recalculateCellVoltages();
        evaluateSafetyLimits();
    }

    // State of Health (SOH) = (FullChargedCapacity / DesignedCapacity) * 100
    [[nodiscard]] uint32_t getStateOfHealthPercent() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_info.designedCapacity == 0) return 100;
        return (m_info.fullChargedCapacity * 100) / m_info.designedCapacity;
    }

    // State of Charge (SOC) = (RemainingCapacity / FullChargedCapacity) * 100
    [[nodiscard]] uint32_t getStateOfChargePercent() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_info.fullChargedCapacity == 0) return 0;
        return (m_status.capacity * 100) / m_info.fullChargedCapacity;
    }

    // Estimated Run-Time Remaining (Seconds)
    [[nodiscard]] uint32_t getEstimatedRuntimeSeconds() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_status.rate >= 0 || m_status.rate == BATTERY_UNKNOWN_RATE)
            return BATTERY_UNKNOWN_TIME;
        uint32_t dischargeMw = static_cast<uint32_t>(std::abs(m_status.rate));
        if (dischargeMw == 0) return BATTERY_UNKNOWN_TIME;
        const uint64_t seconds = static_cast<uint64_t>(m_status.capacity) * 3600U / dischargeMw;
        return static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX - 1U));
    }

    // Time to Full Charge (Seconds)
    [[nodiscard]] uint32_t getTimeToFullSeconds() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_status.rate <= 0) return 0xFFFFFFFF;
        if (m_status.capacity >= m_info.fullChargedCapacity) return 0;
        uint32_t deficitMw = m_info.fullChargedCapacity - m_status.capacity;
        return (deficitMw * 3600) / static_cast<uint32_t>(m_status.rate);
    }

    [[nodiscard]] const CellTelemetry& getCellTelemetry() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_cells;
    }

    [[nodiscard]] const BatteryInformation& getInformation() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_info;
    }

    [[nodiscard]] const BatteryStatus& getStatus() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_status;
    }

    // ========================================================================
    // 5. Windows NT IOCTL Dispatch Engine
    // ========================================================================
    NtStatus handleIoctl(uint32_t ioctlCode, const void* inBuf, size_t inLen, void* outBuf, size_t outLen, size_t& bytesReturned) noexcept {
        bytesReturned = 0;
        std::lock_guard<std::mutex> lock(m_lock);
        const auto copyUnicode = [outBuf, outLen, &bytesReturned](std::string_view value) -> NtStatus {
            const size_t required = (value.size() + 1) * sizeof(uint16_t);
            if (!outBuf || outLen < required) return NtStatus::BufferTooSmall;
            auto* destination = static_cast<uint16_t*>(outBuf);
            for (size_t i = 0; i < value.size(); ++i)
                destination[i] = static_cast<uint8_t>(value[i]);
            destination[value.size()] = 0;
            bytesReturned = required;
            return NtStatus::Success;
        };

        switch (ioctlCode) {
            case IOCTL_BATTERY_QUERY_TAG: {
                if (outLen < sizeof(uint32_t)) return NtStatus::BufferTooSmall;
                if (!m_batteryPresent) return NtStatus::DeviceNotReady;
                *reinterpret_cast<uint32_t*>(outBuf) = m_batteryTag;
                bytesReturned = sizeof(uint32_t);
                return NtStatus::Success;
            }

            case IOCTL_BATTERY_QUERY_INFORMATION: {
                if (inLen < sizeof(BatteryQueryInformation)) return NtStatus::InvalidParameter;
                const auto* query = reinterpret_cast<const BatteryQueryInformation*>(inBuf);
                if (query->batteryTag != m_batteryTag) return NtStatus::DeviceNotReady;

                switch (query->infoLevel) {
                    case BatteryQueryInfoLevel::BatteryInformation: {
                        if (outLen < sizeof(BatteryInformation)) return NtStatus::BufferTooSmall;
                        std::memcpy(outBuf, &m_info, sizeof(BatteryInformation));
                        bytesReturned = sizeof(BatteryInformation);
                        return NtStatus::Success;
                    }
                    case BatteryQueryInfoLevel::BatteryGranularityInformation: {
                        const size_t required = static_cast<size_t>(m_reportingScaleCount) *
                            sizeof(BatteryReportingScale);
                        if (required == 0) return NtStatus::InvalidDeviceRequest;
                        if (outLen < required) return NtStatus::BufferTooSmall;
                        std::memcpy(outBuf, m_reportingScales.data(), required);
                        bytesReturned = required;
                        return NtStatus::Success;
                    }
                    case BatteryQueryInfoLevel::BatteryEstimatedTime: {
                        if (outLen < sizeof(uint32_t)) return NtStatus::BufferTooSmall;
                        uint32_t runtime = BATTERY_UNKNOWN_TIME;
                        const int32_t rate = query->atRate != 0
                            ? static_cast<int32_t>(query->atRate) : m_status.rate;
                        if (rate < 0 && rate != BATTERY_UNKNOWN_RATE) {
                            const uint32_t dischargeRate = static_cast<uint32_t>(-
                                static_cast<int64_t>(rate));
                            if (dischargeRate > 0) {
                                const uint64_t seconds = static_cast<uint64_t>(m_status.capacity) * 3600U /
                                    dischargeRate;
                                runtime = static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX - 1U));
                            }
                        }
                        *reinterpret_cast<uint32_t*>(outBuf) = runtime;
                        bytesReturned = sizeof(uint32_t);
                        return NtStatus::Success;
                    }
                    case BatteryQueryInfoLevel::BatteryDeviceName: {
                        return copyUnicode(m_deviceName);
                    }
                    case BatteryQueryInfoLevel::BatteryManufactureName: {
                        return copyUnicode(m_mfgName);
                    }
                    case BatteryQueryInfoLevel::BatterySerialNumber: {
                        return copyUnicode(m_serialNumber);
                    }
                    case BatteryQueryInfoLevel::BatteryUniqueID:
                    default:
                        return NtStatus::InvalidDeviceRequest;
                }
            }

            case IOCTL_BATTERY_QUERY_STATUS: {
                if (inLen < sizeof(BatteryWaitStatus)) return NtStatus::InvalidParameter;
                const auto* wait = reinterpret_cast<const BatteryWaitStatus*>(inBuf);
                if (wait->batteryTag != m_batteryTag) return NtStatus::DeviceNotReady;
                if (outLen < sizeof(BatteryStatus)) return NtStatus::BufferTooSmall;

                std::memcpy(outBuf, &m_status, sizeof(BatteryStatus));
                bytesReturned = sizeof(BatteryStatus);
                return NtStatus::Success;
            }

            default:
                return NtStatus::InvalidDeviceRequest;
        }
    }

    // Renders real-time ASCII telemetry gauge and multi-cell diagnostics
    void renderAsciiTelemetry(std::string& out) const {
        std::lock_guard<std::mutex> lock(m_lock);
        uint32_t soc = (m_info.fullChargedCapacity > 0) ? (m_status.capacity * 100) / m_info.fullChargedCapacity : 0;
        uint32_t soh = (m_info.designedCapacity > 0) ? (m_info.fullChargedCapacity * 100) / m_info.designedCapacity : 100;

        out.clear();
        out += "\n========================================================================\n";
        out += "       MicaNT Sovereign Battery Management System (BMS) Telemetry       \n";
        out += "       WDK Class Driver: \\Device\\Battery0 | ACPI PNP0C0A \\_SB.BAT0      \n";
        out += "========================================================================\n";

        // Visual Battery Gauge: [████████████████░░░░] 80%
        out += "  Pack Charge:   [";
        uint32_t filledBars = soc / 5; // 20 bars total
        for (uint32_t b = 0; b < 20; ++b) {
            if (b < filledBars) out += "#";
            else out += "-";
        }
        out += "] " + std::to_string(soc) + "% ";
        if (m_status.powerState & BATTERY_CHARGING) {
            out += "(CHARGING via " + std::to_string(m_acWattage) + "W AC/PD)\n";
        } else if (m_status.powerState & BATTERY_DISCHARGING) {
            out += "(DISCHARGING)\n";
        } else if (m_status.powerState & BATTERY_POWER_ON_LINE) {
            out += "(AC ONLINE - FULL)\n";
        } else {
            out += "(IDLE)\n";
        }

        out += "  Health (SOH):  " + std::to_string(soh) + "% (" +
               std::to_string(m_status.capacity) + " mWh / " +
               std::to_string(m_info.fullChargedCapacity) + " mWh Full / " +
               std::to_string(m_info.designedCapacity) + " mWh Design)\n";
        out += "  Terminal Volt: " + std::to_string(m_status.voltage) + " mV (" +
               std::to_string(static_cast<double>(m_status.voltage) / 1000.0) + " V)\n";
        out += "  Flow Rate:     " + std::to_string(m_status.rate) + " mW (" +
               std::to_string(static_cast<double>(m_status.rate) / 1000.0) + " W)\n";
        out += "  Temperature:   " + std::to_string(m_cells.getTemperatureCelsius()) + " C (" +
               std::to_string(m_cells.temperatureKelvinTenths) + " dK)\n";
        out += "  Cycle Count:   " + std::to_string(m_info.cycleCount) + " cycles\n";

        // 4-Cell Voltage Distribution
        out += "\n  --- 4S Lithium Cell Balancing & Internal Resistance Matrix ---\n";
        for (size_t c = 0; c < CellTelemetry::CELL_COUNT; ++c) {
            out += "    Cell " + std::to_string(c + 1) + ": " +
                   std::to_string(m_cells.cellVoltageMv[c]) + " mV | R_int: " +
                   std::to_string(m_cells.internalResistanceMilliOhms[c]) + " mOhm | Balance: " +
                   (m_cells.isBalancing[c] ? "ACTIVE" : "PASSIVE") + "\n";
        }
        out += "    Max Cell Imbalance: " + std::to_string(m_cells.getMaxCellDeltaMv()) + " mV (Nominal < 30 mV)\n";

        // Safety Status Indicators
        out += "\n  --- Hardware BMS Safety Interlocks ---\n";
        out += "    OVP (Over-Voltage):     " + std::string(m_cells.overVoltageFault ? "FAULT TRIP" : "NOMINAL (< 4250 mV)") + "\n";
        out += "    UVP (Under-Voltage):    " + std::string(m_cells.underVoltageFault ? "FAULT TRIP" : "NOMINAL (> 3000 mV)") + "\n";
        out += "    OCP (Over-Current):    " + std::string(m_cells.overCurrentFault ? "FAULT TRIP" : "NOMINAL (< 15 A)") + "\n";
        out += "    OTP (Over-Temperature): " + std::string(m_cells.overTemperatureFault ? "FAULT TRIP" : "NOMINAL (< 60 C)") + "\n";
        out += "========================================================================\n";
    }

private:
    void advanceBatteryTag() noexcept {
        ++m_batteryTag;
        if (m_batteryTag == 0) ++m_batteryTag;
    }

    void recalculateCellVoltages() noexcept {
        uint32_t soc = (m_info.fullChargedCapacity > 0) ? (m_status.capacity * 100) / m_info.fullChargedCapacity : 0;
        // Lithium OCV Curve: ~3.30V at 0%, ~3.70V at 50%, ~4.20V at 100%
        uint16_t baseCellMv = static_cast<uint16_t>(3300 + (soc * 9)); // 3300 to 4200 mV

        m_cells.cellVoltageMv[0] = baseCellMv;
        m_cells.cellVoltageMv[1] = static_cast<uint16_t>(baseCellMv + 5);
        m_cells.cellVoltageMv[2] = static_cast<uint16_t>(baseCellMv - 3);
        m_cells.cellVoltageMv[3] = static_cast<uint16_t>(baseCellMv + 2);

        uint32_t totalPackMv = 0;
        for (auto v : m_cells.cellVoltageMv) totalPackMv += v;
        m_status.voltage = totalPackMv;

        // Auto-balance if delta > 30 mV during charging
        uint16_t delta = m_cells.getMaxCellDeltaMv();
        if ((m_status.powerState & BATTERY_CHARGING) && delta > 30) {
            uint16_t maxV = *std::max_element(m_cells.cellVoltageMv.begin(), m_cells.cellVoltageMv.end());
            for (size_t i = 0; i < CellTelemetry::CELL_COUNT; ++i) {
                m_cells.isBalancing[i] = (m_cells.cellVoltageMv[i] == maxV);
            }
        } else {
            m_cells.isBalancing.fill(false);
        }
    }

    void evaluateSafetyLimits() noexcept {
        m_cells.overVoltageFault = false;
        m_cells.underVoltageFault = false;
        for (auto v : m_cells.cellVoltageMv) {
            if (v >= 4250) m_cells.overVoltageFault = true;
            if (v <= 3000) m_cells.underVoltageFault = true;
        }

        m_cells.overTemperatureFault = (m_cells.temperatureKelvinTenths >= 3332); // 60 °C
        m_cells.underTemperatureFault = (m_cells.temperatureKelvinTenths <= 2732); // 0 °C

        // Autonomous Thermal Throttle: Inhibit charging if hot
        if (m_cells.overTemperatureFault && (m_status.powerState & BATTERY_CHARGING)) {
            m_status.powerState &= ~BATTERY_CHARGING;
            m_status.rate = 0;
        }
    }
};


/**
 * @brief Initializes the battery class driver and registers \\Device\\Battery0
 */
void InitializeBatterySubsystem() noexcept;

} // namespace micant::po::bms
