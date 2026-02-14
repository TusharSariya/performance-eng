/*
 * basic_demo.c — Milestone 1: False Sharing Demonstration
 *
 * Two threads each increment their own counter in a tight loop.
 * When counters share a cache line (packed), coherence traffic
 * causes a massive slowdown compared to separate cache lines (padded).
 *
 * Uses _Atomic with relaxed ordering to guarantee every iteration
 * actually touches the cache line (not optimized to a register).
 */
#include "common.h"

/* ── Packed: both counters on the SAME cache line ────────────── */
struct packed_counters {
    _Atomic long counter_a;
    _Atomic long counter_b;
};

/* ── Padded: each counter on its OWN cache line ─────────────── */
struct padded_counters {
    _Atomic long counter_a;
    char _pad[CACHE_LINE_SIZE - sizeof(_Atomic long)];
    _Atomic long counter_b;
};

/* Verify layout at compile time */
_Static_assert(sizeof(struct packed_counters) <= CACHE_LINE_SIZE,
               "packed_counters must fit in one cache line");
_Static_assert(offsetof(struct padded_counters, counter_b) >= CACHE_LINE_SIZE,
               "counter_b must be on a different cache line than counter_a");

struct thread_args {
    _Atomic long *counter;
    long iterations;
    int core;
};

static void *worker(void *arg)
{
    struct thread_args *ta = (struct thread_args *)arg;
    pin_to_core(ta->core);

    _Atomic long *ctr = ta->counter;
    long iters = ta->iterations;

    /* atomic_fetch_add compiles to `lock xadd` on x86, which requires
       exclusive cache line ownership (MESI M state) on every iteration.
       This is what makes false sharing visible — each core must acquire
       the cache line from the other core on every increment. */
    for (long i = 0; i < iters; i++) {
        atomic_fetch_add_explicit(ctr, 1, memory_order_relaxed);
    }
    return NULL;
}

static double run_benchmark(_Atomic long *counter_a, _Atomic long *counter_b,
                            long iterations, int core_a, int core_b)
{
    atomic_store(counter_a, 0);
    atomic_store(counter_b, 0);

    struct thread_args args_a = { .counter = counter_a, .iterations = iterations, .core = core_a };
    struct thread_args args_b = { .counter = counter_b, .iterations = iterations, .core = core_b };

    pthread_t t1, t2;
    uint64_t start = now_ns();

    pthread_create(&t1, NULL, worker, &args_a);
    pthread_create(&t2, NULL, worker, &args_b);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    uint64_t end = now_ns();
    return elapsed_ms(start, end);
}

int main(void)
{
    long iterations = get_iterations();
    int ncores = get_num_cores();

    /* Pick two cores that are far apart (0 and ncores/2) to maximize
       the chance they're on different physical cores / CCXs. */
    int core_a = 0;
    int core_b = ncores / 2;

    printf("Cache-Line False Sharing Demonstrator\n");
    print_separator();
    printf("Iterations per thread : %ld (%.0fM)\n", iterations, iterations / 1e6);
    printf("Cache line size       : %d bytes\n", CACHE_LINE_SIZE);
    printf("Cores used            : %d, %d  (of %d available)\n", core_a, core_b, ncores);
    printf("sizeof(packed)        : %zu bytes\n", sizeof(struct packed_counters));
    printf("sizeof(padded)        : %zu bytes\n", sizeof(struct padded_counters));
    print_separator();

    /* Allocate cache-line-aligned memory */
    struct packed_counters *packed = aligned_alloc(CACHE_LINE_SIZE, sizeof(*packed));
    struct padded_counters *padded = aligned_alloc(CACHE_LINE_SIZE,
        ((sizeof(*padded) + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE) * CACHE_LINE_SIZE);

    if (!packed || !padded) {
        perror("aligned_alloc");
        return 1;
    }

    /* Warmup run */
    printf("Warmup...\n");
    run_benchmark(&packed->counter_a, &packed->counter_b, iterations / 10, core_a, core_b);
    run_benchmark(&padded->counter_a, &padded->counter_b, iterations / 10, core_a, core_b);

    /* ── Packed benchmark (false sharing) ───────────────────── */
    printf("\nRunning PACKED (false sharing) ...\n");
    double packed_ms = run_benchmark(&packed->counter_a, &packed->counter_b,
                                      iterations, core_a, core_b);

    /* ── Padded benchmark (no false sharing) ────────────────── */
    printf("Running PADDED (no false sharing) ...\n");
    double padded_ms = run_benchmark(&padded->counter_a, &padded->counter_b,
                                      iterations, core_a, core_b);

    /* ── Results ────────────────────────────────────────────── */
    printf("\n");
    print_separator();
    printf("RESULTS\n");
    print_separator();
    printf("  %-20s %10s %15s\n", "Mode", "Time (ms)", "Ops/sec");
    print_separator();

    double packed_ops = (2.0 * iterations) / (packed_ms / 1000.0);
    double padded_ops = (2.0 * iterations) / (padded_ms / 1000.0);

    printf("  %-20s %10.1f %15.0f\n", "PACKED (false share)", packed_ms, packed_ops);
    printf("  %-20s %10.1f %15.0f\n", "PADDED (no share)",    padded_ms, padded_ops);
    print_separator();

    double ratio = packed_ms / padded_ms;
    printf("\n  Slowdown: PACKED is %.1fx slower than PADDED\n", ratio);

    if (ratio < 2.0) {
        printf("\n  NOTE: Slowdown is lower than expected. Try:\n");
        printf("    - Increasing ITERATIONS (export ITERATIONS=1000000000)\n");
        printf("    - Checking cores are on different physical cores\n");
        printf("    - Disabling turbo boost: echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo\n");
    }

    printf("\n  Counters (sanity check): packed=(%ld,%ld) padded=(%ld,%ld)\n",
           atomic_load(&packed->counter_a), atomic_load(&packed->counter_b),
           atomic_load(&padded->counter_a), atomic_load(&padded->counter_b));

    free(packed);
    free(padded);
    return 0;
}
