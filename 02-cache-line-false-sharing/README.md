# Project 02: Cache-Line False Sharing Demonstrator

## What Is False Sharing?

Modern CPUs don't read or write individual bytes from main memory. They operate on
**cache lines** — 64-byte chunks that are the smallest unit of the cache coherence
protocol. When two threads on different cores write to different variables that happen
to sit on the **same 64-byte cache line**, the hardware coherence protocol (MESI) must
bounce that cache line back and forth between cores on every write. This is called
**false sharing** — the threads aren't actually sharing data, but the hardware doesn't
know that.

```
                        64 bytes
            ┌──────────────────────────────────┐
  PACKED:   │ counter_a (8B) │ counter_b (8B)  │  ← same cache line!
            └──────────────────────────────────┘
              Thread 1 writes    Thread 2 writes
              ───────────────    ───────────────
              Core must acquire  Core must acquire
              exclusive access   exclusive access
              EVERY iteration    EVERY iteration

                        64 bytes                          64 bytes
            ┌──────────────────────────────────┐┌──────────────────────────────────┐
  PADDED:   │ counter_a (8B) │   padding       ││ counter_b (8B) │   padding       │
            └──────────────────────────────────┘└──────────────────────────────────┘
              Thread 1 writes                     Thread 2 writes
              ───────────────                     ───────────────
              Stays in Modified                   Stays in Modified
              state on this core                  state on this core
```

The result is that two threads doing completely independent work slow each other down
by 3-9x, simply because their data was allocated next to each other in memory.

## Quick Start

```bash
make all
bin/basic_demo                           # packed vs padded, 2 threads
bin/perf_counters                        # HW cache miss counters (needs root)
bin/scaling --csv > results/scaling.csv  # thread scaling data
bin/patterns                             # 4 real-world anti-patterns
```

## Test Environment

All results below were collected on:

| Parameter | Value |
|-----------|-------|
| CPU | Intel Core i5-13600K (14 cores, 20 threads) |
| Architecture | Alder Lake (P-cores + E-cores) |
| L1D Cache | 48K per P-core, 32K per E-core |
| L2 Cache | 1.25MB per P-core, 2MB shared per E-core cluster |
| L3 Cache | 20MB shared |
| Cache Line | 64 bytes |
| Kernel | Linux 6.8.0 |
| Compiler | gcc 11.4, `-O2 -pthread` |
| Iterations | 50-100M per thread (configurable via `ITERATIONS` env var) |

---

## Milestone 1: Basic False Sharing Demo (`basic_demo`)

### What It Does

Two threads each atomically increment their own counter in a tight loop. The counters
are either **packed** (adjacent, same cache line) or **padded** (separated, different
cache lines).

```c
// PACKED: both on same cache line (16 bytes total)
struct packed_counters {
    _Atomic long counter_a;  // offset 0
    _Atomic long counter_b;  // offset 8  ← same cache line!
};

// PADDED: each on its own cache line (72 bytes total)
struct padded_counters {
    _Atomic long counter_a;              // offset 0
    char _pad[CACHE_LINE_SIZE - 8];      // 56 bytes padding
    _Atomic long counter_b;              // offset 64  ← different cache line
};
```

Each thread runs `atomic_fetch_add` (compiles to `lock xadd` on x86) which requires
exclusive cache line ownership on every iteration — this is what makes false sharing
visible in the measurement.

### Results

```
Iterations per thread : 100,000,000
Cores used            : 0, 10

  Mode                  Time (ms)         Ops/sec
  PACKED (false share)     1330.3       150,342,879
  PADDED (no share)         361.1       553,872,649

  Slowdown: PACKED is 3.7x slower than PADDED
```

### Explanation

When packed, both counters sit in the same 64-byte cache line. Every time Thread 1
does `lock xadd` on `counter_a`, it needs the cache line in **Modified** (exclusive)
state. But Thread 2 just modified `counter_b` on the same line — so Thread 1's core
must send an invalidation, wait for the line from Thread 2's core, modify it, and only
then can the increment proceed. Thread 2 then does the same thing in reverse. This
ping-pong happens on **every single iteration**.

When padded, each counter is on its own cache line. Each core acquires its line in
Modified state once and holds it for the entire benchmark — no coherence traffic at all
after the first access.

The 3.7x ratio is specific to this CPU (Intel 13th gen). The ratio depends on:
- **Inter-core latency**: how far apart the cores are (same CCX vs cross-CCX vs cross-socket)
- **L3 cache behavior**: shared L3 can service coherence requests faster than going to memory
- **Number of threads**: more threads = more contention (see Milestone 3)

---

## Milestone 2: Hardware Counter Proof (`perf_counters`)

### What It Does

Uses the `perf_event_open()` syscall to read CPU hardware performance counters during
the packed and padded benchmarks. This proves the *cause* of the slowdown: cache
coherence traffic.

Counters measured:
- **Cache References** — total L3 cache lookups
- **Cache Misses** — L3 lookups that missed (had to go to memory/other core)
- **L1D Load Misses** — L1 data cache misses (triggers L2/L3 lookup)
- **LLC Load Misses** — last-level (L3) cache misses

### Results

```
Iterations per thread : 50,000,000
Cores                 : 0, 10

  Wall Clock Time:  PACKED = 646.8 ms    PADDED = 186.6 ms    3.5x slowdown
```

Hardware counters require elevated permissions. Run with:
```bash
sudo sysctl kernel.perf_event_paranoid=-1   # allow unprivileged access
# or
sudo bin/perf_counters                       # run as root
```

With counters enabled, you'll see:
- **Cache Misses**: dramatically higher for packed (10-100x)
- **L1D Load Misses**: every iteration is a miss in the packed case (the line was
  invalidated by the other core)
- **LLC Load Misses**: may or may not be high depending on whether the coherence
  protocol services the request from another core's cache (cache-to-cache transfer) or
  from memory

### Why the Counters Matter

The timing difference alone tells you *that* false sharing is happening. The counters
tell you *why*: every increment causes a cache miss because the other core invalidated
the line. In the padded case, the line stays in the local cache and all increments are
L1 hits (~1ns per access vs ~40-70ns for a cross-core transfer).

---

## Milestone 3: Thread Scaling Experiment (`scaling`)

### What It Does

Runs the increment benchmark with 1, 2, 4, 8, 16, and 20 threads in three modes:

- **padded** — each thread's counter on its own cache line (no contention)
- **packed** — all counters adjacent in a plain array (false sharing)
- **true_share** — all threads atomically increment a single shared counter (real contention)

### Results

```
threads  mode          ops/sec         time_ms
──────── ──────────── ─────────────── ────────
1        padded         276,413,675     180.9
1        packed         282,283,439     177.1
1        true_share     282,386,123     177.1
──────── ──────────── ─────────────── ────────
2        padded         247,430,510     404.2
2        packed         243,321,343     411.0
2        true_share     253,060,359     395.2
──────── ──────────── ─────────────── ────────
4        padded         494,093,356     404.8
4        packed         143,770,330   1,391.1
4        true_share     139,145,877   1,437.3
──────── ──────────── ─────────────── ────────
8        padded         988,388,310     202.3
8        packed         107,994,344   1,851.9
8        true_share     107,246,925   1,864.9
──────── ──────────── ─────────────── ────────
16       padded       1,980,744,254     101.0
16       packed         169,310,527   1,181.3
16       true_share      85,727,184   2,333.0
──────── ──────────── ─────────────── ────────
20       padded       2,466,666,760      81.1
20       packed         214,521,962     932.3
20       true_share      81,207,564   2,462.8
```

### Analysis

**Padded (green line in chart)** scales almost perfectly linearly:
- 1 thread: 276M ops/sec
- 20 threads: 2,467M ops/sec (8.9x speedup for 20 threads)

This is what you'd expect from truly independent work — each thread operates on its own
cache line with zero contention. The speedup is less than 20x because the i5-13600K has
a mix of P-cores and E-cores with different clock speeds.

**Packed (red line)** shows **negative scaling** — adding threads makes it *slower*:
- 1 thread: 282M ops/sec
- 4 threads: 144M ops/sec (2x SLOWER than 1 thread!)
- 8 threads: 108M ops/sec (2.6x slower than 1 thread)
- 20 threads: 215M ops/sec (recovers slightly — see note below)

This is the signature of false sharing. With 1 thread there's no contention. With 2+
threads, every core's `lock xadd` invalidates the cache line on all other cores. The
more cores participating, the more invalidation traffic, and the slower each individual
operation becomes.

The slight recovery at 16-20 threads is likely due to the Intel Alder Lake architecture:
at higher thread counts, some threads land on E-cores which share an L2 cache, making
coherence cheaper within that cluster.

**True share (blue line)** shows the worst scaling of all:
- 1 thread: 282M ops/sec
- 20 threads: 81M ops/sec (3.5x SLOWER than 1 thread)

This is real contention on a single atomic counter. Every thread's `lock xadd` must
serialize — only one core can hold the line in Modified state at a time. This is the
theoretical worst case and serves as a baseline showing that false sharing (packed) is
nearly as bad as true sharing, despite the threads operating on independent data.

### Key Takeaway

At 20 threads, the throughput ratio is:

| Mode | Ops/sec | vs Padded |
|------|---------|-----------|
| Padded | 2,467M | 1.0x (baseline) |
| Packed | 215M | **11.5x slower** |
| True share | 81M | **30.4x slower** |

False sharing makes independent work perform almost as poorly as truly contended work.

### Generating the Chart

```bash
bin/scaling --csv > results/scaling.csv
python3 scripts/plot_scaling.py results/scaling.csv -o results/scaling.png
```

---

## Milestone 4: Real-World Anti-Patterns (`patterns`)

### Pattern 1: Array of Per-Thread Counters

**The bug**: `long counters[NUM_THREADS]` where each thread increments `counters[thread_id]`.
Since `sizeof(long) == 8` and a cache line is 64 bytes, 8 counters fit on one cache line.
All 8 threads are writing to the same cache line.

**The fix**: Pad each element to 64 bytes.

```
  PACKED:  [c0|c1|c2|c3|c4|c5|c6|c7]  ← all 8 counters in ONE cache line
  PADDED:  [c0|pad][c1|pad][c2|pad]... ← each counter in its OWN cache line
```

```
  Packed (adjacent)       3499.6 ms     114,299,414 ops/sec
  Padded (separated)       404.0 ms     990,032,798 ops/sec
  Slowdown: 8.7x
```

This is the most dramatic result because all 8 threads contend on the same cache line
simultaneously. Each `lock xadd` invalidates the line for 7 other cores.

**Where this happens in real code**: per-thread statistics counters, per-CPU data
structures in the kernel, per-worker metrics in thread pools.

---

### Pattern 2: Producer-Consumer Flags

**The bug**: A producer and consumer thread communicate via adjacent fields in a struct:

```c
struct {
    _Atomic long producer_count;  // offset 0 — written by producer
    _Atomic long consumer_count;  // offset 8 — written by consumer
};
```

Both fields are on the same cache line despite being accessed by different threads.

**The fix**: Insert padding between producer and consumer fields.

```
  Packed (adjacent)        720.4 ms     138,821,032 ops/sec
  Padded (separated)       199.1 ms     502,260,443 ops/sec
  Slowdown: 3.6x
```

Only 2 threads involved (vs 8 in the array case), so the slowdown is less extreme but
still significant. In a real producer-consumer queue, this would add microseconds of
latency to every enqueue/dequeue operation.

**Where this happens in real code**: lock-free queues with adjacent head/tail pointers,
ring buffers with separate read/write positions, producer-consumer coordination flags.

---

### Pattern 3: Hash Table Bucket Locks

**The bug**: A hash table with per-bucket locks and counters:

```c
struct bucket_packed {
    _Atomic long lock;   // 8 bytes
    _Atomic long count;  // 8 bytes
};  // 16 bytes total — 4 buckets fit in one cache line
```

Threads operating on *different* buckets still contend because their bucket structs are
on the same cache line.

**The fix**: Pad each bucket to 64 bytes.

```
  sizeof(bucket_packed)  = 16 bytes  (4 buckets per cache line)
  sizeof(bucket_padded)  = 64 bytes  (1 bucket per cache line)

  Packed (adjacent)       6091.6 ms      65,664,679 ops/sec
  Padded (separated)      1390.2 ms     287,725,243 ops/sec
  Slowdown: 4.4x
```

Each iteration does 3 atomic operations (lock, count, unlock) per bucket, so the
absolute throughput is lower than Pattern 1. But the ratio still shows clear false
sharing — threads that are logically accessing independent buckets are physically
fighting over cache lines.

**Where this happens in real code**: concurrent hash maps, connection pools with
per-slot locks, any array of small locked structures.

---

### Pattern 4: Per-Thread Statistics Structs

**The bug**: Each thread maintains a small stats struct:

```c
struct stats_packed {
    _Atomic long requests;  // 8 bytes
    _Atomic long bytes;     // 8 bytes
    _Atomic long errors;    // 8 bytes
};  // 24 bytes — almost 3 structs fit in one cache line
```

Multiple threads' stat structs land on the same cache line.

**The fix**: Pad each struct to 64 bytes with `__attribute__((aligned(64)))`.

```
  sizeof(stats_packed)  = 24 bytes  (2.7 structs per cache line)
  sizeof(stats_padded)  = 64 bytes  (1 struct per cache line)

  Packed (adjacent)       3732.0 ms     107,182,567 ops/sec
  Padded (separated)       883.4 ms     452,800,816 ops/sec
  Slowdown: 4.2x
```

Each iteration updates 3 fields (requests, bytes, errors), so there are 3 atomic
operations per loop iteration. The packed version suffers because adjacent threads'
stat structs share cache lines.

**Where this happens in real code**: per-thread metric structs, per-connection
statistics, per-worker counters in web servers and databases.

---

## Summary of All Results

| Benchmark | Packed (ops/sec) | Padded (ops/sec) | Slowdown |
|-----------|----------------:|------------------:|---------:|
| basic_demo (2 threads) | 150M | 554M | **3.7x** |
| array_counters (8 threads) | 114M | 990M | **8.7x** |
| hash_buckets (8 threads) | 66M | 288M | **4.4x** |
| thread_stats (8 threads) | 107M | 453M | **4.2x** |
| producer_consumer (2 threads) | 139M | 502M | **3.6x** |
| scaling at 20 threads | 215M | 2,467M | **11.5x** |

---

## How to Fix False Sharing

### Option 1: Padding

```c
struct padded {
    long counter;
    char _pad[64 - sizeof(long)];  // explicit padding
};
```

### Option 2: Compiler Alignment

```c
struct padded {
    _Alignas(64) long counter_a;
    _Alignas(64) long counter_b;
};
```

### Option 3: GCC Attribute

```c
struct padded {
    long counter __attribute__((aligned(64)));
};
```

### Option 4: C++17 Portable

```cpp
#include <new>
struct padded {
    alignas(std::hardware_destructive_interference_size) long counter;
};
```

### When NOT to Fix

Padding trades **memory** for **performance**. A padded 8-byte counter uses 64 bytes —
an 8x increase. For a fixed number of threads this is negligible. For large arrays
(millions of elements), the cache pressure from the padding itself may hurt performance.
Profile before padding everything.

---

## How to Detect False Sharing in Production

### `perf c2c` (Cache-to-Cache)

The purpose-built tool for finding false sharing:

```bash
perf c2c record -a -- sleep 10        # record for 10 seconds
perf c2c report --stdio               # show contested cache lines
```

Look for lines with high `HITM` (Hit in Modified) counts — these are cache lines being
bounced between cores.

### `perf stat` Quick Check

```bash
perf stat -e cache-misses,cache-references,L1-dcache-load-misses \
    -p <pid> sleep 5
```

High `cache-misses` relative to the workload, combined with high CPU utilization but
low throughput, is a classic false sharing signal.

### Signs of False Sharing

- Adding threads makes performance **worse** (negative scaling)
- CPU utilization is high but throughput is low
- `perf top` shows time in memory access instructions
- `perf c2c` shows high HITM on specific cache lines
- Performance is sensitive to struct layout changes

---

## Configuration

```bash
ITERATIONS=1000000000 bin/basic_demo   # more iterations for stable results
ITERATIONS=50000000   bin/scaling      # fewer for faster runs

bin/scaling --csv                      # CSV output for plotting
bin/scaling --threads 1,2,4,8,16,32    # custom thread counts
bin/patterns array_counters            # run a single pattern
```

## Project Structure

```
02-cache-line-false-sharing/
├── Makefile                 # make all / make clean / make run
├── README.md                # this file
├── src/
│   ├── common.h             # timing, thread pinning, cache line macros
│   ├── basic_demo.c         # Milestone 1: 2-thread packed vs padded
│   ├── perf_counters.c      # Milestone 2: HW counter instrumentation
│   ├── scaling.c            # Milestone 3: throughput vs thread count
│   └── patterns.c           # Milestone 4: 4 real-world anti-patterns
├── scripts/
│   ├── run_all.sh           # build and run all benchmarks
│   ├── perf_compare.sh      # run under perf stat
│   └── plot_scaling.py      # generate scaling charts (needs matplotlib)
└── results/
    └── scaling.csv          # generated by: bin/scaling --csv
```

## References

- Brendan Gregg, "Systems Performance" 2nd Ed, Chapter 6 (CPUs)
- Intel 64 and IA-32 Optimization Reference Manual, Chapter on Cache Coherence
- Herb Sutter, "Eliminate False Sharing"
- Scott Meyers, "CPU Caches and Why You Care" (talk)
- `perf c2c` documentation: `man perf-c2c`
