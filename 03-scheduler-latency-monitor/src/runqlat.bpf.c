/* runqlat.bpf.c — BPF program: measure CPU run-queue latency
 *
 * Hooks sched_wakeup / sched_wakeup_new / sched_switch via tp_btf
 * (raw tracepoints with BTF) to measure the time a task waits on
 * the run queue before getting scheduled.
 *
 * Uses tp_btf so we get typed access to task_struct, allowing proper
 * TGID-based PID filtering (all threads in a process, not just one).
 *
 * Results are stored in a log2 histogram (power-of-2 buckets, microseconds).
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>
#include "runqlat.h"

/* Userspace sets these before loading (via .rodata) */
const volatile __u32 targ_tgid = 0;	/* filter by process PID (TGID) */
const volatile int   per_cpu   = 0;	/* enable per-CPU histograms    */

/* Hash map: tid → enqueue timestamp (ns) */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u64);
} start SEC(".maps");

/* Global histogram: slot → count */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, MAX_SLOTS);
	__type(key, __u32);
	__type(value, __u64);
} hist SEC(".maps");

/* Per-CPU histogram: (cpu * MAX_SLOTS + slot) → count */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, MAX_CPUS * MAX_SLOTS);
	__type(key, __u32);
	__type(value, __u64);
} hist_cpu SEC(".maps");

/* Compute floor(log2(v)), verifier-safe via unrolled loop */
static __always_inline __u32 log2l(__u64 v)
{
	__u32 r = 0;

	#pragma unroll
	for (int i = 0; i < 25; i++) {
		if (v > 1) {
			v >>= 1;
			r++;
		}
	}
	return r;
}

static __always_inline void record_enqueue(__u32 tgid, __u32 tid)
{
	__u64 ts;

	if (targ_tgid && tgid != targ_tgid)
		return;

	ts = bpf_ktime_get_ns();
	bpf_map_update_elem(&start, &tid, &ts, BPF_ANY);
}

static __always_inline void record_dequeue(__u32 tgid, __u32 tid)
{
	__u64 *tsp, delta, now;
	__u32 slot, key;
	__u64 *countp;

	if (targ_tgid && tgid != targ_tgid)
		return;

	tsp = bpf_map_lookup_elem(&start, &tid);
	if (!tsp)
		return;

	now = bpf_ktime_get_ns();
	delta = now - *tsp;
	bpf_map_delete_elem(&start, &tid);

	/* Convert ns → us for histogram buckets */
	delta /= 1000;

	/* Compute log2 bucket */
	slot = log2l(delta);
	if (slot >= MAX_SLOTS)
		slot = MAX_SLOTS - 1;

	/* Update global histogram */
	countp = bpf_map_lookup_elem(&hist, &slot);
	if (countp)
		__sync_fetch_and_add(countp, 1);

	/* Update per-CPU histogram if enabled */
	if (per_cpu) {
		__u32 cpu = bpf_get_smp_processor_id();
		if (cpu < MAX_CPUS) {
			key = cpu * MAX_SLOTS + slot;
			countp = bpf_map_lookup_elem(&hist_cpu, &key);
			if (countp)
				__sync_fetch_and_add(countp, 1);
		}
	}
}

SEC("tp_btf/sched_wakeup")
int BPF_PROG(sched_wakeup, struct task_struct *p)
{
	record_enqueue(BPF_CORE_READ(p, tgid), BPF_CORE_READ(p, pid));
	return 0;
}

SEC("tp_btf/sched_wakeup_new")
int BPF_PROG(sched_wakeup_new, struct task_struct *p)
{
	record_enqueue(BPF_CORE_READ(p, tgid), BPF_CORE_READ(p, pid));
	return 0;
}

SEC("tp_btf/sched_switch")
int BPF_PROG(sched_switch, bool preempt,
	     struct task_struct *prev, struct task_struct *next)
{
	__u32 prev_pid  = BPF_CORE_READ(prev, pid);
	__u32 prev_tgid = BPF_CORE_READ(prev, tgid);
	__u32 next_pid  = BPF_CORE_READ(next, pid);
	__u32 next_tgid = BPF_CORE_READ(next, tgid);
	long  prev_state = BPF_CORE_READ(prev, __state);

	/*
	 * If the previous task is still TASK_RUNNING (state == 0),
	 * it was involuntarily preempted — record its enqueue time.
	 */
	if (prev_state == 0 /* TASK_RUNNING */)
		record_enqueue(prev_tgid, prev_pid);

	/* The next task is leaving the run queue — record its wait time */
	record_dequeue(next_tgid, next_pid);

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
