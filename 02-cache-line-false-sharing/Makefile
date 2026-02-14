CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -pthread
SRCDIR  = src
BINDIR  = bin
RESULTS = results

TARGETS = basic_demo perf_counters scaling patterns

.PHONY: all clean run

all: $(BINDIR) $(RESULTS) $(addprefix $(BINDIR)/,$(TARGETS))

$(BINDIR):
	mkdir -p $(BINDIR)

$(RESULTS):
	mkdir -p $(RESULTS)

$(BINDIR)/basic_demo: $(SRCDIR)/basic_demo.c $(SRCDIR)/common.h
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR)/perf_counters: $(SRCDIR)/perf_counters.c $(SRCDIR)/common.h
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR)/scaling: $(SRCDIR)/scaling.c $(SRCDIR)/common.h
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR)/patterns: $(SRCDIR)/patterns.c $(SRCDIR)/common.h
	$(CC) $(CFLAGS) -o $@ $<

run: all
	@echo "=== Basic Demo ===" && $(BINDIR)/basic_demo
	@echo ""
	@echo "=== Perf Counters ===" && $(BINDIR)/perf_counters
	@echo ""
	@echo "=== Scaling (CSV) ===" && $(BINDIR)/scaling --csv | tee $(RESULTS)/scaling.csv
	@echo ""
	@echo "=== Patterns ===" && $(BINDIR)/patterns

clean:
	rm -rf $(BINDIR) $(RESULTS)/*.csv $(RESULTS)/*.png
