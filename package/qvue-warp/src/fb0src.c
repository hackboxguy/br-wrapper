// fb0 source: the fbdev framebuffer on the primary plane, imported zero-copy
// as dmabuf EGLImages.
//
// The 4500-wide buffer cannot be one texture (V3D max 4096), and Mesa rejects
// an import whose offset + pitch*height exceeds the buffer size. So:
//   - if the buffer has spare rows for three per-panel imports at byte offsets
//     k*6000, use those;
//   - otherwise two overlapping imports [0, max) and [4500-max, 4500), which fit
//     in the page-rounding slack of the 4500x298 XR24 buffer (440 px);
//   - otherwise fail.
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "qvuewarp.h"
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "fb0src.h"

int fb0src_open(struct fb0src *s, struct qw_output *o, char *err, size_t errlen) {
    memset(s, 0, sizeof *s);
    s->dmabuf = -1;
    s->dpy = o->dpy;
    PFNEGLCREATEIMAGEKHRPROC create = (void *)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    s->destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
    if (!create || !target || !s->destroy_image) {
        snprintf(err, errlen, "no EGLImage entry points");
        return -1;
    }

    drmModeFB2 *fb = drmModeGetFB2(o->fd, o->primary_fb);
    if (!fb) { snprintf(err, errlen, "GETFB2 fb %u: %m", o->primary_fb); return -1; }
    s->fb_id = o->primary_fb;
    s->pitch = fb->pitches[0];
    uint32_t handle = fb->handles[0], fmt = fb->pixel_format, w = fb->width, h = fb->height;
    drmModeFreeFB2(fb);
    if (!handle) {
        snprintf(err, errlen, "GETFB2 gave no buffer handle (needs DRM master or CAP_SYS_ADMIN)");
        return -1;
    }
    if (w != QW_FW || h != QW_PH || fmt != DRM_FORMAT_XRGB8888) {
        snprintf(err, errlen, "fb0 is %ux%u %.4s, expected %dx%d XR24", w, h, (char *)&fmt, QW_FW, QW_PH);
        drmCloseBufferHandle(o->fd, handle);
        return -1;
    }
    int rc = drmPrimeHandleToFD(o->fd, handle, O_CLOEXEC | O_RDWR, &s->dmabuf);
    drmCloseBufferHandle(o->fd, handle);
    if (rc) { snprintf(err, errlen, "PRIME export: %m"); s->dmabuf = -1; return -1; }

    s->size = lseek(s->dmabuf, 0, SEEK_END);
    long slack_px = ((long)s->size - (long)s->pitch * QW_PH) / 4;
    int mts = o->max_tex;
    if (slack_px >= (QW_NPANEL - 1) * QW_PW && mts >= QW_PW) {
        s->ntex = QW_NPANEL;
        for (int k = 0; k < QW_NPANEL; k++) { s->tex_x[k] = k * QW_PW; s->tex_w[k] = QW_PW; }
    } else if (mts >= QW_FW) {
        s->ntex = 1;
        s->tex_x[0] = 0; s->tex_w[0] = QW_FW;
    } else if (QW_FW - mts <= slack_px && 2 * mts >= QW_FW) {
        s->ntex = 2;
        s->tex_x[0] = 0; s->tex_w[0] = mts;
        s->tex_x[1] = QW_FW - mts; s->tex_w[1] = mts;
    } else {
        snprintf(err, errlen, "fb0 dmabuf has %ld px slack, too little for the texture split (max tex %d)",
                 slack_px, mts);
        return -1;
    }

    glGenTextures(s->ntex, s->tex);
    for (int k = 0; k < s->ntex; k++) {
        EGLint ia[] = { EGL_WIDTH, s->tex_w[k], EGL_HEIGHT, QW_PH,
                        EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
                        EGL_DMA_BUF_PLANE0_FD_EXT, s->dmabuf,
                        EGL_DMA_BUF_PLANE0_OFFSET_EXT, s->tex_x[k] * 4,
                        EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)s->pitch, EGL_NONE };
        s->img[k] = create(o->dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, ia);
        if (s->img[k] == EGL_NO_IMAGE_KHR) {
            snprintf(err, errlen, "dmabuf import of texture %d (x %d, w %d) failed 0x%x", k, s->tex_x[k],
                     s->tex_w[k], eglGetError());
            fb0src_close(s);
            return -1;
        }
        glBindTexture(GL_TEXTURE_2D, s->tex[k]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        target(GL_TEXTURE_2D, s->img[k]);
        if (glGetError() != GL_NO_ERROR) {
            snprintf(err, errlen, "binding EGLImage %d to a texture failed", k);
            fb0src_close(s);
            return -1;
        }
    }
    QW_INFO("fb0: fb %u, pitch %u, dmabuf %ld bytes, slack %ld px, %d source texture(s)", s->fb_id, s->pitch,
            (long)s->size, slack_px, s->ntex);
    return 0;
}

void fb0src_set_columns(struct fb0src *s, const struct qw_panel_map map[QW_NPANEL]) {
    for (int k = 0; k < s->ntex; k++) { s->cx0[k] = s->tex_w[k]; s->cx1[k] = 0; }
    for (int p = 0; p < QW_NPANEL; p++) {
        int t = fb0src_texture_for(s, map[p].src_x, map[p].src_w);
        if (t < 0) continue;
        int x0 = map[p].src_x - s->tex_x[t] - 2, x1 = map[p].src_x + map[p].src_w - s->tex_x[t] + 2;
        if (x0 < 0) x0 = 0;
        if (x1 > s->tex_w[t]) x1 = s->tex_w[t];
        if (x0 < s->cx0[t]) s->cx0[t] = x0;
        if (x1 > s->cx1[t]) s->cx1[t] = x1;
    }
    // A used texture is blitted whole: a full-image blit takes V3D's fast
    // raster-to-tiled path (~3 ms for both textures); a column range falls
    // back to a shader blit that samples the raster buffer (measured 29-32 ms
    // per frame with the default maps, and max diff 3 in the identity check,
    // 2026-11-19). A texture no panel samples is skipped. QW_COPY_COLUMNS=1
    // restores the column-range blit for measurements.
    if (!getenv("QW_COPY_COLUMNS"))
        for (int k = 0; k < s->ntex; k++)
            if (s->cx1[k] > s->cx0[k]) { s->cx0[k] = 0; s->cx1[k] = s->tex_w[k]; }
    if (s->copy)
        for (int k = 0; k < s->ntex; k++)
            QW_INFO("fb0: texture %d now blits columns %d..%d", k, s->cx0[k], s->cx1[k] - 1);
}

int fb0src_enable_copy(struct fb0src *s, const struct qw_panel_map map[QW_NPANEL]) {
    fb0src_set_columns(s, map);
    glGenTextures(s->ntex, s->ctex);
    glGenFramebuffers(s->ntex, s->rfbo);
    glGenFramebuffers(s->ntex, s->dfbo);
    for (int k = 0; k < s->ntex; k++) {
        glBindTexture(GL_TEXTURE_2D, s->ctex[k]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, s->tex_w[k], QW_PH);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s->rfbo[k]);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex[k], 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->dfbo[k]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->ctex[k], 0);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
            glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            QW_ERR("copy mode: framebuffer %d incomplete", k);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return -1;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s->copy = 1;
    for (int k = 0; k < s->ntex; k++)
        QW_INFO("fb0: texture %d (fb0 x %d..%d) blits columns %d..%d into a tiled copy every frame (copy_source)",
                k, s->tex_x[k], s->tex_x[k] + s->tex_w[k] - 1, s->cx0[k], s->cx1[k] - 1);
    return 0;
}

void fb0src_copy(struct fb0src *s) {
    if (!s->copy) return;
    for (int k = 0; k < s->ntex; k++) {
        if (s->cx1[k] <= s->cx0[k]) continue; // no panel samples this texture
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s->rfbo[k]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->dfbo[k]);
        glBlitFramebuffer(s->cx0[k], 0, s->cx1[k], QW_PH, s->cx0[k], 0, s->cx1[k], QW_PH, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint fb0src_tex(const struct fb0src *s, int k) { return s->copy ? s->ctex[k] : s->tex[k]; }

int fb0src_texture_for(const struct fb0src *s, int x0, int w) {
    for (int k = 0; k < s->ntex; k++)
        if (x0 >= s->tex_x[k] && x0 + w <= s->tex_x[k] + s->tex_w[k]) return k;
    return -1;
}

int fb0src_read(const struct fb0src *s, uint8_t *rgb) {
    uint8_t *m = mmap(NULL, (size_t)s->pitch * QW_PH, PROT_READ, MAP_SHARED, s->dmabuf, 0);
    if (m == MAP_FAILED) return -1;
    for (int y = 0; y < QW_PH; y++)
        for (int x = 0; x < QW_FW; x++) {
            const uint8_t *i = m + (size_t)y * s->pitch + 4 * x; // XRGB8888 little endian: B G R X
            uint8_t *o = rgb + 3 * ((size_t)y * QW_FW + x);
            o[0] = i[2]; o[1] = i[1]; o[2] = i[0];
        }
    munmap(m, (size_t)s->pitch * QW_PH);
    return 0;
}

void fb0src_close(struct fb0src *s) {
    if (s->copy) {
        glDeleteFramebuffers(s->ntex, s->rfbo);
        glDeleteFramebuffers(s->ntex, s->dfbo);
        glDeleteTextures(s->ntex, s->ctex);
        s->copy = 0;
    }
    for (int k = 0; k < QW_NPANEL; k++)
        if (s->img[k] && s->img[k] != EGL_NO_IMAGE_KHR) { s->destroy_image(s->dpy, s->img[k]); s->img[k] = NULL; }
    if (s->ntex) glDeleteTextures(s->ntex, s->tex);
    s->ntex = 0;
    if (s->dmabuf >= 0) close(s->dmabuf);
    s->dmabuf = -1;
}
