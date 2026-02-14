/* runqlat.h — shared constants between BPF and userspace */

#ifndef RUNQLAT_H
#define RUNQLAT_H

#define MAX_SLOTS	26	/* log2 histogram: 0ns .. ~33ms */
#define MAX_CPUS	128
#define TASK_COMM_LEN	16
#define MAX_ENTRIES	10240

#endif /* RUNQLAT_H */
