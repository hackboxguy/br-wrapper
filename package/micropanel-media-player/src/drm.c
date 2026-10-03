/* drm.c - outputs, planes, framebuffers and the vblank grid (dual-video-player) */
#include "player.h"

GQuark fb_quark;
int g_fd = -1;

/* ---- connectors (sysfs) ---- */

/* sysfs: /sys/class/drm/<card>-<connector>/{status,connector_id} */
int read_sysfs(const char *card, const char *conn, const char *attr,
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

int connector_id(const char *card, const char *conn)
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
int find_outputs(char *card, size_t card_len, char conns[][32], int max)
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

/* ---- framebuffers we draw ourselves ---- */

/* a w x h dumb buffer framebuffer; @draw fills it (NULL: all zero) */
uint32_t dumb_fb(int fd, int w, int h, uint32_t fourcc,
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

/* ---- refresh matching ---- */

double mode_hz(const drmModeModeInfo *m)
{
	double hz = m->clock * 1000.0 / ((double)m->htotal * m->vtotal);
	if (m->flags & DRM_MODE_FLAG_INTERLACE)
		hz *= 2;
	return hz;
}

/* refresh shows every frame for the same number of vblanks (0.5% tolerance) */
gboolean matches(double hz, double fps)
{
	double r = hz / fps;
	return r >= 0.99 && fabs(r - round(r)) < 0.005 * r;
}

/*
 * Switch the CRTC driving @conn_id to a mode with the same resolution and a
 * refresh that matches @fps (or exactly @want_hz). The primary plane gets a
 * black dumb buffer; the video goes on an overlay plane.
 */
void set_refresh(int fd, uint32_t conn_id, double fps, int want_hz)
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

uint32_t prop_id(int fd, uint32_t obj, uint32_t type, const char *name,
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
uint32_t find_plane(int fd, int pipe, uint64_t type, uint32_t fourcc,
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

int plane_props(int fd, struct plane *pl, uint32_t id)
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
void plane_set(drmModeAtomicReq *req, const struct plane *pl, uint32_t fb,
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
int setup_output(int fd, struct output *o, uint32_t fourcc,
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

/* advance @o's vblank grid to the vblank at @ts */
void saw_vblank(struct output *o, gint64 ts)
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
void sync_vblank(int fd, struct output *o)
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

void fb_free(gpointer data)
{
	drmModeRmFB(g_fd, GPOINTER_TO_UINT(data));
}

uint32_t drm_fourcc(GstVideoFormat f)
{
	switch (f) {
	case GST_VIDEO_FORMAT_I420: return DRM_FORMAT_YUV420;
	case GST_VIDEO_FORMAT_YV12: return DRM_FORMAT_YVU420;
	case GST_VIDEO_FORMAT_NV12: return DRM_FORMAT_NV12;
	case GST_VIDEO_FORMAT_NV21: return DRM_FORMAT_NV21;
	default: return 0;
	}
}

uint32_t sample_fb(GstSample *s, GstVideoInfo *info)
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
