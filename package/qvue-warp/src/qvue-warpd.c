// qvue-warpd: realtime warp of the 3x QVue HDMI output (fb0 -> three overlay planes).
//
// States:
//   OFF        no DRM resources held; fb0 goes 1:1 to HDMI
//   ON         DRM master, three overlay planes cover fb0 with the warped picture
//   SUSPENDED  like OFF, while a DRM-master app (video player, Kodi) runs;
//              "resume" returns to the warp setting
// The setting (enabled) comes from warp.conf and is changed by on/off/toggle,
// which persist it. One poll() loop serves the signalfd, the control socket
// (/run/qvue-warp/ctl.sock, one text line per request and reply) and the DRM
// fd; frames are paced by the page-flip events. If the warp path cannot start,
// the daemon logs it and stays OFF, so the screen never goes black.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <xf86drmMode.h>

#include "qw_config.h"
#include "fb0src.h"
#include "gridsrc.h"
#include "persist.h"
#include "qvuewarp.h"

#ifndef QW_DATADIR
#define QW_DATADIR "/home/pi/micropanel/share/qvue-warp"
#endif

// 4500x298@60: htotal 5628, vtotal 362, 122.24 MHz -> 16.667 ms
#define FRAME_MS 16.667
#define LATE_MS 20.0
#define MAX_CLIENTS 16
#define RESUME_RETRY_MS 10000.0    // Kodi held the card 2.2-4.2 s after its exit in tests
#define FLIP_TIMEOUT_MS 1000.0     // a flip pending longer than this: the display is stuck
static const double RETRY_DELAYS_MS[] = { 2000, 10000, 60000 }; // automatic restarts after failures
#define NRETRY (int)(sizeof RETRY_DELAYS_MS / sizeof RETRY_DELAYS_MS[0])
#define SEL_PANEL QW_NHANDLE       // selection "panel": all nine handles of the selected panel
#define SEL_ALL (QW_NHANDLE + 1)   // selection "all": every handle of every panel

enum state { ST_OFF, ST_ON, ST_SUSPENDED };
static const char *const state_names[] = { "off", "on", "suspended" };

struct opts {
    const char *config, *state_dir, *run_dir;
    int force;              // -1 config, 0 off, 1 on (not persisted)
    long frames;            // stop after N frames (test mode), -1 = run
    int identity, selfcheck, hold, finish, pipeline;
    const char *only_panel; // debug: digits of the panels to draw
    int source_grid;        // start with the built-in test grid as the source
    int copy_src;           // -1 config, else override copy_source
    long lose_flip;         // debug: stop reading DRM events after this frame (-1 = off)
    long dump_at;
    const char *dump, *dump_src;
    int rt, subdiv, stats_interval;
};

struct stats {
    long frames, late, late_submit, late_flip, missed;
    double render_ms;       // draw + swap (+ glFinish with --finish), summed
    int have_prev;
    double worst, t0;
    unsigned prev_seq;
    double prev_vblank;
};

struct client { int fd; char buf[512]; size_t len; };

static struct daemon {
    struct opts o;
    struct warp_config cfg;
    struct qw_calib calib[QW_NPANEL];
    int calib_ok;
    char state_dir[PATH_MAX - 64], run_dir[PATH_MAX - 64]; // room for the file names
    char conf_path[PATH_MAX], trim_path[PATH_MAX], sock_path[PATH_MAX], state_path[PATH_MAX];

    int enabled;            // the warp setting
    enum state state;
    int start_failed;       // latched until a command or reload asks again
    double resume_until;    // > 0: retrying a resume until then
    double retry_at;        // > 0: automatic restart after a failure at this time
    int retry_n;            // automatic restarts used (RETRY_DELAYS)
    double on_since;        // when the warp last came ON
    char error[256];

    // warp path (ON)
    struct qw_output out;
    struct fb0src src;
    struct qw_renderer ren;
    GLuint grid[QW_NPANEL];
    int out_open, src_open, ren_open;
    struct gbm_bo *ready;   // pipeline: finished, not yet committed
    int geom_dirty;

    // tuning
    struct qw_trim trim, trim_saved;
    int tune, source_grid, sel_panel, sel, step;

    // statistics
    struct stats total, per;
    double fps, fps_t0;
    long fps_n;

    // test hooks
    long frame_no;
    char dump_path[PATH_MAX], dump_src_path[PATH_MAX];
    int dump_pending, dump_client, check_rc;

    int sfd, lfd;
    struct client cl[MAX_CLIENTS];
    int stop;
} D;

static void set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(D.error, sizeof D.error, fmt, ap);
    va_end(ap);
    if (D.error[0]) QW_ERR("%s", D.error);
}

// ------------------------------------------------------------------ statistics
static void stats_reset(struct stats *s) {
    memset(s, 0, sizeof *s);
    s->t0 = qw_now_ms();
}

static void stats_log(const char *what, const struct stats *s) {
    double secs = (qw_now_ms() - s->t0) / 1e3;
    if (secs <= 0) secs = 1e-3;
    QW_INFO("stats %s: %ld frames in %.1f s = %.2f fps; late (>%.0f ms) %ld (%.3f%%: submit %ld, flip %ld), "
            "missed vblanks %ld, worst %.1f ms, render %.2f ms/frame",
            what, s->frames, secs, s->frames / secs, LATE_MS, s->late,
            s->frames ? 100.0 * s->late / s->frames : 0.0, s->late_submit, s->late_flip, s->missed, s->worst,
            s->frames ? s->render_ms / s->frames : 0.0);
}

static void stats_flip(const struct qw_flip_info *fi, int count_interval) {
    struct stats *ss[2] = { &D.per, &D.total };
    for (int i = 0; i < 2; i++) {
        struct stats *s = ss[i];
        if (s->have_prev && count_interval) {
            double dt = fi->vblank_ms - s->prev_vblank;
            long miss = (long)(fi->seq - s->prev_seq) - 1;
            if (miss > 0) s->missed += miss;
            if (dt > s->worst) s->worst = dt;
            if (dt > LATE_MS) {
                // was the commit issued after the vblank it should have made?
                int late_submit = fi->commit_ms > s->prev_vblank + FRAME_MS - 1.0;
                s->late++;
                if (late_submit) s->late_submit++; else s->late_flip++;
                if (i == 1)
                    QW_DBG("late frame %ld at %.3f s: interval %.2f ms, seq +%u, commit at vblank%+.2f ms, "
                           "event read at vblank%+.2f ms -> %s",
                           s->frames, fi->vblank_ms / 1e3, dt, fi->seq - s->prev_seq,
                           fi->commit_ms - s->prev_vblank, qw_now_ms() - fi->vblank_ms,
                           late_submit ? "late submit" : "late flip");
            }
        }
        s->frames++;
        s->prev_seq = fi->seq;
        s->prev_vblank = fi->vblank_ms;
        s->have_prev = 1;
    }
    D.fps_n++;
}

// ------------------------------------------------------------------ state.json
static const char *sel_name(void) {
    return D.sel == SEL_PANEL ? "panel" : D.sel == SEL_ALL ? "all" : qw_handle_names[D.sel];
}

static int trim_unsaved(void) { return memcmp(&D.trim, &D.trim_saved, sizeof D.trim) != 0; }

static void write_state(void) {
    char buf[1024], err[256], esc[sizeof D.error * 2];
    size_t j = 0;
    for (const char *p = D.error; *p && j < sizeof esc - 2; p++) {
        if (*p == '"' || *p == '\\') esc[j++] = '\\';
        esc[j++] = (*p == '\n') ? ' ' : *p;
    }
    esc[j] = 0;
    int n = snprintf(buf, sizeof buf,
                     "{\"state\": \"%s\", \"enabled\": %s, \"warping\": %s, \"resuming\": %s, \"mirror_x\": %s, "
                     "\"mirror_y\": %s, \"tune\": %s, "
                     "\"source\": \"%s\", \"panel\": %d, \"handle\": \"%s\", \"step\": %d, \"trim_unsaved\": %s, "
                     "\"fps\": %.2f, \"frames\": %ld, \"late\": %ld, \"error\": \"%s\"}\n",
                     state_names[D.state], D.enabled ? "true" : "false", D.state == ST_ON ? "true" : "false",
                     D.resume_until > 0 ? "true" : "false", D.cfg.mirror_x ? "true" : "false",
                     D.cfg.mirror_y ? "true" : "false", D.tune ? "true" : "false",
                     D.source_grid ? "grid" : "fb0", D.sel_panel + 1, sel_name(), D.step,
                     trim_unsaved() ? "true" : "false", D.state == ST_ON ? D.fps : 0.0, D.total.frames,
                     D.total.late, esc);
    if (write_atomic(D.state_path, buf, (size_t)n, err, sizeof err)) QW_WARN("state.json: %s", err);
}

// ------------------------------------------------------------------ config
static void load_config(void) {
    char err[512];
    // the defaults, then the writable layer on top: --config, or <state dir>/warp.conf
    qw_setup_config(&D.cfg, QW_DATADIR, D.conf_path);
    if (D.o.subdiv > 0) D.cfg.subdiv = D.o.subdiv;
    if (D.o.pipeline >= 0) D.cfg.pipeline = D.o.pipeline;
    if (D.o.copy_src >= 0) D.cfg.copy_source = D.o.copy_src;
    config_log(&D.cfg);

    for (int k = 0; k < QW_NPANEL; k++) qw_calib_free(&D.calib[k]);
    D.calib_ok = 1;
    if (!D.o.identity && qw_setup_calib(&D.cfg, D.calib, err, sizeof err)) {
        set_error("%s", err);
        D.calib_ok = 0;
    }
}

// ------------------------------------------------------------------ warp path
static void set_rt(int prio) {
    struct sched_param sp = { .sched_priority = prio };
    if (sched_setscheduler(0, prio > 0 ? SCHED_FIFO : SCHED_OTHER, &sp))
        QW_WARN("SCHED_%s %d: %m", prio > 0 ? "FIFO" : "OTHER", prio);
    else if (prio > 0)
        QW_INFO("render loop runs SCHED_FIFO %d", prio);
}

// meshes (calibration + trim) and the tuning overlay
static int build_geometry(void) {
    char err[256];
    struct qw_panel_map maps[QW_NPANEL];
    qw_config_maps(&D.cfg, maps); // mirror_x / mirror_y applied
    for (int k = 0; k < QW_NPANEL; k++) {
        const struct qw_panel_map *m = &maps[k];
        struct qw_calib tc = { 0 };
        if (!D.o.identity && qw_calib_trimmed(&D.calib[k], (const float(*)[2])D.trim.h[k], &tc)) {
            set_error("out of memory");
            return -1;
        }
        struct qw_mesh mesh;
        if (qw_mesh_build(D.o.identity ? NULL : &tc, m, D.cfg.subdiv, &mesh, err, sizeof err)) {
            set_error("panel %d mesh: %s", k + 1, err);
            qw_calib_free(&tc);
            return -1;
        }
        int rc;
        if (D.o.only_panel && !strchr(D.o.only_panel, '1' + k)) {
            rc = 0;
            D.ren.nidx[k] = 0;
        } else if (D.source_grid) {
            // the test grid stands in for fb0: each panel samples the grid
            // texture of the canvas panel its (mirrored) source rectangle is in
            int g = (m->src_x + m->src_w / 2) / QW_PW;
            if (g >= QW_NPANEL) g = QW_NPANEL - 1;
            rc = qw_renderer_set_panel(&D.ren, k, &mesh, D.grid[g], g * QW_PW, QW_PW, QW_PH);
        } else {
            int t = fb0src_texture_for(&D.src, m->src_x, m->src_w);
            if (t < 0) {
                set_error("panel %d source x %d..%d is not inside one source texture", k + 1, m->src_x,
                          m->src_x + m->src_w - 1);
                qw_mesh_free(&mesh);
                qw_calib_free(&tc);
                return -1;
            }
            rc = qw_renderer_set_panel(&D.ren, k, &mesh, fb0src_tex(&D.src, t), D.src.tex_x[t], D.src.tex_w[t], QW_PH);
        }
        qw_mesh_free(&mesh);
        if (rc) { set_error("panel %d: mesh upload failed", k + 1); qw_calib_free(&tc); return -1; }
        int show = D.tune && !D.o.identity && (D.sel == SEL_ALL || k == D.sel_panel);
        if (show) {
            float *v;
            int nl, nt;
            int sel = (D.sel == SEL_PANEL || D.sel == SEL_ALL) ? QW_NHANDLE : D.sel;
            if (qw_overlay_build(&tc, D.cfg.subdiv, sel, &v, &nl, &nt) == 0) {
                qw_renderer_set_overlay(&D.ren, k, v, nl, nt);
                free(v);
            }
        } else {
            qw_renderer_set_overlay(&D.ren, k, NULL, 0, 0);
        }
        qw_calib_free(&tc);
    }
    D.geom_dirty = 0;
    return 0;
}

static void reply(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void warp_stop(void) {
    if (D.dump_pending) {
        if (D.dump_client >= 0) reply(D.dump_client, "ERROR the warp stopped before the dump");
        D.dump_pending = 0;
        D.dump_client = -1;
    }
    if (D.ready) { qw_output_drop(&D.out, D.ready); D.ready = NULL; }
    if (D.ren_open) { gridsrc_free(D.grid); qw_renderer_free(&D.ren); }
    if (D.src_open) fb0src_close(&D.src);
    if (D.out_open) qw_output_close(&D.out); // planes off, fb0 visible again
    if (D.out_open && D.state == ST_ON) {
        stats_log("total", &D.total);
        set_rt(0);
    }
    D.ren_open = D.src_open = D.out_open = 0;
}

static int warp_start(void) {
    char err[512];
    if (!D.calib_ok) { set_error("calibration not loaded; warp stays off"); return -1; }
    if (qw_output_open(&D.out, err, sizeof err)) { set_error("warp start: %s", err); return -1; }
    D.out_open = 1;
    if (fb0src_open(&D.src, &D.out, err, sizeof err)) { set_error("warp start: %s", err); goto fail; }
    D.src_open = 1;
    struct qw_panel_map maps[QW_NPANEL];
    qw_config_maps(&D.cfg, maps);
    if (D.cfg.copy_source && fb0src_enable_copy(&D.src, maps)) { set_error("warp start: fb0 copy textures failed"); goto fail; }
    if (qw_renderer_init(&D.ren, err, sizeof err)) { set_error("warp start: %s", err); goto fail; }
    D.ren_open = 1;
    if (gridsrc_create(D.grid)) { set_error("warp start: test grid textures failed"); goto fail; }
    if (build_geometry()) goto fail;
    stats_reset(&D.total);
    stats_reset(&D.per);
    D.fps = 0;
    D.fps_t0 = qw_now_ms();
    D.fps_n = 0;
    D.frame_no = 0;
    set_rt(D.o.rt >= 0 ? D.o.rt : D.cfg.rt_priority);
    D.error[0] = 0;
    return 0;
fail:
    warp_stop();
    return -1;
}

// After a failure while the warp is wanted: try again later, a few times.
static void schedule_retry(void) {
    if (!D.enabled || D.state == ST_SUSPENDED || D.resume_until > 0) return;
    if (D.retry_n >= NRETRY) {
        if (D.retry_at > 0 || D.retry_n == NRETRY) QW_ERR("no automatic restarts left; a command (on, resume, reload) retries");
        D.retry_at = 0;
        D.retry_n = NRETRY + 1;
        return;
    }
    D.retry_at = qw_now_ms() + RETRY_DELAYS_MS[D.retry_n];
    QW_INFO("automatic restart %d of %d in %.0f s", D.retry_n + 1, NRETRY, RETRY_DELAYS_MS[D.retry_n] / 1e3);
    D.retry_n++;
}

// a command asks for the warp: start over with all automatic restarts
static void clear_retry(void) {
    D.retry_at = 0;
    D.retry_n = 0;
    D.start_failed = 0;
}

// Bring the display state in line with the setting. Returns 0, or -1 if a
// wanted start failed (the error is in D.error).
static int apply_state(void) {
    if (D.state == ST_SUSPENDED) {
        if (D.out_open) warp_stop();
        return 0;
    }
    if (D.enabled && D.state != ST_ON) {
        if (D.start_failed) return -1;
        if (warp_start()) {
            D.start_failed = 1;
            D.state = ST_OFF;
            QW_ERR("warp could not start; staying OFF (fb0 is shown unwarped)");
            schedule_retry();
            return -1;
        }
        D.state = ST_ON;
        D.on_since = qw_now_ms();
        QW_INFO("state ON");
    } else if (!D.enabled && D.state == ST_ON) {
        warp_stop();
        D.state = ST_OFF;
        QW_INFO("state OFF");
    }
    return 0;
}

static void warp_failed(void) {
    QW_ERR("warp failed; turning OFF (fb0 is shown unwarped)");
    warp_stop();
    D.state = ST_OFF;
    D.start_failed = 1;
    schedule_retry();
    write_state();
}

static void check_primary_fb(void) {
    drmModePlane *p = drmModeGetPlane(D.out.fd, D.out.primary_plane);
    if (!p) return;
    uint32_t fb = p->fb_id;
    drmModeFreePlane(p);
    if (!fb || fb == D.src.fb_id) return;
    QW_INFO("primary plane fb changed %u -> %u: re-importing fb0", D.src.fb_id, fb);
    char err[256];
    fb0src_close(&D.src);
    D.src_open = 0;
    D.out.primary_fb = fb;
    if (fb0src_open(&D.src, &D.out, err, sizeof err)) {
        set_error("fb0 re-import: %s", err);
        warp_failed();
        return;
    }
    D.src_open = 1;
    struct qw_panel_map maps[QW_NPANEL];
    qw_config_maps(&D.cfg, maps);
    if (D.cfg.copy_source && fb0src_enable_copy(&D.src, maps)) {
        set_error("fb0 re-import: copy textures failed");
        warp_failed();
        return;
    }
    D.geom_dirty = 1;
}

static int write_ppm(const char *path, const uint8_t *rgb) {
    FILE *f = fopen(path, "wb");
    if (!f) { QW_ERR("%s: %m", path); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", QW_FW, QW_PH);
    size_t n = fwrite(rgb, 3, (size_t)QW_FW * QW_PH, f);
    int bad = fclose(f) || n != (size_t)QW_FW * QW_PH;
    if (!bad) QW_INFO("wrote %s", path);
    return bad ? -1 : 0;
}

// pre/post: fb0 read before the draw and after the read-back; pixels that
// changed in between (animations) are skipped
static int selfcheck(const uint8_t *out, const uint8_t *pre, const uint8_t *post) {
    long bad = 0, live = 0;
    int maxd = 0;
    for (size_t p = 0; p < (size_t)QW_FW * QW_PH; p++) {
        const uint8_t *o = out + 3 * p, *a = pre + 3 * p, *b = post + 3 * p;
        if (memcmp(a, b, 3)) { live++; continue; }
        for (int c = 0; c < 3; c++) {
            int d = abs(o[c] - a[c]);
            if (d) bad++;
            if (d > maxd) maxd = d;
        }
    }
    QW_INFO("selfcheck: %ld of %d samples differ from fb0, max diff %d (%ld pixels changed during the frame, skipped)",
            bad, QW_FW * QW_PH * 3, maxd, live);
    return maxd <= 1 ? 0 : -1;
}

static void reply(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Draw one frame and hand it to the display. Called when no flip is pending.
// Pipeline mode: the frame finished last time is committed first, and the new
// one waits for the next flip event, so the GPU has two frame times for it.
static int frame(void) {
    char err[256];
    if (D.geom_dirty && build_geometry()) return -1;
    if (D.cfg.pipeline && D.ready) {
        if (qw_output_commit(&D.out, D.ready, err, sizeof err)) { set_error("commit: %s", err); return -1; }
        D.ready = NULL;
    }
    int test_dump = D.frame_no == D.o.dump_at && (D.o.dump || D.o.dump_src || D.o.selfcheck);
    int dump_now = D.dump_pending || test_dump;
    uint8_t *pre = NULL;
    if (test_dump && D.o.selfcheck) {
        pre = malloc((size_t)QW_FW * QW_PH * 3);
        if (pre && fb0src_read(&D.src, pre)) { free(pre); pre = NULL; }
    }
    double t0 = qw_now_ms();
    if (!D.source_grid) fb0src_copy(&D.src);
    qw_renderer_draw(&D.ren);
    if (dump_now) {
        uint8_t *a = malloc((size_t)QW_FW * QW_PH * 3), *b = malloc((size_t)QW_FW * QW_PH * 3);
        int ok = a && b;
        if (ok) {
            qw_renderer_readback(a);
            int src_ok = fb0src_read(&D.src, b) == 0;
            const char *dp = D.dump_pending ? D.dump_path : D.o.dump;
            const char *sp = D.dump_pending ? (D.dump_src_path[0] ? D.dump_src_path : NULL) : D.o.dump_src;
            if (dp && write_ppm(dp, a)) ok = 0;
            if (sp && src_ok && write_ppm(sp, b)) ok = 0;
            if (test_dump && D.o.selfcheck) D.check_rc = (pre && src_ok) ? selfcheck(a, pre, b) : -1;
        }
        if (D.dump_pending) {
            if (D.dump_client >= 0) {
                if (ok) reply(D.dump_client, "OK dumped %s", D.dump_path);
                else reply(D.dump_client, "ERROR dump failed (see the journal)");
            }
            D.dump_pending = 0;
            D.dump_client = -1;
        }
        free(a);
        free(b);
    }
    free(pre);
    if (dump_now) t0 = qw_now_ms(); // the read-back is not frame time
    struct gbm_bo *bo = qw_output_finish_frame(&D.out, D.o.finish, err, sizeof err);
    if (!bo) { set_error("frame: %s", err); return -1; }
    double rt = qw_now_ms() - t0;
    D.per.render_ms += rt;
    D.total.render_ms += rt;
    if (D.cfg.pipeline && D.out.flip_pending) {
        D.ready = bo; // committed at the next flip event
    } else if (qw_output_commit(&D.out, bo, err, sizeof err)) {
        qw_output_drop(&D.out, bo);
        set_error("commit: %s", err);
        return -1;
    }
    D.frame_no++;
    return 0;
}

// ------------------------------------------------------------------ control socket
static void reply(int fd, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    buf[n++] = '\n';
    if (send(fd, buf, (size_t)n, MSG_NOSIGNAL | MSG_DONTWAIT) < 0) QW_DBG("reply to fd %d: %m", fd);
}

static void status_line(char *buf, size_t len) {
    snprintf(buf, len,
             "state=%s enabled=%d resuming=%d mirror_x=%d mirror_y=%d tune=%d source=%s panel=%d handle=%s "
             "step=%d unsaved=%d fps=%.2f frames=%ld late=%ld pipeline=%d copy=%d retries=%d error=\"%s\"",
             state_names[D.state], D.enabled, D.resume_until > 0, D.cfg.mirror_x, D.cfg.mirror_y, D.tune,
             D.source_grid ? "grid" : "fb0",
             D.sel_panel + 1, sel_name(), D.step, trim_unsaved(), D.state == ST_ON ? D.fps : 0.0,
             D.total.frames, D.total.late, D.cfg.pipeline, D.cfg.copy_source, D.retry_n > NRETRY ? NRETRY : D.retry_n,
             D.error);
}

static void set_enabled(int fd, int en) {
    char err[256];
    D.enabled = en;
    D.resume_until = 0;
    clear_retry();
    int saved = conf_set_enabled(D.conf_path, en, err, sizeof err) == 0;
    if (!saved) QW_ERR("saving the warp setting: %s", err);
    if (apply_state()) reply(fd, "ERROR warp could not start: %s", D.error);
    else if (!saved) reply(fd, "ERROR state=%s, but the setting was not saved: %s", state_names[D.state], err);
    else reply(fd, "OK state=%s enabled=%d", state_names[D.state], D.enabled);
}

// mirror_x / mirror_y: kept in the writable warp.conf like "enabled", applied
// at once (a geometry rebuild; while off or suspended it waits for the start)
static void set_mirror(int fd, int mx, int my) {
    char err[256];
    if (mx != D.cfg.mirror_x || my != D.cfg.mirror_y)
        QW_INFO("mirror_x=%d mirror_y=%d (was %d %d)", mx, my, D.cfg.mirror_x, D.cfg.mirror_y);
    D.cfg.mirror_x = mx;
    D.cfg.mirror_y = my;
    if (D.src_open && D.src.copy) {
        struct qw_panel_map maps[QW_NPANEL];
        qw_config_maps(&D.cfg, maps);
        fb0src_set_columns(&D.src, maps);
    }
    D.geom_dirty = 1;
    const char *k[] = { "mirror_x", "mirror_y" }, *v[] = { mx ? "true" : "false", my ? "true" : "false" };
    if (conf_set_keys(D.conf_path, k, v, 2, err, sizeof err)) {
        QW_ERR("saving the mirror setting: %s", err);
        reply(fd, "ERROR mirror_x=%d mirror_y=%d, but the setting was not saved: %s", mx, my, err);
    } else {
        reply(fd, "OK mirror_x=%d mirror_y=%d", mx, my);
    }
}

static void nudge(float dx, float dy) {
    for (int k = 0; k < QW_NPANEL; k++) {
        if (D.sel != SEL_ALL && k != D.sel_panel) continue;
        for (int h = 0; h < QW_NHANDLE; h++) {
            if (D.sel < QW_NHANDLE && h != D.sel) continue;
            D.trim.h[k][h][0] += dx;
            D.trim.h[k][h][1] += dy;
        }
    }
}

static int parse_int(const char *s, int lo, int hi, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < lo || v > hi) return -1;
    *out = (int)v;
    return 0;
}

static void reload(void) {
    if (D.state == ST_ON) { warp_stop(); D.state = ST_OFF; }
    load_config();
    D.enabled = D.o.force >= 0 ? D.o.force : D.cfg.enabled;
    D.resume_until = 0;
    clear_retry();
    apply_state();
}

static void command(int fd, char *line) {
    char *argv[8];
    int argc = 0;
    for (char *t = strtok(line, " \t\r"); t && argc < 8; t = strtok(NULL, " \t\r")) argv[argc++] = t;
    if (!argc) { reply(fd, "ERROR empty request"); return; }
    const char *cmd = argv[0];
    char buf[768], err[256];
    int geom = 0;

#define USAGE(u) do { reply(fd, "ERROR usage: %s", u); return; } while (0)
    if (!strcmp(cmd, "status")) {
        if (argc != 1) USAGE("status");
        status_line(buf, sizeof buf);
        reply(fd, "OK %s", buf);
        return;
    } else if (!strcmp(cmd, "on") || !strcmp(cmd, "off") || !strcmp(cmd, "toggle")) {
        if (argc != 1) USAGE("on | off | toggle");
        set_enabled(fd, !strcmp(cmd, "on") ? 1 : !strcmp(cmd, "off") ? 0 : !D.enabled);
    } else if (!strcmp(cmd, "mirror")) {
        int mx = D.cfg.mirror_x, my = D.cfg.mirror_y;
        if (argc == 1) {
            reply(fd, "OK mirror_x=%d mirror_y=%d", mx, my);
            return;
        } else if (argc == 2 && !strcmp(argv[1], "off")) {
            mx = my = 0;
        } else if (argc == 3 && (!strcmp(argv[1], "x") || !strcmp(argv[1], "y")) &&
                   (!strcmp(argv[2], "on") || !strcmp(argv[2], "off") || !strcmp(argv[2], "toggle"))) {
            int *m = argv[1][0] == 'x' ? &mx : &my;
            *m = !strcmp(argv[2], "on") ? 1 : !strcmp(argv[2], "off") ? 0 : !*m;
        } else {
            USAGE("mirror | mirror x|y on|off|toggle | mirror off");
        }
        set_mirror(fd, mx, my);
    } else if (!strcmp(cmd, "suspend")) {
        if (argc != 1) USAGE("suspend");
        D.resume_until = 0;
        D.retry_at = 0;
        if (D.state == ST_ON) warp_stop();
        if (D.state != ST_SUSPENDED) QW_INFO("state SUSPENDED");
        D.state = ST_SUSPENDED;
        reply(fd, "OK state=suspended");
    } else if (!strcmp(cmd, "resume")) {
        if (argc != 1) USAGE("resume");
        if (D.state != ST_SUSPENDED) {
            reply(fd, "OK state=%s (not suspended)", state_names[D.state]);
        } else {
            D.state = ST_OFF;
            clear_retry();
            if (!D.enabled) {
                QW_INFO("resumed; the warp setting is off");
                reply(fd, "OK state=off");
            } else if ((D.resume_until = qw_now_ms() + RESUME_RETRY_MS, apply_state() == 0)) {
                // (resume_until is set first: a failed first try is the
                // resume window's business, not an automatic restart)
                D.resume_until = 0;
                reply(fd, "OK state=on");
            } else {
                // the app that just exited may still hold the card
                D.start_failed = 0;
                QW_INFO("resume: display busy, retrying for %.0f s", RESUME_RETRY_MS / 1e3);
                reply(fd, "OK state=off resuming=1 (display busy, retrying)");
            }
        }
    } else if (!strcmp(cmd, "tune")) {
        if (argc == 2 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))) {
            D.tune = !strcmp(argv[1], "on");
            if (!D.tune) D.source_grid = 0;
            geom = 1;
            reply(fd, "OK tune=%d source=%s", D.tune, D.source_grid ? "grid" : "fb0");
        } else if (argc == 3 && !strcmp(argv[1], "source") && (!strcmp(argv[2], "grid") || !strcmp(argv[2], "fb0"))) {
            D.source_grid = !strcmp(argv[2], "grid");
            geom = 1;
            reply(fd, "OK source=%s", argv[2]);
        } else {
            USAGE("tune on|off | tune source grid|fb0");
        }
    } else if (!strcmp(cmd, "select")) {
        int n;
        if (argc == 3 && !strcmp(argv[1], "panel")) {
            if (!strcmp(argv[2], "next")) D.sel_panel = (D.sel_panel + 1) % QW_NPANEL;
            else if (!strcmp(argv[2], "prev")) D.sel_panel = (D.sel_panel + QW_NPANEL - 1) % QW_NPANEL;
            else if (!parse_int(argv[2], 1, QW_NPANEL, &n)) D.sel_panel = n - 1;
            else USAGE("select panel 1|2|3|next|prev");
        } else if (argc == 3 && !strcmp(argv[1], "handle")) {
            int h = qw_handle_index(argv[2]);
            if (h >= 0) D.sel = h;
            else if (!strcmp(argv[2], "panel")) D.sel = SEL_PANEL;
            else if (!strcmp(argv[2], "all")) D.sel = SEL_ALL;
            else USAGE("select handle tl|t|tr|l|c|r|bl|b|br|panel|all");
        } else if (argc == 2 && !strcmp(argv[1], "next")) {
            D.sel = (D.sel + 1) % (SEL_ALL + 1);
        } else if (argc == 2 && !strcmp(argv[1], "prev")) {
            D.sel = (D.sel + SEL_ALL) % (SEL_ALL + 1);
        } else {
            USAGE("select panel N|next|prev | select handle NAME | select next|prev");
        }
        geom = 1;
        reply(fd, "OK panel=%d handle=%s", D.sel_panel + 1, sel_name());
    } else if (!strcmp(cmd, "nudge")) {
        int dx, dy;
        if (argc != 3 || parse_int(argv[1], -100, 100, &dx) || parse_int(argv[2], -100, 100, &dy))
            USAGE("nudge DX DY (in steps, integers -100..100)");
        nudge((float)dx * D.step, (float)dy * D.step);
        geom = 1;
        const float *h = D.trim.h[D.sel == SEL_ALL ? 0 : D.sel_panel][D.sel < QW_NHANDLE ? D.sel : 4];
        reply(fd, "OK panel=%d handle=%s dx=%g dy=%g", D.sel_panel + 1, sel_name(), h[0], h[1]);
    } else if (!strcmp(cmd, "step")) {
        int n;
        if (argc != 2 || parse_int(argv[1], 1, 50, &n)) USAGE("step N (1..50 px)");
        D.step = n;
        reply(fd, "OK step=%d", D.step);
    } else if (!strcmp(cmd, "save")) {
        if (argc != 1) USAGE("save");
        if (qw_trim_save(D.trim_path, &D.trim, err, sizeof err)) { reply(fd, "ERROR save: %s", err); return; }
        D.trim_saved = D.trim;
        QW_INFO("trim saved to %s", D.trim_path);
        reply(fd, "OK saved %s", D.trim_path);
    } else if (!strcmp(cmd, "revert")) {
        if (argc != 1) USAGE("revert");
        struct qw_trim t;
        int rc = qw_trim_load(D.trim_path, &t, err, sizeof err);
        if (rc < 0) { reply(fd, "ERROR revert: %s", err); return; }
        D.trim = D.trim_saved = t;
        geom = 1;
        reply(fd, "OK reverted%s", rc == 1 ? " (no saved trim: zero)" : "");
    } else if (!strcmp(cmd, "reset")) {
        if (argc != 1) USAGE("reset");
        memset(&D.trim, 0, sizeof D.trim);
        geom = 1;
        reply(fd, "OK trim reset (not saved)");
    } else if (!strcmp(cmd, "reload")) {
        if (argc != 1) USAGE("reload");
        reload();
        if (D.enabled && D.state == ST_OFF) reply(fd, "ERROR reloaded, but the warp could not start: %s", D.error);
        else reply(fd, "OK reloaded state=%s", state_names[D.state]);
    } else if (!strcmp(cmd, "dump")) {
        if (argc < 2 || argc > 3 || argv[1][0] != '/' || (argc == 3 && argv[2][0] != '/'))
            USAGE("dump /OUT.ppm [/FB0.ppm]");
        if (D.state != ST_ON) { reply(fd, "ERROR warp is not on"); return; }
        if (D.dump_pending) { reply(fd, "ERROR a dump is already pending"); return; }
        snprintf(D.dump_path, sizeof D.dump_path, "%s", argv[1]);
        snprintf(D.dump_src_path, sizeof D.dump_src_path, "%s", argc == 3 ? argv[2] : "");
        D.dump_pending = 1;
        D.dump_client = fd; // replied after the next frame
        return;
    } else if (!strcmp(cmd, "help")) {
        reply(fd, "OK commands: status on off toggle mirror suspend resume tune select nudge step save revert "
                  "reset reload dump help");
        return;
    } else {
        reply(fd, "ERROR unknown command '%s' (try help)", cmd);
        return;
    }
#undef USAGE
    if (geom) D.geom_dirty = 1;
    write_state();
}

static void client_close(int i) {
    if (D.dump_client == D.cl[i].fd) D.dump_client = -1;
    close(D.cl[i].fd);
    D.cl[i].fd = -1;
}

static void client_read(int i) {
    struct client *c = &D.cl[i];
    ssize_t n = recv(c->fd, c->buf + c->len, sizeof c->buf - 1 - c->len, MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { client_close(i); return; }
    if (n < 0) return;
    c->len += (size_t)n;
    char *nl;
    while (c->fd >= 0 && (nl = memchr(c->buf, '\n', c->len))) {
        *nl = 0;
        size_t used = (size_t)(nl - c->buf) + 1;
        command(c->fd, c->buf);
        memmove(c->buf, c->buf + used, c->len - used);
        c->len -= used;
    }
    if (c->fd >= 0 && c->len == sizeof c->buf - 1) {
        reply(c->fd, "ERROR request too long");
        client_close(i);
    }
}

static int open_socket(void) {
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    if (strlen(D.sock_path) >= sizeof a.sun_path) { QW_ERR("socket path too long"); return -1; }
    strcpy(a.sun_path, D.sock_path);
    // a live daemon answers on the socket: do not steal it
    int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (probe >= 0 && connect(probe, (struct sockaddr *)&a, sizeof a) == 0) {
        close(probe);
        QW_ERR("%s: another qvue-warpd is running", D.sock_path);
        return -1;
    }
    if (probe >= 0) close(probe);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    unlink(D.sock_path);
    mode_t old = umask(0117); // socket 0660: user pi, group video
    int rc = bind(fd, (struct sockaddr *)&a, sizeof a);
    umask(old);
    if (rc || listen(fd, 8)) { QW_ERR("%s: %m", D.sock_path); close(fd); return -1; }
    return fd;
}

// ------------------------------------------------------------------ main
static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [options]\n"
            "  --config F       writable config (default <state dir>/warp.conf), read over " QW_DATADIR "/warp.conf\n"
            "  --state-dir D    persistent files (default /data/qvue-warp, else ~/.config/qvue-warp)\n"
            "  --run-dir D      ctl.sock and state.json (default /run/qvue-warp)\n"
            "  --on | --off     override 'enabled' at start (not saved)\n"
            "  --subdiv S       override the Catmull-Rom subdivision\n"
            "  --pipeline | --no-pipeline  override 'pipeline'\n"
            "  --rt P           SCHED_FIFO priority of the render loop (0 = normal)\n"
            "  --stats-interval S  seconds between statistics log lines (0 = off)\n"
            "  --verbose        debug log (one line per late frame)\n"
            "test options (no control socket):\n"
            "  --frames N       render N frames, print statistics, exit (status 1 if the warp cannot start)\n"
            "  --identity       identity mesh (output must equal fb0)\n"
            "  --dump F.ppm     read back frame --dump-at (default 0) as a 4500x298 PPM\n"
            "  --dump-src F.ppm write fb0 at the same frame as a PPM\n"
            "  --dump-at N      frame for --dump/--dump-src/--selfcheck\n"
            "  --selfcheck      compare that frame with fb0 (use with --identity; status 3 if max diff > 1)\n"
            "  --hold           after that frame keep it on screen until SIGTERM/SIGINT\n"
            "  --finish         glFinish before each commit (timing experiments)\n"
            "  --only-panel DIGITS  draw only these panels (timing experiments)\n"
            "  --source grid|fb0    start with this source (default fb0)\n"
            "  --copy-src | --no-copy-src  override 'copy_source'\n"
            "  --debug-lose-flip N  ignore the flip events after frame N (tests the flip watchdog)\n",
            argv0);
}

int main(int argc, char **argv) {
    struct opts *o = &D.o;
    *o = (struct opts){ .force = -1, .frames = -1, .rt = -1, .stats_interval = -1, .pipeline = -1, .copy_src = -1, .lose_flip = -1 };
    static const struct option lo[] = {
        { "config", 1, 0, 'c' }, { "state-dir", 1, 0, 'T' }, { "run-dir", 1, 0, 'R' },
        { "on", 0, 0, '1' }, { "off", 0, 0, '0' }, { "subdiv", 1, 0, 's' },
        { "pipeline", 0, 0, 'p' }, { "no-pipeline", 0, 0, 'q' },
        { "rt", 1, 0, 'r' }, { "stats-interval", 1, 0, 'S' }, { "verbose", 0, 0, 'v' },
        { "frames", 1, 0, 'n' }, { "identity", 0, 0, 'i' }, { "dump", 1, 0, 'd' }, { "dump-src", 1, 0, 'D' },
        { "dump-at", 1, 0, 'a' }, { "selfcheck", 0, 0, 'k' }, { "hold", 0, 0, 'H' }, { "finish", 0, 0, 'f' },
        { "only-panel", 1, 0, 'P' }, { "source", 1, 0, 'G' }, { "copy-src", 0, 0, 'C' }, { "no-copy-src", 0, 0, 'N' }, { "debug-lose-flip", 1, 0, 'L' }, { "help", 0, 0, 'h' }, { 0, 0, 0, 0 }
    };
    int ch;
    while ((ch = getopt_long(argc, argv, "c:vh", lo, NULL)) != -1) {
        switch (ch) {
        case 'c': o->config = optarg; break;
        case 'T': o->state_dir = optarg; break;
        case 'R': o->run_dir = optarg; break;
        case '1': o->force = 1; break;
        case '0': o->force = 0; break;
        case 's': o->subdiv = atoi(optarg); break;
        case 'p': o->pipeline = 1; break;
        case 'q': o->pipeline = 0; break;
        case 'r': o->rt = atoi(optarg); break;
        case 'S': o->stats_interval = atoi(optarg); break;
        case 'v': qw_verbose = 1; break;
        case 'n': o->frames = atol(optarg); break;
        case 'i': o->identity = 1; break;
        case 'd': o->dump = optarg; break;
        case 'D': o->dump_src = optarg; break;
        case 'a': o->dump_at = atol(optarg); break;
        case 'k': o->selfcheck = 1; break;
        case 'H': o->hold = 1; break;
        case 'f': o->finish = 1; break;
        case 'P': o->only_panel = optarg; break;
        case 'C': o->copy_src = 1; break;
        case 'N': o->copy_src = 0; break;
        case 'L': o->lose_flip = atol(optarg); break;
        case 'G':
            if (strcmp(optarg, "grid") && strcmp(optarg, "fb0")) { usage(argv[0]); return 2; }
            o->source_grid = !strcmp(optarg, "grid");
            break;
        default: usage(argv[0]); return ch == 'h' ? 0 : 2;
        }
    }
    if (optind < argc) { usage(argv[0]); return 2; }
    setvbuf(stderr, NULL, _IOLBF, 0);
    int test = o->frames >= 0;

    // signals through a signalfd: nothing is lost between a check and the poll
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGTERM);
    sigaddset(&ss, SIGINT);
    sigaddset(&ss, SIGHUP);
    sigprocmask(SIG_BLOCK, &ss, NULL);
    signal(SIGPIPE, SIG_IGN);
    D.sfd = signalfd(-1, &ss, SFD_CLOEXEC | SFD_NONBLOCK);
    if (D.sfd < 0) { QW_ERR("signalfd: %m"); return 1; }
    D.lfd = -1;
    D.dump_client = -1;
    D.step = 1;
    D.sel = 4; // centre handle
    D.source_grid = o->source_grid;
    for (int i = 0; i < MAX_CLIENTS; i++) D.cl[i].fd = -1;

    if (o->state_dir) {
        snprintf(D.state_dir, sizeof D.state_dir, "%s", o->state_dir);
        mkdir(D.state_dir, 0755);
    } else if (qw_state_dir(D.state_dir, sizeof D.state_dir)) {
        QW_WARN("no writable state directory (%s): settings will not be saved", D.state_dir);
    }
    if (o->run_dir) {
        snprintf(D.run_dir, sizeof D.run_dir, "%s", o->run_dir);
        mkdir(D.run_dir, 0775);
    } else if (pick_run_dir(D.run_dir, sizeof D.run_dir)) {
        QW_WARN("no writable runtime directory (%s)", D.run_dir);
    }
    if (o->config) snprintf(D.conf_path, sizeof D.conf_path, "%s", o->config);
    else snprintf(D.conf_path, sizeof D.conf_path, "%s/warp.conf", D.state_dir);
    snprintf(D.trim_path, sizeof D.trim_path, "%s/trim.json", D.state_dir);
    snprintf(D.sock_path, sizeof D.sock_path, "%s/ctl.sock", D.run_dir);
    snprintf(D.state_path, sizeof D.state_path, "%s/state.json", D.run_dir);
    QW_INFO("state dir %s, socket %s", D.state_dir, test ? "(none: test mode)" : D.sock_path);

    if (!test && (D.lfd = open_socket()) < 0) return 1;

    load_config();
    char err[256];
    int tr = qw_trim_load(D.trim_path, &D.trim, err, sizeof err);
    if (tr < 0) QW_ERR("%s (trim ignored)", err);
    else if (tr == 0) QW_INFO("trim %s", D.trim_path);
    D.trim_saved = D.trim;
    D.enabled = o->force >= 0 ? o->force : D.cfg.enabled;
    D.state = ST_OFF;
    if (apply_state() && test) return 1;
    if (!test) write_state();

    int sint = o->stats_interval >= 0 ? o->stats_interval : D.cfg.stats_interval;
    double t = qw_now_ms(), next_log = t + sint * 1e3, next_fbcheck = t + 1000, next_state = t + 5000;
    int hold = 0;
    long skip_until = 0; // flips not paced: start-up and after a read-back
    while (!D.stop) {
        // render when the display can take a frame
        if (D.state == ST_ON && !D.out.flip_pending && !hold) {
            int dumping = D.dump_pending || (D.frame_no == o->dump_at && (o->dump || o->dump_src || o->selfcheck));
            if (frame()) {
                warp_failed();
                if (test) return 1;
                continue;
            }
            if (dumping) skip_until = D.total.frames + 3;
            if (D.cfg.pipeline && !D.out.flip_pending && D.ready) {
                // first frame of a pipelined start: show it and draw the next
                if (qw_output_commit(&D.out, D.ready, err, sizeof err)) {
                    set_error("commit: %s", err);
                    warp_failed();
                    if (test) return 1;
                    continue;
                }
                D.ready = NULL;
                if (frame()) {
                    warp_failed();
                    if (test) return 1;
                    continue;
                }
            }
        }
        struct pollfd pfd[3 + MAX_CLIENTS];
        int np = 0, idx_drm = -1, idx_l = -1, idx_c[MAX_CLIENTS];
        pfd[np++] = (struct pollfd){ .fd = D.sfd, .events = POLLIN };
        int lose = o->lose_flip >= 0 && D.frame_no > o->lose_flip; // debug: events stay unread
        if (D.state == ST_ON && !lose) { idx_drm = np; pfd[np++] = (struct pollfd){ .fd = D.out.fd, .events = POLLIN }; }
        if (D.lfd >= 0) { idx_l = np; pfd[np++] = (struct pollfd){ .fd = D.lfd, .events = POLLIN }; }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            idx_c[i] = -1;
            if (D.cl[i].fd >= 0) { idx_c[i] = np; pfd[np++] = (struct pollfd){ .fd = D.cl[i].fd, .events = POLLIN }; }
        }
        double now = qw_now_ms(), wake = now + 1000;
        if (D.state == ST_ON) {
            if (sint > 0 && next_log < wake) wake = next_log;
            if (next_fbcheck < wake) wake = next_fbcheck;
            if (next_state < wake) wake = next_state;
        }
        if (D.resume_until > 0 && now + 250 < wake) wake = now + 250;
        if (D.retry_at > 0 && D.retry_at < wake) wake = D.retry_at;
        if (D.state == ST_ON && D.out.flip_pending && D.out.queued_commit_ms + FLIP_TIMEOUT_MS + 1 < wake)
            wake = D.out.queued_commit_ms + FLIP_TIMEOUT_MS + 1;
        int timeout = wake > now ? (int)(wake - now) + 1 : 1; // never a busy loop at SCHED_FIFO
        int rc = poll(pfd, (nfds_t)np, timeout);
        if (rc < 0 && errno != EINTR) { QW_ERR("poll: %m"); break; }
        now = qw_now_ms();

        if (rc > 0 && (pfd[0].revents & POLLIN)) {
            struct signalfd_siginfo si;
            while (read(D.sfd, &si, sizeof si) == sizeof si) {
                if (si.ssi_signo == SIGHUP) {
                    QW_INFO("SIGHUP: reload");
                    reload();
                    if (!test) write_state();
                } else {
                    D.stop = 1;
                }
            }
        }
        if (D.stop) break;
        if (idx_drm >= 0 && rc > 0 && (pfd[idx_drm].revents & POLLIN) && D.state == ST_ON) {
            struct qw_flip_info fi;
            int f = qw_output_dispatch(&D.out, &fi, err, sizeof err);
            if (f < 0) {
                set_error("%s", err);
                warp_failed();
                if (test) return 1;
            } else if (f == 1) {
                stats_flip(&fi, D.total.frames > 10 && D.total.frames >= skip_until);
                if (test && D.total.frames >= o->frames) break;
                if (o->hold && D.frame_no > o->dump_at && !hold) {
                    hold = 1;
                    QW_INFO("holding the last frame until SIGTERM/SIGINT");
                }
            }
        }
        if (idx_l >= 0 && rc > 0 && (pfd[idx_l].revents & POLLIN)) {
            int c = accept4(D.lfd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (c >= 0) {
                int i = 0;
                while (i < MAX_CLIENTS && D.cl[i].fd >= 0) i++;
                if (i == MAX_CLIENTS) { reply(c, "ERROR too many clients"); close(c); }
                else { D.cl[i].fd = c; D.cl[i].len = 0; }
            }
        }
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (idx_c[i] >= 0 && rc > 0 && D.cl[i].fd >= 0 && (pfd[idx_c[i]].revents & (POLLIN | POLLHUP | POLLERR)))
                client_read(i);

        // timers
        if (D.resume_until > 0) {
            if (D.state != ST_OFF || !D.enabled) {
                D.resume_until = 0;
            } else {
                D.start_failed = 0;
                if (apply_state() == 0) {
                    QW_INFO("resume: the warp is back");
                    D.resume_until = 0;
                    D.error[0] = 0;
                    write_state();
                } else if (now > D.resume_until) {
                    QW_ERR("resume: display still busy after %.0f s; staying OFF", RESUME_RETRY_MS / 1e3);
                    D.resume_until = 0;
                    schedule_retry();
                    write_state();
                }
            }
        }
        if (D.retry_at > 0 && now >= D.retry_at) {
            D.retry_at = 0;
            if (D.state == ST_OFF && D.enabled) {
                QW_INFO("automatic restart");
                D.start_failed = 0;
                if (apply_state() == 0) D.error[0] = 0;
                write_state();
            }
        }
        if (D.state == ST_ON && D.out.flip_pending && now - D.out.queued_commit_ms > FLIP_TIMEOUT_MS) {
            set_error("no page flip for %.0f ms: the display is stuck", now - D.out.queued_commit_ms);
            warp_failed();
            if (test) return 1;
        }
        if (D.state == ST_ON && D.retry_n && now - D.on_since > 60000) D.retry_n = 0; // ran fine for a minute
        if (D.state == ST_ON) {
            if (now - D.fps_t0 >= 1000) {
                D.fps = D.fps_n * 1e3 / (now - D.fps_t0);
                D.fps_n = 0;
                D.fps_t0 = now;
            }
            if (sint > 0 && now >= next_log) {
                stats_log("interval", &D.per);
                struct stats keep = D.per;
                stats_reset(&D.per);
                D.per.have_prev = 1;
                D.per.prev_seq = keep.prev_seq;
                D.per.prev_vblank = keep.prev_vblank;
                next_log = now + sint * 1e3;
            }
            if (now >= next_fbcheck) { check_primary_fb(); next_fbcheck = now + 1000; }
            if (now >= next_state) { if (!test) write_state(); next_state = now + 5000; }
        }
    }
    if (D.state == ST_ON) {
        warp_stop();
        QW_INFO("state OFF (exit)");
    }
    D.state = ST_OFF;
    for (int i = 0; i < MAX_CLIENTS; i++) if (D.cl[i].fd >= 0) close(D.cl[i].fd);
    if (D.lfd >= 0) { close(D.lfd); unlink(D.sock_path); }
    if (!test) write_state();
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    QW_INFO("process cpu: user %.2f s, system %.2f s",
            ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6, ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6);
    return D.check_rc ? 3 : 0;
}
