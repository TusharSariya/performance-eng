/* runqlat.c — userspace loader for the runqlat BPF program
 *
 * Loads the BPF object, attaches to scheduler tracepoints (tp_btf),
 * and periodically reads + prints the latency histogram.
 *
 * Usage: sudo ./runqlat [options] [interval [count]]
 *        -p PID     trace one process only
 *        -C         show per-CPU histograms
 *        -m         display milliseconds (default: microseconds)
 *        --csv      CSV output (timestamp,p50,p95,p99,max)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <linux/types.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "runqlat.h"

/* ── Configuration ─────────────────────────────────────────────── */

static struct {
	int	interval;	/* seconds between prints      */
	int	count;		/* number of intervals (0=inf) */
	__u32	pid;		/* target PID / TGID (0 = all) */
	int	per_cpu;	/* show per-CPU histograms     */
	int	milliseconds;	/* display in ms not us        */
	int	csv;		/* CSV output mode             */
} env = {
	.interval = 99999999,	/* default: run until Ctrl-C   */
	.count    = 1,
};

static volatile sig_atomic_t exiting;

static void sig_handler(int sig)
{
	(void)sig;
	exiting = 1;
}

/* ── Usage ─────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options] [interval [count]]\n"
		"\n"
		"Measure CPU run-queue (scheduler) latency.\n"
		"\n"
		"Options:\n"
		"  -p PID   trace this PID only\n"
		"  -C       show per-CPU histograms\n"
		"  -m       display in milliseconds (default: microseconds)\n"
		"  --csv    CSV output: timestamp,p50,p95,p99,max\n"
		"  -h       show this help\n",
		prog);
}

/* ── Histogram display ─────────────────────────────────────────── */

#define HIST_WIDTH 40

static void print_hist_header(const char *unit)
{
	printf("     %-19s : %-8s  distribution\n", unit, "count");
}

/* Print a single histogram row: "low -> high : count |***...  |" */
static void print_hist_row(__u64 low, __u64 high, __u64 count, __u64 max_count)
{
	int bar;

	printf("%10llu -> %-10llu: %-8llu |",
	       (unsigned long long)low,
	       (unsigned long long)high,
	       (unsigned long long)count);

	if (max_count > 0)
		bar = (int)((double)count / max_count * HIST_WIDTH);
	else
		bar = 0;

	for (int i = 0; i < HIST_WIDTH; i++)
		putchar(i < bar ? '*' : ' ');

	printf("|\n");
}

/* Print one full power-of-2 histogram from an array of slot counts. */
static void print_histogram(__u64 slots[], int nslots, int use_ms)
{
	__u64 max_count = 0;
	int first = -1, last = -1;
	const char *unit = use_ms ? "msecs" : "usecs";

	/* Find range of non-zero slots and max count */
	for (int i = 0; i < nslots; i++) {
		if (slots[i] > 0) {
			if (first < 0)
				first = i;
			last = i;
			if (slots[i] > max_count)
				max_count = slots[i];
		}
	}

	if (first < 0) {
		printf("     (no events)\n");
		return;
	}

	print_hist_header(unit);

	for (int i = first; i <= last; i++) {
		__u64 low  = (i == 0) ? 0 : (1ULL << i);
		__u64 high = (1ULL << (i + 1)) - 1;

		if (use_ms) {
			low  /= 1000;
			high /= 1000;
		}

		print_hist_row(low, high, slots[i], max_count);
	}
}

/* ── Percentile computation from histogram ─────────────────────── */

struct percentiles {
	__u64 p50;
	__u64 p95;
	__u64 p99;
	__u64 max;
};

static struct percentiles compute_percentiles(__u64 slots[], int nslots)
{
	struct percentiles p = {0};
	__u64 total = 0;

	for (int i = 0; i < nslots; i++)
		total += slots[i];

	if (total == 0)
		return p;

	__u64 cum = 0;
	int got50 = 0, got95 = 0, got99 = 0;

	for (int i = 0; i < nslots; i++) {
		if (slots[i] > 0)
			p.max = (1ULL << (i + 1)) - 1;  /* upper bound of slot */

		cum += slots[i];

		if (!got50 && cum >= total * 50 / 100) {
			p.p50 = (i == 0) ? 0 : (1ULL << i);
			got50 = 1;
		}
		if (!got95 && cum >= total * 95 / 100) {
			p.p95 = (i == 0) ? 0 : (1ULL << i);
			got95 = 1;
		}
		if (!got99 && cum >= total * 99 / 100) {
			p.p99 = (i == 0) ? 0 : (1ULL << i);
			got99 = 1;
		}
	}

	return p;
}

/* ── CSV output ────────────────────────────────────────────────── */

static void print_csv_header(void)
{
	printf("timestamp,p50_us,p95_us,p99_us,max_us\n");
}

static void print_csv_row(__u64 slots[], int nslots)
{
	struct percentiles p = compute_percentiles(slots, nslots);
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	printf("%ld.%03ld,%llu,%llu,%llu,%llu\n",
	       ts.tv_sec, ts.tv_nsec / 1000000,
	       (unsigned long long)p.p50,
	       (unsigned long long)p.p95,
	       (unsigned long long)p.p99,
	       (unsigned long long)p.max);
}

/* ── Read histogram from BPF map ───────────────────────────────── */

static int read_hist(int fd, __u64 slots[], int nslots)
{
	for (int i = 0; i < nslots; i++) {
		__u32 key = i;
		__u64 val = 0;

		if (bpf_map_lookup_elem(fd, &key, &val) < 0)
			return -1;
		slots[i] = val;
	}
	return 0;
}

static int clear_hist(int fd, int nslots)
{
	for (int i = 0; i < nslots; i++) {
		__u32 key = i;
		__u64 val = 0;

		if (bpf_map_update_elem(fd, &key, &val, BPF_ANY) < 0)
			return -1;
	}
	return 0;
}

/* ── .rodata configuration ─────────────────────────────────────── */

/*
 * Layout must match BPF globals declaration order in runqlat.bpf.c:
 *   const volatile __u32 targ_tgid;
 *   const volatile int   per_cpu;
 */
struct rodata {
	__u32	targ_tgid;
	int	per_cpu;
};

/*
 * Find the .rodata map — name varies across libbpf versions:
 *   "runqlat.bpf.rodata", ".rodata", or similar.
 */
static struct bpf_map *find_rodata(struct bpf_object *obj)
{
	struct bpf_map *map;

	bpf_object__for_each_map(map, obj) {
		const char *name = bpf_map__name(map);
		if (strstr(name, ".rodata"))
			return map;
	}
	return NULL;
}

/* ── Attach helper ─────────────────────────────────────────────── */

static struct bpf_link *attach_prog(struct bpf_object *obj,
				    const char *prog_name)
{
	struct bpf_program *prog;
	struct bpf_link *link;

	prog = bpf_object__find_program_by_name(obj, prog_name);
	if (!prog) {
		fprintf(stderr, "ERROR: program %s not found\n", prog_name);
		return NULL;
	}

	link = bpf_program__attach(prog);
	if (libbpf_get_error(link)) {
		fprintf(stderr, "ERROR: failed to attach %s: %s\n",
			prog_name, strerror(errno));
		return NULL;
	}

	return link;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	struct bpf_object *obj = NULL;
	struct bpf_link *link_wakeup = NULL, *link_wakeup_new = NULL,
			*link_switch = NULL;
	struct bpf_map *hist_map, *hist_cpu_map;
	int hist_fd, hist_cpu_fd = -1;
	int err = 0, ncpus = 0;

	/* ── Parse CLI ──────────────────────────────────────────── */

	static struct option long_opts[] = {
		{"csv",  no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL,   0,           NULL,  0 },
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "p:Cmh", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'p':
			env.pid = (__u32)atoi(optarg);
			break;
		case 'C':
			env.per_cpu = 1;
			break;
		case 'm':
			env.milliseconds = 1;
			break;
		case 'V':
			env.csv = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (optind < argc)
		env.interval = atoi(argv[optind]);
	if (optind + 1 < argc)
		env.count = atoi(argv[optind + 1]);
	else if (optind < argc)
		env.count = 0;  /* interval given, no count → infinite */

	/* ── Signal handling ────────────────────────────────────── */

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	/* ── Open BPF object ────────────────────────────────────── */

	obj = bpf_object__open("bin/runqlat.bpf.o");
	if (libbpf_get_error(obj)) {
		fprintf(stderr, "ERROR: failed to open BPF object: %s\n",
			strerror(errno));
		return 1;
	}

	/* ── Set .rodata configuration before load ──────────────── */

	struct bpf_map *rodata_map = find_rodata(obj);
	if (rodata_map) {
		size_t sz;
		struct rodata *rd = bpf_map__initial_value(rodata_map, &sz);
		if (rd && sz >= sizeof(*rd)) {
			rd->targ_tgid = env.pid;
			rd->per_cpu   = env.per_cpu;
		} else {
			fprintf(stderr, "WARN: rodata pointer invalid, "
				"PID filter/per-CPU may not work\n");
		}
	} else if (env.pid || env.per_cpu) {
		fprintf(stderr, "WARN: .rodata map not found, "
			"PID filter/per-CPU not available\n");
	}

	/* ── Load BPF programs + maps into kernel ───────────────── */

	err = bpf_object__load(obj);
	if (err) {
		fprintf(stderr, "ERROR: failed to load BPF object: %s\n",
			strerror(errno));
		goto cleanup;
	}

	/* ── Attach tp_btf programs ─────────────────────────────── */

	link_wakeup = attach_prog(obj, "sched_wakeup");
	if (!link_wakeup) { err = 1; goto cleanup; }

	link_wakeup_new = attach_prog(obj, "sched_wakeup_new");
	if (!link_wakeup_new) { err = 1; goto cleanup; }

	link_switch = attach_prog(obj, "sched_switch");
	if (!link_switch) { err = 1; goto cleanup; }

	/* ── Get histogram map FDs ──────────────────────────────── */

	hist_map = bpf_object__find_map_by_name(obj, "hist");
	if (!hist_map) {
		fprintf(stderr, "ERROR: hist map not found\n");
		err = 1;
		goto cleanup;
	}
	hist_fd = bpf_map__fd(hist_map);

	if (env.per_cpu) {
		hist_cpu_map = bpf_object__find_map_by_name(obj, "hist_cpu");
		if (!hist_cpu_map) {
			fprintf(stderr, "ERROR: hist_cpu map not found\n");
			err = 1;
			goto cleanup;
		}
		hist_cpu_fd = bpf_map__fd(hist_cpu_map);
		ncpus = libbpf_num_possible_cpus();
		if (ncpus < 0) {
			fprintf(stderr, "ERROR: failed to get CPU count\n");
			err = 1;
			goto cleanup;
		}
		if (ncpus > MAX_CPUS)
			ncpus = MAX_CPUS;
	}

	/* ── Print header ───────────────────────────────────────── */

	fprintf(stderr, "Tracing run queue latency...");
	if (env.pid)
		fprintf(stderr, " PID %u.", env.pid);
	fprintf(stderr, " Hit Ctrl-C to end.\n");

	if (env.csv)
		print_csv_header();

	/* ── Main loop ──────────────────────────────────────────── */

	for (int round = 0; !env.count || round < env.count; round++) {
		sleep(env.interval);
		if (exiting)
			break;

		__u64 slots[MAX_SLOTS];

		if (env.csv) {
			/* CSV: read global histogram, print row, clear */
			read_hist(hist_fd, slots, MAX_SLOTS);
			print_csv_row(slots, MAX_SLOTS);
			clear_hist(hist_fd, MAX_SLOTS);
			if (env.per_cpu)
				clear_hist(hist_cpu_fd, MAX_CPUS * MAX_SLOTS);
			continue;
		}

		printf("\n");

		if (env.per_cpu) {
			/* Print per-CPU histograms */
			for (int cpu = 0; cpu < ncpus; cpu++) {
				__u64 cpu_slots[MAX_SLOTS];
				int base = cpu * MAX_SLOTS;
				int has_data = 0;

				for (int s = 0; s < MAX_SLOTS; s++) {
					__u32 key = base + s;
					__u64 val = 0;
					bpf_map_lookup_elem(hist_cpu_fd, &key, &val);
					cpu_slots[s] = val;
					if (val)
						has_data = 1;
				}

				if (!has_data)
					continue;

				printf("cpu = %d\n", cpu);
				print_histogram(cpu_slots, MAX_SLOTS,
						env.milliseconds);
				printf("\n");
			}
			clear_hist(hist_cpu_fd, MAX_CPUS * MAX_SLOTS);
		} else {
			/* Print global histogram */
			read_hist(hist_fd, slots, MAX_SLOTS);
			print_histogram(slots, MAX_SLOTS, env.milliseconds);
		}

		clear_hist(hist_fd, MAX_SLOTS);
	}

	/* ── Final histogram on Ctrl-C ──────────────────────────── */

	if (exiting) {
		__u64 slots[MAX_SLOTS];

		printf("\n");
		read_hist(hist_fd, slots, MAX_SLOTS);

		if (env.csv) {
			print_csv_row(slots, MAX_SLOTS);
		} else {
			print_histogram(slots, MAX_SLOTS, env.milliseconds);
		}
	}

cleanup:
	bpf_link__destroy(link_switch);
	bpf_link__destroy(link_wakeup_new);
	bpf_link__destroy(link_wakeup);
	bpf_object__close(obj);

	return err != 0;
}
