/* item.c - one playlist item: its pipeline, first frame and frames (dual-video-player)
 *
 * Paths (chosen by probe_file() before anything is built):
 *   hw     filesrc ! demux ! h264parse ! [colorimetry fix] ! v4l2h264dec ! appsink
 *          (dmabuf, zero copy: the frames are imported as DRM framebuffers)
 *   sw     filesrc ! demux ! h264parse|h265parse ! avdec_* ! appsink (I420 in
 *          system memory, copied into a ring of dumb buffers by the presenter)
 *   image  filesrc ! jpegparse ! jpegdec | pngdec ! videoscale ! videoflip
 *          (EXIF orientation) ! videoconvert ! appsink (BGRx, one frame)
 *
 * An item is prepared (PAUSED, first frame prerolled) by the worker thread
 * while the previous item plays, then started (PLAYING) when it is shown.
 * Pipelines are built from elements, never from parse_launch strings: stick
 * file names are arbitrary.
 */
#include "player.h"

#define PREPARE_TIMEOUT (5 * GST_SECOND)   /* watchdog: no first frame by then = failed */

/*
 * Announce GstVideoMeta support in the appsink's ALLOCATION query. For 1080p
 * the decoder's buffers are 1920x1088 (padded); without the meta it cannot
 * describe the padding, so v4l2h264dec copies each frame into system memory
 * and sample_fb() cannot import it. With it the dmabuf comes through as is,
 * the plane offsets/strides in the meta.
 */
GstPadProbeReturn allocation_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	GstQuery *q = GST_PAD_PROBE_INFO_QUERY(info);
	(void)pad;
	(void)data;
	if (GST_QUERY_TYPE(q) != GST_QUERY_ALLOCATION)
		return GST_PAD_PROBE_OK;
	gst_query_add_allocation_meta(q, GST_VIDEO_META_API_TYPE, NULL);
	return GST_PAD_PROBE_HANDLED;
}

static GstElement *make(const char *factory)
{
	GstElement *e = gst_element_factory_make(factory, NULL);
	if (!e)
		g_printerr("dual-video-player: GStreamer element %s missing\n", factory);
	return e;
}

/* demuxer by container: qtdemux for mp4/mov/m4v, matroskademux for mkv */
static GstElement *make_demux(const char *file)
{
	const char *dot = strrchr(file, '.');
	return make(dot && !g_ascii_strcasecmp(dot, ".mkv") ? "matroskademux" : "qtdemux");
}

/*
 * The V4L2 H.264 decoder only accepts colorimetries it knows; a stream with
 * an incomplete VUI colour description (e.g. BT.709 matrix but unspecified
 * primaries/transfer, caps "0:3:0:0") fails not-negotiated. Complete it on
 * the decoder's sink pad: bt709 for HD, bt601 for SD.
 */
static GstPadProbeReturn colorimetry_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	struct item *it = data;
	GstEvent *ev = GST_PAD_PROBE_INFO_EVENT(info);
	(void)pad;
	if (GST_EVENT_TYPE(ev) != GST_EVENT_CAPS)
		return GST_PAD_PROBE_OK;
	GstCaps *caps;
	gst_event_parse_caps(ev, &caps);
	GstStructure *st = gst_caps_get_structure(caps, 0);
	const char *cm = gst_structure_get_string(st, "colorimetry");
	GstVideoColorimetry c;
	gboolean parsed = cm && gst_video_colorimetry_from_string(&c, cm);
	gboolean complete = parsed &&
			    c.matrix != GST_VIDEO_COLOR_MATRIX_UNKNOWN &&
			    c.transfer != GST_VIDEO_TRANSFER_UNKNOWN &&
			    c.primaries != GST_VIDEO_COLOR_PRIMARIES_UNKNOWN;
	int h = 0;
	gst_structure_get_int(st, "height", &h);
	/* plane colour comes from the parser caps: the decoder's output says bt601 regardless */
	it->enc = (parsed && c.matrix == GST_VIDEO_COLOR_MATRIX_BT2020) ? 2 :
		  (parsed && c.matrix == GST_VIDEO_COLOR_MATRIX_BT601) ? 0 :
		  (parsed && c.matrix == GST_VIDEO_COLOR_MATRIX_BT709) ? 1 : (h > 576 ? 1 : 0);
	it->range = (parsed && c.range == GST_VIDEO_COLOR_RANGE_0_255) ? 1 : 0;
	if (complete)
		return GST_PAD_PROBE_OK;
	GstCaps *fixed = gst_caps_copy(caps);
	gst_caps_set_simple(fixed, "colorimetry", G_TYPE_STRING, h > 576 ? "bt709" : "bt601", NULL);
	GstEvent *nev = gst_event_new_caps(fixed);
	gst_caps_unref(fixed);
	gst_event_unref(ev);
	GST_PAD_PROBE_INFO_DATA(info) = nev;
	return GST_PAD_PROBE_OK;
}

/* software decoders: colour from the parser caps too, no fix needed */
static GstPadProbeReturn colour_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	struct item *it = data;
	GstEvent *ev = GST_PAD_PROBE_INFO_EVENT(info);
	(void)pad;
	if (GST_EVENT_TYPE(ev) == GST_EVENT_CAPS) {
		GstCaps *caps;
		gst_event_parse_caps(ev, &caps);
		const GstStructure *st = gst_caps_get_structure(caps, 0);
		const char *cm = gst_structure_get_string(st, "colorimetry");
		GstVideoColorimetry c;
		int h = 0;
		gst_structure_get_int(st, "height", &h);
		gboolean ok = cm && gst_video_colorimetry_from_string(&c, cm);
		it->enc = ok && c.matrix == GST_VIDEO_COLOR_MATRIX_BT2020 ? 2 :
			  ok && c.matrix == GST_VIDEO_COLOR_MATRIX_BT601 ? 0 :
			  ok && c.matrix == GST_VIDEO_COLOR_MATRIX_BT709 ? 1 : (h > 576 ? 1 : 0);
		it->range = ok && c.range == GST_VIDEO_COLOR_RANGE_0_255 ? 1 : 0;
	}
	return GST_PAD_PROBE_OK;
}

static void on_demux_pad(GstElement *demux, GstPad *pad, gpointer data)
{
	GstElement *parse = data;
	(void)demux;
	GstCaps *caps = gst_pad_get_current_caps(pad);
	if (!caps)
		caps = gst_pad_query_caps(pad, NULL);
	const char *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
	if (g_str_has_prefix(name, "video/")) {
		GstPad *sinkpad = gst_element_get_static_pad(parse, "sink");
		if (!gst_pad_is_linked(sinkpad))
			gst_pad_link(pad, sinkpad);
		gst_object_unref(sinkpad);
	}
	gst_caps_unref(caps);
}

/* image target size: fit the (rotated) image into @dw x @dh, never upscale, even sizes */
static void image_target(const struct media_info *mi, int dw, int dh, int *sw, int *sh)
{
	gboolean quarter = mi->rotation == 90 || mi->rotation == 270;
	int rw = quarter ? mi->height : mi->width, rh = quarter ? mi->width : mi->height;
	double s = MIN(1.0, MIN((double)dw / rw, (double)dh / rh));
	int tw = MAX(2, ((int)(rw * s)) & ~1), th = MAX(2, ((int)(rh * s)) & ~1);
	*sw = quarter ? th : tw;   /* videoscale runs before videoflip */
	*sh = quarter ? tw : th;
}

static gboolean build(struct item *it, int disp_w, int disp_h)
{
	const struct media_info *mi = &it->mi;
	GstElement *pl = gst_pipeline_new(NULL);
	GstElement *src = make("filesrc"), *sink = make("appsink");
	if (!src || !sink)
		goto fail;
	g_object_set(src, "location", mi->file, NULL);
	gst_bin_add_many(GST_BIN(pl), src, sink, NULL);
	GstCaps *caps = NULL;

	if (mi->decode == DECODE_IMAGE) {
		gboolean jpeg = !strcmp(mi->codec, "jpeg");
		GstElement *parse = jpeg ? make("jpegparse") : NULL;
		GstElement *dec = make(jpeg ? "jpegdec" : "pngdec");
		GstElement *scale = make("videoscale"), *sizef = make("capsfilter");
		GstElement *flip = make("videoflip"), *conv = make("videoconvert");
		if (!dec || !scale || !sizef || !flip || !conv || (jpeg && !parse))
			goto fail;
		int sw, sh;
		image_target(mi, disp_w, disp_h, &sw, &sh);
		GstCaps *sc = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, sw,
						  "height", G_TYPE_INT, sh, NULL);
		g_object_set(sizef, "caps", sc, NULL);
		gst_caps_unref(sc);
		g_object_set(flip, "video-direction", 8 /* auto */, NULL);
		gst_bin_add_many(GST_BIN(pl), dec, scale, sizef, flip, conv, NULL);
		if (jpeg) {
			gst_bin_add(GST_BIN(pl), parse);
			if (!gst_element_link_many(src, parse, dec, NULL))
				goto fail;
		} else if (!gst_element_link(src, dec)) {
			goto fail;
		}
		if (!gst_element_link_many(dec, scale, sizef, flip, conv, sink, NULL))
			goto fail;
		caps = gst_caps_from_string("video/x-raw,format=BGRx");
		it->yuv = FALSE;
	} else {
		gboolean h265 = !strcmp(mi->codec, "h265");
		GstElement *demux = make_demux(mi->file);
		GstElement *parse = make(h265 ? "h265parse" : "h264parse");
		GstElement *dec = make(mi->decode == DECODE_HW ? "v4l2h264dec" :
				       h265 ? "avdec_h265" : "avdec_h264");
		if (!demux || !parse || !dec)
			goto fail;
		if (mi->decode == DECODE_HW)
			g_object_set(dec, "capture-io-mode", 4 /* dmabuf */, NULL);
		gst_bin_add_many(GST_BIN(pl), demux, parse, dec, NULL);
		if (!gst_element_link(src, demux) || !gst_element_link_many(parse, dec, sink, NULL))
			goto fail;
		g_signal_connect(demux, "pad-added", G_CALLBACK(on_demux_pad), parse);
		GstPad *dpad = gst_element_get_static_pad(dec, "sink");
		gst_pad_add_probe(dpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
				  mi->decode == DECODE_HW ? colorimetry_probe : colour_probe, it, NULL);
		gst_object_unref(dpad);
		caps = gst_caps_from_string(mi->decode == DECODE_HW ?
					    "video/x-raw,format=(string){I420,NV12,YV12,NV21}" :
					    "video/x-raw,format=I420");
		it->yuv = TRUE;
	}
	/* sync=false: the presenter paces by PTS. Small queue: a prerolled item
	 * stops after its first frame and never decodes ahead (the hardware
	 * decoder's throughput is shared with the item on screen). */
	g_object_set(sink, "sync", FALSE, "max-buffers", 2, "drop", FALSE,
		     "enable-last-sample", FALSE, "caps", caps, NULL);
	gst_caps_unref(caps);
	GstPad *spad = gst_element_get_static_pad(sink, "sink");
	gst_pad_add_probe(spad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, allocation_probe, NULL, NULL);
	gst_object_unref(spad);
	it->pipeline = pl;
	it->sink = GST_APP_SINK(sink);
	return TRUE;
fail:
	gst_object_unref(pl);
	return FALSE;
}

/* first error message on the item's bus, if any (non-blocking) */
static gboolean bus_error(struct item *it, char *buf, size_t len)
{
	GstBus *bus = gst_element_get_bus(it->pipeline);
	GstMessage *m = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
	gst_object_unref(bus);
	if (!m)
		return FALSE;
	GError *err = NULL;
	gst_message_parse_error(m, &err, NULL);
	g_snprintf(buf, len, "%s: %s", GST_OBJECT_NAME(m->src), err ? err->message : "?");
	g_clear_error(&err);
	gst_message_unref(m);
	return TRUE;
}

/*
 * Probe, build and preroll: on return the item is READY with its first frame
 * in it->first, or FAILED with a reason. Blocks up to PREPARE_TIMEOUT (runs
 * on the worker thread).
 */
void item_prepare(struct item *it, const char *file, int disp_w, int disp_h)
{
	it->enc = 1;
	it->range = 0;
	if (!probe_file(file, &it->mi)) {
		g_snprintf(it->why, sizeof(it->why), "%s", it->mi.reason[0] ? it->mi.reason : "unsupported");
		g_atomic_int_set(&it->state, ITEM_FAILED);
		return;
	}
	if (!build(it, disp_w, disp_h)) {
		g_snprintf(it->why, sizeof(it->why), "cannot build the pipeline");
		g_atomic_int_set(&it->state, ITEM_FAILED);
		return;
	}
	gst_element_set_state(it->pipeline, GST_STATE_PAUSED);
	it->first = gst_app_sink_try_pull_preroll(it->sink, PREPARE_TIMEOUT);
	char err[256];
	if (!it->first) {
		if (bus_error(it, err, sizeof(err)))
			g_snprintf(it->why, sizeof(it->why), "%s", err);
		else
			g_snprintf(it->why, sizeof(it->why), "no frame within %d s", (int)(PREPARE_TIMEOUT / GST_SECOND));
		g_atomic_int_set(&it->state, ITEM_FAILED);
		return;
	}
	GstBuffer *b = gst_sample_get_buffer(it->first);
	it->first_pts = GST_BUFFER_PTS_IS_VALID(b) ? (gint64)GST_BUFFER_PTS(b) : 0;
	g_atomic_int_set(&it->state, ITEM_READY);
}

/* start a prepared video item; frames then come from item_pull() */
void item_play(struct item *it)
{
	it->skip_first = TRUE;
	gst_element_set_state(it->pipeline, GST_STATE_PLAYING);
}

/*
 * Next frame of a playing video item, NULL at the end (it->eos) or on an
 * error (it->why). appsink hands the prerolled frame out once more after
 * PLAYING; that duplicate is dropped.
 */
GstSample *item_pull(struct item *it, GstClockTime timeout)
{
	for (;;) {
		GstSample *s = gst_app_sink_try_pull_sample(it->sink, timeout);
		if (!s) {
			char err[256];
			if (gst_app_sink_is_eos(it->sink))
				it->eos = TRUE;
			else if (bus_error(it, err, sizeof(err)))
				g_snprintf(it->why, sizeof(it->why), "%s", err);
			return NULL;
		}
		if (it->skip_first) {
			it->skip_first = FALSE;
			GstBuffer *b = gst_sample_get_buffer(s);
			if (GST_BUFFER_PTS_IS_VALID(b) && (gint64)GST_BUFFER_PTS(b) == it->first_pts) {
				gst_sample_unref(s);
				continue;
			}
		}
		return s;
	}
}

/* release everything (worker thread: setting a pipeline to NULL can block) */
void item_free(struct item *it)
{
	if (!it)
		return;
	if (it->first)
		gst_sample_unref(it->first);
	if (it->pipeline) {
		gst_element_set_state(it->pipeline, GST_STATE_NULL);
		gst_object_unref(it->pipeline);
	}
	for (int i = 0; i < ITEM_RING; i++)
		dumb_destroy(g_fd, it->ring[i]);
	g_free(it);
}
