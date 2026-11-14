# MicaNT Local Users, Authentication and Interactive Sessions

**Status:** Implementation plan grounded in the current source tree; no phase is considered accepted until its tests and canonical runtime path pass. **Architecture:** AMD64, C++23, modular kernel, UEFI boot. **Scope:** Local accounts and one interactive graphical session first; account identity and isolation must support multiple users.

## 1. Objective

Complete the canonical chain `local account → SAM → LSASS → kernel-authorized token → logon session → user profile → Winlogon/LogonUI → Explorer` with correct access checks, failure rollback, cleanup, lock/unlock, logout and cross-user isolation. User-mode components communicate through shared ABI declarations and established syscalls/IPC; they do not include private `micant/km` headers or instantiate Executive objects. Kernel security authority remains in Se/Ob/Ps.

## 2. Verified current state

This is a source inspection, not runtime evidence. `SamDatabase` in `include/micant/um/subsystems/security/sam.hpp` is an in-memory singleton with hard-coded accounts/passwords, MD4 NT hashes, no persistent serialization, and basic create/delete/lock/group methods. Account mutations and failed-login counters are not durable. The declared account implementation includes a fixed domain SID and increments RIDs in memory. The same header includes the kernel `se.hpp`, so it is not a valid Ring 3 boundary.

`include/micant/um/servers/lsass.hpp` performs in-process password comparison by consulting that singleton and constructs `se::TokenObject` objects in user-mode C++ code. Its challenge helper is not a standard password KDF and must not be treated as a credential storage design. `tools/lsass_app.cpp` is a banner-only Ring 3 executable; it reports mounted SAM, Credential Guard and token state without performing those operations. `tools/winlogon_app.cpp` similarly prints an asserted logged-on state and exits. `tools/logonui_app.cpp` draws a static mock screen and exits; it does not collect input or authenticate.

`include/micant/um/subsystems/mgmt/userenv.hpp` returned the constant `C:\Users\admin`; it neither used a token nor created/loaded profiles. That fallback has been removed; lookup now fails explicitly until a token-to-profile owner exists. The SE implementation has hosted token classes and a separate bounded freestanding token/access-check path in `kernel/se/freestanding.cpp`. A kernel-side interactive-token issuance primitive now validates a SYSTEM issuer and bounded user/group claims and assigns AuthenticationId/SessionId; Ps wraps issuer selection from the current process. This primitive is not yet connected to validated LSASS claims, assignment to the user process, or a user-mode launch route. Ps has token ownership hooks and native process/image code, but authenticated-user process startup and full lifecycle remain open. Cm, Io, filesystem, Win32k and desktop services exist, but this inspection found no demonstrated persisted SAM, authenticated IPC contract, secure desktop, interactive lifecycle, or per-user profile path. Existing M4/M6/M7/M8 and filesystem milestone limits remain dependencies, not proof of logon support.

The inspected source references are `include/micant/km/executive/se.hpp`, `kernel/se/freestanding.cpp`, `include/micant/um/subsystems/security/sam.hpp`, `include/micant/um/servers/{lsass,winlogon}.hpp`, `include/micant/um/subsystems/mgmt/userenv.hpp`, `tools/{lsass,winlogon,logonui}_app.cpp`, `kernel/ps/address_space.cpp`, `kernel/cm`, `kernel/io`, `kernel/win32k`, and `docs/KERNEL_MILESTONES.md`. These findings are bounded to inspected files; declarations elsewhere are not runtime evidence. The three Ring 3 app entrypoints were changed during this work to fail with `STATUS_NOT_SUPPORTED` and state their missing dependencies instead of printing invented active-session/authentication claims; this is explicit failure behavior, not a working service.

## 3. Gap analysis

The end-to-end path is absent. Priority gaps are: (1) a user-mode-safe SAM service with persistent, recoverable records and a vetted password KDF; (2) authenticated, access-checked LSASS IPC; (3) kernel issuance of tokens from trusted authenticated claims, with session IDs and token rights; (4) Ps process creation inheriting/assigning only authorized primary tokens; (5) persistent profiles, registry hives, and ACL-backed storage; (6) input-capable LogonUI on a kernel-enforced secure desktop; (7) Winlogon state/lifecycle and a real Explorer launch; (8) logout, lock/unlock, rundown and isolation regressions. No status banner or hosted test closes these gaps.

## 4. Ring 0 / Ring 3 architecture

Ring 3 owns LSASS policy/package code, SAM record formatting/service interface, Winlogon state machine, LogonUI rendering/input client, Userenv and Explorer. Ring 0 owns object/handle access checks, syscall capture, token object creation/duplication, process token assignment, session authority checks and teardown enforcement. ABI structures and status values used across the boundary belong in `micant/shared`. IPC must bind the server identity to a kernel-protected process object/registration capability; a caller-supplied name, SID or privilege list is not authority. Secrets cross only bounded captured buffers, are wiped after use where possible, and never enter logs.

## 5. Component ownership

| Component | Canonical owner | Responsibility |
|---|---|---|
| Account DB / policy | Ring 3 SAM service; Cm + Io/FsRtl persistence | Account/group records, atomic updates, lockout and password policy |
| Authentication | Ring 3 LSASS; shared IPC ABI | Verify local credentials, issue trusted logon request, package policy |
| Tokens / access | `kernel/se`, `kernel/ob` | Token object, authority checks, access decisions and lifetime |
| Process/session | `kernel/ps`, `kernel/ex`, `kernel/ob` | Session association, token assignment/inheritance and process rundown |
| Profile / environment | Userenv, Cm and filesystem | SID-derived directories, hive lifecycle and environment block |
| Interactive lifecycle | Winlogon / LogonUI Ring 3; Win32k/window manager | Secure input, state transitions, shell start, lock and logout |
| Boot service start | canonical init/SM/SCM path | Start protected services in dependency order; no hardcoded success claims |

## 6. Account model

An account has an immutable machine-domain SID plus unique RID, normalized account name, display metadata, primary group, memberships, account control flags, password verifier parameters, failed-attempt/lockout state and bounded timestamps. RID allocation is monotonic and persisted in the same transaction as account creation; deletion never recycles a RID. Protect built-in/service identities from unsafe deletion. Disabled and locked accounts fail authentication with documented NT status mapping. Groups/aliases have stable SIDs and membership updates validate both records. Admin status derives from protected group membership and policy, never a client boolean.

## 7. SID and group model

Use validated variable-length SIDs in neutral shared ABI form and the canonical SE SID implementation internally. Validate revision, authority, subauthority count and total length on every decode. Built-in aliases use documented SIDs. Token groups distinguish enabled, deny-only, mandatory and owner attributes. The caller cannot submit arbitrary group membership as a logon claim. Session/logon SID construction must use a kernel-controlled unique identifier and cannot collide with account SIDs.

## 8. Persistent SAM database

Use the existing Cm/Registry and canonical filesystem interfaces; do not create a competing Registry Manager. Choose a protected SAM hive/key schema only after confirming current Cm hive persistence and transaction/recovery guarantees. Store a versioned schema, machine SID, next RID, accounts, groups, policy and verifier metadata. ACL the backing object to SYSTEM/authorized SAM service and audit access locally without secrets. Writes must be all-or-nothing: use existing registry transaction/log support if it meets crash guarantees; otherwise use a bounded two-slot generation/checksum journal with flush ordering supported by Io/filesystem. On boot validate versions, lengths, checksums and references, select the newest complete generation, and fail closed on unrecoverable corruption. Do not claim power-loss durability if the block/filesystem path cannot flush it.

## 9. Passwords and authentication

Use a reviewed, maintained cryptographic implementation already present in MicaNT or a vetted freestanding-compatible library with explicit provenance; the current tree inspection has not established a suitable KDF or entropy provider. Do not invent a KDF. Prefer Argon2id when the target and memory limits support validated parameters; otherwise use PBKDF2-HMAC-SHA-256 with a versioned policy and constant-time comparison. Generate per-account salts from a kernel-backed CSPRNG and fail closed if entropy is unavailable. Clear transient password, derived key and serialized secret buffers. No plaintext or derived secret in logs, command line, persistent recovery journal, or diagnostic dumps. NTLM compatibility is separate and disabled unless its exact contract and necessity are implemented.

## 10. LSASS

LSASS is a real Ring 3 service with a protected process identity established by kernel/service startup, a bounded authenticated IPC endpoint, request sequence/lifetime, cancellation and timeout handling. It validates caller access and logon type, queries SAM only through an authorized interface, updates lockout state atomically, and requests a kernel token using an opaque authenticated request context. Kernel verifies the LSASS server capability, account enabled state/policy binding and claims; never trusts SID/groups/privileges sent by Winlogon. Return only status and an authorized session/token handle, never verifier material. On LSASS crash, reject new logons, revoke in-flight request contexts and let session manager recover/terminate only affected incomplete sessions.

## 11. Security tokens

Se owns primary/impersonation token objects and security checks. A token contains user SID, validated group SIDs/attributes, privileges/attributes, default DACL, integrity level, authentication ID, session ID and type. Creation is kernel-only behind a registered trusted authority and one-use authenticated logon context. Define supported rights, duplicate/assign checks and status behavior from NT contracts. Token handles are Ob-owned, rights-limited and reference-counted; process assignment pins the token until process rundown. User mode cannot mutate identity or elevate privileges. Unsupported token information classes fail explicitly.

## 12. Privileges and impersonation

Assign privileges from protected local policy and group membership. `AdjustTokenPrivileges`-equivalent operations may only enable/disable privileges already present and must preserve attributes and `STATUS_NOT_ALL_ASSIGNED` semantics where exposed. Impersonation requires a valid handle with required rights, acceptable impersonation level and caller authority; reverting restores the prior effective token. Primary-token assignment requires the documented rights/privileges and target-process access. Do not infer privilege from account name or UI selection.

## 13. Session Manager

Kernel Ps/Executive session records have unique nonzero logon AuthenticationId, session ID, referenced user/token/process objects, logon type, state and rundown ownership. Interactive is first supported mode. Service logon is limited to trusted boot services needed by the initial system; Network and Batch fail explicitly until implemented. State changes are serialized and validated; object references and IPC requests are cancelled on termination. Session ID is not a PID and cannot be selected by an untrusted process.

## 14. Winlogon

Winlogon runs as a protected Ring 3 service, coordinates secure desktop, LSASS, Userenv, Ps process creation and shell lifetime. State transitions are `LoggedOff → Authenticating → LoggedOn → Locked → LoggedOn`, and logout/critical failures return to `LoggedOff`. Each transition has an owner, rollback and timeout. It does not call kernel Executive C++ APIs directly or claim session success before token/profile/process creation succeeds.

## 15. LogonUI

LogonUI receives input only from the secure input path, displays account selection and password-entry/error UI, and submits credentials through Winlogon to LSASS. Credential buffers are bounded, masked, transient and wiped; no default credential or pre-authenticated button path exists. UI actions cannot create/assign tokens. Authentication errors do not disclose account existence beyond the chosen policy. Rendering and keyboard/mouse input must be actual Win32k/compositor events, not a painted static buffer followed by exit.

## 16. Secure desktop

A desktop name is insufficient isolation. Window station/desktop objects need Ob security and a kernel/window-manager enforced input-routing boundary. Ordinary processes cannot enumerate/read credential surfaces, inject input, capture secure desktop pixels or switch input focus during authentication. SAS handling needs a trusted input path and denial tests from an ordinary session. If current Win32k cannot enforce these properties, do not claim a secure desktop or expose password entry as secure.

## 17. User profiles

Profile root derives from immutable SID, not mutable display name. Creation uses staged directory/hive creation with ACLs before publication; failure rolls back partial paths. Required initial folders are Desktop, Documents, Downloads, AppData and Temp. Profile loading pins the SID-specific hive and profile record to the session; unload occurs only after process/handle rundown. Recovery detects incomplete creation, validates ACL/hive state, and either safely completes or quarantines the incomplete profile. Filesystem ACL support is a hard dependency for private-profile claims.

## 18. Per-user registry

Cm owns hive mounting/unmounting and key handles. Load a SID-specific hive under the user namespace only after token authorization; prevent cross-user hive traversal absent rights. Flush on orderly unload and report flush failure. Do not publish profile loaded when the hive is absent, corrupt or incompletely mounted. If current Cm only supports bootstrap/configuration state and not durable per-user hives, implement the needed Cm path before claiming profile persistence.

## 19. Environment block

Userenv builds a bounded, correctly terminated environment from machine defaults plus authenticated profile values, with user overrides applied after validation. Set `USERPROFILE`, `HOMEDRIVE`, `HOMEPATH`, `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP` and `PATH` from actual mounted paths. Capture arguments/environment into the child process without retaining mutable caller pointers. Environment values cannot set token identity or security policy.

## 20. Explorer startup

Winlogon starts the real canonical `explorer.exe` through Ps/PE loader with the user primary token, session ID, profile environment, current directory and correct desktop handle. Process creation pins all inputs, validates image access and rolls back process/thread/handles on failure. Verify the image reaches its entrypoint at CPL3 and performs profile-scoped file access. Desktop/taskbar must be presented by canonical Win32k/compositor services. A banner, diagnostic application, SYSTEM token launch or process-create log is not acceptance.

## 21. Lock and unlock

Lock freezes/obscures the user desktop and routes input to the secure desktop while keeping processes, token and profile references alive. Unlock authenticates against the same session/user and restores the existing desktop; it cannot substitute a newly selected identity. Failure leaves the session locked. Timeout, component crash and concurrent requests must not deadlock kernel locks or release live references.

## 22. Logout

Logout blocks new session work, terminates user processes by policy with bounded waits outside kernel critical sections, closes session-owned handles, drains IPC, unloads the user hive/profile and releases token/session references. Nonterminating processes trigger a defined force-termination policy and report incomplete cleanup; they cannot keep a new user attached to the old desktop. System services remain unaffected.

## 23. Multiuser isolation

Accounts have distinct immutable SIDs, tokens, logon IDs, profiles and hives. Test file, registry, named-object, process-handle and desktop access across users. One graphical session may be active initially, but switching must log off/lock according to policy and never reuse identity state. Knowledge of another user's path/PID is insufficient for access.

## 24. Services and service accounts

Bootstrap services use protected SYSTEM/service identities and kernel-created tokens. A service may not claim SYSTEM by passing a name or SID. Service account credentials and privileges are separate from interactive credentials; least privilege and service SID policy apply. Only the minimum boot services required for local interactive logon are in initial scope; unsupported service logon semantics fail explicitly.

## 25. Error handling

Map errors to NTSTATUS at native boundaries and Win32 errors only in Win32 wrappers. Distinguish invalid credentials, disabled/locked account, policy restriction, unavailable service, corrupt database, resource exhaustion, timeout and access denial internally. No success status before durable SAM update or complete logon commit. Use transactional rollback for account creation, profile publication, session creation and Explorer startup. Error messages avoid secret and unnecessary account disclosure.

## 26. Concurrency and ownership

Document lock order across SAM service, Cm, filesystem, Se, Ob and Ps. Never wait for user-mode service or process rundown while holding a spin lock/dispatcher lock. Use reference ownership for IPC requests, token, session, process, profile hive and handles. Account mutation and authentication serialize policy-relevant generation/state; token issuance binds to the exact authenticated account generation so disable/password change races cannot authorize stale credentials. Use rundown/cancellation for LSASS and Winlogon termination.

## 27. Security requirements

Fail closed on malformed IPC, invalid SIDs, stale generation, missing entropy, corrupted database, access-check errors and partial object creation. Rate-limit authentication without account enumeration; persist lockout according to policy. Protect SAM/verifier memory and backing ACL; do not put secrets in telemetry or logs. Enforce least privilege, secure input, handle rights, token lifetime, W^X, user-pointer capture and cross-user access denial. Preserve zero telemetry.

## 28. Recovery

At boot, validate SAM journal/hive and recover only a complete committed generation. Reconcile orphan profile staging directories and stale session records without fabricating a login. If LSASS crashes, reject authentication until a trusted restart and recover incomplete session objects. If Winlogon/Explorer exits, either restart the shell with the same still-valid token/profile under policy or log off and clean up. If storage cannot provide ordered flush, record that durability limitation and do not claim crash-safe persistence.

## 29. NT/Win32 compatibility

Use Microsoft Learn/WDK and win32metadata public contracts for names, layouts, access rights, status/error mappings, token semantics, profile APIs and logon APIs. Keep shared ABI neutral and user-mode includes free of `micant/km`. Implement only documented supported subsets; unsupported classes/logon types return explicit errors. Consult local `../reactos` narrowly for implementation detail after checking MicaNT ownership; retain clean-room provenance and notices.

## 30. Hosted tests

Use host tests only for deterministic SAM record codec, validation, RID non-reuse, KDF vector/constant-time wrapper, policy transitions, journal recovery model, SID parsing, token policy matrices, profile path derivation and injected I/O failures. Hosted adapters cannot claim kernel authorization, persistence on target media, Ring 3 isolation or end-to-end login.

## 31. Freestanding tests

Exercise canonical Se/Ob/Ps token creation, invalid/untrusted token request denial, token handle rights, access checks, process assignment/inheritance, session association, rundown and resource rollback in the bare-metal target. Preserve current bootstrap, scheduler, syscall, PE loader and two-user-thread regressions.

## 32. QEMU tests

Use UEFI and the actual `ntoskrnl_core_baremetal` path with a writable persistent disk image. Assert service startup identity, LogonUI input, wrong-password denial, valid account authentication, profile/hive creation, Explorer entrypoint and CPL3 token/session identity, private-file access denial, lock/unlock, logout, second login, reboot-persistent account/SID/password and crash-recovery. Collect serial/QMP and disk evidence; output markers alone are insufficient. Record exact image/source SHA and commands.

## 33. Physical hardware tests

After QEMU acceptance, test UEFI Secure Boot modes as supported, entropy availability, storage flush/error behavior and keyboard/mouse secure input on representative AMD64 systems. Verify suspend/reset/power interruption recovery and cross-user ACL behavior. Hardware not exercised must be reported as unverified.

## 34. Fault injection

Inject entropy failure; short/failed writes and flushes; torn SAM generations; bad checksums/schema; disk full; duplicate/corrupt RID; malformed SID/IPC lengths; LSASS/Winlogon crash at every state transition; timeout and cancellation; token allocation/handle insertion failure; process creation and PE entry failure; profile/hive ACL failure; stuck child process; and concurrent password change/disable during authentication. Assert fail-closed behavior and leak-free rollback.

## 35. Acceptance criteria

The local-login profile is complete only when durable SAM accounts and groups use salted modern verifiers; LSASS authenticates through authorized IPC; the kernel issues protected tokens; logon sessions have correct identity/lifetime; user profiles and registry hives persist with ACLs; LogonUI accepts real input on a kernel-enforced secure desktop; Winlogon controls login/lock/unlock/logout; Explorer reaches its real CPL3 entrypoint with the authenticated token/environment/session; a second account is isolated; reboot persistence and failures recover; regressions remain green; and the full path is executed in QEMU. Report implementation, link, QEMU execution, physical tests and limitations separately.

## 36. Phased implementation order

### L0 — Local accounts and persistent SAM

**Work:** replace hardcoded in-memory demo accounts and NT-hash storage with a versioned account/group/policy codec, vetted KDF/entropy source, stable machine SID/RID allocation, persistent Cm/filesystem backing, atomic update/recovery, administrative account operations and ACLs. **Owners:** Ring 3 SAM service, shared SAM ABI, Cm, Io/FsRtl and cryptographic owner. **Dependencies:** verified KDF, entropy, durable storage/ACL support. **Semantics/security:** never persist plaintext; no RID reuse; authorize mutations; lockout and password policy; SYSTEM/built-ins protected. **Errors/cleanup:** corrupt data fails closed; interrupted transaction recovers old or new generation; wipe transient secrets; no partial indices. **Tests:** codec bounds/fuzzing, KDF vectors, account/group CRUD, power-fault journal, persistence across reboot and unauthorized access. **Done:** actual booted SAM records survive QEMU reboot with stable SID/RID and reject tampering/unauthorized reads.

### L1 — LSASS authentication

**Work:** real service startup, authenticated IPC, SAM authorization, password verification, policy/lockout, request cancellation, generation binding and kernel token request. **Owners:** LSASS Ring 3, shared IPC ABI, Ob/Se service identity. **Dependencies:** L0, service identity/IPC and CSPRNG. **Semantics/security:** supported Interactive only initially; no client-selected claims; constant-time verifier; no secret logs. **Errors/cleanup:** timeout/crash cancellation, lockout persistence, wipe buffers. **Tests:** valid/invalid/disabled/locked, unauthorized client, malformed request, LSASS crash and replay. **Done:** QEMU request from Winlogon authenticates via LSASS and only returns a kernel-authorized opaque result.

### L2 — NT security tokens

**Work:** complete kernel token object fields, authority-gated creation, duplicate/impersonation/access rights, privilege adjustment, default DACL, integrity/authentication/session IDs, process assignment and inheritance. **Owners:** Se/Ob/Ps. **Dependencies:** L1 trusted one-use authentication context and M4/M6 syscall/object/process lifecycle. **Semantics/security:** documented NT access and status behavior; no user token minting/elevation. **Errors/cleanup:** reference-safe rollback and revoke handles on process/session rundown. **Tests:** primary/impersonation token, privilege escalation, DACL allow/deny, invalid handle, cross-user and lifetime. **Done:** actual Ring 3 process token identity and denial cases verified on canonical QEMU kernel.

### L3 — Logon sessions

**Work:** kernel session record, unique AuthenticationId/session ID, supported logon types, state, references and rundown. **Owners:** Ps/Executive/Ob. **Dependencies:** L2, synchronization/wait and process teardown. **Semantics/security:** Interactive supported; service only for minimum boot set; other types explicit failure. **Errors/cleanup:** cancellation on owner death, no phantom records, bounded rundown. **Tests:** create, duplicate request, terminate, crash, stale handle and cleanup. **Done:** login session owns exactly its token/process resources through logout in QEMU.

### L4 — Profiles and Userenv

**Work:** SID-derived directories, ACLs, initial folders, staged profile transaction, user hive mount/flush/unload, environment block and recovery. **Owners:** Userenv Ring 3, Cm, Io/filesystem. **Dependencies:** L2/L3, durable filesystem and per-user ACL/hive support. **Semantics/security:** token-authorized profile lookup; separate users; no hardcoded admin path. **Errors/cleanup:** partial creation rollback/quarantine; hive unload only after references drain. **Tests:** create/load/unload/reboot, ACL denial, bad hive, disk-full and env values. **Done:** two users get distinct persistent profiles/hives and cannot access each other's private data.

### L5 — Winlogon and LogonUI

**Work:** service state machine, actual credential provider/input, secure desktop/window-station enforcement, SAS/input routing, LSASS client and recovery. **Owners:** Winlogon/LogonUI Ring 3, Ob/Win32k/input stack. **Dependencies:** L1/L3, functional input and compositor/security boundary. **Semantics/security:** no static authenticated state; secure desktop is enforced, not named. **Errors/cleanup:** authentication retry, service crash, lockout and transition rollback. **Tests:** keyboard/mouse input, wrong password, injected ordinary-session input/capture, lock and recovery. **Done:** QEMU displays real login and submits credentials through LSASS over protected path.

### L6 — Explorer with authenticated token

**Work:** token/profile/environment/session-bound canonical PE process creation; real Explorer entrypoint and desktop presentation. **Owners:** Winlogon, Ps/PE loader, Mm/Io/Ob, Win32k/compositor. **Dependencies:** L2–L5 plus M4/M6/M7 and working desktop APIs. **Semantics/security:** least-privilege user token; captured creation parameters; shell desktop assigned to same session. **Errors/cleanup:** unwind handles/process/profile on any failure; defined shell restart policy. **Tests:** entrypoint/CPL3 identity, profile file access, cross-user denial, terminate/restart and leak checks. **Done:** real Explorer runs interactively with verified token and profile in QEMU.

### L7 — Lock/unlock and logout

**Work:** secure desktop switch, same-user reauthentication, session-preserving lock, ordered logout and process/profile/token rundown. **Owners:** Winlogon, Ps/Ob/Se, Win32k, Userenv. **Dependencies:** L3–L6. **Semantics/security:** lock retains session; logout revokes user access but preserves system services. **Errors/cleanup:** hung child timeout/escalation, service crash, concurrent requests. **Tests:** lock/unlock/wrong password, logout, forced termination, second login and resource counters. **Done:** all state transitions recover and release session-owned resources in QEMU.

### L8 — Multiuser and isolation

**Work:** multiple local records, distinct tokens/profiles/hives, one active graphical session policy, cross-process/object/filesystem authorization. **Owners:** SAM, Se/Ob/Ps, Cm/filesystem, Winlogon. **Dependencies:** L0–L7. **Semantics/security:** SID is authority; PID/path knowledge grants nothing. **Errors/cleanup:** account disable/delete while logged on follows explicit policy; no stale profile access. **Tests:** two users, handle/file/registry/object/desktop cross-access denial, switch/logout and persistence. **Done:** full isolation regression passes on canonical QEMU path.

### L9 — Stabilization, security and end-to-end verification

**Work:** close races, fault-injection gaps, documentation/evidence ledger, reboot/crash persistence and hardware qualification. **Owners:** all above, with one canonical owner per change. **Dependencies:** L0–L8. **Semantics/security:** every unsupported path explicit; no synthetic success or telemetry. **Errors/cleanup:** fault matrix and recovery complete. **Tests:** full hosted/freestanding/QEMU sequence plus selected physical hardware. **Done:** all criteria in §35 pass with reproducible commands and source/image SHA; hardware claims separately bounded.

## Implementation notes and open dependencies

### Implemented in the current worktree (not yet verified)

- `include/micant/shared/crypto_primitives.hpp` now has a bounded PBKDF2-HMAC-SHA-256 derivation primitive with an iteration range of 100,000–2,000,000 and a 1 KiB output cap.
- `SamUser` now has a versioned 16-byte-salt/32-byte-verifier record. New account/password operations request salt from `std::random_device` in the hosted SAM adapter and local password verification uses the versioned PBKDF2 verifier with constant-time digest comparison.
- The built-in Administrator and Guest records no longer contain sample password hashes and start disabled; the `admin`/`mica` demonstration account was removed. Account enabling rejects an account without an explicitly provisioned password.
- SAM no longer stores an NT hash. LSASS challenge-response currently returns `STATUS_NOT_SUPPORTED`; this is explicit legacy-protocol disablement, not NTLM support.

These edits are only an L0 security improvement in the hosted SAM source. They do **not** establish a persistent account database, machine-stable SID, kernel-backed RNG syscall path, durable lockout/password changes, Ring 3 SAM/LSASS operation, or QEMU login. `std::random_device` is strictly for the isolated hosted adapter; the environment is ARM64 and cannot execute the AMD64 shared hardware entropy routine. L0 still must connect native salt generation to a kernel-authorized system RNG rather than assuming direct user-mode CPU instructions are the final CSPRNG contract. Shell subsystem initialization no longer initiates the sample `admin`/`mica` logon, and Userenv no longer returns a shared hard-coded admin profile path.

The freestanding SE path now has a kernel-only local interactive-token issuance primitive. It verifies that the issuer is a referenced primary SYSTEM token carrying SeCreateTokenPrivilege and SeTcbPrivilege, validates bounded SID/group/privilege claims, and allocates a boot-unique AuthenticationId with console SessionId 1. Ps derives the issuer from the current process rather than accepting a caller-selected authority. The Se authority/claim regression executed and passed in QEMU. A Ps token-assignment primitive and its regression are compiled and linked, but boot stops at the existing APIC timer gate before that later Ps regression runs. No authenticated LSASS claim reaches Se yet, no user-process token assignment/launch has been exercised, and no logon-session object manager consumes these IDs.

### Verified missing dependencies to implement next

`CmHive` can serialize/deserialize a REGF-like buffer, but `ConfigurationManager` is an in-memory tree, `exportToHive` currently emits only a root record, `mountHive` only places a deserialized object in an in-memory map, `TxRJournal` is an in-memory list without durable replay, and `CmFlushKey` returns success without writing storage. The canonical filesystem specification describes genuine persistent write/flush support as incomplete; several filesystem implementations are explicitly simulated or read-only. Therefore the required SAM record persistence/recovery cannot safely be wired through the currently inspected Cm flush interface. Implement durable hive export/mount and connect it to a real writable Io block/filesystem path (with actual flush ordering) before claiming account persistence. No transaction protocol is selected until that owner path can prove its guarantees.

The existing `sam.hpp` still uses hosted STL synchronization/container types and includes private kernel `se.hpp`; it is not a valid native Ring 3 SAM service. Refactor the shared SID/record ABI into `micant/shared` and move storage operations behind an authenticated service/native registry interface. The `tools/lsass_app.cpp`, `winlogon_app.cpp` and `logonui_app.cpp` entrypoints now fail with `STATUS_NOT_SUPPORTED` and explain the missing service path instead of claiming successful logon state. Existing unrelated worktree changes in boot/Executive/Ps must be preserved and integrated around.

The PBKDF2 known-answer test was added to the hosted SAM suite, and its translation unit is currently blocked from full compilation by pre-existing x86-only inline assembly/platform include errors when invoked with the host compiler. A focused standalone SAM smoke binary can run on this host and verifies the KDF vector, explicit Administrator provisioning, correct-password acceptance and wrong-password rejection; it is not kernel or persistence evidence. The canonical cross build initially exposed `ob::detail::resolveHandle` calls outside their freestanding guard; cleanup now uses the canonical kernel-handle close path in hosted builds and guarded resolution in freestanding builds. Its remaining compile error is an `fs::FileObject*` assignment to `io::FileObject*` in `kernel/io/iomgr.cpp`. Resolving this actual file-object bridge is required before reliable I/O persistence work; do not paper it over with an unchecked cast.

### Verification record

The focused standalone SAM smoke binary passed. A fresh CMake configure succeeded at `/private/tmp/micant-userlogon-build`; the canonical cross test-target build still fails before reaching this change on the Io file-object type mismatch. A direct host syntax check of the full test runner also fails on unrelated x86 inline assembly and platform socket headers. The LSASS, Winlogon and LogonUI app syntax checks pass. `git diff --check` is clean. The canonical bare-metal kernel and runtime dependencies linked, native user-mode boundary checks passed, and QEMU executed the Se logon-token authority/claim regression successfully. QEMU then failed at `[MicaNT Hal] Local APIC timer verification failed` (`Live timer dispatch failed=1`) before the later Ps token-assignment regression or user-logon flow; the full boot is not verified. No L0 persistence or end-to-end acceptance item is verified. Required next checks: resolve the Io file-object bridge, compile and run the repository suite with its supported x86 toolchain; implement and verify writable hive persistence/recovery before attempting reboot-persistence QEMU tests.
