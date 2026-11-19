// Forward warp mesh (plan section 4).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qvuewarp.h"

int qw_mesh_build(const struct qw_calib *calib, const struct qw_panel_map *map, int subdiv,
                  struct qw_mesh *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    if (map->src_w < 2 || map->src_h < 2) {
        snprintf(err, errlen, "source rectangle %dx%d too small", map->src_w, map->src_h);
        return -1;
    }
    struct qw_src_affine a = { map->src_x, map->src_w - 1, map->src_y, map->src_h - 1, map->flip_x, map->flip_y };
    return qw_mesh_build_affine(calib, &a, subdiv, out, err, errlen);
}

int qw_mesh_build_affine(const struct qw_calib *calib, const struct qw_src_affine *map, int subdiv,
                         struct qw_mesh *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    if (subdiv < 1 || subdiv > 64) {
        snprintf(err, errlen, "subdiv %d out of range 1..64", subdiv);
        return -1;
    }
    int mw = calib ? (calib->gw - 1) * subdiv + 1 : 2;
    int mh = calib ? (calib->gh - 1) * subdiv + 1 : 2;
    if ((long)mw * mh > 4L * 1024 * 1024) {
        snprintf(err, errlen, "mesh %dx%d too large, lower subdiv", mw, mh);
        return -1;
    }
    float *v = malloc((size_t)mw * mh * 4 * sizeof *v);
    if (!v) { snprintf(err, errlen, "out of memory"); return -1; }
    for (int j = 0; j < mh; j++)
        for (int i = 0; i < mw; i++) {
            float u = (float)i / (mw - 1), w = (float)j / (mh - 1), dx, dy, su, sv;
            if (calib) {
                qw_calib_eval(calib, u * (calib->gw - 1), w * (calib->gh - 1), &dx, &dy);
                su = u; sv = w;
            } else {
                // identity: pixel edge to pixel edge, so the border rows and
                // columns are covered and pixel centres map 1:1
                dx = u * QW_PW - 0.5f;
                dy = w * QW_PH - 0.5f;
                su = dx / (QW_PW - 1);
                sv = dy / (QW_PH - 1);
            }
            if (map->flip_x) su = 1 - su;
            if (map->flip_y) sv = 1 - sv;
            float *o = v + 4 * ((size_t)j * mw + i);
            o[0] = 2 * (dx + 0.5f) / QW_PW - 1;   // destination pixel centre -> NDC
            o[1] = 1 - 2 * (dy + 0.5f) / QW_PH;
            o[2] = map->x0 + su * map->dx; // source pixel centre, pixels
            o[3] = map->y0 + sv * map->dy;
        }
    out->mw = mw; out->mh = mh; out->v = v;
    return 0;
}

void qw_panel_map_mirror(const struct qw_panel_map *in, int mirror_x, int mirror_y, struct qw_panel_map *out) {
    // pixel centre x -> FW-1-x: [x, x+w) becomes [FW-x-w, FW-x), read the other way round
    *out = *in;
    if (mirror_x) { out->src_x = QW_FW - in->src_x - in->src_w; out->flip_x = !in->flip_x; }
    if (mirror_y) { out->src_y = QW_PH - in->src_y - in->src_h; out->flip_y = !in->flip_y; }
}

void qw_mesh_free(struct qw_mesh *m) {
    free(m->v);
    m->v = NULL;
}
