#!/usr/bin/env python3
"""Reference renderer for the qvue-warpd warp (plan section 4) and golden test.

Renders a 4500x298 source image through the same forward mesh as the daemon
(calibration grid, Catmull-Rom subdivision, linear extrapolation past the
edges, destination/source pixel-centre conventions), rasterised on the CPU
with barycentric interpolation and bilinear sampling. Pixels outside the mesh
are black.

With --trim, the trim.json the daemon saved is applied to the calibration
(quadratic Lagrange interpolation of the 3x3 handles per panel).

Golden test: give the daemon read-back of the same source (qvue-warpd --dump
OUT.ppm --dump-src SRC.ppm) and compare:

    warp_ref.py --src SRC.ppm --compare OUT.ppm [--out REF.png] calib1 calib2 calib3

Prints the mean absolute difference and, from the bright dots in the image,
the geometric offset between reference and daemon. Exit status 1 if the mean
difference is >= 2 grey levels or the median dot offset is > 0.5 px.
Needs numpy, Pillow (and scipy for the dot offsets).
"""
import argparse
import re
import sys

import numpy as np
from PIL import Image

PW, PH, NP = 1500, 298, 3
FW = PW * NP


def load_calib(path, reverse=False):
    gw = gh = None
    pts = []
    for line in open(path):
        s = line.strip()
        if not s:
            continue
        if s.startswith("#"):
            m = re.search(r"grid\D*?(\d+)[xX](\d+)", s)
            if m and gw is None:
                gw, gh = int(m.group(1)), int(m.group(2))
            continue
        x, y = s.replace(",", " ").replace(";", " ").split()[:2]
        pts.append((float(x), float(y)))
    if gw is None:
        m = re.findall(r"(\d+)[xX](\d+)", path.split("/")[-1])
        gw, gh = int(m[-1][0]), int(m[-1][1])
    pts = np.array(pts, np.float64)
    assert len(pts) == gw * gh, f"{path}: {len(pts)} points, grid {gw}x{gh}"
    if reverse:
        pts = pts[::-1]
    return pts.reshape(gh, gw, 2)


def lagrange(t):
    """quadratic Lagrange basis on the nodes 0, 0.5, 1"""
    return np.stack([2 * (t - 0.5) * (t - 1), -4 * t * (t - 1), 2 * t * (t - 0.5)], -1)


def apply_trim(g, handles):
    """add the trim displacement (plan section 6) to every grid node.
    handles: 9 x (dx, dy) in the order tl t tr l c r bl b br"""
    gh, gw, _ = g.shape
    h = np.asarray(handles, np.float64).reshape(3, 3, 2)          # [row][col]
    bu = lagrange(np.arange(gw) / (gw - 1))                        # (gw, 3)
    bv = lagrange(np.arange(gh) / (gh - 1))                        # (gh, 3)
    d = np.einsum("jr,ic,rcx->jix", bv, bu, h)
    return g + d


def load_trim(path):
    import json
    t = json.load(open(path))
    return [p["handles"] for p in t["panels"]]


def padded(g):
    """grid with one extra ring of control points, linearly extrapolated"""
    gh, gw, _ = g.shape
    p = np.zeros((gh + 2, gw + 2, 2))
    p[1:-1, 1:-1] = g
    p[1:-1, 0] = 2 * g[:, 0] - g[:, 1]
    p[1:-1, -1] = 2 * g[:, -1] - g[:, -2]
    p[0, 1:-1] = 2 * g[0] - g[1]
    p[-1, 1:-1] = 2 * g[-1] - g[-2]
    # corners as in calib.c: the x and y extrapolations are added
    for (r, c, gr, gc, rr, cc) in ((0, 0, 0, 0, 1, 1), (0, -1, 0, -1, 1, -2), (-1, 0, -1, 0, -2, 1),
                                    (-1, -1, -1, -1, -2, -2)):
        p[r, c] = 3 * g[gr, gc] - g[gr, cc] - g[rr, gc]
    return p


def cr(p0, p1, p2, p3, t):
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                  + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t)


def eval_grid(g, fi, fj):
    gh, gw, _ = g.shape
    p = padded(g)
    i = np.clip(np.floor(fi).astype(int), 0, gw - 2)
    j = np.clip(np.floor(fj).astype(int), 0, gh - 2)
    tx, ty = (fi - i)[..., None], (fj - j)[..., None]
    col = [cr(p[j + m, i], p[j + m, i + 1], p[j + m, i + 2], p[j + m, i + 3], tx) for m in range(4)]
    return cr(col[0], col[1], col[2], col[3], ty)


def build_mesh(g, src, subdiv, flip_x=False, flip_y=False):
    """vertices (mh, mw, 4): dst_x, dst_y (panel pixels), sx, sy (source pixels)"""
    gh, gw, _ = g.shape
    mw, mh = (gw - 1) * subdiv + 1, (gh - 1) * subdiv + 1
    u = np.arange(mw) / (mw - 1)
    v = np.arange(mh) / (mh - 1)
    U, V = np.meshgrid(u, v)
    d = eval_grid(g, U * (gw - 1), V * (gh - 1))
    su = 1 - U if flip_x else U
    sv = 1 - V if flip_y else V
    sx0, sy0, sw, sh = src
    return np.dstack([d[..., 0], d[..., 1], sx0 + su * (sw - 1), sy0 + sv * (sh - 1)])


def bilinear(img, sx, sy):
    h, w, _ = img.shape
    x0 = np.floor(sx).astype(int)
    y0 = np.floor(sy).astype(int)
    fx, fy = (sx - x0)[:, None], (sy - y0)[:, None]
    x0c, x1c = np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1)
    y0c, y1c = np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
    a = img[y0c, x0c] * (1 - fx) + img[y0c, x1c] * fx
    b = img[y1c, x0c] * (1 - fx) + img[y1c, x1c] * fx
    return a * (1 - fy) + b * fy


def render_panel(img, mesh):
    """rasterise the mesh: pixel (x, y) is covered at dst coordinate (x, y)"""
    out = np.zeros((PH, PW, 3))
    mh, mw, _ = mesh.shape
    a = mesh[:-1, :-1].reshape(-1, 4)
    b = mesh[:-1, 1:].reshape(-1, 4)
    c = mesh[1:, :-1].reshape(-1, 4)
    d = mesh[1:, 1:].reshape(-1, 4)
    tris = np.concatenate([np.stack([a, b, d], 1), np.stack([a, d, c], 1)])  # (T, 3, 4)
    xy = tris[:, :, :2]
    x0 = np.ceil(xy[:, :, 0].min(1) - 1e-9).astype(int)
    y0 = np.ceil(xy[:, :, 1].min(1) - 1e-9).astype(int)
    bw = int((np.floor(xy[:, :, 0].max(1)) - x0).max()) + 1
    bh = int((np.floor(xy[:, :, 1].max(1)) - y0).max()) + 1
    oy, ox = np.mgrid[0:bh, 0:bw]
    px = (x0[:, None, None] + ox).reshape(len(tris), -1).astype(float)
    py = (y0[:, None, None] + oy).reshape(len(tris), -1).astype(float)
    (ax, ay), (bx, by), (cx, cy) = [(xy[:, k, 0:1], xy[:, k, 1:2]) for k in range(3)]
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    den = np.where(np.abs(den) < 1e-12, 1e-12, den)
    l0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
    l1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
    l2 = 1 - l0 - l1
    eps = -1e-7
    inside = (l0 >= eps) & (l1 >= eps) & (l2 >= eps) & (px >= 0) & (px < PW) & (py >= 0) & (py < PH)
    t = np.nonzero(inside)
    L0, L1, L2 = l0[t], l1[t], l2[t]
    S = tris[t[0]]
    sx = L0 * S[:, 0, 2] + L1 * S[:, 1, 2] + L2 * S[:, 2, 2]
    sy = L0 * S[:, 0, 3] + L1 * S[:, 1, 3] + L2 * S[:, 2, 3]
    out[py[t].astype(int), px[t].astype(int)] = bilinear(img, sx, sy)
    return out


def render(img, calibs, subdiv=8, reverse=False, srcs=None, flip_x=False, flip_y=False, trim=None):
    img = img.astype(np.float64)
    out = np.zeros((PH, FW, 3))
    for k in range(NP):
        g = load_calib(calibs[min(k, len(calibs) - 1)], reverse)
        if trim is not None:
            g = apply_trim(g, trim[k])
        src = srcs[k] if srcs else (k * PW, 0, PW, PH)
        out[:, k * PW:(k + 1) * PW] = render_panel(img, build_mesh(g, src, subdiv, flip_x, flip_y))
    return out


def dots(img, thresh=150):
    from scipy import ndimage
    white = img.min(axis=2) > thresh
    lab, n = ndimage.label(white)
    if n == 0:
        return np.zeros((0, 2))
    idx = range(1, n + 1)
    cen = np.array(ndimage.center_of_mass(white, lab, idx))[:, ::-1]
    size = np.array(ndimage.sum(white, lab, idx))
    return cen[size >= 4]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("calib", nargs="+")
    ap.add_argument("--src", required=True, help="source image (fb0 dump or PNG), 4500x298")
    ap.add_argument("--compare", help="daemon read-back to compare with")
    ap.add_argument("--out", help="write the reference rendering here")
    ap.add_argument("--subdiv", type=int, default=8)
    ap.add_argument("--reverse", action="store_true")
    ap.add_argument("--trim", help="trim.json written by qvue-warpd (save)")
    a = ap.parse_args()

    src = np.array(Image.open(a.src).convert("RGB"))
    assert src.shape == (PH, FW, 3), src.shape
    ref = render(src, a.calib, a.subdiv, a.reverse, trim=load_trim(a.trim) if a.trim else None)
    ref8 = np.clip(np.rint(ref), 0, 255).astype(np.uint8)
    if a.out:
        Image.fromarray(ref8).save(a.out)
        print("wrote", a.out)
    if not a.compare:
        return 0
    got = np.array(Image.open(a.compare).convert("RGB")).astype(np.float64)
    diff = np.abs(got - ref)
    mad = diff.mean()
    print(f"golden: mean abs diff {mad:.3f} grey levels, max {diff.max():.0f}, "
          f"samples > 8: {(diff > 8).mean() * 100:.3f}%")
    for k in range(NP):
        dk = diff[:, k * PW:(k + 1) * PW]
        print(f"  panel {k + 1}: mean {dk.mean():.3f}, max {dk.max():.0f}")
    ok = mad < 2
    try:
        dr, dg = dots(ref8), dots(got.astype(np.uint8))
    except ImportError:
        print("scipy missing: no geometric check")
        return 0 if ok else 1
    if len(dr) and len(dg):
        dist = np.hypot(*(dr[:, None, :] - dg[None, :, :]).transpose(2, 0, 1))
        nn = dist.argmin(1)
        off = dg[nn] - dr
        dd = dist.min(1)
        good = dd < 5
        print(f"geometry: {good.sum()} of {len(dr)} reference dots matched, offset median "
              f"{np.median(dd[good]):.3f} px, p95 {np.percentile(dd[good], 95):.3f} px, max {dd[good].max():.3f} px, "
              f"mean shift dx {off[good, 0].mean():+.3f} dy {off[good, 1].mean():+.3f}")
        ok = ok and np.median(dd[good]) <= 0.5
    else:
        print("geometry: no bright dots in the image, skipped")
    print("golden test", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
