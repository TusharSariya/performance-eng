/*
 * flamegraph.c — Milestone 3: Folded Stacks → SVG Flame Graph
 *
 * Reads folded stack format from stdin, builds a frame tree,
 * and outputs an interactive SVG flame graph.
 *
 * Usage:
 *   ./selfprofile | ./flamegraph > flame.svg
 *   ./flamegraph < stacks.folded > flame.svg
 *   ./flamegraph -t "My Profile" -w 1200 < stacks.folded > flame.svg
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* ── Configuration ──────────────────────────────────────────── */

#define MAX_CHILDREN  512
#define MAX_NAME_LEN  256
#define MAX_LINE_LEN  8192
#define FRAME_HEIGHT  16
#define FONT_SIZE     11
#define MIN_WIDTH_PX  0.1   /* minimum width to render a frame */
#define CHAR_WIDTH    6.5   /* approximate width per character */

/* ── Frame tree ─────────────────────────────────────────────── */

struct frame {
    char name[MAX_NAME_LEN];
    int  count;           /* samples IN this frame (including children) */
    int  self_count;      /* samples WHERE this frame is the leaf */
    struct frame *children[MAX_CHILDREN];
    int n_children;
};

static struct frame *frame_new(const char *name)
{
    struct frame *f = calloc(1, sizeof(*f));
    if (!f) { perror("calloc"); exit(1); }
    strncpy(f->name, name, MAX_NAME_LEN - 1);
    return f;
}

static struct frame *frame_find_child(struct frame *parent, const char *name)
{
    for (int i = 0; i < parent->n_children; i++) {
        if (strcmp(parent->children[i]->name, name) == 0)
            return parent->children[i];
    }
    return NULL;
}

static struct frame *frame_add_child(struct frame *parent, const char *name)
{
    struct frame *child = frame_find_child(parent, name);
    if (child) return child;

    if (parent->n_children >= MAX_CHILDREN) {
        fprintf(stderr, "warning: too many children for '%s'\n", parent->name);
        return parent;
    }

    child = frame_new(name);
    parent->children[parent->n_children++] = child;
    return child;
}

static void frame_free(struct frame *f)
{
    for (int i = 0; i < f->n_children; i++)
        frame_free(f->children[i]);
    free(f);
}

/* ── Parse folded stacks ────────────────────────────────────── */

static struct frame *root;
static int total_samples = 0;

static void parse_folded(FILE *in)
{
    root = frame_new("root");

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), in)) {
        /* Format: func_a;func_b;func_c 42 */
        line[strcspn(line, "\n\r")] = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;

        /* Find the last space — count is after it */
        char *last_space = strrchr(line, ' ');
        if (!last_space) continue;

        int count = atoi(last_space + 1);
        if (count <= 0) count = 1;
        *last_space = '\0';

        total_samples += count;

        /* Walk the stack and add to tree */
        struct frame *node = root;
        node->count += count;

        char *saveptr;
        char *tok = strtok_r(line, ";", &saveptr);
        struct frame *leaf = node;
        while (tok) {
            node = frame_add_child(node, tok);
            node->count += count;
            leaf = node;
            tok = strtok_r(NULL, ";", &saveptr);
        }
        leaf->self_count += count;
    }
}

/* ── Color generation ───────────────────────────────────────── */

/* Warm color palette (red/orange/yellow) based on function name hash */
static void name_to_color(const char *name, int *r, int *g, int *b)
{
    uint32_t hash = 5381;
    for (const char *p = name; *p; p++)
        hash = ((hash << 5) + hash) + (unsigned char)*p;

    /* Warm palette: hue 0-60 (red to yellow) */
    int hue = hash % 60;
    int sat = 160 + (hash >> 8) % 55;   /* 160-215 */
    int val = 200 + (hash >> 16) % 56;  /* 200-255 */

    /* HSV to RGB (simplified, hue 0-60 maps to red→yellow) */
    float h = hue / 60.0f;
    float s = sat / 255.0f;
    float v = val / 255.0f;
    float c = v * s;
    float x = c * (1.0f - ((h > 1.0f ? h - 1.0f : h) < 0 ? -(h) : h));
    float m = v - c;

    float rf, gf, bf;
    if (h < 1.0f) { rf = c; gf = x; bf = 0; }
    else           { rf = x; gf = c; bf = 0; }

    *r = (int)((rf + m) * 255);
    *g = (int)((gf + m) * 255);
    *b = (int)((bf + m) * 55 + 30);  /* slight blue tint for depth */
}

/* ── SVG rendering ──────────────────────────────────────────── */

static int svg_width = 1200;
static int svg_height;
static FILE *svg_out;

/* Find maximum depth for sizing */
static int max_depth(struct frame *f, int depth)
{
    int m = depth;
    for (int i = 0; i < f->n_children; i++) {
        int d = max_depth(f->children[i], depth + 1);
        if (d > m) m = d;
    }
    return m;
}

/* Sort children alphabetically for consistent layout */
static int cmp_frame_name(const void *a, const void *b)
{
    const struct frame *fa = *(const struct frame **)a;
    const struct frame *fb = *(const struct frame **)b;
    return strcmp(fa->name, fb->name);
}

static void sort_children(struct frame *f)
{
    if (f->n_children > 1)
        qsort(f->children, f->n_children, sizeof(f->children[0]), cmp_frame_name);
    for (int i = 0; i < f->n_children; i++)
        sort_children(f->children[i]);
}

/* Escape XML special characters */
static void xml_escape(FILE *out, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '<': fputs("&lt;", out); break;
        case '>': fputs("&gt;", out); break;
        case '&': fputs("&amp;", out); break;
        case '"': fputs("&quot;", out); break;
        default:  fputc(*s, out); break;
        }
    }
}

static void render_frame(struct frame *f, int depth, double x_left, double x_width)
{
    if (x_width < MIN_WIDTH_PX)
        return;

    double y = svg_height - 30 - (depth + 1) * FRAME_HEIGHT;
    int r, g, b;

    if (depth == 0) {
        /* Root frame — gray */
        r = 200; g = 200; b = 200;
    } else {
        name_to_color(f->name, &r, &g, &b);
    }

    double pct = (total_samples > 0) ? 100.0 * f->count / total_samples : 0;

    /* Rectangle */
    fprintf(svg_out, "<g>\n");
    fprintf(svg_out, "<title>");
    xml_escape(svg_out, f->name);
    fprintf(svg_out, " (%d samples, %.1f%%)</title>\n", f->count, pct);
    fprintf(svg_out, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%d\" "
            "fill=\"rgb(%d,%d,%d)\" rx=\"1\" ry=\"1\" "
            "class=\"frame\" />\n",
            x_left, y, x_width, FRAME_HEIGHT - 1, r, g, b);

    /* Text label (only if box is wide enough) */
    double text_width = strlen(f->name) * CHAR_WIDTH;
    if (x_width > text_width + 6) {
        fprintf(svg_out, "<text x=\"%.1f\" y=\"%.1f\" font-size=\"%d\" "
                "font-family=\"monospace\" fill=\"#000\">",
                x_left + 3, y + FRAME_HEIGHT - 4, FONT_SIZE);
        xml_escape(svg_out, f->name);
        fprintf(svg_out, "</text>\n");
    } else if (x_width > 20) {
        /* Truncated name */
        int max_chars = (int)((x_width - 6) / CHAR_WIDTH);
        if (max_chars > 0) {
            fprintf(svg_out, "<text x=\"%.1f\" y=\"%.1f\" font-size=\"%d\" "
                    "font-family=\"monospace\" fill=\"#000\">",
                    x_left + 3, y + FRAME_HEIGHT - 4, FONT_SIZE);
            char trunc[MAX_NAME_LEN];
            strncpy(trunc, f->name, max_chars);
            trunc[max_chars] = '\0';
            xml_escape(svg_out, trunc);
            fprintf(svg_out, "..</text>\n");
        }
    }

    fprintf(svg_out, "</g>\n");

    /* Render children */
    double child_x = x_left;
    for (int i = 0; i < f->n_children; i++) {
        double child_w = x_width * ((double)f->children[i]->count / f->count);
        render_frame(f->children[i], depth + 1, child_x, child_w);
        child_x += child_w;
    }
}

static const char *svg_javascript =
    "  <script type=\"text/javascript\">\n"
    "  <![CDATA[\n"
    "    var frames = document.querySelectorAll('.frame');\n"
    "    var details = document.getElementById('details');\n"
    "    var searchInput = null;\n"
    "    frames.forEach(function(f) {\n"
    "      f.style.cursor = 'pointer';\n"
    "      f.addEventListener('mouseover', function() {\n"
    "        f.style.stroke = '#000'; f.style.strokeWidth = '0.5';\n"
    "        var t = f.parentNode.querySelector('title');\n"
    "        if (t && details) details.textContent = t.textContent;\n"
    "      });\n"
    "      f.addEventListener('mouseout', function() {\n"
    "        f.style.stroke = 'none';\n"
    "        if (details) details.textContent = '';\n"
    "      });\n"
    "    });\n"
    "    document.addEventListener('keydown', function(e) {\n"
    "      if (e.ctrlKey && e.key === 'f') {\n"
    "        e.preventDefault();\n"
    "        var term = prompt('Search function name:');\n"
    "        if (!term) { frames.forEach(function(f){f.style.opacity='1';}); return; }\n"
    "        term = term.toLowerCase();\n"
    "        frames.forEach(function(f) {\n"
    "          var t = f.parentNode.querySelector('title');\n"
    "          if (t && t.textContent.toLowerCase().indexOf(term) >= 0)\n"
    "            f.style.opacity = '1';\n"
    "          else\n"
    "            f.style.opacity = '0.3';\n"
    "        });\n"
    "      }\n"
    "      if (e.key === 'Escape') {\n"
    "        frames.forEach(function(f){f.style.opacity='1';});\n"
    "      }\n"
    "    });\n"
    "  ]]>\n"
    "  </script>\n";

static void render_svg(FILE *out, const char *title)
{
    svg_out = out;

    sort_children(root);

    int depth = max_depth(root, 0);
    svg_height = (depth + 2) * FRAME_HEIGHT + 60;

    fprintf(out, "<?xml version=\"1.0\" standalone=\"no\"?>\n");
    fprintf(out, "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n",
            svg_width, svg_height, svg_width, svg_height);

    /* Background */
    fprintf(out, "<rect width=\"100%%\" height=\"100%%\" fill=\"#f8f8f8\" />\n");

    /* Title */
    fprintf(out, "<text x=\"%d\" y=\"20\" font-size=\"16\" font-family=\"sans-serif\" "
            "text-anchor=\"middle\" fill=\"#333\">%s</text>\n",
            svg_width / 2, title);

    /* Subtitle */
    fprintf(out, "<text x=\"%d\" y=\"36\" font-size=\"11\" font-family=\"sans-serif\" "
            "text-anchor=\"middle\" fill=\"#888\">%d samples. "
            "Ctrl+F to search, Esc to reset.</text>\n",
            svg_width / 2, total_samples);

    /* Details bar */
    fprintf(out, "<text id=\"details\" x=\"4\" y=\"%d\" font-size=\"11\" "
            "font-family=\"monospace\" fill=\"#333\"></text>\n",
            svg_height - 6);

    /* Render frames */
    double margin = 10.0;
    render_frame(root, 0, margin, svg_width - 2 * margin);

    /* JavaScript */
    fputs(svg_javascript, out);

    fprintf(out, "</svg>\n");
}

/* ── Main ───────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-t title] [-w width] [-i infile] [-o outfile]\n", prog);
    fprintf(stderr, "  Reads folded stacks from stdin (or -i file)\n");
    fprintf(stderr, "  Writes SVG to stdout (or -o file)\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    const char *title = "Flame Graph";
    const char *infile = NULL;
    const char *outfile = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "t:w:i:o:h")) != -1) {
        switch (opt) {
        case 't': title = optarg; break;
        case 'w': svg_width = atoi(optarg); break;
        case 'i': infile = optarg; break;
        case 'o': outfile = optarg; break;
        default:  usage(argv[0]);
        }
    }

    FILE *in = stdin;
    if (infile) {
        in = fopen(infile, "r");
        if (!in) { perror(infile); return 1; }
    }

    parse_folded(in);
    if (in != stdin) fclose(in);

    if (total_samples == 0) {
        fprintf(stderr, "flamegraph: no samples found in input\n");
        return 1;
    }

    fprintf(stderr, "flamegraph: %d total samples, rendering SVG (%dx%d)...\n",
            total_samples, svg_width, svg_height);

    FILE *out = stdout;
    if (outfile) {
        out = fopen(outfile, "w");
        if (!out) { perror(outfile); return 1; }
    }

    render_svg(out, title);

    if (out != stdout) fclose(out);

    frame_free(root);

    fprintf(stderr, "flamegraph: done\n");
    return 0;
}
