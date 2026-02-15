#!/usr/bin/env python3
"""
plot_throughput.py — Bar chart comparing allocator throughput across workloads.

Reads:  results/single_throughput.csv  (Milestone 1)
        results/realistic.csv          (Milestone 4)
Writes: results/throughput_micro.png
        results/throughput_realistic.png
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


def read_csv(path):
    """Read CSV file, return list of dicts."""
    if not os.path.exists(path):
        print(f"File not found: {path}", file=sys.stderr)
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def plot_grouped_bars(data, output_path, title, value_key="ops_per_sec", ylabel="Operations / sec"):
    """
    data: list of dicts with keys: allocator, workload, <value_key>
    Groups by workload, bars by allocator.
    """
    # Aggregate: mean per (allocator, workload)
    agg = defaultdict(list)
    for row in data:
        key = (row["allocator"], row["workload"])
        try:
            agg[key].append(float(row[value_key]))
        except (ValueError, KeyError):
            continue

    if not agg:
        print(f"No data to plot for {output_path}", file=sys.stderr)
        return

    workloads = sorted(set(k[1] for k in agg.keys()))
    allocators = sorted(set(k[0] for k in agg.keys()))

    x = np.arange(len(workloads))
    width = 0.8 / len(allocators)

    fig, ax = plt.subplots(figsize=(max(10, len(workloads) * 2), 6))

    for i, alloc in enumerate(allocators):
        means = []
        stds = []
        for w in workloads:
            vals = agg.get((alloc, w), [])
            means.append(np.mean(vals) if vals else 0)
            stds.append(np.std(vals) if len(vals) > 1 else 0)

        offset = (i - len(allocators) / 2 + 0.5) * width
        bars = ax.bar(x + offset, means, width, yerr=stds,
                      label=alloc, color=COLORS.get(alloc, "#999"),
                      capsize=3, edgecolor="white", linewidth=0.5)

        # Value labels
        for bar, mean in zip(bars, means):
            if mean > 0:
                label = f"{mean:.0f}" if mean < 1e6 else f"{mean/1e6:.1f}M"
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                        label, ha="center", va="bottom", fontsize=7, rotation=45)

    ax.set_xlabel("Workload")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.set_xticks(x)
    ax.set_xticklabels(workloads, rotation=30, ha="right")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    print(f"  Saved: {output_path}")
    plt.close(fig)


def main():
    # Milestone 1: micro-benchmarks
    single_path = os.path.join(RESULTS_DIR, "single_throughput.csv")
    single_data = read_csv(single_path)
    if single_data:
        plot_grouped_bars(
            single_data,
            os.path.join(RESULTS_DIR, "throughput_micro.png"),
            "Micro-Benchmark Throughput by Allocator"
        )

    # Milestone 4: realistic workloads
    real_path = os.path.join(RESULTS_DIR, "realistic.csv")
    real_data = read_csv(real_path)
    if real_data:
        plot_grouped_bars(
            real_data,
            os.path.join(RESULTS_DIR, "throughput_realistic.png"),
            "Realistic Workload Throughput by Allocator"
        )

    # Also plot fragmentation ratios from single_throughput
    if single_data:
        plot_grouped_bars(
            single_data,
            os.path.join(RESULTS_DIR, "frag_ratio_micro.png"),
            "Fragmentation Ratio by Allocator (lower = better)",
            value_key="frag_ratio",
            ylabel="RSS / Live Bytes"
        )


if __name__ == "__main__":
    main()
