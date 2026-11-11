/* presenter.c - frame pacing, atomic commits for both displays, statistics (dual-video-player) */
#include "player.h"

/* ---- presenter ---- */

void on_flip(int fd, unsigned seq, unsigned sec, unsigned usec,
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
gint64 submit_time(struct player *p, gint64 v)
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

/* fit @f into @o keeping its display aspect (pixel aspect ratio honoured), up or down, centred */
static void fit(const struct output *o, const struct frame *f, int *x, int *y, int *w, int *h)
{
	double aspect = (double)f->w * MAX(f->par_n, 1) / ((double)f->h * MAX(f->par_d, 1));
	int dw = o->w, dh = (int)lround(o->w / aspect);
	if (dh > o->h) {
		dh = o->h;
		dw = (int)lround(o->h * aspect);
	}
	*w = MAX(2, dw & ~1);
	*h = MAX(2, dh & ~1);
	*x = (o->w - *w) / 2;
	*y = (o->h - *h) / 2;
}

/*
 * Put f[i] on output i's video plane (plus the black background on the
 * first commit and the EXIT button), all outputs in ONE atomic commit, and
 * wait until every display has latched it. Mirror mode passes the same
 * frame for every output.
 */
int commit_frames(struct player *p, const struct frame *const *f)
{
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	for (int i = 0; i < p->nout; i++) {
		struct output *o = &p->out[i];
		int x, y, w, h;
		fit(o, f[i], &x, &y, &w, &h);
		if (o->black_fb && !o->primary_set)
			plane_set(req, &o->primary, o->black_fb, o->crtc_id, o->w, o->h,
				  0, 0, o->w, o->h, 0);
		plane_set(req, &o->video, f[i]->fb, o->crtc_id, f[i]->w, f[i]->h, x, y, w, h, 1);
		plane_set_color(req, &o->video, f[i]->enc, f[i]->range);
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

/* two-file mode: stream i's frame on output i, plane colour left alone */
int commit(struct player *p, const uint32_t *fb, const GstVideoInfo *vi)
{
	struct frame fr[2];
	const struct frame *f[2];
	for (int i = 0; i < p->nout; i++) {
		fr[i] = (struct frame){ .fb = fb[i], .w = GST_VIDEO_INFO_WIDTH(&vi[i]),
					.h = GST_VIDEO_INFO_HEIGHT(&vi[i]),
					.par_n = GST_VIDEO_INFO_PAR_N(&vi[i]),
					.par_d = GST_VIDEO_INFO_PAR_D(&vi[i]), .enc = -1, .range = 0 };
		f[i] = &fr[i];
	}
	return commit_frames(p, f);
}

/* wake the presenter out of a wait (popup shown/hidden, stop) */
void player_wake(struct player *p)
{
	uint64_t one = 1;
	if (p->wake_fd >= 0 && write(p->wake_fd, &one, sizeof(one)) < 0)
		return;
}

/* take our video planes and the button off screen (the launcher's fbdev
 * console takes the displays back when the fd is closed) */
void disable_planes(struct player *p)
{
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	for (int i = 0; i < p->nout; i++)
		plane_set(req, &p->out[i].video, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if (p->popup.id)
		plane_set(req, &p->popup, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	drmModeAtomicCommit(p->fd, req, 0, NULL);
	drmModeAtomicFree(req);
}

static gint64 sample_pts(GstSample *s)
{
	GstBuffer *b = s ? gst_sample_get_buffer(s) : NULL;
	return b && GST_BUFFER_PTS_IS_VALID(b) ? (gint64)GST_BUFFER_PTS(b) : -1;
}

/* wait for the next frame of every stream after a seek (paused: to show it) */
static gboolean pull_all(struct player *p, GstSample **next, GstClockTime timeout)
{
	for (int i = 0; i < p->nout; i++)
		while (!next[i] && !g_atomic_int_get(&p->stop)) {
			next[i] = gst_app_sink_try_pull_sample(p->sink[i], timeout);
			if (!next[i])
				return FALSE;
		}
	return !g_atomic_int_get(&p->stop);
}

/*
 * Two-file mode, control socket: hold the frames on screen while paused and
 * carry out seeks (both files in one flushing, accurate seek on the shared
 * pipeline, so frame N stays paired; it decodes from the keyframe before the
 * target, 0.6-3.5 s on a 1080p60 clip). Returns TRUE when the frame timing must restart
 * from the next vblank (after a pause or a seek).
 */
static gboolean twofile_control(struct player *p, GstSample **shown, GstSample **next, gint64 *pos,
				gint64 dur)
{
	gboolean changed = FALSE;
	int popup = g_atomic_int_get(&p->popup_on);
	while (!g_atomic_int_get(&p->stop)) {
		gint64 t;
		if (control_take_seek(p, *pos, &t)) {
			t = MAX(0, t);
			if (dur > 0 && t > dur - GST_SECOND)
				t = 0;   /* past the end: from the start, as the loop would */
			for (int i = 0; i < 2; i++)
				if (next[i]) {
					gst_sample_unref(next[i]);
					next[i] = NULL;
				}
			GstSeekFlags fl = GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE;
			if (p->looping)
				fl |= GST_SEEK_FLAG_SEGMENT;
			if (!gst_element_seek(p->pipeline, 1.0, GST_FORMAT_TIME, fl, GST_SEEK_TYPE_SET, t,
					      GST_SEEK_TYPE_NONE, 0))
				g_printerr("dual-video-player: control: seek to %.1f s failed\n", t / 1e9);
			else
				g_print("dual-video-player: control: seek to %.1f s\n", t / 1e9);
			*pos = t;
			changed = TRUE;
			if (control_paused(p) && pull_all(p, next, 3 * GST_SECOND)) {
				/* frozen: show where the seek landed */
				uint32_t fb[2] = { 0, 0 };
				GstVideoInfo vi[2];
				gboolean ok = TRUE;
				for (int i = 0; i < p->nout; i++)
					ok &= (fb[i] = sample_fb(next[i], &vi[i])) != 0;
				if (ok && !commit(p, fb, vi))
					for (int i = 0; i < p->nout; i++) {
						if (shown[i])
							gst_sample_unref(shown[i]);
						shown[i] = next[i];
						next[i] = NULL;
					}
				*pos = MAX(0, sample_pts(shown[0]));
			}
		}
		if (!control_paused(p))
			break;
		if (!changed)
			g_print("dual-video-player: control: paused at %.1f s\n", *pos / 1e9);
		changed = TRUE;
		control_publish(p, "paused", -1, 0, NULL, *pos, dur);
		struct pollfd pfd = { .fd = p->wake_fd, .events = POLLIN };
		if (poll(&pfd, 1, 200) > 0) {
			uint64_t v;
			if (read(p->wake_fd, &v, sizeof(v)) < 0)
				v = 0;
		}
		int now_popup = g_atomic_int_get(&p->popup_on);
		if (now_popup != popup && shown[0]) {
			/* the EXIT button came or went: show the same frames with it */
			popup = now_popup;
			uint32_t fb[2] = { 0, 0 };
			GstVideoInfo vi[2];
			gboolean ok = TRUE;
			for (int i = 0; i < p->nout; i++)
				ok &= shown[i] && (fb[i] = sample_fb(shown[i], &vi[i])) != 0;
			if (ok)
				commit(p, fb, vi);
		}
	}
	if (changed && !control_paused(p))
		g_print("dual-video-player: control: playing from %.1f s\n", *pos / 1e9);
	return changed;
}

gpointer presenter(gpointer data)
{
	struct player *p = data;
	struct output *m = &p->out[p->master];
	GstSample *shown[2] = { NULL, NULL }, *next[2] = { NULL, NULL };
	GstVideoInfo vi[2];
	guint64 k = 0;
	gint64 n0 = -1, last_target = -1;
	double ratio = 1e9 / m->period / p->fps;   /* master vblanks per frame */
	gint64 pos = 0, dur = 0;
	gboolean rebase = FALSE;
	gst_element_query_duration(p->pipeline, GST_FORMAT_TIME, &dur);

	while (!g_atomic_int_get(&p->stop)) {
		uint32_t fb[2] = { 0, 0 };
		gboolean eos = FALSE;
		if ((control_paused(p) || control_seek_pending(p)) && n0 >= 0)
			rebase |= twofile_control(p, shown, next, &pos, dur);
		if (g_atomic_int_get(&p->stop))
			break;
		for (int i = 0; i < p->nout && !g_atomic_int_get(&p->stop); i++) {
			while (!next[i] && !g_atomic_int_get(&p->stop)) {
				next[i] = gst_app_sink_try_pull_sample(p->sink[i], 100 * GST_MSECOND);
				/* the end: EOS (--once), or nothing more after a segment end
				 * that found the loop switched off (if it is on again by
				 * now, the restart below takes over) */
				if (!next[i] && (gst_app_sink_is_eos(p->sink[i]) ||
						 g_atomic_int_get(&p->segment_done))) {
					eos = TRUE;
					break;
				}
			}
			if (eos)
				break;
		}
		if (eos && g_atomic_int_get(&p->looping) && !g_atomic_int_get(&p->stop)) {
			/* loop switched (back) on at the very end: from the start again */
			for (int i = 0; i < 2; i++)
				if (next[i]) {
					gst_sample_unref(next[i]);
					next[i] = NULL;
				}
			g_atomic_int_set(&p->segment_done, 0);
			if (gst_element_seek(p->pipeline, 1.0, GST_FORMAT_TIME,
					     GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_SEGMENT,
					     GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, 0)) {
				g_print("dual-video-player: loop (restarted)\n");
				rebase = TRUE;
				continue;
			}
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
		if (rebase) {
			/* after a pause or a seek: the next frame at the next vblank but one */
			gint64 now_v = m->last_idx + (mono_ns() - m->last_ts) / m->period;
			n0 = now_v + 2 - llround((double)k * ratio);
			last_target = -1;
			rebase = FALSE;
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
		pos = MAX(0, sample_pts(next[0]));
		control_publish(p, "playing", -1, 0, NULL, pos, dur);
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
void print_stats(struct player *p)
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
