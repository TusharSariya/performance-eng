# Project 09: I/O Scheduler Comparator

## Goal

Build a synthetic workload generator and benchmarking framework to compare Linux I/O schedulers (`mq-deadline`, `bfq`, `none`/`noop`) under different workload profiles, with detailed latency distribution analysis.

## What You'll Learn

- Linux multi-queue block layer (blk-mq)
- I/O scheduler algorithms and their tradeoffs
- Workload characterization (IOPS vs throughput, read vs write, sequential vs random)
- Latency distribution analysis (why averages lie)
- How to tune I/O schedulers for specific workloads

## Background

Linux has three I/O schedulers for blk-mq devices:
- **`none`** — no reordering, FIFO; best for NVMe (device has its own scheduler)
- **`mq-deadline`** — prevents starvation with read/write deadlines; good for general use
- **`bfq`** — budget fair queuing; best for interactive/desktop workloads, per-process fairness

Choosing the wrong scheduler can cost 20-50% throughput or cause tail latency spikes. This project teaches you to measure the difference rigorously.

## Prerequisites

- C (for workload generator) or use `fio` for some milestones
- Linux with root access (to change schedulers)
- A real block device (not tmpfs or ramdisk)
- Python or gnuplot for visualization

## Milestones

### Milestone 1: Workload Generator

**Build a configurable I/O workload generator.**

- Parameters:
  - Read/write ratio (0-100%)
  - I/O size (4K, 8K, 16K, 64K, 128K, 1M)
  - Access pattern: sequential, random, Zipfian (hot spots)
  - Queue depth: 1, 4, 16, 32, 64, 128
  - Number of threads/jobs
  - Direct I/O flag
  - Duration or total I/O count
- Use `io_uring` or `libaio` for async I/O (not blocking `read`/`write`)
- Per-I/O latency recording (store in array, compute percentiles after)
- Output: IOPS, throughput (MB/s), latency percentiles (p50, p90, p95, p99, p999, max)

**Deliverable:** `./iogen --rw=randrw --rwmix=70 --bs=4k --depth=32 --direct=1 --runtime=60`

### Milestone 2: Automated Scheduler Comparison

**Run the same workload across all schedulers and compare.**

- Script that:
  1. Sets scheduler: `echo mq-deadline > /sys/block/sda/queue/scheduler`
  2. Drops caches: `echo 3 > /proc/sys/vm/drop_caches`
  3. Runs workload
  4. Collects results
  5. Repeats for each scheduler
- Workload matrix:
  - Sequential read, sequential write
  - Random read, random write
  - Mixed random read/write (70/30)
  - Sequential write with random read interference
- Output: comparison table per workload

**Deliverable:** Automated benchmark runner producing a comparison report.

### Milestone 3: Queue Depth Sensitivity Analysis

**How does each scheduler behave as queue depth increases?**

- For each scheduler, run the same workload at queue depths: 1, 2, 4, 8, 16, 32, 64, 128, 256
- Plot:
  - IOPS vs queue depth (should plateau for HDD, scale for SSD/NVMe)
  - Latency vs queue depth (always increases; the question is how fast)
  - Throughput vs queue depth
- Identify the "sweet spot" queue depth for each scheduler × device combination

**Deliverable:** Charts showing scheduler behavior across queue depths.

### Milestone 4: Multi-Process Fairness

**Test how schedulers handle competing workloads.**

- Scenarios:
  - **Two sequential readers:** does one starve the other?
  - **Sequential writer vs random reader:** does the writer block the reader?
  - **Background bulk write + latency-sensitive random read:** measure read latency impact
  - **Many processes (16+) competing:** latency spread across processes
- For each scenario, measure per-process:
  - Throughput
  - Latency percentiles
  - Jain's fairness index (quantifies fairness)
- `bfq` should shine here (it's designed for fairness)
- `none` should show the most unfairness

**Deliverable:** Fairness analysis report comparing schedulers under contention.

### Milestone 5: Scheduler Tuning Deep-Dive

**Explore tunable parameters for each scheduler.**

- `mq-deadline` tunables:
  - `read_expire` (default 500ms)
  - `write_expire` (default 5000ms)
  - `writes_starved` (how many reads before a write gets priority)
  - `fifo_batch` (batch size before switching direction)
- `bfq` tunables:
  - `low_latency` (prioritize interactive I/O)
  - `slice_idle` (idle time per process for sequential detection)
  - `strict_guarantees`
- Device queue tunables:
  - `nr_requests` — max queue depth
  - `max_sectors_kb` — max I/O size
  - `rotational` — hint for SSD vs HDD
- For each tunable, sweep values and measure impact on your workloads

**Deliverable:** Tuning guide showing which tunables matter for which workloads.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| blk-mq (multi-queue) | Modern block layer; hardware queues map to software queues per CPU |
| Merging and sorting | Schedulers combine adjacent I/Os and sort by sector for HDDs |
| Read vs write priority | Reads are synchronous (app waits); writes can be buffered |
| Queue depth | More outstanding I/Os = higher throughput but higher latency |
| I/O priority (ionice) | `bfq` respects I/O priority classes; `none` ignores them |
| NVMe native queues | NVMe has 64K hardware queues; `none` scheduler often best |

## Validation & Benchmarks

- Cross-validate against `fio` results with same parameters
- Verify scheduler is actually changed: `cat /sys/block/sda/queue/scheduler`
- Use `iostat -x 1` during runs to verify device-level metrics match your tool
- Use `blktrace` or Project 07's biosnoop to see actual I/O reordering
- Run each benchmark 3-5 times and report variance / confidence intervals

## References

- Brendan Gregg's "Systems Performance" Chapter 9 (Disks)
- Linux block layer documentation: `Documentation/block/`
- `fio` documentation and examples
- BFQ scheduler paper: "Budget Fair Queueing" (Valente, Checconi)
- Kernel source: `block/mq-deadline.c`, `block/bfq-iosched.c`
