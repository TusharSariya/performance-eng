#!/usr/bin/env python3
"""
plot_scaling.py — Plot throughput vs thread count from scaling.csv

Usage:
    python3 scripts/plot_scaling.py results/scaling.csv
    python3 scripts/plot_scaling.py results/scaling.csv -o results/scaling.png
"""
import sys
import csv
import argparse

def main():
    parser = argparse.ArgumentParser(description="Plot false sharing scaling results")
    parser.add_argument("csv_file", help="Path to scaling.csv")
    parser.add_argument("-o", "--output", default=None,
                        help="Output image path (default: results/scaling.png)")
    parser.add_argument("--no-display", action="store_true",
                        help="Don't display the plot, just save")
    args = parser.parse_args()

    # Parse CSV
    data = {}  # mode -> {threads: ops_per_sec}
    with open(args.csv_file) as f:
        reader = csv.DictReader(f)
        for row in reader:
            mode = row["mode"]
            threads = int(row["threads"])
            ops = float(row["ops_per_sec"])
            if mode not in data:
                data[mode] = {}
            data[mode][threads] = ops

    if not data:
        print("No data found in CSV", file=sys.stderr)
        sys.exit(1)

    try:
        import matplotlib
        if args.no_display:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed. Printing text table instead.\n")
        print_text_chart(data)
        return

    # Plot
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    colors = {"padded": "#2ecc71", "packed": "#e74c3c", "true_share": "#3498db"}
    markers = {"padded": "o", "packed": "s", "true_share": "^"}
    labels = {"padded": "Padded (no false sharing)",
              "packed": "Packed (false sharing)",
              "true_share": "True sharing (atomic)"}

    # Left plot: absolute throughput
    for mode in ["padded", "packed", "true_share"]:
        if mode not in data:
            continue
        threads = sorted(data[mode].keys())
        ops = [data[mode][t] for t in threads]
        ax1.plot(threads, [o / 1e9 for o in ops],
                 color=colors.get(mode, "gray"),
                 marker=markers.get(mode, "o"),
                 linewidth=2, markersize=8,
                 label=labels.get(mode, mode))

    ax1.set_xlabel("Thread Count", fontsize=12)
    ax1.set_ylabel("Throughput (Billion ops/sec)", fontsize=12)
    ax1.set_title("Absolute Throughput", fontsize=14)
    ax1.legend(fontsize=10)
    ax1.grid(True, alpha=0.3)
    ax1.set_xticks(sorted(list(data.get("padded", data.get("packed", {})).keys())))

    # Right plot: normalized to single-thread performance
    for mode in ["padded", "packed", "true_share"]:
        if mode not in data:
            continue
        threads = sorted(data[mode].keys())
        ops = [data[mode][t] for t in threads]
        if threads and threads[0] == 1 and ops[0] > 0:
            baseline = ops[0]
        else:
            baseline = ops[0] if ops else 1
        normalized = [o / baseline for o in ops]
        ax2.plot(threads, normalized,
                 color=colors.get(mode, "gray"),
                 marker=markers.get(mode, "o"),
                 linewidth=2, markersize=8,
                 label=labels.get(mode, mode))

    # Ideal scaling line
    all_threads = sorted(set(t for m in data.values() for t in m.keys()))
    if all_threads:
        ax2.plot(all_threads, [t / all_threads[0] for t in all_threads],
                 color="gray", linestyle="--", alpha=0.5, label="Ideal linear")

    ax2.set_xlabel("Thread Count", fontsize=12)
    ax2.set_ylabel("Speedup (normalized to 1 thread)", fontsize=12)
    ax2.set_title("Scaling Efficiency", fontsize=14)
    ax2.legend(fontsize=10)
    ax2.grid(True, alpha=0.3)
    ax2.set_xticks(all_threads)

    plt.suptitle("False Sharing: Thread Scaling Behavior", fontsize=16, fontweight="bold")
    plt.tight_layout()

    output = args.output or "results/scaling.png"
    plt.savefig(output, dpi=150, bbox_inches="tight")
    print(f"Plot saved to {output}")

    if not args.no_display:
        try:
            plt.show()
        except Exception:
            pass


def print_text_chart(data):
    """Fallback text-based display when matplotlib is unavailable."""
    print(f"{'Threads':<10}", end="")
    modes = sorted(data.keys())
    for mode in modes:
        print(f"{mode:>20}", end="")
    print()
    print("-" * (10 + 20 * len(modes)))

    all_threads = sorted(set(t for m in data.values() for t in m.keys()))
    max_ops = max(ops for m in data.values() for ops in m.values())

    for t in all_threads:
        print(f"{t:<10}", end="")
        for mode in modes:
            ops = data[mode].get(t, 0)
            bar_len = int(40 * ops / max_ops) if max_ops > 0 else 0
            print(f"{ops/1e6:>12.0f}M {'█' * bar_len}", end="  ")
        print()


if __name__ == "__main__":
    main()
