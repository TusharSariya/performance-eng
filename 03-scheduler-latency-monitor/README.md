# Project 03: Scheduler Latency Monitor

## What Is Run-Queue Latency?

When the kernel wakes a task (marks it runnable), the task may not run immediately
— it sits on the CPU **run queue** waiting for a core. The time between becoming
runnable and actually executing is the **run-queue latency**. This is the key metric
for **CPU saturation** — one of the three pillars of the USE method (Utilization,
Saturation, Errors).

A system can show moderate CPU utilization and still have terrible tail latency if
tasks are queuing behind each other.

```
  sched_wakeup ─────► on run queue ─────► sched_switch (running)
                      ◄────────────────►
                       run-queue latency
```

## Architecture

```
  ┌──────────────────────────────────────────────────────────────────┐
  │                         Kernel (BPF)                             │
  │                                                                  │
  │  tracepoint/sched/sched_wakeup ──┐                               │
  │  tracepoint/sched/sched_wakeup_new──► record_enqueue(pid)        │
  │  tracepoint/sched/sched_switch ──┤    → start[pid] = ktime_ns()  │
  │                                  │                               │
  │                                  └► record_dequeue(next_pid)     │
  │                                      → delta = now - start[pid]  │
  │                                      → hist[log2(delta/1000)]++  │
  │                                                                  │
  │  Maps:                                                           │
  │    start    (hash)   tid → timestamp                             │
  │    hist     (array)  slot → count     (26 log2 buckets, usecs)   │
  │    hist_cpu (array)  cpu*26+slot → count  (per-CPU mode)         │
  └──────────────────────────────────────────────────────────────────┘
                              │
                         map reads
                              │
  ┌──────────────────────────────────────────────────────────────────┐
  │                      Userspace (runqlat)                         │
  │                                                                  │
  │  Open BPF object → set .rodata (pid filter, per-CPU flag)       │
  │  Load → attach tracepoints                                      │
  │  Loop: sleep(interval) → read hist → print → clear              │
  │                                                                  │
  │  Output modes: ASCII histogram, per-CPU, CSV for visualization   │
  └──────────────────────────────────────────────────────────────────┘
```

## Quick Start

```bash
# Install dependencies
sudo apt install -y bpftrace clang libbpf-dev libelf-dev

# Build everything
make all

# M1: bpftrace prototype (Ctrl-C to stop)
sudo bpftrace scripts/runqlat.bt

# M2: C implementation — 3 one-second histograms
sudo bin/runqlat 1 3

# M3: filter to one PID
bin/cpu_stress 15 40 &
sudo bin/runqlat -p $(pgrep cpu_stress) 1 3

# M3: per-CPU histograms
sudo bin/runqlat -C 1 3

# M4: CSV output for visualization
sudo bin/runqlat --csv 1 10 > results/latency.csv
python3 scripts/plot_latency.py results/latency.csv -o results/latency.png

# Full demo (idle vs loaded comparison)
sudo bash scripts/run_demo.sh
```

## Test Environment

| Parameter | Value |
|-----------|-------|
| CPU | Intel Core i5-13600K (14 cores, 20 threads) |
| Kernel | Linux 6.8.0 with BTF |
| libbpf | 0.5.0 |
| Compiler | clang (BPF), gcc (userspace) |

## Milestones

### M1: bpftrace Prototype (`scripts/runqlat.bt`)

Quick prototype using bpftrace. Hooks `sched_wakeup`, `sched_wakeup_new`, and
`sched_switch` to produce a power-of-2 histogram of run-queue latency in
microseconds.

### M2: C + libbpf Implementation (`src/runqlat.bpf.c` + `src/runqlat.c`)

Full reimplementation using libbpf CO-RE. The BPF program records enqueue
timestamps on wakeup events and computes latency deltas on context switches.
Results are stored in a log2 histogram map read periodically by userspace.

### M3: Filtering and Per-CPU Mode

- `-p PID` — trace a single process
- `-C` — show separate histograms per CPU (reveals which CPUs are saturated)
- `-m` — display in milliseconds

### M4: Time-Series Output and Visualization

- `--csv` — output `timestamp,p50_us,p95_us,p99_us,max_us` per interval
- `scripts/plot_latency.py` — plot percentile time series with matplotlib

## What to Expect

**Idle system:** most events in 0–15 us buckets.

```
     usecs               : count     distribution
         0 -> 1          : 1523     |****                                    |
         2 -> 3          : 8932     |*************************               |
         4 -> 7          : 14205    |****************************************|
         8 -> 15         : 11847    |*********************************       |
        16 -> 31         : 3241     |*********                               |
        32 -> 63         : 876      |**                                      |
```

**Under 2x CPU oversubscription** (`cpu_stress` with 40 threads on 20 CPUs):
events shift into the 100 us – 10 ms+ range.

```
     usecs               : count     distribution
         0 -> 1          : 42       |                                        |
         2 -> 3          : 187      |                                        |
         4 -> 7          : 1205     |**                                      |
         8 -> 15         : 3847     |*******                                 |
        16 -> 31         : 8241     |***************                         |
        32 -> 63         : 12876    |***********************                 |
        64 -> 127        : 18567    |*********************************       |
       128 -> 255        : 22034    |****************************************|
       256 -> 511        : 19876    |************************************    |
       512 -> 1023       : 14523    |**************************              |
      1024 -> 2047       : 8901     |****************                        |
      2048 -> 4095       : 4567     |********                                |
      4096 -> 8191       : 1234     |**                                      |
```

## How It Works

1. **`sched_wakeup` / `sched_wakeup_new`**: Task becomes runnable → store
   `ktime_get_ns()` in `start[tid]`
2. **`sched_switch` (prev_state == TASK_RUNNING)**: Previous task was preempted
   (not sleeping) → record its enqueue time
3. **`sched_switch` (next task)**: Task starts running → compute
   `delta = now - start[tid]`, increment `hist[log2(delta/1000)]`

The `prev_state == 0` check in step 2 distinguishes involuntary preemption (task
still wants to run) from voluntary sleep (task called `sleep()`, blocked on I/O,
etc.). Only preempted tasks go back on the run queue.

## Project Structure

```
03-scheduler-latency-monitor/
├── Makefile
├── README.md
├── scripts/
│   ├── runqlat.bt          # M1: bpftrace prototype
│   ├── plot_latency.py     # M4: visualization
│   └── run_demo.sh         # demo orchestration
├── src/
│   ├── vmlinux.h           # generated from /sys/kernel/btf/vmlinux
│   ├── runqlat.h           # shared constants
│   ├── runqlat.bpf.c       # BPF program
│   └── runqlat.c           # userspace loader
├── samples/
│   └── cpu_stress.c        # CPU-bound test workload
├── bin/                    # build output
└── results/                # CSV/PNG output
```

## References

- Brendan Gregg, "BPF Performance Tools" Chapter 6 (CPUs)
- Brendan Gregg's `runqlat` from bcc-tools
- Linux kernel source: `kernel/sched/fair.c`
- libbpf-bootstrap examples
- USE Method: http://www.brendangregg.com/usemethod.html
