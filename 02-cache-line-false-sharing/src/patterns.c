/*
 * patterns.c — Milestone 4: Real-World False Sharing Patterns
 *
 * Demonstrates four common false-sharing anti-patterns with fixes.
 * Uses _Atomic with relaxed ordering to guarantee memory writes each iteration.
 *
 * Usage:
 *   ./patterns                  # run all patterns
 *   ./patterns array_counters   # run one specific pattern
 *   ./patterns producer_consumer
 *   ./patterns hash_buckets
 *   ./patterns thread_stats
 */
#include "common.h"

#define NUM_THREADS 8
#define DEFAULT_ITERS 100000000L

static long get_pattern_iterations(void)
{
    const char *env = getenv("ITERATIONS");
    if (env) {
        long val = atol(env);
        if (val > 0)
            return val;
    }
    return DEFAULT_ITERS;
}

/* Helpers: use atomic_fetch_add which compiles to `lock xadd` on x86,
   forcing cache line ownership transfer on every operation. */
static inline void atomic_inc_relaxed(_Atomic long *p)
{
    atomic_fetch_add_explicit(p, 1, memory_order_relaxed);
}

static inline void atomic_add_relaxed(_Atomic long *p, long v)
{
    atomic_fetch_add_explicit(p, v, memory_order_relaxed);
}

/* ═══════════════════════════════════════════════════════════════
 *  Pattern 1: Array of Counters
 *
 *  Each thread updates counters[thread_id].
 *  Packed: adjacent longs share cache lines.
 *  Fix: pad each element to a full cache line.
 * ═══════════════════════════════════════════════════════════════ */

static _Atomic long array_packed[NUM_THREADS];

struct array_padded_entry {
    _Atomic long value;
    char _pad[CACHE_LINE_SIZE - sizeof(_Atomic long)];
} CACHE_ALIGNED;
static struct array_padded_entry array_padded[NUM_THREADS] CACHE_ALIGNED;

struct array_args {
    int id;
    long iters;
    int padded;
};

static void *array_worker(void *arg)
{
    struct array_args *a = (struct array_args *)arg;
    pin_to_core(a->id);
    long iters = a->iters;

    if (a->padded) {
        _Atomic long *ctr = &array_padded[a->id].value;
        for (long i = 0; i < iters; i++)
            atomic_inc_relaxed(ctr);
    } else {
        _Atomic long *ctr = &array_packed[a->id];
        for (long i = 0; i < iters; i++)
            atomic_inc_relaxed(ctr);
    }
    return NULL;
}

static double run_array_counters(int padded, long iters)
{
    for (int i = 0; i < NUM_THREADS; i++) {
        atomic_store(&array_packed[i], 0);
        atomic_store(&array_padded[i].value, 0);
    }

    pthread_t threads[NUM_THREADS];
    struct array_args args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++)
        args[i] = (struct array_args){ .id = i, .iters = iters, .padded = padded };

    uint64_t start = now_ns();
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, array_worker, &args[i]);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);
    return elapsed_ms(start, now_ns());
}

static void benchmark_array_counters(void)
{
    long iters = get_pattern_iterations();

    printf("\n  Pattern: ARRAY OF COUNTERS\n");
    printf("  long counters[N] — each thread updates counters[thread_id]\n");
    printf("  Threads: %d, Iterations: %ldM\n", NUM_THREADS, iters / 1000000);
    printf("  %-20s %12s %15s\n", "Layout", "Time (ms)", "Ops/sec");

    double packed_ms = run_array_counters(0, iters);
    double padded_ms = run_array_counters(1, iters);
    double total = (double)NUM_THREADS * (double)iters;

    printf("  %-20s %12.1f %15.0f\n", "Packed (adjacent)", packed_ms, total / (packed_ms / 1000));
    printf("  %-20s %12.1f %15.0f\n", "Padded (separated)", padded_ms, total / (padded_ms / 1000));
    printf("  Slowdown: %.1fx\n", packed_ms / padded_ms);
    printf("  Fix: __attribute__((aligned(64))) or pad each entry to 64 bytes\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  Pattern 2: Producer-Consumer Flags
 *
 *  Adjacent counters written by producer and consumer threads.
 * ═══════════════════════════════════════════════════════════════ */

struct flags_packed {
    _Atomic long producer_count;
    _Atomic long consumer_count;
};

struct flags_padded {
    _Atomic long producer_count;
    char _pad1[CACHE_LINE_SIZE - sizeof(_Atomic long)];
    _Atomic long consumer_count;
    char _pad2[CACHE_LINE_SIZE - sizeof(_Atomic long)];
} CACHE_ALIGNED;

struct pc_args {
    void *flags;
    long iters;
    int padded;
    int is_producer;
};

static void *pc_worker(void *arg)
{
    struct pc_args *a = (struct pc_args *)arg;
    pin_to_core(a->is_producer ? 0 : get_num_cores() / 2);
    long iters = a->iters;

    if (a->padded) {
        struct flags_padded *f = (struct flags_padded *)a->flags;
        _Atomic long *ctr = a->is_producer ? &f->producer_count : &f->consumer_count;
        for (long i = 0; i < iters; i++)
            atomic_inc_relaxed(ctr);
    } else {
        struct flags_packed *f = (struct flags_packed *)a->flags;
        _Atomic long *ctr = a->is_producer ? &f->producer_count : &f->consumer_count;
        for (long i = 0; i < iters; i++)
            atomic_inc_relaxed(ctr);
    }
    return NULL;
}

static double run_producer_consumer(int padded, long iters, void *flags)
{
    pthread_t t1, t2;
    struct pc_args p = { .flags = flags, .iters = iters, .padded = padded, .is_producer = 1 };
    struct pc_args c = { .flags = flags, .iters = iters, .padded = padded, .is_producer = 0 };

    uint64_t start = now_ns();
    pthread_create(&t1, NULL, pc_worker, &p);
    pthread_create(&t2, NULL, pc_worker, &c);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return elapsed_ms(start, now_ns());
}

static void benchmark_producer_consumer(void)
{
    long iters = get_pattern_iterations();

    printf("\n  Pattern: PRODUCER-CONSUMER FLAGS\n");
    printf("  Adjacent counters written by producer and consumer threads\n");
    printf("  Iterations: %ldM\n", iters / 1000000);
    printf("  %-20s %12s %15s\n", "Layout", "Time (ms)", "Ops/sec");

    struct flags_packed *packed = aligned_alloc(CACHE_LINE_SIZE, CACHE_LINE_SIZE);
    memset(packed, 0, sizeof(*packed));
    struct flags_padded *padded = aligned_alloc(CACHE_LINE_SIZE,
        ((sizeof(*padded) + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE) * CACHE_LINE_SIZE);
    memset(padded, 0, sizeof(*padded));

    double packed_ms = run_producer_consumer(0, iters, packed);
    double padded_ms = run_producer_consumer(1, iters, padded);
    double total = 2.0 * (double)iters;

    printf("  %-20s %12.1f %15.0f\n", "Packed (adjacent)", packed_ms, total / (packed_ms / 1000));
    printf("  %-20s %12.1f %15.0f\n", "Padded (separated)", padded_ms, total / (padded_ms / 1000));
    printf("  Slowdown: %.1fx\n", packed_ms / padded_ms);
    printf("  Fix: separate producer and consumer fields onto different cache lines\n");

    free(packed);
    free(padded);
}

/* ═══════════════════════════════════════════════════════════════
 *  Pattern 3: Hash Table Bucket Locks
 *
 *  Adjacent spinlocks for hash table buckets cause contention
 *  even when threads access different buckets.
 * ═══════════════════════════════════════════════════════════════ */

#define NUM_BUCKETS 64

struct bucket_packed {
    _Atomic long lock;
    _Atomic long count;
};
static struct bucket_packed buckets_packed[NUM_BUCKETS];

struct bucket_padded {
    _Atomic long lock;
    _Atomic long count;
    char _pad[CACHE_LINE_SIZE - 2 * sizeof(_Atomic long)];
} CACHE_ALIGNED;
static struct bucket_padded buckets_padded[NUM_BUCKETS] CACHE_ALIGNED;

struct bucket_args {
    int id;
    long iters;
    int padded;
};

static void *bucket_worker(void *arg)
{
    struct bucket_args *a = (struct bucket_args *)arg;
    pin_to_core(a->id);
    long iters = a->iters;
    int my_bucket = a->id;

    if (a->padded) {
        _Atomic long *lock = &buckets_padded[my_bucket].lock;
        _Atomic long *count = &buckets_padded[my_bucket].count;
        for (long i = 0; i < iters; i++) {
            atomic_inc_relaxed(lock);
            atomic_inc_relaxed(count);
            atomic_add_relaxed(lock, -1);
        }
    } else {
        _Atomic long *lock = &buckets_packed[my_bucket].lock;
        _Atomic long *count = &buckets_packed[my_bucket].count;
        for (long i = 0; i < iters; i++) {
            atomic_inc_relaxed(lock);
            atomic_inc_relaxed(count);
            atomic_add_relaxed(lock, -1);
        }
    }
    return NULL;
}

static double run_hash_buckets(int padded, long iters)
{
    for (int i = 0; i < NUM_BUCKETS; i++) {
        atomic_store(&buckets_packed[i].lock, 0);
        atomic_store(&buckets_packed[i].count, 0);
        atomic_store(&buckets_padded[i].lock, 0);
        atomic_store(&buckets_padded[i].count, 0);
    }

    pthread_t threads[NUM_THREADS];
    struct bucket_args args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
        args[i] = (struct bucket_args){ .id = i, .iters = iters, .padded = padded };

    uint64_t start = now_ns();
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, bucket_worker, &args[i]);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);
    return elapsed_ms(start, now_ns());
}

static void benchmark_hash_buckets(void)
{
    long iters = get_pattern_iterations();

    printf("\n  Pattern: HASH TABLE BUCKET LOCKS\n");
    printf("  Each thread works on its own bucket, but adjacent buckets share cache lines\n");
    printf("  Threads: %d, Iterations: %ldM\n", NUM_THREADS, iters / 1000000);
    printf("  sizeof(bucket_packed)=%zu sizeof(bucket_padded)=%zu\n",
           sizeof(struct bucket_packed), sizeof(struct bucket_padded));
    printf("  %-20s %12s %15s\n", "Layout", "Time (ms)", "Ops/sec");

    double packed_ms = run_hash_buckets(0, iters);
    double padded_ms = run_hash_buckets(1, iters);
    double total = (double)NUM_THREADS * (double)iters;

    printf("  %-20s %12.1f %15.0f\n", "Packed (adjacent)", packed_ms, total / (packed_ms / 1000));
    printf("  %-20s %12.1f %15.0f\n", "Padded (separated)", padded_ms, total / (padded_ms / 1000));
    printf("  Slowdown: %.1fx\n", packed_ms / padded_ms);
    printf("  Fix: pad each bucket struct to CACHE_LINE_SIZE\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  Pattern 4: Per-Thread Statistics Structs
 *
 *  Small per-thread stat structs packed tightly cause false sharing.
 * ═══════════════════════════════════════════════════════════════ */

struct stats_packed {
    _Atomic long requests;
    _Atomic long bytes;
    _Atomic long errors;
};
static struct stats_packed stats_p[NUM_THREADS];

struct stats_padded {
    _Atomic long requests;
    _Atomic long bytes;
    _Atomic long errors;
    char _pad[CACHE_LINE_SIZE - 3 * sizeof(_Atomic long)];
} CACHE_ALIGNED;
static struct stats_padded stats_d[NUM_THREADS] CACHE_ALIGNED;

struct stats_args {
    int id;
    long iters;
    int padded;
};

static void *stats_worker(void *arg)
{
    struct stats_args *a = (struct stats_args *)arg;
    pin_to_core(a->id);
    long iters = a->iters;

    if (a->padded) {
        _Atomic long *req = &stats_d[a->id].requests;
        _Atomic long *byt = &stats_d[a->id].bytes;
        _Atomic long *err = &stats_d[a->id].errors;
        for (long i = 0; i < iters; i++) {
            atomic_inc_relaxed(req);
            atomic_add_relaxed(byt, 1024);
            if (i % 1000 == 0)
                atomic_inc_relaxed(err);
        }
    } else {
        _Atomic long *req = &stats_p[a->id].requests;
        _Atomic long *byt = &stats_p[a->id].bytes;
        _Atomic long *err = &stats_p[a->id].errors;
        for (long i = 0; i < iters; i++) {
            atomic_inc_relaxed(req);
            atomic_add_relaxed(byt, 1024);
            if (i % 1000 == 0)
                atomic_inc_relaxed(err);
        }
    }
    return NULL;
}

static double run_thread_stats(int padded, long iters)
{
    for (int i = 0; i < NUM_THREADS; i++) {
        atomic_store(&stats_p[i].requests, 0);
        atomic_store(&stats_p[i].bytes, 0);
        atomic_store(&stats_p[i].errors, 0);
        atomic_store(&stats_d[i].requests, 0);
        atomic_store(&stats_d[i].bytes, 0);
        atomic_store(&stats_d[i].errors, 0);
    }

    pthread_t threads[NUM_THREADS];
    struct stats_args args[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
        args[i] = (struct stats_args){ .id = i, .iters = iters, .padded = padded };

    uint64_t start = now_ns();
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, stats_worker, &args[i]);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);
    return elapsed_ms(start, now_ns());
}

static void benchmark_thread_stats(void)
{
    long iters = get_pattern_iterations();

    printf("\n  Pattern: PER-THREAD STATISTICS\n");
    printf("  Per-thread stat structs (requests, bytes, errors) updated in hot loops\n");
    printf("  Threads: %d, Iterations: %ldM\n", NUM_THREADS, iters / 1000000);
    printf("  sizeof(stats_packed)=%zu, sizeof(stats_padded)=%zu\n",
           sizeof(struct stats_packed), sizeof(struct stats_padded));
    printf("  Packed: %.0f structs per cache line\n",
           (double)CACHE_LINE_SIZE / sizeof(struct stats_packed));
    printf("  %-20s %12s %15s\n", "Layout", "Time (ms)", "Ops/sec");

    double packed_ms = run_thread_stats(0, iters);
    double padded_ms = run_thread_stats(1, iters);
    double total = (double)NUM_THREADS * (double)iters;

    printf("  %-20s %12.1f %15.0f\n", "Packed (adjacent)", packed_ms, total / (packed_ms / 1000));
    printf("  %-20s %12.1f %15.0f\n", "Padded (separated)", padded_ms, total / (padded_ms / 1000));
    printf("  Slowdown: %.1fx\n", packed_ms / padded_ms);
    printf("  Fix: align each per-thread struct to cache line size\n");
}

/* ═══════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    const char *pattern = (argc > 1) ? argv[1] : "all";

    printf("Real-World False Sharing Patterns\n");
    print_separator();

    int ran = 0;

    if (strcmp(pattern, "all") == 0 || strcmp(pattern, "array_counters") == 0) {
        benchmark_array_counters();
        print_separator();
        ran = 1;
    }
    if (strcmp(pattern, "all") == 0 || strcmp(pattern, "producer_consumer") == 0) {
        benchmark_producer_consumer();
        print_separator();
        ran = 1;
    }
    if (strcmp(pattern, "all") == 0 || strcmp(pattern, "hash_buckets") == 0) {
        benchmark_hash_buckets();
        print_separator();
        ran = 1;
    }
    if (strcmp(pattern, "all") == 0 || strcmp(pattern, "thread_stats") == 0) {
        benchmark_thread_stats();
        print_separator();
        ran = 1;
    }

    if (!ran) {
        fprintf(stderr, "Unknown pattern: %s\n", pattern);
        fprintf(stderr, "Available: array_counters, producer_consumer, hash_buckets, thread_stats, all\n");
        return 1;
    }

    return 0;
}
