// warp.conf: "key = value" lines, '#' comments. See data/warp.conf.
#ifndef QW_CONFIG_H
#define QW_CONFIG_H

#include <limits.h>
#include <stddef.h>

#include "qvuewarp.h"

struct warp_config {
    int enabled;
    int subdiv;
    int rt_priority;        // SCHED_FIFO priority of the render loop, 0 = normal
    int stats_interval;     // seconds between frame statistics log lines, 0 = off
    int pipeline;           // keep one finished frame queued (+1 frame latency, ~2 frames of GPU time)
    int copy_source;        // blit fb0 into tiled textures each frame instead of sampling it raster
    int mirror_x, mirror_y; // canvas mirror: left-right across all panels / top-bottom
    char calib[QW_NPANEL][PATH_MAX];
    int reverse[QW_NPANEL];
    struct qw_panel_map map[QW_NPANEL];
};

void config_defaults(struct warp_config *c, const char *datadir);
// Missing file: returns 1 and leaves the defaults. Error: -1 with err set.
int config_load(struct warp_config *c, const char *path, char *err, size_t errlen);
void config_log(const struct warp_config *c);
// The panel maps to draw with: the canvas mirror applied, then the per-panel
// keys (src, flip_x, flip_y) as written, interpreted on the mirrored canvas.
void qw_config_maps(const struct warp_config *c, struct qw_panel_map out[QW_NPANEL]);

// ---- shared by qvue-warpd and the video player: one loader for one set of files
// The writable state directory: /data/qvue-warp when /data exists (created if
// possible), else $HOME/.config/qvue-warp. -1 if none is writable.
int qw_state_dir(char *out, size_t outlen);
// Defaults, then <datadir>/warp.conf, then the writable layer conf_path
// (NULL: none) on top. File problems are logged, never fatal.
void qw_setup_config(struct warp_config *c, const char *datadir, const char *conf_path);
// The three calibration files named by the config. -1 with err set if one fails.
int qw_setup_calib(const struct warp_config *c, struct qw_calib calib[QW_NPANEL], char *err, size_t errlen);

#endif
