# Project 03: Scheduler Latency Monitor

## Goal

Build a tool that measures CPU scheduler run-queue latency — the time a task spends waiting to be scheduled onto a CPU — using eBPF or ftrace. This is a reimplementation of Brendan Gregg's `runqlat` from first principles.

## What You'll Learn

- Linux CFS (Completely Fair Scheduler) internals
- Tracepoints: `sched_wakeup`, `sched_wakeup_new`, `sched_switch`
- eBPF maps (hash maps, histograms)
- Latency distribution analysis (histograms, percentiles)
- The difference between CPU utilization and CPU saturation

## Background

A system can show 70% CPU utilization and still have terrible latency if tasks are queuing. Run-queue latency is the key metric that reveals CPU **saturation** — one of the three pillars of the USE method (Utilization, Saturation, Errors).

The scheduler wakes a task (marks it runnable), but it may sit on the run queue waiting for a CPU. The time between wakeup and actually running is the run-queue latency.

## Prerequisites

- C + libbpf (or bpftrace for prototyping)
- Linux kernel 5.x+ with BTF support
- Root access (eBPF requires CAP_BPF or root)
- Understanding of basic scheduler concepts

## Milestones

### Milestone 1: Prototype with bpftrace

**Get a working version quickly using bpftrace.**

```
tracepoint:sched:sched_wakeup   → record timestamp in map[tid]
tracepoint:sched:sched_switch   → if prev_state == RUNNING, record timestamp
                                → lookup map[next_tid], compute delta, store in histogram
```

- Output a power-of-2 histogram of run-queue latency
- Filter by process name or PID
- Show per-CPU breakdowns

**Deliverable:** A bpftrace script that prints latency histograms like `runqlat`.

### Milestone 2: Rewrite in C with libbpf (CO-RE)

**Build a proper portable tool using libbpf and CO-RE (Compile Once, Run Everywhere).**

- Write the eBPF program in C (BPF-side)
- Use BPF hash map to store wakeup timestamps keyed by TID
- Use BPF histogram map (array of 64 slots, log2 buckets) for latency distribution
- Write userspace loader in C:
  - Load and attach the BPF program
  - Periodically read the histogram map
  - Print formatted output
- Use vmlinux.h and CO-RE relocations for portability

**Deliverable:** `./runqlat` binary that works across kernel versions without recompilation.

### Milestone 3: Per-Process and Per-CPU Breakdown

**Add filtering and grouping capabilities.**

- Filter by PID, process name, cgroup
- Group histograms by:
  - Per-PID (which processes suffer the most scheduling delay)
  - Per-CPU (which CPUs are most saturated)
  - Per-priority/nice level
- Add a `--pidnss` flag to work with containers (PID namespaces)

**Deliverable:** `./runqlat -p <pid>` and `./runqlat --per-cpu` modes.

### Milestone 4: Time-Series Output and Visualization

**Track how scheduling latency changes over time.**

- Add interval mode (`./runqlat 1` — print histogram every 1 second)
- Output in CSV/JSON for ingestion by Grafana, Prometheus, or custom plotting
- Build a terminal UI (using ncurses or similar) showing:
  - Live-updating histogram
  - p50/p95/p99 latency over time
  - Per-CPU heat map

**Deliverable:** Real-time terminal dashboard showing scheduling latency.

### Milestone 5: Runqueue Length Tracker (Stretch)

**Measure how many tasks are waiting, not just how long they wait.**

- Hook `sched_wakeup` (increment count) and `sched_switch` (decrement count) per CPU
- Track run-queue length over time
- Correlate queue length with latency:
  - Plot queue length vs p99 latency
  - Identify the inflection point where latency spikes

**Deliverable:** Combined queue-length + latency monitor.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| CFS vruntime | How Linux decides which task runs next |
| Run queue vs wait queue | Runnable (wants CPU) vs sleeping (waiting on I/O/lock) |
| `sched_switch` prev_state | Distinguishes voluntary (sleeping) vs involuntary (preempted) context switches |
| CPU saturation | Run-queue latency is THE metric for CPU saturation |
| Latency histograms | Averages hide outliers; distributions reveal the truth |
| CO-RE and BTF | How to write portable eBPF programs |

## Validation & Benchmarks

- Generate known CPU contention with `stress-ng --cpu <2x num cores>`
- Verify that run-queue latency increases proportionally
- Compare your output against `bcc/runqlat` and `perf sched latency`
- On an idle system, latency should be near-zero (microseconds)
- Under contention, you should see a clear bimodal or shifted distribution

## References

- Brendan Gregg's `runqlat` from bcc-tools
- "BPF Performance Tools" Chapter 6 (CPUs)
- Linux kernel source: `kernel/sched/fair.c`
- libbpf-bootstrap examples: https://github.com/libbpf/libbpf-bootstrap
- `tracepoint:sched:sched_switch` format in `/sys/kernel/debug/tracing/events/sched/`
