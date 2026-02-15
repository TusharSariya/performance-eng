#!/usr/bin/env python3
"""
plot_scaling.py — Line chart of allocator throughput vs thread count.

Reads:  results/mt_scaling.csv  (Milestone 2)
Writes: results/scaling_<workload>.png  (one per workload)
"""
import sys
import os
import csv
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
except ImportError:
    print("ERROR: matplotlib and numpy required. Install: pip install matplotlib numpy",
          file=sys.stderr)
    sys.exit(1)

RESULTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results")

COLORS = {
    "glibc":    "#4e79a7",
    "jemalloc": "#f28e2b",
    "tcmalloc": "#e15759",
    "mimalloc": "#76b7b2",
    "unknown":  "#999999",
}

MARKERS = {
    "glibc":    "o",
    "jemalloc": "s",
    "tcmalloc": "^",
    "mimalloc": "D",
    "unknown":  "x",
}


def read_csv(path):
    if not os.path.exists(path):
        print(f"File not found: {path}", file=sys.stderr)
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def plot_scaling(data, workload, output_path):
    """Plot throughput vs threads for a given workload, one line per allocator."""
    # Group by allocator
    by_alloc = defaultdict(list)
    for row in data:
        if row["workload"] != workload:
            continue
        try:
            threads = int(row["threads"])
            ops = float(row["ops_per_sec"])
            by_alloc[row["allocator"]].append((threads, ops))
        except (ValueError, KeyError):
            continue

    if not by_alloc:
        print(f"No data for workload '{workload}'", file=sys.stderr)
        return

    fig, ax = plt.subplots(figsize=(10, 6))

    for alloc in sorted(by_alloc.keys()):
        points = sorted(by_alloc[alloc])
        threads = [p[0] for p in points]
        ops = [p[1] for p in points]

        ax.plot(threads, ops,
                marker=MARKERS.get(alloc, "x"),
                color=COLORS.get(alloc, "#999"),
                label=alloc, linewidth=2, markersize=8)

    ax.set_xlabel("Thread Count")
    ax.set_ylabel("Operations / sec")
    ax.set_title(f"Multithreaded Scalability: {workload}")
    ax.legend()
    ax.grid(alpha=0.3)
    ax.set_xscale("log", base=2)

    # Set x ticks to actual thread counts
    all_threads = sorted(set(
        int(r["threads"]) for r in data
        if r["workload"] == workload
    ))
    ax.set_xticks(all_threads)
    ax.set_xticklabels([str(t) for t in all_threads])

    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    print(f"  Saved: {output_path}")
    plt.close(fig)


def main():
    csv_path = os.path.join(RESULTS_DIR, "mt_scaling.csv")
    data = read_csv(csv_path)
    if not data:
        print("No data found. Run: ./scripts/run_all.sh first.", file=sys.stderr)
        sys.exit(1)

    workloads = sorted(set(row["workload"] for row in data))
    for w in workloads:
        out = os.path.join(RESULTS_DIR, f"scaling_{w}.png")
        plot_scaling(data, w, out)


if __name__ == "__main__":
    main()
