#pragma once

/**
 * @file core_commands.hpp
 * @brief Core DOS/NT Shell Built-in Commands (help, ver, dir, cd, type, echo, set, cls, mem, sysinfo, ps)
 * Included directly within micant::shell::CommandShell.
 */

    void cmdHelp(std::ostream& out) {
        out << "MicaNT Shell Built-in Commands:\n"
            << "  HELP / ?          Displays this list of commands\n"
            << "  VER               Displays the MicaNT operating system version\n"
            << "  DIR / LS [path]   Lists files and directories in current or specified path\n"
            << "  CD [path]         Displays or changes the current directory\n"
            << "  TYPE / CAT <file> Displays the contents of a text file\n"
            << "  ECHO [text]       Prints text to the console, expanding %VARIABLES%\n"
            << "  SET [var=value]   Displays or modifies environment variables\n"
            << "  CLS / CLEAR       Clears the screen\n"
            << "  COLOR [attr]      Sets the console foreground/background color\n"
            << "  MEM               Displays kernel pool and virtual memory statistics\n"
            << "  SYSTEMINFO        Displays system architecture, HAL, and processor specs\n"
            << "  PS / TASKLIST     Lists active CSRSS registered processes and threads\n"
            << "  TIME / DATE       Displays system chronometry and timestamp\n"
            << "  IPCONFIG [/all]   Displays network adapter configuration and IP addresses\n"
            << "  PING <host>       Sends ICMP Echo Requests to verify network connectivity\n"
            << "  NETSTAT           Displays active TCP/UDP network connections and ports\n"
            << "  NET START/STOP    Controls and lists running Windows services\n"
            << "  NET USER [name]   Enumerates or modifies local user accounts in SAM\n"
            << "  SC QUERY/START    Interrogates and controls Service Control Manager\n"
            << "  WHOAMI [/priv]    Displays user identity, group SIDs, and token privileges\n"
            << "  PRISMX / GPU      Displays GPU adapters, VRAM, and runs 3D tests (prismx test)\n"
            << "  D3D9 / DX9        Initializes Direct3D 9 fixed-function pipeline test\n"
            << "  GDI               Displays GDI32 graphics engine info and verifies 2D drawing\n"
            << "  COM / OLE         Displays COM / OLE runtime info, GUID generation and BSTR test\n"
            << "  SHELL32           Displays known shell folders, tray manager, and parses args\n"
            << "  COMCTL            Displays common controls info and tests Progress/Status bars\n"
            << "  VIEW3D            Launches interactive 3D model viewer (view3d --torus|--cube|--crystal|--wireframe)\n"
            << "  VULKAN / VKINFO   Displays Vulkan ICD status, physical devices, and vkcube test\n"
            << "  WINMM             WinMM Multimedia API (audio devices, high-res timer, MCI, playback)\n"
            << "  DSOUND            DirectSound 8 3D Audio Subsystem & Real-Time Voice Mixer\n"
            << "  VERSION / VERINFO Queries Windows Version Information resource metadata for PE files\n"
            << "  OPENGL / GL / WGL Silicon Graphics OpenGL 1.4 API & Windows WGL 3D Runtime\n"
            << "  WININET           Windows Internet Subsystem (HTTP/1.1, cookies, URL cache)\n"
            << "  URLMON / CURL     Downloads web resources using URLDownloadToFile & MIME sniffer\n"
            << "  BCRYPT / CNG      Cryptography Next Generation (SHA256/384/512, MD5, AES, PBKDF2)\n"
            << "  CERTMGR           Windows Certificate Manager & X.509 Digital Certificate Store\n"
            << "  DPAPI             Data Protection API (CryptProtectData / CryptUnprotectData)\n"
            << "  SSPI              Security Support Provider Interface (Schannel TLS & NTLM)\n"
            << "  SCHANNEL          Secure Channel Subsystem (TLS 1.2 / TLS 1.3 Handshake & Framing)\n"
            << "  RPC               Remote Procedure Call Runtime & NDR Engine (rpcrt4.dll)\n"
            << "  UUIDGEN           Universally Unique Identifier (UUID / GUID) Generator\n"
            << "  OLEAUT            Windows OLE Automation, SafeArray & TypeLib Engine (oleaut32.dll)\n"
            << "  DEVMGMT / SETUP   Windows Device Manager & Installation Subsystem (setupapi.dll)\n"
            << "  STG / DOCFILE     Windows OLE Structured Storage & Compound File System (ole32.dll)\n"
            << "  WEVTUTIL / EVENTLOG Windows Event Log Subsystem & Diagnostics (wevtapi.dll)\n"
            << "  WMIC / WBEM       Windows Management Instrumentation Engine (wbemprox.dll)\n"
            << "  SCHTASKS          Windows Task Scheduler 2.0 Engine (taskschd.dll)\n"
            << "  BITSADMIN / BITS  Background Intelligent Transfer Service Queue Manager (qmgr.dll)\n"
            << "  VSSADMIN / VSS    Volume Shadow Copy Service Administration (vssapi.dll)\n"
            << "  DISM [/online ...] Deployment Image Servicing and Management Subsystem (dism test)\n"
            << "  MSDT [/id <name>] Microsoft Support Diagnostic Tool & WDI engine (msdt test)\n"
            << "  PERFMON / TYPEPERF Performance Monitor & Performance Counter sampling (perfmon test)\n"
            << "  DSQUERY           Active Directory query utility (dsquery user|computer|server|group|*)\n"
            << "  DSGET             Active Directory object attribute inspector (dsget user|computer|group)\n"
            << "  QWINSTA           Query Window Station / Session utility (qwinsta)\n"
            << "  RWINSTA <id>      Reset Window Station / Session utility (rwinsta <id>)\n"
            << "  MSTSC [/v:<host>] Remote Desktop Connection client (mstsc test)\n"
            << "  PRNMNGR           Printer configuration & management utility (prnmngr -l|-d|-s)\n"
            << "  PRINT [/D:<dev>]  Line printer & document spooling utility (print test)\n"
            << "  MCI [command]     Media Control Interface string command processor (mci test)\n"
            << "  WAVEPLAY [tone]   Waveform audio playback & streaming utility (waveplay test)\n"
            << "  SCARD [list|status] Smart Card & PC/SC subsystem utility (scard test)\n"
            << "  NLA [list|status] Windows Network Location Awareness & Network List (nla test)\n"
            << "  NOTIFY [toast|list|channel] Windows Push Notifications & Action Center (notify test)\n"
            << "  LOCATION [status|get|set] Windows Geolocation & Location Framework (location test)\n"
            << "  WPD [list|info|browse] Windows Portable Devices Subsystem (wpd test)\n"
            << "  SENSOR [list|read|test] Windows Sensors API & Sensor Platform (sensor test)\n"
            << "  WINBIO [list|status|verify|enroll|test] Windows Biometric Framework & Windows Hello (winbio test)\n"
            << "  BLUETOOTH [list|radios|info|pair|test] Windows Bluetooth Architecture & Radio (bluetooth test)\n"
            << "  CARDMOD [list|files|containers|auth|sign|test] Windows Smart Card Minidriver (cardmod test)\n"
            << "  POSIX [test|ps|sh|run|env] Windows POSIX.1 Subsystem & UNIX Architecture (posix test)\n"
            << "  WHP [test|capabilities|vms] Windows Hypervisor Platform & Virtualization (whp test)\n"
            << "  DWRITE [test|fonts|layout] Windows DirectWrite & Uniscribe Typography (dwrite test)\n"
            << "  MF [test|transforms|session] Windows Media Foundation Platform & Pipeline (mf test)\n"
            << "  MFSESSION [test|topology|info] Windows Media Foundation Topology & Pipeline (mfsession test)\n"
            << "  EVR [test|render|info]   Windows Enhanced Video Renderer Subsystem (evr test)\n"
            << "  DXVA2 [test|procamp|info] Windows DirectX Video Acceleration 2.0 (dxva2 test)\n"
            << "  D3D11VA [test|proc|info] Windows Direct3D 11 Video Acceleration (d3d11va test)\n"
            << "  DSHOW [test|filters|render|devices] Windows DirectShow & Filter Graph Architecture (dshow test)\n"
            << "  DCOMP [test|info|compose] Windows DirectComposition Modern Compositor (dcomp test)\n"
            << "  UICOMP [test|info|demo]  Windows UI Composition & Scene-Graph Visual Layer (uicomp test)\n"
            << "  WCS [test|info|gamut]    Windows Color System & HDR Subsystem (wcs test)\n"
            << "  POINTER [test|info|inject] Windows Pointer Device & Touch Subsystem (pointer test)\n"
            << "  APPMODEL [test|info|list|plm] Windows AppModel, Package Identity & PLM (appmodel test)\n"
            << "  D2D13 [test|info|demo]   Direct2D 1.3 SVG, Inking & Typography (d2d13 test)\n"
            << "  TSF [test|info|compose|candidates] Windows Text Services & Modern IME (tsf test)\n"
            << "  SPELL [test|info|check]  Windows Spell Checking & Extended Linguistics (spell test)\n"
            << "  SAPI [test|info|voices]  Windows Speech API & Voice Synthesis (sapi test)\n"
            << "  OCR [test|info|recognize] Windows Media OCR & Vision Subsystem (ocr test)\n"
            << "  WINML [test|info|run]    Windows Machine Learning & Neural Inference (winml test)\n"
            << "  WEBAUTHN [test|info|register|auth] Windows Web Authentication & FIDO2 Passkeys (webauthn test)\n"
            << "  WLAN [test|info|scan|list|connect|disconnect] Windows Native Wifi & WLAN (wlan test)\n"
            << "  VHD [test|info|create|attach|detach|expand|list] Windows Virtual Hard Disk (vhd test)\n"
            << "  MANAGE-BDE [status|on|off|lock|unlock|protectors|test] Full Volume Encryption / FVE (manage-bde compatibility)\n"
            << "  FIREWALL [show|set|add|delete|test] Windows Filtering Platform & Advanced Firewall (firewall test)\n"
            << "  SIGNTOOL [verify|sign|catdb|test] Windows Authenticode & Code Integrity Tool (signtool test)\n"
            << "  WDAC [status|mode|rules|logs|test] Windows Defender Application Control & CI (wdac test)\n"
            << "  CIPHER [/e|/d|/c|/k|/w|status|test] Windows Encrypting File System (EFS) Tool (cipher test)\n"
            << "  SENTINEL / WSC [status|health|products|register|unregister|test] Sentinel Security System for MicaNT (sentinel test)\n"
            << "  AMSI / SENTINELSCAN [status|scan|block|unblock|clear|test] Antimalware Scan Interface (amsi test)\n"
            << "  MPCMDRUN / DEFENDER [-Scan|-ListQuarantine|-Restore|-PurgeQuarantine|-GetFiles|test] Microsoft Defender Client & AegisDefender (defender test)\n"
            << "  GUARD / EXPLOITGUARD [status|list|enable|test] Windows Defender Exploit Guard & Process Mitigations (guard test)\n"
            << "  CREDGUARD / LSAISO [status|enable|disable|isolate|dump-attempt|test] Sovereign Credential Guard & IUM Enclave (credguard test)\n"
            << "  PPL / PROTECTEDPROCESS [status|set|signer|tamper|test] Protected Process Light & ELAM (ppl test)\n"
            << "  SYSGUARD / MEASUREDBOOT [status|pcr|measure|log|test] Windows System Guard & Secure Launch (sysguard test)\n"
            << "  VBS / HVCI [status|enable|verify|protect|pages|test] Virtualization-Based Security & HVCI (vbs test)\n"
            << "  DMAGUARD / DMA [status|devices|domains|policy|authorize|revoke|test] Kernel DMA Protection & IOMMU (dmaguard test)\n"
            << "  WSL / BASH / LXSS [status|list|run|mount|test] Windows Subsystem for Linux & Pico Kernel (wsl test)\n"
            << "  SANDBOX / WSB [status|launch|list|stop|destroy|map|exec|test] Windows Sandbox & Lightweight Containers (sandbox test)\n"
            << "  WHP / HYPERV [status|partitions|create|delete|test] Windows Hypervisor Platform & Viridian Hypervisor (whp test)\n"
            << "  WINGET [status|search|show|install|uninstall|list|upgrade|source|test] Windows Package Manager & App Installer (winget test)\n"
            << "  CAMERA [status|list|stream|snap|isp|test] Windows Camera Device Class Extension & Frame Server (camera test)\n"
            << "  VRR [status|list|set|drr|autohdr|profile|test] Windows Display Variable Refresh Rate & Auto HDR (vrr test)\n"
            << "  SENSORSCX [status|list|read|inject|fusion|orientation|test] Windows Sensor Class Extension v2 & 9-DoF Fusion (sensorscx test)\n"
            << "  WWAN / MBBCX [status|list|radio|connect|disconnect|signal|esim|test] Windows Mobile Broadband 5G & eSIM (wwan test)\n"
            << "  PMP / PAVP [status|monitors|hdcp|sessions|keys|decrypt|test] Windows Hardware Protected Media Path & HDCP 2.3 (pmp test)\n"
            << "  VMBUS [status|channels|offer|storvsc|netvsc|hvsock|balloon|test] Hyper-V Virtual Machine Bus & Synthetic Drivers (vmbus test)\n"
            << "  LOCK              Locks workstation and switches to secure Winlogon desktop\n"
            << "  LOGOFF            Logs off current interactive user session\n"
            << "  EXEC <binary.exe> Executes an unmodified 64-bit Windows PE binary\n"
            << "  EXIT / QUIT       Quits the MicaNT command shell\n";
    }


    void cmdVer(std::ostream& out) {
        out << "MicaNT [Version 10.0.26100.1]\n"
            << "Zero Telemetry Clean-Room Executive (x86_64, SMP 4-Core)\n";
    }


    void cmdCls(std::ostream& out) {
        auto session = conhost::ConhostManager::get().getConsole(win32::GetCurrentProcessId());
        if (session) {
            session->getScreenBuffer().clear();
        }
        out << "\033[2J\033[H"; // ANSI VT100 clear
    }


    void cmdDir(const std::vector<std::string>& tokens, std::ostream& out) {
        std::string searchDir = getCurrentDirectory();
        if (tokens.size() > 1) {
            searchDir = tokens[1];
        }

        std::wstring wSearch(searchDir.begin(), searchDir.end());
        std::vector<fs::VirtualFileSystem::DirectoryEntry> entries;
        NtStatus st = fs::VirtualFileSystem::get().queryDirectory(wSearch, entries);
        if (!NT_SUCCESS(st)) {
            out << "File Not Found: " << searchDir << "\n";
            return;
        }

        out << " Directory of " << searchDir << "\n\n";
        size_t totalFiles = 0;
        size_t totalBytes = 0;
        size_t totalDirs = 0;

        for (const auto& e : entries) {
            std::string name;
            for (wchar_t wc : e.name) name.push_back(static_cast<char>(wc & 0x7F));
            out << "10/01/2026  12:00 PM    ";
            if (e.isDirectory) {
                out << "<DIR>          ";
                totalDirs++;
            } else {
                out << std::setw(14) << e.size << " ";
                totalFiles++;
                totalBytes += e.size;
            }
            out << name << "\n";
        }
        out << "               " << totalFiles << " File(s)    " << totalBytes << " bytes\n";
        out << "               " << totalDirs << " Dir(s)     2,629,282,086,912 bytes free\n";
    }


    void cmdCd(const std::vector<std::string>& tokens, std::ostream& out) {
        if (tokens.size() < 2) {
            out << getCurrentDirectory() << "\n";
            return;
        }

        std::string target = tokens[1];
        if (target == "..") {
            std::string cur = getCurrentDirectory();
            size_t slash = cur.find_last_of("\\/");
            if (slash != std::string::npos && slash > 2) {
                target = cur.substr(0, slash);
            } else if (slash == 2) {
                target = cur.substr(0, 3); // Root path
            }
        }

        std::wstring wTarget(target.begin(), target.end());
        if (win32::SetCurrentDirectoryW(wTarget.c_str())) {
            // success
        } else {
            out << "The system cannot find the path specified: " << target << "\n";
        }
    }


    void cmdType(const std::vector<std::string>& tokens, std::ostream& out) {
        if (tokens.size() < 2) {
            out << "The syntax of the command is incorrect. Usage: type <file>\n";
            return;
        }

        std::string filename = tokens[1];
        std::wstring wFile(filename.begin(), filename.end());
        std::shared_ptr<fs::FileObject> fileObj;
        NtStatus st = fs::VirtualFileSystem::get().createOrOpenFile(wFile, fs::FILE_GENERIC_READ, fs::FILE_OPEN, fileObj);
        if (!NT_SUCCESS(st) || !fileObj) {
            out << "The system cannot find the file specified: " << filename << "\n";
            return;
        }

        const auto& data = fileObj->getData();
        std::string content(data.begin(), data.end());
        out << content;
        if (!content.empty() && content.back() != '\n') out << "\n";
    }


    void cmdEcho(const std::vector<std::string>& tokens, const std::string& fullLine, std::ostream& out) {
        if (tokens.size() < 2) {
            out << "ECHO is on.\n";
            return;
        }

        size_t pos = fullLine.find_first_not_of(" \t", 4);
        if (pos == std::string::npos) {
            out << "\n";
            return;
        }

        std::string text = fullLine.substr(pos);
        // Expand %VAR%
        std::string expanded;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '%' && i + 1 < text.size()) {
                size_t endPct = text.find('%', i + 1);
                if (endPct != std::string::npos) {
                    std::string varName = text.substr(i + 1, endPct - i - 1);
                    std::wstring wVar(varName.begin(), varName.end());
                    wchar_t valBuf[256]{};
                    win32::DWORD len = win32::GetEnvironmentVariableW(wVar.c_str(), valBuf, 256);
                    if (len > 0) {
                        for (win32::DWORD c = 0; c < len; ++c) {
                            expanded.push_back(static_cast<char>(valBuf[c] & 0x7F));
                        }
                    }
                    i = endPct;
                    continue;
                }
            }
            expanded.push_back(text[i]);
        }
        out << expanded << "\n";
    }


    void cmdSet(const std::vector<std::string>& tokens, std::ostream& out) {
        if (tokens.size() < 2) {
            // List common variables
            const wchar_t* commonVars[] = {
                L"OS", L"SystemRoot", L"windir", L"ComSpec", L"PATH",
                L"NUMBER_OF_PROCESSORS", L"PROCESSOR_ARCHITECTURE", L"USERPROFILE", L"HOMEDRIVE"
            };
            wchar_t valBuf[256]{};
            for (const auto* v : commonVars) {
                win32::DWORD len = win32::GetEnvironmentVariableW(v, valBuf, 256);
                if (len > 0) {
                    std::wcout << v << L"=" << valBuf << L"\n";
                }
            }
            return;
        }

        std::string expr = tokens[1];
        size_t eq = expr.find('=');
        if (eq == std::string::npos) {
            std::wstring wVar(expr.begin(), expr.end());
            wchar_t valBuf[256]{};
            win32::DWORD len = win32::GetEnvironmentVariableW(wVar.c_str(), valBuf, 256);
            if (len > 0) {
                std::wcout << wVar << L"=" << valBuf << L"\n";
            } else {
                out << "Environment variable " << expr << " not defined\n";
            }
        } else {
            std::string var = expr.substr(0, eq);
            std::string val = expr.substr(eq + 1);
            std::wstring wVar(var.begin(), var.end());
            std::wstring wVal(val.begin(), val.end());
            win32::SetEnvironmentVariableW(wVar.c_str(), wVal.c_str());
        }
    }


    void cmdColor(const std::vector<std::string>& tokens, std::ostream& out) {
        if (tokens.size() < 2) {
            win32::SetConsoleTextAttribute(win32::GetStdHandle(win32::STD_OUTPUT_HANDLE), 0x07); // Default White on Black
            return;
        }
        uint16_t attr = static_cast<uint16_t>(std::strtoul(tokens[1].c_str(), nullptr, 16));
        win32::SetConsoleTextAttribute(win32::GetStdHandle(win32::STD_OUTPUT_HANDLE), attr);
        out << "Console color set to 0x" << std::hex << attr << std::dec << "\n";
    }


    void cmdTime(std::ostream& out) {
        win32::SYSTEMTIME st{};
        win32::GetLocalTime(&st);
        out << "System Date: " << st.wMonth << "/" << st.wDay << "/" << st.wYear << "\n";
        out << "System Time: " << std::setfill('0') << std::setw(2) << st.wHour << ":"
            << std::setw(2) << st.wMinute << ":" << std::setw(2) << st.wSecond << "." 
            << std::setw(3) << st.wMilliseconds << "\n";
    }


    void cmdMem(std::ostream& out) {
        out << "\nMicaNT Memory Manager & Executive Pools:\n";
        out << "  - Kernel NonPaged Pool: " << (ex::ExecutivePool::get().getTotalNonPagedBytes() / 1024) << " KB\n";
        out << "  - Kernel Paged Pool:    " << (ex::ExecutivePool::get().getTotalPagedBytes() / 1024) << " KB\n";
        out << "  - Memory Tag 'Mica':    Active (Pool diagnostic tracking enabled)\n";
        out << "  - VAD Reservations:     Simulated 64-bit user/kernel address spaces\n";
        out << "  - Idle RAM Footprint:   < 32 MB total operating state\n\n";
    }


    void cmdSystemInfo(std::ostream& out) {
        auto& hal = hal::HardwareAbstractionLayer::get();
        (void)hal;
        LargeInteger freq{}, perf{};
        win32::QueryPerformanceFrequency(&freq);
        win32::QueryPerformanceCounter(&perf);

        out << "\nHost Name:                 MICANT-DESKTOP\n"
            << "OS Name:                   MicaNT Dave Cutler Clean-Room Executive\n"
            << "OS Version:                10.0.26100.1 N/A Build 26100\n"
            << "OS Build Type:             Multiprocessor Free (Zero Telemetry)\n"
            << "System Architecture:       x64-based PC (x86_64 AMD64 Long Mode)\n"
            << "Processor(s):              4 Processors Installed [SMP Multi-Core Topology]\n"
            << "HAL KPCR Structure:        GS:[0] (PRCB Active on Core 0..3)\n"
            << "QPC Performance Frequency: " << freq.quadPart << " Hz (1 GHz Monotonic)\n"
            << "Time Elapsed Since Boot:   " << (perf.quadPart / 1000000000ULL) << " seconds\n"
            << "Active Windows Subsystem:  CSRSS ALPC Server & ConHost Screen Blitter\n\n";
    }


    void cmdPs(std::ostream& out) {
        out << "\nImage Name                     PID Session Name        Session#    Mem Usage\n"
            << "========================= ======== ================ =========== ============\n"
            << "System                           4 Services                   0       256 K\n"
            << "smss.exe                       240 Services                   0       384 K\n"
            << "csrss.exe                      352 Console                    1     1,024 K\n"
            << "wininit.exe                    360 Services                   0       512 K\n"
            << "conhost.exe                    420 Console                    1       896 K\n"
            << "cmd.exe (msh)                  512 Console                    1       768 K\n\n";
    }


    void cmdLock(std::ostream& out) {
        if (winlogon::WinlogonManager::get().lockWorkstation()) {
            out << "The workstation is now locked. Switched to secure Winlogon desktop.\n";
        } else {
            out << "Unable to lock workstation: no interactive session is available.\n";
        }
    }


    void cmdLogoff(std::ostream& out) {
        if (winlogon::WinlogonManager::get().getState() == winlogon::LogonState::LoggedOff) {
            out << "No interactive user is logged on.\n";
            return;
        }
        winlogon::WinlogonManager::get().logoff();
        out << "Session terminated. User logged off. Switched to secure Winlogon desktop.\n";
    }


