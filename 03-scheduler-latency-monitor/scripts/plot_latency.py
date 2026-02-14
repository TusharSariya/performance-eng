#!/usr/bin/env python3
"""plot_latency.py — Visualize runqlat CSV output.

Reads CSV from:  sudo bin/runqlat --csv 1 30 > results/latency.csv
Columns:         timestamp, p50_us, p95_us, p99_us, max_us

Usage:
    python3 scripts/plot_latency.py results/latency.csv [-o results/latency.png]

Falls back to a text-mode table if matplotlib is unavailable.
"""

import argparse
import csv
import sys


def load_csv(path):
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append({
                "ts": float(r["timestamp"]),
                "p50": int(r["p50_us"]),
                "p95": int(r["p95_us"]),
                "p99": int(r["p99_us"]),
                "max": int(r["max_us"]),
            })
    return rows


def text_table(rows):
    """Fallback: print a simple ASCII table."""
    print(f"{'#':>4}  {'p50 (us)':>10}  {'p95 (us)':>10}  {'p99 (us)':>10}  {'max (us)':>10}")
    print("-" * 52)
    for i, r in enumerate(rows):
        print(f"{i:4d}  {r['p50']:10d}  {r['p95']:10d}  {r['p99']:10d}  {r['max']:10d}")


def plot_matplotlib(rows, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    t0 = rows[0]["ts"]
    xs = [r["ts"] - t0 for r in rows]

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(xs, [r["p50"] for r in rows], label="p50", linewidth=1.5)
    ax.plot(xs, [r["p95"] for r in rows], label="p95", linewidth=1.5)
    ax.plot(xs, [r["p99"] for r in rows], label="p99", linewidth=1.5, linestyle="--")
    ax.plot(xs, [r["max"] for r in rows], label="max", linewidth=1.0,
            linestyle=":", alpha=0.7)

    ax.set_xlabel("Time (seconds)")
    ax.set_ylabel("Run-queue latency (us)")
    ax.set_title("CPU Scheduler Latency Over Time")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_yscale("log")

    plt.tight_layout()
    if output:
        plt.savefig(output, dpi=150)
        print(f"Saved: {output}", file=sys.stderr)
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(description="Plot runqlat CSV data")
    parser.add_argument("csv_file", help="CSV file from runqlat --csv")
    parser.add_argument("-o", "--output", help="Output PNG path (default: show window)")
    args = parser.parse_args()

    rows = load_csv(args.csv_file)
    if not rows:
        print("No data in CSV file.", file=sys.stderr)
        return 1

    try:
        plot_matplotlib(rows, args.output)
    except ImportError:
        print("matplotlib not available, printing text table:\n", file=sys.stderr)
        text_table(rows)

    return 0


if __name__ == "__main__":
    sys.exit(main())
