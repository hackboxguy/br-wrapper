/* warp.c - the 3x QVue realtime warp, done by the player itself (dual-video-player)
 *
 * On the 3x QVue the daemon qvue-warpd pre-distorts the HDMI picture so its
 * windshield reflection looks straight. The launcher suspends it while this
 * player runs (the player takes the display); with the warp setting on, the
 * player then warps its own frames through libqvuewarp: the same calibration,
 * trim.json and output stage (one 1500x894 buffer, three overlay planes).
 *
 * Decided once at start (warp_gate): the daemon says enabled=1 (or --warp=on),
 * one display of 4500x298, built with libqvuewarp. Otherwise nothing here runs
 * and every frame takes the plane path, unchanged.
 *
 * How a frame becomes the picture (chosen per frame, logged when it changes):
 *   (a) flat hw video (NV12 dmabuf, up to 1920x1088) whose placed area is at
 *       most one panel: zero copy, the dmabuf imported as an external texture
 *   (b) stacked 1500x894 (the three panels one above the other; detected by
 *       size or the playlist's "layout": "stacked"), and flat hw video larger
 *       than one panel: the decoder's NV12 frame mapped and uploaded into
 *       tiled R8 + RG8 textures (sampling the raster dmabuf costs ~6 ms GPU
 *       per panel area on V3D)
 *   (c) 4500x298 H.264, software decoded: I420 uploaded, the Y plane split in
 *       two textures (V3D's limit is 4096); clean at 30 fps, ~56 fps at 60
 *   (d) images (BGRx, scaled to the display by the pipeline): RGBA upload
 * YUV -> RGB happens in the warp shader. Flat content is placed as the plane
 * path places it (fit(), centred), then warped where it lands.
 *
 * Failures (start, upload, commit, flip): the warp stops, its planes go off
 * and the player goes on with its own planes, unwarped.
 */
#include "player.h"

struct warp_opts g_warp_opts = { .mode = "auto", .dump_at = -1 };
gboolean g_warp_formats;

guint64 frame_serial(void)
{
	static guint64 n;
	return __atomic_add_fetch(&n, 1, __ATOMIC_RELAXED);
}

#ifndef HAVE_QVUEWARP

void warp_gate(struct player *p)
{
	(void)p;
	if (!strcmp(g_warp_opts.mode, "on"))
		g_printerr("dual-video-player: warp: built without libqvuewarp\n");
}
gboolean warp_active(const struct player *p) { (void)p; return FALSE; }
gboolean warp_wants_sysmem(const struct media_info *mi, int layout) { (void)mi; (void)layout; return FALSE; }
int warp_commit(struct player *p, const struct frame *f) { (void)p; (void)f; return -1; }
int warp_prepare(struct player *p, const struct frame *f) { (void)p; (void)f; return 0; }
gint64 warp_latest_ns(void) { return GUARD_NS; }
void warp_stop(struct player *p) { (void)p; }

#else

#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "qvuewarp.h"
#include "qw_config.h"
#include <GLES2/gl2ext.h>

#define PANEL_AREA (QW_PW * QW_PH)   /* (a) is measured safe up to one panel's area */
#define NOES 32

enum wpath { WP_NONE, WP_OES, WP_NV12, WP_I420, WP_RGB };
static const char *const wpath_name[] = { "none", "(a) zero-copy dmabuf (external texture)",
					  "(b) NV12 upload", "(c) software I420 upload", "(d) image upload" };

struct texset {
	GLuint t0[2], t1, t2;      /* plane 0 (Y or RGBA) maybe split in two textures */
	int t0x[2], t0w[2], t0n;   /* split: origin and width of each, count */
	int alloc_fmt, alloc_w, alloc_h;
};

struct oes_img {
	GstMemory *mem;
	EGLImageKHR img;
	GLuint tex;
};

struct warp {
	gboolean started;
	struct player *p;
	struct qw_output out;
	gboolean out_open, ren_open;
	struct qw_renderer ren;
	struct warp_config cfg;
	struct qw_calib calib[QW_NPANEL];
	struct qw_trim trim;
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNEGLDESTROYIMAGEKHRPROC destroy_image;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target_tex;

	/* uploaded planes, two sets: the next picture is uploaded into one while
	 * the GPU may still read the other (the pipelined draw, warp_commit) */
	struct texset ts[2];
	int cur;                   /* the set holding the newest picture */

	struct gbm_bo *ready;      /* pipelined: drawn, committed by the next warp_commit */
	struct oes_img oes[NOES];
	GMutex gc_lock;
	GArray *gc;                /* struct oes_img released by GStreamer, deleted here */

	guint64 serial;            /* the picture the textures hold */
	enum wpath path;
	int fw, fh;                /* frame size of that picture */
	/* the writable warp.conf, looked at once a second for mirror changes */
	char conf[PATH_MAX];
	struct timespec conf_mtime;
	gint64 conf_checked;
	int cfg_gen;               /* bumped when the mirror or the panel maps change */
	/* geometry key: rebuild the meshes when it changes */
	int g_w, g_h, g_layout, g_px, g_py, g_pw, g_ph, g_cfg_gen;
	enum wpath g_path;
	float x0[QW_NPANEL], x1[QW_NPANEL];   /* frame columns each panel samples */
	long frames;
	gboolean warned_fps;
	/* QW_WARP_TIMING=1: average times per 120 frames (ms) */
	gboolean timing, finish;   /* QW_WARP_FINISH=1: glFinish before the commit (GPU time in "swap") */
	double t_upd, t_draw, t_swap, t_wait, t_after, t_prep;
	long t_n, t_missed;
	unsigned prev_seq;
	double prev_vbl_ms;
};

static GQuark oes_quark;
static struct warp *g_oes_owner;   /* where released images go (NULL: the warp is gone) */

static void log_w(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void log_w(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	gchar *s = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	g_print("dual-video-player: warp: %s\n", s);
	g_free(s);
}

gboolean warp_active(const struct player *p)
{
	return p->warp != NULL;
}

/* ---- gate ---- */

/* the daemon's status line, or FALSE if no daemon answers */
static gboolean daemon_status(char *buf, size_t len)
{
	const char *path = g_getenv("QVUE_WARP_SOCKET");
	if (!path || !*path)
		path = "/run/qvue-warp/ctl.sock";
	if (access(path, F_OK))
		return FALSE;
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	if (strlen(path) >= sizeof(a.sun_path))
		return FALSE;
	strcpy(a.sun_path, path);
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return FALSE;
	gboolean ok = FALSE;
	if (!connect(fd, (struct sockaddr *)&a, sizeof(a)) && send(fd, "status\n", 7, MSG_NOSIGNAL) == 7) {
		size_t n = 0;
		while (n < len - 1) {
			struct pollfd pfd = { .fd = fd, .events = POLLIN };
			if (poll(&pfd, 1, 1000) <= 0)
				break;
			ssize_t r = recv(fd, buf + n, len - 1 - n, 0);
			if (r <= 0)
				break;
			n += (size_t)r;
			if (memchr(buf, '\n', n))
				break;
		}
		buf[n] = 0;
		ok = n > 0;
	}
	close(fd);
	return ok;
}

void warp_gate(struct player *p)
{
	const char *mode = g_warp_opts.mode;
	gboolean forced = !strcmp(mode, "on");
	if (!strcmp(mode, "off"))
		return;
	if (p->nout != 1 || p->out[0].w != QW_FW || p->out[0].h != QW_PH) {
		if (forced)
			log_w("not used: needs one display of %dx%d (have %d, %dx%d)", QW_FW, QW_PH, p->nout,
			      p->out[0].w, p->out[0].h);
		return;
	}
	if (!forced) {
		char st[512];
		if (!daemon_status(st, sizeof(st)))
			return;   /* no warp daemon: not a 3x QVue, or not installed */
		if (!strstr(st, "enabled=1")) {
			log_w("the warp setting is off: playing unwarped");
			return;
		}
	}
	struct warp *w = g_new0(struct warp, 1);
	w->p = p;
	g_mutex_init(&w->gc_lock);
	w->gc = g_array_new(FALSE, TRUE, sizeof(struct oes_img));
	oes_quark = g_quark_from_static_string("dual-video-player-warp-oes");
	p->warp = w;
	g_warp_formats = TRUE;
	log_w("active (%s): frames are warped by the player", forced ? "--warp=on" : "qvue-warpd: enabled=1");
}

/* ---- start, stop ---- */

static void popup_extra(drmModeAtomicReq *req, void *ctx)
{
	struct player *p = ctx;
	if (!p->popup.id)
		return;
	int px, py;
	popup_rect(p, &px, &py);
	plane_set(req, &p->popup, g_atomic_int_get(&p->popup_on) ? p->popup_fb : 0, p->out[0].crtc_id,
		  POPUP_W, POPUP_H, px, py, POPUP_W, POPUP_H, 2);
}

static void free_set(struct texset *T)
{
	if (T->t0[0] || T->t0[1])
		glDeleteTextures(2, T->t0);
	if (T->t1)
		glDeleteTextures(1, &T->t1);
	if (T->t2)
		glDeleteTextures(1, &T->t2);
	memset(T, 0, sizeof(*T));
}

static void free_textures(struct warp *w)
{
	free_set(&w->ts[0]);
	free_set(&w->ts[1]);
}

static void collect_garbage(struct warp *w)
{
	g_mutex_lock(&w->gc_lock);
	for (guint i = 0; i < w->gc->len; i++) {
		struct oes_img *o = &g_array_index(w->gc, struct oes_img, i);
		if (w->out_open) {
			glDeleteTextures(1, &o->tex);
			w->destroy_image(w->out.dpy, o->img);
		}
		for (int j = 0; j < NOES; j++)
			if (w->oes[j].tex == o->tex && w->oes[j].img == o->img)
				w->oes[j].mem = NULL;
	}
	g_array_set_size(w->gc, 0);
	g_mutex_unlock(&w->gc_lock);
}

static void teardown(struct warp *w)
{
	if (w->ren_open) {
		collect_garbage(w);
		for (int j = 0; j < NOES; j++)
			if (w->oes[j].mem) {
				/* detach: GStreamer may free the memory later */
				gst_mini_object_set_qdata(GST_MINI_OBJECT(w->oes[j].mem), oes_quark, NULL, NULL);
				glDeleteTextures(1, &w->oes[j].tex);
				w->destroy_image(w->out.dpy, w->oes[j].img);
				w->oes[j].mem = NULL;
			}
		free_textures(w);
		qw_renderer_free(&w->ren);
	}
	if (w->ready && w->out_open)
		qw_output_drop(&w->out, w->ready);
	w->ready = NULL;
	if (w->out_open)
		qw_output_close(&w->out);   /* the three planes off; the player's fd stays open */
	w->ren_open = w->out_open = FALSE;
	for (int k = 0; k < QW_NPANEL; k++)
		qw_calib_free(&w->calib[k]);
}

void warp_stop(struct player *p)
{
	struct warp *w = p->warp;
	if (!w)
		return;
	p->warp = NULL;
	g_oes_owner = NULL;
	if (w->started)
		log_w("stopped after %ld frames", w->frames);
	teardown(w);
	g_mutex_clear(&w->gc_lock);
	g_array_unref(w->gc);
	g_free(w);
}

/* failed: off, the player continues with its planes */
static int warp_fail(struct player *p, const char *why)
{
	g_printerr("dual-video-player: warp: %s: playing unwarped from here\n", why);
	warp_stop(p);
	return -1;
}

static int warp_start(struct warp *w)
{
	struct player *p = w->p;
	char err[512];
	if (g_warp_opts.fail && !strcmp(g_warp_opts.fail, "start")) {
		snprintf(err, sizeof(err), "start failure forced (--warp-fail=start)");
		goto fail;
	}
	/* the same files as qvue-warpd: defaults, the writable layer, calibration, trim */
	char dir[PATH_MAX - 32], conf[PATH_MAX], trim[PATH_MAX];
	gboolean have_dir = qw_state_dir(dir, sizeof(dir)) == 0;
	snprintf(conf, sizeof(conf), "%s/warp.conf", dir);
	snprintf(trim, sizeof(trim), "%s/trim.json", dir);
	qw_setup_config(&w->cfg, QW_DATADIR, have_dir ? conf : NULL);
	if (have_dir) {
		struct stat st;
		snprintf(w->conf, sizeof(w->conf), "%s", conf);
		if (!stat(conf, &st))
			w->conf_mtime = st.st_mtim;
		w->conf_checked = g_get_monotonic_time();
	}
	if (qw_setup_calib(&w->cfg, w->calib, err, sizeof(err)))
		goto fail;
	int tr = have_dir ? qw_trim_load(trim, &w->trim, err, sizeof(err)) : 1;
	if (tr < 0) {
		g_printerr("dual-video-player: warp: %s (no trim)\n", err);
		memset(&w->trim, 0, sizeof(w->trim));
	}
	/* the player's own planes (video, EXIT popup) are never the warp's */
	uint32_t excl[3] = { p->out[0].video.id, p->popup.id, p->out[0].primary.id };
	if (qw_output_open_fd(&w->out, p->fd, excl, 3, 1, err, sizeof(err)))
		goto fail;
	w->out_open = TRUE;
	w->out.extra = popup_extra;
	w->out.extra_ctx = p;
	if (qw_renderer_init(&w->ren, err, sizeof(err)))
		goto fail;
	w->ren_open = TRUE;
	w->create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
	w->destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
	w->target_tex = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	if (!w->create_image || !w->destroy_image || !w->target_tex) {
		snprintf(err, sizeof(err), "no EGLImage entry points");
		goto fail;
	}
	log_w("started: overlays %u %u %u, calibration %s.., trim %s, subdiv %d, mirror_x %d, mirror_y %d",
	      w->out.ov[0], w->out.ov[1], w->out.ov[2], w->cfg.calib[0], tr == 0 ? trim : "none", w->cfg.subdiv,
	      w->cfg.mirror_x, w->cfg.mirror_y);
	w->started = TRUE;
	w->timing = g_getenv("QW_WARP_TIMING") != NULL;
	w->finish = g_getenv("QW_WARP_FINISH") != NULL;
	/* the presenter now uploads and renders before each commit: keep it ahead
	 * of the decoder's threads (the launcher's unit allows LimitRTPRIO=10) */
	struct sched_param sp = { .sched_priority = 10 };
	int r = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
	if (r)
		g_printerr("dual-video-player: warp: presenter thread not real-time (%s)\n", strerror(r));
	else
		log_w("presenter thread real-time (SCHED_FIFO %d)", sp.sched_priority);
	return 0;
fail:
	g_printerr("dual-video-player: warp: cannot start: %s\n", err);
	teardown(w);
	return -1;
}

/* ---- textures ---- */

static void tex_params(GLenum target)
{
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint new_tex(GLenum ifmt, int w, int h)
{
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	tex_params(GL_TEXTURE_2D);
	glTexStorage2D(GL_TEXTURE_2D, 1, ifmt, w, h);
	return t;
}

/* (re)allocate the upload textures for @path and a @w x @h frame */
static void alloc_textures(struct warp *w, enum wpath path, int fw, int fh)
{
	struct texset *T = &w->ts[w->cur];
	if (T->alloc_fmt == (int)path && T->alloc_w == fw && T->alloc_h == fh)
		return;
	free_set(T);
	int max = w->out.max_tex;
	T->t0n = fw > max ? 2 : 1;
	T->t0x[0] = 0;
	T->t0w[0] = fw > max ? max : fw;
	T->t0x[1] = fw > max ? fw - max : 0;
	T->t0w[1] = T->t0w[0];
	GLenum f0 = path == WP_RGB ? GL_RGBA8 : GL_R8;
	for (int i = 0; i < T->t0n; i++)
		T->t0[i] = new_tex(f0, T->t0w[i], fh);
	if (path == WP_RGB) {
		/* BGRx in memory, uploaded as RGBA: swap red and blue when sampling */
		for (int i = 0; i < T->t0n; i++) {
			glBindTexture(GL_TEXTURE_2D, T->t0[i]);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
		}
	} else if (path == WP_NV12) {
		T->t1 = new_tex(GL_RG8, (fw + 1) / 2, (fh + 1) / 2);
	} else if (path == WP_I420) {
		T->t1 = new_tex(GL_R8, (fw + 1) / 2, (fh + 1) / 2);
		T->t2 = new_tex(GL_R8, (fw + 1) / 2, (fh + 1) / 2);
	}
	T->alloc_fmt = (int)path;
	T->alloc_w = fw;
	T->alloc_h = fh;
}

static void upload(GLuint t, int x0, int w, int h, GLenum fmt, int bpp, int stride, const void *data)
{
	glBindTexture(GL_TEXTURE_2D, t);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / bpp);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, x0);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
}

static gboolean upload_frame(struct warp *w, enum wpath path, GstSample *s, GstVideoInfo *vi)
{
	GstVideoFrame f;
	if (!gst_video_frame_map(&f, vi, gst_sample_get_buffer(s), GST_MAP_READ))
		return FALSE;
	int fw = GST_VIDEO_INFO_WIDTH(vi), fh = GST_VIDEO_INFO_HEIGHT(vi);
	w->cur ^= 1;   /* the other set: the GPU may still be drawing from this one */
	alloc_textures(w, path, fw, fh);
	struct texset *T = &w->ts[w->cur];
	if (path == WP_RGB) {
		for (int i = 0; i < T->t0n; i++)
			upload(T->t0[i], T->t0x[i], T->t0w[i], fh, GL_RGBA, 4, GST_VIDEO_FRAME_PLANE_STRIDE(&f, 0),
			       GST_VIDEO_FRAME_PLANE_DATA(&f, 0));
	} else {
		for (int i = 0; i < T->t0n; i++)
			upload(T->t0[i], T->t0x[i], T->t0w[i], fh, GL_RED, 1, GST_VIDEO_FRAME_PLANE_STRIDE(&f, 0),
			       GST_VIDEO_FRAME_PLANE_DATA(&f, 0));
		if (path == WP_NV12) {
			upload(T->t1, 0, (fw + 1) / 2, (fh + 1) / 2, GL_RG, 2, GST_VIDEO_FRAME_PLANE_STRIDE(&f, 1),
			       GST_VIDEO_FRAME_PLANE_DATA(&f, 1));
		} else {
			upload(T->t1, 0, (fw + 1) / 2, (fh + 1) / 2, GL_RED, 1, GST_VIDEO_FRAME_PLANE_STRIDE(&f, 1),
			       GST_VIDEO_FRAME_PLANE_DATA(&f, 1));
			upload(T->t2, 0, (fw + 1) / 2, (fh + 1) / 2, GL_RED, 1, GST_VIDEO_FRAME_PLANE_STRIDE(&f, 2),
			       GST_VIDEO_FRAME_PLANE_DATA(&f, 2));
		}
	}
	gst_video_frame_unmap(&f);
	return glGetError() == GL_NO_ERROR;
}

/* GStreamer frees a pool memory we imported: delete its image in the presenter thread */
static void oes_released(gpointer data)
{
	struct oes_img *o = data;
	struct warp *w = g_oes_owner;
	if (w) {
		g_mutex_lock(&w->gc_lock);
		g_array_append_val(w->gc, *o);
		g_mutex_unlock(&w->gc_lock);
	}
	g_free(o);
}

/* zero copy: the NV12 dmabuf as an external texture, cached on the pool memory */
static GLuint import_oes(struct warp *w, GstSample *s, GstVideoInfo *vi, int enc, int full)
{
	GstBuffer *buf = gst_sample_get_buffer(s);
	GstMemory *m0 = gst_buffer_peek_memory(buf, 0);
	if (!gst_is_dmabuf_memory(m0) || GST_VIDEO_INFO_FORMAT(vi) != GST_VIDEO_FORMAT_NV12)
		return 0;
	struct oes_img *cached = gst_mini_object_get_qdata(GST_MINI_OBJECT(m0), oes_quark);
	if (cached)
		return cached->tex;
	int slot = -1;
	for (int j = 0; j < NOES && slot < 0; j++)
		if (!w->oes[j].mem)
			slot = j;
	if (slot < 0)
		return 0;
	GstVideoMeta *meta = gst_buffer_get_video_meta(buf);
	EGLint a[40], n = 0;
	a[n++] = EGL_WIDTH; a[n++] = GST_VIDEO_INFO_WIDTH(vi);
	a[n++] = EGL_HEIGHT; a[n++] = GST_VIDEO_INFO_HEIGHT(vi);
	a[n++] = EGL_LINUX_DRM_FOURCC_EXT; a[n++] = DRM_FORMAT_NV12;
	for (int pl = 0; pl < 2; pl++) {
		gsize off = meta ? meta->offset[pl] : GST_VIDEO_INFO_PLANE_OFFSET(vi, pl);
		gint stride = meta ? meta->stride[pl] : GST_VIDEO_INFO_PLANE_STRIDE(vi, pl);
		guint idx, len;
		gsize skip;
		if (!gst_buffer_find_memory(buf, off, 1, &idx, &len, &skip))
			return 0;
		GstMemory *m = gst_buffer_peek_memory(buf, idx);
		if (!gst_is_dmabuf_memory(m))
			return 0;
		a[n++] = pl ? EGL_DMA_BUF_PLANE1_FD_EXT : EGL_DMA_BUF_PLANE0_FD_EXT;
		a[n++] = gst_dmabuf_memory_get_fd(m);
		a[n++] = pl ? EGL_DMA_BUF_PLANE1_OFFSET_EXT : EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		a[n++] = (EGLint)(m->offset + skip);
		a[n++] = pl ? EGL_DMA_BUF_PLANE1_PITCH_EXT : EGL_DMA_BUF_PLANE0_PITCH_EXT;
		a[n++] = stride;
	}
	a[n++] = EGL_YUV_COLOR_SPACE_HINT_EXT;
	a[n++] = enc == 0 ? EGL_ITU_REC601_EXT : enc == 2 ? EGL_ITU_REC2020_EXT : EGL_ITU_REC709_EXT;
	a[n++] = EGL_SAMPLE_RANGE_HINT_EXT;
	a[n++] = full ? EGL_YUV_FULL_RANGE_EXT : EGL_YUV_NARROW_RANGE_EXT;
	a[n++] = EGL_NONE;
	EGLImageKHR img = w->create_image(w->out.dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
	if (img == EGL_NO_IMAGE_KHR)
		return 0;
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);
	tex_params(GL_TEXTURE_EXTERNAL_OES);
	w->target_tex(GL_TEXTURE_EXTERNAL_OES, img);
	if (glGetError() != GL_NO_ERROR) {
		glDeleteTextures(1, &t);
		w->destroy_image(w->out.dpy, img);
		return 0;
	}
	w->oes[slot] = (struct oes_img){ m0, img, t };
	struct oes_img *q = g_new(struct oes_img, 1);
	*q = w->oes[slot];
	g_oes_owner = w;
	gst_mini_object_set_qdata(GST_MINI_OBJECT(m0), oes_quark, q, oes_released);
	return t;
}

/* ---- geometry ---- */

/* fit like the plane path (presenter.c fit()): aspect kept, centred, even sizes */
static void place(const struct frame *f, int *x, int *y, int *pw, int *ph)
{
	if (g_warp_opts.place[2] > 0) {
		*x = g_warp_opts.place[0];
		*y = g_warp_opts.place[1];
		*pw = g_warp_opts.place[2];
		*ph = g_warp_opts.place[3];
		return;
	}
	double aspect = (double)f->w * MAX(f->par_n, 1) / ((double)f->h * MAX(f->par_d, 1));
	int dw = QW_FW, dh = (int)lround(QW_FW / aspect);
	if (dh > QW_PH) {
		dh = QW_PH;
		dw = (int)lround(QW_PH * aspect);
	}
	*pw = MAX(2, dw & ~1);
	*ph = MAX(2, dh & ~1);
	*x = (QW_FW - *pw) / 2;
	*y = (QW_PH - *ph) / 2;
}

/* will the warp upload this hw item's frames (stacked, or placed larger than
 * one panel)? Then the decoder should hand out system memory. */
gboolean warp_wants_sysmem(const struct media_info *mi, int layout)
{
	if (!g_warp_formats)
		return FALSE;
	if (layout == LAYOUT_STACKED ||
	    (layout == LAYOUT_AUTO && mi->width == QW_PW && mi->height == QW_PH * QW_NPANEL))
		return TRUE;
	struct frame f = { .w = mi->width, .h = mi->height, .par_n = mi->par_n, .par_d = mi->par_d };
	int x, y, pw, ph;
	place(&f, &x, &y, &pw, &ph);
	return (long)pw * ph > PANEL_AREA;
}

/* a mirror change saved by qvue-warpd (qvue-warpctl mirror ..., the Stream
 * Deck keys) while the player runs: picked up within a second, at the next
 * frame. The rest of warp.conf (calibration files, subdiv) stays as started. */
static void check_conf(struct warp *w)
{
	gint64 now = g_get_monotonic_time();
	if (!w->conf[0] || now - w->conf_checked < G_USEC_PER_SEC)
		return;
	w->conf_checked = now;
	struct stat st;
	if (stat(w->conf, &st) || (st.st_mtim.tv_sec == w->conf_mtime.tv_sec &&
				    st.st_mtim.tv_nsec == w->conf_mtime.tv_nsec))
		return;
	w->conf_mtime = st.st_mtim;
	struct warp_config c;
	qw_setup_config(&c, QW_DATADIR, w->conf);
	if (c.mirror_x == w->cfg.mirror_x && c.mirror_y == w->cfg.mirror_y &&
	    !memcmp(c.map, w->cfg.map, sizeof(c.map)))
		return;
	w->cfg.mirror_x = c.mirror_x;
	w->cfg.mirror_y = c.mirror_y;
	memcpy(w->cfg.map, c.map, sizeof(c.map));
	w->cfg_gen++;
	log_w("mirror_x %d, mirror_y %d (warp.conf changed)", c.mirror_x, c.mirror_y);
}

/* rebuild the meshes when the frame size, layout, placement, path or the
 * mirror changed. Each panel shows the canvas rectangle of its map
 * (qw_config_maps: mirror_x / mirror_y applied, then panelN.src / flip_x /
 * flip_y), exactly as qvue-warpd does with fb0; here the canvas holds the
 * placed picture (flat) or the three bands side by side (stacked). */
static int geometry(struct warp *w, const struct frame *f, enum wpath path, int fw, int fh, gboolean stacked)
{
	int px = 0, py = 0, pw = 0, ph = 0;
	if (!stacked)
		place(f, &px, &py, &pw, &ph);
	if (w->g_w == fw && w->g_h == fh && w->g_layout == stacked && w->g_px == px && w->g_py == py &&
	    w->g_pw == pw && w->g_ph == ph && w->g_path == path && w->g_cfg_gen == w->cfg_gen)
		return 0;
	char err[256];
	struct qw_panel_map maps[QW_NPANEL];
	qw_config_maps(&w->cfg, maps);
	for (int k = 0; k < QW_NPANEL; k++) {
		const struct qw_panel_map *m = &maps[k];
		struct qw_src_affine a = { .flip_x = m->flip_x, .flip_y = m->flip_y };
		if (stacked) {
			/* canvas panel j = band j of the frame (rows j*fh/3..); a
			 * rectangle is taken from the band of the canvas panel its
			 * centre is in */
			int j = CLAMP((m->src_x + m->src_w / 2) / QW_PW, 0, QW_NPANEL - 1);
			double bw = fw, bh = fh / 3.0;
			a.x0 = (float)((m->src_x - j * QW_PW + 0.5) * bw / QW_PW - 0.5);
			a.dx = (float)((m->src_w - 1) * bw / QW_PW);
			a.y0 = (float)(j * bh + (m->src_y + 0.5) * bh / QW_PH - 0.5);
			a.dy = (float)((m->src_h - 1) * bh / QW_PH);
		} else {
			/* the canvas pixels of the map's rectangle -> frame pixels of the placed picture */
			double sx = (double)fw / pw, sy = (double)fh / ph;
			a.x0 = (float)((m->src_x + 0.5 - px) * sx - 0.5);
			a.dx = (float)((m->src_w - 1) * sx);
			a.y0 = (float)((m->src_y + 0.5 - py) * sy - 0.5);
			a.dy = (float)((m->src_h - 1) * sy);
		}
		w->x0[k] = MIN(a.x0, a.x0 + a.dx);
		w->x1[k] = MAX(a.x0, a.x0 + a.dx);
		if (w->x1[k] < -1 || w->x0[k] > fw) {   /* the picture does not reach this panel */
			qw_renderer_clear_panel(&w->ren, k);
			continue;
		}
		struct qw_calib tc = { 0 };
		struct qw_mesh mesh;
		if (qw_calib_trimmed(&w->calib[k], (const float(*)[2])w->trim.h[k], &tc) ||
		    qw_mesh_build_affine(&tc, &a, w->cfg.subdiv, &mesh, err, sizeof(err))) {
			qw_calib_free(&tc);
			g_printerr("dual-video-player: warp: panel %d mesh: %s\n", k + 1, err);
			return -1;
		}
		qw_calib_free(&tc);
		int r = qw_renderer_set_mesh(&w->ren, k, &mesh);
		qw_mesh_free(&mesh);
		if (r)
			return -1;
	}
	const char *mir = w->cfg.mirror_x && w->cfg.mirror_y ? ", mirrored x+y" : w->cfg.mirror_x ? ", mirrored x" :
			  w->cfg.mirror_y ? ", mirrored y" : "";
	if (stacked)
		log_w("frame %dx%d stacked (panel k = rows k*%d..)%s: %s", fw, fh, fh / 3, mir, wpath_name[path]);
	else
		log_w("frame %dx%d placed at %d,%d %dx%d on the canvas%s: %s", fw, fh, px, py, pw, ph, mir,
		      wpath_name[path]);
	w->g_w = fw; w->g_h = fh; w->g_layout = stacked; w->g_px = px; w->g_py = py; w->g_pw = pw; w->g_ph = ph;
	w->g_path = path;
	w->g_cfg_gen = w->cfg_gen;
	return 0;
}

/* each drawn panel's source: which texture holds the frame columns it samples */
static int sources(struct warp *w, enum wpath path, GLuint oes, int fw, int fh)
{
	struct texset *T = &w->ts[w->cur];
	for (int k = 0; k < QW_NPANEL; k++) {
		if (!w->ren.nidx[k])
			continue;
		struct qw_source s = { .frame_w = fw, .frame_h = fh, .tex_h = fh };
		if (path == WP_OES) {
			s.fmt = QW_SRC_OES;
			s.tex[0] = oes;
			s.tex_x = 0;
			s.tex_w = fw;
		} else {
			int i = 0;
			if (T->t0n == 2 && !(w->x0[k] >= -1 && w->x1[k] + 2 <= T->t0x[0] + T->t0w[0]))
				i = 1;
			s.fmt = path == WP_RGB ? QW_SRC_RGB_FRAME : path == WP_NV12 ? QW_SRC_NV12 : QW_SRC_I420;
			s.tex[0] = T->t0[i];
			s.tex[1] = T->t1;
			s.tex[2] = T->t2;
			s.tex_x = T->t0x[i];
			s.tex_w = T->t0w[i];
		}
		if (qw_renderer_set_source(&w->ren, k, &s))
			return -1;
	}
	return 0;
}

/* ---- per frame ---- */

static void write_dump(const char *path)
{
	uint8_t *rgb = g_malloc((size_t)QW_FW * QW_PH * 3);
	qw_renderer_readback(rgb);
	FILE *f = fopen(path, "wb");
	if (f) {
		fprintf(f, "P6\n%d %d\n255\n", QW_FW, QW_PH);
		fwrite(rgb, 3, (size_t)QW_FW * QW_PH, f);
		fclose(f);
		log_w("wrote %s", path);
	}
	g_free(rgb);
}

/* the new picture into textures, meshes for its placement */
static int update(struct warp *w, const struct frame *f)
{
	GstSample *s = f->src;
	GstVideoInfo vi;
	if (!gst_video_info_from_caps(&vi, gst_sample_get_caps(s)))
		return -1;
	GstVideoFormat fmt = GST_VIDEO_INFO_FORMAT(&vi);
	int fw = GST_VIDEO_INFO_WIDTH(&vi), fh = GST_VIDEO_INFO_HEIGHT(&vi);
	gboolean stacked = f->layout == LAYOUT_STACKED ||
			   (f->layout == LAYOUT_AUTO && fw == QW_PW && fh == QW_PH * QW_NPANEL);
	int enc = f->enc >= 0 ? f->enc : fh > 576 ? 1 : 0, full = f->range == 1;
	enum wpath path;
	if (fmt == GST_VIDEO_FORMAT_BGRx || fmt == GST_VIDEO_FORMAT_BGRA)
		path = WP_RGB;
	else if (fmt == GST_VIDEO_FORMAT_I420)
		path = WP_I420;
	else if (fmt == GST_VIDEO_FORMAT_NV12) {
		path = WP_NV12;
		if (!stacked && w->ren.sp[QW_SRC_OES].prog) {
			int px, py, pw, ph;
			place(f, &px, &py, &pw, &ph);
			if ((long)pw * ph <= PANEL_AREA)
				path = WP_OES;
		}
	} else {
		g_printerr("dual-video-player: warp: frame format %s not supported\n", gst_video_format_to_string(fmt));
		return -1;
	}
	if (path == WP_I420 && fw == QW_FW && !w->warned_fps) {
		int n = GST_VIDEO_INFO_FPS_N(&vi), d = GST_VIDEO_INFO_FPS_D(&vi);
		if (d > 0 && n > 31 * d)
			log_w("4500x298 at %.0f fps is software decoded and may drop frames; "
			      "convert it to stacked 1500x894 (qvue-stack-video.sh) for 60 fps", (double)n / d);
		w->warned_fps = TRUE;
	}
	GLuint oes = 0;
	if (path == WP_OES) {
		oes = import_oes(w, s, &vi, enc, full);
		if (!oes) {
			path = WP_NV12;   /* not importable: upload instead */
		}
	}
	if (path != WP_OES && !upload_frame(w, path, s, &vi)) {
		g_printerr("dual-video-player: warp: frame upload failed\n");
		return -1;
	}
	qw_renderer_set_yuv(&w->ren, enc, full);
	check_conf(w);
	if (geometry(w, f, path, fw, fh, stacked) || sources(w, path, oes, fw, fh))
		return -1;
	w->path = path;
	w->fw = fw;
	w->fh = fh;
	return 0;
}

/* a warp commit must be at least this long before its vblank (presenter.c
 * submit_time). The commit carries a buffer drawn a frame earlier (pipelined,
 * warp_commit), so only the display path's margin is needed; the draw issued
 * in the same call has until the next commit. */
gint64 warp_latest_ns(void)
{
	static gint64 g = -1;
	if (g < 0) {
		const char *e = g_getenv("QW_WARP_LATEST_MS");
		g = (gint64)((e ? g_ascii_strtod(e, NULL) : 4.0) * GST_MSECOND);
	}
	return g;
}

/* upload a new picture as soon as it is made, before the presenter waits for
 * its vblank: the commit then only draws (the picture on screen is in the
 * output buffer, the textures are free once its flip happened) */
int warp_prepare(struct player *p, const struct frame *f)
{
	struct warp *w = p->warp;
	if (!w || !f->src || f->serial <= w->serial)
		return 0;
	if (!w->started && warp_start(w))
		return warp_fail(p, "start failed");
	gint64 t0 = mono_ns();
	if (update(w, f))
		return warp_fail(p, "cannot show the frame");
	w->serial = f->serial;
	w->t_prep += (mono_ns() - t0) / 1e6;
	return 0;
}

int warp_commit(struct player *p, const struct frame *f)
{
	struct warp *w = p->warp;
	char err[256];
	if (!w->started && warp_start(w))
		return warp_fail(p, "start failed");
	if (g_warp_opts.fail && !strcmp(g_warp_opts.fail, "mid") && w->frames == 120)
		return warp_fail(p, "failure forced (--warp-fail=mid)");
	collect_garbage(w);
	gint64 t0 = mono_ns();
	if (f->src && f->serial > w->serial) {   /* not prepared (two-file mode) */
		if (update(w, f))
			return warp_fail(p, "cannot show the frame");
		w->serial = f->serial;
	}
	if (!w->path)
		return warp_fail(p, "no picture yet");
	gint64 t1 = mono_ns();
	qw_renderer_draw(&w->ren);
	if (g_warp_opts.dump && w->frames == g_warp_opts.dump_at)
		write_dump(g_warp_opts.dump);
	gint64 t2 = mono_ns();
	struct gbm_bo *bo = qw_output_finish_frame(&w->out, w->finish, err, sizeof(err));
	if (!bo)
		return warp_fail(p, err);
	gint64 t3 = mono_ns();
	/* Pipelined (video): the frame drawn now is committed at the next call,
	 * one frame later, so its ~11 ms of GPU work have a whole frame period
	 * (drawn and committed in the same short slot after the previous flip,
	 * ~10% missed their vblank). This call commits the frame drawn last
	 * time. Images are drawn and committed directly (the presenter commits
	 * an image once and then holds it). */
	struct gbm_bo *show = bo;
	if (w->path != WP_RGB) {
		show = w->ready;
		w->ready = bo;
		if (!show) {   /* the pipeline's first frame: shown by the next call */
			w->frames++;
			return 0;
		}
	} else if (w->ready) {
		qw_output_drop(&w->out, w->ready);
		w->ready = NULL;
	}
	if (qw_output_commit(&w->out, show, err, sizeof(err))) {
		qw_output_drop(&w->out, show);
		return warp_fail(p, err);
	}
	/* wait for the flip, as commit_frames() does, and feed the player's vblank grid */
	gint64 deadline = mono_ns() + 200 * GST_MSECOND;
	for (;;) {
		struct pollfd pfd = { .fd = p->fd, .events = POLLIN };
		int left = (int)((deadline - mono_ns()) / GST_MSECOND);
		if (left <= 0 || poll(&pfd, 1, left) <= 0)
			return warp_fail(p, "no page flip within 200 ms");
		struct qw_flip_info fi;
		int r = qw_output_dispatch(&w->out, &fi, err, sizeof(err));
		if (r < 0)
			return warp_fail(p, err);
		if (r == 1) {
			if (w->timing) {
				if (w->prev_seq && fi.seq - w->prev_seq > 1)
					w->t_missed++;
				w->t_after += fi.commit_ms - w->prev_vbl_ms;
				w->prev_seq = fi.seq;
				w->prev_vbl_ms = fi.vblank_ms;
			}
			gint64 us = (gint64)llround(fi.vblank_ms * 1000.0);
			on_flip(p->fd, fi.seq, (unsigned)(us / 1000000), (unsigned)(us % 1000000), p->out[0].crtc_id, p);
			break;
		}
	}
	w->frames++;
	if (w->timing) {
		gint64 t4 = mono_ns();
		w->t_upd += (t1 - t0) / 1e6; w->t_draw += (t2 - t1) / 1e6; w->t_swap += (t3 - t2) / 1e6;
		w->t_wait += (t4 - t3) / 1e6;
		if (++w->t_n == 120) {
			log_w("timing (ms/frame): prepare %.2f, update %.2f, draw %.2f, swap %.2f, commit+flip wait %.2f; "
			      "commit at prev vblank + %.2f; flips after a skipped vblank %ld/120",
			      w->t_prep / 120, w->t_upd / 120, w->t_draw / 120, w->t_swap / 120, w->t_wait / 120, w->t_after / 120,
			      w->t_missed);
			w->t_upd = w->t_draw = w->t_swap = w->t_wait = w->t_after = w->t_prep = 0;
			w->t_n = w->t_missed = 0;
		}
	}
	return 0;
}

#endif /* HAVE_QVUEWARP */
