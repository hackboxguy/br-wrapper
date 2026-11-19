// libqvuewarp: realtime mesh warp for the 3x QVue 4500x298 HDMI output.
//
// The pieces that do not depend on where the source pixels come from:
//   - calibration loading            (calib.c)
//   - forward mesh generation         (mesh.c)   plan section 4
//   - the GL program and mesh draw    (render.c)
//   - the stacked output stage        (output.c) one 1500x894 GBM buffer,
//     three panels stacked, scanned out by three overlay planes
//
// qvue-warpd adds the fb0 source; the video player can add its own source later.
#ifndef QVUEWARP_H
#define QVUEWARP_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <xf86drmMode.h>

#define QW_NPANEL 3
#define QW_PW 1500
#define QW_PH 298
#define QW_FW (QW_NPANEL * QW_PW)

// ------------------------------------------------------------------ logging
// Printed to stderr (the journal under systemd). qw_verbose enables QW_DBG.
extern int qw_verbose;
void qw_log(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define QW_INFO(...) qw_log("info", __VA_ARGS__)
#define QW_WARN(...) qw_log("warn", __VA_ARGS__)
#define QW_ERR(...) qw_log("error", __VA_ARGS__)
#define QW_DBG(...) do { if (qw_verbose) qw_log("debug", __VA_ARGS__); } while (0)
double qw_now_ms(void); // CLOCK_MONOTONIC

// ------------------------------------------------------------------ calibration
// File format: '#' comment lines, then gw*gh lines "dst_x dst_y" (panel pixels,
// origin top-left), row-major over a regular gw x gh source grid, top-left point
// first. ',' and ';' are accepted as separators. The grid size comes from a
// "# grid 21x9" comment, otherwise from the file name ("...-21x9.txt").
// reverse=1 reverses the point order (the CES25 files start bottom-right).
struct qw_calib {
    int gw, gh;
    float *pts; // gw*gh*2: x, y
};
int qw_calib_load(const char *path, int reverse, struct qw_calib *out, char *err, size_t errlen);
void qw_calib_free(struct qw_calib *c);
// destination for continuous grid coordinates (fi in [0,gw-1], fj in [0,gh-1]),
// bicubic Catmull-Rom with linear extrapolation past the grid edge
void qw_calib_eval(const struct qw_calib *c, float fi, float fj, float *x, float *y);

// ------------------------------------------------------------------ mesh
// Which source pixels a panel shows, and its orientation. Coordinates are in
// source (fb) pixels; the default is (k*1500, 0, 1500, 298).
struct qw_panel_map {
    int flip_x, flip_y;
    int src_x, src_y, src_w, src_h;
};
// The canvas mirror (warp.conf mirror_x / mirror_y): the map a panel gets when
// the whole 4500x298 source canvas is mirrored before the per-panel keys
// apply. The source rectangle moves to its mirror position on the canvas and
// the panel's flip toggles: with the default maps, mirror_x gives panel 1 the
// columns of panel 3 right to left (a true mirror image of the whole canvas).
// The calibration and the trim stay with the physical panel.
void qw_panel_map_mirror(const struct qw_panel_map *in, int mirror_x, int mirror_y, struct qw_panel_map *out);
// Vertex: ndc_x, ndc_y (destination pixel centre inside the panel viewport),
// sx, sy (source pixel centre, absolute source pixel coordinates).
struct qw_mesh {
    int mw, mh;
    float *v;   // mw*mh*4
};
// The same with a fractional source mapping: grid u in [0,1] -> source pixel
// centre x0 + u*dx (after flips), v -> y0 + v*dy. qw_mesh_build uses
// x0 = src_x, dx = src_w - 1 (pixel centre to pixel centre).
struct qw_src_affine {
    float x0, dx, y0, dy;
    int flip_x, flip_y;
};
int qw_mesh_build_affine(const struct qw_calib *calib, const struct qw_src_affine *map, int subdiv,
                         struct qw_mesh *out, char *err, size_t errlen);
// calib == NULL builds the identity mesh (2x2, pixel edge to pixel edge) used
// for the self-check. subdiv = Catmull-Rom steps per grid cell.
int qw_mesh_build(const struct qw_calib *calib, const struct qw_panel_map *map, int subdiv,
                  struct qw_mesh *out, char *err, size_t errlen);
void qw_mesh_free(struct qw_mesh *m);

// ------------------------------------------------------------------ trim (plan section 6)
// Per panel a 3x3 lattice of handles over the grid, each a (dx, dy) in panel
// pixels: tl t tr / l c r / bl b br (index = row*3 + col). The displacement of
// grid node (u, v) is the quadratic Lagrange interpolation of the nine handles,
// added to the calibration point. Adding the same offset to all nine handles
// moves the whole panel (the basis sums to 1).
#define QW_NHANDLE 9
extern const char *const qw_handle_names[QW_NHANDLE];
int qw_handle_index(const char *name); // -1 if unknown
struct qw_trim {
    float h[QW_NPANEL][QW_NHANDLE][2];
};
void qw_trim_displacement(const float h[QW_NHANDLE][2], float u, float v, float *dx, float *dy);
// out = in with the trim displacement added to every grid node
int qw_calib_trimmed(const struct qw_calib *in, const float h[QW_NHANDLE][2], struct qw_calib *out);
// JSON file {"version":1,"panels":[{"handles":[[dx,dy] x 9]} x 3]}, handles in
// qw_handle_names order. load: 1 = file missing (trim zeroed), -1 = error.
int qw_trim_load(const char *path, struct qw_trim *t, char *err, size_t errlen);
int qw_trim_save(const char *path, const struct qw_trim *t, char *err, size_t errlen); // atomic
int qw_trim_is_zero(const struct qw_trim *t);

// ------------------------------------------------------------------ output stage
struct qw_output_plane_props {
    uint32_t fb_id, crtc_id, src_x, src_y, src_w, src_h, crtc_x, crtc_y, crtc_w, crtc_h, zpos;
};
struct qw_flip_info {
    unsigned seq;         // vblank sequence of the flip
    double vblank_ms;     // kernel vblank timestamp (CLOCK_MONOTONIC ms)
    double commit_ms;     // when the commit of the flipped frame was issued
    double swap_ms;       // time spent in eglSwapBuffers + lock (present only)
};
struct qw_output {
    int fd;
    char card[32];
    uint32_t crtc, primary_plane, primary_fb, ov[QW_NPANEL];
    struct qw_output_plane_props props[QW_NPANEL];
    struct gbm_device *gbm;
    struct gbm_surface *gs;
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface surf;
    struct gbm_bo *shown;      // on screen
    struct gbm_bo *queued;     // committed, flip pending
    int planes_on;
    int flip_pending;
    unsigned last_seq;
    double last_vblank_ms;
    double queued_commit_ms;
    GLint max_tex;
    int fd_owned;              // 0: the caller's fd (qw_output_open_fd), not closed
    int zpos;                  // >= 0: set on the warp planes (if they have zpos)
    // extra properties for every commit (another plane of the caller, e.g. a popup)
    void (*extra)(drmModeAtomicReq *req, void *ctx);
    void *extra_ctx;
};
// Open the vc4 card (by driver name), become DRM master (first opener), find
// the CRTC, the primary plane's fb and three free XR24 overlay planes, create
// the 1500x894 GBM surface and a current GLES 3 context on it.
int qw_output_open(struct qw_output *o, char *err, size_t errlen);
// On a DRM fd the caller already holds as master (a video player): the planes
// in exclude[] are the caller's and never used; zpos >= 0 is set on the three
// warp planes. qw_output_close does not close this fd.
int qw_output_open_fd(struct qw_output *o, int fd, const uint32_t *exclude, int nexclude, int zpos,
                      char *err, size_t errlen);
// Finish the frame drawn into the surface: swap, lock the front buffer, one
// non-blocking atomic commit with a flip event (the first one also sets up the
// three planes), wait for the event, release the previous buffer.
// Returns 0, or -1 with err set (commit failure, lost master, stop requested).
int qw_output_present(struct qw_output *o, volatile sig_atomic_t *stop, int finish,
                      struct qw_flip_info *fi, char *err, size_t errlen);
// The same in steps, for an event loop:
// finish the drawn frame: (glFinish,) swap, lock the front buffer, add its fb
struct gbm_bo *qw_output_finish_frame(struct qw_output *o, int finish, char *err, size_t errlen);
// non-blocking atomic commit of a finished frame with a flip event (no flip may be pending)
int qw_output_commit(struct qw_output *o, struct gbm_bo *bo, char *err, size_t errlen);
// give back a finished frame that will not be committed
void qw_output_drop(struct qw_output *o, struct gbm_bo *bo);
// read DRM events (call when o->fd is readable). Returns 1 and fills fi when a
// flip completed (the previously shown buffer is released), 0 if not, -1 on error.
int qw_output_dispatch(struct qw_output *o, struct qw_flip_info *fi, char *err, size_t errlen);
// Disable the planes (fb0 shows again), destroy EGL/GBM and close the card.
// Safe on a partly opened output.
void qw_output_close(struct qw_output *o);

// ------------------------------------------------------------------ renderer
// What a panel samples. RGB: one RGBA texture (the daemon's fb0 path, no
// bounds test). The others carry the whole frame's size (uFrame) and show
// black outside it; YUV is converted with the renderer's matrix.
enum qw_src_fmt {
    QW_SRC_RGB,        // tex0 RGBA
    QW_SRC_RGB_FRAME,  // tex0 RGBA, black outside the frame (images)
    QW_SRC_OES,        // tex0 external (EGLImage, e.g. an NV12 dmabuf; the driver converts)
    QW_SRC_NV12,       // tex0 Y (R8), tex1 UV (RG8, whole frame)
    QW_SRC_I420,       // tex0 Y (R8), tex1 U, tex2 V (R8, whole frame)
    QW_SRC_NFMT
};
struct qw_source {
    enum qw_src_fmt fmt;
    GLuint tex[3];
    float tex_x, tex_w, tex_h;   // tex0's origin in frame pixels and its size
    float frame_w, frame_h;      // the whole frame (chroma planes, black outside)
};
// YUV -> RGB: enc 0 BT.601, 1 BT.709, 2 BT.2020; full = full range
void qw_yuv_matrix(int enc, int full, float m[9], float off[3]);

struct qw_src_prog { GLuint prog; GLint u_tex, u_tex1, u_tex2, u_origin, u_inv, u_frame, u_m, u_off; };

struct qw_renderer {
    GLuint prog, ibo, vbo[QW_NPANEL];
    GLint u_tex, u_origin, u_inv;
    struct qw_src_prog sp[QW_SRC_NFMT];   // sp[QW_SRC_RGB] is prog
    struct qw_source src[QW_NPANEL];
    float yuv_m[9], yuv_off[3];
    int nidx[QW_NPANEL];
    GLuint tex[QW_NPANEL];          // source texture per panel (not owned)
    float tex_x[QW_NPANEL];         // source x of the texture's column 0
    float tex_w[QW_NPANEL], tex_h[QW_NPANEL];
    // tuning overlay: per panel lines and triangles, vertices x, y (NDC), r, g, b
    GLuint oprog, ovbo[QW_NPANEL];
    int oline[QW_NPANEL], otri[QW_NPANEL]; // vertex counts
};
int qw_renderer_init(struct qw_renderer *r, char *err, size_t errlen);
// Upload a panel mesh and say which source texture it samples: texture column 0
// is source x = tex_x, the texture is tex_w x tex_h.
int qw_renderer_set_panel(struct qw_renderer *r, int k, const struct qw_mesh *m,
                          GLuint tex, int tex_x, int tex_w, int tex_h);
// The mesh alone, and the source alone (any format). -1 if the format's
// program is not available (QW_SRC_OES without the external-image extension).
int qw_renderer_set_mesh(struct qw_renderer *r, int k, const struct qw_mesh *m);
int qw_renderer_set_source(struct qw_renderer *r, int k, const struct qw_source *s);
void qw_renderer_set_yuv(struct qw_renderer *r, int enc, int full);
// a panel draws nothing (black)
void qw_renderer_clear_panel(struct qw_renderer *r, int k);
// Draw all panels into the current 1500x894 surface (panel k at buffer rows
// k*298 .. k*298+297, top-down), black outside the meshes.
void qw_renderer_draw(struct qw_renderer *r);
// Read the rendered buffer back as a 4500x298 RGB image (panels side by side).
// Call before qw_output_present. rgb must hold 4500*298*3 bytes.
void qw_renderer_readback(uint8_t *rgb);
void qw_renderer_free(struct qw_renderer *r);
// Overlay of panel k: nline line vertices followed by ntri triangle vertices,
// 5 floats each. nline = ntri = 0 removes it.
int qw_renderer_set_overlay(struct qw_renderer *r, int k, const float *v, int nline, int ntri);

// ------------------------------------------------------------------ overlay geometry
// Builds the tuning overlay of one panel from its trimmed calibration: the grid
// lines (yellow) and markers on the handles: sel = handle index 0..8 (that one
// big and magenta, the others small cyan), QW_NHANDLE = all handles marked as
// selected (panel / all selection), -1 = no markers. *v is malloc'ed.
int qw_overlay_build(const struct qw_calib *trimmed, int subdiv, int sel, float **v, int *nline, int *ntri);

#endif
