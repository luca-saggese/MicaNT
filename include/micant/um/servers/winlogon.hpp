#pragma once

/**
 * @file winlogon.hpp
 * @brief MicaNT Interactive Logon Manager (Winlogon Subsystem).
 *
 * Implements interactive user session management, window station desktop
 * segregation (Winlogon secure desktop vs Default user desktop), Secure Attention
 * Sequence (SAS, Ctrl+Alt+Del) handling, workstation locking, and user shell spawning.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <optional>
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/km/executive/se.hpp"
#include "micant/um/subsystems/security/sam.hpp"
#include "micant/um/servers/lsass.hpp"

namespace micant::winlogon {

// ============================================================================
// WindowStation Desktop Isolation Types
// ============================================================================

enum class DesktopType : uint32_t {
    Winlogon    = 0, // Isolated secure desktop (Credentials, Lock, UAC, SAS)
    Default     = 1, // Interactive user desktop (Explorer, CMD, applications)
    ScreenSaver = 2  // Screen saver desktop
};

inline std::wstring_view desktopTypeToString(DesktopType type) noexcept {
    switch (type) {
        case DesktopType::Winlogon:    return L"Winlogon";
        case DesktopType::Default:     return L"Default";
        case DesktopType::ScreenSaver: return L"ScreenSaver";
        default:                       return L"Unknown";
    }
}

// ============================================================================
// Winlogon State Machine States
// ============================================================================

enum class LogonState : uint32_t {
    LoggedOff      = 0, // No user logged on, prompt active on Winlogon desktop
    Authenticating = 1, // Credentials undergoing LSASS package validation
    LoggedOn       = 2, // User active, shell running on Default desktop
    Locked         = 3  // Workstation locked, prompt active on Winlogon desktop
};

inline std::wstring_view logonStateToString(LogonState state) noexcept {
    switch (state) {
        case LogonState::LoggedOff:      return L"LoggedOff";
        case LogonState::Authenticating: return L"Authenticating";
        case LogonState::LoggedOn:       return L"LoggedOn";
        case LogonState::Locked:         return L"Locked";
        default:                         return L"Unknown";
    }
}

// ============================================================================
// Secure Attention Sequence (SAS) Actions
// ============================================================================

enum class SasAction : uint32_t {
    None            = 0,
    LogonPrompt     = 1, // Initiates interactive logon dialog
    SecurityOptions = 2, // Displays Windows Security dialog (Lock, Sign out, etc.)
    UnlockPrompt    = 3  // Prompts for credentials to unlock desktop
};

inline std::wstring_view sasActionToString(SasAction action) noexcept {
    switch (action) {
        case SasAction::LogonPrompt:     return L"LogonPrompt";
        case SasAction::SecurityOptions: return L"SecurityOptions";
        case SasAction::UnlockPrompt:    return L"UnlockPrompt";
        default:                         return L"None";
    }
}

// ============================================================================
// Winlogon Manager Singleton
// ============================================================================

class WinlogonManager {
public:
    static WinlogonManager& get() {
        static WinlogonManager instance;
        return instance;
    }

    WinlogonManager(const WinlogonManager&) = delete;
    WinlogonManager& operator=(const WinlogonManager&) = delete;

    /**
     * @brief Initiates interactive user logon.
     */
    NTSTATUS initiateLogon(
        const std::wstring& username,
        const std::wstring& password,
        const std::wstring& domain = L"MICANT"
    ) {
        (void)username;
        (void)password;
        (void)domain;
        // This hosted compatibility manager has no authenticated LPC path,
        // kernel-issued token assignment, secure desktop, or shell process
        // lifecycle. Never convert its in-process SAM adapter into a logon.
        return STATUS_NOT_SUPPORTED;
    }

    /**
     * @brief Locks workstation display and switches to secure Winlogon desktop.
     */
    bool lockWorkstation() {
        return false;
    }

    /**
     * @brief Unlocks workstation with current user credentials.
     */
    NTSTATUS unlockWorkstation(const std::wstring& password) {
        (void)password;
        return STATUS_NOT_SUPPORTED;
    }

    /**
     * @brief Logs off current session, terminates shell, and returns to secure desktop.
     */
    NTSTATUS logoff() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == LogonState::LoggedOff) {
            return STATUS_SUCCESS;
        }

        if (activeLogonId_ != Luid{0, 0}) {
            lsass::LocalSecurityAuthority::get().logoffUser(activeLogonId_);
        }

        loggedOnUser_.clear();
        loggedOnDomain_.clear();
        activeLogonId_ = Luid{0, 0};
        activeToken_.reset();
        shellPid_ = 0;
        state_ = LogonState::LoggedOff;
        activeDesktop_ = DesktopType::Winlogon;

        return STATUS_SUCCESS;
    }

    /**
     * @brief Triggers Secure Attention Sequence (SAS, Ctrl+Alt+Del).
     */
    SasAction triggerSas() {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (state_) {
            case LogonState::LoggedOff:
                return SasAction::LogonPrompt;

            case LogonState::LoggedOn:
                return SasAction::SecurityOptions;

            case LogonState::Locked:
                return SasAction::UnlockPrompt;

            default:
                return SasAction::None;
        }
    }

    /**
     * @brief Dismisses security options dialog and returns to user desktop.
     */
    void dismissSecurityOptions() {
        // No secure desktop switch is implemented in this hosted adapter.
    }

    /**
     * @brief Switches current active desktop.
     */
    void switchDesktop(DesktopType desktop) {
        (void)desktop;
        // A desktop name cannot substitute for compositor/input isolation.
    }

    // Accessors
    [[nodiscard]] LogonState getState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    [[nodiscard]] DesktopType getActiveDesktop() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeDesktop_;
    }

    [[nodiscard]] std::wstring getLoggedOnUser() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return loggedOnUser_;
    }

    [[nodiscard]] std::wstring getLoggedOnDomain() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return loggedOnDomain_;
    }

    [[nodiscard]] std::shared_ptr<se::TokenObject> getActiveToken() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeToken_;
    }

    [[nodiscard]] Luid getActiveLogonId() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeLogonId_;
    }

    [[nodiscard]] uint32_t getSessionId() const noexcept {
        return sessionId_;
    }

    [[nodiscard]] uint32_t getShellPid() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shellPid_;
    }

private:
    WinlogonManager() = default;

    mutable std::mutex mutex_;
    uint32_t sessionId_{1};
    LogonState state_{LogonState::LoggedOff};
    DesktopType activeDesktop_{DesktopType::Winlogon};
    std::wstring loggedOnUser_;
    std::wstring loggedOnDomain_;
    Luid activeLogonId_{0, 0};
    std::shared_ptr<se::TokenObject> activeToken_;
    uint32_t shellPid_{0};
};

} // namespace micant::winlogon
