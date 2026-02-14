# Project 01: Build a Flame Graph Generator from Scratch

## Goal

Build a profiler that samples stack traces from a running process and renders interactive SVG flame graphs — understanding how profiling works at the syscall level rather than treating tools like `perf` as black boxes.

## What You'll Learn

- How sampling profilers work (`perf_event_open()` syscall, signal-based sampling)
- Stack unwinding (frame pointers, DWARF, ORC)
- The flame graph algorithm (stack folding, width = frequency)
- SVG generation and interactivity
- Sampling bias, skid, and observer effect

## Background

Brendan Gregg invented flame graphs in 2011. They collapse sampled stack traces into a visualization where:
- The x-axis is the **population of stack traces** (sorted alphabetically, NOT time)
- The y-axis is **stack depth**
- Width of each box = proportion of samples that include that function

Most people use `perf record` + Gregg's `flamegraph.pl` scripts. This project builds both pieces from scratch.

## Prerequisites

- C or Rust (for the profiler/sampler)
- Python or Go (for the SVG renderer, or do it all in one language)
- Linux with `perf_event_open` support
- A target workload to profile (even a simple CPU-bound program)

## Milestones

### Milestone 1: Signal-Based Self-Profiler

**Build a basic profiler that samples its own stack traces.**

- Set up a `SIGPROF` or `ITIMER_PROF` timer
- In the signal handler, walk the stack using `__builtin_return_address()` or `backtrace()`
- Store stack traces in a ring buffer
- Dump folded stacks to stdout (format: `func_a;func_b;func_c 42`)

**Deliverable:** A program that outputs folded stack traces of itself.

### Milestone 2: External Process Profiling with `perf_event_open`

**Profile an external process using the Linux perf subsystem.**

- Use `perf_event_open()` with `PERF_SAMPLE_CALLCHAIN`
- Set up sampling on CPU cycles or timer events
- `mmap` the ring buffer to read samples
- Parse `perf_event_header` structures to extract stack frames
- Resolve addresses to symbols using `/proc/[pid]/maps` + ELF symbol tables

**Deliverable:** A CLI tool (`./profiler -p <pid> -d <seconds>`) that outputs folded stacks for any process.

### Milestone 3: SVG Flame Graph Renderer

**Convert folded stacks into an interactive SVG flame graph.**

- Parse folded stack format (`func_a;func_b;func_c count`)
- Build a tree structure from stacks
- Calculate widths proportional to sample count
- Render SVG rectangles with:
  - Color coding (random warm palette, or by module)
  - Text labels (hidden when box too narrow)
  - Mouseover tooltips showing function name + sample count + percentage
- Add JavaScript for zoom-on-click and search

**Deliverable:** An SVG file you can open in a browser with full interactivity.

### Milestone 4: Differential Flame Graphs

**Compare two profiles to show what changed.**

- Take two folded stack files as input
- Compute per-frame deltas (increase = red, decrease = blue)
- Render differential flame graph
- Useful for A/B performance comparison (before/after a code change)

**Deliverable:** `./flamediff before.folded after.folded -o diff.svg`

### Milestone 5: Off-CPU Flame Graphs (Stretch)

**Capture where threads spend time blocked (sleeping, waiting on I/O, locks).**

- Use eBPF or ftrace to hook `sched_switch` events
- Record stack traces at the point threads go off-CPU
- Calculate off-CPU duration per stack
- Render as a separate flame graph

**Deliverable:** Combined on-CPU + off-CPU flame graphs for a target process.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Sampling rate vs overhead | Too frequent = perturbs app; too infrequent = misses short functions |
| Frame pointers (`-fno-omit-frame-pointer`) | Many distros strip these; without them stack walking breaks |
| Kernel vs user stacks | `perf_event_open` can capture both; need to handle the boundary |
| Symbol resolution | Addresses mean nothing without mapping to function names |
| Folded stack format | Gregg's simple text format is the interchange standard |

## Validation & Benchmarks

- Profile a known CPU-bound workload (matrix multiply, hash computation) and verify the hot function dominates the flame graph
- Compare your output against `perf record -g` + `flamegraph.pl` for the same workload
- Measure your profiler's overhead (CPU time consumed by profiling itself)

## References

- Brendan Gregg's flame graph page: https://www.brendangregg.com/flamegraphs.html
- `perf_event_open(2)` man page
- Gregg's original `stackcollapse-perf.pl` and `flamegraph.pl` scripts
- "BPF Performance Tools" Chapter 2 (profiling methodology)
