/*
 * selfprofile.c — Milestone 1: Signal-Based Self-Profiler
 *
 * Profiles its own execution using ITIMER_PROF + SIGPROF.
 * On each signal, captures a stack trace via backtrace().
 * After the workload, resolves symbols via dladdr() and
 * outputs folded stacks to stdout.
 *
 * Usage:
 *   ./selfprofile              # profile built-in workload, print folded stacks
 *   ./selfprofile | ./flamegraph > out.svg
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <execinfo.h>
#include <dlfcn.h>

/* ── Configuration ──────────────────────────────────────────── */

#define MAX_STACK_DEPTH 64
#define MAX_SAMPLES     100000
#define SAMPLE_FREQ_HZ  997  /* prime number avoids aliasing with loops */

/* ── Sample storage (pre-allocated, signal-safe) ────────────── */

struct stack_sample {
    void *frames[MAX_STACK_DEPTH];
    int   depth;
};

static struct stack_sample samples[MAX_SAMPLES];
static volatile int sample_count = 0;

/* ── Signal handler ─────────────────────────────────────────── */

static void sigprof_handler(int sig)
{
    (void)sig;
    if (sample_count >= MAX_SAMPLES)
        return;

    struct stack_sample *s = &samples[sample_count];
    s->depth = backtrace(s->frames, MAX_STACK_DEPTH);
    sample_count++;
}

static void start_profiling(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigprof_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);

    struct itimerval timer;
    long interval_us = 1000000 / SAMPLE_FREQ_HZ;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = interval_us;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = interval_us;
    setitimer(ITIMER_PROF, &timer, NULL);
}

static void stop_profiling(void)
{
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_PROF, &timer, NULL);
    signal(SIGPROF, SIG_DFL);
}

/* ── Symbol resolution ──────────────────────────────────────── */

static const char *resolve_symbol(void *addr)
{
    Dl_info info;
    if (dladdr(addr, &info) && info.dli_sname)
        return info.dli_sname;
    return "[unknown]";
}

/* ── Built-in workload ──────────────────────────────────────── */

static volatile double sink;

__attribute__((noinline))
void compute_hot(long n)
{
    double x = 1.0;
    for (long i = 0; i < n; i++)
        x = x * 1.0000001 + 0.0000001;
    sink = x;
}

__attribute__((noinline))
void compute_medium(long n)
{
    double x = 2.0;
    for (long i = 0; i < n; i++)
        x = x * 0.9999999 + 0.0000002;
    sink = x;
}

__attribute__((noinline))
void compute_cold(long n)
{
    double x = 3.0;
    for (long i = 0; i < n; i++)
        x = x * 1.0000002 - 0.0000001;
    sink = x;
}

__attribute__((noinline))
void run_workload(void)
{
    /* Approximate 70/20/10 split */
    for (int i = 0; i < 200; i++) {
        compute_hot(500000);
        compute_medium(143000);
        compute_cold(71500);
    }
}

/* ── Folded stack output ────────────────────────────────────── */

/*
 * Fold identical stacks and output in Brendan Gregg's folded format:
 *   func_a;func_b;func_c <count>
 *
 * We build a string key for each stack, sort, and count duplicates.
 */

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

static void output_folded_stacks(void)
{
    if (sample_count == 0) {
        fprintf(stderr, "selfprofile: no samples collected\n");
        return;
    }

    fprintf(stderr, "selfprofile: collected %d samples\n", sample_count);

    /* Build folded stack strings */
    struct folded_entry *entries = calloc(sample_count, sizeof(*entries));
    if (!entries) { perror("calloc"); return; }

    int n_entries = 0;
    for (int i = 0; i < sample_count; i++) {
        struct stack_sample *s = &samples[i];
        char buf[MAX_STACK_STR];
        int pos = 0;

        /* Walk stack bottom-up (deepest frame first in backtrace output is index 0,
           but folded format is root;...;leaf, so reverse) */
        int started = 0;
        for (int j = s->depth - 1; j >= 0; j--) {
            const char *sym = resolve_symbol(s->frames[j]);

            /* Skip profiling infrastructure frames */
            if (strcmp(sym, "sigprof_handler") == 0 ||
                strcmp(sym, "[unknown]") == 0 ||
                strcmp(sym, "__restore_rt") == 0)
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

    int write_idx = 0;
    for (int i = 1; i < n_entries; i++) {
        if (strcmp(entries[write_idx].stack, entries[i].stack) == 0) {
            entries[write_idx].count++;
        } else {
            write_idx++;
            if (write_idx != i)
                entries[write_idx] = entries[i];
        }
    }
    int unique = (n_entries > 0) ? write_idx + 1 : 0;

    /* Output */
    for (int i = 0; i < unique; i++) {
        printf("%s %d\n", entries[i].stack, entries[i].count);
    }

    fprintf(stderr, "selfprofile: %d unique stacks from %d samples\n",
            unique, sample_count);

    free(entries);
}

/* ── Main ───────────────────────────────────────────────────── */

int main(void)
{
    fprintf(stderr, "selfprofile: starting (PID %d, sampling at %d Hz)\n",
            getpid(), SAMPLE_FREQ_HZ);

    start_profiling();
    run_workload();
    stop_profiling();

    output_folded_stacks();
    return 0;
}
