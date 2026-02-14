#!/usr/bin/env bash
# run_demo.sh — Build and demonstrate runqlat under idle and loaded conditions.
#
# Usage: sudo bash scripts/run_demo.sh

set -euo pipefail
cd "$(dirname "$0")/.."

RESDIR=results
INTERVAL=1
COUNT=5
STRESS_SEC=15
STRESS_THREADS=40

echo "=== Building everything ==="
make all
mkdir -p "$RESDIR"

echo ""
echo "=== Baseline: idle system (${COUNT}s) ==="
echo "Running: bin/runqlat $INTERVAL $COUNT"
bin/runqlat "$INTERVAL" "$COUNT"

echo ""
echo "=== Starting CPU stress: $STRESS_THREADS threads for ${STRESS_SEC}s ==="
bin/cpu_stress "$STRESS_SEC" "$STRESS_THREADS" &
STRESS_PID=$!
sleep 1  # let threads spin up

echo ""
echo "=== Under load: runqlat $INTERVAL $COUNT ==="
bin/runqlat "$INTERVAL" "$COUNT"

echo ""
echo "=== Under load: CSV output → $RESDIR/latency.csv ==="
bin/runqlat --csv "$INTERVAL" "$COUNT" | tee "$RESDIR/latency.csv"

echo ""
echo "=== Cleaning up stress workload ==="
kill "$STRESS_PID" 2>/dev/null || true
wait "$STRESS_PID" 2>/dev/null || true

echo ""
echo "=== Generating plot ==="
if python3 scripts/plot_latency.py "$RESDIR/latency.csv" -o "$RESDIR/latency.png" 2>/dev/null; then
    echo "Saved: $RESDIR/latency.png"
else
    echo "(matplotlib not available — skipping plot)"
fi

echo ""
echo "Done. Compare the idle and loaded histograms above."
echo "Under load, events should shift from 0–15 us to 100 us – 10 ms+."
