# C11 Memory Ordering

## Why memory ordering matters

Modern CPUs and compilers **reorder** loads and stores to run faster. On a single thread you never notice — the reordering preserves the "as-if" sequential result. But with **multiple threads** sharing data, reordering can cause one thread to see stale or half-updated values from another thread.

Memory ordering tells the CPU and compiler **which reorderings are allowed** around an atomic operation. Choosing the right ordering gives you:

- **Correctness:** Other threads see your writes in the order you intend.
- **Performance:** Weaker orderings allow more hardware-level parallelism (fewer fences/barriers), which can be significantly faster on architectures like ARM and POWER.

Without explicit ordering, concurrent programs can observe "impossible" states — values that no sequential interleaving of statements would produce.

---

## The core problem

Consider two threads sharing `data` and a `ready` flag:

```c
// Thread 1 (producer)
data = 42;
ready = true;

// Thread 2 (consumer)
if (ready) {
    printf("%d\n", data);  // might print 0, not 42!
}
```

Without ordering guarantees, the CPU or compiler can:

1. **Reorder Thread 1's stores:** `ready = true` might become visible before `data = 42`.
2. **Reorder Thread 2's loads:** `data` might be loaded before `ready` is checked.

So Thread 2 sees `ready == true` but reads stale `data == 0`. Memory ordering prevents this.

---

## The five memory orders (weakest to strongest)

### 1. `memory_order_relaxed`

**Guarantee:** Only atomicity — no torn reads/writes. No ordering with respect to other operations.

**What it allows:** The CPU and compiler can freely reorder relaxed atomics with surrounding loads and stores.

**Use for:** Isolated counters, statistics, progress tracking — anything where you need a race-free read/write but don't care about the order relative to other variables.

```c
// Simple shared counter — ordering doesn't matter, just atomicity
atomic_int hits = 0;

// Thread 1
atomic_fetch_add_explicit(&hits, 1, memory_order_relaxed);

// Thread 2
atomic_fetch_add_explicit(&hits, 1, memory_order_relaxed);

// Later, read total
int total = atomic_load_explicit(&hits, memory_order_relaxed);
```

**Cost:** Cheapest. On x86 this compiles to a plain `LOCK XADD` (or similar); on ARM it avoids expensive barrier instructions.

---

### 2. `memory_order_acquire`

**Guarantee:** No load or store **after** this operation (in program order) can be reordered **before** it.

**Think of it as:** "From this point on, I can see everything the releasing thread published."

**Use for:** Loads that check a condition before reading shared data (e.g. loading a "ready" flag, then reading data).

```c
// Thread 2 (consumer) — acquire load
if (atomic_load_explicit(&ready, memory_order_acquire)) {
    // everything Thread 1 did BEFORE its release store is visible here
    printf("%d\n", data);  // guaranteed to see 42
}
```

**Cost:** On x86, acquire is essentially free (x86 loads are naturally acquire). On ARM/POWER it may emit a lightweight barrier.

---

### 3. `memory_order_release`

**Guarantee:** No load or store **before** this operation (in program order) can be reordered **after** it.

**Think of it as:** "I'm publishing — everything I did before this store is visible to anyone who acquires from this location."

**Use for:** Stores that publish data (e.g. writing data, then setting a "ready" flag).

```c
// Thread 1 (producer) — release store
data = 42;
atomic_store_explicit(&ready, true, memory_order_release);
// data = 42 is guaranteed to be visible before ready = true
```

**Cost:** On x86, release is essentially free (x86 stores are naturally release). On ARM/POWER it emits a lightweight barrier.

---

### 4. `memory_order_acq_rel`

**Guarantee:** Combines acquire and release. No loads/stores before this can move after it, and no loads/stores after this can move before it.

**Use for:** Read-modify-write (RMW) operations that both observe a previous release and publish to later acquirers (e.g. reference count decrement, some lock algorithms).

```c
// Decrement refcount: need to see prior writes (acquire)
// and publish the decrement to others (release)
int old = atomic_fetch_sub_explicit(&refcount, 1, memory_order_acq_rel);
if (old == 1) {
    // last reference — safe to free
    free(obj);
}
```

**Cost:** Slightly more expensive than acquire or release alone; on ARM it emits barriers on both sides.

---

### 5. `memory_order_seq_cst`

**Guarantee:** The strongest. All `seq_cst` operations across all threads have a **single total order** that every thread agrees on. Plus acquire/release semantics.

**Think of it as:** "Everyone sees all `seq_cst` operations in the same global order."

**Use for:** The default when you're unsure, or when you need a global ordering guarantee (e.g. Dekker's algorithm, certain lock-free structures where acquire/release alone isn't enough).

```c
// This is what you get by default (no _explicit)
atomic_store(&x, 1);               // seq_cst store
int val = atomic_load(&x);         // seq_cst load
atomic_fetch_add(&counter, 1);     // seq_cst RMW
```

**Cost:** Most expensive. On x86, stores emit an `MFENCE` or `XCHG`; on ARM, full barriers on both sides.

---

## How release and acquire pair together

This is the most important pattern:

```
Thread 1 (producer)            Thread 2 (consumer)
─────────────────              ─────────────────
write shared data              ┊
        │                      ┊
        ▼                      ┊
RELEASE store to flag  ──────► ACQUIRE load from flag
                               │
                               ▼
                               read shared data (sees writes)
```

The release store **synchronizes-with** the acquire load on the same atomic variable. All writes before the release are **visible** to all reads after the acquire.

This is the fundamental building block of lock-free communication between threads.

---

## Comparison table

| Order | Prevents reordering | Typical cost (x86) | Typical cost (ARM) | When to use |
|-------|--------------------|--------------------|--------------------|--------------------------------------------|
| `relaxed` | None (only atomicity) | Free | Free | Counters, stats |
| `acquire` | Later ops can't move before | Free | Lightweight barrier | Loads that "receive" published data |
| `release` | Earlier ops can't move after | Free | Lightweight barrier | Stores that "publish" data |
| `acq_rel` | Both directions | Free | Barriers both sides | RMWs that both receive and publish |
| `seq_cst` | Total global order | MFENCE / XCHG | Full barriers | Default; global ordering needed |

---

## Why x86 is "easy mode"

x86 has a **strong memory model** (Total Store Order / TSO):

- All loads are naturally acquire.
- All stores are naturally release.
- Only `seq_cst` stores need an extra fence (`MFENCE`).

So on x86, most ordering bugs **don't show up** — your code "works" even with wrong ordering. But on **ARM, POWER, or RISC-V** (weaker models), those same bugs cause real failures. Writing correct ordering matters for portability.

---

## Rules of thumb

1. **Start with `seq_cst`** (the default). It's the easiest to reason about.
2. **Optimize to `acquire`/`release`** when profiling shows atomics are a bottleneck.
3. **Use `relaxed`** only for truly independent counters/stats where ordering doesn't matter.
4. **Pair release stores with acquire loads** on the same variable to "publish" data between threads.
5. **Never use `relaxed`** for synchronization (flags, locks, condition signaling).
6. **When in doubt, use `seq_cst`** — correctness first, optimize later.

---

## Quick reference: `_explicit` vs default

```c
// These are equivalent:
atomic_store(&x, 42);
atomic_store_explicit(&x, 42, memory_order_seq_cst);

// These are equivalent:
atomic_load(&x);
atomic_load_explicit(&x, memory_order_seq_cst);

// Use _explicit to choose a weaker (cheaper) ordering:
atomic_store_explicit(&x, 42, memory_order_release);
atomic_load_explicit(&x, memory_order_acquire);
atomic_fetch_add_explicit(&x, 1, memory_order_relaxed);
```

---

## Further reading

- C11 standard, Section 7.17 (`<stdatomic.h>`)
- [cppreference: memory_order](https://en.cppreference.com/w/c/atomic/memory_order) (C++ page, but C11 semantics are the same)
- Jeff Preshing's blog posts on lock-free programming
- Herb Sutter, "atomic<> Weapons" (CppCon talk)
