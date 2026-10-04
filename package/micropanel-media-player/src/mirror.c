/* mirror.c - playlist playback, the same item on every output (dual-video-player --playlist)
 *
 * The presenter thread shows the items in order; a single worker thread
 * prepares the next item (probe, build, preroll) and tears finished ones
 * down, so neither blocks the presenter or the main loop.
 *
 * - Pacing: frame times come from each buffer's PTS relative to the item's
 *   first frame, mapped onto the master display's vblank grid, which runs on
 *   across items (mixed frame rates and variable frame rate need no special
 *   case). A frame that cannot make its vblank is shown late and counted.
 * - Item switch: the next item's first frame is committed and latched first;
 *   only then is the previous item's last frame released and its pipeline
 *   torn down (removing a framebuffer that is on a plane turns the plane off).
 * - Prefetch: the next item is prepared as soon as the current one is on
 *   screen. Below gpu_mem=128 (p->prefetch off) two hardware decoders starve
 *   or wedge the codec firmware, so the rule there is one hardware decoder
 *   alive: the next item is still probed ahead, and images and software items
 *   are prepared ahead as usual; a hardware item that follows a hardware item
 *   is held back (ITEM_PROBED) until the current one's last frame is copied
 *   and on screen and its pipeline freed. Probe results are kept per playlist
 *   index, so later loop passes do not probe again.
 * - Stills (images, the last frame of a video) wait on p->wake_fd, so a tap
 *   shows the EXIT button and EXIT stops at once.
 * - The stick's mount point is watched; if it goes, exit code 3.
 */
#include "player.h"
#include <malloc.h>

#define NO_FRAME_TIMEOUT (5 * GST_SECOND)   /* watchdog for a playing video */
#define MOUNT_CHECK_NS   (500 * GST_MSECOND)

enum job_kind { JOB_PREPARE, JOB_BUILD, JOB_FREE };
struct job {
	enum job_kind kind;
	struct item *it;
	char *file;
	gboolean hold_hw;          /* JOB_PREPARE: stop after the probe if it is a hardware item */
	gboolean have_known;
	struct media_info known;   /* JOB_PREPARE: the classification from an earlier pass */
};

struct mirror {
	struct player *p;
	struct output *m;          /* master display: its vblanks are the time base */
	GThreadPool *pool;         /* the worker: max one thread, jobs run in order */
	GMutex lock;
	GCond cond;
	int disp_w, disp_h;        /* largest output, for image scaling */
	gint64 image_ns;
	unsigned n;                /* playlist length */
	int next_index;            /* playlist index the next prepared item is */
	unsigned loops;            /* completed passes */
	struct item *next;         /* being prepared or ready */
	gint64 last_target;        /* master vblank of the last commit */
	gint64 next_target;        /* earliest vblank for the next item's first frame */
	gint64 item_end;           /* master vblank of the previous item's last own frame */
	struct frame shown;        /* on screen */
	GstSample *shown_sample;   /* the hardware sample behind shown.fb */
	struct dumbbuf *hold;      /* copy of a last frame, held while the next item prepares */
	gint64 mount_checked;
	unsigned hold0[2][8];      /* page-flip hold counters at the item's start */
	unsigned late0;
	unsigned failed_in_row;
	gboolean cur_hw;           /* the item on screen is a hardware item, its decoder alive */
	struct media_info *cache;  /* per playlist index: the classification ... */
	gboolean *cached;          /* ... once one exists */
};

static void job_run(gpointer data, gpointer user)
{
	struct job *j = data;
	struct mirror *mr = user;
	if (j->kind == JOB_PREPARE || j->kind == JOB_BUILD) {
		if (j->kind == JOB_BUILD)
			item_build(j->it, mr->disp_w, mr->disp_h);
		else if (item_probe(j->it, j->file, j->have_known ? &j->known : NULL)) {
			if (j->hold_hw && j->it->mi.decode == DECODE_HW)
				g_atomic_int_set(&j->it->state, ITEM_PROBED);
			else
				item_build(j->it, mr->disp_w, mr->disp_h);
		}
		g_mutex_lock(&mr->lock);
		g_cond_broadcast(&mr->cond);
		g_mutex_unlock(&mr->lock);
	} else {
		item_free(j->it);
		malloc_trim(0);   /* hand the item's freed buffers back to the system */
	}
	g_free(j->file);
	g_free(j);
}

static struct job *new_job(enum job_kind kind, struct item *it, const char *file)
{
	struct job *j = g_new0(struct job, 1);
	j->kind = kind;
	j->it = it;
	j->file = g_strdup(file);
	return j;
}

static void queue_job(struct mirror *mr, enum job_kind kind, struct item *it, const char *file)
{
	g_thread_pool_push(mr->pool, new_job(kind, it, file), NULL);
}

/* probe (or recall) and build the item at it->index */
static void queue_prepare(struct mirror *mr, struct item *it)
{
	struct job *j = new_job(JOB_PREPARE, it, g_ptr_array_index(mr->p->pl->items, it->index));
	j->hold_hw = !mr->p->prefetch && mr->cur_hw;
	if (mr->cached[it->index]) {
		j->have_known = TRUE;
		j->known = mr->cache[it->index];
	}
	g_thread_pool_push(mr->pool, j, NULL);
}

static gboolean stopping(struct mirror *mr)
{
	return g_atomic_int_get(&mr->p->stop) || mr->p->ret;
}

/* advance to the next playlist index (looping), FALSE at the end */
static gboolean advance(struct mirror *mr, int *index)
{
	if (++*index < (int)mr->n)
		return TRUE;
	mr->loops++;
	if (!mr->p->pl->loop || (mr->p->max_loops && mr->loops >= (unsigned)mr->p->max_loops))
		return FALSE;
	*index = 0;
	return TRUE;
}

/* start preparing the item after the one at @index, if there is one */
static void prefetch_after(struct mirror *mr, int index)
{
	if (mr->next)
		return;
	if (!advance(mr, &index))
		return;
	struct item *it = g_new0(struct item, 1);
	it->index = index;
	it->state = ITEM_PREPARING;
	mr->next = it;
	queue_prepare(mr, it);
}

/*
 * A prefetched item that failed (unsupported, missing, no frame) is skipped
 * right away, while the current item still plays, and the one after it is
 * prepared - so a run of bad items does not cost a pause at the boundary.
 */
static void skip_failed_next(struct mirror *mr)
{
	while (mr->next && g_atomic_int_get(&mr->next->state) == ITEM_FAILED) {
		struct item *it = mr->next;
		int index = it->index;
		mr->next = NULL;
		g_printerr("dual-video-player: item %d skipped: %s\n", index + 1, it->why);
		queue_job(mr, JOB_FREE, it, NULL);
		prefetch_after(mr, index);
	}
}

/* wait until mr->next is prepared (or stop); the worker signals mr->cond */
static struct item *take_next(struct mirror *mr)
{
	struct item *it = mr->next;
	if (!it)
		return NULL;
	g_mutex_lock(&mr->lock);
	while (g_atomic_int_get(&it->state) == ITEM_PREPARING && !stopping(mr))
		g_cond_wait_until(&mr->cond, &mr->lock, g_get_monotonic_time() + 100 * G_TIME_SPAN_MILLISECOND);
	g_mutex_unlock(&mr->lock);
	if (g_atomic_int_get(&it->state) == ITEM_PREPARING)
		return NULL;   /* stopping; the worker still owns it and will finish */
	mr->next = NULL;
	/* successes only: a failure may be transient (a slow read at start), so a
	 * failed item is probed again on the next pass */
	if (it->probed && it->mi.decode != DECODE_UNSUPPORTED && !mr->cached[it->index]) {
		mr->cache[it->index] = it->mi;
		mr->cached[it->index] = TRUE;
	}
	return it;
}

/* is the stick still mounted? */
static gboolean mount_present(struct mirror *mr)
{
	const char *mp = mr->p->mount_point;
	gint64 now = mono_ns();
	if (!mp || now - mr->mount_checked < MOUNT_CHECK_NS)
		return TRUE;
	mr->mount_checked = now;
	gchar *text = NULL;
	if (!g_file_get_contents("/proc/self/mountinfo", &text, NULL, NULL))
		return TRUE;
	gchar *needle = g_strdup_printf(" %s ", mp);
	gboolean present = strstr(text, needle) != NULL;
	g_free(needle);
	g_free(text);
	if (!present) {
		g_printerr("dual-video-player: %s is gone (stick removed)\n", mp);
		mr->p->ret = 3;
	}
	return present;
}

/* a decoded sample as a framebuffer: imported (hw) or copied into the item's dumb buffers */
static gboolean make_frame(struct item *it, GstSample *s, struct frame *f)
{
	GstVideoInfo vi;
	if (!gst_video_info_from_caps(&vi, gst_sample_get_caps(s)))
		return FALSE;
	*f = (struct frame){ .w = GST_VIDEO_INFO_WIDTH(&vi), .h = GST_VIDEO_INFO_HEIGHT(&vi),
			     .par_n = GST_VIDEO_INFO_PAR_N(&vi), .par_d = GST_VIDEO_INFO_PAR_D(&vi),
			     .enc = it->yuv ? it->enc : -1, .range = it->range };
	if (it->mi.decode == DECODE_HW)
		return (f->fb = sample_fb(s, &vi)) != 0;
	int slot = 0;
	if (it->mi.decode == DECODE_SW) {
		slot = it->ring_next;
		it->ring_next = (it->ring_next + 1) % ITEM_RING;
	}
	struct dumbbuf **db = &it->ring[slot];
	if (*db && ((*db)->w != (f->w & ~1) || (*db)->h != (f->h & ~1))) {
		dumb_destroy(g_fd, *db);   /* not on screen: that is another ring slot */
		*db = NULL;
	}
	if (!*db)
		*db = dumb_create(g_fd, f->w, f->h, it->yuv ? DRM_FORMAT_YUV420 : DRM_FORMAT_XRGB8888);
	if (!*db || !dumb_copy(*db, s))
		return FALSE;
	f->fb = (*db)->fb;
	f->w = (*db)->w;
	f->h = (*db)->h;
	return TRUE;
}

/*
 * Commit @f to every output at the master vblank @target or the first one
 * still reachable after it. Returns the vblank used, -1 on a commit error.
 * @paced: a video frame due at @target - missing it counts as late. First
 * frames and re-commits just take the next vblank (a boundary that is late
 * because the item was not ready shows in the boundary statistics).
 */
static gint64 show(struct mirror *mr, const struct frame *f, gint64 target, gboolean paced)
{
	struct player *p = mr->p;
	struct output *m = mr->m;
	if (target <= mr->last_target)
		target = mr->last_target + 1;
	gint64 now = mono_ns(), t;
	gboolean counted = FALSE;
	for (;;) {
		gint64 v = m->last_ts + (target - m->last_idx) * m->period;
		t = submit_time(p, v);
		if (t > now + GST_SECOND) {
			for (int i = 0; i < p->nout; i++)
				sync_vblank(p->fd, &p->out[i]);   /* lost the grid: restart it */
			target = m->last_idx + 2;
			now = mono_ns();
			continue;
		}
		if (t > now + 300 * 1000)
			break;
		target++;   /* too late for this vblank */
		if (paced && !counted) {
			p->late++;   /* late frames, not missed vblanks */
			counted = TRUE;
		}
	}
	sleep_until(t);
	const struct frame *fs[2] = { f, f };
	int r = commit_frames(p, fs);
	if (r) {
		g_printerr("dual-video-player: commit failed: %s\n", strerror(-r));
		p->ret = 1;
		return -1;
	}
	mr->shown = *f;
	mr->last_target = target;
	return target;
}

/* the master vblank happening now (the grid only advances with page flips, so extrapolate) */
static gint64 now_idx(const struct output *m)
{
	return m->last_idx + (mono_ns() - m->last_ts) / m->period;
}

/*
 * Keep what is on screen until master vblank @until (the next item's
 * slot). Re-commits on popup changes, returns early on stop or removal.
 */
static void hold_until(struct mirror *mr, gint64 until)
{
	struct output *m = mr->m;
	int popup = g_atomic_int_get(&mr->p->popup_on);
	while (!stopping(mr) && mount_present(mr)) {
		skip_failed_next(mr);
		gint64 at = m->last_ts + (until - 1 - m->last_idx) * m->period;   /* commit point for @until */
		gint64 left = at - mono_ns();
		if (left <= 0)
			return;
		struct pollfd pfd = { .fd = mr->p->wake_fd, .events = POLLIN };
		int ms = (int)MIN(left / GST_MSECOND + 1, MOUNT_CHECK_NS / GST_MSECOND);
		if (poll(&pfd, 1, ms) > 0) {
			uint64_t v;
			if (read(mr->p->wake_fd, &v, sizeof(v)) < 0)
				v = 0;
		}
		int now_popup = g_atomic_int_get(&mr->p->popup_on);
		if (now_popup != popup && !stopping(mr)) {
			popup = now_popup;
			struct frame f = mr->shown;
			/* re-commit at the next vblank (not a late frame: nothing was due) */
			mr->last_target = MAX(mr->last_target, now_idx(m));
			if (show(mr, &f, mr->last_target + 1, FALSE) < 0)
				return;
		}
	}
}

static void stats_begin(struct mirror *mr)
{
	for (int i = 0; i < mr->p->nout; i++)
		memcpy(mr->hold0[i], mr->p->out[i].hold, sizeof(mr->hold0[i]));
	mr->late0 = mr->p->late;
}

/* per item: how long each frame stayed on the master display (videos) */
static void stats_end(struct mirror *mr, const struct item *it, unsigned frames)
{
	if (it->mi.kind != MEDIA_VIDEO)
		return;
	struct output *o = mr->m;
	int mi = (int)(o - mr->p->out);
	GString *s = g_string_new(NULL);
	for (int h = 1; h < 8; h++) {
		unsigned d = o->hold[h] - mr->hold0[mi][h];
		if (d)
			g_string_append_printf(s, " %dx%u", h, d);
	}
	unsigned d8 = o->hold[0] - mr->hold0[mi][0];
	if (d8)
		g_string_append_printf(s, " >=8x%u", d8);
	gchar *name = g_path_get_basename(it->mi.file);
	g_print("dual-video-player: item %d %s: %u frames (%s), held (vblanks x count):%s, late %u\n",
		it->index + 1, name, frames, decode_path_name(it->mi.decode), s->str,
		mr->p->late - mr->late0);
	g_free(name);
	g_string_free(s, TRUE);
}

/* the previous item's frame left the screen: release it and its pipeline */
static void retire(struct mirror *mr, struct item **old)
{
	if (mr->shown_sample) {
		gst_sample_unref(mr->shown_sample);
		mr->shown_sample = NULL;
	}
	if (*old) {
		queue_job(mr, JOB_FREE, *old, NULL);
		*old = NULL;
	}
	if (mr->hold) {
		dumb_destroy(g_fd, mr->hold);
		mr->hold = NULL;
	}
}

/*
 * One hardware decoder at a time (gpu_mem below 128): before a held-back
 * hardware item is built, copy the current hardware frame (NV12) into a dumb
 * buffer and show that, so the item's pipeline - and its decoder - can go
 * away while its picture stays on screen.
 */
static void detach_hw_frame(struct mirror *mr, struct item *it)
{
	if (!mr->shown_sample || it->mi.decode != DECODE_HW)
		return;
	struct frame f = mr->shown;
	struct dumbbuf *db = dumb_create(g_fd, f.w, f.h, DRM_FORMAT_YUV420);
	if (db && dumb_copy(db, mr->shown_sample)) {
		f.fb = db->fb;
		if (show(mr, &f, mr->last_target + 1, FALSE) >= 0) {
			gst_sample_unref(mr->shown_sample);
			mr->shown_sample = NULL;
			mr->hold = db;
			return;
		}
	}
	dumb_destroy(g_fd, db);   /* failed: the next item shows after a gap */
}

/* play one prepared item; on return its last frame is still on screen */
static void play_item(struct mirror *mr, struct item *it, struct item **old)
{
	struct frame f;
	if (!make_frame(it, it->first, &f)) {
		g_printerr("dual-video-player: item %d: cannot show its frame\n", it->index + 1);
		return;
	}
	GstSample *first = it->mi.decode == DECODE_HW ? gst_sample_ref(it->first) : NULL;
	gint64 start = show(mr, &f, mr->next_target, FALSE);
	if (start < 0) {
		if (first)
			gst_sample_unref(first);
		return;
	}
	if (*old || mr->shown_sample || mr->hold)
		g_print("dual-video-player: boundary to item %d: previous frame held %" G_GINT64_FORMAT
			" vblanks (due %" G_GINT64_FORMAT ")\n", it->index + 1, start - mr->item_end,
			mr->next_target - mr->item_end);
	retire(mr, old);
	mr->shown_sample = first;
	mr->cur_hw = it->mi.decode == DECODE_HW;
	prefetch_after(mr, it->index);
	stats_begin(mr);

	if (it->mi.kind == MEDIA_IMAGE) {
		gint64 n = MAX(1, llround((double)mr->image_ns / mr->m->period));
		mr->next_target = start + n;
		mr->item_end = start;   /* re-commits for the EXIT button do not move it */
		hold_until(mr, mr->next_target);
		return;
	}

	item_play(it);
	unsigned frames = 1;
	gint64 frame_ns = it->mi.fps_n > 0 ? GST_SECOND * it->mi.fps_d / it->mi.fps_n : 40 * GST_MSECOND;
	gint64 last_seen = mono_ns();
	while (!stopping(mr) && mount_present(mr)) {
		skip_failed_next(mr);
		GstSample *s = item_pull(it, 200 * GST_MSECOND);
		if (!s) {
			if (it->eos)
				break;
			if (it->why[0]) {
				g_printerr("dual-video-player: item %d: %s\n", it->index + 1, it->why);
				break;
			}
			if (mono_ns() - last_seen > NO_FRAME_TIMEOUT) {
				g_printerr("dual-video-player: item %d: no frame for %d s, skipping\n",
					   it->index + 1, (int)(NO_FRAME_TIMEOUT / GST_SECOND));
				break;
			}
			continue;
		}
		last_seen = mono_ns();
		GstBuffer *b = gst_sample_get_buffer(s);
		gint64 pts = GST_BUFFER_PTS_IS_VALID(b) ? (gint64)GST_BUFFER_PTS(b) - it->first_pts : 0;
		if (GST_BUFFER_DURATION_IS_VALID(b))
			frame_ns = (gint64)GST_BUFFER_DURATION(b);
		gint64 target = start + llround((double)pts / mr->m->period);
		if (!make_frame(it, s, &f)) {
			gst_sample_unref(s);
			continue;
		}
		if (show(mr, &f, target, TRUE) < 0) {
			gst_sample_unref(s);
			break;
		}
		frames++;
		if (mr->shown_sample)
			gst_sample_unref(mr->shown_sample);
		if (it->mi.decode == DECODE_HW)
			mr->shown_sample = s;
		else {
			mr->shown_sample = NULL;
			gst_sample_unref(s);
		}
	}
	stats_end(mr, it, frames);
	mr->item_end = mr->last_target;
	mr->next_target = mr->last_target + MAX(1, llround((double)frame_ns / mr->m->period));
}

gpointer mirror_presenter(gpointer data)
{
	struct player *p = data;
	struct mirror mr = { .p = p, .m = &p->out[p->master] };
	g_mutex_init(&mr.lock);
	g_cond_init(&mr.cond);
	mr.n = p->pl->items->len;
	mr.image_ns = p->image_duration_ns > 0 ? p->image_duration_ns :
		      (gint64)p->pl->image_duration_s * GST_SECOND;
	for (int i = 0; i < p->nout; i++) {
		mr.disp_w = MAX(mr.disp_w, p->out[i].w);
		mr.disp_h = MAX(mr.disp_h, p->out[i].h);
	}
	mr.pool = g_thread_pool_new(job_run, &mr, 1, TRUE, NULL);
	mr.cache = g_new0(struct media_info, mr.n);
	mr.cached = g_new0(gboolean, mr.n);

	for (int i = 0; i < p->nout; i++)
		sync_vblank(p->fd, &p->out[i]);
	mr.last_target = mr.m->last_idx;
	mr.next_target = mr.m->last_idx + 2;

	struct item *old = NULL;
	mr.next = g_new0(struct item, 1);
	mr.next->state = ITEM_PREPARING;
	queue_prepare(&mr, mr.next);
	unsigned shown_items = 0;

	while (!stopping(&mr) && mount_present(&mr)) {
		struct item *it = take_next(&mr);
		if (!it)
			break;
		if (g_atomic_int_get(&it->state) == ITEM_FAILED) {
			g_printerr("dual-video-player: item %d skipped: %s\n", it->index + 1, it->why);
			int index = it->index;
			queue_job(&mr, JOB_FREE, it, NULL);
			if (++mr.failed_in_row >= mr.n) {
				mr.mount_checked = 0;   /* a removed stick also fails every item: say which */
				if (!mount_present(&mr))
					break;
				g_printerr("dual-video-player: nothing playable\n");
				p->ret = 1;
				break;
			}
			prefetch_after(&mr, index);
			if (!mr.next)
				break;   /* end of the list */
			continue;
		}
		if (g_atomic_int_get(&it->state) == ITEM_PROBED) {
			/* a hardware item after a hardware item, one decoder at a time:
			 * hold the last frame as a copy, free the old pipeline, then build */
			detach_hw_frame(&mr, old);
			if (mr.shown_sample) {   /* could not copy: drop the frame, accept a gap */
				gst_sample_unref(mr.shown_sample);
				mr.shown_sample = NULL;
			}
			if (old)
				queue_job(&mr, JOB_FREE, old, NULL);
			old = NULL;
			mr.cur_hw = FALSE;
			g_atomic_int_set(&it->state, ITEM_PREPARING);
			mr.next = it;
			queue_job(&mr, JOB_BUILD, it, NULL);   /* runs after the free: same worker, in order */
			continue;
		}
		mr.failed_in_row = 0;
		play_item(&mr, it, &old);
		shown_items++;
		old = it;   /* its last frame stays until the next item's first one is latched */
		if (stopping(&mr))
			break;
		if (!mr.next)
			break;   /* end of the list (no loop, or --max-loops reached) */
	}

	if (mr.shown_sample)
		gst_sample_unref(mr.shown_sample);
	disable_planes(p);
	if (mr.hold)
		dumb_destroy(g_fd, mr.hold);
	if (old)
		queue_job(&mr, JOB_FREE, old, NULL);
	if (mr.next) {
		/* wait for a running preparation before freeing it */
		while (g_atomic_int_get(&mr.next->state) == ITEM_PREPARING)
			g_usleep(20000);   /* an ITEM_PROBED item has nothing running: free it as it is */
		queue_job(&mr, JOB_FREE, mr.next, NULL);
	}
	g_thread_pool_free(mr.pool, FALSE, TRUE);   /* runs the queued frees */
	g_free(mr.cache);
	g_free(mr.cached);
	g_mutex_clear(&mr.lock);
	g_cond_clear(&mr.cond);
	g_print("dual-video-player: STATS\titems_shown=%u\tloops=%u\tlate=%u\texit=%d\n",
		shown_items, mr.loops, p->late, p->ret);
	g_main_loop_quit(p->loop);
	return NULL;
}
