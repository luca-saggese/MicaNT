#pragma once

// Small native command engine for cmd.exe. This header deliberately depends
// only on the mode-neutral NT ABI; the window, keyboard and screen buffer
// belong to the frontend.

#include "micant/shared/native_nt_api.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" {
struct MicantStartupInfoW {
    uint32_t cb{};
    const wchar_t* reserved{};
    const wchar_t* desktop{};
    const wchar_t* title{};
    uint32_t x{}, y{}, xSize{}, ySize{}, xCountChars{}, yCountChars{}, fillAttribute{}, flags{};
    uint16_t showWindow{}, reserved2Size{};
    uint8_t* reserved2{};
    void* standardInput{};
    void* standardOutput{};
    void* standardError{};
};
struct MicantProcessInformation {
    void* process{};
    void* thread{};
    uint32_t processId{};
    uint32_t threadId{};
};
__declspec(dllimport) int32_t __stdcall CreateProcessW(
    const wchar_t*, wchar_t*, void*, void*, int32_t, uint32_t, void*,
    const wchar_t*, MicantStartupInfoW*, MicantProcessInformation*) noexcept;
__declspec(dllimport) uint32_t __stdcall GetLastError() noexcept;
__declspec(dllimport) uint32_t __stdcall WaitForSingleObject(void*, uint32_t) noexcept;
__declspec(dllimport) int32_t __stdcall GetExitCodeProcess(void*, uint32_t*) noexcept;
}

namespace micant::cmd {

inline constexpr size_t MaxPathCharacters = 1024;
inline constexpr uint32_t FileOpen = 1;
inline constexpr uint32_t FileNonDirectoryFile = 0x00000040;
inline constexpr uint32_t FileShareReadWriteDelete = 0x00000007;
inline constexpr uint32_t FileListDirectory = 0x00000001;
inline constexpr uint32_t FileReadData = 0x00000001;

struct CommandResult {
    micant::NtStatus status{micant::NtStatus::Success};
    bool exitRequested{false};
    [[nodiscard]] explicit operator bool() const noexcept {
        return status == micant::NtStatus::Success;
    }
};

// The frontend supplies a writer accepting std::wstring_view and a clear()
// operation for the cls built-in. The initial
// directory must come from real process startup state; construction validates
// it by opening it as a directory. Paths are retained in user mode, while all
// file operations use native NT handles.
class CommandEngine final {
public:
    static micant::NtStatus systemRootDirectory(wchar_t* output, size_t capacity,
                                                size_t& length) noexcept {
        length = 0;
        if (!output || !capacity) return micant::NtStatus::InvalidParameter;
        const auto* shared = reinterpret_cast<volatile const micant::KUserSharedData*>(
            micant::UserSharedDataAddress);
        size_t count = 0;
        while (count < 260 && shared->ntSystemRoot[count] != 0) ++count;
        if (count < 3 || count >= capacity || shared->ntSystemRoot[1] != u':' ||
            shared->ntSystemRoot[2] != u'\\') return micant::NtStatus::ObjectPathSyntaxBad;
        for (size_t i = 0; i < count; ++i) output[i] = static_cast<wchar_t>(shared->ntSystemRoot[i]);
        output[count] = L'\0';
        length = count;
        return micant::NtStatus::Success;
    }

    explicit CommandEngine(std::wstring_view initialDirectory) {
        if (initialDirectory.empty() || initialDirectory.size() >= MaxPathCharacters) {
            initializationStatus_ = micant::NtStatus::InvalidParameter;
            return;
        }
        wchar_t candidate[MaxPathCharacters]{};
        for (size_t i = 0; i < initialDirectory.size(); ++i) candidate[i] = initialDirectory[i];
        const auto status = openDirectory(std::wstring_view(candidate, initialDirectory.size()), nullptr);
        if (status != micant::NtStatus::Success) {
            initializationStatus_ = status;
            return;
        }
        setCurrentDirectoryValue(std::wstring_view(candidate, initialDirectory.size()));
    }

    [[nodiscard]] micant::NtStatus initializationStatus() const noexcept {
        return initializationStatus_;
    }
    [[nodiscard]] std::wstring_view currentDirectory() const noexcept {
        return {currentDirectory_, currentDirectoryLength_};
    }

    ~CommandEngine() { closeChildHandles(); }

    template<class Writer>
    CommandResult execute(std::wstring_view line, Writer&& write) {
        if (initializationStatus_ != micant::NtStatus::Success)
            return fail(initializationStatus_, write);

        reapChildren(write);
        line = trim(line);
        if (line.empty()) return {};

        const size_t commandEnd = line.find_first_of(L" \t");
        const auto command = std::wstring_view(line.data(),
            commandEnd == std::wstring_view::npos ? line.size() : commandEnd);
        auto argument = commandEnd == std::wstring_view::npos
            ? std::wstring_view{} : trim(std::wstring_view(
                line.data() + commandEnd, line.size() - commandEnd));

        if (equalsInsensitive(command, L"help")) {
            write(L"help  ver  echo  cls  cd  chdir  dir  type  exit\r\n");
            return {};
        }
        if (equalsInsensitive(command, L"ver")) {
            const auto* shared = reinterpret_cast<volatile const micant::KUserSharedData*>(
                micant::UserSharedDataAddress);
            const uint32_t major = shared->ntMajorVersion;
            const uint32_t minor = shared->ntMinorVersion;
            const uint32_t build = shared->ntBuildNumber;
            if (major || minor || build) {
                write(L"LuceOS NT ");
                writeDecimal(major, write);
                write(L".");
                writeDecimal(minor, write);
                write(L" (build ");
                writeDecimal(build, write);
                write(L")\r\n");
                return {};
            }
#if defined(MICANT_PROJECT_VERSION_MAJOR) && defined(MICANT_PROJECT_VERSION_MINOR) && \
    defined(MICANT_PROJECT_VERSION_PATCH)
            // The project version is injected from CMake's VERSION tuple and
            // is the real build metadata when KUSER has no NT version yet.
            write(L"LuceOS/MicaNT ");
            writeDecimal(MICANT_PROJECT_VERSION_MAJOR, write);
            write(L".");
            writeDecimal(MICANT_PROJECT_VERSION_MINOR, write);
            write(L".");
            writeDecimal(MICANT_PROJECT_VERSION_PATCH, write);
            write(L"\r\n");
            return {};
#else
                return fail(micant::NtStatus::NotSupported, write);
#endif
        }
        if (equalsInsensitive(command, L"echo")) {
            write(argument);
            write(L"\r\n");
            return {};
        }
        if (equalsInsensitive(command, L"cls")) {
            write.clear();
            return {};
        }
        if (equalsInsensitive(command, L"exit")) {
            closeChildHandles();
            return {micant::NtStatus::Success, true};
        }
        if (equalsInsensitive(command, L"cd") || equalsInsensitive(command, L"chdir")) {
            if (argument.empty()) {
                write(currentDirectory());
                write(L"\r\n");
                return {};
            }
            wchar_t path[MaxPathCharacters]{};
            size_t pathLength = 0;
            auto status = resolvePath(argument, path, pathLength);
            if (status == micant::NtStatus::Success)
                status = openDirectory(std::wstring_view(path, pathLength), nullptr);
            if (status != micant::NtStatus::Success) return fail(status, write);
            setCurrentDirectoryValue(std::wstring_view(path, pathLength));
            return {};
        }
        if (equalsInsensitive(command, L"dir")) {
            wchar_t path[MaxPathCharacters]{};
            size_t pathLength = 0;
            auto status = resolvePath(argument.empty() ? currentDirectory() : argument, path, pathLength);
            if (status != micant::NtStatus::Success) return fail(status, write);
            return listDirectory(std::wstring_view(path, pathLength), write);
        }
        if (equalsInsensitive(command, L"type")) {
            if (argument.empty()) return fail(micant::NtStatus::InvalidParameter, write);
            wchar_t path[MaxPathCharacters]{};
            size_t pathLength = 0;
            auto status = resolvePath(argument, path, pathLength);
            if (status != micant::NtStatus::Success) return fail(status, write);
            return displayFile(std::wstring_view(path, pathLength), write);
        }
        return launchProgram(line, write);
    }

private:
    struct ChildProcess {
        void* handle{};
        uint32_t processId{};
    };

    void closeChildHandles() noexcept {
        for (auto& child : children_) {
            if (child.handle)
                (void)micant::native_nt::NtClose(reinterpret_cast<micant::Handle>(child.handle));
            child = {};
        }
    }

    template<class Writer>
    void reapChildren(Writer& write) noexcept {
        for (auto& child : children_) {
            if (!child.handle) continue;
            const auto wait = WaitForSingleObject(child.handle, 0);
            if (wait == 0x00000102u) continue;
            if (wait != 0) {
                write(L"Process wait failed for ");
                writeDecimal(child.processId, write);
                write(L".\r\n");
                (void)micant::native_nt::NtClose(reinterpret_cast<micant::Handle>(child.handle));
                child = {};
                continue;
            }
            uint32_t exitCode{};
            if (GetExitCodeProcess(child.handle, &exitCode)) {
                write(L"Process ");
                writeDecimal(child.processId, write);
                write(L" exited with code 0x");
                writeHex(exitCode, write);
                write(L".\r\n");
            } else {
                write(L"Process exit query failed for ");
                writeDecimal(child.processId, write);
                write(L".\r\n");
            }
            (void)micant::native_nt::NtClose(reinterpret_cast<micant::Handle>(child.handle));
            child = {};
        }
    }

    template<class Writer>
    CommandResult launchProgram(std::wstring_view line, Writer& write) {
        // Parse only the executable token here; CreateProcessW receives the
        // original writable command line so the child sees its arguments.
        line = trim(line);
        const bool quoted = !line.empty() && line.front() == L'"';
        size_t tokenEnd = quoted ? line.find(L'"', 1) : line.find_first_of(L" \t");
        if (quoted && tokenEnd == std::wstring_view::npos)
            return fail(micant::NtStatus::InvalidParameter, write);
        if (!quoted && tokenEnd == std::wstring_view::npos) tokenEnd = line.size();
        const size_t tokenStart = quoted ? 1 : 0;
        auto token = std::wstring_view(line.data() + tokenStart, tokenEnd - tokenStart);
        if (token.empty() || token.size() >= MaxPathCharacters)
            return fail(micant::NtStatus::InvalidParameter, write);
        wchar_t imagePath[MaxPathCharacters]{};
        size_t imageLength{};
        auto status = resolvePath(token, imagePath, imageLength);
        if (status != micant::NtStatus::Success) return fail(status, write);
        bool hasExtension = false;
        for (size_t i = token.size(); i > 0; --i) {
            if (token[i - 1] == L'.') { hasExtension = true; break; }
            if (token[i - 1] == L'\\' || token[i - 1] == L'/') break;
        }
        if (!hasExtension) {
            if (imageLength + 4 >= MaxPathCharacters)
                return fail(micant::NtStatus::InvalidParameter, write);
            imagePath[imageLength++] = L'.'; imagePath[imageLength++] = L'e';
            imagePath[imageLength++] = L'x'; imagePath[imageLength++] = L'e';
            imagePath[imageLength] = L'\0';
        }
        ChildProcess* freeSlot{};
        for (auto& candidate : children_) if (!candidate.handle) { freeSlot = &candidate; break; }
        if (!freeSlot) return fail(micant::NtStatus::InsufficientResources, write);
        wchar_t commandLine[4097]{};
        if (line.size() >= 4097) return fail(micant::NtStatus::InvalidParameter, write);
        for (size_t i = 0; i < line.size(); ++i) commandLine[i] = line[i];
        commandLine[line.size()] = L'\0';
        MicantStartupInfoW startup{};
        startup.cb = sizeof(startup);
        MicantProcessInformation child{};
        if (!CreateProcessW(imagePath, commandLine, nullptr, nullptr, 0, 0,
                            nullptr, currentDirectory_, &startup, &child)) {
            const uint32_t error = GetLastError();
            write(L"CreateProcessW failed (Win32 error 0x");
            writeHex(error, write);
            write(L").\r\n");
            return {micant::NtStatus::Unsuccessful, false};
        }
        write(L"Started process ");
        writeDecimal(child.processId, write);
        write(L" (thread ");
        writeDecimal(child.threadId, write);
        write(L").\r\n");
        const auto closeThread = micant::native_nt::NtClose(reinterpret_cast<micant::Handle>(child.thread));
        if (closeThread != micant::NtStatus::Success) {
            (void)micant::native_nt::NtClose(reinterpret_cast<micant::Handle>(child.process));
            return fail(closeThread, write);
        }
        *freeSlot = {child.process, child.processId};
        return {};
    }

    template<class Writer>
    static void writeHex(uint32_t value, Writer& write) {
        constexpr wchar_t digits[] = L"0123456789ABCDEF";
        wchar_t buffer[8];
        for (size_t i = 0; i < 8; ++i) {
            buffer[7 - i] = digits[value & 0xF];
            value >>= 4;
        }
        write(std::wstring_view(buffer, 8));
    }

    static std::wstring_view trim(std::wstring_view text) noexcept {
        while (!text.empty() && (text.front() == L' ' || text.front() == L'\t')) text.remove_prefix(1);
        while (!text.empty() && (text.back() == L' ' || text.back() == L'\t')) text.remove_suffix(1);
        return text;
    }

    static bool equalsInsensitive(std::wstring_view left, std::wstring_view right) noexcept {
        if (left.size() != right.size()) return false;
        for (size_t i = 0; i < left.size(); ++i) {
            wchar_t a = left[i], b = right[i];
            if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
            if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
            if (a != b) return false;
        }
        return true;
    }

    template<class Writer>
    static void writeDecimal(uint32_t value, Writer& write) {
        wchar_t buffer[10];
        size_t count = 0;
        do { buffer[count++] = static_cast<wchar_t>(L'0' + (value % 10)); value /= 10; } while (value);
        while (count) write(std::wstring_view(&buffer[--count], 1));
    }

    template<class Writer>
    static CommandResult fail(micant::NtStatus status, Writer& write) {
        static constexpr wchar_t prefix[] = L"Command failed (NTSTATUS 0x";
        static constexpr wchar_t suffix[] = L").\r\n";
        wchar_t digits[8];
        uint32_t value = static_cast<uint32_t>(status);
        constexpr wchar_t hex[] = L"0123456789ABCDEF";
        for (size_t i = 0; i < 8; ++i) {
            digits[7 - i] = hex[value & 0xF];
            value >>= 4;
        }
        write(std::wstring_view(prefix, sizeof(prefix) / sizeof(prefix[0]) - 1));
        write(std::wstring_view(digits, 8));
        write(std::wstring_view(suffix, sizeof(suffix) / sizeof(suffix[0]) - 1));
        return {status, false};
    }

    static micant::NtStatus openPath(std::wstring_view path, uint32_t access,
                                     uint32_t options, micant::Handle& handle) {
        if (path.empty() || path.size() >= MaxPathCharacters || path.size() > 0x7FFF / sizeof(wchar_t))
            return micant::NtStatus::InvalidParameter;
        wchar_t terminated[MaxPathCharacters]{};
        for (size_t i = 0; i < path.size(); ++i) terminated[i] = path[i];
        micant::UnicodeString name(terminated);
        micant::ObjectAttributes attributes{};
        attributes.objectName = &name;
        attributes.attributes = micant::OBJ_CASE_INSENSITIVE;
        micant::IoStatusBlock ioStatus{};
        return micant::native_nt::NtCreateFile(&handle, access, &attributes, &ioStatus,
            nullptr, 0, FileShareReadWriteDelete, FileOpen, options, nullptr, 0);
    }

    static micant::NtStatus openDirectory(std::wstring_view path, micant::Handle* result) {
        micant::Handle handle{};
        // The current NtCreateFile syscall does not accept FILE_DIRECTORY_FILE.
        // Open through the real filesystem owner, then use its native directory
        // query contract to validate the object type (including empty dirs).
        auto status = openPath(path, FileListDirectory, 0, handle);
        if (status != micant::NtStatus::Success) return status;
        micant::FileBothDirInformation entry{};
        micant::IoStatusBlock ioStatus{};
        status = micant::native_nt::NtQueryDirectoryFile(handle, 0, nullptr, nullptr,
            &ioStatus, &entry, sizeof(entry), micant::FileInformationClass::FileBothDirectoryInformation,
            true, nullptr, true);
        if (status == micant::NtStatus::NoMoreFiles) status = micant::NtStatus::Success;
        if (status != micant::NtStatus::Success) {
            const auto closeStatus = micant::native_nt::NtClose(handle);
            if (closeStatus != micant::NtStatus::Success) return closeStatus;
            return status;
        }
        if (result) *result = handle;
        else {
            const auto closeStatus = micant::native_nt::NtClose(handle);
            if (closeStatus != micant::NtStatus::Success) return closeStatus;
        }
        return status;
    }

    micant::NtStatus resolvePath(std::wstring_view input, wchar_t* result, size_t& resultLength) const {
        resultLength = 0;
        if (!result) return micant::NtStatus::InvalidParameter;
        input = trim(input);
        if (input.size() >= 2 && input.front() == L'"' && input.back() == L'"') {
            input.remove_prefix(1);
            input.remove_suffix(1);
        }
        if (input.empty()) return micant::NtStatus::InvalidParameter;
        if (input.size() >= 2 && input[1] == L':' &&
            (input.size() < 3 || (input[2] != L'\\' && input[2] != L'/')))
            return micant::NtStatus::NotSupported; // Per-drive current directories are not yet represented.
        wchar_t combined[MaxPathCharacters]{};
        size_t combinedLength = 0;
        const auto append = [&](wchar_t ch) {
            if (combinedLength + 1 >= MaxPathCharacters) return false;
            combined[combinedLength++] = ch;
            return true;
        };
        const bool driveAbsolute = input.size() >= 3 && input[1] == L':' &&
            (input[2] == L'\\' || input[2] == L'/');
        if (driveAbsolute) {
            for (wchar_t ch : input) if (!append(ch)) return micant::NtStatus::InvalidParameter;
        }
        else if (input.front() == L'\\' || input.front() == L'/') {
            if (currentDirectoryLength_ < 2 || currentDirectory_[1] != L':')
                return micant::NtStatus::NotSupported;
            if (!append(currentDirectory_[0]) || !append(L':')) return micant::NtStatus::InvalidParameter;
            for (wchar_t ch : input) if (!append(ch)) return micant::NtStatus::InvalidParameter;
        } else {
            for (size_t i = 0; i < currentDirectoryLength_; ++i)
                if (!append(currentDirectory_[i])) return micant::NtStatus::InvalidParameter;
            if (combinedLength && combined[combinedLength - 1] != L'\\')
                if (!append(L'\\')) return micant::NtStatus::InvalidParameter;
            for (wchar_t ch : input) if (!append(ch)) return micant::NtStatus::InvalidParameter;
        }
        for (size_t i = 0; i < combinedLength; ++i) if (combined[i] == L'/') combined[i] = L'\\';
        if (combinedLength < 3 || combined[1] != L':' || combined[2] != L'\\')
            return micant::NtStatus::ObjectPathSyntaxBad;

        wchar_t normalized[MaxPathCharacters]{};
        size_t normalizedLength = 3;
        normalized[0] = combined[0]; normalized[1] = L':'; normalized[2] = L'\\';
        size_t pos = 3;
        while (pos < combinedLength) {
            while (pos < combinedLength && combined[pos] == L'\\') ++pos;
            if (pos == combinedLength) break;
            size_t end = pos;
            while (end < combinedLength && combined[end] != L'\\') ++end;
            auto component = std::wstring_view(combined + pos, end - pos);
            if (component == L".") { }
            else if (component == L"..") {
                if (normalizedLength > 3) {
                    if (normalized[normalizedLength - 1] == L'\\') --normalizedLength;
                    while (normalizedLength > 3 && normalized[normalizedLength - 1] != L'\\') --normalizedLength;
                }
            } else {
                if (normalizedLength > 3 && normalized[normalizedLength - 1] != L'\\')
                    normalized[normalizedLength++] = L'\\';
                if (normalizedLength + component.size() >= MaxPathCharacters)
                    return micant::NtStatus::InvalidParameter;
                for (wchar_t ch : component) normalized[normalizedLength++] = ch;
            }
            pos = end;
        }
        for (size_t i = 0; i < normalizedLength; ++i) result[i] = normalized[i];
        resultLength = normalizedLength;
        return micant::NtStatus::Success;
    }

    template<class Writer>
    static CommandResult listDirectory(std::wstring_view path, Writer& write) {
        micant::Handle directory{};
        auto status = openDirectory(path, &directory);
        if (status != micant::NtStatus::Success) return fail(status, write);
        micant::IoStatusBlock ioStatus{};
        bool restart = true;
        for (;;) {
            micant::FileBothDirInformation entry{};
            status = micant::native_nt::NtQueryDirectoryFile(directory, 0, nullptr, nullptr,
                &ioStatus, &entry, sizeof(entry), micant::FileInformationClass::FileBothDirectoryInformation,
                true, nullptr, restart);
            restart = false;
            if (status == micant::NtStatus::NoMoreFiles) {
                status = micant::NtStatus::Success;
                break;
            }
            if (status != micant::NtStatus::Success) break;
            if (entry.fileNameLength % sizeof(wchar_t) != 0 ||
                entry.fileNameLength > sizeof(entry.fileName)) {
                status = micant::NtStatus::InvalidParameter;
                break;
            }
            write(std::wstring_view(entry.fileName, entry.fileNameLength / sizeof(wchar_t)));
            write(L"\r\n");
        }
        const auto closeStatus = micant::native_nt::NtClose(directory);
        if (status == micant::NtStatus::Success && closeStatus != micant::NtStatus::Success) status = closeStatus;
        return status == micant::NtStatus::Success ? CommandResult{} : fail(status, write);
    }

    template<class Writer>
    static CommandResult displayFile(std::wstring_view path, Writer& write) {
        micant::Handle file{};
        auto status = openPath(path, FileReadData, FileNonDirectoryFile, file);
        if (status != micant::NtStatus::Success) return fail(status, write);
        uint8_t bytes[512];
        wchar_t chars[512];
        bool first = true;
        bool utf16 = false;
        bool pendingByte = false;
        uint8_t lowByte = 0;
        uint8_t utf8Pending[4]{};
        size_t utf8PendingCount = 0;
        for (;;) {
            micant::IoStatusBlock ioStatus{};
            status = micant::native_nt::NtReadFile(file, 0, nullptr, nullptr, &ioStatus,
                bytes, sizeof(bytes), nullptr, nullptr);
            if (status == micant::NtStatus::EndOfFile ||
                (status == micant::NtStatus::Success && ioStatus.information == 0)) {
                status = micant::NtStatus::Success;
                break;
            }
            if (status != micant::NtStatus::Success) break;
            size_t count = static_cast<size_t>(ioStatus.information);
            if (count > sizeof(bytes)) { status = micant::NtStatus::InvalidParameter; break; }
            size_t out = 0;
            size_t i = 0;
            if (first) {
                first = false;
                if (count >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
                    utf16 = true; i = 2;
                } else if (count >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
                    i = 3;
                }
            }
            if (utf16) {
                if (pendingByte && i < count) {
                    chars[out++] = static_cast<wchar_t>(lowByte | (static_cast<uint16_t>(bytes[i++]) << 8));
                    pendingByte = false;
                }
                while (i + 1 < count) {
                    chars[out++] = static_cast<wchar_t>(bytes[i] | (static_cast<uint16_t>(bytes[i + 1]) << 8));
                    i += 2;
                }
                if (i < count) { lowByte = bytes[i]; pendingByte = true; }
            } else {
                uint8_t input[516];
                size_t inputCount = 0;
                for (size_t p = 0; p < utf8PendingCount; ++p) input[inputCount++] = utf8Pending[p];
                utf8PendingCount = 0;
                while (i < count) input[inputCount++] = bytes[i++];
                i = 0;
                while (i < inputCount) {
                    const uint8_t lead = input[i];
                    size_t sequence = lead < 0x80 ? 1 :
                        (lead >= 0xC2 && lead <= 0xDF ? 2 :
                        (lead >= 0xE0 && lead <= 0xEF ? 3 :
                        (lead >= 0xF0 && lead <= 0xF4 ? 4 : 0)));
                    if (sequence == 0) {
                        chars[out++] = static_cast<wchar_t>(0xFFFD);
                        ++i;
                        continue;
                    }
                    if (inputCount - i < sequence) {
                        while (i < inputCount) utf8Pending[utf8PendingCount++] = input[i++];
                        break;
                    }
                    uint32_t codepoint = sequence == 1 ? lead : (lead & ((1u << (7 - sequence)) - 1u));
                    bool valid = true;
                    for (size_t n = 1; n < sequence; ++n) {
                        if ((input[i + n] & 0xC0) != 0x80) { valid = false; break; }
                        codepoint = (codepoint << 6) | (input[i + n] & 0x3F);
                    }
                    if ((sequence == 3 && codepoint < 0x800) ||
                        (sequence == 4 && codepoint < 0x10000) ||
                        codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
                        valid = false;
                    if (!valid) {
                        chars[out++] = static_cast<wchar_t>(0xFFFD);
                        ++i;
                        continue;
                    }
                    if (codepoint <= 0xFFFF) chars[out++] = static_cast<wchar_t>(codepoint);
                    else {
                        codepoint -= 0x10000;
                        chars[out++] = static_cast<wchar_t>(0xD800 + (codepoint >> 10));
                        chars[out++] = static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF));
                    }
                    i += sequence;
                }
            }
            if (out) write(std::wstring_view(chars, out));
        }
        if (status == micant::NtStatus::Success && pendingByte && utf16) {
            chars[0] = static_cast<wchar_t>(lowByte);
            write(std::wstring_view(chars, 1));
        }
        if (status == micant::NtStatus::Success && utf8PendingCount) {
            chars[0] = static_cast<wchar_t>(0xFFFD);
            write(std::wstring_view(chars, 1));
        }
        const auto closeStatus = micant::native_nt::NtClose(file);
        if (status == micant::NtStatus::Success && closeStatus != micant::NtStatus::Success) status = closeStatus;
        return status == micant::NtStatus::Success ? CommandResult{} : fail(status, write);
    }

    void setCurrentDirectoryValue(std::wstring_view value) noexcept {
        currentDirectoryLength_ = value.size();
        for (size_t i = 0; i < value.size(); ++i) currentDirectory_[i] = value[i];
        currentDirectory_[currentDirectoryLength_] = L'\0';
    }

    wchar_t currentDirectory_[MaxPathCharacters]{};
    size_t currentDirectoryLength_{0};
    micant::NtStatus initializationStatus_{micant::NtStatus::Success};
    ChildProcess children_[8]{};
};

} // namespace micant::cmd
