// Stacked output stage: one 1500x894 XRGB8888 GBM surface (three panels stacked,
// because V3D render targets stop at 4096), scanned out by three overlay planes
// that crop it back to 4500x298. The primary plane (fb0) stays untouched below.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "qvuewarp.h"

#define SW QW_PW
#define SH (QW_PH * QW_NPANEL)

static int open_vc4(char *card, size_t cardlen) {
    // the card number changes between boots: pick it by driver name
    for (int i = 0; i < 16; i++) {
        char p[32];
        snprintf(p, sizeof p, "/dev/dri/card%d", i);
        int fd = open(p, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        drmVersionPtr v = drmGetVersion(fd);
        int ok = v && !strcmp(v->name, "vc4");
        if (v) drmFreeVersion(v);
        if (ok) { snprintf(card, cardlen, "%s", p); return fd; }
        close(fd);
    }
    return -1;
}

static uint32_t prop_id(int fd, uint32_t obj, uint32_t type, const char *name, uint64_t *val) {
    drmModeObjectProperties *ps = drmModeObjectGetProperties(fd, obj, type);
    uint32_t id = 0;
    for (uint32_t i = 0; ps && i < ps->count_props; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, ps->props[i]);
        if (p && !strcmp(p->name, name)) {
            id = p->prop_id;
            if (val) *val = ps->prop_values[i];
        }
        if (p) drmModeFreeProperty(p);
    }
    if (ps) drmModeFreeObjectProperties(ps);
    return id;
}

static int plane_has_format(const drmModePlane *p, uint32_t fmt) {
    for (uint32_t i = 0; i < p->count_formats; i++)
        if (p->formats[i] == fmt) return 1;
    return 0;
}

static void page_flip_handler(int fd, unsigned seq, unsigned sec, unsigned usec, void *data) {
    (void)fd;
    struct qw_output *o = data;
    o->flip_pending = 0;
    o->last_seq = seq;
    o->last_vblank_ms = sec * 1e3 + usec / 1e3;
}

struct fbinfo { int fd; uint32_t fb; };
static void bo_destroy(struct gbm_bo *bo, void *data) {
    (void)bo;
    struct fbinfo *f = data;
    drmModeRmFB(f->fd, f->fb);
    free(f);
}
static uint32_t bo_fb(int fd, struct gbm_bo *bo) {
    struct fbinfo *f = gbm_bo_get_user_data(bo);
    if (f) return f->fb;
    f = calloc(1, sizeof *f);
    if (!f) return 0;
    uint32_t h[4] = { gbm_bo_get_handle(bo).u32 }, p[4] = { gbm_bo_get_stride(bo) }, off[4] = { 0 };
    if (drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), DRM_FORMAT_XRGB8888, h, p, off,
                      &f->fb, 0)) {
        free(f);
        return 0;
    }
    f->fd = fd;
    gbm_bo_set_user_data(bo, f, bo_destroy);
    return f->fb;
}

#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); qw_output_close(o); return -1; } while (0)

static int open_common(struct qw_output *o, const uint32_t *exclude, int nexclude, char *err, size_t errlen);

int qw_output_open(struct qw_output *o, char *err, size_t errlen) {
    memset(o, 0, sizeof *o);
    o->dpy = EGL_NO_DISPLAY;
    o->ctx = EGL_NO_CONTEXT;
    o->surf = EGL_NO_SURFACE;
    o->fd_owned = 1;
    o->zpos = -1;
    o->fd = open_vc4(o->card, sizeof o->card);
    if (o->fd < 0) FAIL("no vc4 DRM card");
    if (!drmIsMaster(o->fd) && drmSetMaster(o->fd))
        FAIL("%s: not DRM master (another program holds the display)", o->card);
    return open_common(o, NULL, 0, err, errlen);
}

int qw_output_open_fd(struct qw_output *o, int fd, const uint32_t *exclude, int nexclude, int zpos,
                      char *err, size_t errlen) {
    memset(o, 0, sizeof *o);
    o->dpy = EGL_NO_DISPLAY;
    o->ctx = EGL_NO_CONTEXT;
    o->surf = EGL_NO_SURFACE;
    o->fd = fd;
    o->fd_owned = 0;
    o->zpos = zpos;
    snprintf(o->card, sizeof o->card, "fd %d", fd);
    return open_common(o, exclude, nexclude, err, errlen);
}

static int open_common(struct qw_output *o, const uint32_t *exclude, int nexclude, char *err, size_t errlen) {
    if (drmSetClientCap(o->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) FAIL("no universal planes");
    if (drmSetClientCap(o->fd, DRM_CLIENT_CAP_ATOMIC, 1)) FAIL("no atomic modesetting");

    // the primary plane carries the fbdev framebuffer (fb0) on the active CRTC
    drmModeRes *res = drmModeGetResources(o->fd);
    drmModePlaneRes *pr = drmModeGetPlaneResources(o->fd);
    if (!res || !pr) {
        if (res) drmModeFreeResources(res);
        if (pr) drmModeFreePlaneResources(pr);
        FAIL("drmModeGetResources: %m");
    }
    int crtc_index = -1;
    for (uint32_t i = 0; i < pr->count_planes && !o->crtc; i++) {
        drmModePlane *p = drmModeGetPlane(o->fd, pr->planes[i]);
        if (!p) continue;
        uint64_t type = 0;
        prop_id(o->fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type);
        if (type == DRM_PLANE_TYPE_PRIMARY && p->crtc_id && p->fb_id) {
            o->crtc = p->crtc_id;
            o->primary_plane = p->plane_id;
            o->primary_fb = p->fb_id;
        }
        drmModeFreePlane(p);
    }
    for (int i = 0; i < res->count_crtcs; i++)
        if (res->crtcs[i] == o->crtc) crtc_index = i;
    drmModeFreeResources(res);
    int nov = 0;
    for (uint32_t i = 0; i < pr->count_planes && nov < QW_NPANEL && crtc_index >= 0; i++) {
        drmModePlane *p = drmModeGetPlane(o->fd, pr->planes[i]);
        if (!p) continue;
        uint64_t type = 0;
        prop_id(o->fd, p->plane_id, DRM_MODE_OBJECT_PLANE, "type", &type);
        int excluded = 0;
        for (int e = 0; e < nexclude; e++) excluded |= exclude[e] == p->plane_id;
        if (type == DRM_PLANE_TYPE_OVERLAY && !p->fb_id && !excluded && (p->possible_crtcs & (1u << crtc_index)) &&
            plane_has_format(p, DRM_FORMAT_XRGB8888))
            o->ov[nov++] = p->plane_id;
        drmModeFreePlane(p);
    }
    drmModeFreePlaneResources(pr);
    if (!o->crtc) FAIL("no active CRTC with a framebuffer on its primary plane");
    if (nov < QW_NPANEL) FAIL("only %d free XR24 overlay planes, need %d", nov, QW_NPANEL);

    for (int k = 0; k < QW_NPANEL; k++) {
        struct qw_output_plane_props *pp = &o->props[k];
        uint32_t id = o->ov[k];
#define P(field, name) \
        if (!(pp->field = prop_id(o->fd, id, DRM_MODE_OBJECT_PLANE, name, NULL))) FAIL("plane %u: no %s", id, name)
        P(fb_id, "FB_ID"); P(crtc_id, "CRTC_ID");
        P(src_x, "SRC_X"); P(src_y, "SRC_Y"); P(src_w, "SRC_W"); P(src_h, "SRC_H");
        P(crtc_x, "CRTC_X"); P(crtc_y, "CRTC_Y"); P(crtc_w, "CRTC_W"); P(crtc_h, "CRTC_H");
        pp->zpos = o->zpos >= 0 ? prop_id(o->fd, id, DRM_MODE_OBJECT_PLANE, "zpos", NULL) : 0;
#undef P
    }

    // GBM + EGL
    o->gbm = gbm_create_device(o->fd);
    if (!o->gbm) FAIL("gbm_create_device failed");
    o->gs = gbm_surface_create(o->gbm, SW, SH, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!o->gs) FAIL("gbm_surface_create %dx%d failed", SW, SH);
    o->dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, o->gbm, NULL);
    if (o->dpy == EGL_NO_DISPLAY || !eglInitialize(o->dpy, NULL, NULL)) {
        o->dpy = EGL_NO_DISPLAY;
        FAIL("eglInitialize failed");
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cattr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_NONE };
    EGLConfig cfgs[64];
    EGLint n = 0;
    eglChooseConfig(o->dpy, cattr, cfgs, 64, &n);
    for (int i = 0; i < n && !o->cfg; i++) {
        EGLint vis;
        eglGetConfigAttrib(o->dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vis);
        if (vis == GBM_FORMAT_XRGB8888) o->cfg = cfgs[i];
    }
    if (!o->cfg) FAIL("no XRGB8888 EGL config");
    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    o->ctx = eglCreateContext(o->dpy, o->cfg, EGL_NO_CONTEXT, ctxattr);
    if (o->ctx == EGL_NO_CONTEXT) FAIL("eglCreateContext 0x%x", eglGetError());
    o->surf = eglCreatePlatformWindowSurface(o->dpy, o->cfg, o->gs, NULL);
    if (o->surf == EGL_NO_SURFACE) FAIL("eglCreatePlatformWindowSurface 0x%x", eglGetError());
    if (!eglMakeCurrent(o->dpy, o->surf, o->surf, o->ctx)) FAIL("eglMakeCurrent 0x%x", eglGetError());
    eglSwapInterval(o->dpy, 0); // pacing comes from the page flip
    GLint mrb, mvp[2];
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &o->max_tex);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &mrb);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, mvp);
    QW_INFO("%s: crtc %u, primary plane %u fb %u, overlays %u %u %u; GL %s, max tex %d rb %d viewport %dx%d",
            o->card, o->crtc, o->primary_plane, o->primary_fb, o->ov[0], o->ov[1], o->ov[2],
            (const char *)glGetString(GL_RENDERER), o->max_tex, mrb, mvp[0], mvp[1]);
    if (mvp[0] < SW || mvp[1] < SH || mrb < SH) FAIL("render target limits too small for %dx%d", SW, SH);
    return 0;
}

static int add(drmModeAtomicReq *r, uint32_t obj, uint32_t prop, uint64_t v) {
    return drmModeAtomicAddProperty(r, obj, prop, v) < 0 ? -1 : 0;
}

static int read_events(struct qw_output *o, char *err, size_t errlen) {
    drmEventContext ev = { .version = 2, .page_flip_handler = page_flip_handler };
    if (drmHandleEvent(o->fd, &ev)) { snprintf(err, errlen, "drmHandleEvent: %m"); return -1; }
    return 0;
}

static int wait_flip(struct qw_output *o, char *err, size_t errlen) {
    int timeouts = 0;
    while (o->flip_pending) {
        struct pollfd pfd = { .fd = o->fd, .events = POLLIN };
        int rc = poll(&pfd, 1, 500);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) { snprintf(err, errlen, "poll: %m"); return -1; }
        if (rc == 0) {
            if (++timeouts >= 4) { snprintf(err, errlen, "no page flip event for 2 s"); return -1; }
            continue;
        }
        if (read_events(o, err, errlen)) return -1;
    }
    return 0;
}

struct gbm_bo *qw_output_finish_frame(struct qw_output *o, int finish, char *err, size_t errlen) {
    if (finish) glFinish();
    if (!eglSwapBuffers(o->dpy, o->surf)) { snprintf(err, errlen, "eglSwapBuffers 0x%x", eglGetError()); return NULL; }
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(o->gs);
    if (!bo) { snprintf(err, errlen, "gbm_surface_lock_front_buffer failed (no free buffer)"); return NULL; }
    if (!bo_fb(o->fd, bo)) {
        snprintf(err, errlen, "drmModeAddFB2: %m");
        gbm_surface_release_buffer(o->gs, bo);
        return NULL;
    }
    return bo;
}

void qw_output_drop(struct qw_output *o, struct gbm_bo *bo) {
    if (bo) gbm_surface_release_buffer(o->gs, bo);
}

int qw_output_commit(struct qw_output *o, struct gbm_bo *bo, char *err, size_t errlen) {
    if (o->flip_pending) { snprintf(err, errlen, "commit while a flip is pending"); return -1; }
    uint32_t fb = bo_fb(o->fd, bo);
    drmModeAtomicReq *r = drmModeAtomicAlloc();
    int bad = !r || !fb;
    for (int k = 0; k < QW_NPANEL && !bad; k++) {
        const struct qw_output_plane_props *p = &o->props[k];
        uint32_t id = o->ov[k];
        bad |= add(r, id, p->fb_id, fb);
        if (!o->planes_on) {
            bad |= add(r, id, p->crtc_id, o->crtc);
            bad |= add(r, id, p->src_x, 0);
            bad |= add(r, id, p->src_y, (uint64_t)(k * QW_PH) << 16);
            bad |= add(r, id, p->src_w, (uint64_t)QW_PW << 16);
            bad |= add(r, id, p->src_h, (uint64_t)QW_PH << 16);
            bad |= add(r, id, p->crtc_x, k * QW_PW);
            bad |= add(r, id, p->crtc_y, 0);
            bad |= add(r, id, p->crtc_w, QW_PW);
            bad |= add(r, id, p->crtc_h, QW_PH);
            if (p->zpos) bad |= add(r, id, p->zpos, (uint64_t)o->zpos);
        }
    }
    if (!bad && o->extra) o->extra(r, o->extra_ctx);
    int rc = bad ? -1 : drmModeAtomicCommit(o->fd, r, DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK, o);
    if (r) drmModeAtomicFree(r);
    if (rc) {
        snprintf(err, errlen, "atomic commit: %s", bad ? "request setup failed" : strerror(errno));
        return -1;
    }
    o->planes_on = 1;
    o->flip_pending = 1;
    o->queued = bo;
    o->queued_commit_ms = qw_now_ms();
    return 0;
}

static void flip_done(struct qw_output *o, struct qw_flip_info *fi) {
    if (o->shown) gbm_surface_release_buffer(o->gs, o->shown);
    o->shown = o->queued;
    o->queued = NULL;
    if (fi) {
        fi->seq = o->last_seq;
        fi->vblank_ms = o->last_vblank_ms;
        fi->commit_ms = o->queued_commit_ms;
    }
}

int qw_output_dispatch(struct qw_output *o, struct qw_flip_info *fi, char *err, size_t errlen) {
    if (!o->flip_pending) {
        // nothing expected; still drain the fd
        if (read_events(o, err, errlen)) return -1;
        return 0;
    }
    if (read_events(o, err, errlen)) return -1;
    if (o->flip_pending) return 0;
    flip_done(o, fi);
    return 1;
}

int qw_output_present(struct qw_output *o, volatile sig_atomic_t *stop, int finish,
                      struct qw_flip_info *fi, char *err, size_t errlen) {
    (void)stop;
    double t0 = qw_now_ms();
    struct gbm_bo *bo = qw_output_finish_frame(o, finish, err, errlen);
    if (!bo) return -1;
    double t1 = qw_now_ms();
    if (qw_output_commit(o, bo, err, errlen)) { qw_output_drop(o, bo); return -1; }
    if (wait_flip(o, err, errlen)) return -1; // the buffer may be on screen: released at close
    flip_done(o, fi);
    if (fi) fi->swap_ms = t1 - t0;
    return 0;
}

void qw_output_close(struct qw_output *o) {
    if (o->fd >= 0 && o->planes_on) {
        char e[64];
        if (o->flip_pending) wait_flip(o, e, sizeof e);
        drmModeAtomicReq *r = drmModeAtomicAlloc();
        for (int k = 0; k < QW_NPANEL; k++) {
            add(r, o->ov[k], o->props[k].fb_id, 0);
            add(r, o->ov[k], o->props[k].crtc_id, 0);
        }
        if (drmModeAtomicCommit(o->fd, r, 0, NULL))
            QW_WARN("disabling the overlay planes failed: %m (closing the card restores fb0)");
        drmModeAtomicFree(r);
        o->planes_on = 0;
    }
    if (o->queued) { gbm_surface_release_buffer(o->gs, o->queued); o->queued = NULL; }
    if (o->shown) { gbm_surface_release_buffer(o->gs, o->shown); o->shown = NULL; }
    if (o->dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(o->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (o->surf != EGL_NO_SURFACE) eglDestroySurface(o->dpy, o->surf);
        if (o->ctx != EGL_NO_CONTEXT) eglDestroyContext(o->dpy, o->ctx);
        eglTerminate(o->dpy);
    }
    o->dpy = EGL_NO_DISPLAY;
    o->surf = EGL_NO_SURFACE;
    o->ctx = EGL_NO_CONTEXT;
    if (o->gs) { gbm_surface_destroy(o->gs); o->gs = NULL; }
    if (o->gbm) { gbm_device_destroy(o->gbm); o->gbm = NULL; }
    if (o->fd >= 0 && o->fd_owned) close(o->fd);
    o->fd = -1;
}
