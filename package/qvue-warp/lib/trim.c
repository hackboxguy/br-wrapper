// Trim ("keystone") model, plan section 6.
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qvuewarp.h"

const char *const qw_handle_names[QW_NHANDLE] = { "tl", "t", "tr", "l", "c", "r", "bl", "b", "br" };

int qw_handle_index(const char *name) {
    for (int i = 0; i < QW_NHANDLE; i++)
        if (!strcmp(name, qw_handle_names[i])) return i;
    return -1;
}

// quadratic Lagrange basis on the nodes 0, 0.5, 1
static void basis(float t, float b[3]) {
    b[0] = 2 * (t - 0.5f) * (t - 1);
    b[1] = -4 * t * (t - 1);
    b[2] = 2 * t * (t - 0.5f);
}

void qw_trim_displacement(const float h[QW_NHANDLE][2], float u, float v, float *dx, float *dy) {
    float bu[3], bv[3], x = 0, y = 0;
    basis(u, bu);
    basis(v, bv);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            float w = bu[c] * bv[r];
            x += w * h[r * 3 + c][0];
            y += w * h[r * 3 + c][1];
        }
    *dx = x;
    *dy = y;
}

int qw_calib_trimmed(const struct qw_calib *in, const float h[QW_NHANDLE][2], struct qw_calib *out) {
    size_t n = (size_t)in->gw * in->gh;
    out->gw = in->gw;
    out->gh = in->gh;
    out->flip_x = in->flip_x;
    out->flip_y = in->flip_y;
    out->pts = malloc(n * 2 * sizeof *out->pts);
    if (!out->pts) return -1;
    for (int j = 0; j < in->gh; j++)
        for (int i = 0; i < in->gw; i++) {
            size_t q = (size_t)j * in->gw + i;
            float dx, dy, u = (float)i / (in->gw - 1), v = (float)j / (in->gh - 1);
            // the 3x3 lattice is in panel space: after calib_flip, grid node
            // (i, j) sits at the mirrored place of the panel, so it takes the
            // trim of that place (tl stays the panel's top-left corner)
            if (in->flip_x) u = 1 - u;
            if (in->flip_y) v = 1 - v;
            qw_trim_displacement(h, u, v, &dx, &dy);
            out->pts[2 * q] = in->pts[2 * q] + dx;
            out->pts[2 * q + 1] = in->pts[2 * q + 1] + dy;
        }
    return 0;
}

void qw_calib_handle_pos(const struct qw_calib *c, int h, float *fi, float *fj) {
    float u = (h % 3) * 0.5f, v = (h / 3) * 0.5f;
    if (c->flip_x) u = 1 - u;
    if (c->flip_y) v = 1 - v;
    *fi = u * (c->gw - 1);
    *fj = v * (c->gh - 1);
}

int qw_trim_is_zero(const struct qw_trim *t) {
    for (int k = 0; k < QW_NPANEL; k++)
        for (int i = 0; i < QW_NHANDLE; i++)
            if (t->h[k][i][0] != 0 || t->h[k][i][1] != 0) return 0;
    return 1;
}

int qw_trim_load(const char *path, struct qw_trim *t, char *err, size_t errlen) {
    memset(t, 0, sizeof *t);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT) return 1;
        snprintf(err, errlen, "%s: %m", path);
        return -1;
    }
    char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    struct qw_trim r = { 0 };
    const char *p = strstr(buf, "\"panels\"");
    for (int k = 0; p && k < QW_NPANEL; k++) {
        p = strstr(p, "\"handles\"");
        if (!p) break;
        p += 9;
        for (int i = 0; i < QW_NHANDLE * 2; i++) {
            while (*p && !strchr("+-0123456789.", *p)) p++;
            char *end;
            double v = strtod(p, &end);
            if (end == p || !isfinite(v) || fabs(v) > 1000) {
                snprintf(err, errlen, "%s: panel %d: bad or missing handle value", path, k + 1);
                return -1;
            }
            r.h[k][i / 2][i % 2] = (float)v;
            p = end;
        }
        if (k == QW_NPANEL - 1) { *t = r; return 0; }
    }
    snprintf(err, errlen, "%s: expected {\"panels\": [{\"handles\": [[dx,dy] x 9]} x 3]}", path);
    return -1;
}

int qw_trim_save(const char *path, const struct qw_trim *t, char *err, size_t errlen) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { snprintf(err, errlen, "%s: %m", tmp); return -1; }
    fprintf(f, "{\n  \"version\": 1,\n  \"handle_order\": [");
    for (int i = 0; i < QW_NHANDLE; i++) fprintf(f, "%s\"%s\"", i ? ", " : "", qw_handle_names[i]);
    fprintf(f, "],\n  \"panels\": [\n");
    for (int k = 0; k < QW_NPANEL; k++) {
        fprintf(f, "    {\"handles\": [");
        for (int i = 0; i < QW_NHANDLE; i++)
            fprintf(f, "%s[%g, %g]", i ? ", " : "", t->h[k][i][0], t->h[k][i][1]);
        fprintf(f, "]}%s\n", k < QW_NPANEL - 1 ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    if (fflush(f) || fsync(fileno(f))) { snprintf(err, errlen, "%s: %m", tmp); fclose(f); unlink(tmp); return -1; }
    fclose(f);
    if (rename(tmp, path)) { snprintf(err, errlen, "rename %s: %m", path); unlink(tmp); return -1; }
    return 0;
}
