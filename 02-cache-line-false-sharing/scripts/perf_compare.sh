#!/usr/bin/env bash
#
# perf_compare.sh — Run basic_demo under perf stat for packed vs padded
#
# Uses perf stat with hardware cache counters to show the difference.
# Requires: perf installed, may need sudo or kernel.perf_event_paranoid <= 1
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BIN="$PROJECT_DIR/bin/basic_demo"
RESULTS="$PROJECT_DIR/results"

if [ ! -x "$BIN" ]; then
    echo "Building first..."
    make -C "$PROJECT_DIR" basic_demo
fi

mkdir -p "$RESULTS"

EVENTS="cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses,instructions,cycles"
ITERS="${ITERATIONS:-200000000}"

echo "════════════════════════════════════════════════════════════"
echo "  perf stat comparison: false sharing vs no false sharing"
echo "  Iterations per thread: $ITERS"
echo "════════════════════════════════════════════════════════════"

echo ""
echo ">>> PACKED (false sharing):"
echo "────────────────────────────────────────────────────────────"
ITERATIONS=$ITERS perf stat -e "$EVENTS" "$BIN" 2>&1 | grep -E "(cache|L1|LLC|instructions|cycles|time elapsed|Slowdown)"

echo ""
echo "Note: perf stat counts the ENTIRE process including both"
echo "packed and padded runs. For isolated measurements, use"
echo "bin/perf_counters which uses perf_event_open() per-run."
echo ""
echo "For the most accurate comparison, run bin/perf_counters directly."
