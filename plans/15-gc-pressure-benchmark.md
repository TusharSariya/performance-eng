# Project 15: GC Pressure Benchmark

## Goal

Build benchmark programs in Java, Go, and Python that stress garbage collection in different ways, then instrument and correlate GC pauses with application-level latency using GC logs, runtime metrics, and eBPF.

## What You'll Learn

- How garbage collectors work (mark-sweep, generational, concurrent, compacting)
- GC tuning for each runtime (JVM flags, GOGC, Python gc module)
- Correlating GC pauses with application latency spikes
- Using eBPF to observe GC from outside the runtime
- Object allocation patterns that trigger worst-case GC behavior

## Background

Garbage collection is the #1 source of latency spikes in managed-language applications. A GC pause of 50ms during request processing means a user waits 50ms with no progress. Worse, GC pauses are often correlated — all instances pause around the same time under similar load.

Understanding GC behavior lets you:
- Choose the right collector for your workload
- Tune GC parameters to minimize pauses
- Design data structures that are GC-friendly
- Know when to switch languages for latency-critical paths

## Prerequisites

- Java 17+ (for modern collectors: G1, ZGC, Shenandoah)
- Go 1.19+ (for the GOGC pacer and arena experiment)
- Python 3.10+ (for GC inspection)
- C with libbpf (for eBPF instrumentation)
- Grafana or plotting tool for visualization

## Milestones

### Milestone 1: GC Stress Workloads

**Build workloads that stress GC in different ways.**

- **High allocation rate:** allocate millions of short-lived objects per second (simulates request handling)
- **Long-lived object accumulation:** build a growing cache/map that GC must scan but rarely collect
- **Fragmentation:** allocate objects of varying sizes, free randomly, force compaction
- **Finalizers/weak references:** objects with cleanup code that delay collection
- **Large object allocation:** allocate objects larger than typical generation thresholds
- **Reference chains:** deep object graphs that stress the marking phase

Implement each workload in Java, Go, and Python with identical logical behavior.

**Deliverable:** Benchmark suite with 6 workload types in 3 languages.

### Milestone 2: GC Log Analysis (Java)

**Deep-dive into JVM GC behavior.**

- Test collectors:
  - **G1** (default in modern JDKs): `-XX:+UseG1GC`
  - **ZGC**: `-XX:+UseZGC` (concurrent, sub-ms pauses)
  - **Shenandoah**: `-XX:+UseShenandoahGC` (concurrent compaction)
  - **Parallel GC**: `-XX:+UseParallelGC` (throughput optimized)
- Enable detailed GC logging:
  ```
  -Xlog:gc*:file=gc.log:time,uptime,level,tags
  ```
- Parse GC logs to extract:
  - Pause times (min, avg, p99, max)
  - Pause frequency
  - Heap occupancy before/after collection
  - Time spent in each GC phase (marking, evacuation, cleanup)
  - Promotion rate (young → old generation)
- Correlate with application metrics (request latency)

**Deliverable:** GC analysis report comparing collectors across workloads.

### Milestone 3: Go GC Tuning

**Explore Go's GC behavior and tuning.**

- Key controls:
  - `GOGC` — GC target ratio (default 100; higher = less frequent, more memory)
  - `GOMEMLIMIT` — soft memory limit (Go 1.19+)
  - `debug.SetGCPercent()` — runtime tuning
- Collect metrics:
  - `runtime.ReadMemStats()` — pause times, allocation rate, heap sizes
  - `runtime/trace` — execution trace with GC events
  - `GODEBUG=gctrace=1` — per-GC-cycle log line
- Experiments:
  - Sweep GOGC from 10 to 1000: plot pause time vs memory usage
  - Use `GOMEMLIMIT` to cap memory and observe GC frequency increase
  - Compare with `arena` package (manual memory management, experimental)
  - Object pooling (`sync.Pool`) vs fresh allocation

**Deliverable:** GOGC tuning guide with throughput/latency/memory tradeoff charts.

### Milestone 4: eBPF GC Observation

**Observe GC pauses from outside the runtime using eBPF.**

- Approach 1: USDT probes
  - Java: `-XX:+ExtendedDTraceProbes` exposes `hotspot:gc__begin`/`gc__end`
  - Go: no built-in USDT, but can add with `//go:uretprobe`
  - Attach with eBPF and measure pause durations
- Approach 2: Thread state tracking
  - Hook `sched_switch` to detect when all application threads are stopped simultaneously (STW pause)
  - Duration of the "all-stopped" window = GC pause
- Approach 3: Allocation tracking
  - Hook `mmap`/`brk` to track heap growth
  - Hook `madvise(MADV_DONTNEED)` to track memory return
  - Correlate with GC activity
- Combine: show GC events alongside system-level metrics (CPU, memory, scheduling)

**Deliverable:** eBPF tool that detects and measures GC pauses for JVM and Go processes.

### Milestone 5: Latency Correlation Dashboard

**Build a dashboard correlating GC activity with application latency.**

- Run a simulated web service (HTTP server with database-like workload)
- Measure simultaneously:
  - Request latency (p50, p99, p999) per second
  - GC pause times and durations
  - Heap occupancy
  - Allocation rate
  - CPU utilization
- Visualize:
  - Time-aligned charts showing GC pauses causing latency spikes
  - Scatter plot: request latency vs "was GC happening during this request?"
  - Histogram: request latency with/without concurrent GC
- Demonstrate:
  - Tuning that reduces p99 latency by reducing GC pauses
  - The memory/latency tradeoff (more heap = fewer pauses)

**Deliverable:** Grafana dashboard or terminal UI showing GC-latency correlation.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Stop-the-world (STW) vs concurrent | STW pauses all threads; concurrent GC works alongside the app |
| Generational hypothesis | Most objects die young; young gen GC is fast |
| Write barriers | Allow concurrent GC to track mutations during marking |
| Allocation rate | Primary driver of GC frequency; reducing it reduces pauses |
| Object pooling | Reuse objects instead of allocating/collecting; reduces GC pressure |
| Off-heap storage | Store large data outside GC-managed heap (ByteBuffer, unsafe) |

## Validation & Benchmarks

- Verify GC pause times match between GC logs and eBPF measurements
- Use `jstat -gc` (Java), `runtime.ReadMemStats` (Go) for cross-validation
- Run each benchmark for 5+ minutes to capture steady-state behavior
- Test with realistic heap sizes (4GB+) — small heaps don't reveal real GC issues
- Use `perf stat` to measure CPU time spent in GC vs application code

## References

- Brendan Gregg's "Systems Performance" Chapter 5 (Applications)
- "BPF Performance Tools" Chapter 12 (Languages — JVM, Go)
- Java GC tuning guide: Oracle HotSpot documentation
- Go GC guide: https://tip.golang.org/doc/gc-guide
- Gil Tene: "Understanding Latency" talks (Azul Systems)
- "Java Performance" by Scott Oaks
