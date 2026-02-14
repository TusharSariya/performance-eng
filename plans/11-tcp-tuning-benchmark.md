# Project 11: TCP Tuning Benchmark

## Goal

Build a client-server benchmarking tool that measures TCP throughput and latency under different kernel tuning parameters (`sysctl` settings, congestion control algorithms, buffer sizes), producing a systematic comparison.

## What You'll Learn

- TCP congestion control algorithms (cubic, bbr, reno) and their behavior
- TCP buffer tuning (`tcp_wmem`, `tcp_rmem`, `tcp_mem`)
- Socket options that affect performance (`TCP_NODELAY`, `SO_SNDBUF`, `TCP_CORK`)
- How to benchmark network performance rigorously
- The relationship between bandwidth, latency, and buffer sizes (BDP)

## Background

Linux TCP has dozens of tunable parameters that significantly affect performance. The defaults are reasonable for general use but suboptimal for specific scenarios:
- High-bandwidth, high-latency links (need larger buffers)
- Data center networks (low latency, need to avoid bufferbloat)
- Lossy networks (congestion control algorithm choice matters hugely)

BBR (Bottleneck Bandwidth and RTT) from Google changed the game when it was introduced — it can achieve 2-10x throughput over cubic on lossy links.

## Prerequisites

- C or Go (for client/server)
- Two Linux machines (or use `tc netem` for simulated network conditions)
- Root access (for sysctl changes)
- `tc` (traffic control) for network simulation

## Milestones

### Milestone 1: Benchmark Client and Server

**Build a TCP throughput and latency measurement tool.**

- Server: accepts connections, receives/sends data, measures throughput
- Client: connects, sends/receives data, measures:
  - Throughput (MB/s, Gbps)
  - Latency (for request-response workload)
  - Connection establishment time
  - Time to first byte
- Workload modes:
  - **Bulk transfer:** one-way stream (like `iperf3`)
  - **Request-response:** fixed-size request, fixed-size response (like HTTP)
  - **Mixed:** concurrent bulk + latency-sensitive streams
- Report percentile latencies and throughput over time

**Deliverable:** Client-server benchmark tool with multiple workload modes.

### Milestone 2: Network Condition Simulator

**Use `tc netem` to create reproducible network conditions.**

- Build wrapper scripts for common scenarios:
  - **Data center:** 0.1ms RTT, 10Gbps, no loss
  - **Same region:** 1ms RTT, 1Gbps, 0.01% loss
  - **Cross-continent:** 100ms RTT, 100Mbps, 0.1% loss
  - **Mobile/lossy:** 50ms RTT, 10Mbps, 1% loss, 20ms jitter
  - **Satellite:** 600ms RTT, 10Mbps, 0.5% loss
- Apply with:
  ```
  tc qdisc add dev eth0 root netem delay 100ms loss 0.1% rate 100mbit
  ```
- Verify conditions with ping (RTT) and iperf3 (bandwidth)

**Deliverable:** Network condition profiles that can be applied/removed with a single command.

### Milestone 3: Congestion Control Comparison

**Benchmark different TCP congestion control algorithms.**

- Algorithms to test:
  - **cubic** — Linux default; loss-based
  - **bbr** — Google's; model-based (bandwidth and RTT estimation)
  - **reno** — classic AIMD; baseline comparison
  - **bbr2** (if available on your kernel)
- Set per-socket or system-wide:
  ```
  sysctl net.ipv4.tcp_congestion_control=bbr
  ```
- For each algorithm × network condition:
  - Measure throughput over time (convergence speed)
  - Measure retransmissions
  - Measure RTT (does the algorithm cause bufferbloat?)
  - Multi-flow fairness (two connections sharing a bottleneck)

**Deliverable:** Comparison charts: throughput, retransmissions, RTT per algorithm per network condition.

### Milestone 4: Buffer Size Tuning

**Measure the impact of TCP buffer sizes.**

- Key sysctls:
  - `net.ipv4.tcp_rmem` — min, default, max receive buffer
  - `net.ipv4.tcp_wmem` — min, default, max send buffer
  - `net.core.rmem_max` — maximum socket receive buffer
  - `net.core.wmem_max` — maximum socket send buffer
- Concept: **Bandwidth-Delay Product (BDP)**
  - BDP = bandwidth × RTT
  - Buffer must be ≥ BDP to fully utilize the link
  - E.g., 1Gbps × 100ms = 12.5MB buffer needed
- Sweep buffer sizes from 64KB to 64MB for a high-BDP link
- Show: throughput plateaus when buffer ≥ BDP

**Deliverable:** Chart showing throughput vs buffer size for different BDP scenarios.

### Milestone 5: Socket Option Comparison

**Benchmark socket-level options that affect performance.**

- Options to test:
  - `TCP_NODELAY` — disable Nagle algorithm (critical for latency)
  - `TCP_CORK` — batch small writes into larger segments
  - `TCP_QUICKACK` — disable delayed ACKs
  - `SO_BUSY_POLL` — busy-poll for lower latency (avoids softirq wait)
  - `TCP_FASTOPEN` — save one RTT on connection establishment
  - `SO_ZEROCOPY` — avoid kernel-to-user copy for large transfers
- For each option, measure impact on:
  - Request-response latency
  - Bulk throughput
  - CPU utilization per byte transferred
- Test combinations (e.g., `TCP_NODELAY` + `TCP_QUICKACK`)

**Deliverable:** Option impact matrix with recommendations per workload type.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Bandwidth-Delay Product (BDP) | The fundamental limit on throughput for a given buffer size |
| Congestion window (cwnd) | How many bytes TCP can have in-flight; primary throughput control |
| Slow start vs congestion avoidance | Affects how quickly a new connection reaches full throughput |
| Bufferbloat | Excess buffering in the network causes latency; BBR helps |
| Nagle + delayed ACK | The classic interaction that causes 40ms+ latency for small writes |
| TCP pacing | Spreading packets evenly over time vs bursting |

## Validation & Benchmarks

- Cross-validate throughput against `iperf3` with same conditions
- Use `ss -ti` to inspect TCP state (cwnd, rtt, retransmissions) during tests
- Use `tcpdump` to verify packet patterns (Nagle coalescing, delayed ACKs)
- Verify `tc netem` conditions with `ping -c 100` (check mean and stddev)
- Use `nstat` to track TCP statistics (retransmissions, resets, etc.)

## References

- Brendan Gregg's "Systems Performance" Chapter 10 (Network)
- Google BBR paper: "BBR: Congestion-Based Congestion Control"
- Linux TCP tuning guide: `Documentation/networking/ip-sysctl.rst`
- Cloudflare blog: "Optimizing TCP for high WAN throughput while preserving low latency"
- `ss(8)` and `nstat(8)` man pages
