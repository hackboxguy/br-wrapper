/* probe.c - classify media files without decoding them (dual-video-player --probe)
 *
 * Only parsebin (demuxer + parser) runs: it gives codec, profile, size,
 * frame rate, pixel aspect ratio, colorimetry and the orientation tag. A
 * decoder is never opened - probing many files through decodebin or
 * GstDiscoverer wedged the VideoCore codec firmware on the bench.
 *
 * The classification is the one the player plays by and the USB Media app
 * shows: hw (V4L2 H.264 decoder, zero copy), sw (gst-libav), image, or
 * unsupported with a reason.
 */
#include "player.h"
#include <sys/stat.h>

#define PROBE_TIMEOUT    (3 * GST_SECOND)
#define HW_MAX_W         1920
#define HW_MAX_H         1088
#define SW_MAX_W         2560
#define SW_MAX_H         1600
#define SW_SLOW_MBPS     4.0           /* HEVC above this may play slowly */
#define IMAGE_MAX_PIXELS 50000000LL    /* 50 MP: ~75 MB decoded */

const char *decode_path_name(enum decode_path d)
{
	switch (d) {
	case DECODE_HW: return "hw";
	case DECODE_SW: return "sw";
	case DECODE_IMAGE: return "image";
	default: return "unsupported";
	}
}

struct probe_ctx {
	GstElement *pipeline;
	struct media_info *mi;
};

static int orientation_degrees(const char *tag, gboolean *flip);

static void take_orientation(struct media_info *mi, const GstTagList *tags)
{
	gchar *o = NULL;
	if (gst_tag_list_get_string(tags, GST_TAG_IMAGE_ORIENTATION, &o)) {
		mi->rotation = orientation_degrees(o, &mi->flip);
		g_free(o);
	}
}

/* parsers such as jpegparse send the EXIF orientation as a tag event only */
static GstPadProbeReturn tag_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	GstEvent *ev = GST_PAD_PROBE_INFO_EVENT(info);
	(void)pad;
	if (GST_EVENT_TYPE(ev) == GST_EVENT_TAG) {
		GstTagList *tags = NULL;
		gst_event_parse_tag(ev, &tags);
		take_orientation(data, tags);
	}
	return GST_PAD_PROBE_OK;
}

/*
 * The video/image stream's first buffer: its caps, tags and the container's
 * duration are known now. Ends the wait without waiting for the whole
 * pipeline's ASYNC_DONE, which a file with an audio track reached only by the
 * 3 s timeout (every camera/phone clip).
 */
static GstPadProbeReturn first_buffer_probe(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
	(void)info;
	(void)data;
	GstCaps *caps = gst_pad_get_current_caps(pad);
	if (caps) {
		const char *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
		if (g_str_has_prefix(name, "video/") || g_str_has_prefix(name, "image/")) {
			GstObject *sink = gst_pad_get_parent(pad);
			if (sink) {
				gst_element_post_message(GST_ELEMENT(sink),
					gst_message_new_application(sink, gst_structure_new_empty("probe-stream-ready")));
				gst_object_unref(sink);
			}
		}
		gst_caps_unref(caps);
	}
	return GST_PAD_PROBE_REMOVE;
}

static void on_pad(GstElement *parsebin, GstPad *pad, gpointer data)
{
	struct probe_ctx *ctx = data;
	GstElement *pipeline = ctx->pipeline;
	(void)parsebin;
	/* every stream gets a sink, so no pad is left unlinked (not-linked would stop the preroll) */
	GstElement *sink = gst_element_factory_make("fakesink", NULL);
	g_object_set(sink, "sync", FALSE, "async", TRUE, NULL);
	gst_bin_add(GST_BIN(pipeline), sink);
	gst_element_sync_state_with_parent(sink);
	GstPad *sinkpad = gst_element_get_static_pad(sink, "sink");
	gst_pad_add_probe(sinkpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, tag_probe, ctx->mi, NULL);
	gst_pad_add_probe(sinkpad, GST_PAD_PROBE_TYPE_BUFFER, first_buffer_probe, NULL, NULL);
	gst_pad_link(pad, sinkpad);
	gst_object_unref(sinkpad);
}

/* the first video/image stream's caps among the pipeline's fakesinks */
static GstCaps *stream_caps(GstElement *pipeline)
{
	GstCaps *found = NULL;
	GstIterator *it = gst_bin_iterate_sinks(GST_BIN(pipeline));
	GValue v = G_VALUE_INIT;
	while (!found && gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
		GstElement *sink = g_value_get_object(&v);
		GstPad *pad = gst_element_get_static_pad(sink, "sink");
		GstCaps *caps = pad ? gst_pad_get_current_caps(pad) : NULL;
		if (caps) {
			const char *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
			if (g_str_has_prefix(name, "video/") || g_str_has_prefix(name, "image/"))
				found = caps;
			else
				gst_caps_unref(caps);
		}
		if (pad)
			gst_object_unref(pad);
		g_value_reset(&v);
	}
	g_value_unset(&v);
	gst_iterator_free(it);
	return found;
}

static int orientation_degrees(const char *tag, gboolean *flip)
{
	*flip = g_str_has_prefix(tag, "flip-");
	if (strstr(tag, "90"))
		return 90;
	if (strstr(tag, "180"))
		return 180;
	if (strstr(tag, "270"))
		return 270;
	return 0;
}

static void classify(struct media_info *mi, const GstStructure *st, const char *media)
{
	const char *profile = gst_structure_get_string(st, "profile");
	const char *chroma = gst_structure_get_string(st, "chroma-format");
	guint depth = 8;
	gst_structure_get_uint(st, "bit-depth-luma", &depth);
	if (profile)
		g_strlcpy(mi->profile, profile, sizeof(mi->profile));
	const gboolean is420 = !chroma || !strcmp(chroma, "4:2:0");
	const gboolean hd = mi->width <= HW_MAX_W && mi->height <= HW_MAX_H;
	const gboolean swsize = mi->width <= SW_MAX_W && mi->height <= SW_MAX_H;

	if (!strcmp(media, "video/x-h264")) {
		g_strlcpy(mi->codec, "h264", sizeof(mi->codec));
		mi->kind = MEDIA_VIDEO;
		if (depth > 8 || !is420) {
			g_snprintf(mi->reason, sizeof(mi->reason), "H.264 %u-bit %s not supported", depth,
				   chroma ? chroma : "");
		} else if (hd) {
			mi->decode = DECODE_HW;
		} else if (swsize) {
			mi->decode = DECODE_SW;
			mi->slow = TRUE;
		} else {
			g_snprintf(mi->reason, sizeof(mi->reason), "%dx%d too large (max %dx%d)",
				   mi->width, mi->height, SW_MAX_W, SW_MAX_H);
		}
	} else if (!strcmp(media, "video/x-h265")) {
		g_strlcpy(mi->codec, "h265", sizeof(mi->codec));
		mi->kind = MEDIA_VIDEO;
		if (depth > 8 || (profile && strstr(profile, "10")))
			g_strlcpy(mi->reason, "HEVC 10-bit (Main10) not supported", sizeof(mi->reason));
		else if (!is420)
			g_snprintf(mi->reason, sizeof(mi->reason), "HEVC %s not supported", chroma);
		else if (!hd)
			g_snprintf(mi->reason, sizeof(mi->reason), "HEVC above 1920x1080 is too slow (%dx%d)",
				   mi->width, mi->height);
		else {
			mi->decode = DECODE_SW;
			mi->slow = mi->bitrate_mbps > SW_SLOW_MBPS;
		}
	} else if (!strcmp(media, "image/jpeg") || !strcmp(media, "image/png")) {
		g_strlcpy(mi->codec, media + 6, sizeof(mi->codec));
		mi->kind = MEDIA_IMAGE;
		if (mi->width <= 0 || mi->height <= 0)
			g_strlcpy(mi->reason, "image size unknown", sizeof(mi->reason));
		else if ((gint64)mi->width * mi->height > IMAGE_MAX_PIXELS)
			g_snprintf(mi->reason, sizeof(mi->reason), "image larger than 50 MP (%dx%d)",
				   mi->width, mi->height);
		else
			mi->decode = DECODE_IMAGE;
	} else {
		mi->kind = g_str_has_prefix(media, "image/") ? MEDIA_IMAGE : MEDIA_VIDEO;
		g_strlcpy(mi->codec, strchr(media, '/') ? strchr(media, '/') + 1 : media, sizeof(mi->codec));
		g_snprintf(mi->reason, sizeof(mi->reason), "%s not supported", media);
	}
}

/*
 * EXIF orientation and colour space of a JPEG. jpegparse has rank "none", so
 * parsebin never plugs it and the orientation tag stays inside the file; run
 * it explicitly. A CMYK/YCCK JPEG (print workflows) is refused here: jpegdec
 * cannot decode it.
 */
static void jpeg_details(const char *path, struct media_info *mi)
{
	GstElement *pipeline = gst_pipeline_new(NULL);
	GstElement *src = gst_element_factory_make("filesrc", NULL);
	GstElement *parse = gst_element_factory_make("jpegparse", NULL);
	GstElement *sink = gst_element_factory_make("fakesink", NULL);
	if (!src || !parse || !sink) {
		gst_object_unref(pipeline);
		return;
	}
	g_object_set(src, "location", path, NULL);
	gst_bin_add_many(GST_BIN(pipeline), src, parse, sink, NULL);
	gst_element_link_many(src, parse, sink, NULL);
	GstPad *pad = gst_element_get_static_pad(sink, "sink");
	gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, tag_probe, mi, NULL);
	gst_element_set_state(pipeline, GST_STATE_PAUSED);
	gst_element_get_state(pipeline, NULL, NULL, PROBE_TIMEOUT);   /* preroll: tags precede the buffer */
	GstCaps *caps = gst_pad_get_current_caps(pad);
	if (caps) {
		const char *cs = gst_structure_get_string(gst_caps_get_structure(caps, 0), "colorspace");
		if (cs && (strstr(cs, "CMYK") || strstr(cs, "YCCK"))) {
			mi->decode = DECODE_UNSUPPORTED;
			g_strlcpy(mi->reason, "CMYK JPEG not supported (save as RGB)", sizeof(mi->reason));
		}
		gst_caps_unref(caps);
	}
	gst_object_unref(pad);
	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(pipeline);
}

gboolean probe_file(const char *path, struct media_info *mi)
{
	memset(mi, 0, sizeof(*mi));
	g_strlcpy(mi->file, path, sizeof(mi->file));
	mi->decode = DECODE_UNSUPPORTED;
	mi->par_n = mi->par_d = 1;
	mi->fps_d = 1;
	g_strlcpy(mi->colorimetry, "-", sizeof(mi->colorimetry));

	struct stat sb;
	if (stat(path, &sb) || !S_ISREG(sb.st_mode)) {
		g_strlcpy(mi->reason, "file not found", sizeof(mi->reason));
		return FALSE;
	}
	mi->size = sb.st_size;

	GstElement *pipeline = gst_pipeline_new(NULL);
	GstElement *src = gst_element_factory_make("filesrc", NULL);
	GstElement *parse = gst_element_factory_make("parsebin", NULL);
	if (!src || !parse) {
		g_strlcpy(mi->reason, "GStreamer parsebin missing", sizeof(mi->reason));
		gst_object_unref(pipeline);
		return FALSE;
	}
	g_object_set(src, "location", path, NULL);
	gst_bin_add_many(GST_BIN(pipeline), src, parse, NULL);
	gst_element_link(src, parse);
	struct probe_ctx ctx = { pipeline, mi };
	g_signal_connect(parse, "pad-added", G_CALLBACK(on_pad), &ctx);

	gst_element_set_state(pipeline, GST_STATE_PAUSED);
	GstBus *bus = gst_element_get_bus(pipeline);
	gboolean done = FALSE, failed = FALSE;
	gint64 deadline = mono_ns() + PROBE_TIMEOUT;
	while (!done) {
		gint64 left = deadline - mono_ns();
		GstMessage *m = left > 0 ? gst_bus_timed_pop(bus, (GstClockTime)left) : NULL;
		if (!m) {
			failed = TRUE;
			break;
		}
		switch (GST_MESSAGE_TYPE(m)) {
		case GST_MESSAGE_ASYNC_DONE:
			done = TRUE;
			break;
		case GST_MESSAGE_APPLICATION:
			if (gst_message_has_name(m, "probe-stream-ready"))
				done = TRUE;
			break;
		case GST_MESSAGE_ERROR:
			done = failed = TRUE;
			break;
		case GST_MESSAGE_TAG: {
			GstTagList *tags = NULL;
			gst_message_parse_tag(m, &tags);
			take_orientation(mi, tags);
			gst_tag_list_unref(tags);
			break;
		}
		default:
			break;
		}
		gst_message_unref(m);
	}
	gst_object_unref(bus);

	GstCaps *caps = stream_caps(pipeline);
	gint64 dur = 0;
	if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur) && dur > 0)
		mi->duration_ns = dur;
	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(pipeline);

	if (!caps) {
		g_strlcpy(mi->reason, failed ? "unknown or damaged file" : "no video or image stream",
			  sizeof(mi->reason));
		return FALSE;
	}
	const GstStructure *st = gst_caps_get_structure(caps, 0);
	gst_structure_get_int(st, "width", &mi->width);
	gst_structure_get_int(st, "height", &mi->height);
	gst_structure_get_fraction(st, "framerate", &mi->fps_n, &mi->fps_d);
	gst_structure_get_fraction(st, "pixel-aspect-ratio", &mi->par_n, &mi->par_d);
	const char *cm = gst_structure_get_string(st, "colorimetry");
	if (cm)
		g_strlcpy(mi->colorimetry, cm, sizeof(mi->colorimetry));
	if (mi->duration_ns > 0)
		mi->bitrate_mbps = mi->size * 8.0 / ((double)mi->duration_ns / GST_SECOND) / 1e6;
	classify(mi, st, gst_structure_get_name(st));
	if (!strcmp(gst_structure_get_name(st), "image/jpeg") && mi->decode == DECODE_IMAGE)
		jpeg_details(path, mi);
	gst_caps_unref(caps);
	return mi->decode != DECODE_UNSUPPORTED;
}

void probe_print(const struct media_info *mi)
{
	/* tab-separated key=value, the file last (names may contain anything but tabs) */
	g_print("PROBE\tkind=%s\tdecode=%s\tcodec=%s\tprofile=%s\tsize=%dx%d\tfps=%d/%d\tpar=%d/%d\t"
		"duration=%.3f\tbitrate=%.2f\trotation=%d\tflip=%d\tcolorimetry=%s\tslow=%d\treason=%s\tfile=%s\n",
		mi->kind == MEDIA_IMAGE ? "image" : mi->kind == MEDIA_VIDEO ? "video" : "unknown",
		decode_path_name(mi->decode), mi->codec[0] ? mi->codec : "-",
		mi->profile[0] ? mi->profile : "-", mi->width, mi->height, mi->fps_n, mi->fps_d,
		mi->par_n, mi->par_d, mi->duration_ns / 1e9, mi->bitrate_mbps, mi->rotation, mi->flip,
		mi->colorimetry, mi->slow, mi->reason[0] ? mi->reason : "-", mi->file);
}
