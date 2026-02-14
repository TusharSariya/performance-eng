# Makefile — 03-scheduler-latency-monitor
#
# Targets:
#   all         Build BPF program, userspace loader, and cpu_stress
#   vmlinux     Generate src/vmlinux.h from /sys/kernel/btf/vmlinux
#   clean       Remove build artifacts
#   demo        Build all and run idle vs loaded comparison

CC       = gcc
CLANG    = clang
BPFTOOL  = bpftool
CFLAGS   = -O2 -Wall -Wextra
BINDIR   = bin
SRCDIR   = src
SAMPDIR  = samples
RESDIR   = results

# BPF compilation flags
BPF_CFLAGS = -target bpf -D__TARGET_ARCH_x86 -O2 -g

# libbpf: use kernel headers (libbpf 1.4) for both BPF and userspace.
# The system libbpf0 (0.5.0) is too old for kernel 6.8 BTF format,
# so we statically link against the kernel's bundled libbpf instead.
KLIBBPF  = /usr/src/linux-headers-$(shell uname -r)/tools/bpf/resolve_btfids/libbpf
BPF_INC  = -I$(KLIBBPF)/include -I/usr/include

.PHONY: all clean demo vmlinux

all: $(BINDIR)/runqlat.bpf.o $(BINDIR)/runqlat $(BINDIR)/cpu_stress

# ── vmlinux.h generation (only if missing) ─────────────────────

$(SRCDIR)/vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

vmlinux: $(SRCDIR)/vmlinux.h

# ── BPF program ────────────────────────────────────────────────

$(BINDIR)/runqlat.bpf.o: $(SRCDIR)/runqlat.bpf.c $(SRCDIR)/runqlat.h $(SRCDIR)/vmlinux.h | $(BINDIR)
	$(CLANG) $(BPF_CFLAGS) $(BPF_INC) -I$(SRCDIR) -c $< -o $@

# ── Userspace loader (static link against kernel libbpf 1.4) ──

$(BINDIR)/runqlat: $(SRCDIR)/runqlat.c $(SRCDIR)/runqlat.h | $(BINDIR)
	$(CC) $(CFLAGS) -I$(KLIBBPF)/include -o $@ $< $(KLIBBPF)/libbpf.a -lelf -lz

# ── Test workload ──────────────────────────────────────────────

$(BINDIR)/cpu_stress: $(SAMPDIR)/cpu_stress.c | $(BINDIR)
	$(CC) -O1 -Wall -pthread -o $@ $< -lm

# ── Directories ────────────────────────────────────────────────

$(BINDIR):
	mkdir -p $(BINDIR)

$(RESDIR):
	mkdir -p $(RESDIR)

# ── Demo ───────────────────────────────────────────────────────

demo: all | $(RESDIR)
	@echo "Run: sudo bash scripts/run_demo.sh"

# ── Clean ──────────────────────────────────────────────────────

clean:
	rm -rf $(BINDIR)
	rm -f $(RESDIR)/*.csv $(RESDIR)/*.png
