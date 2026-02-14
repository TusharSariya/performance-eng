# Project 18: Custom `top` with eBPF

## Goal

Build a process monitoring tool that goes far beyond `/proc`-based tools like `top` and `htop` by using eBPF to get precise per-process CPU time, I/O, and network metrics directly from kernel events.

## What You'll Learn

- eBPF program composition (multiple programs working together)
- Scheduler tracepoints for precise CPU accounting
- Block I/O tracepoints for per-process disk metrics
- TCP tracepoints for per-process network metrics
- Building a real-time terminal UI with kernel data

## Background

Traditional `top`/`htop` read from `/proc` every 1-3 seconds. This has limitations:
- **Sampling error:** short-lived processes between samples are invisible
- **Inaccurate CPU time:** `/proc/[pid]/stat` updates are coarse
- **No I/O latency:** `iotop` shows bytes but not latency
- **No network detail:** can't see per-connection metrics
- **No off-CPU time:** can't see where processes are waiting

eBPF hooks into kernel events as they happen, giving exact measurements instead of sampled estimates.

## Prerequisites

- C with libbpf (CO-RE) for eBPF programs
- C or Rust for userspace (ncurses terminal UI)
- Linux kernel 5.x+ with BTF
- Root access

## Milestones

### Milestone 1: Precise CPU Accounting

**Use `sched_switch` for exact per-process CPU time.**

- Hook `tracepoint:sched:sched_switch`:
  - On switch-out: `cpu_time[prev_pid] += now - last_switch_time[cpu]`
  - On switch-in: `last_switch_time[cpu] = now`
- Also hook `sched_process_exec` and `sched_process_exit` for process lifecycle
- Per-process metrics:
  - On-CPU time (microseconds in the last interval)
  - Voluntary context switches (went to sleep)
  - Involuntary context switches (preempted)
  - CPU migrations (moved between CPUs)
  - Run-queue wait time (time runnable but not running)
- Compare against `/proc/[pid]/stat` — your numbers will be more accurate for short-lived processes

**Deliverable:** Per-process CPU metrics from eBPF, updated in real time.

### Milestone 2: Per-Process I/O Metrics

**Track block I/O per process.**

- Hook `tracepoint:block:block_rq_issue` and `block_rq_complete`:
  - Attribute I/O to the process that issued it
  - Track per-process:
    - Read bytes, write bytes
    - Read IOPS, write IOPS
    - I/O latency (min, avg, p99, max)
    - Sequential vs random I/O ratio
- Also hook `vfs_read`/`vfs_write` for filesystem-level I/O:
  - Shows I/O that hits the page cache (no block I/O)
  - Compare: filesystem bytes vs block bytes reveals cache hit ratio
- Handle async I/O attribution (I/O completed by kworker threads)

**Deliverable:** Per-process I/O metrics including latency and cache hit ratio.

### Milestone 3: Per-Process Network Metrics

**Track network activity per process.**

- Hook:
  - `tcp_sendmsg` — bytes sent per connection
  - `tcp_recvmsg` / `tcp_cleanup_rbuf` — bytes received per connection
  - `tcp_retransmit_skb` — retransmissions (indicates network issues)
  - `inet_sock_set_state` — connection state changes
- Per-process metrics:
  - TX bytes/sec, RX bytes/sec
  - Active connections count
  - New connections/sec
  - Retransmission rate
  - Connection errors
- Per-connection breakdown (top connections by traffic)

**Deliverable:** Per-process network metrics with connection-level detail.

### Milestone 4: Terminal UI

**Build an interactive ncurses-based interface.**

- Layout inspired by `htop` but with eBPF-powered columns:
  ```
  PID   USER    CPU%  ONCPU   OFFCPU  DISK-R  DISK-W  IO-LAT  NET-TX  NET-RX  RETX  CMD
  1842  postgres 45%  2.1s    0.8s    12MB/s  4MB/s   0.4ms   1MB/s   2MB/s   0.1%  postgres
  2001  nginx    12%  0.6s    0.2s    0B/s    0B/s    -       50MB/s  80MB/s  0.0%  nginx
  1523  java      8%  0.4s    1.2s    1MB/s   8MB/s   2.1ms   5MB/s   3MB/s   0.3%  java
  ```
- Interactive features:
  - Sort by any column (CPU, I/O, network)
  - Filter by user, process name, PID
  - Drill down into a process: show per-thread metrics, per-connection list
  - Toggle between rate and cumulative views
  - Process tree view (like `htop`)
- Color coding:
  - Red for high CPU saturation (run-queue wait)
  - Yellow for I/O latency spikes
  - Highlight processes with retransmissions

**Deliverable:** Interactive terminal UI combining all eBPF metrics.

### Milestone 5: Off-CPU and Wait Analysis

**Show WHERE processes spend time waiting.**

- Hook `sched_switch` with stack traces:
  - When a process goes off-CPU, capture its kernel stack
  - Categorize wait reasons:
    - **Disk I/O:** stack includes `io_schedule`, `blk_mq_*`
    - **Network:** stack includes `sk_wait_data`, `inet_csk_wait_*`
    - **Lock:** stack includes `mutex_lock`, `rwsem_down_*`
    - **Sleep:** stack includes `schedule_timeout`, `nanosleep`
    - **Pipe/socket:** stack includes `pipe_wait`, `unix_stream_*`
- Per-process wait breakdown:
  ```
  PID 1842 (postgres):  CPU: 45%  Disk-wait: 30%  Lock-wait: 15%  Net-wait: 8%  Sleep: 2%
  ```
- This answers "what is this process DOING when it's not on CPU?"

**Deliverable:** Per-process off-CPU wait categorization.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Event-driven vs polling | eBPF captures every event; /proc misses short-lived activity |
| Per-CPU maps | Avoid lock contention in eBPF programs by using per-CPU storage |
| Map aggregation in userspace | Combine per-CPU maps into per-process totals |
| Short-lived process capture | `execsnoop`-style tracking catches processes /proc misses |
| BPF ring buffer | Efficient event streaming from kernel to userspace |
| Overhead management | Too many hooks = too much overhead; balance coverage vs cost |

## Validation & Benchmarks

- Compare CPU percentages against `top`/`htop` — should be very close for long-running processes
- Compare I/O against `iotop` and `iostat`
- Compare network against `nethogs` and `ss`
- Generate known workloads:
  - `stress-ng --cpu 4` → should show ~400% CPU for the process
  - `fio` → should show matching I/O bytes
  - `iperf3` → should show matching network bytes
- Measure tool overhead: should be <2% CPU on a busy system

## References

- Brendan Gregg's "BPF Performance Tools" (the entire book, essentially)
- `htop` source code (for UI inspiration)
- `bcc/tools/` collection (individual tools this project combines)
- libbpf-bootstrap examples
- "Systems Performance" Chapter 6 (CPUs), 9 (Disks), 10 (Network)
