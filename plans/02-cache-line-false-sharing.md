# Project 02: CPU Cache-Line Bouncing Demonstrator

## Goal

Build a benchmarking suite that demonstrates false sharing — where independent variables on the same cache line cause massive performance degradation in multithreaded code — and teaches you to use hardware performance counters to diagnose it.

## What You'll Learn

- CPU cache hierarchy (L1/L2/L3, cache lines, coherence protocols)
- False sharing and its real-world performance impact
- Using `perf stat` hardware counters to prove cache effects
- Data structure alignment and padding strategies
- MESI/MOESI cache coherence protocols

## Background

Modern CPUs operate on 64-byte cache lines. When two threads write to different variables that happen to sit on the same cache line, the coherence protocol bounces that line between cores. This can cause 10-50x slowdowns for what looks like embarrassingly parallel code.

This is one of the most common and least understood performance problems in concurrent systems.

## Prerequisites

- C or C++ (need control over memory layout)
- Linux with `perf` installed
- Multi-core machine (at minimum 4 cores)
- Basic understanding of pthreads or C++ threads

## Milestones

### Milestone 1: Basic False Sharing Demo

**Show the problem with a minimal example.**

```
struct shared_data {
    long counter_a;  // Thread 1 increments this
    long counter_b;  // Thread 2 increments this
};
```

- Two threads each increment their own counter in a tight loop
- Both counters sit on the same 64-byte cache line
- Benchmark: measure iterations per second
- Compare against padded version:

```
struct padded_data {
    long counter_a;
    char pad[64 - sizeof(long)];
    long counter_b;
};
```

**Deliverable:** Side-by-side benchmark showing the slowdown (expect 5-20x difference).

### Milestone 2: Hardware Counter Instrumentation

**Prove the cause using `perf stat` counters.**

- Run both versions under `perf stat` with counters:
  - `cache-misses`
  - `cache-references`
  - `L1-dcache-load-misses`
  - `LLC-load-misses`
  - `bus-cycles`
- Build a wrapper that runs the benchmark and captures counters programmatically
- Display a comparison table

**Deliverable:** Automated benchmark that shows counter values for false-sharing vs padded versions.

### Milestone 3: Scaling Experiment

**Measure how false sharing scales with core count.**

- Run the benchmark with 1, 2, 4, 8, ... N threads
- Plot throughput vs thread count for:
  - False sharing (all counters packed)
  - Padded (each counter on its own cache line)
  - True sharing (threads actually sharing the same counter with atomics)
- Show that false sharing gets *worse* with more cores (negative scaling)

**Deliverable:** Chart showing scaling curves for all three cases.

### Milestone 4: Real-World Patterns

**Demonstrate false sharing in realistic data structures.**

- **Array of counters:** `long counters[NUM_THREADS]` — each thread updates `counters[thread_id]`, classic false sharing
- **Struct of flags:** Producer-consumer with adjacent `ready`/`done` flags
- **Hash table buckets:** Adjacent bucket locks causing contention
- **Thread-local statistics:** Per-thread stat structs that are too small

For each pattern:
- Show the slow version
- Show the fix (padding, `alignas(64)`, `__attribute__((aligned(64)))`)
- Benchmark both

**Deliverable:** A catalog of false-sharing anti-patterns with benchmarks.

### Milestone 5: Detection Tool

**Build a tool that detects potential false sharing.**

- Use `perf c2c` (cache-to-cache) to identify contested cache lines
- Or use `perf mem` to record memory access samples
- Parse the output to identify:
  - Which cache lines have the most cross-core traffic
  - Which data structures sit on those lines
  - Which threads are involved
- Produce a report with fix suggestions

**Deliverable:** `./detect-false-sharing -p <pid>` that flags likely false sharing.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Cache line size (typically 64 bytes) | The fundamental unit of cache coherence |
| MESI protocol | How cores coordinate cache line ownership |
| `perf c2c` | Purpose-built tool for detecting cache line contention |
| `alignas()` / `__attribute__((aligned))` | Language-level controls for data alignment |
| `std::hardware_destructive_interference_size` | C++17 portable cache line size constant |
| Padding vs separation | Trade memory for performance |

## Validation & Benchmarks

- Verify cache miss counts are dramatically higher in false-sharing case
- Confirm that padding eliminates the performance difference
- Test on different architectures if possible (Intel vs AMD — cache line sizes and coherence protocols differ slightly)
- Use `lstopo` (hwloc) to visualize your machine's cache topology

## References

- Brendan Gregg's "Systems Performance" Chapter 6 (CPUs)
- Intel optimization manual, Section on cache coherence
- `perf c2c` documentation
- Herb Sutter's "Eliminate False Sharing" article
- Scott Meyers' talk "CPU Caches and Why You Care"
