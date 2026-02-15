/*
 * bench_single.c — Milestone 1: Single-threaded micro-benchmark harness
 *
 * Tests malloc/free throughput, latency, RSS, and fragmentation across
 * five workload patterns:
 *   1. Small allocs   (8–64 bytes)
 *   2. Medium allocs  (1 KB–64 KB)
 *   3. Large allocs   (1 MB–4 MB)
 *   4. Mixed sizes    (log-normal distribution, realistic)
 *   5. Alloc/free churn (fragment-inducing pattern)
 *
 * Usage:
 *   ./bench_single [--csv] [workload_name]
 *   LD_PRELOAD=/path/to/libjemalloc.so ./bench_single
 *
 * Environment:
 *   OPS=N  — override number of operations per workload (default varies)
 */
#include "common.h"

/* ── Workload parameters ────────────────────────────────────────────── */

static long get_ops(long default_ops)
{
    const char *env = getenv("OPS");
    if (env) {
        long val = atol(env);
        if (val > 0) return val;
    }
    return default_ops;
}

/* ── Benchmark result ───────────────────────────────────────────────── */

typedef struct {
    const char *name;
    long        ops;
    double      elapsed_ms;
    double      ops_per_sec;
    long        rss_before_kb;
    long        rss_peak_kb;
    long        rss_after_kb;
    long        live_bytes;       /* bytes currently allocated (at peak) */
    double      frag_ratio;       /* peak RSS / live_bytes */
    lat_histogram_t lat_alloc;
    lat_histogram_t lat_free;
} bench_result_t;

/* ── 1. Small allocations (8–64 bytes) ──────────────────────────────── */

static bench_result_t bench_small_allocs(long ops)
{
    bench_result_t r = { .name = "small_allocs", .ops = ops };
    lat_hist_init(&r.lat_alloc);
    lat_hist_init(&r.lat_free);

    uint64_t rng = 0xDEADBEEFCAFE1234ULL;
    void **ptrs = malloc(ops * sizeof(void *));
    if (!ptrs) { perror("malloc ptrs"); exit(1); }

    r.rss_before_kb = get_rss_kb();

    /* Allocate */
    uint64_t t0 = now_ns();
    long total_bytes = 0;
    for (long i = 0; i < ops; i++) {
        size_t sz = rand_size(&rng, 8, 64);
        uint64_t a = now_ns();
        ptrs[i] = malloc(sz);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_alloc, b - a);
        if (!ptrs[i]) { fprintf(stderr, "malloc failed at op %ld\n", i); break; }
        /* Touch the memory to ensure it's mapped */
        ((char *)ptrs[i])[0] = (char)i;
        total_bytes += sz;
    }
    r.rss_peak_kb = get_rss_kb();
    r.live_bytes = total_bytes;
    if (r.rss_peak_kb > 0 && total_bytes > 0)
        r.frag_ratio = (double)(r.rss_peak_kb * 1024L) / total_bytes;

    /* Free */
    for (long i = 0; i < ops; i++) {
        uint64_t a = now_ns();
        free(ptrs[i]);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_free, b - a);
    }
    uint64_t t1 = now_ns();

    r.rss_after_kb = get_rss_kb();
    r.elapsed_ms = elapsed_ms(t0, t1);
    r.ops_per_sec = (double)(ops * 2) / elapsed_s(t0, t1); /* alloc + free */

    free(ptrs);
    return r;
}

/* ── 2. Medium allocations (1 KB–64 KB) ────────────────────────────── */

static bench_result_t bench_medium_allocs(long ops)
{
    bench_result_t r = { .name = "medium_allocs", .ops = ops };
    lat_hist_init(&r.lat_alloc);
    lat_hist_init(&r.lat_free);

    uint64_t rng = 0xCAFEBABE12345678ULL;
    void **ptrs = malloc(ops * sizeof(void *));
    if (!ptrs) { perror("malloc ptrs"); exit(1); }

    r.rss_before_kb = get_rss_kb();

    uint64_t t0 = now_ns();
    long total_bytes = 0;
    for (long i = 0; i < ops; i++) {
        size_t sz = rand_size(&rng, 1024, 65536);
        uint64_t a = now_ns();
        ptrs[i] = malloc(sz);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_alloc, b - a);
        if (!ptrs[i]) { fprintf(stderr, "malloc failed at op %ld\n", i); break; }
        ((char *)ptrs[i])[0] = (char)i;
        total_bytes += sz;
    }
    r.rss_peak_kb = get_rss_kb();
    r.live_bytes = total_bytes;
    if (r.rss_peak_kb > 0 && total_bytes > 0)
        r.frag_ratio = (double)(r.rss_peak_kb * 1024L) / total_bytes;

    for (long i = 0; i < ops; i++) {
        uint64_t a = now_ns();
        free(ptrs[i]);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_free, b - a);
    }
    uint64_t t1 = now_ns();

    r.rss_after_kb = get_rss_kb();
    r.elapsed_ms = elapsed_ms(t0, t1);
    r.ops_per_sec = (double)(ops * 2) / elapsed_s(t0, t1);

    free(ptrs);
    return r;
}

/* ── 3. Large allocations (1 MB–4 MB) ──────────────────────────────── */

static bench_result_t bench_large_allocs(long ops)
{
    bench_result_t r = { .name = "large_allocs", .ops = ops };
    lat_hist_init(&r.lat_alloc);
    lat_hist_init(&r.lat_free);

    uint64_t rng = 0xFEEDFACEBEEF0001ULL;
    void **ptrs = malloc(ops * sizeof(void *));
    if (!ptrs) { perror("malloc ptrs"); exit(1); }

    r.rss_before_kb = get_rss_kb();

    uint64_t t0 = now_ns();
    long total_bytes = 0;
    for (long i = 0; i < ops; i++) {
        size_t sz = rand_size(&rng, 1024 * 1024, 4 * 1024 * 1024);
        uint64_t a = now_ns();
        ptrs[i] = malloc(sz);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_alloc, b - a);
        if (!ptrs[i]) { fprintf(stderr, "malloc failed at op %ld\n", i); break; }
        /* Touch first and last page to force mapping */
        ((char *)ptrs[i])[0] = (char)i;
        ((char *)ptrs[i])[sz - 1] = (char)i;
        total_bytes += sz;
    }
    r.rss_peak_kb = get_rss_kb();
    r.live_bytes = total_bytes;
    if (r.rss_peak_kb > 0 && total_bytes > 0)
        r.frag_ratio = (double)(r.rss_peak_kb * 1024L) / total_bytes;

    for (long i = 0; i < ops; i++) {
        uint64_t a = now_ns();
        free(ptrs[i]);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_free, b - a);
    }
    uint64_t t1 = now_ns();

    r.rss_after_kb = get_rss_kb();
    r.elapsed_ms = elapsed_ms(t0, t1);
    r.ops_per_sec = (double)(ops * 2) / elapsed_s(t0, t1);

    free(ptrs);
    return r;
}

/* ── 4. Mixed sizes (log-normal distribution) ──────────────────────── */

static bench_result_t bench_mixed_allocs(long ops)
{
    bench_result_t r = { .name = "mixed_allocs", .ops = ops };
    lat_hist_init(&r.lat_alloc);
    lat_hist_init(&r.lat_free);

    uint64_t rng = 0xABCD1234FEED5678ULL;
    void **ptrs = malloc(ops * sizeof(void *));
    if (!ptrs) { perror("malloc ptrs"); exit(1); }

    r.rss_before_kb = get_rss_kb();

    /*
     * Log-normal with mu=6, sigma=2 gives a distribution centered around
     * ~400 bytes with a long tail up to ~100KB+.
     * This mimics real application allocation patterns.
     */
    uint64_t t0 = now_ns();
    long total_bytes = 0;
    for (long i = 0; i < ops; i++) {
        size_t sz = rand_size_lognormal(&rng, 6.0, 2.0);
        if (sz > 256 * 1024) sz = 256 * 1024;  /* cap at 256 KB */
        uint64_t a = now_ns();
        ptrs[i] = malloc(sz);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_alloc, b - a);
        if (!ptrs[i]) { fprintf(stderr, "malloc failed at op %ld\n", i); break; }
        ((char *)ptrs[i])[0] = (char)i;
        total_bytes += sz;
    }
    r.rss_peak_kb = get_rss_kb();
    r.live_bytes = total_bytes;
    if (r.rss_peak_kb > 0 && total_bytes > 0)
        r.frag_ratio = (double)(r.rss_peak_kb * 1024L) / total_bytes;

    for (long i = 0; i < ops; i++) {
        uint64_t a = now_ns();
        free(ptrs[i]);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_free, b - a);
    }
    uint64_t t1 = now_ns();

    r.rss_after_kb = get_rss_kb();
    r.elapsed_ms = elapsed_ms(t0, t1);
    r.ops_per_sec = (double)(ops * 2) / elapsed_s(t0, t1);

    free(ptrs);
    return r;
}

/* ── 5. Alloc/free churn (fragmentation stress test) ────────────────── */

static bench_result_t bench_churn(long ops)
{
    bench_result_t r = { .name = "alloc_free_churn", .ops = ops };
    lat_hist_init(&r.lat_alloc);
    lat_hist_init(&r.lat_free);

    uint64_t rng = 0x1234ABCDDEAD5678ULL;
    long pool_size = ops / 2;
    if (pool_size < 1000) pool_size = 1000;
    void **ptrs = calloc(pool_size, sizeof(void *));
    size_t *sizes = calloc(pool_size, sizeof(size_t));
    if (!ptrs || !sizes) { perror("calloc"); exit(1); }

    r.rss_before_kb = get_rss_kb();

    uint64_t t0 = now_ns();
    long total_allocs = 0;
    long total_frees = 0;
    long live_bytes = 0;
    long peak_live = 0;

    /* Phase 1: Fill half the pool */
    for (long i = 0; i < pool_size / 2; i++) {
        size_t sz = rand_size_lognormal(&rng, 5.5, 1.5);
        if (sz > 64 * 1024) sz = 64 * 1024;
        uint64_t a = now_ns();
        ptrs[i] = malloc(sz);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_alloc, b - a);
        if (ptrs[i]) {
            ((char *)ptrs[i])[0] = 1;
            sizes[i] = sz;
            live_bytes += sz;
            total_allocs++;
        }
    }

    /* Phase 2: Churn — randomly free and re-allocate */
    for (long i = 0; i < ops; i++) {
        long idx = (long)(xorshift64(&rng) % pool_size);
        if (ptrs[idx]) {
            uint64_t a = now_ns();
            free(ptrs[idx]);
            uint64_t b = now_ns();
            lat_hist_record(&r.lat_free, b - a);
            live_bytes -= sizes[idx];
            ptrs[idx] = NULL;
            sizes[idx] = 0;
            total_frees++;
        }
        /* Re-allocate with a different size */
        size_t sz = rand_size_lognormal(&rng, 5.5, 1.5);
        if (sz > 64 * 1024) sz = 64 * 1024;
        uint64_t a = now_ns();
        ptrs[idx] = malloc(sz);
        uint64_t b = now_ns();
        lat_hist_record(&r.lat_alloc, b - a);
        if (ptrs[idx]) {
            ((char *)ptrs[idx])[0] = 1;
            sizes[idx] = sz;
            live_bytes += sz;
            total_allocs++;
            if (live_bytes > peak_live) peak_live = live_bytes;
        }
    }

    r.rss_peak_kb = get_rss_kb();
    r.live_bytes = peak_live;
    if (r.rss_peak_kb > 0 && peak_live > 0)
        r.frag_ratio = (double)(r.rss_peak_kb * 1024L) / peak_live;

    /* Cleanup */
    for (long i = 0; i < pool_size; i++) {
        if (ptrs[i]) {
            free(ptrs[i]);
            total_frees++;
        }
    }
    uint64_t t1 = now_ns();

    r.rss_after_kb = get_rss_kb();
    r.elapsed_ms = elapsed_ms(t0, t1);
    r.ops_per_sec = (double)(total_allocs + total_frees) / elapsed_s(t0, t1);

    free(ptrs);
    free(sizes);
    return r;
}

/* ── Output ─────────────────────────────────────────────────────────── */

static int csv_mode = 0;

static void print_result(const bench_result_t *r)
{
    if (csv_mode) {
        printf("%s,%s,%ld,%.1f,%.0f,%ld,%ld,%ld,%ld,%.2f,"
               "%lu,%lu,%lu,%lu,%lu,"
               "%lu,%lu,%lu,%lu,%lu\n",
               detect_allocator(), r->name, r->ops,
               r->elapsed_ms, r->ops_per_sec,
               r->rss_before_kb, r->rss_peak_kb, r->rss_after_kb,
               r->live_bytes, r->frag_ratio,
               r->lat_alloc.min_ns,
               lat_hist_percentile(&r->lat_alloc, 50),
               lat_hist_percentile(&r->lat_alloc, 95),
               lat_hist_percentile(&r->lat_alloc, 99),
               r->lat_alloc.max_ns,
               r->lat_free.min_ns,
               lat_hist_percentile(&r->lat_free, 50),
               lat_hist_percentile(&r->lat_free, 95),
               lat_hist_percentile(&r->lat_free, 99),
               r->lat_free.max_ns);
        return;
    }

    char buf1[32], buf2[32], buf3[32], buf4[32];
    printf("\n  Workload: %s\n", r->name);
    print_separator();
    printf("  Operations        : %ld\n", r->ops);
    printf("  Total time        : %.1f ms\n", r->elapsed_ms);
    printf("  Throughput        : %s ops/sec\n", format_ops(r->ops_per_sec, buf1, sizeof(buf1)));
    printf("  RSS before        : %s\n", format_bytes(r->rss_before_kb * 1024L, buf2, sizeof(buf2)));
    printf("  RSS peak          : %s\n", format_bytes(r->rss_peak_kb * 1024L, buf3, sizeof(buf3)));
    printf("  RSS after free    : %s\n", format_bytes(r->rss_after_kb * 1024L, buf4, sizeof(buf4)));
    printf("  Live bytes (peak) : %s\n", format_bytes(r->live_bytes, buf1, sizeof(buf1)));
    printf("  Frag ratio        : %.2f  (RSS / live bytes; 1.0 = perfect)\n", r->frag_ratio);
    printf("  Alloc latency:\n");
    lat_hist_print(&r->lat_alloc, "malloc");
    printf("  Free latency:\n");
    lat_hist_print(&r->lat_free, "free");
}

static void print_csv_header(void)
{
    printf("allocator,workload,ops,elapsed_ms,ops_per_sec,"
           "rss_before_kb,rss_peak_kb,rss_after_kb,live_bytes,frag_ratio,"
           "alloc_min_ns,alloc_p50_ns,alloc_p95_ns,alloc_p99_ns,alloc_max_ns,"
           "free_min_ns,free_p50_ns,free_p95_ns,free_p99_ns,free_max_ns\n");
}

/* ── Main ───────────────────────────────────────────────────────────── */

typedef bench_result_t (*bench_fn)(long ops);

static struct {
    const char  *name;
    bench_fn     fn;
    long         default_ops;
} workloads[] = {
    { "small_allocs",     bench_small_allocs,  2000000 },
    { "medium_allocs",    bench_medium_allocs,   100000 },
    { "large_allocs",     bench_large_allocs,      500 },
    { "mixed_allocs",     bench_mixed_allocs,  1000000 },
    { "alloc_free_churn", bench_churn,         2000000 },
};
static const int NUM_WORKLOADS = sizeof(workloads) / sizeof(workloads[0]);

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [--csv] [workload_name]\n\n", prog);
    fprintf(stderr, "Workloads: ");
    for (int i = 0; i < NUM_WORKLOADS; i++)
        fprintf(stderr, "%s%s", workloads[i].name, i < NUM_WORKLOADS - 1 ? ", " : "\n");
    fprintf(stderr, "\nEnvironment:\n");
    fprintf(stderr, "  OPS=N          Override operation count\n");
    fprintf(stderr, "  LD_PRELOAD=... Swap allocator\n");
}

int main(int argc, char *argv[])
{
    const char *filter = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0)
            csv_mode = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else
            filter = argv[i];
    }

    if (!csv_mode) {
        printf("Memory Allocator Micro-Benchmark\n");
        print_separator();
        printf("  Allocator: %s\n", detect_allocator());
        printf("  Cores    : %d\n", get_num_cores());
        printf("  PID      : %d\n", getpid());
    } else {
        print_csv_header();
    }

    for (int i = 0; i < NUM_WORKLOADS; i++) {
        if (filter && strcmp(filter, workloads[i].name) != 0)
            continue;

        long ops = get_ops(workloads[i].default_ops);
        bench_result_t r = workloads[i].fn(ops);
        print_result(&r);
    }

    return 0;
}
