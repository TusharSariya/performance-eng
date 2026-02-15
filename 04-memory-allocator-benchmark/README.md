# Project 04: Memory Allocator Benchmark Suite

## What This Is

A benchmark suite that compares memory allocators (`glibc/ptmalloc2`, `jemalloc`,
`tcmalloc`, `mimalloc`) under different workload patterns, measuring throughput,
latency, fragmentation, and scalability.

The default glibc `malloc` (ptmalloc2) is a general-purpose allocator that makes
tradeoffs. Specialized allocators like jemalloc (Facebook/Meta, Redis, Rust),
tcmalloc (Google), and mimalloc (Microsoft) optimize for different scenarios.
Choosing the right allocator — or understanding why your app is slow due to
allocation patterns — is a critical performance engineering skill.

## Quick Start

```bash
# Build
make all

# Run a single benchmark (uses glibc by default)
bin/bench_single                        # micro-benchmark harness
bin/bench_mt                            # multithreaded scalability
bin/bench_frag                          # fragmentation deep-dive
bin/bench_realistic                     # realistic workloads

# Run with a different allocator via LD_PRELOAD
./scripts/run_allocator.sh jemalloc bin/bench_single
./scripts/run_allocator.sh tcmalloc bin/bench_mt --csv

# Run ALL benchmarks across ALL available allocators (produces CSV + plots)
./scripts/run_all.sh
```

## Prerequisites

```bash
# Required
sudo apt install build-essential

# Allocators (install whichever you want to test)
sudo apt install libjemalloc-dev          # jemalloc
sudo apt install libgoogle-perftools-dev  # tcmalloc
sudo apt install libmimalloc-dev          # mimalloc

# For plotting (optional)
pip install matplotlib numpy
```

## Milestones

### Milestone 1: Micro-Benchmark Harness (`bench_single`)

Five workload patterns test different allocation behaviors:

| Workload | Size Range | What It Tests |
|----------|-----------|---------------|
| `small_allocs` | 8–64 B | millions of tiny allocations (thread caches, size classes) |
| `medium_allocs` | 1–64 KB | mid-range allocations (slab allocators) |
| `large_allocs` | 1–4 MB | mmap-backed allocations (OS interaction) |
| `mixed_allocs` | log-normal | realistic size distribution with long tail |
| `alloc_free_churn` | mixed | allocate/free/re-allocate (fragmentation stress) |

Each workload measures:
- **Throughput**: operations per second (alloc + free)
- **Latency**: per-operation min, p50, p95, p99, max (nanoseconds)
- **RSS**: peak resident memory (`/proc/self/status` VmRSS)
- **Fragmentation ratio**: RSS / live bytes (1.0 = perfect, higher = worse)

```bash
bin/bench_single                          # human-readable output
bin/bench_single --csv                    # CSV for analysis
bin/bench_single mixed_allocs             # run a single workload
OPS=5000000 bin/bench_single              # override operation count
```

### Milestone 2: Multithreaded Scalability (`bench_mt`)

Three workload patterns measure how allocators scale with threads:

| Workload | Pattern | What It Tests |
|----------|---------|---------------|
| `thread_local` | Each thread allocs/frees its own memory | Arena-per-thread, no contention |
| `producer_consumer` | Thread A allocates, thread B frees | Cross-thread free (most challenging) |
| `shared_pool` | All threads alloc/free from shared structure | Lock contention on allocation |

Scales from 1 to 2x core count, reporting throughput at each level.

```bash
bin/bench_mt                              # default thread counts
bin/bench_mt --csv                        # CSV for plotting
bin/bench_mt --threads 1,2,4,8,16        # custom thread counts
bin/bench_mt thread_local                 # single workload
```

### Milestone 3: Fragmentation Deep-Dive (`bench_frag`)

Deliberately creates fragmentation and measures its impact:

1. **Phase 1**: Allocate N objects of varying sizes (70% small, 20% medium, 10% large)
2. **Phase 2**: Free every other object (create "holes" in the heap)
3. **Phase 3**: Re-allocate with *different* (larger) sizes — can the allocator reuse the holes?
4. **Phase 4**: Free everything — how much RSS does the allocator retain?

Tracks RSS at each step, outputs a time-series for plotting.

```bash
bin/bench_frag                            # human-readable with 1M objects
bin/bench_frag --csv                      # CSV time-series
bin/bench_frag --objects 500000           # fewer objects for faster runs
```

### Milestone 4: Realistic Workloads (`bench_realistic`)

Simulates real application allocation patterns:

| Workload | Simulation | Allocation Pattern |
|----------|-----------|-------------------|
| `webserver` | HTTP request processing | Burst alloc (buffer + headers + response), then free all |
| `kvstore` | Hash map with random ops | 50% insert, 30% lookup, 20% delete of variable-size entries |
| `json_parser` | JSON document tree | Tree of small nodes with string values, pipelined lifetime |

```bash
bin/bench_realistic                       # all workloads
bin/bench_realistic --csv                 # CSV output
bin/bench_realistic kvstore               # single workload
```

### Milestone 5: Visualization & Analysis

The runner script produces CSV files; the plotting scripts produce charts:

```bash
# Generate all CSV data
./scripts/run_all.sh

# Generate plots
python3 scripts/plot_throughput.py        # bar charts: throughput per allocator
python3 scripts/plot_scaling.py           # line charts: throughput vs thread count
python3 scripts/plot_frag.py              # time-series: RSS and fragmentation ratio
```

## Swapping Allocators with `LD_PRELOAD`

The benchmarks use the standard `malloc`/`free` API. Allocators are swapped at
runtime via `LD_PRELOAD` without recompilation:

```bash
# Direct LD_PRELOAD
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 bin/bench_single

# Using the helper script (auto-detects library paths)
./scripts/run_allocator.sh jemalloc bin/bench_single --csv
./scripts/run_allocator.sh tcmalloc bin/bench_mt --csv
./scripts/run_allocator.sh mimalloc bin/bench_frag --csv
```

The benchmark auto-detects which allocator is active and includes it in the output.

## Key Concepts

| Concept | Why It Matters |
|---------|---------------|
| Thread caches (tcache) | Avoid lock contention on hot allocation paths |
| Size classes / slabs | Reduce fragmentation by bucketing similar sizes |
| Arena-per-thread | jemalloc/tcmalloc strategy for scalability |
| `mmap` vs `brk` | Large allocs use mmap (returnable to OS); small use brk/sbrk |
| `MADV_DONTNEED` / `MADV_FREE` | How allocators return memory to the OS |
| RSS vs heap size | Allocator may hold memory the app has freed |
| Internal vs external fragmentation | Wasted space within vs between allocations |

## Configuration

All benchmarks support environment variables and flags:

```bash
# Operation count (overrides per-workload defaults)
OPS=5000000 bin/bench_single
OPS=100000 bin/bench_mt

# CSV output (machine-readable)
bin/bench_single --csv
bin/bench_mt --csv
bin/bench_frag --csv
bin/bench_realistic --csv

# Filter to specific workload
bin/bench_single small_allocs
bin/bench_mt thread_local
bin/bench_realistic kvstore

# Custom thread counts
bin/bench_mt --threads 1,2,4,8,16,32

# Custom object count for fragmentation
bin/bench_frag --objects 2000000

# Multiple benchmark runs (run_all.sh)
RUNS=5 ./scripts/run_all.sh
```

## Project Structure

```
04-memory-allocator-benchmark/
├── Makefile                    # make all / make clean / make run
├── README.md                   # this file
├── src/
│   ├── common.h                # timing, RSS, latency histogram, RNG, formatting
│   ├── bench_single.c          # Milestone 1: micro-benchmark harness
│   ├── bench_mt.c              # Milestone 2: multithreaded scalability
│   ├── bench_frag.c            # Milestone 3: fragmentation deep-dive
│   └── bench_realistic.c       # Milestone 4: realistic workloads
├── scripts/
│   ├── run_all.sh              # run all benchmarks across all allocators
│   ├── run_allocator.sh        # run single benchmark with specific allocator
│   ├── plot_throughput.py      # bar charts: throughput comparison
│   ├── plot_scaling.py         # line charts: thread scaling
│   └── plot_frag.py            # time-series: RSS and fragmentation
└── results/                    # CSV and PNG output (generated)
```

## Validation Tips

For reproducible results:

```bash
# Pin to specific cores
taskset -c 0-7 bin/bench_mt

# Disable turbo boost
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# Set performance governor
sudo cpupower frequency-set -g performance

# Cross-validate RSS with smaps
cat /proc/<pid>/smaps_rollup

# Use perf stat alongside benchmarks
perf stat -e page-faults,cache-misses bin/bench_single
```

## References

- jemalloc paper: "A Scalable Concurrent malloc Implementation for FreeBSD"
- tcmalloc design doc: https://google.github.io/tcmalloc/design.html
- mimalloc paper: "Mimalloc: Free List Sharding in Action"
- glibc malloc internals: https://sourceware.org/glibc/wiki/MallocInternals
- Brendan Gregg, "Systems Performance" 2nd Ed, Chapter 7 (Memory)
