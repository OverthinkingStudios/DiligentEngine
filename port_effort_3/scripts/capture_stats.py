#!/usr/bin/env python3
"""Pixel-exact analysis for testflight captures (grid.jpg).

RULES (see port_effort_3/vulkan_flat_sampling_handoff.md):
- NO upscaling, resizing, resampling, or interpolation - ever. This script
  contains none and subagents must not add any. If image quality is not
  sufficient to answer the question, REPORT THAT back instead.
- Subagents: USE THIS SCRIPT instead of writing new analysis scripts.

Usage:
  python capture_stats.py IMG --roi x0,y0,x1,y1 [--roi ...]   region medians
  python capture_stats.py IMG --compare IMG2 [--roi ...]      A/B pixel diff
  python capture_stats.py IMG --scan [--tile 40]              flat-tile colour census
"""
import argparse
import statistics
from PIL import Image


def load(path):
    im = Image.open(path)
    im = im.convert("RGB")
    return im


def parse_roi(s):
    x0, y0, x1, y1 = (int(v) for v in s.split(","))
    return x0, y0, x1, y1


def region_medians(im, box):
    px = im.load()
    x0, y0, x1, y1 = box
    rs, gs, bs = [], [], []
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y]
            rs.append(r)
            gs.append(g)
            bs.append(b)
    n = len(rs)
    med = (statistics.median(rs), statistics.median(gs), statistics.median(bs))
    stdev = (round(statistics.pstdev(rs), 1), round(statistics.pstdev(gs), 1),
             round(statistics.pstdev(bs), 1))
    return med, stdev, n


def cmd_regions(im, rois):
    for box in rois:
        med, stdev, n = region_medians(im, box)
        print(f"roi {box}: median RGB {med}  stdev {stdev}  n={n}")


def cmd_compare(im_a, im_b, rois):
    if im_a.size != im_b.size:
        print(f"SIZE MISMATCH: {im_a.size} vs {im_b.size} - aborting")
        return
    pa, pb = im_a.load(), im_b.load()
    w, h = im_a.size
    counts = {1: 0, 10: 0, 30: 0}
    max_d, max_at = 0, (0, 0)
    worst = []
    for y in range(h):
        for x in range(w):
            ra, ga, ba = pa[x, y]
            rb, gb, bb = pb[x, y]
            d = max(abs(ra - rb), abs(ga - gb), abs(ba - bb))
            if d > 0:
                for t in counts:
                    if d > t:
                        counts[t] += 1
                if d > max_d:
                    max_d, max_at = d, (x, y)
                if d > 30:
                    worst.append((d, x, y))
    total = w * h
    print(f"total px {total}")
    for t, c in counts.items():
        print(f"delta>{t}: {c} px ({100.0 * c / total:.4f}%)")
    print(f"max delta {max_d} at {max_at}")
    worst.sort(reverse=True)
    for d, x, y in worst[:15]:
        print(f"  worst: delta {d} at ({x},{y})  A={pa[x, y]} B={pb[x, y]}")
    if rois:
        print("-- per-ROI medians A vs B --")
        for box in rois:
            ma, sa, _ = region_medians(im_a, box)
            mb, sb, n = region_medians(im_b, box)
            print(f"roi {box}: A {ma} (std {sa})  B {mb} (std {sb})  n={n}")


def cmd_scan(im, tile):
    """Census of flat tiles: cluster their median colours (rounded /16)."""
    px = im.load()
    w, h = im.size
    clusters = {}
    for ty in range(0, h - tile, tile):
        for tx in range(0, w - tile, tile):
            med, stdev, _ = region_medians(im, (tx, ty, tx + tile, ty + tile))
            if max(stdev) > 8:
                continue
            key = tuple(int(c) // 16 * 16 for c in med)
            cnt, example = clusters.get(key, (0, (tx, ty)))
            clusters[key] = (cnt + 1, example)
    for key, (cnt, example) in sorted(clusters.items(), key=lambda kv: -kv[1][0]):
        print(f"flat colour ~RGB{key}: {cnt} tiles, e.g. at {example}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--roi", action="append", default=[], type=parse_roi)
    ap.add_argument("--compare")
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--tile", type=int, default=40)
    args = ap.parse_args()

    im = load(args.image)
    print(f"{args.image}: {im.size[0]}x{im.size[1]}")
    if args.compare:
        im_b = load(args.compare)
        print(f"{args.compare}: {im_b.size[0]}x{im_b.size[1]}")
        cmd_compare(im, im_b, args.roi)
    elif args.scan:
        cmd_scan(im, args.tile)
    elif args.roi:
        cmd_regions(im, args.roi)
    else:
        ap.error("give --roi, --compare or --scan")


if __name__ == "__main__":
    main()
