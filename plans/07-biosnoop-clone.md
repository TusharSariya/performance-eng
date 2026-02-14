# Project 07: Build a `biosnoop` Clone

## Goal

Build an eBPF tool that traces block I/O requests at the device level, showing every disk I/O with its latency, size, offset, and originating process — a reimplementation of Brendan Gregg's `biosnoop`.

## What You'll Learn

- The Linux block I/O layer (bio layer, request queue, schedulers)
- Block device tracepoints (`block_rq_issue`, `block_rq_complete`)
- eBPF map types (hash maps for in-flight I/O tracking)
- I/O patterns: sequential vs random, read vs write ratios
- How to correlate I/O to processes

## Background

When applications read/write files, the requests pass through the VFS, filesystem, and block layers before reaching the device driver. `biosnoop` instruments the block layer to see every I/O request. This is essential for understanding:
- Which processes are generating I/O
- Whether I/O is sequential or random
- I/O latency at the device level (below filesystem caches)
- Whether the I/O scheduler is reordering requests effectively

## Prerequisites

- C with libbpf (CO-RE)
- Linux kernel 5.x+ with BTF
- Root access
- A disk to generate I/O against (any local disk works)

## Milestones

### Milestone 1: Basic Block I/O Tracer with bpftrace

**Prototype the tool using bpftrace.**

- Hook tracepoints:
  - `block:block_rq_issue` — request submitted to device
  - `block:block_rq_complete` — request completed by device
- On issue: store timestamp in `@start[dev, sector]`
- On complete: calculate latency, print:
  ```
  TIME      PID    COMM         DISK   T  SECTOR     BYTES   LAT(ms)
  14:23:01  1842   postgres     sda    R  12345678   4096    0.42
  14:23:01  1842   postgres     sda    R  12345686   4096    0.38
  14:23:02  988    jbd2/sda1    sda    W  98765432   16384   1.20
  ```

**Deliverable:** bpftrace one-liner or script that traces all block I/O with latency.

### Milestone 2: Full C Implementation with libbpf

**Build a proper tool with libbpf CO-RE.**

- BPF programs:
  - `tp_btf/block_rq_issue`: record `{timestamp, pid, comm}` in hash map keyed by `(dev, sector)`
  - `tp_btf/block_rq_complete`: lookup start time, calculate delta, send to userspace via ring buffer
- Userspace:
  - Poll the ring buffer
  - Format and print output
  - Handle map cleanup for missed completions (timeout old entries)
- Handle multi-bio merging (block layer may merge adjacent requests)

**Deliverable:** `./biosnoop` binary with clean per-I/O output.

### Milestone 3: Latency Histograms (`biolatency`)

**Aggregate I/O latency into distributions instead of per-event output.**

- Build a companion tool (or mode flag) that shows latency histograms
- Histogram variants:
  - Overall latency distribution
  - Per-disk histograms
  - Per-operation type (read vs write)
  - Per-I/O size bucket
- Power-of-2 histogram using BPF array map
- Interval output: print histogram every N seconds

**Deliverable:** `./biolatency -d sda 1` showing per-second I/O latency distributions.

### Milestone 4: I/O Pattern Analysis

**Detect and classify I/O patterns.**

- Track sequential vs random I/O:
  - Compare each request's sector with the previous request's (sector + size)
  - Sequential = next sector follows immediately
  - Calculate sequential ratio over time
- Track I/O size distribution
- Track queue depth (how many I/Os are in-flight simultaneously)
- Build a summary report:
  ```
  Disk: sda
  Total I/Os: 45,231 (32,100 R / 13,131 W)
  Avg latency: 0.45ms (R: 0.38ms, W: 0.62ms)
  P99 latency: 2.1ms
  Sequential ratio: 72%
  Avg I/O size: 8.2 KB
  Avg queue depth: 3.4
  ```

**Deliverable:** I/O pattern analysis report for any block device.

### Milestone 5: I/O Visualization and Heat Maps

**Visualize I/O patterns over time and across the disk.**

- **Offset heat map:** x-axis = time, y-axis = disk sector, color = latency
  - Shows whether I/O is hitting the same disk regions or spread out
- **Latency heat map:** x-axis = time, y-axis = latency bucket, color = count
  - Shows latency distribution changes over time (detects I/O storms)
- Output as SVG or feed into a terminal-based visualization
- Inspired by Gregg's "I/O heat maps"

**Deliverable:** Heat map visualizations of block I/O activity.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Block layer vs filesystem layer | `biosnoop` sees what actually hits the disk, below page cache |
| Request merging | Adjacent I/Os may be merged; affects what you trace |
| I/O schedulers (mq-deadline, bfq, none) | Reorder and merge requests; affect latency patterns |
| Direct I/O vs buffered I/O | Buffered goes through page cache; direct bypasses it |
| Queue depth | How many I/Os the device is processing concurrently |
| NVMe vs SATA | Different latency profiles, queue depths, multiqueue |

## Validation & Benchmarks

- Generate known I/O patterns with `fio`:
  - Sequential read: `fio --name=seq --rw=read --bs=128k --direct=1`
  - Random write: `fio --name=rand --rw=randwrite --bs=4k --direct=1`
- Verify your tool's output matches `iostat -x` averages
- Compare against `bcc/biosnoop` output
- Test with NVMe (multiqueue, low latency) and HDD (single queue, high latency)

## References

- Brendan Gregg's `biosnoop` from bcc-tools
- "BPF Performance Tools" Chapter 9 (Disk I/O)
- Linux block layer documentation: `Documentation/block/`
- `blktrace(8)` — the older block tracing tool
- libbpf-bootstrap examples
