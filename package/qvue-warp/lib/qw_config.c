#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <sys/stat.h>
#include <unistd.h>

#include "qw_config.h"

void config_defaults(struct warp_config *c, const char *datadir) {
    memset(c, 0, sizeof *c);
    c->enabled = 1;
    c->subdiv = 8;
    c->rt_priority = 10;
    c->stats_interval = 60;
    c->pipeline = 0;
    c->copy_source = 1;
    for (int k = 0; k < QW_NPANEL; k++) {
        // factory calibration: the vendor's grid files, verbatim (they list the
        // points bottom-right first, hence reverse)
        static const char *const vendor[QW_NPANEL] = { "3xqvue-vendor-panel1.txt",
                                                       "3xqvue-vendor-panel2.txt",
                                                       "3xqvue-vendor-panel3.txt" };
        snprintf(c->calib[k], sizeof c->calib[k], "%s/%s", datadir, vendor[k]);
        c->reverse[k] = 1;
        c->map[k] = (struct qw_panel_map){ 0, 0, k * QW_PW, 0, QW_PW, QW_PH };
    }
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int parse_bool(const char *v, int *out) {
    if (!strcasecmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on")) *out = 1;
    else if (!strcasecmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off")) *out = 0;
    else return -1;
    return 0;
}

static int parse_int(const char *v, int lo, int hi, int *out) {
    char *end;
    errno = 0;
    long n = strtol(v, &end, 10);
    if (errno || end == v || *trim(end) || n < lo || n > hi) return -1;
    *out = (int)n;
    return 0;
}

int config_load(struct warp_config *c, const char *path, char *err, size_t errlen) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT) return 1;
        snprintf(err, errlen, "%s: %m", path);
        return -1;
    }
    struct warp_config n = *c;
    char line[PATH_MAX + 64];
    int lineno = 0, rc = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) { snprintf(err, errlen, "%s:%d: expected key = value", path, lineno); rc = -1; break; }
        *eq = 0;
        char *key = trim(s), *val = trim(eq + 1);
        int bad = 0, k = -1;
        const char *sub = key;
        if (!strncmp(key, "panel", 5) && key[5] >= '1' && key[5] < '1' + QW_NPANEL && key[6] == '.') {
            k = key[5] - '1';
            sub = key + 7;
        }
        if (k < 0 && !strcmp(key, "enabled")) bad = parse_bool(val, &n.enabled);
        else if (k < 0 && !strcmp(key, "subdiv")) bad = parse_int(val, 1, 64, &n.subdiv);
        else if (k < 0 && !strcmp(key, "rt_priority")) bad = parse_int(val, 0, 99, &n.rt_priority);
        else if (k < 0 && !strcmp(key, "pipeline")) bad = parse_bool(val, &n.pipeline);
        else if (k < 0 && !strcmp(key, "copy_source")) bad = parse_bool(val, &n.copy_source);
        else if (k < 0 && !strcmp(key, "mirror_x")) bad = parse_bool(val, &n.mirror_x);
        else if (k < 0 && !strcmp(key, "mirror_y")) bad = parse_bool(val, &n.mirror_y);
        else if (k < 0 && !strcmp(key, "stats_interval")) bad = parse_int(val, 0, 86400, &n.stats_interval);
        else if (k >= 0 && !strcmp(sub, "flip_x")) bad = parse_bool(val, &n.map[k].flip_x);
        else if (k >= 0 && !strcmp(sub, "flip_y")) bad = parse_bool(val, &n.map[k].flip_y);
        else if (k >= 0 && !strcmp(sub, "reverse")) bad = parse_bool(val, &n.reverse[k]);
        else if (k >= 0 && !strcmp(sub, "calib_flip_x")) bad = parse_bool(val, &n.calib_flip_x[k]);
        else if (k >= 0 && !strcmp(sub, "calib_flip_y")) bad = parse_bool(val, &n.calib_flip_y[k]);
        else if (k >= 0 && !strcmp(sub, "grid")) {
            char extra;
            int w, h;
            if (sscanf(val, "%d%*[xX]%d %c", &w, &h, &extra) != 2 || w < 2 || h < 2 || w > 1024 || h > 1024) bad = 1;
            else { n.grid_w[k] = w; n.grid_h[k] = h; }
        }
        else if (k >= 0 && !strcmp(sub, "calib")) {
            if (!*val || strlen(val) >= sizeof n.calib[k]) bad = 1;
            else snprintf(n.calib[k], sizeof n.calib[k], "%s", val);
        } else if (k >= 0 && !strcmp(sub, "src")) {
            struct qw_panel_map *m = &n.map[k];
            char extra;
            if (sscanf(val, "%d %d %d %d %c", &m->src_x, &m->src_y, &m->src_w, &m->src_h, &extra) != 4 ||
                m->src_x < 0 || m->src_y < 0 || m->src_w < 2 || m->src_h < 2 ||
                m->src_x + m->src_w > QW_FW || m->src_y + m->src_h > QW_PH)
                bad = 1;
        } else {
            snprintf(err, errlen, "%s:%d: unknown key \"%s\"", path, lineno, key);
            rc = -1;
            break;
        }
        if (bad) {
            snprintf(err, errlen, "%s:%d: bad value \"%s\" for %s", path, lineno, val, key);
            rc = -1;
            break;
        }
    }
    fclose(f);
    if (rc == 0) *c = n;
    return rc;
}

void config_log(const struct warp_config *c) {
    QW_INFO("config: enabled=%d mirror_x=%d mirror_y=%d subdiv=%d rt_priority=%d stats_interval=%d pipeline=%d "
            "copy_source=%d", c->enabled, c->mirror_x, c->mirror_y, c->subdiv, c->rt_priority, c->stats_interval,
            c->pipeline, c->copy_source);
    for (int k = 0; k < QW_NPANEL; k++) {
        const struct qw_panel_map *m = &c->map[k];
        QW_INFO("config: panel%d calib=%s reverse=%d calib_flip=%d,%d flip_x=%d flip_y=%d src=%d %d %d %d", k + 1,
                c->calib[k], c->reverse[k], c->calib_flip_x[k], c->calib_flip_y[k], m->flip_x, m->flip_y, m->src_x,
                m->src_y, m->src_w, m->src_h);
    }
}

void qw_config_maps(const struct warp_config *c, struct qw_panel_map out[QW_NPANEL]) {
    for (int k = 0; k < QW_NPANEL; k++) qw_panel_map_mirror(&c->map[k], c->mirror_x, c->mirror_y, &out[k]);
}

static int usable_dir(const char *dir) {
    struct stat st;
    return !stat(dir, &st) && S_ISDIR(st.st_mode) && !access(dir, W_OK);
}

int qw_state_dir(char *out, size_t outlen) {
    if (usable_dir("/data/qvue-warp") || (usable_dir("/data") && !mkdir("/data/qvue-warp", 0755))) {
        snprintf(out, outlen, "/data/qvue-warp");
        return 0;
    }
    const char *home = getenv("HOME");
    if (!home || !*home) home = "/home/pi";
    char cfg[PATH_MAX];
    snprintf(cfg, sizeof cfg, "%s/.config", home);
    mkdir(cfg, 0755);
    snprintf(out, outlen, "%s/qvue-warp", cfg);
    mkdir(out, 0755);
    return usable_dir(out) ? 0 : -1;
}

void qw_setup_config(struct warp_config *c, const char *datadir, const char *conf_path) {
    char err[512], def[PATH_MAX];
    config_defaults(c, datadir);
    snprintf(def, sizeof def, "%s/warp.conf", datadir);
    int lr = config_load(c, def, err, sizeof err);
    if (lr < 0) QW_ERR("%s", err);
    else if (lr == 1) QW_WARN("%s not found; built-in defaults", def);
    if (!conf_path) return;
    lr = config_load(c, conf_path, err, sizeof err);
    if (lr < 0) QW_ERR("%s (ignored)", err);
    else if (lr == 0) QW_INFO("config %s", conf_path);
}

int qw_setup_calib(const struct warp_config *c, struct qw_calib calib[QW_NPANEL], char *err, size_t errlen) {
    int rc = 0;
    for (int k = 0; k < QW_NPANEL; k++) {
        qw_calib_free(&calib[k]);
        char e[512];
        struct qw_calib_opts o = { c->reverse[k], c->calib_flip_x[k], c->calib_flip_y[k], c->grid_w[k], c->grid_h[k] };
        if (qw_calib_load_opts(c->calib[k], &o, &calib[k], e, sizeof e)) {
            if (!rc) snprintf(err, errlen, "panel %d calibration: %s", k + 1, e);
            rc = -1;
        }
    }
    return rc;
}
