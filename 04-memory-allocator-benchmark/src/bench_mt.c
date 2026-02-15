/*
 * bench_mt.c — Milestone 2: Multithreaded allocator scalability
 *
 * Three workload modes:
 *   1. thread_local  — each thread allocs and frees its own memory
 *   2. producer_consumer — thread A allocates, thread B frees (cross-thread free)
 *   3. shared_pool  — all threads alloc/free from a shared ring buffer
 *
 * Scales from 1 to N threads (default: 2x core count) and reports
 * throughput for each. CSV output for plotting.
 *
 * Usage:
 *   ./bench_mt [--csv] [--threads 1,2,4,8,16] [workload_name]
 *   LD_PRELOAD=/path/to/libjemalloc.so ./bench_mt
 */
#include "common.h"

/* ── Configuration ──────────────────────────────────────────────────── */

#define DEFAULT_OPS_PER_THREAD 500000
#define ALLOC_SIZE_MIN  64
#define ALLOC_SIZE_MAX  4096

static long ops_per_thread = DEFAULT_OPS_PER_THREAD;
static int  csv_mode = 0;

static long get_ops_env(void)
{
    const char *env = getenv("OPS");
    if (env) {
        long val = atol(env);
        if (val > 0) return val;
    }
    return DEFAULT_OPS_PER_THREAD;
}

/* ── Thread-local workload ──────────────────────────────────────────── */

typedef struct {
    int     thread_id;
    int     core;
    long    ops;
    double  ops_per_sec;
    long    total_allocs;
    long    total_frees;
} thread_result_t;

static void *thread_local_worker(void *arg)
{
    thread_result_t *res = (thread_result_t *)arg;
    pin_to_core(res->core);

    uint64_t rng = 0xDEAD0000ULL + res->thread_id * 7919ULL;
    long ops = res->ops;
    void **ptrs = malloc(ops * sizeof(void *));
    if (!ptrs) { perror("malloc"); return NULL; }

    uint64_t t0 = now_ns();

    /* Allocate all */
    for (long i = 0; i < ops; i++) {
        size_t sz = rand_size(&rng, ALLOC_SIZE_MIN, ALLOC_SIZE_MAX);
        ptrs[i] = malloc(sz);
        if (ptrs[i]) ((char *)ptrs[i])[0] = 1;
    }
    /* Free all */
    for (long i = 0; i < ops; i++) {
        free(ptrs[i]);
    }

    uint64_t t1 = now_ns();
    res->total_allocs = ops;
    res->total_frees = ops;
    res->ops_per_sec = (double)(ops * 2) / elapsed_s(t0, t1);

    free(ptrs);
    return NULL;
}

/* ── Producer-consumer workload ─────────────────────────────────────── */

/*
 * Shared ring buffer for producer-consumer.
 * Producers push allocated pointers, consumers pop and free them.
 */
#define RING_SIZE (1 << 16)  /* 64K slots */
#define RING_MASK (RING_SIZE - 1)

typedef struct {
    void    *buf[RING_SIZE];
    _Atomic long head;   /* next write position */
    _Atomic long tail;   /* next read position  */
    _Atomic int  done;   /* producer finished    */
} ring_t;

static ring_t pc_ring;

static void ring_init(ring_t *r)
{
    memset(r->buf, 0, sizeof(r->buf));
    atomic_store(&r->head, 0);
    atomic_store(&r->tail, 0);
    atomic_store(&r->done, 0);
}

typedef struct {
    int     thread_id;
    int     core;
    long    ops;
    int     is_producer;  /* 1 = producer, 0 = consumer */
    ring_t *ring;
    long    count;        /* ops actually done */
} pc_arg_t;

static void *producer_worker(void *arg)
{
    pc_arg_t *a = (pc_arg_t *)arg;
    pin_to_core(a->core);

    uint64_t rng = 0xABCD0000ULL + (uint64_t)a->thread_id * 6271ULL;

    ring_t *r = a->ring;
    long produced = 0;

    for (long i = 0; i < a->ops; i++) {
        size_t sz = rand_size(&rng, ALLOC_SIZE_MIN, ALLOC_SIZE_MAX);
        void *p = malloc(sz);
        if (!p) continue;
        ((char *)p)[0] = 1;

        /* CAS loop to atomically claim a slot */
        long head;
        for (;;) {
            head = atomic_load(&r->head);
            long tail = atomic_load(&r->tail);
            if (head - tail >= RING_SIZE) {
                sched_yield();
                continue;
            }
            if (atomic_compare_exchange_weak(&r->head, &head, head + 1))
                break;
        }
        r->buf[head & RING_MASK] = p;
        produced++;
    }

    atomic_fetch_add(&r->done, 1);
    a->count = produced;
    return NULL;
}

static void *consumer_worker(void *arg)
{
    pc_arg_t *a = (pc_arg_t *)arg;
    pin_to_core(a->core);

    ring_t *r = a->ring;
    long consumed = 0;
    int n_producers = a->ops; /* repurpose ops field for producer count */

    for (;;) {
        long tail = atomic_load(&r->tail);
        long head = atomic_load(&r->head);
        if (tail < head) {
            /* CAS to claim this slot for consumption */
            if (atomic_compare_exchange_weak(&r->tail, &tail, tail + 1)) {
                void *p = r->buf[tail & RING_MASK];
                /* Spin-wait until the producer has actually stored the pointer */
                while (!p) {
                    p = r->buf[tail & RING_MASK];
                    __asm__ volatile("pause" ::: "memory");
                }
                free(p);
                r->buf[tail & RING_MASK] = NULL;
                consumed++;
            }
        } else if (atomic_load(&r->done) >= n_producers) {
            /* All producers done — drain remaining */
            head = atomic_load(&r->head);
            tail = atomic_load(&r->tail);
            if (tail >= head) break;
        } else {
            sched_yield();
        }
    }

    a->count = consumed;
    return NULL;
}

/* ── Shared-pool workload ───────────────────────────────────────────── */

/*
 * All threads randomly allocate into or free from a shared array.
 * Protected by a spinlock for simplicity — the contention IS the point.
 */
#define POOL_SIZE 65536

typedef struct {
    void           *slots[POOL_SIZE];
    _Atomic int     lock;
    _Atomic long    alloc_count;
    _Atomic long    free_count;
} shared_pool_t;

static shared_pool_t shared_pool;

static void pool_init(shared_pool_t *p)
{
    memset(p->slots, 0, sizeof(p->slots));
    atomic_store(&p->lock, 0);
    atomic_store(&p->alloc_count, 0);
    atomic_store(&p->free_count, 0);
}

static inline void pool_lock(shared_pool_t *p)
{
    int expected = 0;
    while (!atomic_compare_exchange_weak(&p->lock, &expected, 1)) {
        expected = 0;
        sched_yield();
    }
}

static inline void pool_unlock(shared_pool_t *p)
{
    atomic_store(&p->lock, 0);
}

typedef struct {
    int     thread_id;
    int     core;
    long    ops;
    double  ops_per_sec;
} pool_arg_t;

static void *shared_pool_worker(void *arg)
{
    pool_arg_t *a = (pool_arg_t *)arg;
    pin_to_core(a->core);

    uint64_t rng = 0xBEEF0000ULL + (uint64_t)a->thread_id * 3571ULL;

    uint64_t t0 = now_ns();

    for (long i = 0; i < a->ops; i++) {
        long idx = (long)(xorshift64(&rng) % POOL_SIZE);

        pool_lock(&shared_pool);
        if (shared_pool.slots[idx]) {
            /* Free existing */
            free(shared_pool.slots[idx]);
            shared_pool.slots[idx] = NULL;
            atomic_fetch_add(&shared_pool.free_count, 1);
        }
        /* Allocate new */
        size_t sz = rand_size(&rng, ALLOC_SIZE_MIN, ALLOC_SIZE_MAX);
        shared_pool.slots[idx] = malloc(sz);
        if (shared_pool.slots[idx])
            ((char *)shared_pool.slots[idx])[0] = 1;
        atomic_fetch_add(&shared_pool.alloc_count, 1);
        pool_unlock(&shared_pool);
    }

    uint64_t t1 = now_ns();
    a->ops_per_sec = (double)(a->ops) / elapsed_s(t0, t1);

    return NULL;
}

/* ── Run helpers ────────────────────────────────────────────────────── */

static int *get_core_list(int nthreads)
{
    int ncores = get_num_cores();
    int *cores = malloc(nthreads * sizeof(int));
    for (int i = 0; i < nthreads; i++)
        cores[i] = i % ncores;
    return cores;
}

static double run_thread_local(int nthreads)
{
    pthread_t *tids = malloc(nthreads * sizeof(pthread_t));
    thread_result_t *res = malloc(nthreads * sizeof(thread_result_t));
    int *cores = get_core_list(nthreads);

    uint64_t t0 = now_ns();

    for (int i = 0; i < nthreads; i++) {
        res[i].thread_id = i;
        res[i].core = cores[i];
        res[i].ops = ops_per_thread;
        pthread_create(&tids[i], NULL, thread_local_worker, &res[i]);
    }
    for (int i = 0; i < nthreads; i++)
        pthread_join(tids[i], NULL);

    uint64_t t1 = now_ns();
    double total_ops = (double)nthreads * ops_per_thread * 2;
    double throughput = total_ops / elapsed_s(t0, t1);

    free(tids);
    free(res);
    free(cores);
    return throughput;
}

static double run_producer_consumer(int nthreads)
{
    /*
     * Half producers, half consumers (minimum 1 each).
     * Producers allocate, consumers free (cross-thread free).
     */
    int n_producers = nthreads / 2;
    if (n_producers < 1) n_producers = 1;
    int n_consumers = nthreads - n_producers;
    if (n_consumers < 1) n_consumers = 1;

    ring_init(&pc_ring);

    pthread_t *tids = malloc(nthreads * sizeof(pthread_t));
    pc_arg_t *args = malloc(nthreads * sizeof(pc_arg_t));
    int *cores = get_core_list(nthreads);

    long ops_per_producer = ops_per_thread;

    uint64_t t0 = now_ns();

    /* Start producers */
    for (int i = 0; i < n_producers; i++) {
        args[i].thread_id = i;
        args[i].core = cores[i];
        args[i].ops = ops_per_producer;
        args[i].is_producer = 1;
        args[i].ring = &pc_ring;
        args[i].count = 0;
        pthread_create(&tids[i], NULL, producer_worker, &args[i]);
    }
    /* Start consumers */
    for (int i = 0; i < n_consumers; i++) {
        int idx = n_producers + i;
        args[idx].thread_id = idx;
        args[idx].core = cores[idx];
        args[idx].ops = n_producers;  /* pass producer count for done check */
        args[idx].is_producer = 0;
        args[idx].ring = &pc_ring;
        args[idx].count = 0;
        pthread_create(&tids[idx], NULL, consumer_worker, &args[idx]);
    }

    for (int i = 0; i < n_producers + n_consumers; i++)
        pthread_join(tids[i], NULL);

    uint64_t t1 = now_ns();

    long total_produced = 0;
    for (int i = 0; i < n_producers; i++)
        total_produced += args[i].count;

    double throughput = (double)(total_produced * 2) / elapsed_s(t0, t1);

    free(tids);
    free(args);
    free(cores);
    return throughput;
}

static double run_shared_pool(int nthreads)
{
    pool_init(&shared_pool);

    pthread_t *tids = malloc(nthreads * sizeof(pthread_t));
    pool_arg_t *args = malloc(nthreads * sizeof(pool_arg_t));
    int *cores = get_core_list(nthreads);

    uint64_t t0 = now_ns();

    for (int i = 0; i < nthreads; i++) {
        args[i].thread_id = i;
        args[i].core = cores[i];
        args[i].ops = ops_per_thread;
        pthread_create(&tids[i], NULL, shared_pool_worker, &args[i]);
    }
    for (int i = 0; i < nthreads; i++)
        pthread_join(tids[i], NULL);

    uint64_t t1 = now_ns();

    long total_ops = atomic_load(&shared_pool.alloc_count) +
                     atomic_load(&shared_pool.free_count);
    double throughput = (double)total_ops / elapsed_s(t0, t1);

    /* Cleanup pool */
    for (int i = 0; i < POOL_SIZE; i++) {
        if (shared_pool.slots[i]) {
            free(shared_pool.slots[i]);
            shared_pool.slots[i] = NULL;
        }
    }

    free(tids);
    free(args);
    free(cores);
    return throughput;
}

/* ── Workload table ─────────────────────────────────────────────────── */

typedef double (*mt_bench_fn)(int nthreads);

static struct {
    const char  *name;
    mt_bench_fn  fn;
} mt_workloads[] = {
    { "thread_local",      run_thread_local },
    { "producer_consumer", run_producer_consumer },
    { "shared_pool",       run_shared_pool },
};
static const int NUM_MT_WORKLOADS = sizeof(mt_workloads) / sizeof(mt_workloads[0]);

/* ── Thread counts ──────────────────────────────────────────────────── */

#define MAX_THREAD_COUNTS 32
static int thread_counts[MAX_THREAD_COUNTS];
static int num_thread_counts = 0;

static void parse_thread_counts(const char *spec)
{
    /* Parse comma-separated list: "1,2,4,8,16" */
    char *buf = strdup(spec);
    char *tok = strtok(buf, ",");
    while (tok && num_thread_counts < MAX_THREAD_COUNTS) {
        int n = atoi(tok);
        if (n > 0) thread_counts[num_thread_counts++] = n;
        tok = strtok(NULL, ",");
    }
    free(buf);
}

static void default_thread_counts(void)
{
    int ncores = get_num_cores();
    int max_threads = ncores * 2;
    /* Powers of 2 up to max, plus max itself */
    thread_counts[num_thread_counts++] = 1;
    for (int n = 2; n <= max_threads && num_thread_counts < MAX_THREAD_COUNTS; n *= 2)
        thread_counts[num_thread_counts++] = n;
    /* Add max if not already there */
    if (thread_counts[num_thread_counts - 1] != max_threads && num_thread_counts < MAX_THREAD_COUNTS)
        thread_counts[num_thread_counts++] = max_threads;
}

/* ── Main ───────────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [--csv] [--threads 1,2,4,8] [workload]\n\n"
        "Workloads: thread_local, producer_consumer, shared_pool\n\n"
        "Environment:\n"
        "  OPS=N          Operations per thread (default: %d)\n"
        "  LD_PRELOAD=... Swap allocator\n",
        prog, DEFAULT_OPS_PER_THREAD);
}

int main(int argc, char *argv[])
{
    const char *filter = NULL;
    ops_per_thread = get_ops_env();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0)
            csv_mode = 1;
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            parse_thread_counts(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else
            filter = argv[i];
    }

    if (num_thread_counts == 0)
        default_thread_counts();

    if (csv_mode) {
        printf("allocator,workload,threads,ops_per_sec,elapsed_ms\n");
    } else {
        printf("Memory Allocator Multithreaded Scalability\n");
        print_separator();
        printf("  Allocator      : %s\n", detect_allocator());
        printf("  Cores          : %d\n", get_num_cores());
        printf("  Ops per thread : %ld\n", ops_per_thread);
        printf("  Thread counts  : ");
        for (int i = 0; i < num_thread_counts; i++)
            printf("%d%s", thread_counts[i], i < num_thread_counts - 1 ? "," : "\n");
    }

    for (int w = 0; w < NUM_MT_WORKLOADS; w++) {
        if (filter && strcmp(filter, mt_workloads[w].name) != 0)
            continue;

        if (!csv_mode) {
            printf("\n  Workload: %s\n", mt_workloads[w].name);
            print_separator();
            printf("  %8s  %15s  %10s\n", "threads", "ops/sec", "time_ms");
        }

        for (int t = 0; t < num_thread_counts; t++) {
            int nthreads = thread_counts[t];

            /* For producer-consumer, need at least 2 threads */
            if (strcmp(mt_workloads[w].name, "producer_consumer") == 0 && nthreads < 2)
                continue;

            uint64_t t0 = now_ns();
            double throughput = mt_workloads[w].fn(nthreads);
            uint64_t t1 = now_ns();
            double ms = elapsed_ms(t0, t1);

            if (csv_mode) {
                printf("%s,%s,%d,%.0f,%.1f\n",
                       detect_allocator(), mt_workloads[w].name,
                       nthreads, throughput, ms);
            } else {
                char buf[32];
                printf("  %8d  %15s  %10.1f\n",
                       nthreads, format_ops(throughput, buf, sizeof(buf)), ms);
            }
            fflush(stdout);
        }
    }

    return 0;
}
