/*
 * bench_realistic.c — Milestone 4: Realistic application workloads
 *
 * Simulates real-world allocation patterns:
 *   1. Web server    — request buffers, header strings, response building
 *   2. Key-value store — random inserts/deletes of variable-size values
 *   3. JSON parser   — tree of small nodes with varying lifetimes
 *
 * Usage:
 *   ./bench_realistic [--csv] [workload_name]
 *   LD_PRELOAD=/path/to/libjemalloc.so ./bench_realistic
 */
#include "common.h"

/* ── Configuration ──────────────────────────────────────────────────── */

static int csv_mode = 0;

static long get_ops(long default_ops)
{
    const char *env = getenv("OPS");
    if (env) {
        long val = atol(env);
        if (val > 0) return val;
    }
    return default_ops;
}

typedef struct {
    const char *name;
    long    ops;
    double  elapsed_ms;
    double  ops_per_sec;
    long    rss_peak_kb;
    long    peak_live_bytes;
    double  frag_ratio;
} realistic_result_t;

/* ── 1. Web server simulation ───────────────────────────────────────── */

/*
 * Simulates processing HTTP requests:
 *   - Allocate a request buffer (2-8 KB)
 *   - Allocate N header key-value pairs (small strings, 16-128 bytes each)
 *   - Allocate a response body (1-32 KB)
 *   - Free everything (request complete)
 *
 * This creates a burst-allocate-then-free-all pattern common in
 * request-scoped memory usage.
 */
static realistic_result_t bench_webserver(long ops)
{
    realistic_result_t r = { .name = "webserver" };

    uint64_t rng = 0xEB000001234ULL;
    long live_bytes = 0, peak_live = 0;

    (void)get_rss_kb();  /* baseline RSS read */
    uint64_t t0 = now_ns();

    for (long req = 0; req < ops; req++) {
        /* Request buffer */
        size_t req_sz = rand_size(&rng, 2048, 8192);
        char *req_buf = malloc(req_sz);
        if (req_buf) { req_buf[0] = 'G'; live_bytes += req_sz; }

        /* Headers: 5-20 key-value pairs */
        int nheaders = 5 + (int)(xorshift64(&rng) % 16);
        char **hdr_keys = malloc(nheaders * sizeof(char *));
        char **hdr_vals = malloc(nheaders * sizeof(char *));
        size_t hdr_total = nheaders * sizeof(char *) * 2;
        live_bytes += hdr_total;

        for (int h = 0; h < nheaders; h++) {
            size_t ksz = rand_size(&rng, 16, 64);
            size_t vsz = rand_size(&rng, 16, 128);
            hdr_keys[h] = malloc(ksz);
            hdr_vals[h] = malloc(vsz);
            if (hdr_keys[h]) { hdr_keys[h][0] = 'K'; live_bytes += ksz; }
            if (hdr_vals[h]) { hdr_vals[h][0] = 'V'; live_bytes += vsz; }
        }

        /* Response body */
        size_t resp_sz = rand_size(&rng, 1024, 32768);
        char *resp_buf = malloc(resp_sz);
        if (resp_buf) { resp_buf[0] = '<'; live_bytes += resp_sz; }

        if (live_bytes > peak_live) peak_live = live_bytes;

        /* Free everything (request done) */
        free(resp_buf); live_bytes -= resp_sz;
        for (int h = 0; h < nheaders; h++) {
            if (hdr_keys[h]) { size_t ksz = rand_size(&rng, 16, 64); (void)ksz; free(hdr_keys[h]); }
            if (hdr_vals[h]) { free(hdr_vals[h]); }
        }
        /* We can't perfectly track sizes since we're using random, so approximate */
        live_bytes = 0;  /* simplification: per-request all freed */
        free(hdr_keys);
        free(hdr_vals);
        free(req_buf);
    }

    uint64_t t1 = now_ns();
    r.ops = ops;
    r.elapsed_ms = elapsed_ms(t0, t1);
    r.ops_per_sec = (double)ops / elapsed_s(t0, t1);
    r.rss_peak_kb = get_rss_kb();
    r.peak_live_bytes = peak_live;
    r.frag_ratio = (peak_live > 0) ? (double)(r.rss_peak_kb * 1024L) / peak_live : 0;

    return r;
}

/* ── 2. Key-value store simulation ──────────────────────────────────── */

/*
 * Simulates a hash map with random inserts, lookups, and deletes:
 *   - Each entry has a key (16-64 bytes) and value (64-8192 bytes)
 *   - Operations: 50% insert/update, 30% lookup (read), 20% delete
 *   - Steady-state occupancy ~50K entries
 */
#define KV_SLOTS 65536

typedef struct {
    char   *key;
    char   *value;
    size_t  key_sz;
    size_t  val_sz;
} kv_entry_t;

static realistic_result_t bench_kvstore(long ops)
{
    realistic_result_t r = { .name = "kvstore" };

    uint64_t rng = 0xAB57000012ULL;

    kv_entry_t *table = calloc(KV_SLOTS, sizeof(kv_entry_t));
    if (!table) { perror("calloc"); exit(1); }

    long live_bytes = 0, peak_live = 0;
    long inserts = 0, deletes = 0, lookups = 0;

    uint64_t t0 = now_ns();

    for (long i = 0; i < ops; i++) {
        long idx = (long)(xorshift64(&rng) % KV_SLOTS);
        uint64_t op = xorshift64(&rng) % 100;

        if (op < 50) {
            /* INSERT / UPDATE */
            if (table[idx].key) {
                live_bytes -= table[idx].key_sz + table[idx].val_sz;
                free(table[idx].key);
                free(table[idx].value);
            }
            table[idx].key_sz = rand_size(&rng, 16, 64);
            table[idx].val_sz = rand_size(&rng, 64, 8192);
            table[idx].key = malloc(table[idx].key_sz);
            table[idx].value = malloc(table[idx].val_sz);
            if (table[idx].key) table[idx].key[0] = 'k';
            if (table[idx].value) table[idx].value[0] = 'v';
            live_bytes += table[idx].key_sz + table[idx].val_sz;
            inserts++;
        } else if (op < 80) {
            /* LOOKUP (just touch the memory) */
            if (table[idx].key) {
                volatile char c = table[idx].key[0];
                (void)c;
                if (table[idx].value)
                    c = table[idx].value[0];
            }
            lookups++;
        } else {
            /* DELETE */
            if (table[idx].key) {
                live_bytes -= table[idx].key_sz + table[idx].val_sz;
                free(table[idx].key);
                free(table[idx].value);
                table[idx].key = NULL;
                table[idx].value = NULL;
                table[idx].key_sz = 0;
                table[idx].val_sz = 0;
                deletes++;
            }
        }

        if (live_bytes > peak_live) peak_live = live_bytes;
    }

    uint64_t t1 = now_ns();

    /* Cleanup */
    for (long i = 0; i < KV_SLOTS; i++) {
        free(table[i].key);
        free(table[i].value);
    }
    free(table);

    r.ops = ops;
    r.elapsed_ms = elapsed_ms(t0, t1);
    r.ops_per_sec = (double)ops / elapsed_s(t0, t1);
    r.rss_peak_kb = get_rss_kb();
    r.peak_live_bytes = peak_live;
    r.frag_ratio = (peak_live > 0) ? (double)(r.rss_peak_kb * 1024L) / peak_live : 0;

    return r;
}

/* ── 3. JSON parser tree simulation ─────────────────────────────────── */

/*
 * Simulates parsing JSON documents into a tree of nodes:
 *   - Each node is small (32-64 bytes)
 *   - String values are 8-256 bytes
 *   - Trees have 50-500 nodes
 *   - Parse a document, process it, then free the whole tree
 *   - Multiple documents alive simultaneously (pipeline)
 */

typedef struct json_node {
    struct json_node *children[4];  /* up to 4 children */
    int               nchildren;
    char             *str_value;     /* optional string value */
    size_t            str_len;
    int               type;          /* 0=object, 1=array, 2=string, 3=number */
} json_node_t;

static json_node_t *make_json_tree(uint64_t *rng, int depth, int max_depth,
                                    long *live_bytes)
{
    json_node_t *node = malloc(sizeof(json_node_t));
    if (!node) return NULL;
    *live_bytes += sizeof(json_node_t);

    memset(node, 0, sizeof(*node));
    node->type = (int)(xorshift64(rng) % 4);

    /* String values for ~60% of nodes */
    if (xorshift64(rng) % 100 < 60) {
        node->str_len = rand_size(rng, 8, 256);
        node->str_value = malloc(node->str_len);
        if (node->str_value) {
            node->str_value[0] = '"';
            *live_bytes += node->str_len;
        }
    }

    /* Recurse for object/array nodes if not too deep */
    if (depth < max_depth && (node->type == 0 || node->type == 1)) {
        int nc = 1 + (int)(xorshift64(rng) % 4);
        node->nchildren = nc;
        for (int i = 0; i < nc; i++) {
            node->children[i] = make_json_tree(rng, depth + 1, max_depth, live_bytes);
        }
    }

    return node;
}

static void free_json_tree(json_node_t *node, long *live_bytes)
{
    if (!node) return;
    for (int i = 0; i < node->nchildren; i++)
        free_json_tree(node->children[i], live_bytes);
    if (node->str_value) {
        *live_bytes -= node->str_len;
        free(node->str_value);
    }
    *live_bytes -= sizeof(json_node_t);
    free(node);
}

static realistic_result_t bench_json_parser(long ops)
{
    realistic_result_t r = { .name = "json_parser" };

    uint64_t rng = 0x150A000012ULL;

    long live_bytes = 0, peak_live = 0;

    /* Keep a pipeline of N documents alive simultaneously */
    #define PIPELINE_SIZE 8
    json_node_t *pipeline[PIPELINE_SIZE] = {0};
    int pipe_idx = 0;

    uint64_t t0 = now_ns();

    for (long i = 0; i < ops; i++) {
        /* Free the oldest document in the pipeline */
        if (pipeline[pipe_idx]) {
            free_json_tree(pipeline[pipe_idx], &live_bytes);
            pipeline[pipe_idx] = NULL;
        }

        /* Parse a new document (create tree) */
        int max_depth = 3 + (int)(xorshift64(&rng) % 4); /* depth 3-6 */
        pipeline[pipe_idx] = make_json_tree(&rng, 0, max_depth, &live_bytes);

        if (live_bytes > peak_live) peak_live = live_bytes;

        pipe_idx = (pipe_idx + 1) % PIPELINE_SIZE;
    }

    /* Cleanup remaining pipeline */
    for (int i = 0; i < PIPELINE_SIZE; i++) {
        if (pipeline[i])
            free_json_tree(pipeline[i], &live_bytes);
    }

    uint64_t t1 = now_ns();

    r.ops = ops;
    r.elapsed_ms = elapsed_ms(t0, t1);
    r.ops_per_sec = (double)ops / elapsed_s(t0, t1);
    r.rss_peak_kb = get_rss_kb();
    r.peak_live_bytes = peak_live;
    r.frag_ratio = (peak_live > 0) ? (double)(r.rss_peak_kb * 1024L) / peak_live : 0;

    return r;
}

/* ── Output ─────────────────────────────────────────────────────────── */

static void print_result(const realistic_result_t *r)
{
    if (csv_mode) {
        printf("%s,%s,%ld,%.1f,%.0f,%ld,%ld,%.2f\n",
               detect_allocator(), r->name, r->ops,
               r->elapsed_ms, r->ops_per_sec,
               r->rss_peak_kb, r->peak_live_bytes, r->frag_ratio);
        return;
    }

    char buf1[32], buf2[32], buf3[32];
    printf("\n  Workload: %s\n", r->name);
    print_separator();
    printf("  Operations        : %ld\n", r->ops);
    printf("  Total time        : %.1f ms\n", r->elapsed_ms);
    printf("  Throughput        : %s ops/sec\n", format_ops(r->ops_per_sec, buf1, sizeof(buf1)));
    printf("  RSS peak          : %s\n", format_bytes(r->rss_peak_kb * 1024L, buf2, sizeof(buf2)));
    printf("  Peak live bytes   : %s\n", format_bytes(r->peak_live_bytes, buf3, sizeof(buf3)));
    printf("  Frag ratio        : %.2f\n", r->frag_ratio);
}

static void print_csv_header(void)
{
    printf("allocator,workload,ops,elapsed_ms,ops_per_sec,"
           "rss_peak_kb,peak_live_bytes,frag_ratio\n");
}

/* ── Main ───────────────────────────────────────────────────────────── */

typedef realistic_result_t (*realistic_fn)(long ops);

static struct {
    const char     *name;
    realistic_fn    fn;
    long            default_ops;
} realistic_workloads[] = {
    { "webserver",    bench_webserver,    100000 },
    { "kvstore",      bench_kvstore,     2000000 },
    { "json_parser",  bench_json_parser,  100000 },
};
static const int NUM_WORKLOADS = sizeof(realistic_workloads) / sizeof(realistic_workloads[0]);

int main(int argc, char *argv[])
{
    const char *filter = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0)
            csv_mode = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: %s [--csv] [workload_name]\n", argv[0]);
            fprintf(stderr, "Workloads: webserver, kvstore, json_parser\n");
            return 0;
        } else
            filter = argv[i];
    }

    if (!csv_mode) {
        printf("Memory Allocator Realistic Workloads\n");
        print_separator();
        printf("  Allocator : %s\n", detect_allocator());
        printf("  Cores     : %d\n", get_num_cores());
        printf("  PID       : %d\n", getpid());
    } else {
        print_csv_header();
    }

    for (int i = 0; i < NUM_WORKLOADS; i++) {
        if (filter && strcmp(filter, realistic_workloads[i].name) != 0)
            continue;
        long ops = get_ops(realistic_workloads[i].default_ops);
        realistic_result_t r = realistic_workloads[i].fn(ops);
        print_result(&r);
    }

    return 0;
}
