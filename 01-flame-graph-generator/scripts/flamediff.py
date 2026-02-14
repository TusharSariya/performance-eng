#!/usr/bin/env python3
"""
flamediff.py — Milestone 4: Differential Flame Graph

Compares two folded stack profiles and generates a differential SVG
where colors indicate regression (red) vs improvement (blue).

Usage:
    python3 flamediff.py before.folded after.folded -o diff.svg
    python3 flamediff.py -a before.folded -b after.folded --title "v1 vs v2"
"""

import sys
import argparse
from collections import defaultdict


# ── Frame tree with delta tracking ──────────────────────────────

class DiffFrame:
    __slots__ = ('name', 'count_a', 'count_b', 'self_a', 'self_b',
                 'children', 'child_map')

    def __init__(self, name):
        self.name = name
        self.count_a = 0  # "before" samples
        self.count_b = 0  # "after" samples
        self.self_a = 0
        self.self_b = 0
        self.children = []
        self.child_map = {}

    @property
    def count(self):
        return max(self.count_a, self.count_b)

    @property
    def delta(self):
        return self.count_b - self.count_a

    def add_child(self, name):
        if name in self.child_map:
            return self.child_map[name]
        child = DiffFrame(name)
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
    """Parse folded stack format, return dict of {stack_string: count}."""
    stacks = defaultdict(int)
    total = 0
    for line in lines:
        line = line.strip()
        if not line or line.startswith('#'):
            continue
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
        stacks[stack_str] += count
        total += count
    return stacks, total


def build_diff_tree(stacks_a, total_a, stacks_b, total_b):
    """Build a merged differential frame tree from two stack profiles."""
    root = DiffFrame("root")

    # Normalize: convert counts to per-sample rates, then compare
    # Or just use raw counts — we'll normalize when computing color

    all_stacks = set(stacks_a.keys()) | set(stacks_b.keys())

    for stack_str in all_stacks:
        count_a = stacks_a.get(stack_str, 0)
        count_b = stacks_b.get(stack_str, 0)

        funcs = stack_str.split(';')
        node = root
        node.count_a += count_a
        node.count_b += count_b

        for func in funcs:
            node = node.add_child(func)
            node.count_a += count_a
            node.count_b += count_b

        node.self_a += count_a
        node.self_b += count_b

    return root


# ── Color: red = regression, blue = improvement ────────────────

def delta_color(count_a, count_b, total_a, total_b):
    """
    Compute color based on normalized delta.
    Red = more samples in "after" (regression)
    Blue = fewer samples in "after" (improvement)
    Gray = unchanged
    """
    if total_a == 0 and total_b == 0:
        return 200, 200, 200

    # Normalize to rates
    rate_a = count_a / total_a if total_a > 0 else 0
    rate_b = count_b / total_b if total_b > 0 else 0

    diff = rate_b - rate_a

    if abs(diff) < 0.001:
        # Essentially unchanged — neutral gray
        return 200, 200, 200

    # Scale: map diff to color intensity
    # Max intensity at +/- 0.5 rate difference
    intensity = min(abs(diff) / 0.3, 1.0)

    if diff > 0:
        # Regression: red
        r = 200 + int(55 * intensity)
        g = 200 - int(140 * intensity)
        b = 200 - int(140 * intensity)
    else:
        # Improvement: blue
        r = 200 - int(140 * intensity)
        g = 200 - int(80 * intensity)
        b = 200 + int(55 * intensity)

    return max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b))


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


def render_diff_frame(out, frame, depth, x_left, x_width, svg_height,
                      total_a, total_b, max_count, fid_counter):
    """Render a differential frame."""
    if x_width < MIN_WIDTH_PX:
        return fid_counter

    y = svg_height - 30 - (depth + 1) * FRAME_HEIGHT

    if depth == 0:
        r, g, b = 200, 200, 200
    else:
        r, g, b = delta_color(frame.count_a, frame.count_b, total_a, total_b)

    escaped_name = xml_escape(frame.name)

    rate_a = 100.0 * frame.count_a / total_a if total_a > 0 else 0
    rate_b = 100.0 * frame.count_b / total_b if total_b > 0 else 0
    diff_pct = rate_b - rate_a

    fid = f"d{fid_counter}"
    fid_counter += 1

    sign = "+" if diff_pct >= 0 else ""

    out.write(f'<g id="{fid}" class="fg">\n')
    out.write(f'<title>{escaped_name} (before: {frame.count_a} [{rate_a:.1f}%], '
              f'after: {frame.count_b} [{rate_b:.1f}%], '
              f'delta: {sign}{diff_pct:.1f}%)</title>\n')
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

    # Children — width based on max(count_a, count_b) for visibility
    child_x = x_left
    parent_count = frame.count if frame.count > 0 else 1
    for child in frame.children:
        child_w = x_width * (child.count / parent_count)
        fid_counter = render_diff_frame(out, child, depth + 1, child_x, child_w,
                                        svg_height, total_a, total_b, max_count, fid_counter)
        child_x += child_w

    return fid_counter


DIFF_JAVASCRIPT = """
<script type="text/javascript">
<![CDATA[
(function() {
    var frames = document.querySelectorAll('.frame');
    var details = document.getElementById('details');

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
    });

    document.addEventListener('keydown', function(e) {
        if (e.ctrlKey && e.key === 'f') {
            e.preventDefault();
            var term = prompt('Search function name:');
            if (!term) { frames.forEach(function(f){f.style.opacity='1';}); return; }
            term = term.toLowerCase();
            frames.forEach(function(f) {
                var t = f.parentNode.querySelector('title');
                if (t && t.textContent.toLowerCase().indexOf(term) >= 0)
                    f.style.opacity = '1';
                else
                    f.style.opacity = '0.3';
            });
        }
        if (e.key === 'Escape') {
            frames.forEach(function(f){f.style.opacity='1';});
        }
    });
})();
]]>
</script>
"""


def render_diff_svg(out, root, total_a, total_b, title="Differential Flame Graph", width=1200):
    """Render the differential SVG."""
    root.sort_children()
    depth = root.max_depth()
    height = (depth + 2) * FRAME_HEIGHT + 100

    max_count = max(root.count_a, root.count_b, 1)

    out.write('<?xml version="1.0" standalone="no"?>\n')
    out.write(f'<svg xmlns="http://www.w3.org/2000/svg" '
              f'width="{width}" height="{height}" '
              f'viewBox="0 0 {width} {height}">\n')

    out.write(f'<rect width="100%" height="100%" fill="#f8f8f8" />\n')

    # Title
    out.write(f'<text x="{width // 2}" y="20" font-size="16" font-family="sans-serif" '
              f'text-anchor="middle" fill="#333">{xml_escape(title)}</text>\n')

    # Subtitle
    out.write(f'<text x="{width // 2}" y="36" font-size="11" font-family="sans-serif" '
              f'text-anchor="middle" fill="#888">Before: {total_a} samples, '
              f'After: {total_b} samples. Ctrl+F to search.</text>\n')

    # Legend
    legend_y = 52
    out.write(f'<rect x="{width // 2 - 160}" y="{legend_y - 10}" width="16" height="12" '
              f'fill="rgb(100,120,255)" rx="2" />\n')
    out.write(f'<text x="{width // 2 - 140}" y="{legend_y}" font-size="11" '
              f'font-family="sans-serif" fill="#333">Improvement (less CPU)</text>\n')
    out.write(f'<rect x="{width // 2 + 40}" y="{legend_y - 10}" width="16" height="12" '
              f'fill="rgb(255,80,80)" rx="2" />\n')
    out.write(f'<text x="{width // 2 + 60}" y="{legend_y}" font-size="11" '
              f'font-family="sans-serif" fill="#333">Regression (more CPU)</text>\n')

    # Details bar
    out.write(f'<text id="details" x="4" y="{height - 6}" font-size="11" '
              f'font-family="monospace" fill="#333"></text>\n')

    # Render frames
    render_diff_frame(out, root, 0, MARGIN, width - 2 * MARGIN, height,
                      total_a, total_b, max_count, 0)

    out.write(DIFF_JAVASCRIPT)
    out.write('</svg>\n')


# ── Main ────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='Generate a differential flame graph from two folded stack profiles')
    parser.add_argument('files', nargs='*', help='before.folded after.folded')
    parser.add_argument('-a', '--before', help='Before profile (folded stacks)')
    parser.add_argument('-b', '--after', help='After profile (folded stacks)')
    parser.add_argument('-o', '--output', help='Output SVG file (default: stdout)')
    parser.add_argument('-t', '--title', default='Differential Flame Graph',
                        help='Graph title')
    parser.add_argument('-w', '--width', type=int, default=1200, help='SVG width')
    args = parser.parse_args()

    # Resolve input files
    before_file = args.before
    after_file = args.after
    if args.files:
        if len(args.files) >= 2:
            before_file = before_file or args.files[0]
            after_file = after_file or args.files[1]
        elif len(args.files) == 1:
            before_file = before_file or args.files[0]

    if not before_file or not after_file:
        parser.error("Need two input files: before.folded after.folded")

    with open(before_file) as f:
        stacks_a, total_a = parse_folded(f.readlines())
    with open(after_file) as f:
        stacks_b, total_b = parse_folded(f.readlines())

    if total_a == 0:
        print(f"flamediff.py: no samples in {before_file}", file=sys.stderr)
        sys.exit(1)
    if total_b == 0:
        print(f"flamediff.py: no samples in {after_file}", file=sys.stderr)
        sys.exit(1)

    print(f"flamediff.py: before={total_a} samples, after={total_b} samples", file=sys.stderr)

    root = build_diff_tree(stacks_a, total_a, stacks_b, total_b)

    if args.output:
        with open(args.output, 'w') as f:
            render_diff_svg(f, root, total_a, total_b, args.title, args.width)
    else:
        render_diff_svg(sys.stdout, root, total_a, total_b, args.title, args.width)

    print("flamediff.py: done", file=sys.stderr)


if __name__ == '__main__':
    main()
