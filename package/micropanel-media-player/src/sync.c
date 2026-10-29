/* sync.c - frame-level playback sync of two players over the network
 *          (dual-video-player --playlist FILE --sync=master|client|auto)
 *
 * A shared clock, not a shared trigger. The master serves its monotonic
 * clock with a GstNetTimeProvider (UDP), the client slaves a
 * GstNetClientClock to it; both then know "now" on the master's clock (the
 * shared clock). The master picks the start of pass 0, T0, and every frame
 * of every item gets a due time on that clock:
 *
 *   due = T0 + pass * pass_ns + offset[item] + PTS (from the item's first frame)
 *
 * and each side shows it on its own display's first vblank at or after the
 * due time (mirror.c). Nothing accumulates: every frame, every loop pass is
 * anchored to T0, so the two displays' different refresh rates only decide
 * which vblank a frame lands on, never how far apart the players drift.
 *
 * Discovery: avahi-publish / avahi-browse (avahi-utils, in the image) announce
 * and find _micropanel-player._tcp; TXT: proto, role, state, host, item,
 * frames, fps, dhcp. --sync-peer=ADDR skips discovery.
 *
 * Control: one TCP connection per client to the master's port (default
 * 8096; the clock is UDP on the same port number), one JSON object per line
 * (see the README for the messages and the state machine). Main-loop thread
 * only; the presenter thread asks for pass starts and the clock under
 * s->lock.
 */
#include "player.h"
#include <arpa/inet.h>
#include <gio/gio.h>
#include <gst/net/net.h>
#include <ifaddrs.h>
#include <json-glib/json-glib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/prctl.h>

#define SERVICE "_micropanel-player._tcp"
#define PROTO 1
#define TICK_MS 20
#define CLOCK_TIMEOUT (5 * GST_SECOND)      /* net clock not synchronised by then: say so */
#define READY_TIMEOUT (10 * GST_SECOND)     /* master: clients not ready by then: start without */
#define START_LEAD (1500 * GST_MSECOND)     /* master: T0 = now + this when clients are ready */
#define ALONE_LEAD (100 * GST_MSECOND)
#define LATE_JOIN (1 * GST_SECOND)          /* a pass that began less than this ago is joined (frames dropped) */
#define AUTO_WINDOW (3 * GST_SECOND)        /* --sync=auto: look this long before choosing */
#define RECONNECT (2 * GST_SECOND)
#define PING_EVERY (1 * GST_SECOND)

enum role { ROLE_AUTO, ROLE_MASTER, ROLE_CLIENT };
static const char *role_name[] = { "auto", "master", "client" };

/* an item as the schedule sees it */
struct sync_item {
	enum media_kind kind;
	gint64 frames;            /* videos */
	int fps_n, fps_d;
	gint64 dur_ns;
	char name[128];
};

/* a player seen on the network */
struct peer {
	char *name, *iface, *addr, *role, *state, *host, *item;
	int port;
	gboolean dhcp;
};

/* a control connection: master: one per client; client: the one to the master */
struct conn {
	struct sync *s;
	GSocketConnection *c;
	GDataInputStream *in;
	GOutputStream *out;
	GCancellable *cancel;
	char *addr;               /* the peer's IPv4 address */
	char name[64];
	gboolean hello, ready, refused;
};

struct sync {
	struct player *p;
	enum role want, role;
	char name[32];            /* service name: dvp-<MAC tail> */
	char host[64];
	int port, wait_s;
	char *peer_arg;           /* --sync-peer: the master's address */
	char *iface;              /* --sync-iface: peers on this interface only (NULL: any) */
	gboolean dhcp;            /* our wired port serves DHCP (NetworkManager shared mode) */
	gint64 started;
	gboolean clock_test;      /* --sync-clock-test: no playlist, clock statistics only */

	/* this playlist, as a schedule */
	unsigned n;
	struct sync_item *items;
	gint64 *offset;           /* start of item i within a pass */
	gint64 pass_ns;
	gboolean loop;

	GMutex lock;              /* the presenter reads/writes these */
	gboolean have_schedule;
	gint64 t0, sched_pass_ns; /* shared clock */
	guint sched_epoch;        /* clock domain the schedule was received in */
	guint epoch;              /* clock domain: 0 = own monotonic clock, n = n-th net clock */
	guint domains;            /* net clocks synchronised so far */
	GstClock *clock;          /* client: the net clock (NULL: own clock) */
	gboolean presenter_ready; /* the first frame is on screen */
	gboolean free_run;        /* client: no master in time, plays on its own clock (keeps looking) */
	gint64 last_start;        /* the last pass start handed out, and its domain */
	guint last_epoch;
	gboolean have_last;

	/* main thread only */
	GstNetTimeProvider *provider;
	GSocketService *service;
	GList *conns;             /* master: clients */
	struct conn *mconn;       /* client: to the master */
	gboolean connecting;
	gint64 next_connect;
	char *clock_addr;
	int clock_port;
	gboolean clock_synced, clock_warned;
	gint64 clock_since;
	gboolean prepared, refused, ready_sent;
	gint64 prepare_sent;      /* master: when the first client got 'prepare' */
	GHashTable *peers;        /* name -> struct peer */
	GSubprocess *browse, *publish;
	GDataInputStream *browse_in;
	GCancellable *cancel;
	char *published;
	guint tick;
	gint64 next_ping;

	/* clock check (client): the net clock against a ping/pong over TCP */
	unsigned pings;
	gint64 rtt_min, rtt_max;
	gint64 err_max, err_sum;  /* |error|, over the samples with a round trip below 2x the minimum */
	unsigned err_n;
	gint64 last_report;
};

static void conn_close(struct conn *c);
static void publish(struct sync *s);

/* ---- helpers ---- */

static void sync_log(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void sync_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	gchar *msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	g_print("dual-video-player: sync: %s\n", msg);
	g_free(msg);
}

static void child_setup(gpointer data)
{
	(void)data;
	prctl(PR_SET_PDEATHSIG, SIGTERM);   /* no orphaned announcement if the player is killed */
}

static GSubprocess *spawn(GSubprocessFlags flags, const char *const *argv)
{
	fflush(stdout);   /* the child must not inherit (and repeat) buffered output */
	fflush(stderr);
	GSubprocessLauncher *l = g_subprocess_launcher_new(flags);
	g_subprocess_launcher_set_child_setup(l, child_setup, NULL, NULL);
	GError *err = NULL;
	GSubprocess *sp = g_subprocess_launcher_spawnv(l, argv, &err);
	g_object_unref(l);
	if (!sp) {
		sync_log("cannot run %s: %s", argv[0], err->message);
		g_clear_error(&err);
	}
	return sp;
}

/* keep TXT values free of spaces and quotes */
static void sanitize(char *dst, size_t len, const char *src)
{
	size_t i = 0;
	for (; *src && i + 1 < len; src++)
		dst[i++] = g_ascii_isalnum(*src) || strchr("._-+", *src) ? *src : '_';
	dst[i] = 0;
}

/* the master's view of the clock domain; the client's net clock once synchronised */
static void conv_now_locked(struct sync *s, struct sync_conv *c)
{
	if (s->epoch && s->clock) {
		gint64 a = mono_ns();
		gint64 g = (gint64)gst_clock_get_time(s->clock);
		gint64 b = mono_ns();
		c->local0 = a + (b - a) / 2;
		c->shared0 = g;
	} else {
		c->local0 = c->shared0 = 0;
	}
	c->epoch = s->epoch;
}

/* NetworkManager shared mode (the Network app's "DHCP server") on a wired port? */
static gboolean wired_serves_dhcp(void)
{
	gchar *out = NULL;
	gboolean serving = FALSE;
	if (!g_spawn_command_line_sync("nmcli -t -f NAME,TYPE connection show --active", &out, NULL, NULL, NULL))
		return FALSE;
	gchar **lines = g_strsplit(out, "\n", -1);
	for (gchar **l = lines; *l && !serving; l++) {
		gchar *colon = strrchr(*l, ':');
		if (!colon || strcmp(colon + 1, "802-3-ethernet"))
			continue;
		*colon = 0;
		gchar *argv[] = { "nmcli", "-g", "ipv4.method", "connection", "show", "id", *l, NULL };
		gchar *m = NULL;
		if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
				 NULL, NULL, &m, NULL, NULL, NULL))
			serving = m && g_str_has_prefix(m, "shared");
		g_free(m);
	}
	g_strfreev(lines);
	g_free(out);
	return serving;
}

/* our IPv4 address on @iface (host order), 0 if none */
static guint32 own_addr(const char *iface)
{
	struct ifaddrs *ifa, *i;
	guint32 a = 0;
	if (getifaddrs(&ifa))
		return 0;
	for (i = ifa; i && !a; i = i->ifa_next)
		if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && iface && !strcmp(i->ifa_name, iface))
			a = ntohl(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
	freeifaddrs(ifa);
	return a;
}

static guint32 parse_addr(const char *s)
{
	struct in_addr a;
	return s && inet_pton(AF_INET, s, &a) == 1 ? ntohl(a.s_addr) : 0;
}

/* ---- the plan: this playlist as a schedule ---- */

gboolean sync_plan(struct sync *s, const struct playlist *pl, gint64 image_ns)
{
	s->n = pl->items->len;
	s->loop = pl->loop;
	s->items = g_new0(struct sync_item, s->n);
	s->offset = g_new0(gint64, s->n);
	for (unsigned i = 0; i < s->n; i++) {
		struct media_info mi;
		struct sync_item *it = &s->items[i];
		const char *file = g_ptr_array_index(pl->items, i);
		gchar *base = g_path_get_basename(file);
		sanitize(it->name, sizeof(it->name), base);
		g_free(base);
		if (!probe_file(file, &mi)) {
			sync_log("off: item %u (%s) cannot be played: %s", i + 1, it->name, mi.reason);
			return FALSE;
		}
		it->kind = mi.kind;
		if (mi.kind == MEDIA_IMAGE) {
			it->dur_ns = image_ns;
		} else {
			if (mi.fps_n <= 0 || mi.fps_d <= 0 || mi.duration_ns <= 0) {
				sync_log("off: item %u (%s) has no fixed frame rate", i + 1, it->name);
				return FALSE;
			}
			it->fps_n = mi.fps_n;
			it->fps_d = mi.fps_d;
			it->frames = llround((double)mi.duration_ns * mi.fps_n / ((double)mi.fps_d * GST_SECOND));
			it->dur_ns = gst_util_uint64_scale((guint64)it->frames, (guint64)mi.fps_d * GST_SECOND,
							   (guint64)mi.fps_n);
		}
		s->offset[i] = s->pass_ns;
		s->pass_ns += it->dur_ns;
	}
	return s->n > 0;
}

gint64 sync_item_offset(struct sync *s, int index)
{
	return index >= 0 && (unsigned)index < s->n ? s->offset[index] : 0;
}

gint64 sync_item_duration(struct sync *s, int index)
{
	return index >= 0 && (unsigned)index < s->n ? s->items[index].dur_ns : 0;
}

/* ---- presenter side ---- */

void sync_presenter_ready(struct sync *s)
{
	g_mutex_lock(&s->lock);
	s->presenter_ready = TRUE;
	g_mutex_unlock(&s->lock);
}

/*
 * The start of the next pass on the shared clock: 1 with *start (a pass of
 * the master's schedule: the one about to begin, or one that began less than
 * LATE_JOIN ago), 0 not known yet (ask again), -1 none (free-running: start
 * now on the own clock). @c gets the clock domain of the pass.
 */
int sync_pass_start(struct sync *s, gint64 *start, struct sync_conv *c)
{
	int r;
	g_mutex_lock(&s->lock);
	s->presenter_ready = TRUE;
	conv_now_locked(s, c);
	gboolean usable = s->have_schedule && s->sched_epoch == s->epoch &&
			  (s->role == ROLE_MASTER || s->epoch > 0);
	if (usable) {
		gint64 now = conv_shared(c, mono_ns());
		gint64 earliest = now - LATE_JOIN;
		if (s->have_last && s->last_epoch == s->epoch)
			earliest = MAX(earliest, s->last_start + 1);
		gint64 n = 0;
		if (earliest > s->t0)
			n = (earliest - s->t0 + s->sched_pass_ns - 1) / s->sched_pass_ns;
		*start = s->t0 + n * s->sched_pass_ns;
		r = 1;
	} else if (s->role == ROLE_CLIENT && s->free_run) {
		r = -1;
	} else {
		r = 0;   /* master: its tick sets the schedule; client: waiting for one; auto: deciding */
	}
	g_mutex_unlock(&s->lock);
	return r;
}

/* the pass the presenter started (its own clock when free-running) */
void sync_pass_started(struct sync *s, gint64 start, const struct sync_conv *c)
{
	g_mutex_lock(&s->lock);
	s->last_start = start;
	s->last_epoch = c->epoch;
	s->have_last = TRUE;
	g_mutex_unlock(&s->lock);
}

/* refresh @c from the clock, as long as the clock domain is still the one @c was made in */
void sync_conv_update(struct sync *s, struct sync_conv *c)
{
	g_mutex_lock(&s->lock);
	if (c->epoch && c->epoch == s->epoch)
		conv_now_locked(s, c);
	g_mutex_unlock(&s->lock);
}

const char *sync_role(struct sync *s)
{
	return role_name[s->role];
}

/* clock check figures for the STATS line */
void sync_clock_stats(struct sync *s, GString *out)
{
	if (s->role != ROLE_CLIENT)
		return;
	g_string_append_printf(out, "\tclock_pings=%u", s->pings);
	if (s->err_n)
		g_string_append_printf(out, "\tclock_err_us_avg=%.0f\tclock_err_us_max=%.0f\trtt_us_min=%.0f",
				       s->err_sum / 1e3 / s->err_n, s->err_max / 1e3, s->rtt_min / 1e3);
}

/* ---- control messages ---- */

static void conn_send(struct conn *c, const char *line)
{
	if (!c || !c->out)
		return;
	gchar *l = g_strconcat(line, "\n", NULL);
	GError *err = NULL;
	if (!g_output_stream_write_all(c->out, l, strlen(l), NULL, NULL, &err)) {
		sync_log("send to %s failed: %s", c->addr, err->message);
		g_clear_error(&err);
	}
	g_free(l);
}

static void send_prepare(struct sync *s, struct conn *c)
{
	JsonBuilder *b = json_builder_new();
	json_builder_begin_object(b);
	json_builder_set_member_name(b, "msg");
	json_builder_add_string_value(b, "prepare");
	json_builder_set_member_name(b, "loop");
	json_builder_add_boolean_value(b, s->loop);
	json_builder_set_member_name(b, "pass_ns");
	json_builder_add_int_value(b, s->pass_ns);
	json_builder_set_member_name(b, "items");
	json_builder_begin_array(b);
	for (unsigned i = 0; i < s->n; i++) {
		const struct sync_item *it = &s->items[i];
		json_builder_begin_object(b);
		json_builder_set_member_name(b, "item");
		json_builder_add_string_value(b, it->name);
		json_builder_set_member_name(b, "kind");
		json_builder_add_string_value(b, it->kind == MEDIA_IMAGE ? "image" : "video");
		json_builder_set_member_name(b, "frames");
		json_builder_add_int_value(b, it->frames);
		json_builder_set_member_name(b, "fps");
		gchar *fps = g_strdup_printf("%d/%d", it->fps_n, it->fps_d);
		json_builder_add_string_value(b, fps);
		g_free(fps);
		json_builder_set_member_name(b, "duration_ns");
		json_builder_add_int_value(b, it->dur_ns);
		json_builder_end_object(b);
	}
	json_builder_end_array(b);
	json_builder_end_object(b);
	JsonGenerator *g = json_generator_new();
	JsonNode *root = json_builder_get_root(b);
	json_generator_set_root(g, root);
	gchar *line = json_generator_to_data(g, NULL);
	conn_send(c, line);
	g_free(line);
	json_node_unref(root);
	g_object_unref(g);
	g_object_unref(b);
	if (!s->prepare_sent)
		s->prepare_sent = mono_ns();
}

static void send_start(struct sync *s, struct conn *c)
{
	gchar *l = g_strdup_printf("{\"msg\":\"start\",\"t0\":%" G_GINT64_FORMAT ",\"pass_ns\":%" G_GINT64_FORMAT "}",
				   s->t0, s->sched_pass_ns);
	conn_send(c, l);
	g_free(l);
}

/* client: does the master's playlist match ours? NULL = yes, else why not */
static gchar *check_prepare(struct sync *s, JsonObject *o)
{
	JsonArray *items = json_object_has_member(o, "items") ? json_object_get_array_member(o, "items") : NULL;
	guint n = items ? json_array_get_length(items) : 0;
	if (n != s->n)
		return g_strdup_printf("the master plays %u item(s), this player %u", n, s->n);
	for (guint i = 0; i < n; i++) {
		JsonObject *m = json_array_get_object_element(items, i);
		const struct sync_item *it = &s->items[i];
		const char *kind = json_object_get_string_member_with_default(m, "kind", "");
		gint64 frames = json_object_get_int_member_with_default(m, "frames", -1);
		const char *fps = json_object_get_string_member_with_default(m, "fps", "");
		gint64 dur = json_object_get_int_member_with_default(m, "duration_ns", -1);
		const char *name = json_object_get_string_member_with_default(m, "item", "?");
		gchar *own_fps = g_strdup_printf("%d/%d", it->fps_n, it->fps_d);
		gchar *why = NULL;
		if (strcmp(kind, it->kind == MEDIA_IMAGE ? "image" : "video"))
			why = g_strdup_printf("item %u: the master's is a%s %s, ours a%s %s", i + 1,
					      *kind == 'i' ? "n" : "", kind, it->kind == MEDIA_IMAGE ? "n" : "",
					      it->kind == MEDIA_IMAGE ? "image" : "video");
		else if (it->kind != MEDIA_IMAGE && (frames != it->frames || strcmp(fps, own_fps)))
			why = g_strdup_printf("item %u: the master's %s has %" G_GINT64_FORMAT " frames at %s fps, "
					      "ours (%s) %" G_GINT64_FORMAT " at %s", i + 1, name, frames, fps,
					      it->name, it->frames, own_fps);
		else if (it->kind == MEDIA_IMAGE && dur != it->dur_ns)
			why = g_strdup_printf("item %u: the master shows images %.1f s, this player %.1f s", i + 1,
					      dur / 1e9, it->dur_ns / 1e9);
		g_free(own_fps);
		if (why)
			return why;
	}
	gboolean loop = json_object_get_boolean_member_with_default(o, "loop", FALSE);
	if (loop != s->loop)
		sync_log("note: the master's playlist %s, ours %s", loop ? "loops" : "does not loop",
			 s->loop ? "loops" : "does not");
	return NULL;
}

/* client: a net clock on the master's provider (a new one = a new clock domain) */
static void use_clock(struct sync *s, const char *addr, int port)
{
	if (s->clock && s->clock_addr && !strcmp(s->clock_addr, addr) && s->clock_port == port)
		return;
	g_free(s->clock_addr);
	s->clock_addr = g_strdup(addr);
	s->clock_port = port;
	GstClock *clk = gst_net_client_clock_new("dvp-sync", addr, port, 0);
	g_mutex_lock(&s->lock);
	if (s->clock)
		gst_object_unref(s->clock);
	s->clock = clk;
	s->epoch = 0;     /* own clock until the new one is synchronised */
	g_mutex_unlock(&s->lock);
	s->clock_synced = s->clock_warned = FALSE;
	s->clock_since = mono_ns();
	sync_log("clock: following %s:%d", addr, port);
}

static void on_message(struct conn *c, const char *line)
{
	struct sync *s = c->s;
	JsonParser *jp = json_parser_new();
	if (!json_parser_load_from_data(jp, line, -1, NULL) || !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(jp))) {
		sync_log("%s: not a message: %.80s", c->addr, line);
		g_object_unref(jp);
		return;
	}
	JsonObject *o = json_node_get_object(json_parser_get_root(jp));
	const char *msg = json_object_get_string_member_with_default(o, "msg", "");

	if (s->role == ROLE_MASTER) {
		if (!strcmp(msg, "hello")) {
			c->hello = TRUE;
			g_snprintf(c->name, sizeof(c->name), "%s",
				   json_object_get_string_member_with_default(o, "name", "?"));
			sync_log("client %s (%s) connected", c->name, c->addr);
			gchar *l = g_strdup_printf("{\"msg\":\"clock\",\"port\":%d,\"proto\":%d}", s->port, PROTO);
			conn_send(c, l);
			g_free(l);
			if (!s->clock_test)
				send_prepare(s, c);
			g_mutex_lock(&s->lock);
			gboolean sched = s->have_schedule;
			g_mutex_unlock(&s->lock);
			if (sched)
				send_start(s, c);
		} else if (!strcmp(msg, "ready")) {
			c->ready = TRUE;
			sync_log("client %s is ready", c->name);
		} else if (!strcmp(msg, "refuse")) {
			c->refused = TRUE;
			sync_log("client %s refuses: %s", c->name,
				 json_object_get_string_member_with_default(o, "reason", "?"));
		} else if (!strcmp(msg, "ping")) {
			gint64 t2 = mono_ns();
			gchar *l = g_strdup_printf("{\"msg\":\"pong\",\"t1\":%" G_GINT64_FORMAT ",\"t2\":%" G_GINT64_FORMAT "}",
						   json_object_get_int_member_with_default(o, "t1", 0), t2);
			conn_send(c, l);
			g_free(l);
		}
	} else {
		if (!strcmp(msg, "clock")) {
			use_clock(s, c->addr, (int)json_object_get_int_member_with_default(o, "port", s->port));
		} else if (!strcmp(msg, "prepare")) {
			gchar *why = check_prepare(s, o);
			s->prepared = TRUE;
			s->ready_sent = FALSE;
			s->refused = why != NULL;
			if (why) {
				sync_log("refusing the master's playlist: %s (playing free-running)", why);
				JsonBuilder *b = json_builder_new();
				json_builder_begin_object(b);
				json_builder_set_member_name(b, "msg");
				json_builder_add_string_value(b, "refuse");
				json_builder_set_member_name(b, "reason");
				json_builder_add_string_value(b, why);
				json_builder_end_object(b);
				JsonGenerator *g = json_generator_new();
				JsonNode *root = json_builder_get_root(b);
				json_generator_set_root(g, root);
				gchar *l = json_generator_to_data(g, NULL);
				conn_send(c, l);
				g_free(l);
				json_node_unref(root);
				g_object_unref(g);
				g_object_unref(b);
				g_mutex_lock(&s->lock);
				s->free_run = TRUE;
				g_mutex_unlock(&s->lock);
				g_free(why);
			}
		} else if (!strcmp(msg, "start")) {
			if (!s->refused) {
				g_mutex_lock(&s->lock);
				s->t0 = json_object_get_int_member_with_default(o, "t0", 0);
				s->sched_pass_ns = json_object_get_int_member_with_default(o, "pass_ns", s->pass_ns);
				s->have_schedule = s->sched_pass_ns > 0;
				s->sched_epoch = 0;   /* bound to the clock domain once the clock is synchronised */
				if (s->clock_synced)
					s->sched_epoch = s->epoch;
				g_mutex_unlock(&s->lock);
				sync_log("schedule: T0 %.3f s on the master's clock, pass %.3f s", s->t0 / 1e9,
					 s->sched_pass_ns / 1e9);
			}
		} else if (!strcmp(msg, "pong")) {
			gint64 t3 = mono_ns();
			gint64 t1 = json_object_get_int_member_with_default(o, "t1", 0);
			gint64 t2 = json_object_get_int_member_with_default(o, "t2", 0);
			struct sync_conv cv;
			g_mutex_lock(&s->lock);
			gboolean ok = s->epoch > 0;
			conv_now_locked(s, &cv);
			g_mutex_unlock(&s->lock);
			if (ok && t1) {
				gint64 rtt = t3 - t1;
				gint64 err = conv_shared(&cv, t1 + rtt / 2) - t2;   /* our clock - master's at the midpoint */
				s->pings++;
				if (!s->rtt_min || rtt < s->rtt_min)
					s->rtt_min = rtt;
				s->rtt_max = MAX(s->rtt_max, rtt);
				if (rtt <= 2 * s->rtt_min) {
					s->err_n++;
					s->err_sum += ABS(err);
					s->err_max = MAX(s->err_max, ABS(err));
				}
				if (s->clock_test)
					g_print("CLOCK\tt=%.1f\trtt_us=%.0f\terr_us=%.1f\n",
						(t3 - s->started) / 1e9, rtt / 1e3, err / 1e3);
			}
		}
	}
	g_object_unref(jp);
}

static void read_line(struct conn *c);

static void on_line(GObject *src, GAsyncResult *res, gpointer data)
{
	struct conn *c = data;
	GError *err = NULL;
	gsize len;
	gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(src), res, &len, &err);
	if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_clear_error(&err);
		return;   /* closed by us: c is gone */
	}
	if (!line) {
		sync_log("%s %s: %s", c->s->role == ROLE_MASTER ? "client" : "master", c->addr,
			 err ? err->message : "disconnected");
		g_clear_error(&err);
		conn_close(c);
		return;
	}
	on_message(c, line);
	g_free(line);
	read_line(c);
}

static void read_line(struct conn *c)
{
	g_data_input_stream_read_line_async(c->in, G_PRIORITY_DEFAULT, c->cancel, on_line, c);
}

static struct conn *conn_new(struct sync *s, GSocketConnection *sc)
{
	struct conn *c = g_new0(struct conn, 1);
	c->s = s;
	c->c = g_object_ref(sc);
	c->in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(sc)));
	c->out = g_io_stream_get_output_stream(G_IO_STREAM(sc));
	c->cancel = g_cancellable_new();
	GSocket *sock = g_socket_connection_get_socket(sc);
	g_socket_set_option(sock, IPPROTO_TCP, TCP_NODELAY, 1, NULL);
	GSocketAddress *ra = g_socket_connection_get_remote_address(sc, NULL);
	if (G_IS_INET_SOCKET_ADDRESS(ra))
		c->addr = g_inet_address_to_string(g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(ra)));
	else
		c->addr = g_strdup("?");
	if (ra)
		g_object_unref(ra);
	return c;
}

static void conn_close(struct conn *c)
{
	struct sync *s = c->s;
	g_cancellable_cancel(c->cancel);
	g_io_stream_close(G_IO_STREAM(c->c), NULL, NULL);
	if (s->mconn == c) {
		s->mconn = NULL;
		s->next_connect = mono_ns() + RECONNECT;
		g_mutex_lock(&s->lock);
		gboolean sched = s->have_schedule;
		g_mutex_unlock(&s->lock);
		if (sched)
			sync_log("lost the master: keeping its schedule on the last clock estimate; "
				 "a restarted master's schedule applies from the next pass");
	}
	s->conns = g_list_remove(s->conns, c);
	g_object_unref(c->in);
	g_object_unref(c->c);
	g_object_unref(c->cancel);
	g_free(c->addr);
	g_free(c);
}

/* master: a client connects */
static gboolean on_incoming(GSocketService *svc, GSocketConnection *sc, GObject *src, gpointer data)
{
	struct sync *s = data;
	(void)svc;
	(void)src;
	struct conn *c = conn_new(s, sc);
	s->conns = g_list_append(s->conns, c);
	read_line(c);
	return TRUE;
}

/* client: connected (or not) to the master */
static void on_connected(GObject *src, GAsyncResult *res, gpointer data)
{
	struct sync *s = data;
	GError *err = NULL;
	GSocketConnection *sc = g_socket_client_connect_finish(G_SOCKET_CLIENT(src), res, &err);
	g_object_unref(src);
	if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_clear_error(&err);
		return;
	}
	s->connecting = FALSE;
	if (!sc) {
		sync_log("cannot reach the master: %s", err->message);
		g_clear_error(&err);
		s->next_connect = mono_ns() + RECONNECT;
		return;
	}
	struct conn *c = conn_new(s, sc);
	g_object_unref(sc);
	s->mconn = c;
	s->prepared = s->refused = s->ready_sent = FALSE;
	sync_log("connected to the master %s", c->addr);
	gchar *l = g_strdup_printf("{\"msg\":\"hello\",\"name\":\"%s\",\"proto\":%d}", s->name, PROTO);
	conn_send(c, l);
	g_free(l);
	read_line(c);
}

static void connect_master(struct sync *s, const char *addr, int port)
{
	GSocketClient *cl = g_socket_client_new();
	g_socket_client_set_timeout(cl, 3);
	s->connecting = TRUE;
	sync_log("connecting to the master %s:%d", addr, port);
	g_socket_client_connect_to_host_async(cl, addr, (guint16)port, s->cancel, on_connected, s);
}

/* ---- discovery (avahi-utils) ---- */

static void peer_free(gpointer data)
{
	struct peer *p = data;
	g_free(p->name);
	g_free(p->iface);
	g_free(p->addr);
	g_free(p->role);
	g_free(p->state);
	g_free(p->host);
	g_free(p->item);
	g_free(p);
}

/* avahi-browse -p escapes as \DDD (decimal) */
static gchar *unescape(const char *s)
{
	GString *o = g_string_new(NULL);
	for (; *s; s++) {
		if (s[0] == '\\' && g_ascii_isdigit(s[1]) && g_ascii_isdigit(s[2]) && g_ascii_isdigit(s[3])) {
			g_string_append_c(o, (char)((s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0')));
			s += 3;
		} else {
			g_string_append_c(o, *s);
		}
	}
	return g_string_free(o, FALSE);
}

/* "=;eth0;IPv4;name;type;domain;host;address;port;"k=v" "k=v"" */
static void on_browse_line(struct sync *s, const char *line)
{
	gchar **f = g_strsplit(line, ";", 10);
	guint nf = g_strv_length(f);
	if (nf >= 4 && strcmp(f[2], "IPv4") == 0 && (!s->iface || !strcmp(f[1], s->iface))) {
		gchar *name = unescape(f[3]);
		struct peer *seen = g_hash_table_lookup(s->peers, name);
		if (!strcmp(name, s->name)) {
			g_free(name);
		} else if (seen && strcmp(seen->iface, f[1])) {
			g_free(name);   /* a peer seen on two networks: keep the first (or use --sync-iface) */
		} else if (f[0][0] == '-') {
			if (g_hash_table_remove(s->peers, name))
				sync_log("peer %s left", name);
			g_free(name);
		} else if (f[0][0] == '=' && nf >= 10) {
			struct peer *p = g_new0(struct peer, 1);
			p->name = name;
			p->iface = g_strdup(f[1]);
			p->host = unescape(f[6]);
			p->addr = g_strdup(f[7]);
			p->port = atoi(f[8]);
			gchar *txt = unescape(f[9]);
			gchar **kv = g_strsplit(txt, "\" \"", -1);
			for (gchar **k = kv; *k; k++) {
				gchar *e = g_strdup(*k);
				g_strdelimit(e, "\"", ' ');
				g_strstrip(e);
				gchar *eq = strchr(e, '=');
				if (eq) {
					*eq = 0;
					const char *v = eq + 1;
					if (!strcmp(e, "role"))
						p->role = g_strdup(v);
					else if (!strcmp(e, "state"))
						p->state = g_strdup(v);
					else if (!strcmp(e, "item"))
						p->item = g_strdup(v);
					else if (!strcmp(e, "dhcp"))
						p->dhcp = !strcmp(v, "1");
				}
				g_free(e);
			}
			g_strfreev(kv);
			g_free(txt);
			struct peer *old = g_hash_table_lookup(s->peers, p->name);
			if (!old || g_strcmp0(old->role, p->role) || g_strcmp0(old->addr, p->addr))
				sync_log("peer %s at %s:%d on %s (%s): role %s, state %s, item %s", p->name, p->addr, p->port,
					 p->iface, p->host, p->role ? p->role : "?", p->state ? p->state : "?",
					 p->item ? p->item : "?");
			g_hash_table_replace(s->peers, p->name, p);
		} else {
			g_free(name);
		}
	}
	g_strfreev(f);
}

static void browse_next(struct sync *s);

static void on_browse(GObject *src, GAsyncResult *res, gpointer data)
{
	struct sync *s = data;
	GError *err = NULL;
	gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(src), res, NULL, &err);
	if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_clear_error(&err);
		return;
	}
	if (!line) {
		sync_log("avahi-browse ended%s%s", err ? ": " : "", err ? err->message : "");
		g_clear_error(&err);
		return;
	}
	on_browse_line(s, line);
	g_free(line);
	browse_next(s);
}

static void browse_next(struct sync *s)
{
	g_data_input_stream_read_line_async(s->browse_in, G_PRIORITY_DEFAULT, s->cancel, on_browse, s);
}

static void browse(struct sync *s)
{
	const char *argv[] = { "avahi-browse", "-p", "-r", "-k", SERVICE, NULL };
	s->browse = spawn(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, argv);
	if (!s->browse)
		return;
	s->browse_in = g_data_input_stream_new(g_subprocess_get_stdout_pipe(s->browse));
	browse_next(s);
}

static const char *own_state(struct sync *s)
{
	g_mutex_lock(&s->lock);
	const char *st = s->have_schedule ? "playing" : s->presenter_ready ? "ready" : "preparing";
	if (s->role == ROLE_CLIENT && s->free_run && !s->have_schedule)
		st = "free";
	g_mutex_unlock(&s->lock);
	return st;
}

/* (re)announce ourselves when anything in the TXT record changed */
static void publish(struct sync *s)
{
	char item[64] = "-", fps[24] = "-", frames[24] = "-";
	if (s->n) {
		sanitize(item, sizeof(item), s->items[0].name);
		if (s->items[0].kind == MEDIA_VIDEO) {
			g_snprintf(fps, sizeof(fps), "%d/%d", s->items[0].fps_n, s->items[0].fps_d);
			g_snprintf(frames, sizeof(frames), "%" G_GINT64_FORMAT, s->items[0].frames);
		}
	}
	gchar *t[9] = {
		g_strdup_printf("proto=%d", PROTO), g_strdup_printf("role=%s", role_name[s->role]),
		g_strdup_printf("state=%s", own_state(s)), g_strdup_printf("host=%s", s->host),
		g_strdup_printf("item=%s", item), g_strdup_printf("frames=%s", frames),
		g_strdup_printf("fps=%s", fps), g_strdup_printf("dhcp=%d", s->dhcp), NULL
	};
	gchar *key = g_strjoinv(" ", t);
	if (!g_strcmp0(key, s->published)) {
		g_free(key);
		for (int i = 0; i < 8; i++)
			g_free(t[i]);
		return;
	}
	g_free(s->published);
	s->published = key;
	if (s->publish) {
		g_subprocess_send_signal(s->publish, SIGTERM);
		g_subprocess_wait(s->publish, NULL, NULL);
		g_clear_object(&s->publish);
	}
	gchar *port = g_strdup_printf("%d", s->port);
	const char *argv[] = { "avahi-publish", "-s", s->name, SERVICE, port,
			       t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], NULL };
	s->publish = spawn(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, argv);
	g_free(port);
	for (int i = 0; i < 8; i++)
		g_free(t[i]);
}

/* ---- roles ---- */

static void become_master(struct sync *s)
{
	GError *err = NULL;
	GstClock *sys = gst_system_clock_obtain();
	gint64 d = (gint64)gst_clock_get_time(sys) - mono_ns();
	if (ABS(d) > GST_MSECOND)
		sync_log("warning: the system clock is %.3f ms off CLOCK_MONOTONIC", d / 1e6);
	s->provider = gst_net_time_provider_new(sys, NULL, s->port);
	gst_object_unref(sys);
	if (!s->provider)
		sync_log("cannot serve the clock on UDP port %d", s->port);
	s->service = g_socket_service_new();
	if (!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(s->service), (guint16)s->port, NULL, &err)) {
		sync_log("cannot listen on TCP port %d: %s", s->port, err->message);
		g_clear_error(&err);
	}
	g_signal_connect(s->service, "incoming", G_CALLBACK(on_incoming), s);
	g_socket_service_start(s->service);
	s->role = ROLE_MASTER;
	sync_log("master: clock and control on port %d", s->port);
}

/* --sync=auto after AUTO_WINDOW: a master on the network -> client; else the
 * rig in DHCP-server mode, else the lowest address, is the master */
static void decide_role(struct sync *s)
{
	GHashTableIter it;
	gpointer v;
	struct peer *master = NULL, *best = NULL;
	gboolean dhcp_peer = FALSE;
	g_hash_table_iter_init(&it, s->peers);
	while (g_hash_table_iter_next(&it, NULL, &v)) {
		struct peer *p = v;
		if (!g_strcmp0(p->role, "master"))
			master = p;
		else if (!g_strcmp0(p->role, "auto")) {
			dhcp_peer |= p->dhcp;
			if (!best || parse_addr(p->addr) < parse_addr(best->addr))
				best = p;
		}
	}
	const char *why;
	if (master) {
		s->role = ROLE_CLIENT;
		why = "a master is announced";
	} else if (s->dhcp) {
		why = "this rig's wired port serves DHCP";
		s->role = ROLE_MASTER;
	} else if (dhcp_peer) {
		s->role = ROLE_CLIENT;
		why = "the peer's wired port serves DHCP";
	} else if (best && parse_addr(best->addr) < own_addr(best->iface)) {
		s->role = ROLE_CLIENT;
		why = "the peer has the lower address";
	} else {
		s->role = ROLE_MASTER;
		why = best ? "this rig has the lower address" : "no other player found";
	}
	sync_log("auto: %s (%s)", role_name[s->role], why);
	if (s->role == ROLE_MASTER)
		become_master(s);
}

/*
 * The master to connect to: a peer announced as master; else the auto peer
 * that won the auto choice (its DHCP server, else the lowest address). The
 * TXT record is read when a peer is first resolved, and a running
 * avahi-browse does not report later changes, so a peer that became master
 * after we saw it may still read "auto". Only a master listens on the
 * control port: a wrong guess is a refused connection and a retry.
 */
static const struct peer *find_master(struct sync *s)
{
	GHashTableIter it;
	gpointer v;
	const struct peer *best = NULL;
	g_hash_table_iter_init(&it, s->peers);
	while (g_hash_table_iter_next(&it, NULL, &v)) {
		const struct peer *p = v;
		if (!g_strcmp0(p->role, "master"))
			return p;
		if (g_strcmp0(p->role, "auto"))
			continue;
		if (!best || (p->dhcp && !best->dhcp) ||
		    (p->dhcp == best->dhcp && parse_addr(p->addr) < parse_addr(best->addr)))
			best = p;
	}
	return best;
}

static void master_tick(struct sync *s, gint64 now)
{
	g_mutex_lock(&s->lock);
	gboolean go = !s->have_schedule && s->presenter_ready;
	g_mutex_unlock(&s->lock);
	if (!go)
		return;
	int clients = 0, ready = 0;
	for (GList *l = s->conns; l; l = l->next) {
		struct conn *c = l->data;
		if (c->hello && !c->refused) {
			clients++;
			ready += c->ready;
		}
	}
	gint64 lead;
	if (clients && ready == clients) {
		lead = START_LEAD;
		sync_log("%d client(s) ready: starting in %.1f s", clients, lead / 1e9);
	} else if (clients && s->prepare_sent && now - s->prepare_sent > READY_TIMEOUT) {
		lead = START_LEAD;
		sync_log("%d of %d client(s) ready after %d s: starting; the others join at a pass start",
			 ready, clients, (int)(READY_TIMEOUT / GST_SECOND));
	} else if (!clients && now - s->started > (gint64)s->wait_s * GST_SECOND) {
		lead = ALONE_LEAD;
		sync_log("no client within %d s: starting alone; clients join at a pass start", s->wait_s);
	} else {
		return;
	}
	g_mutex_lock(&s->lock);
	s->t0 = mono_ns() + lead;
	s->sched_pass_ns = s->pass_ns;
	s->sched_epoch = 0;
	s->have_schedule = TRUE;
	g_mutex_unlock(&s->lock);
	for (GList *l = s->conns; l; l = l->next)
		if (((struct conn *)l->data)->hello)
			send_start(s, l->data);
}

static void client_tick(struct sync *s, gint64 now)
{
	if (!s->mconn && !s->connecting && now >= s->next_connect) {
		if (s->peer_arg) {
			connect_master(s, s->peer_arg, s->port);
		} else {
			const struct peer *m = find_master(s);
			if (m)
				connect_master(s, m->addr, m->port);
		}
	}
	if (s->clock && !s->clock_synced) {
		if (gst_clock_is_synced(s->clock)) {
			s->clock_synced = TRUE;
			g_mutex_lock(&s->lock);
			s->epoch = ++s->domains;
			if (s->have_schedule && !s->sched_epoch)
				s->sched_epoch = s->epoch;
			struct sync_conv cv;
			conv_now_locked(s, &cv);
			g_mutex_unlock(&s->lock);
			sync_log("clock: synchronised after %.2f s (master - own clock %+.3f ms)",
				 (now - s->clock_since) / 1e9, (cv.shared0 - cv.local0) / 1e6);
		} else if (!s->clock_warned && now - s->clock_since > CLOCK_TIMEOUT) {
			s->clock_warned = TRUE;
			sync_log("clock: not synchronised within %d s (UDP %d blocked?); free-running until it is",
				 (int)(CLOCK_TIMEOUT / GST_SECOND), s->clock_port);
		}
	}
	g_mutex_lock(&s->lock);
	gboolean pres = s->presenter_ready, sched = s->have_schedule, free_run = s->free_run;
	g_mutex_unlock(&s->lock);
	if (s->mconn && s->prepared && !s->refused && pres && s->clock_synced && !s->ready_sent) {
		conn_send(s->mconn, "{\"msg\":\"ready\"}");
		s->ready_sent = TRUE;
		sync_log("ready (first frame on screen, clock synchronised)");
	}
	if (!sched && !free_run && !s->clock_test && now - s->started > (gint64)s->wait_s * GST_SECOND) {
		g_mutex_lock(&s->lock);
		s->free_run = TRUE;
		g_mutex_unlock(&s->lock);
		sync_log("no master's schedule within %d s: playing free-running, still looking", s->wait_s);
	}
	if (s->mconn && s->clock_synced && now >= s->next_ping) {
		s->next_ping = now + PING_EVERY;
		gchar *l = g_strdup_printf("{\"msg\":\"ping\",\"t1\":%" G_GINT64_FORMAT "}", mono_ns());
		conn_send(s->mconn, l);
		g_free(l);
	}
	if (s->err_n && now - s->last_report > 60 * GST_SECOND) {
		s->last_report = now;
		sync_log("clock check: %u pings, error avg %.0f us max %.0f us (round trip min %.0f us max %.0f us)",
			 s->pings, s->err_sum / 1e3 / s->err_n, s->err_max / 1e3, s->rtt_min / 1e3, s->rtt_max / 1e3);
	}
}

static gboolean on_tick(gpointer data)
{
	struct sync *s = data;
	gint64 now = mono_ns();
	if (s->role == ROLE_AUTO && now - s->started > AUTO_WINDOW)
		decide_role(s);
	if (s->role == ROLE_MASTER)
		master_tick(s, now);
	else if (s->role == ROLE_CLIENT)
		client_tick(s, now);
	if (!s->peer_arg)
		publish(s);
	return G_SOURCE_CONTINUE;
}

/* ---- life cycle ---- */

struct sync *sync_new(struct player *p, const char *mode, const char *peer, const char *iface, int port,
		      int wait_s)
{
	enum role r;
	if (!strcmp(mode, "master"))
		r = ROLE_MASTER;
	else if (!strcmp(mode, "client"))
		r = ROLE_CLIENT;
	else if (!strcmp(mode, "auto"))
		r = ROLE_AUTO;
	else
		return NULL;
	setvbuf(stdout, NULL, _IOLBF, 0);   /* sync logs in step with the other rig's */
	struct sync *s = g_new0(struct sync, 1);
	s->p = p;
	s->want = r;
	s->role = r == ROLE_MASTER ? ROLE_AUTO : r;   /* the master starts serving in sync_start() */
	s->port = port > 0 ? port : 8096;
	s->wait_s = wait_s > 0 ? wait_s : 10;
	s->peer_arg = peer ? g_strdup(peer) : NULL;
	s->iface = iface ? g_strdup(iface) : NULL;
	g_mutex_init(&s->lock);
	gethostname(s->host, sizeof(s->host) - 1);
	gchar *mac = NULL;
	if (!g_file_get_contents("/sys/class/net/eth0/address", &mac, NULL, NULL))
		mac = g_strdup(s->host);
	gchar **b = g_strsplit(g_strstrip(mac), ":", -1);
	guint nb = g_strv_length(b);
	g_snprintf(s->name, sizeof(s->name), "dvp-%s%s%s", nb >= 3 ? b[nb - 3] : mac, nb >= 3 ? b[nb - 2] : "",
		   nb >= 3 ? b[nb - 1] : "");
	g_strfreev(b);
	g_free(mac);
	s->peers = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, peer_free);
	s->cancel = g_cancellable_new();
	return s;
}

void sync_start(struct sync *s)
{
	s->started = mono_ns();
	s->dhcp = wired_serves_dhcp();
	sync_log("%s as %s (%s%s%s%s%s), %u item(s), pass %.3f s", s->want == ROLE_AUTO ? "auto" : role_name[s->want],
		 s->name, s->host, s->dhcp ? ", wired port serves DHCP" : "",
		 s->peer_arg ? ", peer given" : "", s->iface ? ", peers on " : "", s->iface ? s->iface : "",
		 s->n, s->pass_ns / 1e9);
	if (s->want == ROLE_MASTER)
		become_master(s);
	if (!s->peer_arg) {
		browse(s);
		publish(s);
	}
	s->tick = g_timeout_add(TICK_MS, on_tick, s);
}

void sync_free(struct sync *s)
{
	if (!s)
		return;
	if (s->tick)
		g_source_remove(s->tick);
	g_cancellable_cancel(s->cancel);
	while (s->conns)
		conn_close(s->conns->data);
	if (s->mconn)
		conn_close(s->mconn);
	if (s->service) {
		g_socket_service_stop(s->service);
		g_socket_listener_close(G_SOCKET_LISTENER(s->service));
		g_object_unref(s->service);
	}
	if (s->provider)
		gst_object_unref(s->provider);
	GSubprocess *sp[2] = { s->publish, s->browse };
	for (int i = 0; i < 2; i++)
		if (sp[i]) {
			g_subprocess_send_signal(sp[i], SIGTERM);
			g_subprocess_wait(sp[i], NULL, NULL);
			g_object_unref(sp[i]);
		}
	if (s->browse_in)
		g_object_unref(s->browse_in);
	if (s->clock)
		gst_object_unref(s->clock);
	g_hash_table_destroy(s->peers);
	g_object_unref(s->cancel);
	g_free(s->published);
	g_free(s->clock_addr);
	g_free(s->peer_arg);
	g_free(s->iface);
	g_free(s->items);
	g_free(s->offset);
	g_mutex_clear(&s->lock);
	g_free(s);
}

/* --sync-clock-test=SECONDS: the clock only (no display), one CLOCK line per ping, a summary */
static gboolean test_done(gpointer data)
{
	g_main_loop_quit(data);
	return G_SOURCE_REMOVE;
}

int sync_clock_test(struct player *p, const char *mode, const char *peer, const char *iface, int port,
		    int seconds)
{
	struct sync *s = sync_new(p, mode, peer, iface, port, 3600);
	if (!s)
		return 2;
	s->clock_test = TRUE;
	p->loop = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGINT, on_signal, p);
	g_unix_signal_add(SIGTERM, on_signal, p);
	sync_start(s);
	g_timeout_add_seconds((guint)seconds, test_done, p->loop);
	g_main_loop_run(p->loop);
	if (s->role == ROLE_CLIENT)
		g_print("CLOCKSTATS\tpings=%u\tused=%u\terr_us_avg=%.1f\terr_us_max=%.1f\trtt_us_min=%.0f\trtt_us_max=%.0f\n",
			s->pings, s->err_n, s->err_n ? s->err_sum / 1e3 / s->err_n : 0, s->err_max / 1e3,
			s->rtt_min / 1e3, s->rtt_max / 1e3);
	sync_free(s);
	g_main_loop_unref(p->loop);
	return 0;
}
