/*
 * symbols.c — Symbol resolution via /proc/<pid>/maps + addr2line
 *
 * 1. Parse /proc/<pid>/maps to build a table of VMAs (virtual memory areas)
 * 2. For each address, find the containing VMA and compute the file offset
 * 3. Call addr2line to resolve the offset to a function name
 * 4. Cache results to avoid repeated addr2line invocations
 */
#define _GNU_SOURCE
#include "symbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── VMA table ──────────────────────────────────────────────── */

#define MAX_VMAS 4096

struct vma {
    uint64_t start;
    uint64_t end;
    uint64_t offset;
    char     perms[5];
    char     path[512];
};

static struct vma vmas[MAX_VMAS];
static int n_vmas = 0;

/* ── Symbol cache (simple open-addressing hash map) ─────────── */

#define CACHE_SIZE 65536  /* must be power of 2 */
#define CACHE_MASK (CACHE_SIZE - 1)

struct cache_entry {
    uint64_t addr;
    char     name[256];
    int      valid;
};

static struct cache_entry sym_cache[CACHE_SIZE];

static struct cache_entry *cache_lookup(uint64_t addr)
{
    uint32_t h = (uint32_t)(addr * 2654435761ULL) & CACHE_MASK;
    for (int i = 0; i < 16; i++) {  /* linear probe, max 16 steps */
        struct cache_entry *e = &sym_cache[(h + i) & CACHE_MASK];
        if (!e->valid)
            return e;  /* empty slot — caller can fill */
        if (e->addr == addr)
            return e;  /* found */
    }
    return NULL;  /* cache full in this region */
}

/* ── Parse /proc/<pid>/maps ─────────────────────────────────── */

int sym_init(int pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }

    n_vmas = 0;
    memset(sym_cache, 0, sizeof(sym_cache));

    char line[1024];
    while (fgets(line, sizeof(line), f) && n_vmas < MAX_VMAS) {
        struct vma *v = &vmas[n_vmas];
        v->path[0] = '\0';

        /* Format: start-end perms offset dev inode pathname */
        int n = sscanf(line, "%lx-%lx %4s %lx %*s %*s %511[^\n]",
                       &v->start, &v->end, v->perms, &v->offset, v->path);

        if (n >= 4) {
            /* Strip leading whitespace from path */
            char *p = v->path;
            while (*p == ' ') p++;
            if (p != v->path)
                memmove(v->path, p, strlen(p) + 1);

            /* Only keep executable mappings with file paths */
            if (v->perms[2] == 'x' && v->path[0] == '/') {
                n_vmas++;
            }
        }
    }

    fclose(f);
    return 0;
}

/* ── Find VMA for address ───────────────────────────────────── */

static const struct vma *find_vma(uint64_t addr)
{
    for (int i = 0; i < n_vmas; i++) {
        if (addr >= vmas[i].start && addr < vmas[i].end)
            return &vmas[i];
    }
    return NULL;
}

/* ── Resolve via addr2line ──────────────────────────────────── */

static const char *resolve_via_addr2line(uint64_t addr, const struct vma *v)
{
    /* Compute offset within the file */
    uint64_t file_offset = addr - v->start + v->offset;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "addr2line -f -e '%s' 0x%lx 2>/dev/null",
             v->path, file_offset);

    FILE *p = popen(cmd, "r");
    if (!p)
        return NULL;

    char func[256] = {0};
    if (fgets(func, sizeof(func), p)) {
        /* Remove trailing newline */
        func[strcspn(func, "\n")] = '\0';
    }

    pclose(p);

    if (func[0] && strcmp(func, "??") != 0)
        return strdup(func);

    return NULL;
}

/* ── Public API ─────────────────────────────────────────────── */

const char *sym_resolve(uint64_t addr)
{
    if (addr == 0)
        return "[null]";

    /* Check cache first */
    struct cache_entry *ce = cache_lookup(addr);
    if (ce && ce->valid && ce->addr == addr)
        return ce->name;

    /* Find VMA */
    const struct vma *v = find_vma(addr);
    const char *name = NULL;

    if (v) {
        name = resolve_via_addr2line(addr, v);
    }

    if (!name) {
        /* Try kernel addresses (above 0xffff...) */
        if (addr >= 0xffff000000000000ULL)
            name = "[kernel]";
        else
            name = "[unknown]";
    }

    /* Store in cache */
    if (ce) {
        ce->addr = addr;
        strncpy(ce->name, name, sizeof(ce->name) - 1);
        ce->name[sizeof(ce->name) - 1] = '\0';
        ce->valid = 1;

        /* Free if we strdup'd */
        if (name != ce->name && name[0] != '[')
            free((void *)name);

        return ce->name;
    }

    return name;
}

void sym_cleanup(void)
{
    n_vmas = 0;
    memset(sym_cache, 0, sizeof(sym_cache));
}
