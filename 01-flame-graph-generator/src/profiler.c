/*
 * profiler.c — Milestone 2: External Process Profiler via perf_event_open
 *
 * Profiles an external process by sampling CPU stack traces using the
 * Linux perf subsystem. Outputs folded stacks to stdout.
 *
 * Usage:
 *   ./profiler -p <pid> [-d <seconds>] [-f <freq>] [-o <outfile>]
 *
 * Requires: perf_event_paranoid <= 1, or CAP_PERFMON, or root
 *   sudo sysctl kernel.perf_event_paranoid=-1
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <poll.h>
#include <linux/perf_event.h>
#include <time.h>

#include "symbols.h"

/* ── Configuration ──────────────────────────────────────────── */

#define MAX_STACK_DEPTH   64
#define MAX_SAMPLES       500000
#define MMAP_PAGES        128   /* ring buffer size: (1 + MMAP_PAGES) * page_size */

/* ── Perf helpers ───────────────────────────────────────────── */

static long perf_event_open(struct perf_event_attr *attr, pid_t pid,
                            int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

/* ── Raw sample storage ─────────────────────────────────────── */

struct raw_sample {
    uint64_t ips[MAX_STACK_DEPTH];
    int      depth;
};

static struct raw_sample *raw_samples;
static int n_samples = 0;

/* ── Ring buffer reading ────────────────────────────────────── */

struct ring_buffer {
    struct perf_event_mmap_page *meta;
    char *data;
    size_t data_size;
};

static void rb_init(struct ring_buffer *rb, void *mmap_base, size_t mmap_size)
{
    rb->meta = (struct perf_event_mmap_page *)mmap_base;
    rb->data = (char *)mmap_base + sysconf(_SC_PAGESIZE);
    rb->data_size = mmap_size - sysconf(_SC_PAGESIZE);
}

static void rb_read(struct ring_buffer *rb, void *dest, size_t offset, size_t len)
{
    size_t mask = rb->data_size - 1;
    for (size_t i = 0; i < len; i++)
        ((char *)dest)[i] = rb->data[(offset + i) & mask];
}

static void process_samples(struct ring_buffer *rb)
{
    uint64_t head = __atomic_load_n(&rb->meta->data_head, __ATOMIC_ACQUIRE);
    uint64_t tail = rb->meta->data_tail;

    while (tail < head && n_samples < MAX_SAMPLES) {
        struct perf_event_header hdr;
        rb_read(rb, &hdr, tail, sizeof(hdr));

        if (hdr.type == PERF_RECORD_SAMPLE) {
            /* Layout: { u64 nr; u64 ips[nr]; } for PERF_SAMPLE_CALLCHAIN */
            size_t offset = tail + sizeof(hdr);
            uint64_t nr;
            rb_read(rb, &nr, offset, sizeof(nr));
            offset += sizeof(nr);

            struct raw_sample *s = &raw_samples[n_samples];
            s->depth = 0;

            if (nr > MAX_STACK_DEPTH) nr = MAX_STACK_DEPTH;

            for (uint64_t i = 0; i < nr; i++) {
                uint64_t ip;
                rb_read(rb, &ip, offset, sizeof(ip));
                offset += sizeof(ip);

                /* Skip sentinel markers */
                if (ip >= (uint64_t)-4096)
                    continue;

                s->ips[s->depth++] = ip;
            }

            if (s->depth > 0)
                n_samples++;
        }

        tail += hdr.size;
    }

    __atomic_store_n(&rb->meta->data_tail, tail, __ATOMIC_RELEASE);
}

/* ── Folded stack output ────────────────────────────────────── */

#define MAX_STACK_STR 4096

struct folded_entry {
    char stack[MAX_STACK_STR];
    int  count;
};

static int cmp_folded(const void *a, const void *b)
{
    return strcmp(((const struct folded_entry *)a)->stack,
                 ((const struct folded_entry *)b)->stack);
}

static void output_folded(FILE *out)
{
    struct folded_entry *entries = calloc(n_samples, sizeof(*entries));
    if (!entries) { perror("calloc"); return; }

    int n_entries = 0;

    for (int i = 0; i < n_samples; i++) {
        struct raw_sample *s = &raw_samples[i];
        char buf[MAX_STACK_STR];
        int pos = 0;
        int started = 0;

        /* Stack is deepest-first; output root;...;leaf (reverse order) */
        for (int j = s->depth - 1; j >= 0; j--) {
            const char *sym = sym_resolve(s->ips[j]);

            if (strcmp(sym, "[unknown]") == 0 || strcmp(sym, "[null]") == 0)
                continue;

            if (started && pos < MAX_STACK_STR - 1)
                buf[pos++] = ';';
            int remaining = MAX_STACK_STR - pos - 1;
            int wrote = snprintf(buf + pos, remaining, "%s", sym);
            if (wrote > 0) pos += (wrote < remaining ? wrote : remaining);
            started = 1;
        }
        buf[pos] = '\0';

        if (pos > 0) {
            memcpy(entries[n_entries].stack, buf, pos + 1);
            entries[n_entries].count = 1;
            n_entries++;
        }
    }

    /* Sort and merge duplicates */
    qsort(entries, n_entries, sizeof(*entries), cmp_folded);

    int w = 0;
    for (int i = 1; i < n_entries; i++) {
        if (strcmp(entries[w].stack, entries[i].stack) == 0) {
            entries[w].count++;
        } else {
            w++;
            if (w != i) entries[w] = entries[i];
        }
    }
    int unique = (n_entries > 0) ? w + 1 : 0;

    for (int i = 0; i < unique; i++)
        fprintf(out, "%s %d\n", entries[i].stack, entries[i].count);

    fprintf(stderr, "profiler: %d unique stacks from %d samples\n", unique, n_samples);
    free(entries);
}

/* ── Signal handling ────────────────────────────────────────── */

static volatile int stop = 0;
static void handle_signal(int sig) { (void)sig; stop = 1; }

/* ── Main ───────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s -p <pid> [-d <seconds>] [-f <freq>] [-o <outfile>]\n", prog);
    fprintf(stderr, "  -p PID      Process to profile (required)\n");
    fprintf(stderr, "  -d SECONDS  Duration (default: 5)\n");
    fprintf(stderr, "  -f FREQ     Sampling frequency in Hz (default: 99)\n");
    fprintf(stderr, "  -o FILE     Output file (default: stdout)\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    int target_pid = -1;
    int duration = 5;
    int freq = 99;
    const char *outfile = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "p:d:f:o:h")) != -1) {
        switch (opt) {
        case 'p': target_pid = atoi(optarg); break;
        case 'd': duration = atoi(optarg); break;
        case 'f': freq = atoi(optarg); break;
        case 'o': outfile = optarg; break;
        default:  usage(argv[0]);
        }
    }

    if (target_pid <= 0) usage(argv[0]);

    /* Check process exists */
    if (kill(target_pid, 0) != 0) {
        fprintf(stderr, "profiler: process %d does not exist: %s\n",
                target_pid, strerror(errno));
        return 1;
    }

    raw_samples = calloc(MAX_SAMPLES, sizeof(*raw_samples));
    if (!raw_samples) { perror("calloc"); return 1; }

    /* Set up perf event */
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = PERF_TYPE_SOFTWARE;
    pe.config = PERF_COUNT_SW_CPU_CLOCK;
    pe.size = sizeof(pe);
    pe.sample_period = 0;
    pe.sample_freq = freq;
    pe.freq = 1;
    pe.sample_type = PERF_SAMPLE_CALLCHAIN;
    pe.disabled = 1;
    pe.exclude_kernel = 1;  /* user stacks only (avoids permission issues) */
    pe.exclude_hv = 1;

    int perf_fd = (int)perf_event_open(&pe, target_pid, -1, -1, 0);
    if (perf_fd < 0) {
        fprintf(stderr, "profiler: perf_event_open failed: %s\n", strerror(errno));
        fprintf(stderr, "  Try: sudo sysctl kernel.perf_event_paranoid=-1\n");
        fprintf(stderr, "  Or run as root\n");
        free(raw_samples);
        return 1;
    }

    /* mmap ring buffer */
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t mmap_size = (1 + MMAP_PAGES) * page_size;
    void *mmap_base = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
                           MAP_SHARED, perf_fd, 0);
    if (mmap_base == MAP_FAILED) {
        perror("mmap");
        close(perf_fd);
        free(raw_samples);
        return 1;
    }

    struct ring_buffer rb;
    rb_init(&rb, mmap_base, mmap_size);

    /* Load symbol info */
    if (sym_init(target_pid) < 0) {
        fprintf(stderr, "profiler: warning: could not load symbols for pid %d\n",
                target_pid);
    }

    /* Start profiling */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0);

    fprintf(stderr, "profiler: sampling PID %d at %d Hz for %d seconds...\n",
            target_pid, freq, duration);

    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    struct pollfd pfd = { .fd = perf_fd, .events = POLLIN };

    while (!stop) {
        /* Check duration */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start_ts.tv_sec) +
                         (now.tv_nsec - start_ts.tv_nsec) / 1e9;
        if (elapsed >= duration)
            break;

        int ret = poll(&pfd, 1, 100);  /* 100ms timeout */
        if (ret > 0)
            process_samples(&rb);
    }

    /* Final drain */
    process_samples(&rb);

    ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0);

    fprintf(stderr, "profiler: collected %d samples\n", n_samples);

    /* Output */
    FILE *out = stdout;
    if (outfile) {
        out = fopen(outfile, "w");
        if (!out) { perror(outfile); out = stdout; }
    }

    output_folded(out);

    if (out != stdout) fclose(out);

    /* Cleanup */
    munmap(mmap_base, mmap_size);
    close(perf_fd);
    sym_cleanup();
    free(raw_samples);

    return 0;
}
