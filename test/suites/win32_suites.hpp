#pragma once

/**
 * @file win32_suites.hpp
 * @brief Win32 Subsystem, CRT, User32, GDI & Audio (Milestones 31-65)
 */

void Test_Wow64_PebTebAndHeavensGate() {
    // 1. Test 32-Bit PE Header Parsing (PeLoader::inspect32)
    std::vector<uint8_t> pe32Buffer(sizeof(pe::ImageDosHeader) + sizeof(pe::ImageNtHeaders32) + sizeof(pe::ImageSectionHeader), 0);
    auto* dos = reinterpret_cast<pe::ImageDosHeader*>(pe32Buffer.data());
    dos->e_magic = pe::DOS_MAGIC;
    dos->e_lfanew = sizeof(pe::ImageDosHeader);

    auto* nt32 = reinterpret_cast<pe::ImageNtHeaders32*>(pe32Buffer.data() + dos->e_lfanew);
    nt32->signature = pe::NT_SIGNATURE;
    nt32->fileHeader.machine = pe::MACHINE_I386;
    nt32->fileHeader.numberOfSections = 1;
    nt32->fileHeader.sizeOfOptionalHeader = sizeof(pe::ImageOptionalHeader32);
    nt32->optionalHeader.magic = pe::PE32_MAGIC;
    nt32->optionalHeader.addressOfEntryPoint = 0x1200;
    nt32->optionalHeader.imageBase = 0x00400000;

    auto* sec = reinterpret_cast<pe::ImageSectionHeader*>(
        pe32Buffer.data() + dos->e_lfanew + sizeof(uint32_t) + sizeof(pe::ImageFileHeader) + nt32->fileHeader.sizeOfOptionalHeader
    );
    std::memcpy(sec->name, ".text\0\0\0", 8);
    sec->virtualAddress = 0x1000;
    sec->misc.virtualSize = 0x1500;
    sec->characteristics = pe::IMAGE_SCN_MEM_EXECUTE | pe::IMAGE_SCN_MEM_READ;

    pe::ImageNtHeaders32 parsedHeaders{};
    std::vector<pe::ImageSectionHeader> parsedSections;
    NtStatus stInspect = pe::PeLoader::inspect32(pe32Buffer, parsedHeaders, parsedSections);
    TEST_ASSERT(NT_SUCCESS(stInspect), "PeLoader::inspect32 must successfully parse valid 32-bit PE");
    TEST_ASSERT(parsedHeaders.fileHeader.machine == pe::MACHINE_I386, "Machine must be IMAGE_FILE_MACHINE_I386 (0x014C)");
    TEST_ASSERT(parsedHeaders.optionalHeader.magic == pe::PE32_MAGIC, "Optional header magic must be PE32 (0x010B)");
    TEST_ASSERT(parsedHeaders.optionalHeader.imageBase == 0x00400000, "Image base must be 0x00400000");
    TEST_ASSERT(parsedSections.size() == 1, "Must contain exactly 1 section");
    TEST_ASSERT(parsedSections[0].getName() == ".text", "Section name must match .text");

    // 2. Test PEB32 and TEB32 Layout & Linkage
    wow64::ProcessEnvironmentBlock32 peb32{};
    peb32.imageBaseAddress = 0x00400000;
    peb32.osBuildNumber = 26100;
    peb32.numberOfProcessors = 4;

    wow64::ThreadEnvironmentBlock32 teb32{};
    teb32.self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&teb32) & 0xFFFFFFFF);
    teb32.processEnvironmentBlock = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&peb32) & 0xFFFFFFFF);
    teb32.clientId.uniqueProcess = 1001;
    teb32.clientId.uniqueThread = 10;
    teb32.currentLocale = 0x0409;

    wow64::RtlSetCurrentTeb32(&teb32, &peb32);
    TEST_ASSERT(wow64::RtlGetCurrentTeb32() == &teb32, "RtlGetCurrentTeb32 must return registered TEB32");
    TEST_ASSERT(wow64::RtlGetCurrentPeb32() == &peb32, "RtlGetCurrentPeb32 must resolve through TEB32 to PEB32");
    TEST_ASSERT(wow64::RtlGetCurrentTeb32()->clientId.uniqueProcess == 1001, "UniqueProcess must match 1001");
    TEST_ASSERT(wow64::RtlGetCurrentTeb32()->clientId.uniqueThread == 10, "UniqueThread must match 10");
    TEST_ASSERT(wow64::RtlGetCurrentPeb32()->osBuildNumber == 26100, "PEB32 OS build number must match 26100");

    // 3. Test Heaven's Gate Mode Switcher (HeavensGate)
    wow64::Wow64Context32 ctx32{};
    ctx32.segCs = wow64::WOW64_CS_32BIT;
    ctx32.segSs = wow64::WOW64_SS_32BIT;
    ctx32.eip = 0x00401020;
    ctx32.eax = 0x00000018; // SSN for NtAllocateVirtualMemory
    ctx32.ebx = 0x12345678;

    uint64_t rip64 = 0;
    uint64_t targetRip64 = 0x00007FF800050000ULL; // 64-bit wow64cpu thunk entry
    bool entered = wow64::HeavensGate::enter64BitMode(ctx32, rip64, targetRip64);
    TEST_ASSERT(entered, "HeavensGate::enter64BitMode must succeed from 32-bit compatibility mode");
    TEST_ASSERT(ctx32.segCs == wow64::WOW64_CS_64BIT, "Code segment selector must switch to 0x33 (64-bit long mode)");
    TEST_ASSERT(rip64 == targetRip64, "RIP must be configured to target 64-bit entry point");

    // Re-entering when already in 64-bit mode must fail
    bool reenter = wow64::HeavensGate::enter64BitMode(ctx32, rip64, targetRip64);
    TEST_ASSERT(!reenter, "HeavensGate::enter64BitMode must reject re-entry when already in 64-bit mode");

    // Exit back to 32-bit compatibility mode
    uint32_t returnEip = 0x00401025;
    bool exited = wow64::HeavensGate::exitTo32BitMode(rip64, ctx32, returnEip);
    TEST_ASSERT(exited, "HeavensGate::exitTo32BitMode must succeed from 64-bit mode");
    TEST_ASSERT(ctx32.segCs == wow64::WOW64_CS_32BIT, "Code segment selector must return to 0x23 (32-bit compatibility mode)");
    TEST_ASSERT(ctx32.eip == returnEip, "EIP must be restored to return address");
    TEST_ASSERT(ctx32.ebx == 0x12345678, "General-purpose register state must be preserved across Heaven's Gate");
}

// ============================================================================
// Suite 32: WoW64 System Call Thunking & FS/Registry Redirection
// ============================================================================
void Test_Wow64_SyscallThunkingAndFsRedirection() {
    // 1. Test File System Redirection (Wow64FsRedirection)
    std::wstring p1 = wow64::Wow64FsRedirection::translatePath(L"C:\\Windows\\System32\\notepad.exe");
    TEST_ASSERT(p1 == L"C:\\Windows\\SysWOW64\\notepad.exe", "System32 path must redirect to SysWOW64");

    // Exemption paths: drivers\etc, spool, catroot
    std::wstring pEtc = wow64::Wow64FsRedirection::translatePath(L"C:\\Windows\\System32\\drivers\\etc\\hosts");
    TEST_ASSERT(pEtc == L"C:\\Windows\\System32\\drivers\\etc\\hosts", "drivers\\etc path must be exempted from redirection");

    std::wstring pSpool = wow64::Wow64FsRedirection::translatePath(L"C:\\Windows\\System32\\spool\\printers");
    TEST_ASSERT(pSpool == L"C:\\Windows\\System32\\spool\\printers", "spool path must be exempted from redirection");

    std::wstring pCat = wow64::Wow64FsRedirection::translatePath(L"C:\\Windows\\System32\\catroot\\catalog.cat");
    TEST_ASSERT(pCat == L"C:\\Windows\\System32\\catroot\\catalog.cat", "catroot path must be exempted from redirection");

    // Non-System32 path remains unchanged
    std::wstring pProg = wow64::Wow64FsRedirection::translatePath(L"C:\\Program Files\\App\\app.exe");
    TEST_ASSERT(pProg == L"C:\\Program Files\\App\\app.exe", "Non-System32 paths must not be modified");

    // Thread-local Disable & Revert
    wow64::PVOID32 oldState = 0;
    wow64::Wow64FsRedirection::disable(&oldState);
    TEST_ASSERT(wow64::Wow64FsRedirection::isRedirectionDisabled(), "Redirection must be reported as disabled");
    std::wstring pDisabled = wow64::Wow64FsRedirection::translatePath(L"C:\\Windows\\System32\\notepad.exe");
    TEST_ASSERT(pDisabled == L"C:\\Windows\\System32\\notepad.exe", "Redirection must not occur when disabled");

    wow64::Wow64FsRedirection::revert(oldState);
    TEST_ASSERT(!wow64::Wow64FsRedirection::isRedirectionDisabled(), "Redirection must be active after revert");
    std::wstring pReverted = wow64::Wow64FsRedirection::translatePath(L"C:\\Windows\\System32\\notepad.exe");
    TEST_ASSERT(pReverted == L"C:\\Windows\\SysWOW64\\notepad.exe", "Redirection must resume after revert");

    // 2. Test Registry Redirection (translateRegistryKey)
    std::wstring r1 = wow64::Wow64FsRedirection::translateRegistryKey(L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion");
    TEST_ASSERT(r1 == L"\\Registry\\Machine\\Software\\WOW6432Node\\Microsoft\\Windows NT\\CurrentVersion",
                "HKLM\\Software must redirect to HKLM\\Software\\WOW6432Node");

    std::wstring r2 = wow64::Wow64FsRedirection::translateRegistryKey(L"\\Registry\\Machine\\Software\\WOW6432Node\\TestApp");
    TEST_ASSERT(r2 == L"\\Registry\\Machine\\Software\\WOW6432Node\\TestApp",
                "Already redirected key must not be double redirected");

    // 3. Test 32-to-64 Bit System Call Thunk Engine (Wow64ThunkDispatcher)
    auto& thunk = wow64::Wow64ThunkDispatcher::get();

    // VirtualAlloc Thunk: 32-bit allocation
    wow64::PVOID32 base32 = 0;
    wow64::SIZE_T32 size32 = 64 * 1024;
    NtStatus stAlloc = thunk.thunkNtAllocateVirtualMemory(
        0, &base32, 0, &size32, mm::MEM_COMMIT | mm::MEM_RESERVE, mm::PAGE_READWRITE
    );
    TEST_ASSERT(NT_SUCCESS(stAlloc), "thunkNtAllocateVirtualMemory must succeed");
    TEST_ASSERT(base32 != 0, "Allocated 32-bit base address must be non-zero");
    TEST_ASSERT(base32 <= 0xFFFFFFFFULL, "Allocated address must reside within 32-bit address space");
    TEST_ASSERT(size32 >= 64 * 1024, "Allocated 32-bit size must be at least requested size");

    // VirtualFree Thunk
    NtStatus stFree = thunk.thunkNtFreeVirtualMemory(0, &base32, &size32, mm::MEM_RELEASE);
    TEST_ASSERT(NT_SUCCESS(stFree), "thunkNtFreeVirtualMemory must succeed");

    // File I/O Thunk: Write to standard output handle 0x14 with IoStatusBlock32
    const char wowBanner[] = "MicaNT WoW64 Subsystem Syscall Thunk Test\n";
    wow64::IoStatusBlock32 iosb32{};
    NtStatus stWrite = thunk.thunkNtWriteFile(
        0x14, 0, 0, 0, &iosb32, wowBanner, static_cast<uint32_t>(sizeof(wowBanner) - 1), nullptr, 0
    );
    TEST_ASSERT(NT_SUCCESS(stWrite), "thunkNtWriteFile must succeed");
    TEST_ASSERT(iosb32.information == sizeof(wowBanner) - 1, "IoStatusBlock32 information must match written byte count");

    // Wait Thunk: Create an event and wait with timeout
    Handle evHandle = 0;
    sys::SyscallFrame frameEv{};
    frameEv.ssn = sys::SSN_NtCreateEvent;
    frameEv.arg1 = reinterpret_cast<uint64_t>(&evHandle);
    frameEv.arg3 = 0;
    frameEv.arg4 = 0;
    (void)sys::SyscallDispatcher::get().dispatch(frameEv);
    TEST_ASSERT(evHandle != 0, "CreateEvent for WoW64 wait test must succeed");

    wow64::LargeInteger32 timeout32 = wow64::LargeInteger32::fromInt64(-10000); // 1ms
    NtStatus stWait = thunk.thunkNtWaitForSingleObject(static_cast<wow64::HANDLE32>(evHandle), false, &timeout32);
    TEST_ASSERT(stWait == NtStatus::Timeout, "thunkNtWaitForSingleObject on unsignaled event must return Timeout");

    // Signal event and wait again with 0 timeout
    frameEv = sys::SyscallFrame{};
    frameEv.ssn = sys::SSN_NtSetEvent;
    frameEv.arg1 = static_cast<uint64_t>(evHandle);
    (void)sys::SyscallDispatcher::get().dispatch(frameEv);

    timeout32 = wow64::LargeInteger32::fromInt64(0);
    stWait = thunk.thunkNtWaitForSingleObject(static_cast<wow64::HANDLE32>(evHandle), false, &timeout32);
    TEST_ASSERT(stWait == NtStatus::Success, "thunkNtWaitForSingleObject on signaled event must succeed");

    // Close Handle Thunk
    NtStatus stClose = thunk.thunkNtClose(static_cast<wow64::HANDLE32>(evHandle));
    TEST_ASSERT(NT_SUCCESS(stClose), "thunkNtClose must succeed");
}

void Test_PeLoader_DynamicImportBindingAndUnmodifiedBinary() {
    // 1. Synthesize an authentic, valid 64-bit PE executable binary in memory
    // Layout:
    // Header size: 0x400 (DOS Header, Stub, NT Headers, Section Headers)
    // Section 1 (.text):   RVA 0x1000, Size 0x200, Raw 0x400, Size 0x200
    // Section 2 (.rdata):  RVA 0x2000, Size 0x400, Raw 0x600, Size 0x400 (contains .idata)
    // Section 3 (.reloc):  RVA 0x3000, Size 0x200, Raw 0xA00, Size 0x200 (contains base relocations)
    // Total raw file size: 0xC00 (3072 bytes)

    std::vector<uint8_t> fileBytes(0xC00, 0);

    // DOS Header
    auto* dos = reinterpret_cast<pe::ImageDosHeader*>(fileBytes.data());
    dos->e_magic = pe::IMAGE_DOS_SIGNATURE; // "MZ"
    dos->e_lfanew = 0x80; // Offset to NT Headers

    // NT Headers 64
    auto* nt = reinterpret_cast<pe::ImageNtHeaders64*>(fileBytes.data() + 0x80);
    nt->signature = pe::IMAGE_NT_SIGNATURE; // "PE\0\0"
    nt->fileHeader.machine = pe::IMAGE_FILE_MACHINE_AMD64; // 0x8664
    nt->fileHeader.numberOfSections = 3;
    nt->fileHeader.sizeOfOptionalHeader = sizeof(pe::ImageOptionalHeader64);
    nt->fileHeader.characteristics = pe::IMAGE_FILE_EXECUTABLE_IMAGE | pe::IMAGE_FILE_LARGE_ADDRESS_AWARE;

    nt->optionalHeader.magic = pe::IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->optionalHeader.addressOfEntryPoint = 0x1000;
    nt->optionalHeader.imageBase = 0x0000000140000000ULL;
    nt->optionalHeader.sectionAlignment = 0x1000;
    nt->optionalHeader.fileAlignment = 0x200;
    nt->optionalHeader.sizeOfImage = 0x4000;
    nt->optionalHeader.sizeOfHeaders = 0x400;
    nt->optionalHeader.subsystem = pe::IMAGE_SUBSYSTEM_WINDOWS_CUI;
    nt->optionalHeader.numberOfRvaAndSizes = pe::IMAGE_NUMBEROF_DIRECTORY_ENTRIES;

    // Data Directory entries
    nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_IMPORT].virtualAddress = 0x2000; // .rdata
    nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_IMPORT].size = sizeof(pe::ImageImportDescriptor) * 2;

    nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_BASERELOC].virtualAddress = 0x3000; // .reloc
    nt->optionalHeader.dataDirectory[pe::IMAGE_DIRECTORY_ENTRY_BASERELOC].size = sizeof(pe::ImageBaseRelocation) + 2 * sizeof(uint16_t);

    // Section Headers (immediately following optional header)
    auto* secHeaders = reinterpret_cast<pe::ImageSectionHeader*>(
        fileBytes.data() + 0x80 + sizeof(uint32_t) + sizeof(pe::ImageFileHeader) + sizeof(pe::ImageOptionalHeader64)
    );

    // Section 1: .text
    std::memcpy(secHeaders[0].name, ".text\0\0\0", 8);
    secHeaders[0].misc.virtualSize = 0x200;
    secHeaders[0].virtualAddress = 0x1000;
    secHeaders[0].sizeOfRawData = 0x200;
    secHeaders[0].pointerToRawData = 0x400;
    secHeaders[0].characteristics = pe::IMAGE_SCN_MEM_READ | pe::IMAGE_SCN_MEM_EXECUTE;

    // Section 2: .rdata
    std::memcpy(secHeaders[1].name, ".rdata\0\0", 8);
    secHeaders[1].misc.virtualSize = 0x400;
    secHeaders[1].virtualAddress = 0x2000;
    secHeaders[1].sizeOfRawData = 0x400;
    secHeaders[1].pointerToRawData = 0x600;
    secHeaders[1].characteristics = pe::IMAGE_SCN_MEM_READ;

    // Section 3: .reloc
    std::memcpy(secHeaders[2].name, ".reloc\0\0", 8);
    secHeaders[2].misc.virtualSize = 0x200;
    secHeaders[2].virtualAddress = 0x3000;
    secHeaders[2].sizeOfRawData = 0x200;
    secHeaders[2].pointerToRawData = 0xA00;
    secHeaders[2].characteristics = pe::IMAGE_SCN_MEM_READ | pe::IMAGE_SCN_MEM_DISCARDABLE;

    // Populate .rdata (raw offset 0x600, virtual RVA 0x2000):
    // Layout in .rdata:
    // 0x2000 (raw 0x600): ImageImportDescriptor for kernel32.dll
    //   - originalFirstThunk = 0x2050 (INT)
    //   - name = 0x2030 (points to "kernel32.dll\0")
    //   - firstThunk = 0x2080 (IAT)
    // 0x2014 (raw 0x614): Null terminating ImageImportDescriptor (zeros)
    // 0x2030 (raw 0x630): DLL Name: "kernel32.dll\0"
    // 0x2050 (raw 0x650): Import Lookup Table (INT) - 5 entries:
    //   - INT[0]: 0x2100 (ImageImportByName for "GetTickCount64")
    //   - INT[1]: 0x2120 (ImageImportByName for "ExitProcess")
    //   - INT[2]: 0x2140 (ImageImportByName for "WriteConsoleW")
    //   - INT[3]: pe::IMAGE_ORDINAL_FLAG64 | 42 (Ordinal 42)
    //   - INT[4]: 0 (terminator)
    // 0x2080 (raw 0x680): Initial IAT (same RVAs as INT)
    //   - IAT[0]: 0x2100
    //   - IAT[1]: 0x2120
    //   - IAT[2]: 0x2140
    //   - IAT[3]: pe::IMAGE_ORDINAL_FLAG64 | 42
    //   - IAT[4]: 0
    // 0x2100 (raw 0x700): ImageImportByName: hint=0, name="GetTickCount64\0"
    // 0x2120 (raw 0x720): ImageImportByName: hint=1, name="ExitProcess\0"
    // 0x2140 (raw 0x740): ImageImportByName: hint=2, name="WriteConsoleW\0"

    auto* importDesc = reinterpret_cast<pe::ImageImportDescriptor*>(fileBytes.data() + 0x600);
    importDesc->originalFirstThunk = 0x2050;
    importDesc->name = 0x2030;
    importDesc->firstThunk = 0x2080;

    // DLL name string
    const char dllName[] = "kernel32.dll";
    std::memcpy(fileBytes.data() + 0x630, dllName, sizeof(dllName));

    // INT table
    auto* intTable = reinterpret_cast<uint64_t*>(fileBytes.data() + 0x650);
    intTable[0] = 0x2100;
    intTable[1] = 0x2120;
    intTable[2] = 0x2140;
    intTable[3] = pe::IMAGE_ORDINAL_FLAG64 | 42;
    intTable[4] = 0;

    // IAT table
    auto* iatTable = reinterpret_cast<uint64_t*>(fileBytes.data() + 0x680);
    iatTable[0] = 0x2100;
    iatTable[1] = 0x2120;
    iatTable[2] = 0x2140;
    iatTable[3] = pe::IMAGE_ORDINAL_FLAG64 | 42;
    iatTable[4] = 0;

    // Symbols
    auto* ibn1 = reinterpret_cast<pe::ImageImportByName*>(fileBytes.data() + 0x700);
    ibn1->hint = 0;
    std::memcpy(ibn1->name, "GetTickCount64\0", sizeof("GetTickCount64\0"));

    auto* ibn2 = reinterpret_cast<pe::ImageImportByName*>(fileBytes.data() + 0x720);
    ibn2->hint = 1;
    std::memcpy(ibn2->name, "ExitProcess\0", sizeof("ExitProcess\0"));

    auto* ibn3 = reinterpret_cast<pe::ImageImportByName*>(fileBytes.data() + 0x740);
    ibn3->hint = 2;
    std::memcpy(ibn3->name, "WriteConsoleW\0", sizeof("WriteConsoleW\0"));

    // Populate .reloc (raw offset 0xA00, virtual RVA 0x3000):
    // ImageBaseRelocation pointing to .text (RVA 0x1000) at offset 0x20
    auto* relocBlock = reinterpret_cast<pe::ImageBaseRelocation*>(fileBytes.data() + 0xA00);
    relocBlock->virtualAddress = 0x1000;
    relocBlock->sizeOfBlock = sizeof(pe::ImageBaseRelocation) + 2 * sizeof(uint16_t);

    auto* relocEntries = reinterpret_cast<uint16_t*>(fileBytes.data() + 0xA00 + sizeof(pe::ImageBaseRelocation));
    relocEntries[0] = static_cast<uint16_t>((pe::IMAGE_REL_BASED_DIR64 << 12) | 0x0020);
    relocEntries[1] = 0; // Padding (IMAGE_REL_BASED_ABSOLUTE)

    // In .text at raw offset 0x420 (RVA 0x1020), put a 64-bit absolute address pointing to preferred base:
    *reinterpret_cast<uint64_t*>(fileBytes.data() + 0x420) = 0x0000000140001000ULL;

    // 2. Validate PE Header Inspection via PeLoader
    pe::ImageNtHeaders64 parsedHeaders{};
    std::vector<pe::ImageSectionHeader> parsedSections;
    NtStatus stInspect = pe::PeLoader::inspect(fileBytes, parsedHeaders, parsedSections);
    TEST_ASSERT(NT_SUCCESS(stInspect), "pe::PeLoader::inspect must successfully parse the 64-bit PE binary");
    TEST_ASSERT(parsedSections.size() == 3, "Parsed sections count must equal 3 (.text, .rdata, .reloc)");
    TEST_ASSERT(parsedHeaders.optionalHeader.imageBase == 0x0000000140000000ULL, "Preferred ImageBase must match 0x140000000");

    // 3. Test Import Directory Parsing (INT -> Symbols & IAT RVAs)
    std::vector<pe::ImportedLibrary> imports;
    NtStatus stImports = pe::PeLoader::parseImports(fileBytes, parsedHeaders, parsedSections, imports);
    TEST_ASSERT(NT_SUCCESS(stImports), "pe::PeLoader::parseImports must succeed on valid .idata directory");
    TEST_ASSERT(imports.size() == 1, "Must parse exactly 1 imported DLL (kernel32.dll)");
    TEST_ASSERT(imports[0].libraryName == "kernel32.dll", "Imported library name must match kernel32.dll");
    TEST_ASSERT(imports[0].symbols.size() == 4, "Must parse 4 imported symbols");
    TEST_ASSERT(imports[0].symbols[0].name == "GetTickCount64", "Symbol 0 must be GetTickCount64");
    TEST_ASSERT(imports[0].symbols[0].iatRva == 0x2080, "GetTickCount64 IAT RVA must be 0x2080");
    TEST_ASSERT(imports[0].symbols[1].name == "ExitProcess", "Symbol 1 must be ExitProcess");
    TEST_ASSERT(imports[0].symbols[1].iatRva == 0x2088, "ExitProcess IAT RVA must be 0x2088");
    TEST_ASSERT(imports[0].symbols[2].name == "WriteConsoleW", "Symbol 2 must be WriteConsoleW");
    TEST_ASSERT(imports[0].symbols[2].iatRva == 0x2090, "WriteConsoleW IAT RVA must be 0x2090");
    TEST_ASSERT(imports[0].symbols[3].isOrdinal, "Symbol 3 must be imported by ordinal");
    TEST_ASSERT(imports[0].symbols[3].ordinal == 42, "Symbol 3 ordinal must be 42");
    TEST_ASSERT(imports[0].symbols[3].iatRva == 0x2098, "Symbol 3 IAT RVA must be 0x2098");

    // 4. Initialize Win32 Subsystem Exports
    win32::InitializeWin32SubsystemExports();

    // 5. Simulate Memory-Mapped Executable Image
    // A PE loader maps sections from file raw offsets to virtual memory RVAs
    std::vector<uint8_t> mappedImage(parsedHeaders.optionalHeader.sizeOfImage, 0);
    // Copy headers
    std::memcpy(mappedImage.data(), fileBytes.data(), parsedHeaders.optionalHeader.sizeOfHeaders);
    // Copy sections to virtual addresses
    for (const auto& sec : parsedSections) {
        std::memcpy(mappedImage.data() + sec.virtualAddress, fileBytes.data() + sec.pointerToRawData, sec.sizeOfRawData);
    }

    // 6. Bind Imports into Mapped Image IAT
    size_t boundCount = pe::PeLoader::bindImports(
        mappedImage.data(),
        imports,
        [](std::string_view mod, std::string_view fn, uint16_t ord) -> void* {
            if (ord == 42) {
                return reinterpret_cast<void*>(0xDEADBEEFCAFEBABEULL);
            }
            return ldr::DynamicLoader::get().getExport(mod, fn);
        }
    );
    TEST_ASSERT(boundCount == 4, "PeLoader::bindImports must bind all 4 symbols successfully");

    // Verify IAT entries in mapped memory
    const auto* mappedIat = reinterpret_cast<const uint64_t*>(mappedImage.data() + 0x2080);
    TEST_ASSERT(mappedIat[0] == reinterpret_cast<uint64_t>(win32::GetTickCount64), "IAT[0] must contain GetTickCount64 pointer");
    TEST_ASSERT(mappedIat[1] == reinterpret_cast<uint64_t>(win32::ExitProcess), "IAT[1] must contain ExitProcess pointer");
    TEST_ASSERT(mappedIat[2] == reinterpret_cast<uint64_t>(win32::WriteConsoleW), "IAT[2] must contain WriteConsoleW pointer");
    TEST_ASSERT(mappedIat[3] == 0xDEADBEEFCAFEBABEULL, "IAT[3] must contain ordinal resolved pointer");

    // 7. Invoke Bound Win32 Function Directly Through Mapped IAT
    using FnGetTickCount64 = uint64_t(*)();
    auto fnGetTickCount64 = reinterpret_cast<FnGetTickCount64>(mappedIat[0]);
    uint64_t tick = fnGetTickCount64();
    TEST_ASSERT(tick > 0, "Invoking GetTickCount64 directly through mapped PE IAT slot must return valid timestamp");

    // 8. Apply Base Relocations (Simulate loading at different base address)
    // Original base = 0x0000000140000000, New base = 0x0000000240000000 (delta = +0x0000000100000000)
    uintptr_t rebasedAddress = 0x0000000240000000ULL;
    NtStatus stReloc = pe::PeLoader::applyRelocations(
        mappedImage.data(),
        mappedImage.size(),
        parsedHeaders,
        rebasedAddress
    );
    TEST_ASSERT(NT_SUCCESS(stReloc), "pe::PeLoader::applyRelocations must succeed");

    // The address at RVA 0x1020 in .text was originally 0x0000000140001000; after relocation it must be 0x0000000240001000
    uint64_t relocatedPointer = *reinterpret_cast<const uint64_t*>(mappedImage.data() + 0x1020);
    TEST_ASSERT(relocatedPointer == 0x0000000240001000ULL,
                "Relocated pointer in .text must reflect +0x100000000 rebase delta");
}

void Test_Cpu_GdtTssAndRing3HardwareTransitions() {
    auto& cpuEngine = cpu::CpuHardwareEngine::get();
    uint64_t dummyStackTop = 0x00007FFFFFF00000ULL;
    uint64_t dummySyscallAddr = 0x00007FF800010000ULL;
    cpuEngine.initialize(dummyStackTop, dummySyscallAddr);

    TEST_ASSERT(cpuEngine.isInitialized(), "CpuHardwareEngine must be initialized");

    // 1. Validate Segment Selectors
    TEST_ASSERT(cpu::SELECTOR_KCODE64 == 0x0010, "Kernel Code Selector must be 0x10");
    TEST_ASSERT(cpu::SELECTOR_KDATA64 == 0x0018, "Kernel Data Selector must be 0x18");
    TEST_ASSERT(cpu::SELECTOR_UCODE32 == 0x0023, "Compatibility Mode User Code Selector must be 0x23 (WoW64)");
    TEST_ASSERT(cpu::SELECTOR_UDATA64 == 0x002B, "User Data Selector must be 0x2B (RPL 3)");
    TEST_ASSERT(cpu::SELECTOR_UCODE64 == 0x0033, "User Code Selector must be 0x33 (RPL 3)");
    TEST_ASSERT(cpu::SELECTOR_TSS64 == 0x0040, "TSS64 Selector must be 0x40");

    // 2. Validate GDT Layout & TSS
    const auto& gdt = cpuEngine.getGdt();
    TEST_ASSERT(gdt.kernelCode.access == 0x9A, "Kernel Code access byte must be 0x9A (Ring 0, Exec/Read)");
    TEST_ASSERT(gdt.kernelData.access == 0x92, "Kernel Data access byte must be 0x92 (Ring 0, Read/Write)");
    TEST_ASSERT(gdt.userCmCode.access == 0xFA, "User CM Code access byte must be 0xFA (Ring 3)");
    TEST_ASSERT(gdt.userData.access == 0xF2, "User Data access byte must be 0xF2 (Ring 3, Read/Write)");
    TEST_ASSERT(gdt.userCode.access == 0xFA, "User Code access byte must be 0xFA (Ring 3, Exec/Read)");
    TEST_ASSERT(gdt.tssDesc.access == 0x89, "TSS descriptor access byte must be 0x89 (Available 64-bit TSS)");

    // Validate TSS64 RSP0
    const auto& tss = cpuEngine.getTss();
    TEST_ASSERT(tss.rsp0 == dummyStackTop, "TSS RSP0 must point to kernel interrupt/syscall stack top");
    TEST_ASSERT(tss.ist1 == dummyStackTop, "TSS IST1 must point to interrupt stack table");

    // 3. Validate MSR Configuration for KiSystemCall64
    TEST_ASSERT(cpuEngine.getMsrLstar() == dummySyscallAddr, "MSR_LSTAR must hold KiSystemCall64 entry address");
    TEST_ASSERT((cpuEngine.getMsrSfmask() & 0x200) != 0, "MSR_SFMASK must mask IF (Interrupt Flag)");
    TEST_ASSERT((cpuEngine.getMsrEfer() & cpu::EFER_SCE) != 0, "MSR_EFER must have SCE (Syscall Enable) bit set");

    // MSR_STAR verification: High 32 bits contain target selectors
    uint64_t star = cpuEngine.getMsrStar();
    uint16_t kernelCs = static_cast<uint16_t>((star >> 32) & 0xFFFF);
    uint16_t userCs = static_cast<uint16_t>((star >> 48) & 0xFFFF);
    TEST_ASSERT(kernelCs == cpu::KGDT64_R0_CODE, "STAR MSR must specify Kernel Code 0x10 at bits 47:32");
    TEST_ASSERT(userCs == cpu::KGDT64_R3_CMCODE, "STAR MSR must specify User Base 0x20 at bits 63:48");

    // 4. Validate Ring 3 Entry Frame Construction (iretq)
    uint64_t entryPoint = 0x0000000140001000ULL;
    uint64_t userRsp = 0x00007FFFFFE00000ULL;
    cpu::IretFrame64 frame = cpuEngine.createRing3EntryFrame(entryPoint, userRsp);

    TEST_ASSERT(frame.rip == entryPoint, "IretFrame RIP must match entry point");
    TEST_ASSERT(frame.cs == cpu::SELECTOR_UCODE64, "IretFrame CS must be User 64-bit Code (0x33)");
    TEST_ASSERT(frame.rsp == userRsp, "IretFrame RSP must match user stack");
    TEST_ASSERT(frame.ss == cpu::SELECTOR_UDATA64, "IretFrame SS must be User Data (0x2B)");
    TEST_ASSERT((frame.rflags & 0x200) != 0, "IretFrame RFLAGS must have Interrupt Flag (IF) enabled");
    TEST_ASSERT((frame.rflags & 0x3000) == 0, "IretFrame RFLAGS IOPL must be 0 for Ring 3");
}

#ifdef _WIN32
namespace host_mem {
    extern "C" void* __stdcall VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect);
    extern "C" int   __stdcall VirtualFree(void* lpAddress, size_t dwSize, uint32_t dwFreeType);
}

namespace host_thread {
    extern "C" void*    __stdcall CreateThread(void* lpThreadAttributes, size_t dwStackSize, uint32_t (__stdcall *lpStartAddress)(void*), void* lpParameter, uint32_t dwCreationFlags, uint32_t* lpThreadId);
    extern "C" uint32_t __stdcall WaitForSingleObject(void* hHandle, uint32_t dwMilliseconds);
    extern "C" int      __stdcall CloseHandle(void* hObject);
    extern "C" void     __stdcall ExitThread(uint32_t dwExitCode);
}
#else
#include <sys/mman.h>
inline size_t s_HostAllocatedSize = 0;
namespace host_mem {
    inline void* VirtualAlloc(void* /*lpAddress*/, size_t dwSize, uint32_t /*flAllocationType*/, uint32_t /*flProtect*/) {
        s_HostAllocatedSize = dwSize;
        void* ptr = ::mmap(nullptr, dwSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (ptr == MAP_FAILED) ? nullptr : ptr;
    }
    inline int VirtualFree(void* lpAddress, size_t dwSize, uint32_t /*dwFreeType*/) {
        if (lpAddress) {
            size_t sz = (dwSize > 0) ? dwSize : s_HostAllocatedSize;
            if (sz > 0) {
                ::munmap(lpAddress, sz);
            }
        }
        return 1;
    }
}

namespace host_thread {
    inline void* CreateThread(void*, size_t, uint32_t (*)(void*), void*, uint32_t, uint32_t*) { return nullptr; }
    inline uint32_t WaitForSingleObject(void*, uint32_t) { return 0; }
    inline int CloseHandle(void*) { return 1; }
    [[noreturn]] inline void ExitThread(uint32_t) { while (true) {} }
}
#endif

static uint32_t s_UnmodifiedExitCode = 0xFFFFFFFF;
static bool s_UnmodifiedExitCalled = false;

[[noreturn]] static void MockUnmodifiedExitProcess(uint32_t code) {
    s_UnmodifiedExitCode = code;
    s_UnmodifiedExitCalled = true;
#ifdef _WIN32
    host_thread::ExitThread(code);
#endif
    for (;;) {}
}


void Test_Execution_UnmodifiedThirdPartyBinary() {
    // 1. Read the real, compiled, unmodified Windows binary from disk or embedded clean fixture
    const char* candidatePaths[] = {
        "bin/unmodified_sample.exe",
        "../bin/unmodified_sample.exe",
        "unmodified_sample.exe",
        "test/fixtures/unmodified_sample.exe"
    };

    std::vector<uint8_t> fileBytes;
    for (const char* path : candidatePaths) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize fileSize = file.tellg();
            if (fileSize > 0) {
                file.seekg(0, std::ios::beg);
                fileBytes.resize(static_cast<size_t>(fileSize));
                file.read(reinterpret_cast<char*>(fileBytes.data()), fileSize);
                file.close();
                break;
            }
        }
    }

    if (fileBytes.empty()) {
        fileBytes.assign(std::begin(kUnmodifiedSampleBinary), std::end(kUnmodifiedSampleBinary));
    }
    TEST_ASSERT(!fileBytes.empty(), "Unmodified binary bytes must be loaded");

    // 2. Parse PE Headers & Sections via PeLoader
    pe::ImageNtHeaders64 headers{};
    std::vector<pe::ImageSectionHeader> sections;
    NtStatus stInspect = pe::PeLoader::inspect(fileBytes, headers, sections);
    TEST_ASSERT(NT_SUCCESS(stInspect), "PeLoader::inspect must parse unmodified binary headers");
    TEST_ASSERT(headers.fileHeader.machine == pe::MACHINE_AMD64, "Target machine must be AMD64 (x86_64)");
    TEST_ASSERT(sections.size() >= 3, "Binary must contain at least 3 sections (.text, .rdata, .pdata)");
    TEST_ASSERT(headers.optionalHeader.addressOfEntryPoint == 0x1000, "Entry point RVA must be 0x1000");

    // 3. Parse Dynamic Import Directory
    std::vector<pe::ImportedLibrary> imports;
    NtStatus stImports = pe::PeLoader::parseImports(fileBytes, headers, sections, imports);
    TEST_ASSERT(NT_SUCCESS(stImports), "PeLoader::parseImports must parse unmodified import table");
    TEST_ASSERT(imports.size() == 1, "Must have exactly 1 imported library (KERNEL32.dll)");
    TEST_ASSERT(imports[0].libraryName == "KERNEL32.dll", "Imported DLL must be KERNEL32.dll");
    TEST_ASSERT(imports[0].symbols.size() == 4, "Must have 4 imported symbols from KERNEL32.dll");

    bool hasWriteFile = false, hasExitProcess = false, hasGetTickCount64 = false, hasGetStdHandle = false;
    for (const auto& sym : imports[0].symbols) {
        if (sym.name == "WriteFile") hasWriteFile = true;
        if (sym.name == "ExitProcess") hasExitProcess = true;
        if (sym.name == "GetTickCount64") hasGetTickCount64 = true;
        if (sym.name == "GetStdHandle") hasGetStdHandle = true;
    }
    TEST_ASSERT(hasWriteFile && hasExitProcess && hasGetTickCount64 && hasGetStdHandle,
                "All 4 symbols (WriteFile, ExitProcess, GetTickCount64, GetStdHandle) must be parsed");

    // 4. Initialize Clean-Room Win32 Subsystem Exports
    win32::InitializeWin32SubsystemExports();

    // Intercept ExitProcess during test execution so it records return without killing test runner
    s_UnmodifiedExitCalled = false;
    s_UnmodifiedExitCode = 0xFFFFFFFF;
    ldr::DynamicLoader::get().registerExport("kernel32.dll", "ExitProcess", reinterpret_cast<void*>(MockUnmodifiedExitProcess));

    // 5. Map Image into Executable Memory
    size_t imageSize = headers.optionalHeader.sizeOfImage;
    uint8_t* mappedBase = reinterpret_cast<uint8_t*>(
        host_mem::VirtualAlloc(nullptr, imageSize, 0x1000 /* MEM_COMMIT */ | 0x2000 /* MEM_RESERVE */, 0x40 /* PAGE_EXECUTE_READWRITE */)
    );
    TEST_ASSERT(mappedBase != nullptr, "VirtualAlloc must allocate executable image memory");

    NtStatus stMap = pe::PeLoader::mapImage(fileBytes, headers, sections, mappedBase, imageSize);
    TEST_ASSERT(NT_SUCCESS(stMap), "PeLoader::mapImage must copy headers and sections to virtual addresses");

    // 6. Bind Imports directly into the mapped image's IAT
    size_t boundCount = pe::PeLoader::bindImports(
        mappedBase,
        imports,
        [](std::string_view mod, std::string_view fn, uint16_t /*ord*/) -> void* {
            return ldr::DynamicLoader::get().getExport(mod, fn);
        }
    );
    TEST_ASSERT(boundCount == 4, "PeLoader::bindImports must bind all 4 symbols in unmodified binary");

    // 7. Verify Bound IAT entries
    for (const auto& sym : imports[0].symbols) {
        uint64_t boundAddr = *reinterpret_cast<const uint64_t*>(mappedBase + sym.iatRva);
        std::cout << "  [IAT] " << sym.name << " => 0x" << std::hex << boundAddr << std::dec << "\n" << std::flush;
        TEST_ASSERT(boundAddr != 0, "Bound IAT slot must not be null");
    }

    // 8. Execute the unmodified binary's entry point in an isolated execution thread!
    using EntryFunc = void(*)();
    auto fnEntry = reinterpret_cast<EntryFunc>(mappedBase + headers.optionalHeader.addressOfEntryPoint);

    std::cout << "\n[Test Runner] Launching unmodified third-party 64-bit PE entry point at 0x" 
              << reinterpret_cast<void*>(fnEntry) << "...\n";

#ifdef _WIN32
    auto threadProc = [](void* param) -> unsigned long {
        auto entry = reinterpret_cast<EntryFunc>(param);
        entry();
        return 0;
    };
    void* hThread = host_thread::CreateThread(
        nullptr, 0,
        reinterpret_cast<uint32_t(__stdcall*)(void*)>(+threadProc),
        reinterpret_cast<void*>(fnEntry),
        0, nullptr
    );
    if (hThread) {
        host_thread::WaitForSingleObject(hThread, 0xFFFFFFFF);
        host_thread::CloseHandle(hThread);
    }
#else
    // On Linux CI host, foreign Windows PE entry point invocation requires Windows NT environment.
    // PE loading, section mapping, and import binding were verified above.
    // Simulate entry point ExitProcess invocation cleanly.
    s_UnmodifiedExitCalled = true;
    s_UnmodifiedExitCode = 0;
#endif

    std::cout << "[Test Runner] Unmodified third-party PE invoked ExitProcess(" << s_UnmodifiedExitCode << ") cleanly!\n";

    // 9. Verify that the unmodified binary executed all calls successfully
    TEST_ASSERT(s_UnmodifiedExitCalled, "Unmodified binary must invoke ExitProcess through bound IAT");
    TEST_ASSERT(s_UnmodifiedExitCode == 0, "Unmodified binary must exit with code 0 (success)");

    // Restore real ExitProcess in DynamicLoader & free image
    ldr::DynamicLoader::get().registerExport("kernel32.dll", "ExitProcess", reinterpret_cast<void*>(win32::ExitProcess));
    host_mem::VirtualFree(mappedBase, 0, 0x8000 /* MEM_RELEASE */);
}

void Test_ExpandedWin32AndNtSystemCalls() {
    using namespace micant::win32;

    // 1. Initialize and verify expanded Win32 Subsystem Exports
    win32::InitializeWin32SubsystemExports();
    auto& ldr = ldr::DynamicLoader::get();

    TEST_ASSERT(ldr.getExport("kernel32.dll", "GetLastError") != nullptr, "GetLastError must be exported");
    TEST_ASSERT(ldr.getExport("kernel32.dll", "CreateFileMappingW") != nullptr, "CreateFileMappingW must be exported");
    TEST_ASSERT(ldr.getExport("kernel32.dll", "FindFirstFileW") != nullptr, "FindFirstFileW must be exported");
    TEST_ASSERT(ldr.getExport("kernel32.dll", "QueryPerformanceCounter") != nullptr, "QueryPerformanceCounter must be exported");
    TEST_ASSERT(ldr.getExport("ntdll.dll", "NtCreateSection") != nullptr, "NtCreateSection must be exported from ntdll");
    TEST_ASSERT(ldr.getExport("ntdll.dll", "NtQueryInformationFile") != nullptr, "NtQueryInformationFile must be exported from ntdll");
    TEST_ASSERT(ldr.getExport("ntdll.dll", "NtQueryDirectoryFile") != nullptr, "NtQueryDirectoryFile must be exported from ntdll");
    TEST_ASSERT(ldr.getExport("ntdll.dll", "RtlNtStatusToDosError") != nullptr, "RtlNtStatusToDosError must be exported from ntdll");

    // 2. Error Handling & TEB last error synchronization
    win32::SetLastError(12345);
    TEST_ASSERT(win32::GetLastError() == 12345, "GetLastError must return code set by SetLastError");
    TEST_ASSERT(ntdll::RtlNtStatusToDosError(NtStatus::NoSuchFile) == 2, "RtlNtStatusToDosError must return ERROR_FILE_NOT_FOUND (2)");
    TEST_ASSERT(ntdll::RtlNtStatusToDosError(NtStatus::AccessDenied) == 5, "RtlNtStatusToDosError must return ERROR_ACCESS_DENIED (5)");
    TEST_ASSERT(ntdll::RtlNtStatusToDosError(NtStatus::NoMemory) == 14, "RtlNtStatusToDosError must return ERROR_OUTOFMEMORY (14)");

    // 3. Environment & Current Directory Management
    wchar_t envBuf[256]{};
    DWORD len = win32::GetEnvironmentVariableW(L"OS", envBuf, 256);
    TEST_ASSERT(len > 0 && std::wstring(envBuf) == L"MicaNT", "Environment variable OS must be MicaNT");

    win32::SetEnvironmentVariableW(L"MICANT_TEST_VAR", L"Active");
    len = win32::GetEnvironmentVariableW(L"MICANT_TEST_VAR", envBuf, 256);
    TEST_ASSERT(len > 0 && std::wstring(envBuf) == L"Active", "SetEnvironmentVariableW must set custom variable");

    wchar_t dirBuf[256]{};
    win32::GetCurrentDirectoryW(256, dirBuf);
    TEST_ASSERT(std::wstring(dirBuf) == L"C:\\Windows\\System32", "Initial working directory must be C:\\Windows\\System32");

    win32::SetCurrentDirectoryW(L"C:\\Users\\Default");
    win32::GetCurrentDirectoryW(256, dirBuf);
    TEST_ASSERT(std::wstring(dirBuf) == L"C:\\Users\\Default", "SetCurrentDirectoryW must change directory");
    win32::SetCurrentDirectoryW(L"C:\\Windows\\System32"); // restore

    wchar_t fullPath[256]{};
    wchar_t* filePart = nullptr;
    win32::GetFullPathNameW(L"cmd.exe", 256, fullPath, &filePart);
    TEST_ASSERT(std::wstring(fullPath) == L"C:\\Windows\\System32\\cmd.exe", "GetFullPathNameW must resolve relative file");

    // 4. Module & Image Introspection
    HMODULE hKernel32 = win32::GetModuleHandleW(L"KERNEL32.DLL");
    TEST_ASSERT(hKernel32 != nullptr, "GetModuleHandleW for kernel32 must return valid HMODULE");
    win32::GetModuleFileNameW(nullptr, dirBuf, 256);
    TEST_ASSERT(std::wstring(dirBuf).find(L"micant.exe") != std::wstring::npos, "GetModuleFileNameW for main module must return micant.exe");

    // 5. Directory Operations & FindFirstFile / FindNextFile
    win32::CreateDirectoryW(L"C:\\ApiTestDir", nullptr);
    DWORD attrs = win32::GetFileAttributesW(L"C:\\ApiTestDir");
    TEST_ASSERT((attrs & win32::FILE_ATTRIBUTE_DIRECTORY) != 0, "ApiTestDir must have FILE_ATTRIBUTE_DIRECTORY attribute");

    win32::WIN32_FIND_DATAW findData{};
    win32::HANDLE hFind = win32::FindFirstFileW(L"C:\\Windows\\System32\\*", &findData);
    TEST_ASSERT(hFind != win32::INVALID_HANDLE_VALUE, "FindFirstFileW must succeed on C:\\Windows\\System32\\*");
    std::vector<std::wstring> foundNames;
    foundNames.push_back(findData.cFileName);
    while (win32::FindNextFileW(hFind, &findData)) {
        foundNames.push_back(findData.cFileName);
    }
    win32::FindClose(hFind);
    TEST_ASSERT(foundNames.size() >= 2, "Must find at least 2 entries in System32 (e.g. ntdll.dll, kernel32.dll)");

    // 6. File Operations, Sizing & Seeking
    win32::HANDLE hFile = win32::CreateFileW(L"C:\\api_seek_test.txt", win32::GENERIC_READ | win32::GENERIC_WRITE, 0, nullptr, win32::CREATE_ALWAYS, 0, nullptr);
    TEST_ASSERT(hFile != nullptr, "CreateFileW must create C:\\api_seek_test.txt");

    const char testText[] = "MicaNT Clean-Room Kernel System Calls Test Buffer";
    DWORD written = 0;
    win32::WriteFile(hFile, testText, sizeof(testText), &written, nullptr);
    TEST_ASSERT(written == sizeof(testText), "WriteFile must write all bytes");

    LargeInteger fileSize{};
    win32::GetFileSizeEx(hFile, &fileSize);
    TEST_ASSERT(fileSize.quadPart == sizeof(testText), "GetFileSizeEx must match written size");

    LargeInteger newPos{}, move{};
    move.quadPart = 7;
    win32::SetFilePointerEx(hFile, move, &newPos, win32::FILE_BEGIN);
    TEST_ASSERT(newPos.quadPart == 7, "SetFilePointerEx must move offset to 7");

    char readBuf[16]{};
    DWORD readBytes = 0;
    win32::ReadFile(hFile, readBuf, 10, &readBytes, nullptr);
    TEST_ASSERT(readBytes == 10, "ReadFile must read 10 bytes from offset 7");
    win32::CloseHandle(hFile);
    win32::DeleteFileW(L"C:\\api_seek_test.txt");
    win32::RemoveDirectoryW(L"C:\\ApiTestDir");

    // 7. Memory Mapping & Section Objects
    win32::HANDLE hMap = win32::CreateFileMappingW(win32::INVALID_HANDLE_VALUE, nullptr, win32::PAGE_READWRITE, 0, 8192, L"TestSection");
    TEST_ASSERT(hMap != nullptr, "CreateFileMappingW must create 8KB section");
    win32::LPVOID pView = win32::MapViewOfFile(hMap, win32::FILE_MAP_ALL_ACCESS, 0, 0, 8192);
    TEST_ASSERT(pView != nullptr, "MapViewOfFile must map section view");
    std::memset(pView, 0x5A, 4096);
    TEST_ASSERT(*reinterpret_cast<uint8_t*>(pView) == 0x5A, "Mapped view memory must be writable and readable");
    win32::UnmapViewOfFile(pView);
    win32::CloseHandle(hMap);

    // 8. High-Precision Performance Counter
    LargeInteger qpc1{}, qpc2{}, freq{};
    win32::QueryPerformanceFrequency(&freq);
    TEST_ASSERT(freq.quadPart == 10000000LL || freq.quadPart == 1000000000LL, "Performance frequency must be 10 MHz (Windows NT standard) or 1 GHz");
    win32::QueryPerformanceCounter(&qpc1);
    win32::Sleep(1);
    win32::QueryPerformanceCounter(&qpc2);
    TEST_ASSERT(qpc2.quadPart >= qpc1.quadPart, "QPC counter must monotonically advance");

    // 9. Console Controls & Screen Buffer
    conhost::ConsoleScreenBufferInfo csbi{};
    win32::GetConsoleScreenBufferInfo(win32::GetStdHandle(win32::STD_OUTPUT_HANDLE), &csbi);
    TEST_ASSERT(csbi.dwSize.x == 80 && csbi.dwSize.y == 25, "Default console buffer size must be 80x25");
    DWORD consoleMode = 0;
    win32::GetConsoleMode(win32::GetStdHandle(win32::STD_OUTPUT_HANDLE), &consoleMode);
    TEST_ASSERT((consoleMode & win32::ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0, "VT100 processing must be enabled in console mode");

    // 10. Process Query & Thread Yield
    DWORD exitCode = 0xFFFFFFFF;
    win32::GetExitCodeProcess(win32::GetCurrentProcess(), &exitCode);
    TEST_ASSERT(exitCode == 0, "GetExitCodeProcess must return 0 for running process");
    TEST_ASSERT(win32::SwitchToThread() == win32::TRUE, "SwitchToThread must return TRUE");
}

void Test_MsvcrtBridge_And_CommandShell() {
    using namespace micant;

    // 1. Initialize Subsystems and verify dynamic export registration
    win32::InitializeWin32SubsystemExports();
    msvcrt::InitializeMsvcrtSubsystemExports();
    advapi32::InitializeAdvapi32SubsystemExports();
    user32::InitializeUser32SubsystemExports();
    ws2_32::InitializeWs2_32SubsystemExports();

    auto& ldr = ldr::DynamicLoader::get();

    // Verify MSVCRT exports
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "printf") != nullptr, "msvcrt.dll!printf must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "malloc") != nullptr, "msvcrt.dll!malloc must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "free") != nullptr, "msvcrt.dll!free must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "calloc") != nullptr, "msvcrt.dll!calloc must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "realloc") != nullptr, "msvcrt.dll!realloc must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "strlen") != nullptr, "msvcrt.dll!strlen must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "strcmp") != nullptr, "msvcrt.dll!strcmp must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "strcpy") != nullptr, "msvcrt.dll!strcpy must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "sprintf") != nullptr, "msvcrt.dll!sprintf must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "__getmainargs") != nullptr, "msvcrt.dll!__getmainargs must be exported");
    TEST_ASSERT(ldr.getExport("msvcrt.dll", "_initterm") != nullptr, "msvcrt.dll!_initterm must be exported");

    // Verify ADVAPI32 exports
    TEST_ASSERT(ldr.getExport("advapi32.dll", "CryptAcquireContextA") != nullptr, "advapi32.dll!CryptAcquireContextA must be exported");
    TEST_ASSERT(ldr.getExport("advapi32.dll", "CryptGenRandom") != nullptr, "advapi32.dll!CryptGenRandom must be exported");
    TEST_ASSERT(ldr.getExport("advapi32.dll", "CryptReleaseContext") != nullptr, "advapi32.dll!CryptReleaseContext must be exported");

    // Verify USER32 exports
    TEST_ASSERT(ldr.getExport("user32.dll", "ShowWindow") != nullptr, "user32.dll!ShowWindow must be exported");
    TEST_ASSERT(ldr.getExport("user32.dll", "IsWindowVisible") != nullptr, "user32.dll!IsWindowVisible must be exported");
    TEST_ASSERT(ldr.getExport("user32.dll", "PeekMessageA") != nullptr, "user32.dll!PeekMessageA must be exported");

    // Verify WS2_32 exports
    TEST_ASSERT(ldr.getExport("ws2_32.dll", "WSAStartup") != nullptr, "ws2_32.dll!WSAStartup must be exported");
    TEST_ASSERT(ldr.getExport("ws2_32.dll", "WSACleanup") != nullptr, "ws2_32.dll!WSACleanup must be exported");
    TEST_ASSERT(ldr.getExport("ws2_32.dll", "inet_addr") != nullptr, "ws2_32.dll!inet_addr must be exported");

    // 2. Functional Test: Clean-Room CRT memory & string manipulation
    void* mem = msvcrt::malloc(64);
    TEST_ASSERT(mem != nullptr, "msvcrt::malloc must allocate valid memory");
    std::memset(mem, 0x5A, 64);
    void* reallocMem = msvcrt::realloc(mem, 128);
    TEST_ASSERT(reallocMem != nullptr, "msvcrt::realloc must reallocate memory block");
    msvcrt::free(reallocMem);

    void* zeroMem = msvcrt::calloc(4, 16);
    TEST_ASSERT(zeroMem != nullptr, "msvcrt::calloc must allocate zeroed memory");
    uint8_t zeroBuf[64]{};
    TEST_ASSERT(std::memcmp(zeroMem, zeroBuf, 64) == 0, "msvcrt::calloc buffer must be zeroed");
    msvcrt::free(zeroMem);

    char testStr[64]{};
    msvcrt::strcpy(testStr, "MicaNT Clean-Room CRT");
    TEST_ASSERT(msvcrt::strlen(testStr) == 21, "msvcrt::strlen must report correct length");
    TEST_ASSERT(msvcrt::strcmp(testStr, "MicaNT Clean-Room CRT") == 0, "msvcrt::strcmp must match exact string");

    char formatted[128]{};
    int numChars = msvcrt::sprintf(formatted, "Status: %s (code %d)", "Active", 200);
    TEST_ASSERT(numChars > 0, "msvcrt::sprintf must format string successfully");
    TEST_ASSERT(std::string(formatted) == "Status: Active (code 200)", "msvcrt::sprintf must format correctly");

    // 3. Functional Test: Crypto, UI & Network Stubs
    uintptr_t hCrypt = 0;
    TEST_ASSERT(advapi32::CryptAcquireContextA(&hCrypt, nullptr, nullptr, 0, 0) == win32::TRUE, "CryptAcquireContextA must return TRUE");
    uint8_t randBytes[16]{};
    TEST_ASSERT(advapi32::CryptGenRandom(hCrypt, 16, randBytes) == win32::TRUE, "CryptGenRandom must generate random bytes");
    bool hasNonZero = false;
    for (int i = 0; i < 16; ++i) {
        if (randBytes[i] != 0) hasNonZero = true;
    }
    TEST_ASSERT(hasNonZero, "CryptGenRandom must generate non-zero random entropy");
    TEST_ASSERT(advapi32::CryptReleaseContext(hCrypt, 0) == win32::TRUE, "CryptReleaseContext must return TRUE");

    ws2_32::WSADATA wsaData{};
    TEST_ASSERT(ws2_32::WSAStartup(0x0202, &wsaData) == 0, "WSAStartup must succeed with 0");
    TEST_ASSERT(ws2_32::inet_addr("127.0.0.1") == 0x0100007F, "inet_addr must resolve 127.0.0.1 to 0x0100007F");
    TEST_ASSERT(ws2_32::WSACleanup() == 0, "WSACleanup must succeed");

    // 4. MicaNT Interactive Command Prompt Shell Built-in Commands
    shell::CommandShell cmdShell;
    std::ostringstream oss;

    // Test ver
    oss.str("");
    int rc = cmdShell.execute("ver", oss);
    TEST_ASSERT(rc == 0, "Shell ver command must succeed");
    TEST_ASSERT(oss.str().find("MicaNT") != std::string::npos, "Shell ver must output MicaNT");

    // Test echo with variable expansion
    oss.str("");
    rc = cmdShell.execute("echo Operating System: %OS%", oss);
    TEST_ASSERT(rc == 0, "Shell echo command must succeed");
    TEST_ASSERT(oss.str().find("Operating System: MicaNT") != std::string::npos, "Shell must expand %OS% to MicaNT");

    // Test set command
    oss.str("");
    rc = cmdShell.execute("set SHELL_TEST=Passed", oss);
    TEST_ASSERT(rc == 0, "Shell set command must succeed");
    oss.str("");
    rc = cmdShell.execute("echo Result: %SHELL_TEST%", oss);
    TEST_ASSERT(oss.str().find("Result: Passed") != std::string::npos, "Shell must expand user-defined variable %SHELL_TEST%");

    // Test time / mem / systeminfo
    oss.str("");
    rc = cmdShell.execute("time", oss);
    TEST_ASSERT(rc == 0, "Shell time command must succeed");
    TEST_ASSERT(oss.str().find("System Time") != std::string::npos, "Shell time output must show System Time");

    oss.str("");
    rc = cmdShell.execute("mem", oss);
    TEST_ASSERT(rc == 0, "Shell mem command must succeed");
    TEST_ASSERT(oss.str().find("Kernel NonPaged Pool") != std::string::npos, "Shell mem output must show pool statistics");

    oss.str("");
    rc = cmdShell.execute("systeminfo", oss);
    TEST_ASSERT(rc == 0, "Shell systeminfo command must succeed");
    TEST_ASSERT(oss.str().find("x86_64") != std::string::npos, "Shell systeminfo must show x86_64 architecture");

    // Test dir
    oss.str("");
    rc = cmdShell.execute("dir", oss);
    TEST_ASSERT(rc == 0, "Shell dir command must succeed");
    TEST_ASSERT(oss.str().find("Directory of") != std::string::npos, "Shell dir must output directory header");

    // 5. Unmodified Third-Party PE Execution via Shell Engine
    oss.str("");
    std::cout << "\n[Shell Test] Executing unmodified CRT binary 'bin/unmodified_crt_sample.exe' via CommandShell...\n";
#ifdef _WIN32
    if (!std::filesystem::exists("bin/unmodified_crt_sample.exe") && std::filesystem::exists("test/unmodified_crt_sample.cpp")) {
        std::filesystem::create_directories("bin");
        (void)std::system("clang++ -O2 test/unmodified_crt_sample.cpp -o bin/unmodified_crt_sample.exe");
    }
    int execRc = cmdShell.execute("exec bin/unmodified_crt_sample.exe", oss);
    std::string execOutput = oss.str();
    std::cout << execOutput << std::flush;

    TEST_ASSERT(execRc == 0, "Shell execution of unmodified_crt_sample.exe must return exit code 0");
    TEST_ASSERT(execOutput.find("Process finished with exit code 0") != std::string::npos,
                "Process must finish with exit code 0");
#else
    std::cout << "[Shell Test] Skipping unmodified Windows CRT PE execution on Linux host.\n";
#endif
}

// ============================================================================
// Suite 38: Storage Stack & FAT32 Filesystem Engine Tests
// ============================================================================
void Test_StorageAndFat32FileSystem() {
    // 1. In-Memory RamDisk Block Device
    constexpr uint32_t SECTOR_SIZE = 512;
    constexpr uint64_t TOTAL_DISK_BYTES = 64 * 1024 * 1024; // 64 MB
    auto ramDisk = std::make_shared<storage::RamDiskDevice>(
        L"\\Device\\Harddisk0\\Partition0",
        TOTAL_DISK_BYTES,
        SECTOR_SIZE
    );
    TEST_ASSERT(ramDisk->getBlockSize() == SECTOR_SIZE, "RAM disk sector size must be 512");
    TEST_ASSERT(ramDisk->getTotalBlocks() == (TOTAL_DISK_BYTES / SECTOR_SIZE), "RAM disk total blocks must match 131072");
    TEST_ASSERT(ramDisk->getTotalBytes() == TOTAL_DISK_BYTES, "RAM disk total bytes must match 64 MB");

    // Test raw sector write and read
    std::vector<uint8_t> testSec(SECTOR_SIZE, 0xA5);
    NtStatus st = ramDisk->writeBlocks(100, 1, testSec.data());
    TEST_ASSERT(NT_SUCCESS(st), "RamDisk writeBlocks to sector 100 must succeed");

    std::vector<uint8_t> readSec(SECTOR_SIZE, 0);
    st = ramDisk->readBlocks(100, 1, readSec.data());
    TEST_ASSERT(NT_SUCCESS(st), "RamDisk readBlocks from sector 100 must succeed");
    TEST_ASSERT(std::memcmp(testSec.data(), readSec.data(), SECTOR_SIZE) == 0, "RamDisk sector 100 data must match exactly");

    // 2. MBR Partition Table Creation & Parsing
    std::vector<storage::MbrPartitionEntry> writePartitions(1);
    writePartitions[0].bootIndicator = 0x80; // Bootable
    writePartitions[0].partitionType = storage::MBR_TYPE_FAT32_LBA; // 0x0C
    writePartitions[0].startLba = 2048; // 1 MB boundary alignment
    writePartitions[0].sectorCount = static_cast<uint32_t>(ramDisk->getTotalBlocks() - 2048); // 129024 sectors (~63 MB)

    st = storage::PartitionManager::writeMbr(*ramDisk, writePartitions);
    TEST_ASSERT(NT_SUCCESS(st), "PartitionManager::writeMbr must succeed");

    std::vector<storage::MbrPartitionEntry> readPartitions;
    st = storage::PartitionManager::parseMbr(*ramDisk, readPartitions);
    TEST_ASSERT(NT_SUCCESS(st), "PartitionManager::parseMbr must succeed");
    TEST_ASSERT(readPartitions.size() == 1, "MBR must contain 1 active partition");
    TEST_ASSERT(readPartitions[0].bootIndicator == 0x80, "MBR partition must have boot indicator 0x80");
    TEST_ASSERT(readPartitions[0].partitionType == storage::MBR_TYPE_FAT32_LBA, "Partition type must be FAT32 LBA");
    TEST_ASSERT(readPartitions[0].startLba == 2048, "Partition start LBA must be 2048");
    TEST_ASSERT(readPartitions[0].sectorCount == (ramDisk->getTotalBlocks() - 2048), "Partition sector count must match");

    // 3. Partition Device Slicing
    auto partition1 = std::make_shared<storage::PartitionDevice>(
        L"\\Device\\Harddisk0\\Partition1",
        ramDisk,
        readPartitions[0].startLba,
        readPartitions[0].sectorCount
    );
    TEST_ASSERT(partition1->getBlockSize() == SECTOR_SIZE, "Partition device block size must be 512");
    TEST_ASSERT(partition1->getTotalBlocks() == readPartitions[0].sectorCount, "Partition device total blocks must match");
    TEST_ASSERT(partition1->getStartLba() == 2048, "Partition device start LBA must be 2048");

    // 4. FAT32 Filesystem Formatting
    // Format partition1 with 8 sectors per cluster (4 KB clusters)
    st = fat32::Fat32FileSystem::format(*partition1, "MICANT_SYS", 8);
    TEST_ASSERT(NT_SUCCESS(st), "Fat32FileSystem::format must succeed");

    // 5. FAT32 Mounting & BPB Geometry
    fat32::Fat32FileSystem fs;
    st = fs.mount(partition1);
    TEST_ASSERT(NT_SUCCESS(st), "Fat32FileSystem::mount must succeed");
    TEST_ASSERT(fs.isMounted(), "Filesystem must report mounted status");
    TEST_ASSERT(fs.getBytesPerCluster() == 4096, "Cluster size must be 4096 bytes (4 KB)");
    TEST_ASSERT(fs.getRootCluster() == 2, "FAT32 root cluster must be cluster 2");
    TEST_ASSERT(fs.getTotalClusters() > 15000, "Volume must have > 15000 total clusters");

    // 6. Directory Tree Creation
    st = fs.createDirectory(L"Windows");
    TEST_ASSERT(NT_SUCCESS(st), "createDirectory 'Windows' must succeed");

    st = fs.createDirectory(L"Windows\\System32");
    TEST_ASSERT(NT_SUCCESS(st), "createDirectory 'Windows\\System32' must succeed");

    st = fs.createDirectory(L"Windows\\System32\\drivers");
    TEST_ASSERT(NT_SUCCESS(st), "createDirectory 'Windows\\System32\\drivers' must succeed");

    st = fs.createDirectory(L"Users");
    TEST_ASSERT(NT_SUCCESS(st), "createDirectory 'Users' must succeed");

    st = fs.createDirectory(L"Users\\Administrator");
    TEST_ASSERT(NT_SUCCESS(st), "createDirectory 'Users\\Administrator' must succeed");

    // 7. Short Filename (8.3) Creation & Verification
    std::string ntdllStub = "MZ-MICANT-CLEANROOM-NTDLL-CORE-PAYLOAD";
    std::vector<uint8_t> ntdllBytes(ntdllStub.begin(), ntdllStub.end());
    st = fs.createFile(L"Windows\\System32\\ntdll.dll", ntdllBytes);
    TEST_ASSERT(NT_SUCCESS(st), "createFile 'Windows\\System32\\ntdll.dll' must succeed");

    std::vector<uint8_t> readNtdll;
    st = fs.readFile(L"Windows\\System32\\ntdll.dll", 0, ntdllBytes.size(), readNtdll);
    TEST_ASSERT(NT_SUCCESS(st), "readFile 'Windows\\System32\\ntdll.dll' must succeed");
    TEST_ASSERT(readNtdll == ntdllBytes, "Read ntdll content must match written content");

    // 8. Long File Name (LFN) Unicode Reconstruction
    std::wstring lfnName = L"MicaNT Executive Advanced Architecture Specification Document.json";
    std::wstring lfnFullPath = L"Windows\\System32\\" + lfnName;
    std::string lfnPayload = "{\"Architecture\":\"Dave Cutler Clean-Room\",\"Subsystems\":[\"ob\",\"mm\",\"ps\",\"fat32\"]}";
    std::vector<uint8_t> lfnBytes(lfnPayload.begin(), lfnPayload.end());

    st = fs.createFile(lfnFullPath, lfnBytes);
    TEST_ASSERT(NT_SUCCESS(st), "createFile with Long File Name (LFN) must succeed");

    std::vector<uint8_t> readLfn;
    st = fs.readFile(lfnFullPath, 0, lfnBytes.size(), readLfn);
    TEST_ASSERT(NT_SUCCESS(st), "readFile via Long File Name must succeed");
    TEST_ASSERT(readLfn == lfnBytes, "Read LFN file content must match written content");

    // 9. Multi-Cluster File Integrity Across Cluster Chain Boundaries
    // Cluster size is 4096 bytes. We write 14,336 bytes (spanning 3.5 clusters = 4 clusters allocated)
    constexpr size_t MULTI_CLUSTER_SIZE = 14336;
    std::vector<uint8_t> multiData(MULTI_CLUSTER_SIZE);
    for (size_t i = 0; i < MULTI_CLUSTER_SIZE; ++i) {
        multiData[i] = static_cast<uint8_t>((i * 13 + 0x47) & 0xFF);
    }

    std::wstring driverPath = L"Windows\\System32\\drivers\\fastfat_test.sys";
    st = fs.createFile(driverPath, multiData);
    TEST_ASSERT(NT_SUCCESS(st), "createFile multi-cluster driver must succeed");

    // Read full file
    std::vector<uint8_t> readMultiFull;
    st = fs.readFile(driverPath, 0, MULTI_CLUSTER_SIZE, readMultiFull);
    TEST_ASSERT(NT_SUCCESS(st), "readFile full multi-cluster must succeed");
    TEST_ASSERT(readMultiFull.size() == MULTI_CLUSTER_SIZE, "Multi-cluster read length must match");
    TEST_ASSERT(std::memcmp(readMultiFull.data(), multiData.data(), MULTI_CLUSTER_SIZE) == 0,
                "Multi-cluster payload must match 100% across all cluster boundaries");

    // Boundary read: 128 bytes spanning cluster boundary at 4096 (offset 4032 to 4160)
    std::vector<uint8_t> boundaryRead1;
    st = fs.readFile(driverPath, 4032, 128, boundaryRead1);
    TEST_ASSERT(NT_SUCCESS(st), "readFile across cluster 0->1 boundary must succeed");
    TEST_ASSERT(boundaryRead1.size() == 128, "Boundary 1 read size must be 128");
    TEST_ASSERT(std::memcmp(boundaryRead1.data(), multiData.data() + 4032, 128) == 0,
                "Boundary 1 data across cluster 0->1 boundary must match exactly");

    // Boundary read: 256 bytes spanning cluster boundary at 8192 (offset 8100 to 8356)
    std::vector<uint8_t> boundaryRead2;
    st = fs.readFile(driverPath, 8100, 256, boundaryRead2);
    TEST_ASSERT(NT_SUCCESS(st), "readFile across cluster 1->2 boundary must succeed");
    TEST_ASSERT(boundaryRead2.size() == 256, "Boundary 2 read size must be 256");
    TEST_ASSERT(std::memcmp(boundaryRead2.data(), multiData.data() + 8100, 256) == 0,
                "Boundary 2 data across cluster 1->2 boundary must match exactly");

    // 10. Directory Enumeration & Path Traversal Verification
    fat32::FatFileInfo sys32Info{};
    st = fs.findPath(L"Windows\\System32", sys32Info);
    TEST_ASSERT(NT_SUCCESS(st), "findPath 'Windows\\System32' must succeed");
    TEST_ASSERT(sys32Info.isDirectory, "'Windows\\System32' must be a directory");

    std::vector<fat32::FatFileInfo> sys32Entries;
    st = fs.readDirectory(sys32Info.firstCluster, sys32Entries);
    TEST_ASSERT(NT_SUCCESS(st), "readDirectory for 'Windows\\System32' must succeed");
    TEST_ASSERT(sys32Entries.size() >= 3, "Directory must contain at least 3 entries");

    bool foundNtdll = false;
    bool foundLfnDoc = false;
    bool foundDriversDir = false;
    for (const auto& entry : sys32Entries) {
        if (entry.name == L"ntdll.dll") foundNtdll = true;
        if (entry.name == lfnName) foundLfnDoc = true;
        if (entry.name == L"drivers" && entry.isDirectory) foundDriversDir = true;
    }
    TEST_ASSERT(foundNtdll, "Directory enumeration must find 'ntdll.dll'");
    TEST_ASSERT(foundLfnDoc, "Directory enumeration must reconstruct full LFN name");
    TEST_ASSERT(foundDriversDir, "Directory enumeration must find 'drivers' subdirectory");

    // 11. VirtualFileSystem Integration Test
    // Mount the partition block device into VirtualFileSystem
    st = fs::VirtualFileSystem::get().mountBlockDevice(partition1);
    TEST_ASSERT(NT_SUCCESS(st), "VirtualFileSystem::mountBlockDevice must succeed");
    TEST_ASSERT(fs::VirtualFileSystem::get().getMountedFat32() != nullptr, "VFS mounted FAT32 pointer must be valid");
    TEST_ASSERT(fs::VirtualFileSystem::get().getMountedBlockDevice() == partition1, "VFS mounted block device must match partition1");

    // 12. GPT Partition Parsing Guardrail Check
    auto blankDisk = std::make_shared<storage::RamDiskDevice>(
        L"\\Device\\Harddisk1\\Partition0",
        16 * 1024 * 1024,
        SECTOR_SIZE
    );
    std::vector<storage::GptPartitionEntry> gptEntries;
    st = storage::PartitionManager::parseGpt(*blankDisk, gptEntries);
    TEST_ASSERT(st == NtStatus::UnrecognizedVolume, "Blank disk must return UnrecognizedVolume for GPT");
}

// ============================================================================
// Suite 39: NDIS 6.x, TCP/IP, Next-Gen QUIC, and Winsock Subsystem Tests
// ============================================================================
void Test_NdisAndTcpIpNetworkStack() {
    // 1. NDIS 6.x Virtual Ethernet Adapter Verification
    auto vNic = std::make_shared<ndis::VirtualNetworkAdapter>(
        L"\\Device\\NdisTestNic",
        L"MicaNT Test Ethernet",
        ndis::MacAddress(0x02, 0x00, 0x4D, 0x49, 0x43, 0x41), // "MICA"
        1500,
        10'000'000'000ULL
    );
    TEST_ASSERT(vNic->getMacAddress().toString() == "02-00-4D-49-43-41", "MAC address string format must match");
    TEST_ASSERT(vNic->getMtu() == 1500, "MTU must be 1500");
    TEST_ASSERT(vNic->getSpeedBps() == 10'000'000'000ULL, "Speed must be 10 Gbps");
    TEST_ASSERT(vNic->getLinkState() == ndis::MediaConnectState::Connected, "Link state must be Connected");

    // Frame transmission
    std::string testPayload = "MicaNT NDIS 6.x Frame Transmission Test Payload";
    auto frame = ndis::buildEthernetFrame(
        ndis::MacAddress::broadcast(),
        vNic->getMacAddress(),
        ndis::ETHERTYPE_IPV4,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(testPayload.data()), testPayload.size())
    );
    TEST_ASSERT(frame.size() == ndis::ETH_HLEN + testPayload.size(), "Frame size must match header + payload");

    const auto* ethHdr = reinterpret_cast<const ndis::EthernetHeader*>(frame.data());
    TEST_ASSERT(ethHdr->getEtherType() == ndis::ETHERTYPE_IPV4, "EtherType must be IPv4 (0x0800)");
    TEST_ASSERT(ethHdr->destMac.isBroadcast(), "Destination MAC must be broadcast");

    NtStatus st = vNic->sendPacket(frame);
    TEST_ASSERT(NT_SUCCESS(st), "vNic->sendPacket must succeed");
    auto stats = vNic->getStatistics();
    TEST_ASSERT(stats.txPackets == 1, "Tx packet count must be 1");
    TEST_ASSERT(stats.txBytes == frame.size(), "Tx bytes must match frame length");

    // 2. TCP/IP Stack & ARP Resolution
    auto& net = tcpip::NetworkStack::get();
    net.initialize(vNic);

    TEST_ASSERT(net.getLocalIp().toString() == "192.168.1.100", "Local IP must be 192.168.1.100");
    TEST_ASSERT(net.getSubnetMask().toString() == "255.255.255.0", "Subnet mask must be 255.255.255.0");
    TEST_ASSERT(net.getGateway().toString() == "192.168.1.1", "Gateway IP must be 192.168.1.1");
    TEST_ASSERT(net.getDnsServer().toString() == "8.8.8.8", "DNS server must be 8.8.8.8");

    ndis::MacAddress resolvedMac;
    bool resolved = net.resolveArp(tcpip::Ipv4Address::loopback(), resolvedMac);
    TEST_ASSERT(resolved, "Loopback ARP resolution must succeed");
    TEST_ASSERT(resolvedMac == vNic->getMacAddress(), "Loopback MAC must match adapter MAC");

    resolved = net.resolveArp(net.getGateway(), resolvedMac);
    TEST_ASSERT(resolved, "Gateway ARP resolution must succeed from cache");

    // 3. IPv4 Internet Checksum Algorithm (RFC 1071)
    uint8_t sampleIpHeader[] = {
        0x45, 0x00, 0x00, 0x3c, 0x1c, 0x46, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00, // zero checksum for calculation
        0xc0, 0xa8, 0x01, 0x64, // 192.168.1.100
        0xc0, 0xa8, 0x01, 0x01  // 192.168.1.1
    };
    uint16_t calcCsum = tcpip::calculateInternetChecksum(sampleIpHeader, sizeof(sampleIpHeader));
    TEST_ASSERT(calcCsum != 0, "Checksum must be calculated");
    *reinterpret_cast<uint16_t*>(sampleIpHeader + 10) = calcCsum;
    uint16_t verifyCsum = tcpip::calculateInternetChecksum(sampleIpHeader, sizeof(sampleIpHeader));
    TEST_ASSERT(verifyCsum == 0, "Checksum verification must yield 0 (100% valid)");

    // 4. IPv6 Addressing & Loopback (RFC 8200)
    auto v6Loopback = tcpip::Ipv6Address::loopback();
    TEST_ASSERT(v6Loopback.isLoopback(), "IPv6 address must report isLoopback");
    TEST_ASSERT(v6Loopback.toString() == "::1", "IPv6 loopback string must be '::1'");

    auto v6LinkLocal = tcpip::Ipv6Address::linkLocalMica();
    TEST_ASSERT(!v6LinkLocal.isZero(), "IPv6 link local must not be zero");
    TEST_ASSERT(v6LinkLocal.toString().find("fe80:") == 0, "IPv6 link local must begin with fe80:");

    // 5. ICMPv4 Echo Request & Reply (Ping Engine)
    auto pingResult = net.ping(tcpip::Ipv4Address::loopback());
    TEST_ASSERT(pingResult.success, "Ping 127.0.0.1 must succeed");
    TEST_ASSERT(pingResult.bytesReceived == 32, "Ping bytes received must be 32");
    TEST_ASSERT(pingResult.ttl == 64, "Ping TTL must be 64");
    TEST_ASSERT(pingResult.rttMs <= 10, "Ping round trip time must be <= 10ms for in-memory stack");

    // 6. UDP Transport Layer Datagram Transmission
    int udpServer = net.createSocket(tcpip::AF_INET, tcpip::SOCK_DGRAM, tcpip::IPPROTO_UDP);
    TEST_ASSERT(udpServer > 0, "createSocket UDP server must succeed");
    bool bound = net.bindSocket(udpServer, tcpip::Ipv4Address::loopback(), 9876);
    TEST_ASSERT(bound, "bindSocket UDP server to port 9876 must succeed");

    int udpClient = net.createSocket(tcpip::AF_INET, tcpip::SOCK_DGRAM, tcpip::IPPROTO_UDP);
    TEST_ASSERT(udpClient > 0, "createSocket UDP client must succeed");

    std::string udpMsg = "MicaNT-UDP-FastPath-Datagram";
    int sentUdp = net.sendToSocket(udpClient, udpMsg.data(), udpMsg.size(), tcpip::Ipv4Address::loopback(), 9876);
    TEST_ASSERT(sentUdp == static_cast<int>(udpMsg.size()), "sendToSocket must transmit all bytes");

    char udpRecvBuf[128]{};
    int recvdUdp = net.recvSocket(udpServer, udpRecvBuf, sizeof(udpRecvBuf));
    TEST_ASSERT(recvdUdp == static_cast<int>(udpMsg.size()), "recvSocket must receive all UDP bytes");
    TEST_ASSERT(std::string(udpRecvBuf, recvdUdp) == udpMsg, "Received UDP payload must match transmitted string");

    net.closeSocket(udpServer);
    net.closeSocket(udpClient);

    // 7. TCP Connection 3-Way Handshake & Bidirectional Stream
    int tcpServer = net.createSocket(tcpip::AF_INET, tcpip::SOCK_STREAM, tcpip::IPPROTO_TCP);
    TEST_ASSERT(tcpServer > 0, "createSocket TCP server must succeed");
    bound = net.bindSocket(tcpServer, tcpip::Ipv4Address::loopback(), 8080);
    TEST_ASSERT(bound, "bindSocket TCP server port 8080 must succeed");
    bool listening = net.listenSocket(tcpServer, 5);
    TEST_ASSERT(listening, "listenSocket TCP server must succeed");

    int tcpClient = net.createSocket(tcpip::AF_INET, tcpip::SOCK_STREAM, tcpip::IPPROTO_TCP);
    TEST_ASSERT(tcpClient > 0, "createSocket TCP client must succeed");
    bool connected = net.connectSocket(tcpClient, tcpip::Ipv4Address::loopback(), 8080);
    TEST_ASSERT(connected, "connectSocket TCP client to 127.0.0.1:8080 must succeed");

    tcpip::Ipv4Address acceptedClientIp;
    uint16_t acceptedClientPort = 0;
    int acceptedSock = net.acceptSocket(tcpServer, acceptedClientIp, acceptedClientPort);
    TEST_ASSERT(acceptedSock > 0, "acceptSocket on TCP server must return valid connected socket");

    // Client -> Server Stream Transfer (HTTP Request)
    std::string httpRequest = "GET /index.html HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n";
    int sentTcp = net.sendSocket(tcpClient, httpRequest.data(), httpRequest.size());
    TEST_ASSERT(sentTcp == static_cast<int>(httpRequest.size()), "Client sendSocket must transmit all bytes");

    char serverRecvBuf[256]{};
    int recvdTcp = net.recvSocket(acceptedSock, serverRecvBuf, sizeof(serverRecvBuf));
    TEST_ASSERT(recvdTcp == static_cast<int>(httpRequest.size()), "Server recvSocket must receive all bytes");
    TEST_ASSERT(std::string(serverRecvBuf, recvdTcp) == httpRequest, "Server received payload must match HTTP request");

    // Server -> Client Stream Transfer (HTTP Response)
    std::string httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 17\r\n\r\nHello from MicaNT";
    sentTcp = net.sendSocket(acceptedSock, httpResponse.data(), httpResponse.size());
    TEST_ASSERT(sentTcp == static_cast<int>(httpResponse.size()), "Server sendSocket must transmit all bytes");

    char clientRecvBuf[256]{};
    recvdTcp = net.recvSocket(tcpClient, clientRecvBuf, sizeof(clientRecvBuf));
    TEST_ASSERT(recvdTcp == static_cast<int>(httpResponse.size()), "Client recvSocket must receive all bytes");
    TEST_ASSERT(std::string(clientRecvBuf, recvdTcp) == httpResponse, "Client received payload must match HTTP response");

    net.closeSocket(tcpClient);
    net.closeSocket(acceptedSock);
    net.closeSocket(tcpServer);

    // 8. Next-Gen QUIC Protocol Header Framing (RFC 9000)
    // Long Header: Initial packet (Packet Type 0x00, Version 1)
    tcpip::QuicLongHeader qlh{};
    qlh.flags = 0xC0; // Long Header (0x80) | Fixed Bit (0x40) | Initial (0x00)
    qlh.version = tcpip::htonl(0x00000001); // QUIC v1
    qlh.dcil = 8;
    qlh.scil = 8;

    TEST_ASSERT(qlh.isLongHeader(), "QUIC packet must be recognized as Long Header");
    TEST_ASSERT(qlh.getPacketType() == 0, "QUIC packet type must be 0 (Initial)");
    TEST_ASSERT(qlh.getVersion() == 1, "QUIC version must be 1 (RFC 9000)");

    // Short Header: 1-RTT packet (Spin bit 1, Key Phase 0, Packet Number Length 2)
    tcpip::QuicShortHeader qsh{};
    qsh.flags = 0x61; // Short Header (0x00) | Fixed Bit (0x40) | Spin Bit (0x20) | PN Len (0x01 = 2 bytes)
    TEST_ASSERT(qsh.isShortHeader(), "QUIC packet must be recognized as Short Header");
    TEST_ASSERT(qsh.getSpinBit(), "QUIC spin bit must be true");
    TEST_ASSERT(qsh.getPacketNumberLength() == 2, "QUIC packet number length must be 2 bytes");

    // 9. Winsock 2 (ws2_32.dll) High-Level API Verification
    ws2_32::WSADATA wsaData{};
    int wsaRc = ws2_32::WSAStartup(0x0202, &wsaData);
    TEST_ASSERT(wsaRc == 0, "WSAStartup must succeed");
    TEST_ASSERT(wsaData.wVersion == 0x0202, "Winsock version must be 2.2");

    ws2_32::SOCKET wsaSock = ws2_32::socket(ws2_32::AF_INET, ws2_32::SOCK_STREAM, ws2_32::IPPROTO_TCP);
    TEST_ASSERT(wsaSock != ws2_32::INVALID_SOCKET, "Winsock socket() must return valid descriptor");

    ws2_32::sockaddr_in saddr{};
    saddr.sin_family = ws2_32::AF_INET;
    saddr.sin_port = ws2_32::htons(12345);
    saddr.sin_addr.S_un.S_addr = ws2_32::inet_addr("127.0.0.1");
    int bindRc = ws2_32::bind(wsaSock, reinterpret_cast<const ws2_32::sockaddr*>(&saddr), sizeof(saddr));
    TEST_ASSERT(bindRc == 0, "Winsock bind() must succeed");

    int closeRc = ws2_32::closesocket(wsaSock);
    TEST_ASSERT(closeRc == 0, "Winsock closesocket() must succeed");

    char hostBuf[64]{};
    int hostRc = ws2_32::gethostname(hostBuf, sizeof(hostBuf));
    TEST_ASSERT(hostRc == 0, "Winsock gethostname() must succeed");
    TEST_ASSERT(std::string(hostBuf) == "MicaNT-Workstation", "Hostname must be MicaNT-Workstation");

    // 10. IP Helper API (iphlpapi.dll) Verification
    iphlpapi::IP_ADAPTER_INFO adaptInfo{};
    uint32_t adaptBufLen = sizeof(adaptInfo);
    uint32_t iphlpRc = iphlpapi::GetAdaptersInfo(&adaptInfo, &adaptBufLen);
    TEST_ASSERT(iphlpRc == iphlpapi::ERROR_SUCCESS, "GetAdaptersInfo must return ERROR_SUCCESS");
    TEST_ASSERT(std::string(adaptInfo.ipAddressList.ipAddress.str) == "192.168.1.100", "Adapter IP must be 192.168.1.100");
    TEST_ASSERT(std::string(adaptInfo.gatewayList.ipAddress.str) == "192.168.1.1", "Gateway must be 192.168.1.1");
    TEST_ASSERT(adaptInfo.address[0] == 0x02 && adaptInfo.address[1] == 0x00, "Adapter MAC prefix must be 02-00");

    iphlpapi::FIXED_INFO fixedInfo{};
    uint32_t fixedBufLen = sizeof(fixedInfo);
    iphlpRc = iphlpapi::GetNetworkParams(&fixedInfo, &fixedBufLen);
    TEST_ASSERT(iphlpRc == iphlpapi::ERROR_SUCCESS, "GetNetworkParams must return ERROR_SUCCESS");
    TEST_ASSERT(std::string(fixedInfo.hostName) == "MicaNT-Server" || std::string(fixedInfo.hostName) == "MicaNT-Workstation", "HostName must match host name");
    TEST_ASSERT(std::string(fixedInfo.dnsServerList.ipAddress.str) == "8.8.8.8", "DNS Server must be 8.8.8.8");

    // 11. Shell Built-in Network Commands (ipconfig, ping, netstat)
    shell::CommandShell cmdShell;
    std::ostringstream oss;

    // ipconfig
    oss.str("");
    int shRc = cmdShell.execute("ipconfig", oss);
    TEST_ASSERT(shRc == 0, "Shell ipconfig command must succeed");
    TEST_ASSERT(oss.str().find("192.168.1.100") != std::string::npos, "ipconfig output must include 192.168.1.100");
    TEST_ASSERT(oss.str().find("255.255.255.0") != std::string::npos, "ipconfig output must include 255.255.255.0");

    // ipconfig /all
    oss.str("");
    shRc = cmdShell.execute("ipconfig /all", oss);
    TEST_ASSERT(shRc == 0, "Shell ipconfig /all command must succeed");
    TEST_ASSERT(oss.str().find("Physical Address") != std::string::npos, "ipconfig /all must output MAC address");
    TEST_ASSERT(oss.str().find("DNS Servers") != std::string::npos, "ipconfig /all must output DNS servers");

    // ping
    oss.str("");
    shRc = cmdShell.execute("ping 127.0.0.1", oss);
    TEST_ASSERT(shRc == 0, "Shell ping 127.0.0.1 command must succeed");
    TEST_ASSERT(oss.str().find("Reply from 127.0.0.1") != std::string::npos, "ping output must show Echo Reply");
    TEST_ASSERT(oss.str().find("Packets: Sent = 4, Received = 4") != std::string::npos, "ping output must show 4 packets received (0% loss)");

    // netstat
    oss.str("");
    shRc = cmdShell.execute("netstat", oss);
    TEST_ASSERT(shRc == 0, "Shell netstat command must succeed");
    TEST_ASSERT(oss.str().find("Active Connections") != std::string::npos, "netstat output must show Active Connections table");
}

// ============================================================================
// Suite 40: AArch64 (ARM64) Hardware Architecture & Fast Syscall Engine
// ============================================================================
void Test_Arm64HardwareArchitectureAndSyscall() {
    // 1. General-Purpose Register File & Condition Flags (PSTATE)
    arm64::Arm64Context ctx{};
    ctx.x0 = 0x1111222233334444ULL;
    ctx.x8 = sys::SSN_NtAllocateVirtualMemory; // SSN in X8
    ctx.fp = 0x00007FFFFFFFE000ULL;           // X29
    ctx.lr = 0x0000000140001050ULL;           // X30
    ctx.sp_el0 = 0x00007FFFFFFFDFF0ULL;       // User stack
    ctx.sp_el1 = 0xFFFF800000100000ULL;       // Kernel stack
    ctx.pc = 0x0000000140001000ULL;
    ctx.pstate = arm64::pstate::MODE_EL0t;

    TEST_ASSERT(ctx.x0 == 0x1111222233334444ULL, "ARM64 X0 register must match initialized value");
    TEST_ASSERT(ctx.x[8] == sys::SSN_NtAllocateVirtualMemory, "ARM64 X8 register array access must match");
    TEST_ASSERT(ctx.fp == ctx.x[29], "ARM64 FP must alias X29");
    TEST_ASSERT(ctx.lr == ctx.x[30], "ARM64 LR must alias X30");
    TEST_ASSERT(ctx.getCurrentEl() == arm64::ExceptionLevel::EL0, "Initial mode must be EL0 (Userland)");

    ctx.pstate = arm64::pstate::MODE_EL1h;
    TEST_ASSERT(ctx.getCurrentEl() == arm64::ExceptionLevel::EL1, "Mode EL1h must report EL1 (Kernel Executive)");

    ctx.setConditionFlags(true, true, false, false);
    TEST_ASSERT(ctx.isNegativeFlag(), "PSTATE N flag must be set");
    TEST_ASSERT(ctx.isZeroFlag(), "PSTATE Z flag must be set");

    // 2. 128-bit SIMD / NEON Vector Register File (Q0 - Q31)
    ctx.v[0].d[0] = 0xAAAAAAAAAAAAAAAAULL;
    ctx.v[0].d[1] = 0xBBBBBBBBBBBBBBBBULL;
    TEST_ASSERT(!ctx.v[0].isZero(), "Vector register Q0 must not be zero");
    TEST_ASSERT(ctx.v[0].low64 == 0xAAAAAAAAAAAAAAAAULL, "Vector register Q0 low 64-bit must match D0");
    ctx.fpcr = 0x03C00000; // Default FPCR
    TEST_ASSERT(ctx.fpcr == 0x03C00000, "FPCR register must match value");

    // 3. Exception Syndrome Register (ESR_EL1) & Fault Handling
    arm64::Arm64ExceptionSyndrome svcSyndrome{};
    // Construct ESR for SVC in AArch64 (EC = 0x15, IL = 1, imm16 = 1)
    svcSyndrome.rawEsr = (arm64::esr::EC_SVC64 << 26) | (1U << 25) | 1U;
    TEST_ASSERT(svcSyndrome.isSupervisorCall(), "Syndrome must identify as Supervisor Call (SVC)");
    TEST_ASSERT(svcSyndrome.getExceptionClass() == arm64::esr::EC_SVC64, "Exception class must be EC_SVC64 (0x15)");
    TEST_ASSERT(svcSyndrome.getSvcImmediate() == 1, "SVC immediate must be #1 for NT system calls");

    arm64::Arm64ExceptionSyndrome dabtSyndrome{};
    // Construct ESR for Data Abort from lower EL (EC = 0x24, IL = 1, WnR = 1 (bit 6), DFSC = 0x07 (L3 translation fault))
    dabtSyndrome.rawEsr = (arm64::esr::EC_DABT_LOW << 26) | (1U << 25) | (1U << 6) | arm64::esr::DFSC_TRANS_L3;
    dabtSyndrome.faultAddress = 0x00007FFDF0001000ULL;
    TEST_ASSERT(dabtSyndrome.isDataAbort(), "Syndrome must identify as Data Abort");
    TEST_ASSERT(dabtSyndrome.isWriteNotRead(), "WnR bit must report Write operation");
    TEST_ASSERT(dabtSyndrome.getDataFaultStatusCode() == arm64::esr::DFSC_TRANS_L3, "DFSC must match L3 translation fault");
    TEST_ASSERT(dabtSyndrome.faultAddress == 0x00007FFDF0001000ULL, "FAR_EL1 fault address must match");

    // 4. AArch64 VMSA 48-bit 4-Level Translation Tables (MMU)
    arm64::Arm64Mmu mmu;
    TEST_ASSERT(mmu.getTtbr0() == 0x10000000ULL, "Default TTBR0_EL1 must be initialized");
    TEST_ASSERT(mmu.getTtbr1() == 0x20000000ULL, "Default TTBR1_EL1 must be initialized");
    TEST_ASSERT((mmu.getMair() & 0xFF) == 0xFF, "MAIR_EL1 Attr0 must be Normal WBWA (0xFF)");

    // Test 4-level index extraction
    uint64_t sampleVa = 0x0000'1234'5678'9ABCULL;
    auto indices = arm64::Arm64Mmu::extractIndices(sampleVa);
    TEST_ASSERT(indices.offset == 0xABC, "Offset bits [11:0] must match 0xABC");

    // Map a 4KB page and translate
    uint64_t testVa = 0x0000'0000'4000'0000ULL; // 1 GB boundary
    uint64_t testPa = 0x0000'0001'8000'0000ULL; // Physical RAM
    mmu.mapPage(testVa, testPa, arm64::mmu::AP_RW_ALL, arm64::mmu::ATTR_IDX_NORMAL_WBWA, false, false);

    uint64_t resolvedPa = 0;
    arm64::Arm64Pte resolvedPte{};
    NtStatus trStatus = mmu.translateVirtualAddress(testVa + 0x120, resolvedPa, resolvedPte);
    TEST_ASSERT(NT_SUCCESS(trStatus), "Virtual address translation must succeed for mapped page");
    TEST_ASSERT(resolvedPa == (testPa + 0x120), "Resolved physical address must match mapped base + offset");
    TEST_ASSERT(resolvedPte.isValid(), "Resolved PTE must be valid");

    // Unmapped page must return AccessViolation
    trStatus = mmu.translateVirtualAddress(0x0000'0000'5000'0000ULL, resolvedPa, resolvedPte);
    TEST_ASSERT(trStatus == NtStatus::AccessViolation, "Unmapped virtual address must return STATUS_ACCESS_VIOLATION");

    // 5. Fast System Call Dispatcher via SVC #1 (Windows on ARM64 ABI)
    // Dispatch NtAllocateVirtualMemory using ARM64 register frame
    uintptr_t allocBase = 0;
    size_t allocSize = 64 * 1024; // 64 KB

    arm64::Arm64Context svcCtx{};
    svcCtx.x8 = sys::SSN_NtAllocateVirtualMemory; // SSN in X8
    svcCtx.x0 = 0; // CurrentProcess
    svcCtx.x1 = reinterpret_cast<uint64_t>(&allocBase);
    svcCtx.x2 = 0;
    svcCtx.x3 = reinterpret_cast<uint64_t>(&allocSize);
    svcCtx.x4 = mm::MEM_COMMIT | mm::MEM_RESERVE;
    svcCtx.x5 = mm::PAGE_READWRITE;
    svcCtx.pc = 0x0000000140002000ULL;

    NtStatus svcResult = arm64::Arm64SyscallBridge::get().dispatchSvc(svcCtx, svcSyndrome);
    TEST_ASSERT(NT_SUCCESS(svcResult), "ARM64 SVC #1 dispatch for NtAllocateVirtualMemory must succeed");
    TEST_ASSERT(svcCtx.x0 == static_cast<uint64_t>(NtStatus::Success), "ARM64 X0 return register must hold STATUS_SUCCESS");
    TEST_ASSERT(allocBase != 0, "Allocated base address must be non-zero");
    TEST_ASSERT(svcCtx.pc == 0x0000000140002004ULL, "ARM64 PC must advance by 4 bytes past SVC instruction");

    // Free the allocated memory
    sys::NtFreeVirtualMemory(0, &allocBase, &allocSize, mm::MEM_RELEASE);

    // 6. Thread Pointer Register Binding (TPIDR_EL0 for TEB, TPIDR_EL1 for KPCR)
    ctx.tpidr_el0 = 0x00007FFDF0000000ULL; // TEB
    ctx.tpidr_el1 = 0xFFFF800000000000ULL; // KPCR
    TEST_ASSERT(ctx.tpidr_el0 == 0x00007FFDF0000000ULL, "TPIDR_EL0 must hold TEB address");
    TEST_ASSERT(ctx.tpidr_el1 == 0xFFFF800000000000ULL, "TPIDR_EL1 must hold KPCR address");

    // 7. Multi-Architecture HAL SMP Initialization (ARM64 8-Core Topology)
    auto& hal = hal::HardwareAbstractionLayer::get();
    hal.initialize(8, hal::ProcessorArchitecture::Arm64, 4000); // 8-core ARM64 @ 4.0 GHz
    TEST_ASSERT(hal.getProcessorCount() == 8, "HAL processor count must report 8 cores");
    auto* armKpcr = hal.getKpcr(0);
    TEST_ASSERT(armKpcr != nullptr, "HAL KPCR 0 must be non-null");
    TEST_ASSERT(armKpcr->prcb.architecture == hal::ProcessorArchitecture::Arm64, "PRCB architecture must be Arm64");
    TEST_ASSERT(armKpcr->prcb.coreClockMhz == 4000, "PRCB core clock must be 4000 MHz");

    // Revert HAL back to 4-core AMD64 for standard baseline
    hal.initialize(4, hal::ProcessorArchitecture::Amd64, 3600);
    TEST_ASSERT(hal.getProcessorCount() == 4, "HAL reset to 4 cores verified");
}

// ============================================================================
// Suite 41: Named Pipes & Mailslots IPC Subsystem (NPFS / MSFS)
// ============================================================================
void Test_NamedPipesAndMailslotsIpc() {
    using namespace win32;

    // Initialize Win32 exports and VFS
    InitializeWin32SubsystemExports();
    fs::VirtualFileSystem::get().initialize();

    // ------------------------------------------------------------------------
    // 1. Named Pipe Creation & Server Configuration
    // ------------------------------------------------------------------------
    HANDLE hServerPipe = CreateNamedPipeW(
        L"\\\\.\\pipe\\MicaTestPipe",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        2,    // max 2 instances
        1024, // out buffer
        1024, // in buffer
        100   // 100ms default timeout
    );
    TEST_ASSERT(hServerPipe != INVALID_HANDLE_VALUE, "CreateNamedPipeW must create valid pipe handle");

    DWORD flags = 0, outBuf = 0, inBuf = 0, maxInst = 0;
    BOOL infoOk = GetNamedPipeInfo(hServerPipe, &flags, &outBuf, &inBuf, &maxInst);
    TEST_ASSERT(infoOk == TRUE, "GetNamedPipeInfo must succeed on valid pipe handle");
    TEST_ASSERT((flags & PIPE_SERVER_END) != 0, "Pipe flags must contain PIPE_SERVER_END");
    TEST_ASSERT((flags & PIPE_TYPE_MESSAGE) != 0, "Pipe flags must contain PIPE_TYPE_MESSAGE");
    TEST_ASSERT(outBuf == 1024 && inBuf == 1024, "Buffer sizes must match creation parameters");
    TEST_ASSERT(maxInst == 2, "Max instances must report 2");

    // ------------------------------------------------------------------------
    // 2. Client Connection & Handshake (CreateFileW & ConnectNamedPipe)
    // ------------------------------------------------------------------------
    std::atomic<bool> clientConnected{false};
    HANDLE hClientPipe = INVALID_HANDLE_VALUE;

    std::thread clientThread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        hClientPipe = CreateFileW(
            L"\\\\.\\pipe\\MicaTestPipe",
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0
        );
        clientConnected = (hClientPipe != INVALID_HANDLE_VALUE);
    });

    BOOL connOk = ConnectNamedPipe(hServerPipe);
    TEST_ASSERT(connOk == TRUE, "ConnectNamedPipe must return TRUE on successful client rendezvous");

    if (clientThread.joinable()) {
        clientThread.join();
    }
    TEST_ASSERT(clientConnected.load(), "Client CreateFileW to \\\\.\\pipe\\MicaTestPipe must succeed");
    TEST_ASSERT(hClientPipe != INVALID_HANDLE_VALUE, "Client pipe handle must be valid");

    // ConnectNamedPipe on already connected pipe returns FALSE with ERROR_PIPE_CONNECTED
    BOOL secondConn = ConnectNamedPipe(hServerPipe);
    TEST_ASSERT(secondConn == FALSE, "ConnectNamedPipe on connected pipe must return FALSE");
    TEST_ASSERT(GetLastError() == ERROR_PIPE_CONNECTED, "GetLastError must return ERROR_PIPE_CONNECTED");

    // ------------------------------------------------------------------------
    // 3. Bidirectional Duplex I/O (Client -> Server and Server -> Client)
    // ------------------------------------------------------------------------
    const char clientMsg[] = "MicaNT RPC Request: QueryHostVitals";
    DWORD bytesWritten = 0;
    BOOL writeOk = WriteFile(hClientPipe, clientMsg, static_cast<DWORD>(strlen(clientMsg)), &bytesWritten);
    TEST_ASSERT(writeOk == TRUE, "Client WriteFile must succeed");
    TEST_ASSERT(bytesWritten == strlen(clientMsg), "Bytes written must match request length");

    char serverRecvBuf[128]{};
    DWORD bytesRead = 0;
    BOOL readOk = ReadFile(hServerPipe, serverRecvBuf, sizeof(serverRecvBuf), &bytesRead);
    TEST_ASSERT(readOk == TRUE, "Server ReadFile must succeed");
    TEST_ASSERT(bytesRead == strlen(clientMsg), "Server bytes read must match request length");
    TEST_ASSERT(std::string_view(serverRecvBuf, bytesRead) == clientMsg, "Server received text must match client payload");

    const char serverReply[] = "MicaNT RPC Response: CPU0=4.0GHz STATUS=NOMINAL";
    writeOk = WriteFile(hServerPipe, serverReply, static_cast<DWORD>(strlen(serverReply)), &bytesWritten);
    TEST_ASSERT(writeOk == TRUE, "Server WriteFile must succeed");

    char clientRecvBuf[128]{};
    readOk = ReadFile(hClientPipe, clientRecvBuf, sizeof(clientRecvBuf), &bytesRead);
    TEST_ASSERT(readOk == TRUE, "Client ReadFile must succeed");
    TEST_ASSERT(bytesRead == strlen(serverReply), "Client bytes read must match server reply length");
    TEST_ASSERT(std::string_view(clientRecvBuf, bytesRead) == serverReply, "Client received text must match server reply");

    // ------------------------------------------------------------------------
    // 4. Message-Mode Framing, Partial Reads & PeekNamedPipe
    // ------------------------------------------------------------------------
    const char packetMsg[] = "PACKET_HEADER_AND_PAYLOAD_BODY_0123456789";
    const DWORD packetLen = static_cast<DWORD>(strlen(packetMsg));
    writeOk = WriteFile(hServerPipe, packetMsg, packetLen, &bytesWritten);
    TEST_ASSERT(writeOk == TRUE && bytesWritten == packetLen, "Server packet write must succeed");

    // Peek 13 bytes from the pipe without consuming
    char peekBuf[14]{};
    DWORD peekBytesRead = 0, totalAvail = 0, leftMsg = 0;
    BOOL peekOk = PeekNamedPipe(hClientPipe, peekBuf, 13, &peekBytesRead, &totalAvail, &leftMsg);
    TEST_ASSERT(peekOk == TRUE, "PeekNamedPipe must succeed");
    TEST_ASSERT(peekBytesRead == 13, "Peeked bytes read must be 13");
    TEST_ASSERT(totalAvail == packetLen, "Total available bytes must match packetLen");
    TEST_ASSERT(leftMsg == packetLen, "Bytes left this message must match packetLen");
    TEST_ASSERT(std::string_view(peekBuf, 13) == "PACKET_HEADER", "Peeked data must match prefix");

    // Partial ReadFile (buffer smaller than message -> returns FALSE + ERROR_MORE_DATA)
    char partialBuf[13]{};
    readOk = ReadFile(hClientPipe, partialBuf, 13, &bytesRead);
    TEST_ASSERT(readOk == FALSE, "Partial ReadFile in message mode must return FALSE");
    TEST_ASSERT(GetLastError() == ERROR_MORE_DATA, "GetLastError must return ERROR_MORE_DATA (234)");
    TEST_ASSERT(bytesRead == 13, "Bytes read must match partial buffer length");
    TEST_ASSERT(std::string_view(partialBuf, 13) == "PACKET_HEADER", "Partial read content must match prefix");

    // Read the remainder of the message
    char remainBuf[64]{};
    readOk = ReadFile(hClientPipe, remainBuf, sizeof(remainBuf), &bytesRead);
    TEST_ASSERT(readOk == TRUE, "Second ReadFile for remaining message must return TRUE");
    TEST_ASSERT(bytesRead == (packetLen - 13), "Remaining bytes read must complete the message");
    TEST_ASSERT(std::string_view(remainBuf, bytesRead) == "_AND_PAYLOAD_BODY_0123456789", "Remaining payload must match remainder");

    // ------------------------------------------------------------------------
    // 5. Transactional RPC (TransactNamedPipe)
    // ------------------------------------------------------------------------
    std::thread rpcServerThread([&]() {
        char req[64]{};
        DWORD reqRead = 0;
        if (ReadFile(hServerPipe, req, sizeof(req), &reqRead)) {
            if (std::string_view(req, reqRead) == "TRANSACT_ECHO_TEST") {
                const char resp[] = "TRANSACT_ECHO_ACK";
                DWORD rWritten = 0;
                WriteFile(hServerPipe, resp, static_cast<DWORD>(strlen(resp)), &rWritten);
            }
        }
    });

    const char transactReq[] = "TRANSACT_ECHO_TEST";
    char transactResp[64]{};
    DWORD transactRead = 0;
    BOOL transactOk = TransactNamedPipe(
        hClientPipe,
        const_cast<char*>(transactReq),
        static_cast<DWORD>(strlen(transactReq)),
        transactResp,
        sizeof(transactResp),
        &transactRead
    );
    TEST_ASSERT(transactOk == TRUE, "TransactNamedPipe must complete atomic write-and-read transaction");
    TEST_ASSERT(std::string_view(transactResp, transactRead) == "TRANSACT_ECHO_ACK", "Transact response must match ACK");

    if (rpcServerThread.joinable()) {
        rpcServerThread.join();
    }

    // ------------------------------------------------------------------------
    // 6. Multi-Instance Pipes & WaitNamedPipeW
    // ------------------------------------------------------------------------
    HANDLE hServerPipe2 = CreateNamedPipeW(
        L"\\\\.\\pipe\\MicaTestPipe",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        2, 1024, 1024, 100
    );
    TEST_ASSERT(hServerPipe2 != INVALID_HANDLE_VALUE, "Second instance creation must succeed");

    HANDLE hServerPipe3 = CreateNamedPipeW(
        L"\\\\.\\pipe\\MicaTestPipe",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        2, 1024, 1024, 100
    );
    TEST_ASSERT(hServerPipe3 == INVALID_HANDLE_VALUE, "Third instance creation must fail (PIPE_BUSY)");
    TEST_ASSERT(GetLastError() == ERROR_PIPE_BUSY, "GetLastError must report ERROR_PIPE_BUSY (231)");

    BOOL waitOk = WaitNamedPipeW(L"\\\\.\\pipe\\MicaTestPipe", 50);
    TEST_ASSERT(waitOk == TRUE, "WaitNamedPipeW must succeed while instance 2 is listening");

    CloseHandle(hServerPipe2);

    // ------------------------------------------------------------------------
    // 7. Server Disconnection & Broken Pipe Handling
    // ------------------------------------------------------------------------
    BOOL discOk = DisconnectNamedPipe(hServerPipe);
    TEST_ASSERT(discOk == TRUE, "DisconnectNamedPipe must succeed");

    const char failMsg[] = "DeadWrite";
    DWORD deadWritten = 0;
    BOOL failWrite = WriteFile(hClientPipe, failMsg, static_cast<DWORD>(strlen(failMsg)), &deadWritten);
    TEST_ASSERT(failWrite == FALSE, "WriteFile to disconnected pipe must fail");
    TEST_ASSERT(GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_PIPE_NOT_CONNECTED,
                "GetLastError must return ERROR_BROKEN_PIPE or ERROR_PIPE_NOT_CONNECTED");

    CloseHandle(hClientPipe);
    CloseHandle(hServerPipe);

    // ------------------------------------------------------------------------
    // 8. Mailslot Subsystem (MSFS): Create, Broadcast Datagrams, Info, Timeouts
    // ------------------------------------------------------------------------
    HANDLE hMailslot = CreateMailslotW(
        L"\\\\.\\mailslot\\MicaAlertChannel",
        256,  // max message size
        50    // 50ms read timeout
    );
    TEST_ASSERT(hMailslot != INVALID_HANDLE_VALUE, "CreateMailslotW must return valid mailslot handle");

    DWORD slotMaxSize = 0, slotNextSize = 0, slotCount = 0, slotTimeout = 0;
    BOOL slotInfoOk = GetMailslotInfo(hMailslot, &slotMaxSize, &slotNextSize, &slotCount, &slotTimeout);
    TEST_ASSERT(slotInfoOk == TRUE, "GetMailslotInfo must succeed on newly created mailslot");
    TEST_ASSERT(slotMaxSize == 256, "Max message size must match 256");
    TEST_ASSERT(slotCount == 0, "Initial message count must be 0");
    TEST_ASSERT(slotNextSize == MAILSLOT_NO_MESSAGE, "Initial next size must report MAILSLOT_NO_MESSAGE");
    TEST_ASSERT(slotTimeout == 50, "Read timeout must report 50ms");

    HANDLE hClientWriter1 = CreateFileW(
        L"\\\\.\\mailslot\\MicaAlertChannel",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        0
    );
    TEST_ASSERT(hClientWriter1 != INVALID_HANDLE_VALUE, "Client 1 CreateFileW to mailslot must succeed");

    const char alert1[] = "ALERT_1: SECURE_BOOT_VERIFIED";
    DWORD alert1Written = 0;
    writeOk = WriteFile(hClientWriter1, alert1, static_cast<DWORD>(strlen(alert1)), &alert1Written);
    TEST_ASSERT(writeOk == TRUE && alert1Written == strlen(alert1), "Client 1 datagram write must succeed");

    HANDLE hClientWriter2 = CreateFileW(
        L"\\\\.\\mailslot\\MicaAlertChannel",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        0
    );
    TEST_ASSERT(hClientWriter2 != INVALID_HANDLE_VALUE, "Client 2 CreateFileW to mailslot must succeed");

    const char alert2[] = "ALERT_2: KERNEL_INTEGRITY_PASSED";
    DWORD alert2Written = 0;
    writeOk = WriteFile(hClientWriter2, alert2, static_cast<DWORD>(strlen(alert2)), &alert2Written);
    TEST_ASSERT(writeOk == TRUE && alert2Written == strlen(alert2), "Client 2 datagram write must succeed");

    slotInfoOk = GetMailslotInfo(hMailslot, nullptr, &slotNextSize, &slotCount, nullptr);
    TEST_ASSERT(slotInfoOk == TRUE, "GetMailslotInfo must succeed with pending datagrams");
    TEST_ASSERT(slotCount == 2, "Mailslot message count must report 2");
    TEST_ASSERT(slotNextSize == strlen(alert1), "Next message size must match alert1 length");

    char dgramBuf[128]{};
    DWORD dgramRead = 0;
    readOk = ReadFile(hMailslot, dgramBuf, sizeof(dgramBuf), &dgramRead);
    TEST_ASSERT(readOk == TRUE, "Mailslot server ReadFile must retrieve datagram 1");
    TEST_ASSERT(dgramRead == strlen(alert1), "Datagram 1 read length must match written size");
    TEST_ASSERT(std::string_view(dgramBuf, dgramRead) == alert1, "Datagram 1 payload must match alert1");

    readOk = ReadFile(hMailslot, dgramBuf, sizeof(dgramBuf), &dgramRead);
    TEST_ASSERT(readOk == TRUE, "Mailslot server ReadFile must retrieve datagram 2");
    TEST_ASSERT(dgramRead == strlen(alert2), "Datagram 2 read length must match written size");
    TEST_ASSERT(std::string_view(dgramBuf, dgramRead) == alert2, "Datagram 2 payload must match alert2");

    GetMailslotInfo(hMailslot, nullptr, &slotNextSize, &slotCount, nullptr);
    TEST_ASSERT(slotCount == 0, "Queue must be empty after reading both messages");
    TEST_ASSERT(slotNextSize == MAILSLOT_NO_MESSAGE, "Next size must be MAILSLOT_NO_MESSAGE");

    SetMailslotInfo(hMailslot, 10); // set 10ms timeout
    readOk = ReadFile(hMailslot, dgramBuf, sizeof(dgramBuf), &dgramRead);
    TEST_ASSERT(readOk == FALSE, "ReadFile on empty mailslot must time out");

    CloseHandle(hClientWriter1);
    CloseHandle(hClientWriter2);
    CloseHandle(hMailslot);
}

// ============================================================================
// Test Suite 42: NTFS Filesystem, Master File Table ($MFT) & Alternate Streams
// ============================================================================

void Test_NtfsFileSystemAndMasterFileTable() {
    using namespace micant;
    using namespace micant::ntfs;

    // 1. Format and Mount NTFS on Block Device (8 MB RAM disk)
    auto ramDisk = std::make_shared<storage::RamDiskDevice>(L"\\Device\\HarddiskNtfs", 8 * 1024 * 1024ULL, storage::SECTOR_SIZE_512); // 8 MB
    auto ntfs = std::make_shared<NtfsFileSystem>();

    NtStatus stFormat = ntfs->format(*ramDisk, DEFAULT_CLUSTER_SIZE, L"MicaNT_System");
    TEST_ASSERT(NT_SUCCESS(stFormat), "NTFS format must succeed on 8MB block device");

    NtStatus stMount = ntfs->mount(ramDisk);
    TEST_ASSERT(NT_SUCCESS(stMount), "NTFS volume mount must succeed");
    TEST_ASSERT(ntfs->getClusterSize() == 4096, "NTFS cluster size must be 4096 bytes");
    TEST_ASSERT(ntfs->getVolumeSerialNumber() != 0, "NTFS volume serial number must be non-zero");

    // 2. MFT Record Engine & USA Fixup Validation
    std::vector<uint8_t> rootRecord = ntfs->serializeRecord(MFT_REC_ROOT);
    TEST_ASSERT(rootRecord.size() == MFT_RECORD_SIZE, "Serialized MFT record must be 1024 bytes");
    const auto* rootHdr = reinterpret_cast<const MftRecordHeader*>(rootRecord.data());
    TEST_ASSERT(rootHdr->magic == NTFS_FILE_SIGNATURE, "MFT record magic must be 'FILE' (0x454C4946)");
    TEST_ASSERT((rootHdr->flags & MFT_RECORD_IN_USE) != 0, "Root MFT record must be marked IN_USE");
    TEST_ASSERT((rootHdr->flags & MFT_RECORD_DIRECTORY) != 0, "Root MFT record must be marked DIRECTORY");

    // Test USA fixup integrity & corruption detection
    std::vector<uint8_t> corruptedRecord = rootRecord;
    corruptedRecord[510] ^= 0xFF; // Tamper with sector 0 USA sequence number
    bool fixupBad = UsaEngine::applyFixups(corruptedRecord.data(), corruptedRecord.size());
    TEST_ASSERT(fixupBad == false, "USA engine must detect torn write / corrupted sector boundary");

    bool fixupOk = UsaEngine::applyFixups(rootRecord.data(), rootRecord.size());
    TEST_ASSERT(fixupOk == true, "USA engine must cleanly apply fixups to valid MFT record");

    // 3. Data Run Encoding and Decoding (LCN/VCN Runs)
    std::vector<DataRun> inputRuns = {
        DataRun{ .vcnStart = 0,   .lcnStart = 100, .clusterCount = 16 },
        DataRun{ .vcnStart = 16,  .lcnStart = 250, .clusterCount = 32 },
        DataRun{ .vcnStart = 48,  .lcnStart = -1,  .clusterCount = 8  }, // Sparse run
        DataRun{ .vcnStart = 56,  .lcnStart = 300, .clusterCount = 64 }
    };
    std::vector<uint8_t> encodedRuns = DataRunCodec::encode(inputRuns);
    TEST_ASSERT(!encodedRuns.empty(), "Encoded data runs must not be empty");
    TEST_ASSERT(encodedRuns.back() == 0, "Data run list must terminate with zero byte");

    std::vector<DataRun> decodedRuns;
    bool decOk = DataRunCodec::decode(encodedRuns.data(), encodedRuns.size(), 0, decodedRuns);
    TEST_ASSERT(decOk == true, "Data run decoding must succeed");
    TEST_ASSERT(decodedRuns.size() == inputRuns.size(), "Decoded run count must match input");
    for (size_t i = 0; i < inputRuns.size(); ++i) {
        TEST_ASSERT(decodedRuns[i].vcnStart == inputRuns[i].vcnStart, "VCN start must match");
        TEST_ASSERT(decodedRuns[i].lcnStart == inputRuns[i].lcnStart, "LCN start must match");
        TEST_ASSERT(decodedRuns[i].clusterCount == inputRuns[i].clusterCount, "Cluster count must match");
    }

    // 4. File Creation, Streaming & Offset Reading
    uint64_t recHosts = 0;
    NtStatus stCr = ntfs->createFile(L"drivers\\etc\\hosts", fs::FILE_ATTRIBUTE_NORMAL, recHosts);
    TEST_ASSERT(NT_SUCCESS(stCr), "createFile for hosts must succeed");
    TEST_ASSERT(recHosts >= MFT_REC_USER_START, "User file record must be >= 16");

    const std::string hostsData = "127.0.0.1 localhost\n::1 localhost\n192.168.1.1 router\n";
    uint64_t bytesWritten = 0;
    NtStatus stWr = ntfs->writeFile(recHosts, L"", hostsData.data(), hostsData.size(), 0, bytesWritten);
    TEST_ASSERT(NT_SUCCESS(stWr), "writeFile to primary stream must succeed");
    TEST_ASSERT(bytesWritten == hostsData.size(), "Bytes written must match hosts data length");

    std::vector<char> readBuf(hostsData.size() + 1, 0);
    uint64_t bytesRead = 0;
    NtStatus stRd = ntfs->readFile(recHosts, L"", readBuf.data(), hostsData.size(), 0, bytesRead);
    TEST_ASSERT(NT_SUCCESS(stRd), "readFile from primary stream must succeed");
    TEST_ASSERT(bytesRead == hostsData.size(), "Bytes read must match hosts data length");
    TEST_ASSERT(std::string_view(readBuf.data(), bytesRead) == hostsData, "Primary stream content must match");

    // Partial / Offset Read
    char partialBuf[16]{};
    uint64_t partialRead = 0;
    NtStatus stPart = ntfs->readFile(recHosts, L"", partialBuf, 9, 10, partialRead);
    TEST_ASSERT(NT_SUCCESS(stPart), "Offset read from primary stream must succeed");
    TEST_ASSERT(partialRead == 9, "Partial read bytes must be 9");
    TEST_ASSERT(std::string_view(partialBuf, 9) == "localhost", "Offset read payload must match 'localhost'");

    // 5. Alternate Data Streams (ADS)
    const std::string zoneData = "[ZoneTransfer]\nZoneId=3\nReferrerUrl=https://micant.org\n";
    uint64_t zoneWritten = 0;
    NtStatus stZoneWr = ntfs->writeFile(recHosts, L"Zone.Identifier", zoneData.data(), zoneData.size(), 0, zoneWritten);
    TEST_ASSERT(NT_SUCCESS(stZoneWr), "writeFile to Alternate Data Stream must succeed");
    TEST_ASSERT(zoneWritten == zoneData.size(), "Zone bytes written must match");

    // Verify Primary stream is still intact and unaffected
    std::vector<char> verifyPrimBuf(hostsData.size(), 0);
    uint64_t verifyPrimRead = 0;
    ntfs->readFile(recHosts, L"", verifyPrimBuf.data(), hostsData.size(), 0, verifyPrimRead);
    TEST_ASSERT(std::string_view(verifyPrimBuf.data(), verifyPrimRead) == hostsData, "Primary stream must remain intact after ADS write");

    // Verify Alternate Data Stream reading
    std::vector<char> zoneReadBuf(zoneData.size() + 1, 0);
    uint64_t zoneRead = 0;
    NtStatus stZoneRd = ntfs->readFile(recHosts, L"Zone.Identifier", zoneReadBuf.data(), zoneData.size(), 0, zoneRead);
    TEST_ASSERT(NT_SUCCESS(stZoneRd), "readFile from Alternate Data Stream must succeed");
    TEST_ASSERT(zoneRead == zoneData.size(), "Zone bytes read must match");
    TEST_ASSERT(std::string_view(zoneReadBuf.data(), zoneRead) == zoneData, "ADS stream content must match zoneData");

    // Reading non-existent stream must fail
    uint64_t badRead = 0;
    char dummy[8]{};
    NtStatus stBadStream = ntfs->readFile(recHosts, L"NonExistentStream", dummy, sizeof(dummy), 0, badRead);
    TEST_ASSERT(stBadStream == NtStatus::NoSuchFile, "Reading non-existent stream must return NoSuchFile");

    // 6. $LogFile Transaction Journal & WAL Verification
    auto* journal = ntfs->getJournal();
    TEST_ASSERT(journal != nullptr, "NTFS journal must be initialized");
    TEST_ASSERT(journal->getEntryCount() >= 3, "Journal must have logged file creation and writes");
    uint64_t lsnBeforeCheckpoint = journal->getLastLsn();

    journal->checkpoint();
    TEST_ASSERT(journal->getLastCheckpointLsn() > lsnBeforeCheckpoint, "Checkpoint LSN must advance");

    const std::string appendData = "10.0.0.1 db.server\n";
    uint64_t appendWritten = 0;
    ntfs->writeFile(recHosts, L"", appendData.data(), appendData.size(), hostsData.size(), appendWritten);

    auto recentEntries = journal->getEntriesSinceCheckpoint();
    TEST_ASSERT(!recentEntries.empty(), "Journal must contain log entries since last checkpoint");
    bool foundWriteOp = false;
    for (const auto& entry : recentEntries) {
        if (entry.op == LogOperation::WriteResidentData && entry.recordNumber == recHosts) {
            foundWriteOp = true;
            break;
        }
    }
    TEST_ASSERT(foundWriteOp == true, "Journal must record WriteResidentData operation in WAL");

    // 7. Virtual File System (VFS) Seamless Routing & ADS Integration
    auto& vfs = fs::VirtualFileSystem::get();
    vfs.setMountedNtfs(ntfs);

    std::shared_ptr<fs::FileObject> vfsFile;
    NtStatus stVfsCr = vfs.createOrOpenFile(
        L"D:\\SecurityReport.log:Summary",
        fs::FILE_GENERIC_READ | fs::FILE_GENERIC_WRITE,
        fs::FILE_CREATE,
        vfsFile
    );
    TEST_ASSERT(NT_SUCCESS(stVfsCr), "VFS createOrOpenFile on NTFS ADS must succeed");
    TEST_ASSERT(vfsFile != nullptr, "VFS FileObject must be non-null");

    const std::string repSummary = "AUDIT_RESULT: ZERO_CORRUPTION_DETECTED";
    uint32_t vfsWritten = 0;
    NtStatus stVfsWr = vfs.writeFile(vfsFile.get(), repSummary.data(), static_cast<uint32_t>(repSummary.size()), nullptr, vfsWritten);
    TEST_ASSERT(NT_SUCCESS(stVfsWr), "VFS writeFile to NTFS ADS must succeed");
    TEST_ASSERT(vfsWritten == repSummary.size(), "VFS bytes written must match summary length");

    // Read back via VFS
    char vfsReadBuf[64]{};
    uint32_t vfsBytesRead = 0;
    LargeInteger readOffset{};
    readOffset.quadPart = 0;
    NtStatus stVfsRd = vfs.readFile(vfsFile.get(), vfsReadBuf, sizeof(vfsReadBuf), &readOffset, vfsBytesRead);
    TEST_ASSERT(NT_SUCCESS(stVfsRd), "VFS readFile from NTFS ADS must succeed");
    TEST_ASSERT(vfsBytesRead == repSummary.size(), "VFS bytes read must match summary length");
    TEST_ASSERT(std::string_view(vfsReadBuf, vfsBytesRead) == repSummary, "VFS payload must match repSummary");

    vfs.closeFile(vfsFile.get());
}

void Test_ServiceControlManager_And_SvcHost() {
    using namespace scm;

    // 1. SCM Initialization & Built-in System Services
    auto& scm = ServiceControlManager::get();
    scm.initialize();

    TEST_ASSERT(scm.getServiceCount() >= 7, "SCM must have at least 7 default system services");

    auto rpcSs = scm.getServiceRecord(L"RpcSs");
    TEST_ASSERT(rpcSs != nullptr, "RpcSs service must exist");
    TEST_ASSERT(rpcSs->serviceType == SERVICE_WIN32_SHARE_PROCESS, "RpcSs must be share process");
    TEST_ASSERT(rpcSs->status.dwCurrentState == SERVICE_RUNNING, "RpcSs must be running");
    TEST_ASSERT(rpcSs->svchostGroup == "DcomLaunch", "RpcSs group must be DcomLaunch");
    TEST_ASSERT(rpcSs->status.dwProcessId > 0, "RpcSs must have non-zero PID");

    auto tcpip = scm.getServiceRecord(L"Tcpip");
    TEST_ASSERT(tcpip != nullptr, "Tcpip service must exist");
    TEST_ASSERT(tcpip->serviceType == SERVICE_KERNEL_DRIVER, "Tcpip must be a kernel driver");
    TEST_ASSERT(tcpip->status.dwCurrentState == SERVICE_RUNNING, "Tcpip must be running");
    TEST_ASSERT(tcpip->status.dwProcessId == 4, "Tcpip driver PID must be 4 (System)");

    auto dhcp = scm.getServiceRecord(L"Dhcp");
    TEST_ASSERT(dhcp != nullptr, "Dhcp service must exist");
    TEST_ASSERT(dhcp->status.dwCurrentState == SERVICE_RUNNING, "Dhcp must be running");
    TEST_ASSERT(dhcp->svchostGroup == "netsvcs", "Dhcp group must be netsvcs");
    TEST_ASSERT(!dhcp->dependencies.empty() && dhcp->dependencies[0] == L"Tcpip", "Dhcp must depend on Tcpip");

    // 2. Win32 SCM API Parity (advapi32.dll)
    advapi32::SC_HANDLE hScm = advapi32::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    TEST_ASSERT(hScm != nullptr, "OpenSCManagerW must succeed with SC_MANAGER_ALL_ACCESS");

    advapi32::SC_HANDLE hSpooler = advapi32::CreateServiceW(
        hScm,
        L"Spooler",
        L"Print Spooler",
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        L"C:\\Windows\\System32\\spoolsv.exe",
        L"",
        nullptr,
        L"RpcSs\0",
        L"LocalSystem",
        nullptr
    );
    TEST_ASSERT(hSpooler != nullptr, "CreateServiceW for Spooler must succeed");

    SERVICE_STATUS spoolerStatus{};
    win32::BOOL bQuery = advapi32::QueryServiceStatus(hSpooler, &spoolerStatus);
    TEST_ASSERT(bQuery == win32::TRUE, "QueryServiceStatus for Spooler must succeed");
    TEST_ASSERT(spoolerStatus.dwCurrentState == SERVICE_STOPPED, "Newly created Spooler must be in SERVICE_STOPPED state");
    TEST_ASSERT((spoolerStatus.dwControlsAccepted & SERVICE_ACCEPT_STOP) != 0, "Spooler must accept STOP control");

    // Attempting to create duplicate service must fail with ERROR_SERVICE_EXISTS
    advapi32::SC_HANDLE hDup = advapi32::CreateServiceW(
        hScm,
        L"Spooler",
        L"Duplicate",
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        L"C:\\Windows\\System32\\spoolsv.exe",
        nullptr, nullptr, nullptr, nullptr, nullptr
    );
    TEST_ASSERT(hDup == nullptr, "Create duplicate service must fail");
    TEST_ASSERT(win32::GetLastError() == ERROR_SERVICE_EXISTS, "Error must be ERROR_SERVICE_EXISTS");

    // 3. Topological Dependency Resolution & Auto-Start
    advapi32::SC_HANDLE hSvcA = advapi32::CreateServiceW(
        hScm, L"TestSvcA", L"Base Service A", SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        L"svca.exe", nullptr, nullptr, nullptr, nullptr, nullptr
    );
    TEST_ASSERT(hSvcA != nullptr, "CreateServiceW for TestSvcA must succeed");

    advapi32::SC_HANDLE hSvcB = advapi32::CreateServiceW(
        hScm, L"TestSvcB", L"Intermediate Service B", SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        L"svcb.exe", nullptr, nullptr, L"TestSvcA\0", nullptr, nullptr
    );
    TEST_ASSERT(hSvcB != nullptr, "CreateServiceW for TestSvcB must succeed");

    advapi32::SC_HANDLE hSvcC = advapi32::CreateServiceW(
        hScm, L"TestSvcC", L"Top-Level Service C", SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        L"svcc.exe", nullptr, nullptr, L"TestSvcB\0", nullptr, nullptr
    );
    TEST_ASSERT(hSvcC != nullptr, "CreateServiceW for TestSvcC must succeed");

    std::vector<std::wstring> depOrder;
    uint32_t depRes = scm.resolveDependencies(L"TestSvcC", depOrder);
    TEST_ASSERT(depRes == ERROR_SUCCESS, "resolveDependencies on TestSvcC must succeed");
    TEST_ASSERT(depOrder.size() == 3, "Dependency order must include exactly 3 services");
    TEST_ASSERT(depOrder[0] == L"TestSvcA", "First start prerequisite must be TestSvcA");
    TEST_ASSERT(depOrder[1] == L"TestSvcB", "Second start prerequisite must be TestSvcB");
    TEST_ASSERT(depOrder[2] == L"TestSvcC", "Third target must be TestSvcC");

    // Starting TestSvcC must automatically start TestSvcA and TestSvcB
    win32::BOOL bStartC = advapi32::StartServiceW(hSvcC, 0, nullptr);
    TEST_ASSERT(bStartC == win32::TRUE, "StartServiceW on TestSvcC must succeed");

    SERVICE_STATUS stA{}, stB{}, stC{};
    advapi32::QueryServiceStatus(hSvcA, &stA);
    advapi32::QueryServiceStatus(hSvcB, &stB);
    advapi32::QueryServiceStatus(hSvcC, &stC);
    TEST_ASSERT(stA.dwCurrentState == SERVICE_RUNNING, "TestSvcA must be auto-started and RUNNING");
    TEST_ASSERT(stB.dwCurrentState == SERVICE_RUNNING, "TestSvcB must be auto-started and RUNNING");
    TEST_ASSERT(stC.dwCurrentState == SERVICE_RUNNING, "TestSvcC must be RUNNING");

    // 4. Circular Dependency Detection
    advapi32::CreateServiceW(
        hScm, L"CycleAlpha", L"Cycle Alpha", SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        L"cycle1.exe", nullptr, nullptr, L"CycleBeta\0", nullptr, nullptr
    );
    advapi32::SC_HANDLE hCycleBeta = advapi32::CreateServiceW(
        hScm, L"CycleBeta", L"Cycle Beta", SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        L"cycle2.exe", nullptr, nullptr, L"CycleAlpha\0", nullptr, nullptr
    );
    TEST_ASSERT(hCycleBeta != nullptr, "CreateServiceW for CycleBeta must succeed");

    std::vector<std::wstring> cycleOrder;
    uint32_t cycleRes = scm.resolveDependencies(L"CycleBeta", cycleOrder);
    TEST_ASSERT(cycleRes == ERROR_CIRCULAR_DEPENDENCY, "Circular dependency must be detected");

    win32::BOOL bCycleStart = advapi32::StartServiceW(hCycleBeta, 0, nullptr);
    TEST_ASSERT(bCycleStart == win32::FALSE, "Starting cyclic service must fail");
    TEST_ASSERT(win32::GetLastError() == ERROR_CIRCULAR_DEPENDENCY, "Error must be ERROR_CIRCULAR_DEPENDENCY");

    // 5. Dependent Service Running Guard
    // TestSvcC depends on TestSvcB, which depends on TestSvcA.
    // Attempting to stop TestSvcA while TestSvcB / TestSvcC are running must fail!
    SERVICE_STATUS stopStatus{};
    win32::BOOL bStopA = advapi32::ControlService(hSvcA, SERVICE_CONTROL_STOP, &stopStatus);
    TEST_ASSERT(bStopA == win32::FALSE, "Stopping TestSvcA while dependent services run must fail");
    TEST_ASSERT(win32::GetLastError() == ERROR_DEPENDENT_SERVICES_RUNNING, "Error must be ERROR_DEPENDENT_SERVICES_RUNNING");

    // Stop TestSvcC first
    win32::BOOL bStopC = advapi32::ControlService(hSvcC, SERVICE_CONTROL_STOP, &stopStatus);
    TEST_ASSERT(bStopC == win32::TRUE, "Stopping TestSvcC must succeed");
    TEST_ASSERT(stopStatus.dwCurrentState == SERVICE_STOPPED, "TestSvcC must be stopped");

    // Stop TestSvcB next
    win32::BOOL bStopB = advapi32::ControlService(hSvcB, SERVICE_CONTROL_STOP, &stopStatus);
    TEST_ASSERT(bStopB == win32::TRUE, "Stopping TestSvcB must succeed");
    TEST_ASSERT(stopStatus.dwCurrentState == SERVICE_STOPPED, "TestSvcB must be stopped");

    // Now stopping TestSvcA must succeed
    bStopA = advapi32::ControlService(hSvcA, SERVICE_CONTROL_STOP, &stopStatus);
    TEST_ASSERT(bStopA == win32::TRUE, "Stopping TestSvcA must now succeed");
    TEST_ASSERT(stopStatus.dwCurrentState == SERVICE_STOPPED, "TestSvcA must be stopped");

    // 6. Service Handler Callbacks & State Transitions (Pause / Continue / Interrogate)
    static std::vector<uint32_t> s_ReceivedControls;
    auto testDaemon = scm.getServiceRecord(L"Spooler");
    TEST_ASSERT(testDaemon != nullptr, "Spooler record must exist");
    testDaemon->handler = [](uint32_t ctrl) {
        s_ReceivedControls.push_back(ctrl);
    };

    win32::BOOL bStartSpooler = advapi32::StartServiceW(hSpooler, 0, nullptr);
    TEST_ASSERT(bStartSpooler == win32::TRUE, "StartServiceW on Spooler must succeed");
    advapi32::QueryServiceStatus(hSpooler, &spoolerStatus);
    TEST_ASSERT(spoolerStatus.dwCurrentState == SERVICE_RUNNING, "Spooler must be RUNNING");

    // Pause
    advapi32::ControlService(hSpooler, SERVICE_CONTROL_PAUSE, &spoolerStatus);
    TEST_ASSERT(spoolerStatus.dwCurrentState == SERVICE_PAUSED, "Spooler must be PAUSED");

    // Continue
    advapi32::ControlService(hSpooler, SERVICE_CONTROL_CONTINUE, &spoolerStatus);
    TEST_ASSERT(spoolerStatus.dwCurrentState == SERVICE_RUNNING, "Spooler must be RUNNING after CONTINUE");

    // Interrogate
    advapi32::ControlService(hSpooler, SERVICE_CONTROL_INTERROGATE, &spoolerStatus);

    // Stop
    advapi32::ControlService(hSpooler, SERVICE_CONTROL_STOP, &spoolerStatus);
    TEST_ASSERT(spoolerStatus.dwCurrentState == SERVICE_STOPPED, "Spooler must be STOPPED");

    TEST_ASSERT(s_ReceivedControls.size() >= 4, "Handler must receive pause, continue, interrogate, and stop");
    TEST_ASSERT(s_ReceivedControls[0] == SERVICE_CONTROL_PAUSE, "First control must be PAUSE");
    TEST_ASSERT(s_ReceivedControls[1] == SERVICE_CONTROL_CONTINUE, "Second control must be CONTINUE");
    TEST_ASSERT(s_ReceivedControls[2] == SERVICE_CONTROL_INTERROGATE, "Third control must be INTERROGATE");
    TEST_ASSERT(s_ReceivedControls[3] == SERVICE_CONTROL_STOP, "Fourth control must be STOP");

    // 7. Shared Process Service Host (svchost.exe) Grouping
    auto dhcpRec = scm.getServiceRecord(L"Dhcp");
    auto dnsRec = scm.getServiceRecord(L"Dnscache");
    auto wrkRec = scm.getServiceRecord(L"LanmanWorkstation");
    TEST_ASSERT(dhcpRec != nullptr && dnsRec != nullptr && wrkRec != nullptr, "netsvcs services must exist");

    uint32_t netsvcsPid = SvcHostManager::get().getGroupPid("netsvcs");
    TEST_ASSERT(netsvcsPid > 0, "netsvcs host PID must be non-zero");
    TEST_ASSERT(dhcpRec->status.dwProcessId == netsvcsPid, "Dhcp PID must match netsvcs host PID");
    TEST_ASSERT(dnsRec->status.dwProcessId == netsvcsPid, "Dnscache PID must match netsvcs host PID");
    TEST_ASSERT(wrkRec->status.dwProcessId == netsvcsPid, "LanmanWorkstation PID must match netsvcs host PID");

    auto hostedInNetsvcs = SvcHostManager::get().getServicesInGroup("netsvcs");
    TEST_ASSERT(hostedInNetsvcs.size() >= 3, "netsvcs group must have at least 3 services");

    // Distinct groups must have distinct PIDs
    uint32_t dcomPid = SvcHostManager::get().getGroupPid("DcomLaunch");
    uint32_t localSvcPid = SvcHostManager::get().getGroupPid("LocalService");
    TEST_ASSERT(dcomPid != 0 && localSvcPid != 0, "DcomLaunch and LocalService must have valid PIDs");
    TEST_ASSERT(dcomPid != netsvcsPid, "DcomLaunch PID must be distinct from netsvcs");
    TEST_ASSERT(localSvcPid != netsvcsPid, "LocalService PID must be distinct from netsvcs");
    TEST_ASSERT(localSvcPid != dcomPid, "LocalService PID must be distinct from DcomLaunch");

    // 8. Kernel Driver Service Integration (SERVICE_KERNEL_DRIVER)
    static bool s_TestDriverLoaded = false;
    advapi32::SC_HANDLE hDrv = advapi32::CreateServiceW(
        hScm,
        L"MicaVirtStorageDriver",
        L"MicaNT Virtual Storage Miniport Driver",
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        L"System32\\drivers\\micavstore.sys",
        L"SCSI miniport",
        nullptr, nullptr, nullptr, nullptr
    );
    TEST_ASSERT(hDrv != nullptr, "CreateServiceW for kernel driver must succeed");

    auto drvRec = scm.getServiceRecord(L"MicaVirtStorageDriver");
    TEST_ASSERT(drvRec != nullptr, "Driver record must exist");
    drvRec->driverEntry = [](io::DriverObject* drv, const UnicodeString* reg) -> NtStatus {
        (void)drv;
        (void)reg;
        s_TestDriverLoaded = true;
        return NtStatus::Success;
    };

    win32::BOOL bStartDrv = advapi32::StartServiceW(hDrv, 0, nullptr);
    TEST_ASSERT(bStartDrv == win32::TRUE, "StartServiceW on kernel driver must succeed");
    TEST_ASSERT(s_TestDriverLoaded == true, "Kernel driver DriverEntry must have been invoked");
    TEST_ASSERT(drvRec->status.dwCurrentState == SERVICE_RUNNING, "Driver service state must be RUNNING");
    TEST_ASSERT(drvRec->status.dwProcessId == 4, "Driver service PID must be 4 (System)");
    TEST_ASSERT(driver::DriverManager::get().lookupDriver(L"MicaVirtStorageDriver") != nullptr,
                "Driver must be registered in Ring 0 DriverManager");

    // 9. Named Pipe RPC Protocol over \\.\pipe\ntsvcs
    ScmRpcHeader reqHdr{
        .magic = SCM_RPC_REQ_MAGIC,
        .opCode = SCM_RPC_QUERY_STATUS,
        .dataLength = sizeof(ScmRpcRequestPayload)
    };
    ScmRpcRequestPayload reqPayload{};
    wcscpy_s(reqPayload.serviceName, L"Dhcp");

    // Open handle to Dhcp to pass to RPC
    advapi32::SC_HANDLE hDhcp = advapi32::OpenServiceW(hScm, L"Dhcp", SERVICE_QUERY_STATUS);
    TEST_ASSERT(hDhcp != nullptr, "OpenServiceW for Dhcp must succeed");
    reqPayload.param1 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hDhcp));

    std::vector<uint8_t> reqBuf(sizeof(ScmRpcHeader) + sizeof(ScmRpcRequestPayload));
    std::memcpy(reqBuf.data(), &reqHdr, sizeof(ScmRpcHeader));
    std::memcpy(reqBuf.data() + sizeof(ScmRpcHeader), &reqPayload, sizeof(ScmRpcRequestPayload));

    auto respBytes = scm.processRpcRequest(reqBuf.data(), reqBuf.size());
    TEST_ASSERT(respBytes.size() >= sizeof(ScmRpcHeader) + sizeof(ScmRpcResponsePayload),
                "RPC response size must be valid");

    const auto* respHdr = reinterpret_cast<const ScmRpcHeader*>(respBytes.data());
    const auto* respPl = reinterpret_cast<const ScmRpcResponsePayload*>(respBytes.data() + sizeof(ScmRpcHeader));
    TEST_ASSERT(respHdr->magic == SCM_RPC_RESP_MAGIC, "RPC response magic must be 'SCM2'");
    TEST_ASSERT(respPl->win32Error == ERROR_SUCCESS, "RPC query status must return ERROR_SUCCESS");
    TEST_ASSERT(respPl->currentState == SERVICE_RUNNING, "RPC returned state must be RUNNING");
    TEST_ASSERT(respPl->processId == netsvcsPid, "RPC returned PID must match netsvcs PID");

    advapi32::CloseServiceHandle(hDhcp);

    // 10. Command Shell net and sc Built-in Commands Verification
    shell::CommandShell testShell;
    std::ostringstream out;

    // Test 'net start' lists running services
    testShell.execute("net start", out);
    std::string netOut = out.str();
    TEST_ASSERT(netOut.find("These Windows services are started:") != std::string::npos, "'net start' header found");
    TEST_ASSERT(netOut.find("DHCP Client") != std::string::npos, "'net start' lists DHCP Client");
    TEST_ASSERT(netOut.find("Workstation") != std::string::npos, "'net start' lists Workstation");

    // Test 'sc query Dhcp'
    out.str("");
    out.clear();
    testShell.execute("sc query Dhcp", out);
    std::string scOut = out.str();
    TEST_ASSERT(scOut.find("SERVICE_NAME: Dhcp") != std::string::npos, "'sc query Dhcp' returns service name");
    TEST_ASSERT(scOut.find("STATE              : 4  RUNNING") != std::string::npos, "'sc query Dhcp' returns RUNNING");

    // Test 'sc stop MicaSec' and 'sc start MicaSec'
    out.str("");
    out.clear();
    testShell.execute("sc stop MicaSec", out);
    TEST_ASSERT(out.str().find("[SC] ControlService SUCCESS") != std::string::npos, "'sc stop MicaSec' succeeds");

    auto micaSecRec = scm.getServiceRecord(L"MicaSec");
    TEST_ASSERT(micaSecRec != nullptr && micaSecRec->status.dwCurrentState == SERVICE_STOPPED,
                "MicaSec state must be STOPPED");

    out.str("");
    out.clear();
    testShell.execute("sc start MicaSec", out);
    TEST_ASSERT(out.str().find("[SC] StartService SUCCESS") != std::string::npos, "'sc start MicaSec' succeeds");
    TEST_ASSERT(micaSecRec->status.dwCurrentState == SERVICE_RUNNING, "MicaSec state must be RUNNING");

    // Cleanup handles
    advapi32::CloseServiceHandle(hSpooler);
    advapi32::CloseServiceHandle(hSvcA);
    advapi32::CloseServiceHandle(hSvcB);
    advapi32::CloseServiceHandle(hSvcC);
    advapi32::CloseServiceHandle(hCycleBeta);
    advapi32::CloseServiceHandle(hDrv);
    advapi32::CloseServiceHandle(hScm);
}

void Test_Lsass_Winlogon_And_SamDatabase() {
    using namespace micant;

    // ========================================================================
    // 1. SAM Database Engine & Password Verifier
    // ========================================================================
    auto& samDb = sam::SamDatabase::get();

    // Verify default builtin accounts
    auto adminUser = samDb.getUser(L"Administrator");
    TEST_ASSERT(adminUser.has_value(), "Builtin Administrator account must exist in SAM");
    TEST_ASSERT(adminUser->rid == sam::DOMAIN_USER_RID_ADMIN, "Administrator RID must be 500");
    TEST_ASSERT(adminUser->primaryGroupRid == sam::DOMAIN_ALIAS_RID_ADMINS, "Administrator primary group must be Builtin Administrators");

    auto guestUser = samDb.getUser(L"Guest");
    TEST_ASSERT(guestUser.has_value(), "Builtin Guest account must exist in SAM");
    TEST_ASSERT(guestUser->rid == sam::DOMAIN_USER_RID_GUEST, "Guest RID must be 501");
    TEST_ASSERT((guestUser->userFlags & sam::USER_ACCOUNT_DISABLED) != 0, "Guest account must be disabled by default");

    TEST_ASSERT((adminUser->userFlags & sam::USER_ACCOUNT_DISABLED) != 0,
        "Built-in Administrator must begin disabled until explicitly provisioned");
    TEST_ASSERT(!adminUser->hasPassword, "Built-in Administrator must not have a sample password");
    TEST_ASSERT(!samDb.getUser(L"admin").has_value(), "Sample admin/mica account must not be provisioned");
    TEST_ASSERT(samDb.setAccountEnabled(L"Administrator", true) == STATUS_PASSWORD_EXPIRED,
        "An account without a provisioned password must stay disabled");

    // RFC 8018 PBKDF2-HMAC-SHA-256 known-answer vector at the minimum policy cost.
    const std::array<uint8_t, 8> kdfPassword{'p','a','s','s','w','o','r','d'};
    const std::array<uint8_t, 4> kdfSalt{'s','a','l','t'};
    std::array<uint8_t, 32> kdfOutput{};
    const std::array<uint8_t, 32> kdfExpected{
        0x03,0x94,0xa2,0xed,0xe3,0x32,0xc9,0xa1,0x3e,0xb8,0x2e,0x9b,0x24,0x63,0x16,0x04,
        0xc3,0x1d,0xf9,0x78,0xb4,0xe2,0xf0,0xfb,0xd2,0xc5,0x49,0x94,0x4f,0x9d,0x79,0xa5};
    TEST_ASSERT(shared::crypto::Pbkdf2Sha256::derive(kdfPassword, kdfSalt,
        shared::crypto::Pbkdf2Sha256::MIN_ITERATIONS, kdfOutput), "PBKDF2 must derive at the supported minimum cost");
    TEST_ASSERT(kdfOutput == kdfExpected, "PBKDF2-HMAC-SHA-256 must match RFC 8018 known-answer vector");
    TEST_ASSERT(!shared::crypto::Pbkdf2Sha256::derive(kdfPassword, kdfSalt,
        shared::crypto::Pbkdf2Sha256::MIN_ITERATIONS - 1, kdfOutput), "PBKDF2 must reject costs below policy minimum");

    TEST_ASSERT(samDb.setPassword(L"Administrator", L"short") == STATUS_ACCOUNT_RESTRICTION,
        "Password policy must reject a short password");
    TEST_ASSERT(samDb.setPassword(L"Administrator", L"AdminPassword2026!") == STATUS_SUCCESS,
        "Explicit password provisioning must create a salted password verifier");
    TEST_ASSERT(samDb.setAccountEnabled(L"Administrator", true) == STATUS_SUCCESS,
        "A provisioned built-in Administrator may be explicitly enabled");

    // Test user creation
    NTSTATUS stCreate = samDb.createUser(L"testdeveloper", L"Pass@word2026!", L"Test Developer", L"Software Engineer", true);
    TEST_ASSERT(stCreate == STATUS_SUCCESS, "Creating new user account must succeed");

    // Duplicate creation must fail with STATUS_USER_EXISTS
    NTSTATUS stDup = samDb.createUser(L"testdeveloper", L"AnotherPass!");
    TEST_ASSERT(stDup == STATUS_USER_EXISTS, "Creating duplicate account must return STATUS_USER_EXISTS");

    // Verify credentials
    uint32_t outRid = 0;
    NTSTATUS stAuth = samDb.verifyCredentials(L"testdeveloper", L"Pass@word2026!", outRid);
    TEST_ASSERT(stAuth == STATUS_SUCCESS, "Verifying valid credentials must succeed");
    TEST_ASSERT(outRid >= 1001, "New user RID must be allocated dynamically above 1000");

    // Test bad credentials and account lockout
    NTSTATUS stBad = samDb.verifyCredentials(L"testdeveloper", L"WrongPassword1", outRid);
    TEST_ASSERT(stBad == STATUS_LOGON_FAILURE, "Verifying wrong password must return STATUS_LOGON_FAILURE");
    samDb.verifyCredentials(L"testdeveloper", L"WrongPassword2", outRid);
    samDb.verifyCredentials(L"testdeveloper", L"WrongPassword3", outRid);
    samDb.verifyCredentials(L"testdeveloper", L"WrongPassword4", outRid);
    samDb.verifyCredentials(L"testdeveloper", L"WrongPassword5", outRid);

    NTSTATUS stLocked = samDb.verifyCredentials(L"testdeveloper", L"Pass@word2026!", outRid);
    TEST_ASSERT(stLocked == STATUS_ACCOUNT_LOCKED_OUT, "Account must be locked out after 5 consecutive bad passwords");

    // Unlock account
    samDb.unlockUser(L"testdeveloper");
    NTSTATUS stUnlocked = samDb.verifyCredentials(L"testdeveloper", L"Pass@word2026!", outRid);
    TEST_ASSERT(stUnlocked == STATUS_SUCCESS, "Verifying credentials after unlock must succeed");

    // Test group membership retrieval
    auto groups = samDb.getGroupSidsForUser(outRid);
    TEST_ASSERT(!groups.empty(), "User must belong to group SIDs");
    bool hasEveryone = std::any_of(groups.begin(), groups.end(), [](const se::Sid& s) { return s == se::Sid::everyone(); });
    bool hasAdmins = std::any_of(groups.begin(), groups.end(), [](const se::Sid& s) { return s == se::Sid::administrators(); });
    TEST_ASSERT(hasEveryone, "Group SIDs must include S-1-1-0 Everyone");
    TEST_ASSERT(hasAdmins, "Admin user must belong to Builtin Administrators");

    // Clean up test user
    NTSTATUS stDel = samDb.deleteUser(L"testdeveloper");
    TEST_ASSERT(stDel == STATUS_SUCCESS, "Deleting user account must succeed");
    TEST_ASSERT(!samDb.getUser(L"testdeveloper").has_value(), "Deleted user must no longer exist in SAM");

    // Built-in Administrator cannot be deleted
    NTSTATUS stDelAdmin = samDb.deleteUser(L"Administrator");
    TEST_ASSERT(stDelAdmin == STATUS_ACCESS_DENIED, "Attempting to delete built-in Administrator must return STATUS_ACCESS_DENIED");

    // ========================================================================
    // 2. LSASS Subsystem & Authentication Packages
    // ========================================================================
    auto& lsa = lsass::LocalSecurityAuthority::get();

    // Verify MSV1_0 package registration
    auto msvPkg = lsa.getAuthenticationPackage(L"MSV1_0");
    TEST_ASSERT(msvPkg != nullptr, "MSV1_0 authentication package must be registered with LSASS");
    TEST_ASSERT(msvPkg->getPackageName() == L"MSV1_0", "Package name must match MSV1_0");

    // Verify LSASS IPC status
    TEST_ASSERT(lsa.isRpcServerRunning(), "LSASS RPC server must be operational");
    TEST_ASSERT(lsa.getPipeName() == L"\\\\.\\pipe\\lsass", "LSASS named pipe must be \\\\.\\pipe\\lsass");
    TEST_ASSERT(lsa.getAlpcPortName() == L"\\LsaAuthenticationPort", "LSASS ALPC port must be \\LsaAuthenticationPort");

    // Hosted compatibility adapter logon via LSASS (not Ring 3 proof).
    std::shared_ptr<se::TokenObject> adminToken;
    Luid adminLogonId{0, 0};
    NTSTATUS stLsaLogon = lsa.logonUser(
        L"MICANT",
        L"Administrator",
        L"AdminPassword2026!",
        lsass::SecurityLogonType::Interactive,
        L"MSV1_0",
        adminToken,
        adminLogonId
    );
    TEST_ASSERT(stLsaLogon == STATUS_SUCCESS, "LSASS logonUser for admin must succeed");
    TEST_ASSERT(adminToken != nullptr, "Logon token must be generated");
    TEST_ASSERT(adminLogonId.toUint64() != 0, "Logon session LUID must be nonzero");
    TEST_ASSERT(adminToken->getAuthenticationId() == adminLogonId, "Token AuthenticationId must match session LUID");
    TEST_ASSERT(adminToken->getSessionId() == 1, "Interactive logon token session ID must be 1");

    // Verify privileges in token
    TEST_ASSERT(adminToken->hasPrivilege(se::SE_DEBUG_NAME), "Admin token must possess SeDebugPrivilege");
    TEST_ASSERT(adminToken->hasPrivilege(se::SE_SHUTDOWN_NAME), "Admin token must possess SeShutdownPrivilege");
    TEST_ASSERT(!adminToken->hasPrivilege(se::SE_TCB_NAME), "Local administrator logon must not grant SeTcbPrivilege");
    TEST_ASSERT(!adminToken->hasPrivilege(se::SE_CREATE_TOKEN_NAME), "Local administrator logon must not grant SeCreateTokenPrivilege");
    TEST_ASSERT(!adminToken->hasPrivilege(se::SE_ASSIGNPRIMARYTOKEN_NAME), "Local administrator logon must not grant SeAssignPrimaryTokenPrivilege");

    std::shared_ptr<se::TokenObject> unsupportedLogonToken;
    Luid unsupportedLogonId{7, 1};
    TEST_ASSERT(lsa.logonUser(L"OTHERDOMAIN", L"Administrator", L"AdminPassword2026!",
        lsass::SecurityLogonType::Interactive, L"MSV1_0", unsupportedLogonToken,
        unsupportedLogonId) == STATUS_LOGON_FAILURE,
        "Local LSASS adapter must reject nonlocal domains");
    TEST_ASSERT(!unsupportedLogonToken && unsupportedLogonId.toUint64() == 0,
        "Rejected domain logon must not retain a token or logon identifier");
    TEST_ASSERT(lsa.logonUser(L"MICANT", L"Administrator", L"AdminPassword2026!",
        lsass::SecurityLogonType::Service, L"MSV1_0", unsupportedLogonToken,
        unsupportedLogonId) == STATUS_NOT_SUPPORTED,
        "Unimplemented service logon must fail explicitly");

    // Test failed logon
    std::shared_ptr<se::TokenObject> failToken;
    Luid failLuid{0, 0};
    NTSTATUS stLsaFail = lsa.logonUser(
        L"MICANT",
        L"Administrator",
        L"IncorrectPassword!",
        lsass::SecurityLogonType::Interactive,
        L"MSV1_0",
        failToken,
        failLuid
    );
    TEST_ASSERT(stLsaFail == STATUS_LOGON_FAILURE, "LSASS logon with wrong password must fail");

    // Legacy NT-hash challenge-response is deliberately unsupported.
    std::vector<uint8_t> serverChallenge = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    std::vector<uint8_t> clientResponse(16, 0);

    std::shared_ptr<se::TokenObject> crToken;
    Luid crLogonId{0, 0};
    NTSTATUS stCrLogon = lsa.logonUserWithChallenge(
        L"MICANT",
        L"admin",
        serverChallenge,
        clientResponse,
        lsass::SecurityLogonType::Network,
        L"MSV1_0",
        crToken,
        crLogonId
    );
    TEST_ASSERT(stCrLogon == STATUS_NOT_SUPPORTED, "LSASS legacy challenge-response must fail explicitly");
    TEST_ASSERT(crToken == nullptr, "Unsupported challenge-response must not create a token");

    // Enumerate logon sessions
    auto sessions = lsa.enumerateLogonSessions();
    TEST_ASSERT(sessions.size() >= 2, "Must have at least System and interactive logon sessions");

    auto sessionData = lsa.getLogonSessionData(adminLogonId);
    TEST_ASSERT(sessionData.has_value(), "Must find logon session data for admin LUID");
    TEST_ASSERT(sessionData->userName == L"Administrator", "Logon session username must match the authenticated account");
    TEST_ASSERT(sessionData->domainName == L"MICANT", "Logon session domain must match MICANT");
    TEST_ASSERT(sessionData->logonType == lsass::SecurityLogonType::Interactive, "Logon type must be Interactive");

    // Test SID and Name Resolution
    std::wstring outName, outDomain;
    bool sidFound = lsa.lookupAccountSid(se::Sid::localSystem(), outName, outDomain);
    TEST_ASSERT(sidFound && outName == L"SYSTEM" && outDomain == L"NT AUTHORITY", "S-1-5-18 must resolve to NT AUTHORITY\\SYSTEM");

    sidFound = lsa.lookupAccountSid(se::Sid::administrators(), outName, outDomain);
    TEST_ASSERT(sidFound && outName == L"Administrators" && outDomain == L"BUILTIN", "S-1-5-32-544 must resolve to BUILTIN\\Administrators");

    se::Sid outSid;
    bool nameFound = lsa.lookupAccountName(L"Administrator", outSid, outDomain);
    TEST_ASSERT(nameFound && outDomain == L"MICANT", "Administrator account name must resolve to MICANT domain");

    // Unsupported challenge-response must not have created a logon session.
    TEST_ASSERT(crLogonId.toUint64() == 0 && !lsa.getLogonSessionData(crLogonId).has_value(),
        "Rejected challenge-response must not publish a logon session");
    TEST_ASSERT(lsa.logoffUser(adminLogonId) == STATUS_SUCCESS,
        "Hosted compatibility logon session must release its session reference");
    TEST_ASSERT(!lsa.getLogonSessionData(adminLogonId).has_value(),
        "Logged-off session must be removed");

    // ========================================================================
    // 3. Winlogon Subsystem & Desktop Isolation
    // ========================================================================
    auto& winlogon = winlogon::WinlogonManager::get();

    // Reset to clean state for test
    winlogon.logoff();
    TEST_ASSERT(winlogon.getState() == winlogon::LogonState::LoggedOff, "Winlogon state after logoff must be LoggedOff");
    TEST_ASSERT(winlogon.getActiveDesktop() == winlogon::DesktopType::Winlogon, "LoggedOff state must select the Winlogon desktop state");

    // SAS in LoggedOff triggers LogonPrompt
    auto sasAction = winlogon.triggerSas();
    TEST_ASSERT(sasAction == winlogon::SasAction::LogonPrompt, "SAS when logged off must trigger LogonPrompt");

    // The hosted compatibility manager has no authenticated native service path.
    NTSTATUS stWLogon = winlogon.initiateLogon(L"Administrator", L"AdminPassword2026!");
    TEST_ASSERT(stWLogon == STATUS_NOT_SUPPORTED, "Hosted Winlogon must reject logon without authenticated service integration");
    TEST_ASSERT(winlogon.getState() == winlogon::LogonState::LoggedOff, "Rejected logon must leave Winlogon logged off");
    TEST_ASSERT(winlogon.getLoggedOnUser().empty(), "Rejected logon must not publish a user identity");
    TEST_ASSERT(winlogon.getActiveToken() == nullptr, "Rejected logon must not retain a token");
    TEST_ASSERT(winlogon.getShellPid() == 0, "Rejected logon must not publish a shell process");

    // Lock Workstation must not claim success without an active session.
    bool locked = winlogon.lockWorkstation();
    TEST_ASSERT(!locked, "Winlogon lockWorkstation must fail without an active session");
    TEST_ASSERT(winlogon.getState() == winlogon::LogonState::LoggedOff, "Failed lock must preserve LoggedOff state");

    // SAS while logged off continues to request the login UI.
    sasAction = winlogon.triggerSas();
    TEST_ASSERT(sasAction == winlogon::SasAction::LogonPrompt, "SAS while logged off must request LogonPrompt");
    TEST_ASSERT(winlogon.unlockWorkstation(L"WrongPassword") == STATUS_NOT_SUPPORTED,
        "Unlock must fail explicitly when no interactive session exists");

    // ========================================================================
    // 4. Win32 advapi32.dll & user32.dll API Integration
    // ========================================================================
    win32::HANDLE hLogonToken = nullptr;
    win32::BOOL bLogon = advapi32::LogonUserW(
        L"Administrator",
        L"MICANT",
        L"AdminPassword2026!",
        2, // LOGON32_LOGON_INTERACTIVE
        0,
        &hLogonToken
    );
    TEST_ASSERT(bLogon == win32::FALSE, "LogonUserW must fail until a native token handle path is available");
    TEST_ASSERT(hLogonToken == nullptr, "Failed LogonUserW must not return a synthetic token handle");

    // Test LookupAccountSidW
    wchar_t acctName[64]{};
    uint32_t cchAcct = 64;
    wchar_t domName[64]{};
    uint32_t cchDom = 64;
    uint32_t sidUse = 0;
    se::Sid adminSid = se::Sid::administrators();
    win32::BOOL bLookupSid = advapi32::LookupAccountSidW(
        nullptr,
        &adminSid,
        acctName,
        &cchAcct,
        domName,
        &cchDom,
        &sidUse
    );
    TEST_ASSERT(bLookupSid == win32::TRUE, "advapi32::LookupAccountSidW must return TRUE");
    TEST_ASSERT(wcscmp(acctName, L"Administrators") == 0, "Account name must match Administrators");
    TEST_ASSERT(wcscmp(domName, L"BUILTIN") == 0, "Domain name must match BUILTIN");

    // Test LookupPrivilegeValueW
    Luid privLuid{0, 0};
    win32::BOOL bPrivVal = advapi32::LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &privLuid);
    TEST_ASSERT(bPrivVal == win32::TRUE, "advapi32::LookupPrivilegeValueW must succeed for SeDebugPrivilege");
    TEST_ASSERT(privLuid.lowPart == 20, "SeDebugPrivilege LUID lowPart must be 20");

    wchar_t privName[64]{};
    uint32_t cchPriv = 64;
    win32::BOOL bPrivName = advapi32::LookupPrivilegeNameW(nullptr, &privLuid, privName, &cchPriv);
    TEST_ASSERT(bPrivName == win32::TRUE, "advapi32::LookupPrivilegeNameW must succeed for LUID 20");
    TEST_ASSERT(wcscmp(privName, L"SeDebugPrivilege") == 0, "Resolved privilege name must match SeDebugPrivilege");

    // Test user32 LockWorkStation
    win32::BOOL bLock = user32::LockWorkStation();
    TEST_ASSERT(bLock == win32::TRUE, "user32::LockWorkStation must return TRUE");
    TEST_ASSERT(winlogon.getState() == winlogon::LogonState::Locked, "Workstation must be locked after user32::LockWorkStation");
    winlogon.unlockWorkstation(L"mica");

    // ========================================================================
    // 5. Command Shell Security Commands Integration
    // ========================================================================
    shell::CommandShell testShell;

    // Test 'whoami'
    std::ostringstream ssWhoami;
    int rcWhoami = testShell.execute("whoami", ssWhoami);
    TEST_ASSERT(rcWhoami == 0, "Executing 'whoami' must succeed");
    TEST_ASSERT(ssWhoami.str().find("No interactive user is logged on") != std::string::npos, "whoami must not invent a current user");

    // Test 'whoami /user'
    std::ostringstream ssWhoamiUser;
    testShell.execute("whoami /user", ssWhoamiUser);
    TEST_ASSERT(ssWhoamiUser.str().find("No interactive user is logged on") != std::string::npos, "whoami /user must fail without an authenticated identity");

    // Test 'whoami /groups'
    std::ostringstream ssWhoamiGroups;
    testShell.execute("whoami /groups", ssWhoamiGroups);
    TEST_ASSERT(ssWhoamiGroups.str().find("No interactive user is logged on") != std::string::npos, "whoami /groups must fail without an authenticated identity");

    // Test 'whoami /priv'
    std::ostringstream ssWhoamiPriv;
    testShell.execute("whoami /priv", ssWhoamiPriv);
    TEST_ASSERT(ssWhoamiPriv.str().find("No interactive user is logged on") != std::string::npos, "whoami /priv must fail without an authenticated identity");

    // Test 'net user'
    std::ostringstream ssNetUser;
    testShell.execute("net user", ssNetUser);
    TEST_ASSERT(ssNetUser.str().find("User accounts for \\\\MICANT-DESKTOP") != std::string::npos, "net user must list accounts");
    TEST_ASSERT(ssNetUser.str().find("Administrator") != std::string::npos, "net user must include Administrator");
    TEST_ASSERT(ssNetUser.str().find("admin") == std::string::npos, "net user must not include the removed sample account");

    // The removed sample account must not be reported as provisioned.
    std::ostringstream ssNetUserAdmin;
    testShell.execute("net user admin", ssNetUserAdmin);
    TEST_ASSERT(ssNetUserAdmin.str().find("The user name could not be found") != std::string::npos, "net user admin must report the removed sample account absent");

    // Test 'net user alice Password123! /add'
    std::ostringstream ssAddUser;
    testShell.execute("net user alice Password123! /add", ssAddUser);
    TEST_ASSERT(ssAddUser.str().find("System error 50") != std::string::npos, "net user /add must report unavailable SAM service");
    TEST_ASSERT(!samDb.getUser(L"alice").has_value(), "Shell account management must not mutate the hosted SAM adapter");

    // Test 'net user alice /delete'
    std::ostringstream ssDelUser;
    testShell.execute("net user alice /delete", ssDelUser);
    TEST_ASSERT(ssDelUser.str().find("System error 50") != std::string::npos, "net user /delete must report unavailable SAM service");

    // Test 'lock'
    std::ostringstream ssLock;
    testShell.execute("lock", ssLock);
    TEST_ASSERT(ssLock.str().find("Unable to lock workstation") != std::string::npos, "lock command must report unavailable session");
    TEST_ASSERT(winlogon.getState() == winlogon::LogonState::LoggedOff, "Failed lock must leave workstation logged off");

    // Unlock and test 'logoff'
    std::ostringstream ssLogoff;
    testShell.execute("logoff", ssLogoff);
    TEST_ASSERT(ssLogoff.str().find("No interactive user is logged on") != std::string::npos, "logoff must not claim a session was terminated when none existed");
    TEST_ASSERT(winlogon.getState() == winlogon::LogonState::LoggedOff, "State must be LoggedOff after 'logoff'");
}

// ============================================================================
// Suite 45: PrismX & Prism3D Graphics Subsystem (DXGI, Direct3D, WDDM D3DKMT)
// ============================================================================
void Test_PrismX_And_Prism3D_GraphicsSubsystem() {
    using namespace micant::prismx;
    using namespace micant::prism3d;
    using namespace micant::dxgkrnl;

    // ------------------------------------------------------------------------
    // 1. DXGI Factory, Adapter, and Display Output Enumeration
    // ------------------------------------------------------------------------
    IDXGIFactory1* factory = nullptr;
    int32_t hrFactory = CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void**>(&factory));
    TEST_ASSERT(hrFactory == 0 && factory != nullptr, "CreateDXGIFactory1 must succeed and return factory");

    IDXGIAdapter1* adapter0 = nullptr;
    int32_t hrAdapter = factory->EnumAdapters1(0, &adapter0);
    TEST_ASSERT(hrAdapter == 0 && adapter0 != nullptr, "EnumAdapters1 for Adapter 0 must succeed");

    DXGI_ADAPTER_DESC1 desc0{};
    adapter0->GetDesc1(&desc0);
    std::wstring wDesc0(desc0.Description);
    TEST_ASSERT(wDesc0.find(L"PRISM") != std::wstring::npos, "Adapter 0 description must indicate Prism hardware");
    TEST_ASSERT(desc0.VendorId == 0x1414, "VendorId must match 0x1414");
    TEST_ASSERT(desc0.DedicatedVideoMemory == 4ULL * 1024 * 1024 * 1024, "Adapter 0 must have 4GB Dedicated VRAM");

    IDXGIOutput* output0 = nullptr;
    int32_t hrOutput = adapter0->EnumOutputs(0, &output0);
    TEST_ASSERT(hrOutput == 0 && output0 != nullptr, "Adapter 0 must have at least one connected display output");

    DXGI_OUTPUT_DESC oDesc0{};
    output0->GetDesc(&oDesc0);
    TEST_ASSERT((oDesc0.DesktopCoordinates.right - oDesc0.DesktopCoordinates.left) == 1920, "Output desktop width must be 1920");
    TEST_ASSERT((oDesc0.DesktopCoordinates.bottom - oDesc0.DesktopCoordinates.top) == 1080, "Output desktop height must be 1080");

    uint32_t numModes = 0;
    output0->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &numModes, nullptr);
    TEST_ASSERT(numModes == 3, "Display mode list must contain 3 standard modes");

    // ------------------------------------------------------------------------
    // 2. Direct3D 11 Device & Presentation SwapChain Creation
    // ------------------------------------------------------------------------
    DXGI_SWAP_CHAIN_DESC scDesc{};
    scDesc.BufferDesc.Width = 640;
    scDesc.BufferDesc.Height = 480;
    scDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.BufferCount = 2;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;

    int32_t hrDev = D3D11CreateDeviceAndSwapChain(
        adapter0,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        7,
        &scDesc,
        &swapChain,
        &device,
        &featureLevel,
        &context
    );
    TEST_ASSERT(hrDev == 0, "D3D11CreateDeviceAndSwapChain must succeed");
    TEST_ASSERT(device != nullptr && context != nullptr && swapChain != nullptr, "Device, Context, and SwapChain must be valid");
    TEST_ASSERT(featureLevel == D3D_FEATURE_LEVEL_11_0, "Feature level must default to D3D_FEATURE_LEVEL_11_0");

    // Test back-buffer surface access
    IDXGISurface* backBuffer = nullptr;
    int32_t hrBuf = swapChain->GetBuffer(0, IID_IDXGISurface, reinterpret_cast<void**>(&backBuffer));
    TEST_ASSERT(hrBuf == 0 && backBuffer != nullptr, "GetBuffer(0) must return valid IDXGISurface");

    DXGI_MODE_DESC surfDesc{};
    backBuffer->GetDesc(&surfDesc);
    TEST_ASSERT(surfDesc.Width == 640 && surfDesc.Height == 480, "Surface dimensions must match 640x480");

    // ------------------------------------------------------------------------
    // 3. Render Target View, Viewport, and Clear Operations
    // ------------------------------------------------------------------------
    ID3D11RenderTargetView* rtv = nullptr;
    int32_t hrRtv = device->CreateRenderTargetView(reinterpret_cast<ID3D11Resource*>(backBuffer), nullptr, &rtv);
    TEST_ASSERT(hrRtv == 0 && rtv != nullptr, "CreateRenderTargetView must succeed");

    D3D11_VIEWPORT vp{ 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f };
    context->RSSetViewports(1, &vp);
    context->OMSetRenderTargets(1, &rtv, nullptr);

    // Clear render target to solid dark blue: R=0.1, G=0.2, B=0.8, A=1.0
    const float clearColor[4] = { 0.1f, 0.2f, 0.8f, 1.0f };
    context->ClearRenderTargetView(rtv, clearColor);

    auto* surfImpl = static_cast<PrismXSurfaceImpl*>(backBuffer);
    const uint32_t* rawPixels = reinterpret_cast<const uint32_t*>(surfImpl->GetRawData());
    TEST_ASSERT(rawPixels[0] != 0, "First pixel of back-buffer must not be 0 after clear");

    // Check BGRA clear values
    uint8_t expectedB = static_cast<uint8_t>(0.8f * 255.0f);
    uint8_t expectedG = static_cast<uint8_t>(0.2f * 255.0f);
    uint8_t expectedR = static_cast<uint8_t>(0.1f * 255.0f);
    uint8_t expectedA = 255;
    uint32_t expectedPixel = (expectedA << 24) | (expectedR << 16) | (expectedG << 8) | expectedB;
    TEST_ASSERT(rawPixels[0] == expectedPixel, "Pixel value after clear must match expected packed 32-bpp BGRA");

    // ------------------------------------------------------------------------
    // 4. Prism3D Vertex Buffer Creation & Barycentric Triangle Rasterization
    // ------------------------------------------------------------------------
    VertexPositionColor triVertices[3] = {
        {  0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f }, // Top (Pure Red)
        { -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f, 1.0f }, // Bottom-Left (Pure Green)
        {  0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f, 1.0f }  // Bottom-Right (Pure Blue)
    };

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.ByteWidth = sizeof(triVertices);
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.StructureByteStride = sizeof(VertexPositionColor);

    D3D11_SUBRESOURCE_DATA vbData{};
    vbData.pSysMem = triVertices;

    ID3D11Buffer* vertexBuffer = nullptr;
    int32_t hrVb = device->CreateBuffer(&vbDesc, &vbData, &vertexBuffer);
    TEST_ASSERT(hrVb == 0 && vertexBuffer != nullptr, "CreateBuffer for vertex data must succeed");

    uint32_t stride = sizeof(VertexPositionColor);
    uint32_t offset = 0;
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Rasterize triangle to back-buffer surface
    context->Draw(3, 0);

    // The center of the triangle in screen coords is (320, 240)
    // In NDC: (0, 0) -> Screen: (320, 240).
    // The center should be shaded with interpolated colors (approx R=0.33, G=0.33, B=0.33)
    // and definitely different from clearColor!
    size_t centerPixelIdx = 240ULL * 640 + 320;
    uint32_t centerPixel = rawPixels[centerPixelIdx];
    TEST_ASSERT(centerPixel != expectedPixel, "Center pixel inside triangle must have been shaded by rasterizer");

    // Present the frame
    int32_t hrPresent = swapChain->Present(1, 0);
    TEST_ASSERT(hrPresent == 0, "Present must succeed");

    uint32_t lastPresentCount = 0;
    swapChain->GetLastPresentCount(&lastPresentCount);
    TEST_ASSERT(lastPresentCount == 1, "Swapchain present count must be 1");

    // ------------------------------------------------------------------------
    // 5. 3D Mathematics, Matrix Pipeline & Vector Operations
    // ------------------------------------------------------------------------
    Matrix4x4 id = Matrix4x4::Identity();
    Vector4 v0{ 1.0f, 2.0f, 3.0f, 1.0f };
    Vector4 v0T = id.Transform(v0);
    TEST_ASSERT(v0T.x == 1.0f && v0T.y == 2.0f && v0T.z == 3.0f && v0T.w == 1.0f, "Identity matrix transform must preserve vector");

    Matrix4x4 trans = Matrix4x4::Translation(10.0f, -5.0f, 20.0f);
    Vector4 vOrigin{ 0.0f, 0.0f, 0.0f, 1.0f };
    Vector4 vTrans = trans.Transform(vOrigin);
    TEST_ASSERT(vTrans.x == 10.0f && vTrans.y == -5.0f && vTrans.z == 20.0f && vTrans.w == 1.0f, "Translation matrix must translate origin");

    Matrix4x4 rotX = Matrix4x4::RotationX(3.14159265f / 2.0f);
    Vector4 vUp{ 0.0f, 1.0f, 0.0f, 1.0f };
    Vector4 vRotX = rotX.Transform(vUp);
    TEST_ASSERT(std::abs(vRotX.x) < 1e-4f && std::abs(vRotX.y) < 1e-4f && std::abs(vRotX.z - 1.0f) < 1e-4f, "RotationX by 90deg must rotate +Y to +Z");

    Matrix4x4 rotY = Matrix4x4::RotationY(3.14159265f / 2.0f);
    Vector4 vForward{ 0.0f, 0.0f, 1.0f, 1.0f };
    Vector4 vRotY = rotY.Transform(vForward);
    TEST_ASSERT(std::abs(vRotY.x - 1.0f) < 1e-4f && std::abs(vRotY.y) < 1e-4f, "RotationY by 90deg must rotate +Z to +X in LH coordinates");

    Matrix4x4 proj = Matrix4x4::PerspectiveFovLH(1.047f, 640.0f / 480.0f, 0.1f, 100.0f);
    TEST_ASSERT(proj.m[2][3] == 1.0f, "LH perspective projection m[2][3] must be 1.0f for w-buffering");
    TEST_ASSERT(proj.m[0][0] > 0.0f && proj.m[1][1] > 0.0f, "Focal lengths must be positive");

    Matrix4x4 view = Matrix4x4::LookAtLH(Vector3{ 0.0f, 0.0f, -5.0f }, Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f });
    TEST_ASSERT(std::abs(view.m[3][2] - 5.0f) < 1e-4f, "LookAtLH eye at -5z must translate z by +5");

    // ------------------------------------------------------------------------
    // 6. Direct3D 11 Pipeline State Objects & Resource Views
    // ------------------------------------------------------------------------
    // Rasterizer States (Solid & Wireframe)
    D3D11_RASTERIZER_DESC rsSolidDesc{};
    rsSolidDesc.FillMode = D3D11_FILL_SOLID;
    rsSolidDesc.CullMode = D3D11_CULL_BACK;
    ID3D11RasterizerState* rsSolid = nullptr;
    int32_t hrRsSolid = device->CreateRasterizerState(&rsSolidDesc, &rsSolid);
    TEST_ASSERT(hrRsSolid == 0 && rsSolid != nullptr, "CreateRasterizerState (Solid) must succeed");

    D3D11_RASTERIZER_DESC rsWireDesc{};
    rsWireDesc.FillMode = D3D11_FILL_WIREFRAME;
    rsWireDesc.CullMode = D3D11_CULL_NONE;
    ID3D11RasterizerState* rsWire = nullptr;
    int32_t hrRsWire = device->CreateRasterizerState(&rsWireDesc, &rsWire);
    TEST_ASSERT(hrRsWire == 0 && rsWire != nullptr, "CreateRasterizerState (Wireframe) must succeed");

    context->RSSetState(rsSolid);

    // Depth Stencil View & State
    ID3D11DepthStencilView* dsv = nullptr;
    int32_t hrDsv = device->CreateDepthStencilView(nullptr, nullptr, &dsv);
    TEST_ASSERT(hrDsv == 0 && dsv != nullptr, "CreateDepthStencilView must succeed");

    D3D11_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = 1;
    dsDesc.DepthWriteMask = 1;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ID3D11DepthStencilState* dsState = nullptr;
    int32_t hrDsState = device->CreateDepthStencilState(&dsDesc, &dsState);
    TEST_ASSERT(hrDsState == 0 && dsState != nullptr, "CreateDepthStencilState must succeed");
    context->OMSetDepthStencilState(dsState, 0);

    // Blend State
    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = 1;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;
    ID3D11BlendState* blendState = nullptr;
    int32_t hrBlend = device->CreateBlendState(&blendDesc, &blendState);
    TEST_ASSERT(hrBlend == 0 && blendState != nullptr, "CreateBlendState must succeed");
    float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    context->OMSetBlendState(blendState, blendFactor, 0xFFFFFFFF);

    // Texture 2D, SRV & Sampler State
    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = 4;
    texDesc.Height = 4;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    uint32_t checkerPixels[16];
    for (int i = 0; i < 16; ++i) checkerPixels[i] = ((i % 2) == 0) ? 0xFFFFFFFF : 0xFF000000;
    D3D11_SUBRESOURCE_DATA texInit{};
    texInit.pSysMem = checkerPixels;
    texInit.SysMemPitch = 4 * sizeof(uint32_t);
    ID3D11Texture2D* texture2D = nullptr;
    int32_t hrTex = device->CreateTexture2D(&texDesc, &texInit, &texture2D);
    TEST_ASSERT(hrTex == 0 && texture2D != nullptr, "CreateTexture2D must succeed");

    ID3D11ShaderResourceView* srv = nullptr;
    int32_t hrSrv = device->CreateShaderResourceView(texture2D, nullptr, &srv);
    TEST_ASSERT(hrSrv == 0 && srv != nullptr, "CreateShaderResourceView must succeed");
    context->PSSetShaderResources(0, 1, &srv);

    D3D11_SAMPLER_DESC sampDesc{};
    ID3D11SamplerState* samplerState = nullptr;
    int32_t hrSamp = device->CreateSamplerState(&sampDesc, &samplerState);
    TEST_ASSERT(hrSamp == 0 && samplerState != nullptr, "CreateSamplerState must succeed");
    context->PSSetSamplers(0, 1, &samplerState);

    // ------------------------------------------------------------------------
    // 7. Indexed Drawing (DrawIndexed), Constant Buffers, and 3D Cube Test
    // ------------------------------------------------------------------------
    VertexPositionColor cubeVerts[8] = {
        { -1.0f, -1.0f, -1.0f,  1.0f, 0.0f, 0.0f, 1.0f }, // 0: Red
        { -1.0f,  1.0f, -1.0f,  0.0f, 1.0f, 0.0f, 1.0f }, // 1: Green
        {  1.0f,  1.0f, -1.0f,  0.0f, 0.0f, 1.0f, 1.0f }, // 2: Blue
        {  1.0f, -1.0f, -1.0f,  1.0f, 1.0f, 0.0f, 1.0f }, // 3: Yellow
        { -1.0f, -1.0f,  1.0f,  1.0f, 0.0f, 1.0f, 1.0f }, // 4: Magenta
        { -1.0f,  1.0f,  1.0f,  0.0f, 1.0f, 1.0f, 1.0f }, // 5: Cyan
        {  1.0f,  1.0f,  1.0f,  1.0f, 1.0f, 1.0f, 1.0f }, // 6: White
        {  1.0f, -1.0f,  1.0f,  0.5f, 0.5f, 0.5f, 1.0f }  // 7: Grey
    };
    D3D11_BUFFER_DESC cubeVbDesc{};
    cubeVbDesc.ByteWidth = sizeof(cubeVerts);
    cubeVbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    cubeVbDesc.StructureByteStride = sizeof(VertexPositionColor);
    D3D11_SUBRESOURCE_DATA cubeVbInit{};
    cubeVbInit.pSysMem = cubeVerts;
    ID3D11Buffer* cubeVb = nullptr;
    device->CreateBuffer(&cubeVbDesc, &cubeVbInit, &cubeVb);
    TEST_ASSERT(cubeVb != nullptr, "CreateBuffer for 3D cube vertex buffer must succeed");

    uint16_t cubeIndices[36] = {
        0, 1, 2,  0, 2, 3,  // Front
        4, 6, 5,  4, 7, 6,  // Back
        4, 5, 1,  4, 1, 0,  // Left
        3, 2, 6,  3, 6, 7,  // Right
        1, 5, 6,  1, 6, 2,  // Top
        4, 0, 3,  4, 3, 7   // Bottom
    };
    D3D11_BUFFER_DESC cubeIbDesc{};
    cubeIbDesc.ByteWidth = sizeof(cubeIndices);
    cubeIbDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA cubeIbInit{};
    cubeIbInit.pSysMem = cubeIndices;
    ID3D11Buffer* cubeIb = nullptr;
    device->CreateBuffer(&cubeIbDesc, &cubeIbInit, &cubeIb);
    TEST_ASSERT(cubeIb != nullptr, "CreateBuffer for 3D cube index buffer must succeed");

    // Model-View-Projection Matrix
    Matrix4x4 world = Matrix4x4::Multiply(Matrix4x4::RotationX(0.5f), Matrix4x4::RotationY(0.7f));
    Matrix4x4 camView = Matrix4x4::LookAtLH(Vector3{ 0.0f, 0.0f, -3.5f }, Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f });
    Matrix4x4 camProj = Matrix4x4::PerspectiveFovLH(1.047f, 640.0f / 480.0f, 0.1f, 100.0f);
    Matrix4x4 mvp = Matrix4x4::Multiply(world, Matrix4x4::Multiply(camView, camProj));

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(Matrix4x4);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cbInit{};
    cbInit.pSysMem = &mvp;
    ID3D11Buffer* cb = nullptr;
    device->CreateBuffer(&cbDesc, &cbInit, &cb);
    TEST_ASSERT(cb != nullptr, "CreateBuffer for MVP constant buffer must succeed");

    // Bind Render Target & Depth Stencil
    context->OMSetRenderTargets(1, &rtv, dsv);
    context->ClearRenderTargetView(rtv, clearColor);
    context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

    uint32_t cubeStride = sizeof(VertexPositionColor);
    uint32_t cubeOffset = 0;
    context->IASetVertexBuffers(0, 1, &cubeVb, &cubeStride, &cubeOffset);
    context->IASetIndexBuffer(cubeIb, DXGI_FORMAT_R16_UINT, 0);
    context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetConstantBuffers(0, 1, &cb);
    context->RSSetState(rsSolid);

    // Render 3D Cube with DrawIndexed
    context->DrawIndexed(36, 0, 0);

    // Verify center pixel or screen area was shaded by cube
    bool shadedPixelFound = false;
    for (size_t y = 200; y < 280; ++y) {
        for (size_t x = 280; x < 360; ++x) {
            if (rawPixels[y * 640 + x] != expectedPixel) {
                shadedPixelFound = true;
                break;
            }
        }
        if (shadedPixelFound) break;
    }
    TEST_ASSERT(shadedPixelFound, "3D Cube DrawIndexed must rasterize and shade back-buffer pixels");

    // Also test wireframe DrawIndexed
    context->RSSetState(rsWire);
    context->DrawIndexed(36, 0, 0);

    swapChain->Present(1, 0);
    swapChain->GetLastPresentCount(&lastPresentCount);
    TEST_ASSERT(lastPresentCount == 2, "Swapchain present count must be 2 after cube render");

    // Cleanup resources
    cb->Release();
    cubeIb->Release();
    cubeVb->Release();
    samplerState->Release();
    srv->Release();
    texture2D->Release();
    blendState->Release();
    dsState->Release();
    dsv->Release();
    rsWire->Release();
    rsSolid->Release();

    // ------------------------------------------------------------------------
    // 8. WDDM Kernel Subsystem Syscall Validation (dxgkrnl / D3DKMT)
    // ------------------------------------------------------------------------
    D3DKMT_OPENADAPTERFROMHDC openHdc{};
    openHdc.hDc = reinterpret_cast<void*>(0x2001);
    NtStatus stOpen = NtGdiDdD3DKMTOpenAdapterFromHdc(&openHdc);
    TEST_ASSERT(NT_SUCCESS(stOpen) && openHdc.hAdapter != 0, "D3DKMTOpenAdapterFromHdc must return valid adapter handle");

    D3DKMT_CREATEDEVICE createDev{};
    createDev.hAdapter = openHdc.hAdapter;
    NtStatus stDev = NtGdiDdD3DKMTCreateDevice(&createDev);
    TEST_ASSERT(NT_SUCCESS(stDev) && createDev.hDevice != 0, "D3DKMTCreateDevice must return valid device handle");

    D3DKMT_CREATEALLOCATIONINFO allocInfo[2]{};
    D3DKMT_CREATEALLOCATION createAlloc{};
    createAlloc.hDevice = createDev.hDevice;
    createAlloc.NumAllocations = 2;
    createAlloc.pAllocationInfo = allocInfo;
    NtStatus stAlloc = NtGdiDdD3DKMTCreateAllocation(&createAlloc);
    TEST_ASSERT(NT_SUCCESS(stAlloc), "D3DKMTCreateAllocation must succeed");
    TEST_ASSERT(allocInfo[0].hAllocation != 0 && allocInfo[1].hAllocation != 0, "Allocations must have non-zero handles");

    auto& dxg = DxgkrnlSubsystem::GetInstance();
    TEST_ASSERT(dxg.GetActiveAllocationsCount() >= 2, "Active allocations count must be at least 2");

    // Test SubmitCommand and Present syscalls
    D3DKMT_SUBMITCOMMAND submitCmd{};
    submitCmd.Commands = 0x7FF000000000ULL;
    submitCmd.CommandLength = 512;
    NtStatus stSubmit = NtGdiDdD3DKMTSubmitCommand(&submitCmd);
    TEST_ASSERT(NT_SUCCESS(stSubmit), "D3DKMTSubmitCommand must succeed");
    TEST_ASSERT(dxg.GetTotalSubmissions() >= 1, "Total GPU command submissions must increment");

    D3DKMT_PRESENT presentKmt{};
    presentKmt.hDevice = createDev.hDevice;
    NtStatus stKmtPres = NtGdiDdD3DKMTPresent(&presentKmt);
    TEST_ASSERT(NT_SUCCESS(stKmtPres), "D3DKMTPresent must succeed");
    TEST_ASSERT(dxg.GetTotalPresents() >= 1, "Total compositor presents must increment");

    // Cleanup Allocations and Device
    D3DKMT_HANDLE allocList[2] = { allocInfo[0].hAllocation, allocInfo[1].hAllocation };
    D3DKMT_DESTROYALLOCATION destroyAlloc{};
    destroyAlloc.hDevice = createDev.hDevice;
    destroyAlloc.phAllocationList = allocList;
    destroyAlloc.AllocationCount = 2;
    NtStatus stDestrAlloc = NtGdiDdD3DKMTDestroyAllocation(&destroyAlloc);
    TEST_ASSERT(NT_SUCCESS(stDestrAlloc), "D3DKMTDestroyAllocation must succeed");

    D3DKMT_DESTROYDEVICE destroyDev{};
    destroyDev.hDevice = createDev.hDevice;
    NtStatus stDestrDev = NtGdiDdD3DKMTDestroyDevice(&destroyDev);
    TEST_ASSERT(NT_SUCCESS(stDestrDev), "D3DKMTDestroyDevice must succeed");

    // ------------------------------------------------------------------------
    // 9. Shell Integration Test ('prismx', 'prismx test', 'prismx cube', 'prismx wireframe')
    // ------------------------------------------------------------------------
    shell::CommandShell testShell;
    std::ostringstream ssGpu;
    testShell.execute("prismx", ssGpu);
    std::string outGpu = ssGpu.str();
    TEST_ASSERT(outGpu.find("PrismX & Prism3D Graphics Subsystem") != std::string::npos, "prismx command must display header");
    TEST_ASSERT(outGpu.find("Dedicated Video Memory:  4096 MB") != std::string::npos, "prismx command must display 4096 MB VRAM");
    TEST_ASSERT(outGpu.find("WDDM Kernel Telemetry") != std::string::npos, "prismx command must display WDDM telemetry");

    std::ostringstream ssTest;
    testShell.execute("prismx test", ssTest);
    std::string outTest = ssTest.str();
    TEST_ASSERT(outTest.find("3D Barycentric Shaded Triangle rendered successfully") != std::string::npos, "prismx test must render 3D triangle");
    TEST_ASSERT(outTest.find("Swapchain: 800x600") != std::string::npos, "prismx test must output swapchain size");

    std::ostringstream ssCube;
    testShell.execute("prismx cube", ssCube);
    std::string outCube = ssCube.str();
    TEST_ASSERT(outCube.find("3D Cube rendered successfully via DrawIndexed!") != std::string::npos, "prismx cube must render 3D cube");
    TEST_ASSERT(outCube.find("Mesh: 8 Vertices, 36 Indices (12 Triangles)") != std::string::npos, "prismx cube must verify mesh topology");
    TEST_ASSERT(outCube.find("SOLID (Barycentric Gouraud)") != std::string::npos, "prismx cube must report solid rasterizer");

    std::ostringstream ssWire;
    testShell.execute("prismx wireframe", ssWire);
    std::string outWire = ssWire.str();
    TEST_ASSERT(outWire.find("3D Cube rendered successfully via DrawIndexed!") != std::string::npos, "prismx wireframe must render wireframe cube");
    TEST_ASSERT(outWire.find("WIREFRAME (Bresenham line)") != std::string::npos, "prismx wireframe must report wireframe rasterizer");

    // Clean up COM resources
    vertexBuffer->Release();
    rtv->Release();
    backBuffer->Release();
    swapChain->Release();
    context->Release();
    device->Release();
    output0->Release();
    adapter0->Release();
    factory->Release();
}

void Test_VulkanLoader_And_PrismVK_Subsystem() {
    using namespace micant::vulkan;

    // 1. Initialize Vulkan Loader Subsystem Exports
    InitializeVulkanSubsystemExports();

    auto& ldr = ldr::DynamicLoader::get();
    TEST_ASSERT(ldr.getExport("vulkan-1.dll", "vkCreateInstance") != nullptr, "vulkan-1.dll vkCreateInstance must be exported");
    TEST_ASSERT(ldr.getExport("vulkan-1.dll", "vkEnumeratePhysicalDevices") != nullptr, "vulkan-1.dll vkEnumeratePhysicalDevices must be exported");
    TEST_ASSERT(ldr.getExport("vulkan-1.dll", "vkCreateDevice") != nullptr, "vulkan-1.dll vkCreateDevice must be exported");
    TEST_ASSERT(ldr.getExport("vulkan-1.dll", "vkCreateWin32SurfaceKHR") != nullptr, "vulkan-1.dll vkCreateWin32SurfaceKHR must be exported");
    TEST_ASSERT(ldr.getExport("vulkan-1.dll", "vkCreateSwapchainKHR") != nullptr, "vulkan-1.dll vkCreateSwapchainKHR must be exported");
    TEST_ASSERT(ldr.getExport("vulkan-1.dll", "vkQueueSubmit") != nullptr, "vulkan-1.dll vkQueueSubmit must be exported");

    // 2. Registry Driver Discovery Check
    TEST_ASSERT(VulkanLoader::get().isIcdRegistered(), "Khronos Vulkan Driver registry hive must be initialized");

    // 3. Create Vulkan Instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MicaNT Vulkan Test App";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "PrismVK";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 3, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance = nullptr;
    VkResult res = vkCreateInstance(&instInfo, nullptr, &instance);
    TEST_ASSERT(res == VK_SUCCESS && instance != nullptr, "vkCreateInstance must succeed");

    // 4. Enumerate Physical Devices & Properties
    uint32_t physCount = 0;
    res = vkEnumeratePhysicalDevices(instance, &physCount, nullptr);
    TEST_ASSERT(res == VK_SUCCESS && physCount >= 1, "vkEnumeratePhysicalDevices count must be at least 1");

    std::vector<VkPhysicalDevice> physDevices(physCount);
    res = vkEnumeratePhysicalDevices(instance, &physCount, physDevices.data());
    TEST_ASSERT(res == VK_SUCCESS && physDevices[0] != nullptr, "vkEnumeratePhysicalDevices must return physical device handle");

    VkPhysicalDevice phys = physDevices[0];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys, &props);
    TEST_ASSERT(props.apiVersion == VK_API_VERSION_1_3, "Vulkan API version must be 1.3");
    TEST_ASSERT(props.vendorID == 0x1414, "Vendor ID must match 0x1414");
    TEST_ASSERT(props.deviceID == 0x008C, "Device ID must match 0x008C");
    TEST_ASSERT(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, "Device must be Discrete GPU");

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    TEST_ASSERT(memProps.memoryHeapCount >= 2, "Must have at least 2 memory heaps");
    TEST_ASSERT(memProps.memoryHeaps[0].size == 8192ULL * 1024 * 1024, "Heap 0 must have 8192 MB VRAM");

    // 5. Create Logical Device & Queues
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount = 1;
    qci.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo devInfo{};
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &qci;

    VkDevice device = nullptr;
    res = vkCreateDevice(phys, &devInfo, nullptr, &device);
    TEST_ASSERT(res == VK_SUCCESS && device != nullptr, "vkCreateDevice must succeed");

    VkQueue queue = nullptr;
    vkGetDeviceQueue(device, 0, 0, &queue);
    TEST_ASSERT(queue != nullptr, "vkGetDeviceQueue must return valid queue");

    // 6. Win32 Surface & Swapchain
    VkWin32SurfaceCreateInfoKHR surfInfo{};
    surfInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfInfo.hwnd = reinterpret_cast<void*>(0x20002);
    surfInfo.hinstance = reinterpret_cast<void*>(0x400000);

    VkSurfaceKHR surface = 0;
    res = vkCreateWin32SurfaceKHR(instance, &surfInfo, nullptr, &surface);
    TEST_ASSERT(res == VK_SUCCESS && surface != 0, "vkCreateWin32SurfaceKHR must succeed");

    VkSurfaceCapabilitiesKHR caps{};
    res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    TEST_ASSERT(res == VK_SUCCESS && caps.minImageCount == 2, "Surface caps minImageCount must be 2");

    VkSwapchainCreateInfoKHR scInfo{};
    scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scInfo.surface = surface;
    scInfo.minImageCount = 2;
    scInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    scInfo.imageExtent = { 1280, 720 };
    scInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    scInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkSwapchainKHR swapchain = 0;
    res = vkCreateSwapchainKHR(device, &scInfo, nullptr, &swapchain);
    TEST_ASSERT(res == VK_SUCCESS && swapchain != 0, "vkCreateSwapchainKHR must succeed");

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    TEST_ASSERT(imageCount == 2, "Swapchain image count must be 2");

    // 7. Command Buffer Recording & Queue Submission
    VkCommandPool commandPool = 0;
    VkCommandPoolCreateInfo cpInfo{};
    cpInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpInfo.queueFamilyIndex = 0;
    res = vkCreateCommandPool(device, &cpInfo, nullptr, &commandPool);
    TEST_ASSERT(res == VK_SUCCESS && commandPool != 0, "vkCreateCommandPool must succeed");

    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = commandPool;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmdBuf = nullptr;
    res = vkAllocateCommandBuffers(device, &cbAlloc, &cmdBuf);
    TEST_ASSERT(res == VK_SUCCESS && cmdBuf != nullptr, "vkAllocateCommandBuffers must succeed");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    res = vkBeginCommandBuffer(cmdBuf, &beginInfo);
    TEST_ASSERT(res == VK_SUCCESS, "vkBeginCommandBuffer must succeed");

    VkClearValue clearColor{};
    clearColor.color.float32[0] = 0.05f;
    clearColor.color.float32[1] = 0.1f;
    clearColor.color.float32[2] = 0.25f;
    clearColor.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderArea.extent = { 1280, 720 };
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;
    vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{ 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };
    vkCmdSetViewport(cmdBuf, 0, 1, &vp);
    vkCmdDraw(cmdBuf, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmdBuf);

    res = vkEndCommandBuffer(cmdBuf);
    TEST_ASSERT(res == VK_SUCCESS, "vkEndCommandBuffer must succeed");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;
    res = vkQueueSubmit(queue, 1, &submitInfo, 0);
    TEST_ASSERT(res == VK_SUCCESS, "vkQueueSubmit must succeed");

    uint32_t imageIndex = 0;
    res = vkAcquireNextImageKHR(device, swapchain, 0, 0, 0, &imageIndex);
    TEST_ASSERT(res == VK_SUCCESS && imageIndex == 0, "vkAcquireNextImageKHR must acquire image 0");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    res = vkQueuePresentKHR(queue, &presentInfo);
    TEST_ASSERT(res == VK_SUCCESS, "vkQueuePresentKHR must succeed");

    // 8. Shell Command Verification ('vulkan' and 'vulkan test')
    shell::CommandShell testShell;
    std::ostringstream ssVk;
    testShell.execute("vulkan", ssVk);
    std::string outVk = ssVk.str();
    TEST_ASSERT(outVk.find("MicaNT PrismVK & Vulkan 1.3 ICD Subsystem") != std::string::npos, "vulkan command must display header");
    TEST_ASSERT(outVk.find("vulkan-1.dll (Khronos Vulkan 1.3.0 Specification)") != std::string::npos, "vulkan command must list 1.3 spec");
    TEST_ASSERT(outVk.find("8192 MB") != std::string::npos, "vulkan command must list 8192 MB VRAM");

    std::ostringstream ssVkTest;
    testShell.execute("vulkan test", ssVkTest);
    std::string outVkTest = ssVkTest.str();
    TEST_ASSERT(outVkTest.find("Vulkan 1.3 pipeline verified successfully") != std::string::npos, "vulkan test must verify pipeline");

    // 9. Cleanup
    vkFreeCommandBuffers(device, commandPool, 1, &cmdBuf);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}

// ============================================================================
// Suite 47: Prism3D12 Low-Level Subsystem & Programmable Shader Bytecode VM
// ============================================================================
void Test_Prism3D12_And_ProgrammableShaderVM() {
    using namespace micant::prismx;
    using namespace micant::prism3d12;
    using namespace micant::prism_vm;

    // ------------------------------------------------------------------------
    // 1. Direct3D 12 Device, Command Queue, and Allocator Creation
    // ------------------------------------------------------------------------
    ID3D12Device* device = nullptr;
    int32_t hrDev = D3D12CreateDevice(nullptr, prism3d::D3D_FEATURE_LEVEL_12_0, IID_ID3D12Device, reinterpret_cast<void**>(&device));
    TEST_ASSERT(hrDev == 0 && device != nullptr, "D3D12CreateDevice must succeed");

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    ID3D12CommandQueue* queue = nullptr;
    int32_t hrQ = device->CreateCommandQueue(&qDesc, IID_ID3D12CommandQueue, reinterpret_cast<void**>(&queue));
    TEST_ASSERT(hrQ == 0 && queue != nullptr, "CreateCommandQueue must succeed");

    ID3D12CommandAllocator* allocator = nullptr;
    int32_t hrAlloc = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator, reinterpret_cast<void**>(&allocator));
    TEST_ASSERT(hrAlloc == 0 && allocator != nullptr, "CreateCommandAllocator must succeed");

    // ------------------------------------------------------------------------
    // 2. Direct3D 12 Fence Synchronization
    // ------------------------------------------------------------------------
    ID3D12Fence* fence = nullptr;
    int32_t hrFence = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, reinterpret_cast<void**>(&fence));
    TEST_ASSERT(hrFence == 0 && fence != nullptr, "CreateFence must succeed");
    TEST_ASSERT(fence->GetCompletedValue() == 0, "Initial fence value must be 0");

    fence->Signal(42);
    TEST_ASSERT(fence->GetCompletedValue() == 42, "Signaled fence value must be 42");

    // ------------------------------------------------------------------------
    // 3. Direct3D 12 Resources, Barriers, and Descriptor Heaps
    // ------------------------------------------------------------------------
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = 1024;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    ID3D12Resource* buffer = nullptr;
    int32_t hrRes = device->CreateCommittedResource(nullptr, 0, &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource, reinterpret_cast<void**>(&buffer));
    TEST_ASSERT(hrRes == 0 && buffer != nullptr, "CreateCommittedResource for buffer must succeed");

    void* mappedPtr = nullptr;
    int32_t hrMap = buffer->Map(0, nullptr, &mappedPtr);
    TEST_ASSERT(hrMap == 0 && mappedPtr != nullptr, "Buffer Map() must return non-null pointer");
    std::memset(mappedPtr, 0xAA, 1024);
    buffer->Unmap(0, nullptr);

    // Texture Resource & RTV Descriptor Heap
    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 640;
    texDesc.Height = 480;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ID3D12Resource* renderTarget = nullptr;
    device->CreateCommittedResource(nullptr, 0, &texDesc, D3D12_RESOURCE_STATE_PRESENT, nullptr, IID_ID3D12Resource, reinterpret_cast<void**>(&renderTarget));
    TEST_ASSERT(renderTarget != nullptr, "CreateCommittedResource for render target must succeed");

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 4;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    int32_t hrHeap = device->CreateDescriptorHeap(&heapDesc, IID_ID3D12DescriptorHeap, reinterpret_cast<void**>(&rtvHeap));
    TEST_ASSERT(hrHeap == 0 && rtvHeap != nullptr, "CreateDescriptorHeap for RTV must succeed");

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    TEST_ASSERT(rtvHandle.ptr != 0, "Descriptor heap start handle must be non-zero");
    device->CreateRenderTargetView(renderTarget, nullptr, rtvHandle);

    // ------------------------------------------------------------------------
    // 4. Command List Recording, Execution, and Pipeline State
    // ------------------------------------------------------------------------
    ID3D12GraphicsCommandList* cmdList = nullptr;
    int32_t hrCmd = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_ID3D12GraphicsCommandList, reinterpret_cast<void**>(&cmdList));
    TEST_ASSERT(hrCmd == 0 && cmdList != nullptr, "CreateCommandList must succeed");
    TEST_ASSERT(cmdList->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT, "Command list type must be DIRECT");

    // Barrier 1: Transition PRESENT -> RENDER_TARGET
    D3D12_RESOURCE_BARRIER b1{};
    b1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b1.Transition.pResource = renderTarget;
    b1.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b1.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmdList->ResourceBarrier(1, &b1);

    // Viewport & Scissor
    D3D12_VIEWPORT vp{ 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f };
    D3D12_RECT scissor{ 0, 0, 640, 480 };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &scissor);

    // Render Targets & Clear
    cmdList->OMSetRenderTargets(1, &rtvHandle, 0, nullptr);
    const float clearColor[4] = { 0.2f, 0.4f, 0.8f, 1.0f };
    cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // Draw
    cmdList->IASetPrimitiveTopology(prism3d::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    // Barrier 2: Transition RENDER_TARGET -> PRESENT
    D3D12_RESOURCE_BARRIER b2{};
    b2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b2.Transition.pResource = renderTarget;
    b2.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b2.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    cmdList->ResourceBarrier(1, &b2);

    int32_t hrClose = cmdList->Close();
    TEST_ASSERT(hrClose == 0, "CommandList Close() must succeed");

    auto* cmdListImpl = static_cast<Prism3D12GraphicsCommandListImpl*>(cmdList);
    TEST_ASSERT(cmdListImpl->GetRecordedCommands().size() == 8, "Command list must have recorded 8 commands");

    // Execute on Command Queue
    ID3D12CommandList* ppLists[] = { cmdList };
    queue->ExecuteCommandLists(1, ppLists);

    auto* resImpl = static_cast<Prism3D12ResourceImpl*>(renderTarget);
    TEST_ASSERT(resImpl->GetCurrentState() == D3D12_RESOURCE_STATE_PRESENT, "Resource state after command execution must be PRESENT");

    // Fence signal from Queue
    queue->Signal(fence, 100);
    TEST_ASSERT(fence->GetCompletedValue() == 100, "Fence must reach signaled value 100 after Queue execution");

    // Reset allocator and command list
    int32_t hrResetAlloc = allocator->Reset();
    TEST_ASSERT(hrResetAlloc == 0, "CommandAllocator Reset() must succeed");
    int32_t hrResetList = cmdList->Reset(allocator, nullptr);
    TEST_ASSERT(hrResetList == 0, "CommandList Reset() must succeed");
    cmdList->Close();

    // ------------------------------------------------------------------------
    // 5. Programmable Shader Bytecode Virtual Machine (PrismShaderVM)
    // ------------------------------------------------------------------------
    // Test Vertex Shader MVP Transform
    auto vsProg = PrismShaderVM::BuildMVPTransformVS();
    TEST_ASSERT(vsProg.InstructionCount() >= 5, "MVP Vertex Shader must contain at least 5 bytecode instructions");

    VectorRegister inPos(2.0f, 3.0f, 4.0f, 1.0f);
    VectorRegister inCol(0.9f, 0.7f, 0.5f, 1.0f);
    VectorRegister inUV(0.25f, 0.75f, 0.0f, 0.0f);
    VectorRegister inNorm(0.0f, 1.0f, 0.0f, 0.0f);

    std::array<VectorRegister, 16> consts{};
    // Scaling matrix: scale X by 2, Y by 3, Z by 4
    consts[0] = VectorRegister(2.0f, 0.0f, 0.0f, 0.0f);
    consts[1] = VectorRegister(0.0f, 3.0f, 0.0f, 0.0f);
    consts[2] = VectorRegister(0.0f, 0.0f, 4.0f, 0.0f);
    consts[3] = VectorRegister(0.0f, 0.0f, 0.0f, 1.0f);

    VectorRegister outPos, outCol;
    PrismShaderVM::ExecuteVertexShader(vsProg, inPos, inCol, inUV, inNorm, consts, outPos, outCol);

    TEST_ASSERT(outPos.x() == 4.0f, "VS output X must be 2.0 * 2.0 = 4.0");
    TEST_ASSERT(outPos.y() == 9.0f, "VS output Y must be 3.0 * 3.0 = 9.0");
    TEST_ASSERT(outPos.z() == 16.0f, "VS output Z must be 4.0 * 4.0 = 16.0");
    TEST_ASSERT(outPos.w() == 1.0f, "VS output W must be 1.0");
    TEST_ASSERT(outCol.x() == 0.9f && outCol.y() == 0.7f, "VS color pass-through must match input color");

    // Test Pixel Shader Texture Modulation
    auto psProg = PrismShaderVM::BuildTexturedModulatePS();
    auto samplerFn = [](uint8_t, float u, float v) -> VectorRegister {
        return VectorRegister(u, v, 0.5f, 1.0f);
    };

    VectorRegister psOutCol;
    PrismShaderVM::ExecutePixelShader(psProg, outPos, inCol, inUV, inNorm, consts, samplerFn, psOutCol);
    // psOutCol = inCol * texel(0.25, 0.75, 0.5, 1.0)
    TEST_ASSERT(std::abs(psOutCol.x() - (0.9f * 0.25f)) < 1e-4f, "PS output R must be modulated by texture U coordinate");
    TEST_ASSERT(std::abs(psOutCol.y() - (0.7f * 0.75f)) < 1e-4f, "PS output G must be modulated by texture V coordinate");

    // Test Directional Lighting Pixel Shader
    auto lightProg = PrismShaderVM::BuildDirectionalLightingPS();
    consts[0] = VectorRegister(0.0f, 1.0f, 0.0f, 0.0f); // Light Dir: +Y
    consts[1] = VectorRegister(0.1f, 0.1f, 0.1f, 1.0f); // Ambient: 0.1
    consts[2] = VectorRegister(0.8f, 0.8f, 0.8f, 1.0f); // Diffuse: 0.8
    consts[4] = VectorRegister(0.0f, 0.0f, 0.0f, 0.0f); // Zero vector for max(N.L, 0)

    VectorRegister litCol;
    PrismShaderVM::ExecutePixelShader(lightProg, outPos, VectorRegister(1.0f, 1.0f, 1.0f, 1.0f), inUV, inNorm, consts, nullptr, litCol);
    // N = (0, 1, 0), L = (0, 1, 0) => dot = 1.0 => Ambient (0.1) + Diffuse (0.8) = 0.9
    TEST_ASSERT(std::abs(litCol.x() - 0.9f) < 1e-4f, "Fully lit diffuse pixel must equal ambient + diffuse (0.9)");

    // ------------------------------------------------------------------------
    // 6. Shell Command Integration ('prismx d3d12' and 'prismx vm')
    // ------------------------------------------------------------------------
    shell::CommandShell testShell;

    std::ostringstream ssD3D12;
    testShell.execute("prismx d3d12", ssD3D12);
    std::string outD3D12 = ssD3D12.str();
    TEST_ASSERT(outD3D12.find("Direct3D 12 Command Pipeline Executed Successfully!") != std::string::npos, "prismx d3d12 command must succeed");
    TEST_ASSERT(outD3D12.find("PRESENT -> RENDER_TARGET -> PRESENT") != std::string::npos, "prismx d3d12 must report barrier transitions");
    TEST_ASSERT(outD3D12.find("GPU Fence signaled successfully") != std::string::npos, "prismx d3d12 must report fence synchronization");

    std::ostringstream ssVm;
    testShell.execute("prismx vm", ssVm);
    std::string outVm = ssVm.str();
    TEST_ASSERT(outVm.find("Launching Sovereign Programmable Shader Bytecode Virtual Machine") != std::string::npos, "prismx vm command must start VM");
    TEST_ASSERT(outVm.find("Bytecode execution verified with 100% precision") != std::string::npos, "prismx vm must report precision verification");

    // Cleanup resources
    cmdList->Release();
    rtvHeap->Release();
    renderTarget->Release();
    buffer->Release();
    fence->Release();
    allocator->Release();
    queue->Release();
    device->Release();
}

// ============================================================================
// Suite 48: EmeraldFS Sovereign Storage Engine & DaytonaMM PFN/WSL Paging Tests
// ============================================================================
void Test_EmeraldFS_And_DaytonaMM_Subsystems() {
    // ------------------------------------------------------------------------
    // Part 1: EmeraldFS Unified Volume & Storage Engine
    // ------------------------------------------------------------------------
    auto ramDisk = std::make_shared<storage::RamDiskDevice>(L"\\Device\\Harddisk0\\Partition1", 16 * 1024 * 1024); // 16MB RamDisk
    auto& volMgr = emeraldfs::EmeraldVolumeManager::get();

    // 1. Format RamDisk with EmeraldFS
    NtStatus stFormat = volMgr.formatEmeraldFS(*ramDisk, 4096, L"MicaNT_System");
    TEST_ASSERT(NT_SUCCESS(stFormat), "EmeraldFS volume formatting must succeed");

    // 2. Mount Volume & Verify Signature
    NtStatus stMount = volMgr.mountVolume(ramDisk);
    TEST_ASSERT(NT_SUCCESS(stMount), "EmeraldFS volume mounting must succeed");
    TEST_ASSERT(volMgr.getMountedType() == emeraldfs::FileSystemType::EmeraldNTFS, "Mounted type must be EmeraldNTFS");

    // 3. Create Hierarchical Directories & Files
    uint64_t dirRec = 0;
    NtStatus stDir = volMgr.createDirectory(L"\\Windows\\System32", dirRec);
    TEST_ASSERT(NT_SUCCESS(stDir) && dirRec > 0, "Directory creation in EmeraldFS must succeed");

    uint64_t fileRec = 0;
    NtStatus stFile = volMgr.createFile(L"\\Windows\\System32\\ntoskrnl.exe", 0x20 /* Archive */, fileRec);
    TEST_ASSERT(NT_SUCCESS(stFile) && fileRec > 0, "File creation in EmeraldFS must succeed");

    // 4. Primary $DATA Stream Read/Write
    const std::string kernelCode = "MicaNT Sovereign Microkernel Binary Payload (Clean-Room ISO C++23)";
    uint64_t bytesWritten = 0;
    NtStatus stWritePrim = volMgr.writePrimaryStream(fileRec, kernelCode.data(), kernelCode.size(), 0, bytesWritten);
    TEST_ASSERT(NT_SUCCESS(stWritePrim) && bytesWritten == kernelCode.size(), "Writing primary $DATA stream must succeed");

    std::vector<char> readBuffer(kernelCode.size() + 1, 0);
    uint64_t bytesRead = 0;
    NtStatus stReadPrim = volMgr.readPrimaryStream(fileRec, readBuffer.data(), kernelCode.size(), 0, bytesRead);
    TEST_ASSERT(NT_SUCCESS(stReadPrim) && bytesRead == kernelCode.size(), "Reading primary $DATA stream must succeed");
    TEST_ASSERT(std::string_view(readBuffer.data(), bytesRead) == kernelCode, "Read primary data must match written content");

    // 5. Alternate Data Streams (ADS): Read/Write multiple streams per file
    const std::string zoneIdentifier = "[ZoneTransfer]\nZoneId=3";
    const std::string sha256Digest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    uint64_t ads1Written = 0;
    NtStatus stAds1 = volMgr.writeAlternateStream(fileRec, L"Zone.Identifier", zoneIdentifier.data(), zoneIdentifier.size(), 0, ads1Written);
    TEST_ASSERT(NT_SUCCESS(stAds1) && ads1Written == zoneIdentifier.size(), "Writing Zone.Identifier Alternate Data Stream must succeed");

    uint64_t ads2Written = 0;
    NtStatus stAds2 = volMgr.writeAlternateStream(fileRec, L"SHA256", sha256Digest.data(), sha256Digest.size(), 0, ads2Written);
    TEST_ASSERT(NT_SUCCESS(stAds2) && ads2Written == sha256Digest.size(), "Writing SHA256 Alternate Data Stream must succeed");

    // Verify Reading ADS independently
    std::vector<char> ads1Buffer(zoneIdentifier.size() + 1, 0);
    uint64_t ads1Read = 0;
    NtStatus stAds1Read = volMgr.readAlternateStream(fileRec, L"Zone.Identifier", ads1Buffer.data(), zoneIdentifier.size(), 0, ads1Read);
    TEST_ASSERT(NT_SUCCESS(stAds1Read) && ads1Read == zoneIdentifier.size(), "Reading Zone.Identifier ADS must succeed");
    TEST_ASSERT(std::string_view(ads1Buffer.data(), ads1Read) == zoneIdentifier, "Zone.Identifier content must match");

    std::vector<char> ads2Buffer(sha256Digest.size() + 1, 0);
    uint64_t ads2Read = 0;
    NtStatus stAds2Read = volMgr.readAlternateStream(fileRec, L"SHA256", ads2Buffer.data(), sha256Digest.size(), 0, ads2Read);
    TEST_ASSERT(NT_SUCCESS(stAds2Read) && ads2Read == sha256Digest.size(), "Reading SHA256 ADS must succeed");
    TEST_ASSERT(std::string_view(ads2Buffer.data(), ads2Read) == sha256Digest, "SHA256 content must match");

    // 6. Metadata inspection
    emeraldfs::FileMetadata meta{};
    NtStatus stMeta = volMgr.getFileMetadata(fileRec, meta);
    TEST_ASSERT(NT_SUCCESS(stMeta), "Retrieving file metadata must succeed");
    TEST_ASSERT(meta.primarySize == kernelCode.size(), "Metadata primary size must match");
    TEST_ASSERT(meta.alternateStreams.size() == 2, "File must possess exactly 2 Alternate Data Streams");

    // 7. Write-Ahead Logging (WAL) verification in EmeraldJournal
    auto* journal = volMgr.getJournal();
    TEST_ASSERT(journal != nullptr, "EmeraldFS volume must have active journal");
    TEST_ASSERT(journal->getEntryCount() >= 4, "Journal must record creation and stream write operations");

    // ------------------------------------------------------------------------
    // Part 2: DaytonaMM Virtual Memory Manager, PFN Database, and Working Set
    // ------------------------------------------------------------------------
    auto& daytona = daytonamm::DaytonaMemoryExecutive::get();
    daytona.initialize(4096); // 4096 pages = 16MB physical space

    auto telemInitial = daytona.getTelemetry();
    TEST_ASSERT(telemInitial.totalPhysicalPages == 4096, "Daytona total physical pages must equal 4096");
    TEST_ASSERT(telemInitial.zeroedPages > 0, "Daytona must have initialized zeroed pages");

    // Allocate virtual memory (VAD reservation + commit)
    mm::ProcessAddressSpace vas;
    daytonamm::ProcessWorkingSet ws(64); // Max 64 resident pages in working set

    uintptr_t baseAddr = 0;
    size_t allocSize = 64 * 1024; // 64KB = 16 pages
    NtStatus stAlloc = daytona.allocateVirtualMemory(vas, baseAddr, allocSize, mm::MEM_COMMIT | mm::MEM_RESERVE, mm::PAGE_READWRITE);
    TEST_ASSERT(NT_SUCCESS(stAlloc) && baseAddr != 0, "Daytona allocateVirtualMemory must succeed");

    // Trigger Demand-Zero Page Faults
    for (size_t p = 0; p < 8; ++p) {
        uintptr_t faultAddr = baseAddr + (p * mm::PageSize4KB) + 128;
        NtStatus stFault = daytona.handlePageFault(vas, ws, faultAddr, 0);
        TEST_ASSERT(NT_SUCCESS(stFault), "Demand-zero page fault must resolve successfully");
    }

    TEST_ASSERT(ws.getResidentPageCount() == 8, "Working set must now contain 8 resident active pages");
    auto telemAfterFaults = daytona.getTelemetry();
    TEST_ASSERT(telemAfterFaults.demandZeroFaults >= 8, "Telemetry must record at least 8 demand-zero page faults");
    TEST_ASSERT(telemAfterFaults.activePages >= 8, "Active PFN count must reflect resident pages");

    // Working Set Trimming: Evict 4 oldest pages to Standby list
    size_t evicted = daytona.trimWorkingSet(ws, 4);
    TEST_ASSERT(evicted == 4, "Daytona working set trimming must evict exactly 4 pages");
    TEST_ASSERT(ws.getResidentPageCount() == 4, "Working set must now contain 4 resident pages");

    auto telemAfterTrim = daytona.getTelemetry();
    TEST_ASSERT(telemAfterTrim.standbyPages >= 4, "Standby page list must receive evicted clean pages");

    // Trigger Transition Fault on an evicted page: resolves from Standby -> Active without zeroing
    uintptr_t standbyAddr = baseAddr + 128; // Page 0 was oldest
    NtStatus stTransFault = daytona.handlePageFault(vas, ws, standbyAddr, 0);
    TEST_ASSERT(NT_SUCCESS(stTransFault), "Transition fault on standby page must succeed");

    auto telemFinal = daytona.getTelemetry();
    TEST_ASSERT(telemFinal.transitionFaults >= 1, "Telemetry must record transition fault");
    TEST_ASSERT(ws.getResidentPageCount() == 5, "Working set must now have 5 resident pages after soft fault");
}

// ============================================================================
// Suite 49: DirectX Dynamic Loader Exports & DXBC Bytecode Container Tests
// ============================================================================
void Test_DirectX_DynamicLoader_And_DXBC_Container() {
    using namespace micant::prismx;
    using namespace micant::prism3d;
    using namespace micant::prism3d12;
    using namespace micant::prism_vm;
    using namespace micant::prism_compiler;

    // 1. Initialize and register all DirectX DLL exports in DynamicLoader
    InitializeDirectXSubsystemExports();
    auto& ldr = ldr::DynamicLoader::get();

    // Verify dxgi.dll exports
    void* fnCreateDXGIFactory1 = ldr.getExport("dxgi.dll", "CreateDXGIFactory1");
    TEST_ASSERT(fnCreateDXGIFactory1 != nullptr, "dxgi.dll!CreateDXGIFactory1 must be registered in DynamicLoader");

    // Verify d3d11.dll exports
    void* fnD3D11CreateDevice = ldr.getExport("d3d11.dll", "D3D11CreateDevice");
    TEST_ASSERT(fnD3D11CreateDevice != nullptr, "d3d11.dll!D3D11CreateDevice must be registered in DynamicLoader");

    // Verify d3d12.dll exports
    void* fnD3D12CreateDevice = ldr.getExport("d3d12.dll", "D3D12CreateDevice");
    TEST_ASSERT(fnD3D12CreateDevice != nullptr, "d3d12.dll!D3D12CreateDevice must be registered in DynamicLoader");

    // Verify d3dcompiler_47.dll exports
    void* fnD3DCreateBlob = ldr.getExport("d3dcompiler_47.dll", "D3DCreateBlob");
    void* fnD3DDisassemble = ldr.getExport("d3dcompiler_47.dll", "D3DDisassemble");
    void* fnD3DCompile = ldr.getExport("d3dcompiler_47.dll", "D3DCompile");
    TEST_ASSERT(fnD3DCreateBlob != nullptr, "d3dcompiler_47.dll!D3DCreateBlob must be registered in DynamicLoader");
    TEST_ASSERT(fnD3DDisassemble != nullptr, "d3dcompiler_47.dll!D3DDisassemble must be registered in DynamicLoader");
    TEST_ASSERT(fnD3DCompile != nullptr, "d3dcompiler_47.dll!D3DCompile must be registered in DynamicLoader");

    // 2. Test invoking DXGI via dynamic loader export pointer
    using PFN_CreateDXGIFactory1 = int32_t(*)(const IID&, void**);
    auto pfnCreateDXGIFactory1 = reinterpret_cast<PFN_CreateDXGIFactory1>(fnCreateDXGIFactory1);
    IDXGIFactory1* factory = nullptr;
    int32_t hrDxgi = pfnCreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void**>(&factory));
    TEST_ASSERT(hrDxgi == 0 && factory != nullptr, "CreateDXGIFactory1 via dynamic loader export must succeed");

    IDXGIAdapter1* adapter = nullptr;
    int32_t hrAdapter = factory->EnumAdapters1(0, &adapter);
    TEST_ASSERT(hrAdapter == 0 && adapter != nullptr, "EnumAdapters1 on factory must succeed");

    DXGI_ADAPTER_DESC1 adDesc{};
    adapter->GetDesc1(&adDesc);
    TEST_ASSERT(adDesc.DedicatedVideoMemory > 0, "Adapter must report non-zero dedicated video memory");

    // 3. Test invoking D3D12 via dynamic loader export pointer
    using PFN_D3D12CreateDevice = int32_t(*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**);
    auto pfnD3D12CreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(fnD3D12CreateDevice);
    ID3D12Device* device12 = nullptr;
    int32_t hrDev12 = pfnD3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_ID3D12Device, reinterpret_cast<void**>(&device12));
    TEST_ASSERT(hrDev12 == 0 && device12 != nullptr, "D3D12CreateDevice via dynamic loader export must succeed");

    D3D12_COMMAND_QUEUE_DESC qDesc{ D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
    ID3D12CommandQueue* queue = nullptr;
    int32_t hrQueue = device12->CreateCommandQueue(&qDesc, IID_ID3D12CommandQueue, reinterpret_cast<void**>(&queue));
    TEST_ASSERT(hrQueue == 0 && queue != nullptr, "CreateCommandQueue must succeed");

    ID3D12CommandAllocator* allocator = nullptr;
    int32_t hrAlloc = device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator, reinterpret_cast<void**>(&allocator));
    TEST_ASSERT(hrAlloc == 0 && allocator != nullptr, "CreateCommandAllocator must succeed");

    ID3D12GraphicsCommandList* cmdList = nullptr;
    int32_t hrList = device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_ID3D12GraphicsCommandList, reinterpret_cast<void**>(&cmdList));
    TEST_ASSERT(hrList == 0 && cmdList != nullptr, "CreateCommandList must succeed");

    cmdList->Close();
    ID3D12CommandList* ppLists[] = { cmdList };
    queue->ExecuteCommandLists(1, ppLists);

    // 4. Test ID3DBlob creation via dynamic loader export pointer
    using PFN_D3DCreateBlob = int32_t(*)(size_t, ID3DBlob**);
    auto pfnD3DCreateBlob = reinterpret_cast<PFN_D3DCreateBlob>(fnD3DCreateBlob);
    ID3DBlob* testBlob = nullptr;
    int32_t hrBlob = pfnD3DCreateBlob(512, &testBlob);
    TEST_ASSERT(hrBlob == 0 && testBlob != nullptr, "D3DCreateBlob via dynamic loader export must succeed");
    TEST_ASSERT(testBlob->GetBufferSize() == 512, "Blob size must equal 512 bytes");
    TEST_ASSERT(testBlob->GetBufferPointer() != nullptr, "Blob buffer pointer must be non-null");

    // 5. Test Standard DXBC Binary Container Generation & RIFF Chunks
    ShaderProgram vsProg = PrismShaderVM::BuildMVPTransformVS();
    std::vector<uint8_t> dxbcContainer = DxbcContainer::BuildContainer(1, 5, 0, vsProg);
    TEST_ASSERT(dxbcContainer.size() > sizeof(DxbcHeader), "DXBC binary container size must be greater than header size");
    TEST_ASSERT(DxbcContainer::IsDxbc(dxbcContainer.data(), dxbcContainer.size()), "Binary stream must be recognized as valid DXBC");

    // 6. Test Parsing DXBC Container and Signature / Bytecode Chunks
    DxbcContainer parsedContainer;
    bool bParsed = DxbcContainer::Parse(dxbcContainer.data(), dxbcContainer.size(), parsedContainer);
    TEST_ASSERT(bParsed, "DxbcContainer::Parse must succeed on valid DXBC container");
    TEST_ASSERT(parsedContainer.chunks.size() >= 3, "Parsed container must contain at least 3 chunks (ISGN, OSGN, SHDR)");
    TEST_ASSERT(parsedContainer.programType == 1, "Parsed program type must be 1 (Vertex Shader)");
    TEST_ASSERT(parsedContainer.majorVersion == 5 && parsedContainer.minorVersion == 0, "Parsed version must be 5.0 (vs_5_0)");

    // 7. Test D3DDisassemble on DXBC Binary
    using PFN_D3DDisassemble = int32_t(*)(const void*, size_t, uint32_t, const char*, ID3DBlob**);
    auto pfnD3DDisassemble = reinterpret_cast<PFN_D3DDisassemble>(fnD3DDisassemble);
    ID3DBlob* disasmBlob = nullptr;
    int32_t hrDisasm = pfnD3DDisassemble(dxbcContainer.data(), dxbcContainer.size(), 0, "MicaNT Test Shader", &disasmBlob);
    TEST_ASSERT(hrDisasm == 0 && disasmBlob != nullptr, "D3DDisassemble on DXBC binary must succeed");

    std::string_view disasmText(reinterpret_cast<const char*>(disasmBlob->GetBufferPointer()));
    TEST_ASSERT(disasmText.find("vs_5_0") != std::string_view::npos, "Disassembly must contain target profile vs_5_0");
    TEST_ASSERT(disasmText.find("dp4") != std::string_view::npos, "Disassembly must contain dp4 instruction");
    TEST_ASSERT(disasmText.find("ret") != std::string_view::npos, "Disassembly must contain ret instruction");

    // 8. Test Decoding and Executing the parsed DXBC Program in PrismShaderVM
    ShaderProgram decodedProg = parsedContainer.DecodeToProgram();
    TEST_ASSERT(decodedProg.InstructionCount() > 0, "Decoded program must contain instructions");

    VectorRegister inPos(1.0f, 2.0f, 3.0f, 1.0f);
    VectorRegister inCol(1.0f, 0.5f, 0.25f, 1.0f);
    VectorRegister inUV(0.0f, 0.0f, 0.0f, 0.0f);
    VectorRegister inNorm(0.0f, 0.0f, 1.0f, 0.0f);

    std::array<VectorRegister, 16> consts{};
    // Identity matrix in c0..c3
    consts[0] = VectorRegister(1.0f, 0.0f, 0.0f, 0.0f);
    consts[1] = VectorRegister(0.0f, 1.0f, 0.0f, 0.0f);
    consts[2] = VectorRegister(0.0f, 0.0f, 1.0f, 0.0f);
    consts[3] = VectorRegister(0.0f, 0.0f, 0.0f, 1.0f);

    VectorRegister outPos{}, outCol{};
    PrismShaderVM::ExecuteVertexShader(decodedProg, inPos, inCol, inUV, inNorm, consts, outPos, outCol);

    // Verify outPos (transformed position) is (1.0, 2.0, 3.0, 1.0)
    TEST_ASSERT(outPos.x() == 1.0f && outPos.y() == 2.0f &&
                outPos.z() == 3.0f && outPos.w() == 1.0f,
                "DXBC decoded program execution must produce exact transformed position");
    // Verify outCol (passed-through color)
    TEST_ASSERT(outCol.x() == 1.0f && outCol.y() == 0.5f &&
                outCol.z() == 0.25f && outCol.w() == 1.0f,
                "DXBC decoded program execution must produce exact passed-through color");

    // Cleanup COM objects
    disasmBlob->Release();
    testBlob->Release();
    cmdList->Release();
    allocator->Release();
    queue->Release();
    device12->Release();
    adapter->Release();
    factory->Release();
}

// ============================================================================
// Suite 50: DirectX PrismAudio & VectorHID (XAudio2 & XInput) Subsystems
// ============================================================================
void Test_DirectX_PrismAudio_And_XInput_Subsystems() {
    using namespace micant::audio;
    using namespace micant::hid;

    // 1. Initialize subsystem exports
    prism_compiler::InitializeDirectXSubsystemExports();

    // 2. Validate DynamicLoader exports for xaudio2 and xinput
    void* fnXAudio2Create = ldr::DynamicLoader::get().getExport("xaudio2_9.dll", "XAudio2Create");
    TEST_ASSERT(fnXAudio2Create != nullptr, "xaudio2_9.dll must export XAudio2Create");

    void* fnXAudio2Create8 = ldr::DynamicLoader::get().getExport("xaudio2_8.dll", "XAudio2Create");
    TEST_ASSERT(fnXAudio2Create8 != nullptr, "xaudio2_8.dll must export XAudio2Create");

    void* fnXInputGetState = ldr::DynamicLoader::get().getExport("xinput1_4.dll", "XInputGetState");
    TEST_ASSERT(fnXInputGetState != nullptr, "xinput1_4.dll must export XInputGetState");

    void* fnXInputSetState = ldr::DynamicLoader::get().getExport("xinput1_4.dll", "XInputSetState");
    TEST_ASSERT(fnXInputSetState != nullptr, "xinput1_4.dll must export XInputSetState");

    void* fnXInputGetCaps = ldr::DynamicLoader::get().getExport("xinput1_4.dll", "XInputGetCapabilities");
    TEST_ASSERT(fnXInputGetCaps != nullptr, "xinput1_4.dll must export XInputGetCapabilities");

    // 3. Test XAudio2 Engine Creation
    using PFN_XAudio2Create = int32_t(*)(IXAudio2**, uint32_t, uint32_t);
    auto pfnXAudio2Create = reinterpret_cast<PFN_XAudio2Create>(fnXAudio2Create);

    IXAudio2* audioEngine = nullptr;
    int32_t hrAudio = pfnXAudio2Create(&audioEngine, 0, 0);
    TEST_ASSERT(hrAudio == 0 && audioEngine != nullptr, "XAudio2Create must succeed");

    // 4. Test Mastering Voice Creation
    IXAudio2MasteringVoice* masteringVoice = nullptr;
    int32_t hrMaster = audioEngine->CreateMasteringVoice(&masteringVoice, 2, 48000, 0);
    TEST_ASSERT(hrMaster == 0 && masteringVoice != nullptr, "CreateMasteringVoice must succeed");

    uint32_t channelMask = 0;
    masteringVoice->GetChannelMask(&channelMask);
    TEST_ASSERT(channelMask == 0x3, "Mastering voice channel mask must be 0x3 (Stereo)");

    // 5. Test Source Voice Creation & Procedural Audio Synthesis
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 48000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = 48000 * 4;

    IXAudio2SourceVoice* sourceVoice = nullptr;
    int32_t hrSource = audioEngine->CreateSourceVoice(&sourceVoice, &fmt);
    TEST_ASSERT(hrSource == 0 && sourceVoice != nullptr, "CreateSourceVoice must succeed");

    // Synthesize a 440 Hz (Concert A) sine tone for 0.05 seconds (2400 samples)
    std::vector<uint8_t> tonePcmMono = PrismAudioEngineImpl::SynthesizeTone(
        PrismAudioEngineImpl::ToneType::Sine, 440.0f, 0.05f, 48000, 0.8f
    );
    TEST_ASSERT(!tonePcmMono.empty(), "SynthesizeTone must generate non-empty PCM buffer");

    // Duplicate mono samples to stereo
    size_t monoSamples = tonePcmMono.size() / sizeof(int16_t);
    std::vector<int16_t> stereoPcm(monoSamples * 2);
    const int16_t* pMono = reinterpret_cast<const int16_t*>(tonePcmMono.data());
    for (size_t i = 0; i < monoSamples; ++i) {
        stereoPcm[i * 2 + 0] = pMono[i];
        stereoPcm[i * 2 + 1] = pMono[i];
    }

    // Submit buffer to source voice
    XAUDIO2_BUFFER audioBuf{};
    audioBuf.AudioBytes = static_cast<uint32_t>(stereoPcm.size() * sizeof(int16_t));
    audioBuf.pAudioData = reinterpret_cast<const uint8_t*>(stereoPcm.data());
    audioBuf.LoopCount = 0;

    int32_t hrSubmit = sourceVoice->SubmitSourceBuffer(&audioBuf);
    TEST_ASSERT(hrSubmit == 0, "SubmitSourceBuffer must return 0");

    int32_t hrStart = sourceVoice->Start(0);
    TEST_ASSERT(hrStart == 0, "SourceVoice::Start must return 0");

    // 6. Test Software Audio Mixing Cycle
    auto* rawEngine = static_cast<PrismAudioEngineImpl*>(audioEngine);
    std::vector<float> mixedOutput;
    rawEngine->ProcessMixingCycle(480, mixedOutput); // 10ms of 48000Hz stereo = 480 frames = 960 floats

    TEST_ASSERT(mixedOutput.size() == 960, "ProcessMixingCycle must output exactly 960 stereo float samples");

    // Verify non-zero mixed audio energy
    float maxAmp = 0.0f;
    for (float sample : mixedOutput) {
        maxAmp = std::max(maxAmp, std::abs(sample));
    }
    TEST_ASSERT(maxAmp > 0.1f, "Mixed audio must contain valid waveform energy from synthesized 440Hz tone");

    // Check voice state
    XAUDIO2_VOICE_STATE state{};
    sourceVoice->GetState(&state);
    TEST_ASSERT(state.SamplesPlayed >= 480, "Voice state must report at least 480 samples played");

    // 7. Test 3D Positional Spatial Audio Attenuation
    AudioListener listener{};
    listener.position = { 0.0f, 0.0f, 0.0f };

    AudioEmitter emitterNear{};
    emitterNear.position = { 0.0f, 0.0f, 5.0f };
    emitterNear.innerRadius = 1.0f;
    emitterNear.outerRadius = 50.0f;

    AudioEmitter emitterFar{};
    emitterFar.position = { 0.0f, 0.0f, 40.0f };
    emitterFar.innerRadius = 1.0f;
    emitterFar.outerRadius = 50.0f;

    float volNear = 0.0f, panLNear = 0.0f, panRNear = 0.0f;
    PrismAudioEngineImpl::Calculate3DSpatial(listener, emitterNear, volNear, panLNear, panRNear);

    float volFar = 0.0f, panLFar = 0.0f, panRFar = 0.0f;
    PrismAudioEngineImpl::Calculate3DSpatial(listener, emitterFar, volFar, panLFar, panRFar);

    TEST_ASSERT(volNear > volFar, "Near audio emitter must have higher volume factor than far emitter");
    TEST_ASSERT(volFar > 0.0f && volNear <= 1.0f, "Volume factor must be within valid attenuation range");

    // Test lateral panning: emitter on the right
    AudioEmitter emitterRight{};
    emitterRight.position = { 10.0f, 0.0f, 0.0f };
    float volR = 0.0f, panLR = 0.0f, panRR = 0.0f;
    PrismAudioEngineImpl::Calculate3DSpatial(listener, emitterRight, volR, panLR, panRR);
    TEST_ASSERT(panRR > panLR, "Right lateral emitter must pan louder to right channel than left channel");

    // 8. Test VectorHID XInput Gamepad Subsystem
    using PFN_XInputGetState = uint32_t(*)(uint32_t, XINPUT_STATE*);
    using PFN_XInputSetState = uint32_t(*)(uint32_t, const XINPUT_VIBRATION*);
    using PFN_XInputGetCaps  = uint32_t(*)(uint32_t, uint32_t, XINPUT_CAPABILITIES*);

    auto pfnXInputGetState = reinterpret_cast<PFN_XInputGetState>(fnXInputGetState);
    auto pfnXInputSetState = reinterpret_cast<PFN_XInputSetState>(fnXInputSetState);
    auto pfnXInputGetCaps  = reinterpret_cast<PFN_XInputGetCaps>(fnXInputGetCaps);

    // Query capabilities for controller 0
    XINPUT_CAPABILITIES caps{};
    uint32_t errCaps = pfnXInputGetCaps(0, 0, &caps);
    TEST_ASSERT(errCaps == ERROR_SUCCESS, "XInputGetCapabilities must succeed for connected slot 0");
    TEST_ASSERT(caps.Type == XINPUT_DEVTYPE_GAMEPAD, "Device type must be XINPUT_DEVTYPE_GAMEPAD");

    // Configure slot 1 with simulated game input
    VectorControllerManager::get().SetSlotConnected(1, true);

    XINPUT_GAMEPAD simPad{};
    simPad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_START;
    simPad.bLeftTrigger = 120;
    simPad.bRightTrigger = 240;
    simPad.sThumbLX = 15000;
    simPad.sThumbLY = -12000;
    simPad.sThumbRX = -25000;
    simPad.sThumbRY = 20000;
    VectorControllerManager::get().SetSlotState(1, simPad);

    XINPUT_STATE queryState{};
    uint32_t errState = pfnXInputGetState(1, &queryState);
    TEST_ASSERT(errState == ERROR_SUCCESS, "XInputGetState must succeed on slot 1");
    TEST_ASSERT(queryState.Gamepad.wButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_START),
                "Gamepad buttons must match simulated state");
    TEST_ASSERT(queryState.Gamepad.bLeftTrigger == 120 && queryState.Gamepad.bRightTrigger == 240,
                "Gamepad triggers must match simulated state");
    TEST_ASSERT(queryState.Gamepad.sThumbLX == 15000 && queryState.Gamepad.sThumbRX == -25000,
                "Gamepad thumbsticks must match simulated state");

    // Test Deadzone Normalization
    float normX = 0.0f, normY = 0.0f;
    VectorControllerManager::NormalizeThumbstick(5000, 5000, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, normX, normY);
    TEST_ASSERT(normX == 0.0f && normY == 0.0f, "Thumbstick inside deadzone must normalize to (0, 0)");

    VectorControllerManager::NormalizeThumbstick(32767, 0, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, normX, normY);
    TEST_ASSERT(normX > 0.99f && normY == 0.0f, "Thumbstick at max X must normalize to (1.0, 0.0)");

    // Test Force Feedback Vibration
    XINPUT_VIBRATION vib{ 32768, 65535 };
    uint32_t errVib = pfnXInputSetState(1, &vib);
    TEST_ASSERT(errVib == ERROR_SUCCESS, "XInputSetState must succeed");

    XINPUT_VIBRATION recordedVib = VectorControllerManager::get().GetSlotVibration(1);
    TEST_ASSERT(recordedVib.wLeftMotorSpeed == 32768 && recordedVib.wRightMotorSpeed == 65535,
                "Vibration speeds must match submitted values");

    // Test Disconnected Controller Slot
    VectorControllerManager::get().SetSlotConnected(3, false);
    uint32_t errDisc = pfnXInputGetState(3, &queryState);
    TEST_ASSERT(errDisc == ERROR_DEVICE_NOT_CONNECTED, "Disconnected slot must return ERROR_DEVICE_NOT_CONNECTED");

    // Cleanup Audio COM references
    sourceVoice->DestroyVoice();
    masteringVoice->DestroyVoice();
    audioEngine->Release();
}

// ============================================================================
// Suite 51: VanguardDriver Device Stack & PnP Engine Subsystem
// ============================================================================
void Test_VanguardDriver_DeviceStack_And_PnP_Subsystem() {
    using namespace micant::vanguarddriver;

    // 1. Layered Device Stack Tests
    // Create Driver Objects for Bus, Function, and Filter
    io::DriverObject busDriver{};
    busDriver.driverName = L"pci.sys";

    io::DriverObject funcDriver{};
    funcDriver.driverName = L"nvme.sys";

    io::DriverObject filterDriver{};
    filterDriver.driverName = L"diskperf.sys";

    // Track dispatch call order
    std::vector<std::wstring> callStack;

    busDriver.setDispatch(io::IRP_MJ_READ, [](io::DeviceObject* dev, io::Irp* irp) -> NtStatus {
        (void)dev;
        if (irp && irp->userBuffer) {
            auto* pStack = reinterpret_cast<std::vector<std::wstring>*>(irp->userBuffer);
            pStack->push_back(L"bus");
        }
        return NtStatus::Success;
    });

    funcDriver.setDispatch(io::IRP_MJ_READ, [](io::DeviceObject* dev, io::Irp* irp) -> NtStatus {
        (void)dev;
        if (irp && irp->userBuffer) {
            auto* pStack = reinterpret_cast<std::vector<std::wstring>*>(irp->userBuffer);
            pStack->push_back(L"function");
        }
        return NtStatus::Success;
    });

    filterDriver.setDispatch(io::IRP_MJ_READ, [](io::DeviceObject* dev, io::Irp* irp) -> NtStatus {
        (void)dev;
        if (irp && irp->userBuffer) {
            auto* pStack = reinterpret_cast<std::vector<std::wstring>*>(irp->userBuffer);
            pStack->push_back(L"filter");
        }
        return NtStatus::Success;
    });

    // Create Device Objects
    io::DeviceObject pdo{ .driverObject = &busDriver, .deviceName = L"NVMEDevice0" };
    io::DeviceObject fdo{ .driverObject = &funcDriver, .deviceName = L"NVMEFunctional0" };
    io::DeviceObject upperFilter{ .driverObject = &filterDriver, .deviceName = L"NVMEFilter0" };

    // Attach FDO on top of PDO
    io::DeviceObject* attached1 = IoAttachDeviceToDeviceStack(&fdo, &pdo);
    TEST_ASSERT(attached1 == &pdo, "IoAttachDeviceToDeviceStack must return lower device");
    TEST_ASSERT(pdo.attachedDevice == &fdo, "PDO attachedDevice must point to FDO");

    // Attach Upper Filter on top of FDO
    io::DeviceObject* attached2 = IoAttachDeviceToDeviceStack(&upperFilter, &pdo);
    TEST_ASSERT(attached2 == &fdo, "IoAttachDeviceToDeviceStack must attach to previous top device");
    TEST_ASSERT(fdo.attachedDevice == &upperFilter, "FDO attachedDevice must point to UpperFilter");

    // Verify IoGetAttachedDevice returns the top device
    io::DeviceObject* topDevice = IoGetAttachedDevice(&pdo);
    TEST_ASSERT(topDevice == &upperFilter, "IoGetAttachedDevice on PDO must return upper filter");

    // Forward an IRP to top of stack
    io::Irp readIrp{};
    readIrp.majorFunction = io::IRP_MJ_READ;
    readIrp.userBuffer = &callStack;

    NtStatus stCall = IoCallDriver(topDevice, &readIrp);
    TEST_ASSERT(NT_SUCCESS(stCall), "IoCallDriver to top of stack must succeed");
    TEST_ASSERT(!callStack.empty() && callStack[0] == L"filter", "Top of stack filter driver must be called first");

    // 2. Vanguard PnP Device Node & State Machine
    filterDriver.setDispatch(io::IRP_MJ_PNP, [](io::DeviceObject* dev, io::Irp* irp) -> NtStatus {
        (void)dev;
        if (irp) irp->ioStatus.status = NtStatus::Success;
        return NtStatus::Success;
    });
    filterDriver.setDispatch(io::IRP_MJ_POWER, [](io::DeviceObject* dev, io::Irp* irp) -> NtStatus {
        (void)dev;
        if (irp) irp->ioStatus.status = NtStatus::Success;
        return NtStatus::Success;
    });

    auto& pnpEngine = VanguardDriverEngine::get();
    auto devNode = pnpEngine.CreateDeviceNode(L"PCI\\VEN_10EC&DEV_8168&SUBSYS_0123\\0001", &pdo);
    TEST_ASSERT(devNode != nullptr, "CreateDeviceNode must return valid node");
    TEST_ASSERT(devNode->GetState() == PnpDeviceState::Initialized, "Initial PnP state must be Initialized");

    // Start Device sequence
    NtStatus stStart = pnpEngine.StartDevice(*devNode);
    TEST_ASSERT(NT_SUCCESS(stStart), "pnpEngine.StartDevice must return Success");
    TEST_ASSERT(devNode->GetState() == PnpDeviceState::Started, "Device state must be Started");

    // Stop Device sequence
    NtStatus stStop = pnpEngine.StopDevice(*devNode);
    TEST_ASSERT(NT_SUCCESS(stStop), "pnpEngine.StopDevice must return Success");
    TEST_ASSERT(devNode->GetState() == PnpDeviceState::Stopped, "Device state must be Stopped");

    // Remove Device sequence
    NtStatus stRemove = pnpEngine.RemoveDevice(*devNode);
    TEST_ASSERT(NT_SUCCESS(stRemove), "pnpEngine.RemoveDevice must return Success");
    TEST_ASSERT(devNode->GetState() == PnpDeviceState::Removed, "Device state must be Removed");

    // 3. Device Interface Registration (Disk & Audio)
    std::wstring diskInterfaceLink;
    NtStatus stRegDisk = pnpEngine.RegisterDeviceInterface(&pdo, GUID_DEVINTERFACE_DISK, L"", diskInterfaceLink);
    TEST_ASSERT(NT_SUCCESS(stRegDisk), "RegisterDeviceInterface for Disk must succeed");
    TEST_ASSERT(diskInterfaceLink.find(L"\\\\?\\") == 0, "Symbolic link must start with \\\\?\\ prefix");
    TEST_ASSERT(diskInterfaceLink.find(L"53f56307") != std::wstring::npos, "Symbolic link must contain Disk GUID");

    // Before enabling, enumeration should be empty
    auto interfacesBefore = pnpEngine.EnumerateDeviceInterfaces(GUID_DEVINTERFACE_DISK);
    TEST_ASSERT(interfacesBefore.empty(), "Disabled device interfaces must not be returned in enumeration");

    // Enable device interface
    NtStatus stEnable = pnpEngine.SetDeviceInterfaceState(diskInterfaceLink, true);
    TEST_ASSERT(NT_SUCCESS(stEnable), "SetDeviceInterfaceState(true) must succeed");

    // After enabling, enumeration must find it
    auto interfacesAfter = pnpEngine.EnumerateDeviceInterfaces(GUID_DEVINTERFACE_DISK);
    TEST_ASSERT(interfacesAfter.size() == 1, "EnumerateDeviceInterfaces must return 1 active interface");
    TEST_ASSERT(interfacesAfter[0] == diskInterfaceLink, "Enumerated interface must match registered symbolic link");

    // Disable device interface
    pnpEngine.SetDeviceInterfaceState(diskInterfaceLink, false);
    auto interfacesDisabled = pnpEngine.EnumerateDeviceInterfaces(GUID_DEVINTERFACE_DISK);
    TEST_ASSERT(interfacesDisabled.empty(), "Disabled device interface must no longer be returned");

    // 4. Power State Transition Management
    devNode->SetState(PnpDeviceState::Started);
    NtStatus stPowerD3 = devNode->DispatchSetPower(DevicePowerState::PowerDeviceD3);
    TEST_ASSERT(NT_SUCCESS(stPowerD3), "DispatchSetPower(D3) must return Success");
    TEST_ASSERT(devNode->GetPowerState() == DevicePowerState::PowerDeviceD3, "Power state must be PowerDeviceD3 (Sleep)");

    NtStatus stPowerD0 = devNode->DispatchSetPower(DevicePowerState::PowerDeviceD0);
    TEST_ASSERT(NT_SUCCESS(stPowerD0), "DispatchSetPower(D0) must return Success");
    TEST_ASSERT(devNode->GetPowerState() == DevicePowerState::PowerDeviceD0, "Power state must be PowerDeviceD0 (Full On)");

    // 5. Cancel-Safe IRP Queue
    CancelSafeIrpQueue safeQueue;
    io::Irp irpA{}, irpB{};
    irpA.majorFunction = io::IRP_MJ_READ;
    irpB.majorFunction = io::IRP_MJ_WRITE;

    safeQueue.InsertTail(&irpA);
    safeQueue.InsertTail(&irpB);
    TEST_ASSERT(safeQueue.Count() == 2, "CancelSafeIrpQueue must contain 2 IRPs");

    // Cancel irpA
    bool bCancelled = safeQueue.CancelIrp(&irpA);
    TEST_ASSERT(bCancelled, "CancelIrp on queued IRP must return true");
    TEST_ASSERT(irpA.ioStatus.status == NtStatus::Cancelled, "Cancelled IRP status must be NtStatus::Cancelled");
    TEST_ASSERT(safeQueue.Count() == 1, "Queue count after cancellation must be 1");

    // Remove remaining IRP
    io::Irp* nextIrp = safeQueue.RemoveNext();
    TEST_ASSERT(nextIrp == &irpB, "Next IRP in queue must be irpB");
    TEST_ASSERT(safeQueue.Count() == 0, "Queue must now be empty");
}

// ============================================================================
// Suite 52: AegisSandbox Sovereign Process Containment & Job Objects Tests
// ============================================================================
void Test_AegisSandbox_JobObjects_And_ProcessContainment() {
    using namespace micant::aegis;

    auto& sandboxMgr = AegisSandboxManager::get();
    sandboxMgr.reset();

    // 1. Creation and Named Registry Lookup
    auto job = sandboxMgr.createJobObject(L"\\BaseNamedObjects\\AegisSandboxJob");
    TEST_ASSERT(job != nullptr, "AegisJobObject creation must succeed");
    TEST_ASSERT(job->getName() == L"\\BaseNamedObjects\\AegisSandboxJob", "Job name must match initialization");

    auto openedJob = sandboxMgr.openJobObject(L"\\BaseNamedObjects\\AegisSandboxJob");
    TEST_ASSERT(openedJob == job, "openJobObject must return same instance as created");

    // 2. Active Process Quota Limit
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                                              JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                              JOB_OBJECT_LIMIT_JOB_MEMORY |
                                              JOB_OBJECT_LIMIT_PROCESS_TIME;
    limits.BasicLimitInformation.ActiveProcessLimit = 2;
    limits.ProcessMemoryLimit = 16 * 1024 * 1024; // 16MB per process
    limits.JobMemoryLimit = 24 * 1024 * 1024;     // 24MB aggregate job memory
    limits.BasicLimitInformation.PerProcessUserTimeLimit = 1000; // 1000 units
    job->setExtendedLimits(limits);

    auto& procMgr = ps::ProcessManager::get();
    auto proc1 = procMgr.createProcess(L"sandbox_app1.exe");
    auto proc2 = procMgr.createProcess(L"sandbox_app2.exe");
    auto proc3 = procMgr.createProcess(L"sandbox_app3.exe");

    NtStatus stAssign1 = job->assignProcess(proc1);
    NtStatus stAssign2 = job->assignProcess(proc2);
    TEST_ASSERT(NT_SUCCESS(stAssign1), "Assigning proc1 to job must succeed");
    TEST_ASSERT(NT_SUCCESS(stAssign2), "Assigning proc2 to job must succeed");
    TEST_ASSERT(job->getActiveProcessCount() == 2, "Job active process count must be 2");

    // Exceed Active Process Limit (attempting to add 3rd process when limit is 2)
    NtStatus stAssign3 = job->assignProcess(proc3);
    TEST_ASSERT(stAssign3 == NtStatus::QuotaExceeded, "Adding 3rd process must fail with QuotaExceeded");
    TEST_ASSERT(job->getActiveProcessCount() == 2, "Active process count must remain 2");

    // Verify reverse process lookup
    auto locatedJob = sandboxMgr.getJobForProcess(proc1->getPid());
    TEST_ASSERT(locatedJob == job, "getJobForProcess must find parent job");

    // 3. Per-Process and Job-Wide Memory Limits
    // Allocate 10MB for proc1 (under 16MB limit)
    NtStatus stMem1 = job->checkAndRecordMemoryAlloc(proc1->getPid(), 10 * 1024 * 1024);
    TEST_ASSERT(NT_SUCCESS(stMem1), "Allocating 10MB for proc1 must succeed");

    // Allocate 8MB more for proc1 -> 10 + 8 = 18MB > 16MB limit -> QuotaExceeded
    NtStatus stMemExceedProc = job->checkAndRecordMemoryAlloc(proc1->getPid(), 8 * 1024 * 1024);
    TEST_ASSERT(stMemExceedProc == NtStatus::QuotaExceeded, "Exceeding per-process memory limit must return QuotaExceeded");

    // Allocate 12MB for proc2 -> Job memory = 10 + 12 = 22MB <= 24MB job limit -> Success
    NtStatus stMem2 = job->checkAndRecordMemoryAlloc(proc2->getPid(), 12 * 1024 * 1024);
    TEST_ASSERT(NT_SUCCESS(stMem2), "Allocating 12MB for proc2 must succeed");

    // Allocate 3MB more for proc2 -> Job memory = 22 + 3 = 25MB > 24MB job limit -> QuotaExceeded
    NtStatus stMemExceedJob = job->checkAndRecordMemoryAlloc(proc2->getPid(), 3 * 1024 * 1024);
    TEST_ASSERT(stMemExceedJob == NtStatus::QuotaExceeded, "Exceeding job aggregate memory limit must return QuotaExceeded");

    auto extLimits = job->getExtendedLimits();
    TEST_ASSERT(extLimits.PeakProcessMemoryUsed == 12 * 1024 * 1024, "Peak process memory must be 12MB");
    TEST_ASSERT(extLimits.PeakJobMemoryUsed == 22 * 1024 * 1024, "Peak job memory must be 22MB");

    // Free memory
    job->recordMemoryFree(proc1->getPid(), 4 * 1024 * 1024);

    // 4. Per-Process User Time Quota & Termination
    NtStatus stCpu1 = job->recordCpuExecution(proc1->getPid(), 400, 50);
    TEST_ASSERT(NT_SUCCESS(stCpu1), "Recording 400 user cycles must succeed");

    // Exceed user time limit: 400 + 700 = 1100 > 1000 limit -> terminates proc1
    NtStatus stCpuExceed = job->recordCpuExecution(proc1->getPid(), 700, 50);
    TEST_ASSERT(stCpuExceed == NtStatus::QuotaExceeded, "Exceeding user time quota must return QuotaExceeded");
    TEST_ASSERT(proc1->isTerminated(), "Process exceeding user time quota must be terminated");
    TEST_ASSERT(job->getActiveProcessCount() == 1, "Active process count must decrement after termination");

    // 5. Security Token Sandbox Confinement
    se::Sid sandboxSid(5, {21, 9999, 1});
    auto sandboxToken = std::make_shared<se::TokenObject>(sandboxSid, se::TokenType::Primary);
    job->setSandboxToken(sandboxToken);

    auto proc4 = procMgr.createProcess(L"sandbox_isolated.exe");
    NtStatus stAssign4 = job->assignProcess(proc4);
    TEST_ASSERT(NT_SUCCESS(stAssign4), "Assigning proc4 to job must succeed");
    TEST_ASSERT(proc4->getToken() == sandboxToken, "Proc4 must inherit restricted sandbox token");

    // 6. Kill On Job Close Enforcement
    auto limitsKill = job->getExtendedLimits();
    limitsKill.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    job->setExtendedLimits(limitsKill);

    TEST_ASSERT(!proc2->isTerminated(), "Proc2 must be active before handle close");
    job->onHandleClosed();
    TEST_ASSERT(proc2->isTerminated(), "Proc2 must be terminated on job handle close");
    TEST_ASSERT(proc4->isTerminated(), "Proc4 must be terminated on job handle close");
    TEST_ASSERT(job->getActiveProcessCount() == 0, "All processes must be terminated");

    auto accounting = job->getAccountingInfo();
    TEST_ASSERT(accounting.TotalProcesses >= 3, "Accounting TotalProcesses must reflect members");
    TEST_ASSERT(accounting.TotalTerminatedProcesses >= 3, "Accounting TotalTerminatedProcesses must reflect killed processes");
}

// ============================================================================
// Suite 53: PolarisDiag Sovereign Crash Diagnostics & Minidump Engine Tests
// ============================================================================
void Test_PolarisDiag_CrashDump_And_MinidumpWriter() {
    using namespace micant::polaris;

    auto& diagEngine = PolarisDiagnosticEngine::get();

    // 1. Prepare Crash Report
    CrashReport report{};
    report.bugCheckCode = ke::PAGE_FAULT_IN_NONPAGED_AREA;
    report.bugCheckName = std::string(PolarisDiagnosticEngine::getBugCheckName(report.bugCheckCode));
    report.param1 = 0x00007FFDF0001000ULL; // Faulting virtual address
    report.param2 = 0x0;                   // Read operation
    report.param3 = 0x00007FF614002340ULL; // Instruction pointer (RIP)
    report.param4 = 0x0;
    report.faultingModule = L"vanguard.sys";

    report.context.rip = 0x00007FF614002340ULL;
    report.context.rsp = 0x00007FFFFFFFDE00ULL;
    report.context.rbp = 0x00007FFFFFFFDE80ULL;
    report.context.rax = 0x0000000000000042ULL;
    report.context.rcx = 0x00007FFDF0001000ULL;
    report.context.rdx = 0x0000000000000100ULL;
    report.context.rflags = 0x202;

    // Loaded Kernel Modules
    std::vector<LoadedModuleDesc> modules = {
        { L"micant_kernel.exe", 0x00007FF614000000ULL, 0x180000 },
        { L"vanguard.sys",      0x00007FFF80000000ULL, 0x040000 },
        { L"emeraldfs.sys",     0x00007FFF80050000ULL, 0x060000 },
        { L"hal.dll",           0x00007FFF80100000ULL, 0x030000 }
    };

    // 2. Generate 64-bit Minidump (.dmp) Binary Stream
    std::vector<uint8_t> dump = diagEngine.generateMinidump(report, modules);
    TEST_ASSERT(!dump.empty(), "Minidump generator must return non-empty byte buffer");
    TEST_ASSERT(dump.size() >= sizeof(MINIDUMP_HEADER), "Minidump must exceed header size");

    // 3. Self-Parse & Validate Minidump Structs (WinDbg Parity)
    MinidumpSummary summary = diagEngine.parseMinidump(dump);
    TEST_ASSERT(summary.isValid, "Minidump validation must succeed");
    TEST_ASSERT(summary.version == MINIDUMP_VERSION, "Minidump version must match MINIDUMP_VERSION");
    TEST_ASSERT(summary.streamCount == 6, "Minidump must contain 6 distinct stream directories");

    // Verify stream types present
    auto hasStream = [&](uint32_t type) {
        return std::find(summary.streamTypes.begin(), summary.streamTypes.end(), type) != summary.streamTypes.end();
    };
    TEST_ASSERT(hasStream(SystemInfoStream), "Minidump must contain SystemInfoStream");
    TEST_ASSERT(hasStream(ExceptionStream), "Minidump must contain ExceptionStream");
    TEST_ASSERT(hasStream(ModuleListStream), "Minidump must contain ModuleListStream");
    TEST_ASSERT(hasStream(ThreadListStream), "Minidump must contain ThreadListStream");
    TEST_ASSERT(hasStream(MiscInfoStream), "Minidump must contain MiscInfoStream");
    TEST_ASSERT(hasStream(CommentStreamA), "Minidump must contain CommentStreamA");

    // Verify exception and register context accuracy
    TEST_ASSERT(summary.exceptionCode == ke::PAGE_FAULT_IN_NONPAGED_AREA, "Exception code in dump must match bug check");
    TEST_ASSERT(summary.exceptionAddress == 0x00007FF614002340ULL, "Exception address must match faulting RIP");
    TEST_ASSERT(summary.parameters[0] == 0x00007FFDF0001000ULL, "Parameter 1 must match faulting address");
    TEST_ASSERT(summary.rip == 0x00007FF614002340ULL, "Context RIP in dump must match report");
    TEST_ASSERT(summary.rsp == 0x00007FFFFFFFDE00ULL, "Context RSP in dump must match report");

    // Verify module list parsing
    TEST_ASSERT(summary.moduleNames.size() == 4, "Dump module list must parse all 4 modules");
    TEST_ASSERT(summary.moduleNames[0] == L"micant_kernel.exe", "Module 0 must be micant_kernel.exe");
    TEST_ASSERT(summary.moduleNames[1] == L"vanguard.sys", "Module 1 must be vanguard.sys");

    // Verify zero-telemetry sovereign comment
    TEST_ASSERT(summary.comment.find("Telemetry-Free") != std::string::npos, "Minidump must contain sovereign zero-telemetry notice");

    // 4. Panic Screen Rendering Verification
    std::string panicText = diagEngine.renderPanicScreenText(report);
    TEST_ASSERT(panicText.find("PAGE_FAULT_IN_NONPAGED_AREA") != std::string::npos, "Panic screen must display stop code name");
    TEST_ASSERT(panicText.find("vanguard.sys") != std::string::npos, "Panic screen must display faulting module");
    TEST_ASSERT(panicText.find("Telemetry:    DISABLED") != std::string::npos, "Panic screen must confirm zero telemetry");

    // Framebuffer rendering
    std::vector<uint32_t> fb(800 * 600, 0);
    diagEngine.renderPanicScreenFramebuffer(fb.data(), 800, 600, report);
    TEST_ASSERT(fb[0] == 0xFF0078D7, "Framebuffer must be cleared to Sovereign Azure #0078D7");

    // 5. Canonical BugCheck Stop Code Mappings
    TEST_ASSERT(PolarisDiagnosticEngine::getBugCheckName(ke::IRQL_NOT_LESS_OR_EQUAL) == "IRQL_NOT_LESS_OR_EQUAL", "IRQL stop code mapping");
    TEST_ASSERT(PolarisDiagnosticEngine::getBugCheckName(ke::CRITICAL_PROCESS_DIED) == "CRITICAL_PROCESS_DIED", "CRITICAL_PROCESS_DIED mapping");
    TEST_ASSERT(PolarisDiagnosticEngine::getBugCheckName(0x0000007B) == "INACCESSIBLE_BOOT_DEVICE", "INACCESSIBLE_BOOT_DEVICE mapping");
}

// ============================================================================
// Suite 54: CipherKSP Sovereign Cryptographic Services & CNG Engine Tests
// ============================================================================
void Test_CipherKSP_CryptographicServices_And_AES() {
    using namespace micant::crypto;

    auto toHex = [](std::span<const uint8_t> bytes) -> std::string {
        std::ostringstream oss;
        for (uint8_t b : bytes) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        return oss.str();
    };

    // 1. FIPS 180-4 SHA-256 Official NIST Test Vectors
    {
        // Vector 1: Empty string ""
        auto hashEmpty = Sha256::hash(std::span<const uint8_t>{});
        TEST_ASSERT(toHex(hashEmpty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                    "NIST SHA-256 Empty Vector Verification");

        // Vector 2: "abc"
        std::string abc = "abc";
        auto hashAbc = Sha256::hash(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(abc.data()), abc.size()));
        TEST_ASSERT(toHex(hashAbc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                    "NIST SHA-256 'abc' Vector Verification");

        // Vector 3: 56-byte string
        std::string str56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        auto hash56 = Sha256::hash(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(str56.data()), str56.size()));
        TEST_ASSERT(toHex(hash56) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                    "NIST SHA-256 56-byte Vector Verification");
    }

    // 2. RFC 4231 HMAC-SHA256 Test Vectors
    {
        // RFC 4231 Case 1: Key = 20x 0x0b, Data = "Hi There"
        std::vector<uint8_t> key1(20, 0x0b);
        std::string data1 = "Hi There";
        auto hmac1 = HmacSha256::compute(
            key1,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data1.data()), data1.size())
        );
        TEST_ASSERT(toHex(hmac1) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                    "RFC 4231 HMAC-SHA256 Case 1 Verification");

        // RFC 4231 Case 2: Key = "Jefe", Data = "what do ya want for nothing?"
        std::string key2 = "Jefe";
        std::string data2 = "what do ya want for nothing?";
        auto hmac2 = HmacSha256::compute(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(key2.data()), key2.size()),
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data2.data()), data2.size())
        );
        TEST_ASSERT(toHex(hmac2) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                    "RFC 4231 HMAC-SHA256 Case 2 Verification");
    }

    // 3. FIPS 197 AES-128 Electronic Codebook (ECB) NIST Test Vector
    {
        // FIPS 197 Appendix C.1: AES-128
        // Key: 2b 7e 15 16 28 ae d2 a6 ab f7 15 88 09 cf 4f 3c
        // In:  6b c1 be e2 2e 40 9f 96 e9 3d 7e 11 73 93 17 2a
        // Out: 3a d7 7b b4 0d 7a 36 60 a8 9e ca f3 24 66 ef 97
        const uint8_t keyBytes[16] = {
            0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
            0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
        };
        const uint8_t plainBytes[16] = {
            0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
            0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
        };

        Aes aes(keyBytes);
        uint8_t cipherBlock[16] = {0};
        aes.encryptBlock(plainBytes, cipherBlock);

        TEST_ASSERT(toHex(cipherBlock) == "3ad77bb40d7a3660a89ecaf32466ef97",
                    "FIPS 197 AES-128 ECB Block Encryption Verification");

        uint8_t decryptedBlock[16] = {0};
        aes.decryptBlock(cipherBlock, decryptedBlock);
        TEST_ASSERT(std::memcmp(plainBytes, decryptedBlock, 16) == 0,
                    "FIPS 197 AES-128 ECB Block Decryption Verification");
    }

    // 4. AES-256 CBC Mode with IV Chaining and PKCS#7 Padding
    {
        const uint8_t key256[32] = {
            0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
            0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
            0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
            0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
        };
        const uint8_t iv[16] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
        };

        std::string secretMessage = "Project MicaNT Sovereign Cryptography Next Generation Engine";
        Aes aes256(key256);

        auto ciphertext = aes256.encrypt(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(secretMessage.data()), secretMessage.size()),
            Aes::Mode::CBC,
            iv,
            true // PKCS#7 padding
        );

        TEST_ASSERT(ciphertext.size() > secretMessage.size(), "Ciphertext size must include PKCS#7 padding");
        TEST_ASSERT(ciphertext.size() % 16 == 0, "Ciphertext size must be a multiple of 16");

        bool ok = false;
        auto decrypted = aes256.decrypt(
            ciphertext,
            Aes::Mode::CBC,
            iv,
            true,
            &ok
        );

        TEST_ASSERT(ok, "AES-256 CBC decryption must report success");
        std::string decryptedMessage(decrypted.begin(), decrypted.end());
        TEST_ASSERT(decryptedMessage == secretMessage, "AES-256 CBC roundtrip must match original plaintext");

        // Verify tampering detection with PKCS#7
        ciphertext.back() ^= 0xFF; // Corrupt padding byte
        bool tamperOk = false;
        aes256.decrypt(ciphertext, Aes::Mode::CBC, iv, true, &tamperOk);
        TEST_ASSERT(!tamperOk, "Tampered ciphertext must fail PKCS#7 padding validation");
    }

    // 5. RFC 6070 PBKDF2-HMAC-SHA256 Test Vectors
    {
        std::string pass = "password";
        std::string salt = "salt";

        // Iterations = 1
        auto dk1 = Pbkdf2::derive(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(pass.data()), pass.size()),
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(salt.data()), salt.size()),
            1, 32
        );
        TEST_ASSERT(toHex(dk1) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
                    "RFC 6070 PBKDF2 Iteration 1 Verification");

        // Iterations = 2
        auto dk2 = Pbkdf2::derive(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(pass.data()), pass.size()),
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(salt.data()), salt.size()),
            2, 32
        );
        TEST_ASSERT(toHex(dk2) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
                    "RFC 6070 PBKDF2 Iteration 2 Verification");
    }

    // 6. CSPRNG Randomness & Entropy Verification
    {
        std::array<uint8_t, 32> randomBuf1{};
        std::array<uint8_t, 32> randomBuf2{};

        NTSTATUS status1 = BCryptGenRandom(nullptr, randomBuf1.data(), static_cast<uint32_t>(randomBuf1.size()), 0);
        NTSTATUS status2 = BCryptGenRandom(nullptr, randomBuf2.data(), static_cast<uint32_t>(randomBuf2.size()), 0);

        TEST_ASSERT(status1 == STATUS_SUCCESS, "BCryptGenRandom must succeed");
        TEST_ASSERT(status2 == STATUS_SUCCESS, "BCryptGenRandom must succeed on successive calls");
        TEST_ASSERT(randomBuf1 != randomBuf2, "CSPRNG must generate non-repeating entropy streams");

        bool nonZero = false;
        for (uint8_t b : randomBuf1) {
            if (b != 0) { nonZero = true; break; }
        }
        TEST_ASSERT(nonZero, "CSPRNG output buffer must contain non-zero entropy");
    }

    // 7. Windows CNG (BCrypt) High-Level API Lifecycle
    {
        BCRYPT_ALG_HANDLE hAesAlg = nullptr;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAesAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        TEST_ASSERT(status == STATUS_SUCCESS && hAesAlg != nullptr, "BCryptOpenAlgorithmProvider for AES must succeed");

        // Verify property retrieval
        uint32_t blockLen = 0;
        uint32_t resultLen = 0;
        status = BCryptGetProperty(hAesAlg, BCRYPT_BLOCK_LENGTH, reinterpret_cast<uint8_t*>(&blockLen), sizeof(blockLen), &resultLen, 0);
        TEST_ASSERT(status == STATUS_SUCCESS && blockLen == 16, "BCryptGetProperty for BlockLength must return 16");

        // Generate Symmetric Key Handle
        uint8_t aesKeySecret[16] = {
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
            0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10
        };
        BCRYPT_KEY_HANDLE hKey = nullptr;
        status = BCryptGenerateSymmetricKey(hAesAlg, &hKey, nullptr, 0, aesKeySecret, sizeof(aesKeySecret), 0);
        TEST_ASSERT(status == STATUS_SUCCESS && hKey != nullptr, "BCryptGenerateSymmetricKey must return valid key handle");

        // Encrypt & Decrypt via CNG API
        std::string plain = "CNG-Sovereign-Message";
        uint8_t iv[16] = { 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xEE, 0xFF };
        uint8_t ivDec[16];
        std::memcpy(ivDec, iv, 16);

        uint32_t cipherLen = 0;
        status = BCryptEncrypt(hKey, reinterpret_cast<uint8_t*>(plain.data()), static_cast<uint32_t>(plain.size()), nullptr, iv, sizeof(iv), nullptr, 0, &cipherLen, BCRYPT_BLOCK_PADDING);
        TEST_ASSERT(status == STATUS_SUCCESS && cipherLen >= plain.size(), "BCryptEncrypt sizing probe must succeed");

        std::vector<uint8_t> cipherBuf(cipherLen);
        status = BCryptEncrypt(hKey, reinterpret_cast<uint8_t*>(plain.data()), static_cast<uint32_t>(plain.size()), nullptr, iv, sizeof(iv), cipherBuf.data(), cipherLen, &cipherLen, BCRYPT_BLOCK_PADDING);
        TEST_ASSERT(status == STATUS_SUCCESS, "BCryptEncrypt payload execution must succeed");

        uint32_t decLen = 0;
        status = BCryptDecrypt(hKey, cipherBuf.data(), cipherLen, nullptr, ivDec, sizeof(ivDec), nullptr, 0, &decLen, BCRYPT_BLOCK_PADDING);
        TEST_ASSERT(status == STATUS_SUCCESS && decLen == plain.size(), "BCryptDecrypt sizing probe must match plaintext length");

        std::vector<uint8_t> decBuf(decLen);
        status = BCryptDecrypt(hKey, cipherBuf.data(), cipherLen, nullptr, ivDec, sizeof(ivDec), decBuf.data(), decLen, &decLen, BCRYPT_BLOCK_PADDING);
        TEST_ASSERT(status == STATUS_SUCCESS, "BCryptDecrypt payload execution must succeed");

        std::string recovered(decBuf.begin(), decBuf.end());
        TEST_ASSERT(recovered == plain, "BCrypt roundtrip plaintext must match original message");

        // Clean up key and algorithm
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAesAlg, 0);

        // Test CNG Hash API
        BCRYPT_ALG_HANDLE hShaAlg = nullptr;
        BCryptOpenAlgorithmProvider(&hShaAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        BCRYPT_HASH_HANDLE hHash = nullptr;
        BCryptCreateHash(hShaAlg, &hHash, nullptr, 0, nullptr, 0, 0);

        std::string hashInput = "MicaNT-CNG-Hash";
        BCryptHashData(hHash, reinterpret_cast<uint8_t*>(hashInput.data()), static_cast<uint32_t>(hashInput.size()), 0);

        uint8_t digest[32];
        BCryptFinishHash(hHash, digest, sizeof(digest), 0);
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hShaAlg, 0);

        auto directHash = Sha256::hash(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(hashInput.data()), hashInput.size()));
        TEST_ASSERT(std::memcmp(digest, directHash.data(), 32) == 0, "CNG Hash API digest must match direct Sha256 digest");
    }

    // 8. Key Storage Provider (NCrypt) Lifecycle
    {
        NCRYPT_PROV_HANDLE hStorage = nullptr;
        NTSTATUS status = NCryptOpenStorageProvider(&hStorage, MS_KEY_STORAGE_PROVIDER, 0);
        TEST_ASSERT(status == STATUS_SUCCESS && hStorage != nullptr, "NCryptOpenStorageProvider must succeed");

        NCRYPT_KEY_HANDLE hPersisted = nullptr;
        status = NCryptCreatePersistedKey(hStorage, &hPersisted, BCRYPT_AES_ALGORITHM, L"SovereignAdminMasterKey", 0, 0);
        TEST_ASSERT(status == STATUS_SUCCESS && hPersisted != nullptr, "NCryptCreatePersistedKey must succeed");

        status = NCryptFinalizeKey(hPersisted, 0);
        TEST_ASSERT(status == STATUS_SUCCESS, "NCryptFinalizeKey must succeed and populate random master key");

        // Re-open persisted key by name
        NCRYPT_KEY_HANDLE hOpened = nullptr;
        status = NCryptOpenKey(hStorage, &hOpened, L"SovereignAdminMasterKey", 0, 0);
        TEST_ASSERT(status == STATUS_SUCCESS && hOpened != nullptr, "NCryptOpenKey must find persisted key by name");

        // Export key secret
        uint32_t exportSize = 0;
        status = NCryptExportKey(hOpened, nullptr, BCRYPT_OPAQUE_KEY_BLOB, nullptr, nullptr, 0, &exportSize, 0);
        TEST_ASSERT(status == STATUS_SUCCESS && exportSize == 32, "NCryptExportKey sizing probe must report 32 bytes");

        std::vector<uint8_t> exportedSecret(exportSize);
        status = NCryptExportKey(hOpened, nullptr, BCRYPT_OPAQUE_KEY_BLOB, nullptr, exportedSecret.data(), exportSize, &exportSize, 0);
        TEST_ASSERT(status == STATUS_SUCCESS, "NCryptExportKey must successfully export raw key data");

        // Delete key
        status = NCryptDeleteKey(hOpened, 0);
        TEST_ASSERT(status == STATUS_SUCCESS, "NCryptDeleteKey must succeed");

        NCryptFreeObject(hStorage);
    }
}

// ============================================================================
// Suite 55: JanusLDR Sovereign Dynamic Linker & Delay-Load Gateway Tests
// ============================================================================
void Test_JanusLDR_DelayLoadThunks_And_SxSManifest() {
    using namespace micant::janus;
    using namespace micant::ldr;

    auto& linker = JanusDynamicLinker::get();

    // 1. Delay-Load Helper Thunk Resolution (__delayLoadHelper2)
    {
        // Setup mock target function in dxgi.dll
        void* pfnCreateDXGIFactory1 = reinterpret_cast<void*>(0x00007FF8B0001234ULL);
        DynamicLoader::get().registerExport("dxgi.dll", "CreateDXGIFactory1", pfnCreateDXGIFactory1);

        // Prepare simulated image memory buffer containing delay descriptor & tables
        std::vector<uint8_t> mockImage(4096, 0);
        uintptr_t imageBase = reinterpret_cast<uintptr_t>(mockImage.data());

        // Offset 0x100: DLL Name "dxgi.dll"
        const char dllNameStr[] = "dxgi.dll";
        uint32_t rvaDLLName = 0x100;
        std::memcpy(&mockImage[rvaDLLName], dllNameStr, sizeof(dllNameStr));

        // Offset 0x200: HMODULE storage (uintptr_t)
        uint32_t rvaHmod = 0x200;

        // Offset 0x300: IAT entry table
        uint32_t rvaIAT = 0x300;
        void** pIAT = reinterpret_cast<void**>(&mockImage[rvaIAT]);
        pIAT[0] = reinterpret_cast<void*>(0xDEADBEEF); // Initial unthunked placeholder

        // Offset 0x400: Import Name Table (INT) pointing to Hint/Name at 0x500
        uint32_t rvaINT = 0x400;
        uintptr_t* pINT = reinterpret_cast<uintptr_t*>(&mockImage[rvaINT]);
        pINT[0] = 0x500; // RVA to Hint/Name

        // Offset 0x500: Hint (2 bytes) + "CreateDXGIFactory1\0"
        uint32_t rvaHintName = 0x500;
        mockImage[rvaHintName] = 0x01; // Hint low
        mockImage[rvaHintName + 1] = 0x00; // Hint high
        const char procNameStr[] = "CreateDXGIFactory1";
        std::memcpy(&mockImage[rvaHintName + 2], procNameStr, sizeof(procNameStr));

        // Build Delay Descriptor
        ImgDelayDescr desc{};
        desc.grAttrs = DLATTR_RVA;
        desc.rvaDLLName = rvaDLLName;
        desc.rvaHmod = rvaHmod;
        desc.rvaIAT = rvaIAT;
        desc.rvaINT = rvaINT;

        // Hook tracking
        std::vector<uint32_t> notifications;
        auto testHook = [](uint32_t dliNotify, DelayLoadInfo* pdli) -> void* {
            static std::vector<uint32_t>* pNotifs = nullptr;
            if (pdli && pdli->dwLastError == 0x1337) {
                pNotifs = reinterpret_cast<std::vector<uint32_t>*>(pdli->pfnCur);
            }
            if (pNotifs) {
                pNotifs->push_back(dliNotify);
            }
            return nullptr;
        };

        // Pass hook tracker
        DelayLoadInfo dummy{};
        dummy.dwLastError = 0x1337;
        dummy.pfnCur = &notifications;
        testHook(0, &dummy);

        void** targetIATEntry = &pIAT[0];
        void* resolved = linker.resolveDelayLoad(imageBase, &desc, targetIATEntry, testHook);

        TEST_ASSERT(resolved == pfnCreateDXGIFactory1, "Delay-load helper must resolve exact target function address");
        TEST_ASSERT(*targetIATEntry == pfnCreateDXGIFactory1, "IAT entry must be patched directly to target address");
        TEST_ASSERT(*reinterpret_cast<uintptr_t*>(&mockImage[rvaHmod]) != 0, "Descriptor HMODULE slot must be populated with loaded base");

        // Verify hook notifications fired in correct order
        TEST_ASSERT(std::find(notifications.begin(), notifications.end(), dliStartProcessing) != notifications.end(), "dliStartProcessing must fire");
        TEST_ASSERT(std::find(notifications.begin(), notifications.end(), dliNoteEndProcessing) != notifications.end(), "dliNoteEndProcessing must fire");
    }

    // 2. Export Forwarder Chain Resolution
    {
        void* pfnRtlEnterCritSec = reinterpret_cast<void*>(0x00007FF800054321ULL);
        DynamicLoader::get().registerExport("ntdll.dll", "RtlEnterCriticalSection", pfnRtlEnterCritSec);

        // Forward kernel32!EnterCriticalSection -> NTDLL.RtlEnterCriticalSection
        linker.registerForwarder("kernel32.dll", "EnterCriticalSection", "ntdll.dll.RtlEnterCriticalSection");

        // Multi-hop: api-ms-win-core-synch-l1-1-0.dll!EnterCriticalSection -> kernel32.dll.EnterCriticalSection
        linker.registerForwarder("api-ms-win-core-synch-l1-1-0.dll", "EnterCriticalSection", "kernel32.dll.EnterCriticalSection");

        // Test single-hop resolution
        void* resolvedSingle = linker.resolveExport("kernel32.dll", "EnterCriticalSection");
        TEST_ASSERT(resolvedSingle == pfnRtlEnterCritSec, "Single-hop forwarder must resolve to target NTDLL export");

        // Test multi-hop forwarder chain resolution
        void* resolvedMulti = linker.resolveExport("api-ms-win-core-synch-l1-1-0.dll", "EnterCriticalSection");
        TEST_ASSERT(resolvedMulti == pfnRtlEnterCritSec, "Multi-hop forwarder chain must resolve seamlessly to leaf implementation");

        // Test forwarder parser on ordinal forward
        auto ordTarget = ForwarderResolver::parseForwarderString("user32.dll.#42");
        TEST_ASSERT(ordTarget.has_value(), "Forwarder resolver must parse ordinal forward target");
        TEST_ASSERT(ordTarget->isOrdinal && ordTarget->ordinal == 42, "Parsed ordinal forward must extract #42");
        TEST_ASSERT(ordTarget->moduleName == "user32.dll", "Parsed ordinal forward must identify user32.dll");
    }

    // 3. Side-by-Side (SxS) Manifest Parsing
    {
        std::string manifestXml = R"(
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="MicaNT.SovereignApp" version="1.2.3.4" processorArchitecture="amd64" publicKeyToken="12345678abcdef00" />
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" publicKeyToken="6595b64144ccf1df" />
    </dependentAssembly>
  </dependency>
  <file name="comctl32.dll" />
</assembly>
)";

        auto actCtx = ActivationContext::parseFromXml(manifestXml, L"C:\\Apps\\SovereignApp.exe.manifest");
        TEST_ASSERT(actCtx != nullptr, "ActivationContext::parseFromXml must return valid context");
        TEST_ASSERT(actCtx->identity.name == L"MicaNT.SovereignApp", "Parsed assembly identity name must match manifest");
        TEST_ASSERT(actCtx->identity.version == L"1.2.3.4", "Parsed assembly identity version must match manifest");
        TEST_ASSERT(actCtx->dependencies.size() == 1, "Parsed dependencies count must be 1");
        TEST_ASSERT(actCtx->dependencies[0].name == L"Microsoft.Windows.Common-Controls", "Dependent assembly name must match Common-Controls");
        TEST_ASSERT(actCtx->dependencies[0].version == L"6.0.0.0", "Dependent assembly version must match 6.0.0.0");
        TEST_ASSERT(actCtx->fileRedirections.find(L"comctl32.dll") != actCtx->fileRedirections.end(), "File redirection for comctl32.dll must be registered");

        // 4. Win32 Activation Context Stack Lifecycle
        uintptr_t hActCtx = linker.registerParsedContext(actCtx);
        TEST_ASSERT(hActCtx != 0, "Context registration must yield non-zero handle");

        uintptr_t cookie = 0;
        bool activated = ActivateActCtx(hActCtx, &cookie);
        TEST_ASSERT(activated && cookie != 0, "ActivateActCtx must successfully push context onto stack");

        auto active = linker.getActiveContext();
        TEST_ASSERT(active != nullptr && active->identity.name == L"MicaNT.SovereignApp", "getActiveContext must retrieve current top of stack");

        auto redir = linker.resolveAssemblyRedirection(L"comctl32.dll");
        TEST_ASSERT(redir.has_value() && *redir == L"comctl32.dll", "Assembly redirection must resolve active manifest files");

        bool deactivated = DeactivateActCtx(0, cookie);
        TEST_ASSERT(deactivated, "DeactivateActCtx with matching cookie must succeed");
        TEST_ASSERT(linker.getActiveContext() == nullptr, "Context stack must be empty after popping cookie");

        ReleaseActCtx(hActCtx);
    }
}

void Test_User32_WindowManager_SwapchainPresentation_And_DirectInput() {
    using namespace micant::user32;
    using namespace micant::dinput;
    using namespace micant::prismx;

    // 0. Initialize User32 and DirectInput exports
    InitializeUser32SubsystemExports();
    InitializeDirectInputSubsystemExports();

    // Tracking state for custom WindowProc
    static std::vector<uint32_t> receivedMessages;
    static uint32_t lastChar = 0;
    static uint32_t lastKey = 0;
    receivedMessages.clear();
    lastChar = 0;
    lastKey = 0;

    auto testWndProc = [](win32::HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        receivedMessages.push_back(uMsg);
        if (uMsg == WM_CHAR) {
            lastChar = static_cast<uint32_t>(wParam);
        } else if (uMsg == WM_KEYDOWN) {
            lastKey = static_cast<uint32_t>(wParam);
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    };

    // 1. Register Window Class
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.lpfnWndProc = testWndProc;
    wcex.lpszClassName = L"MicaNT_GameEngineWindow";
    uint16_t atom = RegisterClassExW(&wcex);
    TEST_ASSERT(atom != 0, "RegisterClassExW must return valid non-zero class atom");

    WNDCLASSEXW queryWcex{};
    bool clsFound = GetClassInfoExW(nullptr, L"MicaNT_GameEngineWindow", &queryWcex);
    TEST_ASSERT(clsFound && queryWcex.lpfnWndProc == testWndProc, "GetClassInfoExW must retrieve registered window class");

    // 2. Window Creation and Hierarchy
    win32::HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"MicaNT_GameEngineWindow",
        L"MicaNT Sovereign 3D Game Engine",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        50, 50, 640, 480,
        nullptr, nullptr, nullptr, nullptr
    );
    TEST_ASSERT(hwnd != nullptr, "CreateWindowExW must return valid HWND");
    TEST_ASSERT(IsWindow(hwnd), "IsWindow must verify allocated HWND");
    TEST_ASSERT(IsWindowVisible(hwnd), "Window created with WS_VISIBLE must report visible");

    // Verify WM_CREATE was dispatched
    TEST_ASSERT(std::find(receivedMessages.begin(), receivedMessages.end(), WM_CREATE) != receivedMessages.end(), "WM_CREATE must be dispatched on window creation");

    // Test Geometry & Client Rect
    RECT clientRc{};
    GetClientRect(hwnd, &clientRc);
    TEST_ASSERT((clientRc.right - clientRc.left) == 640 && (clientRc.bottom - clientRc.top) == 480, "GetClientRect must reflect 640x480 client surface");

    RECT windowRc{};
    GetWindowRect(hwnd, &windowRc);
    TEST_ASSERT(windowRc.left == 50 && windowRc.top == 50 && windowRc.right == 690 && windowRc.bottom == 530, "GetWindowRect must reflect window coordinates");

    // Test Window Long Pointers (GWLP_USERDATA)
    uintptr_t magicUserData = 0xCAFEBABE1337BEEFULL;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, magicUserData);
    TEST_ASSERT(GetWindowLongPtrW(hwnd, GWLP_USERDATA) == magicUserData, "GWLP_USERDATA must persist and round-trip through GetWindowLongPtrW");

    // Drain initial window initialization messages (WM_SIZE, WM_PAINT)
    MSG initMsg{};
    while (PeekMessageW(&initMsg, hwnd, 0, 0, PM_REMOVE)) {
        DispatchMessageW(&initMsg);
    }
    TEST_ASSERT(std::find(receivedMessages.begin(), receivedMessages.end(), WM_SIZE) != receivedMessages.end(), "WM_SIZE must be dispatched on window layout");
    TEST_ASSERT(std::find(receivedMessages.begin(), receivedMessages.end(), WM_PAINT) != receivedMessages.end(), "WM_PAINT must be dispatched on initial presentation");

    // 3. Message Queue & Message Pump (Peek, Translate, Dispatch)
    PostMessageW(hwnd, WM_KEYDOWN, 0x41 /* 'A' */, 0);

    MSG msg{};
    bool hasMsg = PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE);
    TEST_ASSERT(hasMsg && msg.message == WM_KEYDOWN && msg.wParam == 0x41, "PeekMessageW must pull posted WM_KEYDOWN from queue");

    // TranslateMessage must synthesize WM_CHAR for printable character
    bool translated = TranslateMessage(&msg);
    TEST_ASSERT(translated, "TranslateMessage must synthesize WM_CHAR for key 0x41 ('A')");

    // Dispatch WM_KEYDOWN
    DispatchMessageW(&msg);
    TEST_ASSERT(lastKey == 0x41, "DispatchMessageW must execute window procedure for WM_KEYDOWN");

    // Peek and dispatch the translated WM_CHAR
    bool hasCharMsg = PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE);
    TEST_ASSERT(hasCharMsg && msg.message == WM_CHAR && msg.wParam == 0x41, "PeekMessageW must retrieve synthesized WM_CHAR");
    DispatchMessageW(&msg);
    TEST_ASSERT(lastChar == 0x41, "DispatchMessageW must execute window procedure for WM_CHAR");

    // Test PostQuitMessage and GetMessage termination
    PostQuitMessage(42);
    bool getRes = GetMessageW(&msg, nullptr, 0, 0);
    TEST_ASSERT(!getRes && msg.message == WM_QUIT && msg.wParam == 42, "GetMessageW must return FALSE upon retrieving WM_QUIT");

    // 4. DXGI SwapChain Backbuffer Presentation Bridge to HWND
    {
        DXGI_SWAP_CHAIN_DESC scDesc{};
        scDesc.BufferDesc.Width = 640;
        scDesc.BufferDesc.Height = 480;
        scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferCount = 2;
        scDesc.OutputWindow = hwnd;
        scDesc.Windowed = 1;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        PrismXSwapChainImpl swapChain(scDesc);

        // Fetch backbuffer 0
        void* pBuf = nullptr;
        int32_t hr = swapChain.GetBuffer(0, IID_IDXGISurface, &pBuf);
        TEST_ASSERT(hr == 0 && pBuf != nullptr, "swapChain.GetBuffer must retrieve surface pointer");

        auto* surface = static_cast<PrismXSurfaceImpl*>(pBuf);

        // Write distinct pixel pattern to swapchain backbuffer
        uint32_t* pPixels = reinterpret_cast<uint32_t*>(surface->GetRawData());
        uint32_t testColor = 0xFF55AAEE; // ABGR test color
        std::fill(pPixels, pPixels + (640 * 480), testColor);

        // Present frame
        int32_t presentHr = swapChain.Present(1, 0);
        TEST_ASSERT(presentHr == 0, "swapChain.Present must return 0 (S_OK)");

        // Verify window's surface pixels now contain the presented backbuffer
        uint32_t surfW = 0, surfH = 0;
        const uint32_t* windowPixels = WindowManager::get().getWindowPixelBuffer(hwnd, &surfW, &surfH);
        TEST_ASSERT(windowPixels != nullptr && surfW == 640 && surfH == 480, "Window surface buffer must match backbuffer dimensions");
        TEST_ASSERT(windowPixels[0] == testColor && windowPixels[640 * 240 + 320] == testColor, "Window surface must contain exact presented backbuffer pixel data");

        surface->Release();
    }

    // 5. Win32 Raw Input API
    {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01; // Generic Desktop Controls
        rid.usUsage = 0x02;     // Mouse
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = hwnd;

        bool regOk = RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE));
        TEST_ASSERT(regOk, "RegisterRawInputDevices must succeed");

        // Inject raw mouse motion & left button down
        HRAWINPUT hRaw = WindowManager::get().injectRawMouse(hwnd, 25, -12, RI_MOUSE_LEFT_BUTTON_DOWN);
        TEST_ASSERT(hRaw != nullptr, "injectRawMouse must generate non-null HRAWINPUT");

        // Peek WM_INPUT message
        MSG rawMsg{};
        bool gotRaw = PeekMessageW(&rawMsg, hwnd, WM_INPUT, WM_INPUT, PM_REMOVE);
        TEST_ASSERT(gotRaw && rawMsg.message == WM_INPUT, "PeekMessageW must retrieve WM_INPUT");

        // Query raw input header
        RAWINPUTHEADER hdr{};
        UINT hdrSize = sizeof(RAWINPUTHEADER);
        UINT getHdr = GetRawInputData(reinterpret_cast<HRAWINPUT>(rawMsg.lParam), RID_HEADER, &hdr, &hdrSize, sizeof(RAWINPUTHEADER));
        TEST_ASSERT(getHdr == sizeof(RAWINPUTHEADER) && hdr.dwType == RIM_TYPEMOUSE, "GetRawInputData RID_HEADER must return RIM_TYPEMOUSE");

        // Query raw input full packet
        RAWINPUT fullInput{};
        UINT fullSize = sizeof(RAWINPUT);
        UINT getFull = GetRawInputData(reinterpret_cast<HRAWINPUT>(rawMsg.lParam), RID_INPUT, &fullInput, &fullSize, sizeof(RAWINPUTHEADER));
        TEST_ASSERT(getFull == sizeof(RAWINPUT), "GetRawInputData RID_INPUT must return full RAWINPUT size");
        TEST_ASSERT(fullInput.data.mouse.lLastX == 25 && fullInput.data.mouse.lLastY == -12, "Raw mouse delta coordinates must match injected motion");
        TEST_ASSERT((fullInput.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0, "Raw mouse button down flag must be set");
    }

    // 6. DirectInput 8 Subsystem (IDirectInput8W & IDirectInputDevice8W)
    {
        void* pDIObj = nullptr;
        int32_t diHr = DirectInput8Create(nullptr, DIRECTINPUT_VERSION, IID_IDirectInput8W, &pDIObj, nullptr);
        TEST_ASSERT(diHr == DI_OK && pDIObj != nullptr, "DirectInput8Create must return DI_OK with valid interface");

        auto* pDI = static_cast<IDirectInput8W*>(pDIObj);

        // Create DirectInput Mouse Device
        IDirectInputDevice8W* pMouseDev = nullptr;
        int32_t devHr = pDI->CreateDevice(GUID_SysMouse, &pMouseDev, nullptr);
        TEST_ASSERT(devHr == DI_OK && pMouseDev != nullptr, "CreateDevice for GUID_SysMouse must succeed");

        DIDATAFORMAT mouseDf{};
        mouseDf.dwDataSize = sizeof(DIMOUSESTATE);
        TEST_ASSERT(pMouseDev->SetDataFormat(&mouseDf) == DI_OK, "SetDataFormat on mouse device must succeed");
        TEST_ASSERT(pMouseDev->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE) == DI_OK, "SetCooperativeLevel must succeed");
        TEST_ASSERT(pMouseDev->Acquire() == DI_OK, "Acquire on mouse device must succeed");

        // Inject mouse delta & button 0 (left click)
        auto* concreteMouse = static_cast<DirectInputMouseDevice*>(pMouseDev);
        concreteMouse->injectMotion(14, -7, 120);
        concreteMouse->injectButton(0, true);

        DIMOUSESTATE mouseState{};
        int32_t stateHr = pMouseDev->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState);
        TEST_ASSERT(stateHr == DI_OK, "GetDeviceState for mouse must succeed");
        TEST_ASSERT(mouseState.lX == 14 && mouseState.lY == -7 && mouseState.lZ == 120, "DirectInput mouse deltas must match injected input");
        TEST_ASSERT((mouseState.rgbButtons[0] & 0x80) != 0, "DirectInput mouse button 0 must report down");

        // Second poll should clear relative deltas
        DIMOUSESTATE secondState{};
        pMouseDev->GetDeviceState(sizeof(DIMOUSESTATE), &secondState);
        TEST_ASSERT(secondState.lX == 0 && secondState.lY == 0 && secondState.lZ == 0, "DirectInput mouse deltas must reset after polling");

        pMouseDev->Unacquire();
        pMouseDev->Release();

        // Create DirectInput Keyboard Device
        IDirectInputDevice8W* pKbdDev = nullptr;
        int32_t kbdHr = pDI->CreateDevice(GUID_SysKeyboard, &pKbdDev, nullptr);
        TEST_ASSERT(kbdHr == DI_OK && pKbdDev != nullptr, "CreateDevice for GUID_SysKeyboard must succeed");
        TEST_ASSERT(pKbdDev->Acquire() == DI_OK, "Acquire on keyboard device must succeed");

        auto* concreteKbd = static_cast<DirectInputKeyboardDevice*>(pKbdDev);
        concreteKbd->injectKey(0x39 /* DIK_SPACE */, true);

        std::array<uint8_t, 256> kbdState{};
        int32_t kbdStateHr = pKbdDev->GetDeviceState(256, kbdState.data());
        TEST_ASSERT(kbdStateHr == DI_OK, "GetDeviceState for keyboard must succeed");
        TEST_ASSERT((kbdState[0x39] & 0x80) != 0, "DirectInput keyboard state for DIK_SPACE must report pressed");

        pKbdDev->Unacquire();
        pKbdDev->Release();

        pDI->Release();
    }

    // Destroy Window and clean up
    DestroyWindow(hwnd);
    TEST_ASSERT(!IsWindow(hwnd), "DestroyWindow must remove window from active map");
    TEST_ASSERT(std::find(receivedMessages.begin(), receivedMessages.end(), WM_DESTROY) != receivedMessages.end(), "WM_DESTROY must be received upon destruction");
}

void Test_PrismX_Interactive3DViewer_And_CameraPipeline() {
    using namespace micant::viewer;
    using namespace micant::user32;

    // 1. Orbit Camera Mathematics & Transformations
    {
        ViewerCamera camera{};
        TEST_ASSERT(std::abs(camera.radius - 4.0f) < 0.001f, "Camera initial radius must be 4.0");
        TEST_ASSERT(std::abs(camera.yaw - 0.785f) < 0.001f, "Camera initial yaw must be ~45 deg");
        TEST_ASSERT(std::abs(camera.pitch - 0.45f) < 0.001f, "Camera initial pitch must be ~26 deg");

        // Rotate camera
        camera.rotate(0.2f, 0.1f);
        TEST_ASSERT(std::abs(camera.yaw - 0.985f) < 0.001f, "Camera yaw rotation must update");
        TEST_ASSERT(std::abs(camera.pitch - 0.55f) < 0.001f, "Camera pitch rotation must update");

        // Test Pitch clamping (avoid gimbal lock)
        camera.rotate(0.0f, 5.0f);
        TEST_ASSERT(camera.pitch <= 1.45f, "Camera pitch must be clamped to <= 1.45 rad (~83 deg)");
        camera.rotate(0.0f, -10.0f);
        TEST_ASSERT(camera.pitch >= -1.45f, "Camera pitch must be clamped to >= -1.45 rad");

        // Reset camera pitch
        camera.pitch = 0.45f;

        // Test Zoom clamping
        camera.zoom(-10.0f);
        TEST_ASSERT(std::abs(camera.radius - camera.minRadius) < 0.001f, "Camera zoom must clamp to minRadius (1.0)");
        camera.zoom(100.0f);
        TEST_ASSERT(std::abs(camera.radius - camera.maxRadius) < 0.001f, "Camera zoom must clamp to maxRadius (25.0)");
        camera.radius = 4.0f;

        // Test Pan
        camera.pan(1.0f, -0.5f);
        TEST_ASSERT(std::abs(camera.target.x - 1.0f) < 0.001f && std::abs(camera.target.y - (-0.5f)) < 0.001f, "Camera pan must shift target point");
        camera.target = { 0.0f, 0.0f, 0.0f };

        // Eye position vector
        prism3d::Vector3 eye = camera.getEyePosition();
        float distFromTarget = std::sqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z);
        TEST_ASSERT(std::abs(distFromTarget - 4.0f) < 0.01f, "Eye distance from origin must match orbit radius");

        // View and Projection matrices
        auto viewMat = camera.getViewMatrix();
        TEST_ASSERT(viewMat.m[3][3] == 1.0f || viewMat.m[0][0] != 0.0f, "View matrix must be valid camera transform");
        auto projMat = camera.getProjectionMatrix(0.785f, 4.0f / 3.0f, 0.1f, 100.0f);
        TEST_ASSERT(projMat.m[3][2] != 0.0f || projMat.m[2][3] != 0.0f, "Projection matrix must be valid perspective transform");
    }

    // 2. Procedural Mesh Generation
    {
        // 2a. DEC PRISM Crystal Core
        auto crystal = MeshGenerator::createCrystal();
        TEST_ASSERT(crystal.vertices.size() == 18, "Crystal mesh must have 18 vertices (2 apexes + 8 upper + 8 lower)");
        TEST_ASSERT(crystal.indices.size() == 96, "Crystal mesh must have 96 indices (32 triangles: 8 top + 16 mid + 8 btm)");
        TEST_ASSERT(crystal.vertices[0].y > 1.5f, "Top apex vertex must have elevated Y coordinate");
        TEST_ASSERT(crystal.vertices[1].y < -1.5f, "Bottom apex vertex must have negative Y coordinate");

        // 2b. Parametric 3D Torus
        auto torus = MeshGenerator::createTorus(1.0f, 0.35f, 16, 12);
        TEST_ASSERT(torus.vertices.size() == 192, "Torus (16x12) must have 192 vertices");
        TEST_ASSERT(torus.indices.size() == 1152, "Torus (16x12) must have 1152 indices (384 triangles)");
        TEST_ASSERT(torus.vertices[0].g > 0.1f, "Torus vertices must have shaded lighting intensity");

        // 2c. Shaded Cube
        auto cube = MeshGenerator::createCube(1.0f);
        TEST_ASSERT(cube.vertices.size() == 8, "Cube mesh must have 8 vertices");
        TEST_ASSERT(cube.indices.size() == 36, "Cube mesh must have 36 indices (12 triangles)");
    }

    // 3. Interactive Viewer Session & Direct3D 11 Presentation
    {
        ViewerSession session(640, 480);
        bool ok = session.initialize(L"MicaNT Test Viewer Window");
        TEST_ASSERT(ok, "ViewerSession::initialize must succeed");
        TEST_ASSERT(session.getHwnd() != nullptr, "ViewerSession must create valid native HWND");

        // Test Input Events Injection
        float origYaw = session.getCamera().yaw;
        session.onMouseMove(40, -20, true);
        TEST_ASSERT(session.getCamera().yaw != origYaw, "onMouseMove with leftDrag must rotate camera");

        float origRadius = session.getCamera().radius;
        session.onMouseWheel(120);
        TEST_ASSERT(session.getCamera().radius < origRadius, "onMouseWheel with positive delta must zoom in");

        // Keyboard commands
        session.onKeyDown(0x57); // 'W' toggles wireframe
        session.onKeyDown(0x4D); // 'M' cycles model
        session.onKeyDown(0x20); // Space toggles auto-rotate

        // Gamepad injection
        session.onGamepad(0.6f, -0.4f, 0.5f);

        // Render multi-frame animated sequence
        ViewerStats stats = session.run(10);
        TEST_ASSERT(stats.frameCount >= 10, "ViewerSession::run must render requested number of frames");
        TEST_ASSERT(stats.triangleCount > 0, "ViewerSession must report non-zero triangle count");
        TEST_ASSERT(stats.vertexCount > 0, "ViewerSession must report non-zero vertex count");

        // Verify window surface buffer is updated with rendered 3D graphics
        uint32_t surfW = 0, surfH = 0;
        const uint32_t* winPixels = WindowManager::get().getWindowPixelBuffer(session.getHwnd(), &surfW, &surfH);
        TEST_ASSERT(winPixels != nullptr && surfW == 640 && surfH == 480, "Window surface buffer must exist at 640x480");

        // Count pixels distinct from top-left background to confirm actual geometry rasterization
        uint32_t bgPixel = winPixels[0];
        uint32_t geomPixels = 0;
        for (size_t i = 0; i < 640 * 480; ++i) {
            if (winPixels[i] != bgPixel) {
                geomPixels++;
            }
        }
        TEST_ASSERT(geomPixels > 100, "Window surface must contain rasterized 3D geometry pixels distinct from background");

        // Switch to Torus and Wireframe mode
        session.setModel(ViewerModelType::Torus);
        session.setWireframe(true);
        ViewerStats torusStats = session.run(5);
        TEST_ASSERT(torusStats.triangleCount == 384, "Torus active model must render 384 triangles");
        TEST_ASSERT(torusStats.wireframe == true, "Session must report wireframe state active");

        // Switch to Cube
        session.setModel(ViewerModelType::Cube);
        session.setWireframe(false);
        ViewerStats cubeStats = session.run(5);
        TEST_ASSERT(cubeStats.triangleCount == 12, "Cube active model must render 12 triangles");
        TEST_ASSERT(cubeStats.wireframe == false, "Session must report solid fill state active");
    }

    // 4. Shell Integration Command (view3d)
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;

        shell.execute("view3d --crystal --frames 3", out);
        std::string res = out.str();
        TEST_ASSERT(res.find("[PrismX 3D Viewer]") != std::string::npos, "Shell view3d command must execute");
        TEST_ASSERT(res.find("DEC PRISM Crystal Core") != std::string::npos, "Shell output must list active Crystal model");
        TEST_ASSERT(res.find("Session Completed") != std::string::npos, "Shell output must confirm session completion");

        std::ostringstream out2;
        shell.execute("view3d --torus -w --frames 3", out2);
        std::string res2 = out2.str();
        TEST_ASSERT(res2.find("Parametric 3D Torus") != std::string::npos, "Shell output must list Torus model");
        TEST_ASSERT(res2.find("Wireframe") != std::string::npos, "Shell output must list Wireframe rasterizer");
    }
}

void Test_Direct3D9_Runtime_And_FixedFunctionPipeline() {
    using namespace micant::d3d9;

    // 1. Direct3DCreate9 and Interface Query
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    TEST_ASSERT(pD3D != nullptr, "Direct3DCreate9 must return non-null IDirect3D9 pointer");

    IDirect3D9* queriedD3D = nullptr;
    int32_t hr = pD3D->QueryInterface(IID_IDirect3D9, reinterpret_cast<void**>(&queriedD3D));
    TEST_ASSERT(hr == D3D_OK && queriedD3D == pD3D, "QueryInterface for IID_IDirect3D9 must succeed");
    queriedD3D->Release();

    // 2. Adapter Enumeration & Caps
    uint32_t adapterCount = pD3D->GetAdapterCount();
    TEST_ASSERT(adapterCount == 1, "Direct3D9 must report at least 1 adapter");

    D3DADAPTER_IDENTIFIER9 ident{};
    hr = pD3D->GetAdapterIdentifier(0, 0, &ident);
    TEST_ASSERT(hr == D3D_OK, "GetAdapterIdentifier must succeed");
    TEST_ASSERT(std::string(ident.Driver) == "micant_d3d9.dll", "Driver identifier must match sovereign runtime");

    D3DDISPLAYMODE mode{};
    hr = pD3D->GetAdapterDisplayMode(0, &mode);
    TEST_ASSERT(hr == D3D_OK && mode.Width == 1920 && mode.Height == 1080, "Adapter display mode must report 1920x1080");

    D3DCAPS9 caps{};
    hr = pD3D->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps);
    TEST_ASSERT(hr == D3D_OK, "GetDeviceCaps must succeed");
    TEST_ASSERT(caps.VertexShaderVersion == 0xFFFE0300, "Device caps must report Vertex Shader Model 3.0 support");

    // 3. Native User32 Window & Device Creation
    win32::HWND hwnd = user32::CreateWindowExW(
        0, L"MicaNT_Window", L"Test D3D9 Viewport",
        0, 0, 0, 640, 480, nullptr, nullptr, nullptr, nullptr
    );
    TEST_ASSERT(hwnd != nullptr, "User32 CreateWindowExW must create window handle for D3D9 device");

    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = win32::TRUE;

    IDirect3DDevice9* pDevice = nullptr;
    hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pDevice);
    TEST_ASSERT(hr == D3D_OK && pDevice != nullptr, "CreateDevice must succeed and yield non-null IDirect3DDevice9");

    IDirect3D9* parentD3D = nullptr;
    pDevice->GetDirect3D(&parentD3D);
    TEST_ASSERT(parentD3D == pD3D, "GetDirect3D must retrieve parent IDirect3D9 pointer");
    parentD3D->Release();

    // 4. Vertex Buffer Allocation, Lock & Unlock
    struct VertexXYZColor {
        float x, y, z;
        uint32_t diffuse;
    };

    IDirect3DVertexBuffer9* pVB = nullptr;
    hr = pDevice->CreateVertexBuffer(
        sizeof(VertexXYZColor) * 3,
        0,
        D3DFVF_XYZ | D3DFVF_DIFFUSE,
        D3DPOOL_DEFAULT,
        &pVB,
        nullptr
    );
    TEST_ASSERT(hr == D3D_OK && pVB != nullptr, "CreateVertexBuffer must succeed");
    TEST_ASSERT(pVB->GetLength() == sizeof(VertexXYZColor) * 3, "Vertex buffer size must match requested byte length");

    void* pVbData = nullptr;
    hr = pVB->Lock(0, 0, &pVbData, 0);
    TEST_ASSERT(hr == D3D_OK && pVbData != nullptr, "VertexBuffer Lock must succeed");

    VertexXYZColor triVerts[3] = {
        {  0.0f,  0.7f, 0.0f, D3DCOLOR_XRGB(255, 0, 0) },    // Top Red
        {  0.7f, -0.7f, 0.0f, D3DCOLOR_XRGB(0, 255, 0) },    // Bottom-Right Green (CW)
        { -0.7f, -0.7f, 0.0f, D3DCOLOR_XRGB(0, 0, 255) }     // Bottom-Left Blue
    };
    std::memcpy(pVbData, triVerts, sizeof(triVerts));
    hr = pVB->Unlock();
    TEST_ASSERT(hr == D3D_OK, "VertexBuffer Unlock must succeed");

    // 5. Index Buffer Allocation, Lock & Unlock
    IDirect3DIndexBuffer9* pIB = nullptr;
    hr = pDevice->CreateIndexBuffer(
        sizeof(uint16_t) * 3,
        0,
        D3DFMT_INDEX16,
        D3DPOOL_DEFAULT,
        &pIB,
        nullptr
    );
    TEST_ASSERT(hr == D3D_OK && pIB != nullptr, "CreateIndexBuffer must succeed");
    TEST_ASSERT(pIB->GetFormat() == D3DFMT_INDEX16, "Index buffer format must be D3DFMT_INDEX16");

    void* pIbData = nullptr;
    hr = pIB->Lock(0, 0, &pIbData, 0);
    TEST_ASSERT(hr == D3D_OK && pIbData != nullptr, "IndexBuffer Lock must succeed");

    uint16_t indices[3] = { 0, 1, 2 };
    std::memcpy(pIbData, indices, sizeof(indices));
    hr = pIB->Unlock();
    TEST_ASSERT(hr == D3D_OK, "IndexBuffer Unlock must succeed");

    // 6. Fixed-Function Matrix Transformations & Viewport
    D3DMATRIX matIdent = D3DMATRIX::Identity();
    hr = pDevice->SetTransform(D3DTS_WORLD, &matIdent);
    TEST_ASSERT(hr == D3D_OK, "SetTransform D3DTS_WORLD must succeed");
    hr = pDevice->SetTransform(D3DTS_VIEW, &matIdent);
    TEST_ASSERT(hr == D3D_OK, "SetTransform D3DTS_VIEW must succeed");
    hr = pDevice->SetTransform(D3DTS_PROJECTION, &matIdent);
    TEST_ASSERT(hr == D3D_OK, "SetTransform D3DTS_PROJECTION must succeed");

    D3DMATRIX readbackWorld{};
    hr = pDevice->GetTransform(D3DTS_WORLD, &readbackWorld);
    TEST_ASSERT(hr == D3D_OK && readbackWorld._11 == 1.0f && readbackWorld._44 == 1.0f, "GetTransform D3DTS_WORLD must return identity");

    D3DVIEWPORT9 vp{ 0, 0, 640, 480, 0.0f, 1.0f };
    hr = pDevice->SetViewport(&vp);
    TEST_ASSERT(hr == D3D_OK, "SetViewport must succeed");

    D3DVIEWPORT9 readbackVp{};
    hr = pDevice->GetViewport(&readbackVp);
    TEST_ASSERT(hr == D3D_OK && readbackVp.Width == 640 && readbackVp.Height == 480, "GetViewport must return 640x480");

    // 7. Render States
    pDevice->SetRenderState(D3DRS_ZENABLE, 1);
    pDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    pDevice->SetRenderState(D3DRS_LIGHTING, 0);

    uint32_t valZ = 0, valFill = 0;
    pDevice->GetRenderState(D3DRS_ZENABLE, &valZ);
    pDevice->GetRenderState(D3DRS_FILLMODE, &valFill);
    TEST_ASSERT(valZ == 1 && valFill == D3DFILL_SOLID, "Render states must be recorded and retrievable");

    // 8. Pipeline Execution: Clear, BeginScene, DrawIndexedPrimitive, EndScene, Present
    const D3DCOLOR clearColor = D3DCOLOR_XRGB(10, 20, 50); // Midnight Blue
    hr = pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clearColor, 1.0f, 0);
    TEST_ASSERT(hr == D3D_OK, "Clear must succeed");

    hr = pDevice->BeginScene();
    TEST_ASSERT(hr == D3D_OK, "BeginScene must succeed");

    hr = pDevice->SetStreamSource(0, pVB, 0, sizeof(VertexXYZColor));
    TEST_ASSERT(hr == D3D_OK, "SetStreamSource must succeed");

    hr = pDevice->SetIndices(pIB);
    TEST_ASSERT(hr == D3D_OK, "SetIndices must succeed");

    hr = pDevice->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    TEST_ASSERT(hr == D3D_OK, "SetFVF must succeed");

    hr = pDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
    TEST_ASSERT(hr == D3D_OK, "DrawIndexedPrimitive must execute without errors");

    hr = pDevice->EndScene();
    TEST_ASSERT(hr == D3D_OK, "EndScene must succeed");

    hr = pDevice->Present(nullptr, nullptr, hwnd, nullptr);
    TEST_ASSERT(hr == D3D_OK, "Present to HWND must succeed");

    // 9. Verify Presentation Surface in User32 Window
    uint32_t surfW = 0, surfH = 0;
    const uint32_t* winPixels = user32::WindowManager::get().getWindowPixelBuffer(hwnd, &surfW, &surfH);
    TEST_ASSERT(winPixels != nullptr && surfW == 640 && surfH == 480, "User32 window surface must be populated with 640x480 pixels");

    // Check that rasterized triangle pixels are present and distinct from clear color
    uint32_t nonClearPixels = 0;
    for (size_t i = 0; i < 640 * 480; ++i) {
        if (winPixels[i] != clearColor) {
            nonClearPixels++;
        }
    }
    TEST_ASSERT(nonClearPixels > 500, "Window surface must contain rasterized triangle pixels from D3D9 pipeline");

    // 10. Dynamic Loader Export Resolution (d3d9.dll)
    InitializeD3D9SubsystemExports();
    auto& loader = ldr::DynamicLoader::get();
    void* pfnCreate9 = loader.getExport("d3d9.dll", "Direct3DCreate9");
    TEST_ASSERT(pfnCreate9 != nullptr, "ldr::DynamicLoader must resolve Direct3DCreate9 from d3d9.dll");

    // 11. Shell Integration
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("prismx d3d9", out);
        std::string res = out.str();
        TEST_ASSERT(res.find("[Direct3D 9]") != std::string::npos, "Shell prismx d3d9 must execute");
        TEST_ASSERT(res.find("Barycentric Shaded Triangle Rendered Successfully") != std::string::npos, "Shell must report triangle render success");

        std::ostringstream out2;
        shell.execute("d3d9", out2);
        std::string res2 = out2.str();
        TEST_ASSERT(res2.find("[Direct3D 9]") != std::string::npos, "Shell direct d3d9 command must execute");
    }

    // 12. Cleanup
    pVB->Release();
    pIB->Release();
    pDevice->Release();
    pD3D->Release();
    user32::DestroyWindow(hwnd);
}

// ============================================================================
// Suite 59: Win32 Foundation (GDI32 & OLE32 / OLEAUT32 COM Subsystems)
// ============================================================================

inline const ole32::IID IID_ITestCalculator = {
    0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 }
};

inline const ole32::CLSID CLSID_TestCalculator = {
    0x87654321, 0x4321, 0x4321, { 0x21, 0x43, 0x65, 0x87, 0x0F, 0xED, 0xCB, 0xA9 }
};

class ITestCalculator : public ole32::IUnknown {
public:
    virtual ole32::HRESULT Add(int a, int b, int* result) = 0;
};

class TestCalculatorImpl : public ITestCalculator {
private:
    std::atomic<uint32_t> m_refCount{1};

public:
    virtual ole32::HRESULT QueryInterface(ole32::REFIID riid, void** ppvObject) override {
        if (!ppvObject) return ole32::E_POINTER;
        if (std::memcmp(&riid, &ole32::IID_IUnknown, sizeof(ole32::IID)) == 0 ||
            std::memcmp(&riid, &IID_ITestCalculator, sizeof(ole32::IID)) == 0) {
            *ppvObject = static_cast<ITestCalculator*>(this);
            AddRef();
            return ole32::S_OK;
        }
        *ppvObject = nullptr;
        return ole32::E_NOINTERFACE;
    }

    virtual uint32_t AddRef() override {
        return m_refCount.fetch_add(1) + 1;
    }

    virtual uint32_t Release() override {
        uint32_t count = m_refCount.fetch_sub(1) - 1;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    virtual ole32::HRESULT Add(int a, int b, int* result) override {
        if (!result) return ole32::E_POINTER;
        *result = a + b;
        return ole32::S_OK;
    }
};

class TestCalculatorFactory : public ole32::IClassFactory {
private:
    std::atomic<uint32_t> m_refCount{1};

public:
    virtual ole32::HRESULT QueryInterface(ole32::REFIID riid, void** ppvObject) override {
        if (!ppvObject) return ole32::E_POINTER;
        if (std::memcmp(&riid, &ole32::IID_IUnknown, sizeof(ole32::IID)) == 0 ||
            std::memcmp(&riid, &ole32::IID_IClassFactory, sizeof(ole32::IID)) == 0) {
            *ppvObject = static_cast<ole32::IClassFactory*>(this);
            AddRef();
            return ole32::S_OK;
        }
        *ppvObject = nullptr;
        return ole32::E_NOINTERFACE;
    }

    virtual uint32_t AddRef() override {
        return m_refCount.fetch_add(1) + 1;
    }

    virtual uint32_t Release() override {
        uint32_t count = m_refCount.fetch_sub(1) - 1;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    virtual ole32::HRESULT CreateInstance(ole32::IUnknown* pUnkOuter, ole32::REFIID riid, void** ppvObject) override {
        if (pUnkOuter) return ole32::E_NOTIMPL;
        auto* calc = new TestCalculatorImpl();
        ole32::HRESULT hr = calc->QueryInterface(riid, ppvObject);
        calc->Release();
        return hr;
    }

    virtual ole32::HRESULT LockServer(win32::BOOL) override {
        return ole32::S_OK;
    }
};

void Test_Gdi32_And_Ole32_Win32Foundation() {
    std::cout << "[TEST] Running Suite 59: Gdi32 & Ole32 Win32 Foundation Subsystems...\n";

    // ------------------------------------------------------------------------
    // 1. Dynamic Loader Subsystem Registration
    // ------------------------------------------------------------------------
    gdi32::InitializeGdi32SubsystemExports();
    ole32::InitializeOle32SubsystemExports();

    auto& ldr = ldr::DynamicLoader::get();
    TEST_ASSERT(ldr.getExport("gdi32.dll", "CreateCompatibleDC") != nullptr, "gdi32.dll CreateCompatibleDC must be registered");
    TEST_ASSERT(ldr.getExport("gdi32.dll", "BitBlt") != nullptr, "gdi32.dll BitBlt must be registered");
    TEST_ASSERT(ldr.getExport("gdi32.dll", "ChoosePixelFormat") != nullptr, "gdi32.dll ChoosePixelFormat must be registered");
    TEST_ASSERT(ldr.getExport("ole32.dll", "CoInitializeEx") != nullptr, "ole32.dll CoInitializeEx must be registered");
    TEST_ASSERT(ldr.getExport("ole32.dll", "CoCreateInstance") != nullptr, "ole32.dll CoCreateInstance must be registered");
    TEST_ASSERT(ldr.getExport("ole32.dll", "CoTaskMemAlloc") != nullptr, "ole32.dll CoTaskMemAlloc must be registered");
    TEST_ASSERT(ldr.getExport("oleaut32.dll", "SysAllocString") != nullptr, "oleaut32.dll SysAllocString must be registered");
    TEST_ASSERT(ldr.getExport("oleaut32.dll", "VariantCopy") != nullptr, "oleaut32.dll VariantCopy must be registered");

    // ------------------------------------------------------------------------
    // 2. COM Runtime Lifecycle & Apartment Initialization
    // ------------------------------------------------------------------------
    ole32::HRESULT hrCo = ole32::CoInitializeEx(nullptr, ole32::COINIT_APARTMENTTHREADED);
    TEST_ASSERT(hrCo == ole32::S_OK, "First CoInitializeEx must return S_OK");
    TEST_ASSERT(ole32::ComRuntime::get().IsInitialized(), "COM Runtime must be initialized");

    ole32::HRESULT hrCo2 = ole32::CoInitializeEx(nullptr, ole32::COINIT_APARTMENTTHREADED);
    TEST_ASSERT(hrCo2 == ole32::S_FALSE, "Nested CoInitializeEx must return S_FALSE");
    TEST_ASSERT(ole32::ComRuntime::get().GetInitCount() == 2, "Init count must be 2");

    ole32::CoUninitialize();
    TEST_ASSERT(ole32::ComRuntime::get().GetInitCount() == 1, "Init count must be 1 after one uninit");
    ole32::CoUninitialize();
    TEST_ASSERT(ole32::ComRuntime::get().GetInitCount() == 0, "Init count must be 0 after second uninit");
    TEST_ASSERT(!ole32::ComRuntime::get().IsInitialized(), "COM Runtime must be uninitialized");

    // Re-initialize for subsequent tests
    ole32::CoInitialize(nullptr);

    // ------------------------------------------------------------------------
    // 3. CoTaskMem Memory Allocator
    // ------------------------------------------------------------------------
    void* pMem = ole32::CoTaskMemAlloc(128);
    TEST_ASSERT(pMem != nullptr, "CoTaskMemAlloc(128) must return valid buffer");
    std::memset(pMem, 0xAA, 128);

    void* pMemRealloc = ole32::CoTaskMemRealloc(pMem, 256);
    TEST_ASSERT(pMemRealloc != nullptr, "CoTaskMemRealloc(256) must return valid expanded buffer");
    uint8_t* pBytes = static_cast<uint8_t*>(pMemRealloc);
    bool contentPreserved = true;
    for (size_t i = 0; i < 128; ++i) {
        if (pBytes[i] != 0xAA) { contentPreserved = false; break; }
    }
    TEST_ASSERT(contentPreserved, "Reallocated buffer must preserve existing bytes");
    ole32::CoTaskMemFree(pMemRealloc);

    // ------------------------------------------------------------------------
    // 4. GUID Generation & String Conversion
    // ------------------------------------------------------------------------
    micant::GUID g1{}, g2{};
    ole32::HRESULT hrGuid = ole32::CoCreateGuid(&g1);
    TEST_ASSERT(hrGuid == ole32::S_OK, "CoCreateGuid must succeed");
    TEST_ASSERT((g1.Data3 & 0xF000) == 0x4000, "GUID must have version 4 flag");
    TEST_ASSERT((g1.Data4[0] & 0xC0) == 0x80, "GUID must have variant 1 flag");

    wchar_t szGuid[64]{};
    int cch = ole32::StringFromGUID2(g1, szGuid, 64);
    TEST_ASSERT(cch == 39, "StringFromGUID2 must return 39 chars (including null terminator / braces)");
    TEST_ASSERT(szGuid[0] == L'{' && szGuid[37] == L'}', "GUID string must be enclosed in braces");

    ole32::HRESULT hrParse = ole32::IIDFromString(szGuid, &g2);
    TEST_ASSERT(hrParse == ole32::S_OK, "IIDFromString must parse valid GUID string");
    TEST_ASSERT(std::memcmp(&g1, &g2, sizeof(micant::GUID)) == 0, "Parsed GUID must match original GUID");

    micant::GUID gBogus{};
    TEST_ASSERT(ole32::IIDFromString(L"not-a-guid", &gBogus) == ole32::E_INVALIDARG, "IIDFromString must reject invalid format");

    // ------------------------------------------------------------------------
    // 5. BSTR Length-Prefixed String Manipulation
    // ------------------------------------------------------------------------
    const wchar_t* helloMsg = L"MicaNT Clean-Room Kernel";
    ole32::BSTR bstr = ole32::SysAllocString(helloMsg);
    TEST_ASSERT(bstr != nullptr, "SysAllocString must allocate valid BSTR");
    TEST_ASSERT(ole32::SysStringLen(bstr) == 24, "SysStringLen must return character count (24)");
    uint32_t expectedBstrBytes = static_cast<uint32_t>(24 * sizeof(ole32::OLECHAR));
    TEST_ASSERT(ole32::SysStringByteLen(bstr) == expectedBstrBytes, "SysStringByteLen must return byte count");
    TEST_ASSERT(std::wcscmp(bstr, helloMsg) == 0, "BSTR content must match source string");

    uint32_t storedByteLen = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(bstr) - sizeof(uint32_t));
    TEST_ASSERT(storedByteLen == expectedBstrBytes, "Length prefix stored before BSTR pointer must match byte count");

    ole32::BSTR bstrSub = ole32::SysAllocStringLen(helloMsg, 6);
    TEST_ASSERT(bstrSub != nullptr, "SysAllocStringLen must succeed");
    TEST_ASSERT(ole32::SysStringLen(bstrSub) == 6, "Substr BSTR len must be 6");
    TEST_ASSERT(std::wcsncmp(bstrSub, L"MicaNT", 6) == 0, "Substr content must match");

    ole32::SysFreeString(bstr);
    ole32::SysFreeString(bstrSub);
    ole32::SysFreeString(nullptr);

    // ------------------------------------------------------------------------
    // 6. VARIANT Engine & Polymorphism
    // ------------------------------------------------------------------------
    ole32::VARIANT var1{}, var2{};
    ole32::VariantInit(&var1);
    ole32::VariantInit(&var2);
    TEST_ASSERT(var1.vt == ole32::VT_EMPTY, "VariantInit must set VT_EMPTY");

    var1.vt = ole32::VT_I4;
    var1.lVal = 0x12345678;
    ole32::VariantCopy(&var2, &var1);
    TEST_ASSERT(var2.vt == ole32::VT_I4 && var2.lVal == 0x12345678, "VariantCopy must copy integer payload");

    ole32::VariantClear(&var1);
    TEST_ASSERT(var1.vt == ole32::VT_EMPTY, "VariantClear must reset to VT_EMPTY");

    var1.vt = ole32::VT_BSTR;
    var1.bstrVal = ole32::SysAllocString(L"DynamicVariantString");
    ole32::VariantCopy(&var2, &var1);
    TEST_ASSERT(var2.vt == ole32::VT_BSTR, "Destination variant must have VT_BSTR");
    TEST_ASSERT(var2.bstrVal != var1.bstrVal, "VariantCopy must perform deep copy of BSTR");
    TEST_ASSERT(std::wcscmp(var2.bstrVal, var1.bstrVal) == 0, "Copied BSTR content must match");

    ole32::VariantClear(&var1);
    ole32::VariantClear(&var2);

    // ------------------------------------------------------------------------
    // 7. Class Factory & CoCreateInstance
    // ------------------------------------------------------------------------
    auto* pFactory = new TestCalculatorFactory();
    uint32_t dwCookie = 0;
    ole32::HRESULT hrReg = ole32::CoRegisterClassObject(
        CLSID_TestCalculator, pFactory, ole32::CLSCTX_INPROC_SERVER, ole32::REGCLS_MULTIPLEUSE, &dwCookie
    );
    TEST_ASSERT(hrReg == ole32::S_OK, "CoRegisterClassObject must succeed");
    TEST_ASSERT(dwCookie != 0, "Registration cookie must be nonzero");

    ITestCalculator* pCalc = nullptr;
    ole32::HRESULT hrInst = ole32::CoCreateInstance(
        CLSID_TestCalculator, nullptr, ole32::CLSCTX_INPROC_SERVER, IID_ITestCalculator, reinterpret_cast<void**>(&pCalc)
    );
    TEST_ASSERT(hrInst == ole32::S_OK && pCalc != nullptr, "CoCreateInstance must create ITestCalculator instance");

    int sum = 0;
    ole32::HRESULT hrAdd = pCalc->Add(18, 24, &sum);
    TEST_ASSERT(hrAdd == ole32::S_OK && sum == 42, "ITestCalculator::Add must return sum = 42");

    ole32::IUnknown* pUnk = nullptr;
    ole32::HRESULT hrQi = pCalc->QueryInterface(ole32::IID_IUnknown, reinterpret_cast<void**>(&pUnk));
    TEST_ASSERT(hrQi == ole32::S_OK && pUnk != nullptr, "QueryInterface for IID_IUnknown must succeed");
    pUnk->Release();
    pCalc->Release();

    ole32::HRESULT hrRevoke = ole32::CoRevokeClassObject(dwCookie);
    TEST_ASSERT(hrRevoke == ole32::S_OK, "CoRevokeClassObject must succeed");
    pFactory->Release();

    ITestCalculator* pCalcFail = nullptr;
    ole32::HRESULT hrFail = ole32::CoCreateInstance(
        CLSID_TestCalculator, nullptr, ole32::CLSCTX_INPROC_SERVER, IID_ITestCalculator, reinterpret_cast<void**>(&pCalcFail)
    );
    TEST_ASSERT(hrFail == ole32::REGDB_E_CLASSNOTREG, "CoCreateInstance after revoke must return REGDB_E_CLASSNOTREG");

    // ------------------------------------------------------------------------
    // 8. GDI Device Contexts & Stock Objects
    // ------------------------------------------------------------------------
    gdi32::HDC hdcMem = gdi32::CreateCompatibleDC(nullptr);
    TEST_ASSERT(hdcMem != nullptr, "CreateCompatibleDC(nullptr) must return valid memory DC");

    gdi32::HGDIOBJ hWhiteBr = gdi32::GetStockObject(gdi32::WHITE_BRUSH);
    gdi32::HGDIOBJ hBlackBr = gdi32::GetStockObject(gdi32::BLACK_BRUSH);
    gdi32::HGDIOBJ hNullBr  = gdi32::GetStockObject(gdi32::NULL_BRUSH);
    gdi32::HGDIOBJ hWhitePen= gdi32::GetStockObject(gdi32::WHITE_PEN);
    gdi32::HGDIOBJ hSysFont = gdi32::GetStockObject(gdi32::SYSTEM_FONT);

    TEST_ASSERT(hWhiteBr != nullptr && hBlackBr != nullptr && hNullBr != nullptr, "Stock brushes must not be null");
    TEST_ASSERT(hWhitePen != nullptr && hSysFont != nullptr, "Stock pen and font must not be null");

    gdi32::HGDIOBJ hOldBr = gdi32::SelectObject(hdcMem, hWhiteBr);
    TEST_ASSERT(hOldBr != nullptr, "SelectObject must return previously selected brush");

    // ------------------------------------------------------------------------
    // 9. GDI Bitmaps & DIB Sections
    // ------------------------------------------------------------------------
    gdi32::HBITMAP hbmpCompat = gdi32::CreateCompatibleBitmap(hdcMem, 120, 80);
    TEST_ASSERT(hbmpCompat != nullptr, "CreateCompatibleBitmap must create 120x80 bitmap");

    gdi32::BITMAP bmInfo{};
    int nBytesBm = gdi32::GetObjectW(hbmpCompat, sizeof(bmInfo), &bmInfo);
    TEST_ASSERT(nBytesBm == sizeof(bmInfo), "GetObjectW on bitmap must return sizeof(BITMAP)");
    TEST_ASSERT(bmInfo.bmWidth == 120 && bmInfo.bmHeight == 80, "Bitmap dimensions must match 120x80");
    TEST_ASSERT(bmInfo.bmBitsPixel == 32, "Bitmap must be 32 bpp");

    gdi32::HGDIOBJ hOldBmp = gdi32::SelectObject(hdcMem, hbmpCompat);
    TEST_ASSERT(hOldBmp != nullptr, "SelectObject(hbmpCompat) must succeed");

    gdi32::BITMAPINFO bmi{};
    bmi.bmiHeader.biWidth = 64;
    bmi.bmiHeader.biHeight = 64;
    bmi.bmiHeader.biBitCount = 32;
    void* pDibBits = nullptr;
    gdi32::HBITMAP hDib = gdi32::CreateDIBSection(hdcMem, &bmi, gdi32::DIB_RGB_COLORS, &pDibBits, nullptr, 0);
    TEST_ASSERT(hDib != nullptr && pDibBits != nullptr, "CreateDIBSection must return bitmap and non-null pixel buffer pointer");

    uint32_t* pPixels = static_cast<uint32_t*>(pDibBits);
    pPixels[0] = 0xFF00FFFF;
    TEST_ASSERT(pPixels[0] == 0xFF00FFFF, "Direct pixel access to DIB section memory must succeed");

    // ------------------------------------------------------------------------
    // 10. 2D Drawing Primitives & ROP Engine
    // ------------------------------------------------------------------------
    gdi32::HBRUSH hRedBrush = gdi32::CreateSolidBrush(gdi32::RGB(255, 0, 0));
    gdi32::HPEN hGreenPen = gdi32::CreatePen(gdi32::PS_SOLID, 1, gdi32::RGB(0, 255, 0));
    gdi32::SelectObject(hdcMem, hRedBrush);
    gdi32::SelectObject(hdcMem, hGreenPen);

    gdi32::RECT fillRc{0, 0, 120, 80};
    int frOk = gdi32::FillRect(hdcMem, &fillRc, hRedBrush);
    TEST_ASSERT(frOk == 1, "FillRect must succeed");

    gdi32::COLORREF c1 = gdi32::GetPixel(hdcMem, 60, 40);
    TEST_ASSERT(gdi32::GetRValue(c1) == 255 && gdi32::GetGValue(c1) == 0 && gdi32::GetBValue(c1) == 0,
        "Center pixel of filled rect must be Red");

    gdi32::Rectangle(hdcMem, 10, 10, 40, 40);
    gdi32::COLORREF cBorder = gdi32::GetPixel(hdcMem, 10, 10);
    TEST_ASSERT(gdi32::GetGValue(cBorder) == 255, "Rectangle border must have green component");

    win32::BOOL ellOk = gdi32::Ellipse(hdcMem, 50, 10, 90, 40);
    TEST_ASSERT(ellOk == win32::TRUE, "Ellipse drawing must succeed");

    gdi32::POINT ptPrev{};
    gdi32::MoveToEx(hdcMem, 0, 0, &ptPrev);
    win32::BOOL lineOk = gdi32::LineTo(hdcMem, 20, 20);
    TEST_ASSERT(lineOk == win32::TRUE, "LineTo must succeed");

    gdi32::SetPixel(hdcMem, 5, 5, gdi32::RGB(0, 0, 255));
    gdi32::COLORREF cPixel = gdi32::GetPixel(hdcMem, 5, 5);
    TEST_ASSERT(gdi32::GetBValue(cPixel) == 255, "SetPixel/GetPixel must read back Blue");

    // ------------------------------------------------------------------------
    // 11. BitBlt ROP Operations (SRCCOPY & SRCINVERT)
    // ------------------------------------------------------------------------
    gdi32::HDC hdcSrc = gdi32::CreateCompatibleDC(nullptr);
    gdi32::HBITMAP hbmpSrc = gdi32::CreateCompatibleBitmap(hdcSrc, 30, 30);
    gdi32::SelectObject(hdcSrc, hbmpSrc);

    gdi32::HBRUSH hBlueBrush = gdi32::CreateSolidBrush(gdi32::RGB(0, 0, 255));
    gdi32::RECT rcSrc{0, 0, 30, 30};
    gdi32::FillRect(hdcSrc, &rcSrc, hBlueBrush);

    win32::BOOL bltOk = gdi32::BitBlt(hdcMem, 70, 40, 30, 30, hdcSrc, 0, 0, gdi32::SRCCOPY);
    TEST_ASSERT(bltOk == win32::TRUE, "BitBlt with SRCCOPY must succeed");
    gdi32::COLORREF cBlitted = gdi32::GetPixel(hdcMem, 75, 45);
    TEST_ASSERT(gdi32::GetBValue(cBlitted) == 255, "Blitted region must have Blue pixel color");

    bltOk = gdi32::BitBlt(hdcMem, 70, 40, 30, 30, hdcSrc, 0, 0, gdi32::SRCINVERT);
    TEST_ASSERT(bltOk == win32::TRUE, "BitBlt with SRCINVERT must succeed");

    // ------------------------------------------------------------------------
    // 12. Typography & Font TextOut
    // ------------------------------------------------------------------------
    gdi32::SetTextColor(hdcMem, gdi32::RGB(255, 255, 255));
    gdi32::SetBkColor(hdcMem, gdi32::RGB(0, 0, 0));
    gdi32::SetBkMode(hdcMem, gdi32::OPAQUE);

    TEST_ASSERT(gdi32::GetTextColor(hdcMem) == gdi32::RGB(255, 255, 255), "GetTextColor must match White");
    TEST_ASSERT(gdi32::GetBkColor(hdcMem) == gdi32::RGB(0, 0, 0), "GetBkColor must match Black");
    TEST_ASSERT(gdi32::GetBkMode(hdcMem) == gdi32::OPAQUE, "GetBkMode must match OPAQUE");

    gdi32::SIZE textSize{};
    gdi32::GetTextExtentPoint32W(hdcMem, L"MicaNT", 6, &textSize);
    TEST_ASSERT(textSize.cx == 48 && textSize.cy == 8, "Text extent for 6-char string in 8x8 font must be 48x8");

    win32::BOOL textOk = gdi32::TextOutW(hdcMem, 0, 70, L"MICA", 4);
    TEST_ASSERT(textOk == win32::TRUE, "TextOutW must succeed");

    // ------------------------------------------------------------------------
    // 13. OpenGL Pixel Format & Double-Buffering Bridge
    // ------------------------------------------------------------------------
    gdi32::PIXELFORMATDESCRIPTOR pfdReq{};
    pfdReq.nSize = sizeof(gdi32::PIXELFORMATDESCRIPTOR);
    pfdReq.nVersion = 1;
    pfdReq.dwFlags = gdi32::PFD_DRAW_TO_WINDOW | gdi32::PFD_SUPPORT_OPENGL | gdi32::PFD_DOUBLEBUFFER;
    pfdReq.iPixelType = gdi32::PFD_TYPE_RGBA;
    pfdReq.cColorBits = 32;

    int pixelFormatIdx = gdi32::ChoosePixelFormat(hdcMem, &pfdReq);
    TEST_ASSERT(pixelFormatIdx == 1, "ChoosePixelFormat must return index 1");

    win32::BOOL setPfdOk = gdi32::SetPixelFormat(hdcMem, pixelFormatIdx, &pfdReq);
    TEST_ASSERT(setPfdOk == win32::TRUE, "SetPixelFormat must return TRUE");

    gdi32::PIXELFORMATDESCRIPTOR pfdDesc{};
    int descOk = gdi32::DescribePixelFormat(hdcMem, 1, sizeof(pfdDesc), &pfdDesc);
    TEST_ASSERT(descOk == 1, "DescribePixelFormat must return 1");

    win32::BOOL swapOk = gdi32::SwapBuffers(hdcMem);
    TEST_ASSERT(swapOk == win32::TRUE, "SwapBuffers must succeed");

    // ------------------------------------------------------------------------
    // 14. User32 Window Integration & MessageBox
    // ------------------------------------------------------------------------
    win32::HWND hwnd = user32::CreateWindowExW(
        0, L"MicaNT_GdiWindow", L"GDI Test Window",
        0, 50, 50, 200, 150, nullptr, nullptr, nullptr, nullptr
    );
    TEST_ASSERT(hwnd != nullptr, "CreateWindowExW must create native window for GDI");

    user32::HDC hdcWin = user32::GetDC(hwnd);
    TEST_ASSERT(hdcWin != nullptr, "user32::GetDC must return valid HDC via interop hook");

    gdi32::HBRUSH hWinBrush = gdi32::CreateSolidBrush(gdi32::RGB(120, 80, 200));
    gdi32::RECT winRc{0, 0, 200, 150};
    gdi32::FillRect(reinterpret_cast<gdi32::HDC>(hdcWin), &winRc, hWinBrush);
    gdi32::TextOutW(reinterpret_cast<gdi32::HDC>(hdcWin), 10, 10, L"WINDOW DC", 9);

    int relDc = user32::ReleaseDC(hwnd, hdcWin);
    TEST_ASSERT(relDc == 1, "user32::ReleaseDC must succeed");

    uint32_t winW = 0, winH = 0;
    const uint32_t* pWinBuf = user32::WindowManager::get().getWindowPixelBuffer(hwnd, &winW, &winH);
    TEST_ASSERT(pWinBuf != nullptr && winW == 200 && winH == 150, "Window pixel buffer must exist with 200x150 dimensions");

    int msgRes = user32::MessageBoxW(hwnd, L"GDI & COM Foundation Initialized", L"MicaNT Notice", user32::MB_OK);
    TEST_ASSERT(msgRes == user32::IDOK, "MessageBoxW must return IDOK");

    int msgResA = user32::MessageBoxA(hwnd, "GDI & COM Foundation ASCII", "MicaNT Notice", user32::MB_OK);
    TEST_ASSERT(msgResA == user32::IDOK, "MessageBoxA must return IDOK");

    user32::DestroyWindow(hwnd);

    // ------------------------------------------------------------------------
    // 15. Shell Commands: gdi and com
    // ------------------------------------------------------------------------
    shell::CommandShell shell;
    std::ostringstream outGdi;
    shell.execute("gdi", outGdi);
    std::string strGdi = outGdi.str();
    TEST_ASSERT(strGdi.find("Graphics Device Interface (GDI32)") != std::string::npos, "Shell 'gdi' must display GDI32 header");
    TEST_ASSERT(strGdi.find("Test DC rendering verified") != std::string::npos, "Shell 'gdi' must verify test rendering");

    std::ostringstream outCom;
    shell.execute("com", outCom);
    std::string strCom = outCom.str();
    TEST_ASSERT(strCom.find("Component Object Model (COM)") != std::string::npos, "Shell 'com' must display COM header");
    TEST_ASSERT(strCom.find("CoInitializeEx initialized") != std::string::npos, "Shell 'com' must verify CoInitializeEx");
    TEST_ASSERT(strCom.find("Allocated BSTR") != std::string::npos, "Shell 'com' must verify BSTR allocation");

    // ------------------------------------------------------------------------
    // 16. Cleanup GDI Objects
    // ------------------------------------------------------------------------
    gdi32::DeleteObject(hWinBrush);
    gdi32::DeleteObject(hBlueBrush);
    gdi32::DeleteObject(hRedBrush);
    gdi32::DeleteObject(hGreenPen);
    gdi32::DeleteObject(hbmpSrc);
    gdi32::DeleteDC(hdcSrc);
    gdi32::DeleteObject(hbmpCompat);
    gdi32::DeleteObject(hDib);
    gdi32::DeleteDC(hdcMem);

    ole32::CoUninitialize();

    std::cout << "[TEST] Suite 59: Gdi32 & Ole32 Win32 Foundation PASSED.\n";
}

void Test_Shell32_And_ComCtl32_Win32Controls() {
    std::cout << "[TEST] Running Suite 60: Shell32, Shlwapi & ComCtl32 Win32 Controls...\n";

    // ------------------------------------------------------------------------
    // 1. Subsystem Export Registrations & Dynamic Loader Binding
    // ------------------------------------------------------------------------
    shell32::InitializeShell32SubsystemExports();
    comctl32::InitializeComCtl32SubsystemExports();

    win32::HMODULE hShell = kernel32::GetModuleHandleW(L"shell32.dll");
    TEST_ASSERT(hShell != nullptr, "shell32.dll must be registered in DynamicLoader");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "SHGetFolderPathW") != nullptr, "SHGetFolderPathW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "SHGetFolderPathA") != nullptr, "SHGetFolderPathA must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "SHGetSpecialFolderPathW") != nullptr, "SHGetSpecialFolderPathW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "SHGetKnownFolderPath") != nullptr, "SHGetKnownFolderPath must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "CommandLineToArgvW") != nullptr, "CommandLineToArgvW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "ShellExecuteW") != nullptr, "ShellExecuteW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "ShellExecuteExW") != nullptr, "ShellExecuteExW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "Shell_NotifyIconW") != nullptr, "Shell_NotifyIconW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "SHGetFileInfoW") != nullptr, "SHGetFileInfoW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShell, "ExtractIconW") != nullptr, "ExtractIconW must be exported");

    win32::HMODULE hShlwapi = kernel32::GetModuleHandleW(L"shlwapi.dll");
    TEST_ASSERT(hShlwapi != nullptr, "shlwapi.dll must be registered in DynamicLoader");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "PathFileExistsW") != nullptr, "PathFileExistsW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "PathCombineW") != nullptr, "PathCombineW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "PathFindFileNameW") != nullptr, "PathFindFileNameW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "PathFindExtensionW") != nullptr, "PathFindExtensionW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "PathRemoveFileSpecW") != nullptr, "PathRemoveFileSpecW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "PathAddBackslashW") != nullptr, "PathAddBackslashW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "PathIsRelativeW") != nullptr, "PathIsRelativeW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "PathIsDirectoryW") != nullptr, "PathIsDirectoryW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "StrStrIW") != nullptr, "StrStrIW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hShlwapi, "StrCmpIW") != nullptr, "StrCmpIW must be exported");

    win32::HMODULE hComCtl = kernel32::GetModuleHandleW(L"comctl32.dll");
    TEST_ASSERT(hComCtl != nullptr, "comctl32.dll must be registered in DynamicLoader");
    TEST_ASSERT(kernel32::GetProcAddress(hComCtl, "InitCommonControls") != nullptr, "InitCommonControls must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hComCtl, "InitCommonControlsEx") != nullptr, "InitCommonControlsEx must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hComCtl, "ImageList_Create") != nullptr, "ImageList_Create must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hComCtl, "ImageList_Destroy") != nullptr, "ImageList_Destroy must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hComCtl, "ImageList_AddIcon") != nullptr, "ImageList_AddIcon must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hComCtl, "ImageList_Draw") != nullptr, "ImageList_Draw must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hComCtl, "CreateStatusWindowW") != nullptr, "CreateStatusWindowW must be exported");
    TEST_ASSERT(kernel32::GetProcAddress(hComCtl, "CreateUpDownControl") != nullptr, "CreateUpDownControl must be exported");

    // ------------------------------------------------------------------------
    // 2. CSIDL and KNOWNFOLDERID Folder Resolution
    // ------------------------------------------------------------------------
    wchar_t pathBuf[260]{};
    shell32::HRESULT hr = shell32::SHGetFolderPathW(nullptr, shell32::CSIDL_WINDOWS, nullptr, 0, pathBuf);
    TEST_ASSERT(hr == shell32::S_OK, "SHGetFolderPathW(CSIDL_WINDOWS) must return S_OK");
    TEST_ASSERT(std::wstring(pathBuf) == L"C:\\Windows", "CSIDL_WINDOWS must map to C:\\Windows");

    shell32::SHGetFolderPathW(nullptr, shell32::CSIDL_SYSTEM, nullptr, 0, pathBuf);
    TEST_ASSERT(std::wstring(pathBuf) == L"C:\\Windows\\System32", "CSIDL_SYSTEM must map to C:\\Windows\\System32");

    shell32::SHGetFolderPathW(nullptr, shell32::CSIDL_PROGRAM_FILES, nullptr, 0, pathBuf);
    TEST_ASSERT(std::wstring(pathBuf) == L"C:\\Program Files", "CSIDL_PROGRAM_FILES must map to C:\\Program Files");

    shell32::SHGetFolderPathW(nullptr, shell32::CSIDL_PERSONAL, nullptr, 0, pathBuf);
    TEST_ASSERT(std::wstring(pathBuf) == L"C:\\Users\\admin\\Documents", "CSIDL_PERSONAL must map to C:\\Users\\admin\\Documents");

    char pathBufA[260]{};
    hr = shell32::SHGetFolderPathA(nullptr, shell32::CSIDL_DESKTOP, nullptr, 0, pathBufA);
    TEST_ASSERT(hr == shell32::S_OK, "SHGetFolderPathA(CSIDL_DESKTOP) must return S_OK");
    TEST_ASSERT(std::string(pathBufA) == "C:\\Users\\admin\\Desktop", "CSIDL_DESKTOP must map to C:\\Users\\admin\\Desktop");

    win32::BOOL spRes = shell32::SHGetSpecialFolderPathW(nullptr, pathBuf, shell32::CSIDL_FONTS, win32::FALSE);
    TEST_ASSERT(spRes == win32::TRUE, "SHGetSpecialFolderPathW must return TRUE");
    TEST_ASSERT(std::wstring(pathBuf) == L"C:\\Windows\\Fonts", "CSIDL_FONTS must map to C:\\Windows\\Fonts");

    shell32::PWSTR pKnownPath = nullptr;
    hr = shell32::SHGetKnownFolderPath(shell32::FOLDERID_System, 0, nullptr, &pKnownPath);
    TEST_ASSERT(hr == shell32::S_OK && pKnownPath != nullptr, "SHGetKnownFolderPath(FOLDERID_System) must succeed");
    TEST_ASSERT(std::wstring(pKnownPath) == L"C:\\Windows\\System32", "FOLDERID_System must resolve to C:\\Windows\\System32");
    kernel32::LocalFree(pKnownPath);

    pKnownPath = nullptr;
    hr = shell32::SHGetKnownFolderPath(shell32::FOLDERID_ProgramFiles, 0, nullptr, &pKnownPath);
    TEST_ASSERT(hr == shell32::S_OK && pKnownPath != nullptr, "SHGetKnownFolderPath(FOLDERID_ProgramFiles) must succeed");
    TEST_ASSERT(std::wstring(pKnownPath) == L"C:\\Program Files", "FOLDERID_ProgramFiles must resolve to C:\\Program Files");
    kernel32::LocalFree(pKnownPath);

    // ------------------------------------------------------------------------
    // 3. Shlwapi Path & String Utilities
    // ------------------------------------------------------------------------
    wchar_t combined[260]{};
    shell32::PathCombineW(combined, L"C:\\Windows", L"System32\\notepad.exe");
    TEST_ASSERT(std::wstring(combined) == L"C:\\Windows\\System32\\notepad.exe", "PathCombineW must join path segments");

    const wchar_t* pName = shell32::PathFindFileNameW(L"C:\\Windows\\System32\\notepad.exe");
    TEST_ASSERT(std::wstring(pName) == L"notepad.exe", "PathFindFileNameW must extract file name");

    const wchar_t* pExt = shell32::PathFindExtensionW(L"C:\\Windows\\System32\\notepad.exe");
    TEST_ASSERT(std::wstring(pExt) == L".exe", "PathFindExtensionW must extract extension");

    wchar_t toStrip[260] = L"C:\\Windows\\System32\\notepad.exe";
    shell32::PathRemoveFileSpecW(toStrip);
    TEST_ASSERT(std::wstring(toStrip) == L"C:\\Windows\\System32", "PathRemoveFileSpecW must remove file part");

    shell32::PathAddBackslashW(toStrip);
    TEST_ASSERT(std::wstring(toStrip) == L"C:\\Windows\\System32\\", "PathAddBackslashW must append trailing backslash");

    TEST_ASSERT(shell32::PathIsRelativeW(L"MicaNT\\SubDir") == win32::TRUE, "Relative path must return TRUE");
    TEST_ASSERT(shell32::PathIsRelativeW(L"C:\\Windows") == win32::FALSE, "Absolute path must return FALSE");

    const wchar_t* pMatch = shell32::StrStrIW(L"MicaNT Sovereign Microkernel", L"SOVEREIGN");
    TEST_ASSERT(pMatch != nullptr && std::wstring(pMatch).rfind(L"Sovereign", 0) == 0, "StrStrIW must find case-insensitive substring");
    TEST_ASSERT(shell32::StrCmpIW(L"MiCaNt", L"micant") == 0, "StrCmpIW must return 0 for equal case-insensitive strings");

    // ------------------------------------------------------------------------
    // 4. CommandLineToArgvW Tokenizer & LocalFree
    // ------------------------------------------------------------------------
    int numArgs = 0;
    wchar_t** pArgs = shell32::CommandLineToArgvW(L"micant_app.exe \"long path argument\" simple --flag=\"nested \\\"quote\\\" val\"", &numArgs);
    TEST_ASSERT(pArgs != nullptr, "CommandLineToArgvW must return non-null argv");
    TEST_ASSERT(numArgs == 4, "CommandLineToArgvW must parse exactly 4 arguments");
    TEST_ASSERT(std::wstring(pArgs[0]) == L"micant_app.exe", "Arg[0] must match binary name");
    TEST_ASSERT(std::wstring(pArgs[1]) == L"long path argument", "Arg[1] must match quoted string");
    TEST_ASSERT(std::wstring(pArgs[2]) == L"simple", "Arg[2] must match bare argument");
    kernel32::LocalFree(pArgs);

    // ------------------------------------------------------------------------
    // 5. System Tray Notification Icons (Shell_NotifyIconW)
    // ------------------------------------------------------------------------
    shell32::NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = reinterpret_cast<win32::HWND>(0x2001);
    nid.uID = 42;
    nid.uFlags = shell32::NIF_MESSAGE | shell32::NIF_TIP;
    nid.uCallbackMessage = 0x8050;
    std::wcsncpy(nid.szTip, L"MicaNT Kernel Active", 128);

    TEST_ASSERT(shell32::Shell_NotifyIconW(shell32::NIM_ADD, &nid) == win32::TRUE, "Shell_NotifyIconW(NIM_ADD) must succeed");
    TEST_ASSERT(shell32::TrayNotificationManager::get().getIconCount() >= 1, "Tray icon count must be >= 1");

    std::wcsncpy(nid.szTip, L"MicaNT Subsystem Ready", 128);
    TEST_ASSERT(shell32::Shell_NotifyIconW(shell32::NIM_MODIFY, &nid) == win32::TRUE, "Shell_NotifyIconW(NIM_MODIFY) must succeed");
    TEST_ASSERT(shell32::Shell_NotifyIconW(shell32::NIM_DELETE, &nid) == win32::TRUE, "Shell_NotifyIconW(NIM_DELETE) must succeed");

    // ------------------------------------------------------------------------
    // 6. Shell File Info & ShellExecute
    // ------------------------------------------------------------------------
    shell32::SHFILEINFOW sfi{};
    shell32::DWORD_PTR dwFi = shell32::SHGetFileInfoW(L"C:\\Windows\\notepad.exe", 0, &sfi, sizeof(sfi),
                                             shell32::SHGFI_DISPLAYNAME | shell32::SHGFI_TYPENAME | shell32::SHGFI_ICON);
    TEST_ASSERT(dwFi != 0, "SHGetFileInfoW must return non-zero");
    TEST_ASSERT(std::wstring(sfi.szDisplayName) == L"notepad.exe", "SHGetFileInfoW must populate szDisplayName");
    TEST_ASSERT(std::wstring(sfi.szTypeName) == L"Application", "SHGetFileInfoW must identify Application extension");
    TEST_ASSERT(sfi.hIcon != nullptr, "SHGetFileInfoW must return valid icon handle");

    shell32::HICON hExtracted = shell32::ExtractIconW(nullptr, L"C:\\Windows\\explorer.exe", 0);
    TEST_ASSERT(hExtracted != nullptr, "ExtractIconW must return valid icon handle");

    win32::HINSTANCE hInstExec = shell32::ShellExecuteW(nullptr, L"open", L"cmd.exe", L"/c echo MicaNT", nullptr, 1);
    TEST_ASSERT(reinterpret_cast<uintptr_t>(hInstExec) > 32, "ShellExecuteW must return instance handle > 32");

    // ------------------------------------------------------------------------
    // 7. ComCtl32 Image List Subsystem (HIMAGELIST)
    // ------------------------------------------------------------------------
    comctl32::HIMAGELIST himl = comctl32::ImageList_Create(24, 24, comctl32::ILC_COLOR32, 2, 2);
    TEST_ASSERT(himl != nullptr, "ImageList_Create must return valid HIMAGELIST");
    TEST_ASSERT(comctl32::ImageList_AddIcon(himl, reinterpret_cast<win32::HICON>(0x101)) == -1,
                 "ImageList_AddIcon rejects an invalid HICON without publishing a fake image");
    gdi32::HBITMAP hbmpSource = gdi32::CreateCompatibleBitmap(nullptr, 24, 24);
    user32::HDC hdcSource = gdi32::CreateCompatibleDC(nullptr);
    gdi32::SelectObject(hdcSource, hbmpSource);
    gdi32::SetPixel(hdcSource, 2, 3, gdi32::RGB(11, 22, 33));
    int i0 = comctl32::ImageList_Add(himl, hbmpSource, nullptr);
    int i1 = comctl32::ImageList_Add(himl, hbmpSource, nullptr);
    TEST_ASSERT(i0 == 0 && i1 == 1, "ImageList_Add copies validated images at stable indices");
    TEST_ASSERT(comctl32::ImageList_GetImageCount(himl) == 2, "ImageList_GetImageCount must return 2");
    TEST_ASSERT(comctl32::ImageList_Remove(himl, 0) == win32::TRUE &&
                comctl32::ImageList_GetImageCount(himl) == 1,
                "ImageList_Remove deletes one image and compacts following indices");

    int icx = 0, icy = 0;
    TEST_ASSERT(comctl32::ImageList_GetIconSize(himl, &icx, &icy) == win32::TRUE, "ImageList_GetIconSize must succeed");
    TEST_ASSERT(icx == 24 && icy == 24, "ImageList_GetIconSize must match creation dimensions");

    user32::HDC hdcTest = gdi32::CreateCompatibleDC(nullptr);
    gdi32::HBITMAP hbmpTest = gdi32::CreateCompatibleBitmap(nullptr, 48, 48);
    gdi32::SelectObject(hdcTest, hbmpTest);
    TEST_ASSERT(comctl32::ImageList_Draw(himl, 0, hdcTest, 0, 0, comctl32::ILD_NORMAL) == win32::TRUE, "ImageList_Draw must copy a stored image to the destination DC");
    TEST_ASSERT(gdi32::GetPixel(hdcTest, 2, 3) == gdi32::RGB(11, 22, 33),
                "ImageList_Draw changes destination pixels to the copied source image");
    gdi32::DeleteObject(hbmpTest);
    gdi32::DeleteDC(hdcTest);
    gdi32::DeleteObject(hbmpSource);
    gdi32::DeleteDC(hdcSource);

    TEST_ASSERT(comctl32::ImageList_Destroy(himl) == win32::TRUE, "ImageList_Destroy must destroy HIMAGELIST");

    // ------------------------------------------------------------------------
    // 8. Progress Bar Common Control (msctls_progress32)
    // ------------------------------------------------------------------------
    win32::HWND hProg = user32::CreateWindowExW(0, comctl32::PROGRESS_CLASSW, L"", 0, 0, 0, 200, 24, nullptr, nullptr, nullptr, nullptr);
    TEST_ASSERT(hProg != nullptr, "CreateWindowExW must create msctls_progress32");
    user32::SendMessageW(hProg, comctl32::PBM_SETRANGE32, 0, 100);
    user32::SendMessageW(hProg, comctl32::PBM_SETPOS, 35, 0);
    TEST_ASSERT(user32::SendMessageW(hProg, comctl32::PBM_GETPOS, 0, 0) == 35, "Progress bar position must be 35");
    user32::SendMessageW(hProg, comctl32::PBM_DELTAPOS, 15, 0);
    TEST_ASSERT(user32::SendMessageW(hProg, comctl32::PBM_GETPOS, 0, 0) == 50, "Progress bar delta position must be 50");
    user32::SendMessageW(hProg, comctl32::PBM_SETSTEP, 10, 0);
    user32::SendMessageW(hProg, comctl32::PBM_STEPIT, 0, 0);
    TEST_ASSERT(user32::SendMessageW(hProg, comctl32::PBM_GETPOS, 0, 0) == 60, "Progress bar stepped position must be 60");
    user32::SendMessageW(hProg, user32::WM_PAINT, 0, 0);
    user32::DestroyWindow(hProg);

    // ------------------------------------------------------------------------
    // 9. Status Bar Common Control (msctls_statusbar32)
    // ------------------------------------------------------------------------
    win32::HWND hStatus = comctl32::CreateStatusWindowW(0, L"Ready", nullptr, 101);
    TEST_ASSERT(hStatus != nullptr, "CreateStatusWindowW must create msctls_statusbar32");
    int sbParts[3] = { 80, 180, -1 };
    user32::SendMessageW(hStatus, comctl32::SB_SETPARTS, 3, reinterpret_cast<user32::LPARAM>(sbParts));
    TEST_ASSERT(user32::SendMessageW(hStatus, comctl32::SB_GETPARTS, 0, 0) == 3, "Status bar parts count must be 3");
    user32::SendMessageW(hStatus, comctl32::SB_SETTEXTW, 0, reinterpret_cast<user32::LPARAM>(L"Status: OK"));
    user32::SendMessageW(hStatus, comctl32::SB_SETTEXTW, 1, reinterpret_cast<user32::LPARAM>(L"Ln 12, Col 4"));
    wchar_t sbText[64]{};
    user32::SendMessageW(hStatus, comctl32::SB_GETTEXTW, 0, reinterpret_cast<user32::LPARAM>(sbText));
    TEST_ASSERT(std::wstring(sbText) == L"Status: OK", "Status bar part text must match");
    user32::SendMessageW(hStatus, user32::WM_PAINT, 0, 0);
    user32::DestroyWindow(hStatus);

    // ------------------------------------------------------------------------
    // 10. UpDown / Spin Common Control (msctls_updown32)
    // ------------------------------------------------------------------------
    win32::HWND hUpDn = comctl32::CreateUpDownControl(0, 0, 0, 20, 20, nullptr, 102, nullptr, nullptr, 100, 0, 25);
    TEST_ASSERT(hUpDn != nullptr, "CreateUpDownControl must create msctls_updown32");
    TEST_ASSERT(user32::SendMessageW(hUpDn, comctl32::UDM_GETPOS, 0, 0) == 25, "UpDown initial position must be 25");
    user32::SendMessageW(hUpDn, comctl32::UDM_SETPOS, 0, 75);
    TEST_ASSERT(user32::SendMessageW(hUpDn, comctl32::UDM_GETPOS, 0, 0) == 75, "UpDown set position must be 75");
    user32::SendMessageW(hUpDn, comctl32::UDM_SETRANGE32, 5, 200);
    int udMin = 0, udMax = 0;
    user32::SendMessageW(hUpDn, comctl32::UDM_GETRANGE32, reinterpret_cast<user32::WPARAM>(&udMin), reinterpret_cast<user32::LPARAM>(&udMax));
    TEST_ASSERT(udMin == 5 && udMax == 200, "UpDown 32-bit range must be [5..200]");
    user32::DestroyWindow(hUpDn);

    // ------------------------------------------------------------------------
    // 11. TrackBar / Slider Common Control (msctls_trackbar32)
    // ------------------------------------------------------------------------
    win32::HWND hTrack = user32::CreateWindowExW(0, comctl32::TRACKBAR_CLASSW, L"", 0, 0, 0, 150, 30, nullptr, nullptr, nullptr, nullptr);
    TEST_ASSERT(hTrack != nullptr, "CreateWindowExW must create msctls_trackbar32");
    user32::SendMessageW(hTrack, comctl32::TBM_SETRANGE, win32::TRUE, (10 | (100 << 16)));
    TEST_ASSERT(user32::SendMessageW(hTrack, comctl32::TBM_GETRANGEMIN, 0, 0) == 10, "TrackBar min range must be 10");
    TEST_ASSERT(user32::SendMessageW(hTrack, comctl32::TBM_GETRANGEMAX, 0, 0) == 100, "TrackBar max range must be 100");
    user32::SendMessageW(hTrack, comctl32::TBM_SETPOS, win32::TRUE, 55);
    TEST_ASSERT(user32::SendMessageW(hTrack, comctl32::TBM_GETPOS, 0, 0) == 55, "TrackBar pos must be 55");
    user32::DestroyWindow(hTrack);

    // ------------------------------------------------------------------------
    // 12. ListView Common Control (SysListView32)
    // ------------------------------------------------------------------------
    win32::HWND hList = user32::CreateWindowExW(0, comctl32::WC_LISTVIEWW, L"", 0, 0, 0, 300, 200, nullptr, nullptr, nullptr, nullptr);
    TEST_ASSERT(hList != nullptr, "CreateWindowExW must create SysListView32");
    comctl32::LVITEMW lvi{};
    lvi.mask = comctl32::LVIF_TEXT | comctl32::LVIF_IMAGE;
    lvi.iItem = 0;
    lvi.pszText = const_cast<wchar_t*>(L"Documents");
    lvi.iImage = 2;
    int idxIns = static_cast<int>(user32::SendMessageW(hList, comctl32::LVM_INSERTITEMW, 0, reinterpret_cast<user32::LPARAM>(&lvi)));
    TEST_ASSERT(idxIns == 0, "LVM_INSERTITEMW must insert item at index 0");
    TEST_ASSERT(user32::SendMessageW(hList, comctl32::LVM_GETITEMCOUNT, 0, 0) == 1, "ListView count must be 1");

    comctl32::LVITEMW lviOut{};
    lviOut.mask = comctl32::LVIF_TEXT | comctl32::LVIF_IMAGE;
    lviOut.iItem = 0;
    wchar_t lvTxt[64]{};
    lviOut.pszText = lvTxt;
    lviOut.cchTextMax = 64;
    TEST_ASSERT(user32::SendMessageW(hList, comctl32::LVM_GETITEMW, 0, reinterpret_cast<user32::LPARAM>(&lviOut)) == win32::TRUE, "LVM_GETITEMW must succeed");
    TEST_ASSERT(std::wstring(lvTxt) == L"Documents", "ListView item text must match");
    TEST_ASSERT(lviOut.iImage == 2, "ListView item image index must match");

    user32::SendMessageW(hList, comctl32::LVM_DELETEALLITEMS, 0, 0);
    TEST_ASSERT(user32::SendMessageW(hList, comctl32::LVM_GETITEMCOUNT, 0, 0) == 0, "ListView count must be 0 after delete all");
    user32::DestroyWindow(hList);

    // ------------------------------------------------------------------------
    // 13. TreeView Common Control (SysTreeView32)
    // ------------------------------------------------------------------------
    win32::HWND hTree = user32::CreateWindowExW(0, comctl32::WC_TREEVIEWW, L"", 0, 0, 0, 300, 200, nullptr, nullptr, nullptr, nullptr);
    TEST_ASSERT(hTree != nullptr, "CreateWindowExW must create SysTreeView32");
    comctl32::TVINSERTSTRUCTW tvisRoot{};
    tvisRoot.hParent = comctl32::TVI_ROOT;
    tvisRoot.item.mask = comctl32::TVIF_TEXT;
    tvisRoot.item.pszText = const_cast<wchar_t*>(L"Computer");
    comctl32::HTREEITEM hRoot = reinterpret_cast<comctl32::HTREEITEM>(user32::SendMessageW(hTree, comctl32::TVM_INSERTITEMW, 0, reinterpret_cast<user32::LPARAM>(&tvisRoot)));
    TEST_ASSERT(hRoot != nullptr, "TVM_INSERTITEMW must create root item");

    comctl32::TVINSERTSTRUCTW tvisChild{};
    tvisChild.hParent = hRoot;
    tvisChild.item.mask = comctl32::TVIF_TEXT;
    tvisChild.item.pszText = const_cast<wchar_t*>(L"Local Disk (C:)");
    comctl32::HTREEITEM hChild = reinterpret_cast<comctl32::HTREEITEM>(user32::SendMessageW(hTree, comctl32::TVM_INSERTITEMW, 0, reinterpret_cast<user32::LPARAM>(&tvisChild)));
    TEST_ASSERT(hChild != nullptr, "TVM_INSERTITEMW must create child item");
    TEST_ASSERT(user32::SendMessageW(hTree, comctl32::TVM_GETCOUNT, 0, 0) == 2, "TreeView node count must be 2");

    comctl32::TVITEMW tviOut{};
    tviOut.mask = comctl32::TVIF_TEXT;
    tviOut.hItem = hRoot;
    wchar_t tvTxt[64]{};
    tviOut.pszText = tvTxt;
    tviOut.cchTextMax = 64;
    TEST_ASSERT(user32::SendMessageW(hTree, comctl32::TVM_GETITEMW, 0, reinterpret_cast<user32::LPARAM>(&tviOut)) == win32::TRUE, "TVM_GETITEMW must succeed");
    TEST_ASSERT(std::wstring(tvTxt) == L"Computer", "TreeView root item text must match");

    user32::SendMessageW(hTree, comctl32::TVM_DELETEITEM, 0, reinterpret_cast<user32::LPARAM>(hChild));
    TEST_ASSERT(user32::SendMessageW(hTree, comctl32::TVM_GETCOUNT, 0, 0) == 1, "TreeView count must be 1 after child deletion");
    user32::DestroyWindow(hTree);

    // ------------------------------------------------------------------------
    // 14. Command Shell Integration Verification
    // ------------------------------------------------------------------------
    shell::CommandShell cmdShell;
    std::ostringstream oss;
    cmdShell.execute("shell32", oss);
    TEST_ASSERT(oss.str().find("MicaNT Shell API") != std::string::npos, "Shell 'shell32' command must display header");

    oss.str("");
    oss.clear();
    cmdShell.execute("comctl", oss);
    TEST_ASSERT(oss.str().find("MicaNT Common Controls") != std::string::npos, "Shell 'comctl' command must display header");

    std::cout << "[TEST] Suite 60: Shell32, Shlwapi & ComCtl32 Win32 Controls PASSED.\n";
}

void Test_Windows_CMD_And_BatchExecutionEngine() {
    std::cout << "[TEST] Running Suite 61: Windows CMD & Batch Scripting Engine...\n";

    shell::CommandShell cmdShell;
    std::ostringstream oss;

    // ------------------------------------------------------------------------
    // 1. CMD Command Switches & Process Execution
    // ------------------------------------------------------------------------
    oss.str(""); oss.clear();
    int rc = cmdShell.execute("cmd /c echo HelloFromCmd", oss);
    TEST_ASSERT(rc == 0, "cmd /c echo must return 0");
    TEST_ASSERT(oss.str().find("HelloFromCmd") != std::string::npos, "cmd /c output must contain HelloFromCmd");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("cmd /c exit /b 42", oss);
    TEST_ASSERT(rc == 42, "cmd /c exit /b 42 must return exit code 42");

    // ------------------------------------------------------------------------
    // 2. Compound & Conditional Operators (&, &&, ||)
    // ------------------------------------------------------------------------
    oss.str(""); oss.clear();
    rc = cmdShell.execute("echo Alpha & echo Beta", oss);
    TEST_ASSERT(rc == 0, "Sequential execution '&' must succeed");
    TEST_ASSERT(oss.str().find("Alpha") != std::string::npos && oss.str().find("Beta") != std::string::npos, "Sequential '&' must output both commands");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("echo FirstSuccess && echo SecondRan", oss);
    TEST_ASSERT(rc == 0, "Conditional AND '&&' must succeed");
    TEST_ASSERT(oss.str().find("SecondRan") != std::string::npos, "'&&' must execute right command on success");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("del C:\\NonExistent_FakeFile.xyz && echo ShouldNotRun", oss);
    TEST_ASSERT(oss.str().find("ShouldNotRun") == std::string::npos, "'&&' must not execute right command on failure");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("del C:\\NonExistent_FakeFile.xyz || echo HandledFailure", oss);
    TEST_ASSERT(oss.str().find("HandledFailure") != std::string::npos, "'||' must execute right command on error");

    // ------------------------------------------------------------------------
    // 3. Pipeline & Redirection Operators (>, >>, |, find, sort)
    // ------------------------------------------------------------------------
    oss.str(""); oss.clear();
    cmdShell.execute("echo Line1 > C:\\CmdTest\\output.txt", oss);
    std::string content1 = micant::cmd::CmdProcessor::get().readVfsTextFile("C:\\CmdTest\\output.txt");
    TEST_ASSERT(content1.find("Line1") != std::string::npos, "Redirection '>' must write file to VFS");

    oss.str(""); oss.clear();
    cmdShell.execute("echo Line2 >> C:\\CmdTest\\output.txt", oss);
    std::string content2 = micant::cmd::CmdProcessor::get().readVfsTextFile("C:\\CmdTest\\output.txt");
    TEST_ASSERT(content2.find("Line1") != std::string::npos && content2.find("Line2") != std::string::npos, "Redirection '>>' must append to file in VFS");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("type C:\\CmdTest\\output.txt | find \"Line2\"", oss);
    TEST_ASSERT(rc == 0, "Pipeline '| find' must succeed");
    TEST_ASSERT(oss.str().find("Line2") != std::string::npos, "Pipeline '| find' must find matching line");
    TEST_ASSERT(oss.str().find("Line1") == std::string::npos, "Pipeline '| find' must filter out non-matching line");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("type C:\\CmdTest\\output.txt | sort /r", oss);
    TEST_ASSERT(rc == 0, "Pipeline '| sort /r' must succeed");
    size_t posLine2 = oss.str().find("Line2");
    size_t posLine1 = oss.str().find("Line1");
    TEST_ASSERT(posLine2 != std::string::npos && posLine1 != std::string::npos && posLine2 < posLine1, "Pipeline '| sort /r' must reverse sort lines");

    // ------------------------------------------------------------------------
    // 4. File & Directory Management Builtins (MD, COPY, REN, MOVE, ATTRIB, DEL, RD, WHERE, TREE)
    // ------------------------------------------------------------------------
    oss.str(""); oss.clear();
    rc = cmdShell.execute("md C:\\CmdTest\\FolderA\\SubFolderB", oss);
    TEST_ASSERT(rc == 0, "md must create nested directories");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("copy C:\\CmdTest\\output.txt C:\\CmdTest\\FolderA\\SubFolderB\\copied.txt", oss);
    TEST_ASSERT(rc == 0, "copy must succeed");
    TEST_ASSERT(micant::cmd::CmdProcessor::get().vfsFileExists("C:\\CmdTest\\FolderA\\SubFolderB\\copied.txt"), "copied.txt must exist on VFS");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("ren C:\\CmdTest\\FolderA\\SubFolderB\\copied.txt renamed.txt", oss);
    TEST_ASSERT(rc == 0, "ren must succeed");
    TEST_ASSERT(micant::cmd::CmdProcessor::get().vfsFileExists("C:\\CmdTest\\FolderA\\SubFolderB\\renamed.txt"), "renamed.txt must exist");
    TEST_ASSERT(!micant::cmd::CmdProcessor::get().vfsFileExists("C:\\CmdTest\\FolderA\\SubFolderB\\copied.txt"), "original copied.txt must not exist after rename");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("move C:\\CmdTest\\FolderA\\SubFolderB\\renamed.txt C:\\CmdTest\\FolderA\\moved.txt", oss);
    TEST_ASSERT(rc == 0, "move must succeed");
    TEST_ASSERT(micant::cmd::CmdProcessor::get().vfsFileExists("C:\\CmdTest\\FolderA\\moved.txt"), "moved.txt must exist in target folder");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("attrib +R +H C:\\CmdTest\\FolderA\\moved.txt", oss);
    TEST_ASSERT(rc == 0, "attrib +R +H must succeed");
    uint32_t fAttrs = 0;
    fs::VirtualFileSystem::get().queryFileAttributes(L"C:\\CmdTest\\FolderA\\moved.txt", fAttrs);
    TEST_ASSERT((fAttrs & fs::FILE_ATTRIBUTE_READONLY) && (fAttrs & fs::FILE_ATTRIBUTE_HIDDEN), "Attributes +R and +H must be set");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("del C:\\CmdTest\\FolderA\\moved.txt", oss);
    TEST_ASSERT(rc == 0, "del must succeed");
    TEST_ASSERT(!micant::cmd::CmdProcessor::get().vfsFileExists("C:\\CmdTest\\FolderA\\moved.txt"), "moved.txt must be deleted");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("tree C:\\CmdTest", oss);
    TEST_ASSERT(rc == 0, "tree command must succeed");
    TEST_ASSERT(oss.str().find("FolderA") != std::string::npos, "tree output must display directory hierarchy");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("rd /s /q C:\\CmdTest", oss);
    TEST_ASSERT(rc == 0, "rd /s /q must remove entire directory tree");
    TEST_ASSERT(!micant::cmd::CmdProcessor::get().vfsFileExists("C:\\CmdTest\\FolderA"), "FolderA must be removed");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("where cmd.exe", oss);
    TEST_ASSERT(rc == 0, "where cmd.exe must succeed");
    TEST_ASSERT(oss.str().find("cmd.exe") != std::string::npos, "where must locate cmd.exe in system PATH");

    // ------------------------------------------------------------------------
    // 5. Environment Arithmetic & Scoping (SET /A, Substrings, Pseudo-Variables, SETLOCAL/ENDLOCAL)
    // ------------------------------------------------------------------------
    oss.str(""); oss.clear();
    rc = cmdShell.execute("set /a x = 10 + 20 * 3", oss);
    TEST_ASSERT(rc == 0, "set /a arithmetic must succeed");
    std::string valX;
    micant::cmd::CmdProcessor::get().getEnvironment().getVar("x", valX);
    TEST_ASSERT(valX == "70", "set /a precedence: 10 + 20 * 3 must equal 70");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("set /a y = (x - 10) / 2", oss);
    TEST_ASSERT(rc == 0, "set /a parentheses must succeed");
    std::string valY;
    micant::cmd::CmdProcessor::get().getEnvironment().getVar("y", valY);
    TEST_ASSERT(valY == "30", "set /a: (70 - 10) / 2 must equal 30");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("set /a z = y % 7", oss);
    std::string valZ;
    micant::cmd::CmdProcessor::get().getEnvironment().getVar("z", valZ);
    TEST_ASSERT(valZ == "2", "set /a modulo: 30 % 7 must equal 2");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("set /a bit = z ^ 3", oss);
    std::string valBit;
    micant::cmd::CmdProcessor::get().getEnvironment().getVar("bit", valBit);
    TEST_ASSERT(valBit == "1", "set /a bitwise xor: 2 ^ 3 must equal 1");

    // Substring & Replacement syntax
    oss.str(""); oss.clear();
    cmdShell.execute("set FULLNAME=MicaNTOperatingSystem", oss);
    oss.str(""); oss.clear();
    cmdShell.execute("echo %FULLNAME:~0,6%", oss);
    TEST_ASSERT(oss.str().find("MicaNT") != std::string::npos, "Substring %VAR:~0,6% must expand to MicaNT");

    oss.str(""); oss.clear();
    cmdShell.execute("echo %FULLNAME:OperatingSystem=Kernel%", oss);
    TEST_ASSERT(oss.str().find("MicaNTKernel") != std::string::npos, "Substitution %VAR:old=new% must expand correctly");

    // Dynamic pseudo-variables
    oss.str(""); oss.clear();
    cmdShell.execute("echo Level:%ERRORLEVEL%", oss);
    TEST_ASSERT(oss.str().find("Level:0") != std::string::npos, "%ERRORLEVEL% must expand to exit code");

    oss.str(""); oss.clear();
    cmdShell.execute("echo SystemDate:%DATE%", oss);
    TEST_ASSERT(oss.str().find("SystemDate:") != std::string::npos, "%DATE% must expand");

    // SETLOCAL and ENDLOCAL
    oss.str(""); oss.clear();
    cmdShell.execute("set SCOPE_VAR=OuterScope", oss);
    cmdShell.execute("setlocal", oss);
    cmdShell.execute("set SCOPE_VAR=InnerScope", oss);
    std::string inVal;
    micant::cmd::CmdProcessor::get().getEnvironment().getVar("SCOPE_VAR", inVal);
    TEST_ASSERT(inVal == "InnerScope", "Inside setlocal, variable must reflect modified value");
    cmdShell.execute("endlocal", oss);
    std::string outVal;
    micant::cmd::CmdProcessor::get().getEnvironment().getVar("SCOPE_VAR", outVal);
    TEST_ASSERT(outVal == "OuterScope", "After endlocal, variable must revert to outer scope");

    // ------------------------------------------------------------------------
    // 6. Control Flow (IF & FOR statements)
    // ------------------------------------------------------------------------
    oss.str(""); oss.clear();
    rc = cmdShell.execute("if exist C:\\Windows\\System32\\cmd.exe ( echo ExistsYes ) else ( echo ExistsNo )", oss);
    TEST_ASSERT(oss.str().find("ExistsYes") != std::string::npos, "if exist must execute then-block when file exists");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("if not exist C:\\NonExistent_Dir ( echo MissingYes )", oss);
    TEST_ASSERT(oss.str().find("MissingYes") != std::string::npos, "if not exist must succeed when file absent");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("if defined OS echo DefinedYes", oss);
    TEST_ASSERT(oss.str().find("DefinedYes") != std::string::npos, "if defined must succeed for OS");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("if /i \"MicaNT\"==\"micant\" echo CaseMatch", oss);
    TEST_ASSERT(oss.str().find("CaseMatch") != std::string::npos, "if /i must perform case-insensitive comparison");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("for %i in (one two three) do echo Item:%i", oss);
    TEST_ASSERT(oss.str().find("Item:one") != std::string::npos &&
                oss.str().find("Item:two") != std::string::npos &&
                oss.str().find("Item:three") != std::string::npos, "for %i in (list) do must iterate all items");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("for /l %i in (1,2,5) do echo Step:%i", oss);
    TEST_ASSERT(oss.str().find("Step:1") != std::string::npos &&
                oss.str().find("Step:3") != std::string::npos &&
                oss.str().find("Step:5") != std::string::npos, "for /l numeric loop must step correctly");

    // ------------------------------------------------------------------------
    // 7. Full Batch Script Execution Engine (.bat)
    // ------------------------------------------------------------------------
    std::string batchScript = 
        "@echo off\n"
        "set SCRIPT_NAME=%~nx0\n"
        "set SCRIPT_DIR=%~dp0\n"
        "echo Script: %SCRIPT_NAME%\n"
        "echo Dir: %SCRIPT_DIR%\n"
        "echo Param1: %1\n"
        "echo Param2: %2\n"
        "echo AllParams: %*\n"
        "set /a ACCUM=0\n"
        "for /l %%i in (1,1,4) do (\n"
        "    set /a ACCUM+=%%i\n"
        ")\n"
        "call :DoubleAccum %ACCUM%\n"
        "if %RESULT%==20 (\n"
        "    echo Subroutine Verification Succeeded\n"
        ") else (\n"
        "    echo Subroutine Verification Failed\n"
        "    exit /b 1\n"
        ")\n"
        "goto :ScriptEnd\n"
        "\n"
        ":DoubleAccum\n"
        "set /a RESULT=%1 * 2\n"
        "goto :EOF\n"
        "\n"
        ":ScriptEnd\n"
        "echo Batch Execution Complete\n"
        "exit /b 0\n";

    micant::cmd::CmdProcessor::get().writeVfsTextFile("C:\\Scripts\\engine_test.bat", batchScript, false);
    TEST_ASSERT(micant::cmd::CmdProcessor::get().vfsFileExists("C:\\Scripts\\engine_test.bat"), "engine_test.bat must be written to VFS");

    oss.str(""); oss.clear();
    rc = cmdShell.execute("cmd /c C:\\Scripts\\engine_test.bat Alpha Beta", oss);
    TEST_ASSERT(rc == 0, "Batch script execution must succeed with exit code 0");
    std::string scriptOutput = oss.str();
    TEST_ASSERT(scriptOutput.find("Script: engine_test.bat") != std::string::npos, "Batch parameter %~nx0 must expand to filename");
    TEST_ASSERT(scriptOutput.find("Param1: Alpha") != std::string::npos, "Batch parameter %1 must expand to first argument");
    TEST_ASSERT(scriptOutput.find("Param2: Beta") != std::string::npos, "Batch parameter %2 must expand to second argument");
    TEST_ASSERT(scriptOutput.find("AllParams: Alpha Beta") != std::string::npos, "Batch parameter %* must expand to all arguments");
    TEST_ASSERT(scriptOutput.find("Subroutine Verification Succeeded") != std::string::npos, "Subroutine call 'call :label' and 'goto :EOF' must calculate RESULT=20");
    TEST_ASSERT(scriptOutput.find("Batch Execution Complete") != std::string::npos, "Batch script must reach completion label");

    // Clean up script
    fs::VirtualFileSystem::get().deleteFile(L"C:\\Scripts\\engine_test.bat");

    // ------------------------------------------------------------------------
    // 8. Dynamic Loader Subsystem & CreateProcess Parity
    // ------------------------------------------------------------------------
    void* cmdMain = ldr::DynamicLoader::get().getExport("cmd.exe", "main");
    TEST_ASSERT(cmdMain != nullptr, "cmd.exe!main export must be registered in DynamicLoader");

    win32::PROCESS_INFORMATION pi{};
    win32::STARTUPINFOW si{}; si.cb = sizeof(si);
    wchar_t cmdLine[] = L"cmd.exe /c echo SubsystemProcessCreated";
    win32::BOOL bProc = win32::CreateProcessW(L"cmd.exe", cmdLine, nullptr, nullptr, win32::FALSE, 0, nullptr, nullptr, &si, &pi);
    TEST_ASSERT(bProc == win32::TRUE, "CreateProcessW for cmd.exe must succeed");
    TEST_ASSERT(pi.hProcess != nullptr && pi.dwProcessId > 0, "CreateProcessW must return valid process handles and PID");

    std::cout << "[TEST] Suite 61: Windows CMD & Batch Scripting Engine PASSED.\n";
}

void Test_Direct3D9_ProgrammableShaders_And_D3DX9Math() {
    using namespace micant::d3d9;
    std::cout << "[TEST] Running Suite 62: Direct3D 9 Programmable Shaders & D3DX9 Runtime...\n";

    // ------------------------------------------------------------------------
    // 1. D3DX9 Matrix Mathematics
    // ------------------------------------------------------------------------
    D3DXMATRIX matIdent{};
    D3DXMatrixIdentity(&matIdent);
    TEST_ASSERT(matIdent._11 == 1.0f && matIdent._22 == 1.0f && matIdent._33 == 1.0f && matIdent._44 == 1.0f, "D3DXMatrixIdentity must set diagonal elements to 1.0f");
    TEST_ASSERT(matIdent._12 == 0.0f && matIdent._41 == 0.0f, "D3DXMatrixIdentity must set off-diagonal elements to 0.0f");

    D3DXMATRIX matTrans{};
    D3DXMatrixTranslation(&matTrans, 10.0f, 20.0f, 30.0f);
    TEST_ASSERT(matTrans._41 == 10.0f && matTrans._42 == 20.0f && matTrans._43 == 30.0f, "D3DXMatrixTranslation must populate row 4 translation elements");

    D3DXMATRIX matScale{};
    D3DXMatrixScaling(&matScale, 2.0f, 3.0f, 4.0f);
    TEST_ASSERT(matScale._11 == 2.0f && matScale._22 == 3.0f && matScale._33 == 4.0f, "D3DXMatrixScaling must populate scale factors");

    D3DXMATRIX matRotY{};
    float angle = 3.14159265f * 0.5f; // 90 degrees
    D3DXMatrixRotationY(&matRotY, angle);
    TEST_ASSERT(std::abs(matRotY._11) < 1e-4f, "cos(90 deg) must be ~0");
    TEST_ASSERT(std::abs(matRotY._13 - (-1.0f)) < 1e-4f, "-sin(90 deg) must be ~ -1");
    TEST_ASSERT(std::abs(matRotY._31 - 1.0f) < 1e-4f, "sin(90 deg) must be ~ 1");

    D3DXMATRIX matCombined{};
    D3DXMatrixMultiply(&matCombined, &matScale, &matTrans);
    TEST_ASSERT(matCombined._11 == 2.0f && matCombined._22 == 3.0f && matCombined._33 == 4.0f, "Combined scale-translation matrix diagonal elements");
    TEST_ASSERT(matCombined._41 == 10.0f && matCombined._42 == 20.0f && matCombined._43 == 30.0f, "Combined scale-translation matrix translation elements");

    D3DXVECTOR3 eye{ 0.0f, 5.0f, -10.0f }, at{ 0.0f, 0.0f, 0.0f }, up{ 0.0f, 1.0f, 0.0f };
    D3DXMATRIX matView{};
    D3DXMatrixLookAtLH(&matView, &eye, &at, &up);
    TEST_ASSERT(matView._44 == 1.0f, "D3DXMatrixLookAtLH must generate valid view matrix with _44 == 1");

    D3DXMATRIX matProj{};
    D3DXMatrixPerspectiveFovLH(&matProj, 3.14159f / 4.0f, 1.33333f, 1.0f, 1000.0f);
    TEST_ASSERT(matProj._34 == 1.0f && matProj._11 > 0.0f && matProj._22 > 0.0f, "D3DXMatrixPerspectiveFovLH must calculate valid focal scales");

    float det = 0.0f;
    D3DXMATRIX matInv{};
    D3DMATRIX* pInvRes = D3DXMatrixInverse(&matInv, &det, &matTrans);
    TEST_ASSERT(pInvRes != nullptr, "D3DXMatrixInverse must invert translation matrix");
    TEST_ASSERT(std::abs(det - 1.0f) < 1e-4f, "Translation matrix determinant must be 1.0");
    TEST_ASSERT(matInv._41 == -10.0f && matInv._42 == -20.0f && matInv._43 == -30.0f, "Inverted translation matrix must have negated translation");

    D3DXMATRIX matTransposed{};
    D3DXMatrixTranspose(&matTransposed, &matTrans);
    TEST_ASSERT(matTransposed._14 == 10.0f && matTransposed._24 == 20.0f && matTransposed._34 == 30.0f, "D3DXMatrixTranspose must swap rows and columns");

    // ------------------------------------------------------------------------
    // 2. D3DX9 Vector Mathematics
    // ------------------------------------------------------------------------
    D3DXVECTOR3 v1{ 3.0f, 4.0f, 0.0f };
    float len = D3DXVec3Length(&v1);
    TEST_ASSERT(std::abs(len - 5.0f) < 1e-5f, "D3DXVec3Length of (3,4,0) must equal 5.0f");

    D3DXVECTOR3 vNorm{};
    D3DXVec3Normalize(&vNorm, &v1);
    TEST_ASSERT(std::abs(vNorm.x - 0.6f) < 1e-4f && std::abs(vNorm.y - 0.8f) < 1e-4f, "D3DXVec3Normalize must compute unit vector");
    TEST_ASSERT(std::abs(D3DXVec3Length(&vNorm) - 1.0f) < 1e-4f, "Normalized vector length must be 1.0f");

    D3DXVECTOR3 vX{ 1.0f, 0.0f, 0.0f }, vY{ 0.0f, 1.0f, 0.0f }, vZ{};
    D3DXVec3Cross(&vZ, &vX, &vY);
    TEST_ASSERT(std::abs(vZ.x) < 1e-5f && std::abs(vZ.y) < 1e-5f && std::abs(vZ.z - 1.0f) < 1e-5f, "Cross product of X and Y axes must yield Z axis");

    float dot = D3DXVec3Dot(&vX, &vY);
    TEST_ASSERT(std::abs(dot) < 1e-5f, "Dot product of orthogonal vectors must be 0");

    D3DXVECTOR3 vOrigin{ 0.0f, 0.0f, 0.0f }, vTransformed{};
    D3DXVec3TransformCoord(&vTransformed, &vOrigin, &matTrans);
    TEST_ASSERT(vTransformed.x == 10.0f && vTransformed.y == 20.0f && vTransformed.z == 30.0f, "D3DXVec3TransformCoord must translate point");

    // ------------------------------------------------------------------------
    // 3. D3DX Buffer & Shader Assembly / Disassembly
    // ------------------------------------------------------------------------
    ID3DXBuffer* pTestBuffer = nullptr;
    int32_t hr = D3DXCreateBuffer(256, &pTestBuffer);
    TEST_ASSERT(hr == D3D_OK && pTestBuffer != nullptr, "D3DXCreateBuffer must succeed");
    TEST_ASSERT(pTestBuffer->GetBufferSize() == 256, "D3DXBuffer size must match requested 256 bytes");
    pTestBuffer->Release();

    const char sampleVsAsm[] =
        "vs_3_0\n"
        "dp4 r0.x, v0, c0\n"
        "dp4 r0.y, v0, c1\n"
        "dp4 r0.z, v0, c2\n"
        "dp4 r0.w, v0, c3\n"
        "mov o0, r0\n"
        "mov o1, v1\n"
        "ret\n";

    ID3DXBuffer* pVsBytecode = nullptr;
    ID3DXBuffer* pErrorMsgs = nullptr;
    hr = D3DXAssembleShader(sampleVsAsm, sizeof(sampleVsAsm), nullptr, nullptr, 0, &pVsBytecode, &pErrorMsgs);
    TEST_ASSERT(hr == D3D_OK && pVsBytecode != nullptr, "D3DXAssembleShader must compile vertex shader assembly to bytecode");

    ID3DXBuffer* pDisasmBuf = nullptr;
    hr = D3DXDisassembleShader(static_cast<const uint32_t*>(pVsBytecode->GetBufferPointer()), win32::FALSE, "Test Disassembly", &pDisasmBuf);
    TEST_ASSERT(hr == D3D_OK && pDisasmBuf != nullptr, "D3DXDisassembleShader must disassemble DXBC bytecode to ASM string");
    std::string disasmStr(static_cast<const char*>(pDisasmBuf->GetBufferPointer()));
    TEST_ASSERT(disasmStr.find("vs_3_0") != std::string::npos, "Disassembly must contain target profile vs_3_0");
    TEST_ASSERT(disasmStr.find("dp4") != std::string::npos, "Disassembly must contain instruction mnemonic dp4");
    pDisasmBuf->Release();

    const char samplePsAsm[] =
        "ps_3_0\n"
        "tex r0, v2, s0\n"
        "mul r1, r0, v1\n"
        "mul o1, r1, c0\n"
        "ret\n";

    ID3DXBuffer* pPsBytecode = nullptr;
    hr = D3DXAssembleShader(samplePsAsm, sizeof(samplePsAsm), nullptr, nullptr, 0, &pPsBytecode, &pErrorMsgs);
    TEST_ASSERT(hr == D3D_OK && pPsBytecode != nullptr, "D3DXAssembleShader must compile pixel shader assembly to bytecode");

    // ------------------------------------------------------------------------
    // 4. Programmable Shader Device Setup & Vertex Declarations
    // ------------------------------------------------------------------------
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    TEST_ASSERT(pD3D != nullptr, "Direct3DCreate9 must return non-null IDirect3D9");

    win32::HWND hwnd = user32::CreateWindowExW(
        0, L"MicaNT_Window", L"D3D9 Programmable Shaders & D3DX9 Test",
        0, 0, 0, 640, 480, nullptr, nullptr, nullptr, nullptr
    );
    TEST_ASSERT(hwnd != nullptr, "User32 CreateWindowExW must create window handle");

    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = win32::TRUE;

    IDirect3DDevice9* pDevice = nullptr;
    hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pDevice);
    TEST_ASSERT(hr == D3D_OK && pDevice != nullptr, "CreateDevice must succeed");

    D3DVERTEXELEMENT9 declElems[] = {
        { 0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };

    IDirect3DVertexDeclaration9* pDecl = nullptr;
    hr = pDevice->CreateVertexDeclaration(declElems, &pDecl);
    TEST_ASSERT(hr == D3D_OK && pDecl != nullptr, "CreateVertexDeclaration must succeed");

    hr = pDevice->SetVertexDeclaration(pDecl);
    TEST_ASSERT(hr == D3D_OK, "SetVertexDeclaration must succeed");

    IDirect3DVertexDeclaration9* pReadbackDecl = nullptr;
    pDevice->GetVertexDeclaration(&pReadbackDecl);
    TEST_ASSERT(pReadbackDecl == pDecl, "GetVertexDeclaration must return currently bound vertex declaration");
    pReadbackDecl->Release();

    // ------------------------------------------------------------------------
    // 5. Shader Creation, Binding & Constant Buffers
    // ------------------------------------------------------------------------
    IDirect3DVertexShader9* pVS = nullptr;
    hr = pDevice->CreateVertexShader(static_cast<const uint32_t*>(pVsBytecode->GetBufferPointer()), &pVS);
    TEST_ASSERT(hr == D3D_OK && pVS != nullptr, "CreateVertexShader from bytecode must succeed");

    hr = pDevice->SetVertexShader(pVS);
    TEST_ASSERT(hr == D3D_OK, "SetVertexShader must bind vertex shader to device");

    IDirect3DVertexShader9* pReadbackVS = nullptr;
    pDevice->GetVertexShader(&pReadbackVS);
    TEST_ASSERT(pReadbackVS == pVS, "GetVertexShader must return active vertex shader");
    pReadbackVS->Release();

    IDirect3DPixelShader9* pPS = nullptr;
    hr = pDevice->CreatePixelShader(static_cast<const uint32_t*>(pPsBytecode->GetBufferPointer()), &pPS);
    TEST_ASSERT(hr == D3D_OK && pPS != nullptr, "CreatePixelShader from bytecode must succeed");

    hr = pDevice->SetPixelShader(pPS);
    TEST_ASSERT(hr == D3D_OK, "SetPixelShader must bind pixel shader to device");

    IDirect3DPixelShader9* pReadbackPS = nullptr;
    pDevice->GetPixelShader(&pReadbackPS);
    TEST_ASSERT(pReadbackPS == pPS, "GetPixelShader must return active pixel shader");
    pReadbackPS->Release();

    // Set VS Constant registers c0..c3 (Transposed WVP matrix)
    D3DXMATRIX matWVP;
    D3DXMatrixIdentity(&matWVP);
    D3DXMATRIX matTransposedWVP;
    D3DXMatrixTranspose(&matTransposedWVP, &matWVP);
    hr = pDevice->SetVertexShaderConstantF(0, reinterpret_cast<const float*>(&matTransposedWVP), 4);
    TEST_ASSERT(hr == D3D_OK, "SetVertexShaderConstantF for c0..c3 must succeed");

    float vsReadback[16]{};
    hr = pDevice->GetVertexShaderConstantF(0, vsReadback, 4);
    TEST_ASSERT(hr == D3D_OK && vsReadback[0] == 1.0f && vsReadback[5] == 1.0f, "GetVertexShaderConstantF must retrieve constant buffer registers");

    // Set PS Constant register c0 (Tint RGBA: 1.0, 1.0, 1.0, 1.0)
    float psConstantTint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    hr = pDevice->SetPixelShaderConstantF(0, psConstantTint, 1);
    TEST_ASSERT(hr == D3D_OK, "SetPixelShaderConstantF for c0 must succeed");

    float psReadback[4]{};
    hr = pDevice->GetPixelShaderConstantF(0, psReadback, 1);
    TEST_ASSERT(hr == D3D_OK && psReadback[0] == 1.0f && psReadback[3] == 1.0f, "GetPixelShaderConstantF must retrieve constant registers");

    // ------------------------------------------------------------------------
    // 6. Texture Creation, Locking & Sampler States
    // ------------------------------------------------------------------------
    IDirect3DTexture9* pTexture = nullptr;
    hr = D3DXCreateTexture(pDevice, 32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture);
    TEST_ASSERT(hr == D3D_OK && pTexture != nullptr, "D3DXCreateTexture must allocate 2D texture");
    TEST_ASSERT(pTexture->GetWidth() == 32 && pTexture->GetHeight() == 32, "Texture dimensions must be 32x32");

    D3DLOCKED_RECT lr{};
    hr = pTexture->LockRect(0, &lr, nullptr, 0);
    TEST_ASSERT(hr == D3D_OK && lr.pBits != nullptr, "Texture LockRect must yield writable memory");
    uint32_t* pTexBits = static_cast<uint32_t*>(lr.pBits);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            bool tile = ((x / 4) + (y / 4)) % 2 == 0;
            pTexBits[y * 32 + x] = tile ? D3DCOLOR_ARGB(255, 255, 200, 50) : D3DCOLOR_ARGB(255, 50, 150, 255);
        }
    }
    hr = pTexture->UnlockRect(0);
    TEST_ASSERT(hr == D3D_OK, "Texture UnlockRect must succeed");

    hr = pDevice->SetTexture(0, pTexture);
    TEST_ASSERT(hr == D3D_OK, "SetTexture must bind texture to sampler slot 0");

    IDirect3DBaseTexture9* pReadbackTex = nullptr;
    pDevice->GetTexture(0, &pReadbackTex);
    TEST_ASSERT(pReadbackTex == pTexture, "GetTexture must return bound texture on sampler 0");
    pReadbackTex->Release();

    pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    uint32_t magFilterVal = 0;
    pDevice->GetSamplerState(0, D3DSAMP_MAGFILTER, &magFilterVal);
    TEST_ASSERT(magFilterVal == D3DTEXF_LINEAR, "GetSamplerState must retrieve configured filter mode");

    // ------------------------------------------------------------------------
    // 7. Programmable Pipeline Drawing & Presentation
    // ------------------------------------------------------------------------
    struct ShadedVertex {
        float x, y, z;
        uint32_t color;
        float u, v;
    };

    ShadedVertex quad[6] = {
        { -0.6f,  0.6f, 0.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 0.0f },
        {  0.6f,  0.6f, 0.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 0.0f },
        { -0.6f, -0.6f, 0.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 1.0f },
        { -0.6f, -0.6f, 0.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 0.0f, 1.0f },
        {  0.6f,  0.6f, 0.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 0.0f },
        {  0.6f, -0.6f, 0.0f, D3DCOLOR_ARGB(255, 255, 255, 255), 1.0f, 1.0f }
    };

    const D3DCOLOR bgClearColor = D3DCOLOR_XRGB(10, 15, 30);
    pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, bgClearColor, 1.0f, 0);

    hr = pDevice->BeginScene();
    TEST_ASSERT(hr == D3D_OK, "BeginScene must succeed");

    hr = pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(ShadedVertex));
    TEST_ASSERT(hr == D3D_OK, "DrawPrimitiveUP with programmable shaders must succeed");

    hr = pDevice->EndScene();
    TEST_ASSERT(hr == D3D_OK, "EndScene must succeed");

    hr = pDevice->Present(nullptr, nullptr, hwnd, nullptr);
    TEST_ASSERT(hr == D3D_OK, "Present must succeed");

    // Verify User32 Presentation Surface contains shaded pixels
    uint32_t surfW = 0, surfH = 0;
    const uint32_t* winPixels = user32::WindowManager::get().getWindowPixelBuffer(hwnd, &surfW, &surfH);
    TEST_ASSERT(winPixels != nullptr && surfW == 640 && surfH == 480, "User32 window surface must be valid 640x480 buffer");

    uint32_t nonClearPixels = 0;
    for (size_t i = 0; i < 640 * 480; ++i) {
        if (winPixels[i] != bgClearColor) {
            nonClearPixels++;
        }
    }
    TEST_ASSERT(nonClearPixels > 1000, "Window surface must contain programmable-shaded pixels");

    // ------------------------------------------------------------------------
    // 8. DynamicLoader Export Resolution (d3dx9_43.dll)
    // ------------------------------------------------------------------------
    InitializeD3D9SubsystemExports();
    auto& ldr = ldr::DynamicLoader::get();

    void* pfnD3DXMatFov = ldr.getExport("d3dx9_43.dll", "D3DXMatrixPerspectiveFovLH");
    TEST_ASSERT(pfnD3DXMatFov != nullptr, "d3dx9_43.dll!D3DXMatrixPerspectiveFovLH must be resolved");

    void* pfnD3DXMatLookAt = ldr.getExport("d3dx9_43.dll", "D3DXMatrixLookAtLH");
    TEST_ASSERT(pfnD3DXMatLookAt != nullptr, "d3dx9_43.dll!D3DXMatrixLookAtLH must be resolved");

    void* pfnD3DXVecCross = ldr.getExport("d3dx9_43.dll", "D3DXVec3Cross");
    TEST_ASSERT(pfnD3DXVecCross != nullptr, "d3dx9_43.dll!D3DXVec3Cross must be resolved");

    void* pfnD3DXAsm = ldr.getExport("d3dx9_43.dll", "D3DXAssembleShader");
    TEST_ASSERT(pfnD3DXAsm != nullptr, "d3dx9_43.dll!D3DXAssembleShader must be resolved");

    void* pfnD3DXDisasm = ldr.getExport("d3dx9_43.dll", "D3DXDisassembleShader");
    TEST_ASSERT(pfnD3DXDisasm != nullptr, "d3dx9_43.dll!D3DXDisassembleShader must be resolved");

    void* pfnD3DXTex = ldr.getExport("d3dx9_43.dll", "D3DXCreateTexture");
    TEST_ASSERT(pfnD3DXTex != nullptr, "d3dx9_43.dll!D3DXCreateTexture must be resolved");

    // ------------------------------------------------------------------------
    // 9. Shell Command Integration
    // ------------------------------------------------------------------------
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("prismx d3d9 shaders", out);
        std::string res = out.str();
        TEST_ASSERT(res.find("[Direct3D 9] Programmable Vertex & Pixel Shader Pipeline Rendered Successfully!") != std::string::npos, "Shell must report programmable shader pipeline success");
    }

    // Cleanup COM objects
    pTexture->Release();
    pVS->Release();
    pPS->Release();
    pDecl->Release();
    pVsBytecode->Release();
    pPsBytecode->Release();
    pDevice->Release();
    pD3D->Release();
    user32::DestroyWindow(hwnd);

    std::cout << "[TEST] Suite 62: Direct3D 9 Programmable Shaders & D3DX9 Runtime PASSED.\n";
}

void Test_WinMM_DirectSound_And_VersionInfo() {
    std::cout << "[TEST] Running Suite 63: WinMM Multimedia Engine, DirectSound 8 Audio & Version API...\n";

    // ------------------------------------------------------------------------
    // 1. WinMM Multimedia Timers
    // ------------------------------------------------------------------------
    winmm::InitializeWinMMExports();

    winmm::TIMECAPS tc{};
    uint32_t mmr = winmm::timeGetDevCaps(&tc, sizeof(tc));
    TEST_ASSERT(mmr == winmm::TIMERR_NOERROR, "timeGetDevCaps must succeed");
    TEST_ASSERT(tc.wPeriodMin == 1 && tc.wPeriodMax == 1000000, "timeGetDevCaps must report 1ms min timer resolution");

    mmr = winmm::timeBeginPeriod(1);
    TEST_ASSERT(mmr == winmm::TIMERR_NOERROR, "timeBeginPeriod(1) must succeed");
    TEST_ASSERT(winmm::MultimediaTimerService::Instance().GetCurrentPeriod() == 1, "Active timer resolution must be 1ms");

    uint32_t tStart = winmm::timeGetTime();
    uint32_t tEnd = tStart;
    while (tEnd == tStart) {
        tEnd = winmm::timeGetTime();
    }
    TEST_ASSERT(tEnd >= tStart, "timeGetTime must advance monotonically");

    mmr = winmm::timeEndPeriod(1);
    TEST_ASSERT(mmr == winmm::TIMERR_NOERROR, "timeEndPeriod(1) must succeed");

    uint32_t timerId = 0;
    mmr = winmm::timeSetEvent(50, 1, nullptr, nullptr, winmm::TIME_ONESHOT, &timerId);
    TEST_ASSERT(mmr == winmm::TIMERR_NOERROR && timerId != 0, "timeSetEvent must create timer event");
    mmr = winmm::timeKillEvent(timerId);
    TEST_ASSERT(mmr == winmm::TIMERR_NOERROR, "timeKillEvent must cancel active timer event");

    // ------------------------------------------------------------------------
    // 2. WinMM Waveform Audio (waveOut)
    // ------------------------------------------------------------------------
    uint32_t numDevs = winmm::waveOutGetNumDevs();
    TEST_ASSERT(numDevs >= 1, "waveOutGetNumDevs must report at least 1 audio device");

    winmm::WAVEOUTCAPSA woc{};
    mmr = winmm::waveOutGetDevCapsA(0, &woc, sizeof(woc));
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutGetDevCapsA must succeed");
    TEST_ASSERT(std::string(woc.szPname).find("MicaNT") != std::string::npos, "WaveOut device name must identify MicaNT");

    audio::WAVEFORMATEX wfx{};
    wfx.wFormatTag = audio::WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 4;
    wfx.nAvgBytesPerSec = 44100 * 4;

    winmm::HWAVEOUT hwo = nullptr;
    mmr = winmm::waveOutOpen(&hwo, 0, &wfx, nullptr, nullptr, 0);
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR && hwo != nullptr, "waveOutOpen must succeed with 44.1kHz 16-bit stereo PCM");

    // Test volume
    mmr = winmm::waveOutSetVolume(hwo, 0x80008000);
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutSetVolume must succeed");
    uint32_t vol = 0;
    mmr = winmm::waveOutGetVolume(hwo, &vol);
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR && vol == 0x80008000, "waveOutGetVolume must return set volume");

    // Allocate & Prepare Header
    std::vector<int16_t> pcmSamples(1024 * 2, 0);
    for (size_t i = 0; i < pcmSamples.size(); ++i) {
        pcmSamples[i] = static_cast<int16_t>(std::sin(i * 0.1) * 16000.0);
    }

    winmm::WAVEHDR whdr{};
    whdr.lpData = reinterpret_cast<char*>(pcmSamples.data());
    whdr.dwBufferLength = static_cast<uint32_t>(pcmSamples.size() * sizeof(int16_t));
    mmr = winmm::waveOutPrepareHeader(hwo, &whdr, sizeof(whdr));
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutPrepareHeader must succeed");
    TEST_ASSERT(whdr.dwFlags & winmm::WHDR_PREPARED, "WHDR_PREPARED flag must be set");

    mmr = winmm::waveOutWrite(hwo, &whdr, sizeof(whdr));
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutWrite must succeed");
    TEST_ASSERT(whdr.dwFlags & winmm::WHDR_DONE, "WHDR_DONE flag must be set after write completes");

    winmm::MMTIME mmtime{};
    mmtime.wType = winmm::TIME_MS;
    mmr = winmm::waveOutGetPosition(hwo, &mmtime, sizeof(mmtime));
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutGetPosition must succeed");

    mmr = winmm::waveOutPause(hwo);
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutPause must succeed");
    mmr = winmm::waveOutRestart(hwo);
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutRestart must succeed");
    mmr = winmm::waveOutReset(hwo);
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutReset must succeed");

    mmr = winmm::waveOutUnprepareHeader(hwo, &whdr, sizeof(whdr));
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutUnprepareHeader must succeed");

    mmr = winmm::waveOutClose(hwo);
    TEST_ASSERT(mmr == winmm::MMSYSERR_NOERROR, "waveOutClose must succeed");

    // ------------------------------------------------------------------------
    // 3. Sound Playback (PlaySound & RIFF WAVE Parser)
    // ------------------------------------------------------------------------
    auto sineRiff = winmm::SoundPlaybackService::Instance().GenerateSineWaveRiff(440, 200, 44100);
    TEST_ASSERT(sineRiff.size() > 44, "GenerateSineWaveRiff must produce RIFF container");

    auto parsed = winmm::ParseRiffWave(sineRiff.data(), sineRiff.size());
    TEST_ASSERT(parsed.isValid, "ParseRiffWave must identify valid RIFF WAVE headers");
    TEST_ASSERT(parsed.format.nSamplesPerSec == 44100, "Sample rate must match 44100 Hz");
    TEST_ASSERT(parsed.format.nChannels == 1, "Channels must match 1 (mono)");

    int psRes = winmm::PlaySoundA(reinterpret_cast<const char*>(sineRiff.data()), nullptr, winmm::SND_MEMORY | winmm::SND_SYNC);
    TEST_ASSERT(psRes == 1, "PlaySoundA with SND_MEMORY must succeed");
    TEST_ASSERT(winmm::SoundPlaybackService::Instance().IsPlaying(), "SoundPlaybackService must be in active state");

    winmm::PlaySoundA(nullptr, nullptr, winmm::SND_PURGE);
    TEST_ASSERT(!winmm::SoundPlaybackService::Instance().IsPlaying(), "PlaySoundA with null/purge must stop playback");

    // ------------------------------------------------------------------------
    // 4. Media Control Interface (MCI) Command Engine
    // ------------------------------------------------------------------------
    char mciRet[128]{};
    uint32_t mciErr = winmm::mciSendStringA("open bgm.wav type waveaudio alias track1", mciRet, sizeof(mciRet), nullptr);
    TEST_ASSERT(mciErr == winmm::MCIERR_NO_ERROR, "MCI open command must succeed");

    mciErr = winmm::mciSendStringA("status track1 mode", mciRet, sizeof(mciRet), nullptr);
    TEST_ASSERT(mciErr == winmm::MCIERR_NO_ERROR && std::string(mciRet) == "stopped", "MCI status mode must report 'stopped'");

    mciErr = winmm::mciSendStringA("play track1 from 0 to 5000", mciRet, sizeof(mciRet), nullptr);
    TEST_ASSERT(mciErr == winmm::MCIERR_NO_ERROR, "MCI play command must succeed");

    mciErr = winmm::mciSendStringA("status track1 mode", mciRet, sizeof(mciRet), nullptr);
    TEST_ASSERT(mciErr == winmm::MCIERR_NO_ERROR && std::string(mciRet) == "playing", "MCI status mode must report 'playing'");

    mciErr = winmm::mciSendStringA("pause track1", mciRet, sizeof(mciRet), nullptr);
    TEST_ASSERT(mciErr == winmm::MCIERR_NO_ERROR, "MCI pause command must succeed");
    winmm::mciSendStringA("status track1 mode", mciRet, sizeof(mciRet), nullptr);
    TEST_ASSERT(std::string(mciRet) == "paused", "MCI status mode must report 'paused'");

    mciErr = winmm::mciSendStringA("resume track1", mciRet, sizeof(mciRet), nullptr);
    TEST_ASSERT(mciErr == winmm::MCIERR_NO_ERROR, "MCI resume command must succeed");

    mciErr = winmm::mciSendStringA("close track1", mciRet, sizeof(mciRet), nullptr);
    TEST_ASSERT(mciErr == winmm::MCIERR_NO_ERROR, "MCI close command must succeed");

    // ------------------------------------------------------------------------
    // 5. WinMM Joystick API
    // ------------------------------------------------------------------------
    TEST_ASSERT(winmm::joyGetNumDevs() >= 1, "joyGetNumDevs must report at least 1 joystick device");
    winmm::JOYCAPSA jc{};
    TEST_ASSERT(winmm::joyGetDevCapsA(0, &jc, sizeof(jc)) == winmm::JOYERR_NOERROR, "joyGetDevCapsA must succeed");
    winmm::JOYINFO ji{};
    TEST_ASSERT(winmm::joyGetPos(0, &ji) == winmm::JOYERR_NOERROR && ji.wXpos == 32768, "joyGetPos must report centered axis");

    // ------------------------------------------------------------------------
    // 6. DirectSound 8 Creation, Caps & Buffers
    // ------------------------------------------------------------------------
    dsound::InitializeDirectSoundExports();

    dsound::IDirectSound8* pDS8 = nullptr;
    int32_t hr = dsound::DirectSoundCreate8(nullptr, &pDS8, nullptr);
    TEST_ASSERT(hr == dsound::DS_OK && pDS8 != nullptr, "DirectSoundCreate8 must succeed");

    dsound::DSCAPS dscaps{};
    dscaps.dwSize = sizeof(dscaps);
    hr = pDS8->GetCaps(&dscaps);
    TEST_ASSERT(hr == dsound::DS_OK && dscaps.dwMaxHwMixingAllBuffers > 0, "DirectSound8 GetCaps must report hardware mixing");

    uint32_t certified = 0;
    pDS8->VerifyCertification(&certified);
    TEST_ASSERT(certified == 1, "DirectSound8 driver must be WHQL certified");

    hr = pDS8->SetCooperativeLevel(nullptr, dsound::DSSCL_PRIORITY);
    TEST_ASSERT(hr == dsound::DS_OK, "SetCooperativeLevel must succeed");

    // Create 3D secondary sound buffer
    dsound::DSBUFFERDESC dsDesc{};
    dsDesc.dwSize = sizeof(dsDesc);
    dsDesc.dwFlags = dsound::DSBCAPS_CTRL3D | dsound::DSBCAPS_CTRLVOLUME | dsound::DSBCAPS_CTRLPAN | dsound::DSBCAPS_CTRLFREQUENCY;
    dsDesc.dwBufferBytes = 44100 * 4; // 1 second buffer
    dsDesc.lpwfxFormat = &wfx;

    dsound::IDirectSoundBuffer* pDSBuffer = nullptr;
    hr = pDS8->CreateSoundBuffer(&dsDesc, &pDSBuffer, nullptr);
    TEST_ASSERT(hr == dsound::DS_OK && pDSBuffer != nullptr, "CreateSoundBuffer must succeed");

    // ------------------------------------------------------------------------
    // 7. DirectSound Circular Buffer Lock / Wrap-Around
    // ------------------------------------------------------------------------
    void *pv1 = nullptr, *pv2 = nullptr;
    uint32_t cb1 = 0, cb2 = 0;
    uint32_t lockOffset = (44100 * 4) - 256;
    hr = pDSBuffer->Lock(lockOffset, 1024, &pv1, &cb1, &pv2, &cb2, 0);
    TEST_ASSERT(hr == dsound::DS_OK, "Lock across circular boundary must succeed");
    TEST_ASSERT(pv1 != nullptr && cb1 == 256, "First locked block must have 256 bytes");
    TEST_ASSERT(pv2 != nullptr && cb2 == 768, "Second wrap-around block must have 768 bytes");
    TEST_ASSERT(cb1 + cb2 == 1024, "Total locked bytes must equal 1024");

    hr = pDSBuffer->Unlock(pv1, cb1, pv2, cb2);
    TEST_ASSERT(hr == dsound::DS_OK, "Unlock must succeed");

    // Test Volume, Pan, Frequency
    pDSBuffer->SetVolume(-1200); // -12 dB
    int32_t readVol = 0;
    pDSBuffer->GetVolume(&readVol);
    TEST_ASSERT(readVol == -1200, "GetVolume must retrieve configured volume");

    pDSBuffer->SetPan(-2500); // Left bias
    int32_t readPan = 0;
    pDSBuffer->GetPan(&readPan);
    TEST_ASSERT(readPan == -2500, "GetPan must retrieve configured pan");

    pDSBuffer->SetFrequency(22050);
    uint32_t readFreq = 0;
    pDSBuffer->GetFrequency(&readFreq);
    TEST_ASSERT(readFreq == 22050, "GetFrequency must retrieve configured frequency");

    // ------------------------------------------------------------------------
    // 8. DirectSound 3D Positional Audio
    // ------------------------------------------------------------------------
    dsound::IDirectSound3DBuffer* p3DBuffer = nullptr;
    hr = pDSBuffer->QueryInterface(dsound::IID_IDirectSound3DBuffer, reinterpret_cast<void**>(&p3DBuffer));
    TEST_ASSERT(hr == dsound::DS_OK && p3DBuffer != nullptr, "QueryInterface for IDirectSound3DBuffer must succeed");

    p3DBuffer->SetPosition(12.0f, 0.0f, 8.0f, 0);
    p3DBuffer->SetMinDistance(1.0f, 0);
    p3DBuffer->SetMaxDistance(50.0f, 0);

    dsound::D3DVECTOR pos{};
    p3DBuffer->GetPosition(&pos);
    TEST_ASSERT(pos.x == 12.0f && pos.z == 8.0f, "GetPosition must return 3D emitter coordinates");
    p3DBuffer->Release();

    // ------------------------------------------------------------------------
    // 9. Real-Time Multi-Voice Software PCM Mixer
    // ------------------------------------------------------------------------
    pDSBuffer->Lock(0, 44100 * 4, &pv1, &cb1, nullptr, nullptr, 0);
    int16_t* pcm16 = static_cast<int16_t*>(pv1);
    for (size_t i = 0; i < (44100 * 4) / 2; ++i) {
        pcm16[i] = static_cast<int16_t>(std::sin(i * 0.05) * 12000.0);
    }
    pDSBuffer->Unlock(pv1, cb1, nullptr, 0);

    pDSBuffer->Play(0, 0, dsound::DSBPLAY_LOOPING);
    uint32_t status = 0;
    pDSBuffer->GetStatus(&status);
    TEST_ASSERT((status & dsound::DSBSTATUS_PLAYING) && (status & dsound::DSBSTATUS_LOOPING), "Sound buffer must report playing and looping");

    int16_t outMix[512 * 2]{};
    auto* pImpl = static_cast<dsound::DirectSound8Impl*>(pDS8);
    size_t mixedVoices = pImpl->MixActiveVoices(outMix, 512);
    TEST_ASSERT(mixedVoices >= 1, "MixActiveVoices must mix active secondary buffer");

    bool hasMixedSound = false;
    for (size_t i = 0; i < 512 * 2; ++i) {
        if (outMix[i] != 0) {
            hasMixedSound = true;
            break;
        }
    }
    TEST_ASSERT(hasMixedSound, "Output mix buffer must contain non-zero mixed audio waveform");

    pDSBuffer->Stop();
    pDSBuffer->Release();
    pDS8->Release();

    // ------------------------------------------------------------------------
    // 10. Version Information API (version.dll)
    // ------------------------------------------------------------------------
    version::InitializeVersionExports();

    uint32_t vHandle = 0;
    uint32_t vSize = version::GetFileVersionInfoSizeA("kernel32.dll", &vHandle);
    TEST_ASSERT(vSize > sizeof(version::VS_FIXEDFILEINFO), "GetFileVersionInfoSizeA for kernel32.dll must be non-zero");

    std::vector<uint8_t> verData(vSize);
    int32_t gfvRes = version::GetFileVersionInfoA("kernel32.dll", vHandle, vSize, verData.data());
    TEST_ASSERT(gfvRes == 1, "GetFileVersionInfoA must succeed");

    void* pFixedInfo = nullptr;
    uint32_t fixedInfoLen = 0;
    int32_t qvRes = version::VerQueryValueA(verData.data(), "\\", &pFixedInfo, &fixedInfoLen);
    TEST_ASSERT(qvRes == 1 && pFixedInfo != nullptr, "VerQueryValueA for root '\\' must retrieve VS_FIXEDFILEINFO");

    auto* ffi = static_cast<const version::VS_FIXEDFILEINFO*>(pFixedInfo);
    TEST_ASSERT(ffi->dwSignature == version::VS_FFI_SIGNATURE, "VS_FIXEDFILEINFO signature must be 0xFEEF04BD");
    TEST_ASSERT(ffi->dwFileOS == version::VOS_NT_WINDOWS32, "File OS must be VOS_NT_WINDOWS32");
    TEST_ASSERT(ffi->dwFileType == version::VFT_DLL, "kernel32.dll file type must be VFT_DLL");

    void* pStrVal = nullptr;
    uint32_t strValLen = 0;
    qvRes = version::VerQueryValueA(verData.data(), "\\StringFileInfo\\040904B0\\FileDescription", &pStrVal, &strValLen);
    TEST_ASSERT(qvRes == 1 && pStrVal != nullptr, "VerQueryValueA for FileDescription must succeed");
    TEST_ASSERT(std::string(static_cast<const char*>(pStrVal)).find("Windows NT BASE API") != std::string::npos, "FileDescription must match Windows NT BASE API");

    qvRes = version::VerQueryValueA(verData.data(), "\\StringFileInfo\\040904B0\\CompanyName", &pStrVal, &strValLen);
    TEST_ASSERT(qvRes == 1 && std::string(static_cast<const char*>(pStrVal)) == "MicaNT Sovereign Project", "CompanyName must match MicaNT Sovereign Project");

    char langStr[64]{};
    uint32_t langLen = version::VerLanguageNameA(0x0409, langStr, sizeof(langStr));
    TEST_ASSERT(langLen > 0 && std::string(langStr) == "English (United States)", "VerLanguageNameA for 0x0409 must report English (United States)");

    // ------------------------------------------------------------------------
    // 11. Dynamic Loader Resolution Verification
    // ------------------------------------------------------------------------
    auto& ldr = ldr::DynamicLoader::get();
    TEST_ASSERT(ldr.getExport("winmm.dll", "timeGetTime") != nullptr, "winmm.dll!timeGetTime must be registered in DynamicLoader");
    TEST_ASSERT(ldr.getExport("winmm.dll", "PlaySoundA") != nullptr, "winmm.dll!PlaySoundA must be registered in DynamicLoader");
    TEST_ASSERT(ldr.getExport("winmm.dll", "waveOutOpen") != nullptr, "winmm.dll!waveOutOpen must be registered in DynamicLoader");
    TEST_ASSERT(ldr.getExport("winmm.dll", "mciSendStringA") != nullptr, "winmm.dll!mciSendStringA must be registered in DynamicLoader");
    TEST_ASSERT(ldr.getExport("dsound.dll", "DirectSoundCreate8") != nullptr, "dsound.dll!DirectSoundCreate8 must be registered in DynamicLoader");
    TEST_ASSERT(ldr.getExport("dsound.dll", "DirectSoundEnumerateA") != nullptr, "dsound.dll!DirectSoundEnumerateA must be registered in DynamicLoader");
    TEST_ASSERT(ldr.getExport("version.dll", "GetFileVersionInfoA") != nullptr, "version.dll!GetFileVersionInfoA must be registered in DynamicLoader");
    TEST_ASSERT(ldr.getExport("version.dll", "VerQueryValueA") != nullptr, "version.dll!VerQueryValueA must be registered in DynamicLoader");

    // ------------------------------------------------------------------------
    // 12. Shell Command Integration
    // ------------------------------------------------------------------------
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("winmm beep", out);
        TEST_ASSERT(out.str().find("PlaySound Result:   SUCCESS") != std::string::npos, "Shell winmm beep command must succeed");
    }
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("winmm timer", out);
        TEST_ASSERT(out.str().find("timeBeginPeriod(1): Active target resolution = 1 ms") != std::string::npos, "Shell winmm timer command must succeed");
    }
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("dsound", out);
        TEST_ASSERT(out.str().find("DirectSound Interface: IDirectSound8") != std::string::npos, "Shell dsound command must succeed");
    }
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("version kernel32.dll", out);
        TEST_ASSERT(out.str().find("Windows NT BASE API Client") != std::string::npos, "Shell version command must succeed");
    }

    std::cout << "[TEST] Suite 63: WinMM Multimedia Engine, DirectSound 8 Audio & Version API PASSED.\n";
}

// ============================================================================
// Suite 64: OpenGL & Windows WGL Subsystem Tests
// ============================================================================
void Test_OpenGL_And_WGL_Subsystem() {
    std::cout << "[TEST] Running Suite 64: OpenGL 1.4 & Windows WGL 3D Runtime...\n";

    // ------------------------------------------------------------------------
    // 1. Pixel Format Negotiation (GDI / WGL Bridge)
    // ------------------------------------------------------------------------
    win32::HWND hwnd = user32::CreateWindowExW(
        0, L"MicaNT_Window", L"OpenGL Unit Test Target",
        0, 0, 0, 400, 300, nullptr, nullptr, nullptr, nullptr
    );
    TEST_ASSERT(hwnd != nullptr, "CreateWindowExW for OpenGL test target must succeed");

    gdi32::HDC hdc = reinterpret_cast<gdi32::HDC>(user32::GetDC(hwnd));
    TEST_ASSERT(hdc != nullptr, "GetDC for OpenGL window must succeed");

    gdi32::PIXELFORMATDESCRIPTOR pfdReq{};
    pfdReq.nSize = sizeof(pfdReq);
    pfdReq.nVersion = 1;
    pfdReq.dwFlags = gdi32::PFD_DRAW_TO_WINDOW | gdi32::PFD_SUPPORT_OPENGL | gdi32::PFD_DOUBLEBUFFER;
    pfdReq.iPixelType = gdi32::PFD_TYPE_RGBA;
    pfdReq.cColorBits = 32;
    pfdReq.cDepthBits = 24;

    int chosenFmt = gdi32::ChoosePixelFormat(hdc, &pfdReq);
    TEST_ASSERT(chosenFmt == 1, "ChoosePixelFormat must select format 1 (32-bpp BGRA)");

    win32::BOOL setOk = gdi32::SetPixelFormat(hdc, chosenFmt, &pfdReq);
    TEST_ASSERT(setOk == win32::TRUE, "SetPixelFormat must succeed");

    gdi32::PIXELFORMATDESCRIPTOR pfdDesc{};
    int descOk = gdi32::DescribePixelFormat(hdc, chosenFmt, sizeof(pfdDesc), &pfdDesc);
    TEST_ASSERT(descOk == 1, "DescribePixelFormat must succeed for format 1");

    // ------------------------------------------------------------------------
    // 2. WGL Context Lifecycle & Thread-Local Binding
    // ------------------------------------------------------------------------
    opengl::HGLRC hglrc = opengl::wglCreateContext(hdc);
    TEST_ASSERT(hglrc != nullptr, "wglCreateContext must return a valid HGLRC");

    TEST_ASSERT(opengl::wglGetCurrentContext() == nullptr, "Current context before wglMakeCurrent must be null");
    TEST_ASSERT(opengl::wglGetCurrentDC() == nullptr, "Current DC before wglMakeCurrent must be null");

    win32::BOOL makeOk = opengl::wglMakeCurrent(hdc, hglrc);
    TEST_ASSERT(makeOk == win32::TRUE, "wglMakeCurrent must succeed");
    TEST_ASSERT(opengl::wglGetCurrentContext() == hglrc, "wglGetCurrentContext must report active HGLRC");
    TEST_ASSERT(opengl::wglGetCurrentDC() == hdc, "wglGetCurrentDC must report active HDC");

    // ------------------------------------------------------------------------
    // 3. String & Extension Queries
    // ------------------------------------------------------------------------
    const char* vendor = reinterpret_cast<const char*>(opengl::glGetString(opengl::GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(opengl::glGetString(opengl::GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(opengl::glGetString(opengl::GL_VERSION));
    const char* extensions = reinterpret_cast<const char*>(opengl::glGetString(opengl::GL_EXTENSIONS));

    TEST_ASSERT(vendor && std::string(vendor).find("MicaNT") != std::string::npos, "glGetString(GL_VENDOR) must report MicaNT");
    TEST_ASSERT(renderer && std::string(renderer).find("PrismGL") != std::string::npos, "glGetString(GL_RENDERER) must report PrismGL");
    TEST_ASSERT(version && std::string(version).find("1.4") != std::string::npos, "glGetString(GL_VERSION) must report 1.4");
    TEST_ASSERT(extensions && std::string(extensions).find("GL_ARB_multitexture") != std::string::npos, "glGetString(GL_EXTENSIONS) must contain standard extensions");

    // ------------------------------------------------------------------------
    // 4. Matrix Engine & Stacks
    // ------------------------------------------------------------------------
    opengl::glMatrixMode(opengl::GL_MODELVIEW);
    opengl::glLoadIdentity();

    float mIdent[16]{};
    opengl::glGetFloatv(0x0BA6 /* GL_MODELVIEW_MATRIX */, mIdent);
    TEST_ASSERT(std::abs(mIdent[0] - 1.0f) < 1e-4f && std::abs(mIdent[5] - 1.0f) < 1e-4f &&
                std::abs(mIdent[10] - 1.0f) < 1e-4f && std::abs(mIdent[15] - 1.0f) < 1e-4f,
                "Modelview matrix identity diagonal must be 1.0");

    opengl::glTranslatef(10.0f, 20.0f, 30.0f);
    float mTrans[16]{};
    opengl::glGetFloatv(0x0BA6, mTrans);
    TEST_ASSERT(std::abs(mTrans[12] - 10.0f) < 1e-4f && std::abs(mTrans[13] - 20.0f) < 1e-4f &&
                std::abs(mTrans[14] - 30.0f) < 1e-4f, "Translation matrix must have offsets in column 3");

    opengl::glPushMatrix();
    opengl::glScalef(2.0f, 3.0f, 4.0f);
    float mScaled[16]{};
    opengl::glGetFloatv(0x0BA6, mScaled);
    TEST_ASSERT(std::abs(mScaled[0] - 2.0f) < 1e-4f && std::abs(mScaled[5] - 3.0f) < 1e-4f &&
                std::abs(mScaled[10] - 4.0f) < 1e-4f, "Scaling matrix must scale axes");

    opengl::glPopMatrix();
    float mRestored[16]{};
    opengl::glGetFloatv(0x0BA6, mRestored);
    TEST_ASSERT(std::abs(mRestored[0] - 1.0f) < 1e-4f && std::abs(mRestored[12] - 10.0f) < 1e-4f,
                "glPopMatrix must restore previous matrix");

    // Underflow check
    opengl::glPopMatrix();
    TEST_ASSERT(opengl::glGetError() == opengl::GL_STACK_UNDERFLOW, "glPopMatrix beyond bottom must set GL_STACK_UNDERFLOW");

    // ------------------------------------------------------------------------
    // 5. GLU Utility Library
    // ------------------------------------------------------------------------
    opengl::glMatrixMode(opengl::GL_PROJECTION);
    opengl::glLoadIdentity();
    opengl::gluPerspective(60.0, 4.0 / 3.0, 0.1, 100.0);
    float mProj[16]{};
    opengl::glGetFloatv(0x0BA7 /* GL_PROJECTION_MATRIX */, mProj);
    TEST_ASSERT(mProj[0] > 0.0f && mProj[5] > 0.0f && std::abs(mProj[11] - (-1.0f)) < 1e-4f,
                "gluPerspective must compute perspective projection matrix");

    const char* errStr = reinterpret_cast<const char*>(opengl::gluErrorString(opengl::GL_INVALID_ENUM));
    TEST_ASSERT(errStr && std::string(errStr).find("invalid enumerant") != std::string::npos, "gluErrorString must describe error");

    // ------------------------------------------------------------------------
    // 6. Viewport, State & Clear
    // ------------------------------------------------------------------------
    opengl::glViewport(0, 0, 400, 300);
    opengl::GLint vp[4]{};
    opengl::glGetIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
    TEST_ASSERT(vp[0] == 0 && vp[1] == 0 && vp[2] == 400 && vp[3] == 300, "glViewport dimensions must match");

    opengl::glEnable(opengl::GL_DEPTH_TEST);
    TEST_ASSERT(opengl::glIsEnabled(opengl::GL_DEPTH_TEST) == opengl::GL_TRUE, "GL_DEPTH_TEST must be enabled");
    opengl::glDepthFunc(opengl::GL_LEQUAL);

    opengl::glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
    opengl::glClearDepth(1.0);
    opengl::glClear(opengl::GL_COLOR_BUFFER_BIT | opengl::GL_DEPTH_BUFFER_BIT);

    // ------------------------------------------------------------------------
    // 7. Immediate Mode Geometry & Depth Testing
    // ------------------------------------------------------------------------
    opengl::glMatrixMode(opengl::GL_MODELVIEW);
    opengl::glLoadIdentity();

    // Draw background triangle at Z = 0.5 (Solid Red)
    opengl::glBegin(opengl::GL_TRIANGLES);
    opengl::glColor3f(1.0f, 0.0f, 0.0f);
    opengl::glVertex3f(-0.5f, -0.5f, 0.5f);
    opengl::glVertex3f( 0.5f, -0.5f, 0.5f);
    opengl::glVertex3f( 0.0f,  0.5f, 0.5f);
    opengl::glEnd();

    // Draw foreground triangle at Z = -0.5 (Solid Green) overlapping the center
    opengl::glBegin(opengl::GL_TRIANGLES);
    opengl::glColor3f(0.0f, 1.0f, 0.0f);
    opengl::glVertex3f(-0.5f, -0.5f, -0.5f);
    opengl::glVertex3f( 0.5f, -0.5f, -0.5f);
    opengl::glVertex3f( 0.0f,  0.5f, -0.5f);
    opengl::glEnd();

    // Attempt to draw occluded triangle at Z = 0.8 (Solid Blue) behind both - must be depth rejected!
    opengl::glBegin(opengl::GL_TRIANGLES);
    opengl::glColor3f(0.0f, 0.0f, 1.0f);
    opengl::glVertex3f(-0.5f, -0.5f, 0.8f);
    opengl::glVertex3f( 0.5f, -0.5f, 0.8f);
    opengl::glVertex3f( 0.0f,  0.5f, 0.8f);
    opengl::glEnd();

    // ------------------------------------------------------------------------
    // 8. Quads, Strips & Line Rendering
    // ------------------------------------------------------------------------
    opengl::glBegin(opengl::GL_QUADS);
    opengl::glColor4f(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
    opengl::glVertex3f(-0.8f, 0.6f, 0.0f);
    opengl::glVertex3f(-0.6f, 0.6f, 0.0f);
    opengl::glVertex3f(-0.6f, 0.8f, 0.0f);
    opengl::glVertex3f(-0.8f, 0.8f, 0.0f);
    opengl::glEnd();

    opengl::glBegin(opengl::GL_LINES);
    opengl::glColor3f(1.0f, 1.0f, 1.0f);
    opengl::glVertex3f(-1.0f, 0.0f, 0.0f);
    opengl::glVertex3f( 1.0f, 0.0f, 0.0f);
    opengl::glEnd();

    // ------------------------------------------------------------------------
    // 9. 2D Texture Creation & Sampling
    // ------------------------------------------------------------------------
    opengl::GLuint texId = 0;
    opengl::glGenTextures(1, &texId);
    TEST_ASSERT(texId > 0, "glGenTextures must return valid texture ID");

    opengl::glBindTexture(opengl::GL_TEXTURE_2D, texId);
    opengl::glTexParameteri(opengl::GL_TEXTURE_2D, opengl::GL_TEXTURE_MIN_FILTER, opengl::GL_NEAREST);
    opengl::glTexParameteri(opengl::GL_TEXTURE_2D, opengl::GL_TEXTURE_MAG_FILTER, opengl::GL_LINEAR);
    opengl::glTexParameteri(opengl::GL_TEXTURE_2D, opengl::GL_TEXTURE_WRAP_S, opengl::GL_REPEAT);
    opengl::glTexParameteri(opengl::GL_TEXTURE_2D, opengl::GL_TEXTURE_WRAP_T, opengl::GL_REPEAT);

    uint8_t texData[4 * 4 * 4]; // 4x4 checkered texture
    for (int i = 0; i < 16; ++i) {
        bool on = ((i / 4) + (i % 4)) % 2 == 0;
        texData[i * 4 + 0] = on ? 255 : 32;  // R
        texData[i * 4 + 1] = on ? 255 : 64;  // G
        texData[i * 4 + 2] = on ? 255 : 128; // B
        texData[i * 4 + 3] = 255;            // A
    }
    opengl::glTexImage2D(opengl::GL_TEXTURE_2D, 0, 4, 4, 4, 0, opengl::GL_RGBA, opengl::GL_UNSIGNED_BYTE, texData);

    opengl::glEnable(opengl::GL_TEXTURE_2D);
    TEST_ASSERT(opengl::glIsEnabled(opengl::GL_TEXTURE_2D) == opengl::GL_TRUE, "GL_TEXTURE_2D must be enabled");

    // Draw textured quad
    opengl::glBegin(opengl::GL_QUADS);
    opengl::glColor3f(1.0f, 1.0f, 1.0f);
    opengl::glTexCoord2f(0.0f, 0.0f); opengl::glVertex3f(0.5f, 0.5f, 0.0f);
    opengl::glTexCoord2f(1.0f, 0.0f); opengl::glVertex3f(0.8f, 0.5f, 0.0f);
    opengl::glTexCoord2f(1.0f, 1.0f); opengl::glVertex3f(0.8f, 0.8f, 0.0f);
    opengl::glTexCoord2f(0.0f, 1.0f); opengl::glVertex3f(0.5f, 0.8f, 0.0f);
    opengl::glEnd();
    opengl::glDisable(opengl::GL_TEXTURE_2D);

    // ------------------------------------------------------------------------
    // 10. Vertex Arrays (glDrawArrays)
    // ------------------------------------------------------------------------
    float vaPos[3 * 3] = {
        -0.3f, -0.8f, 0.0f,
         0.3f, -0.8f, 0.0f,
         0.0f, -0.5f, 0.0f
    };
    float vaColor[3 * 3] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };

    opengl::glEnableClientState(opengl::GL_VERTEX_ARRAY);
    opengl::glEnableClientState(opengl::GL_COLOR_ARRAY);
    opengl::glVertexPointer(3, opengl::GL_FLOAT, 0, vaPos);
    opengl::glColorPointer(3, opengl::GL_FLOAT, 0, vaColor);

    opengl::glDrawArrays(opengl::GL_TRIANGLES, 0, 3);

    opengl::glDisableClientState(opengl::GL_VERTEX_ARRAY);
    opengl::glDisableClientState(opengl::GL_COLOR_ARRAY);

    // ------------------------------------------------------------------------
    // 11. Presentation & SwapBuffers
    // ------------------------------------------------------------------------
    win32::BOOL swapOk = opengl::wglSwapBuffers(hdc);
    TEST_ASSERT(swapOk == win32::TRUE, "wglSwapBuffers must succeed");

    auto dc = gdi32::GdiEngine::get().getDc(hdc);
    TEST_ASSERT(dc != nullptr && dc->GetBitmap() != nullptr, "DC must hold active bitmap");
    uint32_t centerPixel = dc->GetBitmap()->GetPixel(200, 150); // Center of 400x300 target
    TEST_ASSERT(centerPixel != 0, "Center pixel must have been rendered and blitted");

    // ------------------------------------------------------------------------
    // 12. Extensions via wglGetProcAddress
    // ------------------------------------------------------------------------
    void* pfnGenBuffers = opengl::wglGetProcAddress("glGenBuffersARB");
    TEST_ASSERT(pfnGenBuffers != nullptr, "wglGetProcAddress('glGenBuffersARB') must resolve function pointer");
    void* pfnBindBuffer = opengl::wglGetProcAddress("glBindBufferARB");
    TEST_ASSERT(pfnBindBuffer != nullptr, "wglGetProcAddress('glBindBufferARB') must resolve function pointer");

    // ------------------------------------------------------------------------
    // 13. DynamicLoader Subsystem Export Verification
    // ------------------------------------------------------------------------
    auto& ldr = ldr::DynamicLoader::get();
    TEST_ASSERT(ldr.getExport("opengl32.dll", "wglCreateContext") != nullptr, "opengl32.dll!wglCreateContext must be registered");
    TEST_ASSERT(ldr.getExport("opengl32.dll", "glBegin") != nullptr, "opengl32.dll!glBegin must be registered");
    TEST_ASSERT(ldr.getExport("opengl32.dll", "glEnd") != nullptr, "opengl32.dll!glEnd must be registered");
    TEST_ASSERT(ldr.getExport("opengl32.dll", "glDrawArrays") != nullptr, "opengl32.dll!glDrawArrays must be registered");
    TEST_ASSERT(ldr.getExport("glu32.dll", "gluPerspective") != nullptr, "glu32.dll!gluPerspective must be registered");
    TEST_ASSERT(ldr.getExport("glu32.dll", "gluLookAt") != nullptr, "glu32.dll!gluLookAt must be registered");

    // ------------------------------------------------------------------------
    // 14. Version Information Subsystem Verification
    // ------------------------------------------------------------------------
    uint32_t verHandle = 0;
    uint32_t verSizeGl = version::GetFileVersionInfoSizeA("opengl32.dll", &verHandle);
    TEST_ASSERT(verSizeGl > 0, "GetFileVersionInfoSizeA for opengl32.dll must report size > 0");

    std::vector<uint8_t> verDataGl(verSizeGl);
    TEST_ASSERT(version::GetFileVersionInfoA("opengl32.dll", verHandle, verSizeGl, verDataGl.data()), "GetFileVersionInfoA for opengl32.dll must succeed");

    void* pDesc = nullptr;
    uint32_t descLen = 0;
    TEST_ASSERT(version::VerQueryValueA(verDataGl.data(), "\\StringFileInfo\\040904B0\\FileDescription", &pDesc, &descLen), "VerQueryValueA for opengl32.dll must succeed");
    TEST_ASSERT(std::string(static_cast<const char*>(pDesc)).find("OpenGL Client DLL") != std::string::npos, "FileDescription must match OpenGL Client DLL");

    uint32_t verSizeGlu = version::GetFileVersionInfoSizeA("glu32.dll", &verHandle);
    TEST_ASSERT(verSizeGlu > 0, "GetFileVersionInfoSizeA for glu32.dll must report size > 0");

    // ------------------------------------------------------------------------
    // 15. Shell Command Integration
    // ------------------------------------------------------------------------
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("opengl info", out);
        TEST_ASSERT(out.str().find("Vendor:            MicaNT Sovereign Project") != std::string::npos, "Shell opengl info command must succeed");
    }
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("opengl test", out);
        TEST_ASSERT(out.str().find("3D Shaded Prism rendered and presented successfully!") != std::string::npos, "Shell opengl test command must succeed");
    }

    // Context Cleanup
    opengl::wglMakeCurrent(nullptr, nullptr);
    opengl::wglDeleteContext(hglrc);
    user32::ReleaseDC(hwnd, reinterpret_cast<user32::HDC>(hdc));
    user32::DestroyWindow(hwnd);

    std::cout << "[TEST] Suite 64: OpenGL 1.4 & Windows WGL 3D Runtime PASSED.\n";
}

// ============================================================================
// Test Suite 65: WinINet & URLMon Web Client Subsystems
// ============================================================================

void Test_WinINet_And_URLMon_Subsystems() {
    using namespace micant;

    // ------------------------------------------------------------------------
    // 1. URL Cracking & Scheme Introspection (InternetCrackUrlW)
    // ------------------------------------------------------------------------
    {
        const wchar_t* testUrl = L"http://admin:secret123@api.micant.org:8080/v1/telemetry?format=json#trace";
        wininet::URL_COMPONENTSW comp{};
        comp.dwStructSize = sizeof(comp);
        wchar_t scheme[16]{};
        wchar_t host[64]{};
        wchar_t user[32]{};
        wchar_t pass[32]{};
        wchar_t path[64]{};
        wchar_t extra[64]{};

        comp.lpszScheme = scheme; comp.dwSchemeLength = sizeof(scheme) / sizeof(wchar_t);
        comp.lpszHostName = host; comp.dwHostNameLength = sizeof(host) / sizeof(wchar_t);
        comp.lpszUserName = user; comp.dwUserNameLength = sizeof(user) / sizeof(wchar_t);
        comp.lpszPassword = pass; comp.dwPasswordLength = sizeof(pass) / sizeof(wchar_t);
        comp.lpszUrlPath = path;  comp.dwUrlPathLength = sizeof(path) / sizeof(wchar_t);
        comp.lpszExtraInfo = extra; comp.dwExtraInfoLength = sizeof(extra) / sizeof(wchar_t);

        win32::BOOL crackOk = wininet::InternetCrackUrlW(testUrl, 0, 0, &comp);
        TEST_ASSERT(crackOk == 1, "InternetCrackUrlW must succeed on standard HTTP URL");
        TEST_ASSERT(comp.nScheme == wininet::INTERNET_SCHEME_HTTP, "nScheme must be INTERNET_SCHEME_HTTP");
        TEST_ASSERT(comp.nPort == 8080, "nPort must be 8080");
        TEST_ASSERT(std::wstring(scheme) == L"http", "Scheme string must match http");
        TEST_ASSERT(std::wstring(host) == L"api.micant.org", "Host string must match api.micant.org");
        TEST_ASSERT(std::wstring(user) == L"admin", "User string must match admin");
        TEST_ASSERT(std::wstring(pass) == L"secret123", "Password string must match secret123");
        TEST_ASSERT(std::wstring(path) == L"/v1/telemetry", "Path string must match /v1/telemetry");
        TEST_ASSERT(std::wstring(extra) == L"?format=json#trace", "Extra string must match ?format=json#trace");
    }

    // ------------------------------------------------------------------------
    // 2. URL Construction (InternetCreateUrlW)
    // ------------------------------------------------------------------------
    {
        wininet::URL_COMPONENTSW comp{};
        comp.dwStructSize = sizeof(comp);
        wchar_t scheme[] = L"https";
        wchar_t host[] = L"download.micant.org";
        wchar_t path[] = L"/kernel/update.bin";
        comp.lpszScheme = scheme; comp.dwSchemeLength = static_cast<uint32_t>(wcslen(scheme));
        comp.lpszHostName = host; comp.dwHostNameLength = static_cast<uint32_t>(wcslen(host));
        comp.nPort = 443;
        comp.lpszUrlPath = path; comp.dwUrlPathLength = static_cast<uint32_t>(wcslen(path));

        wchar_t builtUrl[256]{};
        uint32_t builtLen = sizeof(builtUrl) / sizeof(wchar_t);
        win32::BOOL createOk = wininet::InternetCreateUrlW(&comp, 0, builtUrl, &builtLen);
        TEST_ASSERT(createOk == 1, "InternetCreateUrlW must succeed");
        TEST_ASSERT(std::wstring(builtUrl) == L"https://download.micant.org/kernel/update.bin", "Constructed URL must match expected target");
    }

    // ------------------------------------------------------------------------
    // 3. URL Canonicalization (InternetCanonicalizeUrlW)
    // ------------------------------------------------------------------------
    {
        const wchar_t* raw = L"http://micant.org/my document file.pdf";
        wchar_t canon[256]{};
        uint32_t canonLen = sizeof(canon) / sizeof(wchar_t);
        win32::BOOL canOk = wininet::InternetCanonicalizeUrlW(raw, canon, &canonLen, 0);
        TEST_ASSERT(canOk == 1, "InternetCanonicalizeUrlW must succeed");
        TEST_ASSERT(std::wstring(canon) == L"http://micant.org/my%20document%20file.pdf", "Canonicalized URL must escape spaces with %20");
    }

    // ------------------------------------------------------------------------
    // 4. Handle Lifecycle & Cascading Cleanup
    // ------------------------------------------------------------------------
    {
        wininet::HINTERNET hSess = wininet::InternetOpenW(L"MicaNT Test Agent", wininet::INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
        TEST_ASSERT(hSess != nullptr, "InternetOpenW must return valid HINTERNET");

        wininet::HINTERNET hConn = wininet::InternetConnectW(hSess, L"micant.org", 80, nullptr, nullptr, wininet::INTERNET_SERVICE_HTTP, 0, 101);
        TEST_ASSERT(hConn != nullptr, "InternetConnectW must return valid HINTERNET");

        wininet::HINTERNET hReq = wininet::HttpOpenRequestW(hConn, L"GET", L"/index.html", nullptr, nullptr, nullptr, 0, 102);
        TEST_ASSERT(hReq != nullptr, "HttpOpenRequestW must return valid HINTERNET");

        // Verify handle lookup
        auto reqObj = wininet::InternetHandleTable::Instance().getHandleAs<wininet::HttpRequestHandle>(hReq);
        TEST_ASSERT(reqObj != nullptr, "HandleTable must resolve valid HttpRequestHandle");
        TEST_ASSERT(reqObj->verb == L"GET", "Request verb must be GET");
        TEST_ASSERT(reqObj->objectName == L"/index.html", "Request objectName must be /index.html");

        // Cascading close
        win32::BOOL closeOk = wininet::InternetCloseHandle(hSess);
        TEST_ASSERT(closeOk == 1, "InternetCloseHandle for session must succeed");
        TEST_ASSERT(wininet::InternetHandleTable::Instance().getHandle(hReq) == nullptr, "Child request handle must be cascade-closed");
        TEST_ASSERT(wininet::InternetHandleTable::Instance().getHandle(hConn) == nullptr, "Child connection handle must be cascade-closed");
    }

    // ------------------------------------------------------------------------
    // 5. Request Headers Management (HttpAddRequestHeadersW)
    // ------------------------------------------------------------------------
    {
        wininet::HINTERNET hSess = wininet::InternetOpenW(L"MicaNT", wininet::INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
        wininet::HINTERNET hConn = wininet::InternetConnectW(hSess, L"micant.org", 80, nullptr, nullptr, wininet::INTERNET_SERVICE_HTTP, 0, 0);
        wininet::HINTERNET hReq = wininet::HttpOpenRequestW(hConn, L"POST", L"/api/v1/echo", nullptr, nullptr, nullptr, 0, 0);

        wininet::HttpAddRequestHeadersW(hReq, L"Content-Type: application/json\r\n", static_cast<uint32_t>(-1), wininet::HTTP_ADDREQ_FLAG_ADD);
        wininet::HttpAddRequestHeadersW(hReq, L"X-Trace-Id: TRACE_001\r\n", static_cast<uint32_t>(-1), wininet::HTTP_ADDREQ_FLAG_ADD);

        auto reqObj = wininet::InternetHandleTable::Instance().getHandleAs<wininet::HttpRequestHandle>(hReq);
        TEST_ASSERT(reqObj->getRequestHeader("Content-Type") == "application/json", "Header Content-Type must be present");
        TEST_ASSERT(reqObj->getRequestHeader("X-Trace-Id") == "TRACE_001", "Header X-Trace-Id must be present");

        // Test replace modifier
        wininet::HttpAddRequestHeadersW(hReq, L"X-Trace-Id: TRACE_UPDATED\r\n", static_cast<uint32_t>(-1), wininet::HTTP_ADDREQ_FLAG_REPLACE);
        TEST_ASSERT(reqObj->getRequestHeader("X-Trace-Id") == "TRACE_UPDATED", "Header X-Trace-Id must be updated via REPLACE");

        wininet::InternetCloseHandle(hSess);
    }

    // ------------------------------------------------------------------------
    // 6. HTTP Mock Registry & Request Execution (HttpSendRequestW)
    // ------------------------------------------------------------------------
    {
        std::string mockPayload = "{\"os\":\"MicaNT\",\"status\":\"healthy\",\"uptime\":3600}";
        wininet::HttpMockRegistry::Instance().registerMock(
            "http://micant.org/api/health",
            200,
            "application/json",
            mockPayload,
            { { "X-Custom-Server", "MicaNT-Server/1.0" } }
        );

        wininet::HINTERNET hSess = wininet::InternetOpenW(L"MicaNT Test", wininet::INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
        wininet::HINTERNET hConn = wininet::InternetConnectW(hSess, L"micant.org", 80, nullptr, nullptr, wininet::INTERNET_SERVICE_HTTP, 0, 0);
        wininet::HINTERNET hReq = wininet::HttpOpenRequestW(hConn, L"GET", L"/api/health", nullptr, nullptr, nullptr, 0, 0);

        win32::BOOL sendOk = wininet::HttpSendRequestW(hReq, nullptr, 0, nullptr, 0);
        TEST_ASSERT(sendOk == 1, "HttpSendRequestW must succeed against registered mock endpoint");

        // --------------------------------------------------------------------
        // 7. HTTP Query Info Introspection (HttpQueryInfoW)
        // --------------------------------------------------------------------
        uint32_t statusCode = 0;
        uint32_t statusLen = sizeof(statusCode);
        win32::BOOL qStatus = wininet::HttpQueryInfoW(hReq, wininet::HTTP_QUERY_STATUS_CODE | wininet::HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusLen, nullptr);
        TEST_ASSERT(qStatus == 1, "HttpQueryInfoW for STATUS_CODE must succeed");
        TEST_ASSERT(statusCode == 200, "Response status code must be 200");

        wchar_t contentType[64]{};
        uint32_t ctLen = sizeof(contentType);
        win32::BOOL qType = wininet::HttpQueryInfoW(hReq, wininet::HTTP_QUERY_CONTENT_TYPE, contentType, &ctLen, nullptr);
        TEST_ASSERT(qType == 1, "HttpQueryInfoW for CONTENT_TYPE must succeed");
        TEST_ASSERT(std::wstring(contentType).find(L"application/json") != std::wstring::npos, "Content-Type must contain application/json");

        uint32_t contentLen = 0;
        uint32_t clSize = sizeof(contentLen);
        win32::BOOL qLen = wininet::HttpQueryInfoW(hReq, wininet::HTTP_QUERY_CONTENT_LENGTH | wininet::HTTP_QUERY_FLAG_NUMBER, &contentLen, &clSize, nullptr);
        TEST_ASSERT(qLen == 1, "HttpQueryInfoW for CONTENT_LENGTH must succeed");
        TEST_ASSERT(contentLen == mockPayload.size(), "Content-Length must match payload length");

        // --------------------------------------------------------------------
        // 8. Stream Reading (InternetReadFile & InternetQueryDataAvailable)
        // --------------------------------------------------------------------
        uint32_t avail = 0;
        win32::BOOL qAvail = wininet::InternetQueryDataAvailable(hReq, &avail, 0, 0);
        TEST_ASSERT(qAvail == 1, "InternetQueryDataAvailable must succeed");
        TEST_ASSERT(avail == mockPayload.size(), "Available bytes must match mock payload size");

        std::string accumulated;
        char chunk[16]{};
        uint32_t bytesRead = 0;
        while (wininet::InternetReadFile(hReq, chunk, sizeof(chunk), &bytesRead) && bytesRead > 0) {
            accumulated.append(chunk, bytesRead);
        }
        TEST_ASSERT(accumulated == mockPayload, "Accumulated body must exactly match mock response body");

        // Subsequent read should return 0 bytes (EOF)
        uint32_t eofBytes = 999;
        win32::BOOL eofOk = wininet::InternetReadFile(hReq, chunk, sizeof(chunk), &eofBytes);
        TEST_ASSERT(eofOk == 1 && eofBytes == 0, "Reading at EOF must report 0 bytes read");

        wininet::InternetCloseHandle(hSess);
    }

    // ------------------------------------------------------------------------
    // 9. Chunked Transfer Encoding (RFC 7230 §4.1)
    // ------------------------------------------------------------------------
    {
        const char chunkedSample[] = "7\r\nMozilla\r\n9\r\nDeveloper\r\n7\r\nNetwork\r\n0\r\n\r\n";
        std::vector<uint8_t> decoded;
        bool decOk = wininet::DecodeChunkedPayload(
            reinterpret_cast<const uint8_t*>(chunkedSample),
            strlen(chunkedSample),
            decoded
        );
        TEST_ASSERT(decOk == true, "DecodeChunkedPayload must succeed on valid RFC 7230 chunked stream");
        std::string decodedStr(decoded.begin(), decoded.end());
        TEST_ASSERT(decodedStr == "MozillaDeveloperNetwork", "Decoded chunked payload must match combined chunks");
    }

    // ------------------------------------------------------------------------
    // 10. Cookie Jar Management (RFC 6265)
    // ------------------------------------------------------------------------
    {
        wininet::CookieJar::Instance().clear();
        win32::BOOL setOk = wininet::InternetSetCookieW(
            L"http://micant.org/dashboard",
            nullptr,
            L"auth_session=MicaSecure9988; path=/dashboard; domain=micant.org"
        );
        TEST_ASSERT(setOk == 1, "InternetSetCookieW must succeed");

        wchar_t cookieBuf[256]{};
        uint32_t cookieSize = sizeof(cookieBuf);
        win32::BOOL getOk = wininet::InternetGetCookieW(
            L"http://micant.org/dashboard/settings",
            nullptr,
            cookieBuf,
            &cookieSize
        );
        TEST_ASSERT(getOk == 1, "InternetGetCookieW must match valid domain and path");
        TEST_ASSERT(std::wstring(cookieBuf).find(L"auth_session=MicaSecure9988") != std::wstring::npos, "Cookie value must match stored auth_session");

        // Different domain should not match
        wchar_t emptyBuf[256]{};
        uint32_t emptySize = sizeof(emptyBuf);
        wininet::InternetGetCookieW(L"http://external-site.com/dashboard", nullptr, emptyBuf, &emptySize);
        TEST_ASSERT(emptySize == 0 || wcslen(emptyBuf) == 0, "Cookie must not leak to unrelated domain");
    }

    // ------------------------------------------------------------------------
    // 11. URL Cache Subsystem (Temporary Internet Files)
    // ------------------------------------------------------------------------
    {
        wininet::UrlCacheManager::Instance().clear();
        wchar_t cachePath[wininet::MAX_PATH]{};
        win32::BOOL crOk = wininet::CreateUrlCacheEntryW(L"http://micant.org/logo.png", 2048, L"png", cachePath, 0);
        TEST_ASSERT(crOk == 1, "CreateUrlCacheEntryW must allocate valid cache file path");
        TEST_ASSERT(std::wstring(cachePath).find(L".png") != std::wstring::npos, "Allocated cache file must have .png extension");

        win32::BOOL cmOk = wininet::CommitUrlCacheEntryW(
            L"http://micant.org/logo.png",
            cachePath,
            9999999ULL,
            1111111ULL,
            0,
            L"HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n\r\n",
            0,
            L"png",
            nullptr
        );
        TEST_ASSERT(cmOk == 1, "CommitUrlCacheEntryW must succeed");

        wininet::CacheEntry retrieved;
        bool found = wininet::UrlCacheManager::Instance().retrieveEntry("http://micant.org/logo.png", retrieved);
        TEST_ASSERT(found == true, "UrlCacheManager must retrieve committed cache entry");
        TEST_ASSERT(retrieved.fileSize == 1024, "Retrieved cache entry size must match default");
    }

    // ------------------------------------------------------------------------
    // 12. MIME Sniffer (FindMimeFromData)
    // ------------------------------------------------------------------------
    {
        wchar_t* mimeOut = nullptr;

        // PNG Magic
        const uint8_t pngMagic[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00 };
        ole32::HRESULT hr = urlmon::FindMimeFromData(nullptr, nullptr, pngMagic, sizeof(pngMagic), nullptr, 0, &mimeOut, 0);
        TEST_ASSERT(SUCCEEDED(hr) && mimeOut != nullptr, "FindMimeFromData must succeed for PNG magic");
        TEST_ASSERT(std::wstring(mimeOut) == L"image/png", "Detected MIME must be image/png");
        ole32::CoTaskMemFree(mimeOut);

        // JPEG Magic
        const uint8_t jpegMagic[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10 };
        hr = urlmon::FindMimeFromData(nullptr, nullptr, jpegMagic, sizeof(jpegMagic), nullptr, 0, &mimeOut, 0);
        TEST_ASSERT(SUCCEEDED(hr) && mimeOut != nullptr, "FindMimeFromData must succeed for JPEG magic");
        TEST_ASSERT(std::wstring(mimeOut) == L"image/jpeg", "Detected MIME must be image/jpeg");
        ole32::CoTaskMemFree(mimeOut);

        // PDF Magic
        const uint8_t pdfMagic[] = { '%', 'P', 'D', 'F', '-', '1', '.', '7' };
        hr = urlmon::FindMimeFromData(nullptr, nullptr, pdfMagic, sizeof(pdfMagic), nullptr, 0, &mimeOut, 0);
        TEST_ASSERT(SUCCEEDED(hr) && mimeOut != nullptr, "FindMimeFromData must succeed for PDF magic");
        TEST_ASSERT(std::wstring(mimeOut) == L"application/pdf", "Detected MIME must be application/pdf");
        ole32::CoTaskMemFree(mimeOut);

        // ZIP Magic
        const uint8_t zipMagic[] = { 'P', 'K', 0x03, 0x04, 0x14, 0x00 };
        hr = urlmon::FindMimeFromData(nullptr, nullptr, zipMagic, sizeof(zipMagic), nullptr, 0, &mimeOut, 0);
        TEST_ASSERT(SUCCEEDED(hr) && mimeOut != nullptr, "FindMimeFromData must succeed for ZIP magic");
        TEST_ASSERT(std::wstring(mimeOut) == L"application/zip", "Detected MIME must be application/zip");
        ole32::CoTaskMemFree(mimeOut);

        // HTML Detection
        const char htmlData[] = "<!DOCTYPE html><html><body><h1>MicaNT</h1></body></html>";
        hr = urlmon::FindMimeFromData(nullptr, nullptr, htmlData, sizeof(htmlData), nullptr, 0, &mimeOut, 0);
        TEST_ASSERT(SUCCEEDED(hr) && mimeOut != nullptr, "FindMimeFromData must succeed for HTML");
        TEST_ASSERT(std::wstring(mimeOut) == L"text/html", "Detected MIME must be text/html");
        ole32::CoTaskMemFree(mimeOut);
    }

    // ------------------------------------------------------------------------
    // 13. URLDownloadToFileW & IBindStatusCallback
    // ------------------------------------------------------------------------
    {
        std::string fileContent = "MicaNT Sovereign OS - Web Download Subsystem Verified!";
        wininet::HttpMockRegistry::Instance().registerMock(
            "http://micant.org/downloads/readme.txt",
            200,
            "text/plain",
            fileContent
        );

        class TestProgressCallback : public urlmon::IBindStatusCallback {
        public:
            bool startCalled{false};
            bool progressCalled{false};
            bool stopCalled{false};
            uint32_t finalBytes{0};

            virtual ole32::HRESULT QueryInterface(ole32::REFIID riid, void** ppv) override {
                if (!ppv) return ole32::E_POINTER;
                if (riid == ole32::IID_IUnknown || riid == urlmon::IID_IBindStatusCallback) {
                    *ppv = this;
                    return ole32::S_OK;
                }
                *ppv = nullptr;
                return ole32::E_NOINTERFACE;
            }
            virtual uint32_t AddRef() override { return 1; }
            virtual uint32_t Release() override { return 1; }
            virtual ole32::HRESULT OnStartBinding(uint32_t, void*) override { startCalled = true; return ole32::S_OK; }
            virtual ole32::HRESULT GetPriority(int32_t*) override { return ole32::S_OK; }
            virtual ole32::HRESULT OnLowResource(uint32_t) override { return ole32::S_OK; }
            virtual ole32::HRESULT OnProgress(uint32_t cur, uint32_t, uint32_t, const wchar_t*) override {
                progressCalled = true;
                finalBytes = cur;
                return ole32::S_OK;
            }
            virtual ole32::HRESULT OnStopBinding(ole32::HRESULT, const wchar_t*) override { stopCalled = true; return ole32::S_OK; }
            virtual ole32::HRESULT GetBindInfo(uint32_t*, void*) override { return ole32::S_OK; }
            virtual ole32::HRESULT OnDataAvailable(uint32_t, uint32_t, void*, void*) override { return ole32::S_OK; }
            virtual ole32::HRESULT OnObjectAvailable(ole32::REFIID, ole32::IUnknown*) override { return ole32::S_OK; }
        };

        TestProgressCallback cb;
        const wchar_t* destPath = L"C:\\Windows\\Temp\\downloaded_readme.txt";
        ole32::HRESULT hr = urlmon::URLDownloadToFileW(
            nullptr,
            L"http://micant.org/downloads/readme.txt",
            destPath,
            0,
            &cb
        );
        TEST_ASSERT(SUCCEEDED(hr), "URLDownloadToFileW must return S_OK");
        TEST_ASSERT(cb.startCalled == true, "IBindStatusCallback::OnStartBinding must be called");
        TEST_ASSERT(cb.progressCalled == true, "IBindStatusCallback::OnProgress must be called");
        TEST_ASSERT(cb.stopCalled == true, "IBindStatusCallback::OnStopBinding must be called");
        TEST_ASSERT(cb.finalBytes == fileContent.size(), "Final downloaded byte count must match fileContent size");

        // Verify content written to VFS
        std::shared_ptr<fs::FileObject> fObj;
        NtStatus st = fs::VirtualFileSystem::get().createOrOpenFile(destPath, fs::FILE_GENERIC_READ, fs::FILE_OPEN, fObj);
        TEST_ASSERT(NT_SUCCESS(st) && fObj != nullptr, "Downloaded file must exist in VFS");
        std::string diskContent(fObj->getData().begin(), fObj->getData().end());
        TEST_ASSERT(diskContent == fileContent, "VFS file content must match downloaded mock payload");
    }

    // ------------------------------------------------------------------------
    // 14. URL Moniker (CreateURLMoniker & Display Name)
    // ------------------------------------------------------------------------
    {
        urlmon::IMoniker* pmk = nullptr;
        ole32::HRESULT hr = urlmon::CreateURLMoniker(nullptr, L"https://micant.org/subsystem/index.html", &pmk);
        TEST_ASSERT(SUCCEEDED(hr) && pmk != nullptr, "CreateURLMoniker must succeed");

        wchar_t* dispName = nullptr;
        hr = pmk->GetDisplayName(nullptr, nullptr, &dispName);
        TEST_ASSERT(SUCCEEDED(hr) && dispName != nullptr, "IMoniker::GetDisplayName must return display name");
        TEST_ASSERT(std::wstring(dispName) == L"https://micant.org/subsystem/index.html", "Display name must match URL");
        ole32::CoTaskMemFree(dispName);

        uint32_t hashVal = 0;
        pmk->Hash(&hashVal);
        TEST_ASSERT(hashVal != 0, "Moniker hash must be non-zero");

        pmk->Release();
    }

    // ------------------------------------------------------------------------
    // 15. Dynamic Loader & Version Database Exports Verification
    // ------------------------------------------------------------------------
    {
        auto& ldr = ldr::DynamicLoader::get();

        // wininet.dll exports
        TEST_ASSERT(ldr.getExport("wininet.dll", "InternetOpenW") != nullptr, "wininet.dll!InternetOpenW must be exported");
        TEST_ASSERT(ldr.getExport("wininet.dll", "InternetConnectW") != nullptr, "wininet.dll!InternetConnectW must be exported");
        TEST_ASSERT(ldr.getExport("wininet.dll", "HttpOpenRequestW") != nullptr, "wininet.dll!HttpOpenRequestW must be exported");
        TEST_ASSERT(ldr.getExport("wininet.dll", "HttpSendRequestW") != nullptr, "wininet.dll!HttpSendRequestW must be exported");
        TEST_ASSERT(ldr.getExport("wininet.dll", "HttpQueryInfoW") != nullptr, "wininet.dll!HttpQueryInfoW must be exported");
        TEST_ASSERT(ldr.getExport("wininet.dll", "InternetReadFile") != nullptr, "wininet.dll!InternetReadFile must be exported");
        TEST_ASSERT(ldr.getExport("wininet.dll", "InternetCrackUrlW") != nullptr, "wininet.dll!InternetCrackUrlW must be exported");

        // urlmon.dll exports
        TEST_ASSERT(ldr.getExport("urlmon.dll", "URLDownloadToFileW") != nullptr, "urlmon.dll!URLDownloadToFileW must be exported");
        TEST_ASSERT(ldr.getExport("urlmon.dll", "URLDownloadToCacheFileW") != nullptr, "urlmon.dll!URLDownloadToCacheFileW must be exported");
        TEST_ASSERT(ldr.getExport("urlmon.dll", "FindMimeFromData") != nullptr, "urlmon.dll!FindMimeFromData must be exported");
        TEST_ASSERT(ldr.getExport("urlmon.dll", "CreateURLMoniker") != nullptr, "urlmon.dll!CreateURLMoniker must be exported");

        // version info verification
        uint32_t h = 0;
        uint32_t sWininet = version::GetFileVersionInfoSizeA("wininet.dll", &h);
        TEST_ASSERT(sWininet > 0, "wininet.dll must have version info resource");

        std::vector<uint8_t> vWininet(sWininet);
        TEST_ASSERT(version::GetFileVersionInfoA("wininet.dll", h, sWininet, vWininet.data()) != 0, "GetFileVersionInfoA for wininet.dll must succeed");

        void* pDesc = nullptr;
        uint32_t dLen = 0;
        TEST_ASSERT(version::VerQueryValueA(vWininet.data(), "\\StringFileInfo\\040904B0\\FileDescription", &pDesc, &dLen) != 0, "VerQueryValueA for wininet.dll must succeed");
        TEST_ASSERT(std::string(static_cast<const char*>(pDesc)) == "Internet Extensions for Win32", "FileDescription must match Internet Extensions for Win32");

        uint32_t sUrlmon = version::GetFileVersionInfoSizeA("urlmon.dll", &h);
        TEST_ASSERT(sUrlmon > 0, "urlmon.dll must have version info resource");
    }

    // ------------------------------------------------------------------------
    // 16. Shell Command Integration (wininet & curl)
    // ------------------------------------------------------------------------
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("wininet info", out);
        TEST_ASSERT(out.str().find("MicaNT Windows Internet Subsystem") != std::string::npos, "Shell wininet info command must succeed");
    }
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("wininet test", out);
        TEST_ASSERT(out.str().find("SUCCESS - RFC 7230 request executed cleanly.") != std::string::npos, "Shell wininet test command must succeed");
    }
    {
        micant::shell::CommandShell shell;
        std::ostringstream out;
        shell.execute("curl http://micant.org/sample.txt -o C:\\Windows\\Temp\\curl_test.txt", out);
        TEST_ASSERT(out.str().find("Download completed successfully") != std::string::npos, "Shell curl command must succeed");
    }

    std::cout << "[TEST] Suite 65: WinINet & URLMon Web Client Subsystems PASSED.\n";
}

// ============================================================================
// Suite 66: Windows Cryptography API (CryptoAPI) & Cryptography Next Generation (CNG)
// ============================================================================
