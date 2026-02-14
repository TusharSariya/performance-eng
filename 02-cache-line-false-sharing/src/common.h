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

#define CACHE_LINE_SIZE 64

#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

/* Default iterations: 500 million. Override with env ITERATIONS. */
static inline long get_iterations(void)
{
    const char *env = getenv("ITERATIONS");
    if (env) {
        long val = atol(env);
        if (val > 0)
            return val;
    }
    return 500000000L;
}

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

static inline void pin_to_core(int core)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
        fprintf(stderr, "warning: failed to pin to core %d: %s\n",
                core, strerror(errno));
    }
}

static inline int get_num_cores(void)
{
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
}

static inline void print_separator(void)
{
    printf("────────────────────────────────────────────────────────────\n");
}

#endif /* COMMON_H */
