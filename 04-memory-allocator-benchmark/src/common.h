#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>

/* ── Timing ─────────────────────────────────────────────────────────── */

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline double elapsed_ms(uint64_t start, uint64_t end)
{
    return (double)(end - start) / 1e6;
}

static inline double elapsed_s(uint64_t start, uint64_t end)
{
    return (double)(end - start) / 1e9;
}

/* ── CPU pinning ────────────────────────────────────────────────────── */

static inline void pin_to_core(int core)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0)
        fprintf(stderr, "warning: failed to pin to core %d: %s\n",
                core, strerror(errno));
}

static inline int get_num_cores(void)
{
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
}

/* ── RSS measurement (/proc/self/status → VmRSS) ───────────────────── */

static inline long get_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long rss = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, " %ld", &rss);
            break;
        }
    }
    fclose(f);
    return rss;  /* in KB */
}

static inline long get_rss_bytes(void)
{
    long kb = get_rss_kb();
    return (kb > 0) ? kb * 1024L : -1;
}

/* ── Latency histogram (log2-based buckets) ─────────────────────────── */

/*
 * Records latencies in nanoseconds into log2 buckets.
 * Bucket i covers [2^i, 2^(i+1)) ns.
 * MAX_LAT_BUCKETS=32 covers up to ~4 seconds.
 */
#define MAX_LAT_BUCKETS 32

typedef struct {
    uint64_t buckets[MAX_LAT_BUCKETS];
    uint64_t count;
    uint64_t sum_ns;
    uint64_t min_ns;
    uint64_t max_ns;
} lat_histogram_t;

static inline void lat_hist_init(lat_histogram_t *h)
{
    memset(h, 0, sizeof(*h));
    h->min_ns = UINT64_MAX;
}

static inline void lat_hist_record(lat_histogram_t *h, uint64_t latency_ns)
{
    int bucket = 0;
    uint64_t v = latency_ns;
    while (v > 1 && bucket < MAX_LAT_BUCKETS - 1) {
        v >>= 1;
        bucket++;
    }
    h->buckets[bucket]++;
    h->count++;
    h->sum_ns += latency_ns;
    if (latency_ns < h->min_ns) h->min_ns = latency_ns;
    if (latency_ns > h->max_ns) h->max_ns = latency_ns;
}

static inline uint64_t lat_hist_percentile(const lat_histogram_t *h, double pct)
{
    uint64_t target = (uint64_t)(h->count * pct / 100.0);
    uint64_t cumulative = 0;
    for (int i = 0; i < MAX_LAT_BUCKETS; i++) {
        cumulative += h->buckets[i];
        if (cumulative >= target)
            return (1ULL << i);  /* lower bound of bucket */
    }
    return h->max_ns;
}

static inline void lat_hist_print(const lat_histogram_t *h, const char *label)
{
    if (h->count == 0) {
        printf("  %-20s (no samples)\n", label);
        return;
    }
    double avg_ns = (double)h->sum_ns / h->count;
    printf("  %-20s  count=%-10lu  avg=%7.0f ns  "
           "min=%lu  p50=%lu  p95=%lu  p99=%lu  max=%lu ns\n",
           label, h->count, avg_ns,
           h->min_ns,
           lat_hist_percentile(h, 50),
           lat_hist_percentile(h, 95),
           lat_hist_percentile(h, 99),
           h->max_ns);
}

/* ── Random size generators ─────────────────────────────────────────── */

/* Simple xorshift64 PRNG (fast, good enough for benchmarks) */
static inline uint64_t xorshift64(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* Random size in [lo, hi] using xorshift */
static inline size_t rand_size(uint64_t *rng, size_t lo, size_t hi)
{
    return lo + (size_t)(xorshift64(rng) % (hi - lo + 1));
}

/* Log-normal distributed size (approximation using Box-Muller on xorshift) */
static inline size_t rand_size_lognormal(uint64_t *rng, double mu, double sigma)
{
    /* Box-Muller transform for a normal sample */
    double u1 = (double)(xorshift64(rng) & 0xFFFFFFFFFULL) / (double)0xFFFFFFFFFULL;
    double u2 = (double)(xorshift64(rng) & 0xFFFFFFFFFULL) / (double)0xFFFFFFFFFULL;
    if (u1 < 1e-15) u1 = 1e-15;
    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    double val = exp(mu + sigma * z);
    size_t s = (size_t)val;
    return (s < 8) ? 8 : s;
}

/* ── Formatting helpers ─────────────────────────────────────────────── */

static inline void print_separator(void)
{
    printf("────────────────────────────────────────────────────────────────────────\n");
}

static inline const char *format_bytes(long bytes, char *buf, size_t bufsz)
{
    if (bytes >= 1024L * 1024 * 1024)
        snprintf(buf, bufsz, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024L * 1024)
        snprintf(buf, bufsz, "%.1f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024L)
        snprintf(buf, bufsz, "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, bufsz, "%ld B", bytes);
    return buf;
}

static inline const char *format_ops(double ops_sec, char *buf, size_t bufsz)
{
    if (ops_sec >= 1e9)
        snprintf(buf, bufsz, "%.2f G", ops_sec / 1e9);
    else if (ops_sec >= 1e6)
        snprintf(buf, bufsz, "%.2f M", ops_sec / 1e6);
    else if (ops_sec >= 1e3)
        snprintf(buf, bufsz, "%.2f K", ops_sec / 1e3);
    else
        snprintf(buf, bufsz, "%.0f", ops_sec);
    return buf;
}

/* ── Detect active allocator ────────────────────────────────────────── */

static inline const char *detect_allocator(void)
{
    const char *preload = getenv("LD_PRELOAD");
    if (!preload || preload[0] == '\0')
        return "glibc";
    if (strstr(preload, "jemalloc"))
        return "jemalloc";
    if (strstr(preload, "tcmalloc"))
        return "tcmalloc";
    if (strstr(preload, "mimalloc"))
        return "mimalloc";
    return "unknown";
}

#endif /* COMMON_H */
