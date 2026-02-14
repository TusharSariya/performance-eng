# Project 06: NUMA-Aware vs NUMA-Unaware Benchmark

## Goal

Build a benchmark suite that quantifies the performance cost of NUMA-remote memory access and demonstrates how NUMA-aware programming can dramatically improve throughput and latency on multi-socket systems.

## What You'll Learn

- NUMA (Non-Uniform Memory Access) architecture
- Memory locality and its performance impact
- `numactl`, `libnuma`, NUMA policies (`membind`, `interleave`, `preferred`)
- NUMA balancing in the Linux kernel
- How databases, JVMs, and high-performance systems handle NUMA

## Background

On multi-socket servers, each CPU socket has its own local memory. Accessing memory attached to a remote socket costs 1.5-3x more latency than local access. On a 2-socket server, if your threads are running on socket 0 but their memory is on socket 1, you lose 40-70% of potential memory bandwidth.

Most cloud instances are single-socket, so many developers never encounter NUMA. But production databases, HPC, and high-frequency trading systems are often multi-socket, where NUMA awareness is critical.

## Prerequisites

- C or C++ (need `libnuma` bindings)
- Multi-socket NUMA system (or VM with emulated NUMA: `qemu -numa`)
- `numactl`, `numastat`, `lstopo` (hwloc)
- `perf` with NUMA-related events

## Milestones

### Milestone 1: NUMA Topology Discovery

**Understand and visualize the NUMA topology of your system.**

- Use `lstopo` (hwloc) to generate a topology diagram
- Parse `/sys/devices/system/node/` for:
  - Number of NUMA nodes
  - CPUs per node
  - Memory per node
  - Inter-node distances (`/sys/devices/system/node/node0/distance`)
- Build a tool that prints a summary:
  ```
  Node 0: CPUs 0-15, 64GB RAM
  Node 1: CPUs 16-31, 64GB RAM
  Distance matrix: [10, 21; 21, 10]
  ```

**Deliverable:** Topology discovery tool + visualization.

### Milestone 2: Memory Latency Benchmark

**Measure the actual latency difference between local and remote memory access.**

- Allocate a large array on a specific NUMA node using `numa_alloc_onnode()`
- Run a pointer-chasing benchmark (linked list traversal with random stride)
  - This defeats prefetchers and measures true memory latency
- Pin the thread to:
  - Same node as memory (local access)
  - Different node (remote access)
- Measure latency in nanoseconds per access

**Deliverable:** Table showing local vs remote memory latency for each node pair.

### Milestone 3: Bandwidth Benchmark

**Measure memory bandwidth for local vs remote access.**

- Allocate large buffers (larger than LLC) on specific nodes
- Stream read/write/copy operations (like STREAM benchmark)
- Pin threads and memory to various combinations:
  - Thread on node 0, memory on node 0 (local)
  - Thread on node 0, memory on node 1 (remote)
  - Thread on node 0, memory interleaved (round-robin across nodes)
- Scale with multiple threads (all local, all remote, mixed)

**Deliverable:** Bandwidth measurements (GB/s) for all combinations.

### Milestone 4: NUMA Policy Comparison

**Benchmark different NUMA allocation policies under realistic workloads.**

- Workloads:
  - **Hash table lookups:** random access pattern, sensitive to latency
  - **Array scan/reduce:** sequential access, sensitive to bandwidth
  - **Producer-consumer:** data allocated on one node, consumed on another
- NUMA policies to compare:
  - `default` — allocate on first-touch node
  - `membind` — force allocation on specific node
  - `interleave` — round-robin across nodes (good for shared data)
  - `preferred` — try a specific node, fall back to others
  - `localalloc` — always allocate on the thread's current node
- Use `numactl` or `set_mempolicy()`/`mbind()` system calls

**Deliverable:** Performance comparison matrix: workload × policy × metric.

### Milestone 5: NUMA Migration and Rebalancing

**Study how the kernel's automatic NUMA balancing works.**

- Enable/disable automatic NUMA balancing:
  ```
  echo 1 > /proc/sys/kernel/numa_balancing
  ```
- Deliberately misplace memory (allocate on node 0, run thread on node 1)
- Trace NUMA migration events with eBPF or `perf`:
  - `migrate:mm_migrate_pages`
  - `sched:sched_move_numa`
  - `numa:numa_hint_faults`
- Measure:
  - How long until the kernel migrates pages
  - Throughput before and after migration
  - Migration overhead (stalls during page migration)

**Deliverable:** Analysis of automatic NUMA balancing effectiveness with traces.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| NUMA distance / interconnect | Not all remote is equal; 2-hop can be worse than 1-hop |
| First-touch policy | Default allocation policy; memory ends up on whichever node touches it first |
| Interleave for shared data | Spreads bandwidth demand; avoids hotspots |
| `numastat` | Shows per-node allocation and miss counters |
| NUMA and huge pages | THP + NUMA interaction can be surprising |
| `perf stat -e node-loads,node-load-misses` | Hardware counters for NUMA hits/misses |

## Validation & Benchmarks

- Verify with `numastat -p <pid>` that memory is placed where you expect
- Use `perf stat` with `node-loads`, `node-load-misses`, `node-stores` counters
- Compare against `Intel MLC` (Memory Latency Checker) results
- Ensure benchmark arrays are larger than LLC to avoid cache effects masking NUMA effects
- Use `taskset` or `numactl --cpunodebind` to pin threads precisely

## References

- Brendan Gregg's "Systems Performance" Chapter 7 (Memory, NUMA section)
- "What Every Programmer Should Know About Memory" — Ulrich Drepper
- Linux NUMA documentation: `Documentation/admin-guide/mm/numa_memory_policy.rst`
- `numactl(8)` and `numa(3)` man pages
- Intel Memory Latency Checker (MLC) tool
