# Project 14: Lock Contention Profiler

## Goal

Build a tool that intercepts `pthread_mutex_lock`/`unlock` calls via `LD_PRELOAD`, measures hold times and wait times per callsite, and identifies the most contended locks in an application.

## What You'll Learn

- Lock contention as a scalability bottleneck
- `LD_PRELOAD` interception technique
- `dlsym(RTLD_NEXT, ...)` for wrapping library functions
- Futex internals (how mutexes actually work in Linux)
- Lock-free alternatives and when they matter

## Background

Lock contention is one of the most common reasons multithreaded applications don't scale. A thread holding a lock blocks all other threads waiting for it. The symptoms:
- CPU utilization stays low despite many threads
- Adding threads makes the application slower (negative scaling)
- `perf top` shows time in `__lll_lock_wait` (futex)

Tools like `mutrace` and `perf lock` exist, but building your own teaches you exactly how locks behave at the syscall level.

## Prerequisites

- C (for the LD_PRELOAD library)
- Understanding of pthreads and mutex semantics
- A multithreaded application to profile (or build test cases)
- Linux with `dlsym` support

## Milestones

### Milestone 1: Basic Lock Interceptor

**Intercept all mutex operations using LD_PRELOAD.**

- Create a shared library that wraps:
  - `pthread_mutex_lock()`
  - `pthread_mutex_unlock()`
  - `pthread_mutex_trylock()`
  - `pthread_mutex_init()` / `pthread_mutex_destroy()`
- Use `dlsym(RTLD_NEXT, "pthread_mutex_lock")` to call the real function
- For each lock/unlock pair, record:
  - Mutex address (identifies which lock)
  - Thread ID
  - Timestamp (using `clock_gettime(CLOCK_MONOTONIC)`)
  - Call site (using `__builtin_return_address(0)`)
- Store events in a thread-local ring buffer (avoid introducing contention in the profiler itself)

**Deliverable:** `LD_PRELOAD=./liblocktrace.so ./my_app` that logs all mutex operations.

### Milestone 2: Hold Time and Wait Time Analysis

**Calculate and report lock hold times and wait times.**

- **Hold time:** time between `lock()` return and `unlock()` call (how long the lock is held)
- **Wait time:** time between `lock()` call and `lock()` return (how long the caller waited)
- **Contention:** wait time > 0 means the lock was already held
- Per-mutex statistics:
  - Total acquisitions
  - Contended acquisitions (wait time > 0)
  - Contention ratio (contended / total)
  - Hold time: min, avg, max, p99
  - Wait time: min, avg, max, p99
  - Total hold time (hold_time × acquisitions — estimates lock "load")
- At program exit (via `atexit` or destructor), print a report sorted by total wait time

**Deliverable:** Lock contention report printed at program exit.

### Milestone 3: Call Site Attribution

**Attribute lock contention to specific code locations.**

- Capture call stacks (not just return address) using `backtrace()`
- Group statistics by:
  - Mutex identity (address + creation callsite)
  - Acquisition callsite (where lock is acquired)
  - Hold callsite (where lock is released — identifies the critical section)
- Resolve addresses to function names using `dladdr()` or `/proc/self/maps` + symbol tables
- Report format:
  ```
  Mutex 0x7f1234 (created at main.c:42)
    Most contended acquisition sites:
      worker.c:88  — 12,000 acquisitions, 45% contended, avg wait 2.3ms
      handler.c:55 — 8,000 acquisitions, 12% contended, avg wait 0.1ms
    Longest hold times:
      processor.c:120-145 — avg hold 5.2ms, max 42ms
  ```

**Deliverable:** Contention report with source file:line attribution.

### Milestone 4: Real-Time Monitoring

**Live monitoring of lock contention.**

- Instead of only reporting at exit, stream data to a monitoring interface
- Options:
  - Shared memory segment that an external viewer reads
  - Unix socket for a dashboard to connect to
  - Periodic dumps to stderr (every N seconds)
- Live display showing:
  - Current lock holders (which thread holds which lock)
  - Lock wait queues (which threads are waiting for which lock)
  - Contention rate over time (sliding window)
  - Top-N most contended locks in the last interval
- Detect and report:
  - Potential deadlocks (lock ordering violations)
  - Lock convoys (same lock acquired in tight sequence by many threads)

**Deliverable:** Live contention dashboard for a running application.

### Milestone 5: Rwlock and Condition Variable Support

**Extend to other synchronization primitives.**

- Intercept `pthread_rwlock_*`:
  - Track reader count, writer hold time
  - Identify writer starvation (writers waiting while readers keep acquiring)
  - Identify reader-writer imbalance
- Intercept `pthread_cond_*`:
  - Track condition wait times
  - Identify spurious wakeups
  - Track signal-to-wake latency
- Intercept `sem_wait`/`sem_post`:
  - Semaphore contention and wait times
- Generate combined report showing all synchronization bottlenecks

**Deliverable:** Comprehensive synchronization profiler covering mutexes, rwlocks, condvars, and semaphores.

## Key Concepts to Explore

| Concept | Why It Matters |
|---------|---------------|
| Futex (`FUTEX_WAIT`/`FUTEX_WAKE`) | Underlying syscall for all pthread synchronization |
| Adaptive spinning | Mutexes spin briefly before sleeping; affects short-hold-time locks |
| Lock convoy | Multiple threads sequentially acquiring the same lock; serializes execution |
| Priority inversion | Low-priority thread holds lock needed by high-priority thread |
| Lock-free data structures | Alternative to mutexes for specific patterns |
| `LD_PRELOAD` ordering | Multiple preloaded libraries; interaction with other interceptors |

## Validation & Benchmarks

- Create test programs with known contention patterns:
  - Single lock, many threads → high contention
  - Per-thread locks → no contention
  - Reader-heavy rwlock → writer starvation
- Compare results against `perf lock record` + `perf lock report`
- Compare against `mutrace` output
- Measure profiler overhead: run a benchmark with and without the preload library
- Verify that the profiler doesn't introduce its own contention (thread-local storage is key)

## References

- `mutrace` by Lennart Poettering (inspiration)
- `perf lock` documentation
- Ulrich Drepper: "Futexes Are Tricky"
- Brendan Gregg's "Systems Performance" Chapter 5 (Applications — lock analysis)
- "BPF Performance Tools" Chapter 5 (lock tracing with eBPF)
- `pthread_mutex_lock(3)` man page
