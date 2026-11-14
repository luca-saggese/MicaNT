# MicaNT — Native P2P and Kademlia Implementation Specification

**Document:** `docs/KERNEL_P2P_KADEMLIA_IMPLEMENTATION_SPEC.md`  
**Status:** Proposed implementation specification — **not** evidence of implemented or verified P2P  
**Target:** MicaNT modular NT-compatible AMD64 operating system, C++23  
**Owner:** User-mode P2P service and client libraries; canonical M10 networking and M11 firewall remain their respective owners  
**Dependencies:** M4/M6/M7/M8 process and user-mode readiness, M10 N0–N4 transport, M14 for durable local data; M11 for network-wide enforcement  
**Scope:** Peer identity, encrypted sessions, LAN/Internet discovery, Kademlia DHT, optional content routing and NAT traversal

## 1. Decision and acceptance boundary

Implement P2P as **Ring 3 software** using the existing MicaNT NT/Winsock and AFD/TCPIP stack. **Do not implement the Kademlia DHT, peer database, application-level packet parsing, or cryptographic session logic in Ring 0.** The kernel owns NICs, DMA, interrupts, TCP/UDP endpoints, access checks and resource isolation. A Ring 3 P2P service owns routing tables, DHT records, peer identity and protocol policy.

The initial deliverable is a **three-peer private-network demonstration** using real MicaNT Ring 3 PE processes and real guest network traffic. It must demonstrate secure peer authentication, bootstrap, iterative lookup, disconnect/expiry and correct cleanup. A hosted simulation, packet fixture or successful build alone is not acceptance.

The initial implementation **must not** be treated as a new milestone ID until the user explicitly updates `docs/KERNEL_MILESTONES.md`. This document proposes a separable workstream that can progress in parallel with M10. It does not claim M10, M11 or M12 completion.

## 2. Architectural boundaries

```text
Ring 3: apps, developer tools, file distribution, services
       |     P2P client API (bounded ABI / named IPC)
       v
Ring 3: micap2p service
       | PeerId, key store, authentication, sessions
       | discovery (LAN + bootstrap), Kademlia routing, records
       | encrypted transport, connection manager, policy, telemetry opt-out
       v
Ring 3: ws2_32.dll / cryptographic provider / system services
       | NT syscall / Io boundary
       v
Ring 0: AFD + Ob + Io + TCPIP + NDIS + hardware drivers
       | UDP/TCP sockets, timers, completions, rate limits, DMA
       v
Physical/virtual NIC (VirtIO-net in QEMU; real NIC later)
```

Rules:
- Preserve the canonical single MicaNT network stack. No parallel transport manager, kernel DHT, new public NT syscall or host-network shortcut.
- Keep `micant/km/` private headers out of Ring 3 images. Put any shared process/service ABI in mode-neutral interfaces.
- No hard-coded network identities, secret keys, IP addresses, fake data responses or unconditional success values.
- Reuse public protocol standards and appropriate license-compatible implementations. If choosing libp2p interoperability, implement its actual protocol details and validate against an independent peer. Do **not** call a custom XOR DHT “libp2p-compatible”.
- Treat historical `old/` monolithic kernel sources as archival only, per `AGENTS.md`.

## 3. Implementation strategy: interoperability decision

Do not immediately write an entire new stack. Make a bounded comparison, documenting selected versions/licenses and cross-compilation cost:

1. **libp2p interoperability track (preferred for ecosystem compatibility):** use a mature and actively maintained implementation compatible with the chosen toolchain/runtime, or implement the smallest standards-conformant subset. Peer IDs, secure channel, stream multiplexing, RPC and DHT record formats must match the chosen libp2p specifications.
2. **MicaNT-native limited track:** a deliberately private protocol for controlled LAN experiments, with a distinct protocol identifier and no claim of libp2p interoperability. Use only if a compliant port is infeasible at this stage; document migration cost and avoid locking a system ABI to the private wire format.

Choose one production protocol per role. Do not maintain two independent DHT routing tables or concurrent incompatible discovery stacks behind the same advertised identifier.

### Initial protocol profile

- Transport: **TCP and/or UDP** from real M10 sockets; specify per protocol. Do not assume QUIC or UDP hole punching exists.
- Security: mutually authenticated, encrypted sessions using a reviewed protocol/profile (libp2p Noise or TLS where applicable). Validate transcript binding, peer identity, replay protection and downgrade resistance.
- Discovery: explicit bootstrap peers; local-network discovery can be added independently through mDNS after multicast is verified.
- DHT: Kademlia XOR-distance lookup, bounded k-buckets, iterative queries, appropriate record expiry and resistance to unsolicited record poisoning.
- Persistence: opt-in durable identity/private-key storage and bounded cache using M14 storage only when secure persistent storage is supported.

## 4. M10 prerequisites and ownership

| Requirement | Required owner | Earliest P2P dependency |
|---|---|---|
| Real NIC RX/TX, DMA/interrupt, IPv4 routing | M10 N0–N1 | All networked P2P |
| UDP sockets, timeouts, DNS/multicast where implemented | M10 N2/N4 | Discovery, UDP-based transports |
| TCP connection, listen/accept, retransmission | M10 N3/N4 | Initial secure P2P sessions |
| Ring 3 Winsock endpoints, blocking/nonblocking, cancellation | M10 N4 | Native PE peer implementation |
| TLS/Noise primitives, entropy, wall clock validation | Ring 3 security/crypto providers | Authentication and encrypted transport |
| Persistent files and access controls | M14 + Se/Io | Durable identity, record store |
| Inbound firewall enforcement / conntrack | M11 | Exposing peers outside test LAN |
| Process lifecycle, threads, IPC, services | M4/M6/M7/M8 | Long-lived `micap2p` service |

P2P work may start with an offline algorithm/codec test harness **without representing it as running MicaNT networking**. Real runtime acceptance starts at a Ring 3 PE process calling real M10 sockets.

## 5. P2P identity, authentication and trust

- Generate peer identity from a cryptographically secure CSPRNG; fail closed without sufficient entropy.
- Preserve the private key in a protected, user-/service-owned store; do not commit or stage reusable private keys in repository images.
- Expose a public PeerId derived in the selected protocol's prescribed way. Bind the authenticated remote key to the observed PeerId; IP address is not identity.
- Authenticate every session before accepting application messages or privileged operations.
- Provide explicit allowlist/denylist and configurable authorization for privileged capabilities. A valid PeerId is **not** sufficient authorization for system-management or update actions.
- Support key rotation/revocation policy with documented effect on identity and cache entries.
- Never disable certificate/peer verification or permit plaintext fallback when an authenticated transport was negotiated.

## 6. Kademlia design and invariants

Implement protocol-specific details of the selected Kademlia profile; avoid mixing incompatible record formats.

- **Routing:** XOR distance; buckets bounded by `k`; deterministic closest-peer selection, LRU/probation behavior and stale-peer eviction; do not trust unverified address advertisements.
- **RPCs:** bounded message sizes and deadlines; at least peer lookup and liveness primitives required by the chosen protocol. Content/provider records may follow after basic routing is stable.
- **Queries:** iterative lookup bounded by parallelism parameter `alpha`, maximum depth, total requests, bytes and wall-clock duration; no recursive unbounded fan-out.
- **Records:** explicit namespace, version, signature/validation policy, expiry/TTL, maximum size and per-peer quotas. No executable payload or implicit authority from arbitrary DHT values.
- **Persistence:** optional, bounded on-disk cache with corruption recovery; successful lookup must not require a persisted cache.
- **Sybil/Eclipse resistance:** diversify peer selection where possible, maintain peer-quality observations, resist poisoning and amplification, cap requests from unauthenticated sources; state limitations rather than claiming full resistance.
- **No implicit public bootstrapping:** provide a deliberate configured list of trusted/known bootstrap addresses for the isolated test network, not random public DHT nodes.

## 7. Connection manager and network traversal

- Maintain bounded outgoing/incoming connection counts per peer, IP/network and globally.
- Enforce connect/handshake/read/write/idle timeouts; prevent slow-peer exhaustion and unbounded queues.
- Handle duplicate simultaneous dials deterministically without losing authenticated identity or leaking sockets.
- Track link/network changes and invalidate stale addresses; retry with exponential backoff and jitter.
- Implement connection teardown, cancellation, process termination and device removal without dangling handles, packet buffers or callbacks.
- **NAT traversal is a later phase:** begin with direct TCP in a private LAN. Add STUN/ICE-like address discovery, UDP hole punching and relay only after packet-level prerequisites exist, with explicit user consent/policy for externally reachable connectivity.
- **IPv6 is optional for first acceptance** and must be tested separately; never advertise it solely because IPv6 structures exist.

## 8. Ring 3 service, process API and lifecycle

Propose a user-mode service executable, e.g. `micap2p.exe`, and a minimal mode-neutral client API (exact filenames/ABI selected after inspection of existing service/IPC infrastructure).

Initial service operations:
- Start/stop and inspect status.
- Configure/list bootstrap peers and local listen addresses.
- Report authenticated local PeerId and currently connected peers.
- Find peer by PeerId with explicit deadline.
- Query limited DHT keys/records only when the record validation policy is implemented.

All operations must enforce caller identity, scoped access rights, quotas and cancellation. Avoid a global kernel peer namespace. Store no mandatory telemetry; allow local opt-in diagnostic counters and bounded logs that exclude secrets and sensitive message contents. Configuration and identity must be recoverable after clean restart; incomplete/corrupt state must not silently authorize peers.

## 9. Content discovery and developer tooling (optional follow-on)

After authenticated discovery works, add **content-addressed discovery**, not unrestricted file exchange:

- Content IDs derived from a chosen secure hash; verify full content against its advertised digest after transfer.
- Provider announcements are signed or otherwise validated under the selected protocol; they only locate content and do not authorize installation/execution.
- Actual file transfer requires authenticated transport, path confinement, quotas, permission checks, temporary files, atomic publication and rollback.
- For MicaNT system updates, accept only release artifacts signed by independent trusted update keys. The DHT is **never** a trust root or update-authority oracle.
- Explicitly separate P2P transport completion from M14 filesystem durability and recovery guarantees.

## 10. Security and robustness requirements

Mandatory controls:
- Bound parsing and arithmetic on all untrusted network fields before allocation or indexing.
- Per-peer and global limits for sessions, DHT queries, records, bandwidth and memory.
- Replay resistance, authenticated peer identity, anti-downgrade controls and verified key provenance.
- Strict decode failures and immediate cleanup on malformed frames, invalid signatures and mismatched PeerIds.
- No secrets in crash dumps, logs, test captures or public build artifacts.
- Fuzzable parsing interfaces, corpus seeds, malformed-record tests and packet-loss fault injection.
- Concurrency tests: simultaneous peer updates, cancellation, shutdown, network loss, repeated join/leave and service restart.
- Respect M11 firewall policy; no privileged automatic rule changes or implicit listener exposure.
- Keep P2P service unprivileged by default; OS management actions require a separately audited authorized service.

## 11. Incremental delivery phases and acceptance

### P0 — Protocol and dependency gate

**Implement:** exact protocol/version choice, licensing/provenance review, Ring 3 build prerequisites, real M10 socket compatibility inventory, test harness.  
**Accept:** reproducible builds and dependency matrix; no P2P networking claim yet.

### P1 — Identity and authenticated point-to-point session

**Implement:** peer identity, key persistence/ephemeral test keys, mutually authenticated secure session over existing TCP.  
**Accept:** two Ring 3 MicaNT peers exchange an authenticated payload over real QEMU NIC traffic; reject wrong PeerId and corrupted handshake; clean shutdown without leaks.

### P2 — Bootstrap and Kademlia lookups

**Implement:** bounded routing table and iterative peer lookup with deadlines, active peer verification, automatic stale-entry eviction.  
**Accept:** three independent MicaNT guests: A bootstraps via B, finds C by PeerId; kill/restart C and verify eviction/recovery. Packet capture matches the selected wire protocol.

### P3 — DHT record/provider routing

**Implement:** namespaced, validated, expiring records and/or provider announcements; quotas, invalid-record rejection, optional local cache.  
**Accept:** publish/discover/expire records across three nodes; malicious or oversize records fail without state corruption or unbounded traffic.

### P4 — Dynamic-network and resilience

**Implement:** backpressure, packet loss/reordering recovery, duplicate dials, network changes, bounded resource use, service restart and cleanup.  
**Accept:** QEMU fault-injection scenarios produce no crash/deadlock; explicit limits and resource recovery are verified.

### P5 — LAN discovery and NAT traversal

**Implement:** LAN multicast discovery and optional NAT/relay mechanisms, independently gated by M10/M11 readiness.  
**Accept:** peers discover one another using actual LAN packets, while spoofing and unauthorized inbound access are rejected. Public NAT traversal requires additional controlled-network tests.

### P6 — Secure content distribution (optional)

**Implement:** hash-verified file transfer and provider discovery, durable temporary storage and atomic publishing; signed release verification where updates are involved.  
**Accept:** transfer a known fixture, verify digest, interrupt transfer and recover without corruption; reject tampered/unauthorized update payloads.

## 12. Test environments and evidence

1. **Hosted unit tests:** XOR distance, bucket ordering, deterministic query traversal, record TTL, canonical serialization, malformed and fuzzed inputs. Hosted success is not bare-metal proof.
2. **Native Ring 3 PE tests:** services, library loading, actual Winsock calls, process teardown, service cancellation and access control.
3. **QEMU integration:** three separately booted MicaNT guests on an isolated network, unique peer keys, controlled bootstrap addresses, packet capture and verified bidirectional messages. Require actual NIC ingress/egress.
4. **Fault injection:** delay, packet loss, duplicates, reorder, invalid records, abrupt peer exit, link down/up, socket exhaustion and restart while queries are live.
5. **Bare-metal validation:** only after compatible real NIC, persistence and credentials are demonstrated. Record hardware, tests, limits and failures separately.

Track test commands, commit SHA, architecture, peer identities (public only), expected and actual outcomes, packet captures with secrets redacted, open leaks and failure cases in `docs/KERNEL_INTEGRATION_STATUS.md` once genuine integration evidence exists.

## 13. Worktree and coordination policy

- Use a separate Git worktree/branch and build directory from M6 and M10 agents; do not overwrite ongoing changes.
- Coordinate changes to shared `Ke`, `Io`, `Ob`, `Mm`, `Ps`, AFD, Winsock and boot staging with their owners.
- Target the earliest real end-to-end path; do not produce a catalog of empty P2P APIs.
- Execute builds/QEMU only when current user authorization permits; do not claim verification from source inspection.
- Avoid new kernel APIs unless an independently justified generic OS contract is missing and approved by its owner.
- Preserve repository rules on C++23, freestanding build, NT contracts, licensing, privacy and explicit unsupported-status behavior.

## 14. Definition of done

The first P2P release is **implemented and verified** only if P0–P4 pass on native Ring 3 MicaNT processes with actual QEMU network packets, peer authentication, DHT lookup, failure handling and complete resource cleanup. P5/P6 are optional extensions with separate acceptance. It is **not** complete merely because a Kademlia algorithm compiles, a server listens, or two host-side test programs connect.

**Non-goals for initial release:** Ring 0 DHT, default public Internet exposure, unaudited decentralized updates, kernel crypto handshakes, content piracy/sharing policy, anonymous overlay guarantees, QUIC/HTTP3, unrestricted remote-code execution and complete libp2p protocol coverage without corresponding interoperability tests.

## 15. References and related MicaNT specifications

- `AGENTS.md` — canonical ownership and execution rules.
- `docs/KERNEL_MILESTONES.md` — authoritative milestone definitions.
- `docs/KERNEL_NETWORK_STACK_IMPLEMENTATION_SPEC.md` — M10 N0–N6, canonical NDIS/TCPIP/AFD/Winsock and transport prerequisites.
- `docs/KERNEL_FIREWALL_BAREMETAL_INTEGRATION_SPEC.md` — M11 ingress/egress enforcement and stateful policy.
- `docs/KERNEL_FILESYSTEM_SPEC.md` — M14 persistence and file safety.
- Public protocol specifications: Kademlia (Maymounkov & Mazières), libp2p Kad-DHT, libp2p Peer IDs, Noise Framework / selected libp2p security transports; pin exact revisions in the implementation decision record before claiming protocol interoperability.
