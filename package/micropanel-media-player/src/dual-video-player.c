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
#include "player.h"

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
		"  --probe FILE...     classify files (parse only, no decoder, no display) and exit\n"
		"  --refresh=auto|keep|HZ  display refresh: auto = integer multiple of the\n"
		"                      video frame rate if offered (default), keep = as is\n",
		prog);
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

	/* --probe FILE...: classify, print one line per file, no display needed */
	if (argc > 1 && !strcmp(argv[1], "--probe")) {
		for (int i = 2; i < argc; i++) {
			struct media_info mi;
			probe_file(argv[i], &mi);
			probe_print(&mi);
		}
		return argc > 2 ? 0 : 2;
	}
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
