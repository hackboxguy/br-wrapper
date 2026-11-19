#ifndef QVUE_WARPD_FB0SRC_H
#define QVUE_WARPD_FB0SRC_H

#include <sys/types.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "qvuewarp.h"

struct fb0src {
    int dmabuf;
    uint32_t fb_id, pitch;
    off_t size;
    int ntex;
    int tex_x[QW_NPANEL], tex_w[QW_NPANEL];
    GLuint tex[QW_NPANEL];
    EGLImageKHR img[QW_NPANEL];
    // copy mode (experiment): fb0 is blitted into tiled textures each frame
    int copy;
    GLuint ctex[QW_NPANEL], rfbo[QW_NPANEL], dfbo[QW_NPANEL];
    int cx0[QW_NPANEL], cx1[QW_NPANEL]; // columns blitted per texture, [cx0, cx1), texture coordinates
    EGLDisplay dpy;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image;
};

// Import the primary plane's framebuffer of an opened output (context current).
int fb0src_open(struct fb0src *s, struct qw_output *o, char *err, size_t errlen);
// copy mode: allocate tiled copies of the imported textures (after open);
// only the columns the panel maps sample (plus a bilinear margin) are blitted
int fb0src_enable_copy(struct fb0src *s, const struct qw_panel_map map[QW_NPANEL]);
// copy mode: the columns to blit for these maps (after a mirror change)
void fb0src_set_columns(struct fb0src *s, const struct qw_panel_map map[QW_NPANEL]);
// copy mode: blit fb0 into the copies (once per frame, before drawing)
void fb0src_copy(struct fb0src *s);
// texture to sample for index k (the copy in copy mode)
GLuint fb0src_tex(const struct fb0src *s, int k);
// Index of the texture that holds source columns [x0, x0+w), or -1.
int fb0src_texture_for(const struct fb0src *s, int x0, int w);
// Copy fb0 into a 4500x298 RGB image (for --dump-src / --selfcheck).
int fb0src_read(const struct fb0src *s, uint8_t *rgb);
void fb0src_close(struct fb0src *s);

#endif
