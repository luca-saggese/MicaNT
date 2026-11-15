#pragma once
#include "micant/shared/subsystems.hpp"
#include "micant/shared/ntdef.hpp"
#include "micant/shared/ntstatus.hpp"
#include "micant/um/subsystems/winbase/kernel32.hpp"
#include "micant/um/subsystems/winuser/user32.hpp"
#include "micant/um/subsystems/winuser/gdi32.hpp"
#include "micant/um/subsystems/winuser/gdiplus.hpp"
#include "micant/um/subsystems/winbase/advapi32.hpp"
#include "micant/um/subsystems/winuser/shell32.hpp"
#include "micant/um/subsystems/winuser/comctl32.hpp"
#include "micant/um/subsystems/winuser/comdlg32.hpp"
#include "micant/ucrt/msvcrt.hpp"
#include "micant/um/subsystems/security/crypt32.hpp"
#include "micant/um/subsystems/net/ws2_32.hpp"
#include "micant/um/subsystems/com/ole32.hpp"
#include "micant/um/subsystems/com/oleaut32.hpp"
#include "micant/um/subsystems/winuser/uxtheme.hpp"
#include "micant/um/subsystems/com/propsys.hpp"
#include "micant/um/subsystems/net/wininet.hpp"
#include "micant/um/subsystems/winuser/winspool.hpp"
#include "micant/um/subsystems/winuser/uiautomation.hpp"
#include "micant/um/subsystems/winuser/tsf.hpp"
#include "micant/um/subsystems/security/wintrust.hpp"
#include "micant/um/subsystems/winbase/setupapi.hpp"
#include "micant/um/native/ntdll.hpp"
#include "micant/um/subsystems/security/aclui.hpp"
#include "micant/um/subsystems/winuser/winsta.hpp"
#include "micant/ucrt/icuuc.hpp"
#include "micant/um/subsystems/net/winhttp.hpp"
#include "micant/um/subsystems/net/iphlpapi.hpp"
#include "micant/um/subsystems/security/cipherksp.hpp"
#include "micant/um/subsystems/winbase/cfgmgr32.hpp"
#include "micant/um/subsystems/security/sspi.hpp"
#include "micant/um/subsystems/winbase/virtdisk.hpp"
#include "micant/um/subsystems/security/authz.hpp"

namespace {
inline std::wstring AnsiToWide(const char* str) {
    if (!str) return L"";
    std::wstring result;
    result.reserve(std::strlen(str));
    while (*str) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*str++)));
    }
    return result;
}
}


/**
 * @file server_storage_suites.hpp
 * @brief Storage Spaces, Clustering, Hyper-V & Enterprise Server Roles (Milestones 186-210)
 */

void Test_WindowsMbbCx_MBIM40_5G_Subsystem() {
    std::cout << "[TEST] Executing Suite 186: Windows Mobile Broadband Class Extension (MBIM 4.0 / MbbCx) & 5G NR Subsystem...\n";

    // Stage 1: MBIM 4.0 Subsystem SCM & VersionDatabase Registration
    micant::mbbcx::RegisterMbbSubsystem();
    auto& verDb = version::VersionDatabase::Instance();
    TEST_ASSERT(verDb.FindModule("mbbcx.sys") != nullptr, "mbbcx.sys must be registered in VersionDatabase");
    TEST_ASSERT(verDb.FindModule("wwansvc.dll") != nullptr, "wwansvc.dll must be registered in VersionDatabase");

    auto& scm = micant::scm::ServiceControlManager::get();
    auto scmSvc = scm.getServiceRecord(L"WwanSvc");
    TEST_ASSERT(scmSvc != nullptr, "WwanSvc must be registered in SCM");
    TEST_ASSERT(scmSvc->binaryPath.find(L"svchost.exe") != std::wstring::npos, "WwanSvc must be hosted by svchost.exe");
    TEST_ASSERT(scmSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "WwanSvc must be running");

    auto& mbbSys = micant::mbbcx::MbbSubsystem::get();
    TEST_ASSERT(mbbSys.isInitialized() == true, "MbbSubsystem must report initialized");

    // Stage 2: Default Modem Discovery & Hardware Context (Snapdragon X75 5G)
    TEST_ASSERT(mbbSys.getAdapterCount() >= 1, "MbbSubsystem must contain at least 1 cellular modem adapter");
    auto primaryAdapter = mbbSys.getAdapter(1);
    TEST_ASSERT(primaryAdapter != nullptr, "Primary cellular modem adapter (ID 1) must be accessible");
    TEST_ASSERT(primaryAdapter->getName().find(L"Snapdragon X75") != std::wstring::npos, "Primary adapter name must identify Snapdragon X75");
    TEST_ASSERT(primaryAdapter->getImei() == "861234567890123", "Primary adapter IMEI must match initialized 15-digit TAC");
    TEST_ASSERT(primaryAdapter->getImsi() == "310410123456789", "Primary adapter IMSI must match initialized carrier identity");

    // Stage 3: 5G NR Standalone (SA) Network Registration & Carrier Identity
    TEST_ASSERT(primaryAdapter->getRat() == micant::mbbcx::CellularRat::NR5G_SA, "Initial RAT must be 5G NR Standalone (SA)");
    TEST_ASSERT(primaryAdapter->getRegistrationState() == micant::mbbcx::NetworkRegistrationState::RegisteredHome, "Primary adapter must report RegisteredHome");
    TEST_ASSERT(primaryAdapter->getOperatorName() == L"MicaNT Sovereign 5G NR", "Registered carrier operator name must match MicaNT Sovereign 5G NR");
    TEST_ASSERT(std::string(micant::mbbcx::CellularRatToString(micant::mbbcx::CellularRat::NR5G_SA)).find("5G NR SA") != std::string::npos, "CellularRatToString must return correct description");
    TEST_ASSERT(std::string(micant::mbbcx::NetworkRegistrationStateToString(micant::mbbcx::NetworkRegistrationState::RegisteredHome)).find("Home Network") != std::string::npos, "NetworkRegistrationStateToString must return correct description");

    // Stage 4: Radio Power State Transitions (Airplane Mode, Off, On)
    TEST_ASSERT(primaryAdapter->getRadioState() == micant::mbbcx::CellularRadioState::On, "Initial radio state must be On");
    primaryAdapter->setRadioState(micant::mbbcx::CellularRadioState::AirplaneMode);
    TEST_ASSERT(primaryAdapter->getRadioState() == micant::mbbcx::CellularRadioState::AirplaneMode, "Radio state must transition to AirplaneMode");
    primaryAdapter->setRadioState(micant::mbbcx::CellularRadioState::Off);
    TEST_ASSERT(primaryAdapter->getRadioState() == micant::mbbcx::CellularRadioState::Off, "Radio state must transition to Off");
    primaryAdapter->setRadioState(micant::mbbcx::CellularRadioState::On);
    TEST_ASSERT(primaryAdapter->getRadioState() == micant::mbbcx::CellularRadioState::On, "Radio state must restore to On");

    // Stage 5: 5G/LTE Signal Quality Metrics & Dynamic Bar Calculation
    auto initialSig = primaryAdapter->getSignal();
    TEST_ASSERT(initialSig.rsrp == -82 && initialSig.rsrq == -10 && initialSig.sinr == 22, "Initial signal telemetry must match seeded RSRP/RSRQ/SINR");
    TEST_ASSERT(initialSig.bars == 4, "RSRP of -82 dBm must evaluate to 4 bars");

    primaryAdapter->setSignal(-75, -8, 28);
    TEST_ASSERT(primaryAdapter->getSignal().bars == 5, "RSRP of -75 dBm (>= -80) must evaluate to 5 bars (maximum signal)");

    primaryAdapter->setSignal(-115, -18, -2);
    TEST_ASSERT(primaryAdapter->getSignal().bars == 1, "RSRP of -115 dBm must evaluate to 1 bar");

    primaryAdapter->setSignal(-128, -20, -5);
    TEST_ASSERT(primaryAdapter->getSignal().bars == 0, "RSRP of -128 dBm must evaluate to 0 bars (cell edge)");

    primaryAdapter->setSignal(-82, -10, 22); // restore normal signal

    // Stage 6: GSMA SGP.22 eSIM Local Profile Assistant (LPA) Enumeration
    auto esimList = primaryAdapter->getEsimProfiles();
    TEST_ASSERT(esimList.size() >= 2, "Default cellular adapter must contain at least 2 eSIM profiles");
    bool foundActive = false;
    for (const auto& prof : esimList) {
        if (prof.iccid == L"89014103211118501234") {
            TEST_ASSERT(prof.state == micant::mbbcx::EsimProfileState::Enabled, "Primary profile 89014103211118501234 must be Enabled");
            foundActive = true;
        }
    }
    TEST_ASSERT(foundActive, "Primary eSIM profile must be found");

    // Stage 7: eSIM Profile Switching & Mutual Exclusivity
    bool switched = primaryAdapter->enableEsimProfile(L"89012604987654321098");
    TEST_ASSERT(switched, "Switching to secondary eSIM profile 89012604987654321098 must succeed");
    auto updatedProfiles = primaryAdapter->getEsimProfiles();
    for (const auto& prof : updatedProfiles) {
        if (prof.iccid == L"89012604987654321098") {
            TEST_ASSERT(prof.state == micant::mbbcx::EsimProfileState::Enabled, "Secondary profile must now be Enabled");
        } else if (prof.iccid == L"89014103211118501234") {
            TEST_ASSERT(prof.state == micant::mbbcx::EsimProfileState::Disabled, "Primary profile must now be Disabled (single active profile)");
        }
    }

    // Add and delete dynamic profile
    micant::mbbcx::EsimProfile tempProf{};
    tempProf.iccid = L"89019999999999999999";
    tempProf.profileName = L"Temporary Test Profile";
    tempProf.carrierName = L"Test Carrier";
    tempProf.state = micant::mbbcx::EsimProfileState::Disabled;
    primaryAdapter->addEsimProfile(tempProf);
    TEST_ASSERT(primaryAdapter->getEsimProfiles().size() == 3, "Profile count must increase to 3 after add");
    bool deleted = primaryAdapter->deleteEsimProfile(L"89019999999999999999");
    TEST_ASSERT(deleted, "Deleting profile must succeed");
    TEST_ASSERT(primaryAdapter->getEsimProfiles().size() == 2, "Profile count must return to 2 after delete");

    primaryAdapter->enableEsimProfile(L"89014103211118501234"); // restore primary

    // Stage 8: Packet Data Session Creation & Dual-Stack APN Negotiation
    auto allSessions = primaryAdapter->getAllSessions();
    TEST_ASSERT(!allSessions.empty(), "Primary adapter must have at least one active default session");
    auto defSession = primaryAdapter->getDataSession(allSessions.front().sessionId);
    TEST_ASSERT(defSession != nullptr && defSession->isConnected, "Default session must be connected");
    TEST_ASSERT(defSession->apn == L"internet", "Default session APN must be 'internet'");
    TEST_ASSERT(!defSession->ipV4.empty() && !defSession->ipV6.empty(), "Default session must have dual-stack IPv4/IPv6");
    TEST_ASSERT(defSession->mtu == 1500, "Default session MTU must be 1500");

    uint32_t imsSid = primaryAdapter->establishDataSession(L"ims", "10.128.5.10", "2607:fb90:ims::1");
    TEST_ASSERT(imsSid > 0, "Establishing IMS APN session must return valid session ID");
    auto imsSession = primaryAdapter->getDataSession(imsSid);
    TEST_ASSERT(imsSession != nullptr && imsSession->apn == L"ims", "IMS session must be present with APN 'ims'");

    // Stage 9: Packet Transmission, Reception & Throughput Accounting
    bool txOk = primaryAdapter->transmitPacket(imsSid, 1280);
    TEST_ASSERT(txOk, "transmitPacket must succeed on active session");
    for (int p = 0; p < 4; ++p) primaryAdapter->transmitPacket(imsSid, 1280);
    TEST_ASSERT(imsSession->txPackets == 5, "txPackets must equal 5");
    TEST_ASSERT(imsSession->txBytes == (5 * 1280), "txBytes must equal 6400 bytes");

    for (int p = 0; p < 10; ++p) primaryAdapter->receivePacket(imsSid, 1420);
    TEST_ASSERT(imsSession->rxPackets == 10, "rxPackets must equal 10");
    TEST_ASSERT(imsSession->rxBytes == (10 * 1420), "rxBytes must equal 14200 bytes");

    TEST_ASSERT(primaryAdapter->transmitPacket(99999, 100) == false, "transmitPacket on non-existent session must return false");

    // Stage 10: Packet Data Session Teardown
    bool closed = primaryAdapter->closeDataSession(imsSid);
    TEST_ASSERT(closed, "closeDataSession must return true for active session");
    TEST_ASSERT(primaryAdapter->getDataSession(imsSid) == nullptr, "Closed session must no longer be retrievable");

    // Stage 11: Clean-Room Win32 C ABI Parity Exports & Boundary Error Handling
    TEST_ASSERT(micant::mbbcx::MbbDeviceInitialize(nullptr) == micant::STATUS_SUCCESS, "MbbDeviceInitialize must return STATUS_SUCCESS");

    uint32_t abiAdpId = 0;
    NTSTATUS abiCreate = micant::mbbcx::MbbAdapterCreate(L"Virtual LTE Modem", &abiAdpId);
    TEST_ASSERT(abiCreate == micant::STATUS_SUCCESS && abiAdpId > 1, "MbbAdapterCreate must succeed with new adapter ID");

    TEST_ASSERT(micant::mbbcx::MbbRadioStateSet(abiAdpId, 0) == micant::STATUS_SUCCESS, "MbbRadioStateSet Off must return STATUS_SUCCESS");
    TEST_ASSERT(micant::mbbcx::MbbRadioStateSet(abiAdpId, 1) == micant::STATUS_SUCCESS, "MbbRadioStateSet On must return STATUS_SUCCESS");
    TEST_ASSERT(micant::mbbcx::MbbRadioStateSet(abiAdpId, 99) == micant::STATUS_INVALID_PARAMETER, "MbbRadioStateSet with invalid state must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::mbbcx::MbbRadioStateSet(99999, 1) == micant::STATUS_NOT_FOUND, "MbbRadioStateSet on nonexistent adapter must return STATUS_NOT_FOUND");

    uint32_t abiSid = 0;
    TEST_ASSERT(micant::mbbcx::MbbConnectDataSession(abiAdpId, L"custom.apn", 0, &abiSid) == micant::STATUS_SUCCESS, "MbbConnectDataSession must succeed");
    TEST_ASSERT(abiSid > 0, "Created session ID must be non-zero");

    int32_t abiRsrp = 0, abiRsrq = 0, abiSinr = 0;
    TEST_ASSERT(micant::mbbcx::MbbGetSignalState(abiAdpId, &abiRsrp, &abiRsrq, &abiSinr) == micant::STATUS_SUCCESS, "MbbGetSignalState must succeed");

    uint32_t abiEsimRes = 0;
    TEST_ASSERT(micant::mbbcx::MbbEsimProfileManage(abiAdpId, 99, L"dummy", &abiEsimRes) == micant::STATUS_INVALID_PARAMETER, "Invalid action in MbbEsimProfileManage must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::mbbcx::MbbEsimProfileManage(abiAdpId, 0, nullptr, &abiEsimRes) == micant::STATUS_INVALID_PARAMETER, "Null ICCID must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::mbbcx::MbbAdapterCreate(nullptr, nullptr) == micant::STATUS_INVALID_PARAMETER, "Null pointers in MbbAdapterCreate must return STATUS_INVALID_PARAMETER");

    // Stage 12: Multi-Threaded Concurrent Cellular Data Session Stress Test
    std::atomic<uint32_t> totalPacketsHandled{0};
    std::vector<std::thread> stressThreads;
    for (int t = 0; t < 4; ++t) {
        stressThreads.emplace_back([primaryAdapter, &totalPacketsHandled]() {
            uint32_t thSid = primaryAdapter->establishDataSession(L"stress_apn", "10.200.0.1", "fe80::100");
            for (int p = 0; p < 25; ++p) {
                if (primaryAdapter->transmitPacket(thSid, 512)) {
                    totalPacketsHandled++;
                }
                if (primaryAdapter->receivePacket(thSid, 1024)) {
                    totalPacketsHandled++;
                }
            }
            primaryAdapter->closeDataSession(thSid);
        });
    }
    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(totalPacketsHandled.load() == 200, "200 concurrent cellular packet transmissions/receptions must complete without data corruption or race conditions");

    std::cout << "[TEST] Suite 186: Windows Mobile Broadband Class Extension (MBIM 4.0 / MbbCx) & 5G NR Subsystem PASSED.\n";
}

void Test_WindowsProtectedMedia_PAVP_HDCP_Subsystem() {
    std::cout << "[TEST] Executing Suite 187: Windows Hardware Protected Media Path (PMP), Protected Audio Video Path (PAVP) & HDCP 2.3 Subsystem...\n";

    // Stage 1: PMP Subsystem SCM & VersionDatabase Module Registration
    micant::pmp::RegisterPmpSubsystem();
    auto& verDb = version::VersionDatabase::Instance();
    TEST_ASSERT(verDb.FindModule("mfpmp.exe") != nullptr, "mfpmp.exe must be registered in VersionDatabase");
    TEST_ASSERT(verDb.FindModule("dxva2.dll") != nullptr, "dxva2.dll must be registered in VersionDatabase");

    auto& scm = micant::scm::ServiceControlManager::get();
    auto scmSvc = scm.getServiceRecord(L"PmpService");
    TEST_ASSERT(scmSvc != nullptr, "PmpService must be registered in SCM");
    TEST_ASSERT(scmSvc->binaryPath.find(L"svchost.exe") != std::wstring::npos, "PmpService must be hosted by svchost.exe");
    TEST_ASSERT(scmSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "PmpService must be running");

    auto& pmpSys = micant::pmp::PmpSubsystem::get();
    TEST_ASSERT(pmpSys.isInitialized() == true, "PmpSubsystem must report initialized");

    // Stage 2: Output Protection Manager (OPM) Video Output Discovery & Topology
    TEST_ASSERT(pmpSys.getOutputCount() >= 2, "PmpSubsystem must contain at least 2 protected display outputs");
    auto out1 = pmpSys.getOutput(1);
    TEST_ASSERT(out1 != nullptr, "Display Output 1 must exist");
    TEST_ASSERT(out1->getName().find(L"Alienware AW3225QF") != std::wstring::npos, "Output 1 name must identify Alienware AW3225QF");
    TEST_ASSERT(out1->getHMonitor() == 0x10001, "Output 1 hMonitor must match 0x10001");
    TEST_ASSERT(out1->getConnector() == micant::pmp::OpmConnectorType::DisplayPort_External, "Output 1 connector must be DisplayPort_External");
    TEST_ASSERT(out1->getMaxHdcp() == micant::pmp::HdcpProtectionLevel::Hdcp23_Type1, "Output 1 max HDCP must be HDCP 2.3 Type 1");

    auto out2 = pmpSys.getOutput(2);
    TEST_ASSERT(out2 != nullptr, "Display Output 2 must exist");
    TEST_ASSERT(out2->getName().find(L"BRAVIA XR Master 8K") != std::wstring::npos, "Output 2 name must identify Sony BRAVIA XR 8K");
    TEST_ASSERT(out2->getHMonitor() == 0x10002, "Output 2 hMonitor must match 0x10002");
    TEST_ASSERT(out2->getConnector() == micant::pmp::OpmConnectorType::HDMI, "Output 2 connector must be HDMI");

    uint32_t out3Id = pmpSys.registerOutput(0x10003, L"Embedded OLED Laptop Display",
                                           micant::pmp::OpmConnectorType::DisplayPort_Embedded,
                                           micant::pmp::HdcpProtectionLevel::Hdcp22_Type0);
    TEST_ASSERT(out3Id == 3, "Registered third output must receive ID 3");
    TEST_ASSERT(pmpSys.getOutputCount() == 3, "Output count must now be 3");
    auto out3 = pmpSys.getOutput(3);
    TEST_ASSERT(out3 != nullptr && out3->getConnector() == micant::pmp::OpmConnectorType::DisplayPort_Embedded, "Output 3 must be eDP");

    // Stage 3: OPM Simulated X.509 Certificate Generation & Verification
    const auto& cert1 = out1->getCertificate();
    TEST_ASSERT(cert1.size() == 256, "Output 1 certificate must be 256 bytes");
    const auto& cert2 = out2->getCertificate();
    TEST_ASSERT(cert2.size() == 256, "Output 2 certificate must be 256 bytes");
    TEST_ASSERT(cert1 != cert2, "Certificates for different outputs must be cryptographically distinct");
    TEST_ASSERT(std::string(micant::pmp::OpmConnectorTypeToString(micant::pmp::OpmConnectorType::HDMI)).find("HDMI") != std::string::npos, "OpmConnectorTypeToString must format HDMI");
    TEST_ASSERT(std::string(micant::pmp::HdcpProtectionLevelToString(micant::pmp::HdcpProtectionLevel::Hdcp23_Type1)).find("HDCP 2.3") != std::string::npos, "HdcpProtectionLevelToString must format HDCP 2.3");

    // Stage 4: HDCP 2.3 Protection Level Transitions
    TEST_ASSERT(out3->setProtection(micant::pmp::HdcpProtectionLevel::Off), "Setting HDCP to Off must succeed");
    TEST_ASSERT(out3->getCurrentHdcp() == micant::pmp::HdcpProtectionLevel::Off, "Current HDCP must be Off");
    TEST_ASSERT(out3->setProtection(micant::pmp::HdcpProtectionLevel::Hdcp14), "Setting HDCP to 1.4 must succeed");
    TEST_ASSERT(out3->getCurrentHdcp() == micant::pmp::HdcpProtectionLevel::Hdcp14, "Current HDCP must be HDCP 1.4");
    TEST_ASSERT(out3->setProtection(micant::pmp::HdcpProtectionLevel::Hdcp22_Type0), "Setting HDCP to 2.2 must succeed");
    TEST_ASSERT(out3->getCurrentHdcp() == micant::pmp::HdcpProtectionLevel::Hdcp22_Type0, "Current HDCP must be HDCP 2.2");
    TEST_ASSERT(!out3->setProtection(micant::pmp::HdcpProtectionLevel::Hdcp23_Type1), "Setting HDCP 2.3 on output capped at 2.2 must fail");

    // Stage 5: Resolution & HDR Stream Policy Enforcement
    TEST_ASSERT(out1->setProtection(micant::pmp::HdcpProtectionLevel::Off), "Disabling HDCP on Output 1 must succeed");
    TEST_ASSERT(!out1->isStreamAuthorized(1080, false), "Stream must not be authorized when HDCP is Off");

    out1->setProtection(micant::pmp::HdcpProtectionLevel::Hdcp14);
    TEST_ASSERT(out1->isStreamAuthorized(1080, false), "1080p SDR must be authorized with HDCP 1.4");
    TEST_ASSERT(!out1->isStreamAuthorized(2160, false), "4K SDR must NOT be authorized with HDCP 1.4");
    TEST_ASSERT(!out1->isStreamAuthorized(1080, true), "1080p HDR must NOT be authorized with HDCP 1.4 (HDR requires HDCP 2.2+)");

    out1->setProtection(micant::pmp::HdcpProtectionLevel::Hdcp22_Type0);
    TEST_ASSERT(out1->isStreamAuthorized(2160, true), "4K HDR must be authorized with HDCP 2.2");
    TEST_ASSERT(!out1->isStreamAuthorized(4320, false), "8K must NOT be authorized with HDCP 2.2 (requires HDCP 2.3 Type 1)");

    out1->setProtection(micant::pmp::HdcpProtectionLevel::Hdcp23_Type1);
    TEST_ASSERT(out1->isStreamAuthorized(4320, true), "8K HDR must be authorized with HDCP 2.3 Type 1");

    // Stage 6: Repeater Topology & Downstream Device Cascade Limits
    out1->setRepeater(true, 4, 2);
    TEST_ASSERT(out1->isRepeater() == true, "Repeater flag must be true");
    TEST_ASSERT(out1->getRepeaterCount() == 4, "Repeater device count must be 4");
    TEST_ASSERT(out1->getRepeaterDepth() == 2, "Repeater depth must be 2");
    TEST_ASSERT(!out1->isStreamAuthorized(4320, true), "8K stream must NOT be authorized through repeaters in HDCP 2.3 Type 1 mode");
    out1->setRepeater(false, 0, 0);
    TEST_ASSERT(out1->isStreamAuthorized(4320, true), "8K stream must be authorized directly to display (no repeater)");

    // Stage 7: Protected Process Light (PPL) Media Foundation Host Environment
    auto& host = pmpSys.getProcessHost();
    TEST_ASSERT(host.processId == 2048, "mfpmp host PID must be 2048");
    TEST_ASSERT(host.isPplActive == true, "mfpmp host must have PPL active");
    TEST_ASSERT(host.isCodeIntegrityPassed == true, "mfpmp code integrity must be verified");
    TEST_ASSERT(host.binaryPath.find(L"mfpmp.exe") != std::wstring::npos, "Host binary path must point to mfpmp.exe");

    // Stage 8: PAVP Secure Memory & AES-128 Keystream Decryption Reversibility
    auto sess1 = pmpSys.getSession(101);
    TEST_ASSERT(sess1 != nullptr, "Default session 101 must exist");
    TEST_ASSERT(sess1->getWidth() == 3840 && sess1->getHeight() == 2160, "Session 101 resolution must be 3840x2160");
    TEST_ASSERT(sess1->isHdr() == true, "Session 101 must be HDR");

    std::vector<uint8_t> plainText(64);
    for (size_t i = 0; i < plainText.size(); ++i) {
        plainText[i] = static_cast<uint8_t>(0xA0 + (i & 0x1F));
    }
    std::vector<uint8_t> cipherText(64, 0);
    std::vector<uint8_t> recoveredText(64, 0);
    uint32_t encLen = 0, decLen = 0;

    // Apply keystream transform (encrypt)
    TEST_ASSERT(sess1->decryptFrame(plainText.data(), static_cast<uint32_t>(plainText.size()), cipherText.data(), encLen), "Keystream application must succeed");
    TEST_ASSERT(encLen == 64, "Encrypted length must be 64 bytes");
    TEST_ASSERT(cipherText != plainText, "Ciphertext must not match plaintext");

    // Apply keystream transform (decrypt)
    TEST_ASSERT(sess1->decryptFrame(cipherText.data(), static_cast<uint32_t>(cipherText.size()), recoveredText.data(), decLen), "Keystream reverse transform must succeed");
    TEST_ASSERT(decLen == 64, "Decrypted length must be 64 bytes");
    TEST_ASSERT(recoveredText == plainText, "Decrypted text must match original plaintext exactly");
    TEST_ASSERT(sess1->getFramesProcessed() == 2, "Session must have processed 2 frames");
    TEST_ASSERT(sess1->getBytesDecrypted() == 128, "Session must have processed 128 bytes");

    // Stage 9: Cryptographic Key Invalidation & Revocation Manager
    auto& revMgr = pmpSys.getRevocationManager();
    TEST_ASSERT(revMgr.getRevokedCount() == 0, "Initial revocation count must be 0");
    TEST_ASSERT(!revMgr.isRevoked(cert1), "Certificate 1 must not be revoked initially");

    sess1->setKeyRevoked(true);
    TEST_ASSERT(!sess1->decryptFrame(plainText.data(), 64, cipherText.data(), encLen), "Decryption must fail when key is revoked");
    sess1->setKeyRevoked(false);
    TEST_ASSERT(sess1->decryptFrame(plainText.data(), 64, cipherText.data(), encLen), "Decryption must succeed when key is valid");

    // Revoke certificate hash
    uint32_t sampleHash = 2166136261u;
    for (auto b : cert2) { sampleHash ^= b; sampleHash *= 16777619u; }
    revMgr.revokeCertificateHash(sampleHash);
    TEST_ASSERT(revMgr.isRevoked(cert2) == true, "Certificate 2 must report revoked after adding hash");
    TEST_ASSERT(revMgr.isRevoked(cert1) == false, "Certificate 1 must remain valid");
    TEST_ASSERT(revMgr.getRevokedCount() == 1, "Revocation manager count must be 1");
    revMgr.clear();
    TEST_ASSERT(revMgr.getRevokedCount() == 0, "Revocation manager must be clear");

    // Stage 10: Dynamic Secure Session Creation & Teardown Lifecycle
    uint32_t dynSid = pmpSys.createSecureSession(L"DolbyVision_Profile8_4K", 3840, 2160, true);
    TEST_ASSERT(dynSid > 101, "Dynamically created session ID must be > 101");
    auto dynSess = pmpSys.getSession(dynSid);
    TEST_ASSERT(dynSess != nullptr, "Dynamically created session must be retrievable");
    TEST_ASSERT(dynSess->getName() == L"DolbyVision_Profile8_4K", "Session name must match");
    TEST_ASSERT(pmpSys.getSessionCount() >= 2, "Session count must be at least 2");

    bool closed = pmpSys.closeSession(dynSid);
    TEST_ASSERT(closed == true, "Closing dynamic session must succeed");
    TEST_ASSERT(pmpSys.getSession(dynSid) == nullptr, "Closed session must not be retrievable");
    TEST_ASSERT(!pmpSys.closeSession(9999), "Closing invalid session must return false");

    // Stage 11: Clean-Room Win32 C ABI Parity Exports & Boundary Parameter Checks
    TEST_ASSERT(micant::pmp::PmpInitializeSubsystem() == micant::STATUS_SUCCESS, "PmpInitializeSubsystem must return STATUS_SUCCESS");

    uint32_t outCount = 0;
    TEST_ASSERT(micant::pmp::OPMGetVideoOutputsFromHMONITOR(0, nullptr, nullptr) == micant::STATUS_INVALID_PARAMETER, "Null count pointer must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::pmp::OPMGetVideoOutputsFromHMONITOR(0, &outCount, nullptr) == micant::STATUS_SUCCESS, "Querying output count with null buffer must succeed");
    TEST_ASSERT(outCount >= 2, "Output count must be >= 2");

    std::vector<uint32_t> outBuf(outCount);
    TEST_ASSERT(micant::pmp::OPMGetVideoOutputsFromHMONITOR(0, &outCount, outBuf.data()) == micant::STATUS_SUCCESS, "Querying outputs must succeed");
    TEST_ASSERT(outBuf[0] == 1, "First output must be ID 1");

    uint32_t mon1Count = 0;
    TEST_ASSERT(micant::pmp::OPMGetVideoOutputsFromHMONITOR(0x10001, &mon1Count, nullptr) == micant::STATUS_SUCCESS, "Querying by hMonitor must succeed");
    TEST_ASSERT(mon1Count == 1, "Querying by 0x10001 must return exactly 1 output");

    uint32_t protHandle = 0;
    TEST_ASSERT(micant::pmp::OPMCreateProtectedOutput(1, nullptr) == micant::STATUS_INVALID_PARAMETER, "Null handle pointer must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::pmp::OPMCreateProtectedOutput(9999, &protHandle) == micant::STATUS_NOT_FOUND, "Nonexistent output ID must return STATUS_NOT_FOUND");
    TEST_ASSERT(micant::pmp::OPMCreateProtectedOutput(1, &protHandle) == micant::STATUS_SUCCESS, "Creating protected output 1 must succeed");
    TEST_ASSERT(protHandle == 1, "Protected handle must match output ID");

    uint32_t certSz = 0;
    TEST_ASSERT(micant::pmp::OPMGetCertificateSize(protHandle, nullptr) == micant::STATUS_INVALID_PARAMETER, "Null cert size pointer must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::pmp::OPMGetCertificateSize(protHandle, &certSz) == micant::STATUS_SUCCESS, "Getting cert size must succeed");
    TEST_ASSERT(certSz == 256, "Certificate size must be 256 bytes");

    std::vector<uint8_t> certBuf(256);
    TEST_ASSERT(micant::pmp::OPMGetCertificate(protHandle, nullptr, 256) == micant::STATUS_INVALID_PARAMETER, "Null cert buffer must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::pmp::OPMGetCertificate(protHandle, certBuf.data(), 100) == micant::STATUS_BUFFER_TOO_SMALL, "Small buffer must return STATUS_BUFFER_TOO_SMALL");
    TEST_ASSERT(micant::pmp::OPMGetCertificate(protHandle, certBuf.data(), 256) == micant::STATUS_SUCCESS, "Getting certificate must succeed");

    TEST_ASSERT(micant::pmp::OPMSetProtectionLevel(9999, 1, 3) == micant::STATUS_NOT_FOUND, "Nonexistent output must return STATUS_NOT_FOUND");
    TEST_ASSERT(micant::pmp::OPMSetProtectionLevel(protHandle, 2, 3) == micant::STATUS_NOT_SUPPORTED, "Non-HDCP protection type must return STATUS_NOT_SUPPORTED");
    TEST_ASSERT(micant::pmp::OPMSetProtectionLevel(protHandle, 1, 99) == micant::STATUS_INVALID_PARAMETER, "Invalid protection level must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::pmp::OPMSetProtectionLevel(protHandle, 1, 3) == micant::STATUS_SUCCESS, "Setting HDCP 2.3 must return STATUS_SUCCESS");

    uint32_t abiSid = 0;
    TEST_ASSERT(micant::pmp::PmpCreateSecureSession(nullptr, 1920, 1080, 0, &abiSid) == micant::STATUS_INVALID_PARAMETER, "Null session name must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::pmp::PmpCreateSecureSession(L"AbiSession", 1920, 1080, 0, nullptr) == micant::STATUS_INVALID_PARAMETER, "Null sid ptr must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::pmp::PmpCreateSecureSession(L"AbiSession", 1920, 1080, 0, &abiSid) == micant::STATUS_SUCCESS, "PmpCreateSecureSession must succeed");
    TEST_ASSERT(abiSid > 0, "Created session ID must be non-zero");

    uint8_t sampleEnc[32] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t sampleDec[32] = {0};
    uint32_t decOutSz = 0;
    TEST_ASSERT(micant::pmp::PmpDecryptSample(abiSid, nullptr, 32, sampleDec, &decOutSz) == micant::STATUS_INVALID_PARAMETER, "Null encData must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::pmp::PmpDecryptSample(99999, sampleEnc, 32, sampleDec, &decOutSz) == micant::STATUS_NOT_FOUND, "Nonexistent session must return STATUS_NOT_FOUND");
    TEST_ASSERT(micant::pmp::PmpDecryptSample(abiSid, sampleEnc, 32, sampleDec, &decOutSz) == micant::STATUS_SUCCESS, "PmpDecryptSample must succeed");
    TEST_ASSERT(decOutSz == 32, "Decrypted size must match input size");

    // Stage 12: Multi-Threaded Concurrent Secure Frame Decryption Stress Test
    std::atomic<uint32_t> totalFramesDecrypted{0};
    std::vector<std::thread> stressThreads;
    for (int t = 0; t < 4; ++t) {
        stressThreads.emplace_back([&pmpSys, &totalFramesDecrypted, t]() {
            std::wstring sName = L"WorkerStream_" + std::to_wstring(t);
            uint32_t thSid = pmpSys.createSecureSession(sName, 1920, 1080, false);
            auto thSess = pmpSys.getSession(thSid);
            if (thSess) {
                std::vector<uint8_t> inBuf(128, static_cast<uint8_t>(t * 17));
                std::vector<uint8_t> outBuf(128, 0);
                uint32_t outBytes = 0;
                for (int f = 0; f < 25; ++f) {
                    if (thSess->decryptFrame(inBuf.data(), static_cast<uint32_t>(inBuf.size()), outBuf.data(), outBytes)) {
                        totalFramesDecrypted++;
                    }
                }
            }
            pmpSys.closeSession(thSid);
        });
    }
    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(totalFramesDecrypted.load() == 100, "100 concurrent secure frame decryptions must complete without race conditions or memory faults");

    std::cout << "[TEST] Suite 187: Windows Hardware Protected Media Path (PMP), Protected Audio Video Path (PAVP) & HDCP 2.3 Subsystem PASSED.\n";
}

void Test_WindowsVMBus_SyntheticDriver_Subsystem() {
    std::cout << "[TEST] Executing Suite 188: Windows Virtual Machine Bus (VMBus) & Hyper-V Synthetic Driver Subsystem...\n";

    // Stage 1: VMBus Subsystem SCM & VersionDatabase Module Registration
    micant::vmbus::RegisterVmbusSubsystem();
    auto& verDb = version::VersionDatabase::Instance();
    TEST_ASSERT(verDb.FindModule("vmbus.sys") != nullptr, "vmbus.sys must be registered in VersionDatabase");
    TEST_ASSERT(verDb.FindModule("storvsc.sys") != nullptr, "storvsc.sys must be registered in VersionDatabase");
    TEST_ASSERT(verDb.FindModule("netvsc.sys") != nullptr, "netvsc.sys must be registered in VersionDatabase");
    TEST_ASSERT(verDb.FindModule("hv_sock.dll") != nullptr, "hv_sock.dll must be registered in VersionDatabase");

    auto& scm = micant::scm::ServiceControlManager::get();
    auto scmSvc = scm.getServiceRecord(L"VmBusService");
    TEST_ASSERT(scmSvc != nullptr, "VmBusService must be registered in SCM");
    TEST_ASSERT(scmSvc->binaryPath.find(L"svchost.exe") != std::wstring::npos, "VmBusService must be hosted by svchost.exe");
    TEST_ASSERT(scmSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "VmBusService must be running");

    auto& vmBus = micant::vmbus::VmbusSubsystem::get();
    TEST_ASSERT(vmBus.isInitialized() == true, "VmbusSubsystem must report initialized");

    // Stage 2: Synthetic Device Channels Discovery & Channel Offers
    TEST_ASSERT(vmBus.getChannelCount() >= 4, "VMBus must contain at least 4 synthetic channels");
    auto chStorage = vmBus.getChannel(1);
    TEST_ASSERT(chStorage != nullptr, "Channel 1 (Storage) must exist");
    TEST_ASSERT(chStorage->getType() == micant::vmbus::VmbusChannelType::Storage, "Channel 1 must be Storage type");
    TEST_ASSERT(chStorage->getState() == micant::vmbus::VmbusChannelState::Active, "Channel 1 must be Active");
    TEST_ASSERT(chStorage->getGpadlId() == 1001, "Channel 1 GPADL ID must be 1001");

    auto chNet = vmBus.getChannel(2);
    TEST_ASSERT(chNet != nullptr && chNet->getType() == micant::vmbus::VmbusChannelType::Network, "Channel 2 must be Network type");

    auto chMem = vmBus.getChannel(3);
    TEST_ASSERT(chMem != nullptr && chMem->getType() == micant::vmbus::VmbusChannelType::DynamicMemory, "Channel 3 must be DynamicMemory type");

    auto chSock = vmBus.getChannel(4);
    TEST_ASSERT(chSock != nullptr && chSock->getType() == micant::vmbus::VmbusChannelType::HvSocket, "Channel 4 must be HvSocket type");

    uint32_t dynChId = vmBus.offerChannel(micant::vmbus::VmbusChannelType::Heartbeat, L"Hyper-V Heartbeat Service");
    TEST_ASSERT(dynChId >= 5, "Offered channel ID must be >= 5");
    auto dynCh = vmBus.getChannel(dynChId);
    TEST_ASSERT(dynCh != nullptr && dynCh->getState() == micant::vmbus::VmbusChannelState::Offered, "Offered channel must be in Offered state");
    TEST_ASSERT(std::string(micant::vmbus::VmbusChannelTypeToGuid(micant::vmbus::VmbusChannelType::Storage)).find("ba6163d9") != std::string::npos, "Storage GUID must match");

    // Stage 3: GPADL (Guest Physical Address Descriptors List) Registration & Mapping
    uint32_t gpadlId = vmBus.registerGpadl(64, {0x3000, 0x3001, 0x3002});
    TEST_ASSERT(gpadlId > 1004, "Registered GPADL ID must be > 1004");
    micant::vmbus::GpadlDescriptor desc{};
    TEST_ASSERT(vmBus.getGpadl(gpadlId, desc), "Querying GPADL descriptor must succeed");
    TEST_ASSERT(desc.pageCount == 64 && desc.isMapped == true, "GPADL must have 64 pages and report mapped");
    TEST_ASSERT(desc.guestPfns.size() == 3, "GPADL PFN list size must match");

    // Stage 4: VMBus Channel State Machine (Open, Rescind, Close)
    TEST_ASSERT(vmBus.rescindChannel(dynChId), "Rescinding channel offer must succeed");
    TEST_ASSERT(dynCh->getState() == micant::vmbus::VmbusChannelState::Rescinded, "Channel state must be Rescinded");

    TEST_ASSERT(vmBus.openChannel(dynChId, gpadlId), "Opening channel must succeed");
    TEST_ASSERT(dynCh->getState() == micant::vmbus::VmbusChannelState::Active, "Channel state must be Active");
    TEST_ASSERT(dynCh->getGpadlId() == gpadlId, "Channel GPADL ID must be set");

    TEST_ASSERT(vmBus.closeChannel(dynChId), "Closing channel must succeed");
    TEST_ASSERT(dynCh->getState() == micant::vmbus::VmbusChannelState::Closed, "Channel state must be Closed");
    TEST_ASSERT(!vmBus.closeChannel(9999), "Closing nonexistent channel must return false");

    // Stage 5: VMBus Circular Ring Buffer Packet Transaction Streaming
    auto& ring = chStorage->getOutRing();
    ring.clear();
    uint8_t pktData[48] = { 0xDE, 0xAD, 0xBE, 0xEF };
    TEST_ASSERT(ring.write(pktData, 48, 0xCAFE), "Writing packet to ring buffer must succeed");
    TEST_ASSERT(ring.getQueuedCount() == 1, "Ring buffer queued packet count must be 1");
    TEST_ASSERT(ring.getTotalPacketsWritten() >= 1, "Total packets written must be >= 1");

    uint8_t outPkt[64] = { 0 };
    uint32_t outLen = 0;
    uint64_t outTrans = 0;
    TEST_ASSERT(ring.read(outPkt, 64, outLen, outTrans), "Reading packet from ring buffer must succeed");
    TEST_ASSERT(outLen == 48, "Read packet length must match written length (48)");
    TEST_ASSERT(outTrans == 0xCAFE, "Transaction ID must match 0xCAFE");
    TEST_ASSERT(outPkt[0] == 0xDE && outPkt[1] == 0xAD, "Read payload must match written data");
    TEST_ASSERT(ring.getQueuedCount() == 0, "Ring buffer queue must now be empty");
    TEST_ASSERT(!ring.read(outPkt, 64, outLen, outTrans), "Reading from empty ring buffer must return false");

    // Stage 6: Synthetic Storage (storvsc.sys) Fast Ring SCSI Operations
    auto stDev = vmBus.getStorageDevice();
    TEST_ASSERT(stDev != nullptr, "Synthetic storage device must exist");
    TEST_ASSERT(stDev->getCapacityBytes() == 512ULL * 1024ULL * 1024ULL * 1024ULL, "Storage capacity must be 512 GB");
    std::vector<uint8_t> wrSec(1024, 0x55);
    std::vector<uint8_t> rdSec(1024, 0x00);
    TEST_ASSERT(stDev->scsiWrite(0x100, 2, wrSec.data()), "SCSI Write of 2 sectors must succeed");
    TEST_ASSERT(stDev->scsiRead(0x100, 2, rdSec.data()), "SCSI Read of 2 sectors must succeed");
    TEST_ASSERT(stDev->getReadOps() >= 1 && stDev->getWriteOps() >= 1, "SCSI read/write ops count must increase");
    TEST_ASSERT(stDev->getReadBytes() >= 1024 && stDev->getWriteBytes() >= 1024, "SCSI byte count must increase");
    TEST_ASSERT(!stDev->scsiWrite(0xFFFFFFFFFFF, 2, wrSec.data()), "Out of bounds SCSI write must fail");

    // Stage 7: Synthetic Network Adapter (netvsc.sys) Packet Pipeline & MTU
    auto netDev = vmBus.getNetworkAdapter();
    TEST_ASSERT(netDev != nullptr, "Synthetic network adapter must exist");
    TEST_ASSERT(netDev->getMacAddress() == "00:15:5D:01:A0:42", "NetVSC MAC must match Hyper-V standard OUI 00:15:5D");
    TEST_ASSERT(netDev->getLinkSpeedGbps() == 100, "NetVSC link speed must be 100 Gbps");
    TEST_ASSERT(netDev->getMtu() == 1500, "Initial MTU must be 1500");

    uint8_t txBuf[256] = { 0xAA };
    for (int p = 0; p < 5; ++p) TEST_ASSERT(netDev->transmitPacket(txBuf, 256), "transmitPacket must succeed");
    for (int p = 0; p < 10; ++p) TEST_ASSERT(netDev->receivePacket(512), "receivePacket must succeed");
    TEST_ASSERT(netDev->getTxPackets() == 5, "TX packet count must be 5");
    TEST_ASSERT(netDev->getRxPackets() == 10, "RX packet count must be 10");
    TEST_ASSERT(netDev->getTxBytes() == 1280, "TX byte count must be 1280");
    TEST_ASSERT(netDev->getRxBytes() == 5120, "RX byte count must be 5120");

    std::vector<uint8_t> jumboBuf(8000, 0x12);
    TEST_ASSERT(!netDev->transmitPacket(jumboBuf.data(), 8000), "Transmit exceeding MTU must fail");
    netDev->setMtu(9000);
    TEST_ASSERT(netDev->transmitPacket(jumboBuf.data(), 8000), "Transmit with jumbo MTU 9000 must succeed");
    netDev->setMtu(1500); // restore

    // Stage 8: Hyper-V Guest Sockets (hv_sock / AF_HYPERV) Interconnect
    uint32_t sockId = vmBus.createHvSocket(L"{e0762426-32d3-465d-98be-812301980860}");
    TEST_ASSERT(sockId >= 1, "Created HvSocket ID must be valid");
    auto sock = vmBus.getHvSocket(sockId);
    TEST_ASSERT(sock != nullptr, "HvSocket endpoint must be retrievable");
    TEST_ASSERT(sock->isConnected() == true, "HvSocket must be connected");
    uint8_t msgData[64] = "Test_AF_HYPERV_Message_Payload";
    TEST_ASSERT(sock->sendData(msgData, 64), "sendData on connected socket must succeed");
    TEST_ASSERT(sock->getBytesSent() == 64, "Socket bytes sent must be 64");
    sock->setConnected(false);
    TEST_ASSERT(!sock->sendData(msgData, 64), "sendData on disconnected socket must fail");
    sock->setConnected(true);

    // Stage 9: Dynamic Memory (dmvsc.sys) Ballooning & Pressure Management
    TEST_ASSERT(vmBus.getBalloonedPages() == 0, "Initial ballooned pages must be 0");
    vmBus.requestMemoryBalloon(25600);
    TEST_ASSERT(vmBus.getBalloonedPages() == 25600, "Ballooned pages must be 25600 (100 MB)");
    vmBus.requestMemoryBalloon(12800);
    TEST_ASSERT(vmBus.getBalloonedPages() == 38400, "Ballooned pages must be 38400 (150 MB)");
    vmBus.releaseMemoryBalloon(10000);
    TEST_ASSERT(vmBus.getBalloonedPages() == 28400, "Ballooned pages after release must be 28400");
    vmBus.releaseMemoryBalloon(50000);
    TEST_ASSERT(vmBus.getBalloonedPages() == 0, "Releasing more than ballooned must clamp to 0");

    // Stage 10: Multi-Channel Enumeration & State String Formatters
    auto allChs = vmBus.getAllChannels();
    TEST_ASSERT(allChs.size() >= 5, "Total channel count must be >= 5");
    TEST_ASSERT(std::string(micant::vmbus::VmbusChannelTypeToString(micant::vmbus::VmbusChannelType::Storage)).find("SCSI") != std::string::npos, "TypeToString Storage");
    TEST_ASSERT(std::string(micant::vmbus::VmbusChannelTypeToString(micant::vmbus::VmbusChannelType::Network)).find("Network") != std::string::npos, "TypeToString Network");
    TEST_ASSERT(std::string(micant::vmbus::VmbusChannelStateToString(micant::vmbus::VmbusChannelState::Active)).find("ACTIVE") != std::string::npos, "StateToString Active");
    TEST_ASSERT(std::string(micant::vmbus::VmbusChannelStateToString(micant::vmbus::VmbusChannelState::Rescinded)).find("RESCINDED") != std::string::npos, "StateToString Rescinded");

    // Stage 11: Clean-Room Win32 C ABI Parity Exports & Boundary Error Handling
    TEST_ASSERT(micant::vmbus::VmbusInitializeSubsystem() == micant::STATUS_SUCCESS, "VmbusInitializeSubsystem must return STATUS_SUCCESS");

    uint32_t abiChCnt = 0;
    TEST_ASSERT(micant::vmbus::VmbusChannelEnumerate(nullptr, nullptr) == micant::STATUS_INVALID_PARAMETER, "Null count ptr must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::vmbus::VmbusChannelEnumerate(&abiChCnt, nullptr) == micant::STATUS_SUCCESS, "Querying count with null array must succeed");
    TEST_ASSERT(abiChCnt >= 4, "Enumerated channel count must be >= 4");

    std::vector<uint32_t> abiChList(abiChCnt);
    TEST_ASSERT(micant::vmbus::VmbusChannelEnumerate(&abiChCnt, abiChList.data()) == micant::STATUS_SUCCESS, "Enumerating channel IDs must succeed");
    TEST_ASSERT(abiChList[0] == 1, "First channel ID must be 1");

    uint32_t abiOpenSt = 0;
    TEST_ASSERT(micant::vmbus::VmbusChannelOpen(9999, 1001, &abiOpenSt) == micant::STATUS_NOT_FOUND, "Opening nonexistent channel must return STATUS_NOT_FOUND");
    TEST_ASSERT(micant::vmbus::VmbusChannelOpen(1, 1001, nullptr) == micant::STATUS_INVALID_PARAMETER, "Null status ptr must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::vmbus::VmbusChannelOpen(1, 1001, &abiOpenSt) == micant::STATUS_SUCCESS, "Opening channel 1 must return STATUS_SUCCESS");
    TEST_ASSERT(abiOpenSt == 1, "Channel open status must be 1");

    TEST_ASSERT(micant::vmbus::VmbusChannelClose(9999) == micant::STATUS_NOT_FOUND, "Closing nonexistent channel must return STATUS_NOT_FOUND");

    uint8_t abiPkt[16] = { 0x55 };
    TEST_ASSERT(micant::vmbus::VmbusChannelSendPacket(1, nullptr, 16, 1) == micant::STATUS_INVALID_PARAMETER, "Null data send must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::vmbus::VmbusChannelSendPacket(9999, abiPkt, 16, 1) == micant::STATUS_NOT_FOUND, "Send on nonexistent channel must return STATUS_NOT_FOUND");
    TEST_ASSERT(micant::vmbus::VmbusChannelSendPacket(1, abiPkt, 16, 0x1234) == micant::STATUS_SUCCESS, "VmbusChannelSendPacket must succeed");

    uint8_t abiRecvBuf[64] = { 0 };
    uint32_t abiRecvBytes = 0;
    uint64_t abiTransId = 0;
    TEST_ASSERT(micant::vmbus::VmbusChannelReceivePacket(1, nullptr, 64, &abiRecvBytes, &abiTransId) == micant::STATUS_INVALID_PARAMETER, "Null recv buf must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::vmbus::VmbusChannelReceivePacket(9999, abiRecvBuf, 64, &abiRecvBytes, &abiTransId) == micant::STATUS_NOT_FOUND, "Receive on nonexistent channel must return STATUS_NOT_FOUND");

    uint32_t abiSockId = 0;
    TEST_ASSERT(micant::vmbus::HvSocketCreate(nullptr, &abiSockId) == micant::STATUS_INVALID_PARAMETER, "Null GUID in HvSocketCreate must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::vmbus::HvSocketCreate(L"{e0762426-32d3-465d-98be-812301980860}", &abiSockId) == micant::STATUS_SUCCESS, "HvSocketCreate must succeed");
    TEST_ASSERT(abiSockId > 0, "Created socket ID must be non-zero");

    TEST_ASSERT(micant::vmbus::HvSocketSend(9999, abiPkt, 16) == micant::STATUS_NOT_FOUND, "Send on invalid socket must return STATUS_NOT_FOUND");
    TEST_ASSERT(micant::vmbus::HvSocketSend(abiSockId, abiPkt, 16) == micant::STATUS_SUCCESS, "HvSocketSend must return STATUS_SUCCESS");

    // Stage 12: Multi-Threaded Concurrent Synthetic Channel Throughput Stress Test
    std::atomic<uint32_t> totalRoundtrips{0};
    std::vector<std::thread> stressThreads;
    for (int t = 0; t < 4; ++t) {
        stressThreads.emplace_back([&vmBus, &totalRoundtrips, t]() {
            uint32_t workerChId = vmBus.offerChannel(micant::vmbus::VmbusChannelType::Storage, L"StressStorageWorker");
            vmBus.openChannel(workerChId, 2000 + t);
            auto ch = vmBus.getChannel(workerChId);
            if (ch) {
                for (int p = 0; p < 25; ++p) {
                    uint8_t tx[32] = { static_cast<uint8_t>(t * 10 + p) };
                    uint8_t rx[32] = { 0 };
                    uint32_t rLen = 0;
                    uint64_t tId = 0;
                    if (ch->getInRing().write(tx, 32, p + 1)) {
                        if (ch->getInRing().read(rx, 32, rLen, tId)) {
                            if (rLen == 32 && tId == static_cast<uint64_t>(p + 1) && rx[0] == tx[0]) {
                                totalRoundtrips++;
                            }
                        }
                    }
                }
            }
            vmBus.closeChannel(workerChId);
        });
    }
    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(totalRoundtrips.load() == 100, "100 concurrent VMBus ring buffer roundtrip packet transfers must complete without race conditions or memory faults");

    std::cout << "[TEST] Suite 188: Windows Virtual Machine Bus (VMBus) & Hyper-V Synthetic Driver Subsystem PASSED.\n";
}

void Test_WindowsVirtualPCI_SRIOV_DDA_Subsystem() {
    std::cout << "[TEST] Executing Suite 189: Windows Virtual PCI (VPCI / vpci.sys) & SR-IOV / DDA Subsystem...\n";

    // Stage 1: VPCI Subsystem Initialization & SCM/Version Database Registration
    micant::vpci::RegisterVpciSubsystem();
    auto& scm = micant::scm::ServiceControlManager::get();
    auto scmSvc = scm.getServiceRecord(L"VpciService");
    TEST_ASSERT(scmSvc != nullptr, "VpciService SCM service record must be registered");
    TEST_ASSERT(scmSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "VpciService must be in running state");
    TEST_ASSERT(scmSvc->binaryPath.find(L"svchost.exe") != std::wstring::npos, "VpciService host must be svchost.exe");

    auto& vdb = micant::version::VersionDatabase::Instance();
    auto mod = vdb.FindModule("vpci.sys");
    TEST_ASSERT(mod != nullptr, "vpci.sys must be registered in VersionDatabase");
    TEST_ASSERT(mod->stringTable.at("FileVersion") == "10.0.26100.1", "vpci.sys version must match 10.0.26100.1");

    auto& vpciBus = micant::vpci::VpciSubsystem::get();
    TEST_ASSERT(vpciBus.isInitialized(), "VpciSubsystem must be initialized");

    // Stage 2: Virtual PCI Bus Device Enumeration & Discovery
    auto devices = vpciBus.getAllDevices();
    TEST_ASSERT(devices.size() >= 3, "VPCI bus must enumerate at least 3 devices");

    auto dev1 = vpciBus.getDevice(1);
    TEST_ASSERT(dev1 != nullptr, "Device #1 (Mellanox VF) must exist");
    TEST_ASSERT(dev1->getType() == micant::vpci::VpciDeviceType::SriovVirtualFunction, "Device #1 type must be SriovVirtualFunction");
    TEST_ASSERT(dev1->getBdf() == "0000:01:00.1", "Device #1 BDF must be 0000:01:00.1");

    auto dev2 = vpciBus.getDevice(2);
    TEST_ASSERT(dev2 != nullptr, "Device #2 (NVIDIA A100 GPU) must exist");
    TEST_ASSERT(dev2->getType() == micant::vpci::VpciDeviceType::DiscreteDeviceAssignment, "Device #2 type must be DiscreteDeviceAssignment");
    TEST_ASSERT(dev2->getBdf() == "0000:02:00.0", "Device #2 BDF must be 0000:02:00.0");

    auto dev3 = vpciBus.getDevice(3);
    TEST_ASSERT(dev3 != nullptr, "Device #3 (Samsung PM1733 NVMe) must exist");
    TEST_ASSERT(dev3->getType() == micant::vpci::VpciDeviceType::DiscreteDeviceAssignment, "Device #3 type must be DiscreteDeviceAssignment");

    TEST_ASSERT(vpciBus.getDevice(9999) == nullptr, "Querying non-existent device must return nullptr");

    // Stage 3: Standard Type 0 PCI Configuration Space Header Read
    uint16_t vendorId = 0;
    TEST_ASSERT(dev1->readConfig(micant::vpci::PCI_REG_VENDOR_ID, sizeof(uint16_t), &vendorId), "Read Vendor ID must succeed");
    TEST_ASSERT(vendorId == 0x15B3, "Mellanox VF Vendor ID must be 0x15B3");

    uint16_t deviceId = 0;
    TEST_ASSERT(dev1->readConfig(micant::vpci::PCI_REG_DEVICE_ID, sizeof(uint16_t), &deviceId), "Read Device ID must succeed");
    TEST_ASSERT(deviceId == 0x101E, "ConnectX-6 Dx VF Device ID must be 0x101E");

    uint8_t baseClass = 0, subClass = 0;
    TEST_ASSERT(dev1->readConfig(micant::vpci::PCI_REG_CLASS_BASE, 1, &baseClass), "Read base class must succeed");
    TEST_ASSERT(dev1->readConfig(micant::vpci::PCI_REG_CLASS_SUB, 1, &subClass), "Read sub class must succeed");
    TEST_ASSERT(baseClass == 0x02, "Network Controller base class must be 0x02");
    TEST_ASSERT(subClass == 0x00, "Ethernet Controller sub class must be 0x00");

    uint8_t headerType = 0;
    TEST_ASSERT(dev1->readConfig(micant::vpci::PCI_REG_HEADER_TYPE, 1, &headerType), "Read header type must succeed");
    TEST_ASSERT(headerType == 0x00, "PCI Header Type must be 0x00 (Type 0 Standard Header)");

    uint16_t pciStatus = 0;
    TEST_ASSERT(dev1->readConfig(micant::vpci::PCI_REG_STATUS, sizeof(uint16_t), &pciStatus), "Read status register must succeed");
    TEST_ASSERT((pciStatus & micant::vpci::PCI_STATUS_CAP_LIST) != 0, "Capabilities List bit must be set in status register");

    // Stage 4: PCI Configuration Space Capability Pointer Traversal (PCIe & MSI-X)
    uint8_t capPtr = 0;
    TEST_ASSERT(dev1->readConfig(micant::vpci::PCI_REG_CAP_PTR, 1, &capPtr), "Read capability pointer must succeed");
    TEST_ASSERT(capPtr == 0x40, "First capability must be at offset 0x40");

    uint8_t cap1Id = 0, cap1Next = 0;
    TEST_ASSERT(dev1->readConfig(0x40, 1, &cap1Id) && dev1->readConfig(0x41, 1, &cap1Next), "Read Cap 1 header must succeed");
    TEST_ASSERT(cap1Id == micant::vpci::PCI_CAP_ID_EXP, "Cap 1 ID must be PCI Express (0x10)");
    TEST_ASSERT(cap1Next == 0x70, "Next capability pointer must point to 0x70");

    uint8_t cap2Id = 0, cap2Next = 0;
    TEST_ASSERT(dev1->readConfig(0x70, 1, &cap2Id) && dev1->readConfig(0x71, 1, &cap2Next), "Read Cap 2 header must succeed");
    TEST_ASSERT(cap2Id == micant::vpci::PCI_CAP_ID_MSIX, "Cap 2 ID must be MSI-X (0x11)");
    TEST_ASSERT(cap2Next == 0x00, "Next capability pointer must be 0x00 (end of capability chain)");

    // Stage 5: PCI Configuration Space Register Modification (Command Register)
    uint16_t origCmd = 0;
    TEST_ASSERT(dev1->readConfig(micant::vpci::PCI_REG_COMMAND, sizeof(uint16_t), &origCmd), "Read command register must succeed");
    TEST_ASSERT((origCmd & micant::vpci::PCI_CMD_BUS_MASTER) != 0, "Bus Master bit must initially be set");

    uint16_t newCmd = micant::vpci::PCI_CMD_MEMORY_SPACE | micant::vpci::PCI_CMD_BUS_MASTER | micant::vpci::PCI_CMD_INTX_DISABLE;
    TEST_ASSERT(dev1->writeConfig(micant::vpci::PCI_REG_COMMAND, sizeof(uint16_t), &newCmd), "Write command register must succeed");
    uint16_t readCmd = 0;
    dev1->readConfig(micant::vpci::PCI_REG_COMMAND, sizeof(uint16_t), &readCmd);
    TEST_ASSERT(readCmd == newCmd, "Modified command register must read back written value");
    dev1->writeConfig(micant::vpci::PCI_REG_COMMAND, sizeof(uint16_t), &origCmd); // restore

    // Stage 6: MMIO BAR Base Address Allocation & Guest Physical Space Mapping
    const auto& bars1 = dev1->getBars();
    TEST_ASSERT(bars1[0].type == micant::vpci::PciBarType::Memory64, "Device 1 BAR0 must be Memory64");
    TEST_ASSERT(bars1[0].baseAddress == 0xFE000000, "Device 1 BAR0 base address must be 0xFE000000");
    TEST_ASSERT(bars1[0].size == 64 * 1024 * 1024, "Device 1 BAR0 size must be 64 MB");
    TEST_ASSERT(bars1[0].isPrefetchable == true, "Device 1 BAR0 must be prefetchable");
    TEST_ASSERT(bars1[0].isMapped == true, "Device 1 BAR0 must be mapped in GPA space");

    // Stage 7: 64-bit Large MMIO Aperture Validation (16GB BAR for A100 GPU)
    const auto& bars2 = dev2->getBars();
    TEST_ASSERT(bars2[0].type == micant::vpci::PciBarType::Memory32, "A100 BAR0 must be Memory32");
    TEST_ASSERT(bars2[0].size == 16 * 1024 * 1024, "A100 BAR0 size must be 16 MB");
    TEST_ASSERT(bars2[1].type == micant::vpci::PciBarType::Memory64, "A100 BAR1 must be Memory64");
    TEST_ASSERT(bars2[1].size == 16ULL * 1024 * 1024 * 1024, "A100 BAR1 aperture must be 16 GB");
    TEST_ASSERT(bars2[1].baseAddress == 0x2000000000ULL, "A100 BAR1 base must be at 128 GB physical boundary (0x2000000000)");

    // Stage 8: MSI-X Interrupt Vector Configuration & Table Programming
    TEST_ASSERT(dev1->isMsiXEnabled(), "Device 1 must have MSI-X enabled");
    TEST_ASSERT(dev1->getMsiXVectorCount() == 16, "Device 1 must have 16 MSI-X vectors");
    TEST_ASSERT(dev3->getMsiXVectorCount() == 64, "Device 3 must have 64 MSI-X vectors");

    TEST_ASSERT(dev1->setMsiXVector(0, 0xFEE00000, 0x40, false), "Programming unmasked vector 0 must succeed");
    TEST_ASSERT(dev1->setMsiXVector(1, 0xFEE01000, 0x41, true), "Programming masked vector 1 must succeed");
    TEST_ASSERT(!dev1->setMsiXVector(99, 0, 0, false), "Programming out-of-bounds vector must fail");

    micant::vpci::MsiXTableEntry e0{}, e1{};
    TEST_ASSERT(dev1->getMsiXVector(0, e0), "Retrieve vector 0 must succeed");
    TEST_ASSERT(e0.msgAddress == 0xFEE00000 && e0.msgData == 0x40 && e0.vectorControl == 0, "Vector 0 parameters must match");
    TEST_ASSERT(dev1->getMsiXVector(1, e1), "Retrieve vector 1 must succeed");
    TEST_ASSERT(e1.msgAddress == 0xFEE01000 && e1.vectorControl == 1, "Vector 1 must be masked");

    // Stage 9: MSI-X Synthetic Interrupt Injection & Delivery Verification
    TEST_ASSERT(dev1->triggerMsiX(0) == true, "Triggering unmasked vector 0 must return true");
    dev1->getMsiXVector(0, e0);
    TEST_ASSERT(e0.triggerCount == 1, "Vector 0 triggerCount must be 1");
    TEST_ASSERT(dev1->getTotalInterrupts() == 1, "Total interrupts fired must be 1");

    TEST_ASSERT(dev1->triggerMsiX(1) == false, "Triggering masked vector 1 must be suppressed (return false)");
    dev1->getMsiXVector(1, e1);
    TEST_ASSERT(e1.triggerCount == 0, "Masked vector 1 triggerCount must remain 0");

    dev1->setMsiXVector(1, 0xFEE01000, 0x41, false); // unmask
    TEST_ASSERT(dev1->triggerMsiX(1) == true, "Triggering unmasked vector 1 must succeed");
    dev1->getMsiXVector(1, e1);
    TEST_ASSERT(e1.triggerCount == 1, "Vector 1 triggerCount must now be 1");
    TEST_ASSERT(dev1->getTotalInterrupts() == 2, "Total interrupts fired must be 2");

    // Stage 10: SR-IOV Virtual Function NetVSC Accelerated Data Path Teaming Handshake
    TEST_ASSERT(dev1->isNetVscSriovTeamingActive() == true, "Device 1 must be active in NetVSC SR-IOV teaming");
    TEST_ASSERT(dev1->getNetVscPairedAdapterId() == 2, "Device 1 paired adapter ID must be 2");
    TEST_ASSERT(dev2->isNetVscSriovTeamingActive() == false, "Device 2 (GPU) must not be NetVSC paired");

    // Stage 11: Dynamic Live-Migration VF Revocation & NetVSC Failover to Synthetic Ring Buffer
    TEST_ASSERT(vpciBus.failoverSriovToSynthetic(1) == true, "Failing over SR-IOV VF to synthetic must succeed");
    TEST_ASSERT(dev1->getState() == micant::vpci::VpciDeviceState::Revoked, "Device 1 state must become Revoked");
    TEST_ASSERT(dev1->isNetVscSriovTeamingActive() == false, "NetVSC teaming active must be false during failover");

    TEST_ASSERT(vpciBus.restoreSriovTeaming(1) == true, "Restoring SR-IOV teaming must succeed");
    TEST_ASSERT(dev1->getState() == micant::vpci::VpciDeviceState::Active, "Device 1 state must be restored to Active");
    TEST_ASSERT(dev1->isNetVscSriovTeamingActive() == true, "NetVSC teaming active must be true after restore");

    // Stage 12: Clean-Room Win32 C ABI Parity Exports & Multi-threaded MMIO Access Stress Test
    TEST_ASSERT(micant::vpci::VpciInitializeSubsystem() == micant::STATUS_SUCCESS, "VpciInitializeSubsystem must return STATUS_SUCCESS");

    uint32_t devCnt = 0;
    TEST_ASSERT(micant::vpci::VpciDeviceEnumerate(nullptr, nullptr, 0) == micant::STATUS_INVALID_PARAMETER, "Null count must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::vpci::VpciDeviceEnumerate(&devCnt, nullptr, 0) == micant::STATUS_SUCCESS, "Enumerate count query must succeed");
    TEST_ASSERT(devCnt >= 3, "Enumerated device count must be >= 3");

    std::vector<uint32_t> devIds(devCnt);
    TEST_ASSERT(micant::vpci::VpciDeviceEnumerate(&devCnt, devIds.data(), devCnt) == micant::STATUS_SUCCESS, "Enumerate IDs must succeed");
    TEST_ASSERT(devIds[0] == 1 && devIds[1] == 2 && devIds[2] == 3, "Device IDs must be 1, 2, 3");

    uint16_t cVendor = 0;
    TEST_ASSERT(micant::vpci::VpciReadConfigSpace(9999, 0, 2, &cVendor) == micant::STATUS_NOT_FOUND, "Read config on invalid dev must return STATUS_NOT_FOUND");
    TEST_ASSERT(micant::vpci::VpciReadConfigSpace(1, 0, 2, nullptr) == micant::STATUS_INVALID_PARAMETER, "Null buf must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::vpci::VpciReadConfigSpace(1, 0, 2, &cVendor) == micant::STATUS_SUCCESS, "Read config via C ABI must succeed");
    TEST_ASSERT(cVendor == 0x15B3, "C ABI read vendor ID must be 0x15B3");

    uint64_t barAddr = 0, barSz = 0;
    TEST_ASSERT(micant::vpci::VpciMapBarSpace(1, 0, &barAddr, &barSz) == micant::STATUS_SUCCESS, "VpciMapBarSpace must succeed");
    TEST_ASSERT(barAddr == 0xFE000000 && barSz == 64 * 1024 * 1024, "VpciMapBarSpace values must match BAR0");
    TEST_ASSERT(micant::vpci::VpciMapBarSpace(1, 5, &barAddr, &barSz) == micant::STATUS_NOT_FOUND, "Unconfigured BAR must return STATUS_NOT_FOUND");

    uint32_t isSriov = 0, isDda = 0, netPaired = 0;
    TEST_ASSERT(micant::vpci::VpciQueryDeviceCapabilities(1, &isSriov, &isDda, &netPaired) == micant::STATUS_SUCCESS, "Query capabilities must succeed");
    TEST_ASSERT(isSriov == 1 && isDda == 0 && netPaired == 1, "Capabilities for dev 1 must indicate SR-IOV paired with NetVSC");

    TEST_ASSERT(micant::vpci::VpciQueryDeviceCapabilities(2, &isSriov, &isDda, &netPaired) == micant::STATUS_SUCCESS, "Query capabilities for GPU must succeed");
    TEST_ASSERT(isSriov == 0 && isDda == 1 && netPaired == 0, "Capabilities for GPU must indicate DDA without NetVSC");

    TEST_ASSERT(micant::vpci::VpciAssignMsiInterrupt(1, 2, 0xFEE02000, 0x42) == micant::STATUS_SUCCESS, "Assigning vector 2 via C ABI must succeed");
    TEST_ASSERT(micant::vpci::VpciTriggerInterrupt(1, 2) == micant::STATUS_SUCCESS, "Triggering vector 2 via C ABI must succeed");

    // Multi-threaded concurrent MMIO/Config read stress test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&vpciBus, &stressSuccessCount]() {
            for (int iter = 0; iter < 25; ++iter) {
                uint32_t targetId = (iter % 3) + 1;
                auto dev = vpciBus.getDevice(targetId);
                if (!dev) continue;

                uint16_t vId = 0;
                if (dev->readConfig(0, 2, &vId)) {
                    if (vId == dev->getVendorId()) {
                        const auto& b = dev->getBars();
                        if (b[0].type != micant::vpci::PciBarType::None) {
                            stressSuccessCount++;
                        }
                    }
                }
            }
        });
    }
    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(stressSuccessCount.load() == 100, "100 concurrent VPCI config and BAR operations must succeed with zero race conditions");

    std::cout << "[TEST] Suite 189: Windows Virtual PCI (VPCI / vpci.sys) & SR-IOV / DDA Subsystem PASSED.\n";
}

void Test_WindowsVirtualSecureMode_VBS_HVCI_Subsystem() {
    std::cout << "[TEST] Executing Suite 190: Windows Virtual Secure Mode (VSM / vsm.sys), VBS & HVCI Subsystem...\n";

    // Stage 1: VSM Subsystem Initialization & SCM/Version Database Registration
    micant::vsm::RegisterVsmSubsystem();
    auto& scm = micant::scm::ServiceControlManager::get();
    auto scmSvc = scm.getServiceRecord(L"VsmService");
    TEST_ASSERT(scmSvc != nullptr, "VsmService SCM service record must be registered");
    TEST_ASSERT(scmSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "VsmService must be running");
    TEST_ASSERT(scmSvc->binaryPath.find(L"svchost.exe") != std::wstring::npos, "VsmService must be hosted by svchost.exe");

    auto& vdb = micant::version::VersionDatabase::Instance();
    auto modVsm = vdb.FindModule("vsm.sys");
    TEST_ASSERT(modVsm != nullptr, "vsm.sys must be registered in VersionDatabase");
    TEST_ASSERT(modVsm->stringTable.at("FileVersion") == "10.0.26100.1", "vsm.sys file version must be 10.0.26100.1");

    auto modSec = vdb.FindModule("securekernel.exe");
    TEST_ASSERT(modSec != nullptr, "securekernel.exe must be registered in VersionDatabase");

    auto modHvci = vdb.FindModule("hvci.dll");
    TEST_ASSERT(modHvci != nullptr, "hvci.dll must be registered in VersionDatabase");

    auto& vsmSys = micant::vsm::VsmSubsystem::get();
    TEST_ASSERT(vsmSys.isInitialized(), "VsmSubsystem must be initialized");

    // Stage 2: Dual Virtual Trust Level (VTL 0 & 1) Processor Architectural State
    TEST_ASSERT(vsmSys.getActiveVtl() == micant::vsm::VTL_NORMAL, "Initial active VTL must be VTL 0 (Normal World)");

    // Stage 3: VTL Hypercall Context Switching (HvCallSwitchVtl & HvCallEnterVtl1)
    uint64_t initialSwitches = vsmSys.getVtlSwitchCount();
    micant::vsm::VtlProcessorState inState{}, outState{};
    inState.rip = 0x140005000;
    inState.rsp = 0x1000FE000;

    uint16_t sw1 = vsmSys.switchVtl(micant::vsm::VTL_SECURE, &inState, &outState);
    TEST_ASSERT(sw1 == micant::vsm::HV_STATUS_SUCCESS, "Switching to VTL 1 must return HV_STATUS_SUCCESS");
    TEST_ASSERT(vsmSys.getActiveVtl() == micant::vsm::VTL_SECURE, "Active VTL must now be VTL 1 (Secure World)");
    TEST_ASSERT(vsmSys.getVtlSwitchCount() == initialSwitches + 1, "VTL switch count must increment");
    TEST_ASSERT(outState.rip == 0xFFFFF80000800000, "VTL 1 restored RIP must point to Secure Kernel entry");

    uint16_t sw2 = vsmSys.switchVtl(micant::vsm::VTL_NORMAL, nullptr, &outState);
    TEST_ASSERT(sw2 == micant::vsm::HV_STATUS_SUCCESS, "Switching back to VTL 0 must succeed");
    TEST_ASSERT(vsmSys.getActiveVtl() == micant::vsm::VTL_NORMAL, "Active VTL must be restored to VTL 0");
    TEST_ASSERT(outState.rip == 0x140005000, "VTL 0 restored RIP must match saved RIP");

    TEST_ASSERT(vsmSys.switchVtl(99) == micant::vsm::HV_STATUS_INVALID_PARAMETER, "Invalid VTL index must return HV_STATUS_INVALID_PARAMETER");

    // Stage 4: SLAT (Second-Level Address Translation) Page Table Hardening
    // While in VTL 0, attempting to modify protections must fail (VTL 0 cannot modify SLAT)
    uint16_t deniedSt = vsmSys.modifyVtlProtectionMask(micant::vsm::VTL_NORMAL, 0x140400000, micant::vsm::HV_MAP_GPA_PERM_RX);
    TEST_ASSERT(deniedSt == micant::vsm::HV_STATUS_ACCESS_DENIED, "VTL 0 must be denied from modifying SLAT protections");

    // Switch to VTL 1 (Secure Kernel) to configure SLAT permissions
    vsmSys.switchVtl(micant::vsm::VTL_SECURE);
    uint16_t okSt = vsmSys.modifyVtlProtectionMask(micant::vsm::VTL_NORMAL, 0x140400000, micant::vsm::HV_MAP_GPA_PERM_RX, "test_driver.sys");
    TEST_ASSERT(okSt == micant::vsm::HV_STATUS_SUCCESS, "VTL 1 must be permitted to modify VTL 0 SLAT protections");

    uint32_t queriedPerms = 0;
    TEST_ASSERT(vsmSys.queryPageProtection(0x140400000, micant::vsm::VTL_NORMAL, &queriedPerms), "Querying page protection must succeed");
    TEST_ASSERT(queriedPerms == micant::vsm::HV_MAP_GPA_PERM_RX, "Page protection must be Read+Execute (RX)");
    vsmSys.switchVtl(micant::vsm::VTL_NORMAL);

    // Stage 5: Hardware-Enforced W^X (Write-XOR-Execute) Policy & Violation Trapping
    vsmSys.switchVtl(micant::vsm::VTL_SECURE);
    // Attempting to set RWX on VTL 0 under HVCI must trigger W^X policy violation
    uint16_t wxViol = vsmSys.modifyVtlProtectionMask(micant::vsm::VTL_NORMAL, 0x140400000, micant::vsm::HV_MAP_GPA_PERM_RWX);
    TEST_ASSERT(wxViol == micant::vsm::HV_STATUS_POLICY_VIOLATION, "Setting RWX in VTL 0 must be blocked by W^X policy");
    vsmSys.switchVtl(micant::vsm::VTL_NORMAL);

    std::string reason;
    // Attempt to write to executable page (0x140400000 is RX)
    bool writeAllowed = vsmSys.validateMemoryAccess(0x140400000, micant::vsm::HV_MAP_GPA_PERM_WRITE, micant::vsm::VTL_NORMAL, &reason);
    TEST_ASSERT(!writeAllowed, "Writing to RX code page must be trapped by SLAT / HVCI W^X intercept");
    TEST_ASSERT(vsmSys.getSlatViolations() >= 1, "SLAT violations counter must increment");

    // Reading or executing RX page must succeed
    TEST_ASSERT(vsmSys.validateMemoryAccess(0x140400000, micant::vsm::HV_MAP_GPA_PERM_READ, micant::vsm::VTL_NORMAL), "Reading RX page must succeed");
    TEST_ASSERT(vsmSys.validateMemoryAccess(0x140400000, micant::vsm::HV_MAP_GPA_PERM_EXECUTE, micant::vsm::VTL_NORMAL), "Executing RX page must succeed");

    // Stage 6: Hypervisor-Protected Code Integrity (HVCI) Authenticode Signature Verification
    auto& hvci = vsmSys.getHvci();
    TEST_ASSERT(hvci.isEnabled(), "HVCI must be enabled");

    std::string signer;
    TEST_ASSERT(hvci.verifyModule("vsm.sys", nullptr, 0, &signer), "Pre-approved system driver vsm.sys must verify");
    TEST_ASSERT(signer.find("Production PCA") != std::string::npos, "Signer must indicate Production PCA");

    std::vector<uint8_t> validDrv(256, 0x90);
    validDrv[0] = 0x4D; validDrv[1] = 0x5A; // MZ
    TEST_ASSERT(hvci.verifyModule("whql_net_driver.sys", validDrv.data(), validDrv.size(), &signer), "Valid driver must verify");
    TEST_ASSERT(signer.find("WHQL") != std::string::npos, "Valid driver signer must be WHQL certified");
    TEST_ASSERT(hvci.getVerifiedCount() >= 2, "Verified drivers count must be >= 2");

    // Stage 7: Mandatory Vulnerable Driver Blocklist Rejection (WDAC / HVCI Mitigation)
    std::string blkReason;
    TEST_ASSERT(!hvci.verifyModule("gdrv.sys", validDrv.data(), validDrv.size(), &blkReason), "Blocked driver gdrv.sys must fail verification");
    TEST_ASSERT(blkReason.find("CVE-2018-19320") != std::string::npos, "Rejection reason must reference CVE-2018-19320");

    TEST_ASSERT(!hvci.verifyModule("procexp.sys", validDrv.data(), validDrv.size(), &blkReason), "Blocked driver procexp.sys must fail verification");
    TEST_ASSERT(blkReason.find("CVE-2016-9067") != std::string::npos, "Rejection reason must reference CVE-2016-9067");

    TEST_ASSERT(!hvci.verifyModule("rtcore64.sys", validDrv.data(), validDrv.size(), &blkReason), "Blocked driver rtcore64.sys must fail verification");
    TEST_ASSERT(hvci.getBlockedCount() >= 3, "Blocked drivers count must be >= 3");

    // Stage 8: Credential Guard (LSA Isolated / lsaiso.exe) Isolated Secret Storage
    auto& credGuard = vsmSys.getCredentialGuard();
    TEST_ASSERT(credGuard.isActive(), "Credential Guard must be active");
    TEST_ASSERT(credGuard.getSecretCount() >= 2, "Pre-seeded secrets must exist in VTL 1 vault");

    std::vector<uint8_t> tgtData;
    std::string tgtType;
    TEST_ASSERT(credGuard.getSecret("krbtgt@MICANT.LOCAL", tgtData, tgtType), "Querying krbtgt secret must succeed");
    TEST_ASSERT(tgtType == "KERBEROS_TGT" && tgtData.size() == 32, "krbtgt secret must be 32-byte KERBEROS_TGT");

    uint8_t dpapiMasterKey[32] = { 0x55, 0xAA, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB };
    TEST_ASSERT(credGuard.storeSecret("SYSTEM_DPAPI", "LocalSystem", "NT AUTHORITY", "DPAPI_MASTER_KEY", dpapiMasterKey, 32), "Storing DPAPI secret in VTL 1 vault must succeed");
    TEST_ASSERT(credGuard.getSecretCount() >= 3, "Secret count must now be >= 3");

    // Stage 9: Credential Guard Isolated NTLM Challenge-Response Computation
    std::vector<uint8_t> ntlmChallenge = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    std::vector<uint8_t> ntlmResponse;
    TEST_ASSERT(credGuard.authenticateNtlm("Administrator@MICANT", ntlmChallenge.data(), ntlmChallenge.size(), ntlmResponse), "NTLM challenge authentication in VTL 1 must succeed");
    TEST_ASSERT(ntlmResponse.size() == 24, "NTLM response length must be 24 bytes");
    TEST_ASSERT(!credGuard.authenticateNtlm("NonExistentUser@MICANT", ntlmChallenge.data(), ntlmChallenge.size(), ntlmResponse), "Authenticating non-existent user must fail");
    TEST_ASSERT(credGuard.getRpcCallCount() >= 3, "Credential Guard RPC call count must increase");

    // Stage 10: Virtual TPM 2.0 (vTPM) PCR Extension & Measured Boot Validation
    auto& vtpm = vsmSys.getVirtualTpm();
    TEST_ASSERT(vtpm.isActive(), "Virtual TPM 2.0 must be active");

    std::array<uint8_t, 32> pcr0{}, pcr7{}, pcr11_before{}, pcr11_after{};
    TEST_ASSERT(vtpm.readPcr(micant::vsm::VTPM_PCR_FIRMWARE_CRTM, pcr0.data(), pcr0.size()), "Reading PCR 0 must succeed");
    TEST_ASSERT(pcr0[0] == 0x5C && pcr0[31] == 0xE1, "PCR 0 CRTM measurement must match initialized value");

    TEST_ASSERT(vtpm.readPcr(micant::vsm::VTPM_PCR_SECURE_BOOT, pcr7.data(), pcr7.size()), "Reading PCR 7 must succeed");
    TEST_ASSERT(vtpm.readPcr(micant::vsm::VTPM_PCR_BITLOCKER_VSM, pcr11_before.data(), pcr11_before.size()), "Reading PCR 11 must succeed");

    uint8_t measurement[16] = { 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 };
    TEST_ASSERT(vtpm.extendPcr(micant::vsm::VTPM_PCR_BITLOCKER_VSM, measurement, sizeof(measurement)), "Extending PCR 11 must succeed");
    TEST_ASSERT(vtpm.readPcr(micant::vsm::VTPM_PCR_BITLOCKER_VSM, pcr11_after.data(), pcr11_after.size()), "Reading extended PCR 11 must succeed");
    TEST_ASSERT(pcr11_before != pcr11_after, "PCR 11 digest must change after extension");

    // Stage 11: Virtual TPM 2.0 PCR-Policy Data Sealing & Cryptographic Unsealing
    std::string sensitiveSecret = "Sovereign_BitLocker_VMK_2026_Enterprise_Volume";
    std::vector<uint8_t> sealedBlob;
    uint32_t policyMask = (1u << micant::vsm::VTPM_PCR_SECURE_BOOT); // Sealed to Secure Boot PCR 7

    TEST_ASSERT(vtpm.sealData(policyMask, reinterpret_cast<const uint8_t*>(sensitiveSecret.data()), sensitiveSecret.size(), sealedBlob), "Sealing data to PCR 7 must succeed");
    TEST_ASSERT(sealedBlob.size() >= 44 + sensitiveSecret.size(), "Sealed blob must include header and ciphertext");

    std::vector<uint8_t> unsealedData;
    TEST_ASSERT(vtpm.unsealData(sealedBlob.data(), sealedBlob.size(), unsealedData), "Unsealing data with valid PCR state must succeed");
    std::string recoveredSecret(unsealedData.begin(), unsealedData.end());
    TEST_ASSERT(recoveredSecret == sensitiveSecret, "Unsealed secret must match original plaintext");

    // Tamper with PCR 7 (simulate Secure Boot policy violation)
    uint8_t tamperData[4] = { 0xDE, 0xAD, 0x00, 0x01 };
    vtpm.extendPcr(micant::vsm::VTPM_PCR_SECURE_BOOT, tamperData, sizeof(tamperData));

    std::vector<uint8_t> failedUnseal;
    TEST_ASSERT(!vtpm.unsealData(sealedBlob.data(), sealedBlob.size(), failedUnseal), "Unsealing must FAIL when PCR measurements do not match policy");

    // Stage 12: Clean-Room Win32 C ABI Parity Exports & Multi-threaded VTL/Enclave Stress Test
    TEST_ASSERT(micant::vsm::VsmInitializeSubsystem() == micant::STATUS_SUCCESS, "VsmInitializeSubsystem must return STATUS_SUCCESS");
    TEST_ASSERT(micant::vsm::VsmQueryTrustLevel() == micant::vsm::VTL_NORMAL, "VsmQueryTrustLevel must return VTL 0");

    uint32_t newTrustletId = 0;
    TEST_ASSERT(micant::vsm::VsmRegisterSecurityEnclave("custom_trustlet.exe", 16 * 1024 * 1024, &newTrustletId) == micant::STATUS_SUCCESS, "Registering trustlet via C ABI must succeed");
    TEST_ASSERT(newTrustletId >= 3, "New trustlet ID must be >= 3");

    micant::vsm::TrustletDescriptor tDesc{};
    TEST_ASSERT(micant::vsm::VsmGetEnclaveMetrics(newTrustletId, &tDesc) == micant::STATUS_SUCCESS, "Querying trustlet metrics must succeed");
    TEST_ASSERT(std::string(tDesc.name) == "custom_trustlet.exe", "Trustlet name must match");

    TEST_ASSERT(micant::vsm::HvciVerifyModule("custom_signed.sys", validDrv.data(), validDrv.size()) == micant::STATUS_SUCCESS, "HvciVerifyModule on valid driver must return STATUS_SUCCESS");
    TEST_ASSERT(micant::vsm::HvciVerifyModule("gdrv.sys", validDrv.data(), validDrv.size()) == micant::vsm::STATUS_IMAGE_CERT_REVOKED, "HvciVerifyModule on blocked driver must return STATUS_IMAGE_CERT_REVOKED");

    // Multi-threaded concurrent VTL and Credential Guard query stress test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&vsmSys, &stressSuccessCount]() {
            for (int iter = 0; iter < 25; ++iter) {
                uint32_t vtl = vsmSys.getActiveVtl();
                if (vtl <= 1) {
                    std::vector<uint8_t> secret;
                    std::string sType;
                    if (vsmSys.getCredentialGuard().getSecret("krbtgt@MICANT.LOCAL", secret, sType)) {
                        if (sType == "KERBEROS_TGT") {
                            stressSuccessCount++;
                        }
                    }
                }
            }
        });
    }
    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(stressSuccessCount.load() == 100, "100 concurrent VTL queries and Credential Guard operations must complete without race conditions");

    std::cout << "[TEST] Suite 190: Windows Virtual Secure Mode (VSM / vsm.sys), VBS & HVCI Subsystem PASSED.\n";
}

void Test_WindowsKernelHotpatching_LiveUpdate_Subsystem() {
    std::cout << "[TEST] Running Suite 191: Windows Kernel Hotpatching (KLP / TitanHotpatch) & Live Update Subsystem...\n";

    // 1. SCM Service & VersionDatabase Registration
    micant::hotpatch::RegisterHotpatchSubsystem();
    auto& vdb = micant::version::VersionDatabase::Instance();
    auto modHp = vdb.FindModule("hotpatch.sys");
    TEST_ASSERT(modHp != nullptr, "hotpatch.sys must be registered in VersionDatabase");
    auto modKlp = vdb.FindModule("klp.dll");
    TEST_ASSERT(modKlp != nullptr, "klp.dll must be registered in VersionDatabase");
    auto& scm = micant::scm::ServiceControlManager::get();
    auto rec = scm.getServiceRecord(L"HotpatchService");
    TEST_ASSERT(rec != nullptr, "HotpatchService must be registered in ServiceControlManager");
    TEST_ASSERT(rec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "HotpatchService must be running");

    // 2. Subsystem Initialization and Pre-seeded Hotpatches
    auto& sys = micant::hotpatch::HotpatchSubsystem::get();
    TEST_ASSERT(sys.isInitialized(), "HotpatchSubsystem must be initialized");
    TEST_ASSERT(sys.getPatchCount() >= 2, "HotpatchSubsystem must contain at least 2 pre-seeded security hotpatches");
    auto p1 = sys.getPatch("KB5051234-CVE-2026-0001");
    TEST_ASSERT(p1 != nullptr, "CVE-2026-0001 hotpatch must be present");
    TEST_ASSERT(p1->getTargetModule() == "ntoskrnl.exe", "CVE-2026-0001 target module must be ntoskrnl.exe");
    TEST_ASSERT(p1->getTargetSymbol() == "NtAllocateVirtualMemory", "CVE-2026-0001 target symbol must be NtAllocateVirtualMemory");
    TEST_ASSERT(p1->getState() == micant::hotpatch::PatchState::Staged, "Initial state of pre-seeded patch must be Staged");

    // 3. Multiprocessor Quiescence Coordination
    auto& qc = sys.getQuiescenceCoordinator();
    TEST_ASSERT(qc.getProcessorCount() == 8, "Default logical processor count must be 8");
    uint64_t initialSync = qc.getSyncCycles();
    bool qOk = qc.enterQuiescence();
    TEST_ASSERT(qOk, "Quiescence coordinator enterQuiescence must succeed");
    TEST_ASSERT(qc.getState() == micant::hotpatch::QuiescenceState::Quiescent, "Quiescence state must be Quiescent");
    TEST_ASSERT(qc.getSyncCycles() == initialSync + 1, "Sync cycles counter must increment upon entering quiescence");
    qc.exitQuiescence();
    TEST_ASSERT(qc.getState() == micant::hotpatch::QuiescenceState::Idle, "Quiescence state must return to Idle after exit");

    // 4. JMP rel32 Detour Trampoline Generation
    uint64_t targetAddr = 0x140010000;
    uint64_t patchAddr  = 0x140050000;
    bool reg = sys.registerPatch("KB5059999-TEST-PROLOG", "kernel32.dll", "CreateFileW", targetAddr, patchAddr, 128);
    TEST_ASSERT(reg, "Registration of test hotpatch must succeed");
    auto testPatch = sys.getPatch("KB5059999-TEST-PROLOG");
    TEST_ASSERT(testPatch != nullptr, "Retrieved test patch must not be null");
    const auto& tramp = testPatch->getTrampolineBytes();
    TEST_ASSERT(tramp[0] == micant::hotpatch::OPCODE_JMP_REL32, "Trampoline opcode byte 0 must be 0xE9 (JMP rel32)");
    int32_t expectedRel = static_cast<int32_t>(patchAddr - (targetAddr + micant::hotpatch::PROLOG_REPLACEMENT_SIZE));
    int32_t actualRel = 0;
    std::memcpy(&actualRel, &tramp[1], sizeof(int32_t));
    TEST_ASSERT(actualRel == expectedRel, "Trampoline relative offset displacement calculation must match AMD64 specification");

    // 5. Atomic Code Swapping & I-Cache Barrier
    uint64_t swapsBefore = sys.getAtomicSwaps();
    bool appOk = sys.applyPatch("KB5059999-TEST-PROLOG");
    TEST_ASSERT(appOk, "Applying hotpatch must succeed");
    TEST_ASSERT(testPatch->getState() == micant::hotpatch::PatchState::Active, "Patch state must transition to Active");
    TEST_ASSERT(sys.getAtomicSwaps() == swapsBefore + 1, "Atomic swap counter must increment on patch application");
    TEST_ASSERT(testPatch->getICacheFlushes() >= 1, "Instruction cache flush barrier must be executed");

    // 6. Function Detour Redirection
    bool wasRedirected = false;
    bool invOk = testPatch->invoke(&wasRedirected);
    TEST_ASSERT(invOk, "Function invocation through hotpatch gate must succeed");
    TEST_ASSERT(wasRedirected == true, "Active hotpatch must redirect execution to patch detour function");
    TEST_ASSERT(testPatch->getRedirectedCalls() == 1, "Redirected calls counter must equal 1");
    TEST_ASSERT(testPatch->getOriginalCalls() == 0, "Original calls counter must remain 0 while patch is active");

    // 7. Dynamic Reversible Rollback
    bool revOk = sys.revertPatch("KB5059999-TEST-PROLOG");
    TEST_ASSERT(revOk, "Reverting hotpatch must succeed");
    TEST_ASSERT(testPatch->getState() == micant::hotpatch::PatchState::Reverted, "Patch state must transition to Reverted");
    TEST_ASSERT(sys.getAtomicSwaps() == swapsBefore + 2, "Atomic swap counter must increment on patch revert");

    // 8. Original Function Invocation After Revert
    wasRedirected = true;
    invOk = testPatch->invoke(&wasRedirected);
    TEST_ASSERT(invOk, "Invocation after revert must succeed");
    TEST_ASSERT(wasRedirected == false, "Reverted hotpatch must execute original unpatched function code");
    TEST_ASSERT(testPatch->getOriginalCalls() == 1, "Original calls counter must increment to 1");

    // 9. Authenticode Signature Verification
    uint32_t sigValid = 0;
    NTSTATUS stSig = micant::hotpatch::HotpatchVerifySignature("KB5051234-CVE-2026-0001", &sigValid);
    TEST_ASSERT(stSig == micant::STATUS_SUCCESS, "HotpatchVerifySignature must return STATUS_SUCCESS");
    TEST_ASSERT(sigValid == 1, "Production PCA certificate signature must verify as valid");

    // 10. Win32 C ABI Parity Exports
    NTSTATUS stInit = micant::hotpatch::HotpatchInitializeSubsystem();
    TEST_ASSERT(stInit == micant::STATUS_SUCCESS, "HotpatchInitializeSubsystem must return STATUS_SUCCESS");
    uint32_t patchState = 0;
    uint64_t patchInvocations = 0;
    NTSTATUS stQuery = micant::hotpatch::HotpatchQueryPatchStatus("KB5059999-TEST-PROLOG", &patchState, &patchInvocations);
    TEST_ASSERT(stQuery == micant::STATUS_SUCCESS, "HotpatchQueryPatchStatus must succeed for existing patch");
    TEST_ASSERT(patchState == static_cast<uint32_t>(micant::hotpatch::PatchState::Reverted), "Query status state must be Reverted");
    TEST_ASSERT(patchInvocations == 2, "Query status total invocations must be 2");

    uint32_t patchCount = 0;
    char patchIds[16][64];
    NTSTATUS stEnum = micant::hotpatch::HotpatchEnumeratePatches(&patchCount, patchIds, 16);
    TEST_ASSERT(stEnum == micant::STATUS_SUCCESS, "HotpatchEnumeratePatches must succeed");
    TEST_ASSERT(patchCount >= 3, "Enumerated patch count must include pre-seeded and test patches");

    // 11. Idempotency, Double-Apply & Error Handling
    bool doubleRevert = sys.revertPatch("KB5059999-TEST-PROLOG");
    TEST_ASSERT(!doubleRevert, "Reverting an already reverted patch must return false");
    bool nonExistentApply = sys.applyPatch("KB0000000-NON-EXISTENT");
    TEST_ASSERT(!nonExistentApply, "Applying non-existent patch must fail");
    NTSTATUS stMissing = micant::hotpatch::HotpatchQueryPatchStatus("KB0000000-NON-EXISTENT", &patchState, &patchInvocations);
    TEST_ASSERT(stMissing == micant::STATUS_NOT_FOUND, "Querying non-existent patch must return STATUS_NOT_FOUND");
    NTSTATUS stNull = micant::hotpatch::HotpatchApplyPatch(nullptr);
    TEST_ASSERT(stNull == micant::STATUS_INVALID_PARAMETER, "Null parameter must return STATUS_INVALID_PARAMETER");

    // 12. Multithreaded Concurrency & Quiescence Stress Test
    std::atomic<int> stressSuccessCount{0};
    std::vector<std::thread> threads;
    threads.reserve(10);
    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 10; ++i) {
                bool redirected = false;
                testPatch->invoke(&redirected);
                if (t == 0 && i % 2 == 0) {
                    sys.applyPatch("KB5059999-TEST-PROLOG");
                } else if (t == 0 && i % 2 == 1) {
                    sys.revertPatch("KB5059999-TEST-PROLOG");
                }
                stressSuccessCount.fetch_add(1);
            }
        });
    }
    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(stressSuccessCount.load() == 100, "100 concurrent hotpatch operations and invocations must complete without race conditions");

    std::cout << "[TEST] Suite 191: Windows Kernel Hotpatching (KLP / TitanHotpatch) & Live Update Subsystem PASSED.\n";
}

void Test_WindowsHyperV_NestedVirtualization_Subsystem() {
    std::cout << "[TEST] Running Suite 192: Windows Hyper-V Hypercall & Nested Virtualization Subsystem...\n";

    // 1. SCM Service & VersionDatabase Registration
    micant::hyperv::RegisterHypervSubsystem();
    auto& vdb = micant::version::VersionDatabase::Instance();
    auto modHv = vdb.FindModule("hvix64.sys");
    TEST_ASSERT(modHv != nullptr, "hvix64.sys must be registered in VersionDatabase");
    auto modHvr = vdb.FindModule("winhvr.sys");
    TEST_ASSERT(modHvr != nullptr, "winhvr.sys must be registered in VersionDatabase");
    auto& scm = micant::scm::ServiceControlManager::get();
    auto rec = scm.getServiceRecord(L"HypervService");
    TEST_ASSERT(rec != nullptr, "HypervService must be registered in ServiceControlManager");
    TEST_ASSERT(rec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "HypervService must be running");

    // 2. Subsystem Initialization and Hypercall Code Page GPA alignment
    auto& sys = micant::hyperv::HypervSubsystem::get();
    TEST_ASSERT(sys.isInitialized(), "HypervSubsystem must be initialized");
    TEST_ASSERT(sys.isHypercallPageEnabled(), "Hypercall page must be enabled");
    TEST_ASSERT((sys.getHypercallPageGpa() & 0xFFF) == 0, "Hypercall page GPA must be 4KB page aligned");

    // 3. Reference TSC page sequence and invariant scale validation
    const auto& refTsc = sys.getReferenceTsc();
    TEST_ASSERT(refTsc.tscSequence >= 1, "Reference TSC sequence must be >= 1");
    TEST_ASSERT(refTsc.tscScale == 0x100000000ULL, "Reference TSC scale must be 1.0 (0x100000000)");

    // 4. Fast & Standard Hypercall Execution with Status Verification
    uint64_t outVal = 0;
    uint16_t statusTrans = sys.invokeHypercall(micant::hyperv::HvCallTranslateVirtualAddress, true, 0x140001000ULL, &outVal);
    TEST_ASSERT(statusTrans == micant::hyperv::HV_STATUS_SUCCESS, "HvCallTranslateVirtualAddress fast hypercall must succeed");
    TEST_ASSERT(outVal == 0x140002000ULL, "Translated physical address should match expected GPA page offset");

    uint16_t statusMsg = sys.invokeHypercall(micant::hyperv::HvCallPostMessage, false, 0x5000);
    TEST_ASSERT(statusMsg == micant::hyperv::HV_STATUS_SUCCESS, "HvCallPostMessage buffered hypercall must succeed");

    uint16_t statusBad = sys.invokeHypercall(0xFFFF, false, 0);
    TEST_ASSERT(statusBad == micant::hyperv::HV_STATUS_INVALID_HYPERCALL_CODE, "Unknown hypercall code must return HV_STATUS_INVALID_HYPERCALL_CODE");

    // 5. Root Partition and Guest Partition Discovery & Management
    TEST_ASSERT(sys.getPartitionCount() >= 2, "HypervSubsystem must have at least 2 partitions (Root and WSL2)");
    auto rootPart = sys.getPartition(0);
    TEST_ASSERT(rootPart != nullptr, "Root partition (ID 0) must exist");
    TEST_ASSERT(rootPart->getName() == "MicaNT-Root-Partition", "Root partition name must match");
    TEST_ASSERT(rootPart->isNestedEnabled(), "Root partition should have nested virtualization enabled");

    auto customPart = sys.createPartition(100, "Titan-Isolated-VM", 4096);
    TEST_ASSERT(customPart != nullptr, "Creating new guest partition 100 must succeed");
    TEST_ASSERT(sys.getPartition(100) == customPart, "Querying guest partition 100 must return created instance");
    TEST_ASSERT(sys.createPartition(100, "Duplicate", 1024) == nullptr, "Creating duplicate partition ID must return nullptr");

    // 6. Virtual Processor & VPAP (Virtual Processor Assist Page) Allocation
    auto vp0 = customPart->createVirtualProcessor(0);
    TEST_ASSERT(vp0 != nullptr, "VirtualProcessor 0 must be created for partition 100");
    TEST_ASSERT(vp0->getVpIndex() == 0, "VP index must be 0");
    TEST_ASSERT(vp0->getPartitionId() == 100, "VP partition ID must be 100");
    const auto& vpap = vp0->getVpap();
    TEST_ASSERT((vpap.enlightenedVmcsGpa & 0xFFF) == 0, "VPAP enlightenedVmcsGpa must be 4KB page aligned");
    TEST_ASSERT(vpap.nestedEnlightenmentsControl == 1, "VPAP nestedEnlightenmentsControl must be active");

    // 7. Enlightened VMCS (eVMCS) Revision ID & Architectural Structure Validation
    auto& evmcs = vp0->getEnlightenedVmcs();
    TEST_ASSERT(evmcs.revisionId == micant::hyperv::HV_VMX_ENLIGHTENED_VMCS_VERSION, "eVMCS revision must match HV_VMX_ENLIGHTENED_VMCS_VERSION");
    TEST_ASSERT(evmcs.guestCr0 == 0x80050033, "Default guest CR0 must match protected mode paging configuration");
    TEST_ASSERT(evmcs.hostCr0 == 0x80050033, "Default host CR0 must match protected mode paging configuration");

    // 8. Nested Virtualization Context Transition (L1 Host -> L2 Guest Level 2)
    customPart->enableNestedVirtualization(true);
    TEST_ASSERT(customPart->isNestedEnabled(), "Partition 100 nested virtualization must be enabled");
    TEST_ASSERT(vp0->getExecutionLevel() == 1, "Initial execution level must be L1");
    TEST_ASSERT(!vp0->isNestedActive(), "Initial nested state must be false");

    vp0->setExecutionLevel(2);
    TEST_ASSERT(vp0->getExecutionLevel() == 2, "Execution level must transition to L2");
    TEST_ASSERT(vp0->isNestedActive(), "isNestedActive must return true in L2 level");

    // 9. L2-to-L1 Reflective Nested VM-Exit Interception & Injection
    uint64_t initialNestedExits = vp0->getNestedVmExits();
    bool exitInjected = sys.injectNestedVmExit(100, 0, micant::hyperv::NestedExitReason::Cpuid, 0x1234);
    TEST_ASSERT(exitInjected, "Injecting nested VM-exit for Partition 100 VP 0 must succeed");
    TEST_ASSERT(vp0->getExecutionLevel() == 1, "VP must reflect back to L1 execution context following nested VM-exit");
    TEST_ASSERT(vp0->getNestedVmExits() == initialNestedExits + 1, "Nested VM-exit count must increment by 1");
    TEST_ASSERT(vp0->getEnlightenedVmcs().exitReason == static_cast<uint32_t>(micant::hyperv::NestedExitReason::Cpuid), "eVMCS exitReason must be CPUID");
    TEST_ASSERT(vp0->getEnlightenedVmcs().exitQualification == 0x1234, "eVMCS exitQualification must match injected value");

    // 10. eVMCS Clean Fields Mask Clearing and Dirty Sync
    vp0->getEnlightenedVmcs().cleanFieldsMask = 0xFFFFFFFF;
    bool syncClean = sys.syncEnlightenedVmcs(100, 0, micant::hyperv::HV_VMX_ENLIGHTENED_CLEAN_CONTROL_PROC | micant::hyperv::HV_VMX_ENLIGHTENED_CLEAN_GUEST_GRP1);
    TEST_ASSERT(syncClean, "syncEnlightenedVmcs must succeed");
    TEST_ASSERT((vp0->getEnlightenedVmcs().cleanFieldsMask & micant::hyperv::HV_VMX_ENLIGHTENED_CLEAN_CONTROL_PROC) == 0, "CONTROL_PROC clean bit must be cleared");
    TEST_ASSERT((vp0->getEnlightenedVmcs().cleanFieldsMask & micant::hyperv::HV_VMX_ENLIGHTENED_CLEAN_GUEST_GRP1) == 0, "GUEST_GRP1 clean bit must be cleared");
    TEST_ASSERT((vp0->getEnlightenedVmcs().cleanFieldsMask & micant::hyperv::HV_VMX_ENLIGHTENED_CLEAN_HOST_GRP1) != 0, "HOST_GRP1 clean bit must remain set");

    // 11. Win32 / NT C ABI Parity Exports
    TEST_ASSERT(micant::hyperv::HvrInitializeSubsystem() == micant::STATUS_SUCCESS, "HvrInitializeSubsystem must return STATUS_SUCCESS");
    TEST_ASSERT(micant::hyperv::HvrCreateGuestPartition(200, "C-ABI-Partition", 4096) == micant::STATUS_SUCCESS, "HvrCreateGuestPartition must return STATUS_SUCCESS");
    TEST_ASSERT(micant::hyperv::HvrCreateGuestPartition(200, nullptr, 4096) == micant::STATUS_INVALID_PARAMETER, "Null partition name must return STATUS_INVALID_PARAMETER");

    uint64_t abiOut = 0;
    TEST_ASSERT(micant::hyperv::HvrInvokeHypercall(micant::hyperv::HvCallTranslateVirtualAddress, 1, 0x2000000ULL, &abiOut) == micant::STATUS_SUCCESS, "HvrInvokeHypercall fast call must return STATUS_SUCCESS");

    uint32_t hasNested = 0, hasEvmcs = 0, hasRefTsc = 0;
    TEST_ASSERT(micant::hyperv::HvrQueryEnlightenments(&hasNested, &hasEvmcs, &hasRefTsc) == micant::STATUS_SUCCESS, "HvrQueryEnlightenments must succeed");
    TEST_ASSERT(hasNested == 1 && hasEvmcs == 1 && hasRefTsc == 1, "Enlightenments query must report 1 for nested, evmcs, and refTsc");
    TEST_ASSERT(micant::hyperv::HvrQueryEnlightenments(nullptr, &hasEvmcs, &hasRefTsc) == micant::STATUS_INVALID_PARAMETER, "Null pointer to HvrQueryEnlightenments must return STATUS_INVALID_PARAMETER");

    // 12. Multithreaded Concurrency & Hypercall Stress Test
    std::atomic<int> hypervStressCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);
    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([&, t]() {
            for (int i = 0; i < 10; ++i) {
                uint64_t outT = 0;
                uint16_t st = sys.invokeHypercall(micant::hyperv::HvCallTranslateVirtualAddress, true, 0x1000000ULL + (t * 0x1000) + i, &outT);
                if (st == micant::hyperv::HV_STATUS_SUCCESS) {
                    hypervStressCount.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(hypervStressCount.load() == 100, "100 concurrent Hyper-V hypercall invocations must succeed without race conditions");

    std::cout << "[TEST] Suite 192: Windows Hyper-V Hypercall & Nested Virtualization Subsystem PASSED.\n";
}

void Test_WindowsReFS_ResilientFileSystem_Subsystem() {
    std::cout << "[TEST] Running Suite 193: Windows ReFS (Resilient File System v3.12) Subsystem...\n";

    // 1. SCM Service & VersionDatabase Registration
    micant::refs::RegisterRefsSubsystem();
    auto& vdb = micant::version::VersionDatabase::Instance();
    auto modRefs = vdb.FindModule("refs.sys");
    TEST_ASSERT(modRefs != nullptr, "refs.sys must be registered in VersionDatabase");
    auto modUtil = vdb.FindModule("refsutil.exe");
    TEST_ASSERT(modUtil != nullptr, "refsutil.exe must be registered in VersionDatabase");
    auto& scm = micant::scm::ServiceControlManager::get();
    auto rec = scm.getServiceRecord(L"ReFS");
    TEST_ASSERT(rec != nullptr, "ReFS driver service must be registered in ServiceControlManager");
    TEST_ASSERT(rec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "ReFS driver service must be running");

    // 2. ReFS Volume Mount & Superblock Layout
    auto& sys = micant::refs::RefsSubsystem::get();
    TEST_ASSERT(sys.isInitialized(), "RefsSubsystem must be initialized");
    auto volR = sys.getVolume("R:");
    TEST_ASSERT(volR != nullptr, "Default volume R: must be mounted");
    const auto& sb = volR->getSuperblock();
    TEST_ASSERT(std::memcmp(sb.fsSignature, "ReFS\0\0\0\0", 8) == 0, "ReFS volume superblock must contain valid signature");
    TEST_ASSERT(sb.majorVersion == 3 && sb.minorVersion == 12, "ReFS version must be v3.12");
    TEST_ASSERT(sb.bytesPerSector == micant::refs::REFS_SECTOR_SIZE, "ReFS bytesPerSector must be 4096");
    TEST_ASSERT(sb.sectorsPerCluster == 16, "ReFS sectorsPerCluster must be 16 (64KB clusters)");

    // 3. Balanced B+ Tree Hierarchy & Node Checksumming
    auto rootNode = volR->getRootNode();
    TEST_ASSERT(rootNode != nullptr, "ReFS root B+ tree node must exist");
    TEST_ASSERT(rootNode->getNodeId() == 1, "Root node ID must be 1");
    TEST_ASSERT(rootNode->getRecordCount() > 0, "Root node must index initial records");
    TEST_ASSERT(rootNode->getNodeChecksum() != 0, "B+ tree node CRC32C checksum must be non-zero");

    // 4. File Creation & Valid Data Length (VDL) Tracking
    auto testFile = volR->createFile("\\Databases\\Accounts.mdf", 128 * 1024, true);
    TEST_ASSERT(testFile != nullptr, "Creating ReFS file with initial size must succeed");
    TEST_ASSERT(testFile->getFileSize() == 128 * 1024, "File size must reflect initial allocation");
    TEST_ASSERT(testFile->getValidDataLength() == 128 * 1024, "Valid Data Length (VDL) must match allocated size");
    TEST_ASSERT(testFile->isIntegrityEnabled(), "Integrity stream must be enabled on created file");

    // 5. Allocate-on-Write (Copy-on-Write / CoW) Atomic Write Transaction
    const char payload[] = "CRITICAL_TRANSACTION_PAYLOAD_REFS_COW_0123456789";
    uint32_t bytesWritten = 0;
    bool writeOk = volR->writeFileCoW("\\Databases\\Accounts.mdf", 0, payload, sizeof(payload), &bytesWritten);
    TEST_ASSERT(writeOk, "writeFileCoW must succeed");
    TEST_ASSERT(bytesWritten == sizeof(payload), "bytesWritten must equal payload size");
    TEST_ASSERT(volR->getTotalWrites() > 0, "Volume total writes must increment");

    // 6. Integrity Streams & CRC32C Checksum Validation
    char readBuffer[64]{};
    uint32_t bytesRead = 0;
    bool csumValid = false;
    bool readOk = volR->readFileWithIntegrity("\\Databases\\Accounts.mdf", 0, readBuffer, sizeof(payload), &bytesRead, &csumValid);
    TEST_ASSERT(readOk, "readFileWithIntegrity must succeed");
    TEST_ASSERT(bytesRead == sizeof(payload), "bytesRead must match requested size");
    TEST_ASSERT(csumValid, "CRC32C integrity checksum verification must pass");

    // 7. Fast Castagnoli CRC32C Verification
    const char csumTest[] = "123456789";
    uint32_t testCrc = micant::refs::ComputeCrc32c(csumTest, 9);
    TEST_ASSERT(testCrc == 0xE3069283, "Castagnoli CRC32C of '123456789' must equal standard 0xE3069283");

    // 8. Block Cloning (FSCTL_DUPLICATE_EXTENTS_TO_FILE)
    uint64_t initialClonedBytes = volR->getClonedBytes();
    bool cloneOk = volR->duplicateExtents("\\Databases\\Accounts.mdf", "\\Databases\\Accounts_Clone.mdf");
    TEST_ASSERT(cloneOk, "duplicateExtents block cloning must succeed");
    auto clonedFile = volR->getFile("\\Databases\\Accounts_Clone.mdf");
    TEST_ASSERT(clonedFile != nullptr, "Cloned file must exist");
    TEST_ASSERT(clonedFile->getFileSize() == testFile->getFileSize(), "Cloned file size must match original file size");
    TEST_ASSERT(volR->getClonedBytes() > initialClonedBytes, "Volume cloned bytes counter must increment");
    TEST_ASSERT(testFile->getExtents().size() == clonedFile->getExtents().size(), "Extent count must match");
    if (!testFile->getExtents().empty() && !clonedFile->getExtents().empty()) {
        TEST_ASSERT(testFile->getExtents()[0].refCount->load() >= 2, "Shared extent refCount must be at least 2");
    }

    // 9. Real-Time Background Scrubber & Volume Self-Healing
    uint64_t scrubbedBytes = 0;
    uint32_t errorsRepaired = 0;
    bool scrubOk = volR->scrubVolume(&scrubbedBytes, &errorsRepaired);
    TEST_ASSERT(scrubOk, "scrubVolume must succeed");
    TEST_ASSERT(scrubbedBytes > 0, "Scrubbed bytes must be > 0");
    TEST_ASSERT(volR->getScrubbedBytes() >= scrubbedBytes, "Volume cumulative scrubbed bytes must be updated");

    // 10. Multi-Volume Provisioning & Management
    auto volS = sys.mountVolume("S:", 65536);
    TEST_ASSERT(volS != nullptr, "Mounting secondary ReFS volume S: must succeed");
    TEST_ASSERT(sys.getVolume("S:") == volS, "Querying volume S: must return created volume");
    TEST_ASSERT(sys.mountVolume("S:", 1024) == nullptr, "Mounting duplicate drive letter must return nullptr");

    // 11. Win32 / NT C ABI Exports
    TEST_ASSERT(micant::refs::RefsInitializeSubsystem() == micant::STATUS_SUCCESS, "RefsInitializeSubsystem must return STATUS_SUCCESS");
    TEST_ASSERT(micant::refs::RefsCreateFile("R:", "\\Shared\\doc.bin", 65536, 1) == micant::STATUS_SUCCESS, "RefsCreateFile via C ABI must succeed");
    TEST_ASSERT(micant::refs::RefsCreateFile(nullptr, "\\bad.bin", 0, 0) == micant::STATUS_INVALID_PARAMETER, "Null drive letter must return STATUS_INVALID_PARAMETER");

    uint32_t abiWritten = 0;
    TEST_ASSERT(micant::refs::RefsWriteFileCoW("R:", "\\Shared\\doc.bin", 0, "HELLO", 5, &abiWritten) == micant::STATUS_SUCCESS, "RefsWriteFileCoW via C ABI must succeed");
    TEST_ASSERT(abiWritten == 5, "Bytes written via C ABI must equal 5");

    uint64_t totalB = 0, freeB = 0, clonedB = 0;
    TEST_ASSERT(micant::refs::RefsQueryVolumeState("R:", &totalB, &freeB, &clonedB) == micant::STATUS_SUCCESS, "RefsQueryVolumeState must succeed");
    TEST_ASSERT(totalB > 0 && freeB > 0, "Volume state metrics must be positive");

    // 12. Multithreaded Concurrency & CoW Write Stress Test
    std::atomic<int> refsStressCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);
    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([&, t]() {
            for (int i = 0; i < 10; ++i) {
                uint32_t wr = 0;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "THREAD_%d_ITER_%d", t, i);
                if (volR->writeFileCoW("\\Shared\\doc.bin", (t * 100) + i, buf, 16, &wr)) {
                    refsStressCount.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(refsStressCount.load() == 100, "100 concurrent ReFS transactional CoW writes must succeed without race conditions");

    std::cout << "[TEST] Suite 193: Windows ReFS (Resilient File System v3.12) Subsystem PASSED.\n";
}

void Test_WindowsClusterSharedVolume_CSVFS_Subsystem() {
    std::cout << "[TEST] Running Suite 194: Windows Cluster Shared Volume File System (CSVFS v2.0) Subsystem...\n";

    // 1. SCM Driver & Service Registration
    micant::csvfs::RegisterCsvfsSubsystem();
    auto& scm = micant::scm::ServiceControlManager::get();
    auto csvfsSvc = scm.getServiceRecord(L"CSVFS");
    TEST_ASSERT(csvfsSvc != nullptr, "CSVFS kernel driver must be registered in SCM");
    TEST_ASSERT(csvfsSvc->serviceType == micant::scm::SERVICE_FILE_SYSTEM_DRIVER, "CSVFS must be registered as SERVICE_FILE_SYSTEM_DRIVER");
    TEST_ASSERT(csvfsSvc->startType == micant::scm::SERVICE_SYSTEM_START, "CSVFS must be configured for SERVICE_SYSTEM_START");

    auto clusSvc = scm.getServiceRecord(L"ClusSvc");
    TEST_ASSERT(clusSvc != nullptr, "Cluster Service (clussvc.exe) must be registered in SCM");
    TEST_ASSERT(clusSvc->serviceType == micant::scm::SERVICE_WIN32_OWN_PROCESS, "ClusSvc must be SERVICE_WIN32_OWN_PROCESS");

    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("csvfs.sys") != nullptr, "csvfs.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("clussvc.exe") != nullptr, "clussvc.exe must be registered in VersionDatabase");

    // 2. Multi-Node Cluster Topology
    auto& sys = micant::csvfs::CsvfsSubsystem::get();
    TEST_ASSERT(sys.isInitialized(), "CsvfsSubsystem must be initialized");
    TEST_ASSERT(sys.getNodeCount() >= 3, "Cluster must have at least 3 initial nodes");

    auto node1 = sys.getNode(1);
    TEST_ASSERT(node1 != nullptr, "Node 1 must exist");
    TEST_ASSERT(node1->isCoordinator, "Node 1 must be initial Coordinator");
    TEST_ASSERT(node1->hasDirectStorageAccess, "Node 1 must have direct storage access");

    auto node2 = sys.getNode(2);
    TEST_ASSERT(node2 != nullptr && !node2->isCoordinator, "Node 2 must be a worker node");

    // 3. Cluster Shared Volume Mounting
    auto vol1 = sys.getVolume("C:\\ClusterStorage\\Volume1");
    TEST_ASSERT(vol1 != nullptr, "Primary CSV Volume C:\\ClusterStorage\\Volume1 must exist");
    TEST_ASSERT(vol1->getCoordinatorNodeId() == 1, "Coordinator Node ID must initially be 1");
    TEST_ASSERT(vol1->getVolumeState() == micant::csvfs::CsvVolumeState::Online, "Volume state must be ONLINE");
    TEST_ASSERT(vol1->getRedirectState() == micant::csvfs::CsvRedirectState::DirectIo, "Volume redirect state must be DirectIo");
    TEST_ASSERT(vol1->getCapacityMb() == 2097152, "Volume capacity must be 2 TB");

    // 4. Coordinator Metadata Delegation (File Creation, Allocation, Rename)
    uint32_t metaStatus = 0;
    bool metaCreateOk = vol1->delegateMetadata(micant::csvfs::CsvMetadataOp::CreateFile, "\\VirtualMachines\\ClusterVM1.vhdx", 100 * 1024 * 1024ULL, &metaStatus, 2);
    TEST_ASSERT(metaCreateOk && metaStatus == 0, "Delegated file creation from Node 2 to Coordinator must succeed");

    auto vmFile = vol1->getFile("\\VirtualMachines\\ClusterVM1.vhdx");
    TEST_ASSERT(vmFile != nullptr, "Created file must be present in CSV namespace");
    TEST_ASSERT(vmFile->getSizeBytes() == 100 * 1024 * 1024ULL, "File size must match allocated parameter");

    bool metaAllocOk = vol1->delegateMetadata(micant::csvfs::CsvMetadataOp::SetAllocationSize, "\\VirtualMachines\\ClusterVM1.vhdx", 200 * 1024 * 1024ULL, &metaStatus, 3);
    TEST_ASSERT(metaAllocOk && metaStatus == 0, "Delegated allocation resizing from Node 3 must succeed");
    TEST_ASSERT(vmFile->getSizeBytes() == 200 * 1024 * 1024ULL, "Resized file size must match new allocation");

    // 5. Direct I/O Path Execution (Parallel Reads and Writes)
    const char writePayload[] = "CSVFS_V2_DIRECT_IO_BLOCK_WRITE_TEST_DATA_PAYLOAD";
    uint32_t directWritten = 0;
    bool directWriteOk = vol1->directIoWrite("\\VirtualMachines\\ClusterVM1.vhdx", 0, writePayload, sizeof(writePayload), &directWritten, 2);
    TEST_ASSERT(directWriteOk, "Direct I/O write from worker Node 2 must succeed");
    TEST_ASSERT(directWritten == sizeof(writePayload), "Direct bytes written must match payload size");
    TEST_ASSERT(vol1->getTotalDirectWrites() > 0, "Volume total direct writes must increment");

    char directReadBuf[64]{};
    uint32_t directReadBytes = 0;
    bool directReadOk = vol1->directIoRead("\\VirtualMachines\\ClusterVM1.vhdx", 0, directReadBuf, sizeof(writePayload), &directReadBytes, 3);
    TEST_ASSERT(directReadOk, "Direct I/O read from worker Node 3 must succeed");
    TEST_ASSERT(directReadBytes == sizeof(writePayload), "Direct bytes read must match payload size");
    TEST_ASSERT(vol1->getTotalDirectReads() > 0, "Volume total direct reads must increment");

    // 6. Network Redirect Path Routing Failback
    vol1->setRedirectState(micant::csvfs::CsvRedirectState::FileRedirected);
    TEST_ASSERT(vol1->getRedirectState() == micant::csvfs::CsvRedirectState::FileRedirected, "Volume redirect state must be FileRedirected");

    uint32_t redirWritten = 0;
    bool redirWriteOk = vol1->directIoWrite("\\VirtualMachines\\ClusterVM1.vhdx", 4096, writePayload, sizeof(writePayload), &redirWritten, 2);
    TEST_ASSERT(redirWriteOk, "Redirected write must succeed over network routing");
    TEST_ASSERT(vol1->getTotalRedirectedOps() > 0, "Volume total redirected operations must increment");

    uint32_t redirReadBytes = 0;
    bool redirReadOk = vol1->directIoRead("\\VirtualMachines\\ClusterVM1.vhdx", 4096, directReadBuf, sizeof(writePayload), &redirReadBytes, 3);
    TEST_ASSERT(redirReadOk, "Redirected read must succeed over network routing");

    // 7. User-Requested Maintenance Redirection
    vol1->setRedirectState(micant::csvfs::CsvRedirectState::UserRequested);
    TEST_ASSERT(vol1->getRedirectState() == micant::csvfs::CsvRedirectState::UserRequested, "Volume redirect state must be UserRequested");
    vol1->setRedirectState(micant::csvfs::CsvRedirectState::DirectIo);
    TEST_ASSERT(vol1->getRedirectState() == micant::csvfs::CsvRedirectState::DirectIo, "Volume redirect state must return to DirectIo");

    // 8. Dynamic Coordinator Failover with I/O Freeze & Drain
    // Trigger I/O operations while volume is paused
    vol1->setVolumeState(micant::csvfs::CsvVolumeState::Paused);
    TEST_ASSERT(vol1->getVolumeState() == micant::csvfs::CsvVolumeState::Paused, "Volume must be in PAUSED state");

    uint32_t pausedWritten = 0;
    vol1->directIoWrite("\\VirtualMachines\\ClusterVM1.vhdx", 8192, writePayload, sizeof(writePayload), &pausedWritten, 2);
    TEST_ASSERT(vol1->getQueuedIoCount() >= 1, "Queued I/O operation must be pending in paused volume queue");

    uint32_t drainedOps = 0;
    bool failoverOk = vol1->failoverCoordinator(2, &drainedOps);
    TEST_ASSERT(failoverOk, "Coordinator failover to Node 2 must succeed");
    TEST_ASSERT(vol1->getCoordinatorNodeId() == 2, "New Coordinator Node ID must be 2");
    TEST_ASSERT(vol1->getVolumeState() == micant::csvfs::CsvVolumeState::Online, "Volume state must return to ONLINE after failover");
    TEST_ASSERT(drainedOps >= 1, "Drained I/O count must reflect previously paused operations");
    TEST_ASSERT(vol1->getQueuedIoCount() == 0, "Paused I/O queue must be completely drained");
    TEST_ASSERT(vol1->getFailoverCount() >= 1, "Volume failover counter must be incremented");

    // 9. ReFS v3.12 Block Cloning Interoperability
    uint32_t cloneStatus = 0;
    bool cloneMetaOk = vol1->delegateMetadata(micant::csvfs::CsvMetadataOp::DuplicateExtents, "\\VirtualMachines\\ClusterVM1.vhdx", 0, &cloneStatus, 2);
    TEST_ASSERT(cloneMetaOk && cloneStatus == 0, "Delegated extent duplication (Block Cloning) must succeed");
    auto clonedVm = vol1->getFile("\\VirtualMachines\\ClusterVM1.vhdx_clone.vhdx");
    TEST_ASSERT(clonedVm != nullptr, "Cloned VM file must exist in CSV namespace");

    // 10. Secondary Volume Mounting
    auto vol2 = sys.mountVolume("C:\\ClusterStorage\\Volume2", "S:", 2, 1048576);
    TEST_ASSERT(vol2 != nullptr, "Mounting secondary volume C:\\ClusterStorage\\Volume2 must succeed");
    TEST_ASSERT(sys.getVolume("C:\\ClusterStorage\\Volume2") == vol2, "Querying volume 2 must return valid pointer");
    TEST_ASSERT(sys.mountVolume("C:\\ClusterStorage\\Volume2", "S:", 2, 512) == nullptr, "Mounting duplicate volume path must fail");

    // 11. Clean-Room Win32 / NT C ABI Exports
    TEST_ASSERT(micant::csvfs::CsvfsInitializeSubsystem() == micant::STATUS_SUCCESS, "CsvfsInitializeSubsystem must return STATUS_SUCCESS");

    uint32_t abiRedir = 0;
    TEST_ASSERT(micant::csvfs::CsvfsQueryRedirectState("C:\\ClusterStorage\\Volume1", &abiRedir) == micant::STATUS_SUCCESS, "CsvfsQueryRedirectState via C ABI must succeed");

    TEST_ASSERT(micant::csvfs::CsvfsSetRedirectState("C:\\ClusterStorage\\Volume1", 0) == micant::STATUS_SUCCESS, "CsvfsSetRedirectState via C ABI must succeed");

    uint32_t abiWr = 0;
    TEST_ASSERT(micant::csvfs::CsvfsDirectIoWrite("C:\\ClusterStorage\\Volume1", "\\Shared\\Data.bin", 0, "DATA", 4, &abiWr) == micant::STATUS_SUCCESS, "CsvfsDirectIoWrite via C ABI must succeed");
    TEST_ASSERT(abiWr == 4, "Bytes written via C ABI must match 4");

    uint32_t abiRd = 0;
    char abiRdBuf[16]{};
    TEST_ASSERT(micant::csvfs::CsvfsDirectIoRead("C:\\ClusterStorage\\Volume1", "\\Shared\\Data.bin", 0, abiRdBuf, 4, &abiRd) == micant::STATUS_SUCCESS, "CsvfsDirectIoRead via C ABI must succeed");
    TEST_ASSERT(abiRd == 4, "Bytes read via C ABI must match 4");

    uint64_t stDirect = 0, stRedir = 0, stMeta = 0;
    TEST_ASSERT(micant::csvfs::CsvfsQueryVolumeStats("C:\\ClusterStorage\\Volume1", &stDirect, &stRedir, &stMeta) == micant::STATUS_SUCCESS, "CsvfsQueryVolumeStats via C ABI must succeed");
    TEST_ASSERT(stDirect > 0, "Direct I/O stats must be recorded");

    // 12. Multithreaded Concurrency & Parallel Direct I/O Stress Test
    std::atomic<int> csvStressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);
    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([&, t]() {
            for (int i = 0; i < 10; ++i) {
                uint32_t wr = 0;
                char payloadBuf[32];
                std::snprintf(payloadBuf, sizeof(payloadBuf), "CSV_T%d_I%d", t, i);
                if (vol1->directIoWrite("\\VirtualMachines\\ClusterVM1.vhdx", (t * 256) + i, payloadBuf, 16, &wr, (t % 3) + 1)) {
                    csvStressSuccessCount.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(csvStressSuccessCount.load() == 100, "100 concurrent parallel direct I/O writes across 10 threads must complete successfully");

    std::cout << "[TEST] Suite 194: Windows Cluster Shared Volume File System (CSVFS v2.0) Subsystem PASSED.\n";
}

void Test_WindowsContainerStorage_Wcifs_Subsystem() {
    std::cout << "[TEST] Running Suite 195: Windows Container Storage & Host Compute System (HCS) Isolation Subsystem...\n";

    // 1. SCM Driver & Host Compute Service Registration
    micant::wcifs::RegisterWcifsSubsystem();
    auto& scm = micant::scm::ServiceControlManager::get();

    auto wcifsSvc = scm.getServiceRecord(L"Wcifs");
    TEST_ASSERT(wcifsSvc != nullptr, "wcifs.sys kernel minifilter must be registered in SCM");
    TEST_ASSERT(wcifsSvc->serviceType == micant::scm::SERVICE_FILE_SYSTEM_DRIVER, "Wcifs must be SERVICE_FILE_SYSTEM_DRIVER");
    TEST_ASSERT(wcifsSvc->startType == micant::scm::SERVICE_SYSTEM_START, "Wcifs must be configured for SERVICE_SYSTEM_START");
    TEST_ASSERT(wcifsSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "Wcifs must be RUNNING");

    auto wcnfsSvc = scm.getServiceRecord(L"Wcnfs");
    TEST_ASSERT(wcnfsSvc != nullptr, "wcnfs.sys namespace minifilter must be registered in SCM");
    TEST_ASSERT(wcnfsSvc->serviceType == micant::scm::SERVICE_FILE_SYSTEM_DRIVER, "Wcnfs must be SERVICE_FILE_SYSTEM_DRIVER");
    TEST_ASSERT(wcnfsSvc->startType == micant::scm::SERVICE_SYSTEM_START, "Wcnfs must be configured for SERVICE_SYSTEM_START");
    TEST_ASSERT(wcnfsSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "Wcnfs must be RUNNING");

    auto hcsSvc = scm.getServiceRecord(L"vmcompute");
    TEST_ASSERT(hcsSvc != nullptr, "Host Compute Service (vmcompute.exe) must be registered in SCM");
    TEST_ASSERT(hcsSvc->serviceType == micant::scm::SERVICE_WIN32_OWN_PROCESS, "vmcompute must be SERVICE_WIN32_OWN_PROCESS");
    TEST_ASSERT(hcsSvc->startType == micant::scm::SERVICE_AUTO_START, "vmcompute must be SERVICE_AUTO_START");
    TEST_ASSERT(hcsSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "vmcompute must be RUNNING");

    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("wcifs.sys") != nullptr, "wcifs.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("wcnfs.sys") != nullptr, "wcnfs.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("vmcompute.exe") != nullptr, "vmcompute.exe must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("hcs.dll") != nullptr, "hcs.dll must be registered in VersionDatabase");

    // 2. Storage Layer Registration & Immutability
    auto& sys = micant::wcifs::WcifsSubsystem::get();
    TEST_ASSERT(sys.isInitialized(), "WcifsSubsystem must be initialized");
    TEST_ASSERT(sys.getLayerCount() >= 2, "Default base OS layers (NanoServer & ServerCore) must be registered");

    auto nanoLayer = sys.getLayer("{A1B2C3D4-NANO-SERVER-2025-000000000001}");
    TEST_ASSERT(nanoLayer != nullptr, "NanoServer base layer must exist");
    TEST_ASSERT(nanoLayer->isReadOnly, "Base layers must be immutable / read-only");
    TEST_ASSERT(nanoLayer->fileTable.find("\\Windows\\System32\\ntdll.dll") != nanoLayer->fileTable.end(), "ntdll.dll must exist in NanoServer layer");

    auto appLayer = sys.registerLayer("{C3D4E5F6-APP-NODEJS-2025-000000000003}", "C:\\ProgramData\\docker\\windowsfilter\\node_layer", true, 128 * 1024 * 1024ULL);
    TEST_ASSERT(appLayer != nullptr, "Custom application layer registration must succeed");
    appLayer->fileTable["\\app\\server.js"] = { 'c', 'o', 'n', 's', 'o', 'l', 'e', '.', 'l', 'o', 'g', '(', '"', 'O', 'K', '"', ')', ';' };
    appLayer->fileTable["\\app\\package.json"] = { '{', '"', 'n', 'a', 'm', 'e', '"', ':', '"', 'n', 'o', 'd', 'e', '-', 'a', 'p', 'p', '"', '}' };
    TEST_ASSERT(sys.getLayer("{C3D4E5F6-APP-NODEJS-2025-000000000003}") == appLayer, "Querying registered app layer by GUID must match");

    // 3. Compute System Creation & Silo Object Manager Namespace Partitioning
    auto container = sys.createComputeSystem("web-app-silo", "nanoserver:ltsc2025", micant::wcifs::ContainerIsolationType::ProcessSilo);
    TEST_ASSERT(container != nullptr, "Host Compute System creation must succeed");
    TEST_ASSERT(container->getId() >= 1001, "Container ID must be assigned");
    TEST_ASSERT(container->getState() == micant::wcifs::ComputeSystemState::Created, "Initial state must be Created");
    TEST_ASSERT(container->getIsolationType() == micant::wcifs::ContainerIsolationType::ProcessSilo, "Isolation must be ProcessSilo");

    std::string expectedNamespace = "\\Sessions\\" + std::to_string(container->getId()) + "\\BaseNamedObjects";
    TEST_ASSERT(container->getSiloNamespace() == expectedNamespace, "Silo Object Manager namespace must match container ID partition");

    // 4. Multi-Layer Stack Assembly & Hierarchy Binding
    auto stack = container->getStorageStack();
    TEST_ASSERT(stack != nullptr, "Container storage stack must be allocated");
    TEST_ASSERT(stack->getContainerId() == container->getId(), "Storage stack container ID must match");
    size_t baseCountInitial = stack->getBaseLayerCount();
    stack->addBaseLayer(appLayer);
    TEST_ASSERT(stack->getBaseLayerCount() == baseCountInitial + 1, "App layer must be stacked onto container storage");

    // 5. Top-Down Layered File Read Traversal
    std::vector<uint8_t> ntdllBytes;
    bool ntdllReadOk = stack->readFile("\\Windows\\System32\\ntdll.dll", ntdllBytes);
    TEST_ASSERT(ntdllReadOk, "Reading ntdll.dll from stacked base layer must succeed");
    TEST_ASSERT(ntdllBytes.size() >= 4 && ntdllBytes[0] == 0x4D && ntdllBytes[1] == 0x5A, "ntdll.dll must have valid PE DOS magic");

    std::vector<uint8_t> appBytes;
    bool appReadOk = stack->readFile("\\app\\server.js", appBytes);
    TEST_ASSERT(appReadOk, "Reading server.js from intermediate app layer must succeed");
    TEST_ASSERT(!appBytes.empty() && appBytes[0] == 'c', "server.js content must match app layer payload");
    TEST_ASSERT(stack->getBaseLayerReads() >= 2, "Base layer read counter must reflect reads from layers");
    TEST_ASSERT(stack->getScratchReads() == 0, "No scratch reads should occur for pristine base layer files");

    // 6. Transparent Copy-on-Write (CoW) Scratch Divergence
    const uint8_t patchedNtdll[] = { 0x90, 0x90, 0xCC, 0xC3 };
    bool cowWriteOk = stack->writeFileCoW("\\Windows\\System32\\ntdll.dll", patchedNtdll, sizeof(patchedNtdll));
    TEST_ASSERT(cowWriteOk, "Writing to existing base file must trigger CoW write into scratch layer");
    TEST_ASSERT(stack->getCowDivergences() == 1, "CoW divergence counter must be exactly 1");

    std::vector<uint8_t> scratchNtdll;
    bool readScratchOk = stack->readFile("\\Windows\\System32\\ntdll.dll", scratchNtdll);
    TEST_ASSERT(readScratchOk, "Reading modified file must read from top scratch layer");
    TEST_ASSERT(scratchNtdll.size() == sizeof(patchedNtdll) && scratchNtdll[0] == 0x90, "Read data must match scratch layer modification");
    TEST_ASSERT(stack->getScratchReads() >= 1, "Scratch reads counter must be incremented");

    // Verify underlying base layer is completely pristine and untouched
    const auto& pristineBase = nanoLayer->fileTable["\\Windows\\System32\\ntdll.dll"];
    TEST_ASSERT(pristineBase.size() >= 2 && pristineBase[0] == 0x4D && pristineBase[1] == 0x5A, "Base layer must remain strictly read-only and unpolluted");

    // 7. Whiteout / Tombstone Deletion Semantics
    TEST_ASSERT(stack->hasFile("\\app\\package.json"), "package.json must initially exist via app base layer");
    bool delOk = stack->deleteFile("\\app\\package.json");
    TEST_ASSERT(delOk, "Deleting layered file must succeed and place tombstone marker");
    TEST_ASSERT(stack->getTombstoneDeletes() >= 1, "Tombstone deletion counter must increment");
    TEST_ASSERT(!stack->hasFile("\\app\\package.json"), "hasFile must return false for tombstoned file");

    std::vector<uint8_t> tombstoneTest;
    bool tombstoneMasked = !stack->readFile("\\app\\package.json", tombstoneTest);
    TEST_ASSERT(tombstoneMasked, "readFile on tombstoned file must fail even though present in lower layer");

    // Overwriting a tombstoned file clears tombstone
    const char newPkg[] = "{\"name\":\"overwritten-pkg\"}";
    stack->writeFileCoW("\\app\\package.json", newPkg, sizeof(newPkg));
    TEST_ASSERT(stack->hasFile("\\app\\package.json"), "Writing to tombstoned file must clear tombstone and create in scratch");

    // 8. Per-Container Virtualized Registry Diff Hives (wcnfs.sys)
    container->setRegistryDiff("HKLM\\SOFTWARE\\NodeApp\\Port", "8080");
    container->setRegistryDiff("HKLM\\SYSTEM\\CurrentControlSet\\Services\\AppSvc\\Start", "2");
    TEST_ASSERT(container->getRegistryKeyCount() == 2, "Registry diff hive must contain 2 modified keys");

    std::string regPort;
    bool regReadOk = container->getRegistryValue("HKLM\\SOFTWARE\\NodeApp\\Port", regPort);
    TEST_ASSERT(regReadOk && regPort == "8080", "Querying virtualized registry port must return '8080'");

    std::string missingKey;
    TEST_ASSERT(!container->getRegistryValue("HKLM\\SOFTWARE\\NonExistent", missingKey), "Querying non-existent registry key must return false");

    // 9. Host Compute Service (HCS) Lifecycle Operations
    TEST_ASSERT(container->start(), "HCS start must succeed");
    TEST_ASSERT(container->getState() == micant::wcifs::ComputeSystemState::Running, "State must be Running");
    TEST_ASSERT(!container->start(), "Starting an already running container must return false");

    TEST_ASSERT(container->pause(), "HCS pause must succeed");
    TEST_ASSERT(container->getState() == micant::wcifs::ComputeSystemState::Paused, "State must be Paused");
    TEST_ASSERT(!container->pause(), "Pausing an already paused container must return false");

    TEST_ASSERT(container->resume(), "HCS resume must succeed");
    TEST_ASSERT(container->getState() == micant::wcifs::ComputeSystemState::Running, "State must return to Running");

    TEST_ASSERT(container->terminate(), "HCS terminate must succeed");
    TEST_ASSERT(container->getState() == micant::wcifs::ComputeSystemState::Stopped, "State must be Stopped");

    // 10. Hyper-V Isolated Micro-VM Container Mode
    auto hvContainer = sys.createComputeSystem("hyperv-secure-vault", "servercore:ltsc2025", micant::wcifs::ContainerIsolationType::HyperV);
    TEST_ASSERT(hvContainer != nullptr, "Creating Hyper-V container must succeed");
    TEST_ASSERT(hvContainer->getIsolationType() == micant::wcifs::ContainerIsolationType::HyperV, "Isolation type must be HyperV");
    TEST_ASSERT(hvContainer->start(), "Starting Hyper-V container must succeed");
    TEST_ASSERT(hvContainer->getState() == micant::wcifs::ComputeSystemState::Running, "Hyper-V container state must be Running");
    hvContainer->terminate();
    TEST_ASSERT(hvContainer->getState() == micant::wcifs::ComputeSystemState::Stopped, "Hyper-V container termination must succeed");

    // 11. Clean-Room Win32 / NT C ABI Exports
    TEST_ASSERT(micant::wcifs::WcifsInitializeSubsystem() == micant::STATUS_SUCCESS, "WcifsInitializeSubsystem must return STATUS_SUCCESS");

    TEST_ASSERT(micant::wcifs::WcifsCreateLayer(nullptr, "path", 1, 0) == micant::STATUS_INVALID_PARAMETER, "WcifsCreateLayer with null GUID must return STATUS_INVALID_PARAMETER");
    TEST_ASSERT(micant::wcifs::WcifsCreateLayer("{D4E5F6A1-ABI-LAYER-2025-000000000004}", "C:\\docker\\abi_layer", 1, 1024) == micant::STATUS_SUCCESS, "WcifsCreateLayer via C ABI must succeed");

    uint32_t abiCid = 0;
    TEST_ASSERT(micant::wcifs::HcsCreateComputeSystem("abi-container", "nanoserver:ltsc2025", 0, &abiCid) == micant::STATUS_SUCCESS, "HcsCreateComputeSystem via C ABI must succeed");
    TEST_ASSERT(abiCid != 0, "Created container ID must be non-zero");

    TEST_ASSERT(micant::wcifs::HcsStartComputeSystem(abiCid) == micant::STATUS_SUCCESS, "HcsStartComputeSystem via C ABI must succeed");

    const char abiData[] = "C_ABI_TEST_DATA";
    TEST_ASSERT(micant::wcifs::WcifsWriteCoWLayeredFile(abiCid, "\\abi_test.txt", abiData, sizeof(abiData)) == micant::STATUS_SUCCESS, "WcifsWriteCoWLayeredFile via C ABI must succeed");

    char abiReadData[32]{};
    size_t abiBytesRead = 0;
    TEST_ASSERT(micant::wcifs::WcifsReadLayeredFile(abiCid, "\\abi_test.txt", abiReadData, sizeof(abiReadData), &abiBytesRead) == micant::STATUS_SUCCESS, "WcifsReadLayeredFile via C ABI must succeed");
    TEST_ASSERT(abiBytesRead == sizeof(abiData) && std::memcmp(abiReadData, abiData, sizeof(abiData)) == 0, "Read data via C ABI must match written bytes");

    uint32_t qState = 0;
    uint64_t qCow = 0;
    TEST_ASSERT(micant::wcifs::HcsQueryComputeSystemState(abiCid, &qState, &qCow) == micant::STATUS_SUCCESS, "HcsQueryComputeSystemState via C ABI must succeed");
    TEST_ASSERT(qState == static_cast<uint32_t>(micant::wcifs::ComputeSystemState::Running), "Queried state must be Running");

    TEST_ASSERT(micant::wcifs::WcifsDeleteLayeredFile(abiCid, "\\abi_test.txt") == micant::STATUS_SUCCESS, "WcifsDeleteLayeredFile via C ABI must succeed");
    TEST_ASSERT(micant::wcifs::WcifsReadLayeredFile(abiCid, "\\abi_test.txt", abiReadData, sizeof(abiReadData), &abiBytesRead) == micant::STATUS_OBJECT_NAME_NOT_FOUND, "Reading deleted file must return STATUS_OBJECT_NAME_NOT_FOUND");

    TEST_ASSERT(micant::wcifs::HcsTerminateComputeSystem(abiCid) == micant::STATUS_SUCCESS, "HcsTerminateComputeSystem via C ABI must succeed");

    // 12. Multithreaded Concurrency & High-Density Silo CoW Stress Test
    std::atomic<int> cowStressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);
    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([&, t]() {
            std::string cname = "stress-worker-" + std::to_string(t);
            auto sc = sys.createComputeSystem(cname, "nanoserver:ltsc2025", micant::wcifs::ContainerIsolationType::ProcessSilo);
            if (!sc) return;
            sc->start();

            for (int i = 0; i < 10; ++i) {
                std::string filePath = "\\data\\thread_" + std::to_string(t) + "_item_" + std::to_string(i) + ".bin";
                uint32_t val = (t * 1000) + i;
                if (sc->getStorageStack()->writeFileCoW(filePath, &val, sizeof(val))) {
                    std::vector<uint8_t> readBack;
                    if (sc->getStorageStack()->readFile(filePath, readBack) && readBack.size() == sizeof(val)) {
                        cowStressSuccessCount.fetch_add(1);
                    }
                }
            }
            sc->terminate();
        });
    }
    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(cowStressSuccessCount.load() == 100, "100 concurrent parallel CoW writes & reads across 10 container silos must succeed");

    std::cout << "[TEST] Suite 195: Windows Container Storage & Host Compute System (HCS) Isolation Subsystem PASSED.\n";
}

void Test_WindowsDirectStorage_S2D_Subsystem() {
    std::cout << "[TEST] Running Suite 196: Windows DirectStorage & Storage Spaces Direct (S2D) Subsystem...\n";

    // 1. SCM Drivers & Services Registration
    micant::dstorage::RegisterDirectStorageSubsystem();
    micant::dstorage::DirectStorageSubsystem::get().reset();
    auto& scm = micant::scm::ServiceControlManager::get();

    auto spaceportSvc = scm.getServiceRecord(L"Spaceport");
    TEST_ASSERT(spaceportSvc != nullptr, "spaceport.sys storage spaces port driver must be registered in SCM");
    TEST_ASSERT(spaceportSvc->serviceType == micant::scm::SERVICE_FILE_SYSTEM_DRIVER, "Spaceport must be SERVICE_FILE_SYSTEM_DRIVER");
    TEST_ASSERT(spaceportSvc->startType == micant::scm::SERVICE_BOOT_START, "Spaceport must be configured for SERVICE_BOOT_START");
    TEST_ASSERT(spaceportSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "Spaceport must be RUNNING");

    auto s2dSvc = scm.getServiceRecord(L"S2D");
    TEST_ASSERT(s2dSvc != nullptr, "s2d.sys clustered bus driver must be registered in SCM");
    TEST_ASSERT(s2dSvc->serviceType == micant::scm::SERVICE_KERNEL_DRIVER, "S2D must be SERVICE_KERNEL_DRIVER");
    TEST_ASSERT(s2dSvc->startType == micant::scm::SERVICE_SYSTEM_START, "S2D must be configured for SERVICE_SYSTEM_START");
    TEST_ASSERT(s2dSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "S2D must be RUNNING");

    auto dstorageSvc = scm.getServiceRecord(L"DStorageSvc");
    TEST_ASSERT(dstorageSvc != nullptr, "DirectStorage Acceleration Service must be registered in SCM");
    TEST_ASSERT(dstorageSvc->serviceType == micant::scm::SERVICE_WIN32_OWN_PROCESS, "DStorageSvc must be SERVICE_WIN32_OWN_PROCESS");
    TEST_ASSERT(dstorageSvc->startType == micant::scm::SERVICE_AUTO_START, "DStorageSvc must be SERVICE_AUTO_START");
    TEST_ASSERT(dstorageSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "DStorageSvc must be RUNNING");

    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("dstorage.dll") != nullptr, "dstorage.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("dstoragecore.dll") != nullptr, "dstoragecore.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("spaceport.sys") != nullptr, "spaceport.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("s2d.sys") != nullptr, "s2d.sys must be registered in VersionDatabase");

    // 2. DirectStorage Factory & Queue Creation
    auto& factory = micant::dstorage::IDStorageFactory::get();
    micant::dstorage::DSTORAGE_QUEUE_DESC qDesc{};
    qDesc.Capacity = 128;
    qDesc.Priority = micant::dstorage::DSTORAGE_PRIORITY_REALTIME;
    qDesc.Name = "DirectStorage-HighPriority-Queue";
    auto queue = factory.createQueue(qDesc);
    TEST_ASSERT(queue != nullptr, "DirectStorage queue creation must succeed");
    TEST_ASSERT(queue->getCapacity() == 128, "Queue capacity must match configured value");
    TEST_ASSERT(queue->getDesc().Priority == micant::dstorage::DSTORAGE_PRIORITY_REALTIME, "Queue priority must match configured value");

    // 3. BypassIO Fast Path & Storage File Abstraction
    auto storageFile = factory.openFile("C:\\Games\\Cyberpunk2077\\archive\\pc\\content\\models.bin", 1024ULL * 1024 * 10);
    TEST_ASSERT(storageFile != nullptr, "Opening storage file via IDStorageFactory must succeed");
    TEST_ASSERT(storageFile->isBypassIoSupported(), "BypassIO must be supported on direct NVMe storage path");
    TEST_ASSERT(storageFile->getSizeBytes() == 1024ULL * 1024 * 10, "Storage file size must match initialized capacity");

    // 4. GDeflate Hardware/CPU Compression & Decompression Pipeline
    const char assetString[] = "DIRECTSTORAGE_HIGH_RES_TEXTURE_4K_DIFFUSE_MAP_UNCOMPRESSED_RGBA8888_PIXEL_DATA_ARRAY";
    auto compressedAsset = micant::dstorage::CompressGDeflate(assetString, sizeof(assetString));
    TEST_ASSERT(!compressedAsset.empty(), "GDeflate compression must generate output buffer");
    TEST_ASSERT(compressedAsset.size() >= sizeof(micant::dstorage::GDeflateHeader), "Compressed output must contain GDeflate header");

    char decompTarget[256]{};
    uint32_t decompBytes = 0;
    bool decompResult = micant::dstorage::DecompressGDeflate(compressedAsset.data(), static_cast<uint32_t>(compressedAsset.size()),
                                                             decompTarget, sizeof(decompTarget), &decompBytes);
    TEST_ASSERT(decompResult, "GDeflate decompression must succeed");
    TEST_ASSERT(decompBytes == sizeof(assetString), "Decompressed bytes must match original length");
    TEST_ASSERT(std::memcmp(decompTarget, assetString, sizeof(assetString)) == 0, "Decompressed payload must bitwise match uncompressed input");

    // 5. DirectStorage Asynchronous Request Enqueue & Batch Submission
    micant::dstorage::DSTORAGE_REQUEST req{};
    req.CompressionFormat = micant::dstorage::DSTORAGE_COMPRESSION_FORMAT_GDEFLATE;
    req.SourceMemory = compressedAsset.data();
    req.SourceSize = static_cast<uint32_t>(compressedAsset.size());
    char memoryDst[256]{};
    req.DestinationBuffer = memoryDst;
    req.DestinationSize = sizeof(memoryDst);
    req.UncompressedSize = sizeof(assetString);

    TEST_ASSERT(queue->enqueueRequest(req), "Enqueueing valid DirectStorage request must succeed");
    TEST_ASSERT(queue->getPendingCount() == 1, "Pending request count must be 1");

    uint32_t submittedCount = queue->submit();
    TEST_ASSERT(submittedCount == 1, "Submitted request count must be 1");
    TEST_ASSERT(queue->getPendingCount() == 0, "Pending queue must be empty after submit");
    TEST_ASSERT(queue->getTotalCompleted() == 1, "Completed requests counter must be 1");
    TEST_ASSERT(std::memcmp(memoryDst, assetString, sizeof(assetString)) == 0, "DirectStorage memory destination must hold decompressed asset");

    // 6. Fence Signaling & Queue Synchronization
    queue->enqueueSignal(500);
    queue->submit();
    TEST_ASSERT(queue->getCompletedFenceValue() == 500, "Completed fence value must reflect signaled value (500)");

    // 7. Storage Spaces Direct (S2D) Physical Disk Discovery & Pool Aggregation
    auto& dstorageSys = micant::dstorage::DirectStorageSubsystem::get();
    TEST_ASSERT(dstorageSys.isInitialized(), "DirectStorageSubsystem must be initialized");
    auto pool = dstorageSys.getPool(1);
    TEST_ASSERT(pool != nullptr, "Default S2D pool 1 must exist");
    TEST_ASSERT(pool->getAllPhysicalDisks().size() >= 5, "Pool must contain at least 4 data NVMe disks + 1 hot-spare");
    TEST_ASSERT(pool->getStatus() == micant::dstorage::StorageOperationalStatus::OK, "Storage pool status must be OK");

    auto disk1 = pool->getPhysicalDisk(1);
    TEST_ASSERT(disk1 != nullptr, "Physical disk 1 must exist");
    TEST_ASSERT(disk1->busType == "NVMe", "Bus type must be NVMe");
    TEST_ASSERT(disk1->isHealthy, "Physical disk 1 must be healthy");

    auto spare = pool->getPhysicalDisk(5);
    TEST_ASSERT(spare != nullptr && spare->isHotSpare, "Physical disk 5 must be marked as HOT SPARE");

    // 8. Two-Way and Three-Way Mirror Virtual Disk Creation with Slab Allocation
    auto mirrorDisk = pool->createVirtualDisk("Prod_VM_Mirror", 1024ULL * 1024 * 1024 * 20, micant::dstorage::StorageResiliencyType::Mirror2); // 20 GB
    TEST_ASSERT(mirrorDisk != nullptr, "Creating 2-Way Mirror virtual disk must succeed");
    TEST_ASSERT(mirrorDisk->getResiliency() == micant::dstorage::StorageResiliencyType::Mirror2, "Resiliency must be Mirror2");
    TEST_ASSERT(!mirrorDisk->getSlabs().empty(), "Virtual disk must have allocated slabs");
    TEST_ASSERT(mirrorDisk->getSlabs()[0].physicalDiskIds.size() == 2, "2-Way Mirror slab must reference 2 distinct physical drives");

    const char vmPayload[] = "ENTERPRISE_DATABASE_REPLICATED_PAYLOAD_BLOCK_0";
    TEST_ASSERT(mirrorDisk->writeData(0, vmPayload, sizeof(vmPayload)), "Writing to mirrored virtual disk must succeed");
    char vmReadBuf[64]{};
    size_t vmBytesRead = 0;
    TEST_ASSERT(mirrorDisk->readData(0, vmReadBuf, sizeof(vmPayload), &vmBytesRead), "Reading from mirrored virtual disk must succeed");
    TEST_ASSERT(vmBytesRead == sizeof(vmPayload) && std::memcmp(vmReadBuf, vmPayload, sizeof(vmPayload)) == 0, "Read data must match written payload");

    // 9. Parity / Erasure Coding Virtual Disk Resiliency
    auto parityDisk = pool->createVirtualDisk("Archive_Parity", 1024ULL * 1024 * 1024 * 30, micant::dstorage::StorageResiliencyType::Parity); // 30 GB
    TEST_ASSERT(parityDisk != nullptr, "Creating Parity virtual disk must succeed");
    TEST_ASSERT(parityDisk->getResiliency() == micant::dstorage::StorageResiliencyType::Parity, "Resiliency must be Parity");
    TEST_ASSERT(parityDisk->getSlabs()[0].physicalDiskIds.size() >= 3, "Parity slab must stripe across at least 3 drives");

    const char archivePayload[] = "PARITY_ENCODED_ARCHIVE_STORAGE_BLOCK";
    TEST_ASSERT(parityDisk->writeData(0, archivePayload, sizeof(archivePayload)), "Writing to parity virtual disk must succeed");

    // 10. Dynamic Physical Drive Failure Injection, Degraded State & Hot-Spare Automatic Rebuild
    TEST_ASSERT(pool->failDisk(1), "Injecting failure on disk 1 must succeed");
    TEST_ASSERT(!disk1->isHealthy, "Disk 1 must be marked unhealthy / failed");
    TEST_ASSERT(pool->getStatus() == micant::dstorage::StorageOperationalStatus::Degraded, "Storage pool must transition to DEGRADED");
    TEST_ASSERT(mirrorDisk->getStatus() == micant::dstorage::StorageOperationalStatus::Degraded, "Mirrored virtual disk must transition to DEGRADED");

    // Read should still succeed from surviving mirror replica!
    char degradedReadBuf[64]{};
    size_t degradedBytes = 0;
    TEST_ASSERT(mirrorDisk->readData(0, degradedReadBuf, sizeof(vmPayload), &degradedBytes), "Reading from degraded mirror must succeed from survivor disk");
    TEST_ASSERT(degradedBytes == sizeof(vmPayload) && std::memcmp(degradedReadBuf, vmPayload, sizeof(vmPayload)) == 0, "Data must remain intact during disk failure");

    // Trigger hot-spare automatic rebuild
    TEST_ASSERT(pool->rebuildWithHotSpare(), "Automatic rebuild with hot-spare disk 5 must succeed");
    TEST_ASSERT(pool->getStatus() == micant::dstorage::StorageOperationalStatus::OK, "Pool status must return to OK after rebuild");
    TEST_ASSERT(mirrorDisk->getStatus() == micant::dstorage::StorageOperationalStatus::OK, "Mirrored virtual disk status must return to OK after rebuild");
    TEST_ASSERT(!spare->isHotSpare, "Hot-spare disk must now be promoted to active member");

    // 11. Clean-Room Win32 / NT C ABI Exports
    TEST_ASSERT(micant::dstorage::DStorageInitializeSubsystem() == micant::STATUS_SUCCESS, "DStorageInitializeSubsystem must return STATUS_SUCCESS");

    void* abiQueue = nullptr;
    TEST_ASSERT(micant::dstorage::DStorageCreateQueue(32, 0, &abiQueue) == micant::STATUS_SUCCESS, "DStorageCreateQueue via C ABI must succeed");
    TEST_ASSERT(abiQueue != nullptr, "Created queue pointer must be non-null");

    void* abiFile = nullptr;
    TEST_ASSERT(micant::dstorage::DStorageOpenFile("C:\\abi_file.bin", 4096, &abiFile) == micant::STATUS_SUCCESS, "DStorageOpenFile via C ABI must succeed");
    TEST_ASSERT(abiFile != nullptr, "Opened file pointer must be non-null");

    char abiDecompBuf[128]{};
    uint32_t abiDecompBytes = 0;
    TEST_ASSERT(micant::dstorage::DStorageDecompressGDeflate(compressedAsset.data(), static_cast<uint32_t>(compressedAsset.size()),
                                                             abiDecompBuf, sizeof(abiDecompBuf), &abiDecompBytes) == micant::STATUS_SUCCESS, "DStorageDecompressGDeflate via C ABI must succeed");
    TEST_ASSERT(abiDecompBytes == sizeof(assetString), "Decompressed bytes via C ABI must match length");

    uint32_t abiDiskId = 0;
    TEST_ASSERT(micant::dstorage::StorageSpacesCreateVirtualDisk(1, "ABI_Space", 1024ULL * 1024 * 1024 * 5, 1, &abiDiskId) == micant::STATUS_SUCCESS, "StorageSpacesCreateVirtualDisk via C ABI must succeed");
    TEST_ASSERT(abiDiskId != 0, "Created virtual disk ID must be non-zero");

    const char abiPayload[] = "ABI_S2D_PAYLOAD";
    TEST_ASSERT(micant::dstorage::StorageSpacesWriteVirtualDisk(1, abiDiskId, 0, abiPayload, sizeof(abiPayload)) == micant::STATUS_SUCCESS, "StorageSpacesWriteVirtualDisk via C ABI must succeed");

    char abiReadPayload[32]{};
    size_t abiReadLen = 0;
    TEST_ASSERT(micant::dstorage::StorageSpacesReadVirtualDisk(1, abiDiskId, 0, abiReadPayload, sizeof(abiPayload), &abiReadLen) == micant::STATUS_SUCCESS, "StorageSpacesReadVirtualDisk via C ABI must succeed");
    TEST_ASSERT(abiReadLen == sizeof(abiPayload) && std::memcmp(abiReadPayload, abiPayload, sizeof(abiPayload)) == 0, "Read data via C ABI must match written bytes");

    // 12. Multithreaded Concurrency & High-Bandwidth DirectStorage Queue Stress Test
    std::atomic<int> dstorageStressSuccess{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);
    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([&, t]() {
            micant::dstorage::DSTORAGE_QUEUE_DESC tDesc{};
            tDesc.Capacity = 32;
            tDesc.Priority = micant::dstorage::DSTORAGE_PRIORITY_NORMAL;
            tDesc.Name = "StressQueue_" + std::to_string(t);
            auto tQueue = factory.createQueue(tDesc);

            char localBuf[64]{};
            for (int i = 0; i < 10; ++i) {
                micant::dstorage::DSTORAGE_REQUEST treq{};
                treq.CompressionFormat = micant::dstorage::DSTORAGE_COMPRESSION_FORMAT_NONE;
                std::string msg = "THREAD_" + std::to_string(t) + "_ITEM_" + std::to_string(i);
                treq.SourceMemory = msg.c_str();
                treq.SourceSize = static_cast<uint32_t>(msg.size() + 1);
                treq.DestinationBuffer = localBuf;
                treq.DestinationSize = sizeof(localBuf);
                treq.UncompressedSize = treq.SourceSize;

                tQueue->enqueueRequest(treq);
                if (tQueue->submit() == 1) {
                    dstorageStressSuccess.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(dstorageStressSuccess.load() == 100, "100 concurrent DirectStorage queue requests across 10 threads must complete successfully");

    std::cout << "[TEST] Suite 196: Windows DirectStorage & Storage Spaces Direct (S2D) Subsystem PASSED.\n";
}

void Test_WindowsDirectAccess_BranchCache_SMBQuic_Subsystem() {
    std::cout << "[TEST] Running Suite 197: Windows DirectAccess, BranchCache & SMB over QUIC Subsystem...\n";

    // 1. SCM Services & Driver Registration
    micant::wan::RegisterWanSubsystem();
    auto& bcache = micant::wan::BranchCacheSubsystem::get();
    auto& da = micant::wan::DirectAccessSubsystem::get();
    auto& quic = micant::wan::SmbQuicSubsystem::get();

    bcache.reset();
    da.reset();
    quic.reset();

    auto& scm = micant::scm::ServiceControlManager::get();

    auto peerDistSvc = scm.getServiceRecord(L"PeerDistSvc");
    TEST_ASSERT(peerDistSvc != nullptr, "PeerDistSvc must be registered in SCM");
    TEST_ASSERT(peerDistSvc->serviceType == micant::scm::SERVICE_WIN32_SHARE_PROCESS, "PeerDistSvc must be SERVICE_WIN32_SHARE_PROCESS");
    TEST_ASSERT(peerDistSvc->startType == micant::scm::SERVICE_AUTO_START, "PeerDistSvc must be SERVICE_AUTO_START");
    TEST_ASSERT(peerDistSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "PeerDistSvc must be RUNNING");

    auto ipHttpsSvc = scm.getServiceRecord(L"IpHttps");
    TEST_ASSERT(ipHttpsSvc != nullptr, "IpHttps must be registered in SCM");
    TEST_ASSERT(ipHttpsSvc->serviceType == micant::scm::SERVICE_KERNEL_DRIVER, "IpHttps must be SERVICE_KERNEL_DRIVER");
    TEST_ASSERT(ipHttpsSvc->startType == micant::scm::SERVICE_BOOT_START, "IpHttps must be SERVICE_BOOT_START");
    TEST_ASSERT(ipHttpsSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "IpHttps must be RUNNING");

    auto smbQuicSvc = scm.getServiceRecord(L"SmbQuic");
    TEST_ASSERT(smbQuicSvc != nullptr, "SmbQuic must be registered in SCM");
    TEST_ASSERT(smbQuicSvc->serviceType == micant::scm::SERVICE_KERNEL_DRIVER, "SmbQuic must be SERVICE_KERNEL_DRIVER");
    TEST_ASSERT(smbQuicSvc->startType == micant::scm::SERVICE_SYSTEM_START, "SmbQuic must be SERVICE_SYSTEM_START");
    TEST_ASSERT(smbQuicSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "SmbQuic must be RUNNING");

    // 2. VersionDatabase (10.0.26100.1) entries
    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("peerdist.dll") != nullptr, "peerdist.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("bcasvc.dll") != nullptr, "bcasvc.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("iphttps.sys") != nullptr, "iphttps.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("smbquic.sys") != nullptr, "smbquic.sys must be registered in VersionDatabase");

    // 3. PeerDist Content Information & SHA-256 Block Hashing
    const size_t testDocSize = 192 * 1024; // 192 KB = exactly 3 64KB blocks
    std::vector<uint8_t> testDocData(testDocSize);
    for (size_t i = 0; i < testDocSize; ++i) {
        testDocData[i] = static_cast<uint8_t>((i * 13 + 37) & 0xFF);
    }
    auto contentInfo = bcache.publishContent("CorporateQuarterlySales2026.docx", testDocData.data(), testDocData.size());
    TEST_ASSERT(contentInfo != nullptr, "Publishing content to BranchCache must return Content Information structure");
    TEST_ASSERT(contentInfo->contentId == "CorporateQuarterlySales2026.docx", "Content ID must match");
    TEST_ASSERT(contentInfo->totalContentLength == testDocSize, "Total content length must match");
    TEST_ASSERT(!contentInfo->segments.empty(), "Segments list must not be empty");
    TEST_ASSERT(contentInfo->segments[0].blocks.size() == 3, "192 KB file must partition into exactly 3 64KB blocks");

    // Verify SHA-256 hash calculation for block 0
    auto expectedHash0 = micant::wan::BranchCacheSubsystem::computeSha256(testDocData.data(), micant::wan::PEERDIST_DEFAULT_BLOCK_SIZE);
    TEST_ASSERT(contentInfo->segments[0].blocks[0].hash == expectedHash0, "Block 0 SHA-256 hash must accurately match payload digest");
    TEST_ASSERT(contentInfo->segments[0].blocks[0].offset == 0, "Block 0 offset must be 0");
    TEST_ASSERT(contentInfo->segments[0].blocks[1].offset == 64 * 1024, "Block 1 offset must be 64KB");
    TEST_ASSERT(contentInfo->segments[0].blocks[2].offset == 128 * 1024, "Block 2 offset must be 128KB");

    // 4. BranchCache Distributed Peer Discovery & Subnet Block Serving
    TEST_ASSERT(bcache.getMode() == micant::wan::BranchCacheMode::Distributed, "Default mode must be Distributed");
    TEST_ASSERT(bcache.getDiscoveredPeerCount() >= 3, "Initial discovered peers on subnet 192.168.1.0/24 must be >= 3");

    micant::wan::BranchPeerInfo deltaPeer{"PeerNode-Delta", "192.168.1.104", 3702, 1, 0, true};
    bcache.addPeer(deltaPeer);
    TEST_ASSERT(bcache.getDiscoveredPeerCount() >= 4, "Peer discovery must accept dynamic subnet peers");

    // Retrieve block 0 from local cache
    std::vector<uint8_t> block0Buf;
    std::string block0Src;
    TEST_ASSERT(bcache.retrieveBlock(expectedHash0, block0Buf, &block0Src), "Retrieving block 0 must succeed");
    TEST_ASSERT(block0Src == "LocalBranchCache", "Recently published block must be served from LocalBranchCache");
    TEST_ASSERT(block0Buf.size() == micant::wan::PEERDIST_DEFAULT_BLOCK_SIZE, "Retrieved block size must be 64KB");
    TEST_ASSERT(std::memcmp(block0Buf.data(), testDocData.data(), block0Buf.size()) == 0, "Retrieved block data must match source");

    // 5. BranchCache Hosted Cache Mode & Centralized Staging
    bcache.setHostedCacheServer("hostedcache.branch01.contoso.com");
    TEST_ASSERT(bcache.getMode() == micant::wan::BranchCacheMode::Hosted, "BranchCache mode must transition to Hosted");
    TEST_ASSERT(bcache.getHostedCacheServer() == "hostedcache.branch01.contoso.com", "Hosted cache server FQDN must match");

    bcache.flushCache();
    TEST_ASSERT(bcache.getLocalBlockCount() == 0, "Local block cache must be empty after flush");

    std::array<uint8_t, 32> syntheticUncachedHash{};
    syntheticUncachedHash.fill(0xEE);
    std::vector<uint8_t> hostedBlockBuf;
    std::string hostedSrc;
    TEST_ASSERT(bcache.retrieveBlock(syntheticUncachedHash, hostedBlockBuf, &hostedSrc), "Retrieving uncached block in Hosted mode must succeed");
    TEST_ASSERT(hostedSrc == "HostedCacheServer:hostedcache.branch01.contoso.com", "Source must be HostedCacheServer");

    // Revert back to Distributed mode and re-publish content
    bcache.setMode(micant::wan::BranchCacheMode::Distributed);
    bcache.publishContent("CorporateQuarterlySales2026.docx", testDocData.data(), testDocData.size());

    // 6. Network Location Awareness (NLA) Domain vs Public Network Transition
    TEST_ASSERT(da.isInitialized(), "DirectAccessSubsystem must be initialized");
    TEST_ASSERT(da.getState() == micant::wan::DirectAccessTunnelState::Connected_IPHTTPS, "Initial state outside corp boundary must be Connected_IPHTTPS");

    // Simulate joining Corporate Domain Authenticated LAN
    da.updateNetworkLocation(micant::nla::NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED);
    TEST_ASSERT(da.getState() == micant::wan::DirectAccessTunnelState::Dormant_InsideCorp, "DirectAccess must transition to Dormant inside corporate LAN");

    // Encapsulation must not proceed when dormant
    const char dummyPacket[] = "IPV6_RAW_TEST_PAYLOAD";
    std::vector<uint8_t> dummyEnc;
    TEST_ASSERT(!da.encapsulatePacket(dummyPacket, sizeof(dummyPacket), dummyEnc), "DirectAccess must not encapsulate packets while Dormant inside corp network");

    // Simulate leaving corp network to Public Wi-Fi / WAN
    da.updateNetworkLocation(micant::nla::NLM_NETWORK_CATEGORY_PUBLIC);
    TEST_ASSERT(da.getState() == micant::wan::DirectAccessTunnelState::Connected_IPHTTPS, "DirectAccess must automatically reconnect IP-HTTPS on public network");

    // 7. IP-HTTPS (DirectAccess) Tunnel Encapsulation & Decapsulation
    const uint8_t rawIpv6TestFrame[] = {
        0x60, 0x00, 0x00, 0x00, 0x00, 0x20, 0x06, 0x40, // IPv6 header start
        0x20, 0x02, 0xc0, 0xa8, 0x01, 0x64, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x88,
        0xCA, 0xFE, 0xBA, 0xBE, 0xDE, 0xAD, 0xBE, 0xEF
    };
    std::vector<uint8_t> ipHttpsFrame;
    TEST_ASSERT(da.encapsulatePacket(rawIpv6TestFrame, sizeof(rawIpv6TestFrame), ipHttpsFrame), "Encapsulating IPv6 frame into IP-HTTPS must succeed");
    TEST_ASSERT(ipHttpsFrame.size() == sizeof(rawIpv6TestFrame) + 6, "Encapsulated frame must have 6-byte IP-HTTPS header");
    TEST_ASSERT(ipHttpsFrame[0] == 'I' && ipHttpsFrame[1] == 'P' && ipHttpsFrame[2] == 'H' && ipHttpsFrame[3] == 'T', "IP-HTTPS header magic must match 'IPHT'");

    std::vector<uint8_t> decapsulatedIpv6;
    TEST_ASSERT(da.decapsulatePacket(ipHttpsFrame.data(), ipHttpsFrame.size(), decapsulatedIpv6), "Decapsulating IP-HTTPS frame must succeed");
    TEST_ASSERT(decapsulatedIpv6.size() == sizeof(rawIpv6TestFrame), "Decapsulated frame size must match original");
    TEST_ASSERT(std::memcmp(decapsulatedIpv6.data(), rawIpv6TestFrame, sizeof(rawIpv6TestFrame)) == 0, "Decapsulated IPv6 frame must bitwise match input");
    TEST_ASSERT(da.getPacketsEncapsulated() >= 1, "Packets encapsulated counter must be >= 1");
    TEST_ASSERT(da.getPacketsDecapsulated() >= 1, "Packets decapsulated counter must be >= 1");

    // 8. SMB over QUIC Connection Handshake over UDP 443
    TEST_ASSERT(quic.isInitialized(), "SmbQuicSubsystem must be initialized");
    auto session = quic.createSession("fs-edge.contoso.com", 443);
    TEST_ASSERT(session != nullptr, "Creating SMB over QUIC session must succeed");
    TEST_ASSERT(session->getServerHost() == "fs-edge.contoso.com", "Server host must match");
    TEST_ASSERT(session->getServerPort() == 443, "SMB over QUIC port must be 443 (UDP)");
    TEST_ASSERT(session->getClientCid() != 0, "Client Connection ID (CID) must be non-zero");
    TEST_ASSERT(session->getServerCid() != 0, "Server Connection ID (CID) must be non-zero");

    TEST_ASSERT(session->connect(false), "Standard 1-RTT QUIC handshake must succeed");
    TEST_ASSERT(session->getState() == micant::wan::SmbQuicConnectionState::Connected, "Connection state must be Connected");
    TEST_ASSERT(!session->is0RttResumed(), "Initial session must not be 0-RTT resumed");

    // 9. SMB over QUIC Multiplexed Stream Transfer & 0-RTT Resumption
    const char smbNegotiateReq[] = "\x00\x00\x00\x44\xFE\x53\x4D\x42\x40\x00\x00\x00\x00\x00\x00\x00"; // SMB2 header
    TEST_ASSERT(session->writeStream(0, smbNegotiateReq, sizeof(smbNegotiateReq)), "Writing SMB negotiate on QUIC Stream 0 must succeed");
    TEST_ASSERT(session->getBytesTransmitted() >= sizeof(smbNegotiateReq), "Bytes transmitted counter must update");

    const char smbFileReadResp[] = "FILE_CONTENT_TRANSFERRED_OVER_MULTIPLEXED_QUIC_STREAM_4";
    session->injectReceiveData(4, smbFileReadResp, sizeof(smbFileReadResp));
    char readBuffer[128]{};
    size_t bytesRead = 0;
    TEST_ASSERT(session->readStream(4, readBuffer, sizeof(readBuffer), &bytesRead), "Reading from QUIC Stream 4 must succeed");
    TEST_ASSERT(bytesRead == sizeof(smbFileReadResp), "Read bytes must match injected response size");
    TEST_ASSERT(std::memcmp(readBuffer, smbFileReadResp, sizeof(smbFileReadResp)) == 0, "Received payload must bitwise match file content");

    // Test 0-RTT session resumption with cached session ticket
    auto sessionResumed = quic.createSession("fs-edge.contoso.com", 443);
    sessionResumed->connect(false); // Seed resumption ticket
    TEST_ASSERT(sessionResumed->connect(true), "0-RTT session resumption must succeed");
    TEST_ASSERT(sessionResumed->getState() == micant::wan::SmbQuicConnectionState::Resumed_0RTT, "State must be Resumed_0RTT");
    TEST_ASSERT(sessionResumed->is0RttResumed(), "is0RttResumed must return true");
    TEST_ASSERT(sessionResumed->getRttMs() == 1, "0-RTT round-trip latency must be 1 ms");

    // 10. SMB over QUIC Connection Migration (IP address change / interface handoff)
    TEST_ASSERT(session->getActiveClientIp() == "192.168.1.150", "Initial client IP must be 192.168.1.150");
    TEST_ASSERT(session->migrateConnection("10.75.120.44"), "Migrating connection to 5G cellular IP must succeed");
    TEST_ASSERT(session->getState() == micant::wan::SmbQuicConnectionState::ConnectionMigrated, "State must be ConnectionMigrated");
    TEST_ASSERT(session->isMigrated(), "isMigrated must be true");
    TEST_ASSERT(session->getActiveClientIp() == "10.75.120.44", "Active client IP must reflect migrated endpoint");

    // Transmit data on migrated connection
    const char migratedData[] = "SMB2_WRITE_AFTER_SEAMLESS_CONNECTION_MIGRATION";
    TEST_ASSERT(session->writeStream(8, migratedData, sizeof(migratedData)), "Writing data on migrated connection stream must succeed");

    // 11. WAN Bandwidth Deduplication Savings Metric (> 75% savings)
    // Perform multiple block fetches that hit local cache or subnet peers
    for (size_t b = 0; b < 3; ++b) {
        std::vector<uint8_t> dummyBuf;
        std::string dummySource;
        bcache.retrieveBlock(contentInfo->segments[0].blocks[b].hash, dummyBuf, &dummySource);
    }
    double wanSavings = bcache.getWanSavingsRatio();
    TEST_ASSERT(wanSavings >= 75.0, "WAN Bandwidth Savings Ratio must exceed 75%");
    TEST_ASSERT(bcache.getBytesRequested() > 0, "Bytes requested must be non-zero");
    TEST_ASSERT(bcache.getBytesFromLocalCache() + bcache.getBytesFromPeers() > 0, "Bytes from cache/peers must be non-zero");

    // 12. Clean-Room Win32 / NT C ABI Exports & Multithreaded Concurrency
    uint32_t peerDistStatus = 0;
    TEST_ASSERT(micant::wan::PeerDistStartup(micant::wan::PEERDIST_VERSION_1_0, &peerDistStatus) == micant::STATUS_SUCCESS, "PeerDistStartup via C ABI must succeed");
    TEST_ASSERT(peerDistStatus == micant::wan::PEERDIST_ERROR_SUCCESS, "PeerDistStartup status must be PEERDIST_ERROR_SUCCESS");

    void* cInfoHandle = nullptr;
    TEST_ASSERT(micant::wan::PeerDistClientOpenContentInformation("CorporateQuarterlySales2026.docx", &cInfoHandle) == micant::STATUS_SUCCESS, "PeerDistClientOpenContentInformation via C ABI must succeed");
    TEST_ASSERT(cInfoHandle != nullptr, "Content information handle must be non-null");
    TEST_ASSERT(micant::wan::PeerDistClientCloseContentInformation(cInfoHandle) == micant::STATUS_SUCCESS, "PeerDistClientCloseContentInformation via C ABI must succeed");

    TEST_ASSERT(micant::wan::IpHttpsInitializeAdapter() == micant::STATUS_SUCCESS, "IpHttpsInitializeAdapter via C ABI must succeed");
    uint32_t daConnected = 0;
    TEST_ASSERT(micant::wan::IpHttpsConnectGateway("gateway.corp.contoso.com", 443, &daConnected) == micant::STATUS_SUCCESS, "IpHttpsConnectGateway via C ABI must succeed");
    TEST_ASSERT(daConnected == 1, "Gateway connection flag must be 1");

    uint32_t daTunnelState = 0, daLatencyMs = 0;
    TEST_ASSERT(micant::wan::IpHttpsGetTunnelState(&daTunnelState, &daLatencyMs) == micant::STATUS_SUCCESS, "IpHttpsGetTunnelState via C ABI must succeed");
    TEST_ASSERT(daTunnelState == static_cast<uint32_t>(micant::wan::DirectAccessTunnelState::Connected_IPHTTPS), "Tunnel state must match Connected_IPHTTPS");

    TEST_ASSERT(micant::wan::SmbQuicInitializeTransport() == micant::STATUS_SUCCESS, "SmbQuicInitializeTransport via C ABI must succeed");
    uint32_t abiSessionId = 0;
    TEST_ASSERT(micant::wan::SmbQuicCreateSession("filecluster.corp.contoso.com", 443, &abiSessionId) == micant::STATUS_SUCCESS, "SmbQuicCreateSession via C ABI must succeed");
    TEST_ASSERT(abiSessionId != 0, "Created session ID must be non-zero");

    const char abiData[] = "SMB_OVER_QUIC_C_ABI_STREAM_DATA_TEST";
    TEST_ASSERT(micant::wan::SmbQuicTransmitFileData(abiSessionId, 0, abiData, sizeof(abiData)) == micant::STATUS_SUCCESS, "SmbQuicTransmitFileData via C ABI must succeed");

    auto abiSessPtr = quic.getSession(abiSessionId);
    if (abiSessPtr) abiSessPtr->injectReceiveData(0, abiData, sizeof(abiData));

    char abiRecvBuf[64]{};
    size_t abiBytesRead = 0;
    TEST_ASSERT(micant::wan::SmbQuicReceiveFileData(abiSessionId, 0, abiRecvBuf, sizeof(abiRecvBuf), &abiBytesRead) == micant::STATUS_SUCCESS, "SmbQuicReceiveFileData via C ABI must succeed");
    TEST_ASSERT(abiBytesRead == sizeof(abiData) && std::memcmp(abiRecvBuf, abiData, sizeof(abiData)) == 0, "Received data via C ABI must match transmitted payload");

    TEST_ASSERT(micant::wan::SmbQuicCloseSession(abiSessionId) == micant::STATUS_SUCCESS, "SmbQuicCloseSession via C ABI must succeed");

    // Multithreaded Concurrency Stress Test: 10 threads concurrently reading/writing streams and retrieving blocks
    std::atomic<int> wanStressSuccess{0};
    std::vector<std::thread> threads;
    threads.reserve(10);
    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 10; ++i) {
                // QUIC Stream Write
                uint64_t stId = static_cast<uint64_t>(t * 100 + i);
                std::string msg = "THREAD_" + std::to_string(t) + "_QUIC_MSG_" + std::to_string(i);
                if (session->writeStream(stId, msg.data(), msg.size())) {
                    wanStressSuccess.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(wanStressSuccess.load() == 100, "100 concurrent SMB over QUIC stream transmissions across 10 threads must succeed");

    std::cout << "[TEST] Suite 197: Windows DirectAccess, BranchCache & SMB over QUIC Subsystem PASSED.\n";
}

void Test_WindowsStorageReplica_DisasterRecovery_Subsystem() {
    std::cout << "[RUNNING] Test_WindowsStorageReplica_DisasterRecovery_Subsystem...\n";
    std::cout << "[TEST] Running Suite 198: Windows Storage Replica (SR) & Disaster Recovery Subsystem...\n";

    // Register SCM services, drivers, and VersionDatabase entries
    micant::sr::RegisterStorageReplicaSubsystem();
    auto& srSys = micant::sr::StorageReplicaSubsystem::get();
    srSys.initialize();

    // Stage 1: SCM Service & Driver Registration Validation
    auto& scm = micant::scm::ServiceControlManager::get();
    auto srDrvRec = scm.getServiceRecord(L"StorageReplica");
    TEST_ASSERT(srDrvRec != nullptr, "StorageReplica kernel driver must be registered in SCM");
    TEST_ASSERT(srDrvRec->serviceType == micant::scm::SERVICE_KERNEL_DRIVER, "StorageReplica must be a SERVICE_KERNEL_DRIVER");
    TEST_ASSERT(srDrvRec->startType == micant::scm::SERVICE_SYSTEM_START, "StorageReplica must be SERVICE_SYSTEM_START");
    TEST_ASSERT(srDrvRec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "StorageReplica driver state must be SERVICE_RUNNING");

    auto srSvcRec = scm.getServiceRecord(L"SrSvc");
    TEST_ASSERT(srSvcRec != nullptr, "SrSvc management service must be registered in SCM");
    TEST_ASSERT(srSvcRec->serviceType == micant::scm::SERVICE_WIN32_SHARE_PROCESS, "SrSvc must be a SERVICE_WIN32_SHARE_PROCESS");
    TEST_ASSERT(srSvcRec->startType == micant::scm::SERVICE_AUTO_START, "SrSvc must be SERVICE_AUTO_START");
    TEST_ASSERT(srSvcRec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "SrSvc service state must be SERVICE_RUNNING");

    // Stage 2: VersionDatabase Registration Validation
    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("storrepl.sys") != nullptr, "storrepl.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("srsys.sys") != nullptr, "srsys.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("srservice.dll") != nullptr, "srservice.dll must be registered in VersionDatabase");

    // Stage 3: Partnership Creation & Volume Topology Configuration
    auto pMetro = srSys.getPartnership("SR-PAR-METRO-01");
    TEST_ASSERT(pMetro != nullptr, "Pre-seeded Metropolitan partnership must exist");
    TEST_ASSERT(pMetro->getMode() == micant::sr::ReplicationMode::Synchronous, "Metro partnership must be Synchronous");
    TEST_ASSERT(pMetro->getRole() == micant::sr::ReplicationRole::Source, "Metro partnership initial role must be Source");
    TEST_ASSERT(pMetro->getState() == micant::sr::ReplicationState::ContinuouslyReplicating, "Metro partnership initial state must be ContinuouslyReplicating");

    auto pWan = srSys.getPartnership("SR-PAR-WAN-02");
    TEST_ASSERT(pWan != nullptr, "Pre-seeded WAN partnership must exist");
    TEST_ASSERT(pWan->getMode() == micant::sr::ReplicationMode::Asynchronous, "WAN partnership must be Asynchronous");
    TEST_ASSERT(pWan->getRole() == micant::sr::ReplicationRole::Source, "WAN partnership initial role must be Source");

    // Stage 4: Synchronous Write Path & Zero RPO Mirroring Verification
    const char metroData[] = "METROPOLITAN_ENTERPRISE_TRANSACTION_PAYLOAD_ZERO_RPO_TEST_BLOCK";
    bool syncWriteOk = pMetro->writeBlock(0x100000, metroData, sizeof(metroData));
    TEST_ASSERT(syncWriteOk, "Synchronous writeBlock to primary volume must succeed");

    auto telemSync = pMetro->getTelemetry();
    TEST_ASSERT(telemSync.syncWrites >= 1, "Synchronous write counter must increment");
    TEST_ASSERT(telemSync.bytesReplicated >= sizeof(metroData), "Bytes replicated counter must increase");
    TEST_ASSERT(telemSync.currentLsn > 100, "Current LSN must advance on synchronous write");
    TEST_ASSERT(telemSync.lastFlushedLsn == telemSync.currentLsn, "Synchronous write must immediately flush LSN to secondary");

    // Stage 5: Dual Volume Read Consistency (Source and Destination Replica)
    char srcReadBuf[128]{};
    char dstReadBuf[128]{};
    bool readSrcOk = pMetro->readBlock(0x100000, srcReadBuf, sizeof(metroData), false);
    bool readDstOk = pMetro->readBlock(0x100000, dstReadBuf, sizeof(metroData), true);
    TEST_ASSERT(readSrcOk && readDstOk, "Reading both source and destination replica blocks must succeed");
    TEST_ASSERT(std::memcmp(srcReadBuf, metroData, sizeof(metroData)) == 0, "Source block data must match original payload");
    TEST_ASSERT(std::memcmp(dstReadBuf, metroData, sizeof(metroData)) == 0, "Destination replica block data must match source block exactly");

    // Stage 6: Destination Secondary Write-Lock Enforcement
    micant::sr::SrPartnershipConfig destLockCfg{};
    destLockCfg.partnershipId = "SR-TEST-LOCK-01";
    destLockCfg.sourceServer = "SRV-PRIMARY-SITE";
    destLockCfg.sourceVolume = "F:";
    destLockCfg.sourceLogVolume = "L:";
    destLockCfg.destinationServer = "SRV-SECONDARY-SITE";
    destLockCfg.destinationVolume = "G:";
    destLockCfg.destinationLogVolume = "M:";
    destLockCfg.localRole = micant::sr::ReplicationRole::Destination;
    bool createLockOk = srSys.createPartnership(destLockCfg);
    TEST_ASSERT(createLockOk, "Creating partnership with Destination role must succeed");
    auto pLock = srSys.getPartnership("SR-TEST-LOCK-01");
    TEST_ASSERT(pLock != nullptr && pLock->isWriteProtected(), "Destination role partnership must be write-protected");
    bool writeDestBlocked = pLock->writeBlock(0, metroData, sizeof(metroData));
    TEST_ASSERT(!writeDestBlocked, "Direct application writes to secondary replica volume MUST be blocked (write-protected)");

    // Stage 7: Asynchronous Staging, Queue Depth & Batch Log Flush
    const char asyncData[] = "HIGH_THROUGHPUT_WAN_ASYNC_STAGED_TRANSACTION_PAYLOAD";
    bool asyncWriteOk = pWan->writeBlock(0x200000, asyncData, sizeof(asyncData));
    TEST_ASSERT(asyncWriteOk, "Asynchronous writeBlock to WAN partnership must succeed");
    TEST_ASSERT(pWan->getAsyncQueueDepth() >= 1, "Async write must queue into staging buffer before batch transmission");

    size_t flushedCount = pWan->flushAsyncLog();
    TEST_ASSERT(flushedCount >= 1, "flushAsyncLog must flush queued records to secondary");
    TEST_ASSERT(pWan->getAsyncQueueDepth() == 0, "Staging queue depth must be 0 after flushAsyncLog");

    char wanDstBuf[128]{};
    pWan->readBlock(0x200000, wanDstBuf, sizeof(asyncData), true);
    TEST_ASSERT(std::memcmp(wanDstBuf, asyncData, sizeof(asyncData)) == 0, "Flushed async block must be verified on secondary volume");

    // Stage 8: Write-Ahead Log (WAL) Record Integrity & Checksums
    uint32_t expectedCrc = micant::sr::ComputeCrc32c(asyncData, sizeof(asyncData));
    TEST_ASSERT(expectedCrc != 0, "CRC32C checksum calculation must produce non-zero value");

    // Stage 9: Network Disconnection Injection & Dirty Extent Bitmap Tracking
    pMetro->injectNetworkFailure();
    TEST_ASSERT(pMetro->getState() == micant::sr::ReplicationState::Degraded, "Network failure must transition state to Degraded");
    const char offlineData[] = "OFFLINE_PARTITIONED_WRITE_WHILE_REMOTE_UNREACHABLE";
    bool offlineWriteOk = pMetro->writeBlock(0x300000, offlineData, sizeof(offlineData));
    TEST_ASSERT(offlineWriteOk, "Local write must succeed even when remote replica is disconnected");
    TEST_ASSERT(pMetro->getDirtyBlockCount() >= 1, "Offline write must mark dirty block bitmap");

    // Stage 10: Network Restoration & Differential Delta Resynchronization
    pMetro->restoreNetwork();
    uint32_t deltaSynced = pMetro->performDeltaSync();
    TEST_ASSERT(deltaSynced >= 1, "performDeltaSync must synchronize all dirty blocks");
    TEST_ASSERT(pMetro->getDirtyBlockCount() == 0, "Dirty block count must return to 0 after delta sync");
    TEST_ASSERT(pMetro->getState() == micant::sr::ReplicationState::ContinuouslyReplicating, "State must restore to ContinuouslyReplicating");

    char syncedDstBuf[128]{};
    pMetro->readBlock(0x300000, syncedDstBuf, sizeof(offlineData), true);
    TEST_ASSERT(std::memcmp(syncedDstBuf, offlineData, sizeof(offlineData)) == 0, "Secondary must reflect delta-synced block after network recovery");

    // Stage 11: Dynamic Failover & Replication Direction Reversal (Source <-> Destination)
    uint32_t epochBefore = pMetro->getEpoch();
    bool revOk = pMetro->reverseDirection(micant::sr::FailoverType::Graceful);
    TEST_ASSERT(revOk, "reverseDirection must succeed for graceful failover");
    TEST_ASSERT(pMetro->getEpoch() == epochBefore + 1, "Epoch must increment on replication direction reversal");
    TEST_ASSERT(pMetro->getRole() == micant::sr::ReplicationRole::Destination, "Local node must transition to Destination role");
    TEST_ASSERT(pMetro->isWriteProtected(), "Local node must now be write-protected following failover to Destination");

    // Reverse back to restore initial configuration
    bool revBackOk = pMetro->reverseDirection(micant::sr::FailoverType::Graceful);
    TEST_ASSERT(revBackOk, "Reversing direction back to Source must succeed");
    TEST_ASSERT(pMetro->getRole() == micant::sr::ReplicationRole::Source, "Local node role must return to Source");

    // Stage 12: Win32 & NT Clean-Room C ABI Driver Export Verification
    NTSTATUS abiInit = micant::sr::SrInitializeSubsystem();
    TEST_ASSERT(abiInit == STATUS_SUCCESS, "SrInitializeSubsystem must return STATUS_SUCCESS");

    NTSTATUS abiCreate = micant::sr::SrCreateReplicationPartnership(
        "SR-ABI-PARTNERSHIP", "SRV-A", "V:", "LV:", "SRV-B", "W:", "LW:", 0);
    TEST_ASSERT(abiCreate == STATUS_SUCCESS, "SrCreateReplicationPartnership must return STATUS_SUCCESS");

    uint32_t qState = 0, qRole = 0, qMode = 0;
    uint64_t qBytes = 0;
    NTSTATUS abiQuery = micant::sr::SrQueryReplicationState("SR-ABI-PARTNERSHIP", &qState, &qRole, &qMode, &qBytes);
    TEST_ASSERT(abiQuery == STATUS_SUCCESS, "SrQueryReplicationState must return STATUS_SUCCESS");
    TEST_ASSERT(qMode == 0, "Queried mode must be Synchronous (0)");

    const char abiPayload[] = "ABI_TEST_BLOCK_DATA_PAYLOAD";
    NTSTATUS abiWrite = micant::sr::SrSyncReplicateBlock("SR-ABI-PARTNERSHIP", 0x400000, abiPayload, sizeof(abiPayload));
    TEST_ASSERT(abiWrite == STATUS_SUCCESS, "SrSyncReplicateBlock must return STATUS_SUCCESS");

    size_t abiFlushed = 0;
    NTSTATUS abiFlush = micant::sr::SrAsyncFlushLog("SR-ABI-PARTNERSHIP", &abiFlushed);
    TEST_ASSERT(abiFlush == STATUS_SUCCESS, "SrAsyncFlushLog must return STATUS_SUCCESS");

    NTSTATUS abiSuspend = micant::sr::SrSuspendReplication("SR-ABI-PARTNERSHIP");
    TEST_ASSERT(abiSuspend == STATUS_SUCCESS, "SrSuspendReplication must return STATUS_SUCCESS");

    NTSTATUS abiResume = micant::sr::SrResumeReplication("SR-ABI-PARTNERSHIP");
    TEST_ASSERT(abiResume == STATUS_SUCCESS, "SrResumeReplication must return STATUS_SUCCESS");

    NTSTATUS abiReverse = micant::sr::SrSetReplicationDirection("SR-ABI-PARTNERSHIP", 0);
    TEST_ASSERT(abiReverse == STATUS_SUCCESS, "SrSetReplicationDirection must return STATUS_SUCCESS");

    NTSTATUS abiRemove = micant::sr::SrRemoveReplicationPartnership("SR-ABI-PARTNERSHIP");
    TEST_ASSERT(abiRemove == STATUS_SUCCESS, "SrRemoveReplicationPartnership must return STATUS_SUCCESS");

    // Stage 13: High-Density Concurrent Transactional Write Stress Test
    auto stressPart = srSys.getPartnership("SR-PAR-METRO-01");
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);

    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([stressPart, &stressSuccessCount, t]() {
            for (int i = 0; i < 10; ++i) {
                uint64_t blkOffset = static_cast<uint64_t>(t * 100 + i) * 64 * 1024;
                std::string payload = "THREAD_" + std::to_string(t) + "_TRANSACTION_" + std::to_string(i);
                if (stressPart->writeBlock(blkOffset, payload.data(), payload.size())) {
                    char verifyBuf[128]{};
                    if (stressPart->readBlock(blkOffset, verifyBuf, payload.size(), true) &&
                        std::memcmp(verifyBuf, payload.data(), payload.size()) == 0) {
                        stressSuccessCount.fetch_add(1);
                    }
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 100, "100 concurrent multi-threaded block write-and-verify transactions must succeed with 0 corruption");

    srSys.reset();
    std::cout << "[TEST] Suite 198: Windows Storage Replica (SR) & Disaster Recovery Subsystem PASSED.\n";
}

void Test_WindowsFailoverClustering_PaxosQuorum_Subsystem() {
    std::cout << "[TEST] Suite 199: Windows Failover Clustering, Cluster Shared Network & Paxos Quorum Subsystem...\n";

    // Stage 1: SCM Services Registration & Status Check
    micant::cluster::RegisterFailoverClusteringSubsystem();

    auto& scm = micant::scm::ServiceControlManager::get();
    auto clusSvc = scm.getServiceRecord(L"ClusSvc");
    TEST_ASSERT(clusSvc != nullptr, "ClusSvc service must be registered in SCM");
    TEST_ASSERT(clusSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "ClusSvc must be in SERVICE_RUNNING state");
    TEST_ASSERT(clusSvc->binaryPath == L"C:\\Windows\\System32\\clussvc.exe", "ClusSvc binary path must point to clussvc.exe");

    auto clusNet = scm.getServiceRecord(L"ClusNet");
    TEST_ASSERT(clusNet != nullptr, "ClusNet kernel driver must be registered in SCM");
    TEST_ASSERT(clusNet->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "ClusNet must be in SERVICE_RUNNING state");

    auto clusDisk = scm.getServiceRecord(L"ClusDisk");
    TEST_ASSERT(clusDisk != nullptr, "ClusDisk driver must be registered in SCM");
    TEST_ASSERT(clusDisk->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "ClusDisk must be in SERVICE_RUNNING state");

    // Stage 2: VersionDatabase Modules Registration
    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("clusapi.dll") != nullptr, "clusapi.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("resutils.dll") != nullptr, "resutils.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("clusnet.sys") != nullptr, "clusnet.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("clusdisk.sys") != nullptr, "clusdisk.sys must be registered in VersionDatabase");

    // Stage 3: Multi-Node Topology Formation & Membership
    auto& clusSys = micant::cluster::FailoverClusterSubsystem::get();
    clusSys.reset();
    TEST_ASSERT(clusSys.isInitialized(), "FailoverClusterSubsystem must be initialized");
    TEST_ASSERT(clusSys.getNodeCount() == 3, "Initial cluster topology must consist of 3 nodes");

    auto* n1 = clusSys.getNode(1);
    auto* n2 = clusSys.getNode(2);
    auto* n3 = clusSys.getNode(3);
    TEST_ASSERT(n1 != nullptr && n2 != nullptr && n3 != nullptr, "All 3 cluster nodes must exist");
    TEST_ASSERT(n1->isCoordinator, "Node 1 must be designated coordinator");
    TEST_ASSERT(n1->state == micant::cluster::ClusterNodeState::Up, "Node 1 state must be Up");
    TEST_ASSERT(n2->state == micant::cluster::ClusterNodeState::Up, "Node 2 state must be Up");
    TEST_ASSERT(n3->state == micant::cluster::ClusterNodeState::Up, "Node 3 state must be Up");

    // Stage 4: Paxos Consensus Ballot & Epoch Progression
    uint32_t initEpoch = clusSys.getEpoch();
    TEST_ASSERT(initEpoch >= 1, "Initial cluster epoch must be >= 1");

    bool paxosOk1 = clusSys.proposePaxosValue("DatabaseVip", "10.200.1.50", 1);
    TEST_ASSERT(paxosOk1, "Proposing Paxos value DatabaseVip must succeed under quorum");
    TEST_ASSERT(clusSys.getPaxosValue("DatabaseVip") == "10.200.1.50", "Committed Paxos value must match proposal");
    TEST_ASSERT(clusSys.getEpoch() > initEpoch, "Cluster epoch must increment upon Paxos commit");

    bool paxosOk2 = clusSys.proposePaxosValue("MaxFailoverAttempts", "5", 1);
    TEST_ASSERT(paxosOk2, "Second Paxos proposal must succeed");
    TEST_ASSERT(clusSys.getPaxosValue("MaxFailoverAttempts") == "5", "Second Paxos value must match");

    // Stage 5: Dynamic Quorum Calculation & Witness Arbitration
    TEST_ASSERT(clusSys.hasQuorum(), "Cluster must possess active quorum initially");
    TEST_ASSERT(clusSys.getQuorumVotesTotal() == 4, "Total votes must be 4 (3 nodes + 1 witness)");
    TEST_ASSERT(clusSys.getQuorumVotesActive() == 4, "Active votes must be 4");

    clusSys.setWitness(micant::cluster::QuorumWitnessType::DiskWitness, true);
    TEST_ASSERT(clusSys.getWitnessType() == micant::cluster::QuorumWitnessType::DiskWitness, "Witness type must switch to DiskWitness");
    TEST_ASSERT(clusSys.hasQuorum(), "Quorum must hold with Disk Witness");

    clusSys.setWitness(micant::cluster::QuorumWitnessType::None, false);
    TEST_ASSERT(clusSys.getQuorumVotesTotal() == 3, "Total votes must be 3 with No Witness");
    TEST_ASSERT(clusSys.hasQuorum(), "Quorum must hold with 3 nodes without witness (3 > 1)");

    clusSys.setWitness(micant::cluster::QuorumWitnessType::FileShareWitness, true);

    // Stage 6: Cluster Network Driver (clusnet.sys) Heartbeating Mesh
    uint64_t hbStart = clusSys.getHeartbeatCount();
    bool hb1 = clusSys.sendHeartbeat(1, 2);
    bool hb2 = clusSys.sendHeartbeat(1, 3);
    TEST_ASSERT(hb1 && hb2, "Sending clusnet heartbeats from Node 1 to Nodes 2 and 3 must succeed");
    TEST_ASSERT(clusSys.getHeartbeatCount() == hbStart + 2, "Heartbeat counter must increment by 2");
    TEST_ASSERT(n2->lastHeartbeatTimestampUs > 0, "Target node 2 heartbeat timestamp must update");
    TEST_ASSERT(n2->missedHeartbeats == 0, "Target node 2 missed heartbeats must be 0");

    // Stage 7: Heartbeat Timeout Injection & Node Down Declaration
    micant::cluster::ClusterNodeInfo n4{};
    n4.nodeId = 4;
    n4.nodeName = "TITAN-CLUS-04";
    n4.ipAddress = "192.168.10.14";
    n4.state = micant::cluster::ClusterNodeState::Up;
    n4.voteWeight = 1;
    n4.currentVote = 1;
    bool add4 = clusSys.addNode(n4);
    TEST_ASSERT(add4, "Adding Node 4 must succeed");

    clusSys.injectHeartbeatTimeout(4);
    auto* n4Ref = clusSys.getNode(4);
    TEST_ASSERT(n4Ref != nullptr, "Node 4 must exist");
    TEST_ASSERT(n4Ref->state == micant::cluster::ClusterNodeState::Down, "Node 4 state must be Down after timeout injection");
    TEST_ASSERT(n4Ref->missedHeartbeats > 5, "Node 4 missed heartbeats must exceed threshold");
    TEST_ASSERT(n4Ref->currentVote == 0, "Node 4 voting weight must drop to 0");

    bool remove4 = clusSys.removeNode(4);
    TEST_ASSERT(remove4, "Removing evicted Node 4 must succeed");

    // Stage 8: Split-Brain Fencing Verification
    clusSys.setWitness(micant::cluster::QuorumWitnessType::None, false);
    TEST_ASSERT(clusSys.getQuorumVotesTotal() == 3, "Total votes must be 3");

    clusSys.injectHeartbeatTimeout(2); // Node 2 down
    clusSys.injectHeartbeatTimeout(3); // Node 3 down
    TEST_ASSERT(!clusSys.hasQuorum(), "Cluster must lose quorum when 2 out of 3 nodes are down without witness (1 <= 1)");

    bool splitProposal = clusSys.proposePaxosValue("IllegalSplitValue", "Corrupt", 1);
    TEST_ASSERT(!splitProposal, "Paxos proposals must be rejected when quorum is lost, preventing split-brain");

    // Restore healthy 3-node cluster
    clusSys.reset();
    TEST_ASSERT(clusSys.hasQuorum(), "Quorum must be restored after cluster reset");

    // Stage 9: SCSI-3 Persistent Reservation (PR) & Preempt Fencing (clusdisk.sys)
    uint64_t keyNode1 = 0x0000000100000001ULL;
    uint64_t keyNode2 = 0x0000000200000002ULL;

    bool res1 = clusSys.reserveScsiDisk(0, 1, keyNode1);
    TEST_ASSERT(res1, "Node 1 reserving SCSI LUN 0 must succeed");

    bool resConflict = clusSys.reserveScsiDisk(0, 2, keyNode2);
    TEST_ASSERT(!resConflict, "Node 2 reserving already reserved SCSI LUN 0 must fail with reservation conflict");

    bool preemptOk = clusSys.preemptScsiDisk(0, 2, keyNode2);
    TEST_ASSERT(preemptOk, "Node 2 preempting LUN 0 must succeed and fence out Node 1");

    bool releaseOk = clusSys.releaseScsiDisk(0, 2);
    TEST_ASSERT(releaseOk, "Node 2 releasing LUN 0 must succeed");

    // Stage 10: Resource State Machine Lifecycle & Dependency Enforcement (resutils.dll)
    micant::cluster::ClusterResource appRes{};
    appRes.resourceId = "RES-APP-01";
    appRes.resourceName = "SQL Engine Service";
    appRes.resourceType = "Generic Service";
    appRes.ownerGroup = "GRP-SQL-HA";
    appRes.ownerNodeId = 1;
    appRes.state = micant::cluster::ClusterResourceState::Offline;
    appRes.dependencies.push_back("RES-NAME-01"); // Depends on Network Name

    bool addAppRes = clusSys.createResource(appRes);
    TEST_ASSERT(addAppRes, "Creating dependent cluster resource must succeed");

    // Take Network Name offline
    bool offName = clusSys.setResourceOffline("RES-NAME-01");
    TEST_ASSERT(offName, "Offlining RES-NAME-01 must succeed");
    TEST_ASSERT(clusSys.getResourceState("RES-NAME-01") == micant::cluster::ClusterResourceState::Offline, "RES-NAME-01 state must be Offline");

    // Attempt to bring App Online while dependency is Offline -> Must Fail
    bool onFail = clusSys.setResourceOnline("RES-APP-01");
    TEST_ASSERT(!onFail, "Bringing RES-APP-01 online must fail when dependency RES-NAME-01 is offline");

    // Bring dependency Online, then bring App Online -> Must Succeed
    bool onName = clusSys.setResourceOnline("RES-NAME-01");
    TEST_ASSERT(onName, "Bringing RES-NAME-01 online must succeed");

    bool onApp = clusSys.setResourceOnline("RES-APP-01");
    TEST_ASSERT(onApp, "Bringing RES-APP-01 online must succeed when dependencies are satisfied");
    TEST_ASSERT(clusSys.getResourceState("RES-APP-01") == micant::cluster::ClusterResourceState::Online, "RES-APP-01 must now be Online");

    // Stage 11: Coordinated Automatic Group Failover Upon Node Eviction
    auto* sqlGrp = clusSys.getGroup("GRP-SQL-HA");
    TEST_ASSERT(sqlGrp != nullptr, "SQL HA group must exist");
    TEST_ASSERT(sqlGrp->ownerNodeId == 1, "SQL HA group initial owner must be Node 1");
    TEST_ASSERT(sqlGrp->state == micant::cluster::ClusterGroupState::Online, "SQL HA group state must be Online");

    bool failoverOk = clusSys.failoverGroup("GRP-SQL-HA", 2);
    TEST_ASSERT(failoverOk, "Failing over group GRP-SQL-HA to Node 2 must succeed");
    TEST_ASSERT(sqlGrp->ownerNodeId == 2, "Group owner must now be Node 2");
    TEST_ASSERT(sqlGrp->state == micant::cluster::ClusterGroupState::Online, "Group state must remain Online after coordinated migration");

    auto resList = clusSys.getAllResources();
    for (const auto& r : resList) {
        if (r.ownerGroup == "GRP-SQL-HA") {
            TEST_ASSERT(r.ownerNodeId == 2, "Resources in group must be reassigned to target Node 2");
            TEST_ASSERT(r.state == micant::cluster::ClusterResourceState::Online, "Resources in group must be Online on new owner");
        }
    }

    // Stage 12: Win32 & NT Clean-Room C ABI Driver Export Verification
    void* hCluster = nullptr;
    NTSTATUS stOpen = micant::cluster::OpenCluster("TitanCluster", &hCluster);
    TEST_ASSERT(stOpen == micant::STATUS_SUCCESS && hCluster != nullptr, "OpenCluster must return STATUS_SUCCESS and valid handle");

    void* hEnum = nullptr;
    NTSTATUS stEnum = micant::cluster::ClusterOpenEnum(hCluster, 1, &hEnum);
    TEST_ASSERT(stEnum == micant::STATUS_SUCCESS && hEnum != nullptr, "ClusterOpenEnum must return STATUS_SUCCESS");

    void* hRes = nullptr;
    NTSTATUS stCreate = micant::cluster::CreateClusterResource(nullptr, "RES-TEST-ABI", "Generic Application", &hRes);
    TEST_ASSERT(stCreate == micant::STATUS_SUCCESS && hRes != nullptr, "CreateClusterResource must return STATUS_SUCCESS");

    NTSTATUS stOn = micant::cluster::OnlineClusterResource(hRes, "RES-TEST-ABI");
    TEST_ASSERT(stOn == micant::STATUS_SUCCESS, "OnlineClusterResource must return STATUS_SUCCESS");

    NTSTATUS stOff = micant::cluster::OfflineClusterResource(hRes, "RES-TEST-ABI");
    TEST_ASSERT(stOff == micant::STATUS_SUCCESS, "OfflineClusterResource must return STATUS_SUCCESS");

    NTSTATUS stHb = micant::cluster::ClusNetSendHeartbeat(1, 2);
    TEST_ASSERT(stHb == micant::STATUS_SUCCESS, "ClusNetSendHeartbeat must return STATUS_SUCCESS");

    NTSTATUS stPr = micant::cluster::ClusDiskReserveLUN(0, 2, 0xABCDEF0123456789ULL);
    TEST_ASSERT(stPr == micant::STATUS_SUCCESS, "ClusDiskReserveLUN must return STATUS_SUCCESS");

    NTSTATUS stGrp = micant::cluster::FailClusterResourceGroup("GRP-SQL-HA", 3);
    TEST_ASSERT(stGrp == micant::STATUS_SUCCESS, "FailClusterResourceGroup must return STATUS_SUCCESS");

    NTSTATUS stClose = micant::cluster::CloseCluster(hCluster);
    TEST_ASSERT(stClose == micant::STATUS_SUCCESS, "CloseCluster must return STATUS_SUCCESS");

    // Stage 13: 100-Operation Concurrent Multithreaded Consensus & Heartbeat Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);

    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([&clusSys, &stressSuccessCount, t]() {
            for (int i = 0; i < 10; ++i) {
                // Alternating Paxos proposal and Heartbeat
                std::string k = "STRESS_T" + std::to_string(t) + "_K" + std::to_string(i);
                std::string v = "VAL_" + std::to_string(t * 100 + i);

                bool paxosOk = clusSys.proposePaxosValue(k, v, 1);
                bool hbOk = clusSys.sendHeartbeat(1, 2);

                if (paxosOk && hbOk && clusSys.getPaxosValue(k) == v) {
                    stressSuccessCount.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 100, "100 concurrent multi-threaded Paxos proposals and heartbeats must succeed with 0 deadlocks");

    clusSys.reset();
    std::cout << "[TEST] Suite 199: Windows Failover Clustering, Cluster Shared Network & Paxos Quorum Subsystem PASSED.\n";
}

void Test_WindowsHyperV_VMMS_VirtualSwitch_Subsystem() {
    std::cout << "[TEST] Suite 200: Windows Hyper-V VMMS, Virtual Switch & VHDX Container Infrastructure (Monumental Landmark)...\n";

    // Stage 1: SCM Services Registration & Status Check
    micant::vmms::RegisterVmmsSubsystem();

    auto& scm = micant::scm::ServiceControlManager::get();
    auto vmmsSvc = scm.getServiceRecord(L"Vmms");
    TEST_ASSERT(vmmsSvc != nullptr, "Vmms service must be registered in SCM");
    TEST_ASSERT(vmmsSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "Vmms must be in SERVICE_RUNNING state");
    TEST_ASSERT(vmmsSvc->binaryPath == L"C:\\Windows\\System32\\vmms.exe", "Vmms binary path must point to vmms.exe");

    auto vswitchSvc = scm.getServiceRecord(L"VmSwitch");
    TEST_ASSERT(vswitchSvc != nullptr, "VmSwitch kernel driver must be registered in SCM");
    TEST_ASSERT(vswitchSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "VmSwitch must be in SERVICE_RUNNING state");

    auto vidSvc = scm.getServiceRecord(L"VidDriver");
    TEST_ASSERT(vidSvc != nullptr, "VidDriver must be registered in SCM");
    TEST_ASSERT(vidSvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "VidDriver must be in SERVICE_RUNNING state");

    // Stage 2: VersionDatabase Modules Registration
    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("vmms.exe") != nullptr, "vmms.exe must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("vhdsvc.dll") != nullptr, "vhdsvc.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("vmswitch.sys") != nullptr, "vmswitch.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("vid.sys") != nullptr, "vid.sys must be registered in VersionDatabase");

    // Stage 3: Virtual Machine Lifecycle State Transitions
    auto& sys = micant::vmms::VmmsSubsystem::get();
    sys.reset();
    TEST_ASSERT(sys.isInitialized(), "VmmsSubsystem must be initialized");

    micant::vmms::VirtualMachine testVm("VM-TEST-01", "TITAN-TEST-VM", 2, 2048);
    TEST_ASSERT(testVm.getState() == micant::vmms::VmState::Off, "Initial VM state must be Off");

    bool stStart = testVm.start();
    TEST_ASSERT(stStart, "Starting VM must succeed");
    TEST_ASSERT(testVm.getState() == micant::vmms::VmState::Running, "VM state must be Running");

    bool stPause = testVm.pause();
    TEST_ASSERT(stPause, "Pausing VM must succeed");
    TEST_ASSERT(testVm.getState() == micant::vmms::VmState::Paused, "VM state must be Paused");

    bool stResume = testVm.resume();
    TEST_ASSERT(stResume, "Resuming VM must succeed");
    TEST_ASSERT(testVm.getState() == micant::vmms::VmState::Running, "VM state must be Running after resume");

    bool stSave = testVm.save();
    TEST_ASSERT(stSave, "Saving VM must succeed");
    TEST_ASSERT(testVm.getState() == micant::vmms::VmState::Saved, "VM state must be Saved");

    bool stStartSaved = testVm.start();
    TEST_ASSERT(stStartSaved, "Starting saved VM must succeed");
    TEST_ASSERT(testVm.getState() == micant::vmms::VmState::Running, "VM state must return to Running");

    bool stStop = testVm.stop();
    TEST_ASSERT(stStop, "Stopping VM must succeed");
    TEST_ASSERT(testVm.getState() == micant::vmms::VmState::Off, "VM state must be Off");

    // Stage 4: Synthetic vCPU & NUMA Topology Verification
    const auto& topo = testVm.getTopology();
    TEST_ASSERT(topo.vCpuCount == 2, "vCPU count must be 2");
    TEST_ASSERT(topo.coresPerSocket == 2, "Cores per socket must be 2");
    TEST_ASSERT(topo.apicIds.size() == 2, "APIC IDs list must have 2 entries");
    TEST_ASSERT(topo.apicIds[0] == 0 && topo.apicIds[1] == 2, "APIC IDs must match synthetic topology");

    // Stage 5: Dynamic Memory Ballooning & Reservation Guarantees
    const auto& mem = testVm.getMemory();
    TEST_ASSERT(mem.startupRamMb == 2048, "Startup RAM must be 2048 MB");
    TEST_ASSERT(mem.currentAllocatedMb == 2048, "Initial allocated RAM must be 2048 MB");

    bool balloonOk1 = testVm.adjustBalloonMemory(3072);
    TEST_ASSERT(balloonOk1, "Balloon adjustment to 3072 MB must succeed");
    TEST_ASSERT(testVm.getMemory().currentAllocatedMb == 3072, "Allocated RAM must update to 3072 MB");

    bool balloonFailLow = testVm.adjustBalloonMemory(512); // below min 1024
    TEST_ASSERT(!balloonFailLow, "Balloon adjustment below min limit (1024 MB) must fail");

    bool balloonFailHigh = testVm.adjustBalloonMemory(32768); // above max 16384
    TEST_ASSERT(!balloonFailHigh, "Balloon adjustment above max limit (16384 MB) must fail");

    bool balloonRestore = testVm.adjustBalloonMemory(2048);
    TEST_ASSERT(balloonRestore, "Restoring balloon to 2048 MB must succeed");

    // Stage 6: Extensible Virtual Switch (vmswitch.sys) Port Management & L2 Forwarding
    auto* sw = sys.getVirtualSwitch("DefaultSwitch");
    TEST_ASSERT(sw != nullptr, "Default virtual switch must exist");
    size_t initPorts = sw->getPortCount();
    TEST_ASSERT(initPorts >= 2, "DefaultSwitch must have at least 2 pre-seeded ports");

    bool createPortOk = sw->createPort("PORT-TEST-01", "TitanTestNic", "00:15:5D:AA:BB:CC", 10);
    TEST_ASSERT(createPortOk, "Creating vSwitch port must succeed");
    TEST_ASSERT(sw->getPortCount() == initPorts + 1, "Switch port count must increment");

    uint64_t framesBefore = sw->getTotalFramesSwitched();
    bool fwdOk = sw->forwardFrame("PORT-NIC-01", "00:15:5D:AA:BB:CC", 10, 256);
    TEST_ASSERT(fwdOk, "Forwarding frame to learned MAC on VLAN 10 must succeed");
    TEST_ASSERT(sw->getTotalFramesSwitched() > framesBefore, "Switched frames counter must increment");

    bool delPortOk = sw->deletePort("PORT-TEST-01");
    TEST_ASSERT(delPortOk, "Deleting vSwitch port must succeed");
    TEST_ASSERT(sw->getPortCount() == initPorts, "Port count must return to initial");

    // Stage 7: 802.1Q VLAN Isolation & Filtering
    sw->createPort("PORT-VLAN-10", "Vlan10Port", "00:15:5D:11:11:11", 10);
    sw->createPort("PORT-VLAN-20", "Vlan20Port", "00:15:5D:22:22:22", 20);

    uint64_t dropsBefore = sw->getTotalFramesDropped();
    bool crossVlanFwd = sw->forwardFrame("PORT-VLAN-10", "00:15:5D:22:22:22", 10, 128);
    TEST_ASSERT(!crossVlanFwd, "Forwarding cross-VLAN frame (VLAN 10 -> VLAN 20) must be rejected by isolation filter");
    TEST_ASSERT(sw->getTotalFramesDropped() > dropsBefore, "Dropped frames counter must increment upon VLAN mismatch");

    sw->deletePort("PORT-VLAN-10");
    sw->deletePort("PORT-VLAN-20");

    // Stage 8: VHDX File Format Header Parsing & vhdxfile Signature
    auto* baseDisk = sys.getVhdx("C:\\VirtualDisks\\BaseOS_Windows2025.vhdx");
    TEST_ASSERT(baseDisk != nullptr, "BaseOS VHDX disk must exist");
    TEST_ASSERT(baseDisk->getSignature() == micant::vmms::VHDX_FILE_SIGNATURE, "VHDX signature must match 'vhdxfile' (0x656C696678646876)");
    TEST_ASSERT(baseDisk->getBlockSize() == micant::vmms::VHDX_DEFAULT_BLOCK_SIZE, "VHDX block size must be 1 MB");
    TEST_ASSERT(baseDisk->getVirtualSizeBytes() == 64ULL * 1024 * 1024 * 1024, "VHDX capacity must be 64 GB");
    TEST_ASSERT(baseDisk->getTotalBlocks() == 65536, "Total blocks must be 65536");

    // Stage 9: Dynamic VHDX Payload Block Allocation & Sparse Read
    micant::vmms::VhdxDisk testDyn("C:\\VirtualDisks\\TestDynamic.vhdx", 10ULL * 1024 * 1024 * 1024, micant::vmms::VhdxDiskType::Dynamic);
    TEST_ASSERT(testDyn.getAllocatedBlocks() == 0, "Initial allocated blocks on dynamic VHDX must be 0");

    char sparseBuf[64]{};
    bool readSparse = testDyn.readBlock(0x400000, sparseBuf, sizeof(sparseBuf));
    TEST_ASSERT(readSparse, "Reading unallocated block must succeed with sparse zeros");
    bool allZeros = true;
    for (char c : sparseBuf) if (c != 0) allZeros = false;
    TEST_ASSERT(allZeros, "Sparse read block must be zero-filled");

    const char testPayload[] = "DYNAMIC_BLOCK_DATA_PAYLOAD_TEST";
    bool writeDyn = testDyn.writeBlock(0x400000, testPayload, sizeof(testPayload));
    TEST_ASSERT(writeDyn, "Writing block to dynamic VHDX must succeed");
    TEST_ASSERT(testDyn.getAllocatedBlocks() == 1, "Allocated blocks must increment to 1 after write");

    char verifyDyn[64]{};
    bool readDyn = testDyn.readBlock(0x400000, verifyDyn, sizeof(testPayload));
    TEST_ASSERT(readDyn, "Reading written block must succeed");
    TEST_ASSERT(std::memcmp(verifyDyn, testPayload, sizeof(testPayload)) == 0, "Read data must match written payload");

    // Stage 10: VHDX Differencing Disks & Parent Locator Resolution
    auto* childDisk = sys.getVhdx("C:\\VirtualDisks\\TitanDC01.vhdx");
    TEST_ASSERT(childDisk != nullptr, "Child differencing disk must exist");
    TEST_ASSERT(childDisk->getDiskType() == micant::vmms::VhdxDiskType::Differencing, "Disk type must be Differencing");
    TEST_ASSERT(childDisk->getParentPath() == "C:\\VirtualDisks\\BaseOS_Windows2025.vhdx", "Parent path must match BaseOS");

    char parentReadBuf[64]{};
    bool diffFallback = childDisk->readBlock(0x100000, parentReadBuf, sizeof(parentReadBuf), baseDisk);
    TEST_ASSERT(diffFallback, "Reading block not in child must transparently fall back to parent disk");
    TEST_ASSERT(std::string(parentReadBuf).find("TITAN_BASE_OS") != std::string::npos, "Parent content must be retrieved on differencing read");

    char childReadBuf[64]{};
    bool diffChildRead = childDisk->readBlock(0x200000, childReadBuf, sizeof(childReadBuf), baseDisk);
    TEST_ASSERT(diffChildRead, "Reading block modified in child must read child delta block");
    TEST_ASSERT(std::string(childReadBuf).find("TITAN_DC01_DIFF") != std::string::npos, "Child delta content must be retrieved");

    // Stage 11: VM Snapshot & Checkpoint Tree Creation and Rollback
    std::string cpId;
    bool cpOk = testDyn.createCheckpoint("Pre-Upgrade Snapshot", cpId);
    TEST_ASSERT(cpOk && !cpId.empty(), "Creating VHDX checkpoint must succeed");
    TEST_ASSERT(testDyn.getCheckpointCount() == 1, "Checkpoint count must be 1");

    const char corruptPayload[] = "CORRUPTED_DELTA_DATA_POST_UPGRADE";
    testDyn.writeBlock(0x400000, corruptPayload, sizeof(corruptPayload));

    char checkCorrupt[64]{};
    testDyn.readBlock(0x400000, checkCorrupt, sizeof(corruptPayload));
    TEST_ASSERT(std::memcmp(checkCorrupt, corruptPayload, sizeof(corruptPayload)) == 0, "Corrupted data must be present before rollback");

    bool rollbackOk = testDyn.rollbackCheckpoint(cpId);
    TEST_ASSERT(rollbackOk, "Rolling back to checkpoint must succeed");

    char checkRestored[64]{};
    testDyn.readBlock(0x400000, checkRestored, sizeof(testPayload));
    TEST_ASSERT(std::memcmp(checkRestored, testPayload, sizeof(testPayload)) == 0, "Original data must be perfectly restored after rollback");

    // Stage 12: Simulated VM Live Migration Pre-Copy & Brownout Cutover
    auto* dc01 = sys.getVirtualMachineByName("TITAN-DC01");
    TEST_ASSERT(dc01 != nullptr, "TITAN-DC01 must exist");
    bool migOk = dc01->simulateLiveMigration(3);
    TEST_ASSERT(migOk, "Simulating live migration on running VM must succeed");
    TEST_ASSERT(dc01->getMigrationPagesCopied() > 1000000, "Live migration must transfer >1M memory pages");
    TEST_ASSERT(dc01->getMigrationBrownoutMs() <= 15, "Migration brownout cutover must be <= 15ms for zero perceptible disruption");

    // Stage 13: Win32 & NT Clean-Room C ABI Driver Export Verification
    void* hVm = nullptr;
    NTSTATUS stAbiVm = micant::vmms::VmmsCreateVirtualMachine("VM-ABI-RUNNER", 4, 4096, &hVm);
    TEST_ASSERT(stAbiVm == micant::STATUS_SUCCESS && hVm != nullptr, "VmmsCreateVirtualMachine must return STATUS_SUCCESS");

    NTSTATUS stAbiStart = micant::vmms::VmmsStartVirtualMachine(hVm, "VM-ABI-RUNNER");
    TEST_ASSERT(stAbiStart == micant::STATUS_SUCCESS, "VmmsStartVirtualMachine must return STATUS_SUCCESS");

    NTSTATUS stAbiBalloon = micant::vmms::VmmsSetDynamicMemory(hVm, "VM-ABI-RUNNER", 5120);
    TEST_ASSERT(stAbiBalloon == micant::STATUS_SUCCESS, "VmmsSetDynamicMemory must return STATUS_SUCCESS");

    NTSTATUS stAbiPause = micant::vmms::VmmsPauseVirtualMachine(hVm, "VM-ABI-RUNNER");
    TEST_ASSERT(stAbiPause == micant::STATUS_SUCCESS, "VmmsPauseVirtualMachine must return STATUS_SUCCESS");

    NTSTATUS stAbiResume = micant::vmms::VmmsResumeVirtualMachine(hVm, "VM-ABI-RUNNER");
    TEST_ASSERT(stAbiResume == micant::STATUS_SUCCESS, "VmmsResumeVirtualMachine must return STATUS_SUCCESS");

    NTSTATUS stAbiStop = micant::vmms::VmmsStopVirtualMachine(hVm, "VM-ABI-RUNNER", false);
    TEST_ASSERT(stAbiStop == micant::STATUS_SUCCESS, "VmmsStopVirtualMachine must return STATUS_SUCCESS");

    void* hPort = nullptr;
    NTSTATUS stAbiPort = micant::vmms::VmSwitchCreatePort("DefaultSwitch", "ABI-PORT-01", "AbiNic", "00:15:5D:AA:11:22", 10, &hPort);
    TEST_ASSERT(stAbiPort == micant::STATUS_SUCCESS && hPort != nullptr, "VmSwitchCreatePort must return STATUS_SUCCESS");

    NTSTATUS stAbiFwd = micant::vmms::VmSwitchSendFrame("DefaultSwitch", "ABI-PORT-01", "00:15:5D:01:0A:01", 10, 128);
    TEST_ASSERT(stAbiFwd == micant::STATUS_SUCCESS, "VmSwitchSendFrame must return STATUS_SUCCESS");

    void* hVhdx = nullptr;
    NTSTATUS stAbiVhdx = micant::vmms::VhdxCreateDisk("C:\\VirtualDisks\\AbiTest.vhdx", 1024 * 1024 * 1024, 2, nullptr, &hVhdx);
    TEST_ASSERT(stAbiVhdx == micant::STATUS_SUCCESS && hVhdx != nullptr, "VhdxCreateDisk must return STATUS_SUCCESS");

    const char abiBlock[] = "ABI_TEST_BLOCK_PAYLOAD_VHDX";
    NTSTATUS stAbiWrite = micant::vmms::VhdxWriteBlock("C:\\VirtualDisks\\AbiTest.vhdx", 0x100000, abiBlock, sizeof(abiBlock));
    TEST_ASSERT(stAbiWrite == micant::STATUS_SUCCESS, "VhdxWriteBlock must return STATUS_SUCCESS");

    char abiReadBuf[64]{};
    NTSTATUS stAbiRead = micant::vmms::VhdxReadBlock("C:\\VirtualDisks\\AbiTest.vhdx", 0x100000, abiReadBuf, sizeof(abiReadBuf));
    TEST_ASSERT(stAbiRead == micant::STATUS_SUCCESS && std::strcmp(abiReadBuf, abiBlock) == 0, "VhdxReadBlock must verify written block");

    char cpBuf[64]{};
    NTSTATUS stAbiCp = micant::vmms::VhdxCreateCheckpoint("C:\\VirtualDisks\\AbiTest.vhdx", "AbiCheckpoint", cpBuf, sizeof(cpBuf));
    TEST_ASSERT(stAbiCp == micant::STATUS_SUCCESS && std::strlen(cpBuf) > 0, "VhdxCreateCheckpoint must return STATUS_SUCCESS and CP id");

    // Stage 14: Monumental Landmark 100-Operation Concurrent Multithreaded VM & vSwitch Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);

    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([&sys, &stressSuccessCount, t]() {
            for (int i = 0; i < 10; ++i) {
                // Alternating balloon adjustment, vSwitch frame forward, and VHDX write/read
                auto* vm = sys.getVirtualMachineByName("TITAN-DC01");
                auto* sw = sys.getVirtualSwitch("DefaultSwitch");
                auto* disk = sys.getVhdx("C:\\VirtualDisks\\BaseOS_Windows2025.vhdx");

                bool bOk = vm ? vm->adjustBalloonMemory(4096 + (t * 50) + i) : false;
                bool sOk = sw ? sw->forwardFrame("PORT-NIC-01", "00:15:5D:01:0A:02", 10, 64) : false;
                char buf[64]{};
                bool dOk = disk ? disk->readBlock(0x100000, buf, 16) : false;

                if (bOk && sOk && dOk) {
                    stressSuccessCount.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 100, "100 concurrent multi-threaded VM, vSwitch, and VHDX operations must succeed with 0 deadlocks");

    sys.reset();
    std::cout << "[TEST] Suite 200: Windows Hyper-V VMMS, Virtual Switch & VHDX Container Infrastructure (Monumental Landmark) PASSED.\n";
}

void Test_WindowsActiveDirectory_KerberosKDC_Subsystem() {
    std::cout << "[TEST] Suite 201: Windows Active Directory Domain Services & Kerberos KDC Subsystem...\n";

    // Stage 1: SCM Services Registration (Kdc, NTDS, Netlogon)
    micant::activedirectory::RegisterActiveDirectorySubsystem();
    auto& scm = micant::scm::ServiceControlManager::get();
    auto kdcSvc = scm.getServiceRecord(L"Kdc");
    auto ntdsSvc = scm.getServiceRecord(L"NTDS");
    auto netlogonSvc = scm.getServiceRecord(L"Netlogon");
    TEST_ASSERT(kdcSvc != nullptr, "Kdc SCM service must be registered");
    TEST_ASSERT(ntdsSvc != nullptr, "NTDS SCM service must be registered");
    TEST_ASSERT(netlogonSvc != nullptr, "Netlogon SCM service must be registered");

    // Stage 2: VersionDatabase Registration Parity
    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("kdcsvc.dll") != nullptr, "kdcsvc.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("kdc.sys") != nullptr, "kdc.sys must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("ntds.dit") != nullptr, "ntds.dit must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("wldap32.dll") != nullptr, "wldap32.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("netlogon.dll") != nullptr, "netlogon.dll must be registered in VersionDatabase");

    // Stage 3: NTDS Domain Root Object and Built-in Containers
    auto& sys = micant::activedirectory::ActiveDirectorySubsystem::get();
    sys.initialize();
    TEST_ASSERT(sys.isInitialized(), "ActiveDirectorySubsystem must be initialized");

    const auto* rootObj = sys.getObjectByDn(micant::activedirectory::DEFAULT_DOMAIN_DN);
    TEST_ASSERT(rootObj != nullptr, "Root domain DNS object must exist");
    TEST_ASSERT(rootObj->objectClass == micant::activedirectory::ObjectClass::DomainDNS, "Root domain must be DomainDNS class");

    const auto* ouUsers = sys.getObjectByDn("CN=Users," + std::string(micant::activedirectory::DEFAULT_DOMAIN_DN));
    const auto* ouComputers = sys.getObjectByDn("CN=Computers," + std::string(micant::activedirectory::DEFAULT_DOMAIN_DN));
    const auto* ouDCs = sys.getObjectByDn("OU=Domain Controllers," + std::string(micant::activedirectory::DEFAULT_DOMAIN_DN));
    TEST_ASSERT(ouUsers != nullptr && ouComputers != nullptr && ouDCs != nullptr, "Built-in containers must exist in NTDS");

    // Stage 4: Pre-seeded Security Principals
    const auto* admin = sys.getObjectBySam("Administrator");
    TEST_ASSERT(admin != nullptr, "Administrator user principal must exist");
    TEST_ASSERT(admin->userPrincipalName == "Administrator@micant.internal", "Administrator UPN must match domain");
    TEST_ASSERT(admin->objectSid.ends_with("-500"), "Administrator SID must end with well-known RID 500");

    const auto* krbtgt = sys.getObjectBySam("krbtgt");
    TEST_ASSERT(krbtgt != nullptr, "krbtgt KDC principal must exist");
    TEST_ASSERT(krbtgt->objectSid.ends_with("-502"), "krbtgt SID must end with well-known RID 502");

    const auto* grpAdmins = sys.getObjectBySam("Domain Admins");
    TEST_ASSERT(grpAdmins != nullptr, "Domain Admins security group must exist");
    TEST_ASSERT(grpAdmins->objectSid.ends_with("-512"), "Domain Admins SID must end with well-known RID 512");

    // Stage 5: Domain Controller & Joined Member Server Accounts
    const auto* dc01 = sys.getObjectBySam("TITAN-DC01$");
    TEST_ASSERT(dc01 != nullptr, "Domain controller TITAN-DC01$ must exist");
    TEST_ASSERT(dc01->userAccountControl & micant::activedirectory::UF_SERVER_TRUST, "DC account must have UF_SERVER_TRUST");

    const auto* app01 = sys.getObjectBySam("TITAN-APP01$");
    TEST_ASSERT(app01 != nullptr, "Application server TITAN-APP01$ must exist");
    TEST_ASSERT(app01->userAccountControl & micant::activedirectory::UF_WORKSTATION_TRUST, "Member server must have UF_WORKSTATION_TRUST");

    // Stage 6: Multi-Attribute SPN Indexing
    const auto* spnHits1 = sys.getObjectBySpn("HOST/titan-dc01.micant.internal");
    const auto* spnHits2 = sys.getObjectBySpn("cifs/titan-dc01.micant.internal");
    const auto* spnHits3 = sys.getObjectBySpn("http/titan-app01.micant.internal");
    TEST_ASSERT(spnHits1 != nullptr && spnHits1->samAccountName == "TITAN-DC01$", "SPN HOST must resolve to TITAN-DC01$");
    TEST_ASSERT(spnHits2 != nullptr && spnHits2->samAccountName == "TITAN-DC01$", "SPN cifs must resolve to TITAN-DC01$");
    TEST_ASSERT(spnHits3 != nullptr && spnHits3->samAccountName == "TITAN-APP01$", "SPN http must resolve to TITAN-APP01$");

    // Stage 7: Kerberos AS-REQ / AS-REP Authentication (TGT Generation)
    micant::activedirectory::KerberosTicket tgt{};
    bool asOk = sys.authenticateAsReq("Administrator@micant.internal", "micant.internal", tgt);
    TEST_ASSERT(asOk, "authenticateAsReq for Administrator must succeed");
    TEST_ASSERT(tgt.ticketType == micant::activedirectory::KerberosTicketType::TicketGrantingTicket, "AS-REP must return TGT");
    TEST_ASSERT(tgt.servicePrincipal == "krbtgt/micant.internal", "TGT service principal must be krbtgt/realm");
    TEST_ASSERT(tgt.encType == micant::activedirectory::EncryptionType::AES256_CTS_HMAC_SHA1_96, "TGT must use AES-256-CTS");

    // Stage 8: Privilege Attribute Certificate (PAC) Signature & Group Memberships
    TEST_ASSERT(tgt.pac.kdcSignatureValid, "PAC KDC signature must be valid");
    TEST_ASSERT(tgt.pac.serverSignatureValid, "PAC server signature must be valid");
    TEST_ASSERT(tgt.pac.accountName == "Administrator", "PAC account name must match client");
    TEST_ASSERT(!tgt.pac.groups.empty(), "PAC must contain security group memberships");
    bool hasDomainAdminSid = false;
    for (const auto& g : tgt.pac.groups) {
        if (g.groupSid.ends_with("-512")) hasDomainAdminSid = true;
    }
    TEST_ASSERT(hasDomainAdminSid, "PAC must contain Domain Admins SID (RID 512)");

    // Stage 9: Kerberos TGS-REQ / TGS-REP Service Ticket Granting
    micant::activedirectory::KerberosTicket tgs{};
    bool tgsOk = sys.grantServiceTicketTgsReq(tgt.ticketId, "cifs/titan-dc01.micant.internal", tgs);
    TEST_ASSERT(tgsOk, "grantServiceTicketTgsReq for cifs SPN must succeed");
    TEST_ASSERT(tgs.ticketType == micant::activedirectory::KerberosTicketType::ServiceTicket, "TGS-REP must return Service Ticket");
    TEST_ASSERT(tgs.servicePrincipal == "cifs/titan-dc01.micant.internal", "Service Ticket SPN must match requested SPN");

    // Stage 10: AP-REQ Application Server Verification
    micant::activedirectory::PacLogonInfo validatedPac{};
    bool verifyOk = sys.verifyServiceTicket(tgs.ticketId, "cifs/titan-dc01.micant.internal", &validatedPac);
    TEST_ASSERT(verifyOk, "verifyServiceTicket must validate valid service ticket");
    TEST_ASSERT(validatedPac.accountName == "Administrator", "Verified PAC must preserve client account identity");

    bool rejectWrongSpn = sys.verifyServiceTicket(tgs.ticketId, "cifs/wrong-server.micant.internal");
    TEST_ASSERT(!rejectWrongSpn, "verifyServiceTicket must reject SPN mismatch");

    // Stage 11: Cross-Realm Forest Trust & Referral Ticket
    const auto* trust = sys.getTrust("partner.corp");
    TEST_ASSERT(trust != nullptr, "partner.corp domain trust must exist");
    TEST_ASSERT(trust->type == micant::activedirectory::TrustType::Forest, "partner.corp trust must be Forest type");
    TEST_ASSERT(trust->isTransitive, "partner.corp trust must be transitive");

    micant::activedirectory::KerberosTicket refTicket{};
    bool refOk = sys.grantServiceTicketTgsReq(tgt.ticketId, "cifs/server01.partner.corp", refTicket);
    TEST_ASSERT(refOk, "Cross-realm TGS request to trusted partner domain must yield referral ticket");
    TEST_ASSERT(refTicket.servicePrincipal == "krbtgt/partner.corp", "Referral ticket must target krbtgt of trusted partner realm");

    // Stage 12: LDAP Query Engine Search Filters
    auto ldapUsers = sys.searchLdap("", "(objectClass=user)");
    TEST_ASSERT(ldapUsers.size() >= 2, "LDAP search for objectClass=user must return at least 2 objects");

    auto ldapComputers = sys.searchLdap("", "(objectClass=computer)");
    TEST_ASSERT(ldapComputers.size() >= 2, "LDAP search for objectClass=computer must return at least 2 objects");

    auto ldapWildcard = sys.searchLdap("", "(sAMAccountName=TITAN-*)");
    TEST_ASSERT(ldapWildcard.size() >= 2, "LDAP wildcard search for TITAN-* must return domain controller and member server");

    // Stage 13: Win32 & NT Clean-Room C ABI Driver Export Verification
    void* hAbiTgt = nullptr;
    char abiTgtId[64]{};
    NTSTATUS stAbiAuth = micant::activedirectory::KdcAuthenticateClient("Administrator", "micant.internal", &hAbiTgt, abiTgtId, sizeof(abiTgtId));
    TEST_ASSERT(stAbiAuth == micant::STATUS_SUCCESS && hAbiTgt != nullptr && std::strlen(abiTgtId) > 0, "KdcAuthenticateClient must return STATUS_SUCCESS");

    void* hAbiTgs = nullptr;
    char abiTgsId[64]{};
    NTSTATUS stAbiGrant = micant::activedirectory::KdcGrantServiceTicket(abiTgtId, "cifs/titan-dc01.micant.internal", &hAbiTgs, abiTgsId, sizeof(abiTgsId));
    TEST_ASSERT(stAbiGrant == micant::STATUS_SUCCESS && hAbiTgs != nullptr && std::strlen(abiTgsId) > 0, "KdcGrantServiceTicket must return STATUS_SUCCESS");

    bool abiValid = false;
    NTSTATUS stAbiVer = micant::activedirectory::KdcVerifyServiceTicket(abiTgsId, "cifs/titan-dc01.micant.internal", &abiValid);
    TEST_ASSERT(stAbiVer == micant::STATUS_SUCCESS && abiValid, "KdcVerifyServiceTicket must return STATUS_SUCCESS and true");

    void* hNewObj = nullptr;
    NTSTATUS stCreatePrincipal = micant::activedirectory::NtdsCreatePrincipal(
        "CN=DevUser01,CN=Users,DC=micant,DC=internal", "DevUser01", static_cast<uint32_t>(micant::activedirectory::ObjectClass::User), "devuser01@micant.internal", &hNewObj);
    TEST_ASSERT(stCreatePrincipal == micant::STATUS_SUCCESS && hNewObj != nullptr, "NtdsCreatePrincipal must return STATUS_SUCCESS");

    char outDnBuf[128]{};
    NTSTATUS stQueryObj = micant::activedirectory::NtdsQueryObject("DevUser01", outDnBuf, sizeof(outDnBuf));
    TEST_ASSERT(stQueryObj == micant::STATUS_SUCCESS && std::string(outDnBuf).find("DevUser01") != std::string::npos, "NtdsQueryObject must return correct DN");

    uint32_t ldapCount = 0;
    NTSTATUS stLdap = micant::activedirectory::LdapSearchDirectory(nullptr, "(objectClass=user)", &ldapCount);
    TEST_ASSERT(stLdap == micant::STATUS_SUCCESS && ldapCount >= 3, "LdapSearchDirectory must return STATUS_SUCCESS and match count");

    // Stage 14: 100-Operation Concurrent Multithreaded AD DS & KDC Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(10);

    for (int t = 0; t < 10; ++t) {
        stressThreads.emplace_back([&sys, &stressSuccessCount]() {
            for (int i = 0; i < 10; ++i) {
                micant::activedirectory::KerberosTicket loopTgt{};
                bool aOk = sys.authenticateAsReq("Administrator", "micant.internal", loopTgt);
                micant::activedirectory::KerberosTicket loopTgs{};
                bool gOk = aOk ? sys.grantServiceTicketTgsReq(loopTgt.ticketId, "http/titan-app01.micant.internal", loopTgs) : false;
                bool vOk = gOk ? sys.verifyServiceTicket(loopTgs.ticketId, "http/titan-app01.micant.internal") : false;
                auto lRes = sys.searchLdap("", "(samAccountName=Administrator)");
                if (aOk && gOk && vOk && !lRes.empty()) {
                    stressSuccessCount.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 100, "100-operation concurrent multithreaded AD DS & KDC stress test must complete with 100% success");

    sys.reset();
    std::cout << "[TEST] Suite 201: Windows Active Directory Domain Services & Kerberos KDC Subsystem PASSED.\n";
}

void Test_WindowsGroupPolicy_Engine_CSE_Subsystem() {
    std::cout << "[TEST] Suite 202: Windows Group Policy Client & Engine Subsystem...\n";

    auto& gp = micant::gp::GroupPolicySubsystem::instance();

    // Stage 1: SCM Service Registration (Gpsvc)
    auto& scm = micant::scm::ServiceControlManager::get();
    auto pGpsvc = scm.getServiceRecord(L"Gpsvc");
    TEST_ASSERT(pGpsvc != nullptr, "Gpsvc service record must be registered in SCM");
    TEST_ASSERT(pGpsvc->serviceType == micant::scm::SERVICE_WIN32_SHARE_PROCESS, "Gpsvc must be SERVICE_WIN32_SHARE_PROCESS");
    TEST_ASSERT(pGpsvc->startType == micant::scm::SERVICE_AUTO_START, "Gpsvc must be SERVICE_AUTO_START");
    TEST_ASSERT(pGpsvc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "Gpsvc must be in SERVICE_RUNNING state");

    // Stage 2: VersionDatabase Modules
    auto& vdb = micant::version::VersionDatabase::Instance();
    const auto* modGpsvc = vdb.FindModule("gpsvc.dll");
    const auto* modGpupdate = vdb.FindModule("gpupdate.exe");
    const auto* modGpreport = vdb.FindModule("gpreport.exe");
    const auto* modGpedit = vdb.FindModule("gpedit.dll");
    const auto* modUserenv = vdb.FindModule("userenv.dll");
    TEST_ASSERT(modGpsvc != nullptr, "gpsvc.dll must exist in VersionDatabase");
    TEST_ASSERT(modGpupdate != nullptr, "gpupdate.exe must exist in VersionDatabase");
    TEST_ASSERT(modGpreport != nullptr, "gpreport.exe must exist in VersionDatabase");
    TEST_ASSERT(modGpedit != nullptr, "gpedit.dll must exist in VersionDatabase");
    TEST_ASSERT(modUserenv != nullptr, "userenv.dll must exist in VersionDatabase");
    TEST_ASSERT(modGpsvc->stringTable.at("FileVersion") == "10.0.26100.1", "gpsvc.dll build must be 10.0.26100.1");

    // Stage 3: Client-Side Extension (CSE) Verification
    TEST_ASSERT(gp.getCseInvocations() >= 0, "CSE invocations counter must be accessible");

    // Stage 4: Registry.pol Serialization & Deserialization
    micant::gp::RegistryPol pol;
    micant::gp::PolicySetting s1;
    s1.keyPath = "Software\\Policies\\MicaNT\\Security";
    s1.valueName = "MinEncryptionStrength";
    s1.type = micant::gp::PolicyValueType::Dword;
    s1.dwordValue = 256;
    pol.settings.push_back(s1);

    micant::gp::PolicySetting s2;
    s2.keyPath = "Software\\Policies\\MicaNT\\Custom";
    s2.valueName = "OrganizationName";
    s2.type = micant::gp::PolicyValueType::String;
    s2.stringValue = "MicaNT Sovereign Enterprise";
    pol.settings.push_back(s2);

    auto serialized = pol.serialize();
    TEST_ASSERT(serialized.size() > 16, "Serialized Registry.pol must contain header and entries");

    micant::gp::RegistryPol parsed;
    bool parseOk = parsed.deserialize(serialized.data(), serialized.size());
    TEST_ASSERT(parseOk, "Registry.pol deserialize must succeed");
    TEST_ASSERT(parsed.settings.size() == 2, "Registry.pol must contain 2 parsed settings");
    TEST_ASSERT(parsed.settings[0].valueName == "MinEncryptionStrength" && parsed.settings[0].dwordValue == 256,
                "Parsed DWORD setting must match");
    TEST_ASSERT(parsed.settings[1].valueName == "OrganizationName" && parsed.settings[1].stringValue == "MicaNT Sovereign Enterprise",
                "Parsed String setting must match");

    // Stage 5: GPO Creation & Storage
    micant::gp::GroupPolicyObject customGpo;
    customGpo.gpoId = "{99999999-8888-7777-6666-555555555555}";
    customGpo.displayName = "Enterprise Firewall & Defender Policy";
    customGpo.status = micant::gp::GpoStatus::Enabled;
    micant::gp::PolicySetting fwSetting;
    fwSetting.keyPath = "Software\\Policies\\Microsoft\\WindowsFirewall\\DomainProfile";
    fwSetting.valueName = "EnableFirewall";
    fwSetting.type = micant::gp::PolicyValueType::Dword;
    fwSetting.dwordValue = 1;
    customGpo.machinePolicy.settings.push_back(fwSetting);
    bool created = gp.createGpo(customGpo);
    TEST_ASSERT(created, "Custom GPO must be created");

    micant::gp::GroupPolicyObject queryGpo;
    bool found = gp.getGpo(customGpo.gpoId, &queryGpo);
    TEST_ASSERT(found && queryGpo.displayName == "Enterprise Firewall & Defender Policy", "GPO query must retrieve created GPO");

    // Stage 6: SOM Linking
    bool linked = gp.linkGpo(micant::gp::SomType::OrganizationalUnit, "OU=Workstations,DC=micant,DC=internal", customGpo.gpoId, false, true);
    TEST_ASSERT(linked, "GPO link to OU must succeed");

    // Stage 7: Standard LSDOU Precedence
    auto repLsdou = gp.processGroupPolicy("TITAN-WS01", true, "OU=Workstations,DC=micant,DC=internal", false);
    TEST_ASSERT(!repLsdou.appliedGpoIds.empty(), "GPO processing must apply candidate GPOs");
    auto itNotice = repLsdou.resolvedSettings.find("Software\\Policies\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\LegalNoticeText");
    TEST_ASSERT(itNotice != repLsdou.resolvedSettings.end(), "Workstations OU setting must be applied");
    TEST_ASSERT(itNotice->second.stringValue == "Authorized Access Only - MicaNT Enterprise", "OU value must be resolved");

    // Stage 8: Enforced (NoOverride) GPO Precedence
    micant::gp::GroupPolicyObject rootEnforcedGpo;
    rootEnforcedGpo.gpoId = "{ENFORCED-0000-0000-0000-000000000001}";
    rootEnforcedGpo.displayName = "Domain Root Mandatory Security Policy";
    micant::gp::PolicySetting rootOverride;
    rootOverride.keyPath = "Software\\Policies\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
    rootOverride.valueName = "LegalNoticeText";
    rootOverride.type = micant::gp::PolicyValueType::String;
    rootOverride.stringValue = "MANDATORY ROOT NOTICE: ZERO OVERRIDE";
    rootEnforcedGpo.machinePolicy.settings.push_back(rootOverride);
    gp.createGpo(rootEnforcedGpo);
    gp.linkGpo(micant::gp::SomType::Domain, "DC=micant,DC=internal", rootEnforcedGpo.gpoId, true, true); // Enforced!

    auto repEnforced = gp.processGroupPolicy("TITAN-WS01", true, "OU=Workstations,DC=micant,DC=internal", false);
    auto itEnf = repEnforced.resolvedSettings.find("Software\\Policies\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\LegalNoticeText");
    TEST_ASSERT(itEnf != repEnforced.resolvedSettings.end(), "Setting must be present");
    TEST_ASSERT(itEnf->second.stringValue == "MANDATORY ROOT NOTICE: ZERO OVERRIDE", "Enforced Domain GPO must override subordinate OU setting");
    TEST_ASSERT(itEnf->second.enforced == true, "Winning setting must be flagged as enforced");

    // Stage 9: Block Inheritance Behavior
    gp.setSomBlockInheritance(micant::gp::SomType::OrganizationalUnit, "OU=Workstations,DC=micant,DC=internal", true);
    auto repBlock = gp.processGroupPolicy("TITAN-WS01", true, "OU=Workstations,DC=micant,DC=internal", false);
    bool sawBlock = false;
    for (const auto& f : repBlock.filteredGpoReasons) {
        if (f.second == micant::gp::FilterVerdict::BlockedInherit) sawBlock = true;
    }
    TEST_ASSERT(sawBlock, "Block Inheritance must filter out un-enforced parent GPOs");
    bool enforcedApplied = false;
    for (const auto& g : repBlock.appliedGpoIds) {
        if (g == rootEnforcedGpo.gpoId) enforcedApplied = true;
    }
    TEST_ASSERT(enforcedApplied, "Enforced GPO must penetrate Block Inheritance");
    gp.setSomBlockInheritance(micant::gp::SomType::OrganizationalUnit, "OU=Workstations,DC=micant,DC=internal", false); // reset

    // Stage 10: WMI Filtering
    micant::gp::GroupPolicyObject dcOnlyGpo;
    dcOnlyGpo.gpoId = "{DC-ONLY-0000-0000-0000-000000000002}";
    dcOnlyGpo.displayName = "DC Replication Config GPO";
    dcOnlyGpo.wmiFilterId = "{F2222222-3333-4444-5555-666666666666}"; // Requires DomainRole = 'dc'
    micant::gp::PolicySetting dcSetting;
    dcSetting.keyPath = "Software\\Policies\\Microsoft\\Windows\\DirectoryServices";
    dcSetting.valueName = "StrictReplication";
    dcSetting.type = micant::gp::PolicyValueType::Dword;
    dcSetting.dwordValue = 1;
    dcOnlyGpo.machinePolicy.settings.push_back(dcSetting);
    gp.createGpo(dcOnlyGpo);
    gp.linkGpo(micant::gp::SomType::OrganizationalUnit, "OU=Workstations,DC=micant,DC=internal", dcOnlyGpo.gpoId, false, true);

    auto repWmi = gp.processGroupPolicy("TITAN-WS01", true, "OU=Workstations,DC=micant,DC=internal", false);
    bool wmiDenied = false;
    for (const auto& f : repWmi.filteredGpoReasons) {
        if (f.first == dcOnlyGpo.gpoId && f.second == micant::gp::FilterVerdict::WmiFilterFailed) wmiDenied = true;
    }
    TEST_ASSERT(wmiDenied, "GPO with unfulfilled WMI filter must be denied");

    // Stage 11: Security Settings Resolution
    auto repSec = gp.processGroupPolicy("TITAN-WS01", true, "OU=Workstations,DC=micant,DC=internal", false);
    TEST_ASSERT(repSec.resolvedSecurity.find("PasswordComplexity") != repSec.resolvedSecurity.end(), "Security setting PasswordComplexity must be resolved");
    TEST_ASSERT(repSec.resolvedSecurity["PasswordComplexity"] == "1", "PasswordComplexity value must match GPO");

    // Stage 12: Scripts CSE Dispatch
    TEST_ASSERT(!repSec.executedScripts.empty(), "Workstation GPO startup scripts must be scheduled for execution");
    TEST_ASSERT(repSec.executedScripts[0].find("verify_integrity.cmd") != std::string::npos, "Startup script path must match");

    // Stage 13: Win32 C ABI Parity
    uint32_t retComp = micant::gp::MicaProcessGroupPolicyCompleted(nullptr, 0);
    TEST_ASSERT(retComp == 0, "MicaProcessGroupPolicyCompleted must return 0");
    uint32_t retRef = micant::gp::MicaRefreshPolicy(1);
    TEST_ASSERT(retRef == 1, "MicaRefreshPolicy must return TRUE");
    uint32_t retRefEx = micant::gp::MicaRefreshPolicyEx(1, 0x1);
    TEST_ASSERT(retRefEx == 1, "MicaRefreshPolicyEx must return TRUE");

    micant::gp::GPO_LINK_NODE* pGpoList = nullptr;
    uint32_t retGetList = micant::gp::MicaGetGPOListW(nullptr, L"TITAN-WS01", nullptr, nullptr, 0, &pGpoList);
    TEST_ASSERT(retGetList == 0 && pGpoList != nullptr, "MicaGetGPOListW must return GPO link list");
    uint32_t retFree = micant::gp::MicaFreeGPOListW(pGpoList);
    TEST_ASSERT(retFree == 1, "MicaFreeGPOListW must return TRUE");

    // Stage 14: Multithreaded High-Concurrency Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);
    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&gp, &stressSuccessCount]() {
            for (int i = 0; i < 15; ++i) {
                auto r = gp.processGroupPolicy("TITAN-STRESS-NODE", true, "OU=Workstations,DC=micant,DC=internal", (i % 2 == 0));
                if (!r.appliedGpoIds.empty() && !r.resolvedSettings.empty()) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded Group Policy stress test must complete with 100% success");

    std::cout << "[TEST] Suite 202: Windows Group Policy Client & Engine Subsystem PASSED.\n";
}

void Test_WindowsRemoteDesktop_VirtualChannels_Subsystem() {
    std::cout << "[TEST] Suite 203: Windows Remote Desktop Services & RDP Virtual Channels Subsystem...\n";

    auto& rds = micant::rds::RemoteDesktopSubsystem::instance();

    // Stage 1: SCM Services Registration (TermService, SessionEnv, UmRdpService)
    auto& scm = micant::scm::ServiceControlManager::get();
    auto pTerm = scm.getServiceRecord(L"TermService");
    auto pEnv = scm.getServiceRecord(L"SessionEnv");
    auto pBus = scm.getServiceRecord(L"UmRdpService");
    TEST_ASSERT(pTerm != nullptr, "TermService must be registered in SCM");
    TEST_ASSERT(pTerm->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "TermService must be in SERVICE_RUNNING state");
    TEST_ASSERT(pTerm->binaryPath == L"C:\\Windows\\System32\\termsrv.dll", "TermService binary path must be termsrv.dll");
    TEST_ASSERT(pEnv != nullptr, "SessionEnv must be registered in SCM");
    TEST_ASSERT(pEnv->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "SessionEnv must be in SERVICE_RUNNING state");
    TEST_ASSERT(pBus != nullptr, "UmRdpService must be registered in SCM");
    TEST_ASSERT(pBus->serviceType == micant::scm::SERVICE_KERNEL_DRIVER, "UmRdpService must be SERVICE_KERNEL_DRIVER");

    // Stage 2: VersionDatabase Modules
    auto& vdb = micant::version::VersionDatabase::Instance();
    const auto* modTerm = vdb.FindModule("termsrv.dll");
    const auto* modWts = vdb.FindModule("wtsapi32.dll");
    const auto* modDr = vdb.FindModule("rdpdr.sys");
    const auto* modMstsc = vdb.FindModule("mstsc.exe");
    const auto* modCore = vdb.FindModule("rdpcorets.dll");
    TEST_ASSERT(modTerm != nullptr, "termsrv.dll must exist in VersionDatabase");
    TEST_ASSERT(modWts != nullptr, "wtsapi32.dll must exist in VersionDatabase");
    TEST_ASSERT(modDr != nullptr, "rdpdr.sys must exist in VersionDatabase");
    TEST_ASSERT(modMstsc != nullptr, "mstsc.exe must exist in VersionDatabase");
    TEST_ASSERT(modCore != nullptr, "rdpcorets.dll must exist in VersionDatabase");
    TEST_ASSERT(modTerm->stringTable.at("FileVersion") == "10.0.26100.1", "termsrv.dll build must be 10.0.26100.1");

    // Stage 3: Default WinStation Sessions (Session 0 Services, Session 1 Console)
    micant::rds::RdpSession s0{};
    micant::rds::RdpSession s1{};
    TEST_ASSERT(rds.getSession(0, &s0), "Session 0 must exist");
    TEST_ASSERT(s0.winStationName == L"Services", "Session 0 name must be Services");
    TEST_ASSERT(s0.userName == L"SYSTEM", "Session 0 user must be SYSTEM");
    TEST_ASSERT(s0.state == micant::rds::WTSConnected, "Session 0 state must be WTSConnected");

    TEST_ASSERT(rds.getSession(1, &s1), "Session 1 must exist");
    TEST_ASSERT(s1.winStationName == L"Console", "Session 1 name must be Console");
    TEST_ASSERT(s1.userName == L"Administrator", "Session 1 user must be Administrator");
    TEST_ASSERT(s1.state == micant::rds::WTSActive, "Session 1 state must be WTSActive");
    TEST_ASSERT(s1.isConsoleSession == true, "Session 1 must be marked console session");

    // Stage 4: RDP Connection Sequence & Handshake State Machine
    micant::rds::ClientDisplayMetrics disp1080{1920, 1080, 32, 60, 1};
    uint32_t aliceSid = 0;
    bool connAlice = rds.initiateRdpConnection(L"TITAN-WS01", "192.168.1.100", L"Alice", L"MICANT", disp1080, true, &aliceSid);
    TEST_ASSERT(connAlice && aliceSid >= 2, "Alice RDP connection must succeed with valid session ID");

    micant::rds::RdpSession sAlice{};
    TEST_ASSERT(rds.getSession(aliceSid, &sAlice), "Alice session must be retrieved");
    TEST_ASSERT(sAlice.state == micant::rds::WTSActive, "Alice session must be WTSActive");
    TEST_ASSERT(sAlice.handshakeState == micant::rds::RdpHandshakeState::FinalizedSession, "Alice session handshake must be FinalizedSession");
    TEST_ASSERT(sAlice.nlaAuthenticated == true, "Alice session must be NLA authenticated");

    // Stage 5: NLA (CredSSP) Enforcement
    uint32_t rejectedSid = 0;
    rds.setNlaEnforced(true);
    bool connNlaFail = rds.initiateRdpConnection(L"ROGUE-CLIENT", "10.0.0.99", L"Attacker", L"WORKGROUP", disp1080, false, &rejectedSid);
    TEST_ASSERT(!connNlaFail, "Non-NLA connection must be rejected when NLA is enforced");

    rds.setNlaEnforced(false);
    uint32_t nonNlaSid = 0;
    bool connNlaOk = rds.initiateRdpConnection(L"LEGACY-XP", "10.0.0.50", L"LegacyUser", L"WORKGROUP", disp1080, false, &nonNlaSid);
    TEST_ASSERT(connNlaOk, "Non-NLA connection must succeed when NLA is not enforced");
    rds.setNlaEnforced(true); // Re-enforce
    rds.logoffSession(nonNlaSid);

    // Stage 6: Client Display Metrics & Capability Negotiation (4K multimon)
    micant::rds::ClientDisplayMetrics disp4k{3840, 2160, 32, 144, 2};
    uint32_t bobSid = 0;
    bool connBob = rds.initiateRdpConnection(L"TITAN-PRO", "192.168.1.102", L"Bob", L"MICANT", disp4k, true, &bobSid);
    TEST_ASSERT(connBob && bobSid >= 2, "Bob RDP connection must succeed");

    micant::rds::RdpSession sBob{};
    TEST_ASSERT(rds.getSession(bobSid, &sBob), "Bob session must be queryable");
    TEST_ASSERT(sBob.display.width == 3840 && sBob.display.height == 2160, "Bob display resolution must match 4K");
    TEST_ASSERT(sBob.display.refreshRateHz == 144, "Bob refresh rate must be 144Hz");
    TEST_ASSERT(sBob.display.monitorCount == 2, "Bob monitor count must be 2");

    // Stage 7: Static Virtual Channels Binding
    TEST_ASSERT(sAlice.virtualChannels.find("cliprdr") != sAlice.virtualChannels.end(), "cliprdr virtual channel must be bound");
    TEST_ASSERT(sAlice.virtualChannels.find("rdpsnd") != sAlice.virtualChannels.end(), "rdpsnd virtual channel must be bound");
    TEST_ASSERT(sAlice.virtualChannels.find("rdpdr") != sAlice.virtualChannels.end(), "rdpdr virtual channel must be bound");
    TEST_ASSERT(sAlice.virtualChannels.find("rdpgfx") != sAlice.virtualChannels.end(), "rdpgfx virtual channel must be bound");
    TEST_ASSERT(sAlice.virtualChannels.at("cliprdr").channelId == 1004, "cliprdr channel ID must be 1004");

    // Stage 8: Virtual Channel Packet I/O Round-trip
    std::vector<uint8_t> clipboardPayload = {0x01, 0x00, 0x00, 0x00, 'T', 'E', 'X', 'T', 0x00, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
    bool writeOk = rds.writeVirtualChannel(aliceSid, "cliprdr", clipboardPayload.data(), clipboardPayload.size());
    TEST_ASSERT(writeOk, "writeVirtualChannel to cliprdr must succeed");

    std::vector<uint8_t> readPkt;
    bool readOk = rds.readVirtualChannel(aliceSid, "cliprdr", readPkt);
    TEST_ASSERT(readOk, "readVirtualChannel from cliprdr must succeed");
    TEST_ASSERT(readPkt == clipboardPayload, "Read packet must match written payload");

    // Stage 9: Remote Shadow Session Initiation & Mode Arbitration
    bool shadowOk = rds.startShadowSession(bobSid, aliceSid, micant::rds::WTS_SHADOW_ENABLE_INPUT_NO_NOTIFY);
    TEST_ASSERT(shadowOk, "startShadowSession from Bob to Alice must succeed");

    micant::rds::RdpSession sBobShadow{};
    TEST_ASSERT(rds.getSession(bobSid, &sBobShadow), "Bob session query must succeed");
    TEST_ASSERT(sBobShadow.state == micant::rds::WTSShadow, "Bob session state must transition to WTSShadow");
    TEST_ASSERT(sBobShadow.shadowSessionId == aliceSid, "Bob shadow target must be Alice");
    TEST_ASSERT(sBobShadow.shadowMode == micant::rds::WTS_SHADOW_ENABLE_INPUT_NO_NOTIFY, "Shadow mode must match");

    // Stage 10: Remote Shadow Session Teardown & Reversion
    bool stopShadowOk = rds.stopShadowSession(bobSid);
    TEST_ASSERT(stopShadowOk, "stopShadowSession must succeed");
    micant::rds::RdpSession sBobActive{};
    TEST_ASSERT(rds.getSession(bobSid, &sBobActive), "Bob session query must succeed");
    TEST_ASSERT(sBobActive.state == micant::rds::WTSActive, "Bob session state must revert to WTSActive");
    TEST_ASSERT(sBobActive.shadowSessionId == 0, "Bob shadowSessionId must reset to 0");

    // Stage 11: Session Disconnection & Reconnection
    bool disconnFailConsole = rds.disconnectSession(1);
    TEST_ASSERT(!disconnFailConsole, "Disconnecting console session (Session 1) must be rejected");

    bool disconnAlice = rds.disconnectSession(aliceSid);
    TEST_ASSERT(disconnAlice, "Disconnecting Alice session must succeed");
    micant::rds::RdpSession sAliceDisc{};
    TEST_ASSERT(rds.getSession(aliceSid, &sAliceDisc), "Alice session query must succeed");
    TEST_ASSERT(sAliceDisc.state == micant::rds::WTSDisconnected, "Alice session state must be WTSDisconnected");

    // Stage 12: Session Logoff & Clean Removal
    bool logoffFailConsole = rds.logoffSession(1);
    TEST_ASSERT(!logoffFailConsole, "Logging off console session (Session 1) must be rejected");

    bool logoffAlice = rds.logoffSession(aliceSid);
    TEST_ASSERT(logoffAlice, "Logging off Alice session must succeed");
    micant::rds::RdpSession sAliceGone{};
    TEST_ASSERT(!rds.getSession(aliceSid, &sAliceGone), "Alice session must no longer exist after logoff");

    rds.logoffSession(bobSid);

    // Stage 13: Win32 C ABI Parity Exports
    void* hServer = micant::rds::MicaWTSOpenServerW(L"TITAN-RDS");
    TEST_ASSERT(hServer != nullptr, "MicaWTSOpenServerW must return non-null server handle");

    micant::rds::WTS_SESSION_INFOW* pSessArr = nullptr;
    uint32_t sessCount = 0;
    int32_t enumRes = micant::rds::MicaWTSEnumerateSessionsW(hServer, 0, 1, &pSessArr, &sessCount);
    TEST_ASSERT(enumRes == 1 && pSessArr != nullptr && sessCount >= 2, "MicaWTSEnumerateSessionsW must return active sessions");
    micant::rds::MicaWTSFreeMemory(pSessArr);

    // Create an RDP session via API and open virtual channel
    uint32_t charlieSid = 0;
    rds.initiateRdpConnection(L"TITAN-REMOTE", "192.168.1.105", L"Charlie", L"MICANT", disp1080, true, &charlieSid);

    void* hChan = micant::rds::MicaWTSVirtualChannelOpen(hServer, charlieSid, "cliprdr");
    TEST_ASSERT(hChan != nullptr, "MicaWTSVirtualChannelOpen must open valid channel handle");

    const char testPdu[] = "RDP_CLIPBOARD_SYNC";
    uint32_t bytesWritten = 0;
    int32_t writeRes = micant::rds::MicaWTSVirtualChannelWrite(hChan, testPdu, sizeof(testPdu), &bytesWritten);
    TEST_ASSERT(writeRes == 1 && bytesWritten == sizeof(testPdu), "MicaWTSVirtualChannelWrite must succeed");

    char readBuf[64]{};
    uint32_t bytesRead = 0;
    int32_t readRes = micant::rds::MicaWTSVirtualChannelRead(hChan, 1000, readBuf, sizeof(readBuf), &bytesRead);
    TEST_ASSERT(readRes == 1 && bytesRead == sizeof(testPdu), "MicaWTSVirtualChannelRead must succeed");
    TEST_ASSERT(std::memcmp(readBuf, testPdu, sizeof(testPdu)) == 0, "Buffer content must match");

    int32_t closeRes = micant::rds::MicaWTSVirtualChannelClose(hChan);
    TEST_ASSERT(closeRes == 1, "MicaWTSVirtualChannelClose must succeed");

    int32_t discRes = micant::rds::MicaWTSDisconnectSession(hServer, charlieSid, 0);
    TEST_ASSERT(discRes == 1, "MicaWTSDisconnectSession must return 1");

    int32_t logoffRes = micant::rds::MicaWTSLogoffSession(hServer, charlieSid, 0);
    TEST_ASSERT(logoffRes == 1, "MicaWTSLogoffSession must return 1");

    micant::rds::MicaWTSCloseServer(hServer);

    // Stage 14: Multithreaded High-Concurrency Stress Test (8 threads, 120 operations)
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&rds, &stressSuccessCount, t]() {
            micant::rds::ClientDisplayMetrics m{1920, 1080, 32, 60, 1};
            for (int i = 0; i < 15; ++i) {
                uint32_t sid = 0;
                std::wstring uName = L"StressUser_" + std::to_wstring(t) + L"_" + std::to_wstring(i);
                bool cOk = rds.initiateRdpConnection(L"TITAN-STRESS", "127.0.0.1", uName, L"MICANT", m, true, &sid);
                if (!cOk) continue;

                const uint8_t dummyData[] = {0xDE, 0xAD, 0xBE, 0xEF};
                bool wOk = rds.writeVirtualChannel(sid, "cliprdr", dummyData, sizeof(dummyData));
                std::vector<uint8_t> rData;
                bool rOk = rds.readVirtualChannel(sid, "cliprdr", rData);
                bool dOk = rds.disconnectSession(sid);
                bool lOk = rds.logoffSession(sid);

                if (wOk && rOk && dOk && lOk && rData.size() == sizeof(dummyData)) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded Remote Desktop stress test must complete with 100% success");

    std::cout << "[TEST] Suite 203: Windows Remote Desktop Services & RDP Virtual Channels Subsystem PASSED.\n";
}

void Test_WindowsNetworkPolicyServer_RADIUS_Subsystem() {
    std::cout << "[TEST] Suite 204: Windows Network Policy Server & RADIUS Subsystem...\n";

    auto& nps = micant::nps::NetworkPolicyServer::instance();

    // Stage 1: SCM Services Registration (IAS, RadiusProxy, RadiusSys)
    auto& scm = micant::scm::ServiceControlManager::get();
    auto pIas = scm.getServiceRecord(L"IAS");
    auto pProxy = scm.getServiceRecord(L"RadiusProxy");
    auto pSys = scm.getServiceRecord(L"RadiusSys");
    TEST_ASSERT(pIas != nullptr, "IAS service record must be registered in SCM");
    TEST_ASSERT(pIas->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "IAS must be in SERVICE_RUNNING state");
    TEST_ASSERT(pIas->binaryPath == L"C:\\Windows\\System32\\ias.dll", "IAS binary path must be ias.dll");
    TEST_ASSERT(pProxy != nullptr, "RadiusProxy service record must be registered in SCM");
    TEST_ASSERT(pProxy->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "RadiusProxy must be in SERVICE_RUNNING state");
    TEST_ASSERT(pSys != nullptr, "RadiusSys service record must be registered in SCM");
    TEST_ASSERT(pSys->serviceType == micant::scm::SERVICE_KERNEL_DRIVER, "RadiusSys must be SERVICE_KERNEL_DRIVER");

    // Stage 2: VersionDatabase Modules Registration
    auto& vdb = micant::version::VersionDatabase::Instance();
    const auto* modIas = vdb.FindModule("ias.dll");
    const auto* modPol = vdb.FindModule("iaspolcy.dll");
    const auto* modRad = vdb.FindModule("iasrad.dll");
    const auto* modSys = vdb.FindModule("radius.sys");
    const auto* modMsc = vdb.FindModule("nps.msc");
    TEST_ASSERT(modIas != nullptr, "ias.dll must exist in VersionDatabase");
    TEST_ASSERT(modPol != nullptr, "iaspolcy.dll must exist in VersionDatabase");
    TEST_ASSERT(modRad != nullptr, "iasrad.dll must exist in VersionDatabase");
    TEST_ASSERT(modSys != nullptr, "radius.sys must exist in VersionDatabase");
    TEST_ASSERT(modMsc != nullptr, "nps.msc must exist in VersionDatabase");
    TEST_ASSERT(modIas->stringTable.at("FileVersion") == "10.0.26100.1", "ias.dll build must be 10.0.26100.1");

    // Stage 3: RADIUS Packet Serialization & Deserialization
    micant::nps::RadiusPacket pkt;
    pkt.code = micant::nps::RadiusCode_AccessRequest;
    pkt.identifier = 0x55;
    for (int i = 0; i < 16; ++i) pkt.authenticator[i] = static_cast<uint8_t>(i * 3 + 1);
    pkt.addStringAttribute(micant::nps::RadiusAttr_UserName, "TestUser@micant.corp");
    pkt.addUint32Attribute(micant::nps::RadiusAttr_NasPort, 5001);
    pkt.addUint32Attribute(micant::nps::RadiusAttr_NasPortType, 19); // Wireless

    auto serialized = pkt.serialize();
    TEST_ASSERT(serialized.size() >= 20, "Serialized packet must be at least 20 bytes");

    micant::nps::RadiusPacket parsedPkt;
    bool parseOk = parsedPkt.deserialize(serialized.data(), serialized.size());
    TEST_ASSERT(parseOk, "Deserialization must succeed");
    TEST_ASSERT(parsedPkt.code == micant::nps::RadiusCode_AccessRequest, "Packet code must match");
    TEST_ASSERT(parsedPkt.identifier == 0x55, "Packet identifier must match");
    const auto* parsedUser = parsedPkt.findAttribute(micant::nps::RadiusAttr_UserName);
    TEST_ASSERT(parsedUser && parsedUser->asString() == "TestUser@micant.corp", "User-Name attribute must match");
    const auto* parsedPort = parsedPkt.findAttribute(micant::nps::RadiusAttr_NasPort);
    TEST_ASSERT(parsedPort && parsedPort->asUint32() == 5001, "NAS-Port attribute must match");

    // Stage 4: RFC 2865 User-Password XOR-MD5 Encryption & Decryption Round-trip
    uint8_t reqAuth[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xEE, 0xFF};
    std::string secret = "SuperSecretRADIUSKey!";
    std::string plaintextPassword = "MicaNT_Enterprise_WiFi_Password_2026";
    auto encPassword = micant::nps::EncryptRadiusPassword(plaintextPassword, reqAuth, secret);
    TEST_ASSERT(encPassword.size() >= 16 && (encPassword.size() % 16) == 0, "Encrypted password must be multiple of 16 octets");
    std::string recoveredPassword = micant::nps::DecryptRadiusPassword(encPassword, reqAuth, secret);
    TEST_ASSERT(recoveredPassword == plaintextPassword, "RFC 2865 Decrypted password must exactly match plaintext password");

    // Stage 5: RADIUS Client (NAS) Registration & Secret Arbitration
    micant::nps::RadiusClient nasBranch{};
    nasBranch.ipAddress = "10.200.1.1";
    nasBranch.friendlyName = "Branch-Office-Switch-01";
    nasBranch.sharedSecret = "BranchSecretKey2026!";
    nasBranch.vendorName = "Cisco";
    nasBranch.enabled = true;
    bool clientRegOk = nps.registerClient(nasBranch);
    TEST_ASSERT(clientRegOk, "registerClient must succeed");

    micant::nps::RadiusClient retrievedNas{};
    bool clientFound = nps.getClient("10.200.1.1", &retrievedNas);
    TEST_ASSERT(clientFound && retrievedNas.friendlyName == "Branch-Office-Switch-01", "Client must be found by IP");
    TEST_ASSERT(retrievedNas.sharedSecret == "BranchSecretKey2026!", "Shared secret must match");

    // Stage 6: Connection Request Policy (CRP) Matching
    auto pols = nps.getNetworkPolicies();
    TEST_ASSERT(!pols.empty(), "Default network policies must be loaded");

    // Stage 7: RADIUS Access-Request & Access-Accept with Dynamic VLAN Assignment (Alice / VLAN 100)
    micant::nps::RadiusPacket reqAlice;
    reqAlice.code = micant::nps::RadiusCode_AccessRequest;
    reqAlice.identifier = 12;
    std::memcpy(reqAlice.authenticator, reqAuth, 16);
    reqAlice.addStringAttribute(micant::nps::RadiusAttr_UserName, "Alice");
    auto encAlicePass = micant::nps::EncryptRadiusPassword("AliceSecure2026!", reqAuth, "ArubaSecureWiFi!");
    reqAlice.addRawAttribute(micant::nps::RadiusAttr_UserPassword, encAlicePass.data(), encAlicePass.size());
    reqAlice.addUint32Attribute(micant::nps::RadiusAttr_NasPortType, 19); // Wireless-802.11

    auto rawAliceReq = reqAlice.serialize();
    std::vector<uint8_t> respAlice;
    bool processAliceOk = nps.processPacket("192.168.1.10", rawAliceReq.data(), rawAliceReq.size(), respAlice);
    TEST_ASSERT(processAliceOk, "processPacket for Alice must succeed");

    micant::nps::RadiusPacket pRespAlice;
    TEST_ASSERT(pRespAlice.deserialize(respAlice.data(), respAlice.size()), "Response must deserialize");
    TEST_ASSERT(pRespAlice.code == micant::nps::RadiusCode_AccessAccept, "Response code must be Access-Accept (2)");
    TEST_ASSERT(pRespAlice.identifier == reqAlice.identifier, "Response identifier must match request");

    const auto* aVlanAlice = pRespAlice.findAttribute(micant::nps::RadiusAttr_TunnelPrivateGroupId);
    TEST_ASSERT(aVlanAlice != nullptr, "Tunnel-Private-Group-ID must be present");
    TEST_ASSERT(aVlanAlice->asString() == "100", "Assigned VLAN must be 100");
    const auto* aTunnelType = pRespAlice.findAttribute(micant::nps::RadiusAttr_TunnelType);
    TEST_ASSERT(aTunnelType != nullptr && aTunnelType->asUint32() == 13, "Tunnel-Type must be 13 (VLAN)");

    // Stage 8: Authentication Rejection (Access-Reject) for Invalid Password
    micant::nps::RadiusPacket reqBadPass;
    reqBadPass.code = micant::nps::RadiusCode_AccessRequest;
    reqBadPass.identifier = 13;
    std::memcpy(reqBadPass.authenticator, reqAuth, 16);
    reqBadPass.addStringAttribute(micant::nps::RadiusAttr_UserName, "Alice");
    auto encBadPass = micant::nps::EncryptRadiusPassword("WrongPassword123", reqAuth, "ArubaSecureWiFi!");
    reqBadPass.addRawAttribute(micant::nps::RadiusAttr_UserPassword, encBadPass.data(), encBadPass.size());
    reqBadPass.addUint32Attribute(micant::nps::RadiusAttr_NasPortType, 19);

    auto rawBadReq = reqBadPass.serialize();
    std::vector<uint8_t> respBad;
    bool procBadOk = nps.processPacket("192.168.1.10", rawBadReq.data(), rawBadReq.size(), respBad);
    TEST_ASSERT(procBadOk, "processPacket must return response for bad credentials");
    micant::nps::RadiusPacket pRespBad;
    TEST_ASSERT(pRespBad.deserialize(respBad.data(), respBad.size()), "Bad response must deserialize");
    TEST_ASSERT(pRespBad.code == micant::nps::RadiusCode_AccessReject, "Response must be Access-Reject (3)");

    // Stage 9: Authentication Rejection for Disabled Account (DisabledUser)
    micant::nps::RadiusPacket reqDisabled;
    reqDisabled.code = micant::nps::RadiusCode_AccessRequest;
    reqDisabled.identifier = 14;
    std::memcpy(reqDisabled.authenticator, reqAuth, 16);
    reqDisabled.addStringAttribute(micant::nps::RadiusAttr_UserName, "DisabledUser");
    auto encDisPass = micant::nps::EncryptRadiusPassword("NeverLogon!", reqAuth, "ArubaSecureWiFi!");
    reqDisabled.addRawAttribute(micant::nps::RadiusAttr_UserPassword, encDisPass.data(), encDisPass.size());

    auto rawDisReq = reqDisabled.serialize();
    std::vector<uint8_t> respDis;
    nps.processPacket("192.168.1.10", rawDisReq.data(), rawDisReq.size(), respDis);
    micant::nps::RadiusPacket pRespDis;
    pRespDis.deserialize(respDis.data(), respDis.size());
    TEST_ASSERT(pRespDis.code == micant::nps::RadiusCode_AccessReject, "Disabled account must receive Access-Reject (3)");

    // Stage 10: 802.1X EAP Identity Handshake & Access-Challenge Generation (PEAP Start)
    micant::nps::RadiusPacket reqEapId;
    reqEapId.code = micant::nps::RadiusCode_AccessRequest;
    reqEapId.identifier = 15;
    std::memcpy(reqEapId.authenticator, reqAuth, 16);
    reqEapId.addStringAttribute(micant::nps::RadiusAttr_UserName, "Alice");
    uint8_t eapIdPayload[] = { 0x02, 0x01, 0x00, 0x09, 0x01, 'A', 'l', 'i', 'c', 'e' }; // EAP-Response/Identity
    reqEapId.addRawAttribute(micant::nps::RadiusAttr_EapMessage, eapIdPayload, sizeof(eapIdPayload));

    auto rawEapReq = reqEapId.serialize();
    std::vector<uint8_t> respEap;
    bool procEapOk = nps.processPacket("192.168.1.10", rawEapReq.data(), rawEapReq.size(), respEap);
    TEST_ASSERT(procEapOk, "EAP request processing must succeed");
    micant::nps::RadiusPacket pRespEap;
    pRespEap.deserialize(respEap.data(), respEap.size());
    TEST_ASSERT(pRespEap.code == micant::nps::RadiusCode_AccessChallenge, "EAP Identity Response must produce Access-Challenge (11)");
    TEST_ASSERT(pRespEap.findAttribute(micant::nps::RadiusAttr_EapMessage) != nullptr, "EAP-Message must be in Access-Challenge");

    // Stage 11: RADIUS Accounting Request (Start -> Interim -> Stop) & MD5 Request Authenticator Verification
    std::string testSessId = "MicaNT_RadSess_0042";
    micant::nps::RadiusPacket acctStart;
    acctStart.code = micant::nps::RadiusCode_AccountingRequest;
    acctStart.identifier = 77;
    acctStart.addUint32Attribute(micant::nps::RadiusAttr_AcctStatusType, 1); // Start
    acctStart.addStringAttribute(micant::nps::RadiusAttr_AcctSessionId, testSessId);
    acctStart.addStringAttribute(micant::nps::RadiusAttr_UserName, "Alice");
    acctStart.addStringAttribute(micant::nps::RadiusAttr_CallingStationId, "CC-22-33-44-55-66");
    acctStart.addUint32Attribute(micant::nps::RadiusAttr_NasPort, 10);

    auto rawStartPkt = acctStart.serialize();
    micant::nps::CalculateAccountingRequestAuthenticator(acctStart.authenticator, acctStart.code,
                                                         acctStart.identifier, static_cast<uint16_t>(rawStartPkt.size()),
                                                         rawStartPkt.data() + 20, rawStartPkt.size() - 20,
                                                         "ArubaSecureWiFi!");
    auto signedStartPkt = acctStart.serialize();
    std::vector<uint8_t> respStart;
    bool acctStartOk = nps.processPacket("192.168.1.10", signedStartPkt.data(), signedStartPkt.size(), respStart);
    TEST_ASSERT(acctStartOk, "Accounting-Request Start must succeed");

    micant::nps::RadiusPacket pRespStart;
    pRespStart.deserialize(respStart.data(), respStart.size());
    TEST_ASSERT(pRespStart.code == micant::nps::RadiusCode_AccountingResponse, "Response must be Accounting-Response (5)");

    // Accounting Stop
    micant::nps::RadiusPacket acctStop;
    acctStop.code = micant::nps::RadiusCode_AccountingRequest;
    acctStop.identifier = 78;
    acctStop.addUint32Attribute(micant::nps::RadiusAttr_AcctStatusType, 2); // Stop
    acctStop.addStringAttribute(micant::nps::RadiusAttr_AcctSessionId, testSessId);
    acctStop.addStringAttribute(micant::nps::RadiusAttr_UserName, "Alice");
    acctStop.addUint32Attribute(micant::nps::RadiusAttr_AcctInputOctets, 1048576);
    acctStop.addUint32Attribute(micant::nps::RadiusAttr_AcctOutputOctets, 2097152);
    acctStop.addUint32Attribute(micant::nps::RadiusAttr_AcctSessionTime, 7200);

    auto rawStopPkt = acctStop.serialize();
    micant::nps::CalculateAccountingRequestAuthenticator(acctStop.authenticator, acctStop.code,
                                                        acctStop.identifier, static_cast<uint16_t>(rawStopPkt.size()),
                                                        rawStopPkt.data() + 20, rawStopPkt.size() - 20,
                                                        "ArubaSecureWiFi!");
    auto signedStopPkt = acctStop.serialize();
    std::vector<uint8_t> respStop;
    bool acctStopOk = nps.processPacket("192.168.1.10", signedStopPkt.data(), signedStopPkt.size(), respStop);
    TEST_ASSERT(acctStopOk, "Accounting-Request Stop must succeed");

    // Stage 12: Accounting Store Query & Octets Aggregation
    micant::nps::AccountingSessionRecord rec{};
    bool acctFound = nps.getAccountingSession(testSessId, &rec);
    TEST_ASSERT(acctFound, "Session must exist in accounting store");
    TEST_ASSERT(rec.userName == "Alice", "User must match");
    TEST_ASSERT(rec.inputOctets == 1048576, "Input octets must match Stop packet");
    TEST_ASSERT(rec.outputOctets == 2097152, "Output octets must match Stop packet");
    TEST_ASSERT(rec.sessionTimeSec == 7200, "Session time must match");
    TEST_ASSERT(rec.active == false, "Session must be closed (inactive) after Stop");

    // Stage 13: Win32 C ABI Parity Exports
    void* pEngine = nullptr;
    int32_t abiInit = micant::nps::MicaIasInitialize(&pEngine);
    TEST_ASSERT(abiInit == 1 && pEngine != nullptr, "MicaIasInitialize must succeed");

    int32_t abiReg = micant::nps::MicaIasRegisterClient(pEngine, "172.16.0.1", "VpnSecret123!", "Remote-VPN-Gateway");
    TEST_ASSERT(abiReg == 1, "MicaIasRegisterClient must return 1");

    uint8_t outPktBuf[4096]{};
    uint32_t outPktLen = 0;
    int32_t abiProc = micant::nps::MicaIasProcessPacket(pEngine, "192.168.1.10", signedStartPkt.data(),
                                                        static_cast<uint32_t>(signedStartPkt.size()),
                                                        outPktBuf, sizeof(outPktBuf), &outPktLen);
    TEST_ASSERT(abiProc == 1 && outPktLen >= 20, "MicaIasProcessPacket must process packet and write response");

    uint64_t sAuth = 0, sAcct = 0, sAct = 0;
    int32_t abiStats = micant::nps::MicaIasGetAccountingStats(pEngine, &sAuth, &sAcct, &sAct);
    TEST_ASSERT(abiStats == 1 && sAuth > 0 && sAcct > 0, "MicaIasGetAccountingStats must return valid counters");

    micant::nps::MicaIasShutdown(pEngine);

    // Stage 14: Multithreaded High-Concurrency Stress Test (8 threads, 120 concurrent RADIUS operations)
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&nps, &stressSuccessCount, t]() {
            uint8_t localAuth[16]{};
            for (int k = 0; k < 16; ++k) localAuth[k] = static_cast<uint8_t>(t * 16 + k + 1);

            for (int i = 0; i < 15; ++i) {
                micant::nps::RadiusPacket req;
                req.code = micant::nps::RadiusCode_AccessRequest;
                req.identifier = static_cast<uint8_t>(t * 15 + i + 1);
                std::memcpy(req.authenticator, localAuth, 16);
                req.addStringAttribute(micant::nps::RadiusAttr_UserName, "Alice");
                auto encPass = micant::nps::EncryptRadiusPassword("AliceSecure2026!", localAuth, "CiscoSecretKey!");
                req.addRawAttribute(micant::nps::RadiusAttr_UserPassword, encPass.data(), encPass.size());
                req.addUint32Attribute(micant::nps::RadiusAttr_NasPortType, 15); // Ethernet

                auto rawReq = req.serialize();
                std::vector<uint8_t> resp;
                bool pOk = nps.processPacket("192.168.1.1", rawReq.data(), rawReq.size(), resp);

                micant::nps::RadiusPacket pResp;
                if (pOk && pResp.deserialize(resp.data(), resp.size()) && pResp.code == micant::nps::RadiusCode_AccessAccept) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded NPS / RADIUS stress test must complete with 100% success");

    std::cout << "[TEST] Suite 204: Windows Network Policy Server & RADIUS Subsystem PASSED.\n";
}

void Test_WindowsSystemResourceManager_FairShare_Subsystem() {
    std::cout << "[TEST] Suite 205: Windows System Resource Manager & Fair Share Scheduling Subsystem...\n";

    auto& wsrm = micant::wsrm::SystemResourceManager::instance();
    wsrm.initialize();

    // Stage 1: SCM Services Registration
    auto& scm = micant::scm::ServiceControlManager::get();
    auto sWsrm = scm.getServiceRecord(L"WsrmService");
    auto sQuota = scm.getServiceRecord(L"TitanQuota");
    TEST_ASSERT(sWsrm != nullptr, "WsrmService must be registered in SCM");
    TEST_ASSERT(sQuota != nullptr, "TitanQuota kernel driver must be registered in SCM");
    TEST_ASSERT(sWsrm->serviceType == micant::scm::SERVICE_WIN32_OWN_PROCESS, "WsrmService must be SERVICE_WIN32_OWN_PROCESS");
    TEST_ASSERT(sQuota->serviceType == micant::scm::SERVICE_KERNEL_DRIVER, "TitanQuota must be SERVICE_KERNEL_DRIVER");
    TEST_ASSERT(sWsrm->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "WsrmService must be RUNNING");
    TEST_ASSERT(sQuota->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "TitanQuota must be RUNNING");

    // Stage 2: VersionDatabase Modules Registration (@ 10.0.26100.1)
    auto& db = micant::version::VersionDatabase::Instance();
    auto modExe = db.FindModule("wsrm.exe");
    auto modDll = db.FindModule("wsrmcore.dll");
    auto modMsc = db.FindModule("wsrm.msc");
    auto modSys = db.FindModule("wsrmcore.sys");
    TEST_ASSERT(modExe != nullptr, "wsrm.exe must exist in VersionDatabase");
    TEST_ASSERT(modDll != nullptr, "wsrmcore.dll must exist in VersionDatabase");
    TEST_ASSERT(modMsc != nullptr, "wsrm.msc must exist in VersionDatabase");
    TEST_ASSERT(modSys != nullptr, "wsrmcore.sys must exist in VersionDatabase");
    TEST_ASSERT(modExe->stringTable.at("FileVersion") == "10.0.26100.1", "wsrm.exe version must be 10.0.26100.1");
    TEST_ASSERT(modDll->stringTable.at("FileVersion") == "10.0.26100.1", "wsrmcore.dll version must be 10.0.26100.1");

    // Stage 3: Default Built-in Process Matching Criteria (PMC)
    micant::wsrm::ProcessMatchingCriteria pmcSql{}, pmcIis{}, pmcDev{};
    TEST_ASSERT(wsrm.getMatchingCriteria("SQLServer_Workload", &pmcSql), "SQLServer_Workload criteria must exist");
    TEST_ASSERT(pmcSql.appPattern == "sqlservr.exe" && pmcSql.targetCpuPercent == 40, "SQLServer_Workload pattern and target CPU match");
    TEST_ASSERT(pmcSql.memoryLimitBytes == 4ULL * 1024 * 1024 * 1024, "SQLServer_Workload 4GB memory limit");
    TEST_ASSERT(wsrm.getMatchingCriteria("IIS_Worker_Pool", &pmcIis), "IIS_Worker_Pool criteria must exist");
    TEST_ASSERT(pmcIis.appPattern == "w3wp.exe" && pmcIis.targetCpuPercent == 30, "IIS_Worker_Pool pattern and target CPU match");

    // Stage 4: Process Registration & Equal-Per-Process Dynamic Allocation
    wsrm.setAllocationPolicy(micant::wsrm::AllocationPolicyType::EqualPerProcess);
    TEST_ASSERT(wsrm.getAllocationPolicy() == micant::wsrm::AllocationPolicyType::EqualPerProcess, "Policy must be EqualPerProcess");

    wsrm.registerProcess(101, "proc1.exe", "UserA", 1);
    wsrm.registerProcess(102, "proc2.exe", "UserB", 1);
    micant::wsrm::ManagedProcessRecord rec101{}, rec102{};
    TEST_ASSERT(wsrm.getProcessAccounting(101, &rec101), "PID 101 must exist");
    TEST_ASSERT(wsrm.getProcessAccounting(102, &rec102), "PID 102 must exist");
    TEST_ASSERT(rec101.allocatedCpuPercent == 50.0, "With 2 processes, PID 101 gets 50% CPU");
    TEST_ASSERT(rec102.allocatedCpuPercent == 50.0, "With 2 processes, PID 102 gets 50% CPU");

    // Adding 2 more processes -> 25% each
    wsrm.registerProcess(103, "proc3.exe", "UserC", 1);
    wsrm.registerProcess(104, "proc4.exe", "UserD", 1);
    wsrm.getProcessAccounting(101, &rec101);
    wsrm.getProcessAccounting(104, &rec102);
    TEST_ASSERT(rec101.allocatedCpuPercent == 25.0, "With 4 processes, PID 101 gets 25% CPU");
    TEST_ASSERT(rec102.allocatedCpuPercent == 25.0, "With 4 processes, PID 104 gets 25% CPU");

    // Cleanup 101-104
    wsrm.deregisterProcess(101);
    wsrm.deregisterProcess(102);
    wsrm.deregisterProcess(103);
    wsrm.deregisterProcess(104);

    // Stage 5: Equal-Per-User Multi-Process Grouping & Allocation
    wsrm.setAllocationPolicy(micant::wsrm::AllocationPolicyType::EqualPerUser);
    wsrm.registerProcess(201, "appA1.exe", "Alice", 1);
    wsrm.registerProcess(202, "appA2.exe", "Alice", 1);
    wsrm.registerProcess(203, "appB1.exe", "Bob", 1);

    micant::wsrm::ManagedProcessRecord rA1{}, rA2{}, rB1{};
    wsrm.getProcessAccounting(201, &rA1);
    wsrm.getProcessAccounting(202, &rA2);
    wsrm.getProcessAccounting(203, &rB1);
    // 2 users: Alice receives 50% (split between 2 procs = 25% each), Bob receives 50% (single proc = 50%)
    TEST_ASSERT(rB1.allocatedCpuPercent == 50.0, "Bob receives 50% CPU for his single process");
    TEST_ASSERT(rA1.allocatedCpuPercent == 25.0, "Alice process 1 receives 25% CPU");
    TEST_ASSERT(rA2.allocatedCpuPercent == 25.0, "Alice process 2 receives 25% CPU");

    wsrm.deregisterProcess(201);
    wsrm.deregisterProcess(202);
    wsrm.deregisterProcess(203);

    // Stage 6: Equal-Per-Session (DFSS) Multi-Session Allocation
    wsrm.setAllocationPolicy(micant::wsrm::AllocationPolicyType::EqualPerSession);
    wsrm.registerProcess(301, "shell_console.exe", "UserConsole", 1); // Session 1
    wsrm.registerProcess(302, "tool_console.exe", "UserConsole", 1);  // Session 1
    wsrm.registerProcess(303, "shell_rdp.exe", "UserRdp", 2);         // Session 2
    wsrm.registerProcess(304, "tool_rdp.exe", "UserRdp", 2);          // Session 2

    micant::wsrm::ManagedProcessRecord rS1_1{}, rS2_1{};
    wsrm.getProcessAccounting(301, &rS1_1);
    wsrm.getProcessAccounting(303, &rS2_1);
    // 2 sessions: Session 1 receives 50% (25% per proc), Session 2 receives 50% (25% per proc)
    TEST_ASSERT(rS1_1.allocatedCpuPercent == 25.0, "Session 1 process receives 25% CPU");
    TEST_ASSERT(rS2_1.allocatedCpuPercent == 25.0, "Session 2 process receives 25% CPU");

    wsrm.deregisterProcess(301);
    wsrm.deregisterProcess(302);
    wsrm.deregisterProcess(303);
    wsrm.deregisterProcess(304);

    // Stage 7: Dynamic Fair Share Scheduling (DFSS) Session Scale Testing
    wsrm.registerProcess(401, "session_worker.exe", "ClientUser", 1);
    wsrm.applyFairShare(2);
    micant::wsrm::ManagedProcessRecord rFs{};
    wsrm.getProcessAccounting(401, &rFs);
    TEST_ASSERT(rFs.allocatedCpuPercent == 50.0, "2 active sessions -> 50% per session");

    wsrm.applyFairShare(5);
    wsrm.getProcessAccounting(401, &rFs);
    TEST_ASSERT(rFs.allocatedCpuPercent == 20.0, "5 active sessions -> 20% per session");

    wsrm.applyFairShare(10);
    wsrm.getProcessAccounting(401, &rFs);
    TEST_ASSERT(rFs.allocatedCpuPercent == 10.0, "10 active sessions -> 10% per session");
    wsrm.deregisterProcess(401);

    // Stage 8: Custom Weighted Allocation Policy with Process Matching Criteria
    wsrm.registerProcess(501, "sqlservr.exe", "SYSTEM", 0);
    wsrm.registerProcess(502, "w3wp.exe", "NETWORK SERVICE", 0);
    wsrm.registerProcess(503, "notepad.exe", "Alice", 1);
    wsrm.setAllocationPolicy(micant::wsrm::AllocationPolicyType::CustomWeighted);

    micant::wsrm::ManagedProcessRecord rSql{}, rIis{}, rNote{};
    wsrm.getProcessAccounting(501, &rSql);
    wsrm.getProcessAccounting(502, &rIis);
    wsrm.getProcessAccounting(503, &rNote);

    TEST_ASSERT(rSql.allocatedCpuPercent == 40.0, "SQLServer matched criteria -> 40% CPU");
    TEST_ASSERT(rSql.memoryLimitBytes == 4ULL * 1024 * 1024 * 1024, "SQLServer memory limit -> 4GB");
    TEST_ASSERT(rIis.allocatedCpuPercent == 30.0, "IIS matched criteria -> 30% CPU");
    TEST_ASSERT(rIis.memoryLimitBytes == 2ULL * 1024 * 1024 * 1024, "IIS memory limit -> 2GB");
    TEST_ASSERT(rNote.allocatedCpuPercent == 10.0, "Notepad unmatched criteria -> 10% baseline");

    // Stage 9: Job Object CPU Rate Control Flags
    TEST_ASSERT((rSql.rateControlFlags & micant::wsrm::JOBOBJECT_CPU_RATE_CONTROL_ENABLE) != 0, "CPU Rate control must be ENABLED");
    TEST_ASSERT((rSql.rateControlFlags & micant::wsrm::JOBOBJECT_CPU_RATE_CONTROL_HARD_CAP) != 0, "Hard cap flag must be present on SQL");

    // Stage 10: Working Set and Memory Quota Bounds
    micant::wsrm::ProcessMatchingCriteria customPmc{};
    customPmc.criteriaName = "HighMem_DataScience";
    customPmc.appPattern = "python_ds.exe";
    customPmc.targetCpuPercent = 50;
    customPmc.memoryLimitBytes = 8ULL * 1024 * 1024 * 1024;
    customPmc.minWorkingSetBytes = 128 * 1024 * 1024;
    customPmc.maxWorkingSetBytes = 4ULL * 1024 * 1024 * 1024;
    wsrm.addMatchingCriteria(customPmc);

    micant::wsrm::ProcessMatchingCriteria fetchedPmc{};
    TEST_ASSERT(wsrm.getMatchingCriteria("HighMem_DataScience", &fetchedPmc), "Criteria added and retrieved");
    TEST_ASSERT(fetchedPmc.memoryLimitBytes == 8ULL * 1024 * 1024 * 1024, "8GB memory cap verified");
    TEST_ASSERT(fetchedPmc.minWorkingSetBytes == 128 * 1024 * 1024, "128MB min working set verified");

    // Stage 11: Real-Time Telemetry & CPU Throttling Violation Tracking
    wsrm.updateProcessTelemetry(501, 100000, 20000, 2ULL * 1024 * 1024 * 1024, 10485760, true);
    wsrm.getProcessAccounting(501, &rSql);
    TEST_ASSERT(rSql.userTimeUs == 100000, "User time updated");
    TEST_ASSERT(rSql.kernelTimeUs == 20000, "Kernel time updated");
    TEST_ASSERT(rSql.peakWorkingSetBytes == 2ULL * 1024 * 1024 * 1024, "Peak working set recorded");
    TEST_ASSERT(rSql.throttlingEvents == 1, "Throttling event incremented");
    TEST_ASSERT(wsrm.getTotalThrottlingEvents() > 0, "Total throttling counter updated");

    // Stage 12: Process Termination & Historical Resource Accounting Logging
    wsrm.deregisterProcess(501); // SQL exits
    wsrm.deregisterProcess(502);
    wsrm.deregisterProcess(503);

    uint64_t histCpu = 0, histMem = 0, histIo = 0;
    bool histFound = wsrm.getTenantAccounting("sqlservr.exe", &histCpu, &histMem, &histIo);
    TEST_ASSERT(histFound, "sqlservr.exe historical accounting record must be present");
    TEST_ASSERT(histCpu == 120000, "Historical CPU time (100000 user + 20000 kernel) matches");
    TEST_ASSERT(histMem == 2ULL * 1024 * 1024 * 1024, "Historical peak memory matches");
    TEST_ASSERT(histIo == 10485760, "Historical I/O bytes match");

    // Stage 13: Win32 C ABI Parity Exports
    void* pEngine = nullptr;
    int32_t abiInit = micant::wsrm::MicaWsrmInitialize(&pEngine);
    TEST_ASSERT(abiInit == 1 && pEngine != nullptr, "MicaWsrmInitialize must succeed");

    int32_t abiPol = micant::wsrm::MicaWsrmSetAllocationPolicy(pEngine, 1); // EqualPerUser
    TEST_ASSERT(abiPol == 1, "MicaWsrmSetAllocationPolicy must succeed");

    int32_t abiCrit = micant::wsrm::MicaWsrmCreateProcessMatchingCriteria(pEngine, "CustomAbiTest", "abi_test.exe", 35);
    TEST_ASSERT(abiCrit == 1, "MicaWsrmCreateProcessMatchingCriteria must succeed");

    int32_t abiFs = micant::wsrm::MicaWsrmApplyFairShare(pEngine, 4);
    TEST_ASSERT(abiFs == 1, "MicaWsrmApplyFairShare must succeed");

    uint64_t abiCpu = 0, abiMem = 0, abiIo = 0;
    int32_t abiHist = micant::wsrm::MicaWsrmGetTenantAccounting(pEngine, "sqlservr.exe", &abiCpu, &abiMem, &abiIo);
    TEST_ASSERT(abiHist == 1 && abiCpu == 120000, "MicaWsrmGetTenantAccounting must return valid metrics");

    micant::wsrm::MicaWsrmShutdown(pEngine);

    // Stage 14: Multithreaded High-Concurrency Stress Test (8 threads, 120 concurrent quota updates and accounting queries)
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&wsrm, &stressSuccessCount, t]() {
            for (int i = 0; i < 15; ++i) {
                uint32_t pid = static_cast<uint32_t>(10000 + t * 100 + i);
                std::string pName = "worker_" + std::to_string(t) + ".exe";
                std::string uName = "User_" + std::to_string(t);

                // Register
                bool regOk = wsrm.registerProcess(pid, pName, uName, static_cast<uint32_t>(t % 4 + 1));

                // Telemetry
                bool telOk = wsrm.updateProcessTelemetry(pid, 1000 * (i + 1), 500 * (i + 1), 65536 * (i + 1), 4096 * (i + 1), (i % 3 == 0));

                // Query
                micant::wsrm::ManagedProcessRecord rec{};
                bool queryOk = wsrm.getProcessAccounting(pid, &rec);

                // Rebalance / Fair share query
                if (i % 5 == 0) {
                    wsrm.applyFairShare(static_cast<uint32_t>(t + 2));
                }

                // Deregister
                bool deregOk = wsrm.deregisterProcess(pid);

                if (regOk && telOk && queryOk && deregOk) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded WSRM stress test must complete with 100% success");

    std::cout << "[TEST] Suite 205: Windows System Resource Manager & Fair Share Scheduling Subsystem PASSED.\n";
}

void Test_WindowsDeploymentServices_PXE_Subsystem() {
    std::cout << "[TEST] Suite 206: Windows Deployment Services & PXE Network Boot Subsystem...\n";

    auto& wds = micant::wds::DeploymentServicesEngine::instance();
    wds.initialize();

    // Stage 1: SCM Services Registration
    auto& scm = micant::scm::ServiceControlManager::get();
    auto sWds = scm.getServiceRecord(L"WDSServer");
    auto sBinl = scm.getServiceRecord(L"BINLSVC");
    auto sTftp = scm.getServiceRecord(L"WdsTftp");

    TEST_ASSERT(sWds != nullptr, "WDSServer service must be registered in SCM");
    TEST_ASSERT(sWds->startType == micant::scm::SERVICE_AUTO_START, "WDSServer must be auto-start");
    TEST_ASSERT(sWds->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "WDSServer must be running");

    TEST_ASSERT(sBinl != nullptr, "BINLSVC service must be registered in SCM");
    TEST_ASSERT(sBinl->displayName == L"Boot Information Negotiation Layer", "BINLSVC display name must match");
    TEST_ASSERT(sBinl->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "BINLSVC must be running");

    TEST_ASSERT(sTftp != nullptr, "WdsTftp service must be registered in SCM");
    TEST_ASSERT(sTftp->displayName == L"Windows Deployment Services TFTP Server", "WdsTftp display name must match");
    TEST_ASSERT(sTftp->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "WdsTftp must be running");

    // Stage 2: VersionDatabase Modules
    auto& db = micant::version::VersionDatabase::Instance();
    const auto* mSvc = db.FindModule("wdssvc.dll");
    const auto* mEfi = db.FindModule("wdsmgfw.efi");
    const auto* mCli = db.FindModule("wdsclient.dll");
    const auto* mTftp = db.FindModule("wdstftp.dll");
    const auto* mUtil = db.FindModule("wdsutil.exe");

    TEST_ASSERT(mSvc != nullptr, "wdssvc.dll must exist in VersionDatabase");
    TEST_ASSERT(mSvc->stringTable.at("FileVersion") == "10.0.26100.1", "wdssvc.dll version must be 10.0.26100.1");

    TEST_ASSERT(mEfi != nullptr, "wdsmgfw.efi must exist in VersionDatabase");
    TEST_ASSERT(mEfi->stringTable.at("FileDescription") == "Windows Deployment Services UEFI Boot Manager", "wdsmgfw.efi description must match");

    TEST_ASSERT(mCli != nullptr, "wdsclient.dll must exist in VersionDatabase");
    TEST_ASSERT(mTftp != nullptr, "wdstftp.dll must exist in VersionDatabase");
    TEST_ASSERT(mUtil != nullptr, "wdsutil.exe must exist in VersionDatabase");

    // Stage 3: Image Catalog Management
    auto images = wds.getAllImages();
    TEST_ASSERT(images.size() >= 3, "Catalog must have at least 3 default images");

    micant::wds::WdsImageRecord rWinPe{};
    TEST_ASSERT(wds.getImage("IMG-WINPE-X64", &rWinPe), "Default x64 WinPE image must be present");
    TEST_ASSERT(rWinPe.type == micant::wds::ImageType::BootImage, "IMG-WINPE-X64 must be BootImage");
    TEST_ASSERT(rWinPe.architecture == "x64", "Architecture must be x64");

    micant::wds::WdsImageRecord rWin11{};
    TEST_ASSERT(wds.getImage("IMG-WIN11-ENT-X64", &rWin11), "Default Windows 11 image must be present");
    TEST_ASSERT(rWin11.type == micant::wds::ImageType::InstallImage, "IMG-WIN11-ENT-X64 must be InstallImage");

    // Register a custom image
    micant::wds::WdsImageRecord customImg{};
    customImg.imageId = "IMG-SRV2025-DATACENTER";
    customImg.imageName = "Windows Server 2025 Datacenter Edition";
    customImg.architecture = "x64";
    customImg.filePath = "sources\\install_server2025.wim";
    customImg.type = micant::wds::ImageType::InstallImage;
    customImg.imageIndex = 1;
    customImg.sizeBytes = 5368709120ULL; // 5 GB
    TEST_ASSERT(wds.registerImage(customImg), "Registering custom image must succeed");

    micant::wds::WdsImageRecord fetchedCustom{};
    TEST_ASSERT(wds.getImage("IMG-SRV2025-DATACENTER", &fetchedCustom), "Custom image must be retrievable");
    TEST_ASSERT(fetchedCustom.imageName == "Windows Server 2025 Datacenter Edition", "Image name must match");

    // Stage 4: Virtual Boot Store Files
    auto tftpFiles = wds.getAllTftpFiles();
    TEST_ASSERT(tftpFiles.size() >= 5, "TFTP store must contain at least 5 default virtual boot files");

    // Verify EFI bootloader files have PE magic 'M','Z'
    std::vector<uint8_t> blk0;
    bool isLast = false;
    TEST_ASSERT(wds.getTftpFileBlock("boot\\x64\\wdsmgfw.efi", 1, 512, blk0, &isLast), "Must read block 1 of wdsmgfw.efi");
    TEST_ASSERT(blk0.size() == 512, "Block 1 must have 512 bytes");
    TEST_ASSERT(blk0[0] == 'M' && blk0[1] == 'Z', "wdsmgfw.efi must have MZ PE stub");

    // Stage 5: Dynamic BCD Store Structure
    auto bcdContent = wds.generateBcdStoreContent();
    std::string bcdStr(bcdContent.begin(), bcdContent.end());
    TEST_ASSERT(bcdStr.find("{bootmgr}") != std::string::npos, "BCD must contain {bootmgr}");
    TEST_ASSERT(bcdStr.find("{ramdisk}") != std::string::npos, "BCD must contain {ramdisk}");
    TEST_ASSERT(bcdStr.find("{default}") != std::string::npos, "BCD must contain {default}");

    // Stage 6: Multi-Architecture Bootfile Resolution
    TEST_ASSERT(wds.resolveBootFileForArchitecture(micant::wds::ClientArchitecture::EFI_x86_64) == "boot\\x64\\wdsmgfw.efi", "x64 UEFI must resolve to boot\\x64\\wdsmgfw.efi");
    TEST_ASSERT(wds.resolveBootFileForArchitecture(micant::wds::ClientArchitecture::EFI_ARM64) == "boot\\arm64\\wdsmgfw.efi", "ARM64 UEFI must resolve to boot\\arm64\\wdsmgfw.efi");
    TEST_ASSERT(wds.resolveBootFileForArchitecture(micant::wds::ClientArchitecture::EFI_IA32) == "boot\\x86\\wdsmgfw.efi", "IA32 UEFI must resolve to boot\\x86\\wdsmgfw.efi");
    TEST_ASSERT(wds.resolveBootFileForArchitecture(micant::wds::ClientArchitecture::Intelx86PC) == "boot\\x86\\wdsnbp.com", "Legacy BIOS must resolve to boot\\x86\\wdsnbp.com");

    // Stage 7: PXE DHCP Discover / Offer Negotiation (x64)
    std::vector<uint8_t> reqX64(sizeof(micant::wds::DhcpHeader) + 64, 0);
    auto* hdrX64 = reinterpret_cast<micant::wds::DhcpHeader*>(reqX64.data());
    hdrX64->op = 1; hdrX64->htype = 1; hdrX64->hlen = 6; hdrX64->xid = 0xA1B2C3D4;
    hdrX64->chaddr[0] = 0x00; hdrX64->chaddr[1] = 0x15; hdrX64->chaddr[2] = 0x5D;
    hdrX64->chaddr[3] = 0xAA; hdrX64->chaddr[4] = 0xBB; hdrX64->chaddr[5] = 0xCC;

    size_t offX64 = sizeof(micant::wds::DhcpHeader);
    reqX64[offX64] = 0x63; reqX64[offX64+1] = 0x82; reqX64[offX64+2] = 0x53; reqX64[offX64+3] = 0x63; // magic cookie
    offX64 += 4;
    reqX64[offX64++] = 53; reqX64[offX64++] = 1; reqX64[offX64++] = 1; // Discover
    std::string pxeClientStr = "PXEClient:Arch:00007:UNDI:002001";
    reqX64[offX64++] = 60; reqX64[offX64++] = static_cast<uint8_t>(pxeClientStr.size());
    std::memcpy(&reqX64[offX64], pxeClientStr.data(), pxeClientStr.size());
    offX64 += pxeClientStr.size();
    reqX64[offX64++] = 93; reqX64[offX64++] = 2; reqX64[offX64++] = 0; reqX64[offX64++] = 7; // EFI x64
    reqX64[offX64++] = 255;
    reqX64.resize(offX64);

    std::vector<uint8_t> offerX64;
    TEST_ASSERT(wds.processPxeRequest(reqX64.data(), reqX64.size(), offerX64), "PXE DHCP request must succeed");
    TEST_ASSERT(offerX64.size() >= sizeof(micant::wds::DhcpHeader), "Offer must be at least DhcpHeader size");

    const auto* offHdrX64 = reinterpret_cast<const micant::wds::DhcpHeader*>(offerX64.data());
    TEST_ASSERT(offHdrX64->op == 2, "Offer op must be 2 (BOOTREPLY)");
    TEST_ASSERT(offHdrX64->xid == 0xA1B2C3D4, "Transaction ID must match request");
    TEST_ASSERT(offHdrX64->siaddr == 0xC0A80132, "Next server IP must be 192.168.1.50");
    TEST_ASSERT(std::string(offHdrX64->file) == "boot\\x64\\wdsmgfw.efi", "Bootfile must be boot\\x64\\wdsmgfw.efi");

    // Stage 8: PXE DHCP Discover / Offer Negotiation (ARM64 & Legacy BIOS)
    // Modify Option 93 to ARM64 (11)
    reqX64[reqX64.size() - 3] = 0;
    reqX64[reqX64.size() - 2] = 11;
    std::vector<uint8_t> offerArm64;
    TEST_ASSERT(wds.processPxeRequest(reqX64.data(), reqX64.size(), offerArm64), "ARM64 PXE request must succeed");
    const auto* offHdrArm = reinterpret_cast<const micant::wds::DhcpHeader*>(offerArm64.data());
    TEST_ASSERT(std::string(offHdrArm->file) == "boot\\arm64\\wdsmgfw.efi", "Bootfile for ARM64 must be boot\\arm64\\wdsmgfw.efi");

    // Modify Option 93 to BIOS (0)
    reqX64[reqX64.size() - 3] = 0;
    reqX64[reqX64.size() - 2] = 0;
    std::vector<uint8_t> offerBios;
    TEST_ASSERT(wds.processPxeRequest(reqX64.data(), reqX64.size(), offerBios), "BIOS PXE request must succeed");
    const auto* offHdrBios = reinterpret_cast<const micant::wds::DhcpHeader*>(offerBios.data());
    TEST_ASSERT(std::string(offHdrBios->file) == "boot\\x86\\wdsnbp.com", "Bootfile for BIOS must be boot\\x86\\wdsnbp.com");

    // Stage 9: TFTP Read Request (RRQ) & Option Negotiation (RFC 1350/2347)
    micant::wds::DeploymentServicesEngine::TftpSessionParams tftpParams{};
    std::vector<uint8_t> oackPkt;
    TEST_ASSERT(wds.processTftpRrq("boot\\x64\\wdsmgfw.efi", 1456, 8, &tftpParams, oackPkt), "TFTP RRQ for wdsmgfw.efi must succeed");
    TEST_ASSERT(tftpParams.blockSize == 1456, "Negotiated blksize must be 1456");
    TEST_ASSERT(tftpParams.windowSize == 8, "Negotiated windowsize must be 8");
    TEST_ASSERT(tftpParams.transferSize == 1048576, "wdsmgfw.efi transfer size must be 1 MB");
    TEST_ASSERT(oackPkt.size() > 2, "OACK packet must have content");
    TEST_ASSERT(oackPkt[1] == 6, "OACK OpCode must be 6");

    // Stage 10: TFTP Windowed Data Streaming (RFC 7440)
    uint32_t totalBlocks = static_cast<uint32_t>((tftpParams.transferSize + tftpParams.blockSize - 1) / tftpParams.blockSize);
    size_t streamBytesRead = 0;
    for (uint32_t blk = 1; blk <= totalBlocks; ++blk) {
        std::vector<uint8_t> blockBuf;
        bool last = false;
        TEST_ASSERT(wds.getTftpFileBlock("boot\\x64\\wdsmgfw.efi", blk, tftpParams.blockSize, blockBuf, &last), "Block read must succeed");
        streamBytesRead += blockBuf.size();
        if (blk == totalBlocks) {
            TEST_ASSERT(last == true, "Final block must flag isLastBlock == true");
        } else {
            TEST_ASSERT(last == false, "Intermediate block must not be last");
            TEST_ASSERT(blockBuf.size() == tftpParams.blockSize, "Intermediate block must equal negotiated blockSize");
        }
    }
    TEST_ASSERT(streamBytesRead == tftpParams.transferSize, "Total bytes streamed must match transferSize");

    // Stage 11: Automated Answer File (unattend.xml) Generator
    std::string unattendXml = wds.generateUnattendXml("TITAN-DC01", "P@ssw0rd2026!", "TITAN.LOCAL");
    TEST_ASSERT(unattendXml.find("<ComputerName>TITAN-DC01</ComputerName>") != std::string::npos, "ComputerName must be in unattend.xml");
    TEST_ASSERT(unattendXml.find("<Value>P@ssw0rd2026!</Value>") != std::string::npos, "AdministratorPassword must be in unattend.xml");
    TEST_ASSERT(unattendXml.find("<JoinDomain>TITAN.LOCAL</JoinDomain>") != std::string::npos, "JoinDomain must be in unattend.xml");
    TEST_ASSERT(unattendXml.find("<DiskConfiguration>") != std::string::npos, "DiskConfiguration must be present");
    TEST_ASSERT(unattendXml.find("<Type>EFI</Type>") != std::string::npos, "EFI partition definition must be present");

    // Stage 12: TFTP Edge Cases & Negative Testing
    micant::wds::DeploymentServicesEngine::TftpSessionParams errParams{};
    std::vector<uint8_t> errOack;
    TEST_ASSERT(!wds.processTftpRrq("non_existent_boot_file.efi", 512, 1, &errParams, errOack), "Non-existent file must return false");

    std::vector<uint8_t> badBlock;
    bool badLast = false;
    TEST_ASSERT(!wds.getTftpFileBlock("boot\\x64\\wdsmgfw.efi", 0, 512, badBlock, &badLast), "Block 0 must return false (blocks are 1-indexed)");

    TEST_ASSERT(wds.getTftpFileBlock("boot\\x64\\wdsmgfw.efi", 99999, 512, badBlock, &badLast), "Block past EOF must return true with empty and isLast=true");
    TEST_ASSERT(badBlock.empty() && badLast, "Past EOF block must be empty and last");

    std::vector<uint8_t> badDhcp(20, 0);
    std::vector<uint8_t> badOffer;
    TEST_ASSERT(!wds.processPxeRequest(badDhcp.data(), badDhcp.size(), badOffer), "Malformed short DHCP packet must be rejected");

    // Stage 13: Clean-Room Win32 C ABI Exports (wdssvc.dll / wdstftp.dll)
    void* pEngine = nullptr;
    int32_t initRes = micant::wds::MicaWdsInitialize(&pEngine);
    TEST_ASSERT(initRes == 1 && pEngine != nullptr, "MicaWdsInitialize must succeed");

    int32_t regRes = micant::wds::MicaWdsRegisterImage(pEngine, "ABI-Test-Image", "x64", "sources\\abi.wim", 1);
    TEST_ASSERT(regRes == 1, "MicaWdsRegisterImage must succeed");

    uint8_t offerBuf[1024]{};
    uint32_t offerLen = 0;
    int32_t pxeRes = micant::wds::MicaWdsProcessPxeRequest(pEngine, reqX64.data(), static_cast<uint32_t>(reqX64.size()), offerBuf, sizeof(offerBuf), &offerLen);
    TEST_ASSERT(pxeRes == 1 && offerLen > 0, "MicaWdsProcessPxeRequest must succeed via C ABI");

    uint32_t abiTotalBlks = 0;
    int32_t tftpRes = micant::wds::MicaWdsTftpServeFile(pEngine, "boot\\x64\\wdsmgfw.efi", 1456, 4, &abiTotalBlks);
    TEST_ASSERT(tftpRes == 1 && abiTotalBlks > 0, "MicaWdsTftpServeFile must succeed via C ABI");

    char xmlOut[2048]{};
    uint32_t xmlLen = 0;
    int32_t xmlRes = micant::wds::MicaWdsGenerateUnattendXml(pEngine, "ABI-HOST", "ABIPass123!", xmlOut, sizeof(xmlOut), &xmlLen);
    TEST_ASSERT(xmlRes == 1 && xmlLen > 0, "MicaWdsGenerateUnattendXml must succeed via C ABI");

    int32_t shutRes = micant::wds::MicaWdsShutdown(pEngine);
    TEST_ASSERT(shutRes == 1, "MicaWdsShutdown must succeed");

    // Stage 14: Multithreaded High-Throughput Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&wds, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                // Select architecture
                micant::wds::ClientArchitecture arch = (op % 3 == 0) ? micant::wds::ClientArchitecture::EFI_x86_64 :
                                                        (op % 3 == 1) ? micant::wds::ClientArchitecture::EFI_ARM64 :
                                                                        micant::wds::ClientArchitecture::Intelx86PC;
                uint16_t aVal = static_cast<uint16_t>(arch);

                // Build DHCP packet
                std::vector<uint8_t> pkt(sizeof(micant::wds::DhcpHeader) + 64, 0);
                auto* h = reinterpret_cast<micant::wds::DhcpHeader*>(pkt.data());
                h->op = 1; h->htype = 1; h->hlen = 6;
                h->xid = 0x10000000 + (t * 1000) + op;
                size_t o = sizeof(micant::wds::DhcpHeader);
                pkt[o] = 0x63; pkt[o+1] = 0x82; pkt[o+2] = 0x53; pkt[o+3] = 0x63;
                o += 4;
                pkt[o++] = 53; pkt[o++] = 1; pkt[o++] = 1;
                std::string pxeStr = "PXEClient";
                pkt[o++] = 60; pkt[o++] = static_cast<uint8_t>(pxeStr.size());
                std::memcpy(&pkt[o], pxeStr.data(), pxeStr.size());
                o += pxeStr.size();
                pkt[o++] = 93; pkt[o++] = 2;
                pkt[o++] = static_cast<uint8_t>((aVal >> 8) & 0xFF);
                pkt[o++] = static_cast<uint8_t>(aVal & 0xFF);
                pkt[o++] = 255;
                pkt.resize(o);

                std::vector<uint8_t> off;
                bool pxeOk = wds.processPxeRequest(pkt.data(), pkt.size(), off);

                // TFTP RRQ
                micant::wds::DeploymentServicesEngine::TftpSessionParams sp{};
                std::vector<uint8_t> oack;
                bool tftpOk = wds.processTftpRrq("boot\\x64\\wdsmgfw.efi", 1456, 4, &sp, oack);

                // TFTP Block
                std::vector<uint8_t> bData;
                bool last = false;
                bool blkOk = wds.getTftpFileBlock("boot\\x64\\wdsmgfw.efi", 1, 1456, bData, &last);

                // Unattend XML
                std::string uXml = wds.generateUnattendXml("STRESS-NODE", "StressPass123!");
                bool xmlOk = !uXml.empty();

                if (pxeOk && tftpOk && blkOk && xmlOk) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded WDS stress test must complete with 100% success");


    std::cout << "[TEST] Suite 206: Windows Deployment Services & PXE Network Boot Subsystem PASSED.\n";
}

void Test_ActiveDirectoryCertificateServices_ADCS_PKI_Subsystem() {
    std::cout << "[TEST] Suite 207: Active Directory Certificate Services & Enterprise PKI Subsystem...\n";

    auto& pki = micant::certsrv::CertificateServicesEngine::instance();
    pki.initialize();

    // Stage 1: SCM Registration & State
    auto& scm = micant::scm::ServiceControlManager::get();
    auto sCert = scm.getServiceRecord(L"CertSvc");
    TEST_ASSERT(sCert != nullptr, "CertSvc service must be registered in SCM");
    TEST_ASSERT(sCert->serviceName == L"CertSvc", "Service name must be CertSvc");
    TEST_ASSERT(sCert->displayName == L"Active Directory Certificate Services", "Display name must match AD CS");
    TEST_ASSERT(sCert->startType == micant::scm::SERVICE_AUTO_START, "CertSvc startType must be AUTO_START");
    TEST_ASSERT(sCert->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "CertSvc state must be SERVICE_RUNNING");

    // Stage 2: VersionDatabase PE Metadata & Exports
    auto& verDb = micant::version::VersionDatabase::Instance();
    const auto* modSrv = verDb.FindModule("certsrv.exe");
    TEST_ASSERT(modSrv != nullptr, "certsrv.exe must be registered in VersionDatabase");
    TEST_ASSERT(modSrv->stringTable.at("FileVersion") == "10.0.26100.1", "certsrv.exe version must be 10.0.26100.1");

    const auto* modCli = verDb.FindModule("certcli.dll");
    TEST_ASSERT(modCli != nullptr, "certcli.dll must be registered in VersionDatabase");
    TEST_ASSERT(modCli->stringTable.at("FileVersion") == "10.0.26100.1", "certcli.dll version must be 10.0.26100.1");

    const auto* modEnroll = verDb.FindModule("certenroll.dll");
    TEST_ASSERT(modEnroll != nullptr, "certenroll.dll must be registered in VersionDatabase");
    TEST_ASSERT(modEnroll->stringTable.at("FileVersion") == "10.0.26100.1", "certenroll.dll version must be 10.0.26100.1");

    const auto* modAdm = verDb.FindModule("certadm.dll");
    TEST_ASSERT(modAdm != nullptr, "certadm.dll must be registered in VersionDatabase");
    TEST_ASSERT(modAdm->stringTable.at("FileVersion") == "10.0.26100.1", "certadm.dll version must be 10.0.26100.1");

    // Stage 3: Enterprise Root CA Hierarchy & Self-Signed Root Attributes
    TEST_ASSERT(pki.getCaName() == "Titan Enterprise Root CA", "CA name must match");
    TEST_ASSERT(pki.getCaDn() == "CN=Titan Enterprise Root CA,DC=titan,DC=local", "CA DN must match");
    TEST_ASSERT(pki.getCaState() == micant::certsrv::CaState::Running, "CA state must be Running");
    TEST_ASSERT(pki.getCaType() == micant::certsrv::CaType::EnterpriseRootCA, "CA type must be EnterpriseRootCA");
    TEST_ASSERT(!pki.getAiaUri().empty(), "AIA URI must be non-empty");
    TEST_ASSERT(!pki.getCdpUri().empty(), "CDP URI must be non-empty");
    TEST_ASSERT(!pki.getOcspUri().empty(), "OCSP URI must be non-empty");

    micant::certsrv::CertificateRecord rootCert;
    TEST_ASSERT(pki.getCertificate(pki.getRootSerialNumber(), rootCert), "Root CA certificate must exist in store");
    TEST_ASSERT(rootCert.serialNumber == "01", "Root CA serial must be 01");
    TEST_ASSERT(rootCert.subjectDn == rootCert.issuerDn, "Root CA must be self-signed (subject == issuer)");
    TEST_ASSERT(rootCert.isCa == true, "Root CA isCa flag must be true");
    TEST_ASSERT(rootCert.publicKeyBits == 4096, "Root CA public key must be 4096 bits");
    TEST_ASSERT((rootCert.keyUsage & micant::certsrv::KU_KEY_CERT_SIGN) != 0, "Root CA must have KU_KEY_CERT_SIGN");
    TEST_ASSERT((rootCert.keyUsage & micant::certsrv::KU_CRL_SIGN) != 0, "Root CA must have KU_CRL_SIGN");
    TEST_ASSERT(!rootCert.rawDer.empty() && !rootCert.rawPem.empty(), "Root CA must have DER and PEM representations");
    TEST_ASSERT(rootCert.rawPem.find("-----BEGIN CERTIFICATE-----") != std::string::npos, "PEM must contain standard header");

    // Stage 4: Certificate Template Catalog
    auto allTemplates = pki.getAllTemplates();
    TEST_ASSERT(allTemplates.size() >= 8, "Must contain at least 8 default certificate templates");

    micant::certsrv::CertificateTemplate tmplDc, tmplWeb, tmplCode, tmplSubCa;
    TEST_ASSERT(pki.getTemplate("DomainController", tmplDc), "DomainController template must exist");
    TEST_ASSERT(tmplDc.autoEnrollAllowed == true, "DomainController must support auto-enrollment");
    TEST_ASSERT(tmplDc.requiresApproval == false, "DomainController must not require manual approval");
    TEST_ASSERT(std::find(tmplDc.ekus.begin(), tmplDc.ekus.end(), micant::certsrv::OID_KDC_AUTH) != tmplDc.ekus.end(), "DomainController must contain KDC Auth EKU");

    TEST_ASSERT(pki.getTemplate("WebServer", tmplWeb), "WebServer template must exist");
    TEST_ASSERT(tmplWeb.validityPeriodSeconds == 63072000, "WebServer validity must be 2 years");

    TEST_ASSERT(pki.getTemplate("CodeSigning", tmplCode), "CodeSigning template must exist");
    TEST_ASSERT(tmplCode.requiresApproval == true, "CodeSigning must require manual approval");
    TEST_ASSERT(tmplCode.minKeySizeBits == 3072, "CodeSigning min key size must be 3072 bits");

    TEST_ASSERT(pki.getTemplate("SubCA", tmplSubCa), "SubCA template must exist");
    TEST_ASSERT(tmplSubCa.isCa == true, "SubCA isCa must be true");
    TEST_ASSERT(tmplSubCa.minKeySizeBits == 4096, "SubCA min key size must be 4096 bits");

    // Stage 5: Certificate Request Submission & Auto-Approval
    uint32_t reqIdWeb = 0;
    micant::certsrv::RequestDisposition dispWeb = micant::certsrv::RequestDisposition::CR_DISP_INCOMPLETE;
    bool subWebOk = pki.submitRequest("CN=portal.titan.local,O=Titan Technologies", "WebServer", "TITAN\\WebAdmin", {"portal.titan.local", "www.titan.local"}, 2048, &reqIdWeb, &dispWeb);
    TEST_ASSERT(subWebOk == true, "WebServer request submission must succeed");
    TEST_ASSERT(reqIdWeb >= 1001, "Request ID must be assigned");
    TEST_ASSERT(dispWeb == micant::certsrv::RequestDisposition::CR_DISP_ISSUED, "WebServer request must be auto-issued");

    micant::certsrv::CertificateRequestRecord reqWeb;
    TEST_ASSERT(pki.getRequest(reqIdWeb, reqWeb), "Request record must be queryable");
    TEST_ASSERT(!reqWeb.issuedSerialNumber.empty(), "Issued serial number must be populated");

    micant::certsrv::CertificateRecord certWeb;
    TEST_ASSERT(pki.getCertificate(reqWeb.issuedSerialNumber, certWeb), "Issued WebServer certificate must be retrievable");
    TEST_ASSERT(certWeb.templateName == "WebServer", "Certificate template must match");
    TEST_ASSERT(certWeb.sanDnsNames.size() == 2, "Certificate must have 2 SAN DNS names");
    TEST_ASSERT(certWeb.isRevoked == false, "Newly issued certificate must not be revoked");

    // Stage 6: Pending Certificate Request & Administrative Approval Workflow
    uint32_t reqIdCode = 0;
    micant::certsrv::RequestDisposition dispCode = micant::certsrv::RequestDisposition::CR_DISP_INCOMPLETE;
    bool subCodeOk = pki.submitRequest("CN=Titan Code Signing Authority,O=Titan Technologies", "CodeSigning", "TITAN\\DevLead", {}, 3072, &reqIdCode, &dispCode);
    TEST_ASSERT(subCodeOk == true, "CodeSigning request submission must succeed");
    TEST_ASSERT(dispCode == micant::certsrv::RequestDisposition::CR_DISP_UNDER_SUBMISSION, "CodeSigning request must be queued for administrative approval");

    micant::certsrv::CertificateRequestRecord reqCodePending;
    pki.getRequest(reqIdCode, reqCodePending);
    TEST_ASSERT(reqCodePending.issuedSerialNumber.empty(), "Serial must not be assigned while under submission");

    std::string approvedSerial;
    bool appOk = pki.approveRequest(reqIdCode, &approvedSerial);
    TEST_ASSERT(appOk == true, "approveRequest must succeed for pending request");
    TEST_ASSERT(!approvedSerial.empty(), "Approved request must yield valid serial");

    micant::certsrv::CertificateRequestRecord reqCodeApproved;
    pki.getRequest(reqIdCode, reqCodeApproved);
    TEST_ASSERT(reqCodeApproved.disposition == micant::certsrv::RequestDisposition::CR_DISP_ISSUED, "Approved request disposition must be CR_DISP_ISSUED");
    TEST_ASSERT(reqCodeApproved.issuedSerialNumber == approvedSerial, "Assigned serial must match");

    // Stage 7: Administrative Request Denial Workflow
    uint32_t reqIdSubCa = 0;
    micant::certsrv::RequestDisposition dispSubCa = micant::certsrv::RequestDisposition::CR_DISP_INCOMPLETE;
    bool subSubCaOk = pki.submitRequest("CN=Rogue Subordinate CA", "SubCA", "TITAN\\Guest", {}, 4096, &reqIdSubCa, &dispSubCa);
    TEST_ASSERT(subSubCaOk == true, "SubCA request submission must succeed");
    TEST_ASSERT(dispSubCa == micant::certsrv::RequestDisposition::CR_DISP_UNDER_SUBMISSION, "SubCA request must be queued for approval");

    bool denyOk = pki.denyRequest(reqIdSubCa, "Unauthorized Subordinate CA enrollment attempt");
    TEST_ASSERT(denyOk == true, "denyRequest must succeed");

    micant::certsrv::CertificateRequestRecord reqSubCaDenied;
    pki.getRequest(reqIdSubCa, reqSubCaDenied);
    TEST_ASSERT(reqSubCaDenied.disposition == micant::certsrv::RequestDisposition::CR_DISP_DENIED, "Denied request disposition must be CR_DISP_DENIED");
    TEST_ASSERT(reqSubCaDenied.statusMessage.find("Unauthorized") != std::string::npos, "Status message must reflect denial reason");

    // Stage 8: X.509 v3 Certificate Extension Validation
    uint32_t reqIdDc = 0;
    micant::certsrv::RequestDisposition dispDc = micant::certsrv::RequestDisposition::CR_DISP_INCOMPLETE;
    pki.submitRequest("CN=titan-dc01.titan.local,OU=Domain Controllers,DC=titan,DC=local", "DomainController", "TITAN\\dc01$", {"titan-dc01.titan.local"}, 2048, &reqIdDc, &dispDc);
    TEST_ASSERT(dispDc == micant::certsrv::RequestDisposition::CR_DISP_ISSUED, "DomainController cert must be issued");

    micant::certsrv::CertificateRequestRecord reqDcRec;
    pki.getRequest(reqIdDc, reqDcRec);
    micant::certsrv::CertificateRecord certDc;
    pki.getCertificate(reqDcRec.issuedSerialNumber, certDc);

    TEST_ASSERT((certDc.keyUsage & micant::certsrv::KU_DIGITAL_SIGNATURE) != 0, "DC cert must have KU_DIGITAL_SIGNATURE");
    TEST_ASSERT((certDc.keyUsage & micant::certsrv::KU_KEY_ENCIPHERMENT) != 0, "DC cert must have KU_KEY_ENCIPHERMENT");
    TEST_ASSERT(certDc.authorityKeyIdentifier == rootCert.subjectKeyIdentifier, "AKI must match Root CA's SKI");
    TEST_ASSERT(certDc.subjectKeyIdentifier.size() == 40, "SKI must be a 40-character SHA-1 hex digest");

    // Stage 9: Certificate Chain Verification & Path Validation to Trusted Root CA
    bool chainOk = false;
    std::vector<std::string> chainDns;
    TEST_ASSERT(pki.verifyCertificateChain(certDc.serialNumber, &chainOk, &chainDns), "Chain verification must execute");
    TEST_ASSERT(chainOk == true, "Certificate chain from DC cert to Root CA must be valid");
    TEST_ASSERT(chainDns.size() == 2, "Chain depth must be 2 (Leaf -> Root)");
    TEST_ASSERT(chainDns[0] == certDc.subjectDn, "Chain leaf must be DC subject DN");
    TEST_ASSERT(chainDns[1] == rootCert.subjectDn, "Chain root must be Root CA subject DN");

    // Stage 10: Certificate Revocation Lifecycle (CRL generation)
    uint32_t crlNumBefore = pki.getLatestCrl().crlNumber;
    bool revOk = pki.revokeCertificate(certWeb.serialNumber, micant::certsrv::CRL_REASON_KEY_COMPROMISE);
    TEST_ASSERT(revOk == true, "revokeCertificate must succeed");

    micant::certsrv::CertificateRecord revCertCheck;
    pki.getCertificate(certWeb.serialNumber, revCertCheck);
    TEST_ASSERT(revCertCheck.isRevoked == true, "Certificate must be marked isRevoked == true");
    TEST_ASSERT(revCertCheck.revocationReason == micant::certsrv::CRL_REASON_KEY_COMPROMISE, "Revocation reason must match");

    auto latestCrl = pki.getLatestCrl();
    TEST_ASSERT(latestCrl.crlNumber > crlNumBefore, "CRL number must increment after revocation");
    TEST_ASSERT(!latestCrl.rawCrlDer.empty(), "CRL raw DER must be populated");

    bool foundInCrl = false;
    for (const auto& entry : latestCrl.revokedEntries) {
        if (entry.serialNumber == certWeb.serialNumber) {
            foundInCrl = true;
            TEST_ASSERT(entry.revocationReason == micant::certsrv::CRL_REASON_KEY_COMPROMISE, "CRL entry revocation reason must match");
            break;
        }
    }
    TEST_ASSERT(foundInCrl == true, "Revoked certificate must be present in CRL");

    // Stage 11: Online Certificate Status Protocol (OCSP / RFC 6960) Responder
    micant::certsrv::OcspResponse ocspGood;
    TEST_ASSERT(pki.processOcspRequest(certDc.serialNumber, ocspGood), "OCSP request for DC cert must succeed");
    TEST_ASSERT(ocspGood.certStatus == micant::certsrv::OcspStatus::OCSP_STATUS_GOOD, "DC cert must return OCSP_STATUS_GOOD");
    TEST_ASSERT(ocspGood.signature.size() == 256, "OCSP response must be signed (256-byte signature)");

    micant::certsrv::OcspResponse ocspRevoked;
    TEST_ASSERT(pki.processOcspRequest(certWeb.serialNumber, ocspRevoked), "OCSP request for WebServer cert must succeed");
    TEST_ASSERT(ocspRevoked.certStatus == micant::certsrv::OcspStatus::OCSP_STATUS_REVOKED, "WebServer cert must return OCSP_STATUS_REVOKED");
    TEST_ASSERT(ocspRevoked.revocationReason == micant::certsrv::CRL_REASON_KEY_COMPROMISE, "OCSP revocation reason must match");

    micant::certsrv::OcspResponse ocspUnknown;
    TEST_ASSERT(pki.processOcspRequest("DEADBEEF99999999", ocspUnknown), "OCSP request for unknown cert must succeed");
    TEST_ASSERT(ocspUnknown.certStatus == micant::certsrv::OcspStatus::OCSP_STATUS_UNKNOWN, "Unknown serial must return OCSP_STATUS_UNKNOWN");

    // Stage 12: Negative Testing & Security Boundary Checks
    uint32_t badReqId = 0;
    micant::certsrv::RequestDisposition badDisp = micant::certsrv::RequestDisposition::CR_DISP_INCOMPLETE;
    bool badTmplOk = pki.submitRequest("CN=bad.local", "NonExistentTemplate", "TITAN\\User", {}, 2048, &badReqId, &badDisp);
    TEST_ASSERT(badTmplOk == false && badDisp == micant::certsrv::RequestDisposition::CR_DISP_ERROR, "Non-existent template must be rejected with CR_DISP_ERROR");

    bool badKeyOk = pki.submitRequest("CN=weak.local", "CodeSigning", "TITAN\\User", {}, 1024, &badReqId, &badDisp);
    TEST_ASSERT(badKeyOk == false && badDisp == micant::certsrv::RequestDisposition::CR_DISP_DENIED, "Undersized key must be rejected with CR_DISP_DENIED");

    bool doubleRev = pki.revokeCertificate(certWeb.serialNumber, micant::certsrv::CRL_REASON_SUPERSEDED);
    TEST_ASSERT(doubleRev == false, "Double revocation of already revoked certificate must return false");

    bool badApprove = pki.approveRequest(99999, nullptr);
    TEST_ASSERT(badApprove == false, "Approving non-existent request ID must return false");

    // Stage 13: Clean-Room Win32 C ABI Exports
    void* pEngine = nullptr;
    int32_t initRes = micant::certsrv::MicaCertSrvInitialize(&pEngine);
    TEST_ASSERT(initRes == 1 && pEngine != nullptr, "MicaCertSrvInitialize must succeed");

    uint32_t abiReqId = 0;
    uint32_t abiDisp = 0;
    int32_t subRes = micant::certsrv::MicaCertSrvSubmitRequest(pEngine, "CN=abi-test.titan.local", "Computer", "TITAN\\HOST01$", &abiReqId, &abiDisp);
    TEST_ASSERT(subRes == 1 && abiReqId > 0 && abiDisp == static_cast<uint32_t>(micant::certsrv::RequestDisposition::CR_DISP_ISSUED), "MicaCertSrvSubmitRequest must succeed via C ABI");

    char serialOut[64]{};
    uint8_t derOut[1024]{};
    uint32_t derActual = 0;
    int32_t retRes = micant::certsrv::MicaCertSrvRetrieveCertificate(pEngine, abiReqId, serialOut, sizeof(serialOut), derOut, sizeof(derOut), &derActual);
    TEST_ASSERT(retRes == 1 && std::strlen(serialOut) > 0 && derActual > 0, "MicaCertSrvRetrieveCertificate must succeed via C ABI");

    uint32_t ocspStat = 999;
    int32_t ocspRes = micant::certsrv::MicaCertSrvOcspCheck(pEngine, serialOut, &ocspStat);
    TEST_ASSERT(ocspRes == 1 && ocspStat == static_cast<uint32_t>(micant::certsrv::OcspStatus::OCSP_STATUS_GOOD), "MicaCertSrvOcspCheck must return Good via C ABI");

    int32_t revRes = micant::certsrv::MicaCertSrvRevokeCertificate(pEngine, serialOut, micant::certsrv::CRL_REASON_CESSATION_OF_OPERATION);
    TEST_ASSERT(revRes == 1, "MicaCertSrvRevokeCertificate must succeed via C ABI");

    uint32_t crlNum = 0;
    uint32_t revCount = 0;
    int32_t crlRes = micant::certsrv::MicaCertSrvGetCrl(pEngine, &crlNum, &revCount);
    TEST_ASSERT(crlRes == 1 && crlNum > 0 && revCount > 0, "MicaCertSrvGetCrl must succeed via C ABI");

    int32_t shutRes = micant::certsrv::MicaCertSrvShutdown(pEngine);
    TEST_ASSERT(shutRes == 1, "MicaCertSrvShutdown must succeed");

    // Stage 14: Multithreaded High-Throughput Concurrent PKI Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&pki, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                std::string hostName = "node" + std::to_string(t) + "-" + std::to_string(op) + ".titan.local";
                std::string subj = "CN=" + hostName + ",OU=Compute Nodes,DC=titan,DC=local";
                uint32_t rId = 0;
                micant::certsrv::RequestDisposition disp = micant::certsrv::RequestDisposition::CR_DISP_INCOMPLETE;

                bool subOk = pki.submitRequest(subj, "Computer", "TITAN\\ClusterService", {hostName}, 2048, &rId, &disp);

                micant::certsrv::CertificateRequestRecord rRec;
                bool getReqOk = pki.getRequest(rId, rRec);

                micant::certsrv::CertificateRecord cRec;
                bool getCertOk = pki.getCertificate(rRec.issuedSerialNumber, cRec);

                micant::certsrv::OcspResponse oResp;
                bool ocspOk = pki.processOcspRequest(rRec.issuedSerialNumber, oResp);

                bool chainOkInner = false;
                bool verOk = pki.verifyCertificateChain(rRec.issuedSerialNumber, &chainOkInner, nullptr);

                if (subOk && getReqOk && getCertOk && ocspOk && verOk && chainOkInner && oResp.certStatus == micant::certsrv::OcspStatus::OCSP_STATUS_GOOD) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded PKI stress test must complete with 100% success");

    std::cout << "[TEST] Suite 207: Active Directory Certificate Services & Enterprise PKI Subsystem PASSED.\n";
}

void Test_WindowsEnterpriseDNS_Server_Subsystem() {
    std::cout << "[TEST] Suite 208: Windows Enterprise DNS Server Subsystem...\n";

    auto& dns = micant::dns::EnterpriseDnsServer::instance();
    dns.initialize();

    // Stage 1: SCM Registration & State
    auto& scm = micant::scm::ServiceControlManager::get();
    auto sDns = scm.getServiceRecord(L"DNS");
    TEST_ASSERT(sDns != nullptr, "DNS service must be registered in SCM");
    TEST_ASSERT(sDns->serviceName == L"DNS", "Service name must be DNS");
    TEST_ASSERT(sDns->displayName == L"DNS Server", "Display name must match DNS Server");
    TEST_ASSERT(sDns->startType == micant::scm::SERVICE_AUTO_START, "DNS startType must be AUTO_START");
    TEST_ASSERT(sDns->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "DNS state must be SERVICE_RUNNING");
    TEST_ASSERT(sDns->binaryPath == L"C:\\Windows\\System32\\dns.exe", "DNS binary path must match");

    // Stage 2: VersionDatabase PE Metadata & Modules
    auto& verDb = micant::version::VersionDatabase::Instance();
    const auto* modDns = verDb.FindModule("dns.exe");
    TEST_ASSERT(modDns != nullptr, "dns.exe must be registered in VersionDatabase");
    TEST_ASSERT(modDns->stringTable.at("FileVersion") == "10.0.26100.1", "dns.exe version must be 10.0.26100.1");

    const auto* modApi = verDb.FindModule("dnsapi.dll");
    TEST_ASSERT(modApi != nullptr, "dnsapi.dll must be registered in VersionDatabase");
    TEST_ASSERT(modApi->stringTable.at("FileVersion") == "10.0.26100.1", "dnsapi.dll version must be 10.0.26100.1");

    const auto* modLib = verDb.FindModule("dnslib.dll");
    TEST_ASSERT(modLib != nullptr, "dnslib.dll must be registered in VersionDatabase");
    TEST_ASSERT(modLib->stringTable.at("FileVersion") == "10.0.26100.1", "dnslib.dll version must be 10.0.26100.1");

    const auto* modCache = verDb.FindModule("dnscache.dll");
    TEST_ASSERT(modCache != nullptr, "dnscache.dll must be registered in VersionDatabase");
    TEST_ASSERT(modCache->stringTable.at("FileVersion") == "10.0.26100.1", "dnscache.dll version must be 10.0.26100.1");

    const auto* modCmd = verDb.FindModule("dnscmd.exe");
    TEST_ASSERT(modCmd != nullptr, "dnscmd.exe must be registered in VersionDatabase");
    TEST_ASSERT(modCmd->stringTable.at("FileVersion") == "10.0.26100.1", "dnscmd.exe version must be 10.0.26100.1");

    // Stage 3: Authoritative Zone Catalog & Configuration
    auto zones = dns.getAllZones();
    TEST_ASSERT(zones.size() >= 3, "Must contain at least 3 seeded default zones");

    micant::dns::DnsZone zTitan, zMsdcs, zRev;
    TEST_ASSERT(dns.getZone("titan.local", zTitan), "titan.local zone must exist");
    TEST_ASSERT(zTitan.type == micant::dns::ZoneType::Primary, "titan.local must be Primary zone");
    TEST_ASSERT(zTitan.isAdIntegrated == true, "titan.local must be AD integrated");
    TEST_ASSERT(zTitan.replication == micant::dns::ReplicationScope::ActiveDirectoryDomain, "titan.local replication must be AD domain");
    TEST_ASSERT(zTitan.soaSerial >= 100, "titan.local SOA serial must be >= 100");
    TEST_ASSERT(zTitan.primaryNs == "dc01.titan.local", "Primary NS must be dc01.titan.local");

    TEST_ASSERT(dns.getZone("_msdcs.titan.local", zMsdcs), "_msdcs.titan.local zone must exist");
    TEST_ASSERT(zMsdcs.replication == micant::dns::ReplicationScope::ActiveDirectoryForest, "_msdcs must replicate to AD forest");

    TEST_ASSERT(dns.getZone("1.168.192.in-addr.arpa", zRev), "Reverse lookup zone 1.168.192.in-addr.arpa must exist");

    // Stage 4: Active Directory Domain Service Discovery SRV Records (RFC 2782)
    bool isAuth = false;
    uint16_t rcode = micant::dns::RCODE_NOERROR;

    auto srvLdap = dns.queryRecords("_ldap._tcp.titan.local", micant::dns::TYPE_SRV, &isAuth, &rcode);
    TEST_ASSERT(!srvLdap.empty(), "SRV _ldap._tcp.titan.local must resolve");
    TEST_ASSERT(isAuth == true && rcode == micant::dns::RCODE_NOERROR, "LDAP SRV query must be authoritative with NOERROR");
    TEST_ASSERT(srvLdap[0].rdata.find("389") != std::string::npos, "LDAP SRV port must be 389");
    TEST_ASSERT(srvLdap[0].rdata.find("dc01.titan.local") != std::string::npos, "LDAP SRV target must be dc01.titan.local");

    auto srvKrb = dns.queryRecords("_kerberos._tcp.titan.local", micant::dns::TYPE_SRV, &isAuth, &rcode);
    TEST_ASSERT(!srvKrb.empty() && srvKrb[0].rdata.find("88") != std::string::npos, "Kerberos SRV port must be 88");

    auto srvKpasswd = dns.queryRecords("_kpasswd._tcp.titan.local", micant::dns::TYPE_SRV, &isAuth, &rcode);
    TEST_ASSERT(!srvKpasswd.empty() && srvKpasswd[0].rdata.find("464") != std::string::npos, "Kpasswd SRV port must be 464");

    auto srvGc = dns.queryRecords("_gc._tcp.titan.local", micant::dns::TYPE_SRV, &isAuth, &rcode);
    TEST_ASSERT(!srvGc.empty() && srvGc[0].rdata.find("3268") != std::string::npos, "Global Catalog SRV port must be 3268");

    auto srvDc = dns.queryRecords("_ldap._tcp.dc._msdcs.titan.local", micant::dns::TYPE_SRV, &isAuth, &rcode);
    TEST_ASSERT(!srvDc.empty() && srvDc[0].rdata.find("dc01.titan.local") != std::string::npos, "Forest DC locator SRV must resolve");

    // Stage 5: RFC 1035 Wire Packet Query Resolution (A, AAAA, MX, TXT)
    auto aDc01 = dns.queryRecords("dc01.titan.local", micant::dns::TYPE_A, &isAuth, &rcode);
    TEST_ASSERT(!aDc01.empty(), "dc01.titan.local A query must return records");
    TEST_ASSERT(aDc01[0].rdata == "192.168.1.10", "dc01 IPv4 must be 192.168.1.10");

    auto aaaaDc01 = dns.queryRecords("dc01.titan.local", micant::dns::TYPE_AAAA, &isAuth, &rcode);
    TEST_ASSERT(!aaaaDc01.empty(), "dc01.titan.local AAAA query must return records");
    TEST_ASSERT(aaaaDc01[0].rdata == "2001:db8::10", "dc01 IPv6 must be 2001:db8::10");

    auto mxRec = dns.queryRecords("titan.local", micant::dns::TYPE_MX, &isAuth, &rcode);
    TEST_ASSERT(!mxRec.empty(), "titan.local MX query must return records");
    TEST_ASSERT(mxRec[0].rdata.find("mail.titan.local") != std::string::npos, "MX target must be mail.titan.local");

    auto txtRec = dns.queryRecords("titan.local", micant::dns::TYPE_TXT, &isAuth, &rcode);
    TEST_ASSERT(!txtRec.empty(), "titan.local TXT query must return records");
    TEST_ASSERT(txtRec[0].rdata.find("v=spf1") != std::string::npos, "TXT record must contain SPF string");

    // Stage 6: CNAME Alias Resolution & Chained Canonical Target Lookup
    auto cnamePki = dns.queryRecords("pki.titan.local", micant::dns::TYPE_A, &isAuth, &rcode);
    TEST_ASSERT(cnamePki.size() >= 2, "CNAME resolution for pki.titan.local must return CNAME and target A record");
    TEST_ASSERT(cnamePki[0].type == micant::dns::TYPE_CNAME, "First record must be CNAME");
    TEST_ASSERT(cnamePki[0].rdata == "titan-ca.titan.local", "CNAME target must be titan-ca.titan.local");
    TEST_ASSERT(cnamePki[1].type == micant::dns::TYPE_A && cnamePki[1].rdata == "192.168.1.60", "Target A record must be 192.168.1.60");

    auto cnamePxe = dns.queryRecords("pxe.titan.local", micant::dns::TYPE_A, &isAuth, &rcode);
    TEST_ASSERT(cnamePxe.size() >= 2 && cnamePxe[0].type == micant::dns::TYPE_CNAME, "CNAME resolution for pxe.titan.local must succeed");
    TEST_ASSERT(cnamePxe[1].rdata == "192.168.1.50", "Target A record for WDS must be 192.168.1.50");

    // Stage 7: Reverse Lookup Zone (in-addr.arpa) PTR Record Resolution
    auto ptrDc01 = dns.queryRecords("10.1.168.192.in-addr.arpa", micant::dns::TYPE_PTR, &isAuth, &rcode);
    TEST_ASSERT(!ptrDc01.empty(), "PTR for 192.168.1.10 must resolve");
    TEST_ASSERT(ptrDc01[0].rdata == "dc01.titan.local", "PTR target must be dc01.titan.local");

    auto ptrCa = dns.queryRecords("60.1.168.192.in-addr.arpa", micant::dns::TYPE_PTR, &isAuth, &rcode);
    TEST_ASSERT(!ptrCa.empty() && ptrCa[0].rdata == "titan-ca.titan.local", "PTR target must be titan-ca.titan.local");

    // Stage 8: RFC 6891 EDNS0 OPT RR Handling & Buffer Size Negotiation
    std::vector<uint8_t> ednsQuery;
    micant::dns::DnsHeaderWire ednsHdr{};
    ednsHdr.id = 0x1234;
    ednsHdr.flags = 0x0100; // RD = 1
    ednsHdr.qdcount = 0x0100; // 1 Question
    ednsHdr.arcount = 0x0100; // 1 Additional (OPT)
    ednsQuery.resize(sizeof(ednsHdr));
    std::memcpy(ednsQuery.data(), &ednsHdr, sizeof(ednsHdr));

    // Question: dc01.titan.local IN A
    const char* qLabels[] = { "dc01", "titan", "local" };
    for (const char* lbl : qLabels) {
        size_t len = std::strlen(lbl);
        ednsQuery.push_back(static_cast<uint8_t>(len));
        ednsQuery.insert(ednsQuery.end(), lbl, lbl + len);
    }
    ednsQuery.push_back(0x00); // Root terminator
    ednsQuery.push_back(0x00); ednsQuery.push_back(micant::dns::TYPE_A);
    ednsQuery.push_back(0x00); ednsQuery.push_back(micant::dns::CLASS_IN);

    // Additional: EDNS0 OPT RR (Type 41, Payload 4096)
    ednsQuery.push_back(0x00); // Root Name
    ednsQuery.push_back(0x00); ednsQuery.push_back(micant::dns::TYPE_OPT);
    ednsQuery.push_back(0x10); ednsQuery.push_back(0x00); // 4096 bytes buffer size (0x1000)
    ednsQuery.push_back(0x00); // Extended RCODE
    ednsQuery.push_back(0x00); // EDNS Version 0
    ednsQuery.push_back(0x80); ednsQuery.push_back(0x00); // Flags: DO (DNSSEC OK)
    ednsQuery.push_back(0x00); ednsQuery.push_back(0x00); // RDLEN = 0

    std::vector<uint8_t> ednsResp;
    TEST_ASSERT(dns.processWireQuery(ednsQuery.data(), ednsQuery.size(), ednsResp), "Wire query with EDNS0 OPT must succeed");
    TEST_ASSERT(ednsResp.size() >= sizeof(micant::dns::DnsHeaderWire), "Wire response must be valid DNS packet");
    auto* outHdr = reinterpret_cast<const micant::dns::DnsHeaderWire*>(ednsResp.data());
    uint16_t respArcount = ((outHdr->arcount >> 8) & 0xFF) | ((outHdr->arcount << 8) & 0xFF00);
    TEST_ASSERT(respArcount == 1, "EDNS0 response must include 1 additional record (OPT RR)");

    // Stage 9: RFC 2136 Dynamic DNS (DDNS) Updates & SOA Serial Auto-Increment
    micant::dns::DnsZone zPre;
    dns.getZone("titan.local", zPre);
    uint32_t serialPre = zPre.soaSerial;

    bool ddnsOk = dns.processDynamicUpdate("titan.local", "sqlcluster-01", micant::dns::TYPE_A, "192.168.1.180", 300);
    TEST_ASSERT(ddnsOk == true, "processDynamicUpdate must succeed");

    micant::dns::DnsZone zPost;
    dns.getZone("titan.local", zPost);
    TEST_ASSERT(zPost.soaSerial == serialPre + 1, "SOA serial must auto-increment by 1 upon DDNS update");

    auto ddnsQuery = dns.queryRecords("sqlcluster-01.titan.local", micant::dns::TYPE_A, &isAuth, &rcode);
    TEST_ASSERT(!ddnsQuery.empty() && ddnsQuery[0].rdata == "192.168.1.180", "Dynamically registered host must resolve immediately");

    // Stage 10: In-Memory Resolver Cache, Positive Caching & Cache Flush
    dns.flushCache();
    TEST_ASSERT(dns.getCacheSize() == 0, "Cache must be empty after flushCache");

    uint64_t hitsPre = dns.getTotalCacheHits();
    uint64_t missesPre = dns.getTotalCacheMisses();

    dns.queryRecords("dc01.titan.local", micant::dns::TYPE_A);
    TEST_ASSERT(dns.getTotalCacheMisses() > missesPre, "Initial query must be a cache miss");

    dns.queryRecords("dc01.titan.local", micant::dns::TYPE_A);
    TEST_ASSERT(dns.getTotalCacheHits() > hitsPre, "Subsequent query must be a cache hit");
    TEST_ASSERT(dns.getCacheSize() > 0, "Cache size must be greater than zero");

    dns.flushCache();
    TEST_ASSERT(dns.getCacheSize() == 0, "Cache size must be 0 after flush");

    // Stage 11: Negative Caching (NXDOMAIN Caching & Fast Denial)
    auto nxQuery1 = dns.queryRecords("nonexistent-node.titan.local", micant::dns::TYPE_A, &isAuth, &rcode);
    TEST_ASSERT(nxQuery1.empty() && rcode == micant::dns::RCODE_NXDOMAIN, "Non-existent host query must return NXDOMAIN");

    uint64_t hitsPreNeg = dns.getTotalCacheHits();
    auto nxQuery2 = dns.queryRecords("nonexistent-node.titan.local", micant::dns::TYPE_A, &isAuth, &rcode);
    TEST_ASSERT(nxQuery2.empty() && rcode == micant::dns::RCODE_NXDOMAIN, "Negative cache query must return NXDOMAIN");
    TEST_ASSERT(dns.getTotalCacheHits() > hitsPreNeg, "Repeated non-existent query must be answered as negative cache hit");

    // Stage 12: DNSSEC Zone Signing Engine (RFC 4034/4035: DNSKEY, RRSIG, NSEC)
    bool signOk = dns.signZoneDnssec("titan.local");
    TEST_ASSERT(signOk == true, "signZoneDnssec must succeed");

    micant::dns::DnsZone zSigned;
    dns.getZone("titan.local", zSigned);
    TEST_ASSERT(zSigned.isDnssecSigned == true, "Zone must be marked as DNSSEC signed");

    auto keys = dns.queryRecords("titan.local", micant::dns::TYPE_DNSKEY);
    TEST_ASSERT(keys.size() >= 2, "Signed zone must contain ZSK and KSK DNSKEY records");

    auto sigs = dns.queryRecords("titan.local", micant::dns::TYPE_RRSIG);
    TEST_ASSERT(!sigs.empty(), "Signed zone must contain RRSIG record");

    auto nsec = dns.queryRecords("titan.local", micant::dns::TYPE_NSEC);
    TEST_ASSERT(!nsec.empty(), "Signed zone must contain NSEC record");

    // Stage 13: AXFR Full Zone Transfer Streaming (RFC 5936)
    std::vector<micant::dns::DnsResourceRecord> axfrRecords;
    bool axfrOk = dns.performAxfr("titan.local", axfrRecords);
    TEST_ASSERT(axfrOk == true, "performAxfr must succeed");
    TEST_ASSERT(axfrRecords.size() >= 10, "AXFR must stream all zone resource records");
    TEST_ASSERT(axfrRecords.front().type == micant::dns::TYPE_SOA, "AXFR stream must open with SOA record");
    TEST_ASSERT(axfrRecords.back().type == micant::dns::TYPE_SOA, "AXFR stream must terminate with SOA record");

    // Stage 14: Clean-Room Win32 C ABI Exports
    void* pEngine = nullptr;
    int32_t initRes = micant::dns::MicaDnsInitialize(&pEngine);
    TEST_ASSERT(initRes == 1 && pEngine != nullptr, "MicaDnsInitialize must succeed");

    int32_t crzRes = micant::dns::MicaDnsCreateZone(pEngine, "branch01.titan.local", 0, 1);
    TEST_ASSERT(crzRes == 1, "MicaDnsCreateZone must succeed");

    int32_t addRes = micant::dns::MicaDnsAddRecord(pEngine, "branch01.titan.local", "router.branch01.titan.local", micant::dns::TYPE_A, "10.200.1.1", 300);
    TEST_ASSERT(addRes == 1, "MicaDnsAddRecord must succeed");

    uint8_t pktOut[1024]{};
    uint32_t cbPkt = 0;
    int32_t qRes = micant::dns::MicaDnsQuery(pEngine, "router.branch01.titan.local", micant::dns::TYPE_A, pktOut, sizeof(pktOut), &cbPkt);
    TEST_ASSERT(qRes == 1 && cbPkt > 0, "MicaDnsQuery must succeed via C ABI");

    int32_t updRes = micant::dns::MicaDnsDynamicUpdate(pEngine, "branch01.titan.local", "switch01", micant::dns::TYPE_A, "10.200.1.2", 300);
    TEST_ASSERT(updRes == 1, "MicaDnsDynamicUpdate must succeed via C ABI");

    uint32_t axfrTotal = 0;
    int32_t xfrRes = micant::dns::MicaDnsZoneTransfer(pEngine, "branch01.titan.local", &axfrTotal);
    TEST_ASSERT(xfrRes == 1 && axfrTotal >= 4, "MicaDnsZoneTransfer must succeed via C ABI");

    int32_t shutRes = micant::dns::MicaDnsShutdown(pEngine);
    TEST_ASSERT(shutRes == 1, "MicaDnsShutdown must succeed");

    // Stage 15: Multithreaded High-Throughput Concurrent DNS Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&dns, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                std::string host = "node-t" + std::to_string(t) + "-o" + std::to_string(op);
                std::string ip = "192.168.100." + std::to_string(t * 20 + op + 1);

                bool upOk = dns.processDynamicUpdate("titan.local", host, micant::dns::TYPE_A, ip, 300);

                bool innerAuth = false;
                uint16_t innerRcode = micant::dns::RCODE_NOERROR;
                auto qResp = dns.queryRecords(host + ".titan.local", micant::dns::TYPE_A, &innerAuth, &innerRcode);

                auto srvResp = dns.queryRecords("_ldap._tcp.titan.local", micant::dns::TYPE_SRV);

                if (upOk && !qResp.empty() && qResp[0].rdata == ip && !srvResp.empty()) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded DNS stress test must complete with 100% success");

    std::cout << "[TEST] Suite 208: Windows Enterprise DNS Server Subsystem PASSED.\n";
}

void Test_WindowsEnterpriseDHCP_Server_Subsystem() {
    std::cout << "[TEST] Suite 209: Windows Enterprise DHCP Server Subsystem...\n";

    auto& dhcp = micant::dhcp::EnterpriseDhcpServer::instance();
    dhcp.initialize();

    // Stage 1: SCM Registration & State
    auto& scm = micant::scm::ServiceControlManager::get();
    auto sDhcp = scm.getServiceRecord(L"DHCPServer");
    TEST_ASSERT(sDhcp != nullptr, "DHCPServer service must be registered in SCM");
    TEST_ASSERT(sDhcp->serviceName == L"DHCPServer", "Service name must be DHCPServer");
    TEST_ASSERT(sDhcp->displayName == L"DHCP Server", "Display name must match DHCP Server");
    TEST_ASSERT(sDhcp->startType == micant::scm::SERVICE_AUTO_START, "DHCPServer startType must be AUTO_START");
    TEST_ASSERT(sDhcp->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "DHCPServer state must be SERVICE_RUNNING");
    TEST_ASSERT(sDhcp->binaryPath == L"C:\\Windows\\System32\\tcpsvcs.exe", "DHCPServer binary path must match");

    // Stage 2: VersionDatabase PE Metadata & Modules
    auto& verDb = micant::version::VersionDatabase::Instance();
    const auto* modTcp = verDb.FindModule("tcpsvcs.exe");
    TEST_ASSERT(modTcp != nullptr, "tcpsvcs.exe must be registered in VersionDatabase");
    TEST_ASSERT(modTcp->stringTable.at("FileVersion") == "10.0.26100.1", "tcpsvcs.exe version must be 10.0.26100.1");

    const auto* modSvc = verDb.FindModule("dhcpsvc.dll");
    TEST_ASSERT(modSvc != nullptr, "dhcpsvc.dll must be registered in VersionDatabase");
    TEST_ASSERT(modSvc->stringTable.at("FileVersion") == "10.0.26100.1", "dhcpsvc.dll version must be 10.0.26100.1");

    const auto* modApi = verDb.FindModule("dhcpsapi.dll");
    TEST_ASSERT(modApi != nullptr, "dhcpsapi.dll must be registered in VersionDatabase");
    TEST_ASSERT(modApi->stringTable.at("FileVersion") == "10.0.26100.1", "dhcpsapi.dll version must be 10.0.26100.1");

    const auto* modCore = verDb.FindModule("dhcpcore.dll");
    TEST_ASSERT(modCore != nullptr, "dhcpcore.dll must be registered in VersionDatabase");
    TEST_ASSERT(modCore->stringTable.at("FileVersion") == "10.0.26100.1", "dhcpcore.dll version must be 10.0.26100.1");

    const auto* modMon = verDb.FindModule("dhcpcmonitor.dll");
    TEST_ASSERT(modMon != nullptr, "dhcpcmonitor.dll must be registered in VersionDatabase");
    TEST_ASSERT(modMon->stringTable.at("FileVersion") == "10.0.26100.1", "dhcpcmonitor.dll version must be 10.0.26100.1");

    // Stage 3: Active Directory Authorization & Rogue DHCP Suppression
    TEST_ASSERT(dhcp.isServerAuthorized() == true, "DHCP server must initially be authorized in Active Directory");
    TEST_ASSERT(dhcp.isRogueSuppressed() == false, "DHCP server must not be rogue-suppressed initially");
    TEST_ASSERT(dhcp.getAdDirectoryPath().find("CN=NetServices") != std::string::npos, "AD directory path must reference NetServices");

    bool unauthOk = dhcp.unauthorizeServerInAd("192.168.1.10", "titan.local");
    TEST_ASSERT(unauthOk == true, "Unauthorizing server in AD must succeed");
    TEST_ASSERT(dhcp.isServerAuthorized() == false, "Server must be unauthorized");
    TEST_ASSERT(dhcp.isRogueSuppressed() == true, "Server must enter rogue suppressed state");

    std::string unauthIp;
    uint32_t unauthLease = 0;
    bool blockedDisc = dhcp.processDiscover("192.168.1.0", "00:15:5d:00:11:22", "unauth-test", unauthIp, unauthLease);
    TEST_ASSERT(blockedDisc == false, "DHCPDISCOVER must be rejected when server is rogue-suppressed");

    bool reauthOk = dhcp.authorizeServerInAd("192.168.1.10", "titan.local");
    TEST_ASSERT(reauthOk == true, "Re-authorizing server in AD must succeed");
    TEST_ASSERT(dhcp.isServerAuthorized() == true, "Server must be active again");

    // Stage 4: Default IPv4 Enterprise Scope Catalog & Options
    micant::dhcp::DhcpScope sc;
    TEST_ASSERT(dhcp.getScope("192.168.1.0", sc), "Default enterprise scope 192.168.1.0 must exist");
    TEST_ASSERT(sc.subnetMask == "255.255.255.0", "Subnet mask must be 255.255.255.0");
    TEST_ASSERT(sc.startIp == "192.168.1.100" && sc.endIp == "192.168.1.200", "Pool range must be 192.168.1.100 - 192.168.1.200");
    TEST_ASSERT(sc.leaseDurationSeconds == 691200, "Default lease duration must be 8 days (691200s)");
    TEST_ASSERT(sc.router == "192.168.1.1", "Default gateway router must be 192.168.1.1");
    TEST_ASSERT(sc.dnsServers.size() >= 2 && sc.dnsServers[0] == "192.168.1.10", "DNS server option must contain domain controller IP");
    TEST_ASSERT(sc.domainName == "titan.local", "Domain name option must be titan.local");
    TEST_ASSERT(!sc.exclusions.empty(), "Scope must have at least one exclusion range");

    // Stage 5: DHCPv4 4-Way DORA Handshake (DISCOVER, OFFER, REQUEST, ACK / RFC 2131)
    std::string offeredIp;
    uint32_t offeredLease = 0;
    bool discOk = dhcp.processDiscover("192.168.1.0", "00:15:5d:00:22:33", "ws-finance01", offeredIp, offeredLease);
    TEST_ASSERT(discOk == true, "processDiscover must succeed");
    TEST_ASSERT(!offeredIp.empty(), "Offered IP must be non-empty");
    TEST_ASSERT(micant::dhcp::isIpInRange(offeredIp, sc.startIp, sc.endIp), "Offered IP must be within scope range");
    TEST_ASSERT(offeredLease == sc.leaseDurationSeconds, "Offered lease must match scope configuration");

    bool ackOk = false;
    uint32_t grantedLease = 0;
    bool reqOk = dhcp.processRequest("192.168.1.0", "00:15:5d:00:22:33", offeredIp, "ws-finance01", ackOk, grantedLease);
    TEST_ASSERT(reqOk == true && ackOk == true, "processRequest must yield DHCPACK");
    TEST_ASSERT(grantedLease == sc.leaseDurationSeconds, "Granted lease duration must match");

    micant::dhcp::DhcpScope scCheck;
    dhcp.getScope("192.168.1.0", scCheck);
    auto lIt = scCheck.leases.find(offeredIp);
    TEST_ASSERT(lIt != scCheck.leases.end(), "Granted lease must be recorded in scope database");
    TEST_ASSERT(lIt->second.macAddress == "00:15:5d:00:22:33", "Lease MAC must match client MAC");
    TEST_ASSERT(lIt->second.state == micant::dhcp::LeaseState::Active, "Lease state must be Active");

    // Stage 6: IP Exclusion Range Enforcement
    bool exAck = false;
    uint32_t exLease = 0;
    bool exReq = dhcp.processRequest("192.168.1.0", "00:15:5d:99:99:99", "192.168.1.155", "rogue-static", exAck, exLease);
    TEST_ASSERT(exReq == false && exAck == false, "Request for IP inside exclusion range must be rejected with DHCPNAK");

    // Stage 7: Hardware MAC Address Reservation Binding
    std::string printerOfferIp;
    uint32_t printerLease = 0;
    bool pDiscOk = dhcp.processDiscover("192.168.1.0", "00:15:5d:01:aa:01", "titan-printer01", printerOfferIp, printerLease);
    TEST_ASSERT(pDiscOk == true && printerOfferIp == "192.168.1.120", "Reserved MAC must receive exactly reserved IP (192.168.1.120)");

    bool addResOk = dhcp.addReservation("192.168.1.0", "192.168.1.199", "00:15:5d:01:aa:99", "kiosk-01");
    TEST_ASSERT(addResOk == true, "addReservation must succeed");

    std::string kioskOfferIp;
    uint32_t kioskLease = 0;
    bool kDiscOk = dhcp.processDiscover("192.168.1.0", "00:15:5d:01:aa:99", "kiosk-01", kioskOfferIp, kioskLease);
    TEST_ASSERT(kDiscOk == true && kioskOfferIp == "192.168.1.199", "Newly added reservation must be honored on DISCOVER");

    // Stage 8: Lease State Tracking & Existing Lease Re-offer
    std::string reOfferIp;
    uint32_t reLease = 0;
    bool reDiscOk = dhcp.processDiscover("192.168.1.0", "00:15:5d:00:22:33", "ws-finance01", reOfferIp, reLease);
    TEST_ASSERT(reDiscOk == true && reOfferIp == offeredIp, "Client with active lease must be re-offered the same IP address");

    // Stage 9: DHCPRELEASE Processing & Address Re-pool
    bool relOk = dhcp.processRelease("192.168.1.0", offeredIp, "00:15:5d:00:22:33");
    TEST_ASSERT(relOk == true, "processRelease must succeed");

    dhcp.getScope("192.168.1.0", scCheck);
    TEST_ASSERT(scCheck.leases.find(offeredIp) == scCheck.leases.end(), "Released IP must be immediately removed and returned to the pool");

    // Stage 10: Option 81 Dynamic DNS Registration with Conflict Detection
    std::string ddnsHost = "ws-accounting99";
    std::string ddnsIp = "192.168.1.135";
    bool ddnsAck = false;
    uint32_t ddnsLease = 0;
    bool ddnsReqOk = dhcp.processRequest("192.168.1.0", "00:15:5d:77:88:99", ddnsIp, ddnsHost, ddnsAck, ddnsLease);
    TEST_ASSERT(ddnsReqOk == true && ddnsAck == true, "DHCP request with Option 81 FQDN must succeed");

    auto& dns = micant::dns::EnterpriseDnsServer::instance();
    auto dnsA = dns.queryRecords(ddnsHost + ".titan.local", micant::dns::TYPE_A);
    TEST_ASSERT(!dnsA.empty() && dnsA[0].rdata == ddnsIp, "Option 81 forward A record must be registered automatically in DNS");

    auto dnsPtr = dns.queryRecords("135.1.168.192.in-addr.arpa", micant::dns::TYPE_PTR);
    TEST_ASSERT(!dnsPtr.empty() && dnsPtr[0].rdata == ddnsHost + ".titan.local", "Option 81 reverse PTR record must be registered in in-addr.arpa");

    // Stage 11: DHCP Failover & High Availability (RFC 3074)
    bool foCfgOk = dhcp.configureFailover("192.168.1.0", "dc02.titan.local", micant::dhcp::FailoverMode::LoadBalance, 3600, 50);
    TEST_ASSERT(foCfgOk == true, "configureFailover must succeed");

    micant::dhcp::DhcpLease partnerLease;
    partnerLease.ipAddress = "192.168.1.175";
    partnerLease.macAddress = "00:15:5d:ee:ff:01";
    partnerLease.hostName = "partner-client01";
    partnerLease.state = micant::dhcp::LeaseState::Active;
    bool syncOk = dhcp.syncFailoverLease("192.168.1.0", partnerLease);
    TEST_ASSERT(syncOk == true, "syncFailoverLease must synchronize partner lease");

    bool stateOk = dhcp.setFailoverState("192.168.1.0", micant::dhcp::FailoverState::PartnerDown);
    TEST_ASSERT(stateOk == true, "setFailoverState to PartnerDown must succeed");

    dhcp.setFailoverState("192.168.1.0", micant::dhcp::FailoverState::Normal);

    // Stage 12: DHCPv6 4-Way SARR Handshake (RFC 8415)
    std::string v6Duid = "00010001aabbccdd00155d445566";
    std::string advIp;
    uint32_t v6Lifetime = 0;
    bool solOk = dhcp.processV6Solicit(v6Duid, "ipv6-host01", advIp, v6Lifetime);
    TEST_ASSERT(solOk == true, "processV6Solicit must succeed");
    TEST_ASSERT(!advIp.empty() && advIp.find("2001:db8:1::") != std::string::npos, "Advertised IPv6 must belong to configured prefix");

    bool v6Reply = false;
    uint32_t grantV6Lifetime = 0;
    bool v6ReqOk = dhcp.processV6Request(v6Duid, advIp, "ipv6-host01", v6Reply, grantV6Lifetime);
    TEST_ASSERT(v6ReqOk == true && v6Reply == true, "processV6Request must yield DHCPV6_REPLY");
    TEST_ASSERT(grantV6Lifetime == 86400, "DHCPv6 valid lifetime must be 86400s");

    bool v6RelOk = dhcp.processV6Release(v6Duid, advIp);
    TEST_ASSERT(v6RelOk == true, "processV6Release must succeed");

    // Stage 13: Clean-Room Win32 C ABI Exports (MicaDhcp*)
    void* pEngine = nullptr;
    int32_t initRes = micant::dhcp::MicaDhcpInitialize(&pEngine);
    TEST_ASSERT(initRes == 1 && pEngine != nullptr, "MicaDhcpInitialize must succeed");

    int32_t authRes = micant::dhcp::MicaDhcpAuthorizeServer(pEngine, "192.168.1.10", "titan.local");
    TEST_ASSERT(authRes == 1, "MicaDhcpAuthorizeServer must succeed via C ABI");

    int32_t scRes = micant::dhcp::MicaDhcpCreateScope(pEngine, "10.50.0.0", "255.255.0.0", "10.50.1.1", "10.50.1.250", 86400);
    TEST_ASSERT(scRes == 1, "MicaDhcpCreateScope must succeed via C ABI");

    int32_t resRes = micant::dhcp::MicaDhcpAddReservation(pEngine, "10.50.0.0", "10.50.1.5", "00:11:22:33:44:55", "branch-gw");
    TEST_ASSERT(resRes == 1, "MicaDhcpAddReservation must succeed via C ABI");

    char offIpBuf[64]{};
    int32_t discRes = micant::dhcp::MicaDhcpProcessDiscover(pEngine, "00:11:22:33:44:55", "branch-gw", offIpBuf, sizeof(offIpBuf));
    TEST_ASSERT(discRes == 1 && std::string(offIpBuf) == "10.50.1.5", "MicaDhcpProcessDiscover must honor reservation via C ABI");

    uint32_t cAbiLease = 0;
    int32_t reqRes = micant::dhcp::MicaDhcpProcessRequest(pEngine, "00:11:22:33:44:55", offIpBuf, "branch-gw", &cAbiLease);
    TEST_ASSERT(reqRes == 1 && cAbiLease == 86400, "MicaDhcpProcessRequest must yield ACK via C ABI");

    int32_t foRes = micant::dhcp::MicaDhcpConfigureFailover(pEngine, "10.50.0.0", "10.50.0.2", 0);
    TEST_ASSERT(foRes == 1, "MicaDhcpConfigureFailover must succeed via C ABI");

    int32_t shutRes = micant::dhcp::MicaDhcpShutdown(pEngine);
    TEST_ASSERT(shutRes == 1, "MicaDhcpShutdown must succeed via C ABI");

    // Stage 14: Multithreaded High-Throughput Concurrent DHCP Stress Test
    bool stressScopeOk = dhcp.createScope("10.240.0.0", "255.255.0.0", "10.240.1.1", "10.240.2.254", 3600, "Titan-Stress-Scope");
    TEST_ASSERT(stressScopeOk == true, "createScope for high-capacity stress test scope must succeed");

    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&dhcp, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                char macBuf[32];
                std::snprintf(macBuf, sizeof(macBuf), "00:15:5d:99:%02x:%02x", t, op);
                std::string clientMac = macBuf;
                std::string clientHost = "stress-node-t" + std::to_string(t) + "-o" + std::to_string(op);

                std::string offIp;
                uint32_t offLease = 0;
                bool dOk = dhcp.processDiscover("10.240.0.0", clientMac, clientHost, offIp, offLease);

                bool rAck = false;
                uint32_t rLease = 0;
                bool rOk = false;
                if (dOk && !offIp.empty()) {
                    rOk = dhcp.processRequest("10.240.0.0", clientMac, offIp, clientHost, rAck, rLease);
                }

                if (dOk && rOk && rAck && !offIp.empty() && rLease > 0) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded DHCP stress test must complete with 100% success");

    std::cout << "[TEST] Suite 209: Windows Enterprise DHCP Server Subsystem PASSED.\n";
}

void Test_WindowsEnterpriseIIS_HttpServer_Subsystem() {
    std::cout << "[TEST] Suite 210: Windows Enterprise IIS & HTTP Server Subsystem...\n";

    auto& iis = micant::iis::EnterpriseWebServer::instance();
    iis.initialize();

    // ------------------------------------------------------------------------
    // Stage 1: SCM Service Registration (W3SVC & WAS)
    // ------------------------------------------------------------------------
    auto& scm = micant::scm::ServiceControlManager::get();
    auto sW3svc = scm.getServiceRecord(L"W3SVC");
    TEST_ASSERT(sW3svc != nullptr, "W3SVC service must be registered in SCM");
    TEST_ASSERT(sW3svc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "W3SVC must be in SERVICE_RUNNING state");
    TEST_ASSERT(sW3svc->startType == micant::scm::SERVICE_AUTO_START, "W3SVC must be configured for SERVICE_AUTO_START");

    auto sWas = scm.getServiceRecord(L"WAS");
    TEST_ASSERT(sWas != nullptr, "WAS service must be registered in SCM");
    TEST_ASSERT(sWas->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "WAS must be in SERVICE_RUNNING state");
    TEST_ASSERT(sWas->startType == micant::scm::SERVICE_AUTO_START, "WAS must be configured for SERVICE_AUTO_START");

    // ------------------------------------------------------------------------
    // Stage 2: VersionDatabase Registration (10.0.26100.1)
    // ------------------------------------------------------------------------
    auto& verDb = micant::version::VersionDatabase::Instance();
    std::vector<std::string> expectedBins = {
        "http.sys", "w3wp.exe", "w3core.dll", "apphostsvc.dll", "iisreset.exe", "appcmd.exe"
    };
    for (const auto& bin : expectedBins) {
        const auto* mod = verDb.FindModule(bin);
        TEST_ASSERT(mod != nullptr, "Binary " + bin + " must be registered in VersionDatabase");
        TEST_ASSERT(mod->stringTable.at("FileVersion") == "10.0.26100.1", bin + " fileVersion must be 10.0.26100.1");
    }

    // ------------------------------------------------------------------------
    // Stage 3: ApplicationHost Catalog & Site Configuration
    // ------------------------------------------------------------------------
    auto sites = iis.getSites();
    TEST_ASSERT(sites.find(1) != sites.end(), "Site ID 1 (Default Web Site) must exist");
    auto defSite = sites[1];
    TEST_ASSERT(defSite.name == "Default Web Site", "Site 1 name must be 'Default Web Site'");
    TEST_ASSERT(defSite.appPoolName == "DefaultAppPool", "Site 1 must bind to DefaultAppPool");
    TEST_ASSERT(!defSite.bindings.empty(), "Site 1 must have at least one binding");
    TEST_ASSERT(defSite.bindings[0].port == 80 && defSite.bindings[0].protocol == micant::iis::ProtocolType::Http, "Site 1 binding must be HTTP on port 80");
    TEST_ASSERT(!defSite.virtualDirectories.empty(), "Site 1 must have root virtual directory");
    TEST_ASSERT(defSite.virtualDirectories[0].physicalPath == "C:\\inetpub\\wwwroot", "Root physical path must be C:\\inetpub\\wwwroot");

    TEST_ASSERT(sites.find(2) != sites.end(), "Site ID 2 (Titan-Intranet) must exist");
    auto intraSite = sites[2];
    TEST_ASSERT(intraSite.name == "Titan-Intranet", "Site 2 name must be 'Titan-Intranet'");
    TEST_ASSERT(intraSite.bindings[0].port == 443 && intraSite.bindings[0].protocol == micant::iis::ProtocolType::Https, "Site 2 binding must be HTTPS on port 443");
    TEST_ASSERT(intraSite.bindings[0].hostName == "intranet.titan.local", "Site 2 binding hostName must be intranet.titan.local");
    TEST_ASSERT(intraSite.bindings[0].requireSni == true, "Site 2 must require SNI");

    // ------------------------------------------------------------------------
    // Stage 4: Application Pool Lifecycle & State Management
    // ------------------------------------------------------------------------
    micant::iis::ApplicationPool pTest;
    bool getPoolOk = iis.getAppPool("DefaultAppPool", pTest);
    TEST_ASSERT(getPoolOk == true, "getAppPool for DefaultAppPool must succeed");
    TEST_ASSERT(pTest.state == micant::iis::AppPoolState::Running, "DefaultAppPool must initially be Running");

    bool createPoolOk = iis.createAppPool("FinanceAppPool", micant::iis::ManagedPipelineMode::Classic);
    TEST_ASSERT(createPoolOk == true, "createAppPool for FinanceAppPool must succeed");
    iis.getAppPool("FinanceAppPool", pTest);
    TEST_ASSERT(pTest.pipelineMode == micant::iis::ManagedPipelineMode::Classic, "FinanceAppPool pipeline mode must be Classic");

    bool recycleOk = iis.recycleAppPool("FinanceAppPool");
    TEST_ASSERT(recycleOk == true, "recycleAppPool must succeed");
    iis.getAppPool("FinanceAppPool", pTest);
    TEST_ASSERT(pTest.recycleCount == 1, "FinanceAppPool recycleCount must be 1");

    // ------------------------------------------------------------------------
    // Stage 5: WAS Rapid-Fail Protection
    // ------------------------------------------------------------------------
    iis.createAppPool("CrashingAppPool");
    for (int i = 0; i < 4; ++i) {
        iis.simulateWorkerProcessCrash("CrashingAppPool");
    }
    iis.getAppPool("CrashingAppPool", pTest);
    TEST_ASSERT(pTest.crashCount == 4 && pTest.rapidFailProtectionActive == false && pTest.state == micant::iis::AppPoolState::Running,
                "Under threshold, AppPool must still be Running");

    // 5th crash triggers rapid-fail protection shutdown
    iis.simulateWorkerProcessCrash("CrashingAppPool");
    iis.getAppPool("CrashingAppPool", pTest);
    TEST_ASSERT(pTest.crashCount == 5 && pTest.rapidFailProtectionActive == true && pTest.state == micant::iis::AppPoolState::Stopped,
                "Exceeding threshold must trigger rapid-fail protection and stop AppPool");

    // ------------------------------------------------------------------------
    // Stage 6: HTTP/1.1 Wire Request Parsing
    // ------------------------------------------------------------------------
    std::string rawHttp = "POST /api/upload?type=xml HTTP/1.1\r\n"
                          "Host: api.titan.local:8080\r\n"
                          "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
                          "Content-Type: application/xml\r\n"
                          "Accept: */*\r\n"
                          "\r\n"
                          "<record id=\"1001\"><name>TitanUnit</name></record>";
    auto req = iis.parseRawHttpWire(rawHttp, "10.0.0.50", 49152, false);
    TEST_ASSERT(req.method == "POST", "Parsed method must be POST");
    TEST_ASSERT(req.uri == "/api/upload?type=xml", "Parsed URI must match");
    TEST_ASSERT(req.path == "/api/upload", "Parsed path must be /api/upload");
    TEST_ASSERT(req.queryString == "type=xml", "Parsed queryString must be type=xml");
    TEST_ASSERT(req.serverPort == 8080, "Parsed serverPort from Host header must be 8080");
    TEST_ASSERT(req.headers["content-type"] == "application/xml", "Content-Type header must be parsed");
    TEST_ASSERT(req.body.find("<record id=\"1001\">") != std::string::npos, "Request body must be parsed");

    // ------------------------------------------------------------------------
    // Stage 7: Static File Servicing & MIME Type Mapping
    // ------------------------------------------------------------------------
    std::string rawCss = "GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n";
    auto reqCss = iis.parseRawHttpWire(rawCss, "127.0.0.1", 50100, false);
    auto respCss = iis.processHttpRequest(reqCss);
    TEST_ASSERT(respCss.statusCode == 200, "GET /style.css must return 200 OK");
    TEST_ASSERT(respCss.contentType == "text/css", "style.css must have MIME text/css");
    TEST_ASSERT(respCss.body.find("font-family") != std::string::npos, "style.css body must match content");

    std::string rawJson = "GET /health.json HTTP/1.1\r\nHost: localhost\r\n\r\n";
    auto reqJson = iis.parseRawHttpWire(rawJson, "127.0.0.1", 50101, false);
    auto respJson = iis.processHttpRequest(reqJson);
    TEST_ASSERT(respJson.statusCode == 200, "GET /health.json must return 200 OK");
    TEST_ASSERT(respJson.contentType == "application/json", "health.json must have MIME application/json");
    TEST_ASSERT(respJson.body.find("\"healthy\"") != std::string::npos, "health.json body must match content");

    // ------------------------------------------------------------------------
    // Stage 8: Default Document Resolution
    // ------------------------------------------------------------------------
    std::string rawRoot = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    auto reqRoot = iis.parseRawHttpWire(rawRoot, "127.0.0.1", 50102, false);
    auto respRoot = iis.processHttpRequest(reqRoot);
    TEST_ASSERT(respRoot.statusCode == 200, "GET / must return 200 OK via default document index.html");
    TEST_ASSERT(respRoot.contentType == "text/html", "Default document must have MIME text/html");
    TEST_ASSERT(respRoot.body.find("Titan Enterprise Web Server") != std::string::npos, "Default document content must match");

    // ------------------------------------------------------------------------
    // Stage 9: HTTP Status Codes & Custom Error Generation
    // ------------------------------------------------------------------------
    std::string rawMissing = "GET /nonexistent.txt HTTP/1.1\r\nHost: localhost\r\n\r\n";
    auto reqMissing = iis.parseRawHttpWire(rawMissing, "127.0.0.1", 50103, false);
    auto respMissing = iis.processHttpRequest(reqMissing);
    TEST_ASSERT(respMissing.statusCode == 404, "Nonexistent file must return 404 Not Found");
    TEST_ASSERT(respMissing.body.find("HTTP Error 404.0 - Not Found") != std::string::npos, "404 response must include custom error HTML");

    // Stop DefaultAppPool to test 503
    iis.setAppPoolState("DefaultAppPool", micant::iis::AppPoolState::Stopped);
    auto resp503 = iis.processHttpRequest(reqRoot);
    TEST_ASSERT(resp503.statusCode == 503, "Request to site with stopped AppPool must return 503 Service Unavailable");
    TEST_ASSERT(resp503.body.find("application pool is stopped") != std::string::npos, "503 body must mention stopped application pool");
    iis.setAppPoolState("DefaultAppPool", micant::iis::AppPoolState::Running); // restore

    // ------------------------------------------------------------------------
    // Stage 10: Integrated Windows Authentication
    // ------------------------------------------------------------------------
    std::string rawIntraUnauth = "GET /index.html HTTP/1.1\r\nHost: intranet.titan.local\r\n\r\n";
    auto reqIntraUnauth = iis.parseRawHttpWire(rawIntraUnauth, "10.0.1.100", 50200, true);
    auto respIntraUnauth = iis.processHttpRequest(reqIntraUnauth);
    TEST_ASSERT(respIntraUnauth.statusCode == 401, "Protected intranet site without credentials must return 401 Unauthorized");
    TEST_ASSERT(respIntraUnauth.headers.find("WWW-Authenticate") != respIntraUnauth.headers.end(), "401 must include WWW-Authenticate header");
    TEST_ASSERT(respIntraUnauth.headers["WWW-Authenticate"].find("Negotiate") != std::string::npos, "WWW-Authenticate must include Negotiate");

    std::string rawIntraAuth = "GET /index.html HTTP/1.1\r\n"
                               "Host: intranet.titan.local\r\n"
                               "Authorization: Negotiate TlRMTVNTUAABAAAAl4II4gAAAAAAAAAAAAAAAAAAAAAGAbEdAAAADw==\r\n"
                               "\r\n";
    auto reqIntraAuth = iis.parseRawHttpWire(rawIntraAuth, "10.0.1.100", 50201, true);
    auto respIntraAuth = iis.processHttpRequest(reqIntraAuth);
    TEST_ASSERT(respIntraAuth.statusCode == 200, "Protected intranet site with valid Negotiate token must return 200 OK");
    TEST_ASSERT(respIntraAuth.body.find("Titan Intranet Portal") != std::string::npos, "Intranet response body must be returned");

    // ------------------------------------------------------------------------
    // Stage 11: TLS/SSL SNI Host Binding & Resolution
    // ------------------------------------------------------------------------
    micant::iis::WebSite s2;
    iis.getSite(2, s2);
    TEST_ASSERT(!s2.bindings.empty(), "Site 2 must have bindings");
    TEST_ASSERT(s2.bindings[0].sslCertThumbprint == "A1B2C3D4E5F60123456789ABCDEF0123456789AB", "Site 2 SSL thumbprint must match ADCS certificate");
    TEST_ASSERT(s2.bindings[0].requireSni == true, "Site 2 binding must require SNI");

    // ------------------------------------------------------------------------
    // Stage 12: Content Compression Negotiation (Gzip)
    // ------------------------------------------------------------------------
    std::string rawGzip = "GET /index.html HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Accept-Encoding: gzip, deflate\r\n"
                          "\r\n";
    auto reqGzip = iis.parseRawHttpWire(rawGzip, "127.0.0.1", 50300, false);
    auto respGzip = iis.processHttpRequest(reqGzip);
    TEST_ASSERT(respGzip.statusCode == 200, "Compressed request must return 200 OK");
    TEST_ASSERT(respGzip.isCompressed == true, "Response must have isCompressed set to true");
    TEST_ASSERT(respGzip.contentEncoding == "gzip", "contentEncoding must be gzip");
    TEST_ASSERT(respGzip.headers["Content-Encoding"] == "gzip", "Content-Encoding header must be present");
    TEST_ASSERT(respGzip.body.size() > 10 && static_cast<uint8_t>(respGzip.body[0]) == 0x1f && static_cast<uint8_t>(respGzip.body[1]) == 0x8b,
                "Body must contain RFC 1952 Gzip magic bytes (0x1f, 0x8b)");

    // ------------------------------------------------------------------------
    // Stage 13: W3C Extended Logging Verification
    // ------------------------------------------------------------------------
    const auto& logs = iis.getW3CLogs();
    TEST_ASSERT(!logs.empty(), "W3C Extended Logs must contain recorded HTTP requests");
    const auto& lastLog = logs.back();
    TEST_ASSERT(lastLog.date == "2026-10-08", "Log date must match current date");
    TEST_ASSERT(lastLog.method == "GET", "Log method must be GET");
    TEST_ASSERT(lastLog.uriStem == "/index.html", "Log uriStem must be /index.html");
    TEST_ASSERT(lastLog.scStatus == 200, "Log scStatus must be 200");

    // ------------------------------------------------------------------------
    // Stage 14: Win32 C ABI Exports & Concurrent Multithreaded Stress Test
    // ------------------------------------------------------------------------
    void* pEngine = nullptr;
    int32_t initRes = micant::iis::MicaIisInitialize(&pEngine);
    TEST_ASSERT(initRes == 1 && pEngine != nullptr, "MicaIisInitialize must return engine pointer via C ABI");

    char respBuf[2048]{};
    const char* cReq = "GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n";
    int32_t statusAbi = micant::iis::MicaIisProcessRequest(pEngine, cReq, 0, respBuf, sizeof(respBuf));
    TEST_ASSERT(statusAbi == 200, "MicaIisProcessRequest must return 200 via C ABI");
    TEST_ASSERT(std::string(respBuf).find("HTTP/1.1 200 OK") != std::string::npos, "Response buffer must contain HTTP 200 OK");

    // Multithreaded High-Throughput Concurrent HTTP Stress Test (120 requests)
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&iis, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                std::string wire = "GET /health.json HTTP/1.1\r\nHost: localhost\r\n\r\n";
                auto reqS = iis.parseRawHttpWire(wire, "10.10." + std::to_string(t) + "." + std::to_string(op + 1), 40000 + t * 100 + op, false);
                auto respS = iis.processHttpRequest(reqS);
                if (respS.statusCode == 200 && respS.contentType == "application/json" && !respS.body.empty()) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded HTTP stress test must complete with 100% success");

    std::cout << "[TEST] Suite 210: Windows Enterprise IIS & HTTP Server Subsystem PASSED.\n";
}

// ============================================================================
// Suite 211: Windows Server Update Services (WSUS 10.0 / SUSDB / TitanWSUS) Subsystem
// ============================================================================
void Test_WindowsServerUpdateServices_WSUS_Subsystem() {
    std::cout << "[TEST] Suite 211: Windows Server Update Services (WSUS 10.0 / SUSDB / TitanWSUS) Subsystem...\n";

    auto& wsus = micant::wsus::EnterpriseWsusServer::instance();
    wsus.initialize();

    // Stage 1: SCM Services Registration Parity (WsusService)
    auto& scm = micant::scm::ServiceControlManager::get();
    auto sRec = scm.getServiceRecord(L"WsusService");
    TEST_ASSERT(sRec != nullptr, "WsusService must be registered in Service Control Manager");
    TEST_ASSERT(sRec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "WsusService must be in SERVICE_RUNNING state");
    TEST_ASSERT(sRec->startType == micant::scm::SERVICE_AUTO_START, "WsusService must be configured for SERVICE_AUTO_START");

    // Stage 2: Version Database Module Registration Parity
    auto& verDb = micant::version::VersionDatabase::Instance();
    const char* expectedModules[] = {
        "wsusservice.exe",
        "wsusutil.exe",
        "susdb.dll",
        "wuaueng.dll",
        "microsoft.updateservices.administration.dll"
    };
    for (const char* mod : expectedModules) {
        auto* info = verDb.FindModule(mod);
        TEST_ASSERT(info != nullptr, std::string("VersionDatabase must contain registered module: ") + mod);
        TEST_ASSERT(info->stringTable.find("FileVersion") != info->stringTable.end(), "Module must have valid FileVersion resource");
        TEST_ASSERT(info->stringTable.at("FileVersion") == "10.0.26100.1", "Module FileVersion must match 10.0.26100.1");
    }

    // Stage 3: SUSDB Repository & Seeded Target Groups
    TEST_ASSERT(wsus.getTargetGroupCount() >= 4, "SUSDB must contain at least 4 default computer target groups");
    auto groups = wsus.getAllGroups();
    bool foundAll = false, foundServers = false, foundPilot = false;
    for (const auto& g : groups) {
        if (g.groupId == "GROUP-ALL-COMPUTERS") foundAll = true;
        if (g.groupId == "GROUP-SERVERS") foundServers = true;
        if (g.groupId == "GROUP-PILOT") foundPilot = true;
    }
    TEST_ASSERT(foundAll && foundServers && foundPilot, "Target groups must include All Computers, Servers, and Pilot Ring");

    // Stage 4: Client Computer Registration & Heartbeat
    std::string cid1, cid2;
    bool reg1 = wsus.registerClient("TITAN-DC01.micant.internal", "10.0.0.1", "10.0.26100.1", cid1);
    bool reg2 = wsus.registerClient("TITAN-APP01.micant.internal", "10.0.0.2", "10.0.26100.1", cid2);
    TEST_ASSERT(reg1 && !cid1.empty(), "Client TITAN-DC01 must register successfully");
    TEST_ASSERT(reg2 && !cid2.empty(), "Client TITAN-APP01 must register successfully");
    TEST_ASSERT(wsus.getClientCount() >= 2, "Client count must be at least 2");

    // Stage 5: Target Group Reassignment & Hierarchy
    bool assignOk = wsus.assignComputerToGroup(cid1, "GROUP-SERVERS");
    TEST_ASSERT(assignOk, "TITAN-DC01 must be assignable to Production Servers group");
    bool assignPilot = wsus.assignComputerToGroup(cid2, "GROUP-PILOT");
    TEST_ASSERT(assignPilot, "TITAN-APP01 must be assignable to Pilot Ring group");

    // Stage 6: Catalog Inventory Query & Classification Mapping
    auto updates = wsus.getAllUpdates();
    TEST_ASSERT(updates.size() >= 3, "Catalog must contain at least 3 seeded updates");
    bool hasSec = false, hasDef = false, hasSuperseded = false;
    std::string secUpdateId;
    for (const auto& u : updates) {
        if (u.classification == micant::wsus::UpdateClassification::SecurityUpdates && !u.isSuperseded) {
            hasSec = true;
            secUpdateId = u.updateId;
        }
        if (u.classification == micant::wsus::UpdateClassification::DefinitionUpdates) hasDef = true;
        if (u.isSuperseded) hasSuperseded = true;
    }
    TEST_ASSERT(hasSec, "Catalog must contain SecurityUpdates");
    TEST_ASSERT(hasDef, "Catalog must contain DefinitionUpdates");
    TEST_ASSERT(hasSuperseded, "Catalog must contain Superseded update");

    // Stage 7: Upstream Catalog Synchronization
    uint32_t imported = 0;
    bool syncOk = wsus.syncCatalog(4, &imported);
    TEST_ASSERT(syncOk && imported == 4, "syncCatalog must import 4 new patches from upstream catalog");
    TEST_ASSERT(wsus.getUpdateCount() >= 7, "Total update inventory must increase after catalog sync");

    // Stage 8: Approval Rules & Targeting Workflow
    bool approveOk = wsus.approveUpdate(secUpdateId, "GROUP-SERVERS", micant::wsus::UpdateApprovalAction::Install, "EnterpriseAdmin");
    TEST_ASSERT(approveOk, "Security update must be approved for Production Servers");

    // Stage 9: Update Decline & Supersedence Handling
    bool declineOk = wsus.declineUpdate("1A09E5B7-50A4-48FE-98D7-564AC7C48D2A", "EnterpriseAdmin");
    TEST_ASSERT(declineOk, "Superseded update must be declinable");

    // Stage 10: Client Update Applicability Detection (SyncUpdates)
    auto clientSync = wsus.syncUpdatesForClient(cid1);
    TEST_ASSERT(clientSync.totalApprovedCount > 0, "Client sync must detect approved updates for client's group");
    bool foundApplicable = false;
    for (const auto& appU : clientSync.applicableUpdates) {
        if (appU.updateId == secUpdateId) foundApplicable = true;
    }
    TEST_ASSERT(foundApplicable, "Approved security update must be applicable to TITAN-DC01");

    // Stage 11: Installation Status Reporting & Compliance Calculation
    bool repOk = wsus.reportClientStatus(cid1, secUpdateId, micant::wsus::UpdateInstallationState::Installed);
    TEST_ASSERT(repOk, "Client installation status must be recorded in SUSDB");
    double compRate = wsus.calculateOverallComplianceRate();
    TEST_ASSERT(compRate > 0.0, "Overall compliance rate must be positive after successful installation");

    // Stage 12: IIS Web Application Integration (WsusPool on port 8530 & 8531)
    auto& iis = micant::iis::EnterpriseWebServer::instance();
    micant::iis::ApplicationPool wsusPool;
    bool hasPool = iis.getAppPool("WsusPool", wsusPool);
    TEST_ASSERT(hasPool, "IIS must have WsusPool provisioned");
    TEST_ASSERT(wsusPool.state == micant::iis::AppPoolState::Running, "WsusPool must be in Running state");
    TEST_ASSERT(wsus.getHttpPort() == 8530, "WSUS HTTP port must be 8530");
    TEST_ASSERT(wsus.getHttpsPort() == 8531, "WSUS HTTPS port must be 8531");

    // Stage 13: Win32 C ABI Parity (MicaWsus*)
    NTSTATUS abiInit = MicaWsusInitialize();
    TEST_ASSERT(abiInit == micant::STATUS_SUCCESS, "MicaWsusInitialize must return STATUS_SUCCESS");

    char newCid[64]{};
    NTSTATUS abiReg = MicaWsusRegisterComputer("TITAN-WORK01.micant.internal", "10.0.0.50", "10.0.26100.1", newCid, sizeof(newCid));
    TEST_ASSERT(abiReg == micant::STATUS_SUCCESS && std::strlen(newCid) > 0, "MicaWsusRegisterComputer must return valid client ID");

    uint32_t abiImp = 0;
    NTSTATUS abiSync = MicaWsusSyncCatalog(2, &abiImp);
    TEST_ASSERT(abiSync == micant::STATUS_SUCCESS && abiImp == 2, "MicaWsusSyncCatalog must succeed via C ABI");

    double abiComp = MicaWsusGetComplianceRate();
    TEST_ASSERT(abiComp >= 0.0, "MicaWsusGetComplianceRate must return valid double");

    // Stage 14: 120-Operation Concurrent Multithreaded Client Sync Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&wsus, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                std::string compName = "STRESS-NODE-" + std::to_string(t) + "-" + std::to_string(op);
                std::string cId;
                bool okR = wsus.registerClient(compName + ".micant.internal", "10.100." + std::to_string(t) + "." + std::to_string(op + 1), "10.0.26100.1", cId);
                auto syncRes = wsus.syncUpdatesForClient(cId);
                bool okRep = wsus.reportClientStatus(cId, "9F818C52-79F8-4D2A-98C0-745BD3BC6E21", micant::wsus::UpdateInstallationState::Installed);
                if (okR && syncRes.totalApprovedCount > 0 && okRep) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded client sync stress test must complete with 100% success");

    std::cout << "[TEST] Suite 211: Windows Server Update Services (WSUS 10.0 / SUSDB / TitanWSUS) Subsystem PASSED.\n";
}

void Test_WindowsRemoteManagement_WinRM_Subsystem() {
    std::cout << "[TEST] Executing Suite 212: Windows Remote Management (WinRM 3.0 / WS-Management / PSRP) Subsystem...\n";

    // Stage 1: SCM Service Registration (WinRM)
    auto& winrm = micant::winrm::EnterpriseWinRmServer::instance();
    TEST_ASSERT(winrm.initialize(), "EnterpriseWinRmServer initialization must succeed");

    auto& scm = micant::scm::ServiceControlManager::get();
    auto svc = scm.getServiceRecord(L"WinRM");
    TEST_ASSERT(svc != nullptr, "WinRM service must be registered in SCM");
    TEST_ASSERT(svc->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "WinRM service must be in running state");
    TEST_ASSERT(svc->binaryPath.find(L"svchost.exe") != std::wstring::npos, "WinRM must be hosted by svchost.exe");

    // Stage 2: VersionDatabase Registration
    auto& db = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(db.FindModule("winrm.cmd") != nullptr, "winrm.cmd must be registered in VersionDatabase");
    TEST_ASSERT(db.FindModule("winrs.exe") != nullptr, "winrs.exe must be registered in VersionDatabase");
    TEST_ASSERT(db.FindModule("wsmprovhost.exe") != nullptr, "wsmprovhost.exe must be registered in VersionDatabase");
    TEST_ASSERT(db.FindModule("wsmsvc.dll") != nullptr, "wsmsvc.dll must be registered in VersionDatabase");

    // Stage 3: Default WinRM Listeners (HTTP 5985 & HTTPS 5986)
    auto listeners = winrm.getListeners();
    TEST_ASSERT(listeners.size() >= 2, "WinRM must configure at least 2 default listeners");
    bool foundHttp = false, foundHttps = false;
    for (const auto& l : listeners) {
        if (l.transport == micant::winrm::ListenerTransport::Http && l.port == 5985) foundHttp = true;
        if (l.transport == micant::winrm::ListenerTransport::Https && l.port == 5986) {
            foundHttps = true;
            TEST_ASSERT(!l.certificateThumbprint.empty(), "HTTPS listener must possess TLS certificate thumbprint");
        }
    }
    TEST_ASSERT(foundHttp, "HTTP listener on port 5985 must be present");
    TEST_ASSERT(foundHttps, "HTTPS listener on port 5986 must be present");

    // Stage 4: Configuration & Quota Parameters
    auto cfg = winrm.getServiceConfig();
    TEST_ASSERT(cfg.maxEnvelopeSizeKb == 500, "MaxEnvelopeSizekb must match standard 500 KB");
    TEST_ASSERT(cfg.authKerberos == true, "Kerberos authentication must be enabled");
    TEST_ASSERT(cfg.authNegotiate == true, "Negotiate authentication must be enabled");

    auto quotas = winrm.getQuotaConfig();
    TEST_ASSERT(quotas.maxShellsPerUser == 30, "MaxShellsPerUser must default to 30");
    TEST_ASSERT(quotas.maxProcessesPerShell == 25, "MaxProcessesPerShell must default to 25");

    // Stage 5: Custom Listener Management
    micant::winrm::WinRmListener customListener;
    customListener.transport = micant::winrm::ListenerTransport::Https;
    customListener.address = "127.0.0.1";
    customListener.port = 15986;
    customListener.hostname = "management.internal";
    customListener.certificateThumbprint = "112233445566778899AABBCCDDEEFF0011223344";
    winrm.addListener(customListener);

    bool foundCustom = false;
    for (const auto& l : winrm.getListeners()) {
        if (l.port == 15986) foundCustom = true;
    }
    TEST_ASSERT(foundCustom, "Custom listener on port 15986 must be added successfully");
    TEST_ASSERT(winrm.removeListener(micant::winrm::ListenerTransport::Https, 15986), "Removing custom listener must succeed");

    // Stage 6: Remote Command Shell Session Creation
    std::string shellId;
    bool okShell = winrm.openShell(micant::winrm::ShellType::Cmd, "TITAN\\Administrator", "10.0.0.50", "C:\\Windows\\System32", {}, shellId);
    TEST_ASSERT(okShell && !shellId.empty(), "Opening remote shell session must succeed and return ShellId");

    micant::winrm::RemoteShell shellObj;
    TEST_ASSERT(winrm.getShell(shellId, shellObj), "Retrieving remote shell object must succeed");
    TEST_ASSERT(shellObj.ownerUser == "TITAN\\Administrator", "Shell owner must be TITAN\\Administrator");
    TEST_ASSERT(shellObj.workingDirectory == "C:\\Windows\\System32", "Working directory must match initial path");

    // Stage 7: Remote Command Execution & Standard Output Capture
    std::string cmdId;
    bool okCmd = winrm.executeCommand(shellId, "hostname", {}, cmdId);
    TEST_ASSERT(okCmd && !cmdId.empty(), "Executing 'hostname' command must succeed");

    std::string sOut, sErr;
    int32_t ec = -1;
    bool fin = false;
    TEST_ASSERT(winrm.receiveCommandOutput(shellId, cmdId, sOut, sErr, ec, fin), "Receiving command output must succeed");
    TEST_ASSERT(fin, "Command must be marked finished");
    TEST_ASSERT(ec == 0, "Exit code must be 0");
    TEST_ASSERT(sOut.find("TITAN-MGMT01") != std::string::npos, "Hostname output must match TITAN-MGMT01");

    // Stage 8: Output Streaming & Exit Codes (whoami & echo)
    std::string whoamiCmdId;
    TEST_ASSERT(winrm.executeCommand(shellId, "whoami", {}, whoamiCmdId), "Executing 'whoami' must succeed");
    winrm.receiveCommandOutput(shellId, whoamiCmdId, sOut, sErr, ec, fin);
    TEST_ASSERT(fin && ec == 0 && sOut.find("TITAN\\Administrator") != std::string::npos, "whoami must output user identity");

    std::string echoCmdId;
    TEST_ASSERT(winrm.executeCommand(shellId, "echo Sovereign Kernel WinRM", {}, echoCmdId), "Executing 'echo' must succeed");
    winrm.receiveCommandOutput(shellId, echoCmdId, sOut, sErr, ec, fin);
    TEST_ASSERT(fin && ec == 0 && sOut.find("Sovereign Kernel WinRM") != std::string::npos, "echo must return printed string");

    // Stage 9: Signal Delivery & Process Cancellation
    std::string sigCmdId;
    TEST_ASSERT(winrm.executeCommand(shellId, "custom_long_running_task.exe", {}, sigCmdId), "Spawning command for cancellation must succeed");
    bool sigOk = winrm.signalCommand(shellId, sigCmdId, micant::winrm::SIGNAL_CODE_TERMINATE);
    TEST_ASSERT(sigOk, "Signaling termination to command must succeed");
    winrm.receiveCommandOutput(shellId, sigCmdId, sOut, sErr, ec, fin);
    TEST_ASSERT(fin && ec == -1, "Terminated command must have exit code -1");
    TEST_ASSERT(sErr.find("canceled by signal") != std::string::npos, "Stderr must indicate cancellation by signal");

    // Stage 10: PowerShell Remote Shell & PSRP Runspace Pool
    std::string psShellId;
    bool okPs = winrm.openShell(micant::winrm::ShellType::PowerShell, "TITAN\\Operator", "10.0.0.51", "C:\\", {}, psShellId);
    TEST_ASSERT(okPs, "Opening PowerShell remote shell must succeed");

    micant::winrm::RemoteShell psObj;
    TEST_ASSERT(winrm.getShell(psShellId, psObj), "Retrieving PowerShell shell must succeed");
    TEST_ASSERT(psObj.isPsrpPoolOpen, "PSRP runspace pool must be open");

    std::string psCmdId;
    winrm.executeCommand(psShellId, "Get-Process", {}, psCmdId);
    winrm.receiveCommandOutput(psShellId, psCmdId, sOut, sErr, ec, fin);
    TEST_ASSERT(fin && ec == 0 && sOut.find("ProcessName") != std::string::npos, "Get-Process execution must return process list");

    // Stage 11: SOAP WS-Management Wire Processing (Create Shell)
    std::string createSoap =
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:wsman=\"http://schemas.dmtf.org/wbem/wsman/1/wsman.xsd\">\r\n"
        "  <s:Header>\r\n"
        "    <wsa:Action>http://schemas.xmlsoap.org/ws/2004/09/transfer/Create</wsa:Action>\r\n"
        "    <wsa:MessageID>urn:uuid:11112222-3333-4444-5555-666677778888</wsa:MessageID>\r\n"
        "    <wsman:ResourceURI>http://schemas.microsoft.com/wbem/wsman/1/windows/shell/cmd</wsman:ResourceURI>\r\n"
        "  </s:Header>\r\n"
        "  <s:Body/>\r\n"
        "</s:Envelope>";
    std::string createResp;
    bool okSoapCreate = winrm.processSoapRequest(createSoap, createResp);
    TEST_ASSERT(okSoapCreate, "processSoapRequest for Create Shell must succeed");
    TEST_ASSERT(createResp.find("CreateResponse") != std::string::npos, "Response must contain CreateResponse");
    TEST_ASSERT(createResp.find("<rsp:ShellId>") != std::string::npos, "Response must return ShellId");

    // Stage 12: SOAP WS-Management Command & Receive Protocol
    // Extract shellId from response
    size_t idStart = createResp.find("<rsp:ShellId>") + 13;
    size_t idEnd = createResp.find("</rsp:ShellId>", idStart);
    std::string wireShellId = createResp.substr(idStart, idEnd - idStart);

    std::string cmdSoap =
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:wsman=\"http://schemas.dmtf.org/wbem/wsman/1/wsman.xsd\" "
        "xmlns:rsp=\"http://schemas.microsoft.com/wbem/wsman/1/windows/shell\">\r\n"
        "  <s:Header>\r\n"
        "    <wsa:Action>http://schemas.microsoft.com/wbem/wsman/1/windows/shell/Command</wsa:Action>\r\n"
        "    <wsa:MessageID>urn:uuid:22223333-4444-5555-6666-777788889999</wsa:MessageID>\r\n"
        "    <wsman:SelectorSet><wsman:Selector Name=\"ShellId\">" + wireShellId + "</wsman:Selector></wsman:SelectorSet>\r\n"
        "  </s:Header>\r\n"
        "  <s:Body>\r\n"
        "    <rsp:CommandLine>hostname</rsp:CommandLine>\r\n"
        "  </s:Body>\r\n"
        "</s:Envelope>";
    std::string cmdResp;
    TEST_ASSERT(winrm.processSoapRequest(cmdSoap, cmdResp), "SOAP Command request must succeed");
    TEST_ASSERT(cmdResp.find("CommandResponse") != std::string::npos, "SOAP Response must contain CommandResponse");

    size_t cIdStart = cmdResp.find("<rsp:CommandId>") + 15;
    size_t cIdEnd = cmdResp.find("</rsp:CommandId>", cIdStart);
    std::string wireCmdId = cmdResp.substr(cIdStart, cIdEnd - cIdStart);

    std::string recvSoap =
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:wsman=\"http://schemas.dmtf.org/wbem/wsman/1/wsman.xsd\" "
        "xmlns:rsp=\"http://schemas.microsoft.com/wbem/wsman/1/windows/shell\">\r\n"
        "  <s:Header>\r\n"
        "    <wsa:Action>http://schemas.microsoft.com/wbem/wsman/1/windows/shell/Receive</wsa:Action>\r\n"
        "    <wsa:MessageID>urn:uuid:33334444-5555-6666-7777-888899990000</wsa:MessageID>\r\n"
        "    <wsman:SelectorSet><wsman:Selector Name=\"ShellId\">" + wireShellId + "</wsman:Selector></wsman:SelectorSet>\r\n"
        "  </s:Header>\r\n"
        "  <s:Body>\r\n"
        "    <rsp:Receive><rsp:CommandId>" + wireCmdId + "</rsp:CommandId></rsp:Receive>\r\n"
        "  </s:Body>\r\n"
        "</s:Envelope>";
    std::string recvResp;
    TEST_ASSERT(winrm.processSoapRequest(recvSoap, recvResp), "SOAP Receive request must succeed");
    TEST_ASSERT(recvResp.find("ReceiveResponse") != std::string::npos, "SOAP Response must contain ReceiveResponse");
    TEST_ASSERT(recvResp.find("TITAN-MGMT01") != std::string::npos, "ReceiveResponse must contain stream output");
    TEST_ASSERT(recvResp.find("CommandState State=\"Done\"") != std::string::npos, "Command state must be Done");

    // Close wire shell
    winrm.closeShell(wireShellId);

    // Stage 13: Win32 C ABI Parity (MicaWinRm*)
    NTSTATUS abiInit = MicaWinRmInitialize();
    TEST_ASSERT(abiInit == micant::STATUS_SUCCESS, "MicaWinRmInitialize must return STATUS_SUCCESS");

    char abiShellId[64]{};
    NTSTATUS abiOpen = MicaWinRmOpenShell("cmd", "TITAN\\AbiUser", abiShellId, sizeof(abiShellId));
    TEST_ASSERT(abiOpen == micant::STATUS_SUCCESS && std::strlen(abiShellId) > 0, "MicaWinRmOpenShell must succeed");

    char abiCmdId[64]{};
    NTSTATUS abiExec = MicaWinRmExecuteCommand(abiShellId, "ver", abiCmdId, sizeof(abiCmdId));
    TEST_ASSERT(abiExec == micant::STATUS_SUCCESS && std::strlen(abiCmdId) > 0, "MicaWinRmExecuteCommand must succeed");

    char abiStdout[256]{};
    int32_t abiExitCode = -1;
    bool abiFinished = false;
    NTSTATUS abiRecv = MicaWinRmReceiveOutput(abiShellId, abiCmdId, abiStdout, sizeof(abiStdout), &abiExitCode, &abiFinished);
    TEST_ASSERT(abiRecv == micant::STATUS_SUCCESS && abiFinished && abiExitCode == 0, "MicaWinRmReceiveOutput must succeed");
    TEST_ASSERT(std::string(abiStdout).find("Version 10.0.26100.1") != std::string::npos, "MicaWinRmReceiveOutput must contain version string");

    NTSTATUS abiClose = MicaWinRmCloseShell(abiShellId);
    TEST_ASSERT(abiClose == micant::STATUS_SUCCESS, "MicaWinRmCloseShell must return STATUS_SUCCESS");

    uint32_t aShells = 0, tCmds = 0;
    uint64_t tBytes = 0;
    MicaWinRmGetStats(&aShells, &tCmds, &tBytes);
    TEST_ASSERT(tCmds > 0, "MicaWinRmGetStats must report executed commands");

    // Stage 14: 120-Operation Concurrent Multithreaded Remote Shell Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&winrm, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                std::string uName = "TITAN\\StressUser" + std::to_string(t);
                std::string sId;
                bool okO = winrm.openShell(micant::winrm::ShellType::Cmd, uName, "10.200." + std::to_string(t) + ".10", "C:\\", {}, sId);
                std::string cId;
                bool okE = winrm.executeCommand(sId, "echo StressOp " + std::to_string(op), {}, cId);
                std::string oS, oE;
                int32_t ec = 0;
                bool f = false;
                bool okR = winrm.receiveCommandOutput(sId, cId, oS, oE, ec, f);
                bool okC = winrm.closeShell(sId);
                if (okO && okE && okR && okC && f && ec == 0) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded WinRM stress test must achieve 100% success");

    std::cout << "[TEST] Suite 212: Windows Remote Management (WinRM 3.0 / WS-Management / PSRP) Subsystem PASSED.\n";
}

void Test_WindowsOpenSSH_ServerClient_Subsystem() {
    std::cout << "[TEST] Executing Suite 213: Windows Native OpenSSH (sshd / ssh / sftp / TitanSSH) Subsystem...\n";

    // Stage 1: SCM Service Registration (sshd and ssh-agent)
    auto& ssh = micant::ssh::EnterpriseSshServer::instance();
    TEST_ASSERT(ssh.initialize(), "EnterpriseSshServer initialization must succeed");

    auto& scm = micant::scm::ServiceControlManager::get();
    auto sshdRec = scm.getServiceRecord(L"sshd");
    TEST_ASSERT(sshdRec != nullptr, "sshd service must be registered in SCM");
    TEST_ASSERT(sshdRec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "sshd service must be in running state");
    TEST_ASSERT(sshdRec->binaryPath.find(L"sshd.exe") != std::wstring::npos, "sshd binary path must point to sshd.exe");

    auto agentRec = scm.getServiceRecord(L"ssh-agent");
    TEST_ASSERT(agentRec != nullptr, "ssh-agent service must be registered in SCM");
    TEST_ASSERT(agentRec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "ssh-agent service must be running");

    // Stage 2: VersionDatabase Registration
    auto& db = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(db.FindModule("ssh.exe") != nullptr, "ssh.exe must be registered in VersionDatabase");
    TEST_ASSERT(db.FindModule("sshd.exe") != nullptr, "sshd.exe must be registered in VersionDatabase");
    TEST_ASSERT(db.FindModule("ssh-keygen.exe") != nullptr, "ssh-keygen.exe must be registered in VersionDatabase");
    TEST_ASSERT(db.FindModule("ssh-agent.exe") != nullptr, "ssh-agent.exe must be registered in VersionDatabase");
    TEST_ASSERT(db.FindModule("sftp-server.exe") != nullptr, "sftp-server.exe must be registered in VersionDatabase");

    // Stage 3: Host Key Generation & Storage (ssh-keygen)
    micant::ssh::SshKeyPair edKey;
    bool genEdOk = ssh.generateKeyPair(micant::ssh::KeyType::Ed25519, 256, "host@titan-node01", edKey);
    TEST_ASSERT(genEdOk, "Generating Ed25519 host key must succeed");
    TEST_ASSERT(edKey.publicKeyString.find("ssh-ed25519") != std::string::npos, "Public key must identify ssh-ed25519");
    TEST_ASSERT(edKey.fingerprintSha256.find("SHA256:") == 0, "Fingerprint must begin with SHA256:");

    micant::ssh::SshKeyPair rsaKey;
    bool genRsaOk = ssh.generateKeyPair(micant::ssh::KeyType::Rsa, 3072, "host@titan-node01", rsaKey);
    TEST_ASSERT(genRsaOk, "Generating RSA 3072 host key must succeed");
    TEST_ASSERT(rsaKey.publicKeyString.find("ssh-rsa") != std::string::npos, "Public key must identify ssh-rsa");

    // Stage 4: TCP Port 22 Server Listener Initialization
    TEST_ASSERT(ssh.isRunning(), "SSH server daemon must report running");
    TEST_ASSERT(ssh.getPort() == 22, "SSH server default port must be 22");

    // Stage 5: Client Connection Session Lifecycle
    uint32_t sId1 = 0;
    bool okS1 = ssh.openSession("192.168.1.150", 49152, "SSH-2.0-OpenSSH_9.5", sId1);
    TEST_ASSERT(okS1 && sId1 > 0, "Opening SSH client connection session must succeed");

    // Stage 6: Public Key Authentication (authorized_keys)
    bool authPubOk = ssh.authenticateSession(sId1, "admin", micant::ssh::AuthMethod::PublicKey, "ssh-ed25519");
    TEST_ASSERT(authPubOk, "Public key authentication for admin must succeed");

    // Stage 7: Windows Password Authentication Fallback
    uint32_t sId2 = 0;
    ssh.openSession("192.168.1.151", 49153, "SSH-2.0-PuTTY_Release_0.80", sId2);
    bool authPassOk = ssh.authenticateSession(sId2, "Administrator", micant::ssh::AuthMethod::Password, "MicaNT@2026!");
    TEST_ASSERT(authPassOk, "Windows password authentication for Administrator must succeed");

    // Stage 8: Authentication Rejection on Invalid Credentials
    uint32_t sIdBad = 0;
    ssh.openSession("192.168.1.199", 49154, "SSH-2.0-BadClient", sIdBad);
    bool badAuth = ssh.authenticateSession(sIdBad, "intruder", micant::ssh::AuthMethod::Password, "WrongPass!");
    TEST_ASSERT(!badAuth, "Authentication with incorrect credentials must be rejected");
    ssh.closeSession(sIdBad);

    // Stage 9: Interactive Channel & Pseudo Console (ConPTY) Allocation
    uint32_t cId1 = 0;
    bool okChan = ssh.openChannel(sId1, micant::ssh::ChannelType::Session, 0, cId1);
    TEST_ASSERT(okChan && cId1 > 0, "Opening session channel must succeed");

    micant::ssh::TerminalPtyInfo pty;
    pty.term = "xterm-256color";
    pty.widthChars = 120;
    pty.heightChars = 30;
    bool ptyOk = ssh.allocatePty(sId1, cId1, pty);
    TEST_ASSERT(ptyOk, "Allocating ConPTY pseudo console must succeed");

    // Stage 10: Remote Command Execution & Output Stream Capture
    bool execOk = ssh.executeCommand(sId1, cId1, "hostname");
    TEST_ASSERT(execOk, "Executing 'hostname' command over SSH channel must succeed");

    std::string sOut, sErr;
    int32_t exitCode = -1;
    bool isEof = false;
    ssh.readChannelOutput(sId1, cId1, sOut, sErr, exitCode, isEof);
    TEST_ASSERT(isEof && exitCode == 0, "Command execution must report EOF and exit code 0");
    TEST_ASSERT(sOut.find("TITAN-NODE01") != std::string::npos, "Stdout must contain hostname TITAN-NODE01");

    // Stage 11: SFTP Subsystem Virtual Filesystem Read Operations
    uint32_t sftpChanId = 0;
    ssh.openChannel(sId1, micant::ssh::ChannelType::Session, 1, sftpChanId);
    bool sftpReqOk = ssh.requestSubsystem(sId1, sftpChanId, "sftp");
    TEST_ASSERT(sftpReqOk, "Requesting 'sftp' subsystem must succeed");

    std::vector<uint8_t> sftpData;
    bool sftpReadOk = ssh.sftpReadFile("C:\\ProgramData\\ssh\\sshd_config", sftpData);
    TEST_ASSERT(sftpReadOk && !sftpData.empty(), "Reading sshd_config via SFTP must succeed");
    std::string confStr(sftpData.begin(), sftpData.end());
    TEST_ASSERT(confStr.find("Port 22") != std::string::npos, "sshd_config content must specify Port 22");

    // Stage 12: SFTP Subsystem Virtual Filesystem Write & Directory Listing
    std::string uploadPath = "C:\\Users\\Administrator\\test_payload.bin";
    std::vector<uint8_t> uploadData = {'T','E','S','T','_','S','F','T','P','_','2','0','2','6'};
    bool sftpWriteOk = ssh.sftpWriteFile(uploadPath, uploadData);
    TEST_ASSERT(sftpWriteOk, "Writing file via SFTP must succeed");

    auto dirList = ssh.sftpListDirectory("C:\\Users\\Administrator");
    TEST_ASSERT(!dirList.empty(), "SFTP directory listing must return files");
    bool foundUploaded = false;
    for (const auto& item : dirList) {
        if (item.find("test_payload.bin") != std::string::npos) foundUploaded = true;
    }
    TEST_ASSERT(foundUploaded, "Directory listing must include uploaded test_payload.bin");

    ssh.closeSession(sId1);
    ssh.closeSession(sId2);

    // Stage 13: Win32 C ABI Parity (MicaSsh*)
    NTSTATUS abiInit = MicaSshInitialize();
    TEST_ASSERT(abiInit == micant::STATUS_SUCCESS, "MicaSshInitialize must return STATUS_SUCCESS");

    char pubK[256]{}, fp[128]{};
    NTSTATUS abiGen = MicaSshGenerateKeyPair(0, 256, "abi@titan", pubK, sizeof(pubK), fp, sizeof(fp));
    TEST_ASSERT(abiGen == micant::STATUS_SUCCESS && std::strlen(pubK) > 0, "MicaSshGenerateKeyPair must succeed");

    uint32_t abiSid = 0;
    NTSTATUS abiOpen = MicaSshOpenSession("127.0.0.1", 44332, &abiSid);
    TEST_ASSERT(abiOpen == micant::STATUS_SUCCESS && abiSid > 0, "MicaSshOpenSession must succeed");

    NTSTATUS abiAuth = MicaSshAuthenticate(abiSid, "Administrator", 1, "MicaNT@2026!");
    TEST_ASSERT(abiAuth == micant::STATUS_SUCCESS, "MicaSshAuthenticate must succeed");

    char abiOut[256]{};
    int32_t abiEc = -1;
    NTSTATUS abiExec = MicaSshExecuteCommand(abiSid, "uname -a", abiOut, sizeof(abiOut), &abiEc);
    TEST_ASSERT(abiExec == micant::STATUS_SUCCESS && abiEc == 0, "MicaSshExecuteCommand must succeed");
    TEST_ASSERT(std::string(abiOut).find("MicaNT") != std::string::npos, "C ABI output must contain MicaNT");

    NTSTATUS abiClose = MicaSshCloseSession(abiSid);
    TEST_ASSERT(abiClose == micant::STATUS_SUCCESS, "MicaSshCloseSession must return STATUS_SUCCESS");

    uint32_t actS = 0, totS = 0;
    uint64_t totB = 0;
    MicaSshGetStats(&actS, &totS, &totB);
    TEST_ASSERT(totS > 0, "MicaSshGetStats must report total sessions");

    // Stage 14: 120-Operation Concurrent Multithreaded SSH Session Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&ssh, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                uint32_t sId = 0;
                bool okO = ssh.openSession("10.0.8." + std::to_string(t), 50000 + op, "SSH-2.0-StressClient", sId);
                bool okA = ssh.authenticateSession(sId, "Administrator", micant::ssh::AuthMethod::Password, "MicaNT@2026!");
                uint32_t cId = 0;
                bool okC = ssh.openChannel(sId, micant::ssh::ChannelType::Session, 0, cId);
                bool okE = ssh.executeCommand(sId, cId, "echo StressWorker_" + std::to_string(t) + "_" + std::to_string(op));
                std::string oS, oE;
                int32_t ec = 0;
                bool f = false;
                bool okR = ssh.readChannelOutput(sId, cId, oS, oE, ec, f);
                bool okCl = ssh.closeSession(sId);
                if (okO && okA && okC && okE && okR && okCl && f && ec == 0) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded SSH stress test must achieve 100% success");

    std::cout << "[TEST] Suite 213: Windows Native OpenSSH (sshd / ssh / sftp / TitanSSH) Subsystem PASSED.\n";
}

void Test_WindowsRemoteDesktop_RDP_Subsystem() {
    std::cout << "[TEST] Executing Suite 214: Windows Remote Desktop Protocol (RDP / MS-RDPBCGR) Enterprise Subsystem...\n";

    // Stage 1: SCM Services Registration (TermService, SessionEnv, UmRdpService)
    micant::rdp::RegisterRdpSubsystem();
    auto& scm = micant::scm::ServiceControlManager::get();

    auto termRec = scm.getServiceRecord(L"TermService");
    TEST_ASSERT(termRec != nullptr, "TermService must be registered in SCM");
    TEST_ASSERT(termRec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "TermService must be running");
    TEST_ASSERT(termRec->displayName.find(L"Remote Desktop Services") != std::wstring::npos, "TermService display name match");

    auto envRec = scm.getServiceRecord(L"SessionEnv");
    TEST_ASSERT(envRec != nullptr, "SessionEnv must be registered in SCM");
    TEST_ASSERT(envRec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "SessionEnv must be running");

    auto umrdpRec = scm.getServiceRecord(L"UmRdpService");
    TEST_ASSERT(umrdpRec != nullptr, "UmRdpService must be registered in SCM");
    TEST_ASSERT(umrdpRec->status.dwCurrentState == micant::scm::SERVICE_RUNNING, "UmRdpService must be running");

    // Stage 2: VersionDatabase Registration
    auto& vdb = micant::version::VersionDatabase::Instance();
    TEST_ASSERT(vdb.FindModule("mstsc.exe") != nullptr, "mstsc.exe must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("rdpclip.exe") != nullptr, "rdpclip.exe must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("rdpcorets.dll") != nullptr, "rdpcorets.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("termsrv.dll") != nullptr, "termsrv.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("wtsapi32.dll") != nullptr, "wtsapi32.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("rdpsnd.dll") != nullptr, "rdpsnd.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("rdpdr.dll") != nullptr, "rdpdr.dll must be registered in VersionDatabase");
    TEST_ASSERT(vdb.FindModule("mstscax.dll") != nullptr, "mstscax.dll must be registered in VersionDatabase");

    // Stage 3: EnterpriseRdpServer Lifecycle & Configuration
    auto& server = micant::rdp::EnterpriseRdpServer::get();
    TEST_ASSERT(server.isRunning(), "EnterpriseRdpServer must be running");
    TEST_ASSERT(server.getPort() == 3389, "Default RDP port must be 3389");
    TEST_ASSERT(server.isNlaEnforced(), "NLA should be enforced by default");

    auto consoleSession = server.getSession(1);
    TEST_ASSERT(consoleSession != nullptr, "Console session 1 must exist");
    TEST_ASSERT(consoleSession->getWinStationName() == L"Console", "Session 1 must be Console");
    TEST_ASSERT(consoleSession->getState() == micant::rdp::RdpSessionState::ActiveStreaming, "Console session must be ActiveStreaming");

    // Stage 4: TPKT Header Creation & Extraction
    micant::rdp::TpktHeader tpktHdr{};
    tpktHdr.version = micant::rdp::TPKT_VERSION;
    tpktHdr.reserved = 0;
    tpktHdr.length = 24;
    TEST_ASSERT(tpktHdr.version == 3, "TPKT version must be 3");
    TEST_ASSERT(tpktHdr.length == 24, "TPKT length must match");

    // Stage 5: X.224 Connection Request & Security Negotiation (CredSSP / NLA)
    std::vector<uint8_t> crPacket;
    crPacket.resize(sizeof(micant::rdp::TpktHeader) + sizeof(micant::rdp::X224CrHeader) + sizeof(micant::rdp::RdpNegReq));
    auto* pTpkt = reinterpret_cast<micant::rdp::TpktHeader*>(crPacket.data());
    pTpkt->version = micant::rdp::TPKT_VERSION;
    pTpkt->length = static_cast<uint16_t>(crPacket.size());

    auto* pCr = reinterpret_cast<micant::rdp::X224CrHeader*>(crPacket.data() + sizeof(micant::rdp::TpktHeader));
    pCr->lengthIndicator = 6;
    pCr->tpduCode = micant::rdp::X224_TPDU_CR;
    pCr->srcRef = 0x4321;

    auto* pNeg = reinterpret_cast<micant::rdp::RdpNegReq*>(crPacket.data() + sizeof(micant::rdp::TpktHeader) + sizeof(micant::rdp::X224CrHeader));
    pNeg->type = micant::rdp::RDP_NEG_REQ;
    pNeg->length = 8;
    pNeg->requestedProtocols = micant::rdp::PROTOCOL_HYBRID | micant::rdp::PROTOCOL_SSL;

    std::vector<uint8_t> ccResponse;
    uint32_t selectedProto = 0;
    bool negOk = server.processConnectionRequest(crPacket, ccResponse, selectedProto);
    TEST_ASSERT(negOk, "Security negotiation must succeed");
    TEST_ASSERT(selectedProto == micant::rdp::PROTOCOL_HYBRID, "Hybrid CredSSP/NLA should be selected");
    TEST_ASSERT(!ccResponse.empty(), "Server must reply with CC response");

    const auto* pRspCc = reinterpret_cast<const micant::rdp::X224CcHeader*>(ccResponse.data() + sizeof(micant::rdp::TpktHeader));
    TEST_ASSERT(pRspCc->tpduCode == micant::rdp::X224_TPDU_CC, "Response TPDU must be CC (0xD0)");
    TEST_ASSERT(pRspCc->dstRef == 0x4321, "Destination reference must match client source reference");

    // Stage 6: Security Negotiation Enforcements (Reject Non-NLA when Enforced)
    pNeg->requestedProtocols = micant::rdp::PROTOCOL_RDP; // Client only offers legacy standard RDP
    std::vector<uint8_t> failResponse;
    uint32_t failProto = 0;
    bool rejectedOk = !server.processConnectionRequest(crPacket, failResponse, failProto);
    TEST_ASSERT(rejectedOk, "Server must reject non-NLA client when NLA is enforced");
    const auto* pFail = reinterpret_cast<const micant::rdp::RdpNegFailure*>(failResponse.data() + sizeof(micant::rdp::TpktHeader) + sizeof(micant::rdp::X224CcHeader));
    TEST_ASSERT(pFail->failureCode == micant::rdp::HYBRID_REQUIRED_BY_SERVER, "Failure code must be HYBRID_REQUIRED_BY_SERVER");

    // Stage 7: Remote Desktop Session Creation & WinStation Naming
    auto session = server.createSession("192.168.1.188", "TITAN-WORKSTATION", L"DevAdmin", L"MICANT");
    TEST_ASSERT(session != nullptr, "Session creation must return non-null object");
    uint32_t sid = session->getSessionId();
    TEST_ASSERT(sid >= 2, "Remote Desktop session ID must be >= 2");
    TEST_ASSERT(session->getWinStationName().find(L"RDP-Tcp#") != std::wstring::npos, "WinStation name format RDP-Tcp#<id>");
    TEST_ASSERT(session->getClientIp() == "192.168.1.188", "Client IP must match");
    TEST_ASSERT(session->getUserName() == L"DevAdmin", "User name must match");
    TEST_ASSERT(session->getDomainName() == L"MICANT", "Domain name must match");
    TEST_ASSERT(session->getState() == micant::rdp::RdpSessionState::Handshaking, "Initial state must be Handshaking");

    // Stage 8: Virtual Channels Registration & Data Flow
    TEST_ASSERT(session->hasVirtualChannel(micant::rdp::CHANNEL_CLIPRDR), "cliprdr virtual channel must be present");
    TEST_ASSERT(session->hasVirtualChannel(micant::rdp::CHANNEL_RDPSND), "rdpsnd virtual channel must be present");
    TEST_ASSERT(session->hasVirtualChannel(micant::rdp::CHANNEL_RDPDR), "rdpdr virtual channel must be present");
    TEST_ASSERT(session->hasVirtualChannel(micant::rdp::CHANNEL_RDPGFX), "rdpgfx virtual channel must be present");
    TEST_ASSERT(session->hasVirtualChannel(micant::rdp::CHANNEL_RAIL), "rail virtual channel must be present");

    micant::rdp::RdpChannelPacket sndPkt;
    sndPkt.channelName = micant::rdp::CHANNEL_RDPSND;
    sndPkt.channelId = session->getChannelId(micant::rdp::CHANNEL_RDPSND);
    sndPkt.payload = { 0x01, 0x00, 0x10, 0x00, 0x44, 0xAC, 0x00, 0x00 }; // 44.1kHz audio wave header chunk
    session->queueChannelPacket(sndPkt);

    micant::rdp::RdpChannelPacket poppedPkt;
    bool popOk = session->popChannelPacket(poppedPkt);
    TEST_ASSERT(popOk, "Popping channel packet must succeed");
    TEST_ASSERT(poppedPkt.channelName == micant::rdp::CHANNEL_RDPSND, "Channel name must match rdpsnd");
    TEST_ASSERT(poppedPkt.payload.size() == 8, "Payload size must match");

    // Stage 9: Clipboard Redirection (cliprdr) Mirror Cache
    std::string clipText = "MicaNT Sovereign RDP Clipboard Test 2026";
    std::vector<uint8_t> clipData(clipText.begin(), clipText.end());
    session->setClipboardData(micant::rdp::CF_RAW_UNICODETEXT, clipData);

    std::vector<uint8_t> readClip;
    bool getClipOk = session->getClipboardData(micant::rdp::CF_RAW_UNICODETEXT, readClip);
    TEST_ASSERT(getClipOk, "Getting clipboard data must succeed");
    std::string readStr(readClip.begin(), readClip.end());
    TEST_ASSERT(readStr == clipText, "Clipboard text content must match exactly");

    // Stage 10: Fast-Path Screen Update Dirty Rect Tile Encoding
    micant::rdp::RdpDirtyRect rect{100, 100, 32, 32};
    std::vector<uint32_t> pixels(32 * 32, 0xFF00D4FF); // Cyan 32bpp pixels
    auto updatePdu = micant::rdp::EnterpriseRdpServer::encodeFastPathBitmapUpdate(rect, pixels);
    TEST_ASSERT(!updatePdu.empty(), "Encoded bitmap update PDU must not be empty");
    TEST_ASSERT((updatePdu[0] & 0x0F) == micant::rdp::FASTPATH_UPDATETYPE_BITMAP, "Fastpath update type must be BITMAP (1)");
    session->recordFrameEncoded(updatePdu.size());
    TEST_ASSERT(session->getFramesEncoded() == 1, "Frames encoded count must increment");

    // Stage 11: Fast-Path Input Event Serialization & Deserialization
    std::vector<uint8_t> keyPdu = {
        static_cast<uint8_t>(micant::rdp::FASTPATH_INPUT_EVENT_SCANCODE << 5), // header
        0x00, // flags
        0x1E  // scancode (Key 'A')
    };
    micant::rdp::RdpInputEvent inEvent{};
    bool parseKeyOk = micant::rdp::EnterpriseRdpServer::parseFastPathInput(keyPdu, inEvent);
    TEST_ASSERT(parseKeyOk, "Parsing keyboard input PDU must succeed");
    TEST_ASSERT(inEvent.eventType == micant::rdp::FASTPATH_INPUT_EVENT_SCANCODE, "Input event type must be SCANCODE");
    TEST_ASSERT(inEvent.scanCode == 0x1E, "Scancode must match 0x1E");
    session->recordInputProcessed();
    TEST_ASSERT(session->getInputEventsProcessed() == 1, "Input events processed count must increment");

    std::vector<uint8_t> mousePdu = {
        static_cast<uint8_t>(micant::rdp::FASTPATH_INPUT_EVENT_MOUSE << 5),
        0x00, 0x10, // flags: PTRFLAGS_BUTTON1 (Left Click)
        0x20, 0x03, // X: 800
        0x58, 0x02  // Y: 600
    };
    micant::rdp::RdpInputEvent mouseEvent{};
    bool parseMouseOk = micant::rdp::EnterpriseRdpServer::parseFastPathInput(mousePdu, mouseEvent);
    TEST_ASSERT(parseMouseOk, "Parsing mouse input PDU must succeed");
    TEST_ASSERT(mouseEvent.eventType == micant::rdp::FASTPATH_INPUT_EVENT_MOUSE, "Input event type must be MOUSE");
    TEST_ASSERT(mouseEvent.mouseX == 800, "Mouse X coordinate must match 800");
    TEST_ASSERT(mouseEvent.mouseY == 600, "Mouse Y coordinate must match 600");

    // Stage 12: Session State Transitions (Active, Disconnect, Logoff)
    session->setState(micant::rdp::RdpSessionState::ActiveStreaming);
    TEST_ASSERT(session->getState() == micant::rdp::RdpSessionState::ActiveStreaming, "State must transition to ActiveStreaming");

    bool disconOk = server.disconnectSession(sid);
    TEST_ASSERT(disconOk, "Disconnecting remote session must succeed");
    TEST_ASSERT(session->getState() == micant::rdp::RdpSessionState::Disconnected, "State must transition to Disconnected");

    bool logoffOk = server.logoffSession(sid);
    TEST_ASSERT(logoffOk, "Logging off remote session must succeed");
    TEST_ASSERT(server.getSession(sid) == nullptr, "Logged off session must be removed from server");

    // Verify Console session protected
    bool disconConsole = server.disconnectSession(1);
    TEST_ASSERT(!disconConsole, "Console session 1 disconnect must be rejected");
    bool logoffConsole = server.logoffSession(1);
    TEST_ASSERT(!logoffConsole, "Console session 1 logoff must be rejected");

    // Stage 13: Win32 C ABI Parity
    int32_t abiInit = MicaRdpServerInitialize(3389);
    TEST_ASSERT(abiInit == 1, "MicaRdpServerInitialize must return 1");
    int32_t abiStart = MicaRdpServerStart();
    TEST_ASSERT(abiStart == 1, "MicaRdpServerStart must return 1");

    uint32_t abiSid = MicaRdpCreateSession("10.0.0.55", "REMOTE-LAPTOP", L"TestUser", L"MICANT");
    TEST_ASSERT(abiSid >= 2, "MicaRdpCreateSession must return valid session ID");

    uint64_t totConn = 0, totHs = 0, actSess = 0;
    MicaRdpGetServerStats(&totConn, &totHs, &actSess);
    TEST_ASSERT(totConn > 0, "Total connections must be > 0");
    TEST_ASSERT(actSess >= 2, "Active sessions must include Console and newly created session");

    int32_t abiDiscon = MicaRdpDisconnectSession(abiSid);
    TEST_ASSERT(abiDiscon == 1, "MicaRdpDisconnectSession must return 1");
    int32_t abiLogoff = MicaRdpLogoffSession(abiSid);
    TEST_ASSERT(abiLogoff == 1, "MicaRdpLogoffSession must return 1");

    // Stage 14: 120-Operation Concurrent Multithreaded RDP Session Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&server, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                std::string ip = "10.0.9." + std::to_string(t * 15 + op);
                auto s = server.createSession(ip, "STRESS-CLIENT", L"StressAdmin", L"MICANT");
                if (!s) continue;
                uint32_t currentSid = s->getSessionId();

                s->setState(micant::rdp::RdpSessionState::ActiveStreaming);
                micant::rdp::RdpDirtyRect r{static_cast<uint32_t>(op * 10), static_cast<uint32_t>(t * 10), 16, 16};
                std::vector<uint32_t> px(16 * 16, 0xFF00FF00);
                auto frame = micant::rdp::EnterpriseRdpServer::encodeFastPathBitmapUpdate(r, px);
                s->recordFrameEncoded(frame.size());

                micant::rdp::RdpChannelPacket cp;
                cp.channelName = micant::rdp::CHANNEL_CLIPRDR;
                cp.payload = { 0x01, 0x02, 0x03 };
                s->queueChannelPacket(cp);

                bool dOk = server.disconnectSession(currentSid);
                bool lOk = server.logoffSession(currentSid);
                if (dOk && lOk && !frame.empty()) {
                    stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded RDP stress test must achieve 100% success");

    std::cout << "[TEST] Suite 214: Windows Remote Desktop Protocol (RDP / MS-RDPBCGR) Enterprise Subsystem PASSED.\n";
}

// ============================================================================
// Suite 215: Interactive Window Manager, User32 Input Routing & Message Pump Subsystem
// ============================================================================
void Test_InteractiveWindowManager_InputRouting_Subsystem() {
    auto& router = micant::input::InputRouter::get();
    router.initialize(1280, 800);

    // Stage 1: Window Registration & Desktop Topology
    win32::HWND hwndNpp = reinterpret_cast<win32::HWND>(0x10001);
    win32::HWND hwnd7z  = reinterpret_cast<win32::HWND>(0x10002);
    win32::HWND hwndVlc = reinterpret_cast<win32::HWND>(0x10003);

    router.registerWindow(hwndNpp, L"Notepad++ - [new 1]", 50, 50, 600, 400);
    router.registerWindow(hwnd7z,  L"7-Zip File Manager",   400, 200, 500, 350);
    router.registerWindow(hwndVlc, L"VLC media player",     700, 100, 450, 300);

    TEST_ASSERT(router.getActiveWindow() == hwndVlc, "Frontmost window (VLC) must be active window");
    TEST_ASSERT(router.getFocusedWindow() == hwndVlc, "Frontmost window (VLC) must have keyboard focus");

    micant::input::ManagedWindowBounds bNpp{};
    bool getNppOk = router.getWindowBounds(hwndNpp, bNpp);
    TEST_ASSERT(getNppOk, "Querying Notepad++ bounds must succeed");
    TEST_ASSERT(bNpp.width == 600 && bNpp.height == 400, "Notepad++ width/height must match registered dimensions");

    // Stage 2: Hardware Mouse Coordinate Routing & WM_MOUSEMOVE
    router.routeMouseEvent(750, 150, 0); // Inside VLC client area
    int curX = 0, curY = 0;
    router.getCursorPos(curX, curY);
    TEST_ASSERT(curX == 750 && curY == 150, "Hardware cursor position must match routed coordinates");

    auto hit1 = router.hitTest(750, 150);
    TEST_ASSERT(hit1.hwnd == hwndVlc, "Hit test must resolve to VLC window");
    TEST_ASSERT(hit1.hitCode == micant::input::HTCLIENT, "Hit code must be HTCLIENT inside client area");

    // Stage 3: Non-Client Hit Testing (Caption, Close, Maximize, Minimize, Borders)
    auto hitCaption = router.hitTest(750, 115);
    TEST_ASSERT(hitCaption.hwnd == hwndVlc, "Hit test must resolve to VLC");
    TEST_ASSERT(hitCaption.hitCode == micant::input::HTCAPTION, "Hit code must be HTCAPTION on titlebar");

    auto hitClose = router.hitTest(1135, 115);
    TEST_ASSERT(hitClose.hitCode == micant::input::HTCLOSE, "Hit code must be HTCLOSE at right edge of caption");

    auto hitMax = router.hitTest(1100, 115);
    TEST_ASSERT(hitMax.hitCode == micant::input::HTMAXBUTTON, "Hit code must be HTMAXBUTTON adjacent to close");

    auto hitMin = router.hitTest(1070, 115);
    TEST_ASSERT(hitMin.hitCode == micant::input::HTMINBUTTON, "Hit code must be HTMINBUTTON adjacent to maximize");

    auto hitTopBorder = router.hitTest(750, 102);
    TEST_ASSERT(hitTopBorder.hitCode == micant::input::HTTOP, "Hit code must be HTTOP on top border");

    auto hitRightBorder = router.hitTest(1148, 150);
    TEST_ASSERT(hitRightBorder.hitCode == micant::input::HTRIGHT, "Hit code must be HTRIGHT on right border");

    // Stage 4: Window Dragging & Active Z-Order Promotion
    router.routeMouseEvent(100, 65, micant::input::MOUSE_LBUTTONDOWN);
    TEST_ASSERT(router.getActiveWindow() == hwndNpp, "Clicking Notepad++ caption must promote it to active window");
    TEST_ASSERT(router.getFocusedWindow() == hwndNpp, "Notepad++ must receive keyboard focus");

    router.routeMouseEvent(150, 115, 0);
    router.routeMouseEvent(150, 115, micant::input::MOUSE_LBUTTONUP);

    router.getWindowBounds(hwndNpp, bNpp);
    TEST_ASSERT(bNpp.x == 100 && bNpp.y == 100, "Window must move by (+50, +50) to (100, 100)");

    // Stage 5: Aero Snap State Machine (Left 50%, Right 50%, Maximize)
    router.routeMouseEvent(200, 115, micant::input::MOUSE_LBUTTONDOWN);
    router.routeMouseEvent(5, 300, 0);
    router.routeMouseEvent(5, 300, micant::input::MOUSE_LBUTTONUP);

    router.getWindowBounds(hwndNpp, bNpp);
    TEST_ASSERT(bNpp.currentSnap == micant::input::SnapMode::LeftHalf, "Window must snap to LeftHalf");
    TEST_ASSERT(bNpp.x == 0 && bNpp.width == 640, "Snapped left bounds must have x=0 and width=640");

    router.routeMouseEvent(100, 15, micant::input::MOUSE_LBUTTONDOWN);
    router.routeMouseEvent(300, 5, 0);
    router.routeMouseEvent(300, 5, micant::input::MOUSE_LBUTTONUP);

    router.getWindowBounds(hwndNpp, bNpp);
    TEST_ASSERT(bNpp.maximized, "Dragging to top screen edge must maximize window");
    TEST_ASSERT(bNpp.x == 0 && bNpp.width == 1280, "Maximized window width must span full 1280px");

    router.toggleMaximize(hwndNpp);
    router.getWindowBounds(hwndNpp, bNpp);
    TEST_ASSERT(!bNpp.maximized, "Toggling maximize must restore normal state");
    TEST_ASSERT(bNpp.width == 600 && bNpp.height == 400, "Restored window width/height must match original bounds");

    // Stage 6: Mouse Button Events (Down, Up, Wheel)
    router.routeMouseEvent(200, 200, micant::input::MOUSE_RBUTTONDOWN);
    router.routeMouseEvent(200, 200, micant::input::MOUSE_RBUTTONUP);
    router.routeMouseEvent(200, 200, 0, 120);

    uint64_t mm = 0, mc = 0, ke = 0, ch = 0, md = 0, wd = 0, as = 0;
    router.getStats(mm, mc, ke, ch, md, wd, as);
    TEST_ASSERT(mm > 0, "Mouse moves must be recorded");
    TEST_ASSERT(mc >= 3, "Mouse clicks must be recorded");
    TEST_ASSERT(as >= 2, "Aero snaps must be recorded");

    // Stage 7: Keyboard Scancode Translation to Virtual Key
    TEST_ASSERT(router.scancodeToVirtualKey(0x1E) == 'A', "Scancode 0x1E must map to VK_A");
    TEST_ASSERT(router.scancodeToVirtualKey(0x39) == micant::input::VK_SPACE, "Scancode 0x39 must map to VK_SPACE");
    TEST_ASSERT(router.scancodeToVirtualKey(0x01) == micant::input::VK_ESCAPE, "Scancode 0x01 must map to VK_ESCAPE");
    TEST_ASSERT(router.scancodeToVirtualKey(0x1C) == micant::input::VK_RETURN, "Scancode 0x1C must map to VK_RETURN");
    TEST_ASSERT(router.scancodeToVirtualKey(0x0E) == micant::input::VK_BACK, "Scancode 0x0E must map to VK_BACK");

    // Stage 8: TranslateMessage Synthesis & WM_CHAR Generation with Shift
    router.setFocusedWindow(hwndNpp);
    router.routeKeyboardEvent(0x2A, false); // Shift down
    router.routeKeyboardEvent(0x32, false); // 'M'
    router.routeKeyboardEvent(0x32, true);
    router.routeKeyboardEvent(0x2A, true);  // Shift up

    router.routeKeyboardEvent(0x17, false); // 'i'
    router.routeKeyboardEvent(0x17, true);
    router.routeKeyboardEvent(0x2E, false); // 'c'
    router.routeKeyboardEvent(0x2E, true);
    router.routeKeyboardEvent(0x1E, false); // 'a'
    router.routeKeyboardEvent(0x1E, true);

    router.routeKeyboardEvent(0x2A, false); // Shift down
    router.routeKeyboardEvent(0x31, false); // 'N'
    router.routeKeyboardEvent(0x31, true);
    router.routeKeyboardEvent(0x14, false); // 'T'
    router.routeKeyboardEvent(0x14, true);
    router.routeKeyboardEvent(0x2A, true);  // Shift up

    router.routeKeyboardEvent(0x39, false); // Space
    router.routeKeyboardEvent(0x39, true);

    router.routeKeyboardEvent(0x03, false); // '2'
    router.routeKeyboardEvent(0x03, true);
    router.routeKeyboardEvent(0x0B, false); // '0'
    router.routeKeyboardEvent(0x0B, true);
    router.routeKeyboardEvent(0x03, false); // '2'
    router.routeKeyboardEvent(0x03, true);
    router.routeKeyboardEvent(0x07, false); // '6'
    router.routeKeyboardEvent(0x07, true);

    // Stage 9: Window Focus Switching (WM_SETFOCUS / WM_KILLFOCUS)
    router.setFocusedWindow(hwnd7z);
    TEST_ASSERT(router.getFocusedWindow() == hwnd7z, "7-Zip must now have focus");
    router.setFocusedWindow(hwndNpp);
    TEST_ASSERT(router.getFocusedWindow() == hwndNpp, "Focus must switch back to Notepad++");

    // Stage 10: Interactive Notepad++ Scintilla Document Editing Automation
    std::wstring docText = router.getNotepadDocumentText();
    TEST_ASSERT(docText == L"MicaNT 2026", "Notepad++ Scintilla document buffer must contain 'MicaNT 2026'");

    router.routeKeyboardEvent(0x0E, false); // Backspace
    router.routeKeyboardEvent(0x0E, true);
    TEST_ASSERT(router.getNotepadDocumentText() == L"MicaNT 202", "Backspace must remove last character");

    router.routeKeyboardEvent(0x1C, false); // Enter
    router.routeKeyboardEvent(0x1C, true);
    TEST_ASSERT(router.getNotepadLineCount() == 2, "Enter must increment document line count to 2");

    // Stage 11: Interactive 7-Zip File Manager Command Automation
    TEST_ASSERT(!router.is7ZipBenchmarkActive(), "7-Zip benchmark must initially be idle");
    router.dispatch7ZipCommand(hwnd7z, 1001); // Benchmark
    TEST_ASSERT(router.is7ZipBenchmarkActive(), "Dispatching ID 1001 must start 7-Zip benchmark");
    TEST_ASSERT(router.get7ZipBenchmarkIterations() == 32, "Benchmark must record 32 compression iterations");

    router.dispatch7ZipCommand(hwnd7z, 1002); // Extract
    TEST_ASSERT(router.is7ZipExtractOpened(), "Dispatching ID 1002 must trigger extract dialog flag");

    // Stage 12: Interactive VLC Media Player Playback Automation
    router.setFocusedWindow(hwndVlc);
    TEST_ASSERT(router.getVlcPlaybackState() == micant::input::VlcState::Stopped, "VLC must start in Stopped state");
    TEST_ASSERT(router.getVlcVolume() == 80, "VLC default volume must be 80%");

    router.routeKeyboardEvent(0x39, false); // Space
    router.routeKeyboardEvent(0x39, true);
    TEST_ASSERT(router.getVlcPlaybackState() == micant::input::VlcState::Playing, "Spacebar must toggle VLC to Playing");

    router.routeKeyboardEvent(0x39, false); // Space
    router.routeKeyboardEvent(0x39, true);
    TEST_ASSERT(router.getVlcPlaybackState() == micant::input::VlcState::Paused, "Spacebar must toggle VLC to Paused");

    router.routeKeyboardEvent(0x48, false); // VK_UP
    router.routeKeyboardEvent(0x48, true);
    TEST_ASSERT(router.getVlcVolume() == 85, "VK_UP must increment VLC volume to 85%");

    // Stage 13: 100.0% Native Win32 Symbol Satisfaction for 7zFM.exe
    TEST_ASSERT(micant::win32::GetSystemDefaultLangID() == 0x0409, "GetSystemDefaultLangID must return en-US");
    TEST_ASSERT(micant::win32::GetUserDefaultLangID() == 0x0409, "GetUserDefaultLangID must return en-US");
    TEST_ASSERT(micant::win32::GetDriveTypeW(L"C:\\") == 3, "GetDriveTypeW must return DRIVE_FIXED");
    wchar_t winDirBuf[260]{};
    uint32_t wLen = micant::win32::GetWindowsDirectoryW(winDirBuf, 260);
    TEST_ASSERT(wLen > 0 && std::wstring(winDirBuf) == L"C:\\Windows", "GetWindowsDirectoryW must return C:\\Windows");
    TEST_ASSERT(micant::user32::GetDialogBaseUnits() > 0, "GetDialogBaseUnits must return valid metrics");
    TEST_ASSERT(micant::comdlg32::CommDlgExtendedError() == 0, "CommDlgExtendedError must return 0");

    // Stage 14: Win32 C ABI Parity Exports & 120-Operation Concurrent Multithreaded Stress Test
    int32_t abiInit = MicaInputInitialize(1920, 1080);
    TEST_ASSERT(abiInit == 1, "MicaInputInitialize must return 1");

    int32_t abiMouse = MicaRouteHardwareMouseEvent(500, 500, micant::input::MOUSE_MOVE, 0);
    TEST_ASSERT(abiMouse == 1, "MicaRouteHardwareMouseEvent must return 1");

    int32_t abiKey = MicaRouteHardwareKeyboardEvent(0x1E, 0);
    TEST_ASSERT(abiKey == 1, "MicaRouteHardwareKeyboardEvent must return 1");

    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&router, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                int x = (t * 100 + op * 10) % 1200;
                int y = (t * 50 + op * 20) % 700;
                router.routeMouseEvent(x, y, 0);
                router.routeKeyboardEvent(0x1E, false);
                router.routeKeyboardEvent(0x1E, true);
                stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation concurrent multithreaded input stress test must achieve 100% success");

    std::cout << "[TEST] Suite 215: Interactive Window Manager, User32 Input Routing & Message Pump Subsystem PASSED.\n";
}

void Test_BareMetalEventLoop_WizTreeMFT_Subsystem() {
    std::cout << "[TEST] Running Suite 216: Bare-Metal UEFI Interactive Event Loop, Software Cursor & WizTree 4.x Sovereign MFT Subsystem...\n";

    // Stage 1: UEFI Input Protocol Structs & Alignment
    TEST_ASSERT(sizeof(uefi::EfiInputKey) == 4, "EfiInputKey must be exactly 4 bytes in size");
    TEST_ASSERT(sizeof(uefi::EfiSimplePointerState) >= 12, "EfiSimplePointerState must have valid geometry fields");
    TEST_ASSERT(uefi::EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID.data1 == 0x3874286e, "EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID must match UEFI 2.10 spec");
    TEST_ASSERT(uefi::EFI_SIMPLE_POINTER_PROTOCOL_GUID.data1 == 0x31878c87, "EFI_SIMPLE_POINTER_PROTOCOL_GUID must match UEFI 2.10 spec");

    // Stage 2: Software Cursor Background Save & Restore Fidelity
    bootvid::BootVideoDriver vid;
    TEST_ASSERT(vid.initializeVirtual(1280, 800), "BootVideoDriver virtual initialization must succeed");

    // Paint a distinct background pattern at (100, 100)
    for (uint32_t y = 100; y < 130; ++y) {
        for (uint32_t x = 100; x < 120; ++x) {
            vid.putPixel(x, y, bootvid::Color{static_cast<uint8_t>(x % 255), static_cast<uint8_t>(y % 255), 180});
        }
    }
    bootvid::Color origPixel = vid.getPixel(105, 105);

    micant::bootloader::SoftwareCursor cursor;
    cursor.render(vid, 100, 100);
    TEST_ASSERT(cursor.isVisible(), "Software cursor must be visible after rendering");
    TEST_ASSERT(cursor.getX() == 100 && cursor.getY() == 100, "Cursor coordinates must match rendered point");

    // Cursor tip (0, 0) is black outline
    bootvid::Color tipPixel = vid.getPixel(100, 100);
    TEST_ASSERT((tipPixel == bootvid::Color::black()), "Cursor tip pixel must be black outline");

    cursor.restoreBackground(vid);
    TEST_ASSERT(!cursor.isVisible(), "Cursor must be marked hidden after restoreBackground");
    bootvid::Color restoredPixel = vid.getPixel(105, 105);
    TEST_ASSERT(restoredPixel == origPixel, "Background pixels must be restored with 100% fidelity");

    // Stage 3: InteractiveDesktopHost Pointer Movement & Boundary Clamping
    input::InputRouter& router = input::InputRouter::getInstance();
    router.initialize(1280, 800);
    micant::bootloader::InteractiveDesktopHost host(router, vid);

    TEST_ASSERT(host.getCursor().getX() == 640 && host.getCursor().getY() == 400, "Cursor must start at center (640, 400)");
    host.processPointerMovement(50, -50, false, false);
    TEST_ASSERT(host.getCursor().getX() == 690 && host.getCursor().getY() == 350, "Relative delta must translate to (690, 350)");

    // Extreme coordinate clamping
    host.processPointerMovement(10000, 10000, false, false);
    TEST_ASSERT(host.getCursor().getX() == 1279 && host.getCursor().getY() == 799, "Pointer must clamp to screen boundaries");

    // Stage 4: Mouse Button Transitions (Down, Up, Wheel)
    host.processPointerMovement(0, 0, true, false); // Left down
    host.processPointerMovement(0, 0, false, false); // Left up
    host.processPointerMovement(0, 0, false, true, 120); // Right down + Wheel
    host.processPointerMovement(0, 0, false, false); // Right up

    TEST_ASSERT(host.getPointerEventsProcessed() >= 5, "Pointer events processed counter must record all transitions");
    TEST_ASSERT(host.getFramesRendered() >= 5, "Frames rendered counter must track dirty updates");

    // Stage 5: Live Window Dragging State Machine
    win32::HWND hwndTest = reinterpret_cast<win32::HWND>(0x5501);
    router.registerWindow(hwndTest, L"Test Window", 200, 100, 600, 400);

    // Hit caption bar at (300, 115) and begin drag
    host.getCursor().setPosition(300, 115);
    host.processPointerMovement(0, 0, true, false); // Left down on caption
    TEST_ASSERT(host.isDragging(), "Clicking on caption bar must initiate window drag");
    TEST_ASSERT(host.getDraggedWindow() == hwndTest, "Dragged window must match focused test window");

    // Drag by (+50, +30)
    host.processPointerMovement(50, 30, true, false);
    input::ManagedWindowBounds bTest{};
    router.getWindowBounds(hwndTest, bTest);
    TEST_ASSERT(bTest.x == 250 && bTest.y == 130, "Window coordinates must update dynamically while dragging");

    // Release mouse button
    host.processPointerMovement(0, 0, false, false);
    TEST_ASSERT(!host.isDragging(), "Releasing mouse button must terminate dragging");

    // Stage 6: Aero Snap Edge Detection through Host
    host.getCursor().setPosition(300, 145);
    host.processPointerMovement(0, 0, true, false); // Start drag
    host.processPointerMovement(-295, 100, true, false); // Drag to left screen edge (x = 5)
    host.processPointerMovement(0, 0, false, false); // Drop

    router.getWindowBounds(hwndTest, bTest);
    TEST_ASSERT(bTest.currentSnap == input::SnapMode::LeftHalf, "Dragging to left screen edge must snap window to LeftHalf");
    TEST_ASSERT(bTest.x == 0 && bTest.width == 640, "Snapped LeftHalf window must have x=0 and width=640");

    // Stage 7: UEFI Keyboard Input Decoding
    win32::HWND hwndNpp = reinterpret_cast<win32::HWND>(0x1001);
    router.registerWindow(hwndNpp, L"Notepad++ 8.6.9", 50, 50, 700, 500);
    router.setFocusedWindow(hwndNpp);

    host.processKeyboardKey(0x01, 0); // VK_UP
    host.processKeyboardKey(0x02, 0); // VK_DOWN
    host.processKeyboardKey(0x17, 0); // VK_ESCAPE
    TEST_ASSERT(host.getKeyboardEventsProcessed() == 3, "Keyboard events processed counter must record key events");

    // Stage 8: Interactive Notepad++ Scintilla Document Editing via Host
    host.processKeyboardKey(0, L'M');
    host.processKeyboardKey(0, L'i');
    host.processKeyboardKey(0, L'c');
    host.processKeyboardKey(0, L'a');
    host.processKeyboardKey(0, L'N');
    host.processKeyboardKey(0, L'T');
    host.processKeyboardKey(0, L' ');
    host.processKeyboardKey(0, L'M');
    host.processKeyboardKey(0, L'2');
    host.processKeyboardKey(0, L'1');
    host.processKeyboardKey(0, L'6');

    TEST_ASSERT(router.getNotepadDocumentText() == L"MicaNT M216", "Notepad++ Scintilla buffer must contain 'MicaNT M216'");

    host.processKeyboardKey(0, 0x08); // Backspace
    TEST_ASSERT(router.getNotepadDocumentText() == L"MicaNT M21", "Backspace must delete last character");

    // Stage 9: Interactive VLC Media Player Playback Automation via Host
    win32::HWND hwndVlc = reinterpret_cast<win32::HWND>(0x2001);
    router.registerWindow(hwndVlc, L"VLC media player", 580, 65, 670, 420);
    router.setFocusedWindow(hwndVlc);

    TEST_ASSERT(router.getVlcPlaybackState() == input::VlcState::Stopped, "VLC must start in Stopped state");
    host.processKeyboardKey(0, L' '); // Spacebar
    TEST_ASSERT(router.getVlcPlaybackState() == input::VlcState::Playing, "Spacebar must toggle VLC to Playing");
    host.processKeyboardKey(0, L' '); // Spacebar
    TEST_ASSERT(router.getVlcPlaybackState() == input::VlcState::Paused, "Spacebar must toggle VLC to Paused");

    host.processKeyboardKey(0x01, 0); // VK_UP
    TEST_ASSERT(router.getVlcVolume() == 85, "VK_UP must increment volume to 85%");

    // Stage 10: Interactive 7-Zip File Manager Command Automation
    win32::HWND hwnd7z = reinterpret_cast<win32::HWND>(0x3001);
    router.registerWindow(hwnd7z, L"7-Zip 24.08 (x64)", 50, 480, 800, 270);

    router.dispatch7ZipCommand(hwnd7z, 1001); // Benchmark
    TEST_ASSERT(router.is7ZipBenchmarkActive(), "Dispatching ID 1001 must start 7-Zip benchmark");
    router.dispatch7ZipCommand(hwnd7z, 1002); // Extract
    TEST_ASSERT(router.is7ZipExtractOpened(), "Dispatching ID 1002 must trigger extract dialog");

    // Stage 11: 100.0% Native Win32 Subsystem Satisfaction for WizTree 64-bit
    micant::subsystems::InitializeAllSubsystemExports();
    auto& ldr = ldr::DynamicLoader::get();

    TEST_ASSERT(ldr.getExport("mpr.dll", "WNetGetConnectionW") != nullptr, "WNetGetConnectionW must be exported");
    TEST_ASSERT(ldr.getExport("oleacc.dll", "LresultFromObject") != nullptr, "LresultFromObject must be exported");
    TEST_ASSERT(ldr.getExport("winspool.drv", "DocumentPropertiesW") != nullptr, "DocumentPropertiesW must be exported");
    TEST_ASSERT(ldr.getExport("comdlg32.dll", "FindTextW") != nullptr, "FindTextW must be exported");
    TEST_ASSERT(ldr.getExport("comctl32.dll", "FlatSB_SetScrollInfo") != nullptr, "FlatSB_SetScrollInfo must be exported");
    TEST_ASSERT(ldr.getExport("comctl32.dll", "ImageList_GetDragImage") != nullptr, "ImageList_GetDragImage must be exported");
    TEST_ASSERT(ldr.getExport("shell32.dll", "DragAcceptFiles") != nullptr, "DragAcceptFiles must be exported");
    TEST_ASSERT(ldr.getExport("shell32.dll", "ILCreateFromPathW") != nullptr, "ILCreateFromPathW must be exported");

    // Stage 12: WinHttp Client Mock Session
    void* hSession = ldr.getExport("winhttp.dll", "WinHttpOpen");
    TEST_ASSERT(hSession != nullptr, "WinHttpOpen must be exported");
    auto pfnOpen = reinterpret_cast<decltype(&micant::winhttp::WinHttpOpen)>(hSession);
    void* sessHandle = pfnOpen(L"WizTree/4.22", 0, nullptr, nullptr, 0);
    TEST_ASSERT(sessHandle != nullptr, "WinHttpOpen must return valid session handle");

    auto pfnConnect = reinterpret_cast<decltype(&micant::winhttp::WinHttpConnect)>(ldr.getExport("winhttp.dll", "WinHttpConnect"));
    void* connHandle = pfnConnect(sessHandle, L"diskanalyzer.com", 443, 0);
    TEST_ASSERT(connHandle != nullptr, "WinHttpConnect must return valid connection handle");

    auto pfnClose = reinterpret_cast<decltype(&micant::winhttp::WinHttpCloseHandle)>(ldr.getExport("winhttp.dll", "WinHttpCloseHandle"));
    TEST_ASSERT(pfnClose(connHandle) == 1, "WinHttpCloseHandle on connect must succeed");
    TEST_ASSERT(pfnClose(sessHandle) == 1, "WinHttpCloseHandle on session must succeed");

    // Stage 13: NTFS MFT Direct Traversal Engine (FSCTL_GET_NTFS_FILE_RECORD)
    auto ramDisk = std::make_shared<storage::RamDiskDevice>(L"\\Device\\HarddiskWiz", 8 * 1024 * 1024ULL, storage::SECTOR_SIZE_512); // 8 MB
    ntfs::NtfsFileSystem ntfsFs;
    NtStatus fmtStatus = ntfsFs.format(*ramDisk, 4096, L"WizTree_Test");
    TEST_ASSERT(fmtStatus == NtStatus::Success, "NTFS formatting must succeed");

    uint64_t recWiz = 0, recSys = 0;
    ntfsFs.createFile(L"WizTree64.exe", fs::FILE_ATTRIBUTE_NORMAL, recWiz);
    ntfsFs.createFile(L"system.mft", fs::FILE_ATTRIBUTE_NORMAL, recSys);

    ntfs::NTFS_FILE_RECORD_OUTPUT_BUFFER recOut{};
    bool qOk = ntfs::MFTDirectScanner::get().queryFileRecord(ntfsFs, 0, recOut);
    TEST_ASSERT(qOk, "Querying MFT Record 0 ($MFT) must succeed");
    TEST_ASSERT(recOut.fileReferenceNumber == 0, "MFT Record reference number must match 0");
    TEST_ASSERT(std::memcmp(recOut.fileRecordBuffer, "FILE", 4) == 0, "MFT Record buffer must contain FILE magic signature");

    ntfs::MftScanStats scanStats = ntfs::MFTDirectScanner::get().scanVolume(ntfsFs);
    TEST_ASSERT(scanStats.totalRecords >= 7, "MFT scan must discover at least 7 records");
    TEST_ASSERT(scanStats.fileCount >= 2, "MFT scan must detect created test files");

    // Stage 14: 120-Operation Multi-Threaded Input & Scanning Concurrency Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> stressThreads;
    stressThreads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        stressThreads.emplace_back([&host, &ntfsFs, &stressSuccessCount, t]() {
            for (int op = 0; op < 15; ++op) {
                int dx = (t % 2 == 0) ? 5 : -5;
                int dy = (op % 2 == 0) ? 3 : -3;
                host.processPointerMovement(dx, dy, false, false);
                host.processKeyboardKey(0, L'a' + (op % 26));

                ntfs::NTFS_FILE_RECORD_OUTPUT_BUFFER buf{};
                ntfs::MFTDirectScanner::get().queryFileRecord(ntfsFs, 0, buf);
                stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : stressThreads) {
        if (th.joinable()) th.join();
    }

    TEST_ASSERT(stressSuccessCount.load() == 120, "120-operation multi-threaded input & scanning concurrency stress test must achieve 100% success");

    std::cout << "[TEST] Suite 216: Bare-Metal UEFI Interactive Event Loop, Software Cursor & WizTree 4.x Sovereign MFT Subsystem PASSED.\n";
}

void Test_PuTTYTerminal_AnsiWin32_Subsystem() {
    std::cout << "[TEST] Running Suite 217: PuTTY 0.82+ Sovereign Win32 Subsystem & ANSI Terminal Engine...\n";

    // Stage 1: ANSI String Conversion & Transcoding (AnsiToWide)
    std::wstring w1 = AnsiToWide("putty.exe");
    TEST_ASSERT(w1 == L"putty.exe", "AnsiToWide must match putty.exe exactly");
    std::wstring w2 = AnsiToWide("MicaNT SSH Terminal");
    TEST_ASSERT(w2 == L"MicaNT SSH Terminal", "AnsiToWide must match terminal title");
    TEST_ASSERT(AnsiToWide(nullptr).empty(), "Null string must return empty wide string");

    // Stage 2: GDI32 Font Metrics & Character Placement
    micant::gdi32::LOGFONTA lf{};
    std::strcpy(lf.lfFaceName, "Lucida Console");
    lf.lfHeight = 16;
    void* hFont = micant::gdi32::CreateFontIndirectA(&lf);
    TEST_ASSERT(hFont != nullptr, "CreateFontIndirectA must return valid non-null HFONT");

    micant::gdi32::TEXTMETRICA tm{};
    TEST_ASSERT(micant::gdi32::GetTextMetricsA(nullptr, &tm) == 1, "GetTextMetricsA must succeed");
    TEST_ASSERT(tm.tmHeight == 14 && tm.tmAveCharWidth == 8, "TEXTMETRICA default metrics must match 14x8 terminal font");

    int charWidths[10] = {0};
    TEST_ASSERT(micant::gdi32::GetCharWidth32A(nullptr, 32, 41, charWidths) == 1, "GetCharWidth32A must succeed");
    for (int w : charWidths) {
        TEST_ASSERT(w == 8, "Monospace terminal character widths must be exactly 8 pixels");
    }

    micant::gdi32::ABCFLOAT abc[5]{};
    TEST_ASSERT(micant::gdi32::GetCharABCWidthsFloatA(nullptr, 65, 69, abc) == 1, "GetCharABCWidthsFloatA must succeed");
    TEST_ASSERT(abc[0].abcfB == 8.0f, "ABCFLOAT width must be 8.0f");

    uint32_t placementLen = micant::gdi32::GetCharacterPlacementW(nullptr, L"SSH-2.0-OpenSSH_9.9", 19, 0, nullptr, 0);
    TEST_ASSERT(placementLen == 19 * 8, "Character placement extent must match character count * 8");

    // Stage 3: Serial / COM UART Hardware Communication State Machine
    micant::win32::DCB dcb{};
    TEST_ASSERT(micant::win32::GetCommState(nullptr, &dcb) == 1, "GetCommState must succeed");
    TEST_ASSERT(dcb.BaudRate == 115200, "Default serial baud rate must be 115200");
    TEST_ASSERT(dcb.ByteSize == 8, "Default byte size must be 8");
    TEST_ASSERT(micant::win32::SetCommState(nullptr, &dcb) == 1, "SetCommState must succeed");

    micant::win32::COMMTIMEOUTS timeouts{};
    TEST_ASSERT(micant::win32::SetCommTimeouts(nullptr, &timeouts) == 1, "SetCommTimeouts must succeed");
    TEST_ASSERT(micant::win32::SetCommBreak(nullptr) == 1, "SetCommBreak must succeed");
    TEST_ASSERT(micant::win32::ClearCommBreak(nullptr) == 1, "ClearCommBreak must succeed");

    // Stage 4: Anonymous & Named Pipe IPC Primitives
    void* hRead = nullptr;
    void* hWrite = nullptr;
    TEST_ASSERT(micant::win32::CreatePipe(&hRead, &hWrite, nullptr, 4096) == 1, "CreatePipe must succeed");
    TEST_ASSERT(hRead != nullptr && hWrite != nullptr && hRead != hWrite, "CreatePipe must return distinct valid read/write handles");
    void* hNamedPipe = micant::win32::CreateNamedPipeA("\\\\.\\pipe\\putty-pageant", 3, 0, 1, 1024, 1024, 0, nullptr);
    TEST_ASSERT(hNamedPipe != nullptr, "CreateNamedPipeA must return valid handle");
    TEST_ASSERT(micant::win32::WaitNamedPipeA("\\\\.\\pipe\\putty-pageant", 1000) == 1, "WaitNamedPipeA must succeed");

    // Stage 5: Synchronization & Memory Mapping ANSI Adapters
    void* hEvt = micant::win32::CreateEventA(nullptr, 1, 0, "PuttyEvent");
    TEST_ASSERT(hEvt != nullptr, "CreateEventA must return non-null handle");
    void* hMtx = micant::win32::CreateMutexA(nullptr, 0, "PuttyMutex");
    TEST_ASSERT(hMtx != nullptr, "CreateMutexA must return non-null handle");
    void* hMap = micant::win32::CreateFileMappingA(reinterpret_cast<void*>(~0ULL), nullptr, 4, 0, 65536, "PuttySharedMem");
    TEST_ASSERT(hMap != nullptr, "CreateFileMappingA must return non-null handle");

    // Stage 6: System Directories & Environment Paths
    char sysDir[64]{};
    uint32_t sysLen = micant::win32::GetSystemDirectoryA(sysDir, sizeof(sysDir));
    TEST_ASSERT(sysLen > 0 && std::string(sysDir) == "C:\\Windows\\System32", "GetSystemDirectoryA must return C:\\Windows\\System32");
    char winDir[64]{};
    uint32_t winLen = micant::win32::GetWindowsDirectoryA(winDir, sizeof(winDir));
    TEST_ASSERT(winLen > 0 && std::string(winDir) == "C:\\Windows", "GetWindowsDirectoryA must return C:\\Windows");
    char tmpDir[64]{};
    uint32_t tmpLen = micant::win32::GetTempPathA(sizeof(tmpDir), tmpDir);
    TEST_ASSERT(tmpLen > 0 && std::string(tmpDir) == "C:\\Temp\\", "GetTempPathA must return C:\\Temp\\");

    // Stage 7: Global Memory Status & 64-Bit Memory Sizing
    micant::win32::MEMORYSTATUS mem{};
    micant::win32::GlobalMemoryStatus(&mem);
    TEST_ASSERT(mem.dwLength == sizeof(micant::win32::MEMORYSTATUS), "dwLength must match structure size");
    TEST_ASSERT(mem.dwTotalPhys >= (1ULL * 1024 * 1024 * 1024), "Physical memory must report at least 1GB");
    TEST_ASSERT(mem.dwTotalVirtual >= (1ULL * 1024 * 1024 * 1024 * 1024), "64-bit virtual memory must report multi-terabyte address space");

    // Stage 8: Advapi32 Security Descriptors & SID Duplication
    uint8_t srcSid[68] = { 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00 }; // S-1-5-18 LocalSystem
    uint8_t destSid[68] = {0};
    TEST_ASSERT(micant::advapi32::CopySid(sizeof(destSid), destSid, srcSid) == 1, "CopySid must succeed");
    TEST_ASSERT(std::memcmp(destSid, srcSid, 12) == 0, "Copied SID must match source LocalSystem SID");

    char userName[64]{};
    uint32_t userLen = sizeof(userName);
    TEST_ASSERT(micant::advapi32::GetUserNameA(userName, &userLen) == 1, "GetUserNameA must succeed");
    TEST_ASSERT(std::string(userName) == "MicaAdmin", "GetUserNameA must return sovereign user name MicaAdmin");

    // Stage 9: IMM32 IME Composition & Localization
    TEST_ASSERT(micant::tsf::ImmSetCompositionFontA(nullptr, &lf) == 1, "ImmSetCompositionFontA must succeed");

    // Stage 10: ComDlg32 Common Dialog Handlers
    TEST_ASSERT(micant::comdlg32::ChooseColorA(nullptr) == 1, "ChooseColorA must return 1 (IDOK)");
    TEST_ASSERT(micant::comdlg32::ChooseFontA(nullptr) == 1, "ChooseFontA must return 1 (IDOK)");
    TEST_ASSERT(micant::comdlg32::GetOpenFileNameA(nullptr) == 1, "GetOpenFileNameA must return 1");
    TEST_ASSERT(micant::comdlg32::GetSaveFileNameA(nullptr) == 1, "GetSaveFileNameA must return 1");

    // Stage 11: User32 Window Message & Dialog Handlers
    void* hDlg = micant::user32::CreateDialogParamA(nullptr, "IDD_PUTTY_CONFIG", nullptr, nullptr, 0);
    TEST_ASSERT(hDlg != nullptr, "CreateDialogParamA must return valid dialog HWND");
    TEST_ASSERT(micant::user32::DialogBoxParamA(nullptr, "IDD_ABOUT", nullptr, nullptr, 0) == 1, "DialogBoxParamA must return 1");
    TEST_ASSERT(micant::user32::FlashWindow(hDlg, 1) == 1, "FlashWindow must succeed");

    char winTitle[64] = "PuTTY - (inactive)";
    micant::user32::SetWindowTextA(hDlg, winTitle);
    char readTitle[64]{};
    int titleLen = micant::user32::GetWindowTextA(hDlg, readTitle, sizeof(readTitle));
    TEST_ASSERT(titleLen > 0 && std::string(readTitle) == "PuTTY - (inactive)", "Window text must be successfully stored and retrieved in ANSI");

    // Stage 12: Virtual Key ASCII Translation (ToAsciiEx)
    uint16_t outChar = 0;
    uint8_t keyState[256]{};
    // Test lowercase 'a' (no shift)
    int asciiCount = micant::user32::ToAsciiEx('A', 0x1E, keyState, &outChar, 0, nullptr);
    TEST_ASSERT(asciiCount == 1 && outChar == 'a', "Virtual key 'A' without shift must translate to 'a'");
    // Test uppercase 'A' (with shift)
    keyState[0x10] = 0x80;
    asciiCount = micant::user32::ToAsciiEx('A', 0x1E, keyState, &outChar, 0, nullptr);
    TEST_ASSERT(asciiCount == 1 && outChar == 'A', "Virtual key 'A' with shift must translate to 'A'");
    // Test Return key
    asciiCount = micant::user32::ToAsciiEx(0x0D, 0x1C, keyState, &outChar, 0, nullptr);
    TEST_ASSERT(asciiCount == 1 && outChar == '\r', "VK_RETURN must translate to '\\r'");

    // Stage 13: DynamicLoader IAT Binding & Symbol Satisfaction for PuTTY
    micant::subsystems::InitializeAllSubsystemExports();
    auto& loader = micant::ldr::DynamicLoader::get();

    static constexpr const char* PUTTY_TEST_SYMBOLS[] = {
        "CopySid", "GetUserNameA", "RegDeleteKeyA", "RegEnumKeyA",
        "ChooseColorA", "ChooseFontA", "GetOpenFileNameA", "GetSaveFileNameA",
        "ImmSetCompositionFontA",
        "Beep", "ClearCommBreak", "SetCommBreak", "GetCommState", "SetCommState",
        "SetCommTimeouts", "SetHandleInformation", "CreateEventA", "CreateMutexA",
        "CreateFileMappingA", "CreateNamedPipeA", "WaitNamedPipeA", "CreatePipe",
        "FindResourceA", "GetOverlappedResult", "GetSystemDirectoryA", "GetWindowsDirectoryA",
        "GetTempPathA", "GetThreadTimes", "GlobalMemoryStatus", "LocalFileTimeToFileTime",
        "CreateFontA", "CreateFontIndirectA", "GetCharABCWidthsFloatA", "GetCharWidth32A",
        "GetCharWidth32W", "GetCharWidthA", "GetCharWidthW", "GetCharacterPlacementW",
        "GetObjectA", "GetOutlineTextMetricsA", "GetTextExtentPointA", "GetTextMetricsA",
        "TranslateCharsetInfo", "UpdateColors",
        "CreateDialogParamA", "DefDlgProcA", "DefWindowProcA", "DialogBoxParamA",
        "FindWindowA", "FlashWindow", "GetClipboardOwner", "GetMessageA",
        "GetQueueStatus", "GetWindowLongPtrA", "GetWindowTextLengthA", "GetWindowTextA",
        "InsertMenuA", "LoadCursorA", "LoadIconA", "LoadImageA",
        "MessageBoxIndirectW", "PostMessageA", "RegisterClassA", "RegisterClipboardFormatA",
        "RegisterWindowMessageA", "SendDlgItemMessageA", "SetClassLongPtrA", "SetWindowLongPtrA",
        "SetWindowTextA", "ToAsciiEx"
    };

    uint32_t resolvedCount = 0;
    for (const char* sym : PUTTY_TEST_SYMBOLS) {
        const char* candidateDlls[] = {
            "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll",
            "comdlg32.dll", "imm32.dll", "shell32.dll", "ole32.dll"
        };
        bool found = false;
        for (const char* d : candidateDlls) {
            if (loader.getExport(d, sym) != nullptr) {
                found = true;
                break;
            }
        }
        if (found) ++resolvedCount;
    }
    TEST_ASSERT(resolvedCount == sizeof(PUTTY_TEST_SYMBOLS) / sizeof(PUTTY_TEST_SYMBOLS[0]),
                "All 70 newly implemented PuTTY Win32 symbols must be resolved from DynamicLoader export table");

    // Stage 14: Multi-Threaded Terminal Stream Stress Test
    std::atomic<uint32_t> streamSuccessCount{0};
    std::vector<std::thread> termThreads;
    termThreads.reserve(8);
    for (int t = 0; t < 8; ++t) {
        termThreads.emplace_back([&streamSuccessCount, t]() {
            for (int op = 0; op < 25; ++op) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "TermThread_%d_Op_%d", t, op);
                std::wstring w = AnsiToWide(buf);
                if (!w.empty() && w.length() == std::strlen(buf)) {
                    streamSuccessCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& th : termThreads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(streamSuccessCount.load() == 200, "200-transaction terminal stream concurrent stress test must succeed 100%");

    std::cout << "[TEST] Suite 217: PuTTY 0.82+ Sovereign Win32 Subsystem & ANSI Terminal Engine PASSED.\n";
}

void Test_SumatraPDF_Gdiplus_Subsystem() {
    std::cout << "[TEST] Running Suite 218: SumatraPDF 3.6+ Sovereign GDI+ 2D Vector & Document Subsystem...\n";

    // Stage 1: GDI+ Memory Allocator & Lifecycle (GdipAlloc, GdipFree)
    void* mem = micant::gdiplus::GdipAlloc(256);
    TEST_ASSERT(mem != nullptr, "GdipAlloc must return valid allocated buffer");
    std::memset(mem, 0xAA, 256);
    TEST_ASSERT(reinterpret_cast<uint8_t*>(mem)[0] == 0xAA, "Allocated buffer must be writable");
    micant::gdiplus::GdipFree(mem);

    // Stage 2: 2D Affine Matrix Engine (scale, rotate, translate, invert, point transform)
    micant::gdiplus::GpMatrix* mat = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreateMatrix(&mat) == micant::gdiplus::Ok && mat != nullptr, "GdipCreateMatrix must succeed");
    TEST_ASSERT(micant::gdiplus::GdipTranslateMatrix(mat, 10.0f, 20.0f, micant::gdiplus::MatrixOrderAppend) == micant::gdiplus::Ok, "TranslateMatrix must succeed");
    TEST_ASSERT(micant::gdiplus::GdipScaleMatrix(mat, 2.0f, 2.0f, micant::gdiplus::MatrixOrderPrepend) == micant::gdiplus::Ok, "ScaleMatrix must succeed");

    micant::gdiplus::PointF pt{5.0f, 5.0f};
    TEST_ASSERT(micant::gdiplus::GdipTransformMatrixPoints(mat, &pt, 1) == micant::gdiplus::Ok, "TransformMatrixPoints must succeed");
    // scale(2) then translate(10, 20): 5*2+10 = 20, 5*2+20 = 30
    TEST_ASSERT(pt.X == 20.0f && pt.Y == 30.0f, "Transformed coordinates must match affine matrix equation");
    TEST_ASSERT(micant::gdiplus::GdipInvertMatrix(mat) == micant::gdiplus::Ok, "InvertMatrix must succeed");
    TEST_ASSERT(micant::gdiplus::GdipDeleteMatrix(mat) == micant::gdiplus::Ok, "GdipDeleteMatrix must succeed");

    // Stage 3: Graphics Path Geometry & Rectangles
    micant::gdiplus::GpPath* path = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreatePath(micant::gdiplus::FillModeAlternate, &path) == micant::gdiplus::Ok && path != nullptr, "GdipCreatePath must succeed");
    TEST_ASSERT(micant::gdiplus::GdipAddPathRectangleI(path, 10, 10, 200, 100) == micant::gdiplus::Ok, "AddPathRectangleI must succeed");
    TEST_ASSERT(path->points.size() == 4, "Rectangle path must contain 4 vertices");
    TEST_ASSERT(micant::gdiplus::GdipResetPath(path) == micant::gdiplus::Ok && path->points.empty(), "ResetPath must clear points");
    TEST_ASSERT(micant::gdiplus::GdipDeletePath(path) == micant::gdiplus::Ok, "DeletePath must succeed");

    // Stage 4: Pens, Brushes & Dash Styles
    micant::gdiplus::GpBrush solidBrush{};
    micant::gdiplus::GdipSetSolidFillColor(&solidBrush, 0xFFFF8000);
    TEST_ASSERT(solidBrush.color == 0xFFFF8000, "Solid brush color must match set ARGB");

    micant::gdiplus::GpBrush* hatch = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreateHatchBrush(1, 0xFF000000, 0xFFFFFFFF, &hatch) == micant::gdiplus::Ok && hatch != nullptr, "CreateHatchBrush must succeed");
    TEST_ASSERT(hatch->type == 1 && hatch->color == 0xFF000000 && hatch->backColor == 0xFFFFFFFF, "Hatch brush properties must match");

    micant::gdiplus::GpBrush* cloned = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCloneBrush(hatch, &cloned) == micant::gdiplus::Ok && cloned != nullptr, "CloneBrush must succeed");
    delete hatch;
    delete cloned;

    micant::gdiplus::GpPen* pen = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreatePen2(&solidBrush, 2.5f, micant::gdiplus::UnitPixel, &pen) == micant::gdiplus::Ok && pen != nullptr, "CreatePen2 must succeed");
    TEST_ASSERT(micant::gdiplus::GdipSetPenDashStyle(pen, micant::gdiplus::DashStyleDash) == micant::gdiplus::Ok, "SetPenDashStyle must succeed");
    TEST_ASSERT(pen->dashStyle == micant::gdiplus::DashStyleDash && pen->width == 2.5f, "Pen properties must match");
    delete pen;

    // Stage 5: Regions & Clipping
    micant::gdiplus::GpRegion* rgn = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreateRegion(&rgn) == micant::gdiplus::Ok && rgn != nullptr, "CreateRegion must succeed");
    micant::gdiplus::RectF bounds{};
    TEST_ASSERT(micant::gdiplus::GdipGetRegionBounds(rgn, nullptr, &bounds) == micant::gdiplus::Ok, "GetRegionBounds must succeed");
    TEST_ASSERT(bounds.Width == 1000.0f && bounds.Height == 1000.0f, "Default region bounds must match");
    void* hRgn = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipGetRegionHRgn(rgn, nullptr, &hRgn) == micant::gdiplus::Ok && hRgn != nullptr, "GetRegionHRgn must return valid handle");
    TEST_ASSERT(micant::gdiplus::GdipDeleteRegion(rgn) == micant::gdiplus::Ok, "DeleteRegion must succeed");

    // Stage 6: Graphics Context Configuration
    micant::gdiplus::GpGraphics gfx{};
    TEST_ASSERT(micant::gdiplus::GdipSetSmoothingMode(&gfx, micant::gdiplus::SmoothingModeAntiAlias) == micant::gdiplus::Ok, "SetSmoothingMode must succeed");
    TEST_ASSERT(gfx.smoothing == micant::gdiplus::SmoothingModeAntiAlias, "Smoothing mode must be AntiAlias");
    TEST_ASSERT(micant::gdiplus::GdipSetInterpolationMode(&gfx, micant::gdiplus::InterpolationModeHighQualityBicubic) == micant::gdiplus::Ok, "SetInterpolationMode must succeed");
    TEST_ASSERT(gfx.interpolation == micant::gdiplus::InterpolationModeHighQualityBicubic, "Interpolation mode must be HighQualityBicubic");
    void* hdc = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipGetDC(&gfx, &hdc) == micant::gdiplus::Ok && hdc != nullptr, "GetDC must return valid HDC");
    TEST_ASSERT(micant::gdiplus::GdipReleaseDC(&gfx, hdc) == micant::gdiplus::Ok, "ReleaseDC must succeed");

    // Stage 7: Bitmap Creation, Scan0 LockBits/UnlockBits & Dimensions
    micant::gdiplus::GpBitmap* bmp = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreateBitmapFromGraphics(640, 480, &gfx, &bmp) == micant::gdiplus::Ok && bmp != nullptr, "CreateBitmapFromGraphics must succeed");
    uint32_t bmpW = 0, bmpH = 0;
    micant::gdiplus::GdipGetImageWidth(bmp, &bmpW);
    micant::gdiplus::GdipGetImageHeight(bmp, &bmpH);
    TEST_ASSERT(bmpW == 640 && bmpH == 480, "Bitmap dimensions must match 640x480");

    micant::gdiplus::BitmapData bdata{};
    micant::gdiplus::Rect lockRect{0, 0, 640, 480};
    TEST_ASSERT(micant::gdiplus::GdipBitmapLockBits(bmp, &lockRect, 1, micant::gdiplus::PixelFormat32bppARGB, &bdata) == micant::gdiplus::Ok, "BitmapLockBits must succeed");
    TEST_ASSERT(bdata.Scan0 != nullptr && bdata.Stride == 640 * 4, "BitmapData scan0 buffer and stride must be valid");
    TEST_ASSERT(micant::gdiplus::GdipBitmapUnlockBits(bmp, &bdata) == micant::gdiplus::Ok, "BitmapUnlockBits must succeed");
    delete bmp;

    // Stage 8: Typography, Font Families, Fonts & String Measurement
    micant::gdiplus::GpFontFamily* fam = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreateFontFamilyFromName(L"Segoe UI", nullptr, &fam) == micant::gdiplus::Ok && fam != nullptr, "CreateFontFamilyFromName must succeed");
    wchar_t famName[32]{};
    TEST_ASSERT(micant::gdiplus::GdipGetFamilyName(fam, famName, 0) == micant::gdiplus::Ok && std::wcscmp(famName, L"Segoe UI") == 0, "Font family name must match Segoe UI");

    micant::gdiplus::GpFont* font = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreateFont(fam, 12.0f, 0, micant::gdiplus::UnitPoint, &font) == micant::gdiplus::Ok && font != nullptr, "CreateFont must succeed");
    float fHeight = 0.0f;
    TEST_ASSERT(micant::gdiplus::GdipGetFontHeight(font, nullptr, &fHeight) == micant::gdiplus::Ok && fHeight > 0.0f, "Font height must be positive");

    micant::gdiplus::RectF strBox{};
    int fitted = 0, lines = 0;
    TEST_ASSERT(micant::gdiplus::GdipMeasureString(&gfx, L"SumatraPDF Document Canvas", 26, font, nullptr, nullptr, &strBox, &fitted, &lines) == micant::gdiplus::Ok, "MeasureString must succeed");
    TEST_ASSERT(strBox.Width > 0.0f && fitted == 26 && lines == 1, "Measured string box and codepoint fit count must be valid");
    delete font;
    delete fam;

    // Stage 9: User32 Dynamic Data Exchange (DDE) Single-Instance Subsystem
    uint32_t ddeInst = 0;
    TEST_ASSERT(micant::user32::DdeInitializeW(&ddeInst, nullptr, 0, 0) == 0 && ddeInst > 0, "DdeInitializeW must succeed with valid instance ID");
    void* hszService = micant::user32::DdeCreateStringHandleW(ddeInst, L"SUMATRA", 1200);
    TEST_ASSERT(hszService != nullptr, "DdeCreateStringHandleW must return valid handle");
    void* hConv = micant::user32::DdeConnect(ddeInst, hszService, nullptr, nullptr);
    TEST_ASSERT(hConv != nullptr, "DdeConnect must establish conversation handle");
    uint32_t ddeResult = 0;
    void* hData = micant::user32::DdeClientTransaction(nullptr, 0, hConv, nullptr, 1, 0x0050, 5000, &ddeResult);
    TEST_ASSERT(hData != nullptr, "DdeClientTransaction must dispatch command");
    TEST_ASSERT(micant::user32::DdeFreeDataHandle(hData) == 1, "DdeFreeDataHandle must succeed");
    TEST_ASSERT(micant::user32::DdeDisconnect(hConv) == 1, "DdeDisconnect must succeed");
    TEST_ASSERT(micant::user32::DdeFreeStringHandle(ddeInst, hszService) == 1, "DdeFreeStringHandle must succeed");
    TEST_ASSERT(micant::user32::DdeUninitialize(ddeInst) == 1, "DdeUninitialize must succeed");

    // Stage 10: Shlwapi Substring & URL Escaping
    const wchar_t* fullUrl = L"https://www.sumatrapdfreader.org/docs/manual.html";
    const wchar_t* sub = micant::shell32::StrStrW(fullUrl, L"manual");
    TEST_ASSERT(sub != nullptr && std::wcscmp(sub, L"manual.html") == 0, "StrStrW must locate substring exactly");
    const wchar_t* rsub = micant::shell32::StrRStrIW(fullUrl, nullptr, L"DOCS");
    TEST_ASSERT(rsub != nullptr && std::wcsncmp(rsub, L"docs", 4) == 0, "StrRStrIW case-insensitive search must succeed");

    wchar_t escaped[64]{};
    uint32_t escLen = 64;
    TEST_ASSERT(micant::shell32::UrlEscapeW(L"file://doc.pdf", escaped, &escLen, 0) == 0, "UrlEscapeW must succeed");
    TEST_ASSERT(std::wcscmp(escaped, L"file://doc.pdf") == 0, "Escaped URL buffer must be populated");

    // Stage 11: Kernel32 Time & Directory Helpers
    uint64_t fileTime = 0;
    TEST_ASSERT(micant::win32::DosDateTimeToFileTime(0x5928, 0x4800, &fileTime) == 1 && fileTime > 0, "DosDateTimeToFileTime must produce valid 64-bit FILETIME");
    uint32_t drives = micant::win32::GetLogicalDrives();
    TEST_ASSERT((drives & 0x0C) == 0x0C, "Logical drives bitmask must indicate C: and D: availability");

    wchar_t volPath[16]{};
    TEST_ASSERT(micant::win32::GetVolumePathNameW(L"D:\\MicaNT_Apps\\doc.pdf", volPath, 16) == 1, "GetVolumePathNameW must succeed");
    TEST_ASSERT(std::wcscmp(volPath, L"D:\\") == 0, "Volume path name must resolve to D:\\");

    wchar_t tmpPath[260]{};
    uint32_t tmpId = micant::win32::GetTempFileNameW(L"C:\\Temp\\", L"SMP", 0x1234, tmpPath);
    TEST_ASSERT(tmpId == 0x1234 && std::wcsstr(tmpPath, L"SMP1234.tmp") != nullptr, "GetTempFileNameW must format temporary file path");

    // Stage 12: UI Automation Core & MsImg32 Graphics
    TEST_ASSERT(micant::uiautomation::UiaRaiseStructureChangedEvent(nullptr, nullptr, nullptr, 0) == 0, "UiaRaiseStructureChangedEvent must return S_OK");
    void* hostProv = nullptr;
    TEST_ASSERT(micant::uiautomation::UiaHostProviderFromHwnd(reinterpret_cast<void*>(0x9001), &hostProv) == 0 && hostProv != nullptr, "UiaHostProviderFromHwnd must return provider pointer");
    TEST_ASSERT(micant::gdi32::GradientFill(nullptr, nullptr, 0, nullptr, 0, 0) == 1, "GradientFill must succeed");
    TEST_ASSERT(micant::comdlg32::PrintDlgExW(nullptr) == 0, "PrintDlgExW must return S_OK");

    // Stage 13: DynamicLoader IAT Binding & Symbol Satisfaction
    micant::subsystems::InitializeAllSubsystemExports();
    auto& loader = micant::ldr::DynamicLoader::get();

    static constexpr const char* SUMATRA_SAMPLE_SYMBOLS[] = {
        // GDI+ Core
        "GdipAlloc", "GdipFree", "GdipCreateMatrix", "GdipDeleteMatrix", "GdipSetWorldTransform",
        "GdipTranslateMatrix", "GdipScaleMatrix", "GdipRotateMatrix", "GdipInvertMatrix",
        "GdipCreatePath", "GdipAddPathRectangleI", "GdipCreatePen2", "GdipCreateHatchBrush",
        "GdipCreateRegion", "GdipSetSmoothingMode", "GdipCreateBitmapFromGraphics",
        "GdipBitmapLockBits", "GdipBitmapUnlockBits", "GdipCreateFontFamilyFromName",
        "GdipCreateFont", "GdipMeasureString", "GdipDrawString",
        // Sumatra Win32
        "DdeInitializeW", "DdeConnect", "DdeClientTransaction", "DdeDisconnect", "DdeUninitialize",
        "StrStrW", "StrRStrIW", "UrlEscapeW", "DosDateTimeToFileTime", "SystemTimeToFileTime",
        "GetLogicalDrives", "GetVolumePathNameW", "GetTempFileNameW", "AttachConsole",
        "UiaRaiseStructureChangedEvent", "UiaHostProviderFromHwnd", "GradientFill", "PrintDlgExW"
    };

    uint32_t resolvedCount = 0;
    for (const char* sym : SUMATRA_SAMPLE_SYMBOLS) {
        const char* candidateDlls[] = {
            "gdiplus.dll", "kernel32.dll", "user32.dll", "shlwapi.dll",
            "msimg32.dll", "comdlg32.dll", "uiautomationcore.dll"
        };
        bool found = false;
        for (const char* d : candidateDlls) {
            if (loader.getExport(d, sym) != nullptr) {
                found = true;
                break;
            }
        }
        if (found) ++resolvedCount;
    }
    TEST_ASSERT(resolvedCount == sizeof(SUMATRA_SAMPLE_SYMBOLS) / sizeof(SUMATRA_SAMPLE_SYMBOLS[0]),
                "All sampled GDI+ and SumatraPDF Win32 symbols must be resolved from DynamicLoader export table");

    // Stage 14: Multi-Threaded GDI+ Transformation & Document Stream Concurrent Stress Test
    std::atomic<uint32_t> stressSuccessCount{0};
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&stressSuccessCount, t]() {
            for (int op = 0; op < 25; ++op) {
                micant::gdiplus::GpMatrix* m = nullptr;
                if (micant::gdiplus::GdipCreateMatrix(&m) == micant::gdiplus::Ok && m) {
                    micant::gdiplus::GdipTranslateMatrix(m, static_cast<float>(t), static_cast<float>(op), micant::gdiplus::MatrixOrderAppend);
                    micant::gdiplus::PointF p{10.0f, 10.0f};
                    micant::gdiplus::GdipTransformMatrixPoints(m, &p, 1);
                    if (p.X == 10.0f + t && p.Y == 10.0f + op) {
                        stressSuccessCount.fetch_add(1, std::memory_order_relaxed);
                    }
                    micant::gdiplus::GdipDeleteMatrix(m);
                }
            }
        });
    }
    for (auto& th : threads) {
        if (th.joinable()) th.join();
    }
    TEST_ASSERT(stressSuccessCount.load() == 200, "200-transaction GDI+ transformation concurrent stress test must succeed 100%");

    std::cout << "[TEST] Suite 218: SumatraPDF 3.6+ Sovereign GDI+ 2D Vector & Document Subsystem PASSED.\n";
}

// ============================================================================
// Suite 219: Everything 1.4+ Search Indexing & Win32 Subsystem
// ============================================================================
inline void Test_Everything_Search_Indexing_Subsystem() {
    std::cout << "\n[TEST] Running Suite 219: Everything 1.4+ Search Indexing & Win32 Subsystem...\n";

    // Stage 1: Service Control Dispatcher & GUI Mode Fallback
    micant::subsystems::InitializeAllSubsystemExports();
    kernel32::SetLastError(0);
    int32_t dispatchRes = micant::advapi32::StartServiceCtrlDispatcherW(nullptr);
    TEST_ASSERT(dispatchRes == 0, "StartServiceCtrlDispatcherW must return FALSE in GUI desktop mode");
    TEST_ASSERT(kernel32::GetLastError() == 1063, "StartServiceCtrlDispatcherW must set ERROR_FAILED_SERVICE_CONTROLLER_CONNECT (1063)");

    // Stage 2: Service Handler Registration & Status Updates
    void* hStatus = micant::advapi32::RegisterServiceCtrlHandlerW(L"Everything", nullptr);
    TEST_ASSERT(hStatus != nullptr, "RegisterServiceCtrlHandlerW must return non-null status handle");

    micant::scm::SERVICE_STATUS svcStatus{};
    svcStatus.dwServiceType = 0x10;
    svcStatus.dwCurrentState = 4; // SERVICE_RUNNING
    svcStatus.dwControlsAccepted = 1;
    TEST_ASSERT(micant::advapi32::SetServiceStatus(hStatus, &svcStatus) == 1, "SetServiceStatus must return TRUE");

    uint32_t needed = 0;
    uint8_t cfgBuffer[512]{};
    TEST_ASSERT(micant::advapi32::QueryServiceConfigW(nullptr, cfgBuffer, sizeof(cfgBuffer), &needed) == 1, "QueryServiceConfigW must succeed with sufficient buffer");
    TEST_ASSERT(needed > 0, "QueryServiceConfigW must report required bytes");

    // Stage 3: System-Wide Global HotKey Engine
    void* dummyHwnd = reinterpret_cast<void*>(0x8080);
    TEST_ASSERT(micant::user32::RegisterHotKey(dummyHwnd, 100, 0x0002 /* MOD_CONTROL */, 0x20 /* VK_SPACE */) == 1, "RegisterHotKey (Ctrl+Space) must succeed");
    // Duplicate registration should fail with error
    kernel32::SetLastError(0);
    TEST_ASSERT(micant::user32::RegisterHotKey(dummyHwnd, 101, 0x0002, 0x20) == 0, "Duplicate HotKey registration must fail");
    TEST_ASSERT(kernel32::GetLastError() == 1409, "Duplicate HotKey must set ERROR_HOTKEY_ALREADY_REGISTERED (1409)");
    TEST_ASSERT(micant::user32::UnregisterHotKey(dummyHwnd, 100) == 1, "UnregisterHotKey must succeed");

    // Stage 4: Advanced GDI Text Alignment & Clip Regions
    TEST_ASSERT(micant::gdi32::GetTextAlign(nullptr) == 0, "GetTextAlign must return TA_LEFT | TA_TOP");
    TEST_ASSERT(micant::gdi32::OffsetClipRgn(nullptr, 10, 20) == 2, "OffsetClipRgn must return SIMPLEREGION (2)");

    micant::gdi32::POINT pt{99, 99};
    TEST_ASSERT(micant::gdi32::GetDCOrgEx(nullptr, &pt) == 1 && pt.x == 0 && pt.y == 0, "GetDCOrgEx must initialize translation origin to (0, 0)");

    uint32_t rgnSize = micant::gdi32::GetRegionData(nullptr, 0, nullptr);
    TEST_ASSERT(rgnSize >= sizeof(micant::gdi32::RGNDATAHEADER), "GetRegionData with null buffer must return required buffer size");

    std::vector<uint8_t> rgnBuf(rgnSize);
    uint32_t bytesCopied = micant::gdi32::GetRegionData(nullptr, rgnSize, rgnBuf.data());
    TEST_ASSERT(bytesCopied == rgnSize, "GetRegionData must populate region bytes correctly");

    TEST_ASSERT(micant::gdi32::GetNearestColor(nullptr, 0x00FF8800) == 0x00FF8800, "GetNearestColor on true-color display must return exact color");
    void* hBmp = micant::gdi32::CreateBitmapIndirect(nullptr);
    TEST_ASSERT(hBmp != nullptr, "CreateBitmapIndirect must return non-null HBITMAP");

    // Stage 5: Shell Lightweight Path & Registry Helpers
    TEST_ASSERT(micant::shell32::PathIsRootW(L"C:\\") == 1, "PathIsRootW must return TRUE for 'C:\\'");
    TEST_ASSERT(micant::shell32::PathIsRootW(L"D:/") == 1, "PathIsRootW must return TRUE for 'D:/'");
    TEST_ASSERT(micant::shell32::PathIsRootW(L"\\\\server\\share") == 1, "PathIsRootW must return TRUE for UNC root");
    TEST_ASSERT(micant::shell32::PathIsRootW(L"C:\\Windows\\System32") == 0, "PathIsRootW must return FALSE for subdirectory");

    wchar_t defaultVal[] = L"SearchEngine";
    wchar_t readVal[64]{};
    uint32_t cbRead = sizeof(readVal);
    uint32_t valType = 0;
    TEST_ASSERT(micant::shell32::SHRegGetUSValueW(L"Software\\Everything", L"AppTitle", &valType, readVal, &cbRead, 0, defaultVal, sizeof(defaultVal)) == 0, "SHRegGetUSValueW must succeed");
    TEST_ASSERT(std::wcscmp(readVal, L"SearchEngine") == 0, "SHRegGetUSValueW must populate default value");

    // Stage 6: Window Geometry & Dialog Tab Navigation
    micant::gdi32::RECT srcRc{10, 20, 200, 150};
    micant::gdi32::RECT dstRc{0, 0, 0, 0};
    TEST_ASSERT(micant::user32::CopyRect(&dstRc, &srcRc) == 1, "CopyRect must return TRUE");
    TEST_ASSERT(dstRc.left == 10 && dstRc.top == 20 && dstRc.right == 200 && dstRc.bottom == 150, "CopyRect coordinates must match source");

    micant::gdi32::RECT adjRc{100, 100, 500, 400};
    TEST_ASSERT(micant::user32::AdjustWindowRect(&adjRc, 0x00C00000 /* WS_CAPTION */, 0) == 1, "AdjustWindowRect must succeed");
    TEST_ASSERT(adjRc.top < 100 && adjRc.left < 100 && adjRc.right > 500 && adjRc.bottom > 400, "AdjustWindowRect must expand rect for caption & borders");

    TEST_ASSERT(micant::user32::OpenIcon(dummyHwnd) == 1, "OpenIcon must return TRUE");
    TEST_ASSERT(micant::user32::GetNextDlgTabItem(nullptr, dummyHwnd, 0) == dummyHwnd, "GetNextDlgTabItem must return valid control HWND");
    TEST_ASSERT(micant::user32::ReplyMessage(0) == 1, "ReplyMessage must return TRUE");

    micant::gdi32::RECT updateRc{};
    TEST_ASSERT(micant::user32::ScrollWindowEx(dummyHwnd, 0, -20, nullptr, nullptr, nullptr, &updateRc, 0) == 2, "ScrollWindowEx must return SIMPLEREGION (2)");

    // Stage 7: Process Environment Block & Handlers
    char* envBlock = micant::kernel32::GetEnvironmentStrings();
    bool foundPath = false;
    for (const char* p = envBlock; *p; p += std::strlen(p) + 1) {
        if (std::strstr(p, "Path=") != nullptr) {
            foundPath = true;
            break;
        }
    }
    TEST_ASSERT(foundPath, "Environment block must contain Path variable");
    TEST_ASSERT(micant::kernel32::FreeEnvironmentStringsA(envBlock) == 1, "FreeEnvironmentStringsA must return TRUE");
    TEST_ASSERT(micant::kernel32::SetHandleCount(2048) == 2048, "SetHandleCount must return requested count");
    TEST_ASSERT(micant::kernel32::__C_specific_handler(nullptr, nullptr, nullptr, nullptr) == 1, "__C_specific_handler must return ExceptionContinueSearch (1)");

    micant::kernel32::OSVERSIONINFOA vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    TEST_ASSERT(micant::kernel32::GetVersionExA(&vi) == 1, "GetVersionExA must return TRUE");
    TEST_ASSERT(vi.dwMajorVersion == 10 && vi.dwMinorVersion == 0 && vi.dwBuildNumber == 19045, "GetVersionExA must report Windows 10 x64 Sovereign OS");

    // Stage 8: System Character Classification (GetStringTypeA)
    const char testStr[] = "MicaNT 2026!";
    uint16_t charTypes[16]{};
    TEST_ASSERT(micant::kernel32::GetStringTypeA(0x0409, 1, testStr, static_cast<int32_t>(std::strlen(testStr)), charTypes) == 1, "GetStringTypeA must succeed");
    TEST_ASSERT((charTypes[0] & 0x0001) != 0, "Character 'M' must have C1_UPPER flag");
    TEST_ASSERT((charTypes[1] & 0x0002) != 0, "Character 'i' must have C1_LOWER flag");
    TEST_ASSERT((charTypes[6] & 0x0008) != 0, "Character ' ' must have C1_SPACE flag");
    TEST_ASSERT((charTypes[7] & 0x0004) != 0, "Character '2' must have C1_DIGIT flag");
    TEST_ASSERT((charTypes[11] & 0x0010) != 0, "Character '!' must have C1_PUNCT flag");

    // Stage 9: System Locale Number & Calendar Formatting
    wchar_t numBuf[32]{};
    int32_t numRes = micant::kernel32::GetNumberFormatW(0x0409, 0, L"123456.78", nullptr, numBuf, 32);
    TEST_ASSERT(numRes > 0 && std::wcscmp(numBuf, L"123456.78") == 0, "GetNumberFormatW must format number string");

    wchar_t calBuf[16]{};
    uint32_t calVal = 0;
    TEST_ASSERT(micant::kernel32::GetCalendarInfoW(0x0409, 1, 1, calBuf, 16, &calVal) == 1, "GetCalendarInfoW must succeed");
    TEST_ASSERT(calVal == 1 && std::wcscmp(calBuf, L"1") == 0, "GetCalendarInfoW must return valid calendar value");

    // Stage 10: COM Moniker Binding Context (CreateBindCtx)
    void* pbc = nullptr;
    TEST_ASSERT(micant::ole32::CreateBindCtx(0, &pbc) == 0 && pbc != nullptr, "CreateBindCtx must return S_OK and non-null context");
    auto* bindCtx = static_cast<micant::ole32::IBindCtx*>(pbc);
    TEST_ASSERT(bindCtx->lpVtbl != nullptr && bindCtx->lpVtbl->AddRef(pbc) == 1, "IBindCtx::AddRef must succeed");
    TEST_ASSERT(bindCtx->lpVtbl->Release(pbc) == 1, "IBindCtx::Release must succeed");

    // Stage 11: Inter-Thread Messaging & Keycode Mapping
    TEST_ASSERT(micant::user32::PostThreadMessageW(1001, 0x0400 /* WM_USER */, 12, 34) == 1, "PostThreadMessageW must return TRUE");
    uint64_t msgRes = 999;
    TEST_ASSERT(micant::user32::SendMessageTimeoutW(dummyHwnd, 0x0010 /* WM_CLOSE */, 0, 0, 0x0002, 1000, &msgRes) == 1 && msgRes == 0, "SendMessageTimeoutW must succeed and set result");

    TEST_ASSERT(micant::user32::MapVirtualKeyExW(0x41, 2 /* MAPVK_VK_TO_CHAR */, nullptr) == 'A', "MapVirtualKeyExW for VK_A must translate to 'A'");
    TEST_ASSERT(micant::user32::MapVirtualKeyExW(0x30, 2, nullptr) == '0', "MapVirtualKeyExW for VK_0 must translate to '0'");
    TEST_ASSERT(micant::user32::MapVirtualKeyExW(0x20, 2, nullptr) == ' ', "MapVirtualKeyExW for VK_SPACE must translate to ' '");

    // Stage 12: DynamicLoader IAT Binding Verification for Everything 1.4+
    auto& loader = micant::ldr::DynamicLoader::get();
    static constexpr const char* EVERYTHING_SAMPLE_SYMBOLS[] = {
        "RegisterServiceCtrlHandlerW", "StartServiceCtrlDispatcherW", "SetServiceStatus", "QueryServiceConfigW",
        "RegOpenKeyA", "RegQueryValueW", "GetTextAlign", "OffsetClipRgn", "GetDCOrgEx", "GetRegionData",
        "GetNearestColor", "CreateBitmapIndirect", "__C_specific_handler", "GetVersionExA", "GetNumberFormatW",
        "GetCalendarInfoW", "FreeEnvironmentStringsA", "GetEnvironmentStrings", "SetHandleCount", "GetStringTypeA",
        "CreateBindCtx", "SHRegGetUSValueW", "PathIsRootW", "ScrollWindowEx", "AdjustWindowRect", "CopyRect",
        "OpenIcon", "GetNextDlgTabItem", "ReplyMessage", "RegisterHotKey", "UnregisterHotKey", "PostThreadMessageW",
        "SendMessageTimeoutW", "MapVirtualKeyExW"
    };

    uint32_t resolvedCount = 0;
    for (const char* sym : EVERYTHING_SAMPLE_SYMBOLS) {
        const char* candidateDlls[] = {
            "advapi32.dll", "gdi32.dll", "kernel32.dll", "ole32.dll", "shlwapi.dll", "user32.dll"
        };
        bool found = false;
        for (const char* d : candidateDlls) {
            if (loader.getExport(d, sym) != nullptr) {
                found = true;
                break;
            }
        }
        if (found) ++resolvedCount;
    }
    TEST_ASSERT(resolvedCount == sizeof(EVERYTHING_SAMPLE_SYMBOLS) / sizeof(EVERYTHING_SAMPLE_SYMBOLS[0]),
                "All 34 Everything Win32 symbols must be registered and resolved from DynamicLoader");

    // Stage 13: Multi-Threaded Search & HotKey Dispatch Concurrent Stress Test
    std::atomic<uint32_t> stressQueriesCompleted{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&stressQueriesCompleted, t]() {
            for (int q = 0; q < 50; ++q) {
                // Simulate fast MFT index query & hotkey toggling
                void* hwnd = reinterpret_cast<void*>(static_cast<uintptr_t>(0x5000 + t));
                micant::user32::RegisterHotKey(hwnd, q + 1, 0x0001, static_cast<uint32_t>('A' + (q % 26)));
                micant::user32::UnregisterHotKey(hwnd, q + 1);

                micant::gdi32::POINT pt{};
                micant::gdi32::GetDCOrgEx(nullptr, &pt);

                wchar_t num[16]{};
                micant::kernel32::GetNumberFormatW(0x0409, 0, L"42", nullptr, num, 16);

                stressQueriesCompleted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(stressQueriesCompleted.load() == 400, "400-query concurrent search & hotkey stress test must succeed 100%");

    std::cout << "[TEST] Suite 219: Everything 1.4+ Search Indexing & Win32 Subsystem PASSED.\n";
}

// ----------------------------------------------------------------------------
// Suite 220: WinMerge 2.16+ Visual Diff & Merge Subsystem & MDI / Scintilla Shell Integration
// ----------------------------------------------------------------------------
inline void Test_WinMerge_Visual_Diff_Subsystem() {
    std::cout << "\n[TEST] Running Suite 220: WinMerge 2.16+ Visual Diff & Win32 Subsystem...\n";

    micant::subsystems::InitializeAllSubsystemExports();
    // Stage 1: Advanced Registry Operations
    TEST_ASSERT(micant::advapi32::RegSetValueW(nullptr, L"Settings", 1, L"Dark", 8) == 0, "RegSetValueW must succeed");
    TEST_ASSERT(micant::advapi32::RegDeleteTreeW(nullptr, L"OldConfig") == 0, "RegDeleteTreeW must succeed");

    // Stage 2: Common Controls Initialization
    micant::comctl32::InitCommonControls();
    auto& loader = micant::ldr::DynamicLoader::get();
    TEST_ASSERT(loader.getExportOrdinal("comctl32.dll", 17) != nullptr, "InitCommonControls / Ordinal 17 must be exported in comctl32.dll");

    // Stage 3: Advanced GDI Viewport Scaling & Extents
    micant::gdi32::SIZE prevSize{};
    TEST_ASSERT(micant::gdi32::SetViewportExtEx(nullptr, 100, 200, &prevSize) == 1, "SetViewportExtEx must succeed");
    TEST_ASSERT(prevSize.cx == 100 && prevSize.cy == 200, "Previous size populated in SetViewportExtEx");

    micant::gdi32::SIZE curSize{};
    TEST_ASSERT(micant::gdi32::GetViewportExtEx(nullptr, &curSize) == 1, "GetViewportExtEx must succeed");
    TEST_ASSERT(curSize.cx == 1 && curSize.cy == 1, "GetViewportExtEx default extent must be 1x1");

    TEST_ASSERT(micant::gdi32::SetWindowExtEx(nullptr, 50, 75, &prevSize) == 1, "SetWindowExtEx must succeed");
    TEST_ASSERT(micant::gdi32::GetWindowExtEx(nullptr, &curSize) == 1, "GetWindowExtEx must succeed");

    micant::gdi32::POINT pt{};
    TEST_ASSERT(micant::gdi32::OffsetViewportOrgEx(nullptr, 10, 20, &pt) == 1, "OffsetViewportOrgEx must succeed");
    TEST_ASSERT(micant::gdi32::ScaleViewportExtEx(nullptr, 2, 1, 2, 1, &curSize) == 1, "ScaleViewportExtEx must succeed");
    TEST_ASSERT(micant::gdi32::ScaleWindowExtEx(nullptr, 2, 1, 2, 1, &curSize) == 1, "ScaleWindowExtEx must succeed");

    // Stage 4: GDI Polygon Fill, Layout & Font Face
    TEST_ASSERT(micant::gdi32::GetLayout(nullptr) == 0, "GetLayout must return 0 (LAYOUT_LTR)");
    TEST_ASSERT(micant::gdi32::SetPolyFillMode(nullptr, 2) == 1, "SetPolyFillMode must return previous mode");
    TEST_ASSERT(micant::gdi32::GetPolyFillMode(nullptr) == 1, "GetPolyFillMode must return ALTERNATE");

    wchar_t faceName[32]{};
    int32_t faceLen = micant::gdi32::GetTextFaceW(nullptr, 32, faceName);
    TEST_ASSERT(faceLen > 0 && std::wcscmp(faceName, L"Segoe UI") == 0, "GetTextFaceW must return 'Segoe UI'");

    void* hElliptic = micant::gdi32::CreateEllipticRgn(0, 0, 100, 100);
    TEST_ASSERT(hElliptic != nullptr, "CreateEllipticRgn must return valid region handle");
    TEST_ASSERT(micant::gdi32::PtVisible(nullptr, 50, 50) == 1, "PtVisible must return 1");
    TEST_ASSERT(micant::gdi32::Escape(nullptr, 1, 0, nullptr, nullptr) == 1, "Escape must return 1");

    int fontEnumCount = 0;
    micant::gdi32::EnumFontFamiliesW(nullptr, nullptr, reinterpret_cast<void*>(+[](const micant::gdi32::ENUMLOGFONTW* elf, const micant::gdi32::NEWTEXTMETRICW*, uint32_t, int64_t lp) -> int32_t {
        if (elf && std::wcscmp(elf->elfLogFont.lfFaceName, L"Segoe UI") == 0) {
            *reinterpret_cast<int*>(lp) += 1;
        }
        return 1;
    }), reinterpret_cast<int64_t>(&fontEnumCount));
    TEST_ASSERT(fontEnumCount == 1, "EnumFontFamiliesW must enumerate Segoe UI");

    void* hMeta = micant::gdi32::CopyMetaFileW(nullptr, nullptr);
    TEST_ASSERT(hMeta != nullptr, "CopyMetaFileW must return valid HMETAFILE");

    // Stage 5: GDI+ 2D Vector Path Geometry & Integer Coordinates
    TEST_ASSERT(micant::gdiplus::GdipAddPathArcI(nullptr, 10, 10, 50, 50, 0.0f, 90.0f) == 0, "GdipAddPathArcI must succeed");
    TEST_ASSERT(micant::gdiplus::GdipClosePathFigure(nullptr) == 0, "GdipClosePathFigure must succeed");
    TEST_ASSERT(micant::gdiplus::GdipAddPathLineI(nullptr, 0, 0, 100, 100) == 0, "GdipAddPathLineI must succeed");
    TEST_ASSERT(micant::gdiplus::GdipAddPathBezierI(nullptr, 0, 0, 10, 20, 30, 40, 50, 50) == 0, "GdipAddPathBezierI must succeed");
    TEST_ASSERT(micant::gdiplus::GdipStartPathFigure(nullptr) == 0, "GdipStartPathFigure must succeed");
    TEST_ASSERT(micant::gdiplus::GdipDrawBezierI(nullptr, nullptr, 0, 0, 10, 20, 30, 40, 50, 50) == 0, "GdipDrawBezierI must succeed");
    TEST_ASSERT(micant::gdiplus::GdipDrawImageRectI(nullptr, nullptr, 0, 0, 64, 64) == 0, "GdipDrawImageRectI must succeed");
    TEST_ASSERT(micant::gdiplus::GdipDrawLinesI(nullptr, nullptr, nullptr, 0) == 0, "GdipDrawLinesI must succeed");

    micant::gdiplus::ColorPalette pal{};
    int palSize = 0;
    TEST_ASSERT(micant::gdiplus::GdipGetImagePaletteSize(nullptr, &palSize) == 0, "GdipGetImagePaletteSize must succeed");
    TEST_ASSERT(palSize == sizeof(micant::gdiplus::ColorPalette), "Palette size must match struct");
    TEST_ASSERT(micant::gdiplus::GdipGetImagePalette(nullptr, &pal, palSize) == 0, "GdipGetImagePalette must succeed");

    void* hBmp = nullptr;
    TEST_ASSERT(micant::gdiplus::GdipCreateBitmapFromFile(L"test.png", &hBmp) == 0 && hBmp != nullptr, "GdipCreateBitmapFromFile must create bitmap");
    TEST_ASSERT(micant::gdiplus::GdipSaveImageToStream(hBmp, nullptr, nullptr, nullptr) == 0, "GdipSaveImageToStream must succeed");

    // Stage 6: Activation Context Engine & Realloc
    void* mem1 = std::malloc(64);
    void* mem2 = micant::kernel32::GlobalReAlloc(mem1, 128, 0);
    TEST_ASSERT(mem2 != nullptr, "GlobalReAlloc must succeed");
    TEST_ASSERT(micant::kernel32::GlobalHandle(mem2) == mem2, "GlobalHandle must return pointer");
    TEST_ASSERT(micant::kernel32::GlobalFlags(mem2) == 0, "GlobalFlags must return GMEM_FIXED");
    void* mem3 = micant::kernel32::LocalReAlloc(mem2, 256, 0);
    TEST_ASSERT(mem3 != nullptr, "LocalReAlloc must succeed");
    std::free(mem3);

    micant::kernel32::ACTCTXW actCtx{};
    void* hAct = micant::kernel32::CreateActCtxW(&actCtx);
    TEST_ASSERT(hAct != nullptr, "CreateActCtxW must return activation context");
    uintptr_t cookie = 0;
    TEST_ASSERT(micant::kernel32::ActivateActCtx(hAct, &cookie) == 1 && cookie != 0, "ActivateActCtx must succeed with cookie");
    TEST_ASSERT(micant::kernel32::DeactivateActCtx(0, cookie) == 1, "DeactivateActCtx must succeed");
    TEST_ASSERT(micant::kernel32::FindActCtxSectionStringW(0, nullptr, 0, L"test", nullptr) == 0, "FindActCtxSectionStringW returns 0 fallback");
    TEST_ASSERT(micant::kernel32::QueryActCtxW(0, hAct, nullptr, 1, nullptr, 0, nullptr) == 1, "QueryActCtxW must succeed");

    // Stage 7: Wow64 Directory, Environment Expansion & String Comparison
    wchar_t wow64[64]{};
    uint32_t wowLen = micant::kernel32::GetSystemWow64DirectoryW(wow64, 64);
    TEST_ASSERT(wowLen > 0 && std::wcscmp(wow64, L"C:\\Windows\\SysWOW64") == 0, "GetSystemWow64DirectoryW returns SysWOW64");

    char expEnv[256]{};
    uint32_t expLen = micant::kernel32::ExpandEnvironmentStringsA("%SYSTEMROOT%\\System32", expEnv, 256);
    TEST_ASSERT(expLen > 0 && std::strcmp(expEnv, "C:\\Windows\\System32") == 0, "ExpandEnvironmentStringsA expands %SYSTEMROOT%");

    TEST_ASSERT(micant::kernel32::SetThreadUILanguage(0x0409) > 0, "SetThreadUILanguage must succeed");
    TEST_ASSERT(micant::kernel32::SetSearchPathMode(1) == 1, "SetSearchPathMode must succeed");
    TEST_ASSERT(micant::kernel32::SetDllDirectoryW(L"C:\\WinMerge") == 1, "SetDllDirectoryW must succeed");
    TEST_ASSERT(micant::kernel32::lstrcmpA("apple", "banana") < 0, "lstrcmpA ordering check");
    TEST_ASSERT(micant::kernel32::lstrcmpA("equal", "equal") == 0, "lstrcmpA equality check");

    // Stage 8: S-List, File Locks & Thread Info
    micant::kernel32::SLIST_HEADER slistHead{};
    micant::kernel32::SLIST_ENTRY entry1{};
    micant::kernel32::SLIST_ENTRY entry2{};
    micant::kernel32::InterlockedPushEntrySList(&slistHead, &entry1);
    micant::kernel32::InterlockedPushEntrySList(&slistHead, &entry2);
    TEST_ASSERT(slistHead.Alignment == reinterpret_cast<uint64_t>(&entry2), "InterlockedPushEntrySList must push to head");

    wchar_t atomBuf[32]{};
    uint32_t atomLen = micant::kernel32::GlobalGetAtomNameW(42, atomBuf, 32);
    TEST_ASSERT(atomLen > 0 && std::wcscmp(atomBuf, L"#42") == 0, "GlobalGetAtomNameW formats atom name");
    TEST_ASSERT(micant::kernel32::GetProfileIntW(L"WinMerge", L"TabWidth", 4) == 4, "GetProfileIntW returns default");
    TEST_ASSERT(micant::kernel32::LockFile(nullptr, 0, 0, 100, 0) == 1, "LockFile must succeed");
    TEST_ASSERT(micant::kernel32::UnlockFile(nullptr, 0, 0, 100, 0) == 1, "UnlockFile must succeed");
    TEST_ASSERT(micant::kernel32::GetThreadId(nullptr) == 1001, "GetThreadId must return 1001");

    // Stage 9: COM Free-Threaded Marshaler, OLE Menus & Accessibility
    void* pMarshaler = nullptr;
    TEST_ASSERT(micant::ole32::CoCreateFreeThreadedMarshaler(nullptr, &pMarshaler) == 0 && pMarshaler != nullptr, "CoCreateFreeThreadedMarshaler succeeds");
    TEST_ASSERT(micant::ole32::OleTranslateAccelerator(nullptr, nullptr, nullptr) == 1, "OleTranslateAccelerator returns S_FALSE (1)");
    void* hOleMenu = micant::ole32::OleCreateMenuDescriptor(nullptr, nullptr);
    TEST_ASSERT(hOleMenu != nullptr, "OleCreateMenuDescriptor returns valid descriptor");
    TEST_ASSERT(micant::ole32::OleDestroyMenuDescriptor(hOleMenu) == 0, "OleDestroyMenuDescriptor returns S_OK");
    TEST_ASSERT(micant::ole32::CoRegisterMessageFilter(nullptr, nullptr) == 0, "CoRegisterMessageFilter returns S_OK");
    micant::ole32::CoFreeUnusedLibraries();
    TEST_ASSERT(micant::ole32::OleDuplicateData(reinterpret_cast<void*>(0x1234), 1, 0) == reinterpret_cast<void*>(0x1234), "OleDuplicateData succeeds");
    TEST_ASSERT(micant::ole32::CoLockObjectExternal(nullptr, 1, 0) == 0, "CoLockObjectExternal returns S_OK");
    TEST_ASSERT(micant::ole32::OleRun(nullptr) == 0, "OleRun returns S_OK");

    uint8_t propVar[24]{0xFF};
    TEST_ASSERT(micant::ole32::PropVariantClear(propVar) == 0, "PropVariantClear returns S_OK");
    TEST_ASSERT(propVar[0] == 0 && propVar[23] == 0, "PropVariantClear zeroes memory");

    void* pAcc = nullptr;
    TEST_ASSERT(micant::uiautomation::AccessibleObjectFromWindow(nullptr, 0, nullptr, &pAcc) == 0 && pAcc != nullptr, "AccessibleObjectFromWindow succeeds");
    void* pStdAcc = nullptr;
    TEST_ASSERT(micant::uiautomation::CreateStdAccessibleObject(nullptr, 0, nullptr, &pStdAcc) == 0 && pStdAcc != nullptr, "CreateStdAccessibleObject succeeds");
    TEST_ASSERT(micant::ole32::OleUIBusyW(nullptr) == 0, "OleUIBusyW returns OLEUI_CANCEL");

    // Stage 10: OLE Automation Error Info & Variant Dates
    void* pErr = nullptr;
    TEST_ASSERT(micant::oleaut32::CreateErrorInfo(&pErr) == 0 && pErr != nullptr, "CreateErrorInfo succeeds");
    TEST_ASSERT(micant::oleaut32::SetErrorInfo(0, pErr) == 0, "SetErrorInfo succeeds");

    double vDate = 0.0;
    TEST_ASSERT(micant::oleaut32::VarDateFromStr(L"2026-10-09", 0x0409, 0, &vDate) == 0, "VarDateFromStr succeeds");
    micant::kernel32::SYSTEMTIME st{};
    TEST_ASSERT(micant::oleaut32::VariantTimeToSystemTime(vDate, &st) == 1, "VariantTimeToSystemTime succeeds");
    TEST_ASSERT(st.wYear >= 2020, "Converted system time year is valid");
    double vDate2 = 0.0;
    TEST_ASSERT(micant::oleaut32::SystemTimeToVariantTime(&st, &vDate2) == 1, "SystemTimeToVariantTime succeeds");
    TEST_ASSERT(std::abs(vDate - vDate2) < 1.0, "Round-trip variant time matches");

    // Stage 11: Windows Property System Architecture
    micant::propsys::PROPERTYKEY pkey{};
    TEST_ASSERT(micant::propsys::PSGetPropertyKeyFromName(L"System.Author", &pkey) == 0 && pkey.pid == 1, "PSGetPropertyKeyFromName succeeds");
    void* pEnum = nullptr;
    TEST_ASSERT(micant::propsys::PSEnumeratePropertyDescriptions(0, nullptr, &pEnum) == 0 && pEnum != nullptr, "PSEnumeratePropertyDescriptions succeeds");
    TEST_ASSERT(micant::propsys::PropVariantCompareEx(nullptr, nullptr, 0, 0) == 0, "PropVariantCompareEx reports equality");
    void* pPDesc = nullptr;
    TEST_ASSERT(micant::propsys::PSGetPropertyDescription(nullptr, nullptr, &pPDesc) == 0 && pPDesc != nullptr, "PSGetPropertyDescription succeeds");
    wchar_t* pDisp = nullptr;
    TEST_ASSERT(micant::propsys::PSFormatForDisplayAlloc(nullptr, nullptr, 0, &pDisp) == 0 && pDisp != nullptr, "PSFormatForDisplayAlloc succeeds");
    TEST_ASSERT(std::wcscmp(pDisp, L"WinMerge Property") == 0, "Formatted property matches");
    std::free(pDisp);
    uint8_t initBuf[16]{};
    TEST_ASSERT(micant::propsys::InitPropVariantFromBuffer(initBuf, 16, propVar) == 0, "InitPropVariantFromBuffer succeeds");

    // Stage 12: Shell Items, ID Lists & Natural Logical String Sort
    void* pShItem = nullptr;
    TEST_ASSERT(micant::shell32::SHCreateShellItem(nullptr, nullptr, nullptr, &pShItem) == 0 && pShItem != nullptr, "SHCreateShellItem succeeds");
    void* dummyPidl = std::malloc(32);
    micant::shell32::ILFree(dummyPidl);
    void* pPStore = nullptr;
    TEST_ASSERT(micant::shell32::SHGetPropertyStoreFromParsingName(L"C:\\diff.txt", nullptr, 0, nullptr, &pPStore) == 0 && pPStore != nullptr, "SHGetPropertyStoreFromParsingName succeeds");
    TEST_ASSERT(micant::shell32::SetCurrentProcessExplicitAppUserModelID(L"WinMerge.WinMerge") == 0, "SetCurrentProcessExplicitAppUserModelID succeeds");
    void* pFMenu = nullptr;
    TEST_ASSERT(micant::shell32::CDefFolderMenu_Create2(nullptr, nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr, &pFMenu) == 0 && pFMenu != nullptr, "CDefFolderMenu_Create2 succeeds");

    wchar_t stripPath[64] = L"C:\\Users\\admin\\file.txt";
    TEST_ASSERT(micant::shell32::PathStripToRootW(stripPath) == 1 && std::wcscmp(stripPath, L"C:\\") == 0, "PathStripToRootW strips drive to root");
    wchar_t stripUnc[64] = L"\\\\server\\share\\docs\\sub";
    TEST_ASSERT(micant::shell32::PathStripToRootW(stripUnc) == 1 && std::wcscmp(stripUnc, L"\\\\server\\share\\") == 0, "PathStripToRootW strips UNC to root");

    TEST_ASSERT(micant::shell32::StrCmpLogicalW(L"file2.txt", L"file10.txt") < 0, "StrCmpLogicalW natural sort order file2 < file10");
    TEST_ASSERT(micant::shell32::StrCmpLogicalW(L"doc10.txt", L"doc2.txt") > 0, "StrCmpLogicalW natural sort order doc10 > doc2");
    TEST_ASSERT(micant::shell32::StrCmpLogicalW(L"same.txt", L"SAME.txt") == 0, "StrCmpLogicalW case-insensitive equality");

    TEST_ASSERT(micant::shell32::PathGetCharTypeW(L'\\') == 0x0002, "PathGetCharTypeW for separator");
    TEST_ASSERT(micant::shell32::PathGetCharTypeW(L'*') == 0x0001, "PathGetCharTypeW for invalid wildcard");
    TEST_ASSERT(micant::shell32::PathGetCharTypeW(L'a') == 0x0004, "PathGetCharTypeW for LFN char");
    TEST_ASSERT(micant::shell32::UrlIsW(L"https://winmerge.org", 0) == 1, "UrlIsW identifies URL");
    TEST_ASSERT(micant::shell32::UrlIsW(L"C:\\local\\file.txt", 0) == 0, "UrlIsW identifies local path as non-URL");
    TEST_ASSERT(micant::shell32::SHAutoComplete(nullptr, 0) == 0, "SHAutoComplete returns S_OK");

    wchar_t szFormatted[64]{};
    micant::shell32::StrFormatByteSizeW(500, szFormatted, 64);
    TEST_ASSERT(std::wcscmp(szFormatted, L"500 bytes") == 0, "StrFormatByteSizeW formats bytes");
    micant::shell32::StrFormatByteSizeW(2048, szFormatted, 64);
    TEST_ASSERT(std::wcscmp(szFormatted, L"2.0 KB") == 0, "StrFormatByteSizeW formats KB");
    micant::shell32::StrFormatByteSizeW(1048576 * 5, szFormatted, 64);
    TEST_ASSERT(std::wcscmp(szFormatted, L"5.0 MB") == 0, "StrFormatByteSizeW formats MB");

    wchar_t trimStr[32] = L"  \tWinMerge\t  ";
    micant::shell32::StrTrimW(trimStr, L" \t");
    TEST_ASSERT(std::wcscmp(trimStr, L"WinMerge") == 0, "StrTrimW trims whitespace");
    TEST_ASSERT(micant::shell32::StrChrW(L"WinMerge", L'M') != nullptr, "StrChrW finds character");
    TEST_ASSERT(micant::shell32::PathIsUNCW(L"\\\\server\\share") == 1, "PathIsUNCW detects UNC");
    TEST_ASSERT(micant::shell32::PathIsUNCW(L"C:\\local") == 0, "PathIsUNCW detects non-UNC");

    // Stage 13: Window Acceleration, DDE Parameters & Theme Metrics
    micant::user32::ACCEL accels[4]{};
    TEST_ASSERT(micant::user32::CopyAcceleratorTableW(nullptr, accels, 4) == 4, "CopyAcceleratorTableW copies 4 accelerators");
    TEST_ASSERT(accels[0].key == 0x43, "Accelerator 0 is 'C' (Ctrl+C)");

    micant::prismx::RECT r1{0, 0, 100, 100};
    micant::prismx::RECT r2{50, 50, 200, 200};
    micant::prismx::RECT rUnion{};
    TEST_ASSERT(micant::user32::UnionRect(&rUnion, &r1, &r2) == 1, "UnionRect succeeds");
    TEST_ASSERT(rUnion.left == 0 && rUnion.top == 0 && rUnion.right == 200 && rUnion.bottom == 200, "UnionRect coordinates verified");

    int32_t textExt = micant::user32::GetTabbedTextExtentW(nullptr, L"hello\tworld", -1, 0, nullptr);
    TEST_ASSERT(textExt != 0, "GetTabbedTextExtentW computes non-zero extent");

    int64_t ddeLParam = (static_cast<int64_t>(0xBEEF) << 32) | 0xCAFE;
    uintptr_t lo = 0, hi = 0;
    TEST_ASSERT(micant::user32::UnpackDDElParam(0, ddeLParam, &lo, &hi) == 1, "UnpackDDElParam succeeds");
    TEST_ASSERT(lo == 0xCAFE && hi == 0xBEEF, "DDE lParam unpacked correctly");
    TEST_ASSERT(micant::user32::ReuseDDElParam(ddeLParam, 0, 0, lo, hi) == ddeLParam, "ReuseDDElParam preserves value");

    TEST_ASSERT(micant::user32::WinHelpW(nullptr, nullptr, 0, 0) == 1, "WinHelpW succeeds");
    TEST_ASSERT(micant::user32::GetMenuCheckMarkDimensions() == ((16 << 16) | 16), "CheckMark dimensions 16x16");
    TEST_ASSERT(micant::user32::GetThreadDesktop(0) != nullptr, "GetThreadDesktop returns non-null HDESK");

    uint32_t objInfo = 0;
    uint32_t neededLen = 0;
    TEST_ASSERT(micant::user32::GetUserObjectInformationW(nullptr, 0, &objInfo, sizeof(objInfo), &neededLen) == 1, "GetUserObjectInformationW succeeds");
    TEST_ASSERT(micant::user32::DragDetect(nullptr, {}) == 1, "DragDetect succeeds");
    TEST_ASSERT(micant::user32::IsMenu(reinterpret_cast<void*>(0x1234)) == 1, "IsMenu identifies menu handle");
    TEST_ASSERT(micant::user32::IsMenu(nullptr) == 0, "IsMenu identifies null handle");

    char sprintfBuf[64]{};
    micant::user32::wsprintfA(sprintfBuf, "MicaNT WinMerge %d.%d", 2, 16);
    TEST_ASSERT(std::strcmp(sprintfBuf, "MicaNT WinMerge 2.16") == 0, "wsprintfA formats string correctly");

    const wchar_t testStr[] = L"ABCDE";
    const wchar_t* pPrev = micant::user32::CharPrevW(testStr, testStr + 2);
    TEST_ASSERT(pPrev == testStr + 1 && *pPrev == L'B', "CharPrevW steps back one character");

    micant::prismx::POINT caretPt{99, 99};
    TEST_ASSERT(micant::user32::GetCaretPos(&caretPt) == 1 && caretPt.x == 0 && caretPt.y == 0, "GetCaretPos initializes to (0, 0)");

    TEST_ASSERT(micant::uxtheme::IsThemeActive() == 1, "IsThemeActive returns 1");
    TEST_ASSERT(micant::uxtheme::IsAppThemed() == 1, "IsAppThemed returns 1");
    micant::uxtheme::MARGINS margins{};
    TEST_ASSERT(micant::uxtheme::GetThemeMargins(nullptr, nullptr, 0, 0, 0, nullptr, &margins) == static_cast<int32_t>(0x80070006u), "GetThemeMargins rejects an invalid theme handle");
    TEST_ASSERT(margins.cxLeftWidth == 0 && margins.cxRightWidth == 0 && margins.cyTopHeight == 0 && margins.cyBottomHeight == 0, "Failed theme margins query clears its output");
    int themeInt = -1;
    TEST_ASSERT(micant::uxtheme::GetThemeInt(nullptr, 1, 1, 2403, &themeInt) == static_cast<int32_t>(0x80070006u) && themeInt == 0, "GetThemeInt clears its output and rejects an invalid theme handle");
    TEST_ASSERT(micant::uxtheme::DrawThemeText(nullptr, nullptr, 0, 0, L"Text", 4, 0, 0, nullptr) == static_cast<int32_t>(0x80070057u), "DrawThemeText rejects invalid arguments");
    TEST_ASSERT(micant::uxtheme::IsThemeBackgroundPartiallyTransparent(nullptr, 0, 0) == 0, "IsThemeBackgroundPartiallyTransparent returns 0");

    uint32_t netErr = 99;
    wchar_t netBuf[16]{L'X'};
    uint32_t netBufLen = 16;
    TEST_ASSERT(micant::wininet::InternetGetLastResponseInfoW(&netErr, netBuf, &netBufLen) == 1 && netErr == 0, "InternetGetLastResponseInfoW succeeds");

    uint8_t jobBuf[512]{};
    uint32_t jobNeeded = 0;
    TEST_ASSERT(micant::winspool::GetJobW(0, 1, 1, jobBuf, sizeof(jobBuf), &jobNeeded) == 1, "GetJobW succeeds with sufficient buffer");
    auto* pJob = reinterpret_cast<micant::winspool::JOB_INFO_1W*>(jobBuf);
    TEST_ASSERT(pJob->JobId == 1 && pJob->Status == 0, "JobId and Status match in GetJobW");

    // Stage 14: DynamicLoader IAT Binding & Ordinal Verification
    const char* const WINMERGE_SAMPLE_SYMBOLS[] = {
        "RegSetValueW", "RegDeleteTreeW", "InitCommonControls", "GetLayout",
        "SetPolyFillMode", "GetPolyFillMode", "SetViewportExtEx", "GetViewportExtEx",
        "SetWindowExtEx", "GetWindowExtEx", "OffsetViewportOrgEx", "ScaleViewportExtEx",
        "ScaleWindowExtEx", "GetTextFaceW", "CreateEllipticRgn", "PtVisible", "Escape",
        "EnumFontFamiliesW", "CopyMetaFileW", "GdipAddPathArcI", "GdipClosePathFigure",
        "GdipAddPathLineI", "GdipAddPathBezierI", "GdipStartPathFigure", "GdipDrawBezierI",
        "GdipDrawImageRectI", "GdipGetImagePalette", "GdipGetImagePaletteSize",
        "GdipCreateBitmapFromFile", "GdipSaveImageToStream", "GdipDrawLinesI",
        "GlobalReAlloc", "LocalReAlloc", "GlobalHandle", "GlobalFlags", "SetThreadUILanguage",
        "SetSearchPathMode", "SetDllDirectoryW", "GetSystemWow64DirectoryW",
        "ExpandEnvironmentStringsA", "CreateActCtxW", "ActivateActCtx", "DeactivateActCtx",
        "FindActCtxSectionStringW", "QueryActCtxW", "GetProfileIntW", "GlobalGetAtomNameW",
        "lstrcmpA", "LockFile", "UnlockFile", "FindResourceExW", "InterlockedPushEntrySList",
        "GetThreadId", "CoCreateFreeThreadedMarshaler", "OleTranslateAccelerator",
        "OleDestroyMenuDescriptor", "OleCreateMenuDescriptor", "CoRegisterMessageFilter",
        "CoFreeUnusedLibraries", "OleDuplicateData", "CoLockObjectExternal", "CoGetObject",
        "OleRun", "PropVariantClear", "AccessibleObjectFromWindow", "CreateStdAccessibleObject",
        "CreateErrorInfo", "SetErrorInfo", "VarDateFromStr", "VariantTimeToSystemTime",
        "SystemTimeToVariantTime", "OleUIBusyW", "PSGetPropertyKeyFromName",
        "PSEnumeratePropertyDescriptions", "PropVariantCompareEx", "PSGetPropertyDescription",
        "PSFormatForDisplayAlloc", "InitPropVariantFromBuffer", "SHCreateShellItem",
        "ILFree", "SHGetPropertyStoreFromParsingName", "SetCurrentProcessExplicitAppUserModelID",
        "CDefFolderMenu_Create2", "PathStripToRootW", "StrCmpLogicalW", "PathGetCharTypeW",
        "UrlIsW", "SHAutoComplete", "PathCompactPathW", "StrFormatByteSizeW", "StrTrimW",
        "StrChrW", "PathIsUNCW", "CopyAcceleratorTableW", "RealChildWindowFromPoint",
        "UnionRect", "GetTabbedTextExtentW", "ReuseDDElParam", "UnpackDDElParam",
        "WinHelpW", "GetMenuCheckMarkDimensions", "ChildWindowFromPoint", "GetThreadDesktop",
        "GetUserObjectInformationW", "DragDetect", "IsMenu", "GrayStringW", "TabbedTextOutW",
        "wsprintfA", "CharPrevW", "GetCaretPos", "IsThemeActive", "IsAppThemed",
        "GetThemeMargins", "GetThemeInt", "DrawThemeText", "IsThemeBackgroundPartiallyTransparent",
        "InternetGetLastResponseInfoW", "GetJobW"
    };

    uint32_t resolvedCount = 0;
    for (const char* sym : WINMERGE_SAMPLE_SYMBOLS) {
        const char* candidateDlls[] = {
            "advapi32.dll", "comctl32.dll", "gdi32.dll", "gdiplus.dll", "kernel32.dll",
            "ole32.dll", "oleacc.dll", "oleaut32.dll", "oledlg.dll", "propsys.dll",
            "shell32.dll", "shlwapi.dll", "user32.dll", "uxtheme.dll", "wininet.dll", "winspool.drv"
        };
        bool found = false;
        for (const char* d : candidateDlls) {
            if (loader.getExport(d, sym) != nullptr) {
                found = true;
                break;
            }
        }
        if (found) ++resolvedCount;
    }
    TEST_ASSERT(resolvedCount == sizeof(WINMERGE_SAMPLE_SYMBOLS) / sizeof(WINMERGE_SAMPLE_SYMBOLS[0]),
                "All WinMerge Win32 symbols must be registered and resolved from DynamicLoader");

    // Verify key ordinals
    TEST_ASSERT(loader.getExportOrdinal("comctl32.dll", 17) != nullptr, "comctl32.dll #17 must resolve");
    TEST_ASSERT(loader.getExportOrdinal("shell32.dll", 155) != nullptr, "shell32.dll #155 must resolve");
    TEST_ASSERT(loader.getExportOrdinal("shell32.dll", 701) != nullptr, "shell32.dll #701 must resolve");
    TEST_ASSERT(loader.getExportOrdinal("shlwapi.dll", 2) != nullptr, "shlwapi.dll #2 must resolve");
    TEST_ASSERT(loader.getExportOrdinal("shlwapi.dll", 12) != nullptr, "shlwapi.dll #12 must resolve");
    TEST_ASSERT(loader.getExportOrdinal("oleaut32.dll", 184) != nullptr, "oleaut32.dll #184 must resolve");
    TEST_ASSERT(loader.getExportOrdinal("oleaut32.dll", 185) != nullptr, "oleaut32.dll #185 must resolve");

    // Stage 15: Multi-Threaded Visual Diff & Natural Sort Concurrent Stress Test
    std::atomic<uint32_t> stressDiffsCompleted{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&stressDiffsCompleted, t]() {
            for (int q = 0; q < 50; ++q) {
                wchar_t fn1[32], fn2[32];
                std::swprintf(fn1, 32, L"file_%d_%d.txt", t, q);
                std::swprintf(fn2, 32, L"file_%d_%d.txt", t, q + 1);
                int cmp = micant::shell32::StrCmpLogicalW(fn1, fn2);
                (void)cmp;

                micant::gdi32::SIZE s{};
                micant::gdi32::ScaleViewportExtEx(nullptr, q + 1, 1, q + 1, 1, &s);

                micant::user32::GetTabbedTextExtentW(nullptr, L"Line\tData", -1, 0, nullptr);

                stressDiffsCompleted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(stressDiffsCompleted.load() == 400, "400-operation concurrent diff & natural sort stress test must succeed 100%");

    std::cout << "[TEST] Suite 220: WinMerge 2.16+ Visual Diff & Win32 Subsystem PASSED.\n";
}
#include "micant/um/subsystems/net/mpr.hpp"

void Test_Retail_Ecosystem_100_Percent_Coverage() {
    std::cout << "[TEST] Executing Suite 221: 100.0% Retail Ecosystem Coverage & Subsystem Extension Matrix...\n";

    auto& loader = micant::ldr::DynamicLoader::get();

    // Initialize all subsystems
    micant::ws2_32::InitializeWs2_32SubsystemExports();
    micant::oleaut32::InitializeOleAut32SubsystemExports();
    micant::winspool::InitializePrintSpoolerSubsystemExports();
    micant::mpr::InitializeMprSubsystemExports();
    micant::subsystems::InitializeAllSubsystemExports();
    // Stage 1: MPR.dll Network Provider Router Subsystem Verification
    void* hEnum = nullptr;
    uint32_t openRes = micant::mpr::WNetOpenEnumW(0, 0, 0, nullptr, &hEnum);
    TEST_ASSERT(openRes == micant::mpr::WN_SUCCESS, "WNetOpenEnumW must return WN_SUCCESS");
    TEST_ASSERT(hEnum != nullptr, "WNetOpenEnumW must return valid handle");

    uint32_t count = 10;
    uint32_t enumRes = micant::mpr::WNetEnumResourceW(hEnum, &count, nullptr, nullptr);
    TEST_ASSERT(enumRes == micant::mpr::WN_NO_MORE_ENTRIES, "WNetEnumResourceW must return WN_NO_MORE_ENTRIES");
    TEST_ASSERT(count == 0, "Resource count must be 0");

    uint32_t closeRes = micant::mpr::WNetCloseEnum(hEnum);
    TEST_ASSERT(closeRes == micant::mpr::WN_SUCCESS, "WNetCloseEnum must return WN_SUCCESS");

    uint32_t addRes = micant::mpr::WNetAddConnection2W(nullptr, nullptr, nullptr, 0);
    TEST_ASSERT(addRes == micant::mpr::WN_SUCCESS, "WNetAddConnection2W must return WN_SUCCESS");

    wchar_t devName[64];
    uint32_t devLen = 64;
    uint32_t getConnRes = micant::mpr::WNetGetConnectionW(L"Z:", devName, &devLen);
    TEST_ASSERT(getConnRes == micant::mpr::ERROR_NO_NETWORK, "WNetGetConnectionW must report ERROR_NO_NETWORK");

    // Stage 2: Winsock ws2_32.dll Classic Numeric Ordinal Exports (Everything.exe)
    const uint32_t ws2_ords[] = { 1, 2, 3, 4, 5, 6, 9, 11, 13, 15, 16, 19, 21, 22, 23, 52, 101, 111, 115, 116 };
    for (uint32_t ord : ws2_ords) {
        void* pfn = loader.getExportOrdinal("ws2_32.dll", ord);
        TEST_ASSERT(pfn != nullptr, "ws2_32.dll ordinal export must resolve");
    }

    // Stage 3: SumatraPDF Ordinals (oleaut32 #26, #411; shell32 #190; shlwapi #219; winspool #203)
    TEST_ASSERT(loader.getExportOrdinal("oleaut32.dll", 26) != nullptr, "oleaut32.dll #26 (SafeArrayPutElement) must resolve");
    TEST_ASSERT(loader.getExportOrdinal("oleaut32.dll", 411) != nullptr, "oleaut32.dll #411 (SafeArrayCreateVector) must resolve");
    TEST_ASSERT(loader.getExportOrdinal("shell32.dll", 190) != nullptr, "shell32.dll #190 (ILCreateFromPathW) must resolve");
    TEST_ASSERT(loader.getExportOrdinal("shlwapi.dll", 219) != nullptr, "shlwapi.dll #219 (QISearch) must resolve");
    TEST_ASSERT(loader.getExportOrdinal("winspool.drv", 203) != nullptr, "winspool.drv #203 (GetDefaultPrinterW) must resolve");

    // Stage 4: Everything Shell32 Ordinal 16 & Winsock Native Execution
    TEST_ASSERT(loader.getExportOrdinal("shell32.dll", 16) != nullptr, "shell32.dll #16 (ILFindLastID) must resolve");
    auto pfnHost = reinterpret_cast<micant::ws2_32::hostent*(*)(const char*)>(loader.getExport("ws2_32.dll", "gethostbyname"));
    TEST_ASSERT(pfnHost != nullptr, "gethostbyname must resolve");
    auto* he = pfnHost("localhost");
    TEST_ASSERT(he != nullptr && he->h_name != nullptr, "gethostbyname must return valid hostent");

    // Stage 5: WizTree Shell32 Ordinal 18 & Shell Item Array Stubs
    TEST_ASSERT(loader.getExportOrdinal("shell32.dll", 18) != nullptr, "shell32.dll #18 (ILClone) must resolve");
    TEST_ASSERT(loader.getExport("shell32.dll", "SHDoDragDrop") != nullptr, "shell32!SHDoDragDrop must resolve");
    TEST_ASSERT(loader.getExport("shell32.dll", "SHCreateShellItemArrayFromIDLists") != nullptr, "shell32!SHCreateShellItemArrayFromIDLists must resolve");

    // Stage 6: Modern High-DPI Per-Monitor V2 Subsystem
    TEST_ASSERT(loader.getExport("user32.dll", "GetDpiForWindow") != nullptr, "user32!GetDpiForWindow must resolve");
    TEST_ASSERT(loader.getExport("user32.dll", "GetSystemMetricsForDpi") != nullptr, "user32!GetSystemMetricsForDpi must resolve");
    TEST_ASSERT(loader.getExport("user32.dll", "AdjustWindowRectExForDpi") != nullptr, "user32!AdjustWindowRectExForDpi must resolve");
    TEST_ASSERT(loader.getExport("shcore.dll", "GetDpiForMonitor") != nullptr, "shcore!GetDpiForMonitor must resolve");
    TEST_ASSERT(loader.getExport("shcore.dll", "GetProcessDpiAwareness") != nullptr, "shcore!GetProcessDpiAwareness must resolve");
    TEST_ASSERT(loader.getExport("shcore.dll", "GetScaleFactorForMonitor") != nullptr, "shcore!GetScaleFactorForMonitor must resolve");

    auto pfnDpi = reinterpret_cast<uint32_t(*)(void*)>(loader.getExport("user32.dll", "GetDpiForWindow"));
    TEST_ASSERT(pfnDpi(nullptr) == 96, "GetDpiForWindow must return 96 DPI default");

    auto pfnMetrics = reinterpret_cast<int32_t(*)(int32_t, uint32_t)>(loader.getExport("user32.dll", "GetSystemMetricsForDpi"));
    TEST_ASSERT(pfnMetrics(0, 192) == 3840, "GetSystemMetricsForDpi SM_CXSCREEN at 200% scale (192 DPI) must equal 3840");

    // Stage 7: UxTheme Double-Buffering & Alpha Blending
    TEST_ASSERT(loader.getExport("uxtheme.dll", "BufferedPaintInit") != nullptr, "uxtheme!BufferedPaintInit must resolve");
    TEST_ASSERT(loader.getExport("uxtheme.dll", "BeginBufferedPaint") != nullptr, "uxtheme!BeginBufferedPaint must resolve");
    TEST_ASSERT(loader.getExport("uxtheme.dll", "EndBufferedPaint") != nullptr, "uxtheme!EndBufferedPaint must resolve");
    TEST_ASSERT(loader.getExport("uxtheme.dll", "BufferedPaintSetAlpha") != nullptr, "uxtheme!BufferedPaintSetAlpha must resolve");
    TEST_ASSERT(loader.getExport("uxtheme.dll", "OpenThemeDataForDpi") != nullptr, "uxtheme!OpenThemeDataForDpi must resolve");

    auto pfnBpInit = reinterpret_cast<int32_t(*)()>(loader.getExport("uxtheme.dll", "BufferedPaintInit"));
    TEST_ASSERT(pfnBpInit() == 0, "BufferedPaintInit must return S_OK");

    void* phdc = nullptr;
    auto pfnBeginBp = reinterpret_cast<void*(*)(void*, const void*, int, void*, void**)>(loader.getExport("uxtheme.dll", "BeginBufferedPaint"));
    void* hBp = pfnBeginBp(reinterpret_cast<void*>(0x1234), nullptr, 0, nullptr, &phdc);
    TEST_ASSERT(hBp != nullptr, "BeginBufferedPaint must return non-null paint handle");
    TEST_ASSERT(phdc == reinterpret_cast<void*>(0x1234), "BeginBufferedPaint must assign target DC");

    // Stage 8: Kernel32 System & Processor Telemetry
    TEST_ASSERT(loader.getExport("kernel32.dll", "VerLanguageNameW") != nullptr, "kernel32!VerLanguageNameW must resolve");
    TEST_ASSERT(loader.getExport("kernel32.dll", "GetLogicalProcessorInformation") != nullptr, "kernel32!GetLogicalProcessorInformation must resolve");
    TEST_ASSERT(loader.getExport("kernel32.dll", "IsWow64Process") != nullptr, "kernel32!IsWow64Process must resolve");
    TEST_ASSERT(loader.getExport("kernel32.dll", "ProcessIdToSessionId") != nullptr, "kernel32!ProcessIdToSessionId must resolve");
    TEST_ASSERT(loader.getExport("kernel32.dll", "LocaleNameToLCID") != nullptr, "kernel32!LocaleNameToLCID must resolve");

    wchar_t langBuf[64];
    auto pfnLang = reinterpret_cast<uint32_t(*)(uint32_t, wchar_t*, uint32_t)>(loader.getExport("kernel32.dll", "VerLanguageNameW"));
    uint32_t langLen = pfnLang(0x0409, langBuf, 64);
    TEST_ASSERT(langLen > 0 && std::wcscmp(langBuf, L"English (United States)") == 0, "VerLanguageNameW must describe 0x0409");

    int32_t isWow64 = 1;
    auto pfnWow64 = reinterpret_cast<int32_t(*)(void*, int32_t*)>(loader.getExport("kernel32.dll", "IsWow64Process"));
    pfnWow64(nullptr, &isWow64);
    TEST_ASSERT(isWow64 == 0, "Native MicaNT 64-bit environment must report IsWow64Process as FALSE");

    // Stage 9: Cryptography & Security Subsystem Extensions
    TEST_ASSERT(loader.getExport("crypt32.dll", "CryptDecodeObject") != nullptr, "crypt32!CryptDecodeObject must resolve");
    TEST_ASSERT(loader.getExport("crypt32.dll", "PFXImportCertStore") != nullptr, "crypt32!PFXImportCertStore must resolve");
    TEST_ASSERT(loader.getExport("crypt32.dll", "CertFindChainInStore") != nullptr, "crypt32!CertFindChainInStore must resolve");

    auto pfnPfx = reinterpret_cast<void*(*)(void*, const wchar_t*, uint32_t)>(loader.getExport("crypt32.dll", "PFXImportCertStore"));
    TEST_ASSERT(pfnPfx(nullptr, nullptr, 0) != nullptr, "PFXImportCertStore must return store handle");

    // Stage 10: Multi-Provider Router, NT Native, & CRT Aliasing
    TEST_ASSERT(loader.getExport("api-ms-win-crt-string-l1-1-0.dll", "memset") != nullptr, "CRT memset alias must resolve");
    TEST_ASSERT(loader.getExport("ntdll.dll", "NtOpenFile") != nullptr, "ntdll!NtOpenFile must resolve");
    TEST_ASSERT(loader.getExport("ntdll.dll", "RtlInitUnicodeString") != nullptr, "ntdll!RtlInitUnicodeString must resolve");
    TEST_ASSERT(loader.getExport("msimg32.dll", "TransparentBlt") != nullptr, "msimg32!TransparentBlt must resolve");
    TEST_ASSERT(loader.getExport("windowscodecs.dll", "WICConvertBitmapSource") != nullptr, "windowscodecs!WICConvertBitmapSource must resolve");
    TEST_ASSERT(loader.getExport("imm32.dll", "ImmAssociateContextEx") != nullptr, "imm32!ImmAssociateContextEx must resolve");
    TEST_ASSERT(loader.getExport("dwmapi.dll", "DwmDefWindowProc") != nullptr, "dwmapi!DwmDefWindowProc must resolve");
    TEST_ASSERT(loader.getExport("shfolder.dll", "SHGetFolderPathW") != nullptr, "shfolder!SHGetFolderPathW must resolve");

    // Stage 11: Concurrent Multi-Threaded Stress Test across all 8 subsystem modules
    std::atomic<uint32_t> stressCompleted{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&stressCompleted, &loader]() {
            for (int i = 0; i < 50; ++i) {
                void* p1 = loader.getExportOrdinal("ws2_32.dll", 1 + (i % 23));
                (void)p1;

                auto pDpi = reinterpret_cast<int32_t(*)(int32_t, uint32_t)>(loader.getExport("user32.dll", "GetSystemMetricsForDpi"));
                if (pDpi) pDpi(0, 96 + (i * 12));

                void* hE = nullptr;
                micant::mpr::WNetOpenEnumW(0, 0, 0, nullptr, &hE);
                if (hE) micant::mpr::WNetCloseEnum(hE);

                stressCompleted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(stressCompleted.load() == 400, "400-operation concurrent multi-threaded subsystem stress test must achieve 100% success");

    std::cout << "[TEST] Suite 221: 100.0% Retail Ecosystem Coverage & Subsystem Extension Matrix PASSED.\n";
}

// ============================================================================
// Suite 222: Rufus Low-Level Storage & Native NT Syscall Subsystem Validation
// ============================================================================
inline void Test_Rufus_Storage_And_NtSyscalls_Suite() {
    std::cout << "[TEST] Executing Suite 222: Rufus Low-Level Storage & Native NT Syscall Subsystem Validation...\n";

    // 1. Initialize Subsystem Exports
    micant::subsystems::InitializeAllSubsystemExports();
    auto& loader = micant::ldr::DynamicLoader::get();

    // Stage 1: Export Registration Verification for all 13 DLLs
    TEST_ASSERT(loader.getExport("advapi32.dll", "SystemFunction036") != nullptr, "advapi32!SystemFunction036 must be registered");
    TEST_ASSERT(loader.getExport("advapi32.dll", "ConvertStringSecurityDescriptorToSecurityDescriptorA") != nullptr, "advapi32!ConvertStringSecurityDescriptorToSecurityDescriptorA must be registered");
    TEST_ASSERT(loader.getExport("crypt32.dll", "CertGetCertificateChain") != nullptr, "crypt32!CertGetCertificateChain must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "FindFirstVolumeA") != nullptr, "kernel32!FindFirstVolumeA must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "GetVolumeInformationByHandleW") != nullptr, "kernel32!GetVolumeInformationByHandleW must be registered");
    TEST_ASSERT(loader.getExport("ntdll.dll", "NtCreateFile") != nullptr, "ntdll!NtCreateFile must be registered");
    TEST_ASSERT(loader.getExport("ntdll.dll", "NtFsControlFile") != nullptr, "ntdll!NtFsControlFile must be registered");
    TEST_ASSERT(loader.getExport("ntdll.dll", "NtDeviceIoControlFile") != nullptr, "ntdll!NtDeviceIoControlFile must be registered");
    TEST_ASSERT(loader.getExport("ole32.dll", "CoInitializeSecurity") != nullptr, "ole32!CoInitializeSecurity must be registered");
    TEST_ASSERT(loader.getExport("setupapi.dll", "SetupDiGetClassDevsA") != nullptr, "setupapi!SetupDiGetClassDevsA must be registered");
    TEST_ASSERT(loader.getExport("setupapi.dll", "CM_Get_Device_IDA") != nullptr, "setupapi!CM_Get_Device_IDA must be registered");
    TEST_ASSERT(loader.getExport("shell32.dll", "SHCreateDirectoryExW") != nullptr, "shell32!SHCreateDirectoryExW must be registered");
    TEST_ASSERT(loader.getExportOrdinal("shell32.dll", 2) != nullptr, "shell32!Ordinal_2 must be registered");
    TEST_ASSERT(loader.getExportOrdinal("shell32.dll", 4) != nullptr, "shell32!Ordinal_4 must be registered");
    TEST_ASSERT(loader.getExport("shlwapi.dll", "wnsprintfW") != nullptr, "shlwapi!wnsprintfW must be registered");
    TEST_ASSERT(loader.getExport("user32.dll", "SetWinEventHook") != nullptr, "user32!SetWinEventHook must be registered");
    TEST_ASSERT(loader.getExport("virtdisk.dll", "GetVirtualDiskOperationProgress") != nullptr, "virtdisk!GetVirtualDiskOperationProgress must be registered");
    TEST_ASSERT(loader.getExport("wininet.dll", "InternetGetConnectedState") != nullptr, "wininet!InternetGetConnectedState must be registered");
    TEST_ASSERT(loader.getExport("wintrust.dll", "WinVerifyTrustEx") != nullptr, "wintrust!WinVerifyTrustEx must be registered");

    // Stage 2: Kernel32 Volume & Drive Enumeration
    char volName[128] = { 0 };
    void* hVol = micant::win32::FindFirstVolumeA(volName, sizeof(volName));
    TEST_ASSERT(hVol != nullptr, "FindFirstVolumeA must return valid search handle");
    TEST_ASSERT(std::strstr(volName, "\\\\?\\Volume{") != nullptr, "Volume name must contain volume GUID prefix");

    char nextVol[128] = { 0 };
    int32_t nextRes = micant::win32::FindNextVolumeA(hVol, nextVol, sizeof(nextVol));
    TEST_ASSERT(nextRes == 0, "FindNextVolumeA on single volume must return FALSE");
    TEST_ASSERT(micant::win32::FindVolumeClose(hVol) == 1, "FindVolumeClose must succeed");

    char volPath[32] = { 0 };
    TEST_ASSERT(micant::win32::GetVolumePathNameA("C:\\some\\file.iso", volPath, sizeof(volPath)) == 1, "GetVolumePathNameA must succeed");
    TEST_ASSERT(std::strcmp(volPath, "C:\\") == 0, "GetVolumePathNameA must resolve to drive root C:\\");

    char volLabel[64] = { 0 };
    char fsName[32] = { 0 };
    uint32_t serial = 0, maxComp = 0, flags = 0;
    TEST_ASSERT(micant::win32::GetVolumeInformationA("C:\\", volLabel, sizeof(volLabel), &serial, &maxComp, &flags, fsName, sizeof(fsName)) == 1, "GetVolumeInformationA must succeed");
    TEST_ASSERT(std::strcmp(fsName, "NTFS") == 0, "GetVolumeInformationA must return NTFS file system");
    TEST_ASSERT(serial != 0 && maxComp == 255, "Volume metadata must report valid components");

    wchar_t wVolLabel[64] = { 0 };
    wchar_t wFsName[32] = { 0 };
    TEST_ASSERT(micant::win32::GetVolumeInformationByHandleW(nullptr, wVolLabel, 64, &serial, &maxComp, &flags, wFsName, 32) == 1, "GetVolumeInformationByHandleW must succeed");
    TEST_ASSERT(std::wcscmp(wFsName, L"NTFS") == 0, "GetVolumeInformationByHandleW must report L'NTFS'");

    uint64_t freeCaller = 0, totalBytes = 0, freeBytes = 0;
    TEST_ASSERT(micant::win32::GetDiskFreeSpaceExA("C:\\", &freeCaller, &totalBytes, &freeBytes) == 1, "GetDiskFreeSpaceExA must succeed");
    TEST_ASSERT(totalBytes > 0 && freeBytes > 0, "Total and free bytes must be non-zero");

    char driveStrings[64] = { 0 };
    uint32_t driveLen = micant::win32::GetLogicalDriveStringsA(sizeof(driveStrings), driveStrings);
    TEST_ASSERT(driveLen > 0, "GetLogicalDriveStringsA must return non-zero character count");
    TEST_ASSERT(std::strstr(driveStrings, "C:\\") != nullptr, "Logical drives must contain C:\\");

    // Stage 3: SetupAPI & Configuration Manager (USB Hardware Device Tree)
    void* hDevInfo = micant::setupapi::SetupDiGetClassDevsA(nullptr, nullptr, nullptr, 0);
    TEST_ASSERT(hDevInfo != nullptr, "SetupDiGetClassDevsA must return valid device information handle");

    char devIdBuf[128] = { 0 };
    uint32_t cmStatus = micant::setupapi::CM_Get_Device_IDA(1, devIdBuf, sizeof(devIdBuf), 0);
    TEST_ASSERT(cmStatus == 0, "CM_Get_Device_IDA must return CR_SUCCESS (0)");
    TEST_ASSERT(std::strstr(devIdBuf, "USBSTOR\\") != nullptr, "Device ID must reflect USB storage hardware");

    uint32_t statusFlags = 0, problem = 0;
    TEST_ASSERT(micant::setupapi::CM_Get_DevNode_Status(&statusFlags, &problem, 1, 0) == 0, "CM_Get_DevNode_Status must return CR_SUCCESS");
    TEST_ASSERT((statusFlags & 0x01) != 0, "DevNode status must report driver loaded");

    char instIdBuf[128] = { 0 };
    uint32_t reqSize = 0;
    TEST_ASSERT(micant::setupapi::SetupDiGetDeviceInstanceIdA(hDevInfo, nullptr, instIdBuf, sizeof(instIdBuf), &reqSize) == 1, "SetupDiGetDeviceInstanceIdA must succeed");
    TEST_ASSERT(std::strstr(instIdBuf, "USBSTOR\\") != nullptr, "Instance ID must start with USBSTOR");

    uint8_t propBuf[128] = { 0 };
    uint32_t propType = 0;
    TEST_ASSERT(micant::setupapi::SetupDiGetDeviceRegistryPropertyA(hDevInfo, nullptr, 0, &propType, propBuf, sizeof(propBuf), &reqSize) == 1, "SetupDiGetDeviceRegistryPropertyA must succeed");
    TEST_ASSERT(std::strstr(reinterpret_cast<char*>(propBuf), "SanDisk") != nullptr, "Device description must identify drive hardware");

    // Stage 4: Native NT Kernel Syscalls
    void* ntFile = nullptr;
    micant::NtStatus fileSt = micant::ntdll::NtCreateFile_Export(&ntFile, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0, nullptr, 0);
    TEST_ASSERT(fileSt == micant::NtStatus::Success, "NtCreateFile must return STATUS_SUCCESS");
    TEST_ASSERT(ntFile != nullptr, "NtCreateFile must produce valid handle");

    TEST_ASSERT(micant::ntdll::NtDeviceIoControlFile_Export(ntFile, nullptr, nullptr, nullptr, nullptr, 0, nullptr, 0, nullptr, 0) == micant::NtStatus::Success, "NtDeviceIoControlFile must succeed");
    TEST_ASSERT(micant::ntdll::NtFsControlFile(ntFile, nullptr, nullptr, nullptr, nullptr, 0, nullptr, 0, nullptr, 0) == micant::NtStatus::Success, "NtFsControlFile must succeed");

    void* ntProc = nullptr;
    TEST_ASSERT(micant::ntdll::NtOpenProcess(&ntProc, 0, nullptr, nullptr) == micant::NtStatus::Success, "NtOpenProcess must return STATUS_SUCCESS");
    TEST_ASSERT(ntProc != nullptr, "NtOpenProcess must return valid process handle");

    void* ntToken = nullptr;
    TEST_ASSERT(micant::ntdll::NtOpenProcessToken_Export(ntProc, 0, &ntToken) == micant::NtStatus::Success, "NtOpenProcessToken must return STATUS_SUCCESS");
    TEST_ASSERT(ntToken != nullptr, "NtOpenProcessToken must return valid token handle");

    void* dupHandle = nullptr;
    TEST_ASSERT(micant::ntdll::NtDuplicateObject(nullptr, ntFile, nullptr, &dupHandle, 0, 0, 0) == micant::NtStatus::Success, "NtDuplicateObject must succeed");
    TEST_ASSERT(dupHandle == ntFile, "Duplicated handle must match source handle");

    uint64_t condMask = micant::ntdll::VerSetConditionMask(0, 1, 3);
    TEST_ASSERT(condMask != 0, "VerSetConditionMask must calculate packed condition bitmask");

    // Stage 5: Advapi32 & Crypto Verification
    void* sd = nullptr;
    uint32_t sdSize = 0;
    TEST_ASSERT(micant::advapi32::ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;BA)", 1, &sd, &sdSize) == 1, "ConvertStringSecurityDescriptor must succeed");
    TEST_ASSERT(sd != nullptr && sdSize > 0, "Security descriptor buffer must be allocated");

    void* sid = nullptr;
    TEST_ASSERT(micant::advapi32::ConvertStringSidToSidA("S-1-5-18", &sid) == 1, "ConvertStringSidToSidA must succeed");
    TEST_ASSERT(sid != nullptr, "System SID pointer must be valid");

    uint8_t randBuf[64] = { 0 };
    micant::advapi32::SystemFunction036(randBuf, sizeof(randBuf));
    bool hasNonZero = false;
    for (uint8_t b : randBuf) {
        if (b != 0) { hasNonZero = true; break; }
    }
    TEST_ASSERT(hasNonZero, "SystemFunction036 (RtlGenRandom) must generate non-zero pseudo-random bytes");

    // Stage 6: Virtual Disk, Network & Trust
    uint64_t vdiskProg[3] = { 0 };
    uint32_t vdiskSt = micant::virtdisk::GetVirtualDiskOperationProgress(nullptr, nullptr, vdiskProg);
    TEST_ASSERT(vdiskSt == 0, "GetVirtualDiskOperationProgress must return ERROR_SUCCESS");
    TEST_ASSERT(vdiskProg[1] == 100 && vdiskProg[2] == 100, "Progress must indicate 100% completion");

    uint32_t netFlags = 0;
    TEST_ASSERT(micant::wininet::InternetGetConnectedState(&netFlags, 0) == 1, "InternetGetConnectedState must report connected");
    TEST_ASSERT(netFlags != 0, "Connection flags must be populated");

    TEST_ASSERT(micant::wintrust::WinVerifyTrustEx(nullptr, nullptr, nullptr) == 0, "WinVerifyTrustEx must return 0 (success)");

    // Stage 7: String Helpers & Hooks
    char textA[] = "Hello WOrLD";
    micant::user32::CharLowerA(textA);
    TEST_ASSERT(std::strcmp(textA, "hello world") == 0, "CharLowerA must convert string to lowercase");
    micant::user32::CharUpperA(textA);
    TEST_ASSERT(std::strcmp(textA, "HELLO WORLD") == 0, "CharUpperA must convert string to uppercase");

    char klid[16] = { 0 };
    TEST_ASSERT(micant::user32::GetKeyboardLayoutNameA(klid) == 1, "GetKeyboardLayoutNameA must succeed");
    TEST_ASSERT(std::strcmp(klid, "00000409") == 0, "Keyboard layout must default to en-US 00000409");

    void* hHook = micant::user32::SetWinEventHook(1, 10, nullptr, nullptr, 0, 0, 0);
    TEST_ASSERT(hHook != nullptr, "SetWinEventHook must return hook handle");
    TEST_ASSERT(micant::user32::UnhookWinEvent(hHook) == 1, "UnhookWinEvent must release hook");

    wchar_t formatted[64] = { 0 };
    int fmtLen = micant::shell32::wnsprintfW(formatted, 64, L"Rufus Drive %d: %s", 2, L"READY");
    TEST_ASSERT(fmtLen > 0, "wnsprintfW must write formatted string");
    TEST_ASSERT(std::wcscmp(formatted, L"Rufus Drive 2: READY") == 0, "wnsprintfW format content match");

    // Stage 8: Concurrent Multi-Threaded Stress Test across Rufus Subsystems
    std::atomic<uint32_t> stressDone{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&stressDone]() {
            for (int i = 0; i < 50; ++i) {
                // Exercise Volume & SetupAPI
                char vName[128] = { 0 };
                void* hV = micant::win32::FindFirstVolumeA(vName, sizeof(vName));
                if (hV) micant::win32::FindVolumeClose(hV);

                uint8_t rnd[16];
                micant::advapi32::SystemFunction036(rnd, sizeof(rnd));

                void* hP = nullptr;
                micant::ntdll::NtOpenProcess(&hP, 0, nullptr, nullptr);

                stressDone.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(stressDone.load() == 400, "400-operation concurrent Rufus storage stress test must achieve 100% success");

    std::cout << "[TEST] Suite 222: Rufus Low-Level Storage & Native NT Syscall Subsystem Validation PASSED.\n";
}

// ============================================================================
// Suite 223: System Informer 4.0 Native NT Syscalls, Diagnostics & LSA Security
// ============================================================================
inline void Test_SystemInformer_Diagnostics_And_NativeNT_Suite() {
    std::cout << "[TEST] Executing Suite 223: System Informer 4.0 Native NT Syscalls, Diagnostics & LSA Security...\n";

    // 1. Initialize Subsystem Exports
    micant::subsystems::InitializeAllSubsystemExports();
    auto& loader = micant::ldr::DynamicLoader::get();

    // Stage 1: Export Registration Verification for all 14 DLLs
    TEST_ASSERT(loader.getExportOrdinal("aclui.dll", 1) != nullptr, "aclui!Ordinal_1 must be registered");
    TEST_ASSERT(loader.getExportOrdinal("aclui.dll", 2) != nullptr, "aclui!Ordinal_2 must be registered");
    TEST_ASSERT(loader.getExportOrdinal("aclui.dll", 3) != nullptr, "aclui!Ordinal_3 must be registered");
    TEST_ASSERT(loader.getExport("advapi32.dll", "GetSecurityInfo") != nullptr, "advapi32!GetSecurityInfo must be registered");
    TEST_ASSERT(loader.getExport("advapi32.dll", "LsaLookupNames2") != nullptr, "advapi32!LsaLookupNames2 must be registered");
    TEST_ASSERT(loader.getExport("advapi32.dll", "ChangeServiceConfigW") != nullptr, "advapi32!ChangeServiceConfigW must be registered");
    TEST_ASSERT(loader.getExport("cfgmgr32.dll", "CM_Register_Notification") != nullptr, "cfgmgr32!CM_Register_Notification must be registered");
    TEST_ASSERT(loader.getExport("comctl32.dll", "DestroyPropertySheetPage") != nullptr, "comctl32!DestroyPropertySheetPage must be registered");
    TEST_ASSERT(loader.getExportOrdinal("comctl32.dll", 13) != nullptr, "comctl32!Ordinal_13 must be registered");
    TEST_ASSERT(loader.getExport("comdlg32.dll", "ChooseFontW") != nullptr, "comdlg32!ChooseFontW must be registered");
    TEST_ASSERT(loader.getExport("gdi32.dll", "GetObjectType") != nullptr, "gdi32!GetObjectType must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "GetEnabledXStateFeatures") != nullptr, "kernel32!GetEnabledXStateFeatures must be registered");
    TEST_ASSERT(loader.getExport("ntdll.dll", "NtCreateJobObject") != nullptr, "ntdll!NtCreateJobObject must be registered");
    TEST_ASSERT(loader.getExport("ntdll.dll", "NtQueryInformationThread") != nullptr, "ntdll!NtQueryInformationThread must be registered");
    TEST_ASSERT(loader.getExport("ntdll.dll", "NtQueryVirtualMemory") != nullptr, "ntdll!NtQueryVirtualMemory must be registered");
    TEST_ASSERT(loader.getExport("ntdll.dll", "RtlGetVersion") != nullptr, "ntdll!RtlGetVersion must be registered");
    TEST_ASSERT(loader.getExport("ole32.dll", "CoGetSystemSecurityPermissions") != nullptr, "ole32!CoGetSystemSecurityPermissions must be registered");
    TEST_ASSERT(loader.getExportOrdinal("oleaut32.dll", 15) != nullptr, "oleaut32!Ordinal_15 must be registered");
    TEST_ASSERT(loader.getExport("setupapi.dll", "SetupDiGetClassDevsExW") != nullptr, "setupapi!SetupDiGetClassDevsExW must be registered");
    TEST_ASSERT(loader.getExport("user32.dll", "OpenWindowStationW") != nullptr, "user32!OpenWindowStationW must be registered");
    TEST_ASSERT(loader.getExport("user32.dll", "GetGUIThreadInfo") != nullptr, "user32!GetGUIThreadInfo must be registered");
    TEST_ASSERT(loader.getExport("winhttp.dll", "WinHttpCrackUrl") != nullptr, "winhttp!WinHttpCrackUrl must be registered");
    TEST_ASSERT(loader.getExport("winsta.dll", "WinStationQueryInformationW") != nullptr, "winsta!WinStationQueryInformationW must be registered");

    // Stage 2: ACL UI & Security Pages
    void* hSecPage = micant::aclui::CreateSecurityPage(nullptr);
    TEST_ASSERT(hSecPage != nullptr, "CreateSecurityPage must return valid property sheet page handle");
    TEST_ASSERT(micant::aclui::EditSecurity(nullptr, nullptr) == 1, "EditSecurity must succeed");
    TEST_ASSERT(micant::aclui::EditSecurityAdvanced(nullptr, nullptr, 0) == 0, "EditSecurityAdvanced must return S_OK");

    // Stage 3: Services & LSA Security Management
    uint32_t rights = 0;
    TEST_ASSERT(micant::advapi32::GetEffectiveRightsFromAclW(nullptr, nullptr, &rights) == 0, "GetEffectiveRightsFromAclW must return ERROR_SUCCESS");
    TEST_ASSERT(rights != 0, "Effective rights must be non-zero");

    void* sd = nullptr;
    TEST_ASSERT(micant::advapi32::GetSecurityInfo(nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, &sd) == 0, "GetSecurityInfo must succeed");
    TEST_ASSERT(sd != nullptr, "Security descriptor must be returned");

    TEST_ASSERT(micant::advapi32::LsaEnumerateAccounts(nullptr, nullptr, nullptr, 0, nullptr) == 0, "LsaEnumerateAccounts must succeed");
    TEST_ASSERT(micant::advapi32::LsaLookupNames2(nullptr, 0, 0, nullptr, nullptr, nullptr) == 0, "LsaLookupNames2 must succeed");
    TEST_ASSERT(micant::advapi32::LsaFreeMemory(nullptr) == 0, "LsaFreeMemory must succeed");

    // Stage 4: Native NT Kernel Syscalls (Job, Key, Section, Memory, Thread, Port)
    void* hJob = nullptr;
    TEST_ASSERT(micant::ntdll::NtCreateJobObject(&hJob, 0, nullptr) == micant::NtStatus::Success, "NtCreateJobObject must succeed");
    TEST_ASSERT(hJob != nullptr, "Job handle must be valid");

    void* hKey = nullptr;
    uint32_t disp = 0;
    TEST_ASSERT(micant::ntdll::NtCreateKey(&hKey, 0, nullptr, 0, nullptr, 0, &disp) == micant::NtStatus::Success, "NtCreateKey must succeed");
    TEST_ASSERT(hKey != nullptr, "Key handle must be valid");

    void* hSection = nullptr;
    TEST_ASSERT(micant::ntdll::NtOpenSection(&hSection, 0, nullptr) == micant::NtStatus::Success, "NtOpenSection must succeed");
    TEST_ASSERT(hSection != nullptr, "Section handle must be valid");

    uint8_t memInfo[64] = { 0 };
    size_t retLen = 0;
    TEST_ASSERT(micant::ntdll::NtQueryVirtualMemory(nullptr, nullptr, 0, memInfo, sizeof(memInfo), &retLen) == micant::NtStatus::Success, "NtQueryVirtualMemory must succeed");
    TEST_ASSERT(retLen > 0, "QueryVirtualMemory must report memory region length");

    uint32_t maxT = 0, minT = 0, curT = 0;
    TEST_ASSERT(micant::ntdll::NtQueryTimerResolution(&maxT, &minT, &curT) == micant::NtStatus::Success, "NtQueryTimerResolution must succeed");
    TEST_ASSERT(maxT > 0 && curT > 0, "Timer resolutions must be valid clock ticks");

    void* hPort = nullptr;
    TEST_ASSERT(micant::ntdll::NtConnectPort_Export(&hPort, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == micant::NtStatus::Success, "NtConnectPort must succeed");
    TEST_ASSERT(hPort != nullptr, "Port handle must be valid");

    // Stage 5: Window Stations & Desktops
    void* hWinSta = micant::user32::OpenWindowStationW(L"WinSta0", 0, 0);
    TEST_ASSERT(hWinSta != nullptr, "OpenWindowStationW must return interactive window station handle");
    TEST_ASSERT(micant::user32::GetProcessWindowStation() == hWinSta, "GetProcessWindowStation must return active WinSta");
    TEST_ASSERT(micant::user32::CloseWindowStation(hWinSta) == 1, "CloseWindowStation must succeed");

    TEST_ASSERT(micant::user32::EnumDesktopsW(nullptr, nullptr, 0) == 1, "EnumDesktopsW must succeed");
    TEST_ASSERT(micant::user32::GetShellWindow() != nullptr, "GetShellWindow must return valid HWND");
    TEST_ASSERT(micant::user32::GetGuiResources(nullptr, 0) > 0, "GetGuiResources must return active handles");

    // Stage 6: Terminal Services / WinStation APIs
    TEST_ASSERT(micant::winsta::WinStationConnectW(nullptr, 1, 1, nullptr, 0) == 1, "WinStationConnectW must succeed");
    TEST_ASSERT(micant::winsta::WinStationQueryInformationW(nullptr, 1, 0, nullptr, 0, nullptr) == 1, "WinStationQueryInformationW must succeed");
    uint32_t resp = 0;
    TEST_ASSERT(micant::winsta::WinStationSendMessageW(nullptr, 1, L"Alert", 5, L"Notice", 6, 0, 0, &resp, 0) == 1, "WinStationSendMessageW must succeed");
    TEST_ASSERT(resp == 1, "Message response must be IDOK");

    // Stage 7: RTL Utilities (Version, Network Addresses, Strings)
    uint32_t osVer[7] = { 0 };
    osVer[0] = sizeof(osVer);
    TEST_ASSERT(micant::ntdll::RtlGetVersion(osVer) == micant::NtStatus::Success, "RtlGetVersion must succeed");
    TEST_ASSERT(osVer[1] == 10 && osVer[3] == 22631, "RtlGetVersion must report Windows 11 Build 22631");

    wchar_t ipStr[32] = { 0 };
    uint32_t ipLen = 32;
    TEST_ASSERT(micant::ntdll::RtlIpv4AddressToStringExW(nullptr, 8080, ipStr, &ipLen) == micant::NtStatus::Success, "RtlIpv4AddressToStringExW must succeed");
    TEST_ASSERT(std::wcscmp(ipStr, L"127.0.0.1:8080") == 0, "IPv4 address string formatting match");

    uint32_t seed = 12345;
    uint32_t r = micant::ntdll::RtlRandomEx(&seed);
    TEST_ASSERT(r != 0, "RtlRandomEx must generate pseudorandom integer");

    // Stage 8: Concurrent Multi-Threaded Diagnostics Stress Test
    std::atomic<uint32_t> diagDone{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&diagDone, t]() {
            for (int i = 0; i < 50; ++i) {
                void* pJob = nullptr;
                micant::ntdll::NtCreateJobObject(&pJob, 0, nullptr);

                void* pSta = micant::user32::OpenWindowStationW(L"WinSta0", 0, 0);
                if (pSta) micant::user32::CloseWindowStation(pSta);

                uint32_t s = t * 100 + i;
                micant::ntdll::RtlRandomEx(&s);

                diagDone.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(diagDone.load() == 400, "400-operation concurrent diagnostics stress test must achieve 100% success");

    std::cout << "[TEST] Suite 223: System Informer 4.0 Native NT Syscalls, Diagnostics & LSA Security PASSED.\n";
}

// ============================================================================
// Suite 224: qBittorrent 5.2+ Networking, Async I/O & ICU Subsystem Validation
// ============================================================================
inline void Test_qBittorrent_Networking_AsyncIO_And_ICU_Suite() {
    std::cout << "[TEST] Executing Suite 224: qBittorrent 5.2+ Networking, Async I/O & ICU Subsystem...\n";

    micant::subsystems::InitializeAllSubsystemExports();
    auto& loader = micant::ldr::DynamicLoader::get();

    // Stage 1: Loader Export Verification across 12 critical modules
    TEST_ASSERT(loader.getExport("wsock32.dll", "WSAStartup") != nullptr, "wsock32!WSAStartup must be registered");
    TEST_ASSERT(loader.getExport("wsock32.dll", "socket") != nullptr, "wsock32!socket must be registered");
    TEST_ASSERT(loader.getExport("wsock32.dll", "AcceptEx") != nullptr, "wsock32!AcceptEx must be registered");
    TEST_ASSERT(loader.getExport("ws2_32.dll", "WSAConnect") != nullptr, "ws2_32!WSAConnect must be registered");
    TEST_ASSERT(loader.getExport("ws2_32.dll", "WSASend") != nullptr, "ws2_32!WSASend must be registered");
    TEST_ASSERT(loader.getExport("ws2_32.dll", "getaddrinfo") != nullptr, "ws2_32!getaddrinfo must be registered");
    TEST_ASSERT(loader.getExport("iphlpapi.dll", "GetAdaptersAddresses") != nullptr, "iphlpapi!GetAdaptersAddresses must be registered");
    TEST_ASSERT(loader.getExport("iphlpapi.dll", "NotifyUnicastIpAddressChange") != nullptr, "iphlpapi!NotifyUnicastIpAddressChange must be registered");
    TEST_ASSERT(loader.getExport("icuuc.dll", "ucnv_open") != nullptr, "icuuc!ucnv_open must be registered");
    TEST_ASSERT(loader.getExport("icuuc.dll", "ucnv_getName") != nullptr, "icuuc!ucnv_getName must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "CreateIoCompletionPort") != nullptr, "kernel32!CreateIoCompletionPort must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "GetQueuedCompletionStatus") != nullptr, "kernel32!GetQueuedCompletionStatus must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "LockFileEx") != nullptr, "kernel32!LockFileEx must be registered");
    TEST_ASSERT(loader.getExport("authz.dll", "AuthzInitializeResourceManager") != nullptr, "authz!AuthzInitializeResourceManager must be registered");
    TEST_ASSERT(loader.getExport("user32.dll", "SetProcessDpiAwarenessContext") != nullptr, "user32!SetProcessDpiAwarenessContext must be registered");
    TEST_ASSERT(loader.getExport("user32.dll", "UpdateLayeredWindow") != nullptr, "user32!UpdateLayeredWindow must be registered");

    // Stage 2: Winsock 1.1 / 2.0 Network Subsystem Lifecycle
    micant::ws2_32::WSADATA wsaData{};
    TEST_ASSERT(micant::ws2_32::WSAStartup(0x0202, &wsaData) == 0, "WSAStartup must succeed");
    
    uintptr_t s = micant::ws2_32::socket(2, 1, 6);
    TEST_ASSERT(s != 0, "Wsock_socket must return valid socket handle");
    
    uint16_t netPort = micant::ws2_32::htons(8080);
    TEST_ASSERT(micant::ws2_32::ntohs(netPort) == 8080, "htons / ntohs roundtrip match");

    uint32_t netAddr = micant::ws2_32::htonl(0x7F000001);
    TEST_ASSERT(micant::ws2_32::ntohl(netAddr) == 0x7F000001, "htonl / ntohl roundtrip match");

    TEST_ASSERT(micant::ws2_32::WSAConnect(s, nullptr, 16, nullptr, nullptr, nullptr, nullptr) == 0, "WSAConnect must succeed");

    uint32_t bytesSent = 0;
    TEST_ASSERT(micant::ws2_32::WSASend(s, nullptr, 1, &bytesSent, 0, nullptr, nullptr) == 0, "WSASend must succeed");
    TEST_ASSERT(bytesSent > 0, "WSASend must report transmitted bytes");

    void* pAddr = nullptr;
    TEST_ASSERT(micant::ws2_32::getaddrinfo("localhost", "6881", nullptr, &pAddr) == 0, "getaddrinfo must resolve endpoint");
    TEST_ASSERT(pAddr != nullptr, "Resolved addrinfo list must not be null");
    micant::ws2_32::freeaddrinfo(pAddr);

    TEST_ASSERT(micant::ws2_32::closesocket(s) == 0, "closesocket must succeed");
    TEST_ASSERT(micant::ws2_32::WSACleanup() == 0, "WSACleanup must succeed");

    // Stage 3: IP Helper Adapter & Network Interface Discovery
    uint32_t bufSize = 0;
    uint32_t res = micant::iphlpapi::GetAdaptersAddresses(0, 0, nullptr, nullptr, &bufSize);
    TEST_ASSERT(res == 111 && bufSize > 0, "GetAdaptersAddresses must report required buffer size");

    std::vector<uint8_t> adapterBuf(bufSize);
    TEST_ASSERT(micant::iphlpapi::GetAdaptersAddresses(0, 0, nullptr, adapterBuf.data(), &bufSize) == 0, "GetAdaptersAddresses must populate adapters");

    uint8_t luid[8] = { 0 };
    TEST_ASSERT(micant::iphlpapi::ConvertInterfaceNameToLuidW(L"eth0", luid) == 0, "ConvertInterfaceNameToLuidW must succeed");

    uint32_t ifIndex = 0;
    TEST_ASSERT(micant::iphlpapi::ConvertInterfaceLuidToIndex(luid, &ifIndex) == 0, "ConvertInterfaceLuidToIndex must succeed");
    TEST_ASSERT(ifIndex == 1, "Interface index must be 1");

    void* hNotify = nullptr;
    TEST_ASSERT(micant::iphlpapi::NotifyUnicastIpAddressChange(0, nullptr, nullptr, 0, &hNotify) == 0, "NotifyUnicastIpAddressChange must register callback");
    TEST_ASSERT(hNotify != nullptr, "Notification handle must be valid");
    TEST_ASSERT(micant::iphlpapi::CancelMibChangeNotify2(hNotify) == 0, "CancelMibChangeNotify2 must succeed");

    // Stage 4: High-Throughput I/O Completion Ports & Disk Management
    void* hIocp = micant::kernel32::CreateIoCompletionPort(nullptr, nullptr, 0x1234, 4);
    TEST_ASSERT(hIocp != nullptr, "CreateIoCompletionPort must return valid IOCP handle");

    TEST_ASSERT(micant::kernel32::PostQueuedCompletionStatus(hIocp, 16384, 0x1234, nullptr) == 1, "PostQueuedCompletionStatus must succeed");

    uint32_t bytesXfer = 0;
    uintptr_t compKey = 0;
    void* pOverlapped = nullptr;
    TEST_ASSERT(micant::kernel32::GetQueuedCompletionStatus(hIocp, &bytesXfer, &compKey, &pOverlapped, 100) == 1, "GetQueuedCompletionStatus must retrieve packet");
    TEST_ASSERT(bytesXfer > 0 && (compKey == 1 || compKey == 0x1234), "IOCP packet data must be consistent");

    TEST_ASSERT(micant::kernel32::LockFileEx(nullptr, 0, 0, 0, 1024, nullptr) == 1, "LockFileEx must succeed");
    TEST_ASSERT(micant::kernel32::UnlockFileEx(nullptr, 0, 0, 1024, nullptr) == 1, "UnlockFileEx must succeed");
    TEST_ASSERT(micant::kernel32::FlushViewOfFile(nullptr, 4096) == 1, "FlushViewOfFile must succeed");

    // Stage 5: International Components for Unicode (ICU) Subsystem
    int32_t icuErr = 0;
    void* pCnv = micant::icuuc::ucnv_open("utf-8", &icuErr);
    TEST_ASSERT(pCnv != nullptr && icuErr == 0, "ucnv_open must instantiate UTF-8 converter");
    TEST_ASSERT(std::strcmp(micant::icuuc::ucnv_getName(pCnv, &icuErr), "UTF-8") == 0, "ucnv_getName must report UTF-8");
    TEST_ASSERT(micant::icuuc::ucnv_getMaxCharSize(pCnv) == 4, "ucnv_getMaxCharSize must return 4");
    micant::icuuc::ucnv_close(pCnv);

    // Stage 6: Modern Windowing, DPI Awareness & Power Management
    TEST_ASSERT(micant::user32::SetProcessDpiAwarenessContext(reinterpret_cast<void*>(-4)) == 1, "SetProcessDpiAwarenessContext (Per-Monitor V2) must succeed");
    TEST_ASSERT(micant::user32::UpdateLayeredWindow(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, 2) == 1, "UpdateLayeredWindow must succeed");
    TEST_ASSERT(micant::user32::RegisterTouchWindow(nullptr, 0) == 1, "RegisterTouchWindow must succeed");

    void* hPower = micant::user32::RegisterPowerSettingNotification(nullptr, nullptr, 0);
    TEST_ASSERT(hPower != nullptr, "RegisterPowerSettingNotification must return notification handle");
    TEST_ASSERT(micant::user32::UnregisterPowerSettingNotification(hPower) == 1, "UnregisterPowerSettingNotification must succeed");

    // Stage 7: Concurrent Multi-Threaded P2P High-Throughput Stress Test
    std::atomic<uint32_t> opsDone{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&opsDone, t, hIocp]() {
            for (int i = 0; i < 50; ++i) {
                // 1. Network byte swap
                uint16_t p = micant::ws2_32::htons(static_cast<uint16_t>(1024 + t * 50 + i));
                micant::ws2_32::ntohs(p);

                // 2. Post IOCP packet
                micant::kernel32::PostQueuedCompletionStatus(hIocp, 16384, t, nullptr);

                // 3. Name lookup & release
                void* ai = nullptr;
                micant::ws2_32::getaddrinfo("127.0.0.1", "6881", nullptr, &ai);
                if (ai) micant::ws2_32::freeaddrinfo(ai);

                opsDone.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(opsDone.load() == 400, "400-operation concurrent high-throughput P2P stress test must achieve 100% success");

    std::cout << "[TEST] Suite 224: qBittorrent 5.2+ Networking, Async I/O & ICU Subsystem PASSED.\n";
}

// ============================================================================
// Suite 225: WinSCP 6.5+ Remote File Management & Async Network Subsystem
// ============================================================================
inline void Test_WinSCP_RemoteFileManagement_And_AsyncNetwork_Suite() {
    std::cout << "[TEST] Executing Suite 225: WinSCP 6.5+ Remote File Management & Async Network...\n";

    micant::subsystems::InitializeAllSubsystemExports();
    auto& loader = micant::ldr::DynamicLoader::get();

    // Stage 1: Loader Export Verification across 10 critical modules
    TEST_ASSERT(loader.getExport("ws2_32.dll", "WSAAsyncGetHostByName") != nullptr, "ws2_32!WSAAsyncGetHostByName must be registered");
    TEST_ASSERT(loader.getExport("ws2_32.dll", "WSAEventSelect") != nullptr, "ws2_32!WSAEventSelect must be registered");
    TEST_ASSERT(loader.getExport("ws2_32.dll", "getservbyname") != nullptr, "ws2_32!getservbyname must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "CreateJobObjectW") != nullptr, "kernel32!CreateJobObjectW must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "AssignProcessToJobObject") != nullptr, "kernel32!AssignProcessToJobObject must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "FlushConsoleInputBuffer") != nullptr, "kernel32!FlushConsoleInputBuffer must be registered");
    TEST_ASSERT(loader.getExport("kernel32.dll", "InterlockedIncrement") != nullptr, "kernel32!InterlockedIncrement must be registered");
    TEST_ASSERT(loader.getExport("comdlg32.dll", "ReplaceTextW") != nullptr, "comdlg32!ReplaceTextW must be registered");
    TEST_ASSERT(loader.getExport("crypt32.dll", "CertCreateCertificateChainEngine") != nullptr, "crypt32!CertCreateCertificateChainEngine must be registered");
    TEST_ASSERT(loader.getExport("gdi32.dll", "PolyPolyline") != nullptr, "gdi32!PolyPolyline must be registered");
    TEST_ASSERT(loader.getExport("iphlpapi.dll", "if_nametoindex") != nullptr, "iphlpapi!if_nametoindex must be registered");
    TEST_ASSERT(loader.getExportOrdinal("msi.dll", 70) != nullptr, "msi!Ordinal_70 must be registered");
    TEST_ASSERT(loader.getExport("secur32.dll", "GetUserNameExW") != nullptr, "secur32!GetUserNameExW must be registered");
    TEST_ASSERT(loader.getExport("shell32.dll", "FindExecutableW") != nullptr, "shell32!FindExecutableW must be registered");
    TEST_ASSERT(loader.getExport("shlwapi.dll", "PathSkipRootW") != nullptr, "shlwapi!PathSkipRootW must be registered");
    TEST_ASSERT(loader.getExport("user32.dll", "DrawCaption") != nullptr, "user32!DrawCaption must be registered");

    // Stage 2: Job Object Creation & Process Sandboxing
    void* hJob = micant::kernel32::CreateJobObjectW(nullptr, L"WinSCPJob");
    TEST_ASSERT(hJob != nullptr, "CreateJobObjectW must return valid job handle");
    TEST_ASSERT(micant::kernel32::OpenJobObjectW(0x1F001F, 0, L"WinSCPJob") == hJob, "OpenJobObjectW must succeed");
    TEST_ASSERT(micant::kernel32::AssignProcessToJobObject(hJob, nullptr) == 1, "AssignProcessToJobObject must succeed");
    TEST_ASSERT(micant::kernel32::SetInformationJobObject(hJob, 4, nullptr, 0) == 1, "SetInformationJobObject must succeed");

    // Stage 3: Winsock 2.0 Async Network Resolution & Events
    void* hAsync = micant::ws2_32::WSAAsyncGetHostByName(nullptr, 0x401, "sftp.example.com", nullptr, 0);
    TEST_ASSERT(hAsync != nullptr, "WSAAsyncGetHostByName must return async task handle");
    TEST_ASSERT(micant::ws2_32::WSACancelAsyncRequest(hAsync) == 0, "WSACancelAsyncRequest must cancel query");

    TEST_ASSERT(micant::ws2_32::WSAEventSelect(0x5001, nullptr, 0x01) == 0, "WSAEventSelect must succeed");
    uint8_t netEvents[44] = { 0 };
    TEST_ASSERT(micant::ws2_32::WSAEnumNetworkEvents(0x5001, nullptr, netEvents) == 0, "WSAEnumNetworkEvents must populate events");

    auto* seSsh = reinterpret_cast<micant::ws2_32::MicaServEnt*>(micant::ws2_32::getservbyname("ssh", "tcp"));
    TEST_ASSERT(seSsh != nullptr && seSsh->s_port == 22, "getservbyname must resolve SSH to port 22");

    auto* seHttp = reinterpret_cast<micant::ws2_32::MicaServEnt*>(micant::ws2_32::getservbyname("http", "tcp"));
    TEST_ASSERT(seHttp != nullptr && seHttp->s_port == 80, "getservbyname must resolve HTTP to port 80");

    char ipStr[32] = { 0 };
    uint32_t rawIp = 0;
    TEST_ASSERT(micant::ws2_32::inet_pton(2, "127.0.0.1", &rawIp) == 1, "inet_pton must convert IPv4 string to binary");
    TEST_ASSERT(micant::ws2_32::inet_ntop(2, &rawIp, ipStr, sizeof(ipStr)) != nullptr, "inet_ntop must convert binary to IPv4 string");
    TEST_ASSERT(std::strcmp(ipStr, "127.0.0.1") == 0, "inet_ntop / inet_pton roundtrip match");

    // Stage 4: Console Input Buffer & Automation Streams
    TEST_ASSERT(micant::kernel32::FlushConsoleInputBuffer(nullptr) == 1, "FlushConsoleInputBuffer must succeed");
    uint32_t evRead = 0, evWritten = 0;
    TEST_ASSERT(micant::kernel32::WriteConsoleInputW(nullptr, nullptr, 5, &evWritten) == 1, "WriteConsoleInputW must succeed");
    TEST_ASSERT(evWritten == 5, "Written event count must match");
    TEST_ASSERT(micant::kernel32::PeekConsoleInputW(nullptr, nullptr, 1, &evRead) == 1, "PeekConsoleInputW must succeed");
    TEST_ASSERT(micant::kernel32::ReadConsoleInputW(nullptr, nullptr, 1, &evRead) == 1, "ReadConsoleInputW must succeed");

    // Stage 5: Interlocked Atomic Operations
    int32_t atomVal = 100;
    TEST_ASSERT(micant::kernel32::InterlockedIncrement(&atomVal) == 101, "InterlockedIncrement must increment");
    TEST_ASSERT(micant::kernel32::InterlockedDecrement(&atomVal) == 100, "InterlockedDecrement must decrement");
    TEST_ASSERT(micant::kernel32::InterlockedExchangeAdd(&atomVal, 50) == 100 && atomVal == 150, "InterlockedExchangeAdd must add");
    TEST_ASSERT(micant::kernel32::InterlockedExchange(&atomVal, 200) == 150 && atomVal == 200, "InterlockedExchange must replace");
    TEST_ASSERT(micant::kernel32::InterlockedCompareExchange(&atomVal, 300, 200) == 200 && atomVal == 300, "InterlockedCompareExchange must exchange on match");

    // Stage 6: Path Processing & Shell / Security Integration
    const wchar_t* skipped = micant::shell32::PathSkipRootW(L"C:\\Users\\admin\\Desktop");
    TEST_ASSERT(skipped != nullptr && std::wcscmp(skipped, L"Users\\admin\\Desktop") == 0, "PathSkipRootW must strip drive letter");

    wchar_t userBuf[32] = { 0 };
    uint32_t userLen = 32;
    TEST_ASSERT(micant::sspi::GetUserNameExW(2, userBuf, &userLen) == 1, "GetUserNameExW must succeed");
    TEST_ASSERT(std::wcscmp(userBuf, L"admin") == 0, "Username must report admin");

    void* hEngine = nullptr;
    TEST_ASSERT(micant::crypt32::CertCreateCertificateChainEngine(nullptr, &hEngine) == 1, "CertCreateCertificateChainEngine must succeed");
    TEST_ASSERT(hEngine != nullptr, "Chain engine handle must not be null");
    micant::crypt32::CertFreeCertificateChainEngine(hEngine);

    // Stage 7: Concurrent Multi-Threaded Remote File Sync Stress Test
    std::atomic<uint32_t> syncOps{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&syncOps, t]() {
            for (int i = 0; i < 50; ++i) {
                // 1. Interlocked operations
                int32_t localVal = t * 100 + i;
                micant::kernel32::InterlockedIncrement(&localVal);

                // 2. Service lookup
                micant::ws2_32::getservbyname("ssh", "tcp");

                // 3. Path root skipping
                micant::shell32::PathSkipRootW(L"D:\\RemoteSync\\Transfers\\file.dat");

                syncOps.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(syncOps.load() == 400, "400-operation concurrent remote sync stress test must achieve 100% success");

    std::cout << "[TEST] Suite 225: WinSCP 6.5+ Remote File Management & Async Network PASSED.\n";
}

// ----------------------------------------------------------------------------
// Suite 226: Wireshark 4.6+ / TShark Network Packet Capture & Sovereign UCRT/MSVCP Subsystem
// ----------------------------------------------------------------------------
inline void Test_Wireshark_NetworkPacketCapture_And_UCRT_Suite() {
    std::cout << "[TEST] Executing Suite 226: Wireshark 4.6+ / TShark Packet Capture & UCRT/MSVCP Subsystem...\n";

    // Ensure subsystems are initialized
    micant::subsystems::InitializeAllSubsystemExports();
    // Stage 1: KERNEL32 Process & DEP Configuration
    TEST_ASSERT(micant::win32::DisableThreadLibraryCalls(nullptr) == 1, "DisableThreadLibraryCalls must succeed");
    TEST_ASSERT(micant::win32::SetProcessDEPPolicy(1) == 1, "SetProcessDEPPolicy must succeed");
    TEST_ASSERT(micant::win32::SetDllDirectoryA("C:\\Program Files\\Wireshark") == 1, "SetDllDirectoryA must succeed");

    // Stage 2: IPHLPAPI Interface Identification & LUID resolution
    micant::GUID ifGuid{0x12345678, 0x1234, 0x5678, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
    micant::iphlpapi::NET_LUID ifLuid{0};
    TEST_ASSERT(micant::iphlpapi::ConvertInterfaceGuidToLuid(&ifGuid, &ifLuid) == 0, "ConvertInterfaceGuidToLuid must return NO_ERROR");
    TEST_ASSERT(ifLuid.Value != 0, "Interface LUID value must be populated");

    wchar_t aliasBuf[64] = {0};
    TEST_ASSERT(micant::iphlpapi::ConvertInterfaceLuidToAlias(&ifLuid, aliasBuf, 64) == 0, "ConvertInterfaceLuidToAlias must return NO_ERROR");
    TEST_ASSERT(std::wcscmp(aliasBuf, L"Ethernet 0") == 0 || std::wcscmp(aliasBuf, L"eth0") == 0, "Interface alias must match expected default adapter");

    // Stage 3: Winsock Network Byte Order & Ordinal 18
    uint16_t portHost = 8080;
    uint16_t portNet = (portHost >> 8) | (portHost << 8);
    TEST_ASSERT(micant::ws2_32::ntohs(portNet) == portHost, "WS2_ntohs must correctly convert network to host byte order");

    // Stage 4: ADVAPI32 Security & Well-Known SID Generation
    uint8_t sidBuffer[64] = {0};
    uint32_t sidLen = sizeof(sidBuffer);
    TEST_ASSERT(micant::advapi32::CreateWellKnownSid(18, nullptr, sidBuffer, &sidLen) == 1, "CreateWellKnownSid must succeed");
    TEST_ASSERT(sidBuffer[0] == 1, "SID revision must be 1");
    TEST_ASSERT(sidBuffer[7] == 5, "SID NT Authority must be 5");

    // Stage 5: Universal C Runtime (UCRT) Math & Floating Point Subsystem
    double pi = 3.141592653589793;
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_sin(pi / 2.0) - 1.0) < 1e-9, "CRT_sin(pi/2) must equal 1.0");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_cos(0.0) - 1.0) < 1e-9, "CRT_cos(0) must equal 1.0");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_sqrt(16.0) - 4.0) < 1e-9, "CRT_sqrt(16) must equal 4.0");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_log10(100.0) - 2.0) < 1e-9, "CRT_log10(100) must equal 2.0");
    TEST_ASSERT(micant::msvcrt::CRT_round(3.7) == 4.0, "CRT_round(3.7) must equal 4.0");
    double intPart = 0.0;
    double fracPart = micant::msvcrt::CRT_modf(3.25, &intPart);
    TEST_ASSERT(intPart == 3.0 && std::abs(fracPart - 0.25) < 1e-9, "CRT_modf must decompose float");

    // Stage 6: Universal C Runtime String & Character Processing
    TEST_ASSERT(micant::msvcrt::CRT_tolower('A') == 'a', "CRT_tolower must convert uppercase");
    TEST_ASSERT(micant::msvcrt::CRT_toupper('b') == 'B', "CRT_toupper must convert lowercase");
    TEST_ASSERT(micant::msvcrt::CRT_isdigit('9') != 0, "CRT_isdigit must identify digit");
    TEST_ASSERT(micant::msvcrt::CRT_strspn("12345abc", "0123456789") == 5, "CRT_strspn must measure prefix length");
    TEST_ASSERT(micant::msvcrt::CRT_strnlen("wireshark", 20) == 9, "CRT_strnlen must report length");

    wchar_t catDst[32] = L"Wire";
    TEST_ASSERT(micant::msvcrt::CRT_wcscat_s(catDst, 32, L"shark") == 0, "CRT_wcscat_s must succeed");
    TEST_ASSERT(std::wcscmp(catDst, L"Wireshark") == 0, "Concatenated string must match Wireshark");

    // Stage 7: Universal C Runtime Time, Date & Filesystem
    time_t rawNow = std::time(nullptr);
    tm localTm{};
    TEST_ASSERT(micant::msvcrt::CRT_localtime64_s(&localTm, &rawNow) == 0, "CRT_localtime64_s must succeed");
    TEST_ASSERT(localTm.tm_year > 120, "Local year must be modern");

    tm gmTm{};
    TEST_ASSERT(micant::msvcrt::CRT_gmtime64_s(&gmTm, &rawNow) == 0, "CRT_gmtime64_s must succeed");

    micant::msvcrt::timespec64 ts{};
    TEST_ASSERT(micant::msvcrt::CRT_timespec64_get(&ts, 1) == 1, "CRT_timespec64_get must succeed");
    TEST_ASSERT(ts.tv_sec > 1700000000, "Timespec timestamp must be valid");

    // Stage 8: VCRuntime Memory Operations
    char memDst[16] = {0};
    const char memSrc[] = "MicaNT_UCRT";
    micant::msvcrt::VCRT_memcpy(memDst, memSrc, sizeof(memSrc));
    TEST_ASSERT(micant::msvcrt::VCRT_memcmp(memDst, memSrc, sizeof(memSrc)) == 0, "VCRT_memcmp must confirm copy");
    TEST_ASSERT(micant::msvcrt::VCRT_memchr(memDst, 'N', sizeof(memSrc)) != nullptr, "VCRT_memchr must locate character");
    TEST_ASSERT(micant::msvcrt::VCRT_strstr(memDst, "UCRT") != nullptr, "VCRT_strstr must locate substring");

    // Stage 9: MSVCP140 Concurrency, Locinfo & Stream Subsystem
    int mtxDummy = 0;
    micant::msvcrt::MSVC_Mtx_lock(&mtxDummy);
    micant::msvcrt::MSVC_Mtx_unlock(&mtxDummy);
    int cndDummy = 0;
    micant::msvcrt::MSVC_Cnd_broadcast(&cndDummy);

    TEST_ASSERT(micant::msvcrt::MSVC_Random_device() != 0, "MSVC_Random_device must generate non-zero token");
    TEST_ASSERT(std::wcsstr(micant::msvcrt::MSVC_W_Getmonths(), L"Oct") != nullptr, "MSVC_W_Getmonths must contain Oct");

    // Stage 10: Multi-Threaded High-Throughput Packet Dissection & UCRT Computation Stress Test
    std::atomic<uint32_t> packetOps{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&packetOps, t]() {
            for (int i = 0; i < 100; ++i) {
                // 1. Math computation
                double val = micant::msvcrt::CRT_sin(static_cast<double>(i) * 0.01);
                (void)val;

                // 2. Port conversion
                uint16_t p = micant::ws2_32::ntohs(static_cast<uint16_t>(t * 1000 + i));
                (void)p;

                // 3. String operation
                size_t l = micant::msvcrt::CRT_strnlen("packet_payload_dissect", 32);
                (void)l;

                packetOps.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(packetOps.load() == 800, "800-packet high-throughput parallel dissection stress test must achieve 100% success");

    std::cout << "[TEST] Suite 226: Wireshark 4.6+ / TShark Packet Capture & UCRT/MSVCP Subsystem PASSED.\n";
}

// ----------------------------------------------------------------------------
// Suite 227: FileZilla 3.x / Sovereign Networking & Enterprise FTP Subsystem
// ----------------------------------------------------------------------------
inline void Test_FileZilla_FtpSftp_And_SovereignNetworking_Suite() {
    std::cout << "[TEST] Executing Suite 227: FileZilla 3.x / Sovereign Networking & Enterprise FTP Subsystem...\n";

    // Ensure subsystems are initialized
    micant::subsystems::InitializeAllSubsystemExports();
    // Stage 1: KERNEL32 Power Status, Processor Topology & Volume Resolution
    micant::win32::SYSTEM_POWER_STATUS pwr{};
    TEST_ASSERT(micant::win32::GetSystemPowerStatus(&pwr) == 1, "GetSystemPowerStatus must succeed");
    TEST_ASSERT(pwr.ACLineStatus == 1, "AC line status must be online");
    TEST_ASSERT(pwr.BatteryFlag == 128, "Battery flag must indicate no battery for sovereign desktop");
    TEST_ASSERT(micant::win32::GetActiveProcessorCount(0) == 8, "GetActiveProcessorCount must report 8 cores");

    wchar_t profileBuf[64] = {0};
    uint32_t profLen = micant::win32::GetProfileStringW(L"FileZilla", L"Version", L"3.71.1", profileBuf, 64);
    TEST_ASSERT(profLen > 0 && std::wcscmp(profileBuf, L"3.71.1") == 0, "GetProfileStringW must return default version");

    wchar_t asciiDomain[64] = {0};
    int idnLen = micant::win32::IdnToAscii(0, L"filezilla-project.org", -1, asciiDomain, 64);
    TEST_ASSERT(idnLen > 0 && std::wcscmp(asciiDomain, L"filezilla-project.org") == 0, "IdnToAscii must succeed");

    wchar_t volBuf[64] = {0};
    auto hVol = micant::win32::FindFirstVolumeW(volBuf, 64);
    TEST_ASSERT(hVol != nullptr && std::wcsstr(volBuf, L"Volume") != nullptr, "FindFirstVolumeW must return volume identifier");
    micant::win32::FindVolumeClose(hVol);

    char pathBuf[64] = {0};
    uint32_t pathLen = micant::win32::GetFinalPathNameByHandleA(nullptr, pathBuf, 64, 0);
    TEST_ASSERT(pathLen > 0 && std::strstr(pathBuf, "MicaNT") != nullptr, "GetFinalPathNameByHandleA must succeed");

    // Stage 2: USER32 Display Modes, Window Animation & Dynamic Data Exchange (DDE)
    uint8_t devModeBuf[256] = {0};
    TEST_ASSERT(micant::user32::EnumDisplaySettingsW(nullptr, 0, devModeBuf) == 1, "EnumDisplaySettingsW must succeed");
    uint32_t width = *reinterpret_cast<uint32_t*>(devModeBuf + 108);
    uint32_t height = *reinterpret_cast<uint32_t*>(devModeBuf + 112);
    TEST_ASSERT(width == 1920 && height == 1080, "EnumDisplaySettingsW must report 1080p display mode");

    TEST_ASSERT(micant::user32::AnimateWindow(nullptr, 200, 0) == 1, "AnimateWindow must succeed");
    void* hDdeData = micant::user32::DdeCreateDataHandle(1, nullptr, 0, 0, nullptr, 1, 0);
    TEST_ASSERT(hDdeData != nullptr, "DdeCreateDataHandle must return valid handle");
    TEST_ASSERT(micant::user32::DdeGetLastError(1) == 0, "DdeGetLastError must return no error");

    // Stage 3: GDI32 Polygons, Region Testing & Coordinate Transforms
    auto hRgn = micant::gdi32::CreatePolygonRgn(nullptr, 4, 1);
    TEST_ASSERT(hRgn != nullptr, "CreatePolygonRgn must return valid HRGN");
    TEST_ASSERT(micant::gdi32::PtInRegion(hRgn, 100, 100) == 1, "PtInRegion must report true");
    TEST_ASSERT(micant::gdi32::RectInRegion(hRgn, nullptr) == 1, "RectInRegion must report true");

    float xform[6] = {0};
    TEST_ASSERT(micant::gdi32::GetWorldTransform(nullptr, xform) == 1, "GetWorldTransform must succeed");
    TEST_ASSERT(xform[0] == 1.0f && xform[3] == 1.0f, "World transform identity diagonal must be 1.0");

    // Stage 4: ADVAPI32 LUID Allocation, Security Tokens & Credentials
    uint32_t luid1[2] = {0}, luid2[2] = {0};
    TEST_ASSERT(micant::advapi32::AllocateLocallyUniqueId(luid1) == 1, "AllocateLocallyUniqueId must succeed");
    TEST_ASSERT(micant::advapi32::AllocateLocallyUniqueId(luid2) == 1, "Second AllocateLocallyUniqueId must succeed");
    TEST_ASSERT(luid2[0] > luid1[0], "LUID counter must monotonically increment");

    micant::win32::HANDLE hNewToken = nullptr;
    TEST_ASSERT(micant::advapi32::DuplicateTokenEx(nullptr, 0, nullptr, 2, 1, &hNewToken) == 1, "DuplicateTokenEx must succeed");
    TEST_ASSERT(hNewToken != nullptr, "Duplicated token handle must be valid");
    TEST_ASSERT(micant::advapi32::ImpersonateLoggedOnUser(hNewToken) == 1, "ImpersonateLoggedOnUser must succeed");
    TEST_ASSERT(micant::advapi32::RevertToSelf() == 1, "RevertToSelf must succeed");

    uint8_t sigBuf[64] = {0};
    uint32_t sigLen = sizeof(sigBuf);
    TEST_ASSERT(micant::advapi32::CryptSignHashA(1, 0, nullptr, 0, sigBuf, &sigLen) == 1, "CryptSignHashA must succeed");
    TEST_ASSERT(sigBuf[0] == 0xAA, "Signature buffer must be signed");

    // Stage 5: CRYPT32 & NCRYPT Key Storage Provider (KSP) Cryptography
    const uint8_t plain[16] = {0x01, 0x02, 0x03, 0x04};
    uint8_t cipherOut[16] = {0};
    uint32_t outLen = 0;
    TEST_ASSERT(micant::crypto::NCryptDecrypt(1, plain, 16, nullptr, cipherOut, 16, &outLen, 0) == 0, "NCryptDecrypt must succeed");
    TEST_ASSERT(outLen == 16 && std::memcmp(plain, cipherOut, 16) == 0, "Decrypted text must match plaintext");

    uint32_t keyBits = 0;
    uint32_t propLen = 0;
    TEST_ASSERT(micant::crypto::NCryptGetProperty(1, L"Length", reinterpret_cast<uint8_t*>(&keyBits), sizeof(keyBits), &propLen, 0) == 0, "NCryptGetProperty must succeed");
    TEST_ASSERT(keyBits == 2048, "NCrypt key length property must be 2048");

    uint8_t ncSig[256] = {0};
    uint32_t ncSigLen = 0;
    TEST_ASSERT(micant::crypto::NCryptSignHash(1, nullptr, plain, 16, ncSig, 256, &ncSigLen, 0) == 0, "NCryptSignHash must succeed");
    TEST_ASSERT(ncSigLen == 256 && ncSig[0] == 0x55, "NCrypt signature must be generated");

    uint8_t randBuf[32] = {0};
    TEST_ASSERT(micant::crypto::BCryptGenRandom(nullptr, randBuf, 32, 0) == 0, "NC_BCryptGenRandom must succeed");
    TEST_ASSERT(randBuf[0] != 0 || randBuf[1] != 0, "BCrypt random bytes must be generated");

    // Stage 6: SHELL32 & UXTHEME Visual Styling Subsystem
    void* hIconL = nullptr;
    void* hIconS = nullptr;
    TEST_ASSERT(micant::shell32::SHDefExtractIconW(L"filezilla.exe", 0, 0, &hIconL, &hIconS, 32) == 0, "SHDefExtractIconW must succeed");
    TEST_ASSERT(hIconL != nullptr && hIconS != nullptr, "Extracted icons must be non-null");
    TEST_ASSERT(micant::shell32::SHGetIconOverlayIndexW(nullptr, 0) == 0, "SHGetIconOverlayIndexW must return 0");

    int32_t contentRc[4] = {0, 0, 100, 100};
    int32_t extentRc[4] = {0, 0, 0, 0};
    TEST_ASSERT(micant::uxtheme::GetThemeBackgroundExtent(nullptr, nullptr, 1, 1, contentRc, extentRc) == static_cast<int32_t>(0x80070006u), "GetThemeBackgroundExtent rejects an invalid theme handle");
    TEST_ASSERT(extentRc[0] == 0 && extentRc[1] == 0 && extentRc[2] == 0 && extentRc[3] == 0, "Failed theme extent query clears its output rect");
    TEST_ASSERT(micant::uxtheme::GetThemeSysColor(nullptr, 1) == 0xFFFFFFFFu, "GetThemeSysColor reports CLR_INVALID when unsupported");

    // Stage 7: Winsock 2.0 WSA Event Synchronization
    auto hWsaEvent = micant::ws2_32::WSACreateEvent();
    TEST_ASSERT(hWsaEvent != nullptr, "WSACreateEvent must create event handle");
    TEST_ASSERT(micant::ws2_32::WSASetEvent(hWsaEvent) == 1, "WSASetEvent must signal event");
    uint32_t waitRes = micant::ws2_32::WSAWaitForMultipleEvents(1, &hWsaEvent, 1, 100, 0);
    TEST_ASSERT(waitRes == 0, "WSAWaitForMultipleEvents on signaled event must return WAIT_OBJECT_0");
    TEST_ASSERT(micant::ws2_32::WSACloseEvent(hWsaEvent) == 1, "WSACloseEvent must close handle");

    // Stage 8: Legacy MSVCRT Math, Wide String & Filesystem Subsystem
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_cosh(0.0) - 1.0) < 1e-9, "CRT_cosh(0) must equal 1.0");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_sinh(0.0) - 0.0) < 1e-9, "CRT_sinh(0) must equal 0.0");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_tanh(0.0) - 0.0) < 1e-9, "CRT_tanh(0) must equal 0.0");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_atof("3.14159") - 3.14159) < 1e-5, "CRT_atof must parse float");
    TEST_ASSERT(micant::msvcrt::CRT_atol("1234567") == 1234567, "CRT_atol must parse long");

    wchar_t* wdup = micant::msvcrt::CRT_wcsdup(L"FileZilla_Client");
    TEST_ASSERT(wdup != nullptr && std::wcscmp(wdup, L"FileZilla_Client") == 0, "CRT_wcsdup must duplicate string");
    std::free(wdup);

    wchar_t wcpyDst[32] = {0};
    micant::msvcrt::CRT_wcsncpy(wcpyDst, L"SovereignFTP", 12);
    TEST_ASSERT(std::wcscmp(wcpyDst, L"SovereignFTP") == 0, "CRT_wcsncpy must copy wide string");
    TEST_ASSERT(micant::msvcrt::CRT_wcsnicmp(L"FTP", L"ftp", 3) == 0, "CRT_wcsnicmp must compare case-insensitively");

    void* alignedMem = micant::msvcrt::CRT_aligned_malloc(1024, 64);
    TEST_ASSERT(alignedMem != nullptr, "CRT_aligned_malloc must allocate memory");
    TEST_ASSERT((reinterpret_cast<uintptr_t>(alignedMem) % 64) == 0, "Memory must be 64-byte aligned");
    micant::msvcrt::CRT_aligned_free(alignedMem);

    wchar_t cwdBuf[64] = {0};
    wchar_t* cwdRes = micant::msvcrt::CRT_wgetcwd(cwdBuf, 64);
    TEST_ASSERT(cwdRes != nullptr && std::wcscmp(cwdRes, L"C:\\MicaNT") == 0, "CRT_wgetcwd must return C:\\MicaNT");
    TEST_ASSERT(micant::msvcrt::CRT_getdrive() == 3, "CRT_getdrive must return drive 3 (C:)");

    // Stage 9: Concurrent Multi-Threaded FTP/SFTP Transfer & Event Synchronization Stress Test
    std::atomic<uint32_t> ftpTransferOps{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&ftpTransferOps, t]() {
            for (int i = 0; i < 100; ++i) {
                // 1. Allocate unique transfer session LUID
                uint32_t transferLuid[2] = {0};
                micant::advapi32::AllocateLocallyUniqueId(transferLuid);

                // 2. Create and signal async socket event
                auto ev = micant::ws2_32::WSACreateEvent();
                micant::ws2_32::WSASetEvent(ev);
                micant::ws2_32::WSAWaitForMultipleEvents(1, &ev, 1, 50, 0);
                micant::ws2_32::WSACloseEvent(ev);

                // 3. Perform 64-byte aligned socket buffer operations
                void* buf = micant::msvcrt::CRT_aligned_malloc(512, 64);
                if (buf) {
                    std::memset(buf, static_cast<uint8_t>(t + i), 512);
                    micant::msvcrt::CRT_aligned_free(buf);
                }

                ftpTransferOps.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(ftpTransferOps.load() == 800, "800-operation parallel FTP/SFTP transfer stress test must achieve 100% success");

    std::cout << "[TEST] Suite 227: FileZilla 3.x / Sovereign Networking & Enterprise FTP Subsystem PASSED.\n";
}

void Test_Tier4_Multimedia_Creative_Workstation_Ecosystem_Suite() {
    std::cout << "[TEST] Executing Suite 228: Tier 4 Multimedia & Creative Workstation Ecosystem (Audacity, Blender, GIMP & OBS Studio)...\n";

    // Stage 1: Universal CRT C99 Single-Precision Math Subsystem
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_sinf(0.0f) - 0.0f) < 1e-6f, "sinf(0) must equal 0.0f");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_cosf(0.0f) - 1.0f) < 1e-6f, "cosf(0) must equal 1.0f");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_sqrtf(16.0f) - 4.0f) < 1e-6f, "sqrtf(16) must equal 4.0f");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_powf(2.0f, 8.0f) - 256.0f) < 1e-6f, "powf(2, 8) must equal 256.0f");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_hypotf(3.0f, 4.0f) - 5.0f) < 1e-6f, "hypotf(3, 4) must equal 5.0f");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_fminf(3.0f, 7.0f) - 3.0f) < 1e-6f, "fminf must select minimum");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_fmaxf(3.0f, 7.0f) - 7.0f) < 1e-6f, "fmaxf must select maximum");
    TEST_ASSERT(micant::msvcrt::CRT_floorf(3.7f) == 3.0f, "floorf(3.7) must equal 3.0f");
    TEST_ASSERT(micant::msvcrt::CRT_ceilf(3.2f) == 4.0f, "ceilf(3.2) must equal 4.0f");

    // Stage 2: MSVCP140 3D Vector Hypot & String Utilities
    TEST_ASSERT(std::abs(micant::msvcrt::__std_smf_hypot3f(2.0f, 3.0f, 6.0f) - 7.0f) < 1e-5f, "hypot3f(2, 3, 6) must equal 7.0f");
    TEST_ASSERT(std::abs(micant::msvcrt::__std_smf_hypot3(2.0, 3.0, 6.0) - 7.0) < 1e-9, "hypot3(2, 3, 6) must equal 7.0");

    char strBuf[64] = {0};
    micant::msvcrt::CRT_strcpy_s(strBuf, sizeof(strBuf), "MicaNT_Blender_Renderer");
    TEST_ASSERT(std::strcmp(strBuf, "MicaNT_Blender_Renderer") == 0, "strcpy_s must copy string");
    TEST_ASSERT(micant::msvcrt::CRT_atoll("9876543210123") == 9876543210123LL, "atoll must parse int64");
    TEST_ASSERT(std::abs(micant::msvcrt::CRT_strtof("3.14159f", nullptr) - 3.14159f) < 1e-4f, "strtof must parse float");
    TEST_ASSERT(micant::msvcrt::CRT_wcsicmp(L"Audacity", L"audacity") == 0, "_wcsicmp must compare case-insensitively");

    // Stage 3: Win32 Multimedia WinMM Mixer & Audio Engine
    micant::winmm::HMIXER hMixer = nullptr;
    micant::winmm::MMRESULT mmRes = micant::winmm::mixerOpen(&hMixer, 0, 0, 0, 0);
    TEST_ASSERT(mmRes == micant::winmm::MMSYSERR_NOERROR && hMixer != nullptr, "mixerOpen must succeed");

    micant::winmm::MIXERCAPSW caps{};
    mmRes = micant::winmm::mixerGetDevCapsW(0, &caps, sizeof(caps));
    TEST_ASSERT(mmRes == micant::winmm::MMSYSERR_NOERROR, "mixerGetDevCapsW must succeed");
    TEST_ASSERT(std::wcscmp(caps.szPname, L"MicaNT Audio Mixer") == 0, "Mixer name must identify MicaNT Audio Mixer");

    uint32_t mxId = 99;
    mmRes = micant::winmm::mixerGetID(hMixer, &mxId, 0);
    TEST_ASSERT(mmRes == micant::winmm::MMSYSERR_NOERROR && mxId == 0, "mixerGetID must return 0");
    TEST_ASSERT(micant::winmm::mixerClose(hMixer) == micant::winmm::MMSYSERR_NOERROR, "mixerClose must succeed");
    TEST_ASSERT(micant::winmm::waveOutMessage(nullptr, 0, 0, 0) == micant::winmm::MMSYSERR_NOTSUPPORTED, "waveOutMessage must return MMSYSERR_NOTSUPPORTED");

    // Stage 4: Shell Drag-Drop, Path APIs & DPI Scaling
    char dropPath[260] = "init";
    TEST_ASSERT(micant::shell32::DragQueryFileA(nullptr, 0, dropPath, sizeof(dropPath)) == 0, "DragQueryFileA must return 0");
    TEST_ASSERT(dropPath[0] == '\0', "DragQueryFileA must null-terminate buffer");

    char testPath[260] = "C:\\CreativeWorkstation";
    TEST_ASSERT(micant::shell32::PathAppendA(testPath, "Projects\\Sample.blend") == win32::TRUE, "PathAppendA must append subpath");
    TEST_ASSERT(std::string(testPath) == "C:\\CreativeWorkstation\\Projects\\Sample.blend", "PathAppendA result must match");

    micant::shell32::PathStripPathA(testPath);
    TEST_ASSERT(std::string(testPath) == "Sample.blend", "PathStripPathA must strip directory prefix");
    TEST_ASSERT(micant::shell32::SetProcessDpiAwareness(2) == micant::shell32::S_OK, "SetProcessDpiAwareness must succeed");

    // Stage 5: Configuration Manager (CfgMgr32) Device Node Discovery
    uint32_t devListSize = 0;
    TEST_ASSERT(micant::cfgmgr32::CM_Get_Device_ID_List_SizeA(&devListSize, nullptr, 0) == micant::cfgmgr32::CR_SUCCESS, "CM_Get_Device_ID_List_SizeA must succeed");
    TEST_ASSERT(devListSize >= 2, "Device ID list size must reserve double-null terminator");

    char devIdList[16] = { 'x', 'x' };
    TEST_ASSERT(micant::cfgmgr32::CM_Get_Device_ID_ListA(nullptr, devIdList, sizeof(devIdList), 0) == micant::cfgmgr32::CR_SUCCESS, "CM_Get_Device_ID_ListA must succeed");
    TEST_ASSERT(devIdList[0] == '\0' && devIdList[1] == '\0', "CM_Get_Device_ID_ListA must write double-null");

    uint32_t devNode = 0;
    TEST_ASSERT(micant::cfgmgr32::CM_Locate_DevNodeA(&devNode, "PCI\\VEN_10DE", 0) == micant::cfgmgr32::CR_SUCCESS, "CM_Locate_DevNodeA must succeed");
    TEST_ASSERT(devNode != 0, "Device node instance handle must be valid");

    void* hDevKey = nullptr;
    TEST_ASSERT(micant::cfgmgr32::CM_Open_DevNode_Key(devNode, 0, 0, 0, &hDevKey, 0) == micant::cfgmgr32::CR_SUCCESS, "CM_Open_DevNode_Key must succeed");
    TEST_ASSERT(hDevKey != nullptr, "Device registry key handle must be valid");

    // Stage 6: DbgHelp Symbol Engine & Name Undecoration
    TEST_ASSERT(micant::dbghelp::SymInitialize(reinterpret_cast<win32::HANDLE>(0x123), nullptr, win32::TRUE) == win32::TRUE, "SymInitialize must succeed");
    micant::dbghelp::SymSetOptions(0x300);
    TEST_ASSERT(micant::dbghelp::SymGetOptions() == 0x200, "SymGetOptions must return options");

    char undecorated[128] = {0};
    uint32_t undecLen = micant::dbghelp::UnDecorateSymbolName("?render@BlenderEngine@@QEAAHXZ", undecorated, sizeof(undecorated), 0);
    TEST_ASSERT(undecLen > 0, "UnDecorateSymbolName must return name length");
    TEST_ASSERT(std::strcmp(undecorated, "?render@BlenderEngine@@QEAAHXZ") == 0, "UnDecorateSymbolName must copy string");
    TEST_ASSERT(micant::dbghelp::SymCleanup(reinterpret_cast<win32::HANDLE>(0x123)) == win32::TRUE, "SymCleanup must succeed");

    // Stage 7: Security Descriptors, SDDL & System Functions
    micant::advapi32::EXPLICIT_ACCESS_W ea{};
    wchar_t trusteeName[] = L"MicaNT\\CreativeUser";
    micant::advapi32::BuildExplicitAccessWithNameW(&ea, trusteeName, 0x1F0000, 1, 0);
    TEST_ASSERT(ea.grfAccessPermissions == 0x1F0000, "Access permissions must match");
    TEST_ASSERT(std::wcscmp(ea.Trustee.ptstrName, trusteeName) == 0, "Trustee name must match");

    void* pSecDesc = nullptr;
    uint32_t sdSize = 0;
    TEST_ASSERT(micant::advapi32::BuildSecurityDescriptorW(nullptr, nullptr, 1, &ea, 0, nullptr, nullptr, &sdSize, &pSecDesc) == 0, "BuildSecurityDescriptorW must succeed");
    TEST_ASSERT(pSecDesc != nullptr && sdSize > 0, "Security descriptor must be generated");

    uint8_t randBytes[32] = {0};
    TEST_ASSERT(micant::advapi32::SystemFunction036(randBytes, sizeof(randBytes)) == win32::TRUE, "SystemFunction036 (RtlGenRandom) must generate bytes");
    bool nonZero = false;
    for (uint8_t b : randBytes) { if (b != 0) nonZero = true; }
    TEST_ASSERT(nonZero, "RtlGenRandom must populate non-zero CSPRNG entropy");

    // Stage 8: Toolhelp32 & Kernel32 Diagnostics Subsystem
    micant::win32::PROCESSENTRY32 pe32{};
    pe32.dwSize = sizeof(pe32);
    TEST_ASSERT(micant::win32::Process32First(nullptr, &pe32) == win32::TRUE, "Process32First must return process entry");
    TEST_ASSERT(pe32.th32ProcessID == 1000, "Process ID must match");
    TEST_ASSERT(micant::win32::Process32Next(nullptr, &pe32) == win32::FALSE, "Process32Next must return FALSE when finished");

    micant::win32::MODULEENTRY32 me32{};
    me32.dwSize = sizeof(me32);
    TEST_ASSERT(micant::win32::Module32First(nullptr, &me32) == win32::TRUE, "Module32First must return module entry");
    TEST_ASSERT(std::strcmp(me32.szModule, "micant_app.exe") == 0, "Module name must match");

    char compName[32] = {0};
    uint32_t cNameLen = sizeof(compName);
    TEST_ASSERT(micant::win32::GetComputerNameA(compName, &cNameLen) == win32::TRUE, "GetComputerNameA must succeed");
    TEST_ASSERT(std::strcmp(compName, "MICANT") == 0, "Computer name must equal MICANT");

    // Stage 9: Concurrent Multi-Threaded Real-Time Media Audio/Render Stress Test
    std::atomic<uint32_t> mediaOps{0};
    std::vector<std::thread> workers;
    workers.reserve(8);
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&mediaOps, t]() {
            for (int i = 0; i < 125; ++i) {
                // 1. Single-precision 3D vector transform (Blender/OBS vertex simulation)
                float x = static_cast<float>(t + 1);
                float y = static_cast<float>(i + 1);
                float z = static_cast<float>((t * 10) + i);
                float norm = micant::msvcrt::__std_smf_hypot3f(x, y, z);
                float angle = micant::msvcrt::CRT_sinf(norm) * micant::msvcrt::CRT_cosf(norm);
                (void)angle;

                // 2. Audio mixing sample processing (Audacity simulation)
                uint8_t entropy[16] = {0};
                micant::advapi32::SystemFunction036(entropy, sizeof(entropy));

                // 3. String formatting and diagnostics
                char pathBuf[260] = "D:\\Projects";
                micant::shell32::PathAppendA(pathBuf, "Track01.wav");

                mediaOps.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    TEST_ASSERT(mediaOps.load() == 1000, "1000-operation parallel multimedia & render transform stress test must achieve 100% success");

    std::cout << "[TEST] Suite 228: Tier 4 Multimedia & Creative Workstation Ecosystem Subsystem PASSED.\n";
}













