/* cpu_stress.c — CPU-bound workload for testing runqlat
 *
 * Spawns N threads (default: 2 * nproc) doing tight FP loops.
 * More threads than CPUs → run-queue contention → measurable latency.
 *
 * Usage: ./cpu_stress [seconds] [threads]
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>

static volatile sig_atomic_t running = 1;

static void sig_handler(int sig)
{
	(void)sig;
	running = 0;
}

static void *worker(void *arg)
{
	(void)arg;
	double x = 1.0001;

	while (running) {
		/* Tight FP loop — keeps the CPU busy */
		for (int i = 0; i < 100000; i++)
			x = sin(x) * cos(x) + 1.0001;
	}

	/* Prevent the compiler from optimizing away the loop */
	if (x == 0.0)
		printf("%f\n", x);

	return NULL;
}

int main(int argc, char **argv)
{
	int duration = 10;
	int nthreads = 0;

	if (argc > 1)
		duration = atoi(argv[1]);
	if (argc > 2)
		nthreads = atoi(argv[2]);

	if (nthreads <= 0) {
		long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
		nthreads = (int)(ncpus * 2);
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	printf("cpu_stress: %d threads for %d seconds (PID %d)\n",
	       nthreads, duration, getpid());

	pthread_t *tids = calloc(nthreads, sizeof(pthread_t));
	if (!tids) {
		perror("calloc");
		return 1;
	}

	for (int i = 0; i < nthreads; i++) {
		if (pthread_create(&tids[i], NULL, worker, NULL) != 0) {
			perror("pthread_create");
			running = 0;
			break;
		}
	}

	/* Run for the requested duration, or until signal */
	for (int s = 0; s < duration && running; s++)
		sleep(1);

	running = 0;

	for (int i = 0; i < nthreads; i++)
		pthread_join(tids[i], NULL);

	free(tids);
	printf("cpu_stress: done\n");
	return 0;
}
