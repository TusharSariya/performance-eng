/*
 * workload.c — CPU-bound test workload with known profile distribution
 *
 * Three functions with deliberately different CPU costs:
 *   hot_function()    — ~70% of CPU time
 *   medium_function() — ~20% of CPU time
 *   cold_function()   — ~10% of CPU time
 *
 * Compile with: gcc -O1 -fno-omit-frame-pointer -g -rdynamic -o workload workload.c
 * (-O1 to prevent inlining but still optimize loops; -fno-omit-frame-pointer for stacks)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

static volatile int keep_running = 1;

static void handle_sigterm(int sig) { (void)sig; keep_running = 0; }

/* Prevent compiler from optimizing away the work */
static volatile double sink;

__attribute__((noinline))
static void hot_inner(long n)
{
    double x = 1.0;
    for (long i = 0; i < n; i++)
        x = x * 1.0000001 + 0.0000001;
    sink = x;
}

__attribute__((noinline))
static void hot_function(void)
{
    hot_inner(700000);
}

__attribute__((noinline))
static void medium_function(void)
{
    double x = 1.0;
    for (long i = 0; i < 200000; i++)
        x = x * 1.0000001 + 0.0000001;
    sink = x;
}

__attribute__((noinline))
static void cold_function(void)
{
    double x = 1.0;
    for (long i = 0; i < 100000; i++)
        x = x * 1.0000001 + 0.0000001;
    sink = x;
}

__attribute__((noinline))
static void do_work(void)
{
    hot_function();
    medium_function();
    cold_function();
}

int main(int argc, char *argv[])
{
    int seconds = 10;
    if (argc > 1) seconds = atoi(argv[1]);

    signal(SIGTERM, handle_sigterm);
    signal(SIGINT, handle_sigterm);

    fprintf(stderr, "workload: PID %d, running for %d seconds\n", getpid(), seconds);
    fprintf(stderr, "workload: expected profile — hot:70%% medium:20%% cold:10%%\n");

    time_t start = time(NULL);
    long iterations = 0;

    while (keep_running && (time(NULL) - start) < seconds) {
        do_work();
        iterations++;
    }

    fprintf(stderr, "workload: completed %ld iterations\n", iterations);
    return 0;
}
