# Project 08: Write-Ahead Log (WAL) Performance Tester

## Goal

Implement a write-ahead log and benchmark different `fsync`/persistence strategies, revealing the fundamental tradeoff every database makes between durability and performance.

## What You'll Learn

- Write-ahead logging — the core durability mechanism in databases
- `fsync`, `fdatasync`, `O_DIRECT`, `O_DSYNC` — what each guarantees
- Filesystem behavior: page cache, writeback, journaling
- Why I/O is the bottleneck in most database workloads
- Group commit optimization

## Background

Every database (PostgreSQL, MySQL, SQLite, etcd, Redis with AOF) uses some form of write-ahead logging. Before a transaction is committed, the changes are written to a sequential log file and `fsync`'d to disk. The WAL is the critical path for write performance.

The performance difference between `fsync` strategies can be 10-100x. Understanding these tradeoffs is arguably the single most important performance engineering skill for backend systems.

## Prerequisites

- C or Rust (need precise control over syscalls)
- Linux with ext4 or XFS
- A real disk (SSD preferred; benchmarks on tmpfs are meaningless for this)
- `blktrace` or `biosnoop` (from Project 07) for validation

## Milestones

### Milestone 1: Simple WAL Implementation

**Build a basic write-ahead log.**

- WAL entry format: `[length][checksum][sequence_number][payload]`
- Operations:
  - `wal_append(data)` — append entry to log
  - `wal_sync()` — ensure entries are durable
  - `wal_read(sequence)` — read entries for recovery
  - `wal_truncate(sequence)` — discard entries before sequence
- Recovery: on startup, read the WAL and verify checksums to find the last valid entry
- Simple API; no transaction semantics yet

**Deliverable:** A working WAL library with append, sync, read, and recovery.

### Milestone 2: fsync Strategy Comparison

**Benchmark different durability strategies.**

- Strategies to compare:
  1. **`fsync` every write** — maximum durability, worst throughput
  2. **`fdatasync` every write** — skip metadata sync (faster on ext4)
  3. **`O_DSYNC` flag** — sync on every `write()` call
  4. **`O_DIRECT` + `fsync`** — bypass page cache entirely
  5. **Periodic fsync** — fsync every N milliseconds (batched durability)
  6. **No fsync** — rely on OS writeback (data loss on crash)
- For each strategy, measure:
  - Writes per second
  - Write latency (p50, p99, p999)
  - Actual disk I/Os (use `iostat` or biosnoop)

**Deliverable:** Comparison table/chart showing throughput and latency per strategy.

### Milestone 3: Group Commit

**Implement group commit — the most important WAL optimization.**

- Multiple writers append to the WAL concurrently
- Instead of each writer calling `fsync`, a single "sync leader" batches:
  1. Writers append their entries and register for notification
  2. A sync thread (or the first waiting writer) calls `fsync` once
  3. All writers in the batch are notified of completion
- This amortizes the cost of `fsync` across multiple transactions
- Benchmark:
  - Individual fsync per write vs group commit
  - Measure throughput at different concurrency levels (1, 4, 16, 64 writers)
  - Measure commit latency (time from append to durable)

**Deliverable:** Group commit implementation + benchmark showing the throughput improvement.

### Milestone 4: Filesystem and Device Comparison

**Same workload, different filesystems and devices.**

- Filesystems: ext4, XFS, btrfs (with and without journaling)
- Filesystem options: `barrier`/`nobarrier`, journal mode (`data=ordered` vs `data=journal` vs `data=writeback`)
- Devices: NVMe SSD, SATA SSD, HDD (if available)
- Test `fallocate` pre-allocation effect (avoid metadata updates on append)
- Benchmark:
  - Same WAL workload across all combinations
  - Measure: throughput, latency, write amplification

**Deliverable:** Matrix of filesystem × device × strategy performance results.

### Milestone 5: Crash Recovery Testing

**Verify durability guarantees actually hold.**

- Use `dm-flakey` (device-mapper target that simulates disk failures):
  - Write entries with fsync
  - Trigger simulated crash (drop writes)
  - Attempt recovery — verify last fsync'd entry is intact
- Test each strategy:
  - `fsync` every write: no data loss expected
  - Periodic fsync: expect loss of entries since last sync
  - No fsync: expect arbitrary loss
- Use `dm-log-writes` to replay exact I/O sequence and verify ordering

**Deliverable:** Crash test harness that validates durability claims for each strategy.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| `fsync` vs `fdatasync` | `fdatasync` skips metadata (atime, mtime); faster when file size doesn't change |
| `O_DIRECT` | Bypasses page cache; needed when you manage your own caching |
| Write barriers | Ensure writes reach stable storage in order; critical for journaling |
| Write amplification | One logical write may cause multiple physical writes (journal + data) |
| Group commit | The key optimization that makes databases usable with fsync |
| Fallocate/prealloc | Pre-allocating file space avoids extent allocation during writes |
| `sync_file_range` | Fine-grained control over which byte ranges to sync |

## Validation & Benchmarks

- Use `strace -e fsync,fdatasync,write` to verify syscalls match expectations
- Use `blktrace` or biosnoop to verify actual device I/O
- Compare your WAL throughput against PostgreSQL's `pg_test_fsync` utility
- Verify crash safety with `dm-flakey` (not just testing performance)
- Test with `ionice` and background I/O to see interference effects

## References

- Brendan Gregg's "Systems Performance" Chapter 8 (File Systems)
- "BPF Performance Tools" Chapter 8 (File Systems)
- PostgreSQL WAL documentation
- "Optimistic Crash Consistency" paper (Chidambaram et al.)
- `pg_test_fsync` source code
- Alice paper: "All File Systems Are Not Created Equal" (Pillai et al.)
