// Calibration loading and evaluation (plan section 4 steps 1 and 3, section 7).
#define _GNU_SOURCE
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "qvuewarp.h"

int qw_verbose;

void qw_log(const char *level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", level);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

double qw_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

// "21x9" anywhere in s (the last match wins); 0 if none
static int parse_grid(const char *s, int *gw, int *gh) {
    int found = 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p) || (p > s && isdigit((unsigned char)p[-1]))) continue;
        int a, b, n;
        if (sscanf(p, "%d%*[xX]%d%n", &a, &b, &n) == 2 && a >= 2 && b >= 2 && a <= 1024 && b <= 1024) {
            *gw = a; *gh = b; found = 1;
        }
    }
    return found;
}

int qw_calib_load(const char *path, int reverse, struct qw_calib *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, errlen, "%s: %m", path); return -1; }
    int gw = 0, gh = 0, have_grid = 0, cap = 256, n = 0, lineno = 0;
    float *pts = malloc(cap * 2 * sizeof *pts);
    char line[512];
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        if (!*s) continue;
        if (*s == '#') {
            char *g = strstr(s, "grid");
            if (g && !have_grid && parse_grid(g, &gw, &gh)) have_grid = 1;
            continue;
        }
        for (char *c = s; *c; c++) if (*c == ',' || *c == ';') *c = ' ';
        float x, y;
        char extra;
        int m = sscanf(s, "%f %f %c", &x, &y, &extra);
        if (m != 2 || !isfinite(x) || !isfinite(y)) {
            snprintf(err, errlen, "%s:%d: expected \"dst_x dst_y\"", path, lineno);
            fclose(f); free(pts); return -1;
        }
        if (n == cap) { cap *= 2; pts = realloc(pts, cap * 2 * sizeof *pts); }
        pts[2 * n] = x; pts[2 * n + 1] = y; n++;
    }
    fclose(f);
    if (!have_grid) {
        const char *base = strrchr(path, '/');
        have_grid = parse_grid(base ? base + 1 : path, &gw, &gh);
    }
    if (!have_grid) {
        snprintf(err, errlen, "%s: grid size unknown (add a \"# grid 21x9\" comment or put 21x9 in the name)", path);
        free(pts); return -1;
    }
    if (n != gw * gh) {
        snprintf(err, errlen, "%s: %d points, grid %dx%d needs %d", path, n, gw, gh, gw * gh);
        free(pts); return -1;
    }
    if (reverse)
        for (int a = 0, b = n - 1; a < b; a++, b--) {
            float t0 = pts[2 * a], t1 = pts[2 * a + 1];
            pts[2 * a] = pts[2 * b]; pts[2 * a + 1] = pts[2 * b + 1];
            pts[2 * b] = t0; pts[2 * b + 1] = t1;
        }
    out->gw = gw; out->gh = gh; out->pts = pts;
    return 0;
}

void qw_calib_free(struct qw_calib *c) {
    free(c->pts);
    c->pts = NULL;
}

static float cr(float p0, float p1, float p2, float p3, float t) {
    return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
                   (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
}

// control point (i, j), linearly extrapolated past the grid edge
static float cp(const struct qw_calib *c, int i, int j, int k) {
    const float *g = c->pts;
    int gw = c->gw, gh = c->gh;
    int ii = i < 0 ? 0 : i >= gw ? gw - 1 : i, jj = j < 0 ? 0 : j >= gh ? gh - 1 : j;
    float v = g[2 * (jj * gw + ii) + k];
    if (i < 0)   v += g[2 * (jj * gw) + k] - g[2 * (jj * gw + 1) + k];
    if (i >= gw) v += g[2 * (jj * gw + gw - 1) + k] - g[2 * (jj * gw + gw - 2) + k];
    if (j < 0)   v += g[2 * ii + k] - g[2 * (gw + ii) + k];
    if (j >= gh) v += g[2 * ((gh - 1) * gw + ii) + k] - g[2 * ((gh - 2) * gw + ii) + k];
    return v;
}

void qw_calib_eval(const struct qw_calib *c, float fi, float fj, float *ox, float *oy) {
    int i = (int)floorf(fi), j = (int)floorf(fj);
    if (i > c->gw - 2) i = c->gw - 2;
    if (j > c->gh - 2) j = c->gh - 2;
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    float tx = fi - i, ty = fj - j, out[2];
    for (int k = 0; k < 2; k++) {
        float col[4];
        for (int m = 0; m < 4; m++)
            col[m] = cr(cp(c, i - 1, j - 1 + m, k), cp(c, i, j - 1 + m, k),
                        cp(c, i + 1, j - 1 + m, k), cp(c, i + 2, j - 1 + m, k), tx);
        out[k] = cr(col[0], col[1], col[2], col[3], ty);
    }
    *ox = out[0]; *oy = out[1];
}
