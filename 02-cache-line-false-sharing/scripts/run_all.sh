#!/usr/bin/env bash
#
# run_all.sh — Build and run all false-sharing benchmarks
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "Building..."
make all

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  1. BASIC DEMO (Milestone 1)"
echo "════════════════════════════════════════════════════════════"
ITERATIONS=200000000 bin/basic_demo

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  2. HARDWARE COUNTERS (Milestone 2)"
echo "════════════════════════════════════════════════════════════"
ITERATIONS=200000000 bin/perf_counters

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  3. SCALING EXPERIMENT (Milestone 3)"
echo "════════════════════════════════════════════════════════════"
ITERATIONS=200000000 bin/scaling --csv | tee results/scaling.csv
echo ""
echo "CSV saved to results/scaling.csv"

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  4. REAL-WORLD PATTERNS (Milestone 4)"
echo "════════════════════════════════════════════════════════════"
ITERATIONS=100000000 bin/patterns

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  All benchmarks complete!"
echo "  Generate scaling plot: python3 scripts/plot_scaling.py results/scaling.csv"
echo "════════════════════════════════════════════════════════════"
