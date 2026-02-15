#!/usr/bin/env bash
#
# run_allocator.sh — Run a benchmark binary with a specific allocator via LD_PRELOAD
#
# Usage:
#   ./scripts/run_allocator.sh <allocator> <benchmark> [args...]
#
# Allocators: glibc, jemalloc, tcmalloc, mimalloc
#
# Examples:
#   ./scripts/run_allocator.sh jemalloc bin/bench_single --csv
#   ./scripts/run_allocator.sh tcmalloc bin/bench_mt --csv --threads 1,2,4,8
#
set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <allocator> <benchmark> [args...]"
    echo ""
    echo "Allocators: glibc, jemalloc, tcmalloc, mimalloc"
    echo ""
    echo "Examples:"
    echo "  $0 jemalloc bin/bench_single --csv"
    echo "  $0 tcmalloc bin/bench_mt --csv"
    exit 1
fi

ALLOCATOR="$1"
shift
BENCHMARK="$1"
shift

# Common library paths on Debian/Ubuntu
JEMALLOC_PATHS=(
    /usr/lib/x86_64-linux-gnu/libjemalloc.so.2
    /usr/lib/x86_64-linux-gnu/libjemalloc.so
    /usr/lib/libjemalloc.so.2
    /usr/lib/libjemalloc.so
    /usr/local/lib/libjemalloc.so
)

TCMALLOC_PATHS=(
    /usr/lib/x86_64-linux-gnu/libtcmalloc_minimal.so.4
    /usr/lib/x86_64-linux-gnu/libtcmalloc.so.4
    /usr/lib/x86_64-linux-gnu/libtcmalloc_minimal.so
    /usr/lib/x86_64-linux-gnu/libtcmalloc.so
    /usr/lib/libtcmalloc_minimal.so
    /usr/lib/libtcmalloc.so
    /usr/local/lib/libtcmalloc.so
)

MIMALLOC_PATHS=(
    /usr/lib/x86_64-linux-gnu/libmimalloc.so.2
    /usr/lib/x86_64-linux-gnu/libmimalloc.so
    /usr/lib/libmimalloc.so.2
    /usr/lib/libmimalloc.so
    /usr/local/lib/libmimalloc.so
    /usr/local/lib/mimalloc-2.1/libmimalloc.so
)

find_lib() {
    local -n paths=$1
    for p in "${paths[@]}"; do
        if [ -f "$p" ]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

case "$ALLOCATOR" in
    glibc)
        exec "$BENCHMARK" "$@"
        ;;
    jemalloc)
        LIB=$(find_lib JEMALLOC_PATHS) || {
            echo "ERROR: jemalloc not found. Install: sudo apt install libjemalloc-dev" >&2
            exit 1
        }
        LD_PRELOAD="$LIB" exec "$BENCHMARK" "$@"
        ;;
    tcmalloc)
        LIB=$(find_lib TCMALLOC_PATHS) || {
            echo "ERROR: tcmalloc not found. Install: sudo apt install libgoogle-perftools-dev" >&2
            exit 1
        }
        LD_PRELOAD="$LIB" exec "$BENCHMARK" "$@"
        ;;
    mimalloc)
        LIB=$(find_lib MIMALLOC_PATHS) || {
            echo "ERROR: mimalloc not found. Install: sudo apt install libmimalloc-dev" >&2
            exit 1
        }
        LD_PRELOAD="$LIB" exec "$BENCHMARK" "$@"
        ;;
    *)
        echo "Unknown allocator: $ALLOCATOR" >&2
        echo "Supported: glibc, jemalloc, tcmalloc, mimalloc" >&2
        exit 1
        ;;
esac
