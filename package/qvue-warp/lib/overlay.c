// Tuning overlay geometry: grid lines of the trimmed calibration and handle markers.
#include <stdlib.h>
#include <string.h>

#include "qvuewarp.h"

struct vbuf { float *v; int n, cap; };

static int push(struct vbuf *b, float x, float y, const float c[3]) {
    if (b->n == b->cap) {
        int cap = b->cap ? 2 * b->cap : 1024;
        float *v = realloc(b->v, (size_t)cap * 5 * sizeof *v);
        if (!v) return -1;
        b->v = v;
        b->cap = cap;
    }
    float *o = b->v + 5 * (size_t)b->n++;
    o[0] = x; o[1] = y; o[2] = c[0]; o[3] = c[1]; o[4] = c[2];
    return 0;
}

// destination pixel centre -> NDC
static float nx(float x) { return 2 * (x + 0.5f) / QW_PW - 1; }
static float ny(float y) { return 1 - 2 * (y + 0.5f) / QW_PH; }

// square of half-size hs pixels around a destination pixel centre (covers whole pixels)
static int square(struct vbuf *b, float cx, float cy, float hs, const float c[3]) {
    float x0 = nx(cx - hs - 0.5f), x1 = nx(cx + hs + 0.5f), y0 = ny(cy - hs - 0.5f), y1 = ny(cy + hs + 0.5f);
    return push(b, x0, y0, c) | push(b, x1, y0, c) | push(b, x1, y1, c) |
           push(b, x0, y0, c) | push(b, x1, y1, c) | push(b, x0, y1, c);
}

int qw_overlay_build(const struct qw_calib *g, int subdiv, int sel, float **out, int *nline, int *ntri) {
    static const float yellow[3] = { 1, 1, 0 }, magenta[3] = { 1, 0, 1 }, cyan[3] = { 0, 1, 1 },
                       black[3] = { 0, 0, 0 };
    struct vbuf lines = { 0 }, tris = { 0 };
    int bad = 0;
    if (subdiv < 1) subdiv = 1;
    // grid rows and columns as polylines through the Catmull-Rom surface
    int nu = (g->gw - 1) * subdiv, nv = (g->gh - 1) * subdiv;
    for (int j = 0; j < g->gh; j++)
        for (int s = 0; s < nu; s++) {
            float x0, y0, x1, y1;
            qw_calib_eval(g, (float)s / subdiv, j, &x0, &y0);
            qw_calib_eval(g, (float)(s + 1) / subdiv, j, &x1, &y1);
            bad |= push(&lines, nx(x0), ny(y0), yellow) | push(&lines, nx(x1), ny(y1), yellow);
        }
    for (int i = 0; i < g->gw; i++)
        for (int s = 0; s < nv; s++) {
            float x0, y0, x1, y1;
            qw_calib_eval(g, i, (float)s / subdiv, &x0, &y0);
            qw_calib_eval(g, i, (float)(s + 1) / subdiv, &x1, &y1);
            bad |= push(&lines, nx(x0), ny(y0), yellow) | push(&lines, nx(x1), ny(y1), yellow);
        }
    // handle markers; the selected ones on top
    if (sel >= 0)
        for (int pass = 0; pass < 2; pass++)
            for (int h = 0; h < QW_NHANDLE; h++) {
                int is_sel = sel == QW_NHANDLE || sel == h;
                if (is_sel != pass) continue;
                float x, y;
                qw_calib_eval(g, (h % 3) * 0.5f * (g->gw - 1), (h / 3) * 0.5f * (g->gh - 1), &x, &y);
                if (is_sel) {
                    bad |= square(&tris, x, y, 7, magenta);
                    bad |= square(&tris, x, y, 3, black);
                    bad |= square(&tris, x, y, 0, magenta);
                } else {
                    bad |= square(&tris, x, y, 3, cyan);
                }
            }
    if (bad) { free(lines.v); free(tris.v); return -1; }
    float *v = malloc(((size_t)lines.n + tris.n) * 5 * sizeof *v + 1);
    if (!v) { free(lines.v); free(tris.v); return -1; }
    if (lines.n) memcpy(v, lines.v, (size_t)lines.n * 5 * sizeof *v);
    if (tris.n) memcpy(v + 5 * (size_t)lines.n, tris.v, (size_t)tris.n * 5 * sizeof *v);
    free(lines.v);
    free(tris.v);
    *out = v;
    *nline = lines.n;
    *ntri = tris.n;
    return 0;
}
