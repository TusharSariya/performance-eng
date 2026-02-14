# Project 16: Syscall Latency Dashboard

## Goal

Build an eBPF-based tool that hooks `sys_enter`/`sys_exit` tracepoints, records per-syscall latency histograms, and exports data to a real-time terminal dashboard or Prometheus.

## What You'll Learn

- Linux system call interface and overhead
- eBPF tracepoints (`raw_syscalls:sys_enter`, `raw_syscalls:sys_exit`)
- BPF map types (per-CPU arrays, histograms, ring buffers)
- Real-time data visualization from kernel-space data
- Which syscalls are hot paths for different application types

## Background

System calls are the boundary between user applications and the kernel. Every file read, network send, memory allocation, and process creation goes through a syscall. Understanding which syscalls an application makes, how often, and how long they take reveals:
- Whether the app is I/O-bound, CPU-bound, or syscall-bound
- Hidden sources of latency (slow syscalls the app doesn't log)
- Unexpected behavior (why is this web server calling `stat()` 10,000 times/sec?)

This is a reimplementation of aspects of `strace -c` and `syscount` from bcc-tools, but with histograms and real-time visualization.

## Prerequisites

- C with libbpf (CO-RE)
- Linux kernel 5.x+ with BTF
- Root access
- ncurses (for terminal UI) or Prometheus client library

## Milestones

### Milestone 1: Syscall Counter

**Count syscalls by type for all processes or a specific PID.**

- Hook `raw_syscalls:sys_enter` tracepoint
- Key a BPF hash map by `(pid, syscall_nr)`
- Increment count on each entry
- Userspace periodically reads the map and displays:
  ```
  SYSCALL          COUNT     RATE(/s)
  read             145,231   14,523
  write            98,421    9,842
  epoll_wait       45,100    4,510
  futex            12,340    1,234
  clock_gettime    8,901     890
  ```
- Translate syscall numbers to names using `/usr/include/asm/unistd_64.h` or `ausyscall`
- Support PID filtering and process name filtering

**Deliverable:** `./syscount -p <pid> -i 1` showing syscall frequency.

### Milestone 2: Syscall Latency Measurement

**Measure how long each syscall takes.**

- Hook both `raw_syscalls:sys_enter` and `raw_syscalls:sys_exit`
- On enter: store `{timestamp, syscall_nr}` in hash map keyed by `(pid, tid)`
- On exit: look up start time, compute delta, aggregate into histogram
- Per-syscall statistics:
  - Count, total time, average, min, max
  - Latency histogram (log2 buckets: 0-1us, 1-2us, 2-4us, ..., 512ms-1s, >1s)
- Output sorted by total time (shows where the app spends time in the kernel)

**Deliverable:** `./syslat -p <pid>` showing per-syscall latency distributions.

### Milestone 3: Slow Syscall Tracer

**Alert on syscalls that exceed a latency threshold.**

- Define threshold per syscall (or global default):
  - `read` > 10ms (might indicate disk I/O)
  - `futex` > 1ms (might indicate lock contention)
  - `connect` > 100ms (might indicate DNS or network issue)
- When a syscall exceeds its threshold, emit a detailed event:
  ```
  SLOW SYSCALL: read() took 45.2ms
    PID: 1842  COMM: postgres  TID: 1845
    fd=7 (/var/lib/postgresql/data/base/16384/2663)
    Args: buf=0x7f..., count=8192
    Stack: postgres`heap_fetch+0x42 → postgres`index_getnext+0x1a → ...
  ```
- Capture syscall arguments using tracepoint fields
- Optionally capture user stack traces (`bpf_get_stackid`)

**Deliverable:** `./sysslow --threshold 10ms` that traces slow syscalls with context.

### Milestone 4: Real-Time Terminal Dashboard

**Build an ncurses-based live dashboard.**

- Layout:
  - Top panel: top-10 syscalls by rate (bar chart)
  - Middle panel: selected syscall's latency histogram (ASCII art)
  - Bottom panel: recent slow syscall events
  - Side panel: summary stats (total syscalls/sec, avg latency, CPU time in kernel)
- Interactive features:
  - Arrow keys to select different syscalls
  - Filter by PID/process name
  - Toggle between rate/latency/total-time sorting
  - Zoom into a time window
- Refresh every 100ms-1s (configurable)

**Deliverable:** Interactive terminal dashboard for real-time syscall monitoring.

### Milestone 5: Prometheus/Grafana Export

**Export metrics for long-term monitoring.**

- Expose Prometheus metrics endpoint (`/metrics` on HTTP):
  - `syscall_total{syscall="read",pid="1842"}` — counter
  - `syscall_duration_seconds{syscall="read",quantile="0.99"}` — summary
  - `syscall_duration_seconds_bucket{syscall="read",le="0.001"}` — histogram
  - `syscall_slow_total{syscall="read"}` — counter of slow syscalls
- Build a Grafana dashboard template:
  - Syscall rate over time (stacked area chart)
  - Latency heatmap (x=time, y=latency bucket, color=count)
  - Top slow syscalls table
  - Per-process syscall breakdown
- Alert rules:
  - Spike in `futex` calls (lock contention)
  - Spike in `read` latency (disk issues)
  - Unusual syscall patterns (security: unexpected `execve`)

**Deliverable:** Prometheus exporter + Grafana dashboard JSON.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Syscall overhead | Each syscall costs ~100ns+ (context switch to kernel and back) |
| vDSO | Some "syscalls" (`clock_gettime`, `gettimeofday`) bypass the kernel entirely |
| Batching syscalls | `io_uring`, `sendmmsg`, `recvmmsg` reduce per-call overhead |
| Seccomp | Can block specific syscalls; useful to verify with this tool |
| strace vs eBPF | strace uses ptrace (high overhead); eBPF is in-kernel (low overhead) |
| Syscall arguments | Understanding what an app is doing requires knowing the args, not just the call |

## Validation & Benchmarks

- Compare syscall counts against `strace -c` output
- Compare latency against `strace -T` per-call timing (with overhead caveat)
- Measure tool overhead: run a benchmark with and without the tool attached
  - eBPF should add <5% overhead; strace adds 50-100%
- Test with known workloads:
  - `dd` → should show heavy `read`/`write`
  - Redis → should show `epoll_wait`, `read`, `write`
  - Compilation → should show `execve`, `open`, `read`, `mmap`

## References

- Brendan Gregg's `syscount` and `syscall_latency` from bcc-tools
- "BPF Performance Tools" Chapter 5 (Applications) and Chapter 4 (BPF tools)
- `raw_syscalls` tracepoint format: `/sys/kernel/debug/tracing/events/raw_syscalls/`
- libbpf-bootstrap examples
- Prometheus client library documentation
