#!/usr/bin/env python3
import os
import re
import subprocess
import sys
import paramiko
import random
import socket
from pathlib import Path

from verify_kernel_architecture import validate_build_manifest

sys.stdout.reconfigure(encoding='utf-8')

REPO_ROOT = str(Path(__file__).resolve().parents[1])
EFI_SOURCE = os.path.join(REPO_ROOT, "build", "esp", "EFI", "BOOT", "BOOTX64.EFI")
KRNL_SOURCE = os.path.join(REPO_ROOT, "build", "baremetal", "ntoskrnl.exe")
VIRTIO_GPU_SOURCE = os.path.join(REPO_ROOT, "build", "virtio_gpu.sys")
APP_SOURCE = os.path.join(REPO_ROOT, "build", "app.exe")
BENCH_SOURCE = os.path.join(REPO_ROOT, "build", "bench.exe")
CUBE_SOURCE = os.path.join(REPO_ROOT, "build", "bin", "prismx_3d_cube_app.exe")
if not os.path.exists(CUBE_SOURCE):
    CUBE_SOURCE = os.path.join(REPO_ROOT, "build", "prismx_3d_cube_app.exe")

HOST = os.environ.get("MICANT_TARGET_HOST", "147.135.1.229")
USER = os.environ.get("MICANT_TARGET_USER", "ubuntu")
KEY_PATH = os.environ.get("MICANT_SSH_KEY", r"C:\Users\admin\.ssh\micant_ci_key")

SYSTEM32_TOOLS = [
    ("tools/ipconfig_app.cpp", "build/ipconfig.exe", "ipconfig.exe"),
    ("tools/whoami_app.cpp", "build/whoami.exe", "whoami.exe"),
    ("tools/tasklist_app.cpp", "build/tasklist.exe", "tasklist.exe"),
    ("tools/procdump_app.cpp", "build/procdump.exe", "procdump.exe"),
    ("tools/lucecrash_app.cpp", "build/lucecrash.exe", "lucecrash.exe"),
    ("tools/procdump_app.cpp", "build/proctop.exe", "proctop.exe"),
    ("tools/ping_app.cpp", "build/ping.exe", "ping.exe"),
    ("tools/cmd_app.cpp", "build/cmd.exe", "cmd.exe"),
    ("tools/hostname_app.cpp", "build/hostname.exe", "hostname.exe"),
    ("tools/systeminfo_app.cpp", "build/systeminfo.exe", "systeminfo.exe"),
    ("tools/netstat_app.cpp", "build/netstat.exe", "netstat.exe"),
    ("tools/chkdsk_app.cpp", "build/chkdsk.exe", "chkdsk.exe"),
    ("tools/diskpart_app.cpp", "build/diskpart.exe", "diskpart.exe"),
    ("tools/arp_app.cpp", "build/arp.exe", "arp.exe"),
    ("tools/route_app.cpp", "build/route.exe", "route.exe"),
    ("tools/reg_app.cpp", "build/reg.exe", "reg.exe"),
    ("tools/sc_app.cpp", "build/sc.exe", "sc.exe"),
    ("tools/driverquery_app.cpp", "build/driverquery.exe", "driverquery.exe"),
    ("tools/type_app.cpp", "build/type.exe", "type.exe"),
    ("tools/find_app.cpp", "build/find.exe", "find.exe"),
    ("tools/fc_app.cpp", "build/fc.exe", "fc.exe"),
    ("tools/attrib_app.cpp", "build/attrib.exe", "attrib.exe"),
    ("tools/more_app.cpp", "build/more.exe", "more.exe"),
    ("tools/format_app.cpp", "build/format.exe", "format.exe"),
    ("tools/taskkill_app.cpp", "build/taskkill.exe", "taskkill.exe"),
    ("tools/shutdown_app.cpp", "build/shutdown.exe", "shutdown.exe"),
    ("tools/robocopy_app.cpp", "build/robocopy.exe", "robocopy.exe"),
    ("tools/mountvol_app.cpp", "build/mountvol.exe", "mountvol.exe"),
    ("tools/refsutil_app.cpp", "build/refsutil.exe", "refsutil.exe"),
    ("tools/zfs_app.cpp", "build/zfs.exe", "zfs.exe"),
    ("tools/zpool_app.cpp", "build/zpool.exe", "zpool.exe"),
    ("tools/btrfs_app.cpp", "build/btrfs.exe", "btrfs.exe"),
    ("tools/extutil_app.cpp", "build/extutil.exe", "extutil.exe"),
    ("tools/xfsutil_app.cpp", "build/xfsutil.exe", "xfsutil.exe"),
    ("tools/f2fsutil_app.cpp", "build/f2fsutil.exe", "f2fsutil.exe"),
    ("tools/certutil_app.cpp", "build/certutil.exe", "certutil.exe"),
    ("tools/klist_app.cpp", "build/klist.exe", "klist.exe"),
    ("tools/fltmc.cpp", "build/fltmc.exe", "fltmc.exe"),
    ("tools/calc_app.cpp", "build/calc.exe", "calc.exe"),
    ("tools/notepad_app.cpp", "build/notepad.exe", "notepad.exe"),
    ("tools/taskmgr_app.cpp", "build/taskmgr.exe", "taskmgr.exe"),
    ("tools/regedit_app.cpp", "build/regedit.exe", "regedit.exe"),
    ("tools/explorer_shell_app.cpp", "build/explorer.exe", "explorer.exe"),
    ("tools/paint_app.cpp", "build/mspaint.exe", "mspaint.exe"),
    ("tools/devmgmt_app.cpp", "build/devmgmt.exe", "devmgmt.exe"),
    ("tools/diskmgmt_app.cpp", "build/diskmgmt.exe", "diskmgmt.exe"),
    ("tools/services_mgmt_app.cpp", "build/services_mgmt.exe", "services_mgmt.exe"),
    ("tools/eventvwr_app.cpp", "build/eventvwr.exe", "eventvwr.exe"),
    ("tools/msinfo32_app.cpp", "build/msinfo32.exe", "msinfo32.exe"),
    ("tools/control_app.cpp", "build/control.exe", "control.exe"),
    ("tools/sysdm_app.cpp", "build/sysdm.exe", "sysdm.exe"),
    ("tools/ncpa_app.cpp", "build/ncpa.exe", "ncpa.exe"),
    ("tools/dxdiag_app.cpp", "build/dxdiag.exe", "dxdiag.exe"),
    ("tools/msconfig_app.cpp", "build/msconfig.exe", "msconfig.exe"),
    ("tools/resmon_app.cpp", "build/resmon.exe", "resmon.exe"),
    ("tools/compmgmt_app.cpp", "build/compmgmt.exe", "compmgmt.exe"),
    ("tools/perfmon_app.cpp", "build/perfmon.exe", "perfmon.exe"),
    ("tools/sfc_app.cpp", "build/sfc.exe", "sfc.exe"),
    ("tools/bcdedit_app.cpp", "build/bcdedit.exe", "bcdedit.exe"),
    ("tools/tracert_app.cpp", "build/tracert.exe", "tracert.exe"),
    ("tools/nslookup_app.cpp", "build/nslookup.exe", "nslookup.exe"),
    ("tools/nbtstat_app.cpp", "build/nbtstat.exe", "nbtstat.exe"),
    ("tools/query_app.cpp", "build/query.exe", "query.exe"),
    ("tools/run_app.cpp", "build/run.exe", "run.exe"),
    ("tools/cleanmgr_app.cpp", "build/cleanmgr.exe", "cleanmgr.exe"),
    ("tools/charmap_app.cpp", "build/charmap.exe", "charmap.exe"),
    ("tools/photo_app.cpp", "build/photo.exe", "photo.exe"),
    ("tools/search_app.cpp", "build/searchui.exe", "searchui.exe"),
    ("tools/logonui_app.cpp", "build/logonui.exe", "logonui.exe"),
    ("tools/net_app.cpp", "build/net.exe", "net.exe"),
    ("tools/netsh_app.cpp", "build/netsh.exe", "netsh.exe"),
    ("tools/fsutil_app.cpp", "build/fsutil.exe", "fsutil.exe"),
    ("tools/wmic_app.cpp", "build/wmic.exe", "wmic.exe"),
    ("tools/powershell_app.cpp", "build/powershell.exe", "powershell.exe"),
    ("tools/where_app.cpp", "build/where.exe", "where.exe"),
    ("tools/clip_app.cpp", "build/clip.exe", "clip.exe"),
    ("tools/timeout_app.cpp", "build/timeout.exe", "timeout.exe"),
    ("tools/choice_app.cpp", "build/choice.exe", "choice.exe"),
    ("tools/powercfg_app.cpp", "build/powercfg.exe", "powercfg.exe"),
    ("tools/appwiz_app.cpp", "build/appwiz.exe", "appwiz.exe"),
    ("tools/desk_app.cpp", "build/desk.exe", "desk.exe"),
    ("tools/mmsys_app.cpp", "build/mmsys.exe", "mmsys.exe"),
    ("tools/powercfg_cpl_app.cpp", "build/powercfg_cpl.exe", "powercfg_cpl.exe"),
    ("tools/timedate_app.cpp", "build/timedate.exe", "timedate.exe"),
    ("tools/firewall_app.cpp", "build/firewall.exe", "firewall.exe"),
    ("tools/write_app.cpp", "build/write.exe", "write.exe"),
    ("tools/snippingtool_app.cpp", "build/snippingtool.exe", "snippingtool.exe"),
    ("tools/mstsc_app.cpp", "build/mstsc.exe", "mstsc.exe"),
    ("tools/magnify_app.cpp", "build/magnify.exe", "magnify.exe"),
    ("tools/osk_app.cpp", "build/osk.exe", "osk.exe"),
    ("tools/sndrec_app.cpp", "build/sndrec32.exe", "sndrec32.exe"),
    ("tools/icacls_app.cpp", "build/icacls.exe", "icacls.exe"),
    ("tools/takeown_app.cpp", "build/takeown.exe", "takeown.exe"),
    ("tools/openfiles_app.cpp", "build/openfiles.exe", "openfiles.exe"),
    ("tools/logman_app.cpp", "build/logman.exe", "logman.exe"),
    ("tools/typeperf_app.cpp", "build/typeperf.exe", "typeperf.exe"),
    ("tools/reagentc_app.cpp", "build/reagentc.exe", "reagentc.exe"),
    ("tools/gpupdate_app.cpp", "build/gpupdate.exe", "gpupdate.exe"),
    ("tools/gpresult_app.cpp", "build/gpresult.exe", "gpresult.exe"),
    ("tools/diskperf_app.cpp", "build/diskperf.exe", "diskperf.exe"),
    ("tools/main_cpl_app.cpp", "build/main.exe", "main.exe"),
    ("tools/inetcpl_app.cpp", "build/inetcpl.exe", "inetcpl.exe"),
    ("tools/joy_cpl_app.cpp", "build/joy.exe", "joy.exe"),
    ("tools/intl_cpl_app.cpp", "build/intl.exe", "intl.exe"),
    ("tools/telephon_cpl_app.cpp", "build/telephon.exe", "telephon.exe"),
    ("tools/rundll32_app.cpp", "build/rundll32.exe", "rundll32.exe"),
    ("tools/regsvr32_app.cpp", "build/regsvr32.exe", "regsvr32.exe"),
    ("tools/sigverif_app.cpp", "build/sigverif.exe", "sigverif.exe"),
    ("tools/verifier_app.cpp", "build/verifier.exe", "verifier.exe"),
    ("tools/gpedit_app.cpp", "build/gpedit.exe", "gpedit.exe"),
    ("tools/lusrmgr_app.cpp", "build/lusrmgr.exe", "lusrmgr.exe"),
    ("tools/certmgr_app.cpp", "build/certmgr.exe", "certmgr.exe"),
    ("tools/fsmgmt_app.cpp", "build/fsmgmt.exe", "fsmgmt.exe"),
    ("tools/secpol_app.cpp", "build/secpol.exe", "secpol.exe"),
    ("tools/tpm_app.cpp", "build/tpm.exe", "tpm.exe"),
    ("tools/winver_app.cpp", "build/winver.exe", "winver.exe"),
    ("tools/sysprep_app.cpp", "build/sysprep.exe", "sysprep.exe"),
    ("tools/wscui_app.cpp", "build/wscui.exe", "wscui.exe"),
    ("tools/hdwwiz_app.cpp", "build/hdwwiz.exe", "hdwwiz.exe"),
    ("tools/tftp_app.cpp", "build/tftp.exe", "tftp.exe"),
    ("tools/ftp_app.cpp", "build/ftp.exe", "ftp.exe"),
    ("tools/telnet_app.cpp", "build/telnet.exe", "telnet.exe"),
    ("tools/whois_app.cpp", "build/whois.exe", "whois.exe"),
    ("tools/finger_app.cpp", "build/finger.exe", "finger.exe"),
    ("tools/pathping_app.cpp", "build/pathping.exe", "pathping.exe"),
    ("tools/bitsadmin_app.cpp", "build/bitsadmin.exe", "bitsadmin.exe"),
    ("tools/dsquery_app.cpp", "build/dsquery.exe", "dsquery.exe"),
    ("tools/dsget_app.cpp", "build/dsget.exe", "dsget.exe"),
    ("tools/nltest_app.cpp", "build/nltest.exe", "nltest.exe"),
    ("tools/ksetup_app.cpp", "build/ksetup.exe", "ksetup.exe"),
    ("tools/tzutil_app.cpp", "build/tzutil.exe", "tzutil.exe"),
    ("tools/chcp_app.cpp", "build/chcp.exe", "chcp.exe"),
    ("tools/mode_app.cpp", "build/mode.exe", "mode.exe"),
    ("tools/doskey_app.cpp", "build/doskey.exe", "doskey.exe"),
    ("tools/comp_app.cpp", "build/comp.exe", "comp.exe"),
    ("tools/compact_app.cpp", "build/compact.exe", "compact.exe"),
    ("tools/cipher_app.cpp", "build/cipher.exe", "cipher.exe"),
    ("tools/expand_app.cpp", "build/expand.exe", "expand.exe"),
    ("tools/makecab_app.cpp", "build/makecab.exe", "makecab.exe"),
    ("tools/extrac32_app.cpp", "build/extrac32.exe", "extrac32.exe"),
    ("tools/manage_bde_app.cpp", "build/manage-bde.exe", "manage-bde.exe"),
    ("tools/chkntfs_app.cpp", "build/chkntfs.exe", "chkntfs.exe"),
    ("tools/defrag_app.cpp", "build/defrag.exe", "defrag.exe"),
    ("tools/relog_app.cpp", "build/relog.exe", "relog.exe"),
    ("tools/tracerpt_app.cpp", "build/tracerpt.exe", "tracerpt.exe"),
    ("tools/dism_app.cpp", "build/dism.exe", "dism.exe"),
    ("tools/auditpol_app.cpp", "build/auditpol.exe", "auditpol.exe"),
    ("tools/wevtutil_app.cpp", "build/wevtutil.exe", "wevtutil.exe"),
    ("tools/fondue_app.cpp", "build/fondue.exe", "fondue.exe"),
    ("tools/certreq_app.cpp", "build/certreq.exe", "certreq.exe"),
    ("tools/wscript_app.cpp", "build/wscript.exe", "wscript.exe"),
    ("tools/cscript_app.cpp", "build/cscript.exe", "cscript.exe"),
    ("tools/print_app.cpp", "build/print.exe", "print.exe"),
    ("tools/prnport_app.cpp", "build/prnport.exe", "prnport.exe"),
    ("tools/prnmngr_app.cpp", "build/prnmngr.exe", "prnmngr.exe"),
    ("tools/prnjobs_app.cpp", "build/prnjobs.exe", "prnjobs.exe"),
    ("tools/shadow_app.cpp", "build/shadow.exe", "shadow.exe"),
    ("tools/qwinsta_app.cpp", "build/qwinsta.exe", "qwinsta.exe"),
    ("tools/rwinsta_app.cpp", "build/rwinsta.exe", "rwinsta.exe"),
    ("tools/tsdiscon_app.cpp", "build/tsdiscon.exe", "tsdiscon.exe"),
    ("tools/msg_app.cpp", "build/msg.exe", "msg.exe"),
    ("tools/change_app.cpp", "build/change.exe", "change.exe"),
    ("tools/reset_app.cpp", "build/reset.exe", "reset.exe"),
    ("tools/lodctr_app.cpp", "build/lodctr.exe", "lodctr.exe"),
    ("tools/unlodctr_app.cpp", "build/unlodctr.exe", "unlodctr.exe"),
    ("tools/mount_app.cpp", "build/mount.exe", "mount.exe"),
    ("tools/umount_app.cpp", "build/umount.exe", "umount.exe"),
    ("tools/nfsstat_app.cpp", "build/nfsstat.exe", "nfsstat.exe"),
    ("tools/showmount_app.cpp", "build/showmount.exe", "showmount.exe"),
    ("tools/rpcinfo_app.cpp", "build/rpcinfo.exe", "rpcinfo.exe"),
    ("tools/rasdial_app.cpp", "build/rasdial.exe", "rasdial.exe"),
    ("tools/rasphone_app.cpp", "build/rasphone.exe", "rasphone.exe"),
    ("tools/pnputil_app.cpp", "build/pnputil.exe", "pnputil.exe"),
    ("tools/recdisc_app.cpp", "build/recdisc.exe", "recdisc.exe"),
    ("tools/slmgr_app.cpp", "build/slmgr.exe", "slmgr.exe"),
    ("tools/licensingdiag_app.cpp", "build/licensingdiag.exe", "licensingdiag.exe"),
    ("tools/secedit_app.cpp", "build/secedit.exe", "secedit.exe"),
    ("tools/ktpass_app.cpp", "build/ktpass.exe", "ktpass.exe"),
    ("tools/ntdsutil_app.cpp", "build/ntdsutil.exe", "ntdsutil.exe"),
    ("tools/rendom_app.cpp", "build/rendom.exe", "rendom.exe"),
    ("tools/wpr_app.cpp", "build/wpr.exe", "wpr.exe"),
    ("tools/xperf_app.cpp", "build/xperf.exe", "xperf.exe"),
    ("tools/schtasks_app.cpp", "build/schtasks.exe", "schtasks.exe"),
    ("tools/at_app.cpp", "build/at.exe", "at.exe"),
    ("tools/wbadmin_app.cpp", "build/wbadmin.exe", "wbadmin.exe"),
    ("tools/vssadmin_app.cpp", "build/vssadmin.exe", "vssadmin.exe"),
    ("tools/diskshadow_app.cpp", "build/diskshadow.exe", "diskshadow.exe"),
    ("tools/bdehdcfg_app.cpp", "build/bdehdcfg.exe", "bdehdcfg.exe"),
    ("tools/repair_bde_app.cpp", "build/repair-bde.exe", "repair-bde.exe"),
    ("tools/bootsect_app.cpp", "build/bootsect.exe", "bootsect.exe"),
    ("tools/bootcfg_app.cpp", "build/bootcfg.exe", "bootcfg.exe"),
    ("tools/label_app.cpp", "build/label.exe", "label.exe"),
    ("tools/vol_app.cpp", "build/vol.exe", "vol.exe"),
    ("tools/subst_app.cpp", "build/subst.exe", "subst.exe"),
    ("tools/tree_app.cpp", "build/tree.exe", "tree.exe"),
    ("tools/rc_app.cpp", "build/rc.exe", "rc.exe"),
    ("tools/cvtres_app.cpp", "build/cvtres.exe", "cvtres.exe"),
    ("tools/mc_app.cpp", "build/mc.exe", "mc.exe"),
    ("tools/muicache_app.cpp", "build/muicache.exe", "muicache.exe"),
    ("tools/netmsg_app.cpp", "build/netmsg.exe", "netmsg.exe"),
    ("tools/help_app.cpp", "build/help.exe", "help.exe"),
    ("tools/replace_app.cpp", "build/replace.exe", "replace.exe"),
    ("tools/recover_app.cpp", "build/recover.exe", "recover.exe"),
    ("tools/append_app.cpp", "build/append.exe", "append.exe"),
    ("tools/graftabl_app.cpp", "build/graftabl.exe", "graftabl.exe"),
    ("tools/ktmutil_app.cpp", "build/ktmutil.exe", "ktmutil.exe"),
    ("tools/fsquirt_app.cpp", "build/fsquirt.exe", "fsquirt.exe"),
    ("tools/eudcedit_app.cpp", "build/eudcedit.exe", "eudcedit.exe"),
    ("tools/narrator_app.cpp", "build/narrator.exe", "narrator.exe"),
    ("tools/setx_app.cpp", "build/setx.exe", "setx.exe"),
    ("tools/wsl_app.cpp", "build/wsl.exe", "wsl.exe"),
    ("tools/bash_app.cpp", "build/bash.exe", "bash.exe"),
    ("tools/curl_app.cpp", "build/curl.exe", "curl.exe"),
    ("tools/tar_app.cpp", "build/tar.exe", "tar.exe"),
    ("tools/ssh_app.cpp", "build/ssh.exe", "ssh.exe"),
    ("tools/scp_app.cpp", "build/scp.exe", "scp.exe"),
    ("tools/sftp_app.cpp", "build/sftp.exe", "sftp.exe"),
    ("tools/ssh_keygen_app.cpp", "build/ssh-keygen.exe", "ssh-keygen.exe"),
    ("tools/winget_app.cpp", "build/winget.exe", "winget.exe"),
    ("tools/quser_app.cpp", "build/quser.exe", "quser.exe"),
    ("tools/qprocess_app.cpp", "build/qprocess.exe", "qprocess.exe"),
    ("tools/clipbrd_app.cpp", "build/clipbrd.exe", "clipbrd.exe"),
    ("tools/displayswitch_app.cpp", "build/displayswitch.exe", "displayswitch.exe"),
    ("tools/tabcal_app.cpp", "build/tabcal.exe", "tabcal.exe"),
    ("tools/stikynot_app.cpp", "build/stikynot.exe", "stikynot.exe"),
    ("tools/wt_app.cpp", "build/wt.exe", "wt.exe"),
]

SYSTEM32_SERVICES = [
    ("tools/smss_app.cpp", "build/smss.exe", "smss.exe"),
    ("tools/csrss_app.cpp", "build/csrss.exe", "csrss.exe"),
    ("tools/winlogon_app.cpp", "build/winlogon.exe", "winlogon.exe"),
    ("tools/services_app.cpp", "build/services.exe", "services.exe"),
    ("tools/lsass_app.cpp", "build/lsass.exe", "lsass.exe"),
    ("tools/svchost_app.cpp", "build/svchost.exe", "svchost.exe"),
    ("tools/conhost_app.cpp", "build/conhost.exe", "conhost.exe"),
]

KNOWN_DLLS = [
    ("tools/dlls/ntdll.cpp", "build/ntdll.dll", "ntdll.dll"),
    ("tools/dlls/abi/bcrypt_system32.cpp", "build/bcrypt.dll", "bcrypt.dll"),
    ("tools/dlls/abi/secur32_system32.cpp", "build/secur32.dll", "secur32.dll"),
    ("tools/dlls/ucrtbase.cpp", "build/ucrtbase.dll", "ucrtbase.dll"),
    ("tools/dlls/ws2_32.cpp", "build/ws2_32.dll", "ws2_32.dll"),
    ("tools/dlls/dnsapi.cpp", "build/dnsapi.dll", "dnsapi.dll"),
    ("tools/dlls/crypt32.cpp", "build/crypt32.dll", "crypt32.dll"),
    ("tools/dlls/winhttp.cpp", "build/winhttp.dll", "winhttp.dll"),
    ("tools/dlls/kernel32.cpp", "build/kernel32.dll", "kernel32.dll"),
    ("tools/dlls/kernelbase.cpp", "build/kernelbase.dll", "kernelbase.dll"),
    ("tools/dlls/advapi32.cpp", "build/advapi32.dll", "advapi32.dll"),
    ("tools/dlls/comctl32.cpp", "build/comctl32.dll", "comctl32.dll"),
    ("tools/dlls/comdlg32.cpp", "build/comdlg32.dll", "comdlg32.dll"),
    ("tools/dlls/shell32.cpp", "build/shell32.dll", "shell32.dll"),
    ("tools/dlls/shlwapi.cpp", "build/shlwapi.dll", "shlwapi.dll"),
    ("tools/dlls/ole32.cpp", "build/ole32.dll", "ole32.dll"),
    ("tools/dlls/oleaut32.cpp", "build/oleaut32.dll", "oleaut32.dll"),
    ("tools/dlls/version.cpp", "build/version.dll", "version.dll"),
]

WINDOWING_DLLS = [
    ("kernel/ps/winnt_win32k_server.cpp", "win32k.dll", "MICANT_WIN32K_EXPORTS", False),
    ("tools/dlls/gdi32.cpp", "gdi32.dll", "BUILD_SYSTEM32_GDI32_DLL", True),
    ("tools/dlls/user32.cpp", "user32.dll", "BUILD_SYSTEM32_USER32_DLL", True),
    ("tools/dlls/imm32.cpp", "imm32.dll", "BUILD_SYSTEM32_IMM32_DLL", True),
]

DRIVER_STUBS = [
    ("tools/drivers/driver_template.cpp", "build/ahci.sys", "ahci.sys"),
    ("tools/drivers/driver_template.cpp", "build/tcpip.sys", "tcpip.sys"),
    ("tools/drivers/driver_template.cpp", "build/fltmgr.sys", "fltmgr.sys"),
    ("tools/drivers/driver_template.cpp", "build/xhci.sys", "xhci.sys"),
    ("tools/drivers/driver_template.cpp", "build/usbhub.sys", "usbhub.sys"),
    ("tools/drivers/driver_template.cpp", "build/extfs.sys", "extfs.sys"),
    ("tools/drivers/driver_template.cpp", "build/null.sys", "null.sys"),
    ("tools/drivers/driver_template.cpp", "build/beep.sys", "beep.sys"),
    ("tools/drivers/ntfs_driver.cpp", "build/ntfs.sys", "ntfs.sys"),
    ("tools/drivers/exfat_driver.cpp", "build/exfat.sys", "exfat.sys"),
    ("tools/drivers/cdfs_driver.cpp", "build/cdfs.sys", "cdfs.sys"),
    ("tools/drivers/udfs_driver.cpp", "build/udfs.sys", "udfs.sys"),
    ("tools/drivers/refs_driver.cpp", "build/refs.sys", "refs.sys"),
    ("tools/drivers/zfs_driver.cpp", "build/zfs.sys", "zfs.sys"),
]

NETWORK_FILES = [
    ("tools/etc/hosts", "hosts"),
    ("tools/etc/services", "services"),
    ("tools/etc/networks", "networks"),
    ("tools/etc/protocol", "protocol"),
]

REGISTRY_HIVES = [
    ("tools/config/SYSTEM", "SYSTEM"),
    ("tools/config/SOFTWARE", "SOFTWARE"),
    ("tools/config/SAM", "SAM"),
    ("tools/config/SECURITY", "SECURITY"),
    ("tools/config/DEFAULT", "DEFAULT"),
]

def run_local_build():
    print("[1/5] Verifying boot artifacts and compiling clean-room userland...")
    validate_build_manifest(Path(REPO_ROOT))
    for path in (EFI_SOURCE, KRNL_SOURCE, VIRTIO_GPU_SOURCE):
        print(f"      {os.path.basename(path)} verified ({os.path.getsize(path):,} bytes)")

    cmd_app = [
        "clang++",
        "-target", "x86_64-w64-mingw32",
        "-std=c++23",
        "-ffreestanding",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-stack-protector",
        "-mno-red-zone",
        "-mno-stack-arg-probe",
        "-O2",
        "-Wl,--subsystem,console",
        "-Wl,-e,_start",
        "-nostdlib",
        "tools/micant_app.cpp",
        "-o", "build/app.exe"
    ]
    res_a = subprocess.run(cmd_app, cwd=REPO_ROOT, capture_output=True, text=True)
    if res_a.returncode != 0:
        print(f"[FATAL] app.exe build failed:\n{res_a.stderr}")
        sys.exit(1)
    sz_a = os.path.getsize(APP_SOURCE)
    print(f"      app.exe Compiled OK ({sz_a:,} bytes)")

    cmd_bench = [
        "clang++",
        "-target", "x86_64-w64-mingw32",
        "-std=c++23",
        "-ffreestanding",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-stack-protector",
        "-mno-red-zone",
        "-mno-stack-arg-probe",
        "-O2",
        "-Wl,--subsystem,console",
        "-Wl,-e,_start",
        "-nostdlib",
        "tools/bench_app.cpp",
        "-o", "build/bench.exe"
    ]
    res_b = subprocess.run(cmd_bench, cwd=REPO_ROOT, capture_output=True, text=True)
    if res_b.returncode != 0:
        print(f"[FATAL] bench.exe build failed:\n{res_b.stderr}")
        sys.exit(1)
    sz_b = os.path.getsize(BENCH_SOURCE)
    print(f"      bench.exe Compiled OK ({sz_b:,} bytes)")

    cli_common_path = os.path.join(REPO_ROOT, "tools/cli_common.hpp")
    cli_mtime = os.path.getmtime(cli_common_path) if os.path.exists(cli_common_path) else 0

    # Compile System32 Standalone Ring 3 Utilities (Incremental)
    print(f"      Compiling {len(SYSTEM32_TOOLS)} System32 Utilities & Desktop Apps...")
    for src, out_path, name in SYSTEM32_TOOLS:
        # explorer.exe links to the win32k import library, built below.
        if name in ("explorer.exe", "lucecrash.exe"):
            continue
        full_src = os.path.join(REPO_ROOT, src)
        full_out = os.path.join(REPO_ROOT, out_path)
        if os.path.exists(full_out) and os.path.exists(full_src):
            src_mtime = os.path.getmtime(full_src)
            out_mtime = os.path.getmtime(full_out)
            if out_mtime >= max(src_mtime, cli_mtime):
                continue

        c_cmd = [
            "clang++",
            "-target", "x86_64-w64-mingw32",
            "-std=c++23",
            "-ffreestanding",
            "-fno-exceptions",
            "-fno-rtti",
            "-fno-stack-protector",
            "-mno-red-zone",
            "-mno-stack-arg-probe",
            "-O2",
            "-Iinclude",
            "-Itools",
            "-Wl,--subsystem,console",
            "-Wl,-e,_start",
            "-nostdlib",
            src,
            "-o", out_path
        ]
        r = subprocess.run(c_cmd, cwd=REPO_ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[FATAL] {name} build failed:\n{r.stderr}")
            sys.exit(1)

    # Compile System32 Subsystem Services (Incremental)
    print("      Compiling 7 System32 Subsystem Services...")
    for src, out_path, name in SYSTEM32_SERVICES:
        # svchost links the private DHCP implementation and the native WS2
        # import library, which are emitted by the Known DLL pass below.
        if name == "svchost.exe":
            continue
        full_src = os.path.join(REPO_ROOT, src)
        full_out = os.path.join(REPO_ROOT, out_path)
        if os.path.exists(full_out) and os.path.exists(full_src):
            if os.path.getmtime(full_out) >= os.path.getmtime(full_src):
                continue

        c_cmd = [
            "clang++",
            "-target", "x86_64-w64-mingw32",
            "-std=c++23",
            "-ffreestanding",
            "-fno-exceptions",
            "-fno-rtti",
            "-fno-stack-protector",
            "-mno-red-zone",
            "-mno-stack-arg-probe",
            "-O2",
            "-Iinclude",
            "-Itools",
            "-Wl,--subsystem,console",
            "-Wl,-e,_start",
            "-nostdlib",
            src,
            "-o", out_path
        ]
        r = subprocess.run(c_cmd, cwd=REPO_ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[FATAL] {name} build failed:\n{r.stderr}")
            sys.exit(1)

    # Compile native Known DLLs (PE32+ Shared Libraries, Incremental)
    print("      Compiling Known DLLs...")
    for src, out_path, name in KNOWN_DLLS:
        full_src = os.path.join(REPO_ROOT, src)
        full_out = os.path.join(REPO_ROOT, out_path)
        auxiliary_output = {
            "ntdll.dll": os.path.join(REPO_ROOT, "build", "libntdll.dll.a"),
            "kernel32.dll": os.path.join(REPO_ROOT, "build", "libkernel32.dll.a"),
            "ucrtbase.dll": os.path.join(REPO_ROOT, "build", "libucrtbase.dll.a"),
            "ws2_32.dll": os.path.join(REPO_ROOT, "build", "libws2_32.dll.a"),
            "dnsapi.dll": os.path.join(REPO_ROOT, "build", "libdnsapi.dll.a"),
            "crypt32.dll": os.path.join(REPO_ROOT, "build", "libcrypt32.dll.a"),
        }.get(name)
        runtime_dependencies = []
        if name == "ntdll.dll":
            # Rebuild when the no-entry link policy itself changes.
            runtime_dependencies.append("scripts/stage_micant.py")
        if name == "bcrypt.dll":
            runtime_dependencies.append("tools/dlls/abi/bcrypt_crypt32_secur32_wintrust_impl.inc")
        if name == "secur32.dll":
            runtime_dependencies.append("tools/dlls/abi/bcrypt_crypt32_secur32_wintrust_impl.inc")
        if name == "kernel32.dll":
            runtime_dependencies.extend((
                "tools/dlls/abi/kernel32_advapi32_impl.inc",
                "tools/dlls/native_stack_probe.cpp",
                "kernel/ex/freestanding_memory.cpp",
            ))
        if name == "version.dll":
            runtime_dependencies.extend(("tools/dlls/version.def", "build/libkernel32.dll.a"))
        if name in ("ntdll.dll", "ws2_32.dll", "dnsapi.dll", "crypt32.dll", "winhttp.dll"):
            runtime_dependencies.append("tools/dlls/native_stack_probe.cpp")
        if name in ("ws2_32.dll", "crypt32.dll", "winhttp.dll"):
            runtime_dependencies.append("kernel/ex/freestanding_memory.cpp")
        if name == "winhttp.dll":
            runtime_dependencies.extend((
                "include/micant/um/subsystems/net/winhttp.hpp",
                "include/micant/shared/native_network_api.hpp",
                "include/micant/shared/native_nt_api.hpp",
                "include/micant/shared/native_crypt32_api.hpp",
                "include/micant/shared/bounded_containers.hpp",
                "include/micant/shared/crypto_primitives.hpp",
                "include/micant/shared/x509_profile.hpp",
                "include/micant/shared/tls13_client.hpp",
            ))
        if name == "crypt32.dll":
            runtime_dependencies.extend((
                "include/micant/shared/native_nt_api.hpp",
                "include/micant/shared/native_crypt32_api.hpp",
                "include/micant/shared/bounded_containers.hpp",
                "include/micant/shared/crypto_primitives.hpp",
                "include/micant/shared/x509_profile.hpp",
            ))
        if name == "ws2_32.dll":
            runtime_dependencies.extend((
                "include/micant/shared/native_nt_api.hpp",
                "include/micant/shared/afd_socket_abi.hpp",
                "include/micant/shared/bounded_containers.hpp",
            ))
        if os.path.exists(full_out) and os.path.exists(full_src):
            if (os.path.getmtime(full_out) >= max(os.path.getmtime(os.path.join(REPO_ROOT, item))
                    for item in [src] + runtime_dependencies) and
                    (not auxiliary_output or os.path.exists(auxiliary_output))):
                continue

        c_cmd = [
            "clang++",
            "-target", "x86_64-w64-mingw32",
            "-shared",
            "-nostdlib",
            "-Iinclude",
            "-Wl,--entry=" if name == "ntdll.dll" else
                ("-Wl,-e,DllMainCRTStartup" if name in ("kernel32.dll", "bcrypt.dll", "secur32.dll") else "-Wl,-e,DllMain"),
            "-Wl,--subsystem,native",
            "-O2",
        ]
        if name in ("ntdll.dll", "ws2_32.dll", "dnsapi.dll", "crypt32.dll", "winhttp.dll"):
            c_cmd.extend(("-std=c++23", "-ffreestanding", "-fno-builtin",
                          "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                          "-fno-stack-protector", "-mno-red-zone"))
        if name == "ntdll.dll":
            c_cmd.extend(("-fuse-ld=lld", "-Wl,--out-implib,build/libntdll.dll.a"))
        if name == "kernel32.dll":
            c_cmd.extend(("-std=c++23", "-ffreestanding", "-fno-builtin",
                          "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                          "-fno-stack-protector", "-mno-red-zone",
                          "-DMICANT_FREESTANDING_KERNEL32", "-DBUILD_SYSTEM32_KERNEL32_DLL",
                          "-Wl,--out-implib,build/libkernel32.dll.a", src,
                          "tools/dlls/native_stack_probe.cpp", "kernel/ex/freestanding_memory.cpp",
                          "build/libntdll.dll.a", "build/libucrtbase.dll.a", "-o", out_path))
        elif name == "bcrypt.dll":
            c_cmd.extend(("-std=c++23", "-ffreestanding", "-fno-builtin",
                          "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                          "-fno-stack-protector", "-mno-red-zone",
                          "-ffunction-sections", "-fdata-sections",
                          "-DBUILD_SYSTEM32_STANDALONE_DLL", "-DBUILD_SYSTEM32_BCRYPT_DLL",
                          "-Wl,--gc-sections", src, "kernel/ex/freestanding_memory.cpp",
                          "-o", out_path))
        elif name == "secur32.dll":
            c_cmd.extend(("-std=c++23", "-ffreestanding", "-fno-builtin",
                          "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                          "-fno-stack-protector", "-mno-red-zone",
                          "-ffunction-sections", "-fdata-sections",
                          "-DBUILD_SYSTEM32_STANDALONE_DLL", "-DBUILD_SYSTEM32_SECUR32_DLL",
                          "-Wl,--gc-sections", src, "-o", out_path))
        elif name == "ucrtbase.dll":
            c_cmd.extend(("-Wl,--out-implib,build/libucrtbase.dll.a", src, "-o", out_path))
        elif name == "ws2_32.dll":
            c_cmd.extend(("-DMICANT_NATIVE_USERMODE", "-Wl,--out-implib,build/libws2_32.dll.a", src,
                          "tools/dlls/native_stack_probe.cpp", "kernel/ex/freestanding_memory.cpp",
                          "build/libntdll.dll.a", "-o", out_path))
        elif name == "dnsapi.dll":
            c_cmd.extend(("-DMICANT_NATIVE_USERMODE", "-Wl,--out-implib,build/libdnsapi.dll.a",
                          src, "tools/dlls/native_stack_probe.cpp", "build/libws2_32.dll.a",
                          "build/libntdll.dll.a", "-o", out_path))
        elif name == "crypt32.dll":
            c_cmd.extend(("-DMICANT_NATIVE_USERMODE", "-Wl,--out-implib,build/libcrypt32.dll.a", src,
                          "tools/dlls/native_stack_probe.cpp", "kernel/ex/freestanding_memory.cpp",
                          "build/libntdll.dll.a", "-o", out_path))
        elif name == "winhttp.dll":
            c_cmd.extend(("-DMICANT_NATIVE_USERMODE", "-Wl,--out-implib,build/libwinhttp.dll.a", src,
                          "tools/dlls/native_stack_probe.cpp", "kernel/ex/freestanding_memory.cpp",
                          "build/libcrypt32.dll.a", "build/libdnsapi.dll.a", "build/libws2_32.dll.a", "build/libntdll.dll.a",
                          "-o", out_path))
        elif name == "ntdll.dll":
            c_cmd.extend((src, "tools/dlls/native_stack_probe.cpp", "-o", out_path))
        elif name == "version.dll":
            c_cmd.extend(("-std=c++23", "-ffreestanding", "-fno-builtin",
                          "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                          "-fno-stack-protector", "-mno-red-zone", src,
                          "tools/dlls/version.def",
                          "build/libkernel32.dll.a", "-o", out_path))
        else:
            c_cmd.extend((src, "-o", out_path))
        r = subprocess.run(c_cmd, cwd=REPO_ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[FATAL] {name} build failed:\n{r.stderr}")
            sys.exit(1)

    svchost_source = os.path.join(REPO_ROOT, "tools", "svchost_app.cpp")
    dhcp_source = os.path.join(REPO_ROOT, "tools", "dlls", "dhcpcsvc.cpp")
    svchost_output = os.path.join(REPO_ROOT, "build", "svchost.exe")
    ws2_import = os.path.join(REPO_ROOT, "build", "libws2_32.dll.a")
    ntdll_import = os.path.join(REPO_ROOT, "build", "libntdll.dll.a")
    if (not os.path.exists(svchost_output) or
            os.path.getmtime(svchost_output) < max(os.path.getmtime(svchost_source), os.path.getmtime(dhcp_source))):
        svchost_cmd = [
            "clang++", "-target", "x86_64-w64-mingw32", "-std=c++23",
            "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-stack-protector",
            "-mno-red-zone", "-mno-stack-arg-probe", "-O2", "-DMICANT_NATIVE_USERMODE",
            "-Iinclude", "-Itools", "-Wl,--subsystem,console", "-Wl,-e,_start", "-nostdlib",
            svchost_source, dhcp_source, ws2_import, ntdll_import, "-o", "build/svchost.exe",
        ]
        r = subprocess.run(svchost_cmd, cwd=REPO_ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[FATAL] svchost.exe build failed:\n{r.stderr}")
            sys.exit(1)

    # Build the real User32/GDI32/IMM32 -> win32k.dll chain, with exact
    # Windows DLL names and an import library shared by all three clients.
    windowing_dir = os.path.join(REPO_ROOT, "bin", "system32")
    os.makedirs(windowing_dir, exist_ok=True)
    win32k_dll = os.path.join(windowing_dir, "win32k.dll")
    win32k_implib = os.path.join(windowing_dir, "libwin32k.dll.a")
    win32k_src = "kernel/ps/winnt_win32k_server.cpp"
    win32k_cmd = [
        "clang++", "-target", "x86_64-w64-mingw32", "-std=c++23", "-O2",
        "-Iinclude", "-DBUILD_SYSTEM32_STANDALONE_DLL", "-DMICANT_WIN32K_EXPORTS",
        "-shared", "-static", "-fno-rtti", "-fno-threadsafe-statics",
        "-fno-use-cxa-atexit", "-ffunction-sections", "-fdata-sections",
        "-Wl,--gc-sections", "-Wl,-e,DllMain",
        f"-Wl,--out-implib,{win32k_implib}", win32k_src,
        "-o", win32k_dll,
    ]
    r = subprocess.run(win32k_cmd, cwd=REPO_ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"[FATAL] win32k.dll build failed:\n{r.stderr}")
        sys.exit(1)

    # The shell imports only the documented user32.dll/gdi32.dll client APIs;
    # those DLLs translate the Windows ABI to MicaNT's private win32k backend.
    explorer_cmd = [
        "clang++", "-target", "x86_64-w64-mingw32", "-std=c++23",
        "-ffreestanding", "-fno-exceptions", "-fno-rtti", "-fno-stack-protector",
        "-mno-red-zone", "-mno-stack-arg-probe", "-O2", "-Iinclude", "-Itools",
        "-Wl,--subsystem,windows", "-Wl,-e,_start", "-nostdlib",
        "tools/explorer_shell_app.cpp", "tools/explorer_shell/taskbar.cpp",
        "tools/explorer_shell/task_switcher.cpp",
        "tools/explorer_shell/start_menu.cpp", "tools/explorer_shell/desktop.cpp",
        "tools/explorer_shell/notification_area.cpp",
        "-luser32", "-lgdi32", "-o", "build/explorer.exe",
    ]
    explorer_result = subprocess.run(explorer_cmd, cwd=REPO_ROOT, capture_output=True, text=True)
    if explorer_result.returncode != 0:
        print(f"[FATAL] explorer.exe build failed:\n{explorer_result.stderr}")
        sys.exit(1)

    for src, dll_name, export_def, imports_win32k in WINDOWING_DLLS[1:]:
        dll_path = os.path.join(windowing_dir, dll_name)
        import_library = os.path.join(windowing_dir, f"lib{dll_name}.a")
        cmd = [
            "clang++", "-target", "x86_64-w64-mingw32", "-std=c++23", "-O2",
            "-Iinclude", "-DBUILD_SYSTEM32_STANDALONE_DLL", f"-D{export_def}",
            "-shared", "-static", "-fno-rtti", "-fno-threadsafe-statics",
            "-fno-use-cxa-atexit", "-ffunction-sections", "-fdata-sections",
            "-Wl,--gc-sections", "-Wl,-e,DllMain",
            f"-Wl,--out-implib,{import_library}",
        ]
        link_win32k = False
        if imports_win32k:
            cmd.append("-DMICANT_WIN32K_IMPORTS")
            link_win32k = True
        cmd.extend([src, "-o", dll_path])
        if link_win32k:
            cmd.append(win32k_implib)
        if dll_name == "user32.dll":
            cmd.append(os.path.join(windowing_dir, "libgdi32.dll.a"))
        r = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[FATAL] {dll_name} build failed:\n{r.stderr}")
            sys.exit(1)

    # Compile Kernel Driver Stubs (PE32+ Native Drivers, Incremental)
    print("      Compiling 14 Kernel Driver Stubs (.sys)...")
    for src, out_path, name in DRIVER_STUBS:
        full_src = os.path.join(REPO_ROOT, src)
        full_out = os.path.join(REPO_ROOT, out_path)
        if os.path.exists(full_out) and os.path.exists(full_src):
            if os.path.getmtime(full_out) >= os.path.getmtime(full_src):
                continue

        c_cmd = [
            "clang++",
            "-target", "x86_64-w64-mingw32",
            "-nostdlib",
            "-Wl,-e,DriverEntry",
            "-Wl,--subsystem,native",
            "-O2",
            src,
            "-o", out_path
        ]
        r = subprocess.run(c_cmd, cwd=REPO_ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[FATAL] {name} build failed:\n{r.stderr}")
            sys.exit(1)

    # Generate Registry Hives if needed
    hives_script = os.path.join(REPO_ROOT, "tools", "config", "gen_hives.py")
    if os.path.exists(hives_script):
        subprocess.run([sys.executable, hives_script], cwd=REPO_ROOT, check=True)

    print("      Local build completed successfully!")

def get_ssh():
    k = paramiko.RSAKey.from_private_key_file(KEY_PATH)
    for _ in range(20):
        try:
            s = socket.socket()
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            port = random.randint(49152, 65535)
            s.bind(('0.0.0.0', port))
            s.settimeout(5.0)
            s.connect((HOST, 22))
            client = paramiko.SSHClient()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            client.connect(HOST, 22, USER, pkey=k, sock=s, timeout=5)
            return client
        except OSError:
            pass
    raise RuntimeError('Could not connect')

def exec_sudo(ssh, cmd):
    sudo_pass = os.environ.get("MICANT_SUDO_PASS", "")
    if sudo_pass:
        full_cmd = f"echo {sudo_pass} | sudo -S bash -c {cmd!r}"
    else:
        full_cmd = f"sudo bash -c {cmd!r}"
    stdin, stdout, stderr = ssh.exec_command(full_cmd)
    out = stdout.read().decode('utf-8', errors='replace').strip()
    err = stderr.read().decode('utf-8', errors='replace').strip()
    rc = stdout.channel.recv_exit_status()
    clean_err = "\n".join(l for l in err.splitlines() if "[sudo]" not in l and "password" not in l.lower())
    return rc, out, clean_err

def main():
    run_local_build()

    print("[2/5] Connecting to server via SSH key...")
    ssh = get_ssh()
    print("      Connected as 'ubuntu'.")

    print("[3/5] Uploading binaries and configuration files via SFTP...")
    sftp = ssh.open_sftp()
    sftp.put(EFI_SOURCE, "/tmp/BOOTX64.EFI")
    sftp.put(KRNL_SOURCE, "/tmp/ntoskrnl.exe")
    sftp.put(VIRTIO_GPU_SOURCE, "/tmp/virtio_gpu.sys")
    sftp.put(APP_SOURCE, "/tmp/app.exe")
    sftp.put(BENCH_SOURCE, "/tmp/bench.exe")
    if os.path.exists(CUBE_SOURCE):
        sftp.put(CUBE_SOURCE, "/tmp/prismx_3d_cube_app.exe")
    surshell_path = os.path.join(REPO_ROOT, "build", "surshell.exe")
    if not os.path.exists(surshell_path):
        alt_surshell = r"C:\Users\admin\source\SurShell\build\bin\surshell_app.exe"
        if os.path.exists(alt_surshell):
            import shutil
            shutil.copy2(alt_surshell, surshell_path)
    if os.path.exists(surshell_path):
        sftp.put(surshell_path, "/tmp/surshell.exe")

    for _, out_path, name in SYSTEM32_TOOLS:
        p = os.path.join(REPO_ROOT, out_path)
        if os.path.exists(p):
            sftp.put(p, f"/tmp/{name}")

    for _, out_path, name in SYSTEM32_SERVICES:
        p = os.path.join(REPO_ROOT, out_path)
        if os.path.exists(p):
            sftp.put(p, f"/tmp/{name}")

    for _, out_path, name in KNOWN_DLLS:
        p = os.path.join(REPO_ROOT, out_path)
        if os.path.exists(p):
            sftp.put(p, f"/tmp/{name}")

    for _, name, _, _ in WINDOWING_DLLS:
        p = os.path.join(REPO_ROOT, "bin", "system32", name)
        if os.path.exists(p):
            sftp.put(p, f"/tmp/{name}")

    for _, out_path, name in DRIVER_STUBS:
        p = os.path.join(REPO_ROOT, out_path)
        if os.path.exists(p):
            sftp.put(p, f"/tmp/{name}")

    # Real boot-start OEM images, built by the canonical CMake targets.
    for name in ("storahci.sys", "hidusb.sys", "usbstor.sys", "usbxhci.sys", "ehci.sys", "i210.sys", "virtio_net.sys"):
        p = os.path.join(REPO_ROOT, "build", name)
        if os.path.exists(p):
            sftp.put(p, f"/tmp/{name}")

    for src_rel, name in NETWORK_FILES:
        p = os.path.join(REPO_ROOT, src_rel)
        if os.path.exists(p):
            sftp.put(p, f"/tmp/{name}")

    for src_rel, name in REGISTRY_HIVES:
        p = os.path.join(REPO_ROOT, src_rel)
        if os.path.exists(p):
            sftp.put(p, f"/tmp/{name}")

    stage_script = """#!/bin/bash
set -e
echo "--- Unmounting and Checking Filesystems ---"
umount /mnt/micasys 2>/dev/null || true
umount /mnt/micaesp 2>/dev/null || true
mkdir -p /mnt/micasys /mnt/micaesp

# Clear any dirty bits or FAT discrepancies
fsck.vfat -a -w /dev/disk/by-partuuid/b6457ee0-ed3e-4d78-b82c-a08971cc1b52 || true
fsck.vfat -a -w /dev/disk/by-partuuid/0fb58002-611e-491c-829d-1d7c669fdef2 || true

echo "--- Mounting Target Partitions ---"
mount -o rw,umask=000 /dev/disk/by-partuuid/b6457ee0-ed3e-4d78-b82c-a08971cc1b52 /mnt/micasys
mount -o rw,umask=000 /dev/disk/by-partuuid/0fb58002-611e-491c-829d-1d7c669fdef2 /mnt/micaesp

mkdir -p /mnt/micasys/MicaNT/System32/drivers/etc
mkdir -p /mnt/micasys/MicaNT/System32/drivers
mkdir -p /mnt/micasys/MicaNT/System32/config
mkdir -p /mnt/micasys/MicaNT/System32/LogFiles

echo "--- Copying Kernel and Userland Executables ---"
cp -v /tmp/ntoskrnl.exe /mnt/micasys/MicaNT/System32/
cp -v /tmp/ntoskrnl.exe /mnt/micasys/
cp -v /tmp/virtio_gpu.sys /mnt/micasys/MicaNT/System32/drivers/
cp -v /tmp/app.exe /mnt/micasys/MicaNT/System32/
cp -v /tmp/bench.exe /mnt/micasys/MicaNT/System32/
[ -f /tmp/prismx_3d_cube_app.exe ] && cp -v /tmp/prismx_3d_cube_app.exe /mnt/micasys/MicaNT/System32/ && cp -v /tmp/prismx_3d_cube_app.exe /mnt/micasys/ || true

for f in /tmp/*.exe; do
    if [ -f "$f" ]; then
        cp -v "$f" /mnt/micasys/MicaNT/System32/
        cp -v "$f" /mnt/micasys/
    fi
done

echo "--- Generating MMC Snap-in Consoles (.msc) & Control Panel Applets (.cpl) ---"
cp -v /mnt/micasys/MicaNT/System32/devmgmt.exe /mnt/micasys/MicaNT/System32/devmgmt.msc || true
cp -v /mnt/micasys/MicaNT/System32/devmgmt.exe /mnt/micasys/devmgmt.msc || true
cp -v /mnt/micasys/MicaNT/System32/diskmgmt.exe /mnt/micasys/MicaNT/System32/diskmgmt.msc || true
cp -v /mnt/micasys/MicaNT/System32/diskmgmt.exe /mnt/micasys/diskmgmt.msc || true
cp -v /mnt/micasys/MicaNT/System32/services_mgmt.exe /mnt/micasys/MicaNT/System32/services.msc || true
cp -v /mnt/micasys/MicaNT/System32/services_mgmt.exe /mnt/micasys/services.msc || true
cp -v /mnt/micasys/MicaNT/System32/eventvwr.exe /mnt/micasys/MicaNT/System32/eventvwr.msc || true
cp -v /mnt/micasys/MicaNT/System32/eventvwr.exe /mnt/micasys/eventvwr.msc || true
cp -v /mnt/micasys/MicaNT/System32/compmgmt.exe /mnt/micasys/MicaNT/System32/compmgmt.msc || true
cp -v /mnt/micasys/MicaNT/System32/compmgmt.exe /mnt/micasys/compmgmt.msc || true
cp -v /mnt/micasys/MicaNT/System32/perfmon.exe /mnt/micasys/MicaNT/System32/perfmon.msc || true
cp -v /mnt/micasys/MicaNT/System32/perfmon.exe /mnt/micasys/perfmon.msc || true
cp -v /mnt/micasys/MicaNT/System32/sysdm.exe /mnt/micasys/MicaNT/System32/sysdm.cpl || true
cp -v /mnt/micasys/MicaNT/System32/sysdm.exe /mnt/micasys/sysdm.cpl || true
cp -v /mnt/micasys/MicaNT/System32/ncpa.exe /mnt/micasys/MicaNT/System32/ncpa.cpl || true
cp -v /mnt/micasys/MicaNT/System32/ncpa.exe /mnt/micasys/ncpa.cpl || true
cp -v /mnt/micasys/MicaNT/System32/appwiz.exe /mnt/micasys/MicaNT/System32/appwiz.cpl || true
cp -v /mnt/micasys/MicaNT/System32/appwiz.exe /mnt/micasys/appwiz.cpl || true
cp -v /mnt/micasys/MicaNT/System32/desk.exe /mnt/micasys/MicaNT/System32/desk.cpl || true
cp -v /mnt/micasys/MicaNT/System32/desk.exe /mnt/micasys/desk.cpl || true
cp -v /mnt/micasys/MicaNT/System32/mmsys.exe /mnt/micasys/MicaNT/System32/mmsys.cpl || true
cp -v /mnt/micasys/MicaNT/System32/mmsys.exe /mnt/micasys/mmsys.cpl || true
cp -v /mnt/micasys/MicaNT/System32/powercfg_cpl.exe /mnt/micasys/MicaNT/System32/powercfg.cpl || true
cp -v /mnt/micasys/MicaNT/System32/powercfg_cpl.exe /mnt/micasys/powercfg.cpl || true
cp -v /mnt/micasys/MicaNT/System32/timedate.exe /mnt/micasys/MicaNT/System32/timedate.cpl || true
cp -v /mnt/micasys/MicaNT/System32/timedate.exe /mnt/micasys/timedate.cpl || true
cp -v /mnt/micasys/MicaNT/System32/firewall.exe /mnt/micasys/MicaNT/System32/firewall.cpl || true
cp -v /mnt/micasys/MicaNT/System32/firewall.exe /mnt/micasys/firewall.cpl || true
cp -v /mnt/micasys/MicaNT/System32/main.exe /mnt/micasys/MicaNT/System32/main.cpl || true
cp -v /mnt/micasys/MicaNT/System32/main.exe /mnt/micasys/main.cpl || true
cp -v /mnt/micasys/MicaNT/System32/inetcpl.exe /mnt/micasys/MicaNT/System32/inetcpl.cpl || true
cp -v /mnt/micasys/MicaNT/System32/inetcpl.exe /mnt/micasys/inetcpl.cpl || true
cp -v /mnt/micasys/MicaNT/System32/joy.exe /mnt/micasys/MicaNT/System32/joy.cpl || true
cp -v /mnt/micasys/MicaNT/System32/joy.exe /mnt/micasys/joy.cpl || true
cp -v /mnt/micasys/MicaNT/System32/intl.exe /mnt/micasys/MicaNT/System32/intl.cpl || true
cp -v /mnt/micasys/MicaNT/System32/intl.exe /mnt/micasys/intl.cpl || true
cp -v /mnt/micasys/MicaNT/System32/telephon.exe /mnt/micasys/MicaNT/System32/telephon.cpl || true
cp -v /mnt/micasys/MicaNT/System32/telephon.exe /mnt/micasys/telephon.cpl || true
cp -v /mnt/micasys/MicaNT/System32/write.exe /mnt/micasys/MicaNT/System32/wordpad.exe || true
cp -v /mnt/micasys/MicaNT/System32/write.exe /mnt/micasys/wordpad.exe || true
cp -v /mnt/micasys/MicaNT/System32/sndrec32.exe /mnt/micasys/MicaNT/System32/soundrecorder.exe || true
cp -v /mnt/micasys/MicaNT/System32/sndrec32.exe /mnt/micasys/soundrecorder.exe || true
cp -v /mnt/micasys/MicaNT/System32/gpedit.exe /mnt/micasys/MicaNT/System32/gpedit.msc || true
cp -v /mnt/micasys/MicaNT/System32/gpedit.exe /mnt/micasys/gpedit.msc || true
cp -v /mnt/micasys/MicaNT/System32/lusrmgr.exe /mnt/micasys/MicaNT/System32/lusrmgr.msc || true
cp -v /mnt/micasys/MicaNT/System32/lusrmgr.exe /mnt/micasys/lusrmgr.msc || true
cp -v /mnt/micasys/MicaNT/System32/certmgr.exe /mnt/micasys/MicaNT/System32/certmgr.msc || true
cp -v /mnt/micasys/MicaNT/System32/certmgr.exe /mnt/micasys/certmgr.msc || true
cp -v /mnt/micasys/MicaNT/System32/fsmgmt.exe /mnt/micasys/MicaNT/System32/fsmgmt.msc || true
cp -v /mnt/micasys/MicaNT/System32/fsmgmt.exe /mnt/micasys/fsmgmt.msc || true
cp -v /mnt/micasys/MicaNT/System32/secpol.exe /mnt/micasys/MicaNT/System32/secpol.msc || true
cp -v /mnt/micasys/MicaNT/System32/secpol.exe /mnt/micasys/secpol.msc || true
cp -v /mnt/micasys/MicaNT/System32/tpm.exe /mnt/micasys/MicaNT/System32/tpm.msc || true
cp -v /mnt/micasys/MicaNT/System32/tpm.exe /mnt/micasys/tpm.msc || true
cp -v /mnt/micasys/MicaNT/System32/wscui.exe /mnt/micasys/MicaNT/System32/wscui.cpl || true
cp -v /mnt/micasys/MicaNT/System32/wscui.exe /mnt/micasys/wscui.cpl || true
cp -v /mnt/micasys/MicaNT/System32/hdwwiz.exe /mnt/micasys/MicaNT/System32/hdwwiz.cpl || true
cp -v /mnt/micasys/MicaNT/System32/hdwwiz.exe /mnt/micasys/hdwwiz.cpl || true
cp -v /mnt/micasys/MicaNT/System32/chcp.exe /mnt/micasys/MicaNT/System32/chcp.com || true
cp -v /mnt/micasys/MicaNT/System32/chcp.exe /mnt/micasys/chcp.com || true
cp -v /mnt/micasys/MicaNT/System32/mode.exe /mnt/micasys/MicaNT/System32/mode.com || true
cp -v /mnt/micasys/MicaNT/System32/mode.exe /mnt/micasys/mode.com || true
cp -v /mnt/micasys/MicaNT/System32/graftabl.exe /mnt/micasys/MicaNT/System32/graftabl.com || true
cp -v /mnt/micasys/MicaNT/System32/graftabl.exe /mnt/micasys/graftabl.com || true

echo "--- Copying Known DLLs ---"
for f in /tmp/*.dll; do
    if [ -f "$f" ]; then
        cp -v "$f" /mnt/micasys/MicaNT/System32/
    fi
done

echo "--- Copying Kernel Drivers (.sys) ---"
for f in /tmp/*.sys; do
    if [ -f "$f" ]; then
        cp -v "$f" /mnt/micasys/MicaNT/System32/drivers/
    fi
done

echo "--- Copying Network Tables ---"
for f in hosts services networks protocol; do
    if [ -f "/tmp/$f" ]; then
        cp -v "/tmp/$f" /mnt/micasys/MicaNT/System32/drivers/etc/
    fi
done

echo "--- Copying Registry Hives ---"
for f in SYSTEM SOFTWARE SAM SECURITY DEFAULT; do
    if [ -f "/tmp/$f" ]; then
        cp -v "/tmp/$f" /mnt/micasys/MicaNT/System32/config/
    fi
done

echo "--- Mirroring Bootloader to ESP ---"
mkdir -p /mnt/micaesp/EFI/BOOT /mnt/micaesp/EFI/MicaNT
mkdir -p /mnt/micaesp/EFI/MicaNT/System32/drivers
cp -v /tmp/BOOTX64.EFI /mnt/micaesp/EFI/BOOT/BOOTX64.EFI
cp -v /tmp/BOOTX64.EFI /mnt/micaesp/EFI/MicaNT/BOOTX64.EFI
cp -v /tmp/ntoskrnl.exe /mnt/micaesp/EFI/MicaNT/
cp -v /tmp/virtio_gpu.sys /mnt/micaesp/EFI/MicaNT/System32/drivers/
for f in storahci.sys hidusb.sys usbstor.sys usbxhci.sys ehci.sys i210.sys virtio_net.sys; do
    [ ! -f "/tmp/$f" ] || cp -v "/tmp/$f" /mnt/micaesp/EFI/MicaNT/System32/drivers/
done
cp -v /tmp/ntoskrnl.exe /mnt/micaesp/
rm -f /mnt/micaesp/EFI/MicaNT/boot.ini /mnt/micaesp/boot.ini /mnt/micasys/boot.ini /mnt/micasys/EFI/MicaNT/boot.ini 2>/dev/null || true

mkdir -p /boot/efi/EFI/MicaNT
mkdir -p /boot/efi/EFI/MicaNT/System32/drivers
cp -v /tmp/BOOTX64.EFI /boot/efi/EFI/MicaNT/BOOTX64.EFI
cp -v /tmp/ntoskrnl.exe /boot/efi/EFI/MicaNT/
cp -v /tmp/virtio_gpu.sys /boot/efi/EFI/MicaNT/System32/drivers/
for f in storahci.sys hidusb.sys usbstor.sys usbxhci.sys ehci.sys i210.sys virtio_net.sys; do
    [ ! -f "/tmp/$f" ] || cp -v "/tmp/$f" /boot/efi/EFI/MicaNT/System32/drivers/
done
cp -v /tmp/ntoskrnl.exe /boot/efi/
rm -f /boot/efi/EFI/MicaNT/boot.ini /boot/efi/boot.ini 2>/dev/null || true

sync
umount /mnt/micasys /mnt/micaesp
echo "--- Staging Script Completed Successfully ---"
"""
    with sftp.open("/tmp/stage.sh", "w") as sf:
        sf.write(stage_script)
    sftp.close()

    print("      Staging files strictly to sovereign MicaNT partitions by PARTUUID...")
    exec_sudo(ssh, "chmod +x /tmp/stage.sh")
    rc, out, err = exec_sudo(ssh, "/tmp/stage.sh")
    if rc != 0:
        print(f"[FATAL] Copy failed:\nSTDOUT: {out}\nSTDERR: {err}")
        sys.exit(1)
    print("      Staging files copied successfully:\n" + out)

    print("[4/5] Verifying staged files...")
    verify_cmd = (
        'mkdir -p /mnt/micasys /mnt/micaesp && '
        'mount /dev/disk/by-partuuid/b6457ee0-ed3e-4d78-b82c-a08971cc1b52 /mnt/micasys && '
        'mount /dev/disk/by-partuuid/0fb58002-611e-491c-829d-1d7c669fdef2 /mnt/micaesp && '
        'echo "=== /mnt/micasys/MicaNT/System32/ (Executables & DLLs) ===" && '
        'ls -la /mnt/micasys/MicaNT/System32/ && '
        'echo "=== /mnt/micasys/MicaNT/System32/drivers/ (Kernel Drivers) ===" && '
        'ls -la /mnt/micasys/MicaNT/System32/drivers/ && '
        'echo "=== /mnt/micasys/MicaNT/System32/drivers/etc/ (Network Tables) ===" && '
        'ls -la /mnt/micasys/MicaNT/System32/drivers/etc/ && '
        'echo "=== /mnt/micasys/MicaNT/System32/config/ (Registry Hives) ===" && '
        'ls -la /mnt/micasys/MicaNT/System32/config/ && '
        'echo "=== /mnt/micaesp/EFI/MicaNT/ ===" && '
        'ls -la /mnt/micaesp/EFI/MicaNT/ && '
        'umount /mnt/micasys /mnt/micaesp && '
        'echo "=== /boot/efi/EFI/MicaNT/ ===" && '
        'ls -la /boot/efi/EFI/MicaNT/'
    )
    rc, out, err = exec_sudo(ssh, verify_cmd)
    print(f"      Verified files:\n{out}")

    print("[5/5] Ensuring UEFI NVRAM Boot Order (Ubuntu fallback primary)...")
    target_boot_order = "000C,000D,0003,0000,0008,0006,0004,0007,0005,0001,0002,0009,000B"
    exec_sudo(ssh, f"efibootmgr -o {target_boot_order}")
    exec_sudo(ssh, "efibootmgr -a -b 000D 2>/dev/null || true")

    rc, out, err = exec_sudo(ssh, "efibootmgr")
    print(out)

    sz = os.path.getsize(EFI_SOURCE)
    print(f"\n=======================================================")
    print(f"  MicaNT Cutler Edition is staged and ready on bare metal!")
    print(f"  MicaNT ESP (PARTUUID 0fb58002...): /EFI/MicaNT/BOOTX64.EFI ({sz:,} bytes)")
    print(f"  Ubuntu ESP (PARTUUID 63426ac5...): /EFI/MicaNT/BOOTX64.EFI mirror")
    print(f"  Default BootOrder: {target_boot_order}")
    print(f"=======================================================")

    ssh.close()

if __name__ == "__main__":
    main()
