# Project 04: Memory Allocator Benchmark Suite

## Goal

Build a comprehensive benchmark suite that compares memory allocators (`malloc/glibc`, `jemalloc`, `tcmalloc`, `mimalloc`) under different workload patterns, measuring throughput, latency, fragmentation, and scalability.

## What You'll Learn

- How memory allocators work (free lists, arenas, thread caches, size classes)
- Memory fragmentation (internal vs external) and its real-world impact
- Multithreaded allocation scalability and contention
- RSS vs VSZ vs heap fragmentation metrics
- `LD_PRELOAD` for swapping allocators without recompilation

## Background

The default glibc `malloc` (ptmalloc2) is a general-purpose allocator that makes tradeoffs. Specialized allocators like jemalloc (used by Facebook/Meta, Redis, Rust), tcmalloc (Google), and mimalloc (Microsoft) optimize for different scenarios. Choosing the right allocator — or understanding why your app is slow due to allocation patterns — is a critical performance engineering skill.

## Prerequisites

- C or C++
- Linux with `perf` installed
- Install allocators: `jemalloc`, `tcmalloc`, `mimalloc` (dev packages)
- Python or gnuplot for visualization

## Milestones

### Milestone 1: Micro-Benchmark Harness

**Build the benchmarking framework.**

- Design workload types:
  - **Small allocs:** millions of 8-64 byte allocations
  - **Medium allocs:** thousands of 1KB-64KB allocations
  - **Large allocs:** hundreds of 1MB+ allocations
  - **Mixed sizes:** realistic distribution (log-normal or based on real app traces)
  - **Alloc/free churn:** allocate N, free random subset, allocate more (stress fragmentation)
- Measure:
  - Operations per second (throughput)
  - Per-operation latency (min, p50, p95, p99, max)
  - Peak RSS (`/proc/self/status` → VmRSS)
  - Heap fragmentation ratio (RSS / live bytes)
- Use `LD_PRELOAD` to swap allocators:
  ```
  LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so ./benchmark
  ```

**Deliverable:** Benchmark binary + runner script that tests all allocators across all workloads.

### Milestone 2: Multithreaded Scalability

**Measure how allocators scale with threads.**

- Workloads:
  - **Producer-consumer:** thread A allocates, thread B frees (cross-thread free)
  - **Thread-local:** each thread allocs and frees its own memory
  - **Shared pool:** all threads allocate from and free to a shared data structure
- Scale from 1 to N threads (where N = 2x core count)
- Plot throughput vs thread count for each allocator
- Measure lock contention using `perf lock` or `futex` tracepoints

**Deliverable:** Scaling charts showing which allocator handles contention best.

### Milestone 3: Fragmentation Deep-Dive

**Deliberately create fragmentation and measure its impact.**

- **Fragmentation workload:**
  1. Allocate 1M objects of varying sizes
  2. Free every other object (create holes)
  3. Try to allocate objects of different sizes (larger than holes)
  4. Measure: can the allocator reuse the holes? What's the RSS overhead?
- Track RSS over time during the workload
- Compare fragmentation ratios across allocators
- Use `malloc_info()` (glibc) or allocator-specific introspection APIs

**Deliverable:** Time-series plots of RSS and fragmentation ratio per allocator.

### Milestone 4: Real Application Benchmarks

**Test with realistic workloads, not just micro-benchmarks.**

- **Web server simulation:** allocate request buffers, parse headers (many small strings), build response, free all
- **Key-value store simulation:** random inserts/deletes of variable-size values
- **JSON parser simulation:** tree of small allocations with varying lifetimes
- Record allocation traces from a real application using:
  - `LD_PRELOAD` wrapper that logs `malloc`/`free` calls
  - Or `ltrace -e malloc+free`
- Replay traces against each allocator

**Deliverable:** Realistic workload benchmarks + trace replay tool.

### Milestone 5: Allocator Internals Visualization

**Visualize what each allocator is doing internally.**

- For each allocator, capture:
  - Arena/heap layout via `mmap` tracing (`strace -e mmap,munmap,brk`)
  - Size class distributions
  - Thread cache hit rates (jemalloc: `malloc_stats_print`, tcmalloc: `MallocExtension`)
- Build a visualization showing:
  - Memory map of the heap (colored by allocation state)
  - Size class bucket fill levels
  - Cross-thread free queue depths

**Deliverable:** Visual report explaining why each allocator performs differently.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Thread caches (tcache) | Avoid lock contention on hot paths |
| Size classes / slabs | Reduce fragmentation by bucketing similar sizes |
| Arena-per-thread | jemalloc/tcmalloc strategy for scalability |
| `mmap` vs `brk` | Large allocs use mmap (returnable to OS); small use brk/sbrk |
| `MADV_DONTNEED` / `MADV_FREE` | How allocators return memory to the OS |
| RSS vs heap size | Allocator may hold memory the app has freed |

## Validation & Benchmarks

- Verify results are reproducible (run each benchmark 5+ times, report variance)
- Pin threads to cores to reduce noise (`taskset` or `pthread_setaffinity`)
- Disable turbo boost and frequency scaling for consistent results
- Cross-validate RSS measurements with `/proc/self/smaps_rollup`
- Use `perf stat` to count `page-faults`, `cache-misses` alongside throughput

## References

- jemalloc paper: "A Scalable Concurrent malloc Implementation for FreeBSD"
- tcmalloc design doc: https://google.github.io/tcmalloc/design.html
- mimalloc paper: "Mimalloc: Free List Sharding in Action"
- Brendan Gregg's "Systems Performance" Chapter 7 (Memory)
- glibc malloc internals: https://sourceware.org/glibc/wiki/MallocInternals
