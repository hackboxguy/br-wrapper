/* player.h - shared types and functions of dual-video-player (see dual-video-player.c) */
#ifndef PLAYER_H
#define PLAYER_H

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
	uint32_t color_enc, color_range;   /* 0: not available */
	uint64_t enc_val[3];      /* COLOR_ENCODING values: BT.601, BT.709, BT.2020 */
	uint64_t range_val[2];    /* COLOR_RANGE values: limited, full */
};

/* what one output shows: a framebuffer and how to place and colour it */
struct frame {
	uint32_t fb;
	int w, h;                 /* source size */
	int par_n, par_d;         /* pixel aspect ratio */
	int enc;                  /* -1: leave the plane alone; 0 601, 1 709, 2 2020 */
	int range;                /* 0 limited, 1 full */
};

/* a mappable dumb buffer framebuffer (images, software-decoded frames) */
struct dumbbuf {
	uint32_t handle, fb, pitch;
	uint64_t size;
	void *map;
	int w, h;
	uint32_t fourcc;
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

/* probe.c: what a media file is and how the player plays it */
enum media_kind { MEDIA_UNKNOWN, MEDIA_VIDEO, MEDIA_IMAGE };
enum decode_path { DECODE_UNSUPPORTED, DECODE_HW, DECODE_SW, DECODE_IMAGE };

struct media_info {
	char file[1024];
	enum media_kind kind;
	enum decode_path decode;
	char codec[24];
	char profile[32];
	int width, height;
	int fps_n, fps_d;             /* 0/1 for images and variable rate */
	int par_n, par_d;
	gint64 duration_ns;
	gint64 size;                  /* bytes */
	double bitrate_mbps;
	int rotation;                 /* degrees from the orientation tag */
	gboolean flip;
	char colorimetry[32];
	gboolean slow;                /* decodes, but maybe below real time */
	char reason[128];             /* why unsupported */
};

/* playlist items (item.c) */
enum item_state { ITEM_PREPARING, ITEM_READY, ITEM_FAILED };
#define ITEM_RING 3               /* dumb buffers per software-decoded item */

struct item {
	int index;                /* position in the playlist */
	struct media_info mi;
	GstElement *pipeline;
	GstAppSink *sink;
	gint state;               /* enum item_state, set by the worker */
	GstSample *first;         /* prerolled first frame */
	gint64 first_pts;
	gboolean skip_first, eos, yuv;
	int enc, range;           /* plane colour from the parser caps */
	struct dumbbuf *ring[ITEM_RING];
	int ring_next;
	char why[256];            /* why it failed */
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

	int wake_fd;              /* eventfd: popup change / stop while the presenter waits */
	struct playlist *pl;      /* playlist mode (NULL: two-file mode) */
	gboolean mirror;
	int max_loops;            /* 0: unlimited */
	gint64 image_duration_ns; /* > 0: overrides the playlist */
	gboolean prefetch;        /* prepare the next item while one plays (gpu_mem >= 128) */
	const char *mount_point;  /* the stick's mount point, watched for removal */

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

/* playlist.c */
struct playlist {
	char *file;               /* the playlist file */
	char *root;               /* directory the item paths are relative to */
	int image_duration_s;
	gboolean loop, autostart;
	GPtrArray *items;         /* absolute paths */
};
struct playlist *playlist_load(const char *file, const char *root, GError **err);
void playlist_free(struct playlist *pl);
int playlist_list(struct playlist *pl);

gboolean probe_file(const char *path, struct media_info *mi);
void probe_print(const struct media_info *mi);
const char *decode_path_name(enum decode_path d);

extern GQuark fb_quark;
extern int g_fd;

static inline gint64 mono_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (gint64)ts.tv_sec * GST_SECOND + ts.tv_nsec;
}

static inline void sleep_until(gint64 t)
{
	struct timespec ts = { .tv_sec = t / GST_SECOND, .tv_nsec = t % GST_SECOND };
	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
		;
}

/* drm.c: outputs, planes, framebuffers, vblank grid */
int read_sysfs(const char *card, const char *conn, const char *attr, char *buf, size_t len);
int connector_id(const char *card, const char *conn);
int find_outputs(char *card, size_t card_len, char conns[][32], int max);
uint32_t dumb_fb(int fd, int w, int h, uint32_t fourcc, void (*draw)(uint32_t *px, int stride, int w, int h));
double mode_hz(const drmModeModeInfo *m);
gboolean matches(double hz, double fps);
void set_refresh(int fd, uint32_t conn_id, double fps, int want_hz);
uint32_t prop_id(int fd, uint32_t obj, uint32_t type, const char *name, uint64_t *value);
uint32_t find_plane(int fd, int pipe, uint64_t type, uint32_t fourcc, const uint32_t *taken, int ntaken);
int plane_props(int fd, struct plane *pl, uint32_t id);
void plane_set(drmModeAtomicReq *req, const struct plane *pl, uint32_t fb, uint32_t crtc, int sw, int sh, int x, int y, int w, int h, int z);
int setup_output(int fd, struct output *o, uint32_t fourcc, const uint32_t *taken, int ntaken);
void saw_vblank(struct output *o, gint64 ts);
void sync_vblank(int fd, struct output *o);
void fb_free(gpointer data);
uint32_t drm_fourcc(GstVideoFormat f);
uint32_t sample_fb(GstSample *s, GstVideoInfo *info);
struct dumbbuf *dumb_create(int fd, int w, int h, uint32_t fourcc);
void dumb_destroy(int fd, struct dumbbuf *db);
gboolean dumb_copy(struct dumbbuf *db, GstSample *s);
void plane_set_color(drmModeAtomicReq *req, const struct plane *pl, int enc, int range);

/* item.c, mirror.c: playlist playback */
GstPadProbeReturn allocation_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
void item_prepare(struct item *it, const char *file, int disp_w, int disp_h);
void item_play(struct item *it);
GstSample *item_pull(struct item *it, GstClockTime timeout);
void item_free(struct item *it);
gpointer mirror_presenter(gpointer data);

/* input.c: signals, input grab, EXIT popup */
gboolean on_signal(gpointer data);
int has_bit(const unsigned long *bits, unsigned bit);
gboolean hide_popup(gpointer data);
gboolean popup_timeout(gpointer data);
void popup_rect(struct player *p, int *x, int *y);
void on_tap(struct player *p, gboolean have_pos, int x, int y);
int scale_abs(int v, const struct input_absinfo *a, int size);
gboolean on_input(gint fd, GIOCondition cond, gpointer data);
void watch_inputs(struct player *p);
void draw_popup(uint32_t *px, int stride, int w, int h);
void setup_popup(struct player *p);

/* presenter.c: pacing, atomic commits, statistics */
void on_flip(int fd, unsigned seq, unsigned sec, unsigned usec, unsigned crtc_id, void *data);
gint64 submit_time(struct player *p, gint64 v);
int commit(struct player *p, const uint32_t *fb, const GstVideoInfo *vi);
int commit_frames(struct player *p, const struct frame *const *f);
void player_wake(struct player *p);
void disable_planes(struct player *p);
gpointer presenter(gpointer data);
void print_stats(struct player *p);

#endif
