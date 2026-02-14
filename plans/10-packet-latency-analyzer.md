# Project 10: Build a Packet Latency Analyzer

## Goal

Build a tool that measures network latency at various points in the Linux kernel network stack, revealing where time is spent between a packet arriving at the NIC and reaching the application (and vice versa).

## What You'll Learn

- The Linux network receive/transmit path (NIC → driver → softirq → socket buffer → application)
- eBPF hooks at different network layers (XDP, TC, socket, kprobes)
- Kernel network stack overhead and where latency hides
- Software interrupt (softirq) processing and its scheduling
- Network buffer (sk_buff) lifecycle

## Background

When a packet arrives at a server, it passes through many layers:
1. NIC hardware → DMA to ring buffer
2. Driver interrupt → NAPI polling
3. softirq processing → protocol handling (IP, TCP)
4. Socket buffer → application `recv()`

Each layer adds latency. On a busy server, softirq processing alone can add milliseconds. Understanding where latency occurs in the stack is critical for optimizing network-intensive applications.

## Prerequisites

- C with libbpf
- Linux kernel 5.x+ with BTF and XDP support
- Root access
- Two machines (or use loopback with synthetic delay for initial testing)
- Basic understanding of TCP/IP

## Milestones

### Milestone 1: Socket-Level Latency with eBPF

**Measure the time between packet arrival and application recv().**

- Hook tracepoints/kprobes:
  - `net:netif_receive_skb` — packet enters the stack from the driver
  - `sock:inet_sock_set_state` or kprobe on `tcp_rcv_established` — TCP processing
  - kprobe on `tcp_recvmsg` — application reads data
- For each connection (identified by 4-tuple), measure:
  - Driver → TCP processing latency
  - TCP processing → application read latency
  - Total stack latency
- Output per-connection latency breakdown

**Deliverable:** Tool showing per-connection network stack latency breakdown.

### Milestone 2: softirq Latency Analysis

**Measure softirq scheduling and processing delays.**

- Hooks:
  - `irq:softirq_raise` — softirq scheduled (NET_RX)
  - `irq:softirq_entry` — softirq starts processing
  - `irq:softirq_exit` — softirq finishes
- Measure:
  - softirq scheduling delay (raise → entry): how long packets wait for CPU
  - softirq processing time (entry → exit): how many packets processed per batch
  - softirq frequency and burst sizes
- Track per-CPU softirq behavior

**Deliverable:** softirq latency analysis showing scheduling delays and processing times.

### Milestone 3: XDP Timestamping

**Add the earliest possible timestamp using XDP.**

- Write an XDP program that:
  - Timestamps packets at the XDP layer (earliest software hook)
  - Stores timestamps in a BPF map keyed by packet hash (5-tuple)
  - Passes packets up the stack (`XDP_PASS`)
- Compare XDP timestamp against:
  - `net:netif_receive_skb` timestamp
  - TCP receive timestamp
  - Application recv() timestamp
- This gives the most accurate measurement of in-kernel latency

**Deliverable:** End-to-end timestamping from XDP to application.

### Milestone 4: Transmit Path Analysis

**Measure latency in the send direction.**

- Hooks:
  - Application `sendmsg()` / `tcp_sendmsg` — application hands data to kernel
  - `tcp_write_xmit` — TCP sends segment
  - `net:net_dev_xmit` — packet handed to driver
  - `net:net_dev_queue` — packet queued at qdisc
- Measure:
  - Application → TCP segmentation latency
  - TCP → qdisc queuing latency
  - Qdisc → driver transmit latency
  - Queuing delays in traffic control (tc)
- Detect Nagle algorithm delays and TCP corking

**Deliverable:** Transmit path latency breakdown per connection.

### Milestone 5: Network Latency Dashboard

**Combine everything into a real-time monitoring tool.**

- Real-time display showing:
  - Per-connection stack latency (RX and TX)
  - softirq scheduling delays per CPU
  - Top connections by stack latency
  - Latency histograms (overall and per-layer)
  - Anomaly detection: flag connections with unusual stack latency
- Output formats:
  - Terminal UI (ncurses)
  - Prometheus metrics
  - JSON for Grafana dashboards

**Deliverable:** Real-time network stack latency dashboard.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| NAPI (New API) polling | Switches from interrupt to polling under load; affects batch sizes |
| softirq budget | `net.core.netdev_budget` limits packets per softirq cycle |
| GRO (Generic Receive Offload) | Coalesces packets; affects what you see at higher layers |
| Busy polling (`SO_BUSY_POLL`) | Bypasses softirq for latency-sensitive apps |
| RSS (Receive Side Scaling) | Distributes packets across CPUs; affects per-CPU softirq load |
| sk_buff lifecycle | The fundamental network buffer; follows the packet through the stack |

## Validation & Benchmarks

- Use `ping` for baseline RTT; your tool should show the kernel's portion of that RTT
- Generate traffic with `iperf3` or `wrk` at known rates
- Compare against `ss -ti` (per-connection TCP info)
- Verify with `perf sched` that softirq delays match your measurements
- Test under load: increasing traffic should increase softirq scheduling delays

## References

- Brendan Gregg's "Systems Performance" Chapter 10 (Network)
- "BPF Performance Tools" Chapter 10 (Networking)
- Linux kernel networking documentation: `Documentation/networking/`
- "Understanding Linux Network Internals" — Christian Benvenuti
- Cloudflare blog posts on kernel network tuning
- XDP tutorial: https://github.com/xdp-project/xdp-tutorial
