# Project 05: Page Fault Tracer

## Goal

Build a tool that traces minor and major page faults for a target process, visualizes the working set size over time, and reveals how virtual memory actually behaves under real workloads.

## What You'll Learn

- Virtual memory mechanics (page tables, TLB, demand paging)
- Minor faults (page in memory but not mapped) vs major faults (page on disk)
- Working set estimation techniques
- `userfaultfd` for userspace fault handling
- eBPF tracepoints for memory events

## Background

Every process has a virtual address space, but physical memory is allocated lazily. When a process touches a page for the first time, a **minor fault** occurs (kernel allocates a physical page). When a page has been swapped out and must be read from disk, a **major fault** occurs (orders of magnitude slower).

Understanding page fault patterns reveals:
- Actual memory usage patterns (not just RSS)
- Whether an application's working set fits in RAM
- Memory access locality (sequential vs random)

## Prerequisites

- C with libbpf or bpftrace
- Linux kernel 5.x+ with BTF
- Understanding of virtual memory basics

## Milestones

### Milestone 1: Page Fault Counter with eBPF

**Count page faults per process in real time.**

- Attach to tracepoints:
  - `exceptions:page_fault_user` (or `mm:mm_page_fault` depending on kernel)
  - Use kprobes on `handle_mm_fault` as fallback
- Record per-PID:
  - Minor fault count
  - Major fault count
  - Fault rate (faults/sec)
- Output interval-based summary

**Deliverable:** `./pgfaultstat -p <pid> 1` that prints fault rates every second.

### Milestone 2: Fault Address Tracking

**Record which virtual addresses are faulting.**

- Capture the faulting virtual address from the tracepoint
- Build a heat map of faulted address ranges
- Group by VMA (virtual memory area) using `/proc/[pid]/maps`
  - Identify: is it heap? stack? mmap'd file? anonymous?
- Track temporal patterns:
  - Sequential access (streaming)?
  - Random access (pointer chasing)?
  - Repeated faults on the same regions (thrashing)?

**Deliverable:** Report showing which memory regions fault most and access patterns.

### Milestone 3: Working Set Size Estimator

**Estimate the process's working set over time.**

- **Approach 1 — Idle page tracking:**
  - Write to `/sys/kernel/mm/page_idle/bitmap` to mark all pages idle
  - Wait an interval
  - Read bitmap to see which pages were accessed
  - Working set = count of non-idle pages × page size
- **Approach 2 — Referenced bit clearing:**
  - Use `/proc/[pid]/clear_refs` to reset reference bits
  - Read `/proc/[pid]/smaps` to count referenced pages after interval
- **Approach 3 — Page fault injection (WSS tool):**
  - Periodically soft-fault all pages (remove present bit)
  - Count how many fault back in during the interval
- Track working set size over time

**Deliverable:** Time-series plot of working set size for a target process.

### Milestone 4: `userfaultfd` Interceptor

**Use userfaultfd to intercept and handle page faults in userspace.**

- Register a memory region with `userfaultfd`
- Handle faults in a dedicated thread:
  - Log every fault (address, timestamp, thread)
  - Provide the page (copy or zero-fill)
  - Measure fault handling latency
- Use this to build:
  - A memory access recorder
  - A lazy-loading simulation (populate pages on demand from a file or network)

**Deliverable:** A library that wraps `userfaultfd` and records access traces.

### Milestone 5: THP and NUMA Fault Analysis (Stretch)

**Analyze transparent huge page and NUMA faults.**

- Track THP (Transparent Huge Pages) events:
  - `thp:thp_fault_alloc` — huge page allocated
  - `thp:thp_fault_fallback` — fell back to regular pages
  - `thp:thp_collapse` — pages merged into huge page
- Track NUMA balancing faults:
  - `migrate:mm_migrate_pages` — pages migrated between NUMA nodes
  - Identify NUMA-remote access patterns
- Correlate with application performance

**Deliverable:** Report showing THP effectiveness and NUMA migration patterns.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Demand paging | Memory isn't allocated until touched — `malloc` != physical memory |
| Minor vs major faults | Minor = microseconds; major = milliseconds (1000x difference) |
| Working set | The set of pages actively used; if > RAM, thrashing occurs |
| TLB misses | Page table lookups are cached in TLB; misses are expensive |
| Copy-on-write (COW) | `fork()` shares pages; first write triggers a minor fault |
| `madvise` hints | Applications can hint to the kernel about access patterns |

## Validation & Benchmarks

- Verify fault counts match `/proc/[pid]/stat` fields (minflt, majflt)
- Create a controlled workload:
  - `mmap` a large file, read sequentially → should see sequential minor faults
  - `mmap` a large file, read randomly → should see scattered faults
  - Allocate more memory than RAM, touch it all → should see major faults (swap)
- Compare working set estimates against RSS from `/proc/[pid]/status`

## References

- Brendan Gregg's "Systems Performance" Chapter 7 (Memory)
- "BPF Performance Tools" Chapter 7 (Memory)
- Brendan Gregg's WSS tool: http://www.brendangregg.com/wss.pl
- `userfaultfd(2)` man page
- Linux kernel docs on idle page tracking
- Gorman, "Understanding the Linux Virtual Memory Manager"
