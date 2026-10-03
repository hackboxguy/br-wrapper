/*
 * dual-video-player.c - play two H.264 MP4 files, one per display, in sync.
 *
 * Decoding: one GStreamer pipeline with a branch per file,
 *
 *   filesrc ! qtdemux ! h264parse ! v4l2h264dec ! appsink
 *
 * using the Pi's V4L2 hardware decoder; decoded frames stay in dmabufs.
 *
 * Display: a presenter thread imports the dmabufs as DRM framebuffers (zero
 * copy) and puts frame N of both files on screen in ONE atomic commit. Two
 * independent sinks (e.g. two kmssinks) stutter on vc4: a commit on one CRTC
 * waits for the other CRTC's pending flip, so each display regularly misses
 * its vblank. A single commit for both has nothing to wait for.
 *
 * Pacing: frames are mapped onto the vblank grid of a "master" display -
 * the one whose refresh is an integer multiple of the frame rate, if any
 * (25 fps -> every 2nd vblank at 50 Hz) - and each commit is submitted just
 * after the displays' previous vblanks, leaving the most room before their
 * next ones, so a scheduling delay cannot push a frame to a later vblank.
 * Page-flip events report when each frame actually latched; per-display
 * hold statistics are logged at every loop. Vblank positions are derived from event timestamps only: the
 * sequence numbers vc4 puts in page-flip events are not reliable for every
 * CRTC.
 *
 * Refresh matching (--refresh=auto, default): 25 fps on a 60 Hz display
 * shows frames for alternately 2 and 3 refreshes (judder). After preroll each
 * display is switched, at its current resolution, to a mode whose refresh is
 * an integer multiple of the frame rate (>= 48 Hz) when one is offered. The
 * kernel restores the launcher's mode when the player exits.
 *
 * Looping uses segment seeks: when both files end, a non-flushing seek
 * restarts both at 0, so there is no gap and frame N stays paired.
 *
 * With only one display connected, video 1 plays on it alone.
 *
 * Every display shows black around the video (primary plane). While playing,
 * the player grabs the touch/key input devices so the launcher behind it does
 * not react. A tap on display 1 (or a mouse click) shows an EXIT button on
 * it; tapping the button stops playback, tapping elsewhere or waiting 5 s
 * hides it. Enter (front-panel key) or Esc stop playback directly, as does
 * SIGTERM from the launcher's stop-app.
 *
 * Build: gcc -O2 -o dual-video-player dual-video-player.c -lm $(pkg-config \
 *   --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 \
 *   gstreamer-allocators-1.0 libdrm)
 */
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define GUARD_NS (1500 * 1000)   /* min distance of a commit from a vblank */

/* a DRM plane and the ids of the properties we set on it */
struct plane {
	uint32_t id;
	uint32_t fb, crtc, sx, sy, sw, sh, cx, cy, cw, ch, zpos;
	uint64_t zmin, zmax;      /* zpos range (zpos 0: not settable) */
};

/* one display: connector, CRTC, its planes */
struct output {
	const char *name;
	int conn_id;
	uint32_t crtc_id;
	int pipe;
	gint64 period;            /* refresh period, ns */
	int w, h;                 /* mode size */
	struct plane video;       /* overlay plane showing the decoded frames */
	struct plane primary;     /* black background */
	uint32_t black_fb;
	gboolean primary_set;
	gint64 last_idx;          /* index of the last vblank seen, own grid */
	gint64 last_ts;           /* its CLOCK_MONOTONIC time, ns */
	gint64 flip_idx;          /* vblank the previous frame latched on */
	gboolean flipped, have_flip;
	unsigned hold[8];         /* frames held for 1..7 vblanks ([0]: >= 8) */
};

#define POPUP_W 520
#define POPUP_H 200
#define POPUP_SECONDS 5

struct player {
	GstElement *pipeline;
	GstAppSink *sink[2];
	GMainLoop *loop;
	gboolean looping;
	unsigned loops;
	gint64 started;           /* monotonic us; input ignored for the first second */
	int ret;

	int fd;
	struct output out[2];
	int nout, master;
	double fps;
	GThread *thread;
	gint stop;
	unsigned late;            /* frames that missed their vblank slot */

	struct plane popup;       /* EXIT button on display 1 (id 0: none) */
	uint32_t popup_fb;
	gint popup_on;            /* set by the input handler, read by the presenter */
	guint popup_timer;
};

/* one grabbed input device */
struct indev {
	struct player *p;
	int ax, ay;               /* ABS codes of the touch position */
	struct input_absinfo xi, yi;
	int x, y;
	gboolean down;
};

/* evdev codes that make an input device interesting */
static const unsigned exit_keys[] = { BTN_TOUCH, BTN_LEFT, KEY_ENTER, KEY_ESC };

static GQuark fb_quark;
static int g_fd = -1;

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options] VIDEO1 VIDEO2\n"
		"  Plays VIDEO1 on the first and VIDEO2 on the second display, in sync.\n"
		"  Files must be H.264 in MP4 (Pi4 hardware decoder: max 1920x1080).\n"
		"Options:\n"
		"  --connector1=NAME   display for VIDEO1 (default: first connected, e.g. HDMI-A-1)\n"
		"  --connector2=NAME   display for VIDEO2 (default: second connected)\n"
		"  --card=PATH         DRM device (default: first card with connectors)\n"
		"  --once              play once and exit (default: loop forever)\n"
		"  --refresh=auto|keep|HZ  display refresh: auto = integer multiple of the\n"
		"                      video frame rate if offered (default), keep = as is\n",
		prog);
}

static gint64 mono_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (gint64)ts.tv_sec * GST_SECOND + ts.tv_nsec;
}

static void sleep_until(gint64 t)
{
	struct timespec ts = { .tv_sec = t / GST_SECOND, .tv_nsec = t % GST_SECOND };
	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
		;
}

/* ---- connectors (sysfs) ---- */

/* sysfs: /sys/class/drm/<card>-<connector>/{status,connector_id} */
static int read_sysfs(const char *card, const char *conn, const char *attr,
		      char *buf, size_t len)
{
	char path[256];
	snprintf(path, sizeof(path), "/sys/class/drm/%s-%s/%s", card, conn, attr);
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	if (!fgets(buf, (int)len, f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

static int connector_id(const char *card, const char *conn)
{
	char buf[32];
	if (read_sysfs(card, conn, "connector_id", buf, sizeof(buf)))
		return -1;
	return atoi(buf);
}

/*
 * Find the KMS card (first cardN with connectors, unless given) and the
 * connected connectors on it, ordered by connector id.
 */
static int find_outputs(char *card, size_t card_len, char conns[][32], int max)
{
	GDir *dir = g_dir_open("/sys/class/drm", 0, NULL);
	if (!dir)
		return -1;
	GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
	const char *e;
	while ((e = g_dir_read_name(dir)))
		g_ptr_array_add(names, g_strdup(e));
	g_dir_close(dir);

	int n = 0;
	for (guint i = 0; i < names->len && n < max; i++) {
		const char *name = g_ptr_array_index(names, i);
		const char *dash = strchr(name, '-');
		if (strncmp(name, "card", 4) || !dash)
			continue;
		char c[32];
		snprintf(c, sizeof(c), "%.*s", (int)(dash - name), name);
		if (*card && strcmp(card, c))
			continue;
		char status[32];
		if (read_sysfs(c, dash + 1, "status", status, sizeof(status)) ||
		    strcmp(status, "connected"))
			continue;
		if (!*card)
			snprintf(card, card_len, "%s", c);
		snprintf(conns[n++], 32, "%s", dash + 1);
	}
	g_ptr_array_free(names, TRUE);

	for (int i = 0; i < n; i++)
		for (int j = i + 1; j < n; j++)
			if (connector_id(card, conns[j]) < connector_id(card, conns[i])) {
				char t[32];
				memcpy(t, conns[i], 32);
				memcpy(conns[i], conns[j], 32);
				memcpy(conns[j], t, 32);
			}
	return n;
}

/* ---- input / signals ---- */

static gboolean on_signal(gpointer data)
{
	struct player *p = data;
	g_printerr("dual-video-player: stopping\n");
	g_main_loop_quit(p->loop);
	return G_SOURCE_CONTINUE;
}

#define NLONGS(x) (((x) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
static int has_bit(const unsigned long *bits, unsigned bit)
{
	return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1;
}

static gboolean hide_popup(gpointer data)
{
	struct player *p = data;
	g_atomic_int_set(&p->popup_on, 0);
	if (p->popup_timer)
		g_source_remove(p->popup_timer);
	p->popup_timer = 0;
	return G_SOURCE_REMOVE;
}

static gboolean popup_timeout(gpointer data)
{
	struct player *p = data;
	p->popup_timer = 0;   /* this source ends by returning REMOVE */
	g_atomic_int_set(&p->popup_on, 0);
	return G_SOURCE_REMOVE;
}

static void popup_rect(struct player *p, int *x, int *y)
{
	*x = (p->out[0].w - POPUP_W) / 2;
	*y = (p->out[0].h - POPUP_H) / 2;
}

/*
 * A tap at display-1 coordinates (x, y), or a click without a position:
 * the first shows the EXIT button, a tap on it (or a second click) stops.
 */
static void on_tap(struct player *p, gboolean have_pos, int x, int y)
{
	if (g_get_monotonic_time() - p->started < G_USEC_PER_SEC)
		return;
	if (!p->popup.id) {
		/* no plane for the button: stop right away */
		g_print("dual-video-player: tap, stopping\n");
		g_main_loop_quit(p->loop);
		return;
	}
	if (!g_atomic_int_get(&p->popup_on)) {
		g_atomic_int_set(&p->popup_on, 1);
		if (p->popup_timer)
			g_source_remove(p->popup_timer);
		p->popup_timer = g_timeout_add_seconds(POPUP_SECONDS, popup_timeout, p);
		return;
	}
	int px, py;
	popup_rect(p, &px, &py);
	if (!have_pos || (x >= px && x < px + POPUP_W && y >= py && y < py + POPUP_H)) {
		g_print("dual-video-player: EXIT pressed, stopping\n");
		g_main_loop_quit(p->loop);
	} else {
		hide_popup(p);
	}
}

static int scale_abs(int v, const struct input_absinfo *a, int size)
{
	int range = a->maximum - a->minimum + 1;
	return range > 0 ? (int)((gint64)(v - a->minimum) * size / range) : v;
}

static gboolean on_input(gint fd, GIOCondition cond, gpointer data)
{
	struct indev *d = data;
	struct player *p = d->p;
	struct input_event ev[64];

	if (cond & (G_IO_ERR | G_IO_HUP)) {
		close(fd);
		g_free(d);
		return G_SOURCE_REMOVE;  /* device unplugged */
	}
	ssize_t n = read(fd, ev, sizeof(ev));
	for (ssize_t i = 0; i < n / (ssize_t)sizeof(ev[0]); i++) {
		struct input_event *e = &ev[i];
		if (e->type == EV_ABS) {
			if (e->code == d->ax)
				d->x = e->value;
			else if (e->code == d->ay)
				d->y = e->value;
		} else if (e->type == EV_KEY && e->value == 1) {
			if (e->code == BTN_TOUCH)
				d->down = TRUE;   /* position arrives with the frame's SYN */
			else if (e->code == BTN_LEFT)
				on_tap(p, FALSE, 0, 0);
			else if ((e->code == KEY_ENTER || e->code == KEY_ESC) &&
				 g_get_monotonic_time() - p->started > G_USEC_PER_SEC) {
				g_print("dual-video-player: key %u, stopping\n", e->code);
				g_main_loop_quit(p->loop);
			}
		} else if (e->type == EV_SYN && e->code == SYN_REPORT && d->down) {
			d->down = FALSE;
			/* the touchscreen belongs to display 1 */
			on_tap(p, TRUE, scale_abs(d->x, &d->xi, p->out[0].w),
			       scale_abs(d->y, &d->yi, p->out[0].h));
		}
	}
	return G_SOURCE_CONTINUE;
}

/*
 * Grab every input device that can send one of the exit keys, so the
 * launcher underneath does not react while the video plays (the grab ends
 * when the player exits).
 */
static void watch_inputs(struct player *p)
{
	GDir *dir = g_dir_open("/dev/input", 0, NULL);
	const char *e;
	if (!dir)
		return;
	while ((e = g_dir_read_name(dir))) {
		if (strncmp(e, "event", 5))
			continue;
		gchar *path = g_build_filename("/dev/input", e, NULL);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		g_free(path);
		if (fd < 0)
			continue;
		unsigned long keys[NLONGS(KEY_MAX + 1)] = { 0 };
		unsigned long abs[NLONGS(ABS_MAX + 1)] = { 0 };
		gboolean want = FALSE;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0)
			for (size_t k = 0; k < G_N_ELEMENTS(exit_keys); k++)
				want |= has_bit(keys, exit_keys[k]);
		if (!want) {
			close(fd);
			continue;
		}
		struct indev *d = g_new0(struct indev, 1);
		d->p = p;
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
		gboolean mt = has_bit(abs, ABS_MT_POSITION_X) && has_bit(abs, ABS_MT_POSITION_Y);
		d->ax = mt ? ABS_MT_POSITION_X : ABS_X;
		d->ay = mt ? ABS_MT_POSITION_Y : ABS_Y;
		ioctl(fd, EVIOCGABS(d->ax), &d->xi);
		ioctl(fd, EVIOCGABS(d->ay), &d->yi);
		if (ioctl(fd, EVIOCGRAB, 1))
			g_printerr("dual-video-player: cannot grab %s: %s\n", e, strerror(errno));
		g_unix_fd_add(fd, G_IO_IN | G_IO_ERR | G_IO_HUP, on_input, d);
	}
	g_dir_close(dir);
}

/* ---- framebuffers we draw ourselves ---- */

/* a w x h dumb buffer framebuffer; @draw fills it (NULL: all zero) */
static uint32_t dumb_fb(int fd, int w, int h, uint32_t fourcc,
			void (*draw)(uint32_t *px, int stride, int w, int h))
{
	uint32_t handle, pitch, fb = 0;
	uint64_t size, offset;
	if (drmModeCreateDumbBuffer(fd, (uint32_t)w, (uint32_t)h, 32, 0, &handle, &pitch, &size) ||
	    drmModeMapDumbBuffer(fd, handle, &offset)) {
		g_printerr("dual-video-player: dumb buffer: %s\n", strerror(errno));
		return 0;
	}
	void *map = mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, (off_t)offset);
	if (map != MAP_FAILED) {
		memset(map, 0, size);
		if (draw)
			draw(map, (int)(pitch / 4), w, h);
		munmap(map, size);
	}
	uint32_t handles[4] = { handle }, pitches[4] = { pitch }, offsets[4] = { 0 };
	if (drmModeAddFB2(fd, (uint32_t)w, (uint32_t)h, fourcc, handles, pitches, offsets, &fb, 0))
		fb = 0;
	drmCloseBufferHandle(fd, handle);   /* the fb keeps the buffer */
	return fb;
}

/* 5x7 glyphs, rows top to bottom, bit 4 = leftmost column */
static const uint8_t glyph_E[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f };
static const uint8_t glyph_X[7] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 };
static const uint8_t glyph_I[7] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f };
static const uint8_t glyph_T[7] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };

/* EXIT button: red rounded rectangle, white border and text (premultiplied ARGB) */
static void draw_popup(uint32_t *px, int stride, int w, int h)
{
	const int r = 32, border = 6, scale = 14;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			/* distance outside the rounded corner arc */
			int cx = x < r ? r - x : x >= w - r ? x - (w - r - 1) : 0;
			int cy = y < r ? r - y : y >= h - r ? y - (h - r - 1) : 0;
			double d = sqrt((double)cx * cx + (double)cy * cy);
			if (d > r)
				continue;   /* transparent */
			gboolean edge = d > r - border || x < border || x >= w - border ||
					y < border || y >= h - border;
			px[y * stride + x] = edge ? 0xffffffff : 0xffc0392b;
		}
	const uint8_t *text[4] = { glyph_E, glyph_X, glyph_I, glyph_T };
	int tw = 4 * 5 * scale + 3 * scale, x0 = (w - tw) / 2, y0 = (h - 7 * scale) / 2;
	for (int g = 0; g < 4; g++)
		for (int row = 0; row < 7; row++)
			for (int col = 0; col < 5; col++)
				if (text[g][row] & (0x10 >> col))
					for (int yy = 0; yy < scale; yy++)
						for (int xx = 0; xx < scale; xx++)
							px[(y0 + row * scale + yy) * stride + x0 +
							   g * 6 * scale + col * scale + xx] = 0xffffffff;
}

/* ---- refresh matching ---- */

static double mode_hz(const drmModeModeInfo *m)
{
	double hz = m->clock * 1000.0 / ((double)m->htotal * m->vtotal);
	if (m->flags & DRM_MODE_FLAG_INTERLACE)
		hz *= 2;
	return hz;
}

/* refresh shows every frame for the same number of vblanks (0.5% tolerance) */
static gboolean matches(double hz, double fps)
{
	double r = hz / fps;
	return r >= 0.99 && fabs(r - round(r)) < 0.005 * r;
}

/*
 * Switch the CRTC driving @conn_id to a mode with the same resolution and a
 * refresh that matches @fps (or exactly @want_hz). The primary plane gets a
 * black dumb buffer; the video goes on an overlay plane.
 */
static void set_refresh(int fd, uint32_t conn_id, double fps, int want_hz)
{
	drmModeConnector *c = drmModeGetConnector(fd, conn_id);
	drmModeEncoder *e = c && c->encoder_id ? drmModeGetEncoder(fd, c->encoder_id) : NULL;
	drmModeCrtc *crtc = e && e->crtc_id ? drmModeGetCrtc(fd, e->crtc_id) : NULL;
	if (!crtc || !crtc->mode_valid)
		goto out;

	double cur = mode_hz(&crtc->mode);
	if (want_hz ? fabs(cur - want_hz) < 0.5 : matches(cur, fps)) {
		g_print("dual-video-player: connector %u: %.2f Hz already suits %.2f fps\n",
			conn_id, cur, fps);
		goto out;
	}
	drmModeModeInfo *best = NULL;
	for (int i = 0; i < c->count_modes; i++) {
		drmModeModeInfo *m = &c->modes[i];
		double hz = mode_hz(m);
		if (m->hdisplay != crtc->mode.hdisplay || m->vdisplay != crtc->mode.vdisplay ||
		    (m->flags & DRM_MODE_FLAG_INTERLACE))
			continue;
		if (want_hz ? fabs(hz - want_hz) >= 0.5 : (hz < 48 || !matches(hz, fps)))
			continue;
		/* lowest suitable refresh: least bandwidth, still flicker-free */
		if (!best || hz < mode_hz(best))
			best = m;
	}
	if (!best) {
		g_print("dual-video-player: connector %u: no %ux%u mode matching %.2f fps, "
			"keeping %.2f Hz (expect judder)\n", conn_id, crtc->mode.hdisplay,
			crtc->mode.vdisplay, fps, cur);
		goto out;
	}

	uint32_t fb = dumb_fb(fd, best->hdisplay, best->vdisplay, DRM_FORMAT_XRGB8888, NULL);
	if (!fb)
		goto out;
	if (drmModeSetCrtc(fd, crtc->crtc_id, fb, 0, 0, &conn_id, 1, best))
		g_printerr("dual-video-player: connector %u: mode set failed: %s\n",
			   conn_id, strerror(errno));
	else
		g_print("dual-video-player: connector %u: %.2f Hz -> %s %.2f Hz for %.2f fps\n",
			conn_id, cur, best->name, mode_hz(best), fps);
	/* the fb lives until the fd is closed at exit */
out:
	if (crtc)
		drmModeFreeCrtc(crtc);
	if (e)
		drmModeFreeEncoder(e);
	if (c)
		drmModeFreeConnector(c);
}

/* ---- display setup ---- */

static uint32_t prop_id(int fd, uint32_t obj, uint32_t type, const char *name,
			uint64_t *value)
{
	uint32_t id = 0;
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, type);
	for (uint32_t i = 0; props && i < props->count_props && !id; i++) {
		drmModePropertyRes *pr = drmModeGetProperty(fd, props->props[i]);
		if (pr && !strcmp(pr->name, name)) {
			id = pr->prop_id;
			if (value)
				*value = props->prop_values[i];
		}
		drmModeFreeProperty(pr);
	}
	drmModeFreeObjectProperties(props);
	return id;
}

/* first plane of @type usable on CRTC @pipe that supports @fourcc */
static uint32_t find_plane(int fd, int pipe, uint64_t type, uint32_t fourcc,
			   const uint32_t *taken, int ntaken)
{
	uint32_t id = 0;
	drmModePlaneRes *pr = drmModeGetPlaneResources(fd);
	for (uint32_t i = 0; pr && i < pr->count_planes && !id; i++) {
		drmModePlane *pl = drmModeGetPlane(fd, pr->planes[i]);
		uint64_t t = ~0ull;
		gboolean ok = pl && (pl->possible_crtcs & (1u << pipe)) &&
			      prop_id(fd, pl->plane_id, DRM_MODE_OBJECT_PLANE, "type", &t) &&
			      t == type;
		for (int k = 0; ok && k < ntaken; k++)
			ok = pl->plane_id != taken[k];
		gboolean fmt = FALSE;
		for (uint32_t f = 0; ok && f < pl->count_formats; f++)
			fmt |= pl->formats[f] == fourcc;
		if (ok && fmt)
			id = pl->plane_id;
		drmModeFreePlane(pl);
	}
	drmModeFreePlaneResources(pr);
	return id;
}

static int plane_props(int fd, struct plane *pl, uint32_t id)
{
	pl->id = id;
#define P(field, name) \
	if (!(pl->field = prop_id(fd, id, DRM_MODE_OBJECT_PLANE, name, NULL))) return -1
	P(fb, "FB_ID"); P(crtc, "CRTC_ID");
	P(sx, "SRC_X"); P(sy, "SRC_Y"); P(sw, "SRC_W"); P(sh, "SRC_H");
	P(cx, "CRTC_X"); P(cy, "CRTC_Y"); P(cw, "CRTC_W"); P(ch, "CRTC_H");
#undef P
	/* zpos is optional; only set it where it is mutable, within its range */
	pl->zpos = prop_id(fd, id, DRM_MODE_OBJECT_PLANE, "zpos", NULL);
	drmModePropertyRes *z = pl->zpos ? drmModeGetProperty(fd, pl->zpos) : NULL;
	if (!z || (z->flags & DRM_MODE_PROP_IMMUTABLE) || !(z->flags & DRM_MODE_PROP_RANGE) ||
	    z->count_values < 2)
		pl->zpos = 0;
	else {
		pl->zmin = z->values[0];
		pl->zmax = z->values[1];
	}
	drmModeFreeProperty(z);
	return 0;
}

/* show @fb (src @sw x @sh) at @x,@y size @w x @h on @crtc; fb 0 disables */
static void plane_set(drmModeAtomicReq *req, const struct plane *pl, uint32_t fb,
		      uint32_t crtc, int sw, int sh, int x, int y, int w, int h, int z)
{
	drmModeAtomicAddProperty(req, pl->id, pl->fb, fb);
	drmModeAtomicAddProperty(req, pl->id, pl->crtc, fb ? crtc : 0);
	if (!fb)
		return;
	drmModeAtomicAddProperty(req, pl->id, pl->sx, 0);
	drmModeAtomicAddProperty(req, pl->id, pl->sy, 0);
	drmModeAtomicAddProperty(req, pl->id, pl->sw, (uint64_t)sw << 16);
	drmModeAtomicAddProperty(req, pl->id, pl->sh, (uint64_t)sh << 16);
	drmModeAtomicAddProperty(req, pl->id, pl->cx, (uint64_t)x);
	drmModeAtomicAddProperty(req, pl->id, pl->cy, (uint64_t)y);
	drmModeAtomicAddProperty(req, pl->id, pl->cw, (uint64_t)w);
	drmModeAtomicAddProperty(req, pl->id, pl->ch, (uint64_t)h);
	if (pl->zpos)
		drmModeAtomicAddProperty(req, pl->id, pl->zpos,
					 MIN(MAX((uint64_t)z, pl->zmin), pl->zmax));
}

/* CRTC, mode, video overlay plane (supporting @fourcc) and black primary for @o */
static int setup_output(int fd, struct output *o, uint32_t fourcc,
			const uint32_t *taken, int ntaken)
{
	drmModeRes *res = drmModeGetResources(fd);
	drmModeConnector *c = drmModeGetConnector(fd, (uint32_t)o->conn_id);
	drmModeEncoder *e = c && c->encoder_id ? drmModeGetEncoder(fd, c->encoder_id) : NULL;
	drmModeCrtc *crtc = e && e->crtc_id ? drmModeGetCrtc(fd, e->crtc_id) : NULL;
	int ret = -1;
	if (!res || !crtc || !crtc->mode_valid)
		goto out;
	o->crtc_id = crtc->crtc_id;
	for (int i = 0; i < res->count_crtcs; i++)
		if (res->crtcs[i] == crtc->crtc_id)
			o->pipe = i;
	o->w = crtc->mode.hdisplay;
	o->h = crtc->mode.vdisplay;
	o->period = (gint64)((double)crtc->mode.htotal * crtc->mode.vtotal * 1e6 /
			     crtc->mode.clock);

	uint32_t vid = find_plane(fd, o->pipe, DRM_PLANE_TYPE_OVERLAY, fourcc, taken, ntaken);
	uint32_t pri = find_plane(fd, o->pipe, DRM_PLANE_TYPE_PRIMARY, DRM_FORMAT_XRGB8888, NULL, 0);
	if (!vid || plane_props(fd, &o->video, vid))
		goto out;
	/* black background: optional, the launcher shows around the video without it */
	if (pri && !plane_props(fd, &o->primary, pri))
		o->black_fb = dumb_fb(fd, o->w, o->h, DRM_FORMAT_XRGB8888, NULL);
	ret = 0;
out:
	if (crtc)
		drmModeFreeCrtc(crtc);
	if (e)
		drmModeFreeEncoder(e);
	if (c)
		drmModeFreeConnector(c);
	if (res)
		drmModeFreeResources(res);
	return ret;
}

/* plane + drawn framebuffer for the EXIT button on display 1 */
static void setup_popup(struct player *p)
{
	uint32_t taken[2] = { p->out[0].video.id, p->nout > 1 ? p->out[1].video.id : 0 };
	uint32_t id = find_plane(p->fd, p->out[0].pipe, DRM_PLANE_TYPE_OVERLAY,
				 DRM_FORMAT_ARGB8888, taken, 2);
	if (!id || plane_props(p->fd, &p->popup, id) ||
	    !(p->popup_fb = dumb_fb(p->fd, POPUP_W, POPUP_H, DRM_FORMAT_ARGB8888, draw_popup))) {
		memset(&p->popup, 0, sizeof(p->popup));
		g_printerr("dual-video-player: no plane for the EXIT button, a tap stops playback\n");
	}
}

/* advance @o's vblank grid to the vblank at @ts */
static void saw_vblank(struct output *o, gint64 ts)
{
	if (o->last_ts)
		o->last_idx += llround((double)(ts - o->last_ts) / o->period);
	o->last_ts = ts;
}

/*
 * Wait for the next vblank of @o and record its time. A query (relative 0)
 * is not used: with vblank interrupts idle the kernel answers it from stale
 * state, with a zero timestamp. Afterwards the page-flip events of every
 * commit keep the grid fresh.
 */
static void sync_vblank(int fd, struct output *o)
{
	drmVBlank vb = { .request = {
		.type = DRM_VBLANK_RELATIVE |
			((o->pipe << DRM_VBLANK_HIGH_CRTC_SHIFT) & DRM_VBLANK_HIGH_CRTC_MASK),
		.sequence = 1 } };
	if (!drmWaitVBlank(fd, &vb))
		saw_vblank(o, (gint64)vb.reply.tval_sec * GST_SECOND +
			      (gint64)vb.reply.tval_usec * GST_USECOND);
}

/* ---- dmabuf -> DRM framebuffer (cached on the decoder's pool memory) ---- */

static void fb_free(gpointer data)
{
	drmModeRmFB(g_fd, GPOINTER_TO_UINT(data));
}

static uint32_t drm_fourcc(GstVideoFormat f)
{
	switch (f) {
	case GST_VIDEO_FORMAT_I420: return DRM_FORMAT_YUV420;
	case GST_VIDEO_FORMAT_YV12: return DRM_FORMAT_YVU420;
	case GST_VIDEO_FORMAT_NV12: return DRM_FORMAT_NV12;
	case GST_VIDEO_FORMAT_NV21: return DRM_FORMAT_NV21;
	default: return 0;
	}
}

static uint32_t sample_fb(GstSample *s, GstVideoInfo *info)
{
	GstBuffer *buf = gst_sample_get_buffer(s);
	GstCaps *caps = gst_sample_get_caps(s);
	if (!buf || !caps || !gst_video_info_from_caps(info, caps))
		return 0;
	GstMemory *mem0 = gst_buffer_peek_memory(buf, 0);
	gpointer cached = gst_mini_object_get_qdata(GST_MINI_OBJECT(mem0), fb_quark);
	if (cached)
		return GPOINTER_TO_UINT(cached);

	uint32_t fourcc = drm_fourcc(GST_VIDEO_INFO_FORMAT(info));
	GstVideoMeta *meta = gst_buffer_get_video_meta(buf);
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	if (!fourcc)
		return 0;
	for (guint pl = 0; pl < GST_VIDEO_INFO_N_PLANES(info); pl++) {
		gsize off = meta ? meta->offset[pl] : GST_VIDEO_INFO_PLANE_OFFSET(info, pl);
		gint stride = meta ? meta->stride[pl] : GST_VIDEO_INFO_PLANE_STRIDE(info, pl);
		guint idx, len;
		gsize skip;
		if (!gst_buffer_find_memory(buf, off, 1, &idx, &len, &skip))
			return 0;
		GstMemory *m = gst_buffer_peek_memory(buf, idx);
		if (!gst_is_dmabuf_memory(m) ||
		    drmPrimeFDToHandle(g_fd, gst_dmabuf_memory_get_fd(m), &handles[pl]))
			return 0;
		pitches[pl] = (uint32_t)stride;
		offsets[pl] = (uint32_t)(m->offset + skip);
	}
	uint32_t fb = 0;
	int r = drmModeAddFB2(g_fd, GST_VIDEO_INFO_WIDTH(info), GST_VIDEO_INFO_HEIGHT(info),
			      fourcc, handles, pitches, offsets, &fb, 0);
	/* the fb holds its own reference to the buffer object */
	for (int i = 0; i < 4; i++)
		if (handles[i] && (i == 0 || handles[i] != handles[i - 1]))
			drmCloseBufferHandle(g_fd, handles[i]);
	if (r)
		return 0;
	gst_mini_object_set_qdata(GST_MINI_OBJECT(mem0), fb_quark, GUINT_TO_POINTER(fb), fb_free);
	return fb;
}

/* ---- presenter ---- */

static void on_flip(int fd, unsigned seq, unsigned sec, unsigned usec,
		    unsigned crtc_id, void *data)
{
	struct player *p = data;
	(void)fd;
	(void)seq;
	for (int i = 0; i < p->nout; i++) {
		struct output *o = &p->out[i];
		if (o->crtc_id != crtc_id)
			continue;
		saw_vblank(o, (gint64)sec * GST_SECOND + (gint64)usec * GST_USECOND);
		if (o->have_flip) {
			gint64 held = o->last_idx - o->flip_idx;
			o->hold[held > 0 && held < 8 ? held : 0]++;
		}
		o->flip_idx = o->last_idx;
		o->have_flip = TRUE;
		o->flipped = TRUE;
	}
}

/*
 * Commit time for a frame that must latch on the master's vblank @v: inside
 * (v - period, v). A commit is only ever delayed (scheduling), never early,
 * so pick the time that leaves the most room before every display's next
 * vblank, while staying GUARD_NS after each display's previous vblank (a
 * commit right at a vblank may or may not make it).
 */
static gint64 submit_time(struct player *p, gint64 v)
{
	struct output *m = &p->out[p->master];
	gint64 best = v - m->period + GUARD_NS, best_score = -1;
	for (gint64 t = v - m->period + GUARD_NS; t <= v - GUARD_NS; t += 250 * 1000) {
		gint64 score = v - t;
		for (int i = 0; i < p->nout; i++) {
			struct output *o = &p->out[i];
			if (i == p->master || o->period <= 0)
				continue;
			gint64 since = (t - o->last_ts) % o->period;  /* since its last vblank */
			if (since < 0)
				since += o->period;
			if (since < GUARD_NS)
				score = -1;
			else
				score = MIN(score, o->period - since);
		}
		if (score > best_score) {
			best_score = score;
			best = t;
		}
	}
	return best;
}

static int commit(struct player *p, const uint32_t *fb, const GstVideoInfo *vi)
{
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	for (int i = 0; i < p->nout; i++) {
		struct output *o = &p->out[i];
		int vw = GST_VIDEO_INFO_WIDTH(&vi[i]), vh = GST_VIDEO_INFO_HEIGHT(&vi[i]);
		/* centred, unscaled; scaled down to fit (keeping aspect) if larger */
		int dw = vw, dh = vh;
		if (dw > o->w || dh > o->h) {
			double s = MIN((double)o->w / vw, (double)o->h / vh);
			dw = (int)(vw * s);
			dh = (int)(vh * s);
		}
		if (o->black_fb && !o->primary_set)
			plane_set(req, &o->primary, o->black_fb, o->crtc_id, o->w, o->h,
				  0, 0, o->w, o->h, 0);
		plane_set(req, &o->video, fb[i], o->crtc_id, vw, vh,
			  (o->w - dw) / 2, (o->h - dh) / 2, dw, dh, 1);
		o->flipped = FALSE;
	}
	if (p->popup.id) {
		int px, py;
		popup_rect(p, &px, &py);
		plane_set(req, &p->popup, g_atomic_int_get(&p->popup_on) ? p->popup_fb : 0,
			  p->out[0].crtc_id, POPUP_W, POPUP_H, px, py, POPUP_W, POPUP_H, 2);
	}
	int r = drmModeAtomicCommit(p->fd, req,
				    DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, p);
	drmModeAtomicFree(req);
	if (r)
		return r;
	for (int i = 0; i < p->nout; i++)
		p->out[i].primary_set = TRUE;

	/* wait until every display has latched the new frames */
	drmEventContext ev = { .version = 3, .page_flip_handler2 = on_flip };
	gint64 deadline = mono_ns() + 200 * GST_MSECOND;
	for (;;) {
		gboolean all = TRUE;
		for (int i = 0; i < p->nout; i++)
			all &= p->out[i].flipped;
		if (all)
			return 0;
		struct pollfd pfd = { .fd = p->fd, .events = POLLIN };
		int left = (int)((deadline - mono_ns()) / GST_MSECOND);
		if (left <= 0 || poll(&pfd, 1, left) <= 0)
			return -ETIMEDOUT;
		drmHandleEvent(p->fd, &ev);
	}
}

/* take our video planes and the button off screen (the launcher's fbdev
 * console takes the displays back when the fd is closed) */
static void disable_planes(struct player *p)
{
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	for (int i = 0; i < p->nout; i++)
		plane_set(req, &p->out[i].video, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if (p->popup.id)
		plane_set(req, &p->popup, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	drmModeAtomicCommit(p->fd, req, 0, NULL);
	drmModeAtomicFree(req);
}

static gpointer presenter(gpointer data)
{
	struct player *p = data;
	struct output *m = &p->out[p->master];
	GstSample *shown[2] = { NULL, NULL }, *next[2] = { NULL, NULL };
	GstVideoInfo vi[2];
	guint64 k = 0;
	gint64 n0 = -1, last_target = -1;
	double ratio = 1e9 / m->period / p->fps;   /* master vblanks per frame */

	while (!g_atomic_int_get(&p->stop)) {
		uint32_t fb[2] = { 0, 0 };
		gboolean eos = FALSE;
		for (int i = 0; i < p->nout && !g_atomic_int_get(&p->stop); i++) {
			while (!next[i] && !g_atomic_int_get(&p->stop)) {
				next[i] = gst_app_sink_try_pull_sample(p->sink[i], 100 * GST_MSECOND);
				if (!next[i] && gst_app_sink_is_eos(p->sink[i])) {
					eos = TRUE;
					break;
				}
			}
			if (eos)
				break;
		}
		if (eos || g_atomic_int_get(&p->stop))
			break;
		for (int i = 0; i < p->nout; i++)
			if (!(fb[i] = sample_fb(next[i], &vi[i]))) {
				g_printerr("dual-video-player: cannot import frame as DRM framebuffer\n");
				p->ret = 1;
				goto done;
			}

		if (n0 < 0) {
			for (int i = 0; i < p->nout; i++)
				sync_vblank(p->fd, &p->out[i]);
			n0 = m->last_idx + 2;
		}
		gint64 target = n0 + llround((double)k * ratio);
		if (last_target >= 0 && target <= last_target)
			target = last_target + 1;
		gint64 now = mono_ns(), t;
		for (;;) {
			gint64 v = m->last_ts + (target - m->last_idx) * m->period;
			t = submit_time(p, v);
			if (t > now + GST_SECOND) {
				/* lost track of the vblank grid: start a new one */
				g_printerr("dual-video-player: resync to display vblanks\n");
				for (int i = 0; i < p->nout; i++)
					sync_vblank(p->fd, &p->out[i]);
				n0 = m->last_idx + 2 - llround((double)k * ratio);
				target = m->last_idx + 2;
				now = mono_ns();
				continue;
			}
			if (t > now + 300 * 1000)
				break;
			/* too late for this vblank: show the current frame one more vblank */
			target++;
			n0++;
			p->late++;
		}
		sleep_until(t);
		int r = commit(p, fb, vi);
		if (r) {
			g_printerr("dual-video-player: commit failed: %s\n", strerror(-r));
			p->ret = 1;
			goto done;
		}
		last_target = target;
		/* previous frames are off screen now: give them back to the decoder */
		for (int i = 0; i < p->nout; i++) {
			if (shown[i])
				gst_sample_unref(shown[i]);
			shown[i] = next[i];
			next[i] = NULL;
		}
		k++;
	}
done:
	if (p->nout)
		disable_planes(p);
	for (int i = 0; i < 2; i++) {
		if (shown[i])
			gst_sample_unref(shown[i]);
		if (next[i])
			gst_sample_unref(next[i]);
	}
	g_main_loop_quit(p->loop);
	return NULL;
}

/* per display: how many vblanks each frame stayed on screen */
static void print_stats(struct player *p)
{
	for (int i = 0; i < p->nout; i++) {
		struct output *o = &p->out[i];
		GString *s = g_string_new(NULL);
		for (int h = 1; h < 8; h++)
			if (o->hold[h])
				g_string_append_printf(s, " %dx%u", h, o->hold[h]);
		if (o->hold[0])
			g_string_append_printf(s, " >=8x%u", o->hold[0]);
		g_print("dual-video-player: %s %.2f Hz%s: frames held (vblanks x count):%s\n",
			o->name, 1e9 / o->period, i == p->master ? " (master)" : "", s->str);
		g_string_free(s, TRUE);
		memset(o->hold, 0, sizeof(o->hold));
	}
	if (p->late)
		g_print("dual-video-player: %u frame(s) missed their vblank\n", p->late);
	p->late = 0;
}

/* ---- pipeline ---- */

static gboolean seek_start(struct player *p, gboolean flush)
{
	GstSeekFlags flags = GST_SEEK_FLAG_ACCURATE;
	if (flush)
		flags |= GST_SEEK_FLAG_FLUSH;
	if (p->looping)
		flags |= GST_SEEK_FLAG_SEGMENT;
	return gst_element_seek(p->pipeline, 1.0, GST_FORMAT_TIME, flags,
				GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, 0);
}

static gboolean on_bus(GstBus *bus, GstMessage *msg, gpointer data)
{
	struct player *p = data;
	(void)bus;

	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_ERROR: {
		GError *err = NULL;
		gchar *dbg = NULL;
		gst_message_parse_error(msg, &err, &dbg);
		g_printerr("dual-video-player: error from %s: %s\n%s\n",
			   GST_OBJECT_NAME(msg->src), err->message, dbg ? dbg : "");
		g_clear_error(&err);
		g_free(dbg);
		p->ret = 1;
		g_main_loop_quit(p->loop);
		break;
	}
	case GST_MESSAGE_SEGMENT_DONE:
		/* both files demuxed to the end: queue both again from 0 (no gap) */
		g_print("dual-video-player: loop %u\n", ++p->loops);
		print_stats(p);
		if (!seek_start(p, FALSE)) {
			g_printerr("dual-video-player: loop seek failed\n");
			p->ret = 1;
			g_main_loop_quit(p->loop);
		}
		break;
	case GST_MESSAGE_EOS:
		if (p->looping && seek_start(p, TRUE))
			break;  /* fallback if a segment seek was not honoured */
		/* --once: the presenter drains the appsinks and stops at their EOS */
		break;
	default:
		break;
	}
	return TRUE;
}

/* quote a file name for gst_parse_launch */
static gchar *quote(const char *s)
{
	GString *q = g_string_new("\"");
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			g_string_append_c(q, '\\');
		g_string_append_c(q, *s);
	}
	g_string_append_c(q, '"');
	return g_string_free(q, FALSE);
}

static gchar *branch(const char *file, int idx)
{
	gchar *loc = quote(file);
	/* sync=false: the presenter paces frames on the display vblanks */
	gchar *b = g_strdup_printf(
		"filesrc location=%s ! qtdemux ! h264parse ! "
		"v4l2h264dec capture-io-mode=dmabuf ! "
		"appsink name=sink%d sync=false max-buffers=2 enable-last-sample=false ",
		loc, idx);
	g_free(loc);
	return b;
}

/*
 * Announce GstVideoMeta support in the appsink's ALLOCATION query. For 1080p
 * the decoder's buffers are 1920x1088 (padded); without the meta it cannot
 * describe the padding, so v4l2h264dec copies each frame into system memory
 * and sample_fb() cannot import it. With it the dmabuf comes through as is,
 * the plane offsets/strides in the meta.
 */
static GstPadProbeReturn allocation_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	GstQuery *q = GST_PAD_PROBE_INFO_QUERY(info);
	(void)pad;
	(void)data;
	if (GST_QUERY_TYPE(q) != GST_QUERY_ALLOCATION)
		return GST_PAD_PROBE_OK;
	gst_query_add_allocation_meta(q, GST_VIDEO_META_API_TYPE, NULL);
	return GST_PAD_PROBE_HANDLED;
}

/* frame rate negotiated on sink1, 0 if unknown */
static double video_fps(GstAppSink *sink)
{
	double fps = 0;
	GstPad *pad = gst_element_get_static_pad(GST_ELEMENT(sink), "sink");
	GstCaps *caps = pad ? gst_pad_get_current_caps(pad) : NULL;
	gint n, d;
	if (caps && gst_structure_get_fraction(gst_caps_get_structure(caps, 0),
					       "framerate", &n, &d) && n > 0 && d > 0)
		fps = (double)n / d;
	if (caps)
		gst_caps_unref(caps);
	if (pad)
		gst_object_unref(pad);
	return fps;
}

/* DRM fourcc of the format negotiated on @sink */
static uint32_t sink_fourcc(GstAppSink *sink)
{
	GstPad *pad = gst_element_get_static_pad(GST_ELEMENT(sink), "sink");
	GstCaps *caps = pad ? gst_pad_get_current_caps(pad) : NULL;
	GstVideoInfo vi;
	uint32_t f = caps && gst_video_info_from_caps(&vi, caps) ?
		     drm_fourcc(GST_VIDEO_INFO_FORMAT(&vi)) : 0;
	if (caps)
		gst_caps_unref(caps);
	if (pad)
		gst_object_unref(pad);
	return f;
}

int main(int argc, char **argv)
{
	const char *c1 = NULL, *c2 = NULL;
	char card[32] = "";
	const char *files[2] = { NULL, NULL };
	int nfiles = 0;
	const char *refresh = "auto";
	struct player p = { .looping = TRUE };

	gst_init(&argc, &argv);
	fb_quark = g_quark_from_static_string("dual-video-player-fb");
	for (int i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "--connector1=", 13))
			c1 = argv[i] + 13;
		else if (!strncmp(argv[i], "--connector2=", 13))
			c2 = argv[i] + 13;
		else if (!strncmp(argv[i], "--card=", 7))
			snprintf(card, sizeof(card), "%s", g_path_get_basename(argv[i] + 7));
		else if (!strncmp(argv[i], "--refresh=", 10))
			refresh = argv[i] + 10;
		else if (!strcmp(argv[i], "--once"))
			p.looping = FALSE;
		else if (argv[i][0] == '-') {
			usage(argv[0]);
			return 2;
		} else if (nfiles < 2)
			files[nfiles++] = argv[i];
	}
	if (nfiles != 2) {
		usage(argv[0]);
		return 2;
	}
	for (int i = 0; i < 2; i++)
		if (access(files[i], R_OK)) {
			g_printerr("dual-video-player: cannot read %s: %s\n", files[i],
				   strerror(errno));
			return 1;
		}

	char conns[8][32];
	int n = find_outputs(card, sizeof(card), conns, 8);
	if (n <= 0) {
		g_printerr("dual-video-player: no connected display found\n");
		return 1;
	}
	if (!c1)
		c1 = conns[0];
	if (!c2) {
		for (int i = 0; i < n && !c2; i++)
			if (strcmp(conns[i], c1))
				c2 = conns[i];
	}
	int id1 = connector_id(card, c1);
	int id2 = c2 ? connector_id(card, c2) : -1;
	if (id1 < 0) {
		g_printerr("dual-video-player: unknown connector %s\n", c1);
		return 1;
	}

	char dev[64];
	snprintf(dev, sizeof(dev), "/dev/dri/%s", card);
	int fd = open(dev, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		g_printerr("dual-video-player: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	/* first opener is master already; this fails if another app owns the display */
	if (drmSetMaster(fd)) {
		g_printerr("dual-video-player: display busy (another DRM master, e.g. Kodi?): %s\n",
			   strerror(errno));
		return 1;
	}
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) ||
	    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		g_printerr("dual-video-player: atomic modesetting not supported\n");
		return 1;
	}
	g_fd = p.fd = fd;

	gchar *b1 = branch(files[0], 1);
	gchar *b2 = id2 >= 0 ? branch(files[1], 2) : g_strdup("");
	gchar *desc = g_strconcat(b1, b2, NULL);
	g_free(b1);
	g_free(b2);

	p.out[p.nout].name = c1;
	p.out[p.nout++].conn_id = id1;
	g_print("dual-video-player: %s [%s] -> %s (id %d)\n", files[0], card, c1, id1);
	if (id2 >= 0) {
		p.out[p.nout].name = c2;
		p.out[p.nout++].conn_id = id2;
		g_print("dual-video-player: %s [%s] -> %s (id %d)\n", files[1], card, c2, id2);
	} else {
		g_print("dual-video-player: single display: playing %s only\n", files[0]);
	}

	GError *err = NULL;
	p.pipeline = gst_parse_launch(desc, &err);
	g_free(desc);
	if (!p.pipeline || err) {
		g_printerr("dual-video-player: pipeline: %s\n", err ? err->message : "?");
		return 1;
	}
	GstCaps *caps = gst_caps_from_string("video/x-raw,format=(string){I420,NV12,YV12,NV21}");
	for (int i = 0; i < p.nout; i++) {
		gchar name[8];
		g_snprintf(name, sizeof(name), "sink%d", i + 1);
		p.sink[i] = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(p.pipeline), name));
		gst_app_sink_set_caps(p.sink[i], caps);
		GstPad *pad = gst_element_get_static_pad(GST_ELEMENT(p.sink[i]), "sink");
		gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, allocation_probe, NULL, NULL);
		gst_object_unref(pad);
	}
	gst_caps_unref(caps);

	p.loop = g_main_loop_new(NULL, FALSE);
	GstBus *bus = gst_element_get_bus(p.pipeline);
	gst_bus_add_watch(bus, on_bus, &p);
	gst_object_unref(bus);
	g_unix_signal_add(SIGINT, on_signal, &p);
	g_unix_signal_add(SIGTERM, on_signal, &p);

	/* preroll (negotiates format + frame rate), then arm segment looping */
	if (gst_element_set_state(p.pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE ||
	    gst_element_get_state(p.pipeline, NULL, NULL, 10 * GST_SECOND) == GST_STATE_CHANGE_FAILURE) {
		g_printerr("dual-video-player: failed to start pipeline\n");
		GstMessage *m = gst_bus_pop_filtered(GST_ELEMENT_BUS(p.pipeline), GST_MESSAGE_ERROR);
		if (m) {
			on_bus(NULL, m, &p);
			gst_message_unref(m);
		}
		gst_element_set_state(p.pipeline, GST_STATE_NULL);
		return 1;
	}

	p.fps = video_fps(p.sink[0]);
	if (p.fps <= 0)
		p.fps = 25;
	if (strcmp(refresh, "keep")) {
		int want = strcmp(refresh, "auto") ? atoi(refresh) : 0;
		for (int i = 0; i < p.nout; i++)
			set_refresh(fd, (uint32_t)p.out[i].conn_id, p.fps, want);
	}
	uint32_t taken[2] = { 0, 0 };
	for (int i = 0; i < p.nout; i++) {
		if (setup_output(fd, &p.out[i], sink_fourcc(p.sink[i]), taken, i)) {
			g_printerr("dual-video-player: %s: no usable CRTC/overlay plane\n",
				   p.out[i].name);
			gst_element_set_state(p.pipeline, GST_STATE_NULL);
			return 1;
		}
		taken[i] = p.out[i].video.id;
		/* master: the display whose refresh matches the frame rate */
		if (matches(1e9 / p.out[i].period, p.fps) &&
		    !matches(1e9 / p.out[p.master].period, p.fps))
			p.master = i;
	}
	setup_popup(&p);
	g_print("dual-video-player: pacing on %s (%.2f Hz) for %.2f fps\n",
		p.out[p.master].name, 1e9 / p.out[p.master].period, p.fps);

	if (p.looping && !seek_start(&p, TRUE))
		g_printerr("dual-video-player: segment seek failed, looping via EOS\n");
	gst_element_set_state(p.pipeline, GST_STATE_PLAYING);
	p.thread = g_thread_new("presenter", presenter, &p);
	p.started = g_get_monotonic_time();
	watch_inputs(&p);

	g_main_loop_run(p.loop);

	g_atomic_int_set(&p.stop, 1);
	g_thread_join(p.thread);
	print_stats(&p);
	gst_element_set_state(p.pipeline, GST_STATE_NULL);
	for (int i = 0; i < p.nout; i++)
		gst_object_unref(p.sink[i]);
	gst_object_unref(p.pipeline);
	g_main_loop_unref(p.loop);
	close(fd);
	return p.ret;
}
