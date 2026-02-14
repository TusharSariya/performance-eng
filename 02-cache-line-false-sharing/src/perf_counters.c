/*
 * perf_counters.c — Milestone 2: Hardware Counter Instrumentation
 *
 * Uses perf_event_open() to read hardware performance counters
 * programmatically, comparing cache behavior for packed vs padded layouts.
 */
#include "common.h"
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

/* ── Perf event helpers ─────────────────────────────────────── */

static long perf_event_open(struct perf_event_attr *attr, pid_t pid,
                            int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

struct counter_set {
    int fd_cache_refs;
    int fd_cache_misses;
    int fd_l1d_misses;
    int fd_llc_misses;
};

static void start_counters(struct counter_set *cs)
{
    int fds[] = { cs->fd_cache_refs, cs->fd_cache_misses, cs->fd_l1d_misses, cs->fd_llc_misses };
    for (int i = 0; i < 4; i++) {
        if (fds[i] >= 0) {
            ioctl(fds[i], PERF_EVENT_IOC_RESET, 0);
            ioctl(fds[i], PERF_EVENT_IOC_ENABLE, 0);
        }
    }
}

static void stop_counters(struct counter_set *cs)
{
    int fds[] = { cs->fd_cache_refs, cs->fd_cache_misses, cs->fd_l1d_misses, cs->fd_llc_misses };
    for (int i = 0; i < 4; i++) {
        if (fds[i] >= 0)
            ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);
    }
}

static long long read_counter(int fd)
{
    long long val = -1;
    if (fd >= 0) {
        if (read(fd, &val, sizeof(val)) < 0)
            val = -1;
    }
    return val;
}

struct counter_vals {
    long long cache_refs;
    long long cache_misses;
    long long l1d_misses;
    long long llc_misses;
};

static void read_all_counters(struct counter_set *cs, struct counter_vals *vals)
{
    vals->cache_refs   = read_counter(cs->fd_cache_refs);
    vals->cache_misses = read_counter(cs->fd_cache_misses);
    vals->l1d_misses   = read_counter(cs->fd_l1d_misses);
    vals->llc_misses   = read_counter(cs->fd_llc_misses);
}

static void close_counters(struct counter_set *cs)
{
    int fds[] = { cs->fd_cache_refs, cs->fd_cache_misses, cs->fd_l1d_misses, cs->fd_llc_misses };
    for (int i = 0; i < 4; i++) {
        if (fds[i] >= 0)
            close(fds[i]);
    }
}

/* ── Benchmark (same as basic_demo but single-threaded per measurement) ── */

/*
 * We run the benchmark in the MAIN thread to capture perf counters
 * (perf_event_open with pid=0 counts the calling thread).
 * Two volatile pointers simulate the two-thread write pattern by
 * alternating writes to both counters.
 */

struct packed_counters {
    _Atomic long counter_a;
    _Atomic long counter_b;
};

struct padded_counters {
    _Atomic long counter_a;
    char _pad[CACHE_LINE_SIZE - sizeof(_Atomic long)];
    _Atomic long counter_b;
};

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
    for (long i = 0; i < iters; i++)
        atomic_fetch_add_explicit(ctr, 1, memory_order_relaxed);
    return NULL;
}

static int open_process_counter(uint32_t type, uint64_t config)
{
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = type;
    pe.size = sizeof(pe);
    pe.config = config;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    pe.inherit = 1;  /* count child threads too */

    int fd = (int)perf_event_open(&pe, 0, -1, -1, 0);
    if (fd < 0)
        fprintf(stderr, "warning: perf_event_open failed: %s\n", strerror(errno));
    return fd;
}

static void open_process_counters(struct counter_set *cs)
{
    cs->fd_cache_refs   = open_process_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES);
    cs->fd_cache_misses = open_process_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);

    uint64_t l1d_config = PERF_COUNT_HW_CACHE_L1D
                        | (PERF_COUNT_HW_CACHE_OP_READ << 8)
                        | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    cs->fd_l1d_misses = open_process_counter(PERF_TYPE_HW_CACHE, l1d_config);

    uint64_t llc_config = PERF_COUNT_HW_CACHE_LL
                        | (PERF_COUNT_HW_CACHE_OP_READ << 8)
                        | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    cs->fd_llc_misses = open_process_counter(PERF_TYPE_HW_CACHE, llc_config);
}

static void run_with_counters(_Atomic long *counter_a, _Atomic long *counter_b,
                              long iterations, int core_a, int core_b,
                              const char *label, double *out_ms,
                              struct counter_vals *out_vals)
{
    atomic_store(counter_a, 0);
    atomic_store(counter_b, 0);

    struct thread_args args_a = { .counter = counter_a, .iterations = iterations, .core = core_a };
    struct thread_args args_b = { .counter = counter_b, .iterations = iterations, .core = core_b };

    struct counter_set cs;
    open_process_counters(&cs);

    start_counters(&cs);
    uint64_t t0 = now_ns();

    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker, &args_a);
    pthread_create(&t2, NULL, worker, &args_b);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    uint64_t t1_end = now_ns();
    stop_counters(&cs);
    read_all_counters(&cs, out_vals);
    close_counters(&cs);

    *out_ms = elapsed_ms(t0, t1_end);
    (void)label;
}

static void print_val(const char *name, long long packed, long long padded)
{
    double ratio = (padded > 0) ? (double)packed / (double)padded : 0;
    printf("  %-25s %15lld %15lld %10.1fx\n", name, packed, padded, ratio);
}

int main(void)
{
    long iterations = get_iterations();
    int ncores = get_num_cores();
    int core_a = 0;
    int core_b = ncores / 2;

    printf("Hardware Counter Comparison: False Sharing\n");
    print_separator();
    printf("Iterations per thread : %ld (%.0fM)\n", iterations, iterations / 1e6);
    printf("Cores                 : %d, %d\n", core_a, core_b);
    print_separator();

    struct packed_counters *packed = aligned_alloc(CACHE_LINE_SIZE, sizeof(*packed));
    struct padded_counters *padded = aligned_alloc(CACHE_LINE_SIZE,
        ((sizeof(*padded) + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE) * CACHE_LINE_SIZE);

    if (!packed || !padded) {
        perror("aligned_alloc");
        return 1;
    }

    /* Warmup */
    printf("Warmup...\n");
    struct counter_vals dummy_vals;
    double dummy_ms;
    run_with_counters(&packed->counter_a, &packed->counter_b,
                      iterations / 10, core_a, core_b, "warmup", &dummy_ms, &dummy_vals);

    /* Packed (false sharing) */
    printf("Running PACKED...\n");
    double packed_ms;
    struct counter_vals packed_vals;
    run_with_counters(&packed->counter_a, &packed->counter_b,
                      iterations, core_a, core_b, "packed", &packed_ms, &packed_vals);

    /* Padded (no false sharing) */
    printf("Running PADDED...\n");
    double padded_ms;
    struct counter_vals padded_vals;
    run_with_counters(&padded->counter_a, &padded->counter_b,
                      iterations, core_a, core_b, "padded", &padded_ms, &padded_vals);

    /* Results */
    printf("\n");
    print_separator();
    printf("HARDWARE COUNTER COMPARISON\n");
    print_separator();
    printf("  %-25s %15s %15s %10s\n", "Counter", "PACKED", "PADDED", "Ratio");
    print_separator();

    print_val("Cache References", packed_vals.cache_refs, padded_vals.cache_refs);
    print_val("Cache Misses", packed_vals.cache_misses, padded_vals.cache_misses);
    print_val("L1D Load Misses", packed_vals.l1d_misses, padded_vals.l1d_misses);
    print_val("LLC Load Misses", packed_vals.llc_misses, padded_vals.llc_misses);
    print_separator();
    printf("  %-25s %12.1f ms %12.1f ms %9.1fx\n",
           "Wall Clock Time", packed_ms, padded_ms, packed_ms / padded_ms);
    print_separator();

    printf("\n  False sharing causes %.1fx more cache misses and %.1fx slowdown.\n",
           (padded_vals.cache_misses > 0)
               ? (double)packed_vals.cache_misses / (double)padded_vals.cache_misses
               : 0.0,
           packed_ms / padded_ms);

    if (packed_vals.cache_misses < 0 || padded_vals.cache_misses < 0) {
        printf("\n  NOTE: Some counters returned -1 (unavailable).\n");
        printf("  Try running with: sudo ./perf_counters\n");
        printf("  Or set: sudo sysctl kernel.perf_event_paranoid=-1\n");
    }

    free(packed);
    free(padded);
    return 0;
}
