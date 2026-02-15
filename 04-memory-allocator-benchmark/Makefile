CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -pthread
LDLIBS  = -lm -lpthread
SRCDIR  = src
BINDIR  = bin
RESULTS = results

TARGETS = bench_single bench_mt bench_frag bench_realistic

.PHONY: all clean run

all: $(BINDIR) $(RESULTS) $(addprefix $(BINDIR)/,$(TARGETS))

$(BINDIR):
	mkdir -p $(BINDIR)

$(RESULTS):
	mkdir -p $(RESULTS)

$(BINDIR)/bench_single: $(SRCDIR)/bench_single.c $(SRCDIR)/common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BINDIR)/bench_mt: $(SRCDIR)/bench_mt.c $(SRCDIR)/common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BINDIR)/bench_frag: $(SRCDIR)/bench_frag.c $(SRCDIR)/common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BINDIR)/bench_realistic: $(SRCDIR)/bench_realistic.c $(SRCDIR)/common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

run: all
	@echo "=== Micro-Benchmarks (Milestone 1) ===" && $(BINDIR)/bench_single
	@echo ""
	@echo "=== Multithreaded Scalability (Milestone 2) ===" && $(BINDIR)/bench_mt
	@echo ""
	@echo "=== Fragmentation (Milestone 3) ===" && $(BINDIR)/bench_frag
	@echo ""
	@echo "=== Realistic Workloads (Milestone 4) ===" && $(BINDIR)/bench_realistic

clean:
	rm -rf $(BINDIR) $(RESULTS)/*.csv $(RESULTS)/*.png
