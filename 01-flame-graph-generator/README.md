# 01 — Flame Graph Generator

A from-scratch CPU profiler and flame graph renderer in C and Python, implementing the full pipeline from sampling to interactive SVG visualization.

## Overview

This project builds the complete stack-sampling → flame-graph pipeline:

1. **Self-Profiler** (Milestone 1) — profiles its own execution using `ITIMER_PROF` + `SIGPROF` + `backtrace()`
2. **External Profiler** (Milestone 2) — profiles any running process via `perf_event_open()` syscall
3. **SVG Flame Graph** (Milestone 3) — renders folded stacks as interactive SVG flame graphs (C and Python implementations)
4. **Differential Flame Graph** (Milestone 4) — compares two profiles to highlight regressions and improvements

All components communicate via Brendan Gregg's **folded stack format**:
```
func_a;func_b;func_c 42
func_a;func_b;func_d 17
```

## Architecture

```
┌──────────────┐     folded stacks     ┌──────────────┐
│ selfprofile  │ ──────────────────────>│  flamegraph  │──> SVG
│ (SIGPROF)    │     (stdout)           │  (C or Py)   │
└──────────────┘                        └──────────────┘

┌──────────────┐     folded stacks     ┌──────────────┐
│  profiler    │ ──────────────────────>│  flamegraph  │──> SVG
│ (perf_event) │     (stdout)           │  (C or Py)   │
└──────────────┘                        └──────────────┘

┌──────────┐ ┌──────────┐              ┌──────────────┐
│ before.  │ │ after.   │ ────────────>│  flamediff   │──> diff SVG
│ folded   │ │ folded   │              │  (Python)    │
└──────────┘ └──────────┘              └──────────────┘
```

## Building

```bash
make all        # builds: selfprofile, profiler, flamegraph, workload
```

Requirements:
- GCC with `-rdynamic`, `-ldl` support
- Linux kernel with `perf_event_open` (for external profiler)
- Python 3 (for Python renderer and differential graphs)
- `addr2line` (from binutils, for external profiler symbol resolution)

## Usage

### Milestone 1: Self-Profiler

The self-profiler runs a built-in CPU workload and profiles it using signal-based sampling:

```bash
# Print folded stacks to stdout
./bin/selfprofile

# Pipe directly to flame graph renderer
./bin/selfprofile | ./bin/flamegraph -t "Self-Profile" -o results/self.svg
```

The built-in workload has three functions with intentionally different CPU weights:
- `compute_hot` — tight FP loop, ~70% of CPU time
- `compute_medium` — lighter loop, ~20%
- `compute_cold` — lightest loop, ~10%

**Example output:**
```
_start;__libc_start_main;main;run_workload;compute_cold 20
_start;__libc_start_main;main;run_workload;compute_hot 142
_start;__libc_start_main;main;run_workload;compute_medium 5
```

### Milestone 2: External Process Profiler

Profile any running process by PID:

```bash
# Start the test workload
./bin/workload 10 &    # runs for 10 seconds

# Profile it
./bin/profiler -p $(pgrep workload) -d 3 -f 99 | ./bin/flamegraph \
    -t "Workload Profile" -o results/workload.svg
```

Options:
- `-p PID` — process to profile (required)
- `-d SECONDS` — duration (default: 5)
- `-f FREQ` — sampling frequency in Hz (default: 99)
- `-o FILE` — output file (default: stdout)

**Note:** Requires `perf_event_paranoid <= 1` or root:
```bash
sudo sysctl kernel.perf_event_paranoid=-1
```

### Milestone 3: SVG Flame Graph Renderer

Two implementations — a fast C version and a feature-rich Python version:

```bash
# C renderer (fast, minimal)
./bin/flamegraph [-t title] [-w width] [-i infile] [-o outfile]

# Python renderer (interactive: zoom, search, reset button)
python3 scripts/flamegraph.py [-t title] [-w width] [-i infile] [-o outfile]
```

Both read folded stacks from stdin (or `-i file`) and write SVG to stdout (or `-o file`).

**Interactive features (in browser):**
- Hover over frames for tooltip (function name, sample count, percentage)
- Ctrl+F to search by function name (dims non-matching frames)
- Escape to reset view
- Click to focus on a function (Python version)
- [Reset] button (Python version)

### Milestone 4: Differential Flame Graph

Compare two profiles to see what changed:

```bash
python3 scripts/flamediff.py before.folded after.folded -o results/diff.svg
```

Color coding:
- **Red** — regression (function uses more CPU in "after")
- **Blue** — improvement (function uses less CPU in "after")
- **Gray** — unchanged

## Quick Demo

```bash
# Build everything
make all

# Full self-profiler pipeline
make demo

# Or step by step:
./bin/selfprofile | ./bin/flamegraph -t "My Profile" -o results/self.svg

# Open in browser
xdg-open results/self.svg   # or: open results/self.svg (macOS)
```

## How It Works

### Signal-Based Self-Profiling (Milestone 1)

1. Set `ITIMER_PROF` timer at 997 Hz (prime to avoid aliasing)
2. `SIGPROF` handler calls `backtrace()` — stores raw frame pointers in a pre-allocated array (signal-safe)
3. After workload completes, resolve addresses via `dladdr()` (needs `-rdynamic`)
4. Build folded stack strings, sort, merge duplicates, output counts

### perf_event_open Profiling (Milestone 2)

1. Open a perf event fd with `PERF_TYPE_SOFTWARE` / `PERF_COUNT_SW_CPU_CLOCK` + `PERF_SAMPLE_CALLCHAIN`
2. `mmap()` a ring buffer for the kernel to write samples into
3. `poll()` the fd, parse `perf_event_header` records to extract callchain IPs
4. After profiling, resolve addresses via `/proc/<pid>/maps` + `addr2line`
5. Symbol cache (open-addressing hash map) avoids repeated `addr2line` invocations

### SVG Rendering (Milestone 3)

1. Parse folded stacks into a frame tree (trie)
2. Sort children alphabetically for consistent layout
3. Compute widths proportional to sample count (children divide parent's width)
4. Generate SVG rectangles with:
   - Warm color palette (HSV hash of function name → red/orange/yellow)
   - Text labels (truncated or hidden when frame is narrow)
   - `<title>` elements for browser tooltip
   - Embedded JavaScript for interactivity

### Differential Rendering (Milestone 4)

1. Parse both profiles into `{stack_string: count}` maps
2. Build a merged tree with both `count_a` and `count_b` per node
3. Normalize counts to rates (percentage of total) for fair comparison
4. Map rate delta to color: positive delta → red gradient, negative → blue gradient

## File Structure

```
01-flame-graph-generator/
├── Makefile
├── README.md
├── src/
│   ├── selfprofile.c      # M1: SIGPROF self-profiler
│   ├── profiler.c          # M2: perf_event_open external profiler
│   ├── symbols.c           # Symbol resolution (/proc/pid/maps + addr2line)
│   ├── symbols.h
│   └── flamegraph.c        # M3: folded stacks → SVG (C)
├── scripts/
│   ├── flamegraph.py       # M3: folded stacks → SVG (Python, more interactive)
│   └── flamediff.py        # M4: differential flame graph
├── samples/
│   └── workload.c          # CPU-bound test workload (hot/medium/cold)
└── results/                # Output directory for generated SVGs
```

## Key Concepts

- **Folded stacks** — Brendan Gregg's text interchange format: one stack per line, semicolon-delimited, sample count at the end
- **Frame pointer unwinding** — `-fno-omit-frame-pointer` preserves the frame chain so `backtrace()` can walk the stack
- **`-rdynamic`** — exports all symbols to the dynamic symbol table so `dladdr()` can resolve them
- **Ring buffer** — kernel writes perf samples into an `mmap()`'d circular buffer; userspace reads behind the write head
- **HSV color hashing** — deterministic warm palette from function name ensures the same function always gets the same color
