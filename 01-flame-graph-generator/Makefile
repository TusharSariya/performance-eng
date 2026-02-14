# Makefile — 01-flame-graph-generator
#
# Targets:
#   all         Build everything
#   selfprofile Signal-based self-profiler (Milestone 1)
#   profiler    External process profiler via perf_event_open (Milestone 2)
#   flamegraph  Folded stacks → SVG renderer (Milestone 3)
#   workload    CPU-bound test workload for profiling
#   demo        Build all and run self-profiler → flame graph pipeline
#   clean       Remove build artifacts

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -fno-omit-frame-pointer
BINDIR   = bin
SRCDIR   = src
SAMPDIR  = samples
RESDIR   = results

.PHONY: all clean demo demo-external

all: $(BINDIR)/selfprofile $(BINDIR)/profiler $(BINDIR)/flamegraph $(BINDIR)/workload

# ── Milestone 1: Self-profiler ──────────────────────────────────

$(BINDIR)/selfprofile: $(SRCDIR)/selfprofile.c | $(BINDIR)
	$(CC) $(CFLAGS) -rdynamic -o $@ $< -ldl

# ── Milestone 2: External profiler ─────────────────────────────

$(BINDIR)/profiler: $(SRCDIR)/profiler.c $(SRCDIR)/symbols.c $(SRCDIR)/symbols.h | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(SRCDIR)/profiler.c $(SRCDIR)/symbols.c -I$(SRCDIR)

# ── Milestone 3: SVG flame graph renderer ──────────────────────

$(BINDIR)/flamegraph: $(SRCDIR)/flamegraph.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< -lm

# ── Test workload ──────────────────────────────────────────────

$(BINDIR)/workload: $(SAMPDIR)/workload.c | $(BINDIR)
	$(CC) -O1 -g -fno-omit-frame-pointer -rdynamic -Wall -o $@ $<

# ── Directory ──────────────────────────────────────────────────

$(BINDIR):
	mkdir -p $(BINDIR)

$(RESDIR):
	mkdir -p $(RESDIR)

# ── Demo: self-profiler pipeline ───────────────────────────────

demo: all | $(RESDIR)
	@echo "=== Self-profiler → Flame Graph ==="
	$(BINDIR)/selfprofile | $(BINDIR)/flamegraph -t "Self-Profile" -o $(RESDIR)/self.svg
	@echo "Output: $(RESDIR)/self.svg"
	@echo ""
	@echo "=== Self-profiler → Python Flame Graph ==="
	$(BINDIR)/selfprofile | python3 scripts/flamegraph.py -t "Self-Profile (Python)" -o $(RESDIR)/self_py.svg
	@echo "Output: $(RESDIR)/self_py.svg"

# ── Demo: external profiler pipeline ───────────────────────────

demo-external: all | $(RESDIR)
	@echo "=== Starting workload in background ==="
	$(BINDIR)/workload 10 &
	@sleep 1
	@echo "=== Profiling workload (PID $$!) for 3 seconds ==="
	$(BINDIR)/profiler -p $$(pgrep -n workload) -d 3 -f 99 | $(BINDIR)/flamegraph \
		-t "External Profile: workload" -o $(RESDIR)/workload.svg
	@echo "Output: $(RESDIR)/workload.svg"

# ── Clean ──────────────────────────────────────────────────────

clean:
	rm -rf $(BINDIR)
	rm -f $(RESDIR)/*.svg $(RESDIR)/*.folded
