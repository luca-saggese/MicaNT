# MicaNT — Network Namespace Specification

**Status:** Proposed  
**Primary owners:** canonical TCPIP/NDIS/AFD, Io/Ob, Ps/Se  
**Dependencies:** M10 network stack, `KERNEL_PROCESS_SILO_SPEC.md`, `KERNEL_CONTAINER_SECURITY_SPEC.md`  
**Reference:** `KERNEL_NETWORK_STACK_IMPLEMENTATION_SPEC.md`, `KERNEL_FIREWALL_BAREMETAL_INTEGRATION_SPEC.md`

## 1. Purpose and non-goals

Support isolated network views for groups of native NT processes without instantiating multiple TCP/IP engines. A network namespace owns a logical set of interfaces, IP addressing, routes, neighbor tables, transport port spaces, and allowed socket bindings. It is **not** a separate NIC driver, not a new firewall engine, and not a Linux networking ABI.

The initial target is two MicaNT Ring 3 process groups communicating over virtual interfaces in QEMU while remaining isolated from host network bindings. Existing M10 virtio-net, AFD, Winsock and transport logic are reused.

## 2. Isolation invariants

1. AFD endpoints are bound to exactly one namespace at creation; binding cannot be changed after publication.
2. IPv4/IPv6 addresses, local ports, routing, neighbor information and connected endpoint lookup are scoped to that namespace.
3. Two namespaces can listen on the same port without conflict if their interface and transport contexts differ.
4. Ingress packets are classified to a namespace before socket demultiplexing; no ambiguous or opportunistic fallback to the host namespace.
5. A socket cannot join another namespace through inherited handles or duplicated handles without an explicit authorized policy; first version can reject such transfers.
6. Teardown cancels sockets, timers and packet work and releases all namespace references without leaving RX/TX buffers accessible to freed owners.
7. All external network traffic remains governed by canonical WFP/firewall enforcement; namespacing does not automatically provide Internet access or security.

## 3. Object model

- **NetworkNamespace:** Ob-owned reference-counted configuration/context, unique opaque ID, access descriptor, state, interface membership and transport tables.
- **NetworkEndpointBinding:** non-null reference to namespace captured at AFD endpoint creation.
- **VirtualLink:** a bounded vNIC pair or bridge endpoint with frame ownership, queue bounds, backpressure and teardown.
- **Physical NIC:** controlled by canonical NDIS/HAL; attach to host/bridge according to policy, not directly exposed to untrusted namespaces.
- **Namespace state:** `Created -> Configured -> Active -> Stopping -> Dead`.

Host network is the privileged default namespace. First version supports one host namespace and flat isolated namespaces. Do not introduce nested hierarchies or physical NIC passthrough without explicit isolation guarantees.

## 4. Datapath

```text
Ring 3 PE -> ws2_32 -> AFD endpoint (NamespaceRef)
                          -> canonical TCPIP (per-namespace state)
                          -> virtual link / bridge / routed uplink
                          -> canonical NDIS -> NIC
```

Packet ownership is explicit at every boundary. Validate ingress source interface, namespace ID, packet lengths, checksums and buffer lifetime. Interrupt/DPC paths remain hardware-owned; enqueue to the appropriate namespace execution context with correct synchronization. Never call an arbitrary namespace user callback in ISR context.

### Functional requirements

- Isolated interface enumeration, address assignment and route tables.
- Port binding, listen/connect, UDP send/receive and endpoint lookup within namespace.
- Local loopback confined to the namespace; no host loopback leakage.
- Namespace-local DHCP/DNS configuration where the actual M10 services support it; no fake address leases.
- Virtual link to host with explicit bridge or router configuration.
- Optional externally accessible port mapping managed by authorized Ring 3 policy; no automatic open inbound ports.
- Link down, device reset, namespace stop and endpoint cancellation.
- IPv6 and NDP only when the corresponding M10 scope is verified; IPv4-only is permitted as an explicitly limited first milestone.

## 5. Security and firewall

Deny unprivileged configuration of physical NICs, cross-namespace routes, MAC spoofing, bridge membership and port publishing. The canonical firewall owns per-namespace enforcement hooks with default deny for unapproved inbound cross-boundary traffic. Prevent broadcast/multicast leaking unless policy explicitly permits it. NAT, packet filtering and rate shaping are optional capabilities with a single canonical implementation; do not add second packet parsers in the namespace layer.

Cross-namespace named sockets, device handles, inherited AFD handles and privileged IOCTLs require negative tests. Treat kernel namespace IDs as trusted object references rather than user-controlled packet labels.

## 6. Lifetimes and concurrency

Namespace references remain alive while any endpoint, route work item, packet buffer or in-flight timer needs them. Stop prohibits new endpoints, drains or cancels pending I/O, stops virtual links, then releases routing/neighbor/socket state. Never hold a namespace global lock across blocking socket calls, TCP retransmit callbacks or packet completion. Namespace deletion must be safe under simultaneous receive, accept, close and device reset.

## 7. Delivery stages

- **NNS0 — Context:** a namespace object, creation/close and access controls; no claim of isolation yet.
- **NNS1 — Local port and loopback scope:** two namespaces bind identical ports and exchange loopback packets only with own processes.
- **NNS2 — Virtual link:** paired vNIC/bridge with actual packet queues and bidirectional TCP/UDP; correct MTU and cleanup.
- **NNS3 — Host routing and firewall:** explicit egress through host routing; host firewall policy cannot be bypassed.
- **NNS4 — Configuration:** scoped DHCP/DNS, interface state, link notification and controlled port publishing.
- **NNS5 — Fault/SMP:** packet loss, malformed frames, reset, concurrent exit, multi-vCPU stress.

## 8. Tests and acceptance

**Isolation:** in QEMU two namespaces both listen on `0.0.0.0:8080` and accept independent clients; traffic cannot cross namespaces without an explicit virtual link.  
**Loopback:** each namespace has its own localhost binding; a host loopback service does not automatically appear inside it.  
**Route:** configured virtual link sends real UDP/TCP frames with capture evidence; false/canned packets do not count.  
**Access:** unprivileged process cannot claim physical interface, change routes, duplicate a forbidden socket handle, or publish a host port.  
**Lifecycle:** termination with outstanding `accept`, `recv`, retransmit timer or TX completion does not leak buffers, PFN, handles or leave callbacks to dead namespaces.  
**Faults:** malformed frames, dropped packets, link reset, abrupt vNIC removal and out-of-order completion do not cause memory corruption or deadlock.  
**Regression:** existing `qemu.py --network-test` path still works in host namespace.

## 9. Out of scope

OCI parsing, Docker overlay configuration, VPN, overlay networks across hosts, general IPv6 unless M10 provides it, and an independent NDIS miniport model. Container runtimes are Ring 3 clients of the namespace service.

## 10. Completion gate

Network namespaces are complete for the supported profile when independent socket state, real virtual-link I/O, controlled host connectivity, correct security checks and safe teardown are exercised by actual Ring 3 PE applications in the canonical QEMU kernel. Record unsupported variants explicitly.
