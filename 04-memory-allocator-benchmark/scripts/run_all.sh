#!/usr/bin/env bash
#
# run_all.sh — Run all benchmarks across all available allocators
#
# Produces CSV files in results/ for plotting.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "Building..."
make all

# Detect available allocators
ALLOCATORS=("glibc")

check_allocator() {
    local name=$1
    if ./scripts/run_allocator.sh "$name" /bin/true 2>/dev/null; then
        ALLOCATORS+=("$name")
        echo "  ✓ $name found"
    else
        echo "  ✗ $name not found (skipping)"
    fi
}

echo ""
echo "Detecting allocators..."
echo "  ✓ glibc (always available)"
check_allocator jemalloc
check_allocator tcmalloc
check_allocator mimalloc

echo ""
echo "Running with allocators: ${ALLOCATORS[*]}"
echo ""

RUNS=${RUNS:-3}  # Number of repetitions per benchmark

# ── Milestone 1: Micro-benchmarks ──────────────────────────────────

echo "════════════════════════════════════════════════════════════"
echo "  Milestone 1: Micro-Benchmarks (bench_single)"
echo "════════════════════════════════════════════════════════════"

SINGLE_CSV="results/single_throughput.csv"
# Write header once
head_written=0
for alloc in "${ALLOCATORS[@]}"; do
    for run in $(seq 1 "$RUNS"); do
        echo "  [$alloc] run $run/$RUNS..."
        output=$(./scripts/run_allocator.sh "$alloc" bin/bench_single --csv 2>/dev/null)
        if [ $head_written -eq 0 ]; then
            echo "$output" > "$SINGLE_CSV"
            head_written=1
        else
            echo "$output" | tail -n +2 >> "$SINGLE_CSV"
        fi
    done
done
echo "  → $SINGLE_CSV"

# ── Milestone 2: Multithreaded scalability ─────────────────────────

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  Milestone 2: Multithreaded Scalability (bench_mt)"
echo "════════════════════════════════════════════════════════════"

MT_CSV="results/mt_scaling.csv"
head_written=0
for alloc in "${ALLOCATORS[@]}"; do
    echo "  [$alloc] scaling test..."
    output=$(./scripts/run_allocator.sh "$alloc" bin/bench_mt --csv 2>/dev/null)
    if [ $head_written -eq 0 ]; then
        echo "$output" > "$MT_CSV"
        head_written=1
    else
        echo "$output" | tail -n +2 >> "$MT_CSV"
    fi
done
echo "  → $MT_CSV"

# ── Milestone 3: Fragmentation ─────────────────────────────────────

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  Milestone 3: Fragmentation Deep-Dive (bench_frag)"
echo "════════════════════════════════════════════════════════════"

for alloc in "${ALLOCATORS[@]}"; do
    FRAG_CSV="results/frag_${alloc}.csv"
    echo "  [$alloc] fragmentation test..."
    ./scripts/run_allocator.sh "$alloc" bin/bench_frag --csv > "$FRAG_CSV" 2>/dev/null
    echo "  → $FRAG_CSV"
done

# ── Milestone 4: Realistic workloads ───────────────────────────────

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  Milestone 4: Realistic Workloads (bench_realistic)"
echo "════════════════════════════════════════════════════════════"

REAL_CSV="results/realistic.csv"
head_written=0
for alloc in "${ALLOCATORS[@]}"; do
    for run in $(seq 1 "$RUNS"); do
        echo "  [$alloc] run $run/$RUNS..."
        output=$(./scripts/run_allocator.sh "$alloc" bin/bench_realistic --csv 2>/dev/null)
        if [ $head_written -eq 0 ]; then
            echo "$output" > "$REAL_CSV"
            head_written=1
        else
            echo "$output" | tail -n +2 >> "$REAL_CSV"
        fi
    done
done
echo "  → $REAL_CSV"

# ── Summary ────────────────────────────────────────────────────────

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  All benchmarks complete!"
echo ""
echo "  CSV files in results/:"
ls -la results/*.csv 2>/dev/null || echo "  (none)"
echo ""
echo "  Generate plots:"
echo "    python3 scripts/plot_throughput.py"
echo "    python3 scripts/plot_scaling.py"
echo "    python3 scripts/plot_frag.py"
echo "════════════════════════════════════════════════════════════"
