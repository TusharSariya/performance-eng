/*
 * bench_frag.c — Milestone 3: Fragmentation deep-dive
 *
 * Deliberately creates fragmentation and measures its impact:
 *   Phase 1: Allocate N objects of varying sizes
 *   Phase 2: Free every other object (create "holes")
 *   Phase 3: Allocate objects of different sizes (test hole reuse)
 *   Phase 4: Free everything, measure RSS retention
 *
 * Tracks RSS at each step and outputs a time-series CSV for plotting.
 *
 * Usage:
 *   ./bench_frag [--csv] [--objects N]
 *   LD_PRELOAD=/path/to/libjemalloc.so ./bench_frag
 */
#include "common.h"

/* ── Configuration ──────────────────────────────────────────────────── */

#define DEFAULT_OBJECTS  1000000
#define SAMPLE_INTERVAL  10000    /* record RSS every N operations */

static int  csv_mode = 0;
static long num_objects = DEFAULT_OBJECTS;

/* ── RSS time-series recording ──────────────────────────────────────── */

#define MAX_SAMPLES 10000

typedef struct {
    long    step;           /* operation index */
    char    phase[16];      /* phase name */
    long    rss_kb;
    long    live_bytes;
    double  frag_ratio;     /* rss / live_bytes */
} sample_t;

static sample_t samples[MAX_SAMPLES];
static int      num_samples = 0;

static void record_sample(long step, const char *phase, long live_bytes)
{
    if (num_samples >= MAX_SAMPLES) return;
    sample_t *s = &samples[num_samples++];
    s->step = step;
    strncpy(s->phase, phase, sizeof(s->phase) - 1);
    s->phase[sizeof(s->phase) - 1] = '\0';
    s->rss_kb = get_rss_kb();
    s->live_bytes = live_bytes;
    s->frag_ratio = (live_bytes > 0)
        ? (double)(s->rss_kb * 1024L) / live_bytes
        : 0.0;
}

/* ── Main fragmentation benchmark ───────────────────────────────────── */

static void run_fragmentation_bench(void)
{
    uint64_t rng = 0xF4A61234DEAD5678ULL;

    void  **ptrs  = calloc(num_objects, sizeof(void *));
    size_t *sizes = calloc(num_objects, sizeof(size_t));
    if (!ptrs || !sizes) { perror("calloc"); exit(1); }

    long live_bytes = 0;
    long total_step = 0;
    long live_after_alloc = 0, live_after_holes = 0, live_after_realloc = 0;

    record_sample(total_step, "start", live_bytes);

    /* ── Phase 1: Allocate N objects with varying sizes ──────────── */

    if (!csv_mode) {
        printf("\n  Phase 1: Allocating %ld objects...\n", num_objects);
        fflush(stdout);
    }

    uint64_t t1_start = now_ns();
    for (long i = 0; i < num_objects; i++) {
        /*
         * Mix of sizes: 70% small (32-256), 20% medium (256-4096),
         * 10% large (4096-65536)
         */
        size_t sz;
        uint64_t r = xorshift64(&rng) % 100;
        if (r < 70)
            sz = rand_size(&rng, 32, 256);
        else if (r < 90)
            sz = rand_size(&rng, 256, 4096);
        else
            sz = rand_size(&rng, 4096, 65536);

        ptrs[i] = malloc(sz);
        if (ptrs[i]) {
            ((char *)ptrs[i])[0] = 1;
            sizes[i] = sz;
            live_bytes += sz;
        }
        total_step++;
        if (total_step % SAMPLE_INTERVAL == 0)
            record_sample(total_step, "alloc", live_bytes);
    }
    uint64_t t1_end = now_ns();
    record_sample(total_step, "alloc_done", live_bytes);

    live_after_alloc = live_bytes;
    long rss_after_alloc = get_rss_kb();
    double frag_after_alloc = (live_bytes > 0)
        ? (double)(rss_after_alloc * 1024L) / live_bytes : 0;

    if (!csv_mode) {
        char buf1[32], buf2[32];
        printf("    Time          : %.1f ms\n", elapsed_ms(t1_start, t1_end));
        printf("    Live bytes    : %s\n", format_bytes(live_bytes, buf1, sizeof(buf1)));
        printf("    RSS           : %s\n", format_bytes(rss_after_alloc * 1024L, buf2, sizeof(buf2)));
        printf("    Frag ratio    : %.2f\n", frag_after_alloc);
    }

    /* ── Phase 2: Free every other object (create holes) ─────────── */

    if (!csv_mode) {
        printf("\n  Phase 2: Freeing every other object (creating holes)...\n");
        fflush(stdout);
    }

    uint64_t t2_start = now_ns();
    long freed_count = 0;
    for (long i = 0; i < num_objects; i += 2) {
        if (ptrs[i]) {
            live_bytes -= sizes[i];
            free(ptrs[i]);
            ptrs[i] = NULL;
            sizes[i] = 0;
            freed_count++;
        }
        total_step++;
        if (total_step % SAMPLE_INTERVAL == 0)
            record_sample(total_step, "free_holes", live_bytes);
    }
    uint64_t t2_end = now_ns();
    record_sample(total_step, "holes_done", live_bytes);

    live_after_holes = live_bytes;
    long rss_after_holes = get_rss_kb();
    double frag_after_holes = (live_bytes > 0)
        ? (double)(rss_after_holes * 1024L) / live_bytes : 0;

    if (!csv_mode) {
        char buf1[32], buf2[32];
        printf("    Freed         : %ld objects\n", freed_count);
        printf("    Time          : %.1f ms\n", elapsed_ms(t2_start, t2_end));
        printf("    Live bytes    : %s\n", format_bytes(live_bytes, buf1, sizeof(buf1)));
        printf("    RSS           : %s\n", format_bytes(rss_after_holes * 1024L, buf2, sizeof(buf2)));
        printf("    Frag ratio    : %.2f  (holes created)\n", frag_after_holes);
    }

    /* ── Phase 3: Re-allocate with DIFFERENT sizes ───────────────── */

    if (!csv_mode) {
        printf("\n  Phase 3: Re-allocating freed slots with different sizes...\n");
        fflush(stdout);
    }

    uint64_t t3_start = now_ns();
    long realloc_count = 0;
    for (long i = 0; i < num_objects; i += 2) {
        /*
         * Try to allocate objects LARGER than the original holes.
         * This tests whether the allocator can coalesce or reuse fragmented space.
         */
        size_t sz;
        uint64_t r = xorshift64(&rng) % 100;
        if (r < 40)
            sz = rand_size(&rng, 512, 2048);   /* bigger than most small holes */
        else if (r < 70)
            sz = rand_size(&rng, 2048, 8192);  /* bigger than medium holes */
        else
            sz = rand_size(&rng, 8192, 131072); /* much bigger */

        ptrs[i] = malloc(sz);
        if (ptrs[i]) {
            ((char *)ptrs[i])[0] = 1;
            sizes[i] = sz;
            live_bytes += sz;
            realloc_count++;
        }
        total_step++;
        if (total_step % SAMPLE_INTERVAL == 0)
            record_sample(total_step, "realloc", live_bytes);
    }
    uint64_t t3_end = now_ns();
    record_sample(total_step, "realloc_done", live_bytes);

    live_after_realloc = live_bytes;
    long rss_after_realloc = get_rss_kb();
    double frag_after_realloc = (live_bytes > 0)
        ? (double)(rss_after_realloc * 1024L) / live_bytes : 0;

    if (!csv_mode) {
        char buf1[32], buf2[32];
        printf("    Re-allocated  : %ld objects\n", realloc_count);
        printf("    Time          : %.1f ms\n", elapsed_ms(t3_start, t3_end));
        printf("    Live bytes    : %s\n", format_bytes(live_bytes, buf1, sizeof(buf1)));
        printf("    RSS           : %s\n", format_bytes(rss_after_realloc * 1024L, buf2, sizeof(buf2)));
        printf("    Frag ratio    : %.2f  (can allocator reuse holes?)\n", frag_after_realloc);
    }

    /* ── Phase 4: Free everything, check RSS retention ───────────── */

    if (!csv_mode) {
        printf("\n  Phase 4: Freeing everything...\n");
        fflush(stdout);
    }

    uint64_t t4_start = now_ns();
    for (long i = 0; i < num_objects; i++) {
        if (ptrs[i]) {
            live_bytes -= sizes[i];
            free(ptrs[i]);
            ptrs[i] = NULL;
        }
        total_step++;
        if (total_step % SAMPLE_INTERVAL == 0)
            record_sample(total_step, "free_all", live_bytes);
    }
    uint64_t t4_end = now_ns();
    record_sample(total_step, "done", 0);

    long rss_after_free = get_rss_kb();

    if (!csv_mode) {
        char buf1[32], buf2[32];
        printf("    Time          : %.1f ms\n", elapsed_ms(t4_start, t4_end));
        printf("    RSS retained  : %s  (not returned to OS)\n",
               format_bytes(rss_after_free * 1024L, buf1, sizeof(buf1)));
        printf("    RSS peak      : %s\n",
               format_bytes(rss_after_realloc * 1024L, buf2, sizeof(buf2)));

        printf("\n  Summary:\n");
        print_separator();
        printf("  %-22s  %10s  %10s  %10s\n", "Phase", "RSS (KB)", "Live (KB)", "Frag Ratio");
        printf("  %-22s  %10ld  %10ld  %10.2f\n", "After initial alloc",
               rss_after_alloc, live_after_alloc / 1024, frag_after_alloc);
        printf("  %-22s  %10ld  %10ld  %10.2f\n", "After creating holes",
               rss_after_holes, live_after_holes / 1024, frag_after_holes);
        printf("  %-22s  %10ld  %10ld  %10.2f\n", "After re-allocation",
               rss_after_realloc, live_after_realloc / 1024, frag_after_realloc);
        printf("  %-22s  %10ld  %10ld  %10s\n", "After free all",
               rss_after_free, 0L, "-");
    }

    free(ptrs);
    free(sizes);
}

static void print_csv_samples(void)
{
    printf("allocator,step,phase,rss_kb,live_bytes,frag_ratio\n");
    for (int i = 0; i < num_samples; i++) {
        sample_t *s = &samples[i];
        printf("%s,%ld,%s,%ld,%ld,%.3f\n",
               detect_allocator(), s->step, s->phase,
               s->rss_kb, s->live_bytes, s->frag_ratio);
    }
}

/* ── Main ───────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0)
            csv_mode = 1;
        else if (strcmp(argv[i], "--objects") == 0 && i + 1 < argc) {
            num_objects = atol(argv[++i]);
            if (num_objects <= 0) num_objects = DEFAULT_OBJECTS;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [--csv] [--objects N]\n", argv[0]);
            return 0;
        }
    }

    if (!csv_mode) {
        printf("Memory Allocator Fragmentation Deep-Dive\n");
        print_separator();
        printf("  Allocator : %s\n", detect_allocator());
        printf("  Objects   : %ld\n", num_objects);
        printf("  PID       : %d\n", getpid());
    }

    run_fragmentation_bench();

    if (csv_mode)
        print_csv_samples();

    return 0;
}
