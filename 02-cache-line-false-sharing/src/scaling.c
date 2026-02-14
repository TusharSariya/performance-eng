/*
 * scaling.c — Milestone 3: Thread Scaling Experiment
 *
 * Measures throughput vs thread count for three modes:
 *   packed     — all counters adjacent (false sharing)
 *   padded     — each counter on its own cache line
 *   true_share — all threads atomically increment ONE shared counter
 *
 * Usage:
 *   ./scaling                    # pretty-print table
 *   ./scaling --csv              # output CSV for plotting
 *   ./scaling --threads 1,2,4,8  # custom thread counts
 */
#include "common.h"

#define MAX_THREADS 256

/* ── Counter layouts ────────────────────────────────────────── */

/* Packed: array of longs, adjacent in memory */
static _Atomic long packed_counters[MAX_THREADS];

/* Padded: each counter on its own cache line */
struct padded_counter {
    _Atomic long value;
    char _pad[CACHE_LINE_SIZE - sizeof(_Atomic long)];
} CACHE_ALIGNED;
static struct padded_counter padded_counters[MAX_THREADS] CACHE_ALIGNED;

/* True sharing: single atomic counter */
static _Atomic long shared_counter;

/* ── Thread work ────────────────────────────────────────────── */

enum mode { MODE_PACKED, MODE_PADDED, MODE_TRUE_SHARE };

struct thread_args {
    enum mode mode;
    int thread_id;
    long iterations;
    int core;
};

static void *worker(void *arg)
{
    struct thread_args *ta = (struct thread_args *)arg;
    pin_to_core(ta->core);
    long iters = ta->iterations;

    switch (ta->mode) {
    case MODE_PACKED: {
        _Atomic long *ctr = &packed_counters[ta->thread_id];
        for (long i = 0; i < iters; i++)
            atomic_fetch_add_explicit(ctr, 1, memory_order_relaxed);
        break;
    }
    case MODE_PADDED: {
        _Atomic long *ctr = &padded_counters[ta->thread_id].value;
        for (long i = 0; i < iters; i++)
            atomic_fetch_add_explicit(ctr, 1, memory_order_relaxed);
        break;
    }
    case MODE_TRUE_SHARE:
        for (long i = 0; i < iters; i++)
            atomic_fetch_add_explicit(&shared_counter, 1, memory_order_relaxed);
        break;
    }
    return NULL;
}

/* ── Run a benchmark for given mode and thread count ─────── */

static double run_benchmark(enum mode m, int nthreads, long iterations)
{
    /* Reset counters */
    for (int i = 0; i < nthreads; i++) {
        atomic_store(&packed_counters[i], 0);
        atomic_store(&padded_counters[i].value, 0);
    }
    atomic_store(&shared_counter, 0);

    int ncores = get_num_cores();
    pthread_t threads[MAX_THREADS];
    struct thread_args args[MAX_THREADS];

    for (int i = 0; i < nthreads; i++) {
        args[i] = (struct thread_args){
            .mode = m,
            .thread_id = i,
            .iterations = iterations,
            .core = i % ncores,
        };
    }

    uint64_t start = now_ns();
    for (int i = 0; i < nthreads; i++)
        pthread_create(&threads[i], NULL, worker, &args[i]);
    for (int i = 0; i < nthreads; i++)
        pthread_join(threads[i], NULL);
    uint64_t end = now_ns();

    return elapsed_ms(start, end);
}

static const char *mode_name(enum mode m)
{
    switch (m) {
    case MODE_PACKED:     return "packed";
    case MODE_PADDED:     return "padded";
    case MODE_TRUE_SHARE: return "true_share";
    }
    return "unknown";
}

/* ── Parse --threads argument ───────────────────────────────── */

static int parse_thread_list(const char *str, int *out, int max)
{
    int count = 0;
    char *buf = strdup(str);
    char *tok = strtok(buf, ",");
    while (tok && count < max) {
        int val = atoi(tok);
        if (val > 0 && val <= MAX_THREADS)
            out[count++] = val;
        tok = strtok(NULL, ",");
    }
    free(buf);
    return count;
}

int main(int argc, char *argv[])
{
    long iterations = get_iterations();
    int csv_mode = 0;

    /* Default thread counts */
    int thread_counts[32];
    int num_counts = 0;
    int ncores = get_num_cores();

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0) {
            csv_mode = 1;
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            num_counts = parse_thread_list(argv[++i], thread_counts, 32);
        }
    }

    /* Default: powers of 2 up to nproc */
    if (num_counts == 0) {
        for (int t = 1; t <= ncores && num_counts < 32; t *= 2)
            thread_counts[num_counts++] = t;
        /* Always include the actual core count if not a power of 2 */
        if (thread_counts[num_counts - 1] != ncores && num_counts < 32)
            thread_counts[num_counts++] = ncores;
    }

    /* Reduce iterations for higher thread counts to keep runtime reasonable */
    long base_iters = iterations;

    if (csv_mode) {
        printf("threads,mode,ops_per_sec,time_ms,total_ops\n");
    } else {
        printf("Thread Scaling Experiment: False Sharing\n");
        print_separator();
        printf("Base iterations/thread: %ld (%.0fM)\n", base_iters, base_iters / 1e6);
        printf("Available cores       : %d\n", ncores);
        print_separator();
        printf("  %-8s %-12s %15s %12s\n", "Threads", "Mode", "Ops/sec", "Time (ms)");
        print_separator();
    }

    enum mode modes[] = { MODE_PADDED, MODE_PACKED, MODE_TRUE_SHARE };
    int nmodes = 3;

    for (int ti = 0; ti < num_counts; ti++) {
        int nthreads = thread_counts[ti];
        /* Scale iterations down for higher thread counts */
        long iters = base_iters;
        if (nthreads > 4)
            iters = base_iters / (nthreads / 4);
        if (iters < 1000000)
            iters = 1000000;

        for (int mi = 0; mi < nmodes; mi++) {
            double ms = run_benchmark(modes[mi], nthreads, iters);
            double total_ops = (double)nthreads * (double)iters;
            double ops_per_sec = total_ops / (ms / 1000.0);

            if (csv_mode) {
                printf("%d,%s,%.0f,%.1f,%.0f\n",
                       nthreads, mode_name(modes[mi]), ops_per_sec, ms, total_ops);
            } else {
                printf("  %-8d %-12s %15.0f %12.1f\n",
                       nthreads, mode_name(modes[mi]), ops_per_sec, ms);
            }
        }

        if (!csv_mode && ti < num_counts - 1)
            printf("  %s\n", "--------");
    }

    if (!csv_mode) {
        print_separator();
        printf("\nExpected behavior:\n");
        printf("  - PADDED scales linearly (no contention)\n");
        printf("  - PACKED gets WORSE with more threads (false sharing)\n");
        printf("  - TRUE_SHARE gets worse (real contention on atomic)\n");
    }

    return 0;
}
