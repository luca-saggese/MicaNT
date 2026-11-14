# MicaNT — OCI-Compatible Native Container Runtime Specification

**Status:** Proposed; Ring 3 implementation specification  
**Primary owner:** Native user-mode container runtime and service  
**Kernel dependencies:** Process Silos, Resource Groups, Network Namespaces, Container Security, UnaryFS  
**References:** `KERNEL_PROCESS_SILO_SPEC.md`, `KERNEL_RESOURCE_GROUPS_SPEC.md`, `KERNEL_NETWORK_NAMESPACE_SPEC.md`, `KERNEL_CONTAINER_SECURITY_SPEC.md`

## 1. Mission

Build a **Ring 3 native container runtime** for MicaNT that launches NT PE applications in a constrained silo with resource controls, virtual filesystem layers and optional network namespaces. OCI interoperability is a user-mode adapter, not a new kernel subsystem. Docker Engine compatibility is a separate possible integration target and must not be claimed from support for a subset of OCI image layouts.

The first supported workload is a native AMD64 PE application on the MicaNT kernel. Linux OCI images containing ELF/Linux executables are **not executable merely because their filesystem layers can be read**: they require a Linux ABI environment or a Linux guest VM. Windows container image compatibility likewise depends on complete ABI/runtime and platform version contracts.

## 2. Runtime separation

```text
CLI / Developer Tools (Ring 3)
    -> MicaNT Container Service (Ring 3)
        -> OCI image/config adapter and content store (Ring 3)
        -> UnaryFS layer composition (through canonical Io boundary)
        -> Silo + Resource Group + Network Namespace policy (kernel handles)
            -> Canonical Ps/Ob/Se/Ke/Mm/Io/NDIS/TCPIP kernel
```

The runtime may use a private native manifest initially. A future OCI adapter reads the OCI Image Specification and Runtime Specification using a pinned version and a conformance matrix. Do not present private manifest fields as official OCI extensions without explicit namespacing and compatibility documentation.

## 3. Image and filesystem contract

- Validate image configuration, layer digests, content length, decompression bounds and extraction paths before use.
- Store content-addressed archives in a **Ring 3 content store**; never parse untrusted archive compression in Ring 0.
- Convert or materialize layers into UnaryFS-compatible sparse layers through a trusted user-mode importer while preserving supported metadata, whiteouts and delete semantics.
- All lower image layers are sealed/read-only. Every running container gets one isolated writable layer, or an explicitly configured ephemeral one.
- Use an explicit base system image/ABI compatibility ID; refuse to attach a Windows/NT image to an incompatible MicaNT base without validated compatibility.
- Volume/bind mounts are explicit paths with read-only or read-write policies; host immutable system areas cannot be mounted writable.
- Layer mount/unmount, app execution, image removal and garbage collection must respect in-use handle and mapped-section references.
- Committing runtime writes into a new image is a separate privileged operation; it must not automatically include secrets, device nodes or host binds.

## 4. Workload manifest

The first native manifest includes: version; image content ID; executable entry path and arguments; working directory; environment; permitted runtime DLLs; root UnaryFS view; mount points; user/token context; silo limits; network mode; approved IPC; logging sinks; lifecycle stop policy; expected platform/architecture. Validate lengths, offsets, path normalization and unsupported features before starting any child.

Container identity and image digest are distinct. A mutable tag never substitutes for cryptographic content validation. Use local trust policy for signed images; an unsigned local-development image may be permitted only under an explicitly restricted policy.

## 5. Lifecycle and state machine

**States:** `Created -> Prepared -> Running -> Stopping -> Stopped -> Deleted`. `Failed` is an explicit recoverable state for incomplete preparation/cleanup with preserved error evidence.

1. **Create:** validate image/config, authorization, quotas and the platform profile; allocate a runtime record without launching processes.
2. **Prepare:** materialize/mount a verified UnaryFS view, create silo/resource/network contexts, bind narrow policy and create stdio/console channels. Nothing becomes runnable before all dependencies are published coherently.
3. **Start:** create native process under silo, inheriting namespace and limits before CPL3 entry. Report actual process handle/ID only after success.
4. **Run:** manage signals/stop equivalents, stdout/stderr, exit status, child processes, events and lifecycle notifications. No fake service readiness.
5. **Stop:** request graceful exit, allow bounded grace period, then invoke the supported canonical termination path; retain backing references until process/rundown finishes.
6. **Delete:** release endpoint/handle references, writable ephemeral layers, virtual links and quotas; refuse unsafe deletion of referenced images.

A manager crash must not silently free active kernel resources. Restart recovery must reconstruct ownership from durable records or safely reap abandoned workloads.

## 6. Network modes

- `none`: namespace with no external uplink; loopback only when implemented.
- `private`: isolated namespace with virtual link to host bridge/router and explicit firewall policy.
- `host`: privileged explicit mode, disallowed for untrusted workloads by default.

Port publishing and host network access must be explicit. Default inbound denial must remain effective regardless of container runtime configuration bugs. Docker-compatible networking CLI behavior is a later user-mode translation, not an implicit kernel feature.

## 7. Security model

The user-mode runtime is not the security boundary. It requests kernel-enforced silo membership, quotas, UnaryFS confinement, object access and networking rules. Host manager authentication uses normal MicaNT security and narrowly scoped object handles; untrusted image contents cannot select host privileges. Container Administrator is not host Administrator. Reject kernel driver installation, raw devices, host kernel tampering and any unknown capabilities absent a verified grant path.

Protect image metadata, cached layer blobs, private keys and run secrets using canonical ACLs. Do not include secrets in an immutable image or export them by default in a committed layer. Maintain local diagnostic logs without network telemetry.

## 8. CLI and interfaces

A minimal CLI may expose conceptual operations `image import`, `create`, `start`, `list`, `inspect`, `stop`, `delete`, `logs`, and `run`. Exact command names are a Ring 3 UX choice. Use one versioned service protocol with structured errors, not unvalidated strings sent into kernel control paths. Preserve exit codes and status distinctions.

OCI image pull, registry authentication, Docker API emulation and compose orchestration are future adapters. They should reuse the same image store and kernel primitives rather than implement independent container managers.

## 9. Incremental milestones

- **OCI0 — Native process isolation:** runtime creates silo with one PE, waits and reaps it; no image pull.
- **OCI1 — Layered root:** mount a prepared UnaryFS image read-only plus writable delta; verify shadowing and base immutability.
- **OCI2 — Limits and stdio:** process/thread/commit quotas and interactive/noninteractive console channels; reliable exit propagation.
- **OCI3 — Networking:** namespace, virtual link, loopback, controlled published port and actual TCP/UDP test.
- **OCI4 — OCI import adapter:** validate OCI manifest/config and layered blob content, explicitly reject unsupported platform/metadata, run a compatible native PE image.
- **OCI5 — Operations:** restart recovery, repeated create/delete, image GC, quotas and concurrency; controlled upgrades.
- **OCI6 — Optional Docker compatibility:** CLI/API adapter and conformance evidence for an explicitly named subset only.

## 10. QEMU acceptance

1. Launch an original native PE inside a silo with a UnaryFS overlay; file writes land in the writable layer while base bytes remain identical.
2. Two containers operate independently, cannot access each other's process objects, files or sockets, and can bind identical local ports.
3. Enforce process/thread and memory quotas while recovering charges correctly after termination and failed startup.
4. Execute a real network payload through canonical M10 and observe packet capture; verify firewall denial for unauthorized inbound.
5. Kill a process while a file mapping, socket and pending I/O are active; no kernel memory/handle leak or use-after-free occurs.
6. Import a well-formed, explicitly compatible OCI image; reject wrong architecture, unsupported OS, malicious traversal, malformed layer and digest mismatch.
7. Simulate crash during preparation and image/layer commit; restart and recover a consistent runtime state.
8. Demonstrate repeated launch/delete with stable PFN/handle accounting; preserve ordinary QEMU boot and network regressions.

Acceptance of OCI image parsing alone does **not** prove runtime conformance or Docker compatibility. Native container readiness requires real Ring 3 execution and isolation proofs.

## 11. Hardware acceptance

Repeat supported native workloads and image storage on a real AMD64 device with stable block I/O and NIC; capture driver reset, power-loss and network disconnect behavior separately. A successful QEMU run is not universal hardware support.

## 12. Out of scope and completion

Not in first release: Linux syscall compatibility, direct execution of Linux Docker images, full HCS parity, arbitrary Windows Server base images, rootless Linux runtime semantics, Kubernetes orchestration, OCI distribution registry or Docker swarm. Runtime completion for the supported profile requires correct lifecycle, isolation, storage, quotas, real network and recovery, each verified end-to-end. Keep historical test evidence separate from proposed implementation.
