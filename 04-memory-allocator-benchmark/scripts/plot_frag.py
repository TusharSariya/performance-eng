#!/usr/bin/env python3
"""
plot_frag.py — Time-series plots of RSS and fragmentation ratio.

Reads:  results/frag_<allocator>.csv  (Milestone 3, one per allocator)
Writes: results/frag_rss_timeseries.png
        results/frag_ratio_timeseries.png
"""
import sys
import os
import csv
import glob as globmod
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


def read_frag_csvs():
    """Read all frag_*.csv files, return dict of allocator -> list of rows."""
    result = {}
    pattern = os.path.join(RESULTS_DIR, "frag_*.csv")
    for path in sorted(globmod.glob(pattern)):
        if not os.path.exists(path):
            continue
        with open(path, newline="") as f:
            rows = list(csv.DictReader(f))
        if rows:
            alloc = rows[0].get("allocator", "unknown")
            result[alloc] = rows
    return result


def plot_rss_timeseries(all_data, output_path):
    """Plot RSS (KB) over operation steps for each allocator."""
    fig, ax = plt.subplots(figsize=(12, 6))

    for alloc in sorted(all_data.keys()):
        rows = all_data[alloc]
        steps = []
        rss = []
        for r in rows:
            try:
                steps.append(int(r["step"]))
                rss.append(int(r["rss_kb"]))
            except (ValueError, KeyError):
                continue
        if steps:
            ax.plot(steps, rss,
                    color=COLORS.get(alloc, "#999"),
                    label=alloc, linewidth=1.5, alpha=0.8)

    # Add phase annotations from first allocator
    first_alloc = next(iter(all_data.values()), [])
    phase_boundaries = {}
    for r in first_alloc:
        phase = r.get("phase", "")
        if phase.endswith("_done") or phase in ("start", "done"):
            try:
                step = int(r["step"])
                phase_boundaries[phase] = step
            except (ValueError, KeyError):
                pass

    for phase, step in phase_boundaries.items():
        label = phase.replace("_done", "").replace("_", " ").title()
        ax.axvline(x=step, color="gray", linestyle="--", alpha=0.4)
        ax.text(step, ax.get_ylim()[1] * 0.95, f" {label}",
                fontsize=8, alpha=0.6, rotation=90, va="top")

    ax.set_xlabel("Operation Step")
    ax.set_ylabel("RSS (KB)")
    ax.set_title("RSS Over Time During Fragmentation Workload")
    ax.legend()
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    print(f"  Saved: {output_path}")
    plt.close(fig)


def plot_frag_ratio_timeseries(all_data, output_path):
    """Plot fragmentation ratio over time for each allocator."""
    fig, ax = plt.subplots(figsize=(12, 6))

    for alloc in sorted(all_data.keys()):
        rows = all_data[alloc]
        steps = []
        ratios = []
        for r in rows:
            try:
                frag = float(r["frag_ratio"])
                if frag > 0:
                    steps.append(int(r["step"]))
                    ratios.append(frag)
            except (ValueError, KeyError):
                continue
        if steps:
            ax.plot(steps, ratios,
                    color=COLORS.get(alloc, "#999"),
                    label=alloc, linewidth=1.5, alpha=0.8)

    ax.axhline(y=1.0, color="green", linestyle=":", alpha=0.5, label="Perfect (1.0)")
    ax.set_xlabel("Operation Step")
    ax.set_ylabel("Fragmentation Ratio (RSS / Live Bytes)")
    ax.set_title("Fragmentation Ratio Over Time")
    ax.legend()
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    print(f"  Saved: {output_path}")
    plt.close(fig)


def main():
    all_data = read_frag_csvs()
    if not all_data:
        print("No fragmentation data found. Run: ./scripts/run_all.sh first.",
              file=sys.stderr)
        sys.exit(1)

    plot_rss_timeseries(
        all_data,
        os.path.join(RESULTS_DIR, "frag_rss_timeseries.png")
    )
    plot_frag_ratio_timeseries(
        all_data,
        os.path.join(RESULTS_DIR, "frag_ratio_timeseries.png")
    )


if __name__ == "__main__":
    main()
