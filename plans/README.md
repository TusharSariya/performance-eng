# Performance Engineering Project Plans

20 hands-on projects for learning systems performance engineering, eBPF, and observability — inspired by Brendan Gregg's work.

## Projects by Category

### CPU & Scheduling
| # | Project | Key Skills |
|---|---------|------------|
| 01 | [Flame Graph Generator](01-flame-graph-generator.md) | `perf_event_open`, stack unwinding, SVG rendering |
| 02 | [Cache-Line False Sharing](02-cache-line-false-sharing.md) | Cache coherence, `perf stat`, data alignment |
| 03 | [Scheduler Latency Monitor](03-scheduler-latency-monitor.md) | eBPF, `sched_switch`, latency histograms |

### Memory
| # | Project | Key Skills |
|---|---------|------------|
| 04 | [Memory Allocator Benchmark](04-memory-allocator-benchmark.md) | malloc/jemalloc/tcmalloc, fragmentation, `LD_PRELOAD` |
| 05 | [Page Fault Tracer](05-page-fault-tracer.md) | Virtual memory, working set, `userfaultfd` |
| 06 | [NUMA Benchmark](06-numa-benchmark.md) | NUMA topology, memory locality, `numactl` |

### Disk / I/O
| # | Project | Key Skills |
|---|---------|------------|
| 07 | [biosnoop Clone](07-biosnoop-clone.md) | Block I/O layer, eBPF tracepoints, I/O patterns |
| 08 | [WAL/fsync Benchmark](08-wal-fsync-benchmark.md) | fsync strategies, group commit, crash safety |
| 09 | [I/O Scheduler Comparator](09-io-scheduler-comparator.md) | blk-mq schedulers, queue depth, fairness |

### Network
| # | Project | Key Skills |
|---|---------|------------|
| 10 | [Packet Latency Analyzer](10-packet-latency-analyzer.md) | Network stack, XDP, softirq, `sk_buff` |
| 11 | [TCP Tuning Benchmark](11-tcp-tuning-benchmark.md) | Congestion control (BBR/cubic), buffer tuning, BDP |
| 12 | [Connection Storm Simulator](12-connection-storm-simulator.md) | SYN/accept queues, TIME_WAIT, connection scaling |

### Application-Level
| # | Project | Key Skills |
|---|---------|------------|
| 13 | [Latency Injection Proxy](13-latency-injection-proxy.md) | TCP proxying, chaos engineering, tail latency |
| 14 | [Lock Contention Profiler](14-lock-contention-profiler.md) | `LD_PRELOAD`, futex, hold time / wait time |
| 15 | [GC Pressure Benchmark](15-gc-pressure-benchmark.md) | JVM/Go/Python GC, USDT probes, latency correlation |

### eBPF Specific
| # | Project | Key Skills |
|---|---------|------------|
| 16 | [Syscall Latency Dashboard](16-syscall-latency-dashboard.md) | `raw_syscalls` tracepoints, BPF maps, Prometheus |
| 17 | [Security Event Monitor](17-security-event-monitor.md) | LSM hooks, process/file/network monitoring, Falco-style rules |
| 18 | [Custom eBPF `top`](18-custom-ebpf-top.md) | Multi-subsystem eBPF, ncurses UI, off-CPU analysis |

### Full-Stack / Capstone
| # | Project | Key Skills |
|---|---------|------------|
| 19 | [End-to-End Request Tracer](19-end-to-end-request-tracer.md) | Distributed tracing, USDT, kernel-user correlation |
| 20 | [Performance Regression CI](20-perf-regression-ci.md) | Statistical testing, CI/CD, change-point detection |

## Suggested Learning Path

```
Start here (quick wins):
  02 Cache-Line False Sharing  →  immediate "aha" moment
  08 WAL/fsync Benchmark       →  most practical database knowledge

Build eBPF skills:
  16 Syscall Latency Dashboard →  first real eBPF project
  03 Scheduler Latency Monitor →  scheduler internals
  07 biosnoop Clone            →  block I/O tracing

Go deeper:
  01 Flame Graph Generator     →  profiling from scratch
  14 Lock Contention Profiler  →  application-level instrumentation
  18 Custom eBPF top           →  ties all subsystems together

Capstone:
  19 End-to-End Request Tracer →  full distributed tracing
  20 Performance Regression CI →  production-grade benchmarking
```

## Core References

- Brendan Gregg, *Systems Performance*, 2nd Edition (2020)
- Brendan Gregg, *BPF Performance Tools* (2019)
- https://www.brendangregg.com — blog, tools, talks
- https://github.com/libbpf/libbpf-bootstrap — eBPF project template
- https://github.com/iovisor/bcc — BPF Compiler Collection
