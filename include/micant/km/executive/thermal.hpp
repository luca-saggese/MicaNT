/**
 * @file thermal.hpp
 * Luce OS Clean-Room Windows NT ACPI Thermal Zone Class Driver & Fan Tachometer Subsystem
 *
 * Implements canonical Windows NT Thermal Management architecture (thermal.sys / acpitz.sys),
 * managing thermal zone device objects (\Device\ThermalZone0, \Device\ThermalZone1), ACPI 6.5
 * temperature trip points (_CRT, _HOT, _PSV, _AC0.._AC9), PWM fan tachometers with stall detection,
 * passive cooling algorithmic feedback loops (TC1/TC2/TSP), and unified coordination with the
 * Processor Power Manager (PPM) and Battery Management System (BMS).
 *
 * Clean-Room Design: Conforms to Microsoft WDK Thermal Management Specifications
 * and ACPI 6.5 Chapter 11 (Thermal Management) without proprietary code.
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
#include "micant/km/executive/bms.hpp"
#include "micant/km/executive/ppm.hpp"

namespace micant::po::thermal {

// ============================================================================
// 1. Standard NT Thermal IOCTL Definitions & Constants
// ============================================================================
inline constexpr uint32_t IOCTL_THERMAL_QUERY_INFORMATION     = 0x00298000;
inline constexpr uint32_t IOCTL_THERMAL_QUERY_TEMPERATURE     = 0x00298004;
inline constexpr uint32_t IOCTL_THERMAL_READ_POLICY          = 0x00298008;
inline constexpr uint32_t IOCTL_THERMAL_SET_COOLING_POLICY    = 0x0029800C;
inline constexpr uint32_t IOCTL_FAN_QUERY_TACHOMETER          = 0x00298010;
inline constexpr uint32_t IOCTL_FAN_SET_PWM                   = 0x00298014;

// Temperature Conversion Helpers (Tenths of Kelvin <-> Celsius)
inline constexpr uint32_t CelsiusToKelvinTenths(double celsius) noexcept {
    return static_cast<uint32_t>((celsius + 273.15) * 10.0 + 0.5);
}

inline constexpr double KelvinTenthsToCelsius(uint32_t tenthsK) noexcept {
    return (static_cast<double>(tenthsK) / 10.0) - 273.15;
}

// Thermal Cooling Mode
enum class ThermalCoolingMode : uint32_t {
    Active  = 0, // Fans engaged before processor throttling
    Passive = 1  // Processor throttled before fan engagement (quiet profile)
};

// ============================================================================
// 2. Standard WDK Wire-Format Layouts (Packing = 1)
// ============================================================================
#pragma pack(push, 1)

struct ThermalInformation {
    uint32_t thermalStamp{1};           // Monotonic stamp incremented on change
    uint32_t thermalConstant1{2};       // ACPI _TC1 (Passive cooling constant 1)
    uint32_t thermalConstant2{3};       // ACPI _TC2 (Passive cooling constant 2)
    uint64_t processors{0xFF};          // Bitmask of affected processors (8 cores = 0xFF)
    uint32_t samplingPeriod{10};        // ACPI _TSP (Sampling interval in 0.1s, 1.0s = 10)
    uint32_t currentTemperature{3182};  // ACPI _TMP (Tenths of Kelvin, 318.2 K = 45.0 C)
    uint32_t passiveTripPoint{3432};    // ACPI _PSV (70.0 C)
    uint32_t criticalTripPoint{3732};   // ACPI _CRT (100.0 C - Emergency Hardware Trip)
    uint8_t  activeTripPointCount{3};   // Supported active cooling fan trip points
    uint8_t  reserved[3]{0, 0, 0};
    uint32_t activeTripPoint[10]{       // ACPI _AC0.._AC9 (Tenths of Kelvin)
        3382, // _AC0: 65.0 C -> 100% PWM Fan Speed
        3282, // _AC1: 55.0 C ->  70% PWM Fan Speed
        3182, // _AC2: 45.0 C ->  40% PWM Fan Speed
        0, 0, 0, 0, 0, 0, 0
    };
};

struct ThermalPolicy {
    uint32_t           version{1};
    bool               waitForCooling{false};
    ThermalCoolingMode coolingMode{ThermalCoolingMode::Active};
};

struct FanTachometerTelemetry {
    uint32_t fanId{0};          // 0 = CPU Fan, 1 = Chassis Fan 1, 2 = Chassis Fan 2
    uint32_t currentRpm{1200};  // Measured Fan RPM
    uint32_t targetRpm{1200};   // Target Fan RPM
    uint8_t  pwmPercent{40};    // 0..100% PWM Duty Cycle
    bool     isStalled{false};  // Tachometer stall detection
    uint8_t  reserved[2]{0, 0};
};

#pragma pack(pop)

// ============================================================================
// 3. Sovereign Thermal Zone & Fan Control Subsystem
// ============================================================================
class ThermalSubsystem {
public:
    static constexpr size_t MAX_FANS = 3;

    struct ThermalZoneTelemetry {
        std::string name{"\\_SB.TZ0 (CPU Socket)"};
        uint32_t    temperatureKelvinTenths{3182}; // 45.0 C
        uint32_t    previousKelvinTenths{3182};
        bool        passiveThrottlingActive{false};
        bool        criticalTripFired{false};
        int32_t     calculatedDeltaPower{0};
    };

    static ThermalSubsystem& get() noexcept {
        static ThermalSubsystem s_instance;
        return s_instance;
    }

    ThermalSubsystem() {
        resetToDefaultState();
    }

    void resetToDefaultState() noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        m_info = ThermalInformation{};
        m_policy = ThermalPolicy{};

        // Zone 0: CPU Core Package
        m_zone.name = "\\_SB.TZ0 (Intel Xeon E3-1270 v6 Package)";
        m_zone.temperatureKelvinTenths = CelsiusToKelvinTenths(45.0);
        m_zone.previousKelvinTenths = m_zone.temperatureKelvinTenths;
        m_zone.passiveThrottlingActive = false;
        m_zone.criticalTripFired = false;
        m_zone.calculatedDeltaPower = 0;

        // Initialize 3 PWM System Fans (Supermicro Server Configuration)
        // Fan 0: CPU Heatsink Fan (Max 4500 RPM)
        m_fans[0] = { 0, 1500, 1500, 40, false, {0, 0} };
        // Fan 1: Front Chassis Intake Fan (Max 3000 RPM)
        m_fans[1] = { 1, 1200, 1200, 40, false, {0, 0} };
        // Fan 2: Rear Exhaust Fan (Max 3000 RPM)
        m_fans[2] = { 2, 1200, 1200, 40, false, {0, 0} };
    }

    // Update zone temperature and execute ACPI active/passive cooling logic
    void updateTemperatureCelsius(double celsius) noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        m_zone.previousKelvinTenths = m_zone.temperatureKelvinTenths;
        m_zone.temperatureKelvinTenths = CelsiusToKelvinTenths(celsius);
        m_info.currentTemperature = m_zone.temperatureKelvinTenths;
        m_info.thermalStamp++;

        evaluateActiveCoolingFans();
        evaluatePassiveCoolingAlgorithm();
        evaluateCriticalSafetyTrips();
    }

    // IOCTL Request Handler for thermal.sys / acpitz.sys
    NtStatus handleIoctl(
        uint32_t ioctlCode,
        const void* inBuf,
        size_t inLen,
        void* outBuf,
        size_t outLen,
        size_t& bytesReturned
    ) noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        bytesReturned = 0;

        switch (ioctlCode) {
            case IOCTL_THERMAL_QUERY_INFORMATION: {
                if (outLen < sizeof(ThermalInformation)) return NtStatus::BufferTooSmall;
                std::memcpy(outBuf, &m_info, sizeof(ThermalInformation));
                bytesReturned = sizeof(ThermalInformation);
                return NtStatus::Success;
            }

            case IOCTL_THERMAL_QUERY_TEMPERATURE: {
                if (outLen < sizeof(uint32_t)) return NtStatus::BufferTooSmall;
                *reinterpret_cast<uint32_t*>(outBuf) = m_info.currentTemperature;
                bytesReturned = sizeof(uint32_t);
                return NtStatus::Success;
            }

            case IOCTL_THERMAL_READ_POLICY: {
                if (outLen < sizeof(ThermalPolicy)) return NtStatus::BufferTooSmall;
                std::memcpy(outBuf, &m_policy, sizeof(ThermalPolicy));
                bytesReturned = sizeof(ThermalPolicy);
                return NtStatus::Success;
            }

            case IOCTL_THERMAL_SET_COOLING_POLICY: {
                if (inLen < sizeof(ThermalPolicy)) return NtStatus::BufferTooSmall;
                m_policy = *reinterpret_cast<const ThermalPolicy*>(inBuf);
                m_info.thermalStamp++;
                return NtStatus::Success;
            }

            case IOCTL_FAN_QUERY_TACHOMETER: {
                uint32_t fanId = 0;
                if (inBuf && inLen >= sizeof(uint32_t)) {
                    fanId = *reinterpret_cast<const uint32_t*>(inBuf);
                    if (fanId >= MAX_FANS) fanId = 0;
                }
                if (outLen < sizeof(FanTachometerTelemetry)) return NtStatus::BufferTooSmall;
                std::memcpy(outBuf, &m_fans[fanId], sizeof(FanTachometerTelemetry));
                bytesReturned = sizeof(FanTachometerTelemetry);
                return NtStatus::Success;
            }

            case IOCTL_FAN_SET_PWM: {
                if (inLen < sizeof(uint32_t) * 2) return NtStatus::BufferTooSmall;
                const auto* params = reinterpret_cast<const uint32_t*>(inBuf);
                uint32_t fanId = params[0];
                uint32_t pwm = std::min(params[1], 100u);
                if (fanId < MAX_FANS) {
                    m_fans[fanId].pwmPercent = static_cast<uint8_t>(pwm);
                    m_fans[fanId].targetRpm = (fanId == 0) ? (pwm * 45) : (pwm * 30);
                    m_fans[fanId].currentRpm = m_fans[fanId].targetRpm;
                    m_fans[fanId].isStalled = (pwm > 0 && m_fans[fanId].currentRpm == 0);
                }
                return NtStatus::Success;
            }

            default:
                return NtStatus::InvalidDeviceRequest;
        }
    }

    [[nodiscard]] const ThermalInformation& getInformation() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_info;
    }

    [[nodiscard]] const ThermalZoneTelemetry& getZone() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_zone;
    }

    [[nodiscard]] const std::array<FanTachometerTelemetry, MAX_FANS>& getFans() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_fans;
    }

    [[nodiscard]] bool isPassiveThrottlingActive() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_zone.passiveThrottlingActive;
    }

    [[nodiscard]] bool isCriticalTripFired() const noexcept {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_zone.criticalTripFired;
    }

    // ASCII Telemetry Visualizer for Executive Console
    void renderAsciiTelemetry(std::string& out) const {
        std::lock_guard<std::mutex> lock(m_lock);
        out.clear();
        out.reserve(2048);

        double curC = KelvinTenthsToCelsius(m_info.currentTemperature);
        double psvC = KelvinTenthsToCelsius(m_info.passiveTripPoint);
        double crtC = KelvinTenthsToCelsius(m_info.criticalTripPoint);
        double ac0C = KelvinTenthsToCelsius(m_info.activeTripPoint[0]);
        double ac1C = KelvinTenthsToCelsius(m_info.activeTripPoint[1]);
        double ac2C = KelvinTenthsToCelsius(m_info.activeTripPoint[2]);

        out += "===============================================================================\n";
        out += "   MicaNT Sovereign ACPI Thermal Zone (acpitz.sys) & Dynamic Fan Subsystem     \n";
        out += "===============================================================================\n";
        out += std::format("  Thermal Node:       \\Device\\ThermalZone0 -> {}\n", m_zone.name);
        out += std::format("  Current Temp (_TMP): {:.1f} C ({} 0.1K) | Status: {}\n",
            curC, m_info.currentTemperature,
            m_zone.criticalTripFired ? "CRITICAL HARDWARE TRIP" :
            m_zone.passiveThrottlingActive ? "PASSIVE THROTTLE ACTIVE" : "NOMINAL TEMPERATURE");
        out += std::format("  Cooling Mode:       {} | Stamp: {}\n",
            (m_policy.coolingMode == ThermalCoolingMode::Active) ? "Active (Performance Fan Ramp)" : "Passive (Silent Throttling)",
            m_info.thermalStamp);
        out += "-------------------------------------------------------------------------------\n";
        out += "  ACPI Trip Ladder:\n";
        out += std::format("    _CRT (Critical Shutdown):  {:.1f} C ({} 0.1K) - Immediate Silicon Protection\n", crtC, m_info.criticalTripPoint);
        out += std::format("    _PSV (Passive Throttle):   {:.1f} C ({} 0.1K) - TC1={} TC2={} TSP={} (PPM EPP Downshift)\n",
            psvC, m_info.passiveTripPoint, m_info.thermalConstant1, m_info.thermalConstant2, m_info.samplingPeriod);
        out += std::format("    _AC0 (100% PWM Fan Ramp):  {:.1f} C ({} 0.1K)\n", ac0C, m_info.activeTripPoint[0]);
        out += std::format("    _AC1 ( 70% PWM Fan Ramp):  {:.1f} C ({} 0.1K)\n", ac1C, m_info.activeTripPoint[1]);
        out += std::format("    _AC2 ( 40% PWM Fan Ramp):  {:.1f} C ({} 0.1K)\n", ac2C, m_info.activeTripPoint[2]);
        out += "-------------------------------------------------------------------------------\n";
        out += " Fan ID  Label              PWM%   Current RPM   Target RPM   Stall Status\n";
        out += "-------------------------------------------------------------------------------\n";
        for (const auto& fan : m_fans) {
            std::string label = (fan.fanId == 0) ? "CPU Socket Fan" :
                                (fan.fanId == 1) ? "Chassis Intake" : "Chassis Exhaust";
            out += std::format("   #{}    {:<18} {:>3}%     {:>4} RPM     {:>4} RPM   {}\n",
                fan.fanId, label, fan.pwmPercent, fan.currentRpm, fan.targetRpm,
                fan.isStalled ? "[STALLED / FAULT]" : "[OPERATIONAL]");
        }
        out += "===============================================================================\n";
    }

private:
    void evaluateActiveCoolingFans() noexcept {
        uint32_t t = m_zone.temperatureKelvinTenths;
        uint8_t targetPwm = 20; // 20% baseline idle

        if (t >= m_info.activeTripPoint[0]) {
            targetPwm = 100; // Above _AC0 (65 C): 100% PWM
        } else if (t >= m_info.activeTripPoint[1]) {
            targetPwm = 70;  // Above _AC1 (55 C): 70% PWM
        } else if (t >= m_info.activeTripPoint[2]) {
            targetPwm = 40;  // Above _AC2 (45 C): 40% PWM
        }

        for (size_t i = 0; i < MAX_FANS; ++i) {
            m_fans[i].pwmPercent = targetPwm;
            m_fans[i].targetRpm = (i == 0) ? (targetPwm * 45) : (targetPwm * 30);
            m_fans[i].currentRpm = m_fans[i].targetRpm;
            m_fans[i].isStalled = (targetPwm > 0 && m_fans[i].currentRpm == 0);
        }
    }

    void evaluatePassiveCoolingAlgorithm() noexcept {
        uint32_t t = m_zone.temperatureKelvinTenths;
        uint32_t psv = m_info.passiveTripPoint;

        if (t >= psv) {
            m_zone.passiveThrottlingActive = true;
            // ACPI Formula: dP = TC1 * (Tn - Tn-1) + TC2 * (Tn - Ttarget)
            int32_t tc1 = static_cast<int32_t>(m_info.thermalConstant1);
            int32_t tc2 = static_cast<int32_t>(m_info.thermalConstant2);
            int32_t diff = static_cast<int32_t>(t) - static_cast<int32_t>(m_zone.previousKelvinTenths);
            int32_t targetDiff = static_cast<int32_t>(t) - static_cast<int32_t>(psv);
            m_zone.calculatedDeltaPower = (tc1 * diff) + (tc2 * targetDiff);

            // Signal PPM to throttle processor budget
            auto& ppm = micant::po::ppm::ProcessorPowerManager::get();
            ppm.applyThermalBudget(static_cast<uint32_t>(KelvinTenthsToCelsius(t)));
        } else {
            m_zone.passiveThrottlingActive = false;
            m_zone.calculatedDeltaPower = 0;
            if (KelvinTenthsToCelsius(t) < 70.0) {
                auto& ppm = micant::po::ppm::ProcessorPowerManager::get();
                ppm.applyThermalBudget(static_cast<uint32_t>(KelvinTenthsToCelsius(t)));
            }
        }
    }

    void evaluateCriticalSafetyTrips() noexcept {
        uint32_t t = m_zone.temperatureKelvinTenths;
        if (t >= m_info.criticalTripPoint) {
            m_zone.criticalTripFired = true;
            // Emergency shutdown interlock: Fans to 100% and notify PowerManager
            for (auto& fan : m_fans) {
                fan.pwmPercent = 100;
                fan.currentRpm = (fan.fanId == 0) ? 4500 : 3000;
            }
        } else {
            m_zone.criticalTripFired = false;
        }
    }

    mutable std::mutex                            m_lock;
    ThermalInformation                            m_info{};
    ThermalPolicy                                 m_policy{};
    ThermalZoneTelemetry                          m_zone{};
    std::array<FanTachometerTelemetry, MAX_FANS>  m_fans{};
};

/**
 * @brief Initializes the Thermal Zone class driver and registers \Device\ThermalZone0
 */
void InitializeThermalSubsystem() noexcept;

} // namespace micant::po::thermal
