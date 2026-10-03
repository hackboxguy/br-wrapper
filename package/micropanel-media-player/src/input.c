/* input.c - signals, input grab and the EXIT popup (dual-video-player) */
#include "player.h"

/* evdev codes that make an input device interesting */
static const unsigned exit_keys[] = { BTN_TOUCH, BTN_LEFT, KEY_ENTER, KEY_ESC };

/* ---- input / signals ---- */

gboolean on_signal(gpointer data)
{
	struct player *p = data;
	g_printerr("dual-video-player: stopping\n");
	g_main_loop_quit(p->loop);
	return G_SOURCE_CONTINUE;
}

#define NLONGS(x) (((x) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
int has_bit(const unsigned long *bits, unsigned bit)
{
	return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1;
}

gboolean hide_popup(gpointer data)
{
	struct player *p = data;
	g_atomic_int_set(&p->popup_on, 0);
	player_wake(p);
	if (p->popup_timer)
		g_source_remove(p->popup_timer);
	p->popup_timer = 0;
	return G_SOURCE_REMOVE;
}

gboolean popup_timeout(gpointer data)
{
	struct player *p = data;
	p->popup_timer = 0;   /* this source ends by returning REMOVE */
	g_atomic_int_set(&p->popup_on, 0);
	player_wake(p);
	return G_SOURCE_REMOVE;
}

void popup_rect(struct player *p, int *x, int *y)
{
	*x = (p->out[0].w - POPUP_W) / 2;
	*y = (p->out[0].h - POPUP_H) / 2;
}

/*
 * A tap at display-1 coordinates (x, y), or a click without a position:
 * the first shows the EXIT button, a tap on it (or a second click) stops.
 */
void on_tap(struct player *p, gboolean have_pos, int x, int y)
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
		player_wake(p);
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

int scale_abs(int v, const struct input_absinfo *a, int size)
{
	int range = a->maximum - a->minimum + 1;
	return range > 0 ? (int)((gint64)(v - a->minimum) * size / range) : v;
}

gboolean on_input(gint fd, GIOCondition cond, gpointer data)
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
void watch_inputs(struct player *p)
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

/* 5x7 glyphs, rows top to bottom, bit 4 = leftmost column */
static const uint8_t glyph_E[7] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f };
static const uint8_t glyph_X[7] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 };
static const uint8_t glyph_I[7] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f };
static const uint8_t glyph_T[7] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };

/* EXIT button: red rounded rectangle, white border and text (premultiplied ARGB) */
void draw_popup(uint32_t *px, int stride, int w, int h)
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

/* plane + drawn framebuffer for the EXIT button on display 1 */
void setup_popup(struct player *p)
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
