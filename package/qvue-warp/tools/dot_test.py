#!/usr/bin/env python3
"""Dot test: warped straight grid vs the pre-warped reference pattern.

    dot_test.py WARPED.ppm REFERENCE.png

WARPED is a qvue-warpd read-back (--dump) while the gallery shows
3xqvue-straight-grid-21x9.png; REFERENCE is
warping-images/qvue-warping-calibration-test-pattern.png. For every white dot
of the reference, the distance to the nearest white dot of the warped image is
measured, per panel. Blobs closer than 8 px are merged first (a border line
through a dot splits it). Needs numpy, scipy, Pillow.
"""
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

PW = 1500


def dots(path):
    img = np.array(Image.open(path).convert("RGB")).astype(int)
    white = img.min(axis=2) > 150
    lab, n = ndimage.label(white)
    idx = range(1, n + 1)
    cen = np.array(ndimage.center_of_mass(white, lab, idx))[:, ::-1]
    size = np.array(ndimage.sum(white, lab, idx))
    used, pts = np.zeros(len(cen), bool), []
    for i in np.argsort(-size):
        if used[i]:
            continue
        near = (~used) & (np.hypot(*(cen - cen[i]).T) < 8)
        used |= near
        pts.append((cen[near] * size[near, None]).sum(0) / size[near].sum())
    return np.array(pts), img.shape[1] // PW


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    got, npanel = dots(sys.argv[1])
    ref, _ = dots(sys.argv[2])
    alld = []
    for k in range(npanel):
        r = ref[(ref[:, 0] >= k * PW) & (ref[:, 0] < (k + 1) * PW)]
        g = got[(got[:, 0] >= k * PW) & (got[:, 0] < (k + 1) * PW)]
        if not len(r) or not len(g):
            print(f"panel {k + 1}: no dots (reference {len(r)}, warped {len(g)})")
            continue
        d = np.hypot(*(r[:, None, :] - g[None, :, :]).transpose(2, 0, 1)).min(1)
        alld.append(d)
        print(f"panel {k + 1}: {len(r)} reference dots, {len(g)} warped dots; distance median {np.median(d):.2f} px, "
              f"p95 {np.percentile(d, 95):.2f} px, max {d.max():.2f} px")
    if alld:
        d = np.concatenate(alld)
        print(f"all panels: median {np.median(d):.2f} px, p95 {np.percentile(d, 95):.2f} px, max {d.max():.2f} px")


if __name__ == "__main__":
    main()
