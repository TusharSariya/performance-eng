#!/usr/bin/env python3
"""
flamegraph.py — Folded Stacks → Interactive SVG Flame Graph

Reads Brendan Gregg's folded stack format from stdin or a file and
produces an interactive SVG flame graph with zoom, search, and tooltips.

Usage:
    ./selfprofile | python3 flamegraph.py > flame.svg
    python3 flamegraph.py -i stacks.folded -o flame.svg
    python3 flamegraph.py --title "My Profile" --width 1400 < stacks.folded
"""

import sys
import argparse
import hashlib
from collections import defaultdict


# ── Frame tree ──────────────────────────────────────────────────

class Frame:
    __slots__ = ('name', 'count', 'self_count', 'children', 'child_map')

    def __init__(self, name):
        self.name = name
        self.count = 0
        self.self_count = 0
        self.children = []
        self.child_map = {}

    def add_child(self, name):
        if name in self.child_map:
            return self.child_map[name]
        child = Frame(name)
        self.children.append(child)
        self.child_map[name] = child
        return child

    def sort_children(self):
        self.children.sort(key=lambda f: f.name)
        for c in self.children:
            c.sort_children()

    def max_depth(self, depth=0):
        m = depth
        for c in self.children:
            d = c.max_depth(depth + 1)
            if d > m:
                m = d
        return m


# ── Parse folded stacks ────────────────────────────────────────

def parse_folded(lines):
    root = Frame("root")
    total = 0

    for line in lines:
        line = line.strip()
        if not line or line.startswith('#'):
            continue

        # Format: func_a;func_b;func_c 42
        idx = line.rfind(' ')
        if idx < 0:
            continue

        stack_str = line[:idx]
        try:
            count = int(line[idx + 1:])
        except ValueError:
            count = 1
        if count <= 0:
            count = 1

        total += count
        funcs = stack_str.split(';')

        node = root
        node.count += count
        for func in funcs:
            node = node.add_child(func)
            node.count += count
        node.self_count += count

    return root, total


# ── Color generation ───────────────────────────────────────────

def name_to_color(name):
    """Warm color palette based on function name hash."""
    h = int(hashlib.md5(name.encode()).hexdigest()[:8], 16)
    hue = h % 60           # 0-60: red to yellow
    sat = 160 + (h >> 8) % 55
    val = 200 + (h >> 16) % 56

    # HSV to RGB (hue 0-60 → red to yellow)
    hf = hue / 60.0
    s = sat / 255.0
    v = val / 255.0
    c = v * s
    x = c * (1.0 - abs(hf if hf <= 1.0 else hf - 1.0))
    m = v - c

    if hf < 1.0:
        rf, gf, bf = c, x, 0
    else:
        rf, gf, bf = x, c, 0

    r = int((rf + m) * 255)
    g = int((gf + m) * 255)
    b = int((bf + m) * 55 + 30)
    return r, g, b


# ── XML escaping ───────────────────────────────────────────────

def xml_escape(s):
    return (s.replace('&', '&amp;')
             .replace('<', '&lt;')
             .replace('>', '&gt;')
             .replace('"', '&quot;'))


# ── SVG rendering ──────────────────────────────────────────────

FRAME_HEIGHT = 16
FONT_SIZE = 11
MIN_WIDTH_PX = 0.1
CHAR_WIDTH = 6.5
MARGIN = 10


def render_frame(out, frame, depth, x_left, x_width, svg_height, total, frame_id_counter):
    """Render a single frame and its children recursively."""
    if x_width < MIN_WIDTH_PX:
        return frame_id_counter

    y = svg_height - 30 - (depth + 1) * FRAME_HEIGHT

    if depth == 0:
        r, g, b = 200, 200, 200
    else:
        r, g, b = name_to_color(frame.name)

    pct = 100.0 * frame.count / total if total > 0 else 0
    self_pct = 100.0 * frame.self_count / total if total > 0 else 0
    escaped_name = xml_escape(frame.name)

    fid = f"f{frame_id_counter}"
    frame_id_counter += 1

    # Group with title for tooltip
    out.write(f'<g id="{fid}" class="fg">\n')
    out.write(f'<title>{escaped_name} ({frame.count} samples, {pct:.1f}%'
              f'{f", self: {self_pct:.1f}%" if frame.self_count > 0 else ""})</title>\n')
    out.write(f'<rect x="{x_left:.1f}" y="{y:.1f}" width="{x_width:.1f}" '
              f'height="{FRAME_HEIGHT - 1}" fill="rgb({r},{g},{b})" '
              f'rx="1" ry="1" class="frame" '
              f'data-name="{escaped_name}" />\n')

    # Text label
    text_width = len(frame.name) * CHAR_WIDTH
    if x_width > text_width + 6:
        out.write(f'<text x="{x_left + 3:.1f}" y="{y + FRAME_HEIGHT - 4:.1f}" '
                  f'font-size="{FONT_SIZE}" font-family="monospace" fill="#000">'
                  f'{escaped_name}</text>\n')
    elif x_width > 20:
        max_chars = int((x_width - 6) / CHAR_WIDTH)
        if max_chars > 0:
            trunc = xml_escape(frame.name[:max_chars])
            out.write(f'<text x="{x_left + 3:.1f}" y="{y + FRAME_HEIGHT - 4:.1f}" '
                      f'font-size="{FONT_SIZE}" font-family="monospace" fill="#000">'
                      f'{trunc}..</text>\n')

    out.write('</g>\n')

    # Render children
    child_x = x_left
    for child in frame.children:
        child_w = x_width * (child.count / frame.count) if frame.count > 0 else 0
        frame_id_counter = render_frame(out, child, depth + 1, child_x, child_w,
                                        svg_height, total, frame_id_counter)
        child_x += child_w

    return frame_id_counter


SVG_JAVASCRIPT = """
<script type="text/javascript">
<![CDATA[
(function() {
    var frames = document.querySelectorAll('.frame');
    var details = document.getElementById('details');
    var searchMatch = document.getElementById('search-match');
    var zoomStack = [];

    // Hover effects
    frames.forEach(function(f) {
        f.style.cursor = 'pointer';
        f.addEventListener('mouseover', function() {
            f.style.stroke = '#000';
            f.style.strokeWidth = '0.5';
            var t = f.parentNode.querySelector('title');
            if (t && details) details.textContent = t.textContent;
        });
        f.addEventListener('mouseout', function() {
            f.style.stroke = 'none';
            if (details) details.textContent = '';
        });

        // Click to zoom
        f.addEventListener('click', function() {
            var name = f.getAttribute('data-name');
            if (!name) return;
            zoomToFrame(name);
        });
    });

    function zoomToFrame(name) {
        // Highlight matching frames
        var matched = 0, total = 0;
        frames.forEach(function(f) {
            total++;
            var n = f.getAttribute('data-name');
            if (n === name) {
                f.style.opacity = '1';
                f.style.stroke = '#000';
                f.style.strokeWidth = '1';
                matched++;
            } else {
                f.style.opacity = '0.4';
                f.style.stroke = 'none';
            }
        });
        if (searchMatch) searchMatch.textContent = 'Focused: ' + name + ' (' + matched + ' frames)';
        zoomStack.push(name);
    }

    function resetView() {
        frames.forEach(function(f) {
            f.style.opacity = '1';
            f.style.stroke = 'none';
        });
        if (searchMatch) searchMatch.textContent = '';
        zoomStack = [];
    }

    // Keyboard shortcuts
    document.addEventListener('keydown', function(e) {
        // Ctrl+F: search
        if (e.ctrlKey && e.key === 'f') {
            e.preventDefault();
            var term = prompt('Search function name:');
            if (!term) { resetView(); return; }
            term = term.toLowerCase();
            var matched = 0;
            frames.forEach(function(f) {
                var t = f.parentNode.querySelector('title');
                if (t && t.textContent.toLowerCase().indexOf(term) >= 0) {
                    f.style.opacity = '1';
                    matched++;
                } else {
                    f.style.opacity = '0.3';
                }
            });
            if (searchMatch) searchMatch.textContent = 'Search: "' + term + '" (' + matched + ' matches)';
        }
        // Escape: reset
        if (e.key === 'Escape') {
            resetView();
        }
    });

    // Reset button
    var resetBtn = document.getElementById('reset-btn');
    if (resetBtn) {
        resetBtn.addEventListener('click', resetView);
        resetBtn.style.cursor = 'pointer';
    }
})();
]]>
</script>
"""


def render_svg(out, root, total, title="Flame Graph", width=1200):
    """Render the complete SVG flame graph."""
    root.sort_children()
    depth = root.max_depth()
    height = (depth + 2) * FRAME_HEIGHT + 80

    out.write('<?xml version="1.0" standalone="no"?>\n')
    out.write(f'<svg xmlns="http://www.w3.org/2000/svg" '
              f'width="{width}" height="{height}" '
              f'viewBox="0 0 {width} {height}">\n')

    # Background
    out.write(f'<rect width="100%" height="100%" fill="#f8f8f8" />\n')

    # Title
    out.write(f'<text x="{width // 2}" y="20" font-size="16" font-family="sans-serif" '
              f'text-anchor="middle" fill="#333">{xml_escape(title)}</text>\n')

    # Subtitle
    out.write(f'<text x="{width // 2}" y="36" font-size="11" font-family="sans-serif" '
              f'text-anchor="middle" fill="#888">{total} samples. '
              f'Click to focus, Ctrl+F to search, Esc to reset.</text>\n')

    # Reset button
    out.write(f'<text id="reset-btn" x="{width - 60}" y="20" font-size="11" '
              f'font-family="sans-serif" fill="#4477cc">[Reset]</text>\n')

    # Search match indicator
    out.write(f'<text id="search-match" x="4" y="{height - 22}" font-size="11" '
              f'font-family="monospace" fill="#cc4444"></text>\n')

    # Details bar
    out.write(f'<text id="details" x="4" y="{height - 6}" font-size="11" '
              f'font-family="monospace" fill="#333"></text>\n')

    # Render all frames
    render_frame(out, root, 0, MARGIN, width - 2 * MARGIN, height, total, 0)

    # JavaScript
    out.write(SVG_JAVASCRIPT)

    out.write('</svg>\n')


# ── Main ────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='Generate an interactive SVG flame graph from folded stacks')
    parser.add_argument('-i', '--input', help='Input file (default: stdin)')
    parser.add_argument('-o', '--output', help='Output file (default: stdout)')
    parser.add_argument('-t', '--title', default='Flame Graph', help='Graph title')
    parser.add_argument('-w', '--width', type=int, default=1200, help='SVG width in pixels')
    parser.add_argument('--colors', choices=['warm', 'hot', 'cool'], default='warm',
                        help='Color palette (default: warm)')
    args = parser.parse_args()

    # Read input
    if args.input:
        with open(args.input) as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()

    if not lines:
        print("flamegraph.py: no input", file=sys.stderr)
        sys.exit(1)

    root, total = parse_folded(lines)

    if total == 0:
        print("flamegraph.py: no samples found in input", file=sys.stderr)
        sys.exit(1)

    print(f"flamegraph.py: {total} samples, generating SVG...", file=sys.stderr)

    # Write output
    if args.output:
        with open(args.output, 'w') as f:
            render_svg(f, root, total, args.title, args.width)
    else:
        render_svg(sys.stdout, root, total, args.title, args.width)

    print("flamegraph.py: done", file=sys.stderr)


if __name__ == '__main__':
    main()
